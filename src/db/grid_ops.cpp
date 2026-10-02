#include "db/grid_ops.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <set>

namespace otter::db {
namespace {

// "a, b" -> {"a", "b"}. As chaves compostas chegam assim do catalogo.
std::vector<std::string> split_list(std::string_view list) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        std::string_view piece = list.substr(
            start, comma == std::string_view::npos ? std::string_view::npos
                                                   : comma - start);
        while (!piece.empty() && (piece.front() == ' ' || piece.front() == '"')) {
            piece.remove_prefix(1);
        }
        while (!piece.empty() && (piece.back() == ' ' || piece.back() == '"')) {
            piece.remove_suffix(1);
        }
        if (!piece.empty()) out.emplace_back(piece);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

// Numero inteiro ou decimal, sem lixo no fim. strtod sozinho aceitaria "12abc".
bool parse_number(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string copy(text);
    char* end = nullptr;
    out = std::strtod(copy.c_str(), &end);
    return end != nullptr && *end == '\0' && end != copy.c_str();
}

// Sem zeros a' direita: 12.500000 -> 12.5, 3.000000 -> 3.
std::string format_number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.6f", value);
    std::string text(buffer);
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    return text;
}

// "chave = valor AND ..." da linha. Falha quando a chave nao identifica.
Result<std::string> key_predicate(const ResultSet& rs, const EditTarget& target,
                                  std::size_t row) {
    if (target.key_columns.empty()) {
        return fail(Errc::not_supported, std::string(to_string(target.refusal)));
    }

    std::string where;
    for (std::size_t i = 0; i < target.key_columns.size(); ++i) {
        const std::size_t column = target.key_columns[i];
        const ColumnInfo& info = rs.column(column).info();

        // O valor da chave e' o do BANCO, nao o do buffer: e' ele que
        // identifica a linha que existe la'.
        if (rs.is_null(row, column)) {
            return fail(Errc::invalid_argument,
                        "the key column '" + info.name + "' is NULL in this row; "
                        "it cannot be identified");
        }
        if (i > 0) where += "\n   AND ";
        where += quote_if_needed(info.name) + " = " +
                 sql_literal(info, rs.text(row, column), false);
    }
    return where;
}

} // namespace

CellValue cell_value(const ResultSet& rs, const EditBuffer& edits,
                     std::size_t row, std::size_t column) {
    CellValue value;
    if (row >= rs.row_count() || column >= rs.column_count()) {
        value.is_null = true;
        return value;
    }

    if (const CellEdit* pending = edits.find(row, column); pending != nullptr) {
        // "Set to default" ainda nao tem valor: quem decide e' o servidor.
        value.is_null = pending->is_null || pending->is_default;
        if (!value.is_null) value.text = pending->value;
        return value;
    }

    value.is_null = rs.is_null(row, column);
    if (!value.is_null) value.text = std::string(rs.text(row, column));
    return value;
}

ResultSet slice(const ResultSet& rs, const EditBuffer& edits,
                const GridSelection& selection) {
    ResultSetBuilder builder;
    for (const std::size_t column : selection.columns) {
        if (column < rs.column_count()) builder.add_column(rs.column(column).info());
    }

    std::size_t rows = 0;
    for (std::size_t row = selection.row_first;
         row <= selection.row_last && row < rs.row_count(); ++row) {
        std::size_t out = 0;
        for (const std::size_t column : selection.columns) {
            if (column >= rs.column_count()) continue;
            const CellValue value = cell_value(rs, edits, row, column);
            if (value.is_null) builder.append_null(out);
            else               builder.append_text(out, value.text);
            ++out;
        }
        ++rows;
    }
    builder.set_row_count(rows);
    return builder.take();
}

std::string selection_to_text(const ResultSet& rs, const EditBuffer& edits,
                              const GridSelection& selection, bool with_header) {
    std::string out;

    if (with_header) {
        bool first = true;
        for (const std::size_t column : selection.columns) {
            if (column >= rs.column_count()) continue;
            if (!first) out += '\t';
            first = false;
            out += rs.column(column).info().name;
        }
        out += '\n';
    }

    for (std::size_t row = selection.row_first;
         row <= selection.row_last && row < rs.row_count(); ++row) {
        if (row > selection.row_first) out += '\n';

        bool first = true;
        for (const std::size_t column : selection.columns) {
            if (column >= rs.column_count()) continue;
            if (!first) out += '\t';
            first = false;

            const CellValue value = cell_value(rs, edits, row, column);
            if (value.is_null) continue;

            // Tab e quebra de linha DENTRO do valor desalinhariam a colagem
            // numa planilha; viram espaco, como o DBeaver faz.
            for (const char c : value.text) {
                out += (c == '\t' || c == '\n' || c == '\r') && !selection.single_cell()
                           ? ' ' : c;
            }
        }
    }
    return out;
}

std::vector<std::vector<std::string>> parse_clipboard(std::string_view text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> current;
    std::string cell;

    const auto end_row = [&] {
        current.push_back(std::move(cell));
        cell.clear();
        rows.push_back(std::move(current));
        current.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') continue;
        if (c == '\t') {
            current.push_back(std::move(cell));
            cell.clear();
        } else if (c == '\n') {
            end_row();
        } else {
            cell += c;
        }
    }

    // O que sobrou depois do ultimo '\n'. Vazio = so' o terminador da
    // planilha, que nao e' uma linha.
    if (!cell.empty() || !current.empty()) end_row();
    return rows;
}

Result<std::string> row_script(const ResultSet& rs, const EditBuffer& edits,
                               const EditTarget& target,
                               const GridSelection& selection, RowScript kind) {
    if (target.table.empty()) {
        return fail(Errc::not_supported, std::string(to_string(target.refusal)));
    }
    const std::string table = qualified_name(target.schema, target.table);

    // As colunas que vem DA tabela: uma expressao (`now()`, um agregado) nao
    // tem para onde ser gravada.
    std::vector<std::size_t> columns;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (rs.column(c).info().has_source()) columns.push_back(c);
    }
    if (columns.empty()) {
        return fail(Errc::not_supported, "the result has no source table");
    }

    const auto is_key = [&target](std::size_t column) {
        return std::find(target.key_columns.begin(), target.key_columns.end(),
                         column) != target.key_columns.end();
    };

    std::string script;
    for (std::size_t row = selection.row_first;
         row <= selection.row_last && row < rs.row_count(); ++row) {
        if (!script.empty()) script += "\n\n";

        switch (kind) {
            case RowScript::select_by_key: {
                OTTER_ASSIGN_OR_RETURN(const std::string where,
                                       key_predicate(rs, target, row));
                std::string names;
                for (const std::size_t c : columns) {
                    if (!names.empty()) names += ", ";
                    names += quote_if_needed(rs.column(c).info().name);
                }
                script += "SELECT " + names + "\n  FROM " + table +
                          "\n WHERE " + where + ";";
                break;
            }

            case RowScript::insert: {
                std::string names;
                std::string values;
                for (const std::size_t c : columns) {
                    const ColumnInfo& info = rs.column(c).info();
                    const CellValue value = cell_value(rs, edits, row, c);
                    if (!names.empty()) { names += ", "; values += ", "; }
                    names  += quote_if_needed(info.name);
                    values += sql_literal(info, value.text, value.is_null);
                }
                script += "INSERT INTO " + table + " (" + names + ")\nVALUES (" +
                          values + ");";
                break;
            }

            case RowScript::update: {
                OTTER_ASSIGN_OR_RETURN(const std::string where,
                                       key_predicate(rs, target, row));
                std::string sets;
                for (const std::size_t c : columns) {
                    // A chave fica so' no WHERE: reescreve-la com o proprio
                    // valor e' ruido, e convida a troca-la sem querer.
                    if (is_key(c)) continue;
                    const ColumnInfo& info = rs.column(c).info();
                    const CellValue value = cell_value(rs, edits, row, c);
                    if (!sets.empty()) sets += "\n     , ";
                    sets += quote_if_needed(info.name) + " = " +
                            sql_literal(info, value.text, value.is_null);
                }
                if (sets.empty()) {
                    return fail(Errc::not_supported,
                                "the result has only key columns");
                }
                script += "UPDATE " + table + "\n   SET " + sets +
                          "\n WHERE " + where + ";";
                break;
            }

            case RowScript::delete_by_key: {
                OTTER_ASSIGN_OR_RETURN(const std::string where,
                                       key_predicate(rs, target, row));
                script += "DELETE FROM " + table + "\n WHERE " + where + ";";
                break;
            }
        }
    }
    return script;
}

SelectionStats selection_stats(const ResultSet& rs, const EditBuffer& edits,
                               const GridSelection& selection) {
    SelectionStats stats;

    std::set<std::string> seen;
    bool   all_numeric = true;
    bool   any_value   = false;
    double minimum = 0.0;
    double maximum = 0.0;

    for (std::size_t row = selection.row_first;
         row <= selection.row_last && row < rs.row_count(); ++row) {
        for (const std::size_t column : selection.columns) {
            if (column >= rs.column_count()) continue;
            ++stats.cells;

            const CellValue value = cell_value(rs, edits, row, column);
            if (value.is_null) {
                ++stats.nulls;
                continue;
            }
            seen.insert(value.text);

            double number = 0.0;
            if (all_numeric && parse_number(value.text, number)) {
                stats.sum += number;
                if (!any_value || number < minimum) minimum = number;
                if (!any_value || number > maximum) maximum = number;
            } else {
                all_numeric = false;
            }

            // Lexicografico, para o caso de nao ser tudo numero.
            if (!any_value || value.text < stats.minimum) stats.minimum = value.text;
            if (!any_value || value.text > stats.maximum) stats.maximum = value.text;
            any_value = true;
        }
    }

    stats.distinct = seen.size();
    stats.numeric  = any_value && all_numeric;

    if (stats.numeric) {
        const std::size_t values = stats.cells - stats.nulls;
        stats.average = stats.sum / static_cast<double>(values);
        stats.minimum = format_number(minimum);
        stats.maximum = format_number(maximum);
    } else {
        stats.sum = 0.0;
    }
    return stats;
}

std::vector<DistinctValue> distinct_values(const ResultSet& rs, std::size_t column,
                                           std::size_t limit) {
    std::vector<DistinctValue> out;
    if (column >= rs.column_count()) return out;

    std::map<std::string, std::size_t> counts;
    std::size_t nulls = 0;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        if (rs.is_null(row, column)) ++nulls;
        else ++counts[std::string(rs.text(row, column))];
    }

    out.reserve(counts.size() + 1);
    for (auto& [text, count] : counts) out.push_back({text, false, count});
    if (nulls > 0) out.push_back({{}, true, nulls});

    // Mais frequente primeiro; empate pelo texto, para a ordem nao mudar de
    // um quadro para o outro.
    std::stable_sort(out.begin(), out.end(),
                     [](const DistinctValue& a, const DistinctValue& b) {
                         return a.count > b.count;
                     });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::string distinct_query(std::string_view inner_sql, std::string_view column,
                           std::size_t limit) {
    // A coluna vai citada, como no filtro da paginacao: ela vem do cabecalho
    // do resultado e pode se chamar "order" ou "Nome". SEMPRE citada, e com o
    // delimitador do dialeto.
    char open = '"', close = '"';
    if (sql_dialect() == QuoteStyle::backticks) { open = '`'; close = '`'; }
    if (sql_dialect() == QuoteStyle::brackets)  { open = '['; close = ']'; }

    std::string quoted(1, open);
    for (const char c : column) {
        if (c == close) quoted.push_back(close);
        quoted.push_back(c);
    }
    quoted.push_back(close);

    const std::string inner(strip_trailing_semicolon(inner_sql));

    if (sql_dialect() == QuoteStyle::anywhere) {
        // SQL Anywhere: TOP sem parenteses; o resto como no T-SQL.
        return "SELECT TOP " + std::to_string(limit) + " " + quoted +
               ", COUNT(*)\n  FROM (\n" + inner +
               "\n) AS otter_distinct\n GROUP BY " + quoted +
               "\n ORDER BY 2 DESC, 1";
    }
    if (sql_dialect() == QuoteStyle::brackets) {
        // T-SQL: TOP no lugar de LIMIT, e GROUP BY nao aceita posicao.
        return "SELECT TOP (" + std::to_string(limit) + ") " + quoted +
               ", COUNT(*)\n  FROM (\n" + inner +
               "\n) AS otter_distinct\n GROUP BY " + quoted +
               "\n ORDER BY 2 DESC, 1";
    }

    return "SELECT " + quoted + ", COUNT(*)\n  FROM (\n" + inner +
           "\n) AS otter_distinct\n GROUP BY 1\n ORDER BY 2 DESC, 1\n LIMIT " +
           std::to_string(limit);
}

std::string equals_expression(const ColumnInfo& info, std::string_view value,
                              bool is_null) {
    if (is_null) return "IS NULL";
    return "= " + sql_literal(info, value, false);
}

LinkQuery navigate_link_query(const ResultSet& rs, const EditBuffer& edits,
                              const TableMeta& table, std::string_view schema,
                              std::size_t row, std::size_t column) {
    LinkQuery link;
    if (row >= rs.row_count() || column >= rs.column_count()) return link;

    const std::string& name = rs.column(column).info().name;

    for (const ForeignKeyMeta& key : table.foreign_keys) {
        const std::vector<std::string> sources = split_list(key.source_column);
        const std::vector<std::string> targets = split_list(key.target_column);
        if (sources.size() != targets.size() || sources.empty()) continue;
        if (std::find(sources.begin(), sources.end(), name) == sources.end()) continue;

        // TODAS as colunas da chave: com so' uma de duas, a consulta
        // devolveria as linhas de outros pais tambem.
        std::string where;
        bool usable = true;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto index = rs.find_column(sources[i]);
            if (!index) { usable = false; break; }

            const CellValue value = cell_value(rs, edits, row, *index);
            if (value.is_null) { usable = false; break; }

            if (!where.empty()) where += "\n   AND ";
            where += quote_if_needed(targets[i]) + " = " +
                     sql_literal(rs.column(*index).info(), value.text, false);
        }
        if (!usable) continue;

        const std::string_view target_schema =
            key.target_schema.empty() ? schema : std::string_view(key.target_schema);
        link.title = key.target_table;
        link.sql   = "SELECT *\n  FROM " + qualified_name(target_schema, key.target_table) +
                     "\n WHERE " + where;
        return link;
    }
    return link;
}

std::vector<LinkQuery> reference_queries(const ResultSet& rs, const EditBuffer& edits,
                                         const TableMeta& table,
                                         std::string_view schema, std::size_t row) {
    std::vector<LinkQuery> links;
    if (row >= rs.row_count()) return links;

    for (const ForeignKeyMeta& key : table.references) {
        const std::vector<std::string> sources = split_list(key.source_column);
        const std::vector<std::string> targets = split_list(key.target_column);
        if (sources.size() != targets.size() || sources.empty()) continue;

        std::string where;
        bool usable = true;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            // O valor esta' NESTA tabela, na coluna de destino da chave.
            const auto index = rs.find_column(targets[i]);
            if (!index) { usable = false; break; }

            const CellValue value = cell_value(rs, edits, row, *index);
            if (value.is_null) { usable = false; break; }

            if (!where.empty()) where += "\n   AND ";
            where += quote_if_needed(sources[i]) + " = " +
                     sql_literal(rs.column(*index).info(), value.text, false);
        }
        if (!usable) continue;

        const std::string_view source_schema =
            key.source_schema.empty() ? schema : std::string_view(key.source_schema);

        LinkQuery link;
        link.title = key.source_table + " (" + key.source_column + ")";
        link.sql   = "SELECT *\n  FROM " + qualified_name(source_schema, key.source_table) +
                     "\n WHERE " + where;
        links.push_back(std::move(link));
    }
    return links;
}

} // namespace otter::db
