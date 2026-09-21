#include "db/alter.hpp"

#include "db/ddl.hpp"

#include <algorithm>

namespace otter::db {
namespace {

bool is_mysql() noexcept { return sql_dialect() == QuoteStyle::backticks; }

// Definicao COMPLETA de uma coluna, como o MySQL exige em MODIFY/CHANGE.
//
// Repetir tudo nao e' escolha nossa: MODIFY COLUMN substitui a definicao
// inteira, e o que nao for mencionado e' REMOVIDO. Um MODIFY que esqueca o
// AUTO_INCREMENT o apaga em silencio, e o defeito so' aparece quando alguem
// nota que o id parou de incrementar.
std::string mysql_full_definition(const ColumnMeta& current,
                                  const ColumnChange& change) {
    std::string out = quote_if_needed(change.new_name.value_or(current.name));
    out += " " + change.type_name.value_or(current.type_name);

    const bool nullable = change.nullable.value_or(current.nullable);
    if (!nullable) out += " NOT NULL";

    // O default corrente carrega o EXTRA (auto_increment, colunas geradas),
    // que o catalogo junta ali. Separar os dois e' o que impede perde-lo.
    const std::string current_default = current.default_value;
    const bool has_auto =
        current_default.find("auto_increment") != std::string::npos;

    std::string value = change.default_value.value_or(current_default);

    // Tira o auto_increment do texto do default para nao duplica-lo: ele vai
    // depois, como atributo proprio.
    if (const std::size_t at = value.find("auto_increment");
        at != std::string::npos) {
        value.erase(at, std::string_view("auto_increment").size());
    }
    while (!value.empty() && value.back() == ' ') value.pop_back();

    if (!value.empty()) out += " DEFAULT " + value;
    if (has_auto)       out += " AUTO_INCREMENT";

    const std::string comment = change.comment.value_or(current.comment);
    if (!comment.empty()) {
        out += " COMMENT " + quote_literal(comment);
    }
    return out;
}

// Definicao de uma coluna NOVA, para ADD COLUMN e CREATE TABLE.
std::string new_column_definition(const NewColumn& column) {
    std::string out = quote_if_needed(column.name) + " " + column.type_name;

    if (!column.nullable)            out += " NOT NULL";
    if (!column.default_value.empty()) out += " DEFAULT " + column.default_value;

    if (!column.comment.empty()) {
        if (is_mysql()) {
            out += " COMMENT " + quote_literal(column.comment);
        }
        // No PostgreSQL o comentario NAO cabe aqui: vai num COMMENT ON
        // separado. Quem chama emite o comando extra.
    }
    return out;
}

const ColumnMeta* find_column(const TableMeta& table, std::string_view name) {
    for (const ColumnMeta& column : table.columns) {
        if (column.name == name) return &column;
    }
    return nullptr;
}

} // namespace

AlterScript generate_alter(const TableMeta& current,
                           const TableAlteration& wanted) {
    AlterScript script;

    if (wanted.empty()) {
        script.error = "nothing to change";
        return script;
    }
    if (wanted.table.empty()) {
        script.error = "table name is required";
        return script;
    }

    // Sem as colunas carregadas nao da' para gerar MODIFY no MySQL, que exige
    // a definicao inteira. Recusar e' melhor que gerar um ALTER que apaga
    // atributos -- diretiva 6: nada que finge funcionar.
    if (!current.columns_loaded &&
        (!wanted.alter_columns.empty() || !wanted.drop_columns.empty())) {
        script.error = "table columns are not loaded yet";
        return script;
    }

    const std::string qualified = qualified_name(wanted.schema, wanted.table);
    const std::string prefix    = "ALTER TABLE " + qualified;

    auto add = [&script](std::string statement, bool destructive = false) {
        if (destructive) script.destructive.push_back(script.statements.size());
        script.statements.push_back(std::move(statement));
    };

    // --- Colunas novas -------------------------------------------------------

    for (const NewColumn& column : wanted.add_columns) {
        if (column.name.empty() || column.type_name.empty()) {
            script.error = "new column needs a name and a type";
            return script;
        }

        std::string statement = prefix + " ADD COLUMN " +
                                new_column_definition(column);

        if (is_mysql()) {
            // Posicao so' existe no MySQL.
            if (column.first)            statement += " FIRST";
            else if (!column.after.empty())
                statement += " AFTER " + quote_if_needed(column.after);
        } else if (column.first || !column.after.empty()) {
            script.warnings.push_back(
                "PostgreSQL does not support column position; '" + column.name +
                "' will be added at the end");
        }
        add(std::move(statement));

        // NOT NULL sem DEFAULT numa tabela COM DADOS falha nos dois SGBDs.
        // Avisar antes e' melhor que o usuario descobrir pelo erro.
        if (!column.nullable && column.default_value.empty() &&
            current.estimated_rows > 0) {
            script.warnings.push_back(
                "'" + column.name +
                "' is NOT NULL without a DEFAULT; this fails when the table "
                "already has rows");
        }

        // No PostgreSQL o comentario e' um comando separado.
        if (!is_mysql() && !column.comment.empty()) {
            add("COMMENT ON COLUMN " + qualified + "." +
                quote_if_needed(column.name) + " IS " +
                quote_literal(column.comment));
        }
    }

    // --- Colunas alteradas ---------------------------------------------------

    for (const ColumnChange& change : wanted.alter_columns) {
        if (change.empty()) continue;

        const ColumnMeta* column = find_column(current, change.name);
        if (column == nullptr) {
            script.error = "column '" + change.name + "' not found in the table";
            return script;
        }

        if (is_mysql()) {
            // O MySQL nao altera atributo por atributo: CHANGE (com nome novo)
            // ou MODIFY (mesmo nome) substituem a definicao INTEIRA. Por isso
            // um unico comando cobre todas as mudancas desta coluna.
            const std::string definition = mysql_full_definition(*column, change);

            if (change.new_name && *change.new_name != change.name) {
                add(prefix + " CHANGE COLUMN " + quote_if_needed(change.name) +
                    " " + definition);
            } else {
                add(prefix + " MODIFY COLUMN " + definition);
            }
            continue;
        }

        // PostgreSQL: um comando por atributo.
        if (change.new_name && *change.new_name != change.name) {
            add(prefix + " RENAME COLUMN " + quote_if_needed(change.name) +
                " TO " + quote_if_needed(*change.new_name));
        }

        // Os comandos seguintes usam o nome NOVO: o RENAME acima ja' passou.
        const std::string current_name =
            quote_if_needed(change.new_name.value_or(change.name));

        if (change.type_name && *change.type_name != column->type_name) {
            // USING nao e' gerado: a conversao implicita cobre os casos
            // comuns, e inventar um cast poderia corromper dados em silencio.
            add(prefix + " ALTER COLUMN " + current_name + " TYPE " +
                *change.type_name);

            script.warnings.push_back(
                "changing the type of '" + change.name +
                "' rewrites the table and fails when a value does not convert");
        }

        if (change.nullable && *change.nullable != column->nullable) {
            add(prefix + " ALTER COLUMN " + current_name +
                (*change.nullable ? " DROP NOT NULL" : " SET NOT NULL"));

            if (!*change.nullable) {
                script.warnings.push_back(
                    "SET NOT NULL on '" + change.name +
                    "' fails when the column already has NULLs");
            }
        }

        if (change.default_value) {
            if (change.default_value->empty()) {
                add(prefix + " ALTER COLUMN " + current_name + " DROP DEFAULT");
            } else {
                add(prefix + " ALTER COLUMN " + current_name + " SET DEFAULT " +
                    *change.default_value);
            }
        }

        if (change.comment) {
            add("COMMENT ON COLUMN " + qualified + "." + current_name + " IS " +
                quote_literal(*change.comment));
        }
    }

    // --- Colunas removidas ---------------------------------------------------

    for (const std::string& name : wanted.drop_columns) {
        if (find_column(current, name) == nullptr) {
            script.error = "column '" + name + "' not found in the table";
            return script;
        }
        add(prefix + " DROP COLUMN " + quote_if_needed(name),
            /*destructive=*/true);
    }

    // --- Comentario e nome da tabela ------------------------------------------

    if (wanted.comment) {
        if (is_mysql()) {
            add(prefix + " COMMENT = " + quote_literal(*wanted.comment));
        } else {
            add("COMMENT ON TABLE " + qualified + " IS " +
                quote_literal(*wanted.comment));
        }
    }

    // O RENAME vem POR ULTIMO: os comandos acima usam o nome antigo, e
    // renomear primeiro os quebraria todos.
    if (wanted.new_name && *wanted.new_name != wanted.table) {
        if (is_mysql()) {
            // O MySQL tem ALTER TABLE ... RENAME TO, mas RENAME TABLE e' o
            // comando canonico e o unico que move entre bancos.
            add("RENAME TABLE " + qualified + " TO " +
                qualified_name(wanted.schema, *wanted.new_name));
        } else {
            add(prefix + " RENAME TO " + quote_if_needed(*wanted.new_name));
        }
    }

    if (script.statements.empty()) script.error = "nothing to change";
    return script;
}

AlterScript generate_create_table(std::string_view schema,
                                  std::string_view table,
                                  const std::vector<NewColumn>& columns,
                                  const std::vector<std::string>& primary_key,
                                  std::string_view comment) {
    AlterScript script;

    if (table.empty()) {
        script.error = "table name is required";
        return script;
    }
    if (columns.empty()) {
        script.error = "a table needs at least one column";
        return script;
    }

    const std::string qualified = qualified_name(schema, table);
    std::string statement = "CREATE TABLE " + qualified + " (\n";

    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].name.empty() || columns[i].type_name.empty()) {
            script.error = "every column needs a name and a type";
            return script;
        }
        statement += "    " + new_column_definition(columns[i]);
        if (i + 1 < columns.size() || !primary_key.empty()) statement += ",";
        statement += "\n";
    }

    if (!primary_key.empty()) {
        statement += "    PRIMARY KEY (";
        for (std::size_t i = 0; i < primary_key.size(); ++i) {
            if (i > 0) statement += ", ";
            statement += quote_if_needed(primary_key[i]);
        }
        statement += ")\n";
    } else {
        // Sem PK a grade nao edita (ADR 0014) e a replicacao do MySQL
        // reclama. Avisar na hora de criar e' mais barato que descobrir
        // depois.
        script.warnings.push_back(
            "table without a primary key: the grid will not be editable");
    }

    statement += ")";
    if (is_mysql() && !comment.empty()) {
        statement += " COMMENT = " + quote_literal(comment);
    }
    script.statements.push_back(std::move(statement));

    if (!is_mysql() && !comment.empty()) {
        script.statements.push_back("COMMENT ON TABLE " + qualified + " IS " +
                                    quote_literal(comment));
    }

    // Comentario de coluna no PostgreSQL vai em comando separado.
    if (!is_mysql()) {
        for (const NewColumn& column : columns) {
            if (column.comment.empty()) continue;
            script.statements.push_back(
                "COMMENT ON COLUMN " + qualified + "." +
                quote_if_needed(column.name) + " IS " +
                quote_literal(column.comment));
        }
    }
    return script;
}

AlterScript generate_drop(std::string_view schema, std::string_view name,
                          ObjKind kind, bool cascade) {
    AlterScript script;

    if (name.empty()) {
        script.error = "object name is required";
        return script;
    }

    std::string_view keyword;
    switch (kind) {
        case ObjKind::table:             keyword = "TABLE"; break;
        case ObjKind::view:              keyword = "VIEW"; break;
        case ObjKind::materialized_view: keyword = "MATERIALIZED VIEW"; break;
        case ObjKind::sequence:          keyword = "SEQUENCE"; break;
        case ObjKind::index:             keyword = "INDEX"; break;
        case ObjKind::function:          keyword = "FUNCTION"; break;
        case ObjKind::procedure:         keyword = "PROCEDURE"; break;
        case ObjKind::trigger:           keyword = "TRIGGER"; break;
        case ObjKind::schema:            keyword = "SCHEMA"; break;
        case ObjKind::data_type:         keyword = "TYPE"; break;
        default:
            script.error = "dropping this object type is not supported";
            return script;
    }

    std::string statement = "DROP " + std::string(keyword) + " " +
                            qualified_name(schema, name);

    if (cascade) {
        if (is_mysql()) {
            // O MySQL aceita a palavra em DROP TABLE mas a IGNORA. Emiti-la
            // faria o usuario acreditar numa cascata que nao acontece --
            // exatamente o campo que finge funcionar da diretiva 6.
            script.warnings.push_back(
                "MySQL ignores CASCADE; dependent objects are not dropped");
        } else {
            statement += " CASCADE";
            script.warnings.push_back(
                "CASCADE also drops every dependent object (views, foreign "
                "keys), and there is no undo");
        }
    }

    script.destructive.push_back(0);
    script.statements.push_back(std::move(statement));
    return script;
}

} // namespace otter::db
