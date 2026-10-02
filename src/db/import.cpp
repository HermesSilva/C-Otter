#include "db/import.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace otter::db {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view without_bom(std::string_view text) noexcept {
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    return text;
}

} // namespace

char detect_delimiter(std::string_view text) noexcept {
    text = without_bom(text);

    constexpr std::array<char, 4> candidates = {',', ';', '\t', '|'};
    std::array<std::size_t, 4> counts{};

    // So' a primeira linha, e fora de aspas: uma virgula dentro de
    // "Silva, Ana" nao e' separador.
    bool quoted = false;
    for (const char c : text) {
        if (c == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == '\n' || c == '\r') break;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (c == candidates[i]) ++counts[i];
        }
    }

    std::size_t best = 0;
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        if (counts[i] > counts[best]) best = i;
    }
    return candidates[best];
}

CsvTable parse_csv(std::string_view text, const CsvOptions& options,
                   std::size_t max_rows) {
    CsvTable table;
    text = without_bom(text);

    std::vector<std::optional<std::string>> record;
    std::string field;
    bool        quoted = false;        // dentro de aspas
    bool        was_quoted = false;    // o campo corrente teve aspas
    bool        field_started = false;
    std::size_t line = 1;              // linha do ARQUIVO, para a mensagem
    std::size_t record_line = 1;
    bool        header_done = !options.header;

    const auto end_field = [&] {
        // NULL so' sem aspas: `""` e' string vazia, `` e' NULL.
        if (!was_quoted && field == options.null_text) record.emplace_back(std::nullopt);
        else                                           record.emplace_back(std::move(field));
        field.clear();
        was_quoted    = false;
        field_started = false;
    };

    // Devolve falso quando e' para parar (erro ou limite).
    const auto end_record = [&]() -> bool {
        end_field();

        // Linha em branco (um campo so', vazio e sem aspas): ignorada, como
        // fazem as planilhas -- o arquivo costuma terminar com uma.
        if (record.size() == 1 && !record.front().has_value()) {
            record.clear();
            return true;
        }

        if (!header_done) {
            for (auto& name : record) table.header.push_back(name.value_or(std::string{}));
            header_done = true;
            record.clear();
            return true;
        }

        if (table.header.empty()) {
            for (std::size_t c = 0; c < record.size(); ++c) {
                table.header.push_back("column" + std::to_string(c + 1));
            }
        }
        if (record.size() != table.header.size()) {
            table.error = "line " + std::to_string(record_line) + " has " +
                          std::to_string(record.size()) + " field(s), expected " +
                          std::to_string(table.header.size());
            return false;
        }
        if (table.rows.size() >= max_rows) {
            table.truncated = true;
            return false;
        }
        table.rows.push_back(std::move(record));
        record.clear();
        return true;
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];

        if (quoted) {
            if (c == options.quote) {
                // Aspas dobradas dentro de aspas: uma aspa literal.
                if (i + 1 < text.size() && text[i + 1] == options.quote) {
                    field.push_back(options.quote);
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                if (c == '\n') ++line;
                field.push_back(c);
            }
            continue;
        }

        if (c == options.quote && !field_started) {
            quoted        = true;
            was_quoted    = true;
            field_started = true;
        } else if (c == options.delimiter) {
            end_field();
        } else if (c == '\r') {
            // CRLF: o '\n' seguinte fecha o registro. Um '\r' solto (Mac
            // antigo) fecha tambem.
            if (i + 1 < text.size() && text[i + 1] == '\n') continue;
            if (!end_record()) return table;
            ++line;
            record_line = line;
        } else if (c == '\n') {
            if (!end_record()) return table;
            ++line;
            record_line = line;
        } else {
            field.push_back(c);
            field_started = true;
        }
    }

    if (quoted) {
        table.error = "line " + std::to_string(record_line) +
                      ": a quoted field is never closed";
        return table;
    }

    // Ultima linha sem quebra no fim.
    if (field_started || was_quoted || !record.empty()) (void)end_record();
    return table;
}

std::vector<std::string> match_columns(const std::vector<std::string>& file_columns,
                                       const std::vector<std::string>& table_columns) {
    std::vector<std::string> out(file_columns.size());
    for (std::size_t i = 0; i < file_columns.size(); ++i) {
        const std::string wanted = lower(file_columns[i]);
        for (const std::string& column : table_columns) {
            if (lower(column) == wanted) {
                out[i] = column;
                break;
            }
        }
    }
    return out;
}

ImportScript generate_import(const CsvTable& data, const ImportPlan& plan) {
    ImportScript script;

    if (plan.table.empty()) {
        script.error = "the target table is required";
        return script;
    }
    if (!data.error.empty()) {
        script.error = data.error;
        return script;
    }

    // As colunas do arquivo que tem destino, na ordem do arquivo.
    std::vector<std::size_t> used;
    std::string              column_list;
    for (std::size_t c = 0; c < plan.columns.size() && c < data.header.size(); ++c) {
        if (plan.columns[c].empty()) continue;

        // Duas colunas do arquivo para a mesma coluna da tabela: o servidor
        // recusaria com "column specified more than once" no meio da carga.
        for (const std::size_t other : used) {
            if (plan.columns[other] == plan.columns[c]) {
                script.error = "two file columns are mapped to " + plan.columns[c];
                return script;
            }
        }
        if (!used.empty()) column_list += ", ";
        column_list += quote_if_needed(plan.columns[c]);
        used.push_back(c);
    }
    if (used.empty()) {
        script.error = "map at least one file column to a table column";
        return script;
    }

    const std::string target = qualified_name(plan.schema, plan.table);
    if (plan.truncate_first) script.statements.push_back("TRUNCATE TABLE " + target);

    const std::string prefix = "INSERT INTO " + target + " (" + column_list + ") VALUES";
    // O SQL Server recusa um VALUES com mais de 1000 linhas.
    const std::size_t limit =
        sql_dialect() == QuoteStyle::brackets ? std::size_t{1000} : plan.batch_rows;
    const std::size_t batch = std::clamp<std::size_t>(plan.batch_rows, 1, std::max<std::size_t>(limit, 1));

    std::string statement;
    std::size_t in_batch = 0;
    for (const auto& row : data.rows) {
        statement += in_batch == 0 ? prefix + "\n    (" : ",\n    (";
        for (std::size_t i = 0; i < used.size(); ++i) {
            if (i > 0) statement += ", ";
            const std::optional<std::string>& value = row[used[i]];
            statement += value.has_value() ? quote_literal(*value) : std::string("NULL");
        }
        statement += ")";

        ++script.rows;
        if (++in_batch == batch) {
            script.statements.push_back(std::move(statement));
            statement.clear();
            in_batch = 0;
        }
    }
    if (in_batch > 0) script.statements.push_back(std::move(statement));
    return script;
}

} // namespace otter::db
