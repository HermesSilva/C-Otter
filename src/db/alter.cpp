#include "db/alter.hpp"

#include "db/catalog_mssql.hpp"   // mssql_literal, mssql_object_id
#include "db/ddl.hpp"
#include "db/mssql_object.hpp"
#include "db/sqlanywhere_object.hpp"

#include <algorithm>
#include <cctype>

namespace otter::db {
namespace {

bool is_mysql() noexcept { return sql_dialect() == QuoteStyle::backticks; }
bool is_mssql() noexcept { return sql_dialect() == QuoteStyle::brackets; }
bool is_anywhere() noexcept { return sql_dialect() == QuoteStyle::anywhere; }

// O nome do SGBD nos avisos: "PostgreSQL does not..." num SQL Server mandaria
// procurar a causa no lugar errado.
const char* engine_name() noexcept {
    return is_mysql()      ? "MySQL"
           : is_mssql()    ? "SQL Server"
           : is_anywhere() ? "SQL Anywhere"
                           : "PostgreSQL";
}

// --- SQL Anywhere ----------------------------------------------------------------

// O ALTER TABLE do SQL Anywhere: ADD e DROP sem a palavra COLUMN; "ALTER
// coluna" seguido do que muda (tipo, NULL / NOT NULL, DEFAULT), um comando por
// atributo; RENAME sem COLUMN nem TO para a tabela; comentario por COMMENT ON.
void anywhere_alter(AlterScript& script, const TableMeta& current,
                    const TableAlteration& wanted) {
    const std::string qualified = qualified_name(wanted.schema, wanted.table);
    const std::string prefix    = "ALTER TABLE " + qualified;

    const auto add = [&script](std::string statement, bool destructive = false) {
        if (destructive) script.destructive.push_back(script.statements.size());
        script.statements.push_back(std::move(statement));
    };

    for (const NewColumn& column : wanted.add_columns) {
        if (column.name.empty() || column.type_name.empty()) {
            script.error = "new column needs a name and a type";
            return;
        }
        // NULL explicito: sem ele a nulidade depende de allow_nulls_by_default
        // da conexao, e a mesma janela criaria colunas diferentes.
        std::string statement = prefix + " ADD " + quote_if_needed(column.name) + " " +
                                column.type_name + (column.nullable ? " NULL" : " NOT NULL");
        if (!column.default_value.empty()) statement += " DEFAULT " + column.default_value;
        add(std::move(statement));

        if (column.first || !column.after.empty()) {
            script.warnings.push_back(
                "SQL Anywhere does not support column position; '" + column.name +
                "' will be added at the end");
        }
        if (!column.nullable && column.default_value.empty() && current.estimated_rows > 0) {
            script.warnings.push_back(
                "'" + column.name +
                "' is NOT NULL without a DEFAULT; this fails when the table "
                "already has rows");
        }
        if (!column.comment.empty()) {
            add("COMMENT ON COLUMN " + qualified + "." + quote_if_needed(column.name) +
                " IS " + quote_literal(column.comment));
        }
    }

    for (const ColumnChange& change : wanted.alter_columns) {
        if (change.empty()) continue;

        const ColumnMeta* column = nullptr;
        for (const ColumnMeta& candidate : current.columns) {
            if (candidate.name == change.name) column = &candidate;
        }
        if (column == nullptr) {
            script.error = "column '" + change.name + "' not found in the table";
            return;
        }

        if (change.new_name && *change.new_name != change.name) {
            add(prefix + " RENAME " + quote_if_needed(change.name) + " TO " +
                quote_if_needed(*change.new_name));
        }
        // Os comandos seguintes usam o nome NOVO: o RENAME acima ja' passou.
        const std::string name = quote_if_needed(change.new_name.value_or(change.name));

        if (change.type_name && *change.type_name != column->type_name) {
            add(prefix + " ALTER " + name + " " + *change.type_name);
            script.warnings.push_back(
                "changing the type of '" + change.name +
                "' fails when a value does not convert, or when the column is part "
                "of a key");
        }
        if (change.nullable && *change.nullable != column->nullable) {
            add(prefix + " ALTER " + name + (*change.nullable ? " NULL" : " NOT NULL"));
            if (!*change.nullable) {
                script.warnings.push_back("NOT NULL on '" + change.name +
                                          "' fails when the column already has NULLs");
            }
        }
        if (change.default_value) {
            if (change.default_value->empty()) {
                add(prefix + " ALTER " + name + " DROP DEFAULT");
            } else {
                add(prefix + " ALTER " + name + " DEFAULT " + *change.default_value);
            }
        }
        if (change.comment) {
            add("COMMENT ON COLUMN " + qualified + "." + name + " IS " +
                (change.comment->empty() ? std::string("NULL")
                                         : quote_literal(*change.comment)));
        }
    }

    for (const std::string& name : wanted.drop_columns) {
        const bool known = std::any_of(
            current.columns.begin(), current.columns.end(),
            [&name](const ColumnMeta& candidate) { return candidate.name == name; });
        if (!known) {
            script.error = "column '" + name + "' not found in the table";
            return;
        }
        add(prefix + " DROP " + quote_if_needed(name), /*destructive=*/true);
    }

    if (wanted.comment) {
        add("COMMENT ON TABLE " + qualified + " IS " +
            (wanted.comment->empty() ? std::string("NULL") : quote_literal(*wanted.comment)));
    }
    // O RENAME vem por ultimo: os comandos acima usam o nome antigo.
    if (wanted.new_name && *wanted.new_name != wanted.table) {
        add(prefix + " RENAME " + quote_if_needed(*wanted.new_name));
    }
}

// --- SQL Server ------------------------------------------------------------------

ObjectRef mssql_column_ref(std::string_view schema, std::string_view table,
                           std::string_view column) {
    ObjectRef ref;
    ref.type   = ObjectType::column;
    ref.schema = std::string(schema);
    ref.parent = std::string(table);
    ref.name   = std::string(column);
    return ref;
}

ObjectRef mssql_table_ref(std::string_view schema, std::string_view table) {
    ObjectRef ref;
    ref.type   = ObjectType::table;
    ref.schema = std::string(schema);
    ref.name   = std::string(table);
    return ref;
}

// No SQL Server o DEFAULT e' uma CONSTRAINT com nome proprio, quase sempre
// gerado (DF__tabela__col__5AEE82B9). Para tira-lo e' preciso descobrir o
// nome no catalogo -- por isso um lote com SQL dinamico, e nao um ALTER fixo.
// Sem default na coluna o lote nao faz nada.
std::string mssql_drop_default(std::string_view schema, std::string_view table,
                               std::string_view column) {
    // O comando e' montado numa variavel: EXEC('...' + QUOTENAME(x)) nao
    // compila -- o EXEC de texto so' concatena literais e variaveis.
    return "DECLARE @sql nvarchar(max) = (SELECT " +
           mssql_literal("ALTER TABLE " + qualified_name(schema, table) +
                         " DROP CONSTRAINT ") +
           " + QUOTENAME(dc.name) FROM sys.default_constraints dc"
           " JOIN sys.columns c ON c.object_id = dc.parent_object_id"
           " AND c.column_id = dc.parent_column_id"
           " WHERE dc.parent_object_id = " + mssql_object_id(schema, table) +
           " AND c.name = " + mssql_literal(column) + ");\n"
           "IF @sql IS NOT NULL EXEC(@sql)";
}

// IDENTITY e coluna calculada chegam do catalogo no campo do default, para a
// arvore mostrar de onde o valor vem. Nao sao constraint de default.
bool mssql_real_default(const ColumnMeta& column) {
    return !column.default_value.empty() && column.default_value != "IDENTITY" &&
           !column.default_value.starts_with("AS ");
}

// O que o ALTER de uma tabela do SQL Server tem de diferente dos outros dois:
// ADD sem a palavra COLUMN; ALTER COLUMN repete tipo E nulidade; default e'
// constraint; renomear e comentar sao procedimentos do sistema.
void mssql_alter(AlterScript& script, const TableMeta& current,
                 const TableAlteration& wanted) {
    const std::string qualified = qualified_name(wanted.schema, wanted.table);
    const std::string prefix    = "ALTER TABLE " + qualified;

    const auto add = [&script](std::string statement, bool destructive = false) {
        if (destructive) script.destructive.push_back(script.statements.size());
        script.statements.push_back(std::move(statement));
    };
    const auto append = [&script, &add](AlterScript part) {
        if (!part.error.empty()) {
            if (script.error.empty()) script.error = std::move(part.error);
            return;
        }
        for (std::string& statement : part.statements) add(std::move(statement));
        for (std::string& warning : part.warnings) script.warnings.push_back(std::move(warning));
    };

    for (const NewColumn& column : wanted.add_columns) {
        if (column.name.empty() || column.type_name.empty()) {
            script.error = "new column needs a name and a type";
            return;
        }
        std::string statement = prefix + " ADD " + quote_if_needed(column.name) + " " +
                                column.type_name;
        // NULL explicito: sem ele a nulidade depende de ANSI_NULL_DFLT_ON da
        // sessao, e a mesma janela criaria colunas diferentes.
        statement += column.nullable ? " NULL" : " NOT NULL";
        if (!column.default_value.empty()) statement += " DEFAULT " + column.default_value;
        add(std::move(statement));

        if (column.first || !column.after.empty()) {
            script.warnings.push_back(
                "SQL Server does not support column position; '" + column.name +
                "' will be added at the end");
        }
        if (!column.nullable && column.default_value.empty() && current.estimated_rows > 0) {
            script.warnings.push_back(
                "'" + column.name +
                "' is NOT NULL without a DEFAULT; this fails when the table "
                "already has rows");
        }
        if (!column.comment.empty()) {
            append(mssql_object_comment(
                mssql_column_ref(wanted.schema, wanted.table, column.name), column.comment));
        }
    }

    for (const ColumnChange& change : wanted.alter_columns) {
        if (change.empty()) continue;

        const ColumnMeta* column = nullptr;
        for (const ColumnMeta& candidate : current.columns) {
            if (candidate.name == change.name) column = &candidate;
        }
        if (column == nullptr) {
            script.error = "column '" + change.name + "' not found in the table";
            return;
        }

        const std::string name = change.new_name.value_or(change.name);
        if (name != change.name) {
            append(mssql_object_rename(
                mssql_column_ref(wanted.schema, wanted.table, change.name), name));
        }

        const bool type_changed = change.type_name && *change.type_name != column->type_name;
        const bool null_changed = change.nullable && *change.nullable != column->nullable;
        if (type_changed || null_changed) {
            // Tipo e nulidade vao JUNTOS: omitir a nulidade a devolve ao padrao
            // da sessao, e um ALTER de tipo tornaria nula uma coluna NOT NULL.
            const bool nullable = change.nullable.value_or(column->nullable);
            add(prefix + " ALTER COLUMN " + quote_if_needed(name) + " " +
                change.type_name.value_or(column->type_name) +
                (nullable ? " NULL" : " NOT NULL"));

            if (type_changed) {
                script.warnings.push_back(
                    "changing the type of '" + change.name +
                    "' fails when a value does not convert, or when an index or "
                    "constraint depends on the column");
            }
            if (null_changed && !nullable) {
                script.warnings.push_back("SET NOT NULL on '" + change.name +
                                          "' fails when the column already has NULLs");
            }
        }

        if (change.default_value) {
            if (mssql_real_default(*column)) {
                add(mssql_drop_default(wanted.schema, wanted.table, name));
            }
            if (!change.default_value->empty()) {
                add(prefix + " ADD DEFAULT " + *change.default_value + " FOR " +
                    quote_if_needed(name));
            }
        }

        if (change.comment) {
            append(mssql_object_comment(
                mssql_column_ref(wanted.schema, wanted.table, name), *change.comment));
        }
    }

    for (const std::string& name : wanted.drop_columns) {
        const ColumnMeta* column = nullptr;
        for (const ColumnMeta& candidate : current.columns) {
            if (candidate.name == name) column = &candidate;
        }
        if (column == nullptr) {
            script.error = "column '" + name + "' not found in the table";
            return;
        }
        // A constraint de default prende a coluna: o DROP COLUMN e' recusado
        // enquanto ela existir.
        if (mssql_real_default(*column)) {
            add(mssql_drop_default(wanted.schema, wanted.table, name));
        }
        add(prefix + " DROP COLUMN " + quote_if_needed(name), /*destructive=*/true);
    }

    if (wanted.comment) {
        append(mssql_object_comment(mssql_table_ref(wanted.schema, wanted.table),
                                    *wanted.comment));
    }
    if (wanted.new_name && *wanted.new_name != wanted.table) {
        append(mssql_object_rename(mssql_table_ref(wanted.schema, wanted.table),
                                   *wanted.new_name));
    }
}

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
    // SQL Anywhere: NULL explicito. Sem ele vale allow_nulls_by_default da
    // conexao, que o protocolo TDS liga ao contrario do padrao do banco.
    else if (is_anywhere())          out += " NULL";
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

    if (is_mssql()) {
        mssql_alter(script, current, wanted);
        if (script.error.empty() && script.statements.empty()) {
            script.error = "nothing to change";
        }
        return script;
    }
    if (is_anywhere()) {
        anywhere_alter(script, current, wanted);
        if (script.error.empty() && script.statements.empty()) {
            script.error = "nothing to change";
        }
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

    if (is_mssql()) {
        // Comentario e' a propriedade estendida MS_Description, uma por
        // objeto e por coluna.
        const auto describe = [&script](AlterScript part) {
            for (std::string& text : part.statements) script.statements.push_back(std::move(text));
        };
        if (!comment.empty()) {
            describe(mssql_object_comment(mssql_table_ref(schema, table), comment));
        }
        for (const NewColumn& column : columns) {
            if (column.comment.empty()) continue;
            describe(mssql_object_comment(mssql_column_ref(schema, table, column.name),
                                          column.comment));
        }
        return script;
    }

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

    if (is_anywhere()) {
        // O tipo de usuario e' um DOMAIN, e nao leva o dono no nome. O
        // "schema" e' um usuario, que sai por DROP USER.
        if (kind == ObjKind::data_type) statement = "DROP DOMAIN " + quote_if_needed(name);
        if (kind == ObjKind::schema)    statement = "DROP USER " + quote_if_needed(name);
        if (cascade) {
            script.warnings.push_back(
                "SQL Anywhere has no CASCADE: the views that use the object become "
                "invalid, and they are not dropped");
        }
        script.destructive.push_back(0);
        script.statements.push_back(std::move(statement));
        return script;
    }

    if (cascade) {
        if (is_mysql()) {
            // O MySQL aceita a palavra em DROP TABLE mas a IGNORA. Emiti-la
            // faria o usuario acreditar numa cascata que nao acontece --
            // exatamente o campo que finge funcionar da diretiva 6.
            script.warnings.push_back(
                "MySQL ignores CASCADE; dependent objects are not dropped");
        } else if (is_mssql()) {
            // O T-SQL nem aceita a palavra: o DROP e' recusado enquanto houver
            // chave estrangeira apontando para a tabela.
            script.warnings.push_back(
                "SQL Server has no CASCADE; the drop is refused while a foreign "
                "key references the object");
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

// --- Indices e constraints -------------------------------------------------------

namespace {

// Lista de colunas delimitadas, entre parenteses.
std::string column_list(const std::vector<std::string>& columns) {
    std::string out = "(";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) out += ", ";
        out += quote_if_needed(columns[i]);
    }
    out += ")";
    return out;
}

} // namespace

AlterScript generate_create_index(std::string_view schema, std::string_view table,
                                  const NewIndex& index) {
    AlterScript script;

    if (index.name.empty() || index.columns.empty()) {
        script.error = "an index needs a name and at least one column";
        return script;
    }

    const std::string qualified = qualified_name(schema, table);
    const std::string columns   = column_list(index.columns);

    if (is_mysql()) {
        // No MySQL o indice nasce DENTRO do ALTER TABLE, e o metodo vem depois
        // das colunas -- ao contrario do PostgreSQL, onde vem antes.
        std::string statement = "ALTER TABLE " + qualified + " ADD " +
                                std::string(index.unique ? "UNIQUE " : "") +
                                "INDEX " + quote_if_needed(index.name) + " " +
                                columns;

        if (!index.method.empty()) statement += " USING " + index.method;

        if (index.concurrently) {
            script.warnings.push_back(
                "MySQL has no CONCURRENTLY; the index is built with the table "
                "locked for writes");
        }
        script.statements.push_back(std::move(statement));
        return script;
    }

    if (is_anywhere()) {
        std::string statement = "CREATE ";
        if (index.unique) statement += "UNIQUE ";
        if (index.method == "CLUSTERED") {
            statement += "CLUSTERED ";
        } else if (!index.method.empty()) {
            script.warnings.push_back("SQL Anywhere has no index method '" + index.method +
                                      "'; a b-tree index is created");
        }
        statement += "INDEX " + quote_if_needed(index.name) + " ON " + qualified + " " +
                     columns;
        if (index.concurrently) {
            script.warnings.push_back(
                "SQL Anywhere has no CONCURRENTLY; the index is built with the table "
                "locked");
        }
        script.statements.push_back(std::move(statement));
        return script;
    }

    if (is_mssql()) {
        // CLUSTERED / NONCLUSTERED fica ANTES de INDEX, e e' a unica coisa que
        // o "metodo" significa aqui.
        std::string statement = "CREATE ";
        if (index.unique) statement += "UNIQUE ";
        if (index.method == "CLUSTERED" || index.method == "NONCLUSTERED") {
            statement += index.method + " ";
        } else if (!index.method.empty()) {
            script.warnings.push_back("SQL Server has no index method '" + index.method +
                                      "'; a NONCLUSTERED b-tree is created");
        }
        statement += "INDEX " + quote_if_needed(index.name) + " ON " + qualified + " " +
                     columns;
        if (index.concurrently) {
            script.warnings.push_back(
                "SQL Server has no CONCURRENTLY (ONLINE = ON needs the Enterprise "
                "edition); the index is built with the table locked for writes");
        }
        script.statements.push_back(std::move(statement));
        return script;
    }

    // PostgreSQL: comando proprio, com USING ANTES das colunas.
    std::string statement = "CREATE ";
    if (index.unique) statement += "UNIQUE ";
    statement += "INDEX ";

    if (index.concurrently) {
        statement += "CONCURRENTLY ";

        // CONCURRENTLY NAO roda dentro de transacao, e a UI envolve scripts de
        // varios comandos em BEGIN/COMMIT. Avisar aqui evita o erro
        // "CREATE INDEX CONCURRENTLY cannot run inside a transaction block",
        // que nao diz o que fazer a respeito.
        script.warnings.push_back(
            "CONCURRENTLY cannot run inside a transaction, and leaves an "
            "INVALID index behind when it fails");
    }

    statement += quote_if_needed(index.name) + " ON " + qualified;
    if (!index.method.empty()) statement += " USING " + index.method;
    statement += " " + columns;

    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript generate_drop_index(std::string_view schema, std::string_view table,
                                std::string_view index, bool from_constraint) {
    AlterScript script;

    if (index.empty()) {
        script.error = "index name is required";
        return script;
    }

    // Indice que existe por causa de uma constraint nao se remove sozinho.
    //
    // O PostgreSQL RECUSA com "cannot drop index ... because constraint
    // requires it". O MySQL ACEITA -- e remove a constraint junto, em
    // silencio. O segundo caso e' pior: o usuario perde a chave sem saber.
    if (from_constraint) {
        script.error =
            "this index belongs to a constraint; drop the constraint instead";
        return script;
    }

    if (is_mysql()) {
        // No MySQL o indice pertence a' TABELA: nao ha' DROP INDEX solto que
        // saiba onde procurar.
        script.statements.push_back("ALTER TABLE " +
                                    qualified_name(schema, table) +
                                    " DROP INDEX " + quote_if_needed(index));
    } else if (is_mssql()) {
        // O nome do indice so' e' unico DENTRO da tabela.
        script.statements.push_back("DROP INDEX " + quote_if_needed(index) + " ON " +
                                    qualified_name(schema, table));
    } else if (is_anywhere()) {
        // dono.tabela.indice: com dois nomes o servidor leria "tabela.indice".
        script.statements.push_back("DROP INDEX " + qualified_name(schema, table) + "." +
                                    quote_if_needed(index));
    } else {
        // No PostgreSQL o indice e' objeto do SCHEMA, nao da tabela.
        script.statements.push_back("DROP INDEX " +
                                    qualified_name(schema, index));
    }
    script.destructive.push_back(0);
    return script;
}

AlterScript generate_add_constraint(std::string_view schema,
                                    std::string_view table,
                                    const NewConstraint& constraint) {
    AlterScript script;

    const std::string prefix = "ALTER TABLE " + qualified_name(schema, table);
    std::string statement = prefix + " ADD ";

    // Nome e' OPCIONAL: sem ele o SGBD gera um. Forcar o usuario a inventar um
    // nome para uma PK seria atrito sem ganho.
    if (!constraint.name.empty()) {
        statement += "CONSTRAINT " + quote_if_needed(constraint.name) + " ";
    }

    switch (constraint.kind) {
        case ConstraintKind::primary_key:
        case ConstraintKind::unique:
            if (constraint.columns.empty()) {
                script.error = "this constraint needs at least one column";
                return script;
            }
            statement += std::string(constraint.kind == ConstraintKind::primary_key
                                         ? "PRIMARY KEY "
                                         : "UNIQUE ") +
                         column_list(constraint.columns);

            if (constraint.kind == ConstraintKind::primary_key) {
                script.warnings.push_back(
                    "adding a PRIMARY KEY fails when the columns have NULLs or "
                    "duplicate values");
            }
            break;

        case ConstraintKind::check:
            if (constraint.expression.empty()) {
                script.error = "a CHECK constraint needs an expression";
                return script;
            }
            statement += "CHECK (" + constraint.expression + ")";

            // O MySQL so' PASSOU A APLICAR o CHECK no 8.0.16: antes ele
            // aceitava a sintaxe e ignorava a restricao -- uma constraint que
            // finge funcionar.
            if (is_mysql()) {
                script.warnings.push_back(
                    "CHECK constraints are only enforced from MySQL 8.0.16 and "
                    "MariaDB 10.2 on; older servers accept and ignore them");
            }
            break;
    }

    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript generate_drop_constraint(std::string_view schema,
                                     std::string_view table,
                                     std::string_view name, ObjKind kind) {
    AlterScript script;

    if (name.empty()) {
        script.error = "constraint name is required";
        return script;
    }

    const std::string prefix = "ALTER TABLE " + qualified_name(schema, table);

    if (is_mysql()) {
        // O MySQL tem sintaxe PROPRIA por tipo, e a chave primaria nem tem
        // nome: e' sempre "PRIMARY". O DROP CONSTRAINT generico so' existe a
        // partir do 8.0.19, e usa-lo quebraria em servidor mais antigo.
        if (kind == ObjKind::primary_key) {
            script.statements.push_back(prefix + " DROP PRIMARY KEY");
        } else {
            script.statements.push_back(prefix + " DROP KEY " +
                                        quote_if_needed(name));
        }
    } else {
        script.statements.push_back(prefix + " DROP CONSTRAINT " +
                                    quote_if_needed(name));
    }

    script.destructive.push_back(0);

    if (kind == ObjKind::primary_key) {
        // Sem PK a grade deixa de editar (ADR 0014). Dizer antes evita a
        // surpresa de descobrir depois, ao tentar alterar uma celula.
        script.warnings.push_back(
            "without a primary key the grid can no longer edit this table");
    }
    return script;
}

AlterScript generate_add_foreign_key(std::string_view schema,
                                     std::string_view table,
                                     const NewForeignKey& key) {
    AlterScript script;

    if (key.columns.empty() || key.target_table.empty() ||
        key.target_columns.empty()) {
        script.error = "a foreign key needs source columns, a target table and "
                       "target columns";
        return script;
    }
    if (key.columns.size() != key.target_columns.size()) {
        // Um FK com 2 colunas de origem e 1 de destino e' sintaxe valida que o
        // servidor recusa com mensagem obscura. Recusar aqui e' mais util.
        script.error = "the number of source and target columns must match";
        return script;
    }

    std::string statement = "ALTER TABLE " + qualified_name(schema, table) +
                            " ADD ";
    // No SQL Anywhere o nome de uma chave estrangeira e' o "papel" dela -- o
    // nome do indice que a sustenta, e o que DROP FOREIGN KEY recebe. Vai
    // depois de FOREIGN KEY, nao numa clausula CONSTRAINT.
    if (!key.name.empty() && !is_anywhere()) {
        statement += "CONSTRAINT " + quote_if_needed(key.name) + " ";
    }

    const std::string_view target_schema =
        key.target_schema.empty() ? schema : std::string_view(key.target_schema);

    statement += "FOREIGN KEY ";
    if (!key.name.empty() && is_anywhere()) statement += quote_if_needed(key.name) + " ";
    statement += column_list(key.columns) + " REFERENCES " +
                 qualified_name(target_schema, key.target_table) + " " +
                 column_list(key.target_columns);

    // O T-SQL nao tem RESTRICT; NO ACTION e' o mesmo efeito (a exclusao e'
    // recusada), e e' o que o servidor guarda.
    const auto action = [](const std::string& wanted) {
        return is_mssql() && wanted == "RESTRICT" ? std::string("NO ACTION") : wanted;
    };
    if (!key.on_delete.empty()) statement += " ON DELETE " + action(key.on_delete);
    if (!key.on_update.empty()) statement += " ON UPDATE " + action(key.on_update);

    if (key.on_delete == "CASCADE") {
        script.warnings.push_back(
            "ON DELETE CASCADE deletes the referencing rows automatically");
    }

    // A FK exige um indice nas colunas de ORIGEM: o MySQL cria um sozinho, o
    // PostgreSQL NAO -- e sem ele todo DELETE na tabela de destino varre a de
    // origem inteira. E' a causa mais comum de DELETE lento num banco com
    // muitas FKs.
    // O SQL Anywhere tambem cria o indice sozinho (a chave E' um indice).
    if (!is_mysql() && !is_anywhere()) {
        script.warnings.push_back(
            std::string(engine_name()) +
            " does not index the referencing columns automatically; "
            "without an index, deletes on the target table scan this one");
    }

    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript generate_drop_foreign_key(std::string_view schema,
                                      std::string_view table,
                                      std::string_view name) {
    AlterScript script;

    if (name.empty()) {
        script.error = "foreign key name is required";
        return script;
    }

    const std::string prefix = "ALTER TABLE " + qualified_name(schema, table);

    // O MySQL tem comando proprio; no PostgreSQL a FK e' uma constraint como
    // as outras.
    script.statements.push_back(
        prefix + (is_mysql() || is_anywhere() ? " DROP FOREIGN KEY " : " DROP CONSTRAINT ") +
        quote_if_needed(name));

    script.destructive.push_back(0);
    return script;
}
// --- View, sequence e trigger ------------------------------------------------------

AlterScript generate_create_view(std::string_view schema, std::string_view name,
                                 std::string_view definition, bool or_replace) {
    AlterScript script;

    if (name.empty()) {
        script.error = "view name is required";
        return script;
    }
    if (definition.empty()) {
        script.error = "a view needs a query";
        return script;
    }

    // CREATE OR REPLACE preserva as PERMISSOES concedidas sobre a view. Fazer
    // DROP + CREATE as perderia em silencio, e o usuario so' descobriria
    // quando alguem reclamasse de acesso negado -- dias depois.
    // No T-SQL e' CREATE OR ALTER (SQL Server 2016 SP1 em diante).
    std::string statement = !or_replace ? "CREATE VIEW "
                            : is_mssql() ? "CREATE OR ALTER VIEW "
                                         : "CREATE OR REPLACE VIEW ";
    statement += qualified_name(schema, name) + " AS\n" +
                 std::string(strip_trailing_semicolon(definition));

    script.statements.push_back(std::move(statement));

    if (!or_replace) {
        script.warnings.push_back(
            "without OR REPLACE this fails when the view already exists");
    }

    // A view guarda o corpo COMO ESCRITO. Uma tabela sem banco no FROM
    // depende do banco corrente da conexão -- e a nossa não tem um por
    // padrão, o que faz o MySQL recusar com "No database selected", uma
    // mensagem que não aponta para a causa.
    if (definition.find('.') == std::string_view::npos) {
        script.warnings.push_back(
            "the query has no qualified table name; it depends on the current "
            "database and may fail");
    }
    return script;
}

AlterScript generate_create_sequence(std::string_view schema,
                                     const NewSequence& sequence) {
    AlterScript script;

    if (sequence.name.empty()) {
        script.error = "sequence name is required";
        return script;
    }
    if (sequence.increment == 0) {
        // Incremento zero e' aceito pela sintaxe e gera uma sequence que
        // devolve sempre o mesmo numero. O servidor recusa, mas com uma
        // mensagem que nao diz o que fazer.
        script.error = "the increment cannot be zero";
        return script;
    }

    // Sequence so' existe no PostgreSQL e no MariaDB 10.3+. No MySQL o
    // equivalente e' AUTO_INCREMENT, que e' propriedade da COLUNA -- oferecer
    // CREATE SEQUENCE ali daria erro de sintaxe.
    if (is_mysql()) {
        script.warnings.push_back(
            "MySQL has no sequences; use AUTO_INCREMENT on the column. This "
            "only works on MariaDB 10.3 and later");
    }

    std::string statement = "CREATE SEQUENCE " +
                            qualified_name(schema, sequence.name);

    statement += "\n  START WITH " + std::to_string(sequence.start);
    statement += "\n  INCREMENT BY " + std::to_string(sequence.increment);

    if (sequence.minimum != 0) {
        statement += "\n  MINVALUE " + std::to_string(sequence.minimum);
    }
    if (sequence.maximum != 0) {
        statement += "\n  MAXVALUE " + std::to_string(sequence.maximum);
    }

    // NO CYCLE explicito: e' o padrao nos dois SGBDs, mas dizer torna o DDL
    // legivel sem consultar o manual.
    statement += sequence.cycle ? "\n  CYCLE" : "\n  NO CYCLE";

    if (sequence.cycle) {
        script.warnings.push_back(
            "with CYCLE the sequence restarts after the maximum, and can "
            "return a value it already returned");
    }

    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript generate_create_trigger(std::string_view schema,
                                    const NewTrigger& trigger) {
    AlterScript script;

    if (trigger.name.empty() || trigger.table.empty()) {
        script.error = "a trigger needs a name and a table";
        return script;
    }
    if (trigger.body.empty()) {
        script.error = "a trigger needs a body";
        return script;
    }
    if (trigger.timing.empty() || trigger.event.empty()) {
        script.error = "a trigger needs a timing (BEFORE/AFTER) and an event";
        return script;
    }

    if (is_mysql()) {
        // O CREATE TRIGGER do MySQL NAO aceita nome qualificado: e' preciso
        // `USE <banco>` antes. Por isso o script tem dois comandos.
        if (!schema.empty()) {
            script.statements.push_back("USE " + quote_if_needed(schema));
        }

        script.statements.push_back(
            "CREATE TRIGGER " + quote_if_needed(trigger.name) + " " +
            trigger.timing + " " + trigger.event + " ON " +
            quote_if_needed(trigger.table) + " FOR EACH ROW\n" +
            std::string(strip_trailing_semicolon(trigger.body)));

        // O MySQL permite UMA trigger por combinacao (momento, evento,
        // tabela) ate' a versao 5.7. Do 8.0 em diante aceita varias, mas a
        // ordem entre elas precisa ser declarada com FOLLOWS/PRECEDES.
        script.warnings.push_back(
            "MySQL before 8.0 allows only one trigger per timing and event "
            "on the same table");
        return script;
    }

    if (is_anywhere()) {
        // O corpo E' o codigo, num bloco BEGIN ... END; a linha nova e a
        // antiga sao lidas pelos apelidos de REFERENCING.
        std::string body(strip_trailing_semicolon(trigger.body));
        std::string head = body.substr(0, 5);
        std::transform(head.begin(), head.end(), head.begin(), [](char c) {
            return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        });
        if (head != "BEGIN") body = "BEGIN\n    " + body + ";\nEND";

        std::string referencing;
        if (trigger.event.find("INSERT") != std::string::npos ||
            trigger.event.find("UPDATE") != std::string::npos) {
            referencing = " NEW AS new_row";
        }
        if (trigger.event.find("DELETE") != std::string::npos ||
            trigger.event.find("UPDATE") != std::string::npos) {
            referencing = " OLD AS old_row" + referencing;
        }
        script.statements.push_back(
            "CREATE TRIGGER " + quote_if_needed(trigger.name) + " " + trigger.timing + " " +
            trigger.event + " ON " + qualified_name(schema, trigger.table) +
            "\nREFERENCING" + referencing + "\nFOR EACH ROW\n" + body);
        script.warnings.push_back(
            "the row values are read as new_row.<column> and old_row.<column>");
        return script;
    }

    if (is_mssql()) {
        // No T-SQL o corpo E' o codigo, o gatilho dispara por COMANDO (nao ha'
        // FOR EACH ROW) e so' existem AFTER e INSTEAD OF.
        if (trigger.timing == "BEFORE") {
            script.error = "SQL Server has no BEFORE triggers: use AFTER or INSTEAD OF";
            return script;
        }
        script.statements.push_back(
            "CREATE TRIGGER " + qualified_name(schema, trigger.name) + " ON " +
            qualified_name(schema, trigger.table) + "\n" + trigger.timing + " " +
            trigger.event + "\nAS\n" +
            std::string(strip_trailing_semicolon(trigger.body)));
        script.warnings.push_back(
            "SQL Server triggers fire once per statement: read the changed rows "
            "from the inserted and deleted tables");
        return script;
    }

    // PostgreSQL: a trigger chama uma FUNCAO, que precisa existir antes. O
    // corpo aqui e' o nome da funcao, nao codigo -- e dizer isso evita que o
    // usuario cole um bloco PL/pgSQL que o servidor recusa.
    script.statements.push_back(
        "CREATE TRIGGER " + quote_if_needed(trigger.name) + " " +
        trigger.timing + " " + trigger.event + " ON " +
        qualified_name(schema, trigger.table) +
        "\n  FOR EACH ROW EXECUTE FUNCTION " +
        std::string(strip_trailing_semicolon(trigger.body)));

    script.warnings.push_back(
        "in PostgreSQL the trigger calls a FUNCTION that must already exist; "
        "the body here is the function name, not code");

    return script;
}

} // namespace otter::db
