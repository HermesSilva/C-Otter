#include "db/object_ddl.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <cctype>

namespace otter::db {
namespace {

// Literal no padrao SQL: tudo aqui e' PostgreSQL.
std::string lit(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += "''";
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
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

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// Uma palavra de um conjunto fechado, que entra no comando sem aspas.
bool one_of(std::string_view word, std::initializer_list<std::string_view> allowed) {
    return std::find(allowed.begin(), allowed.end(), word) != allowed.end();
}

// "a, B c , PUBLIC" -> "a, \"B c\", PUBLIC".
std::string role_list(std::string_view roles) {
    std::string out;
    std::size_t start = 0;
    while (start <= roles.size()) {
        const std::size_t comma = roles.find(',', start);
        const std::string_view part = trimmed(
            roles.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                                : comma - start));
        if (!part.empty()) {
            if (!out.empty()) out += ", ";
            out += (part == "PUBLIC" || part == "public")
                       ? std::string("PUBLIC")
                       : (part == "CURRENT_USER" || part == "SESSION_USER")
                             ? std::string(part)
                             : quote_if_needed(part);
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

// "chave=valor, chave2=valor2" -> "chave = valor, ...". So' o que tem a forma
// de parametro de armazenamento passa: a lista vai para o comando sem aspas.
bool storage_options(std::string_view text, std::string& out) {
    out.clear();
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string_view part = trimmed(
            text.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                               : comma - start));
        if (!part.empty()) {
            const std::size_t eq = part.find('=');
            if (eq == std::string_view::npos) return false;
            const std::string_view key   = trimmed(part.substr(0, eq));
            const std::string_view value = trimmed(part.substr(eq + 1));
            if (key.empty() || value.empty()) return false;

            for (const char c : key) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
            }
            for (const char c : value) {
                if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') return false;
            }
            if (!out.empty()) out += ", ";
            out += std::string(key) + " = " + std::string(value);
        }
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return true;
}

} // namespace

// --- CREATE ------------------------------------------------------------------------

AlterScript generate_create_database(const NewDatabase& database) {
    if (database.name.empty()) return refused("the database name is required");

    std::string sql = "CREATE DATABASE " + quote_if_needed(database.name);
    if (!database.owner.empty()) {
        sql += "\n    OWNER " + quote_if_needed(database.owner);
    }
    if (!database.template_db.empty()) {
        sql += "\n    TEMPLATE " + quote_if_needed(database.template_db);
    }
    if (!database.encoding.empty()) {
        sql += "\n    ENCODING " + lit(database.encoding);
    }
    if (!database.tablespace.empty()) {
        sql += "\n    TABLESPACE " + quote_if_needed(database.tablespace);
    }

    AlterScript script = single(sql + ";");
    // CREATE DATABASE recusa bloco de transacao, e copiar o template exige
    // que ninguem esteja conectado a ele.
    script.warnings.push_back(
        "CREATE DATABASE cannot run inside a transaction: switch to auto-commit first");
    if (!database.encoding.empty() && database.template_db.empty()) {
        script.warnings.push_back(
            "an encoding different from the one of template1 needs TEMPLATE template0");
    }
    return script;
}

AlterScript generate_create_schema(std::string_view name, std::string_view owner) {
    if (name.empty()) return refused("the schema name is required");

    std::string sql = "CREATE SCHEMA " + quote_if_needed(name);
    if (!owner.empty()) sql += " AUTHORIZATION " + quote_if_needed(owner);
    return single(sql + ";");
}

AlterScript generate_create_extension(std::string_view name, std::string_view schema) {
    if (name.empty()) return refused("the extension name is required");

    std::string sql = "CREATE EXTENSION " + quote_if_needed(name);
    if (!schema.empty()) sql += " SCHEMA " + quote_if_needed(schema);
    return single(sql + ";");
}

AlterScript generate_create_role(const NewRole& role) {
    if (role.name.empty()) return refused("the role name is required");

    std::string sql = "CREATE ROLE " + quote_if_needed(role.name);
    if (role.is_user) {
        sql += " LOGIN";
        if (!role.password.empty()) sql += " PASSWORD " + lit(role.password);
    }

    AlterScript script = single(sql + ";");
    if (role.is_user && !role.password.empty()) {
        // O comando viaja em texto e fica no log do servidor se
        // log_statement estiver ligado -- com a senha dentro.
        script.warnings.push_back(
            "the password travels inside the command and may be written to the "
            "server log (log_statement)");
    }
    return script;
}

AlterScript generate_create_tablespace(const NewTablespace& tablespace) {
    if (tablespace.name.empty()) return refused("the tablespace name is required");
    if (tablespace.location.empty()) {
        return refused("the location is required: a directory on the server");
    }

    std::string options;
    if (!storage_options(tablespace.options, options)) {
        return refused("options must be a list like seq_page_cost=1, random_page_cost=1.1");
    }

    std::string sql = "CREATE TABLESPACE " + quote_if_needed(tablespace.name);
    if (!tablespace.owner.empty()) sql += "\n    OWNER " + quote_if_needed(tablespace.owner);
    sql += "\n    LOCATION " + lit(tablespace.location);
    if (!options.empty()) sql += "\n    WITH (" + options + ")";

    AlterScript script = single(sql + ";");
    script.warnings.push_back(
        "the directory must already exist on the SERVER, empty and owned by the "
        "postgres system user; CREATE TABLESPACE cannot run inside a transaction");
    return script;
}

std::string routine_template(const NewRoutine& routine) {
    const std::string kind = routine.procedure ? "PROCEDURE" : "FUNCTION";
    const std::string tag  = routine.procedure ? "$procedure$" : "$function$";
    const std::string name = qualified_name(routine.schema, routine.name);

    std::string out = "CREATE OR REPLACE " + kind + " " + name + "()\n";
    if (!routine.procedure && !routine.return_type.empty()) {
        out += "\tRETURNS " + routine.return_type + "\n";
    }
    if (!routine.language.empty()) out += "\tLANGUAGE " + routine.language + "\n";

    // O corpo do DBeaver e' "BEGIN END" para qualquer linguagem; em `sql`
    // isso nao compila. Aqui cada linguagem ganha um corpo que roda.
    out += "AS " + tag + "\n";
    if (routine.language == "plpgsql") {
        out += "\tBEGIN\n\n\tEND;\n";
    } else if (routine.language == "sql") {
        out += routine.procedure ? "\tSELECT 1;\n" : "\tSELECT NULL::" +
                                                         (routine.return_type.empty()
                                                              ? std::string("int4")
                                                              : routine.return_type) +
                                                         ";\n";
    } else {
        out += "\n";
    }
    out += tag + ";\n";
    return out;
}

const std::vector<std::string_view>& event_trigger_events() {
    static const std::vector<std::string_view> events = {
        "ddl_command_start", "ddl_command_end", "table_rewrite", "sql_drop"};
    return events;
}

AlterScript generate_create_event_trigger(std::string_view name, std::string_view event,
                                          std::string_view function) {
    if (name.empty()) return refused("the trigger name is required");
    if (function.empty()) return refused("the trigger function is required");

    const auto& events = event_trigger_events();
    if (std::find(events.begin(), events.end(), event) == events.end()) {
        return refused("unknown event type");
    }

    // `function` e' "schema.nome": cada parte citada.
    std::string target;
    const std::size_t dot = function.find('.');
    if (dot == std::string_view::npos) {
        target = quote_if_needed(function);
    } else {
        target = qualified_name(function.substr(0, dot), function.substr(dot + 1));
    }

    AlterScript script =
        single("CREATE EVENT TRIGGER " + quote_if_needed(name) + " ON " +
               std::string(event) + "\n    EXECUTE FUNCTION " + target + "();");
    script.warnings.push_back(
        "an event trigger fires on every matching DDL command of the whole database");
    return script;
}

AlterScript generate_create_policy(const NewPolicy& policy) {
    if (policy.name.empty()) return refused("the policy name is required");
    if (policy.table.empty()) return refused("the table is required");
    if (!one_of(policy.command, {"ALL", "SELECT", "INSERT", "UPDATE", "DELETE"})) {
        return refused("unknown command");
    }

    // USING nao vale para INSERT, e WITH CHECK nao vale para SELECT/DELETE:
    // o servidor recusa. Dito aqui, com o motivo.
    if (policy.command == "INSERT" && !trimmed(policy.using_expression).empty()) {
        return refused("an INSERT policy takes WITH CHECK, not USING");
    }
    if ((policy.command == "SELECT" || policy.command == "DELETE") &&
        !trimmed(policy.check_expression).empty()) {
        return refused("a SELECT or DELETE policy takes USING, not WITH CHECK");
    }

    std::string sql = "CREATE POLICY " + quote_if_needed(policy.name) + " ON " +
                      qualified_name(policy.schema, policy.table) +
                      "\n    AS " + (policy.permissive ? "PERMISSIVE" : "RESTRICTIVE") +
                      "\n    FOR " + policy.command;

    const std::string roles = role_list(policy.roles);
    sql += "\n    TO " + (roles.empty() ? std::string("PUBLIC") : roles);

    if (const std::string_view using_ = trimmed(policy.using_expression); !using_.empty()) {
        sql += "\n    USING (" + std::string(using_) + ")";
    }
    if (const std::string_view check = trimmed(policy.check_expression); !check.empty()) {
        sql += "\n    WITH CHECK (" + std::string(check) + ")";
    }

    AlterScript script = single(sql + ";");
    script.warnings.push_back(
        "the policy only filters after ALTER TABLE ... ENABLE ROW LEVEL SECURITY");
    return script;
}

AlterScript generate_create_materialized_view(std::string_view schema,
                                              std::string_view name,
                                              std::string_view definition,
                                              bool with_data) {
    if (name.empty()) return refused("the view name is required");

    const std::string_view body = strip_trailing_semicolon(trimmed(definition));
    if (body.empty()) return refused("the query is required");

    return single("CREATE MATERIALIZED VIEW " + qualified_name(schema, name) + " AS\n" +
                  std::string(body) + "\n" + (with_data ? "WITH DATA" : "WITH NO DATA") +
                  ";");
}

// --- Papel -------------------------------------------------------------------------

std::string_view role_option_keyword(std::string_view label, bool enabled) noexcept {
    if (label == "Can login")        return enabled ? "LOGIN" : "NOLOGIN";
    if (label == "Superuser")        return enabled ? "SUPERUSER" : "NOSUPERUSER";
    if (label == "Create database")  return enabled ? "CREATEDB" : "NOCREATEDB";
    if (label == "Create role")      return enabled ? "CREATEROLE" : "NOCREATEROLE";
    if (label == "Inherit")          return enabled ? "INHERIT" : "NOINHERIT";
    if (label == "Replication")      return enabled ? "REPLICATION" : "NOREPLICATION";
    if (label == "Bypass RLS")       return enabled ? "BYPASSRLS" : "NOBYPASSRLS";
    return {};
}

AlterScript generate_role_option(std::string_view role, std::string_view label,
                                 bool enabled) {
    if (role.empty()) return refused("the role is required");

    const std::string_view keyword = role_option_keyword(label, enabled);
    if (keyword.empty()) return refused("this is not a role attribute");

    AlterScript script =
        single("ALTER ROLE " + quote_if_needed(role) + " " + std::string(keyword) + ";");
    if (label == "Superuser" && enabled) {
        script.warnings.push_back("a superuser bypasses every permission check");
    }
    return script;
}

AlterScript generate_role_password(std::string_view role, std::string_view password) {
    if (role.empty()) return refused("the role is required");

    AlterScript script = single("ALTER ROLE " + quote_if_needed(role) + " PASSWORD " +
                                (password.empty() ? std::string("NULL") : lit(password)) +
                                ";");
    if (password.empty()) {
        script.warnings.push_back(
            "without a password the role cannot log in with password authentication");
    } else {
        script.warnings.push_back(
            "the password travels inside the command and may be written to the "
            "server log (log_statement)");
    }
    return script;
}

AlterScript generate_role_membership(std::string_view group, std::string_view member,
                                     bool grant) {
    if (group.empty() || member.empty()) return refused("both roles are required");
    if (group == member) return refused("a role cannot be a member of itself");

    return single(grant ? "GRANT " + quote_if_needed(group) + " TO " +
                              quote_if_needed(member) + ";"
                        : "REVOKE " + quote_if_needed(group) + " FROM " +
                              quote_if_needed(member) + ";");
}

// --- Tools -------------------------------------------------------------------------

AlterScript generate_vacuum(std::string_view schema, std::string_view table,
                            const VacuumOptions& options) {
    // A mesma ordem do DBeaver (PostgreToolBaseVacuum). VERBOSE sempre: e' a
    // saida dele que mostra o que o VACUUM fez.
    std::string sql = "VACUUM (";
    if (options.full)   sql += "FULL, ";
    if (options.freeze) sql += "FREEZE, ";
    sql += "VERBOSE";
    if (options.analyze)               sql += ", ANALYZE";
    if (options.disable_page_skipping) sql += ", DISABLE_PAGE_SKIPPING";
    if (options.skip_locked)           sql += ", SKIP_LOCKED";
    if (options.index_cleanup)         sql += ", INDEX_CLEANUP";
    if (options.truncate)              sql += ", TRUNCATE";
    sql += ")";
    if (!table.empty()) sql += " " + qualified_name(schema, table);

    AlterScript script = single(sql + ";");
    script.warnings.push_back(
        "VACUUM cannot run inside a transaction: switch to auto-commit first");
    if (options.full) {
        // FULL reescreve a tabela inteira sob ACCESS EXCLUSIVE: ninguem le'
        // nem grava enquanto dura.
        script.warnings.push_back(
            "VACUUM FULL rewrites the table and locks it exclusively meanwhile");
    }
    return script;
}

AlterScript generate_analyze(std::string_view schema, std::string_view table) {
    std::string sql = "ANALYZE VERBOSE";
    if (!table.empty()) sql += " " + qualified_name(schema, table);
    return single(sql + ";");
}

AlterScript generate_truncate(std::string_view schema, std::string_view table,
                              const TruncateOptions& options) {
    if (table.empty()) return refused("the table is required");

    std::string sql = "TRUNCATE TABLE";
    if (options.only) sql += " ONLY";
    sql += " " + qualified_name(schema, table);
    sql += options.restart_identity ? " RESTART IDENTITY" : " CONTINUE IDENTITY";
    sql += options.cascade ? " CASCADE" : " RESTRICT";

    AlterScript script = single(sql + ";");
    script.destructive.push_back(0);
    script.warnings.push_back("TRUNCATE removes EVERY row of the table");
    if (options.cascade) {
        script.warnings.push_back(
            "CASCADE also truncates every table that references this one");
    }
    return script;
}

AlterScript generate_refresh_materialized_view(std::string_view schema,
                                               std::string_view name, bool with_data) {
    if (name.empty()) return refused("the view name is required");

    AlterScript script = single("REFRESH MATERIALIZED VIEW " + qualified_name(schema, name) +
                                (with_data ? " WITH DATA;" : " WITH NO DATA;"));
    if (!with_data) {
        script.warnings.push_back(
            "WITH NO DATA empties the view: it cannot be queried until refreshed again");
    }
    return script;
}

AlterScript generate_trigger_enable(const ObjectRef& ref, bool enable) {
    if (ref.name.empty()) return refused("the trigger name is required");

    const char* action = enable ? " ENABLE" : " DISABLE";
    if (ref.type == ObjectType::event_trigger) {
        return single("ALTER EVENT TRIGGER " + quote_if_needed(ref.name) + action + ";");
    }
    if (ref.type == ObjectType::trigger) {
        if (ref.parent.empty()) return refused("the table of the trigger is required");
        return single("ALTER TABLE " + qualified_name(ref.schema, ref.parent) + action +
                      " TRIGGER " + quote_if_needed(ref.name) + ";");
    }
    return refused("only triggers can be enabled or disabled");
}

AlterScript generate_reindex(const ObjectRef& ref) {
    std::string target;
    switch (ref.type) {
        case ObjectType::index:
            target = "INDEX " + qualified_name(ref.schema, ref.name);
            break;
        case ObjectType::table:
        case ObjectType::materialized_view:
            target = "TABLE " + qualified_name(ref.schema, ref.name);
            break;
        case ObjectType::schema:
            target = "SCHEMA " + quote_if_needed(ref.name);
            break;
        case ObjectType::database:
            target = "DATABASE " + quote_if_needed(ref.name);
            break;
        default:
            return refused("this object type cannot be reindexed");
    }

    AlterScript script = single("REINDEX " + target + ";");
    script.warnings.push_back(
        "REINDEX blocks writes to the table (and reads through the index) while it runs");
    return script;
}

AlterScript generate_session_kill(std::string_view pid, bool terminate) {
    // O pid vem de uma celula da grade: so' digitos entram no comando.
    if (pid.empty() || pid.size() > 10 ||
        !std::all_of(pid.begin(), pid.end(),
                     [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
        return refused("the process id must be a number");
    }

    AlterScript script =
        single(std::string("SELECT ") +
               (terminate ? "pg_terminate_backend(" : "pg_cancel_backend(") +
               std::string(pid) + ");");
    if (terminate) {
        script.destructive.push_back(0);
        script.warnings.push_back(
            "the session is closed and its open transaction is rolled back");
    }
    return script;
}

} // namespace otter::db
