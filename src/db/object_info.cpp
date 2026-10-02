#include "db/object_info.hpp"

#include "db/ddl.hpp"
#include "db/mssql_object.hpp"
#include "db/sqlanywhere_object.hpp"
#include "db/mysql_object.hpp"

#include <algorithm>

namespace otter::db {
namespace {

// Literal de texto para as consultas ao catalogo. Sempre no padrao SQL: estas
// consultas so' rodam no PostgreSQL, qualquer que seja o dialeto corrente.
std::string lit(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += "''";
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

// Identificador SEMPRE entre aspas duplas, para montar um regclass: sem elas
// o servidor rebaixa "TIDxAcao" para minusculas e nao acha a tabela.
std::string ident(std::string_view name) {
    std::string out = "\"";
    for (const char c : name) {
        if (c == '"') out += "\"\"";
        else          out.push_back(c);
    }
    out.push_back('"');
    return out;
}

// O texto que, convertido para regclass, resolve para a relacao. Vai como
// LITERAL na consulta: `'"s"."t"'::regclass`.
std::string regclass(std::string_view schema, std::string_view name) {
    const std::string qualified =
        schema.empty() ? ident(name) : ident(schema) + "." + ident(name);
    return lit(qualified) + "::regclass";
}

// O mesmo para rotina: `'"s"."f"(integer, text)'::regprocedure`.
std::string regprocedure(const ObjectRef& ref) {
    const std::string qualified =
        (ref.schema.empty() ? std::string{} : ident(ref.schema) + ".") +
        ident(ref.name) + "(" + ref.signature + ")";
    return lit(qualified) + "::regprocedure";
}

bool is_relation(ObjectType type) noexcept {
    return type == ObjectType::table || type == ObjectType::view ||
           type == ObjectType::materialized_view ||
           type == ObjectType::foreign_table || type == ObjectType::sequence ||
           type == ObjectType::index;
}

bool is_routine(ObjectType type) noexcept {
    return type == ObjectType::function || type == ObjectType::procedure ||
           type == ObjectType::aggregate;
}

// "sim/nao" de um booleano do catalogo, para a coluna de valor.
std::string yes_no(std::string_view expression) {
    return "CASE WHEN " + std::string(expression) + " THEN 'yes' ELSE 'no' END";
}

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

// "ON tabela" dos objetos que pertencem a uma tabela.
std::string on_table(const ObjectRef& ref) {
    return " ON " + qualified_name(ref.schema, ref.parent);
}

} // namespace

// O dialeto corrente e' o do MySQL? Os geradores genericos abaixo desviam
// para db/mysql_object.cpp: a tela chama o mesmo nome nos dois SGBDs.
static bool on_mysql() noexcept { return sql_dialect() == QuoteStyle::backticks; }
static bool on_mssql() noexcept { return sql_dialect() == QuoteStyle::brackets; }
static bool on_anywhere() noexcept { return sql_dialect() == QuoteStyle::anywhere; }

std::string pg_regclass(std::string_view schema, std::string_view name) {
    return regclass(schema, name);
}

std::string_view to_string(ObjectType type) noexcept {
    switch (type) {
        case ObjectType::database:             return "database";
        case ObjectType::schema:               return "schema";
        case ObjectType::table:                return "table";
        case ObjectType::view:                 return "view";
        case ObjectType::materialized_view:    return "materialized view";
        case ObjectType::foreign_table:        return "foreign table";
        case ObjectType::column:               return "column";
        case ObjectType::index:                return "index";
        case ObjectType::constraint:           return "constraint";
        case ObjectType::foreign_key:          return "foreign key";
        case ObjectType::trigger:              return "trigger";
        case ObjectType::rule:                 return "rule";
        case ObjectType::policy:               return "policy";
        case ObjectType::sequence:             return "sequence";
        case ObjectType::function:             return "function";
        case ObjectType::procedure:            return "procedure";
        case ObjectType::aggregate:            return "aggregate";
        case ObjectType::data_type:            return "data type";
        case ObjectType::role:                 return "role";
        case ObjectType::extension:            return "extension";
        case ObjectType::tablespace:           return "tablespace";
        case ObjectType::event_trigger:        return "event trigger";
        case ObjectType::foreign_server:       return "foreign server";
        case ObjectType::foreign_data_wrapper: return "foreign data wrapper";
        case ObjectType::user_mapping:         return "user mapping";
        case ObjectType::language:             return "language";
        case ObjectType::event:                return "event";
    }
    return "object";
}

std::string_view sql_keyword(ObjectType type) noexcept {
    switch (type) {
        case ObjectType::database:             return "DATABASE";
        case ObjectType::schema:               return "SCHEMA";
        case ObjectType::table:                return "TABLE";
        case ObjectType::view:                 return "VIEW";
        case ObjectType::materialized_view:    return "MATERIALIZED VIEW";
        case ObjectType::foreign_table:        return "FOREIGN TABLE";
        case ObjectType::column:               return "COLUMN";
        case ObjectType::index:                return "INDEX";
        case ObjectType::constraint:           return "CONSTRAINT";
        case ObjectType::foreign_key:          return "CONSTRAINT";
        case ObjectType::trigger:              return "TRIGGER";
        case ObjectType::rule:                 return "RULE";
        case ObjectType::policy:               return "POLICY";
        case ObjectType::sequence:             return "SEQUENCE";
        case ObjectType::function:             return "FUNCTION";
        case ObjectType::procedure:            return "PROCEDURE";
        case ObjectType::aggregate:            return "AGGREGATE";
        case ObjectType::data_type:            return "TYPE";
        case ObjectType::role:                 return "ROLE";
        case ObjectType::extension:            return "EXTENSION";
        case ObjectType::tablespace:           return "TABLESPACE";
        case ObjectType::event_trigger:        return "EVENT TRIGGER";
        case ObjectType::foreign_server:       return "SERVER";
        case ObjectType::foreign_data_wrapper: return "FOREIGN DATA WRAPPER";
        case ObjectType::user_mapping:         return "USER MAPPING";
        case ObjectType::language:             return "LANGUAGE";
        case ObjectType::event:                return "EVENT";
    }
    return "";
}

std::string ObjectRef::key() const {
    std::string out = std::to_string(static_cast<int>(type));
    for (const std::string* part : {&schema, &parent, &name, &signature}) {
        out.push_back('\x1f');
        out += *part;
    }
    return out;
}

std::string ObjectRef::title() const {
    if (is_routine(type)) return name + "(" + signature + ")";
    if (type == ObjectType::user_mapping) return name + " @ " + parent;
    return name;
}

// --- Propriedades ------------------------------------------------------------------

std::string pg_properties_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::foreign_table:
        case ObjectType::view:
        case ObjectType::materialized_view:
            // pg_total_relation_size de uma view e' zero, nao erro.
            return "SELECT c.relname AS \"Name\","
                   "       n.nspname AS \"Schema\","
                   "       c.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(c.relowner) AS \"Owner\","
                   "       COALESCE(t.spcname, '') AS \"Tablespace\","
                   "       CASE WHEN c.reltuples < 0 THEN ''"
                   "            ELSE c.reltuples::bigint::text END AS \"Row count estimate\","
                   "       pg_size_pretty(pg_total_relation_size(c.oid)) AS \"Total size\","
                   "       pg_size_pretty(pg_relation_size(c.oid)) AS \"Data size\","
                   "       CASE c.relpersistence WHEN 'u' THEN 'unlogged'"
                   "                             WHEN 't' THEN 'temporary'"
                   "                             ELSE 'permanent' END AS \"Persistence\","
                   "       " + yes_no("c.relrowsecurity") + " AS \"Row-level security\","
                   "       COALESCE(pg_get_partkeydef(c.oid), '') AS \"Partition by\","
                   "       " + yes_no("c.relispartition") + " AS \"Is partition\","
                   "       COALESCE(array_to_string(c.reloptions, ', '), '') AS \"Options\","
                   "       COALESCE(obj_description(c.oid, 'pg_class'), '') AS \"Comment\""
                   "  FROM pg_class c"
                   "  JOIN pg_namespace n ON n.oid = c.relnamespace"
                   "  LEFT JOIN pg_tablespace t ON t.oid = c.reltablespace"
                   " WHERE c.oid = " + regclass(ref.schema, ref.name);

        case ObjectType::sequence:
            return "SELECT c.relname AS \"Name\","
                   "       n.nspname AS \"Schema\","
                   "       c.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(c.relowner) AS \"Owner\","
                   "       format_type(s.seqtypid, NULL) AS \"Data type\","
                   "       s.seqstart::text AS \"Start\","
                   "       s.seqincrement::text AS \"Increment\","
                   "       s.seqmin::text AS \"Minimum\","
                   "       s.seqmax::text AS \"Maximum\","
                   "       s.seqcache::text AS \"Cache\","
                   "       " + yes_no("s.seqcycle") + " AS \"Cycle\","
                   "       COALESCE((SELECT d.refobjid::regclass::text || '.' || a.attname"
                   "                   FROM pg_depend d"
                   "                   JOIN pg_attribute a ON a.attrelid = d.refobjid"
                   "                                      AND a.attnum = d.refobjsubid"
                   "                  WHERE d.objid = c.oid AND d.deptype IN ('a', 'i')"
                   "                    AND d.classid = 'pg_class'::regclass"
                   "                  LIMIT 1), '') AS \"Owned by\","
                   "       COALESCE(obj_description(c.oid, 'pg_class'), '') AS \"Comment\""
                   "  FROM pg_class c"
                   "  JOIN pg_namespace n ON n.oid = c.relnamespace"
                   "  JOIN pg_sequence s ON s.seqrelid = c.oid"
                   " WHERE c.oid = " + regclass(ref.schema, ref.name);

        case ObjectType::index:
            return "SELECT c.relname AS \"Name\","
                   "       n.nspname AS \"Schema\","
                   "       i.indrelid::regclass::text AS \"Table\","
                   "       am.amname AS \"Access method\","
                   "       " + yes_no("i.indisunique") + " AS \"Unique\","
                   "       " + yes_no("i.indisprimary") + " AS \"Primary\","
                   "       " + yes_no("i.indisvalid") + " AS \"Valid\","
                   "       " + yes_no("i.indisclustered") + " AS \"Clustered\","
                   "       COALESCE(pg_get_expr(i.indpred, i.indrelid), '') AS \"Predicate\","
                   "       COALESCE(t.spcname, '') AS \"Tablespace\","
                   "       pg_size_pretty(pg_relation_size(c.oid)) AS \"Size\","
                   "       COALESCE(obj_description(c.oid, 'pg_class'), '') AS \"Comment\""
                   "  FROM pg_class c"
                   "  JOIN pg_namespace n ON n.oid = c.relnamespace"
                   "  JOIN pg_index i ON i.indexrelid = c.oid"
                   "  JOIN pg_am am ON am.oid = c.relam"
                   "  LEFT JOIN pg_tablespace t ON t.oid = c.reltablespace"
                   " WHERE c.oid = " + regclass(ref.schema, ref.name);

        case ObjectType::column:
            return "SELECT a.attname AS \"Name\","
                   "       a.attrelid::regclass::text AS \"Table\","
                   "       a.attnum::text AS \"Position\","
                   "       format_type(a.atttypid, a.atttypmod) AS \"Data type\","
                   "       " + yes_no("a.attnotnull") + " AS \"Not null\","
                   "       COALESCE(pg_get_expr(d.adbin, d.adrelid), '') AS \"Default\","
                   "       CASE a.attidentity WHEN 'a' THEN 'always'"
                   "                          WHEN 'd' THEN 'by default'"
                   "                          ELSE '' END AS \"Identity\","
                   "       CASE a.attgenerated WHEN 's' THEN 'stored' ELSE '' END AS \"Generated\","
                   "       COALESCE((SELECT collname FROM pg_collation"
                   "                  WHERE oid = a.attcollation AND collname <> 'default'), '')"
                   "           AS \"Collation\","
                   "       COALESCE(col_description(a.attrelid, a.attnum), '') AS \"Comment\""
                   "  FROM pg_attribute a"
                   "  LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum"
                   " WHERE a.attrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND a.attname = " + lit(ref.name) + " AND NOT a.attisdropped";

        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return "SELECT con.conname AS \"Name\","
                   "       con.conrelid::regclass::text AS \"Table\","
                   "       CASE con.contype WHEN 'p' THEN 'PRIMARY KEY'"
                   "                        WHEN 'u' THEN 'UNIQUE'"
                   "                        WHEN 'c' THEN 'CHECK'"
                   "                        WHEN 'f' THEN 'FOREIGN KEY'"
                   "                        WHEN 'x' THEN 'EXCLUDE'"
                   "                        ELSE con.contype::text END AS \"Type\","
                   "       pg_get_constraintdef(con.oid) AS \"Definition\","
                   "       " + yes_no("con.condeferrable") + " AS \"Deferrable\","
                   "       " + yes_no("con.condeferred") + " AS \"Initially deferred\","
                   "       " + yes_no("con.convalidated") + " AS \"Validated\","
                   "       COALESCE(obj_description(con.oid, 'pg_constraint'), '') AS \"Comment\""
                   "  FROM pg_constraint con"
                   " WHERE con.conrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND con.conname = " + lit(ref.name);

        case ObjectType::trigger:
            return "SELECT tg.tgname AS \"Name\","
                   "       tg.tgrelid::regclass::text AS \"Table\","
                   "       tg.tgfoid::regprocedure::text AS \"Function\","
                   "       CASE tg.tgenabled WHEN 'D' THEN 'disabled'"
                   "                         WHEN 'R' THEN 'replica'"
                   "                         WHEN 'A' THEN 'always'"
                   "                         ELSE 'enabled' END AS \"State\","
                   "       CASE WHEN (tg.tgtype::int & 1) <> 0 THEN 'ROW'"
                   "            ELSE 'STATEMENT' END AS \"Level\","
                   "       pg_get_triggerdef(tg.oid) AS \"Definition\","
                   "       COALESCE(obj_description(tg.oid, 'pg_trigger'), '') AS \"Comment\""
                   "  FROM pg_trigger tg"
                   " WHERE tg.tgrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND tg.tgname = " + lit(ref.name) + " AND NOT tg.tgisinternal";

        case ObjectType::rule:
            return "SELECT r.rulename AS \"Name\","
                   "       r.ev_class::regclass::text AS \"Table\","
                   "       CASE r.ev_type WHEN '1' THEN 'SELECT' WHEN '2' THEN 'UPDATE'"
                   "                      WHEN '3' THEN 'INSERT' WHEN '4' THEN 'DELETE'"
                   "                      ELSE r.ev_type::text END AS \"Event\","
                   "       " + yes_no("r.is_instead") + " AS \"Instead\","
                   "       CASE r.ev_enabled WHEN 'D' THEN 'disabled' ELSE 'enabled' END"
                   "           AS \"State\","
                   "       COALESCE(obj_description(r.oid, 'pg_rewrite'), '') AS \"Comment\""
                   "  FROM pg_rewrite r"
                   " WHERE r.ev_class = " + regclass(ref.schema, ref.parent) +
                   "   AND r.rulename = " + lit(ref.name);

        case ObjectType::policy:
            return "SELECT p.polname AS \"Name\","
                   "       p.polrelid::regclass::text AS \"Table\","
                   "       CASE p.polcmd WHEN 'r' THEN 'SELECT' WHEN 'a' THEN 'INSERT'"
                   "                     WHEN 'w' THEN 'UPDATE' WHEN 'd' THEN 'DELETE'"
                   "                     ELSE 'ALL' END AS \"Command\","
                   "       CASE WHEN p.polpermissive THEN 'PERMISSIVE' ELSE 'RESTRICTIVE' END"
                   "           AS \"Type\","
                   "       CASE WHEN p.polroles = '{0}' THEN 'PUBLIC'"
                   "            ELSE (SELECT string_agg(rolname, ', ')"
                   "                    FROM pg_roles WHERE oid = ANY (p.polroles)) END"
                   "           AS \"Roles\","
                   "       COALESCE(pg_get_expr(p.polqual, p.polrelid), '') AS \"Using\","
                   "       COALESCE(pg_get_expr(p.polwithcheck, p.polrelid), '') AS \"With check\""
                   "  FROM pg_policy p"
                   " WHERE p.polrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND p.polname = " + lit(ref.name);

        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::aggregate:
            return "SELECT p.proname AS \"Name\","
                   "       n.nspname AS \"Schema\","
                   "       p.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(p.proowner) AS \"Owner\","
                   "       CASE p.prokind WHEN 'p' THEN 'procedure' WHEN 'a' THEN 'aggregate'"
                   "                      WHEN 'w' THEN 'window' ELSE 'function' END AS \"Kind\","
                   "       l.lanname AS \"Language\","
                   "       pg_get_function_arguments(p.oid) AS \"Arguments\","
                   "       COALESCE(pg_get_function_result(p.oid), '') AS \"Returns\","
                   "       CASE p.provolatile WHEN 'i' THEN 'immutable' WHEN 's' THEN 'stable'"
                   "                          ELSE 'volatile' END AS \"Volatility\","
                   "       " + yes_no("p.proisstrict") + " AS \"Strict\","
                   "       " + yes_no("p.prosecdef") + " AS \"Security definer\","
                   "       CASE p.proparallel WHEN 's' THEN 'safe' WHEN 'r' THEN 'restricted'"
                   "                          ELSE 'unsafe' END AS \"Parallel\","
                   "       p.procost::text AS \"Cost\","
                   "       COALESCE(obj_description(p.oid, 'pg_proc'), '') AS \"Comment\""
                   "  FROM pg_proc p"
                   "  JOIN pg_namespace n ON n.oid = p.pronamespace"
                   "  JOIN pg_language l ON l.oid = p.prolang"
                   " WHERE p.oid = " + regprocedure(ref);

        case ObjectType::schema:
            return "SELECT n.nspname AS \"Name\","
                   "       n.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(n.nspowner) AS \"Owner\","
                   "       (SELECT count(*)::text FROM pg_class c"
                   "         WHERE c.relnamespace = n.oid AND c.relkind IN ('r', 'p'))"
                   "           AS \"Tables\","
                   "       (SELECT count(*)::text FROM pg_class c"
                   "         WHERE c.relnamespace = n.oid AND c.relkind IN ('v', 'm'))"
                   "           AS \"Views\","
                   "       (SELECT count(*)::text FROM pg_proc p WHERE p.pronamespace = n.oid)"
                   "           AS \"Routines\","
                   "       COALESCE(obj_description(n.oid, 'pg_namespace'), '') AS \"Comment\""
                   "  FROM pg_namespace n"
                   " WHERE n.nspname = " + lit(ref.name);

        case ObjectType::database:
            return "SELECT d.datname AS \"Name\","
                   "       d.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(d.datdba) AS \"Owner\","
                   "       pg_encoding_to_char(d.encoding) AS \"Encoding\","
                   "       d.datcollate AS \"Collate\","
                   "       d.datctype AS \"Ctype\","
                   "       t.spcname AS \"Tablespace\","
                   "       d.datconnlimit::text AS \"Connection limit\","
                   "       " + yes_no("d.datistemplate") + " AS \"Template\","
                   "       " + yes_no("d.datallowconn") + " AS \"Allow connections\","
                   "       CASE WHEN has_database_privilege(d.oid, 'CONNECT')"
                   "            THEN pg_size_pretty(pg_database_size(d.oid)) ELSE '' END"
                   "           AS \"Size\","
                   "       COALESCE(shobj_description(d.oid, 'pg_database'), '') AS \"Comment\""
                   "  FROM pg_database d"
                   "  JOIN pg_tablespace t ON t.oid = d.dattablespace"
                   " WHERE d.datname = " + lit(ref.name);

        case ObjectType::role:
            return "SELECT r.rolname AS \"Name\","
                   "       r.oid::text AS \"Object ID\","
                   "       " + yes_no("r.rolcanlogin") + " AS \"Can login\","
                   "       " + yes_no("r.rolsuper") + " AS \"Superuser\","
                   "       " + yes_no("r.rolcreatedb") + " AS \"Create database\","
                   "       " + yes_no("r.rolcreaterole") + " AS \"Create role\","
                   "       " + yes_no("r.rolinherit") + " AS \"Inherit\","
                   "       " + yes_no("r.rolreplication") + " AS \"Replication\","
                   "       " + yes_no("r.rolbypassrls") + " AS \"Bypass RLS\","
                   "       r.rolconnlimit::text AS \"Connection limit\","
                   "       COALESCE(r.rolvaliduntil::text, '') AS \"Valid until\","
                   "       COALESCE(array_to_string(r.rolconfig, ', '), '') AS \"Settings\","
                   "       COALESCE(shobj_description(r.oid, 'pg_authid'), '') AS \"Comment\""
                   "  FROM pg_roles r"
                   " WHERE r.rolname = " + lit(ref.name);

        case ObjectType::extension:
            return "SELECT e.extname AS \"Name\","
                   "       e.extversion AS \"Version\","
                   "       n.nspname AS \"Schema\","
                   "       pg_get_userbyid(e.extowner) AS \"Owner\","
                   "       " + yes_no("e.extrelocatable") + " AS \"Relocatable\","
                   "       COALESCE((SELECT default_version FROM pg_available_extensions"
                   "                  WHERE name = e.extname), '') AS \"Available version\","
                   "       COALESCE(obj_description(e.oid, 'pg_extension'), '') AS \"Comment\""
                   "  FROM pg_extension e"
                   "  JOIN pg_namespace n ON n.oid = e.extnamespace"
                   " WHERE e.extname = " + lit(ref.name);

        case ObjectType::tablespace:
            return "SELECT t.spcname AS \"Name\","
                   "       t.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(t.spcowner) AS \"Owner\","
                   "       pg_tablespace_location(t.oid) AS \"Location\","
                   "       COALESCE(array_to_string(t.spcoptions, ', '), '') AS \"Options\","
                   "       CASE WHEN has_tablespace_privilege(t.oid, 'CREATE')"
                   "            THEN pg_size_pretty(pg_tablespace_size(t.oid)) ELSE '' END"
                   "           AS \"Size\","
                   "       COALESCE(shobj_description(t.oid, 'pg_tablespace'), '') AS \"Comment\""
                   "  FROM pg_tablespace t"
                   " WHERE t.spcname = " + lit(ref.name);

        case ObjectType::data_type:
            return "SELECT t.typname AS \"Name\","
                   "       n.nspname AS \"Schema\","
                   "       t.oid::text AS \"Object ID\","
                   "       pg_get_userbyid(t.typowner) AS \"Owner\","
                   "       CASE t.typtype WHEN 'e' THEN 'enum' WHEN 'c' THEN 'composite'"
                   "                      WHEN 'd' THEN 'domain' WHEN 'r' THEN 'range'"
                   "                      WHEN 'm' THEN 'multirange' WHEN 'p' THEN 'pseudo'"
                   "                      ELSE 'base' END AS \"Kind\","
                   "       CASE WHEN t.typtype = 'd' THEN format_type(t.typbasetype, t.typtypmod)"
                   "            ELSE '' END AS \"Base type\","
                   "       " + yes_no("t.typnotnull") + " AS \"Not null\","
                   "       COALESCE(t.typdefault, '') AS \"Default\","
                   "       t.typlen::text AS \"Length\","
                   "       COALESCE(obj_description(t.oid, 'pg_type'), '') AS \"Comment\""
                   "  FROM pg_type t"
                   "  JOIN pg_namespace n ON n.oid = t.typnamespace"
                   " WHERE n.nspname = " + lit(ref.schema) +
                   "   AND t.typname = " + lit(ref.name);

        case ObjectType::event_trigger:
            return "SELECT e.evtname AS \"Name\","
                   "       e.evtevent AS \"Event\","
                   "       e.evtfoid::regprocedure::text AS \"Function\","
                   "       pg_get_userbyid(e.evtowner) AS \"Owner\","
                   "       CASE e.evtenabled WHEN 'D' THEN 'disabled' WHEN 'R' THEN 'replica'"
                   "                         WHEN 'A' THEN 'always' ELSE 'enabled' END"
                   "           AS \"State\","
                   "       COALESCE(array_to_string(e.evttags, ', '), '') AS \"Tags\","
                   "       COALESCE(obj_description(e.oid, 'pg_event_trigger'), '') AS \"Comment\""
                   "  FROM pg_event_trigger e"
                   " WHERE e.evtname = " + lit(ref.name);

        case ObjectType::foreign_server:
            return "SELECT s.srvname AS \"Name\","
                   "       w.fdwname AS \"Foreign data wrapper\","
                   "       pg_get_userbyid(s.srvowner) AS \"Owner\","
                   "       COALESCE(s.srvtype, '') AS \"Type\","
                   "       COALESCE(s.srvversion, '') AS \"Version\","
                   "       COALESCE(array_to_string(s.srvoptions, ', '), '') AS \"Options\","
                   "       COALESCE(obj_description(s.oid, 'pg_foreign_server'), '') AS \"Comment\""
                   "  FROM pg_foreign_server s"
                   "  JOIN pg_foreign_data_wrapper w ON w.oid = s.srvfdw"
                   " WHERE s.srvname = " + lit(ref.name);

        case ObjectType::foreign_data_wrapper:
            return "SELECT w.fdwname AS \"Name\","
                   "       pg_get_userbyid(w.fdwowner) AS \"Owner\","
                   "       CASE WHEN w.fdwhandler = 0 THEN ''"
                   "            ELSE w.fdwhandler::regproc::text END AS \"Handler\","
                   "       CASE WHEN w.fdwvalidator = 0 THEN ''"
                   "            ELSE w.fdwvalidator::regproc::text END AS \"Validator\","
                   "       COALESCE(array_to_string(w.fdwoptions, ', '), '') AS \"Options\","
                   "       COALESCE(obj_description(w.oid, 'pg_foreign_data_wrapper'), '')"
                   "           AS \"Comment\""
                   "  FROM pg_foreign_data_wrapper w"
                   " WHERE w.fdwname = " + lit(ref.name);

        case ObjectType::user_mapping:
            // As OPCOES do mapeamento guardam a senha remota; pg_user_mappings
            // ja' as esconde de quem nao pode ve-las.
            return "SELECT m.usename AS \"User\","
                   "       m.srvname AS \"Server\","
                   "       COALESCE(array_to_string(m.umoptions, ', '), '') AS \"Options\""
                   "  FROM pg_user_mappings m"
                   " WHERE m.srvname = " + lit(ref.parent) +
                   "   AND m.usename = " + lit(ref.name);

        case ObjectType::language:
            return "SELECT l.lanname AS \"Name\","
                   "       pg_get_userbyid(l.lanowner) AS \"Owner\","
                   "       " + yes_no("l.lanpltrusted") + " AS \"Trusted\","
                   "       CASE WHEN l.lanplcallfoid = 0 THEN ''"
                   "            ELSE l.lanplcallfoid::regproc::text END AS \"Handler\","
                   "       COALESCE(obj_description(l.oid, 'pg_language'), '') AS \"Comment\""
                   "  FROM pg_language l"
                   " WHERE l.lanname = " + lit(ref.name);
    }
    return {};
}

// --- DDL ---------------------------------------------------------------------------

std::string pg_ddl_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::foreign_table:
            return {};   // montado no cliente: db/ddl.hpp

        case ObjectType::view:
            return "SELECT 'CREATE OR REPLACE VIEW ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   " || E' AS\\n' || pg_get_viewdef(" + regclass(ref.schema, ref.name) + ", true)";

        case ObjectType::materialized_view:
            return "SELECT 'CREATE MATERIALIZED VIEW ' || " +
                   lit(qualified_name(ref.schema, ref.name)) +
                   " || E' AS\\n' || rtrim(pg_get_viewdef(" + regclass(ref.schema, ref.name) +
                   ", true), ';') || E'\\nWITH ' ||"
                   " CASE WHEN c.relispopulated THEN '' ELSE 'NO ' END || 'DATA;'"
                   "  FROM pg_class c WHERE c.oid = " + regclass(ref.schema, ref.name);

        case ObjectType::sequence:
            return "SELECT 'CREATE SEQUENCE ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   " || E'\\n    AS ' || format_type(s.seqtypid, NULL) ||"
                   " E'\\n    INCREMENT BY ' || s.seqincrement ||"
                   " E'\\n    MINVALUE ' || s.seqmin ||"
                   " E'\\n    MAXVALUE ' || s.seqmax ||"
                   " E'\\n    START WITH ' || s.seqstart ||"
                   " E'\\n    CACHE ' || s.seqcache ||"
                   " CASE WHEN s.seqcycle THEN E'\\n    CYCLE' ELSE E'\\n    NO CYCLE' END || ';'"
                   "  FROM pg_sequence s WHERE s.seqrelid = " + regclass(ref.schema, ref.name);

        case ObjectType::index:
            return "SELECT pg_get_indexdef(" + regclass(ref.schema, ref.name) + ") || ';'";

        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return "SELECT 'ALTER TABLE ' || con.conrelid::regclass::text ||"
                   " E'\\n    ADD CONSTRAINT ' || quote_ident(con.conname) || ' ' ||"
                   " pg_get_constraintdef(con.oid) || ';'"
                   "  FROM pg_constraint con"
                   " WHERE con.conrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND con.conname = " + lit(ref.name);

        case ObjectType::trigger:
            return "SELECT pg_get_triggerdef(tg.oid, true) || ';'"
                   "  FROM pg_trigger tg"
                   " WHERE tg.tgrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND tg.tgname = " + lit(ref.name) + " AND NOT tg.tgisinternal";

        case ObjectType::rule:
            return "SELECT pg_get_ruledef(r.oid, true)"
                   "  FROM pg_rewrite r"
                   " WHERE r.ev_class = " + regclass(ref.schema, ref.parent) +
                   "   AND r.rulename = " + lit(ref.name);

        case ObjectType::policy:
            return "SELECT 'CREATE POLICY ' || quote_ident(p.polname) || ' ON ' ||"
                   " p.polrelid::regclass::text ||"
                   " E'\\n    AS ' || CASE WHEN p.polpermissive THEN 'PERMISSIVE'"
                   "                      ELSE 'RESTRICTIVE' END ||"
                   " E'\\n    FOR ' || CASE p.polcmd WHEN 'r' THEN 'SELECT' WHEN 'a' THEN 'INSERT'"
                   "                               WHEN 'w' THEN 'UPDATE' WHEN 'd' THEN 'DELETE'"
                   "                               ELSE 'ALL' END ||"
                   " E'\\n    TO ' || CASE WHEN p.polroles = '{0}' THEN 'PUBLIC'"
                   "       ELSE (SELECT string_agg(quote_ident(rolname), ', ')"
                   "               FROM pg_roles WHERE oid = ANY (p.polroles)) END ||"
                   " COALESCE(E'\\n    USING (' || pg_get_expr(p.polqual, p.polrelid) || ')', '') ||"
                   " COALESCE(E'\\n    WITH CHECK (' ||"
                   "          pg_get_expr(p.polwithcheck, p.polrelid) || ')', '') || ';'"
                   "  FROM pg_policy p"
                   " WHERE p.polrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND p.polname = " + lit(ref.name);

        case ObjectType::function:
        case ObjectType::procedure:
            return "SELECT pg_get_functiondef(" + regprocedure(ref) + ")";

        case ObjectType::aggregate:
            // pg_get_functiondef recusa agregado; o comando e' montado do
            // pg_aggregate.
            return "SELECT 'CREATE AGGREGATE ' || p.oid::regprocedure::text || E' (\\n'"
                   " || '    SFUNC = ' || a.aggtransfn::regproc::text"
                   " || E',\\n    STYPE = ' || format_type(a.aggtranstype, NULL)"
                   " || CASE WHEN a.aggfinalfn = 0 THEN ''"
                   "         ELSE E',\\n    FINALFUNC = ' || a.aggfinalfn::regproc::text END"
                   " || CASE WHEN a.agginitval IS NULL THEN ''"
                   "         ELSE E',\\n    INITCOND = ' || quote_literal(a.agginitval) END"
                   " || E'\\n);'"
                   "  FROM pg_proc p JOIN pg_aggregate a ON a.aggfnoid = p.oid"
                   " WHERE p.oid = " + regprocedure(ref);

        case ObjectType::schema:
            return "SELECT 'CREATE SCHEMA ' || quote_ident(n.nspname) ||"
                   " ' AUTHORIZATION ' || quote_ident(pg_get_userbyid(n.nspowner)) || ';'"
                   "  FROM pg_namespace n WHERE n.nspname = " + lit(ref.name);

        case ObjectType::database:
            return "SELECT 'CREATE DATABASE ' || quote_ident(d.datname) ||"
                   " E'\\n    WITH OWNER = ' || quote_ident(pg_get_userbyid(d.datdba)) ||"
                   " E'\\n    ENCODING = ' || quote_literal(pg_encoding_to_char(d.encoding)) ||"
                   " E'\\n    LC_COLLATE = ' || quote_literal(d.datcollate) ||"
                   " E'\\n    LC_CTYPE = ' || quote_literal(d.datctype) ||"
                   " E'\\n    TABLESPACE = ' || quote_ident(t.spcname) ||"
                   " E'\\n    CONNECTION LIMIT = ' || d.datconnlimit ||"
                   " CASE WHEN d.datistemplate THEN E'\\n    IS_TEMPLATE = true' ELSE '' END || ';'"
                   "  FROM pg_database d JOIN pg_tablespace t ON t.oid = d.dattablespace"
                   " WHERE d.datname = " + lit(ref.name);

        case ObjectType::role:
            // A senha NAO entra: pg_roles a esconde, e um DDL com o hash dela
            // copiado para um chamado seria um vazamento.
            return "SELECT 'CREATE ROLE ' || quote_ident(r.rolname) || ' WITH' ||"
                   " CASE WHEN r.rolcanlogin THEN E'\\n    LOGIN' ELSE E'\\n    NOLOGIN' END ||"
                   " CASE WHEN r.rolsuper THEN E'\\n    SUPERUSER' ELSE E'\\n    NOSUPERUSER' END ||"
                   " CASE WHEN r.rolcreatedb THEN E'\\n    CREATEDB' ELSE E'\\n    NOCREATEDB' END ||"
                   " CASE WHEN r.rolcreaterole THEN E'\\n    CREATEROLE'"
                   "      ELSE E'\\n    NOCREATEROLE' END ||"
                   " CASE WHEN r.rolinherit THEN E'\\n    INHERIT' ELSE E'\\n    NOINHERIT' END ||"
                   " CASE WHEN r.rolreplication THEN E'\\n    REPLICATION'"
                   "      ELSE E'\\n    NOREPLICATION' END ||"
                   " CASE WHEN r.rolbypassrls THEN E'\\n    BYPASSRLS' ELSE '' END ||"
                   " E'\\n    CONNECTION LIMIT ' || r.rolconnlimit ||"
                   " COALESCE(E'\\n    VALID UNTIL ' || quote_literal(r.rolvaliduntil::text), '')"
                   " || ';'"
                   "  FROM pg_roles r WHERE r.rolname = " + lit(ref.name);

        case ObjectType::extension:
            return "SELECT 'CREATE EXTENSION IF NOT EXISTS ' || quote_ident(e.extname) ||"
                   " E'\\n    SCHEMA ' || quote_ident(n.nspname) ||"
                   " E'\\n    VERSION ' || quote_literal(e.extversion) || ';'"
                   "  FROM pg_extension e JOIN pg_namespace n ON n.oid = e.extnamespace"
                   " WHERE e.extname = " + lit(ref.name);

        case ObjectType::tablespace:
            return "SELECT 'CREATE TABLESPACE ' || quote_ident(t.spcname) ||"
                   " E'\\n    OWNER ' || quote_ident(pg_get_userbyid(t.spcowner)) ||"
                   " E'\\n    LOCATION ' || quote_literal(pg_tablespace_location(t.oid)) || ';'"
                   "  FROM pg_tablespace t WHERE t.spcname = " + lit(ref.name);

        case ObjectType::data_type:
            return "SELECT CASE t.typtype"
                   " WHEN 'e' THEN 'CREATE TYPE ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   "   || E' AS ENUM (\\n    ' ||"
                   "   (SELECT string_agg(quote_literal(enumlabel), E',\\n    '"
                   "                      ORDER BY enumsortorder)"
                   "      FROM pg_enum WHERE enumtypid = t.oid) || E'\\n);'"
                   " WHEN 'd' THEN 'CREATE DOMAIN ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   "   || ' AS ' || format_type(t.typbasetype, t.typtypmod) ||"
                   "   CASE WHEN t.typnotnull THEN ' NOT NULL' ELSE '' END ||"
                   "   COALESCE(' DEFAULT ' || t.typdefault, '') ||"
                   "   COALESCE((SELECT string_agg(E'\\n    CONSTRAINT ' || quote_ident(conname)"
                   "                               || ' ' || pg_get_constraintdef(oid), '')"
                   "               FROM pg_constraint WHERE contypid = t.oid), '') || ';'"
                   " WHEN 'c' THEN 'CREATE TYPE ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   "   || E' AS (\\n    ' ||"
                   "   COALESCE((SELECT string_agg(quote_ident(a.attname) || ' ' ||"
                   "                     format_type(a.atttypid, a.atttypmod), E',\\n    '"
                   "                     ORDER BY a.attnum)"
                   "               FROM pg_attribute a"
                   "              WHERE a.attrelid = t.typrelid AND a.attnum > 0"
                   "                AND NOT a.attisdropped), '') || E'\\n);'"
                   " WHEN 'r' THEN 'CREATE TYPE ' || " + lit(qualified_name(ref.schema, ref.name)) +
                   "   || ' AS RANGE (SUBTYPE = ' ||"
                   "   (SELECT format_type(rngsubtype, NULL) FROM pg_range"
                   "     WHERE rngtypid = t.oid) || ');'"
                   " ELSE '-- ' || format_type(t.oid, NULL) || ': base type' END"
                   "  FROM pg_type t JOIN pg_namespace n ON n.oid = t.typnamespace"
                   " WHERE n.nspname = " + lit(ref.schema) +
                   "   AND t.typname = " + lit(ref.name);

        case ObjectType::event_trigger:
            return "SELECT 'CREATE EVENT TRIGGER ' || quote_ident(e.evtname) ||"
                   " E'\\n    ON ' || e.evtevent ||"
                   " COALESCE(E'\\n    WHEN TAG IN (' ||"
                   "   (SELECT string_agg(quote_literal(tag), ', ') FROM unnest(e.evttags) AS tag)"
                   "   || ')', '') ||"
                   " E'\\n    EXECUTE FUNCTION ' || e.evtfoid::regproc::text || '();'"
                   "  FROM pg_event_trigger e WHERE e.evtname = " + lit(ref.name);

        case ObjectType::foreign_server:
            return "SELECT 'CREATE SERVER ' || quote_ident(s.srvname) ||"
                   " COALESCE(' TYPE ' || quote_literal(s.srvtype), '') ||"
                   " COALESCE(' VERSION ' || quote_literal(s.srvversion), '') ||"
                   " E'\\n    FOREIGN DATA WRAPPER ' || quote_ident(w.fdwname) ||"
                   " COALESCE(E'\\n    OPTIONS (' ||"
                   "   (SELECT string_agg(split_part(o, '=', 1) || ' ' ||"
                   "                      quote_literal(substr(o, strpos(o, '=') + 1)), ', ')"
                   "      FROM unnest(s.srvoptions) AS o) || ')', '') || ';'"
                   "  FROM pg_foreign_server s"
                   "  JOIN pg_foreign_data_wrapper w ON w.oid = s.srvfdw"
                   " WHERE s.srvname = " + lit(ref.name);

        case ObjectType::foreign_data_wrapper:
            return "SELECT 'CREATE FOREIGN DATA WRAPPER ' || quote_ident(w.fdwname) ||"
                   " CASE WHEN w.fdwhandler = 0 THEN ''"
                   "      ELSE E'\\n    HANDLER ' || w.fdwhandler::regproc::text END ||"
                   " CASE WHEN w.fdwvalidator = 0 THEN ''"
                   "      ELSE E'\\n    VALIDATOR ' || w.fdwvalidator::regproc::text END || ';'"
                   "  FROM pg_foreign_data_wrapper w WHERE w.fdwname = " + lit(ref.name);

        case ObjectType::user_mapping:
            // Sem as opcoes: e' la' que fica a senha remota.
            return "SELECT 'CREATE USER MAPPING FOR ' ||"
                   " CASE WHEN m.usename = 'public' THEN 'PUBLIC'"
                   "      ELSE quote_ident(m.usename) END ||"
                   " E'\\n    SERVER ' || quote_ident(m.srvname) ||"
                   " E';\\n-- options are not shown: they hold the remote password'"
                   "  FROM pg_user_mappings m"
                   " WHERE m.srvname = " + lit(ref.parent) +
                   "   AND m.usename = " + lit(ref.name);

        case ObjectType::column:
            return "SELECT 'ALTER TABLE ' || a.attrelid::regclass::text ||"
                   " E'\\n    ADD COLUMN ' || quote_ident(a.attname) || ' ' ||"
                   " format_type(a.atttypid, a.atttypmod) ||"
                   " CASE WHEN a.attnotnull THEN ' NOT NULL' ELSE '' END ||"
                   " COALESCE(' DEFAULT ' || pg_get_expr(d.adbin, d.adrelid), '') || ';'"
                   "  FROM pg_attribute a"
                   "  LEFT JOIN pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum"
                   " WHERE a.attrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND a.attname = " + lit(ref.name) + " AND NOT a.attisdropped";

        case ObjectType::language:
            return "SELECT 'CREATE ' || CASE WHEN l.lanpltrusted THEN 'TRUSTED ' ELSE '' END ||"
                   " 'LANGUAGE ' || quote_ident(l.lanname) || ';'"
                   "  FROM pg_language l WHERE l.lanname = " + lit(ref.name);
    }
    return {};
}

// --- Permissoes --------------------------------------------------------------------

std::string pg_permissions_query(const ObjectRef& ref) {
    // aclexplode devolve (grantor, grantee, privilege_type, is_grantable);
    // grantee 0 e' o pseudo-papel PUBLIC.
    //
    // ACL nula NAO e' "ninguem pode": e' "vale o padrao do tipo" (o dono pode
    // tudo; PUBLIC pode EXECUTE em funcao, CONNECT em banco...). acldefault()
    // devolve esse padrao -- sem ele a aba apareceria vazia justamente para o
    // objeto recem-criado, e o usuario concluiria que nem o dono tem acesso.
    const auto exploded = [](std::string_view acl, std::string_view kind,
                             std::string_view owner, const std::string& from,
                             const std::string& where) {
        return "SELECT CASE WHEN a.grantee = 0 THEN 'PUBLIC'"
               "            ELSE pg_get_userbyid(a.grantee) END,"
               "       a.privilege_type,"
               "       a.is_grantable,"
               "       pg_get_userbyid(a.grantor)"
               "  FROM " + from + ","
               "       LATERAL aclexplode(COALESCE(" + std::string(acl) +
               ", acldefault('" + std::string(kind) + "', " + std::string(owner) +
               "))) AS a"
               " WHERE " + where +
               " ORDER BY 1, 2";
    };

    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::materialized_view:
        case ObjectType::foreign_table:
            return exploded("c.relacl", "r", "c.relowner", "pg_class c",
                            "c.oid = " + regclass(ref.schema, ref.name));

        case ObjectType::sequence:
            return exploded("c.relacl", "s", "c.relowner", "pg_class c",
                            "c.oid = " + regclass(ref.schema, ref.name));

        case ObjectType::column:
            // Coluna nao tem padrao: attacl nulo e' mesmo "nenhuma concessao
            // de coluna" (valem as da tabela).
            return "SELECT CASE WHEN a.grantee = 0 THEN 'PUBLIC'"
                   "            ELSE pg_get_userbyid(a.grantee) END,"
                   "       a.privilege_type,"
                   "       a.is_grantable,"
                   "       pg_get_userbyid(a.grantor)"
                   "  FROM pg_attribute att, LATERAL aclexplode(att.attacl) AS a"
                   " WHERE att.attrelid = " + regclass(ref.schema, ref.parent) +
                   "   AND att.attname = " + lit(ref.name) +
                   " ORDER BY 1, 2";

        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::aggregate:
            return exploded("p.proacl", "f", "p.proowner", "pg_proc p",
                            "p.oid = " + regprocedure(ref));

        case ObjectType::schema:
            return exploded("n.nspacl", "n", "n.nspowner", "pg_namespace n",
                            "n.nspname = " + lit(ref.name));

        case ObjectType::database:
            return exploded("d.datacl", "d", "d.datdba", "pg_database d",
                            "d.datname = " + lit(ref.name));

        case ObjectType::tablespace:
            return exploded("t.spcacl", "t", "t.spcowner", "pg_tablespace t",
                            "t.spcname = " + lit(ref.name));

        case ObjectType::data_type:
            return exploded("t.typacl", "T", "t.typowner",
                            "pg_type t JOIN pg_namespace n ON n.oid = t.typnamespace",
                            "n.nspname = " + lit(ref.schema) +
                                " AND t.typname = " + lit(ref.name));

        case ObjectType::language:
            return exploded("l.lanacl", "l", "l.lanowner", "pg_language l",
                            "l.lanname = " + lit(ref.name));

        case ObjectType::foreign_data_wrapper:
            return exploded("w.fdwacl", "F", "w.fdwowner", "pg_foreign_data_wrapper w",
                            "w.fdwname = " + lit(ref.name));

        case ObjectType::foreign_server:
            return exploded("s.srvacl", "S", "s.srvowner", "pg_foreign_server s",
                            "s.srvname = " + lit(ref.name));

        default:
            return {};
    }
}

std::string pg_statistics_query(const ObjectRef& ref) {
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::materialized_view:
            return "SELECT s.seq_scan::text AS \"Sequential scans\","
                   "       s.seq_tup_read::text AS \"Rows read sequentially\","
                   "       COALESCE(s.idx_scan::text, '') AS \"Index scans\","
                   "       COALESCE(s.idx_tup_fetch::text, '') AS \"Rows fetched by index\","
                   "       s.n_tup_ins::text AS \"Rows inserted\","
                   "       s.n_tup_upd::text AS \"Rows updated\","
                   "       s.n_tup_del::text AS \"Rows deleted\","
                   "       s.n_live_tup::text AS \"Live rows\","
                   "       s.n_dead_tup::text AS \"Dead rows\","
                   "       COALESCE(s.last_vacuum::text, '') AS \"Last vacuum\","
                   "       COALESCE(s.last_autovacuum::text, '') AS \"Last autovacuum\","
                   "       COALESCE(s.last_analyze::text, '') AS \"Last analyze\","
                   "       COALESCE(s.last_autoanalyze::text, '') AS \"Last autoanalyze\""
                   "  FROM pg_stat_all_tables s"
                   " WHERE s.relid = " + regclass(ref.schema, ref.name);

        case ObjectType::index:
            return "SELECT s.idx_scan::text AS \"Index scans\","
                   "       s.idx_tup_read::text AS \"Entries read\","
                   "       s.idx_tup_fetch::text AS \"Rows fetched\""
                   "  FROM pg_stat_all_indexes s"
                   " WHERE s.indexrelid = " + regclass(ref.schema, ref.name);

        case ObjectType::database:
            return "SELECT s.numbackends::text AS \"Connections\","
                   "       s.xact_commit::text AS \"Commits\","
                   "       s.xact_rollback::text AS \"Rollbacks\","
                   "       s.blks_read::text AS \"Blocks read\","
                   "       s.blks_hit::text AS \"Blocks hit\","
                   "       s.tup_returned::text AS \"Rows returned\","
                   "       s.tup_fetched::text AS \"Rows fetched\","
                   "       s.deadlocks::text AS \"Deadlocks\","
                   "       COALESCE(s.stats_reset::text, '') AS \"Statistics reset\""
                   "  FROM pg_stat_database s"
                   " WHERE s.datname = " + lit(ref.name);

        default:
            return {};
    }
}

ObjectEdit editable_property(ObjectType type, std::string_view label) noexcept {
    if (on_mysql()) return mysql_editable_property(type, label);
    if (on_mssql()) return mssql_editable_property(type, label);
    if (on_anywhere()) return sqlanywhere_editable_property(type, label);
    const bool has_owner =
        type != ObjectType::column && type != ObjectType::index &&
        type != ObjectType::constraint && type != ObjectType::foreign_key &&
        type != ObjectType::trigger && type != ObjectType::rule &&
        type != ObjectType::policy && type != ObjectType::role &&
        type != ObjectType::extension && type != ObjectType::user_mapping;

    if (label == "Name") {
        // Extensao e user mapping nao se renomeiam.
        if (type == ObjectType::extension || type == ObjectType::user_mapping) {
            return ObjectEdit::none;
        }
        return ObjectEdit::name;
    }
    if (label == "Comment") {
        return type == ObjectType::user_mapping ? ObjectEdit::none
                                                : ObjectEdit::comment;
    }
    if (label == "Owner") return has_owner ? ObjectEdit::owner : ObjectEdit::none;

    if (label == "Schema") {
        const bool movable =
            type == ObjectType::table || type == ObjectType::view ||
            type == ObjectType::materialized_view ||
            type == ObjectType::foreign_table || type == ObjectType::sequence ||
            is_routine(type) || type == ObjectType::data_type ||
            type == ObjectType::extension;
        return movable ? ObjectEdit::schema : ObjectEdit::none;
    }
    if (label == "Tablespace") {
        const bool placed =
            type == ObjectType::table || type == ObjectType::materialized_view ||
            type == ObjectType::index || type == ObjectType::database;
        return placed ? ObjectEdit::tablespace : ObjectEdit::none;
    }
    return ObjectEdit::none;
}

std::vector<std::string_view> privileges_for(ObjectType type) {
    if (on_mysql()) return mysql_privileges_for(type);
    if (on_mssql()) return mssql_privileges_for(type);
    if (on_anywhere()) return sqlanywhere_privileges_for(type);
    switch (type) {
        case ObjectType::table:
        case ObjectType::foreign_table:
        case ObjectType::view:
        case ObjectType::materialized_view:
            return {"SELECT", "INSERT", "UPDATE", "DELETE", "TRUNCATE",
                    "REFERENCES", "TRIGGER"};
        case ObjectType::column:
            return {"SELECT", "INSERT", "UPDATE", "REFERENCES"};
        case ObjectType::sequence:
            return {"USAGE", "SELECT", "UPDATE"};
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::aggregate:
            return {"EXECUTE"};
        case ObjectType::schema:
            return {"USAGE", "CREATE"};
        case ObjectType::database:
            return {"CONNECT", "CREATE", "TEMPORARY"};
        case ObjectType::tablespace:
            return {"CREATE"};
        case ObjectType::data_type:
        case ObjectType::language:
        case ObjectType::foreign_data_wrapper:
        case ObjectType::foreign_server:
            return {"USAGE"};
        default:
            return {};
    }
}

// --- Nome no comando ---------------------------------------------------------------

std::string object_sql_name(const ObjectRef& ref) {
    if (on_mysql()) return mysql_object_name(ref);
    if (on_mssql()) return mssql_object_name(ref);
    if (on_anywhere()) return sqlanywhere_object_name(ref);
    switch (ref.type) {
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::aggregate:
            return qualified_name(ref.schema, ref.name) + "(" + ref.signature + ")";

        case ObjectType::column:
            return qualified_name(ref.schema, ref.parent) + "." +
                   quote_if_needed(ref.name);

        // Globais do servidor ou do banco: sem schema.
        case ObjectType::database:
        case ObjectType::schema:
        case ObjectType::role:
        case ObjectType::extension:
        case ObjectType::tablespace:
        case ObjectType::event_trigger:
        case ObjectType::foreign_server:
        case ObjectType::foreign_data_wrapper:
        case ObjectType::language:
            return quote_if_needed(ref.name);

        // Pertencem a uma tabela: o nome sozinho, e o "ON tabela" vai depois.
        case ObjectType::constraint:
        case ObjectType::foreign_key:
        case ObjectType::trigger:
        case ObjectType::rule:
        case ObjectType::policy:
            return quote_if_needed(ref.name);

        case ObjectType::user_mapping:
            return (ref.name == "public" ? std::string("PUBLIC")
                                         : quote_if_needed(ref.name)) +
                   " SERVER " + quote_if_needed(ref.parent);

        default:
            return qualified_name(ref.schema, ref.name);
    }
}

// --- Alteracoes --------------------------------------------------------------------

AlterScript generate_object_rename(const ObjectRef& ref, std::string_view new_name) {
    if (on_mysql()) return mysql_object_rename(ref, new_name);
    if (on_mssql()) return mssql_object_rename(ref, new_name);
    if (on_anywhere()) return sqlanywhere_object_rename(ref, new_name);
    if (new_name.empty()) return refused("the new name is required");
    if (new_name == ref.name) return refused("the name did not change");

    const std::string target = quote_if_needed(new_name);

    switch (ref.type) {
        case ObjectType::extension:
        case ObjectType::user_mapping:
            return refused("this object type cannot be renamed");

        case ObjectType::column:
            return single("ALTER TABLE " + qualified_name(ref.schema, ref.parent) +
                          " RENAME COLUMN " + quote_if_needed(ref.name) + " TO " +
                          target + ";");

        case ObjectType::constraint:
        case ObjectType::foreign_key:
            return single("ALTER TABLE " + qualified_name(ref.schema, ref.parent) +
                          " RENAME CONSTRAINT " + quote_if_needed(ref.name) + " TO " +
                          target + ";");

        case ObjectType::trigger:
        case ObjectType::rule:
        case ObjectType::policy:
            return single("ALTER " + std::string(sql_keyword(ref.type)) + " " +
                          quote_if_needed(ref.name) + on_table(ref) + " RENAME TO " +
                          target + ";");

        case ObjectType::role: {
            AlterScript script = single("ALTER ROLE " + quote_if_needed(ref.name) +
                                        " RENAME TO " + target + ";");
            // O hash MD5 da senha leva o NOME do papel junto: renomear o
            // invalida, e o servidor avisa so' com um NOTICE.
            script.warnings.push_back(
                "renaming a role clears its password when it is stored as MD5");
            return script;
        }

        case ObjectType::database: {
            AlterScript script = single("ALTER DATABASE " + quote_if_needed(ref.name) +
                                        " RENAME TO " + target + ";");
            script.warnings.push_back(
                "a database cannot be renamed while anyone is connected to it, "
                "including this connection");
            return script;
        }

        default:
            return single("ALTER " + std::string(sql_keyword(ref.type)) + " " +
                          object_sql_name(ref) + " RENAME TO " + target + ";");
    }
}

AlterScript generate_object_comment(const ObjectRef& ref, std::string_view comment) {
    if (on_mysql()) return mysql_object_comment(ref, comment);
    if (on_mssql()) return mssql_object_comment(ref, comment);
    if (on_anywhere()) return sqlanywhere_object_comment(ref, comment);
    if (ref.type == ObjectType::user_mapping) {
        return refused("a user mapping has no comment");
    }

    // Vazio REMOVE o comentario: `IS NULL`, e nao `IS ''`.
    const std::string value = comment.empty() ? std::string("NULL") : lit(comment);

    std::string target;
    switch (ref.type) {
        case ObjectType::constraint:
        case ObjectType::foreign_key:
        case ObjectType::trigger:
        case ObjectType::rule:
        case ObjectType::policy:
            target = std::string(sql_keyword(ref.type)) + " " +
                     quote_if_needed(ref.name) + on_table(ref);
            break;
        default:
            target = std::string(sql_keyword(ref.type)) + " " + object_sql_name(ref);
            break;
    }
    return single("COMMENT ON " + target + " IS " + value + ";");
}

AlterScript generate_object_owner(const ObjectRef& ref, std::string_view owner) {
    if (on_mysql()) return refused("MySQL objects have no owner: see the DEFINER");
    if (on_mssql()) {
        return refused("change the owner with ALTER AUTHORIZATION ON <object> TO <principal>");
    }
    if (on_anywhere()) {
        return refused("SQL Anywhere cannot change the owner of an object: create it "
                       "again under the other user");
    }
    if (owner.empty()) return refused("the new owner is required");
    if (editable_property(ref.type, "Owner") == ObjectEdit::none) {
        return refused("this object type has no owner of its own");
    }
    return single("ALTER " + std::string(sql_keyword(ref.type)) + " " +
                  object_sql_name(ref) + " OWNER TO " + quote_if_needed(owner) + ";");
}

AlterScript generate_object_schema(const ObjectRef& ref, std::string_view new_schema) {
    if (on_mysql()) {
        return refused("to move a table to another database, rename it: "
                       "RENAME TABLE db1.t TO db2.t");
    }
    if (on_mssql()) return mssql_object_schema(ref, new_schema);
    if (on_anywhere()) {
        return refused("the schema of a SQL Anywhere object is its owner, and it cannot "
                       "be changed: create the object again under the other user");
    }
    if (new_schema.empty()) return refused("the new schema is required");
    if (new_schema == ref.schema) return refused("the schema did not change");
    if (editable_property(ref.type, "Schema") == ObjectEdit::none) {
        return refused("this object type cannot be moved to another schema");
    }
    return single("ALTER " + std::string(sql_keyword(ref.type)) + " " +
                  object_sql_name(ref) + " SET SCHEMA " + quote_if_needed(new_schema) +
                  ";");
}

AlterScript generate_object_tablespace(const ObjectRef& ref,
                                       std::string_view tablespace) {
    if (on_mysql()) return refused("not available on MySQL");
    if (on_mssql()) return refused("not available on SQL Server");
    if (on_anywhere()) return refused("not available on SQL Anywhere");
    if (tablespace.empty()) return refused("the new tablespace is required");
    if (editable_property(ref.type, "Tablespace") == ObjectEdit::none) {
        return refused("this object type is not stored in a tablespace");
    }

    AlterScript script =
        single("ALTER " + std::string(sql_keyword(ref.type)) + " " +
               object_sql_name(ref) + " SET TABLESPACE " + quote_if_needed(tablespace) +
               ";");
    // Mover de tablespace REESCREVE o objeto, com trava exclusiva enquanto
    // dura. Numa tabela grande isso para o sistema.
    script.warnings.push_back(
        "moving to another tablespace rewrites the object and locks it meanwhile");
    return script;
}

AlterScript generate_object_drop(const ObjectRef& ref, bool cascade) {
    if (ref.name.empty()) return refused("object name is required");
    // O MySQL nao tem CASCADE util em DROP: `cascade` e' ignorado la'.
    if (on_mysql()) return mysql_object_drop(ref);
    if (on_mssql()) return mssql_object_drop(ref);
    if (on_anywhere()) return sqlanywhere_object_drop(ref);

    std::string statement;
    switch (ref.type) {
        case ObjectType::column:
            statement = "ALTER TABLE " + qualified_name(ref.schema, ref.parent) +
                        " DROP COLUMN " + quote_if_needed(ref.name);
            break;
        case ObjectType::constraint:
        case ObjectType::foreign_key:
            statement = "ALTER TABLE " + qualified_name(ref.schema, ref.parent) +
                        " DROP CONSTRAINT " + quote_if_needed(ref.name);
            break;
        case ObjectType::trigger:
        case ObjectType::rule:
        case ObjectType::policy:
            statement = "DROP " + std::string(sql_keyword(ref.type)) + " " +
                        quote_if_needed(ref.name) + on_table(ref);
            break;
        case ObjectType::user_mapping:
            statement = "DROP USER MAPPING FOR " + object_sql_name(ref);
            break;
        default:
            statement = "DROP " + std::string(sql_keyword(ref.type)) + " " +
                        object_sql_name(ref);
            break;
    }

    // Banco, papel, tablespace e user mapping nao tem CASCADE.
    const bool cascadable =
        ref.type != ObjectType::database && ref.type != ObjectType::role &&
        ref.type != ObjectType::tablespace && ref.type != ObjectType::user_mapping &&
        ref.type != ObjectType::policy;

    AlterScript script;
    if (cascade && cascadable) {
        statement += " CASCADE";
        script.warnings.push_back(
            "CASCADE also drops every object that depends on this one");
    }
    script.statements.push_back(statement + ";");
    script.destructive.push_back(0);

    if (ref.type == ObjectType::database) {
        script.warnings.push_back(
            "dropping a database removes every object and every row in it; it "
            "cannot run inside a transaction");
    }
    return script;
}

namespace {

// O alvo de GRANT/REVOKE: "ON TABLE s.t", "ON SCHEMA s", "ON FUNCTION f(args)".
std::string grant_target(const ObjectRef& ref, std::string& column_list) {
    column_list.clear();
    switch (ref.type) {
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::materialized_view:
        case ObjectType::foreign_table:
            // Sem a palavra: `GRANT ... ON TABLE` nao aceita view em versoes
            // antigas, e sem ela aceita os quatro.
            return qualified_name(ref.schema, ref.name);
        case ObjectType::column:
            column_list = " (" + quote_if_needed(ref.name) + ")";
            return qualified_name(ref.schema, ref.parent);
        case ObjectType::sequence:
            return "SEQUENCE " + qualified_name(ref.schema, ref.name);
        case ObjectType::function:
        case ObjectType::aggregate:
            return "FUNCTION " + object_sql_name(ref);
        case ObjectType::procedure:
            return "PROCEDURE " + object_sql_name(ref);
        case ObjectType::schema:
            return "SCHEMA " + quote_if_needed(ref.name);
        case ObjectType::database:
            return "DATABASE " + quote_if_needed(ref.name);
        case ObjectType::tablespace:
            return "TABLESPACE " + quote_if_needed(ref.name);
        case ObjectType::data_type:
            return "TYPE " + qualified_name(ref.schema, ref.name);
        case ObjectType::language:
            return "LANGUAGE " + quote_if_needed(ref.name);
        case ObjectType::foreign_data_wrapper:
            return "FOREIGN DATA WRAPPER " + quote_if_needed(ref.name);
        case ObjectType::foreign_server:
            return "FOREIGN SERVER " + quote_if_needed(ref.name);
        default:
            return {};
    }
}

bool known_privilege(const ObjectRef& ref, std::string_view privilege) {
    if (privilege == "ALL") return true;

    // MAINTAIN so' existe do PostgreSQL 17 em diante: fica fora da lista
    // oferecida (num servidor mais antigo seria uma caixa que so' da' erro),
    // mas quem ja' o tem concedido precisa poder revoga-lo.
    if (privilege == "MAINTAIN") {
        return ref.type == ObjectType::table || ref.type == ObjectType::materialized_view ||
               ref.type == ObjectType::view || ref.type == ObjectType::foreign_table;
    }
    const std::vector<std::string_view> allowed = privileges_for(ref.type);
    return std::find(allowed.begin(), allowed.end(), privilege) != allowed.end();
}

std::string grantee_name(std::string_view grantee) {
    return grantee == "PUBLIC" ? std::string("PUBLIC") : quote_if_needed(grantee);
}

} // namespace

AlterScript generate_grant(const ObjectRef& ref, std::string_view privilege,
                           std::string_view grantee, bool with_grant_option) {
    if (on_mysql()) return mysql_grant(ref, privilege, grantee, with_grant_option);
    if (on_mssql()) return mssql_grant(ref, privilege, grantee, with_grant_option);
    if (on_anywhere()) return sqlanywhere_grant(ref, privilege, grantee, with_grant_option);
    if (grantee.empty()) return refused("the role is required");

    // O privilegio vai para o comando SEM aspas: so' os conhecidos passam.
    if (!known_privilege(ref, privilege)) {
        return refused("this privilege does not apply to this object type");
    }

    std::string columns;
    const std::string target = grant_target(ref, columns);
    if (target.empty()) return refused("this object type has no privileges");

    // PUBLIC nao pode receber WITH GRANT OPTION.
    const bool option = with_grant_option && grantee != "PUBLIC";

    return single("GRANT " + std::string(privilege) + columns + " ON " + target +
                  " TO " + grantee_name(grantee) +
                  (option ? " WITH GRANT OPTION" : "") + ";");
}

AlterScript generate_revoke(const ObjectRef& ref, std::string_view privilege,
                            std::string_view grantee) {
    if (on_mysql()) return mysql_revoke(ref, privilege, grantee);
    if (on_mssql()) return mssql_revoke(ref, privilege, grantee);
    if (on_anywhere()) return sqlanywhere_revoke(ref, privilege, grantee);
    if (grantee.empty()) return refused("the role is required");
    if (!known_privilege(ref, privilege)) {
        return refused("this privilege does not apply to this object type");
    }

    std::string columns;
    const std::string target = grant_target(ref, columns);
    if (target.empty()) return refused("this object type has no privileges");

    return single("REVOKE " + std::string(privilege) + columns + " ON " + target +
                  " FROM " + grantee_name(grantee) + ";");
}

} // namespace otter::db
