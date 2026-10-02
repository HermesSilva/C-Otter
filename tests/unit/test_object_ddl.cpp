// C-Otter -- testes de db/object_ddl: CREATE por dialogo e as ferramentas.
//
// O servidor confere a sintaxe em tests/integration/test_object_info_live.cpp.
// Aqui, o que o servidor NAO pega: texto livre que vira comando (injecao), a
// opcao que muda o significado em silencio (CONTINUE x RESTART IDENTITY), o
// aviso que precisa aparecer antes de um comando sem volta.
#include "test_main.hpp"

#include "db/ddl.hpp"
#include "db/object_ddl.hpp"

#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string first(const AlterScript& script) {
    return script.statements.empty() ? std::string{} : script.statements.front();
}

struct DialectGuard {
    QuoteStyle previous = sql_dialect();
    DialectGuard() { set_sql_dialect(QuoteStyle::double_quotes); }
    ~DialectGuard() { set_sql_dialect(previous); }
};

} // namespace

// --- CREATE ------------------------------------------------------------------------

OTTER_TEST(object_ddl_create_database) {
    const DialectGuard guard;

    NewDatabase database;
    OTTER_CHECK(!generate_create_database(database).ok());

    database.name = "vendas";
    OTTER_CHECK(first(generate_create_database(database)) == "CREATE DATABASE vendas;");

    database.owner       = "ana";
    database.template_db = "template0";
    database.encoding    = "UTF8";
    database.tablespace  = "rapido";
    const AlterScript script = generate_create_database(database);
    OTTER_CHECK(first(script) ==
                "CREATE DATABASE vendas\n    OWNER ana\n    TEMPLATE template0\n"
                "    ENCODING 'UTF8'\n    TABLESPACE rapido;");
    // Nao roda em transacao: o usuario em modo manual precisa saber antes.
    OTTER_CHECK(!script.warnings.empty());
}

OTTER_TEST(object_ddl_create_database_warns_about_encoding_without_template0) {
    NewDatabase database;
    database.name     = "x";
    database.encoding = "LATIN1";
    OTTER_CHECK(generate_create_database(database).warnings.size() == 2);

    database.template_db = "template0";
    OTTER_CHECK(generate_create_database(database).warnings.size() == 1);
}

OTTER_TEST(object_ddl_create_schema_extension) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_create_schema("vendas", "")) == "CREATE SCHEMA vendas;");
    OTTER_CHECK(first(generate_create_schema("Vendas 2", "ana")) ==
                "CREATE SCHEMA \"Vendas 2\" AUTHORIZATION ana;");
    OTTER_CHECK(!generate_create_schema("", "ana").ok());

    OTTER_CHECK(first(generate_create_extension("pg_trgm", "public")) ==
                "CREATE EXTENSION pg_trgm SCHEMA public;");
    // uuid-ossp tem hifen: precisa de aspas.
    OTTER_CHECK(first(generate_create_extension("uuid-ossp", "")) ==
                "CREATE EXTENSION \"uuid-ossp\";");
}

OTTER_TEST(object_ddl_create_role) {
    const DialectGuard guard;

    NewRole role;
    role.name    = "leitores";
    role.is_user = false;
    OTTER_CHECK(first(generate_create_role(role)) == "CREATE ROLE leitores;");

    role.name     = "ana";
    role.is_user  = true;
    role.password = "it's";
    const AlterScript script = generate_create_role(role);
    OTTER_CHECK(first(script) == "CREATE ROLE ana LOGIN PASSWORD 'it''s';");
    OTTER_CHECK(!script.warnings.empty());   // a senha vai no comando

    // Grupo nao tem senha, mesmo que o campo tenha ficado preenchido.
    role.is_user = false;
    OTTER_CHECK(first(generate_create_role(role)) == "CREATE ROLE ana;");
}

OTTER_TEST(object_ddl_create_tablespace) {
    const DialectGuard guard;

    NewTablespace tablespace;
    tablespace.name = "rapido";
    OTTER_CHECK(!generate_create_tablespace(tablespace).ok());   // sem local

    tablespace.location = "/mnt/ssd/pg";
    tablespace.owner    = "ana";
    tablespace.options  = "seq_page_cost=1, random_page_cost = 1.1";
    OTTER_CHECK(first(generate_create_tablespace(tablespace)) ==
                "CREATE TABLESPACE rapido\n    OWNER ana\n    LOCATION '/mnt/ssd/pg'\n"
                "    WITH (seq_page_cost = 1, random_page_cost = 1.1);");

    // As opcoes entram sem aspas: so' chave=numero passa.
    tablespace.options = "x=1); DROP TABLE t; --";
    OTTER_CHECK(!generate_create_tablespace(tablespace).ok());
    tablespace.options = "sem_igual";
    OTTER_CHECK(!generate_create_tablespace(tablespace).ok());
}

OTTER_TEST(object_ddl_routine_template) {
    const DialectGuard guard;

    NewRoutine routine;
    routine.schema = "otter_test";
    routine.name   = "fn_nova";

    std::string text = routine_template(routine);
    OTTER_CHECK(has(text, "CREATE OR REPLACE FUNCTION otter_test.fn_nova()"));
    OTTER_CHECK(has(text, "RETURNS int4"));
    OTTER_CHECK(has(text, "LANGUAGE sql"));
    OTTER_CHECK(has(text, "$function$"));
    // Corpo SQL de verdade: "BEGIN END" so' compila em plpgsql.
    OTTER_CHECK(!has(text, "BEGIN"));

    routine.language = "plpgsql";
    OTTER_CHECK(has(routine_template(routine), "BEGIN"));

    routine.procedure = true;
    text = routine_template(routine);
    OTTER_CHECK(has(text, "CREATE OR REPLACE PROCEDURE"));
    OTTER_CHECK(!has(text, "RETURNS"));   // procedure nao devolve
    OTTER_CHECK(has(text, "$procedure$"));
}

OTTER_TEST(object_ddl_create_event_trigger) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_create_event_trigger("audita", "ddl_command_end",
                                                    "otter_test.fn_evento")) ==
                "CREATE EVENT TRIGGER audita ON ddl_command_end\n"
                "    EXECUTE FUNCTION otter_test.fn_evento();");
    // O evento entra sem aspas: so' os quatro conhecidos.
    OTTER_CHECK(!generate_create_event_trigger("x", "ddl; DROP", "f").ok());
    OTTER_CHECK(!generate_create_event_trigger("x", "sql_drop", "").ok());
    OTTER_CHECK(!generate_create_event_trigger("", "sql_drop", "f").ok());
    OTTER_CHECK(event_trigger_events().size() == 4);
}

OTTER_TEST(object_ddl_create_policy) {
    const DialectGuard guard;

    NewPolicy policy;
    policy.schema           = "s";
    policy.table            = "doc";
    policy.name             = "so_meus";
    policy.command          = "SELECT";
    policy.roles            = "ana, Grupo A, PUBLIC";
    policy.using_expression = "dono = current_user";

    OTTER_CHECK(first(generate_create_policy(policy)) ==
                "CREATE POLICY so_meus ON s.doc\n    AS PERMISSIVE\n    FOR SELECT\n"
                "    TO ana, \"Grupo A\", PUBLIC\n    USING (dono = current_user);");

    // Sem papeis = PUBLIC.
    policy.roles.clear();
    OTTER_CHECK(has(first(generate_create_policy(policy)), "TO PUBLIC"));

    // O servidor recusaria; aqui a recusa vem com o motivo.
    policy.check_expression = "true";
    OTTER_CHECK(!generate_create_policy(policy).ok());

    policy.command = "INSERT";
    OTTER_CHECK(!generate_create_policy(policy).ok());   // USING em INSERT
    policy.using_expression.clear();
    OTTER_CHECK(generate_create_policy(policy).ok());

    policy.command = "MERGE";
    OTTER_CHECK(!generate_create_policy(policy).ok());
}

OTTER_TEST(object_ddl_create_materialized_view) {
    const DialectGuard guard;

    // O ';' final do texto colado nao pode ficar no meio do comando.
    OTTER_CHECK(first(generate_create_materialized_view("s", "mv", "SELECT 1 AS a;  ")) ==
                "CREATE MATERIALIZED VIEW s.mv AS\nSELECT 1 AS a\nWITH DATA;");
    OTTER_CHECK(has(first(generate_create_materialized_view("s", "mv", "SELECT 1", false)),
                    "WITH NO DATA"));
    OTTER_CHECK(!generate_create_materialized_view("s", "mv", "  ; ").ok());
}

// --- Papel -------------------------------------------------------------------------

OTTER_TEST(object_ddl_role_options) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_role_option("ana", "Can login", false)) ==
                "ALTER ROLE ana NOLOGIN;");
    OTTER_CHECK(first(generate_role_option("ana", "Create database", true)) ==
                "ALTER ROLE ana CREATEDB;");
    OTTER_CHECK(!generate_role_option("ana", "Superuser", true).warnings.empty());
    OTTER_CHECK(generate_role_option("ana", "Superuser", false).warnings.empty());
    OTTER_CHECK(!generate_role_option("ana", "Object ID", true).ok());

    // Todo rotulo booleano que a consulta de propriedades devolve precisa de
    // palavra-chave: senao a caixa na tela nao gravaria nada.
    for (const char* label : {"Can login", "Superuser", "Create database", "Create role",
                              "Inherit", "Replication", "Bypass RLS"}) {
        OTTER_CHECK(!role_option_keyword(label, true).empty());
        OTTER_CHECK(role_option_keyword(label, true) != role_option_keyword(label, false));
        OTTER_CHECK(has(pg_properties_query(ObjectRef{ObjectType::role, "", "r", "", ""}),
                        std::string("\"") + label + "\""));
    }
}

OTTER_TEST(object_ddl_role_password_and_membership) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_role_password("ana", "s'1")) ==
                "ALTER ROLE ana PASSWORD 's''1';");
    OTTER_CHECK(first(generate_role_password("ana", "")) == "ALTER ROLE ana PASSWORD NULL;");

    OTTER_CHECK(first(generate_role_membership("leitores", "ana", true)) ==
                "GRANT leitores TO ana;");
    OTTER_CHECK(first(generate_role_membership("leitores", "ana", false)) ==
                "REVOKE leitores FROM ana;");
    OTTER_CHECK(!generate_role_membership("ana", "ana", true).ok());
}

// --- Tools -------------------------------------------------------------------------

OTTER_TEST(object_ddl_vacuum_matches_dbeaver) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_vacuum("s", "t", {})) == "VACUUM (VERBOSE) s.t;");

    VacuumOptions all;
    all.full = all.freeze = all.analyze = true;
    all.disable_page_skipping = all.skip_locked = all.index_cleanup = all.truncate = true;
    const AlterScript script = generate_vacuum("s", "t", all);
    OTTER_CHECK(first(script) ==
                "VACUUM (FULL, FREEZE, VERBOSE, ANALYZE, DISABLE_PAGE_SKIPPING, "
                "SKIP_LOCKED, INDEX_CLEANUP, TRUNCATE) s.t;");
    OTTER_CHECK(script.warnings.size() == 2);   // sem transacao + trava do FULL

    // Sem tabela: o banco inteiro.
    OTTER_CHECK(first(generate_vacuum("", "", {})) == "VACUUM (VERBOSE);");
    OTTER_CHECK(first(generate_analyze("s", "t")) == "ANALYZE VERBOSE s.t;");
    OTTER_CHECK(first(generate_analyze("", "")) == "ANALYZE VERBOSE;");
}

OTTER_TEST(object_ddl_truncate) {
    const DialectGuard guard;

    // O padrao e' o conservador, escrito por extenso -- como no DBeaver.
    AlterScript script = generate_truncate("s", "t", {});
    OTTER_CHECK(first(script) == "TRUNCATE TABLE s.t CONTINUE IDENTITY RESTRICT;");
    OTTER_CHECK(script.has_destructive());
    OTTER_CHECK(!script.warnings.empty());

    TruncateOptions options;
    options.only = options.restart_identity = options.cascade = true;
    script = generate_truncate("s", "t", options);
    OTTER_CHECK(first(script) == "TRUNCATE TABLE ONLY s.t RESTART IDENTITY CASCADE;");
    OTTER_CHECK(script.warnings.size() == 2);

    OTTER_CHECK(!generate_truncate("s", "", {}).ok());
}

OTTER_TEST(object_ddl_refresh_trigger_reindex) {
    const DialectGuard guard;

    OTTER_CHECK(first(generate_refresh_materialized_view("s", "mv")) ==
                "REFRESH MATERIALIZED VIEW s.mv WITH DATA;");
    OTTER_CHECK(!generate_refresh_materialized_view("s", "mv", false).warnings.empty());

    ObjectRef trigger{ObjectType::trigger, "s", "tg", "pedido", ""};
    OTTER_CHECK(first(generate_trigger_enable(trigger, false)) ==
                "ALTER TABLE s.pedido DISABLE TRIGGER tg;");
    OTTER_CHECK(first(generate_trigger_enable(trigger, true)) ==
                "ALTER TABLE s.pedido ENABLE TRIGGER tg;");

    ObjectRef event{ObjectType::event_trigger, "", "ev", "", ""};
    OTTER_CHECK(first(generate_trigger_enable(event, false)) ==
                "ALTER EVENT TRIGGER ev DISABLE;");
    OTTER_CHECK(!generate_trigger_enable(ObjectRef{ObjectType::table, "s", "t", "", ""},
                                         true)
                     .ok());

    OTTER_CHECK(first(generate_reindex(ObjectRef{ObjectType::index, "s", "ix", "", ""})) ==
                "REINDEX INDEX s.ix;");
    OTTER_CHECK(first(generate_reindex(ObjectRef{ObjectType::table, "s", "t", "", ""})) ==
                "REINDEX TABLE s.t;");
    OTTER_CHECK(!generate_reindex(ObjectRef{ObjectType::view, "s", "v", "", ""}).ok());
}

OTTER_TEST(object_ddl_session_kill_takes_only_a_number) {
    OTTER_CHECK(first(generate_session_kill("1234", false)) ==
                "SELECT pg_cancel_backend(1234);");

    const AlterScript kill = generate_session_kill("1234", true);
    OTTER_CHECK(first(kill) == "SELECT pg_terminate_backend(1234);");
    OTTER_CHECK(kill.has_destructive());

    // O pid vem de uma celula da grade.
    OTTER_CHECK(!generate_session_kill("1); DROP TABLE t; --", true).ok());
    OTTER_CHECK(!generate_session_kill("", true).ok());
    OTTER_CHECK(!generate_session_kill("-1", true).ok());
    OTTER_CHECK(!generate_session_kill("12345678901", true).ok());
}
