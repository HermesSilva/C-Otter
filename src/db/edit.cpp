#include "db/edit.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <set>

namespace otter::db {
namespace {

// Colunas que formam a chave: PK se houver, senao a primeira constraint
// unica. PK e' preferida por ser a intencao declarada do autor da tabela.
const ConstraintMeta* find_key_constraint(const TableMeta& table) {
    const ConstraintMeta* unique = nullptr;

    for (const ConstraintMeta& constraint : table.constraints) {
        if (constraint.kind == ObjKind::primary_key) return &constraint;
        if (constraint.kind == ObjKind::unique_key && unique == nullptr) {
            unique = &constraint;
        }
    }
    return unique;
}

// Separa "cliente_id, item_id" nos nomes individuais.
std::vector<std::string> split_columns(std::string_view list) {
    std::vector<std::string> out;
    std::size_t start = 0;

    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        std::string_view piece = list.substr(
            start, comma == std::string_view::npos ? std::string_view::npos
                                                   : comma - start);

        // Tira espacos e aspas que a definicao possa trazer.
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

// Valor formatado para SQL. Numeros e booleanos sem aspas; o resto citado --
// o PostgreSQL converte data, uuid e json a partir de literal de texto.
std::string literal_for(const ColumnInfo& info, std::string_view value,
                        bool is_null) {
    if (is_null) return "NULL";

    switch (info.kind) {
        case DataKind::boolean:
            return (value == "t" || value == "true" || value == "TRUE")
                       ? "TRUE" : "FALSE";
        case DataKind::integer:
        case DataKind::floating:
        case DataKind::numeric:
            // Vazio num campo numerico nao e' zero -- e' ausencia. Gerar
            // "SET credito = " produziria erro de sintaxe; NULL e' o que o
            // usuario quis dizer ao apagar o conteudo.
            return value.empty() ? "NULL" : std::string(value);
        default:
            break;
    }

    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') out += "''";
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

} // namespace

std::string_view to_string(EditRefusal refusal) noexcept {
    switch (refusal) {
        case EditRefusal::none:             return "editable";
        case EditRefusal::no_result:        return "no result to edit";
        case EditRefusal::multiple_tables:  return "the result joins more than one table";
        case EditRefusal::no_source_table:  return "the result has no source table";
        case EditRefusal::key_not_loaded:   return "loading the table keys...";
        case EditRefusal::no_key:           return "the table has no primary key";
        case EditRefusal::key_not_selected: return "the key columns are not in the result";
    }
    return "unknown";
}

EditTarget find_edit_target(const ResultSet& rs,
                            const std::vector<SchemaMeta>& schemas) {
    EditTarget target;

    if (rs.row_count() == 0 || rs.column_count() == 0) {
        target.refusal = EditRefusal::no_result;
        return target;
    }

    // Uma tabela so'. Colunas com OID zero sao expressoes e agregados --
    // ignoradas, nao desqualificam o resultado: "SELECT id, nome, now()"
    // continua editavel nas duas primeiras.
    std::set<std::uint32_t> oids;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        const std::uint32_t oid = rs.column(c).info().source_table_oid;
        if (oid != 0) oids.insert(oid);
    }

    if (oids.empty()) {
        target.refusal = EditRefusal::no_source_table;
        return target;
    }
    if (oids.size() > 1) {
        target.refusal = EditRefusal::multiple_tables;
        return target;
    }

    const std::uint32_t table_oid = *oids.begin();

    // Acha a tabela no catalogo pelo OID.
    const SchemaMeta* found_schema = nullptr;
    const TableMeta*  found_table = nullptr;

    for (const SchemaMeta& schema : schemas) {
        for (const TableMeta& table : schema.tables) {
            if (table.oid != table_oid) continue;
            found_schema = &schema;
            found_table  = &table;
            break;
        }
        if (found_table != nullptr) break;
    }

    if (found_table == nullptr) {
        // Tabela fora do catalogo carregado -- outro schema, ou de um banco
        // diferente. Nao ha' como saber a chave.
        target.refusal = EditRefusal::key_not_loaded;
        return target;
    }

    target.schema = found_schema->name;
    target.table  = found_table->name;

    // View nao e' editavel sem trigger INSTEAD OF; nao presumimos que exista.
    if (found_table->is_view()) {
        target.refusal = EditRefusal::no_key;
        return target;
    }

    // "Sem chave" e "chave ainda nao lida" sao coisas diferentes, e a
    // mensagem na tela precisa distingui-las: dizer "a tabela nao tem chave
    // primaria" de uma tabela que TEM seria mentira -- o no' simplesmente
    // ainda nao foi expandido.
    if (!found_table->constraints_loaded) {
        target.refusal = EditRefusal::key_not_loaded;
        return target;
    }

    const ConstraintMeta* key = find_key_constraint(*found_table);
    if (key == nullptr) {
        target.refusal = EditRefusal::no_key;
        return target;
    }

    // Todas as colunas da chave precisam estar no resultado: sem elas o WHERE
    // nao pode ser montado, e um UPDATE sem WHERE completo alteraria linhas
    // demais.
    for (const std::string& name : split_columns(key->columns)) {
        const auto index = rs.find_column(name);
        if (!index) {
            target.key_columns.clear();
            target.refusal = EditRefusal::key_not_selected;
            return target;
        }
        target.key_columns.push_back(*index);
    }

    if (target.key_columns.empty()) {
        target.refusal = EditRefusal::no_key;
    }
    return target;
}

void EditBuffer::set(std::size_t row, std::size_t column, std::string value) {
    CellEdit edit;
    edit.row     = row;
    edit.column  = column;
    edit.value   = std::move(value);
    edit.is_null = false;
    edits_[{row, column}] = std::move(edit);
}

void EditBuffer::set_null(std::size_t row, std::size_t column) {
    CellEdit edit;
    edit.row     = row;
    edit.column  = column;
    edit.is_null = true;
    edits_[{row, column}] = std::move(edit);
}

void EditBuffer::clear() { edits_.clear(); }

void EditBuffer::revert(std::size_t row, std::size_t column) {
    edits_.erase({row, column});
}

const CellEdit* EditBuffer::find(std::size_t row, std::size_t column) const {
    const auto it = edits_.find({row, column});
    return it != edits_.end() ? &it->second : nullptr;
}

std::size_t EditBuffer::touched_rows() const {
    std::set<std::size_t> rows;
    for (const auto& [key, edit] : edits_) rows.insert(edit.row);
    return rows.size();
}

Result<std::vector<std::string>> generate_updates(const ResultSet& rs,
                                                  const EditTarget& target,
                                                  const EditBuffer& buffer) {
    if (!target.editable()) {
        return fail(Errc::not_supported, std::string(to_string(target.refusal)));
    }
    if (buffer.empty()) return std::vector<std::string>{};

    // Agrupa por linha: um UPDATE por linha, com todas as colunas sujas dela.
    // Um UPDATE por celula faria tres comandos para uma linha com tres
    // campos alterados.
    std::map<std::size_t, std::vector<const CellEdit*>> by_row;
    for (const auto& [key, edit] : buffer.edits()) {
        by_row[edit.row].push_back(&edit);
    }

    std::vector<std::string> statements;
    statements.reserve(by_row.size());

    for (const auto& [row, edits] : by_row) {
        if (row >= rs.row_count()) {
            return fail(Errc::out_of_range, "edited row is out of range");
        }

        std::string sql = "UPDATE " +
                          qualified_name(target.schema, target.table) + "\n   SET ";

        bool first = true;
        for (const CellEdit* edit : edits) {
            if (edit->column >= rs.column_count()) {
                return fail(Errc::out_of_range, "edited column is out of range");
            }

            // Alterar uma coluna da chave mudaria a propria linha que o WHERE
            // identifica. E' possivel, mas nao por acidente numa grade.
            const bool is_key =
                std::find(target.key_columns.begin(), target.key_columns.end(),
                          edit->column) != target.key_columns.end();
            if (is_key) {
                return fail(Errc::not_supported,
                            "cannot edit a key column from the grid");
            }

            const ColumnInfo& info = rs.column(edit->column).info();
            if (!first) sql += "\n     , ";
            first = false;

            sql += quote_if_needed(info.name) + " = " +
                   literal_for(info, edit->value, edit->is_null);
        }

        sql += "\n WHERE ";
        for (std::size_t i = 0; i < target.key_columns.size(); ++i) {
            const std::size_t column = target.key_columns[i];
            const ColumnInfo& info = rs.column(column).info();

            // Chave nula nao identifica linha: "WHERE id = NULL" nunca casa, e
            // o UPDATE alteraria zero linhas em silencio.
            if (rs.is_null(row, column)) {
                return fail(Errc::invalid_argument,
                            "the key column '" + info.name + "' is NULL in "
                            "this row; it cannot be identified");
            }

            if (i > 0) sql += "\n   AND ";
            sql += quote_if_needed(info.name) + " = " +
                   literal_for(info, rs.text(row, column), false);
        }
        sql += ";";

        statements.push_back(std::move(sql));
    }
    return statements;
}

} // namespace otter::db
