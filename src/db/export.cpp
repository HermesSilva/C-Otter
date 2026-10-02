#include "db/export.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

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

// --- Escritores em tres tempos ----------------------------------------------------
//
// Cabecalho, linhas, rodape'. Um resultado de dois milhoes de linhas nao cabe
// na memoria: a exportacao le' o servidor em pedacos e cada pedaco passa por
// `rows`. O que precisa de estado entre pedacos (quantas linhas ja' sairam,
// as larguras de coluna) mora no ExportState.

struct ExportState {
    std::size_t              rows = 0;
    std::vector<std::size_t> widths;    // Markdown e TXT: do PRIMEIRO pedaco
    std::vector<std::string> names;     // XML: nomes de coluna ja' saneados
};

void csv_begin(std::string& out, const ResultSet& rs, const ExportOptions& options) {
    if (!options.write_header) return;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (c > 0) out.push_back(options.delimiter);
        write_csv_value(out, rs.column(c).info().name, options);
    }
    out += "\r\n";   // RFC 4180 pede CRLF
}

void csv_rows(std::string& out, const ResultSet& rs, const ExportOptions& options) {
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
}

void json_rows(std::string& out, const ResultSet& rs, ExportState& state) {
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        // A virgula vai ANTES de cada linha menos a primeira: so' assim a
        // ultima do ultimo pedaco fica sem virgula sem saber que e' a ultima.
        out += state.rows + r == 0 ? "  {" : ",\n  {";
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
        out += "}";
    }
}

// '|' dentro do valor quebraria a tabela.
void markdown_cell(std::string& out, std::string_view text, std::size_t width) {
    for (const char c : text) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += "<br>";
        else out.push_back(c);
    }
    for (std::size_t i = text.size(); i < width; ++i) out.push_back(' ');
}

// Larguras por coluna: uma tabela desalinhada e' valida e ilegivel no
// texto-fonte, que e' onde ela costuma ser editada. Medidas no primeiro
// pedaco -- as linhas dos seguintes podem passar da largura, sem quebrar nada.
//
// `characters`: mede em caracteres, nao em bytes -- "ação" tem 4 letras e 6
// bytes, e a borda de uma tabela de largura fixa medida em bytes sai torta.
std::size_t text_width(std::string_view text, bool characters) {
    if (!characters) return text.size();
    std::size_t length = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++length;
    }
    return length;
}

void measure(const ResultSet& rs, ExportState& state, std::size_t null_width,
             bool characters = false) {
    state.widths.assign(rs.column_count(), 0);
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        state.widths[c] = text_width(rs.column(c).info().name, characters);
    }
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            const std::size_t length =
                rs.is_null(r, c) ? null_width : text_width(rs.text(r, c), characters);
            state.widths[c] = std::max(state.widths[c], length);
        }
    }
}

void markdown_begin(std::string& out, const ResultSet& rs, ExportState& state) {
    measure(rs, state, 6);

    out += "|";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        out += " ";
        markdown_cell(out, rs.column(c).info().name, state.widths[c]);
        out += " |";
    }
    out += "\n|";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        // Numero alinha a' direita, que e' como se le' numero.
        const bool right = is_numeric_literal(rs.column(c).info().kind);
        out += " ";
        out.append(state.widths[c], '-');
        out += right ? ": |" : " |";
    }
    out += "\n";
}

void markdown_rows(std::string& out, const ResultSet& rs, const ExportState& state) {
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "|";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            out += " ";
            markdown_cell(out, rs.is_null(r, c) ? "*null*" : rs.text(r, c),
                          c < state.widths.size() ? state.widths[c] : 0);
            out += " |";
        }
        out += "\n";
    }
}

void sql_rows(std::string& out, const ResultSet& rs, const ExportOptions& options) {
    if (rs.column_count() == 0) return;

    std::string columns;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (c > 0) columns += ", ";
        columns += rs.column(c).info().name;
    }

    const std::string prefix =
        "INSERT INTO " + options.table_name + " (" + columns + ") VALUES";

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

    // Um comando por PEDACO: um INSERT unico de dois milhoes de tuplas
    // estouraria o limite de tamanho de comando de qualquer servidor.
    if (!options.one_statement_per_row && rs.row_count() > 0) out += ";\n";
}

// & < > " -- o minimo para HTML e XML. O apostrofo fica: so' importa dentro de
// atributo com aspas simples, que nao e' escrito aqui.
void write_markup(std::string& out, std::string_view value) {
    for (const char c : value) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            default:
                // Caractere de controle e' invalido em XML 1.0 (menos tab e
                // quebras): um arquivo com ele nao abre em leitor nenhum.
                if (static_cast<unsigned char>(c) < 0x20 && c != '\t' && c != '\n' &&
                    c != '\r') {
                    out.push_back(' ');
                } else {
                    out.push_back(c);
                }
        }
    }
}

void html_begin(std::string& out, const ResultSet& rs) {
    out += "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
           "<style>\n"
           "table { border-collapse: collapse; font-family: sans-serif; }\n"
           "th, td { border: 1px solid #999; padding: 2px 6px; }\n"
           "th { background: #ddd; }\n"
           "td.n { text-align: right; }\n"
           "td.null { color: #999; }\n"
           "</style>\n</head>\n<body>\n<table>\n<tr>";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        out += "<th>";
        write_markup(out, rs.column(c).info().name);
        out += "</th>";
    }
    out += "</tr>\n";
}

void html_rows(std::string& out, const ResultSet& rs) {
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "<tr>";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (rs.is_null(r, c)) {
                out += "<td class=\"null\">NULL</td>";
                continue;
            }
            out += is_numeric_literal(rs.column(c).info().kind) ? "<td class=\"n\">"
                                                                 : "<td>";
            write_markup(out, rs.text(r, c));
            out += "</td>";
        }
        out += "</tr>\n";
    }
}

// Nome de elemento XML valido: letra ou '_' primeiro, depois letra, digito,
// '_', '-' ou '.'. "valor total" e "1a_coluna" nao sao, e um XML com elemento
// invalido nao e' XML.
std::string xml_name(std::string_view name) {
    std::string out;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        const bool letter = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                            c == '_' || u >= 0x80;
        const bool rest   = (u >= '0' && u <= '9') || c == '-' || c == '.';
        if (letter || (rest && !out.empty())) out.push_back(c);
        else                                 out.push_back('_');
    }
    if (out.empty()) out = "_";
    return out;
}

void xml_begin(std::string& out, const ResultSet& rs, ExportState& state) {
    state.names.clear();
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        state.names.push_back(xml_name(rs.column(c).info().name));
    }
    // A forma do exportador XML do DBeaver: um DATA_RECORD por linha, um
    // elemento por coluna.
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<data>\n";
}

void xml_rows(std::string& out, const ResultSet& rs, const ExportState& state) {
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "  <DATA_RECORD>\n";
        for (std::size_t c = 0; c < rs.column_count() && c < state.names.size(); ++c) {
            // NULL: o elemento vazio com o atributo que o distingue de ''.
            if (rs.is_null(r, c)) {
                out += "    <" + state.names[c] + " null=\"true\"/>\n";
                continue;
            }
            out += "    <" + state.names[c] + ">";
            write_markup(out, rs.text(r, c));
            out += "</" + state.names[c] + ">\n";
        }
        out += "  </DATA_RECORD>\n";
    }
}

// Texto de largura fixa: o que se cola num e-mail ou num chamado.
void txt_line(std::string& out, const ExportState& state) {
    out += "+";
    for (const std::size_t width : state.widths) {
        out.append(width + 2, '-');
        out += "+";
    }
    out += "\n";
}

void txt_cell(std::string& out, std::string_view text, std::size_t width, bool right) {
    std::string flat;
    for (const char c : text) flat.push_back(c == '\n' || c == '\r' || c == '\t' ? ' ' : c);

    const std::size_t length = text_width(flat, /*characters=*/true);
    const std::size_t pad = length < width ? width - length : 0;

    out += " ";
    if (right) out.append(pad, ' ');
    out += flat;
    if (!right) out.append(pad, ' ');
    out += " |";
}

void txt_begin(std::string& out, const ResultSet& rs, ExportState& state) {
    measure(rs, state, 6, /*characters=*/true);
    txt_line(out, state);
    out += "|";
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        txt_cell(out, rs.column(c).info().name, state.widths[c], false);
    }
    out += "\n";
    txt_line(out, state);
}

void txt_rows(std::string& out, const ResultSet& rs, const ExportState& state) {
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        out += "|";
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            txt_cell(out, rs.is_null(r, c) ? "[NULL]" : rs.text(r, c),
                     c < state.widths.size() ? state.widths[c] : 0,
                     is_numeric_literal(rs.column(c).info().kind));
        }
        out += "\n";
    }
}

void emit_begin(std::string& out, const ResultSet& rs, const ExportOptions& options,
           ExportState& state) {
    switch (options.format) {
        case ExportFormat::csv:        csv_begin(out, rs, options); break;
        case ExportFormat::json:       out += "[\n"; break;
        case ExportFormat::markdown:   markdown_begin(out, rs, state); break;
        case ExportFormat::sql_insert: break;
        case ExportFormat::html:       html_begin(out, rs); break;
        case ExportFormat::xml:        xml_begin(out, rs, state); break;
        case ExportFormat::txt:        txt_begin(out, rs, state); break;
    }
}

void emit_rows(std::string& out, const ResultSet& rs, const ExportOptions& options,
          ExportState& state) {
    switch (options.format) {
        case ExportFormat::csv:        csv_rows(out, rs, options); break;
        case ExportFormat::json:       json_rows(out, rs, state); break;
        case ExportFormat::markdown:   markdown_rows(out, rs, state); break;
        case ExportFormat::sql_insert: sql_rows(out, rs, options); break;
        case ExportFormat::html:       html_rows(out, rs); break;
        case ExportFormat::xml:        xml_rows(out, rs, state); break;
        case ExportFormat::txt:        txt_rows(out, rs, state); break;
    }
    state.rows += rs.row_count();
}

void emit_end(std::string& out, const ExportOptions& options, const ExportState& state) {
    switch (options.format) {
        case ExportFormat::json:
            out += state.rows > 0 ? "\n]\n" : "]\n";
            break;
        case ExportFormat::html:
            out += "</table>\n</body>\n</html>\n";
            break;
        case ExportFormat::xml:
            out += "</data>\n";
            break;
        case ExportFormat::txt:
            txt_line(out, state);
            break;
        default:
            break;
    }
}

} // namespace

std::string_view to_string(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::csv:        return "CSV";
        case ExportFormat::json:       return "JSON";
        case ExportFormat::markdown:   return "Markdown";
        case ExportFormat::sql_insert: return "SQL INSERT";
        case ExportFormat::html:       return "HTML";
        case ExportFormat::xml:        return "XML";
        case ExportFormat::txt:        return "TXT";
    }
    return "unknown";
}

std::string_view file_extension(ExportFormat format) noexcept {
    switch (format) {
        case ExportFormat::csv:        return ".csv";
        case ExportFormat::json:       return ".json";
        case ExportFormat::markdown:   return ".md";
        case ExportFormat::sql_insert: return ".sql";
        case ExportFormat::html:       return ".html";
        case ExportFormat::xml:        return ".xml";
        case ExportFormat::txt:        return ".txt";
    }
    return ".txt";
}

std::string export_to_string(const ResultSet& rs,
                             const ExportOptions& options) {
    std::string out;
    out.reserve(rs.row_count() * rs.column_count() * 16);

    ExportState state;
    emit_begin(out, rs, options, state);
    emit_rows(out, rs, options, state);
    emit_end(out, options, state);
    return out;
}

Status export_to_file(const ResultSet& rs, const ExportOptions& options,
                      std::string_view path) {
    ExportStream stream(options);
    OTTER_RETURN_IF_ERROR(stream.open(path));
    OTTER_RETURN_IF_ERROR(stream.write(rs));
    return stream.finish();
}

// --- ExportStream ------------------------------------------------------------------

struct ExportStream::Impl {
    ExportOptions options;
    ExportState   state;
    std::ofstream file;
    std::string   path;
    bool          started = false;
};

ExportStream::ExportStream(ExportOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->options = std::move(options);
}

ExportStream::~ExportStream() = default;

Status ExportStream::open(std::string_view path) {
    const std::filesystem::path target(path);

    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }

    impl_->path = target.string();
    impl_->file.open(target, std::ios::binary | std::ios::trunc);
    if (!impl_->file) {
        return fail(Errc::io_error, "cannot write " + impl_->path);
    }
    return {};
}

Status ExportStream::write(const ResultSet& chunk) {
    std::string out;
    out.reserve(chunk.row_count() * chunk.column_count() * 16);

    // O cabecalho sai com o PRIMEIRO pedaco: e' dele que vem as colunas.
    if (!impl_->started) {
        emit_begin(out, chunk, impl_->options, impl_->state);
        impl_->started = true;
    }
    emit_rows(out, chunk, impl_->options, impl_->state);

    impl_->file.write(out.data(), static_cast<std::streamsize>(out.size()));
    if (!impl_->file) {
        // Disco cheio no meio da exportacao: dizer, e nao seguir escrevendo
        // num arquivo que ja' esta' truncado.
        return fail(Errc::io_error, "write failed: " + impl_->path);
    }
    return {};
}

Status ExportStream::finish() {
    std::string out;
    if (impl_->started) emit_end(out, impl_->options, impl_->state);
    impl_->file.write(out.data(), static_cast<std::streamsize>(out.size()));
    impl_->file.flush();
    if (!impl_->file) {
        return fail(Errc::io_error, "write failed: " + impl_->path);
    }
    impl_->file.close();
    return {};
}

std::size_t ExportStream::rows() const noexcept { return impl_->state.rows; }

} // namespace otter::db
