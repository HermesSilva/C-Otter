#include "db/sqlanywhere_object.hpp"

#include "db/catalog_sqlanywhere.hpp"   // sqlanywhere_quote, _literal, _table_filter

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

std::string quote(std::string_view name) { return sqlanywhere_quote(name); }
std::string literal(std::string_view text) { return sqlanywhere_literal(text); }

std::string full_name(const ObjectRef& ref) {
    return ref.schema.empty() ? quote(ref.name) : quote(ref.schema) + "." + quote(ref.name);
}

std::string table_of(const ObjectRef& ref) {
    return ref.schema.empty() ? quote(ref.parent)
                              : quote(ref.schema) + "." + quote(ref.parent);
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

// Um numero inteiro com sinal: os limites de uma sequence vem de um campo de
// texto e entram no comando sem aspas.
bool is_integer(std::string_view text) {
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) text.remove_prefix(1);
    return only_digits(text);
}

// A palavra do DROP para o tipo. Funcao e procedure tem comandos proprios; o
// COMMENT usa PROCEDURE para as duas.
const char* keyword(ObjectType type) {
    switch (type) {
        case ObjectType::table:             return "TABLE";
        case ObjectType::view:              return "VIEW";
        case ObjectType::materialized_view: return "MATERIALIZED VIEW";
        case ObjectType::procedure:         return "PROCEDURE";
        case ObjectType::function:          return "FUNCTION";
        case ObjectType::sequence:          return "SEQUENCE";
        case ObjectType::event:             return "EVENT";
        case ObjectType::data_type:         return "DOMAIN";
        default:                            return nullptr;
    }
}

// O dono e a tabela, como condicao sobre o apelido `t` de SYS.SYSTAB.
std::string table_filter(const ObjectRef& ref, std::string_view table) {
    return sqlanywhere_table_filter("t", ref.schema, table);
}

std::string owner_id(const ObjectRef& ref) {
    return "user_id(" + literal(ref.schema) + ")";
}

// O comentario do objeto cujo object_id e' `expression`.
std::string remark(const std::string& expression) {
    return "(SELECT rk.remarks FROM SYS.SYSREMARK rk WHERE rk.object_id = " + expression + ")";
}

std::string created(const std::string& expression) {
    return "(SELECT ob.creation_time FROM SYS.SYSOBJECT ob WHERE ob.object_id = " +
           expression + ")";
}

} // namespace

// --- Nomes --------------------------------------------------------------------------

std::string sqlanywhere_object_name(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::schema:
        case ObjectType::role:
        case ObjectType::database:
        case ObjectType::event:
        case ObjectType::tablespace:
            return quote(ref.name);
        case ObjectType::column:
        case ObjectType::trigger:
        case ObjectType::index:
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return table_of(ref) + "." + quote(ref.name);
        default:
            return full_name(ref);
    }
}

// --- Consultas ----------------------------------------------------------------------

std::string sqlanywhere_properties_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::database:
            // O banco da conexao: so' ha' um.
            return "SELECT db_name() AS [Name], db_property('File') AS [File],"
                   "       db_property('PageSize') AS [Page size],"
                   "       CAST(db_property('FileSize') AS bigint)"
                   "         * CAST(db_property('PageSize') AS bigint) AS [Size],"
                   "       db_property('CharSet') AS [Character set],"
                   "       db_property('NcharCharSet') AS [NCHAR character set],"
                   "       db_property('Collation') AS [Collation],"
                   "       db_property('CaseSensitive') AS [Case sensitive],"
                   "       db_property('BlankPadding') AS [Blank padding],"
                   "       db_property('Encryption') AS [Encryption],"
                   "       db_property('LogName') AS [Transaction log],"
                   "       property('Name') AS [Server],"
                   "       property('ProductVersion') AS [Server version]";

        case ObjectType::schema:
        case ObjectType::role:
            // O dono dos objetos e' um usuario. Nunca o hash da senha.
            return "SELECT u.user_name AS [Name],"
                   "       CASE WHEN (u.user_type & 1) = 0 AND (u.user_type & 2) <> 0"
                   "            THEN 'user, role'"
                   "            WHEN (u.user_type & 1) = 0 THEN 'user' ELSE 'role' END AS [Kind],"
                   "       lp.login_policy_name AS [Login policy],"
                   "       u.last_login_time AS [Last login],"
                   "       u.password_creation_time AS [Password changed],"
                   "       (SELECT count(*) FROM SYS.SYSTAB t WHERE t.creator = u.user_id"
                   "           AND t.table_type IN (1, 2, 3, 21)) AS [Tables and views],"
                   "       (SELECT count(*) FROM SYS.SYSPROCEDURE p"
                   "         WHERE p.creator = u.user_id) AS [Routines],"
                   "       " + remark("u.object_id") + " AS [Comment]"
                   "  FROM SYS.SYSUSER u"
                   "  LEFT JOIN SYS.SYSLOGINPOLICY lp ON lp.login_policy_id = u.login_policy_id"
                   " WHERE u.user_name = " + literal(ref.name);

        case ObjectType::table:
            return "SELECT t.table_name AS [Name], u.user_name AS [Schema],"
                   "       t.table_type_str AS [Type], t.[count] AS [Row count],"
                   "       (CAST(t.table_page_count AS bigint) + t.ext_page_count)"
                   "         * CAST(db_property('PageSize') AS bigint) AS [Size],"
                   "       (SELECT d.dbspace_name FROM SYS.SYSDBSPACE d"
                   "         WHERE d.dbspace_id = t.dbspace_id) AS [Dbspace],"
                   "       (SELECT i.index_name FROM SYS.SYSIDX i"
                   "         WHERE i.table_id = t.table_id"
                   "           AND i.index_id = t.clustered_index_id) AS [Clustered index],"
                   "       " + created("t.object_id") + " AS [Created],"
                   "       t.last_modified_at AS [Data modified],"
                   "       " + remark("t.object_id") + " AS [Comment]"
                   "  FROM SYS.SYSTAB t"
                   "  JOIN SYS.SYSUSER u ON u.user_id = t.creator"
                   " WHERE " + table_filter(ref, ref.name) + " AND t.table_type IN (1, 3)";

        case ObjectType::view:
            // status: 1 = valida, 2 = invalida (um objeto de que depende mudou),
            // 4 = desabilitada.
            return "SELECT t.table_name AS [Name], u.user_name AS [Schema],"
                   "       CASE ob.status WHEN 1 THEN 'valid' WHEN 2 THEN 'invalid'"
                   "            WHEN 4 THEN 'disabled' ELSE '' END AS [Status],"
                   "       ob.creation_time AS [Created],"
                   "       " + remark("t.object_id") + " AS [Comment]"
                   "  FROM SYS.SYSTAB t"
                   "  JOIN SYS.SYSUSER u ON u.user_id = t.creator"
                   "  JOIN SYS.SYSOBJECT ob ON ob.object_id = t.object_id"
                   " WHERE " + table_filter(ref, ref.name) + " AND t.table_type = 21";

        case ObjectType::materialized_view:
            return "SELECT t.table_name AS [Name], u.user_name AS [Schema],"
                   "       CASE ob.status WHEN 1 THEN 'valid' WHEN 2 THEN 'invalid'"
                   "            WHEN 4 THEN 'disabled' ELSE '' END AS [Status],"
                   "       CASE v.mv_refresh_type WHEN 1 THEN 'manual' WHEN 2 THEN 'immediate'"
                   "            ELSE '' END AS [Refresh type],"
                   "       v.mv_last_refreshed_at AS [Last refreshed],"
                   "       v.mv_known_stale_at AS [Stale since],"
                   "       t.[count] AS [Row count],"
                   "       (CAST(t.table_page_count AS bigint) + t.ext_page_count)"
                   "         * CAST(db_property('PageSize') AS bigint) AS [Size],"
                   "       ob.creation_time AS [Created],"
                   "       " + remark("t.object_id") + " AS [Comment]"
                   "  FROM SYS.SYSTAB t"
                   "  JOIN SYS.SYSUSER u ON u.user_id = t.creator"
                   "  JOIN SYS.SYSOBJECT ob ON ob.object_id = t.object_id"
                   "  JOIN SYS.SYSVIEW v ON v.view_object_id = t.object_id"
                   " WHERE " + table_filter(ref, ref.name) + " AND t.table_type = 2";

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT p.proc_name AS [Name], u.user_name AS [Schema],"
                   "       CASE WHEN EXISTS (SELECT 1 FROM SYS.SYSPROCPARM pp"
                   "                          WHERE pp.proc_id = p.proc_id AND pp.parm_type = 4)"
                   "            THEN 'function' ELSE 'procedure' END AS [Kind],"
                   "       (SELECT pp.base_type_str FROM SYS.SYSPROCPARM pp"
                   "         WHERE pp.proc_id = p.proc_id AND pp.parm_type = 4) AS [Returns],"
                   "       " + created("p.object_id") + " AS [Created],"
                   "       COALESCE(" + remark("p.object_id") + ", p.remarks) AS [Comment]"
                   "  FROM SYS.SYSPROCEDURE p"
                   "  JOIN SYS.SYSUSER u ON u.user_id = p.creator"
                   " WHERE p.creator = " + owner_id(ref) +
                   "   AND p.proc_name = " + literal(ref.name);

        case ObjectType::trigger:
            return "SELECT tr.trigger_name AS [Name], t.table_name AS [Table],"
                   "       CASE tr.trigger_time WHEN 'B' THEN 'BEFORE' WHEN 'I' THEN 'INSTEAD OF'"
                   "            WHEN 'K' THEN 'INSTEAD OF (statement)'"
                   "            WHEN 'S' THEN 'AFTER (statement)' WHEN 'R' THEN 'RESOLVE'"
                   "            ELSE 'AFTER' END AS [Timing],"
                   "       CASE tr.event WHEN 'A' THEN 'INSERT, DELETE'"
                   "            WHEN 'B' THEN 'INSERT, UPDATE' WHEN 'C' THEN 'UPDATE OF columns'"
                   "            WHEN 'D' THEN 'DELETE' WHEN 'E' THEN 'DELETE, UPDATE'"
                   "            WHEN 'I' THEN 'INSERT' WHEN 'U' THEN 'UPDATE'"
                   "            WHEN 'M' THEN 'INSERT, DELETE, UPDATE' ELSE '' END AS [Events],"
                   "       tr.trigger_order AS [Order],"
                   "       " + created("tr.object_id") + " AS [Created],"
                   "       COALESCE(" + remark("tr.object_id") + ", tr.remarks) AS [Comment]"
                   "  FROM SYS.SYSTRIGGER tr"
                   "  JOIN SYS.SYSTAB t ON t.table_id = tr.table_id"
                   " WHERE " + table_filter(ref, ref.parent) +
                   "   AND tr.trigger_name = " + literal(ref.name);

        case ObjectType::sequence:
            return "SELECT s.sequence_name AS [Name], u.user_name AS [Schema],"
                   "       s.resume_at AS [Next value], s.start_with AS [Start],"
                   "       s.increment_by AS [Increment], s.min_value AS [Minimum],"
                   "       s.max_value AS [Maximum], s.cache AS [Cache],"
                   "       CASE WHEN s.cycle = 1 THEN 'yes' ELSE 'no' END AS [Cycle],"
                   "       " + remark("s.object_id") + " AS [Comment]"
                   "  FROM SYS.SYSSEQUENCE s"
                   "  JOIN SYS.SYSUSER u ON u.user_id = s.owner"
                   " WHERE s.owner = " + owner_id(ref) +
                   "   AND s.sequence_name = " + literal(ref.name);

        case ObjectType::data_type:
            return "SELECT ut.type_name AS [Name], u.user_name AS [Schema],"
                   "       ut.base_type_str AS [Base type],"
                   "       CASE ut.nulls WHEN 'N' THEN 'no' WHEN 'Y' THEN 'yes'"
                   "            ELSE 'connection default' END AS [Nullable],"
                   "       ut.[default] AS [Default], ut.[check] AS [Check]"
                   "  FROM SYS.SYSUSERTYPE ut"
                   "  JOIN SYS.SYSUSER u ON u.user_id = ut.creator"
                   " WHERE ut.creator = " + owner_id(ref) +
                   "   AND ut.type_name = " + literal(ref.name);

        case ObjectType::event:
            return "SELECT e.event_name AS [Name], u.user_name AS [Schema],"
                   "       CASE WHEN e.enabled = 'Y' THEN 'yes' ELSE 'no' END AS [Enabled],"
                   "       et.name AS [System event], e.condition AS [Condition],"
                   "       CASE e.location WHEN 'C' THEN 'consolidated' WHEN 'R' THEN 'remote'"
                   "            ELSE 'all' END AS [Location],"
                   "       " + created("e.object_id") + " AS [Created],"
                   "       COALESCE(" + remark("e.object_id") + ", e.remarks) AS [Comment]"
                   "  FROM SYS.SYSEVENT e"
                   "  JOIN SYS.SYSUSER u ON u.user_id = e.creator"
                   "  LEFT JOIN SYS.SYSEVENTTYPE et ON et.event_type_id = e.event_type_id"
                   " WHERE e.event_name = " + literal(ref.name);

        default:
            return {};
    }
}

std::string sqlanywhere_definition_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
            // Quem roda isto precisa repor as opcoes da conexao depois: a
            // funcao troca `chained`, `date_format`, `quoted_identifier` e
            // outras, e nao as devolve (SqlAnywhereCatalog::load_table_definition
            // faz isso).
            return "SELECT sa_get_table_definition(" + literal(ref.schema) + ", " +
                   literal(ref.name) + ")";

        case ObjectType::view:
        case ObjectType::materialized_view:
            return "SELECT COALESCE(s.source, v.view_def)"
                   "  FROM SYS.SYSTAB t"
                   "  JOIN SYS.SYSVIEW v ON v.view_object_id = t.object_id"
                   "  LEFT JOIN SYS.SYSSOURCE s ON s.object_id = t.object_id"
                   " WHERE " + table_filter(ref, ref.name);

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT COALESCE(p.source, p.proc_defn) FROM SYS.SYSPROCEDURE p"
                   " WHERE p.creator = " + owner_id(ref) +
                   "   AND p.proc_name = " + literal(ref.name);

        case ObjectType::trigger:
            return "SELECT COALESCE(tr.source, tr.trigger_defn)"
                   "  FROM SYS.SYSTRIGGER tr"
                   "  JOIN SYS.SYSTAB t ON t.table_id = tr.table_id"
                   " WHERE " + table_filter(ref, ref.parent) +
                   "   AND tr.trigger_name = " + literal(ref.name);

        case ObjectType::event:
            // So' o CORPO: o catalogo nao guarda o CREATE EVENT. O comando
            // inteiro e' montado por SqlAnywhereCatalog::load_event_definition.
            return "SELECT COALESCE(e.source, e.action) FROM SYS.SYSEVENT e"
                   " WHERE e.event_name = " + literal(ref.name);

        case ObjectType::sequence:
            return "SELECT 'CREATE SEQUENCE \"' || u.user_name || '\".\"' || s.sequence_name"
                   "       || '\"\n    START WITH ' || s.start_with"
                   "       || '\n    INCREMENT BY ' || s.increment_by"
                   "       || '\n    MINVALUE ' || s.min_value"
                   "       || '\n    MAXVALUE ' || s.max_value"
                   "       || CASE WHEN s.cache > 0 THEN '\n    CACHE ' || s.cache"
                   "               ELSE '\n    NO CACHE' END"
                   "       || CASE WHEN s.cycle = 1 THEN '\n    CYCLE' ELSE '\n    NO CYCLE' END"
                   "       || ';'"
                   "  FROM SYS.SYSSEQUENCE s"
                   "  JOIN SYS.SYSUSER u ON u.user_id = s.owner"
                   " WHERE s.owner = " + owner_id(ref) +
                   "   AND s.sequence_name = " + literal(ref.name);

        case ObjectType::data_type:
            return "SELECT 'CREATE DOMAIN \"' || ut.type_name || '\" ' || ut.base_type_str"
                   "       || CASE ut.nulls WHEN 'N' THEN ' NOT NULL' WHEN 'Y' THEN ' NULL'"
                   "               ELSE '' END"
                   "       || CASE WHEN ut.[default] IS NOT NULL"
                   "               THEN ' DEFAULT ' || ut.[default] ELSE '' END"
                   "       || CASE WHEN ut.[check] IS NOT NULL THEN ' ' || ut.[check] ELSE '' END"
                   "       || ';'"
                   "  FROM SYS.SYSUSERTYPE ut"
                   " WHERE ut.creator = " + owner_id(ref) +
                   "   AND ut.type_name = " + literal(ref.name);

        default:
            return {};
    }
}

std::string sqlanywhere_permissions_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::materialized_view: {
            // SYSTABLEPERM guarda uma coluna por privilegio: 'Y' concedido,
            // 'G' com direito de repassar. Uma linha por privilegio aqui.
            static constexpr struct { const char* column; const char* name; } kColumns[] = {
                {"selectauth", "SELECT"},       {"insertauth", "INSERT"},
                {"updateauth", "UPDATE"},       {"deleteauth", "DELETE"},
                {"alterauth", "ALTER"},         {"referenceauth", "REFERENCES"},
                {"loadauth", "LOAD"},           {"truncateauth", "TRUNCATE"},
            };
            std::string sql;
            for (const auto& item : kColumns) {
                if (!sql.empty()) sql += " UNION ALL ";
                sql += std::string("SELECT g.user_name, '") + item.name + "',"
                       "       CASE WHEN p." + item.column + " = 'G' THEN 1 ELSE 0 END,"
                       "       gr.user_name"
                       "  FROM SYS.SYSTABLEPERM p"
                       "  JOIN SYS.SYSTAB t ON t.table_id = p.stable_id"
                       "  JOIN SYS.SYSUSER g ON g.user_id = p.grantee"
                       "  JOIN SYS.SYSUSER gr ON gr.user_id = p.grantor"
                       " WHERE " + table_filter(ref, ref.name) +
                       "   AND p." + item.column + " IN ('Y', 'G')";
            }
            return sql + " ORDER BY 1, 2";
        }

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT g.user_name, 'EXECUTE', 0, o.user_name"
                   "  FROM SYS.SYSPROCPERM pp"
                   "  JOIN SYS.SYSPROCEDURE p ON p.proc_id = pp.proc_id"
                   "  JOIN SYS.SYSUSER g ON g.user_id = pp.grantee"
                   "  JOIN SYS.SYSUSER o ON o.user_id = p.creator"
                   " WHERE p.creator = " + owner_id(ref) +
                   "   AND p.proc_name = " + literal(ref.name) + " ORDER BY 1";

        case ObjectType::sequence:
            return "SELECT g.user_name, 'USAGE', 0, gr.user_name"
                   "  FROM SYS.SYSSEQUENCEPERM sp"
                   "  JOIN SYS.SYSSEQUENCE s ON s.object_id = sp.sequence_id"
                   "  JOIN SYS.SYSUSER g ON g.user_id = sp.grantee"
                   "  JOIN SYS.SYSUSER gr ON gr.user_id = sp.grantor"
                   " WHERE s.owner = " + owner_id(ref) +
                   "   AND s.sequence_name = " + literal(ref.name) + " ORDER BY 1";

        default:
            return {};
    }
}

ObjectEdit sqlanywhere_editable_property(ObjectType type, std::string_view label) noexcept {
    if (label == "Name") {
        // So' o que tem comando de renomear: view, rotina, trigger e sequence
        // nao tem -- recriar e' o unico jeito.
        switch (type) {
            case ObjectType::table:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::constraint:
            case ObjectType::foreign_key:
                return ObjectEdit::name;
            default:
                return ObjectEdit::none;
        }
    }
    if (label == "Comment") {
        switch (type) {
            case ObjectType::table:
            case ObjectType::view:
            case ObjectType::materialized_view:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::foreign_key:
            case ObjectType::function:
            case ObjectType::procedure:
            case ObjectType::trigger:
            case ObjectType::sequence:
            case ObjectType::event:
            case ObjectType::schema:
            case ObjectType::role:
                return ObjectEdit::comment;
            default:
                return ObjectEdit::none;
        }
    }
    return ObjectEdit::none;
}

std::vector<std::string_view> sqlanywhere_privileges_for(ObjectType type) {
    switch (type) {
        case ObjectType::table:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "ALTER", "REFERENCES", "LOAD",
                    "TRUNCATE"};
        case ObjectType::view:
        case ObjectType::materialized_view:
            return {"SELECT", "INSERT", "UPDATE", "DELETE"};
        case ObjectType::function:
        case ObjectType::procedure:
            return {"EXECUTE"};
        case ObjectType::sequence:
            return {"USAGE"};
        default:
            return {};
    }
}

// --- Alteracoes ---------------------------------------------------------------------

AlterScript sqlanywhere_object_rename(const ObjectRef& ref, std::string_view new_name) {
    if (new_name.empty()) return refused("the new name is required");
    if (new_name == ref.name) return refused("the name is unchanged");

    switch (ref.type) {
        case ObjectType::table:
            return single("ALTER TABLE " + full_name(ref) + " RENAME " + quote(new_name));
        case ObjectType::column:
            return single("ALTER TABLE " + table_of(ref) + " RENAME " + quote(ref.name) +
                          " TO " + quote(new_name));
        case ObjectType::index:
            return single("ALTER INDEX " + quote(ref.name) + " ON " + table_of(ref) +
                          " RENAME TO " + quote(new_name));
        case ObjectType::foreign_key:
            // O nome de uma chave estrangeira e' o do indice dela.
            return single("ALTER INDEX FOREIGN KEY " + quote(ref.name) + " ON " +
                          table_of(ref) + " RENAME TO " + quote(new_name));
        case ObjectType::constraint:
            return single("ALTER TABLE " + table_of(ref) + " RENAME CONSTRAINT " +
                          quote(ref.name) + " TO " + quote(new_name));
        default:
            return refused("SQL Anywhere cannot rename this object type: drop it and "
                           "create it again with the new name");
    }
}

AlterScript sqlanywhere_object_comment(const ObjectRef& ref, std::string_view comment) {
    std::string target;
    switch (ref.type) {
        case ObjectType::table:             target = "TABLE " + full_name(ref); break;
        case ObjectType::view:              target = "VIEW " + full_name(ref); break;
        case ObjectType::materialized_view: target = "MATERIALIZED VIEW " + full_name(ref); break;
        case ObjectType::column:
            target = "COLUMN " + table_of(ref) + "." + quote(ref.name);
            break;
        case ObjectType::index:
            // dono.tabela.indice (nao "indice ON tabela").
            target = "INDEX " + table_of(ref) + "." + quote(ref.name);
            break;
        case ObjectType::foreign_key:
            target = "FOREIGN KEY " + table_of(ref) + "." + quote(ref.name);
            break;
        // COMMENT ON PROCEDURE vale para funcao tambem.
        case ObjectType::function:
        case ObjectType::procedure:         target = "PROCEDURE " + full_name(ref); break;
        case ObjectType::trigger:
            target = "TRIGGER " + table_of(ref) + "." + quote(ref.name);
            break;
        case ObjectType::sequence:          target = "SEQUENCE " + full_name(ref); break;
        case ObjectType::event:             target = "EVENT " + quote(ref.name); break;
        case ObjectType::schema:
        case ObjectType::role:              target = "USER " + quote(ref.name); break;
        default:
            return refused("SQL Anywhere has no comment for this object type");
    }
    return single("COMMENT ON " + target + " IS " +
                  (comment.empty() ? std::string("NULL") : literal(comment)));
}

AlterScript sqlanywhere_object_drop(const ObjectRef& ref) {
    AlterScript script;
    switch (ref.type) {
        case ObjectType::schema:
            script = single("DROP USER " + quote(ref.name));
            script.warnings.emplace_back(
                "it is refused while the user owns tables, views or procedures");
            break;
        case ObjectType::role:
            // Um papel puro sai por DROP ROLE; um usuario, por DROP USER. Quem
            // chama sabe qual e' pelo `parent` ("role").
            if (ref.parent == "role") {
                // WITH REVOKE: sem ele o DROP e' recusado enquanto o papel
                // estiver concedido a alguem.
                script = single("DROP ROLE " + quote(ref.name) + " WITH REVOKE");
                script.warnings.emplace_back(
                    "the role is revoked from every user and role that has it");
            } else {
                script = single("DROP USER " + quote(ref.name));
            }
            break;
        case ObjectType::column:
            script = single("ALTER TABLE " + table_of(ref) + " DROP " + quote(ref.name));
            break;
        case ObjectType::index:
            script = single("DROP INDEX " + table_of(ref) + "." + quote(ref.name));
            break;
        case ObjectType::constraint:
            script = single("ALTER TABLE " + table_of(ref) + " DROP CONSTRAINT " +
                            quote(ref.name));
            break;
        case ObjectType::foreign_key:
            script = single("ALTER TABLE " + table_of(ref) + " DROP FOREIGN KEY " +
                            quote(ref.name));
            break;
        case ObjectType::trigger:
            script = single("DROP TRIGGER " + table_of(ref) + "." + quote(ref.name));
            break;
        case ObjectType::event:
            script = single("DROP EVENT " + quote(ref.name));
            break;
        case ObjectType::data_type:
            // Um dominio nao e' qualificado pelo dono.
            script = single("DROP DOMAIN " + quote(ref.name));
            break;
        default:
            if (const char* word = keyword(ref.type)) {
                script = single(std::string("DROP ") + word + " " + full_name(ref));
            } else {
                return refused("this object type cannot be dropped on SQL Anywhere");
            }
            break;
    }
    script.destructive.push_back(0);
    return script;
}

namespace {

// " ON "dono"."objeto"", ou vazio com o erro preenchido.
std::string securable(const ObjectRef& ref, std::string_view privilege, std::string& error) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::materialized_view:
        case ObjectType::function:
        case ObjectType::procedure:
            return " ON " + full_name(ref);
        case ObjectType::sequence:
            if (upper(privilege) != "USAGE") {
                error = "the only privilege of a sequence is USAGE";
                return {};
            }
            return " ON SEQUENCE " + full_name(ref);
        default:
            error = "this object type has no privileges on SQL Anywhere";
            return {};
    }
}

bool known_privilege(std::string_view privilege) {
    return !privilege.empty() &&
           std::all_of(privilege.begin(), privilege.end(), [](char c) {
               return std::isalpha(static_cast<unsigned char>(c)) != 0;
           });
}

// Depois de espacos e comentarios (--, //, /* */): onde o comando comeca.
std::size_t skip_leading(std::string_view source) {
    std::size_t i = 0;
    for (;;) {
        while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i])) != 0) ++i;
        if (source.substr(i, 2) == "--" || source.substr(i, 2) == "//") {
            const std::size_t end = source.find('\n', i);
            if (end == std::string_view::npos) return source.size();
            i = end + 1;
        } else if (source.substr(i, 2) == "/*") {
            const std::size_t end = source.find("*/", i + 2);
            if (end == std::string_view::npos) return source.size();
            i = end + 2;
        } else {
            return i;
        }
    }
}

} // namespace

AlterScript sqlanywhere_grant(const ObjectRef& ref, std::string_view privilege,
                              std::string_view grantee, bool with_grant_option) {
    if (grantee.empty()) return refused("the user or role is required");
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = securable(ref, privilege, error);
    if (!error.empty()) return refused(error);

    // WITH GRANT OPTION so' existe para privilegio de tabela e de view.
    const bool relation = ref.type == ObjectType::table || ref.type == ObjectType::view ||
                          ref.type == ObjectType::materialized_view;
    if (with_grant_option && !relation) {
        return refused("WITH GRANT OPTION is only available for table and view privileges");
    }
    return single("GRANT " + upper(privilege) + target + " TO " + quote(grantee) +
                  (with_grant_option ? " WITH GRANT OPTION" : ""));
}

AlterScript sqlanywhere_revoke(const ObjectRef& ref, std::string_view privilege,
                               std::string_view grantee) {
    if (grantee.empty()) return refused("the user or role is required");
    if (!known_privilege(privilege)) return refused("invalid privilege name");

    std::string error;
    const std::string target = securable(ref, privilege, error);
    if (!error.empty()) return refused(error);

    return single("REVOKE " + upper(privilege) + target + " FROM " + quote(grantee));
}

bool sqlanywhere_source_editable(ObjectType type) noexcept {
    // A view materializada fica de fora: ALTER MATERIALIZED VIEW nao aceita
    // outra consulta -- trocar a definicao e' apagar e criar.
    return type == ObjectType::view || type == ObjectType::function ||
           type == ObjectType::procedure || type == ObjectType::trigger ||
           type == ObjectType::event;
}

AlterScript sqlanywhere_source_script(const ObjectRef& ref, std::string_view source) {
    if (!sqlanywhere_source_editable(ref.type)) {
        return refused("this object type is not saved from its source on SQL Anywhere");
    }
    while (!source.empty() && (std::isspace(static_cast<unsigned char>(source.back())) != 0 ||
                               source.back() == ';')) {
        source.remove_suffix(1);
    }
    if (source.empty()) return refused("the source is empty");

    // O texto guardado comeca com CREATE (depois de comentarios e espacos).
    // Trocado por ALTER, o objeto e' alterado no lugar e as permissoes ficam.
    const std::size_t i = skip_leading(source);

    std::string text(source);
    const std::string head = upper(source.substr(i, 18));
    if (head.starts_with("CREATE OR REPLACE") || head.starts_with("ALTER")) {
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

AlterScript sqlanywhere_create_user(const SqlAnywhereNewUser& user) {
    if (user.name.empty()) return refused("the user name is required");

    std::string sql = "CREATE USER " + quote(user.name);
    if (!user.password.empty()) sql += " IDENTIFIED BY " + literal(user.password);
    if (!user.login_policy.empty()) sql += " LOGIN POLICY " + quote(user.login_policy);

    AlterScript script = single(std::move(sql));
    if (user.password.empty()) {
        script.warnings.emplace_back(
            "without a password the user cannot connect: it can only own objects");
    }
    return script;
}

AlterScript sqlanywhere_create_role(std::string_view name) {
    if (name.empty()) return refused("the role name is required");
    return single("CREATE ROLE " + quote(name));
}

AlterScript sqlanywhere_user_password(std::string_view user, std::string_view password) {
    if (user.empty()) return refused("the user name is required");
    if (password.empty()) return refused("the password is required");
    return single("ALTER USER " + quote(user) + " IDENTIFIED BY " + literal(password));
}

AlterScript sqlanywhere_create_sequence(const SqlAnywhereNewSequence& sequence) {
    if (sequence.name.empty()) return refused("the sequence name is required");
    for (const std::string* number : {&sequence.start, &sequence.increment,
                                      &sequence.minimum, &sequence.maximum}) {
        if (!number->empty() && !is_integer(*number)) {
            return refused("'" + *number + "' is not an integer");
        }
    }

    std::string sql = "CREATE SEQUENCE " +
                      (sequence.schema.empty() ? quote(sequence.name)
                                               : quote(sequence.schema) + "." +
                                                     quote(sequence.name));
    if (!sequence.increment.empty()) sql += " INCREMENT BY " + sequence.increment;
    if (!sequence.start.empty())     sql += " START WITH " + sequence.start;
    if (!sequence.minimum.empty())   sql += " MINVALUE " + sequence.minimum;
    if (!sequence.maximum.empty())   sql += " MAXVALUE " + sequence.maximum;
    if (sequence.cycle)              sql += " CYCLE";
    return single(std::move(sql));
}

AlterScript sqlanywhere_create_domain(std::string_view name, std::string_view base_type,
                                      bool not_null, std::string_view default_value,
                                      std::string_view check) {
    if (name.empty()) return refused("the domain name is required");
    if (base_type.empty()) return refused("the base type is required");

    // O tipo, o padrao e a condicao sao SQL que o usuario digita: vao como
    // estao, e o comando passa pela janela de revisao antes de rodar.
    std::string sql = "CREATE DOMAIN " + quote(name) + " " + std::string(base_type);
    if (not_null) sql += " NOT NULL";
    if (!default_value.empty()) sql += " DEFAULT " + std::string(default_value);
    if (!check.empty()) sql += " CHECK (" + std::string(check) + ")";
    return single(std::move(sql));
}

std::string sqlanywhere_routine_template(std::string_view schema, std::string_view name,
                                         bool procedure) {
    const std::string full = quote(schema) + "." + quote(name);
    if (procedure) {
        return "CREATE PROCEDURE " + full + "()\nBEGIN\n\nEND";
    }
    return "CREATE FUNCTION " + full + "()\nRETURNS integer\nBEGIN\n    RETURN 0;\nEND";
}

std::string sqlanywhere_trigger_template(std::string_view schema, std::string_view table,
                                         std::string_view name, std::string_view timing,
                                         std::string_view event) {
    return "CREATE TRIGGER " + quote(name) + " " +
           std::string(timing.empty() ? "AFTER" : timing) + " " +
           std::string(event.empty() ? "INSERT" : event) + "\nON " + quote(schema) + "." +
           quote(table) + "\nREFERENCING NEW AS new_row\nFOR EACH ROW\nBEGIN\n\nEND";
}

std::string sqlanywhere_event_template(std::string_view name) {
    return "CREATE EVENT " + quote(name) +
           "\nSCHEDULE START TIME '00:00' EVERY 24 HOURS\nHANDLER\nBEGIN\n\nEND";
}

// --- Tools --------------------------------------------------------------------------

AlterScript sqlanywhere_table_tool(SqlAnywhereTableTool tool, std::string_view schema,
                                   std::string_view table) {
    const std::string full = quote(schema) + "." + quote(table);
    switch (tool) {
        case SqlAnywhereTableTool::validate:
            return single("VALIDATE TABLE " + full);
        case SqlAnywhereTableTool::reorganize:
            return single("REORGANIZE TABLE " + full);
        case SqlAnywhereTableTool::create_statistics:
            return single("CREATE STATISTICS " + full);
    }
    return refused("unknown tool");
}

AlterScript sqlanywhere_truncate(std::string_view schema, std::string_view table) {
    AlterScript script = single("TRUNCATE TABLE " + quote(schema) + "." + quote(table));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        "removes every row and commits: it cannot be rolled back, and delete triggers "
        "do not fire");
    return script;
}

AlterScript sqlanywhere_refresh_view(std::string_view schema, std::string_view view) {
    return single("REFRESH MATERIALIZED VIEW " + quote(schema) + "." + quote(view));
}

AlterScript sqlanywhere_view_enable(const ObjectRef& ref, bool enable) {
    if (ref.type != ObjectType::view && ref.type != ObjectType::materialized_view) {
        return refused("only views can be enabled or disabled");
    }
    AlterScript script = single(
        std::string(ref.type == ObjectType::view ? "ALTER VIEW " : "ALTER MATERIALIZED VIEW ") +
        full_name(ref) + (enable ? " ENABLE" : " DISABLE"));
    if (!enable) {
        script.warnings.emplace_back(
            "a disabled view cannot be queried; the views that depend on it are "
            "disabled too");
    }
    return script;
}

AlterScript sqlanywhere_event_enable(std::string_view name, bool enable) {
    if (name.empty()) return refused("the event name is required");
    return single("ALTER EVENT " + quote(name) + (enable ? " ENABLE" : " DISABLE"));
}

AlterScript sqlanywhere_event_trigger(std::string_view name) {
    if (name.empty()) return refused("the event name is required");
    AlterScript script = single("TRIGGER EVENT " + quote(name));
    script.warnings.emplace_back("runs the event handler now, on a connection of its own");
    return script;
}

AlterScript sqlanywhere_checkpoint() { return single("CHECKPOINT"); }

AlterScript sqlanywhere_validate_database() {
    AlterScript script = single("VALIDATE DATABASE");
    script.warnings.emplace_back(
        "reads every page of the database; run it when no one else is changing data, "
        "or it may report errors that are not there");
    return script;
}

AlterScript sqlanywhere_backup_database(std::string_view directory) {
    if (directory.empty()) return refused("the backup directory is required");
    AlterScript script = single("BACKUP DATABASE DIRECTORY " + literal(directory));
    script.warnings.emplace_back(
        "the files are written by the database server, on the SERVER machine: the "
        "directory must exist there and be writable by it");
    return script;
}

// --- Sessoes ------------------------------------------------------------------------

std::string_view sqlanywhere_sessions_query() noexcept {
    return "SELECT c.Number, c.Userid, c.Name, c.NodeAddr, c.CommLink,\n"
           "       c.ReqType, c.LastReqTime, c.BlockedOn, c.LockTable, c.UncommitOps,\n"
           "       connection_property('LoginTime', c.Number) AS LoginTime,\n"
           "       connection_property('LastStatement', c.Number) AS LastStatement\n"
           "  FROM sa_conn_info() c\n"
           " ORDER BY c.Number";
}

std::string_view sqlanywhere_locks_query() noexcept {
    return "SELECT l.conn_id, l.conn_name, l.user_id, l.creator, l.table_name,\n"
           "       l.table_type, l.lock_class, l.lock_duration, l.lock_type,\n"
           "       l.row_identifier\n"
           "  FROM sa_locks() l\n"
           " ORDER BY l.conn_id, l.table_name";
}

AlterScript sqlanywhere_session_kill(std::string_view id) {
    if (!only_digits(id)) return refused("select a connection row first");
    AlterScript script = single("DROP CONNECTION " + std::string(id));
    script.destructive.push_back(0);
    script.warnings.emplace_back(
        "closes the connection: its open transaction is rolled back");
    return script;
}

} // namespace otter::db
