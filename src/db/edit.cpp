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

    // Uma tabela so'. Colunas SEM origem sao expressoes e agregados --
    // ignoradas, nao desqualificam o resultado: "SELECT id, nome, now()"
    // continua editavel nas duas primeiras.
    //
    // A origem chega de dois jeitos, conforme o SGBD: por OID no PostgreSQL
    // (RowDescription) e por NOME no MySQL (ColumnDefinition41), que nao tem
    // OID. Olhar so' o OID fazia todo resultado de MySQL ser recusado com
    // "nao vem de uma tabela" -- inclusive um SELECT * numa tabela.
    std::set<std::uint32_t> oids;
    std::set<std::pair<std::string, std::string>> names;   // (schema, tabela)

    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        const ColumnInfo& info = rs.column(c).info();
        if (info.source_table_oid != 0) {
            oids.insert(info.source_table_oid);
        } else if (!info.source_table.empty()) {
            names.emplace(info.source_schema, info.source_table);
        }
    }

    if (oids.empty() && names.empty()) {
        target.refusal = EditRefusal::no_source_table;
        return target;
    }
    if (oids.size() + names.size() > 1) {
        target.refusal = EditRefusal::multiple_tables;
        return target;
    }

    // Acha a tabela no catalogo, pelo OID ou pelo nome.
    const SchemaMeta* found_schema = nullptr;
    const TableMeta*  found_table = nullptr;

    if (!oids.empty()) {
        const std::uint32_t table_oid = *oids.begin();
        for (const SchemaMeta& schema : schemas) {
            for (const TableMeta& table : schema.tables) {
                if (table.oid != table_oid) continue;
                found_schema = &schema;
                found_table  = &table;
                break;
            }
            if (found_table != nullptr) break;
        }
    } else {
        const auto& [schema_name, table_name] = *names.begin();
        for (const SchemaMeta& schema : schemas) {
            // O schema pode vir vazio quando o servidor nao o informa; nesse
            // caso o nome da tabela decide sozinho.
            if (!schema_name.empty() && schema.name != schema_name) continue;

            for (const TableMeta& table : schema.tables) {
                if (table.name != table_name) continue;
                found_schema = &schema;
                found_table  = &table;
                break;
            }
            if (found_table != nullptr) break;
        }
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

void EditBuffer::clear() {
    // Limpa TUDO. Um "Descartar" que deixasse exclusoes ou insercoes
    // pendentes seria pior que nenhum: o usuario acharia que desfez e a
    // proxima gravacao apagaria linhas.
    edits_.clear();
    deleted_.clear();
    insertions_.clear();
}

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

    // Exclusoes e insercoes tambem sao linhas afetadas. Contar so' as
    // alteracoes fazia a barra dizer "1 alteracao em 0 linhas" ao inserir --
    // visto na tela.
    for (const std::size_t row : deleted_) rows.insert(row);

    return rows.size() + insertions_.size();
}

void EditBuffer::mark_deleted(std::size_t row) {
    deleted_.insert(row);

    // Alteracoes na linha excluida viram ruido: o UPDATE rodaria antes do
    // DELETE e o resultado final seria o mesmo.
    for (auto it = edits_.begin(); it != edits_.end();) {
        it = it->second.row == row ? edits_.erase(it) : std::next(it);
    }
}

void EditBuffer::unmark_deleted(std::size_t row) { deleted_.erase(row); }

bool EditBuffer::is_deleted(std::size_t row) const {
    return deleted_.contains(row);
}

std::size_t EditBuffer::add_row() {
    insertions_.emplace_back();
    return insertions_.size() - 1;
}

void EditBuffer::remove_new_row(std::size_t index) {
    if (index < insertions_.size()) {
        insertions_.erase(insertions_.begin() +
                          static_cast<std::ptrdiff_t>(index));
    }
}

void EditBuffer::set_new_value(std::size_t index, std::size_t column,
                               std::string value) {
    if (index >= insertions_.size()) return;
    insertions_[index].values[column] = std::move(value);
    insertions_[index].nulls[column]  = false;
}

void EditBuffer::set_new_null(std::size_t index, std::size_t column) {
    if (index >= insertions_.size()) return;
    insertions_[index].values[column].clear();
    insertions_[index].nulls[column] = true;
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

Result<std::vector<std::string>> generate_changes(const ResultSet& rs,
                                                  const EditTarget& target,
                                                  const EditBuffer& buffer) {
    if (!target.editable()) {
        return fail(Errc::not_supported, std::string(to_string(target.refusal)));
    }

    std::vector<std::string> statements;

    // --- INSERT --------------------------------------------------------------
    //
    // Antes do DELETE: uma linha nova pode referenciar algo que a exclusao
    // removeria, e a ordem inversa violaria a chave estrangeira.
    for (const RowInsertion& insertion : buffer.insertions()) {
        if (insertion.values.empty()) continue;   // linha em branco: ignora

        std::string columns;
        std::string values;

        for (const auto& [column, value] : insertion.values) {
            if (column >= rs.column_count()) {
                return fail(Errc::out_of_range, "new row column is out of range");
            }
            const ColumnInfo& info = rs.column(column).info();

            const auto null_it = insertion.nulls.find(column);
            const bool is_null = null_it != insertion.nulls.end() &&
                                 null_it->second;

            // Coluna vazia e nao marcada como NULL fica de fora: assim a
            // tabela aplica o DEFAULT, que e' o que o usuario espera ao nao
            // preencher um serial ou um timestamp.
            if (value.empty() && !is_null) continue;

            if (!columns.empty()) { columns += ", "; values += ", "; }
            columns += quote_if_needed(info.name);
            values  += literal_for(info, value, is_null);
        }

        if (columns.empty()) continue;

        statements.push_back(
            "INSERT INTO " + qualified_name(target.schema, target.table) +
            " (" + columns + ")\nVALUES (" + values + ");");
    }

    // --- UPDATE --------------------------------------------------------------
    OTTER_ASSIGN_OR_RETURN(auto updates, generate_updates(rs, target, buffer));
    for (std::string& update : updates) statements.push_back(std::move(update));

    // --- DELETE --------------------------------------------------------------
    for (const std::size_t row : buffer.deleted()) {
        if (row >= rs.row_count()) {
            return fail(Errc::out_of_range, "deleted row is out of range");
        }

        std::string sql = "DELETE FROM " +
                          qualified_name(target.schema, target.table) +
                          "\n WHERE ";

        for (std::size_t i = 0; i < target.key_columns.size(); ++i) {
            const std::size_t column = target.key_columns[i];
            const ColumnInfo& info = rs.column(column).info();

            // Chave nula nao identifica linha -- o DELETE apagaria zero
            // linhas, ou (sem WHERE) todas.
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
