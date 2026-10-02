#include "db/mssql_object.hpp"

#include "db/catalog_mssql.hpp"   // mssql_quote, mssql_literal, mssql_object_id

#include <algorithm>
#include <cctype>

namespace otter::db {
namespace {

AlterScript single(std::string statement) {
    AlterScript script;
    script.statements.push_back(std::move(statement));
    return script;
}

AlterScript refused(std::string reason) {
    AlterScript script;
    script.error = std::move(reason);
    return script;
}

std::string full_name(const ObjectRef& ref) {
    return ref.schema.empty() ? mssql_quote(ref.name)
                              : mssql_quote(ref.schema) + "." + mssql_quote(ref.name);
}

std::string table_of(const ObjectRef& ref) {
    return ref.schema.empty() ? mssql_quote(ref.parent)
                              : mssql_quote(ref.schema) + "." + mssql_quote(ref.parent);
}

bool only_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    });
}

std::string upper(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](char c) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    return out;
}

// O "tipo de nivel 1" das extended properties e do sp_rename.
const char* level1_type(ObjectType type) {
    switch (type) {
        case ObjectType::table:     return "TABLE";
        case ObjectType::view:      return "VIEW";
        case ObjectType::procedure: return "PROCEDURE";
        case ObjectType::function:  return "FUNCTION";
        case ObjectType::sequence:  return "SEQUENCE";
        case ObjectType::data_type: return "TYPE";
        default:                    return nullptr;
    }
}

// A palavra do DROP / ALTER para o tipo.
const char* keyword(ObjectType type) {
    switch (type) {
        case ObjectType::table:     return "TABLE";
        case ObjectType::view:      return "VIEW";
        case ObjectType::procedure: return "PROCEDURE";
        case ObjectType::function:  return "FUNCTION";
        case ObjectType::trigger:   return "TRIGGER";
        case ObjectType::sequence:  return "SEQUENCE";
        case ObjectType::data_type: return "TYPE";
        default:                    return nullptr;
    }
}

// MS_Description do objeto (ou de uma coluna dele).
std::string description_join(const std::string& object_id, const std::string& minor) {
    return "(SELECT CAST(ep.value AS nvarchar(4000)) FROM sys.extended_properties ep"
           " WHERE ep.class = 1 AND ep.major_id = " + object_id +
           " AND ep.minor_id = " + minor + " AND ep.name = N'MS_Description')";
}

} // namespace

// --- Nomes --------------------------------------------------------------------------

std::string mssql_object_name(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::database:
        case ObjectType::schema:
        case ObjectType::role:
            return mssql_quote(ref.name);
        case ObjectType::column:
        case ObjectType::index:
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return table_of(ref) + "." + mssql_quote(ref.name);
        default:
            return full_name(ref);
    }
}

// --- Consultas ----------------------------------------------------------------------

std::string mssql_properties_query(const ObjectRef& ref) {
    const std::string object = mssql_object_id(ref.schema, ref.name);

    switch (ref.type) {
        case ObjectType::database:
            return "SELECT d.name AS [Name], SUSER_SNAME(d.owner_sid) AS [Owner],"
                   "       d.collation_name AS [Collation],"
                   "       d.compatibility_level AS [Compatibility level],"
                   "       d.recovery_model_desc AS [Recovery model],"
                   "       d.state_desc AS [State], d.user_access_desc AS [User access],"
                   "       CASE WHEN d.is_read_only = 1 THEN 'yes' ELSE 'no' END AS [Read only],"
                   "       d.create_date AS [Created]"
                   "  FROM sys.databases d WHERE d.name = " + mssql_literal(ref.name);

        case ObjectType::schema:
            return "SELECT s.name AS [Name], USER_NAME(s.principal_id) AS [Owner],"
                   "       (SELECT COUNT(*) FROM sys.objects o WHERE o.schema_id = s.schema_id"
                   "           AND o.type IN ('U','V','P','FN','IF','TF')) AS [Objects]"
                   "  FROM sys.schemas s WHERE s.name = " + mssql_literal(ref.name);

        case ObjectType::table:
            return "SELECT o.name AS [Name], SCHEMA_NAME(o.schema_id) AS [Schema],"
                   "       (SELECT SUM(p.rows) FROM sys.partitions p WHERE p.object_id ="
                   "           o.object_id AND p.index_id IN (0, 1)) AS [Row count],"
                   "       (SELECT SUM(a.total_pages) * 8192 FROM sys.partitions p"
                   "          JOIN sys.allocation_units a ON a.container_id IN"
                   "               (p.hobt_id, p.partition_id)"
                   "         WHERE p.object_id = o.object_id) AS [Size],"
                   "       CASE WHEN EXISTS (SELECT 1 FROM sys.indexes i WHERE i.object_id ="
                   "           o.object_id AND i.index_id = 1) THEN 'clustered index'"
                   "            ELSE 'heap' END AS [Storage],"
                   "       o.create_date AS [Created], o.modify_date AS [Modified],"
                   "       " + description_join("o.object_id", "0") + " AS [Comment]"
                   "  FROM sys.objects o WHERE o.object_id = " + object + " AND o.type = 'U'";

        case ObjectType::view:
            return "SELECT o.name AS [Name], SCHEMA_NAME(o.schema_id) AS [Schema],"
                   "       CASE WHEN m.is_schema_bound = 1 THEN 'yes' ELSE 'no' END"
                   "           AS [Schema bound],"
                   "       CASE WHEN v.with_check_option = 1 THEN 'yes' ELSE 'no' END"
                   "           AS [Check option],"
                   "       o.create_date AS [Created], o.modify_date AS [Modified],"
                   "       " + description_join("o.object_id", "0") + " AS [Comment]"
                   "  FROM sys.objects o"
                   "  JOIN sys.views v ON v.object_id = o.object_id"
                   "  LEFT JOIN sys.sql_modules m ON m.object_id = o.object_id"
                   " WHERE o.object_id = " + object;

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT o.name AS [Name], SCHEMA_NAME(o.schema_id) AS [Schema],"
                   "       o.type_desc AS [Kind],"
                   "       (SELECT TYPE_NAME(pa.user_type_id) FROM sys.parameters pa"
                   "         WHERE pa.object_id = o.object_id AND pa.parameter_id = 0)"
                   "           AS [Returns],"
                   "       CASE WHEN m.is_schema_bound = 1 THEN 'yes' ELSE 'no' END"
                   "           AS [Schema bound],"
                   "       o.create_date AS [Created], o.modify_date AS [Modified],"
                   "       " + description_join("o.object_id", "0") + " AS [Comment]"
                   "  FROM sys.objects o"
                   "  LEFT JOIN sys.sql_modules m ON m.object_id = o.object_id"
                   " WHERE o.object_id = " + object;

        case ObjectType::trigger:
            return "SELECT t.name AS [Name], OBJECT_NAME(t.parent_id) AS [Table],"
                   "       CASE WHEN t.is_instead_of_trigger = 1 THEN 'INSTEAD OF'"
                   "            ELSE 'AFTER' END AS [Timing],"
                   "       CASE WHEN t.is_disabled = 1 THEN 'no' ELSE 'yes' END AS [Enabled],"
                   "       t.create_date AS [Created], t.modify_date AS [Modified]"
                   "  FROM sys.triggers t WHERE t.object_id = " + object;

        case ObjectType::sequence:
            return "SELECT s.name AS [Name], SCHEMA_NAME(s.schema_id) AS [Schema],"
                   "       TYPE_NAME(s.user_type_id) AS [Data type],"
                   "       CAST(s.current_value AS bigint) AS [Current value],"
                   "       CAST(s.start_value AS bigint) AS [Start],"
                   "       CAST(s.increment AS bigint) AS [Increment],"
                   "       CAST(s.minimum_value AS bigint) AS [Minimum],"
                   "       CAST(s.maximum_value AS bigint) AS [Maximum],"
                   "       CASE WHEN s.is_cycling = 1 THEN 'yes' ELSE 'no' END AS [Cycle],"
                   "       " + description_join("s.object_id", "0") + " AS [Comment]"
                   "  FROM sys.sequences s WHERE s.object_id = " + object;

        case ObjectType::role:
            // O login do servidor. Nunca o hash da senha.
            return "SELECT p.name AS [Name], p.type_desc AS [Type],"
                   "       p.default_database_name AS [Default database],"
                   "       p.default_language_name AS [Default language],"
                   "       CASE WHEN p.is_disabled = 1 THEN 'no' ELSE 'yes' END AS [Enabled],"
                   "       p.create_date AS [Created], p.modify_date AS [Modified]"
                   "  FROM sys.server_principals p WHERE p.name = " + mssql_literal(ref.name);

        default:
            return {};
    }
}

std::string mssql_definition_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::view:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::trigger:
            return "SELECT OBJECT_DEFINITION(" + mssql_object_id(ref.schema, ref.name) + ")";
        default:
            return {};
    }
}

std::string mssql_permissions_query(const ObjectRef& ref) {
    // class 1 = objeto, 3 = schema, 0 = banco. state: G = GRANT, W = GRANT
    // WITH GRANT OPTION; os DENY (D) ficam de fora -- nao sao concessao.
    const std::string head =
        "SELECT USER_NAME(dp.grantee_principal_id), dp.permission_name,"
        "       CASE WHEN dp.state = 'W' THEN 1 ELSE 0 END,"
        "       USER_NAME(dp.grantor_principal_id)"
        "  FROM sys.database_permissions dp"
        " WHERE dp.state IN ('G', 'W') AND ";
    const std::string tail = " ORDER BY 1, 2";

    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::sequence:
            return head + "dp.class = 1 AND dp.minor_id = 0 AND dp.major_id = " +
                   mssql_object_id(ref.schema, ref.name) + tail;
        case ObjectType::schema:
            return head + "dp.class = 3 AND dp.major_id = SCHEMA_ID(" +
                   mssql_literal(ref.name) + ")" + tail;
        case ObjectType::database:
            return head + "dp.class = 0" + tail;
        default:
            return {};
    }
}

ObjectEdit mssql_editable_property(ObjectType type, std::string_view label) noexcept {
    if (label == "Name") {
        switch (type) {
            case ObjectType::table:
            case ObjectType::view:
            case ObjectType::function:
            case ObjectType::procedure:
            case ObjectType::trigger:
            case ObjectType::sequence:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::constraint:
            case ObjectType::foreign_key:
            case ObjectType::database:
            case ObjectType::role:
                return ObjectEdit::name;
            default:
                return ObjectEdit::none;
        }
    }
    if (label == "Comment") {
        return level1_type(type) != nullptr ? ObjectEdit::comment : ObjectEdit::none;
    }
    if (label == "Schema") {
        return type == ObjectType::table || type == ObjectType::view ||
                       type == ObjectType::function || type == ObjectType::procedure ||
                       type == ObjectType::sequence
                   ? ObjectEdit::schema
                   : ObjectEdit::none;
    }
    return ObjectEdit::none;
}

std::vector<std::string_view> mssql_privileges_for(ObjectType type) {
    switch (type) {
        case ObjectType::table:
        case ObjectType::view:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "REFERENCES", "ALTER",
                    "CONTROL", "VIEW DEFINITION"};
        case ObjectType::procedure:
            return {"EXECUTE", "ALTER", "CONTROL", "VIEW DEFINITION"};
        case ObjectType::function:
            return {"EXECUTE", "SELECT", "REFERENCES", "ALTER", "CONTROL",
                    "VIEW DEFINITION"};
        case ObjectType::sequence:
            return {"UPDATE", "ALTER", "CONTROL", "VIEW DEFINITION"};
        case ObjectType::schema:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "EXECUTE", "REFERENCES",
                    "ALTER", "CONTROL", "VIEW DEFINITION"};
        case ObjectType::database:
            return {"CONNECT", "SELECT", "INSERT", "UPDATE", "DELETE", "EXECUTE",
                    "CREATE TABLE", "CREATE VIEW", "CREATE PROCEDURE", "CREATE FUNCTION",
                    "ALTER", "CONTROL", "VIEW DEFINITION", "BACKUP DATABASE"};
        default:
            return {};
    }
}

std::string mssql_table_ddl(std::string_view schema, const TableMeta& table,
                            const std::vector<MssqlIdentity>& identity) {
    const std::string full = mssql_quote(schema) + "." + mssql_quote(table.name);
    if (table.columns.empty()) {
        return "-- the columns of " + full + " could not be read\n";
    }

    std::size_t width = 0;
    for (const ColumnMeta& column : table.columns) {
        width = std::max(width, mssql_quote(column.name).size());
    }

    std::string out = "CREATE TABLE " + full + " (\n";
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        const ColumnMeta& column = table.columns[i];
        std::string name = mssql_quote(column.name);
        name.resize(width, ' ');
        out += "    " + name + " ";

        if (column.default_value.starts_with("AS ")) {
            // Coluna calculada: nao tem tipo nem NULL -- so' a expressao.
            out += column.default_value;
        } else {
            out += column.type_name;
            const auto found = std::find_if(
                identity.begin(), identity.end(),
                [&column](const MssqlIdentity& item) { return item.column == column.name; });
            if (found != identity.end()) {
                out += " " + found->clause;
            }
            out += column.nullable ? " NULL" : " NOT NULL";
            if (!column.default_value.empty() && column.default_value != "IDENTITY") {
                out += " DEFAULT " + column.default_value;
            }
        }
        if (i + 1 < table.columns.size() || !table.constraints.empty()) out += ",";
        out += "\n";
    }

    const auto bracket_list = [](const std::string& list) {
        std::string result;
        std::size_t start = 0;
        while (start <= list.size()) {
            const std::size_t comma = list.find(',', start);
            if (!result.empty()) result += ", ";
            result += mssql_quote(list.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return result;
    };

    for (std::size_t i = 0; i < table.constraints.size(); ++i) {
        const ConstraintMeta& constraint = table.constraints[i];
        out += "    CONSTRAINT " + mssql_quote(constraint.name) + " ";
        if (constraint.kind == ObjKind::primary_key) {
            out += "PRIMARY KEY (" + bracket_list(constraint.columns) + ")";
        } else if (constraint.kind == ObjKind::unique_key) {
            out += "UNIQUE (" + bracket_list(constraint.columns) + ")";
        } else {
            out += constraint.definition;   // CHECK (...), como o servidor guarda
        }
        if (i + 1 < table.constraints.size()) out += ",";
        out += "\n";
    }
    out += ");\n";

    // Indices: os que NAO nasceram de uma constraint (o da PK e os de UNIQUE
    // tem o nome dela, e repeti-los daria "ja' existe").
    bool wrote = false;
    for (const IndexMeta& index : table.indexes) {
        const bool from_constraint = std::any_of(
            table.constraints.begin(), table.constraints.end(),
            [&index](const ConstraintMeta& c) { return c.name == index.name; });
        if (from_constraint || index.definition.empty()) continue;
        if (!wrote) { out += "\n"; wrote = true; }
        out += index.definition + ";\n";
    }

    // Chaves estrangeiras depois, como ALTER: a tabela referenciada pode vir
    // adiante num script com varias.
    wrote = false;
    for (const ForeignKeyMeta& key : table.foreign_keys) {
        if (key.definition.empty()) continue;
        if (!wrote) { out += "\n"; wrote = true; }
        out += "ALTER TABLE " + full + " ADD CONSTRAINT " + mssql_quote(key.name) + " " +
               key.definition + ";\n";
    }

    if (!table.comment.empty()) {
        out += "\nEXEC sys.sp_addextendedproperty @name = N'MS_Description', @value = " +
               mssql_literal(table.comment) + ", @level0type = N'SCHEMA', @level0name = " +
               mssql_literal(schema) + ", @level1type = N'TABLE', @level1name = " +
               mssql_literal(table.name) + ";\n";
    }
    return out;
}

// --- Alteracoes ---------------------------------------------------------------------

AlterScript mssql_object_rename(const ObjectRef& ref, std::string_view new_name) {
    if (new_name.empty()) return refused("the new name is required");
    if (new_name == ref.name) return refused("the name is unchanged");

    const std::string fresh = mssql_literal(new_name);
    switch (ref.type) {
        case ObjectType::database:
            return single("ALTER DATABASE " + mssql_quote(ref.name) + " MODIFY NAME = " +
                          mssql_quote(new_name));
        case ObjectType::role:
            return single("ALTER LOGIN " + mssql_quote(ref.name) + " WITH NAME = " +
                          mssql_quote(new_name));
        case ObjectType::schema:
            return refused("SQL Server cannot rename a schema: create the new one and "
                           "transfer the objects");
        case ObjectType::column:
            // sp_rename recebe o nome ATUAL qualificado e citado, e o NOVO cru.
            return single("EXEC sp_rename " +
                          mssql_literal(table_of(ref) + "." + mssql_quote(ref.name)) + ", " +
                          fresh + ", N'COLUMN'");
        case ObjectType::index:
            return single("EXEC sp_rename " +
                          mssql_literal(table_of(ref) + "." + mssql_quote(ref.name)) + ", " +
                          fresh + ", N'INDEX'");
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            // A constraint mora no SCHEMA, nao na tabela.
            return single("EXEC sp_rename " +
                          mssql_literal(mssql_quote(ref.schema) + "." + mssql_quote(ref.name)) +
                          ", " + fresh + ", N'OBJECT'");
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::trigger:
        case ObjectType::sequence: {
            AlterScript script = single("EXEC sp_rename " + mssql_literal(full_name(ref)) +
                                        ", " + fresh + ", N'OBJECT'");
            if (ref.type != ObjectType::table && ref.type != ObjectType::sequence) {
                script.warnings.emplace_back(
                    "sp_rename does not change the name inside the stored definition: "
                    "scripting the object later shows the old CREATE text");
            }
            return script;
        }
        default:
            return refused("this object type cannot be renamed on SQL Server");
    }
}

AlterScript mssql_object_comment(const ObjectRef& ref, std::string_view comment) {
    // O caminho da propriedade: schema > objeto [> coluna].
    std::string path;
    std::string exists;
    if (ref.type == ObjectType::column) {
        path = ", @level0type = N'SCHEMA', @level0name = " + mssql_literal(ref.schema) +
               ", @level1type = N'TABLE', @level1name = " + mssql_literal(ref.parent) +
               ", @level2type = N'COLUMN', @level2name = " + mssql_literal(ref.name);
        exists = "ep.major_id = " + mssql_object_id(ref.schema, ref.parent) +
                 " AND ep.minor_id = COLUMNPROPERTY(" +
                 mssql_object_id(ref.schema, ref.parent) + ", " + mssql_literal(ref.name) +
                 ", 'ColumnId')";
    } else if (const char* level1 = level1_type(ref.type)) {
        path = ", @level0type = N'SCHEMA', @level0name = " + mssql_literal(ref.schema) +
               ", @level1type = N'" + level1 + "', @level1name = " + mssql_literal(ref.name);
        exists = "ep.major_id = " + mssql_object_id(ref.schema, ref.name) +
                 " AND ep.minor_id = 0";
    } else {
        return refused("SQL Server has no comment for this object type");
    }

    const std::string found =
        "EXISTS (SELECT 1 FROM sys.extended_properties ep WHERE ep.class = 1 AND " + exists +
        " AND ep.name = N'MS_Description')";

    // Num lote so': acrescenta, troca ou remove conforme o que ja' existe.
    if (comment.empty()) {
        return single("IF " + found +
                      " EXEC sys.sp_dropextendedproperty @name = N'MS_Description'" + path);
    }
    const std::string value = ", @value = " + mssql_literal(comment);
    return single("IF " + found +
                  " EXEC sys.sp_updateextendedproperty @name = N'MS_Description'" + value +
                  path + " ELSE EXEC sys.sp_addextendedproperty @name = N'MS_Description'" +
                  value + path);
}

AlterScript mssql_object_schema(const ObjectRef& ref, std::string_view new_schema) {
    if (new_schema.empty()) return refused("the new schema is required");
    if (new_schema == ref.schema) return refused("the schema did not change");
    if (mssql_editable_property(ref.type, "Schema") == ObjectEdit::none) {
        return refused("this object type cannot be moved to another schema");
    }
    return single("ALTER SCHEMA " + mssql_quote(new_schema) + " TRANSFER " + full_name(ref));
}

AlterScript mssql_object_drop(const ObjectRef& ref) {
    AlterScript script;
    switch (ref.type) {
        case ObjectType::database:
            script = single("DROP DATABASE " + mssql_quote(ref.name));
            script.warnings.emplace_back(
                "deletes the database and its files; it fails while other sessions "
                "are using it");
            break;
        case ObjectType::schema:
            script = single("DROP SCHEMA " + mssql_quote(ref.name));
            break;
        case ObjectType::role:
            script = single("DROP LOGIN " + mssql_quote(ref.name));
            break;
        case ObjectType::column:
            script = single("ALTER TABLE " + table_of(ref) + " DROP COLUMN " +
                            mssql_quote(ref.name));
            break;
        case ObjectType::index:
            script = single("DROP INDEX " + mssql_quote(ref.name) + " ON " + table_of(ref));
            break;
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            script = single("ALTER TABLE " + table_of(ref) + " DROP CONSTRAINT " +
                            mssql_quote(ref.name));
            break;
        default:
            if (const char* word = keyword(ref.type)) {
                script = single(std::string("DROP ") + word + " " + full_name(ref));
            } else {
                return refused("this object type cannot be dropped on SQL Server");
            }
            break;
    }
    script.destructive.push_back(0);
    return script;
}

namespace {

// "OBJECT::[s].[t]", "SCHEMA::[s]"; vazio para o banco (a permissao e' dele).
std::string securable(const ObjectRef& ref, std::string& error) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::sequence:
            return " ON OBJECT::" + full_name(ref);
        case ObjectType::schema:
            return " ON SCHEMA::" + mssql_quote(ref.name);
        case ObjectType::database:
            return {};
        default:
            error = "this object type has no privileges on SQL Server";
            return {};
    }
}

bool known_privilege(std::string_view privilege) {
    return !privilege.empty() &&
           std::all_of(privilege.begin(), privilege.end(), [](char c) {
               return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == ' ';
           });
}

} // namespace

AlterScript mssql_grant(const ObjectRef& ref, std::string_view privilege,
                        std::string_view grantee, bool with_grant_option) {
    if (grantee.empty()) return refused("the role is required");
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = securable(ref, error);
    if (!error.empty()) return refused(error);

    return single("GRANT " + upper(privilege) + target + " TO " + mssql_quote(grantee) +
                  (with_grant_option ? " WITH GRANT OPTION" : ""));
}

AlterScript mssql_revoke(const ObjectRef& ref, std::string_view privilege,
                         std::string_view grantee) {
    if (grantee.empty()) return refused("the role is required");
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = securable(ref, error);
    if (!error.empty()) return refused(error);

    // CASCADE: sem ele o REVOKE de quem tinha WITH GRANT OPTION e' recusado.
    return single("REVOKE " + upper(privilege) + target + " FROM " + mssql_quote(grantee) +
                  " CASCADE");
}

bool mssql_source_editable(ObjectType type) noexcept {
    return type == ObjectType::view || type == ObjectType::function ||
           type == ObjectType::procedure || type == ObjectType::trigger;
}

AlterScript mssql_source_script(const ObjectRef& ref, std::string_view source) {
    if (!mssql_source_editable(ref.type)) {
        return refused("this object type is not saved from its source on SQL Server");
    }
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back())) != 0) {
        source.remove_suffix(1);
    }
    if (source.empty()) return refused("the source is empty");

    // O texto guardado comeca com CREATE (depois de comentarios e espacos).
    // Trocado por ALTER, o objeto e' alterado no lugar e as permissoes ficam.
    std::size_t i = 0;
    for (;;) {
        while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i])) != 0) ++i;
        if (source.substr(i, 2) == "--") {
            const std::size_t end = source.find('\n', i);
            if (end == std::string_view::npos) break;
            i = end + 1;
        } else if (source.substr(i, 2) == "/*") {
            const std::size_t end = source.find("*/", i + 2);
            if (end == std::string_view::npos) break;
            i = end + 2;
        } else {
            break;
        }
    }

    std::string text(source);
    const std::string head = upper(source.substr(i, 16));
    if (head.starts_with("CREATE OR ALTER") || head.starts_with("ALTER")) {
        return single(std::move(text));
    }
    if (head.starts_with("CREATE") && source.size() > i + 6 &&
        std::isspace(static_cast<unsigned char>(source[i + 6])) != 0) {
        text.replace(i, 6, "ALTER");
        return single(std::move(text));
    }
    return refused("the source must start with CREATE or ALTER");
}

// --- CREATE -------------------------------------------------------------------------

AlterScript mssql_create_database(std::string_view name, std::string_view collation) {
    if (name.empty()) return refused("the database name is required");
    const bool word = std::all_of(collation.begin(), collation.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    });
    if (!word) return refused("invalid collation name");

    std::string sql = "CREATE DATABASE " + mssql_quote(name);
    if (!collation.empty()) sql += " COLLATE " + std::string(collation);
    return single(std::move(sql));
}

AlterScript mssql_create_schema(std::string_view name, std::string_view owner) {
    if (name.empty()) return refused("the schema name is required");
    std::string sql = "CREATE SCHEMA " + mssql_quote(name);
    if (!owner.empty()) sql += " AUTHORIZATION " + mssql_quote(owner);
    return single(std::move(sql));
}

AlterScript mssql_create_login(const MssqlNewLogin& login) {
    if (login.name.empty()) return refused("the login name is required");
    if (login.password.empty()) return refused("the password is required");

    std::string sql = "CREATE LOGIN " + mssql_quote(login.name) + " WITH PASSWORD = " +
                      mssql_literal(login.password);
    if (!login.default_database.empty()) {
        sql += ", DEFAULT_DATABASE = " + mssql_quote(login.default_database);
    }
    return single(std::move(sql));
}

AlterScript mssql_login_password(std::string_view login, std::string_view password) {
    if (login.empty()) return refused("the login name is required");
    if (password.empty()) return refused("the password is required");
    return single("ALTER LOGIN " + mssql_quote(login) + " WITH PASSWORD = " +
                  mssql_literal(password));
}

AlterScript mssql_create_synonym(std::string_view schema, std::string_view name,
                                 std::string_view target) {
    if (name.empty()) return refused("the name is required");
    if (target.empty()) return refused("the target object is required");
    // O alvo e' um nome de ate' quatro partes digitado pelo usuario
    // (servidor.banco.schema.objeto): vai como esta'.
    return single("CREATE SYNONYM " + mssql_quote(schema) + "." + mssql_quote(name) +
                  " FOR " + std::string(target));
}

std::string mssql_routine_template(std::string_view schema, std::string_view name,
                                   bool procedure) {
    const std::string full = mssql_quote(schema) + "." + mssql_quote(name);
    if (procedure) {
        return "CREATE PROCEDURE " + full + "\nAS\nBEGIN\n    SET NOCOUNT ON;\n\nEND";
    }
    return "CREATE FUNCTION " + full + "()\nRETURNS int\nAS\nBEGIN\n    RETURN 0;\nEND";
}

std::string mssql_trigger_template(std::string_view schema, std::string_view table,
                                   std::string_view name, std::string_view timing,
                                   std::string_view event) {
    return "CREATE TRIGGER " + mssql_quote(schema) + "." + mssql_quote(name) + "\nON " +
           mssql_quote(schema) + "." + mssql_quote(table) + "\n" +
           std::string(timing.empty() ? "AFTER" : timing) + " " +
           std::string(event.empty() ? "INSERT" : event) +
           "\nAS\nBEGIN\n    SET NOCOUNT ON;\n\nEND";
}

// --- Tools --------------------------------------------------------------------------

AlterScript mssql_table_tool(MssqlTableTool tool, std::string_view schema,
                             std::string_view table) {
    const std::string full = mssql_quote(schema) + "." + mssql_quote(table);
    switch (tool) {
        case MssqlTableTool::update_statistics:
            return single("UPDATE STATISTICS " + full);
        case MssqlTableTool::rebuild_indexes:
            return single("ALTER INDEX ALL ON " + full + " REBUILD");
        case MssqlTableTool::reorganize_indexes:
            return single("ALTER INDEX ALL ON " + full + " REORGANIZE");
        case MssqlTableTool::check:
            // O nome vai como LITERAL: e' argumento do DBCC, nao identificador.
            return single("DBCC CHECKTABLE (" + mssql_literal(full) + ") WITH NO_INFOMSGS");
    }
    return refused("unknown tool");
}

AlterScript mssql_truncate(std::string_view schema, std::string_view table) {
    AlterScript script =
        single("TRUNCATE TABLE " + mssql_quote(schema) + "." + mssql_quote(table));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        "removes every row; it is refused when a foreign key references the table");
    return script;
}

AlterScript mssql_trigger_enable(const ObjectRef& ref, bool enable) {
    if (ref.parent.empty()) return refused("the table of the trigger is required");
    return single(std::string(enable ? "ENABLE" : "DISABLE") + " TRIGGER " + full_name(ref) +
                  " ON " + table_of(ref));
}

AlterScript mssql_backup_database(std::string_view database,
                                  const MssqlBackupOptions& options) {
    if (database.empty()) return refused("the database name is required");
    if (options.file.empty()) return refused("the backup file is required");

    std::string with;
    const auto add = [&with](const char* option) {
        with += with.empty() ? " WITH " : ", ";
        with += option;
    };
    if (options.copy_only)   add("COPY_ONLY");
    if (options.compression) add("COMPRESSION");
    add(options.overwrite ? "INIT" : "NOINIT");

    AlterScript script = single("BACKUP DATABASE " + mssql_quote(database) + " TO DISK = " +
                                mssql_literal(options.file) + with);
    script.warnings.emplace_back(
        "the file is written by the SQL Server service, on the SERVER machine: the "
        "path must exist there and be writable by its account");
    return script;
}

AlterScript mssql_restore_database(std::string_view database, std::string_view file,
                                   bool replace) {
    if (database.empty()) return refused("the database name is required");
    if (file.empty()) return refused("the backup file is required");

    AlterScript script = single("RESTORE DATABASE " + mssql_quote(database) + " FROM DISK = " +
                                mssql_literal(file) + (replace ? " WITH REPLACE" : ""));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        replace ? "REPLACE overwrites the existing database with the content of the backup"
                : "fails if the database already exists; the file is read on the SERVER");
    return script;
}

// --- Sessoes ------------------------------------------------------------------------

std::string_view mssql_sessions_query() noexcept {
    return "SELECT s.session_id, s.login_name, s.host_name, s.program_name,\n"
           "       DB_NAME(s.database_id) AS database_name, s.status,\n"
           "       r.command, r.blocking_session_id, r.wait_type,\n"
           "       s.cpu_time, s.memory_usage, s.login_time, s.last_request_start_time,\n"
           "       t.text AS sql_text\n"
           "  FROM sys.dm_exec_sessions s\n"
           "  LEFT JOIN sys.dm_exec_requests r ON r.session_id = s.session_id\n"
           " OUTER APPLY sys.dm_exec_sql_text(r.sql_handle) t\n"
           " WHERE s.is_user_process = 1\n"
           " ORDER BY s.session_id";
}

std::string_view mssql_locks_query() noexcept {
    return "SELECT r.session_id AS blocked_session_id, s.login_name AS blocked_login,\n"
           "       r.blocking_session_id, bs.login_name AS blocking_login,\n"
           "       r.wait_type, r.wait_time, bt.text AS blocked_statement\n"
           "  FROM sys.dm_exec_requests r\n"
           "  JOIN sys.dm_exec_sessions s ON s.session_id = r.session_id\n"
           "  LEFT JOIN sys.dm_exec_sessions bs ON bs.session_id = r.blocking_session_id\n"
           " OUTER APPLY sys.dm_exec_sql_text(r.sql_handle) bt\n"
           " WHERE r.blocking_session_id <> 0";
}

AlterScript mssql_session_kill(std::string_view id) {
    if (!only_digits(id)) return refused("select a session row first");
    AlterScript script = single("KILL " + std::string(id));
    script.destructive.push_back(0);
    script.warnings.emplace_back("closes the session: its open transaction is rolled back");
    return script;
}

} // namespace otter::db
