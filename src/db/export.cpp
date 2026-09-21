#include "db/export.hpp"

#include <filesystem>
#include <fstream>

namespace otter::db {
namespace {

// Um valor que comeca por estes caracteres vira formula ao abrir numa
// planilha -- e formula executa. Os equivalentes de largura total (FF1D e
// seguintes) entram porque Excel e LibreOffice tambem os interpretam.
//
// Ver https://owasp.org/www-community/attacks/CSV_Injection
bool starts_a_formula(std::string_view value) noexcept {
    if (value.empty()) return false;

    switch (value.front()) {
        case '=': case '+': case '-': case '@':
        case '\t': case '\r': case '\n':
            return true;
        default:
            break;
    }

    // Formas de largura total, em UTF-8: ＝ ＋ － ＠
    static constexpr std::string_view kWide[] = {
        "\xEF\xBC\x9D", "\xEF\xBC\x8B", "\xEF\xBC\x8D", "\xEF\xBC\xA0",
    };
    for (const std::string_view prefix : kWide) {
        if (value.starts_with(prefix)) return true;
    }
    return false;
}

// Tipos que dispensam aspas num INSERT: numeros e booleanos. O resto vai
// entre aspas simples, inclusive data e uuid -- o PostgreSQL converte.
bool is_numeric_literal(DataKind kind) noexcept {
    switch (kind) {
        case DataKind::boolean:
        case DataKind::integer:
        case DataKind::floating:
        case DataKind::numeric:
            return true;
        default:
            return false;
    }
}

void write_csv_value(std::string& out, std::string_view value,
                     const ExportOptions& options) {
    std::string text(value);

    if (options.escape_formulas && starts_a_formula(text)) {
        // Apostrofo na frente: a planilha mostra o valor original e nao o
        // avalia. Alterar o conteudo seria pior, mas silenciar a formula sem
        // avisar tambem -- e' por isso que a opcao existe e e' desligavel.
        text.insert(text.begin(), '\'');
    }

    // Cita quando o valor contem o delimitador, aspas ou quebra de linha.
    // Citar tudo seria mais simples e produziria arquivos maiores e menos
    // legiveis.
    const bool needs_quotes =
        text.find(options.delimiter) != std::string::npos ||
        text.find('"') != std::string::npos ||
        text.find('\n') != std::string::npos ||
        text.find('\r') != std::string::npos;

    if (!needs_quotes) {
        out += text;
        return;
    }

    out.push_back('"');
    for (const char c : text) {
        if (c == '"') out.push_back('"');   // aspas dobram, RFC 4180
        out.push_back(c);
    }
    out.push_back('"');
}

void write_json_string(std::string& out, std::string_view value) {
    out.push_back('"');
    for (const char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x",
                                  static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void write_sql_literal(std::string& out, std::string_view value) {
    out.push_back('\'');
    for (const char c : value) {
        if (c == '\'') out.push_back('\'');   // aspas simples dobram
        out.push_back(c);
    }
    out.push_back('\'');
}

std::string to_csv(const ResultSet& rs, const ExportOptions& options) {
    std::string out;
    out.reserve(rs.row_count() * rs.column_count() * 16);

    if (options.write_header) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (c > 0) out.push_back(options.delimiter);
            write_csv_value(out, rs.column(c).info().name, options);
        }
        out += "\r\n";   // RFC 4180 pede CRLF
    }

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (c > 0) out.push_back(options.delimiter);
            if (rs.is_null(r, c)) {
                out += options.null_text;
            } else {
                write_csv_value(out, rs.text(r, c), options);
            }
        }
        out += "\r\n";
    }
    return out;
}

std::string to_json(const ResultSet& rs) {
    std::string out = "[\n";

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "  {";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (c > 0) out += ", ";
            write_json_string(out, rs.column(c).info().name);
            out += ": ";

            if (rs.is_null(r, c)) {
                // null de verdade, nao a string "null": e' a diferenca entre
                // um JSON que reimporta e um que mente.
                out += "null";
                continue;
            }

            // Numeros e booleanos sem aspas; o resto como string. Um numero
            // citado vira string ao ler de volta.
            const DataKind kind = rs.column(c).info().kind;
            const std::string_view value = rs.text(r, c);

            if (kind == DataKind::boolean) {
                out += (value == "t" || value == "true") ? "true" : "false";
            } else if (is_numeric_literal(kind) && !value.empty()) {
                out += value;
            } else {
                write_json_string(out, value);
            }
        }
        out += r + 1 < rs.row_count() ? "},\n" : "}\n";
    }

    out += "]\n";
    return out;
}

std::string to_markdown(const ResultSet& rs) {
    // Larguras por coluna: uma tabela Markdown desalinhada e' valida e
    // ilegivel no texto-fonte, que e' onde ela costuma ser editada.
    std::vector<std::size_t> widths(rs.column_count());
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        widths[c] = rs.column(c).info().name.size();
    }
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            const std::size_t length =
                rs.is_null(r, c) ? 6 : rs.text(r, c).size();
            widths[c] = std::max(widths[c], length);
        }
    }

    const auto write_cell = [&](std::string& out, std::string_view text,
                                std::size_t width) {
        // '|' dentro do valor quebraria a tabela.
        for (const char c : text) {
            if (c == '|') out += "\\|";
            else if (c == '\n') out += "<br>";
            else out.push_back(c);
        }
        for (std::size_t i = text.size(); i < width; ++i) out.push_back(' ');
    };

    std::string out = "|";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        out += " ";
        write_cell(out, rs.column(c).info().name, widths[c]);
        out += " |";
    }
    out += "\n|";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        // Numero alinha a' direita, que e' como se le' numero.
        const bool right = is_numeric_literal(rs.column(c).info().kind);
        out += right ? " " : " ";
        out.append(widths[c], '-');
        out += right ? ": |" : " |";
    }
    out += "\n";

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "|";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            out += " ";
            write_cell(out, rs.is_null(r, c) ? "*null*" : rs.text(r, c),
                       widths[c]);
            out += " |";
        }
        out += "\n";
    }
    return out;
}

std::string to_sql_insert(const ResultSet& rs, const ExportOptions& options) {
    if (rs.column_count() == 0) return {};

    std::string columns;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (c > 0) columns += ", ";
        columns += rs.column(c).info().name;
    }

    const std::string prefix =
        "INSERT INTO " + options.table_name + " (" + columns + ") VALUES";

    std::string out;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        if (options.one_statement_per_row) {
            out += prefix + "\n    (";
        } else {
            out += r == 0 ? prefix + "\n    (" : ",\n    (";
        }

        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (c > 0) out += ", ";

            if (rs.is_null(r, c)) {
                out += "NULL";
                continue;
            }

            const DataKind kind = rs.column(c).info().kind;
            const std::string_view value = rs.text(r, c);

            if (kind == DataKind::boolean) {
                out += (value == "t" || value == "true") ? "TRUE" : "FALSE";
            } else if (is_numeric_literal(kind) && !value.empty()) {
                out += value;
            } else {
                write_sql_literal(out, value);
            }
        }

        out += options.one_statement_per_row ? ");\n" : ")";
    }

    if (!options.one_statement_per_row && rs.row_count() > 0) out += ";\n";
    return out;
}

} // namespace

std::string_view to_string(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::csv:        return "CSV";
        case ExportFormat::json:       return "JSON";
        case ExportFormat::markdown:   return "Markdown";
        case ExportFormat::sql_insert: return "SQL INSERT";
    }
    return "unknown";
}

std::string_view file_extension(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::csv:        return ".csv";
        case ExportFormat::json:       return ".json";
        case ExportFormat::markdown:   return ".md";
        case ExportFormat::sql_insert: return ".sql";
    }
    return ".txt";
}

std::string export_to_string(const ResultSet& rs,
                             const ExportOptions& options) {
    switch (options.format) {
        case ExportFormat::csv:        return to_csv(rs, options);
        case ExportFormat::json:       return to_json(rs);
        case ExportFormat::markdown:   return to_markdown(rs);
        case ExportFormat::sql_insert: return to_sql_insert(rs, options);
    }
    return {};
}

Status export_to_file(const ResultSet& rs, const ExportOptions& options,
                      std::string_view path) {
    const std::filesystem::path target(path);

    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }

    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    if (!file) {
        return fail(Errc::io_error, "cannot write " + target.string());
    }

    const std::string content = export_to_string(rs, options);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));

    if (!file) {
        return fail(Errc::io_error, "write failed: " + target.string());
    }
    return {};
}

} // namespace otter::db
