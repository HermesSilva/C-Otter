// O editor de objeto contra um PostgreSQL de verdade.
//
// db/object_info monta ~80 consultas ao catalogo e ~15 formas de ALTER, GRANT
// e DROP. O teste unitario confere o TEXTO; so' o servidor diz se o texto e'
// SQL valido -- e se o DDL que a aba mostra realmente recria o objeto.
//
// Tres provas por objeto da fixture:
//
//   1. as quatro consultas (propriedades, DDL, permissoes, estatisticas) sao
//      aceitas e devolvem o objeto certo;
//   2. cada alteracao que o editor oferece e' aceita;
//   3. o DDL mostrado RECRIA o objeto: apaga-se o original e roda-se o DDL.
//
// Tudo dentro de BEGIN ... ROLLBACK: a fixture sai intacta.
//
//     otter_tests_object_live [<host> <port> <db> <user> <pass>]
#include "live_connect.hpp"

#include "db/connection_config.hpp"
#include "db/ddl.hpp"
#include "db/object_ddl.hpp"
#include "db/object_info.hpp"
#include "db/object_info_load.hpp"

#include <string>
#include <vector>

using namespace otter::db;
using live::check;

namespace {

constexpr const char* kSchema = "otter_test";

Holt* g_holt = nullptr;

ObjectRef object(ObjectType type, std::string name, std::string parent = {},
                 std::string signature = {}, std::string schema = kSchema) {
    ObjectRef ref;
    ref.type      = type;
    ref.schema    = std::move(schema);
    ref.name      = std::move(name);
    ref.parent    = std::move(parent);
    ref.signature = std::move(signature);
    return ref;
}

std::string label(const ObjectRef& ref) {
    return std::string(to_string(ref.type)) + " " + ref.title();
}

// Uma consulta de um valor so'. Vazio (e a falha impressa) quando o servidor
// recusa.
std::string scalar(const std::string& sql) {
    auto rs = g_holt->query(sql);
    if (!rs) {
        std::printf("         %s\n", rs.error().to_string().c_str());
        return {};
    }
    if (rs->row_count() == 0 || rs->column_count() == 0 || rs->is_null(0, 0)) return {};
    return std::string(rs->text(0, 0));
}

bool run(const std::string& sql) {
    auto status = g_holt->execute(sql);
    if (!status) {
        std::printf("         %s\n         >> %s\n", status.error().to_string().c_str(),
                    sql.c_str());
        return false;
    }
    return true;
}

// Roda o script e desfaz. O servidor aceitar e' a prova; o efeito nao fica.
bool accepted(const AlterScript& script) {
    if (!script.ok()) {
        std::printf("         gerador recusou: %s\n", script.error.c_str());
        return false;
    }
    if (!run("BEGIN")) return false;
    bool ok = true;
    for (const std::string& statement : script.statements) {
        if (!run(statement)) { ok = false; break; }
    }
    (void)g_holt->execute("ROLLBACK");
    return ok;
}

// --- 1. As quatro consultas ---------------------------------------------------------

void check_queries(const ObjectRef& ref) {
    const std::string who = label(ref);

    {
        auto rs = g_holt->query(pg_properties_query(ref));
        if (!rs) std::printf("         %s\n", rs.error().to_string().c_str());
        check(rs.has_value(), who + ": propriedades aceitas");

        // UMA linha, e do objeto pedido: zero seria editor vazio, duas seria
        // a sobrecarga errada.
        const bool one = rs && rs->row_count() == 1;
        check(one, who + ": exatamente uma linha");
        if (one) {
            check(rs->text(0, 0) == ref.name, who + ": a linha e' a do objeto");
            bool labelled = true;
            for (std::size_t c = 0; c < rs->column_count(); ++c) {
                // Coluna sem alias sairia como "?column?" na tela.
                const std::string name(rs->column(c).info().name);
                if (name.empty() || name == "?column?" || name == "case" ||
                    name == "coalesce") {
                    labelled = false;
                }
            }
            check(labelled, who + ": toda coluna tem rotulo");
        }
    }

    const std::string ddl_sql = pg_ddl_query(ref);
    if (!ddl_sql.empty()) {
        auto rs = g_holt->query(ddl_sql);
        if (!rs) std::printf("         %s\n", rs.error().to_string().c_str());
        check(rs && rs->row_count() == 1 && !rs->is_null(0, 0) &&
                  !rs->text(0, 0).empty(),
              who + ": DDL devolvido");
    }

    const std::string acl_sql = pg_permissions_query(ref);
    if (!acl_sql.empty()) {
        auto rs = g_holt->query(acl_sql);
        if (!rs) std::printf("         %s\n", rs.error().to_string().c_str());
        check(rs && rs->column_count() == 4, who + ": permissoes aceitas");
        // Coluna nao tem ACL padrao; todo o resto mostra ao menos o dono.
        if (rs && ref.type != ObjectType::column) {
            check(rs->row_count() > 0, who + ": o dono aparece nas permissoes");
        }
    }

    const std::string stat_sql = pg_statistics_query(ref);
    if (!stat_sql.empty()) {
        auto rs = g_holt->query(stat_sql);
        if (!rs) std::printf("         %s\n", rs.error().to_string().c_str());
        check(rs && rs->row_count() == 1, who + ": estatisticas aceitas");
    }
}

// --- 2. As alteracoes ---------------------------------------------------------------

void check_edits(const ObjectRef& ref, const std::string& owner) {
    const std::string who = label(ref);

    if (editable_property(ref.type, "Name") != ObjectEdit::none) {
        check(accepted(generate_object_rename(ref, ref.name + "_x")), who + ": renomear");
    }
    if (editable_property(ref.type, "Comment") != ObjectEdit::none) {
        check(accepted(generate_object_comment(ref, "editor's note")), who + ": comentar");
        check(accepted(generate_object_comment(ref, "")), who + ": remover o comentario");
    }
    if (editable_property(ref.type, "Owner") != ObjectEdit::none) {
        check(accepted(generate_object_owner(ref, owner)), who + ": trocar o dono");
    }
    // Sequencia de coluna serial/identity nao muda de schema sozinha ("esta'
    // vinculada a' tabela"): vai junto com a tabela. A solta e' conferida
    // adiante.
    if (editable_property(ref.type, "Schema") != ObjectEdit::none &&
        ref.type != ObjectType::sequence) {
        check(accepted(generate_object_schema(ref, "public")), who + ": mover de schema");
    }

    for (const std::string_view privilege : privileges_for(ref.type)) {
        const std::string name(privilege);
        check(accepted(generate_grant(ref, privilege, "PUBLIC")),
              who + ": GRANT " + name);
        check(accepted(generate_revoke(ref, privilege, "PUBLIC")),
              who + ": REVOKE " + name);
    }
    if (!privileges_for(ref.type).empty()) {
        check(accepted(generate_grant(ref, "ALL", owner, true)),
              who + ": GRANT ALL WITH GRANT OPTION");
    }
}

// --- 3. O DDL recria o objeto ----------------------------------------------------------

// Apaga o objeto e roda o DDL que a aba mostra. `cascade`: o que depende dele
// vai junto -- tudo volta no ROLLBACK.
void check_roundtrip(const ObjectRef& ref, bool cascade = true) {
    const std::string who = label(ref);

    const std::string ddl = scalar(pg_ddl_query(ref));
    if (ddl.empty()) {
        check(false, who + ": DDL para recriar");
        return;
    }

    const AlterScript drop = generate_object_drop(ref, cascade);
    bool ok = drop.ok() && run("BEGIN");
    if (ok) {
        ok = run(drop.statements.front());
        check(ok, who + ": DROP aceito");

        // O objeto sumiu mesmo? Um DROP do objeto errado passaria batido.
        if (ok) {
            auto gone = g_holt->query(pg_properties_query(ref));
            // Com o objeto apagado, o regclass/regprocedure da consulta FALHA
            // -- e a falha aborta a transacao. SAVEPOINT nao: mais simples
            // recomecar a transacao e repetir o DROP.
            if (!gone) {
                (void)g_holt->execute("ROLLBACK");
                ok = run("BEGIN") && run(drop.statements.front());
            } else {
                check(gone->row_count() == 0, who + ": o DROP removeu o objeto");
            }
        }
        if (ok) {
            ok = run(ddl);
            check(ok, who + ": o DDL mostrado recria o objeto");
        }
        if (ok) {
            auto back = g_holt->query(pg_properties_query(ref));
            check(back && back->row_count() == 1, who + ": o objeto voltou");
        }
        (void)g_holt->execute("ROLLBACK");
    } else {
        check(false, who + ": DROP gerado");
    }
}

} // namespace

int main(int argc, char** argv) {
    const ConnConfig config = live::config_from(argc, argv);

    auto holt = postgres_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexao falhou: %s\n", holt.error().to_string().c_str());
        return 2;
    }
    g_holt = holt->get();
    set_sql_dialect(QuoteStyle::double_quotes);

    // --- Inicializacao da sessao do perfil ---------------------------------------
    //
    // "Default schema", "Read-only connection", consultas de inicializacao e
    // auto-commit do dialogo de conexao: eram gravados e NADA os aplicava.
    // Conferido no servidor, numa segunda conexao.
    {
        ConnectionProfile profile;
        profile.default_schema    = "otter_test";
        profile.read_only         = true;
        profile.auto_commit       = false;
        profile.bootstrap_queries = "SET application_name = 'otter-setup'";

        ConnConfig setup = config;
        const ConnConfig from_profile = profile.to_conn_config();
        setup.init_statements = from_profile.init_statements;
        setup.auto_commit     = from_profile.auto_commit;

        auto second = postgres_driver().connect(setup);
        check(second.has_value(), "setup: segunda conexao abre");
        if (second) {
            Holt& other = **second;
            check(apply_session_setup(other, setup).has_value(),
                  "setup: as instrucoes do perfil rodam");
            const auto value = [&other](const char* sql) {
                auto result = other.query_internal(sql);
                return result && result->row_count() > 0 ? std::string(result->text(0, 0))
                                                         : std::string("(erro)");
            };
            check(value("SHOW transaction_read_only") == "on",
                  "setup: a sessao e' somente leitura no servidor");
            check(value("SELECT current_schema()") == "otter_test",
                  "setup: o schema padrao vale no servidor");
            check(other.current_schema() == "otter_test",
                  "setup: a barra de status acompanha o search_path");
            check(value("SHOW application_name") == "otter-setup",
                  "setup: a consulta de inicializacao rodou");
            check(!other.auto_commit(), "setup: auto-commit desligado pelo perfil");

            check(!other.execute("CREATE TABLE otter_test.nao_pode (x int)").has_value(),
                  "setup: o servidor recusa DDL na sessao somente leitura");
            (void)other.rollback();
        }

        // Instrucao que falha derruba a inicializacao NOMEANDO-SE...
        setup.init_statements = {"SELECT * FROM tabela_que_nao_existe_xyz"};
        setup.auto_commit     = true;
        auto third = postgres_driver().connect(setup);
        if (third) {
            const otter::Status failed = apply_session_setup(**third, setup);
            check(!failed.has_value() &&
                      failed.error().to_string().find("tabela_que_nao_existe_xyz") !=
                          std::string::npos,
                  "setup: a falha nomeia a instrucao");
            // ...salvo com "Ignore errors".
            (void)(*third)->rollback();
            setup.ignore_init_errors = true;
            check(apply_session_setup(**third, setup).has_value(),
                  "setup: 'ignore errors' deixa a conexao seguir");
        }
    }

    // As rotinas da fixture citam `pedido` e `cliente` SEM schema, e o
    // servidor valida o corpo de uma funcao SQL ao cria-la: recriar exige o
    // mesmo search_path com que a fixture foi aplicada. Vale para o usuario
    // tambem -- e' o comportamento do servidor, igual no DBeaver.
    (void)g_holt->execute("SET search_path = otter_test, public");

    const std::string database = scalar("SELECT current_database()");
    const std::string user     = scalar("SELECT current_user");
    std::printf("banco %s, usuario %s, schema %s\n\n", database.c_str(), user.c_str(),
                kSchema);

    // Nomes que o servidor gera: descobertos, nao presumidos.
    const std::string primary_key = scalar(
        "SELECT conname FROM pg_constraint"
        " WHERE conrelid = 'otter_test.cliente'::regclass AND contype = 'p'");
    const std::string foreign_key = scalar(
        "SELECT conname FROM pg_constraint"
        " WHERE conrelid = 'otter_test.pedido'::regclass AND contype = 'f' LIMIT 1");
    const std::string check_constraint = scalar(
        "SELECT conname FROM pg_constraint"
        " WHERE conrelid = 'otter_test.pedido'::regclass AND contype = 'c' LIMIT 1");
    const std::string sequence = scalar(
        "SELECT c.relname FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = 'otter_test' AND c.relkind = 'S' ORDER BY 1 LIMIT 1");

    check(!primary_key.empty(), "a fixture tem chave primaria em cliente");
    check(!foreign_key.empty(), "a fixture tem chave estrangeira em pedido");

    std::vector<ObjectRef> objects = {
        object(ObjectType::table, "cliente"),
        object(ObjectType::table, "lancamento"),          // particionada
        object(ObjectType::table, "lancamento_2025"),     // particao
        object(ObjectType::view, "vw_cliente_ativo"),
        object(ObjectType::materialized_view, "mvw_faturamento_mes"),
        object(ObjectType::foreign_table, "importacao"),
        object(ObjectType::column, "email", "cliente"),
        object(ObjectType::index, "ix_pedido_cliente"),
        object(ObjectType::constraint, primary_key, "cliente"),
        object(ObjectType::foreign_key, foreign_key, "pedido"),
        object(ObjectType::trigger, "tg_pedido_auditoria", "pedido"),
        object(ObjectType::rule, "rl_documento_noop", "documento"),
        object(ObjectType::policy, "pl_documento_leitura", "documento"),
        object(ObjectType::function, "fn_credito_disponivel", {}, "integer"),
        object(ObjectType::function, "fn_dividir", {}, "integer, integer"),   // OUT
        object(ObjectType::function, "fn_pedido_auditoria", {}, ""),
        object(ObjectType::procedure, "sp_cancelar_pedido", {}, "integer"),
        object(ObjectType::aggregate, "soma_total", {}, "numeric"),
        object(ObjectType::data_type, "situacao_pedido"),   // enum
        object(ObjectType::data_type, "cnpj"),              // dominio
        object(ObjectType::data_type, "endereco"),          // composto
        object(ObjectType::schema, kSchema, {}, {}, ""),
        object(ObjectType::database, database, {}, {}, ""),
        object(ObjectType::role, user, {}, {}, ""),
        object(ObjectType::extension, "file_fdw", {}, {}, ""),
        object(ObjectType::tablespace, "pg_default", {}, {}, ""),
        object(ObjectType::event_trigger, "otter_evento_ddl", {}, {}, ""),
        object(ObjectType::foreign_server, "otter_arquivos", {}, {}, ""),
        object(ObjectType::foreign_data_wrapper, "file_fdw", {}, {}, ""),
        object(ObjectType::user_mapping, "public", "otter_arquivos", {}, ""),
        object(ObjectType::language, "plpgsql", {}, {}, ""),
    };
    if (!check_constraint.empty()) {
        objects.push_back(object(ObjectType::constraint, check_constraint, "pedido"));
    }
    if (!sequence.empty()) {
        objects.push_back(object(ObjectType::sequence, sequence));
    }

    std::printf("\n1. consultas\n");
    for (const ObjectRef& ref : objects) check_queries(ref);

    std::printf("\n2. alteracoes (cada uma em BEGIN ... ROLLBACK)\n");
    for (const ObjectRef& ref : objects) {
        // Do servidor inteiro, e nao da fixture: nao se renomeia o banco em
        // uso, o papel da propria sessao, o tablespace padrao nem a linguagem
        // de sistema -- nem dentro de transacao. Conferidos adiante com
        // objetos criados para isso.
        if (ref.type == ObjectType::database || ref.type == ObjectType::role ||
            ref.type == ObjectType::tablespace || ref.type == ObjectType::language) {
            continue;
        }
        check_edits(ref, user);
    }

    std::printf("\n   papel criado para o teste\n");
    {
        // CREATE ROLE e' transacional: o papel some no ROLLBACK.
        const ObjectRef role = object(ObjectType::role, "otter_papel_tmp", {}, {}, "");
        const auto in_transaction = [&](const AlterScript& script, const char* what) {
            bool ok = script.ok() && run("BEGIN") &&
                      run("CREATE ROLE otter_papel_tmp NOLOGIN");
            if (ok) ok = run(script.statements.front());
            (void)g_holt->execute("ROLLBACK");
            check(ok, std::string("role: ") + what);
        };
        in_transaction(generate_object_rename(role, "otter_papel_tmp2"), "renomear");
        in_transaction(generate_object_comment(role, "temporario"), "comentar");
        in_transaction(generate_object_drop(role), "remover");
        in_transaction(generate_grant(object(ObjectType::table, "cliente"), "SELECT",
                                      "otter_papel_tmp", true),
                       "GRANT a um papel, WITH GRANT OPTION");
        in_transaction(generate_grant(object(ObjectType::database, database, {}, {}, ""),
                                      "CONNECT", "otter_papel_tmp"),
                       "GRANT CONNECT no banco");
        in_transaction(generate_grant(object(ObjectType::language, "plpgsql", {}, {}, ""),
                                      "USAGE", "otter_papel_tmp"),
                       "GRANT USAGE na linguagem");
        in_transaction(generate_grant(object(ObjectType::tablespace, "pg_default", {}, {}, ""),
                                      "CREATE", "otter_papel_tmp"),
                       "GRANT CREATE no tablespace");
        in_transaction(generate_object_owner(object(ObjectType::table, "cliente"),
                                             "otter_papel_tmp"),
                       "virar dono de uma tabela");
    }

    std::printf("\n   sequencia solta\n");
    {
        const ObjectRef loose = object(ObjectType::sequence, "otter_seq_tmp");
        bool ok = run("BEGIN") && run("CREATE SEQUENCE otter_test.otter_seq_tmp");
        if (ok) {
            auto rs = g_holt->query(pg_properties_query(loose));
            check(rs && rs->row_count() == 1, "sequence: propriedades da sequencia solta");
            ok = run(generate_object_schema(loose, "public").statements.front());
        }
        (void)g_holt->execute("ROLLBACK");
        check(ok, "sequence: mover de schema");
    }

    std::printf("\n   tablespace das relacoes\n");
    check(accepted(generate_object_tablespace(object(ObjectType::table, "documento"),
                                              "pg_default")),
          "table: SET TABLESPACE");
    check(accepted(generate_object_tablespace(object(ObjectType::index, "ix_pedido_cliente"),
                                              "pg_default")),
          "index: SET TABLESPACE");
    check(accepted(generate_object_tablespace(
              object(ObjectType::materialized_view, "mvw_faturamento_mes"), "pg_default")),
          "materialized view: SET TABLESPACE");

    std::printf("\n3. o DDL mostrado recria o objeto (DROP + DDL, em ROLLBACK)\n");
    check_roundtrip(object(ObjectType::view, "vw_cliente_ativo"));
    check_roundtrip(object(ObjectType::materialized_view, "mvw_faturamento_mes"));
    check_roundtrip(object(ObjectType::index, "ix_pedido_cliente"));
    check_roundtrip(object(ObjectType::constraint, primary_key, "cliente"));
    check_roundtrip(object(ObjectType::foreign_key, foreign_key, "pedido"));
    if (!check_constraint.empty()) {
        check_roundtrip(object(ObjectType::constraint, check_constraint, "pedido"));
    }
    check_roundtrip(object(ObjectType::trigger, "tg_pedido_auditoria", "pedido"));
    check_roundtrip(object(ObjectType::rule, "rl_documento_noop", "documento"));
    check_roundtrip(object(ObjectType::policy, "pl_documento_leitura", "documento"));
    check_roundtrip(object(ObjectType::function, "fn_credito_disponivel", {}, "integer"));
    check_roundtrip(object(ObjectType::function, "fn_dividir", {}, "integer, integer"));
    check_roundtrip(object(ObjectType::procedure, "sp_cancelar_pedido", {}, "integer"));
    check_roundtrip(object(ObjectType::aggregate, "soma_total", {}, "numeric"));
    check_roundtrip(object(ObjectType::data_type, "situacao_pedido"));
    check_roundtrip(object(ObjectType::data_type, "cnpj"));
    check_roundtrip(object(ObjectType::data_type, "endereco"));
    check_roundtrip(object(ObjectType::event_trigger, "otter_evento_ddl", {}, {}, ""));
    check_roundtrip(object(ObjectType::foreign_server, "otter_arquivos", {}, {}, ""));
    check_roundtrip(object(ObjectType::user_mapping, "public", "otter_arquivos", {}, ""),
                    false);
    check_roundtrip(object(ObjectType::extension, "file_fdw", {}, {}, ""));
    check_roundtrip(object(ObjectType::column, "email", "cliente"));
    if (!sequence.empty()) check_roundtrip(object(ObjectType::sequence, sequence));

    // O DDL de TABELA nao vem do servidor: e' montado de quatro leituras do
    // catalogo. E' o mais facil de sair quase certo -- indice de constraint
    // repetido, chave estrangeira faltando, PARTITION BY perdido.
    std::printf("\n   DDL de tabela, montado no cliente\n");
    {
        PostgresCatalog catalog(*g_holt);
        for (const char* name : {"cliente", "pedido", "documento", "lancamento"}) {
            const ObjectRef ref = object(ObjectType::table, name);
            const std::string who = label(ref);

            const ObjectInfo info = load_object_info(catalog, *g_holt, ref);
            if (!info.error.empty()) std::printf("         %s\n", info.error.c_str());
            check(info.error.empty(), who + ": leitura completa sem erro");
            check(!info.properties.empty() && info.properties.front().value == name,
                  who + ": propriedades");
            check(info.has_permissions && !info.permissions.empty(),
                  who + ": permissoes com o dono");
            check(!info.statistics.empty(), who + ": estatisticas");

            const std::string before = scalar(
                "SELECT count(*) FROM pg_constraint WHERE conrelid = " +
                std::string("'otter_test.") + name + "'::regclass");
            const std::string indexes_before = scalar(
                "SELECT count(*) FROM pg_index WHERE indrelid = " +
                std::string("'otter_test.") + name + "'::regclass");

            bool ok = run("BEGIN") &&
                      run(generate_object_drop(ref, true).statements.front());
            if (ok) ok = run(info.ddl);
            check(ok, who + ": o DDL recria a tabela");
            if (ok) {
                check(scalar("SELECT count(*) FROM pg_constraint WHERE conrelid = " +
                             std::string("'otter_test.") + name + "'::regclass") == before,
                      who + ": mesmas constraints e chaves estrangeiras");
                check(scalar("SELECT count(*) FROM pg_index WHERE indrelid = " +
                             std::string("'otter_test.") + name + "'::regclass") ==
                          indexes_before,
                      who + ": mesmos indices");
            }
            (void)g_holt->execute("ROLLBACK");
        }

        // Objeto que nao existe: erro dito, e nao editor vazio.
        const ObjectInfo missing =
            load_object_info(catalog, *g_holt, object(ObjectType::schema, "nao_existe_xyz",
                                                      {}, {}, ""));
        check(!missing.error.empty() && missing.properties.empty(),
              "objeto inexistente devolve erro");
    }

    // --- 5. CREATE por dialogo e ferramentas (db/object_ddl.hpp) ------------------
    std::printf("\n5. criar objetos e ferramentas\n");
    {
        check(accepted(generate_create_schema("otter_esquema_tmp", user)),
              "CREATE SCHEMA com dono");

        NewRole group;
        group.name    = "otter_papel_tmp";
        group.is_user = false;
        check(accepted(generate_create_role(group)), "CREATE ROLE (grupo)");

        NewRole login;
        login.name     = "otter_papel_tmp";
        login.password = "s'enha";
        check(accepted(generate_create_role(login)), "CREATE ROLE LOGIN PASSWORD");

        // Atributos, senha e associacao: sobre um papel criado na transacao.
        const auto on_role = [&](const AlterScript& script, const std::string& what) {
            bool ok = script.ok() && run("BEGIN") &&
                      run("CREATE ROLE otter_papel_tmp NOLOGIN") &&
                      run("CREATE ROLE otter_grupo_tmp NOLOGIN");
            if (ok) ok = run(script.statements.front());
            (void)g_holt->execute("ROLLBACK");
            check(ok, what);
        };
        for (const char* option : {"Can login", "Superuser", "Create database",
                                   "Create role", "Inherit", "Replication",
                                   "Bypass RLS"}) {
            on_role(generate_role_option("otter_papel_tmp", option, true),
                    std::string("ALTER ROLE: ") + option + " = sim");
            on_role(generate_role_option("otter_papel_tmp", option, false),
                    std::string("ALTER ROLE: ") + option + " = nao");
        }
        on_role(generate_role_password("otter_papel_tmp", "nova"), "ALTER ROLE PASSWORD");
        on_role(generate_role_password("otter_papel_tmp", ""), "ALTER ROLE PASSWORD NULL");
        on_role(generate_role_membership("otter_grupo_tmp", "otter_papel_tmp", true),
                "GRANT grupo TO membro");

        // A extensao ja' existe na fixture: apaga e recria, na transacao.
        {
            bool ok = run("BEGIN") && run("DROP EXTENSION file_fdw CASCADE");
            if (ok) ok = run(generate_create_extension("file_fdw", kSchema)
                                 .statements.front());
            (void)g_holt->execute("ROLLBACK");
            check(ok, "CREATE EXTENSION ... SCHEMA");
        }

        check(accepted(generate_create_event_trigger("otter_evento_tmp", "sql_drop",
                                                     "otter_test.fn_evento_ddl")),
              "CREATE EVENT TRIGGER");

        NewPolicy policy;
        policy.schema           = kSchema;
        policy.table            = "documento";
        policy.name             = "pl_tmp";
        policy.command          = "UPDATE";
        policy.permissive       = false;
        policy.roles            = user + ", PUBLIC";
        policy.using_expression = "documento_id > 0";
        policy.check_expression = "titulo IS NOT NULL";
        check(accepted(generate_create_policy(policy)), "CREATE POLICY (restritiva)");

        check(accepted(generate_create_materialized_view(
                  kSchema, "mvw_tmp", "SELECT count(*) AS n FROM otter_test.cliente;")),
              "CREATE MATERIALIZED VIEW");

        // Os esqueletos de rotina: precisam COMPILAR como estao, senao o
        // usuario comeca de um texto que o servidor recusa.
        for (const char* language : {"sql", "plpgsql"}) {
            for (const bool procedure : {false, true}) {
                NewRoutine routine;
                routine.schema    = kSchema;
                routine.name      = "rotina_tmp";
                routine.procedure = procedure;
                routine.language  = language;

                AlterScript script;
                script.statements.push_back(routine_template(routine));
                check(accepted(script), std::string("esqueleto de ") +
                                            (procedure ? "procedure" : "function") +
                                            " em " + language);
            }
        }

        // Ferramentas.
        check(accepted(generate_analyze(kSchema, "cliente")), "ANALYZE VERBOSE tabela");
        check(accepted(generate_truncate(kSchema, "documento_fiscal", {})),
              "TRUNCATE CONTINUE IDENTITY RESTRICT");
        TruncateOptions truncate;
        truncate.only = truncate.restart_identity = truncate.cascade = true;
        check(accepted(generate_truncate(kSchema, "cliente", truncate)),
              "TRUNCATE ONLY ... RESTART IDENTITY CASCADE");
        check(accepted(generate_refresh_materialized_view(kSchema, "mvw_faturamento_mes")),
              "REFRESH MATERIALIZED VIEW WITH DATA");
        check(accepted(generate_refresh_materialized_view(kSchema, "mvw_faturamento_mes",
                                                          false)),
              "REFRESH MATERIALIZED VIEW WITH NO DATA");

        const ObjectRef trigger =
            object(ObjectType::trigger, "tg_pedido_auditoria", "pedido");
        check(accepted(generate_trigger_enable(trigger, false)), "DISABLE TRIGGER");
        check(accepted(generate_trigger_enable(trigger, true)), "ENABLE TRIGGER");
        const ObjectRef event =
            object(ObjectType::event_trigger, "otter_evento_ddl", {}, {}, "");
        check(accepted(generate_trigger_enable(event, false)), "ALTER EVENT TRIGGER DISABLE");

        check(accepted(generate_reindex(object(ObjectType::index, "ix_pedido_cliente"))),
              "REINDEX INDEX");
        check(accepted(generate_reindex(object(ObjectType::table, "documento"))),
              "REINDEX TABLE");

        // VACUUM recusa transacao: roda de verdade, numa tabela de 0 linhas.
        VacuumOptions vacuum;
        vacuum.analyze = true;
        check(run(generate_vacuum(kSchema, "documento", vacuum).statements.front()),
              "VACUUM (VERBOSE, ANALYZE)");
        if (PostgresCatalog(*g_holt).version().at_least(12)) {
            VacuumOptions all;
            all.freeze = all.analyze = all.disable_page_skipping = true;
            all.skip_locked = all.index_cleanup = all.truncate = true;
            check(run(generate_vacuum(kSchema, "documento", all).statements.front()),
                  "VACUUM com todas as opcoes do 12+");
        }
        (void)g_holt->take_server_output();

        // Um pid que nao existe: a funcao responde falso, sem derrubar ninguem.
        check(run(generate_session_kill("2147483600", false).statements.front()),
              "pg_cancel_backend aceito");
        (void)g_holt->take_server_output();
    }

    // A fixture saiu intacta?
    std::printf("\n4. a fixture continua inteira\n");
    check(scalar("SELECT count(*) FROM otter_test.cliente") == "3",
          "cliente ainda tem 3 linhas");
    check(scalar("SELECT count(*) FROM pg_trigger WHERE tgname = 'tg_pedido_auditoria'") ==
              "1",
          "o trigger continua la'");
    check(scalar("SELECT count(*) FROM pg_roles WHERE rolname LIKE 'otter_papel_tmp%'") ==
              "0",
          "o papel temporario nao ficou");

    std::printf("\n%d verificacoes, %d falha(s)\n", live::checks, live::failures);
    return live::failures == 0 ? 0 : 1;
}
