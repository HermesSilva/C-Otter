// Executa contra um MySQL de verdade o que db/mysql_object.cpp GERA: o perfil
// MySQL do editor de objeto, dos dialogos de criacao, das ferramentas, das
// sessoes e do cliente nativo.
//
// Um comando sintaticamente plausivel pode ser recusado pelo servidor -- ou,
// pior, aceito fazendo outra coisa. Aqui cada gerador roda e o EFEITO e' lido
// de volta do information_schema.
//
// Tudo acontece num banco de rascunho (`otter_scratch`, `otter_scratch2`) e
// numa conta de rascunho, criados e apagados pelo proprio teste. A fixture
// `otter_test` nao e' tocada.
//
//     otter_tests_mysql_object_live [<host> <port> <db> <user> <pass>]
//
// Sem argumentos, usa MYSQL_HOST/MYSQL_PORT/MYSQL_USER/MYSQL_PASSWORD. So'
// roda contra localhost: o teste cria e apaga bancos e contas.
#include "base/process.hpp"
#include "db/catalog_mysql.hpp"
#include "db/ddl.hpp"
#include "db/drivers/mysql.hpp"
#include "db/mysql_object.hpp"
#include "db/object_info_load.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace otter::db;

namespace {

int failures = 0;
int checks   = 0;
Holt* g_holt = nullptr;

void check(bool condition, const std::string& what) {
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string(fallback);
}

std::string scalar(const std::string& sql) {
    auto rs = g_holt->query(sql);
    if (!rs) return "(erro: " + rs.error().to_string() + ")";
    if (rs->row_count() == 0 || rs->is_null(0, 0)) return {};
    return std::string(rs->text(0, 0));
}

// Roda o script inteiro; devolve o primeiro erro, ou vazio.
std::string run(const AlterScript& script) {
    if (!script.ok()) return "(nao gerado: " + script.error + ")";
    for (const std::string& statement : script.statements) {
        if (auto status = g_holt->execute(statement); !status) {
            return status.error().to_string() + "  <<  " + statement;
        }
    }
    return {};
}

void runs(const AlterScript& script, const std::string& what) {
    const std::string error = run(script);
    check(error.empty(), what + (error.empty() ? "" : "  -- " + error));
}

ObjectRef ref_of(ObjectType type, const char* schema, const char* name,
                 const char* parent = "") {
    ObjectRef ref;
    ref.type   = type;
    ref.schema = schema;
    ref.name   = name;
    ref.parent = parent;
    return ref;
}

const std::string* property(const ObjectInfo& info, std::string_view name) {
    for (const ObjectProperty& item : info.properties) {
        if (item.name == name) return &item.value;
    }
    return nullptr;
}

// Roda um programa ate' o fim; devolve (codigo de saida, saida).
std::pair<int, std::string> run_process(const otter::ProcessOptions& options) {
    auto process = otter::Process::start(options);
    if (!process) return {-1, process.error().to_string()};
    std::string output;
    for (int i = 0; i < 600 && process->running(); ++i) {
        output += process->read_output();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    output += process->read_output();
    return {process->exit_code().value_or(-1), output};
}

constexpr const char* kDb  = "otter_scratch";
constexpr const char* kDb2 = "otter_scratch2";

} // namespace

int main(int argc, char** argv) {
    ConnConfig config;
    if (argc >= 6) {
        config.host     = argv[1];
        config.port     = static_cast<std::uint16_t>(std::atoi(argv[2]));
        config.database = argv[3];
        config.user     = argv[4];
        config.password = argv[5];
    } else {
        config.host     = env_or("MYSQL_HOST", "localhost");
        config.port     = static_cast<std::uint16_t>(
                              std::atoi(env_or("MYSQL_PORT", "3306").c_str()));
        config.database = env_or("MYSQL_DATABASE", "otter_test");
        config.user     = env_or("MYSQL_USER", "root");
        config.password = env_or("MYSQL_PASSWORD", "");
    }
    config.driver_id = "mysql";

    // Este teste cria e apaga bancos e contas: so' num servidor local.
    if (config.host != "localhost" && config.host != "127.0.0.1") {
        std::fprintf(stderr, "recusado: so' roda contra localhost (host = %s)\n",
                     config.host.c_str());
        return 2;
    }

    auto holt = mysql_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexao falhou: %s\n", holt.error().to_string().c_str());
        return 2;
    }
    g_holt = holt->get();
    set_sql_dialect(QuoteStyle::backticks);
    MysqlCatalog catalog(*g_holt);
    std::printf("Servidor: %s\n\n", g_holt->server_version().c_str());

    // Restos de uma execucao interrompida.
    (void)g_holt->execute("DROP DATABASE IF EXISTS `otter_scratch`");
    (void)g_holt->execute("DROP DATABASE IF EXISTS `otter_scratch2`");
    (void)g_holt->execute("DROP USER IF EXISTS 'otter_scratch_u'@'localhost'");
    (void)g_holt->execute("DROP USER IF EXISTS 'otter_scratch_v'@'localhost'");

    // --- Banco ------------------------------------------------------------------
    std::printf("Create database\n");
    runs(mysql_create_database(kDb, "utf8mb4", "utf8mb4_0900_ai_ci"), "CREATE DATABASE");
    check(scalar("SELECT DEFAULT_COLLATION_NAME FROM information_schema.SCHEMATA "
                 "WHERE SCHEMA_NAME = 'otter_scratch'") == "utf8mb4_0900_ai_ci",
          "o banco nasceu com o collation pedido");

    (void)g_holt->execute(
        "CREATE TABLE `otter_scratch`.`cliente` ("
        "  id INT PRIMARY KEY, nome VARCHAR(60) NOT NULL, email VARCHAR(90),"
        "  KEY ix_nome (nome), CONSTRAINT uq_email UNIQUE (email))");
    (void)g_holt->execute(
        "CREATE TABLE `otter_scratch`.`pedido` ("
        "  id INT PRIMARY KEY, cliente_id INT, total DECIMAL(10,2), obs TEXT,"
        "  CONSTRAINT fk_cliente FOREIGN KEY (cliente_id) REFERENCES cliente (id))");
    (void)g_holt->execute("INSERT INTO `otter_scratch`.`cliente` VALUES (1,'Ana','a@x'),(2,'Bia','b@x')");
    (void)g_holt->execute("INSERT INTO `otter_scratch`.`pedido` VALUES (1,1,10.50,'x'),(2,2,20,'y')");
    (void)g_holt->execute(
        "CREATE VIEW `otter_scratch`.`v_total` AS SELECT 1 AS a");

    // --- Esqueletos: o que o dialogo de criacao abre no editor -------------------
    std::printf("\nTemplates (o servidor aceita o esqueleto como esta')\n");
    {
        AlterScript script;
        script.statements = {mysql_routine_template(kDb, "p_fecha", true)};
        runs(script, "CREATE PROCEDURE do esqueleto");
        script.statements = {mysql_routine_template(kDb, "f_total", false)};
        runs(script, "CREATE FUNCTION do esqueleto");
        script.statements = {mysql_trigger_template(kDb, "pedido", "trg_pedido", "BEFORE",
                                                    "INSERT")};
        runs(script, "CREATE TRIGGER do esqueleto");
        script.statements = {mysql_event_template(kDb, "ev_limpa")};
        runs(script, "CREATE EVENT do esqueleto");
    }

    // --- Editor de objeto: propriedades, DDL, permissoes -------------------------
    std::printf("\nload_object_info\n");
    const auto loads = [&](const ObjectRef& ref, const char* label, bool with_ddl = true) {
        const ObjectInfo info = load_object_info(catalog, *g_holt, ref);
        check(info.error.empty(), std::string(label) + ": sem erro" +
                                      (info.error.empty() ? "" : "  -- " + info.error));
        check(!info.properties.empty(), std::string(label) + ": tem propriedades");
        if (with_ddl) check(!info.ddl.empty(), std::string(label) + ": tem DDL");
        return info;
    };
    {
        const ObjectInfo db = loads(ref_of(ObjectType::database, "", kDb), "banco");
        check(property(db, "Charset") != nullptr && *property(db, "Charset") == "utf8mb4",
              "banco: charset lido");
        check(db.has_permissions, "banco: tem lista de permissoes");

        const ObjectInfo table = loads(ref_of(ObjectType::table, kDb, "cliente"), "tabela");
        check(table.ddl.find("CREATE TABLE") != std::string::npos, "tabela: SHOW CREATE");
        bool comment_editable = false, name_editable = false, engine_fixed = true;
        for (const ObjectProperty& item : table.properties) {
            if (item.name == "Comment") comment_editable = item.edit == ObjectEdit::comment;
            if (item.name == "Name")    name_editable = item.edit == ObjectEdit::name;
            if (item.name == "Engine")  engine_fixed = item.edit == ObjectEdit::none;
        }
        check(comment_editable && name_editable && engine_fixed,
              "tabela: Name e Comment editaveis, Engine nao");

        loads(ref_of(ObjectType::view, kDb, "v_total"), "view");
        loads(ref_of(ObjectType::procedure, kDb, "p_fecha"), "procedure");
        loads(ref_of(ObjectType::function, kDb, "f_total"), "function");
        loads(ref_of(ObjectType::trigger, kDb, "trg_pedido"), "trigger");
        const ObjectInfo event = loads(ref_of(ObjectType::event, kDb, "ev_limpa"), "evento");
        check(property(event, "Status") != nullptr, "evento: estado lido");

        const ObjectInfo missing =
            load_object_info(catalog, *g_holt, ref_of(ObjectType::table, kDb, "nao_existe"));
        check(!missing.error.empty(), "objeto inexistente: diz que nao existe");
    }

    // --- Alterar ----------------------------------------------------------------
    std::printf("\nRename / comment\n");
    runs(mysql_object_comment(ref_of(ObjectType::table, kDb, "cliente"), "cadastro d'agua"),
         "COMMENT de tabela");
    check(scalar("SELECT TABLE_COMMENT FROM information_schema.TABLES WHERE TABLE_SCHEMA="
                 "'otter_scratch' AND TABLE_NAME='cliente'") == "cadastro d'agua",
          "o comentario (com aspa) chegou inteiro");
    runs(mysql_object_comment(ref_of(ObjectType::procedure, kDb, "p_fecha"), "fecha o mes"),
         "COMMENT de procedure");
    check(scalar("SELECT ROUTINE_COMMENT FROM information_schema.ROUTINES WHERE "
                 "ROUTINE_SCHEMA='otter_scratch' AND ROUTINE_NAME='p_fecha'") == "fecha o mes",
          "comentario da procedure gravado");
    runs(mysql_object_comment(ref_of(ObjectType::event, kDb, "ev_limpa"), "limpeza"),
         "COMMENT de evento");

    runs(mysql_object_rename(ref_of(ObjectType::view, kDb, "v_total"), "v_resumo"),
         "RENAME de view");
    check(scalar("SELECT COUNT(*) FROM information_schema.VIEWS WHERE TABLE_SCHEMA="
                 "'otter_scratch' AND TABLE_NAME='v_resumo'") == "1",
          "a view tem o nome novo");
    runs(mysql_object_rename(ref_of(ObjectType::event, kDb, "ev_limpa"), "ev_limpeza"),
         "RENAME de evento");
    check(scalar("SELECT COUNT(*) FROM information_schema.EVENTS WHERE EVENT_SCHEMA="
                 "'otter_scratch' AND EVENT_NAME='ev_limpeza'") == "1",
          "o evento tem o nome novo");
    runs(mysql_object_rename(ref_of(ObjectType::index, kDb, "ix_nome", "cliente"), "ix_cliente_nome"),
         "RENAME INDEX");
    check(scalar("SELECT COUNT(*) FROM information_schema.STATISTICS WHERE TABLE_SCHEMA="
                 "'otter_scratch' AND INDEX_NAME='ix_cliente_nome'") == "1",
          "o indice tem o nome novo");

    // --- Fonte: gravar o que o editor mostra --------------------------------------
    std::printf("\nSource (Save do editor)\n");
    {
        ObjectInfo view = load_object_info(catalog, *g_holt,
                                           ref_of(ObjectType::view, kDb, "v_resumo"));
        std::string text = view.ddl;
        const std::size_t at = text.find("select 1");
        check(at != std::string::npos, "o fonte da view traz o SELECT");
        if (at != std::string::npos) text.replace(at, 8, "select 7");
        runs(mysql_source_script(ref_of(ObjectType::view, kDb, "v_resumo"), text),
             "view: CREATE OR REPLACE com o texto do editor");
        check(scalar("SELECT a FROM `otter_scratch`.`v_resumo`") == "7",
              "a view passou a devolver o valor novo");

        for (const ObjectRef& ref :
             {ref_of(ObjectType::procedure, kDb, "p_fecha"),
              ref_of(ObjectType::function, kDb, "f_total"),
              ref_of(ObjectType::trigger, kDb, "trg_pedido"),
              ref_of(ObjectType::event, kDb, "ev_limpeza")}) {
            const ObjectInfo info = load_object_info(catalog, *g_holt, ref);
            const std::string label = std::string(to_string(ref.type));
            // O texto do SHOW CREATE, regravado como esta': DROP + CREATE.
            runs(mysql_source_script(ref, info.ddl), label + ": DROP + CREATE do fonte");
            const ObjectInfo again = load_object_info(catalog, *g_holt, ref);
            check(again.error.empty() && !again.ddl.empty(),
                  label + ": continua existindo depois de regravado");
        }
    }

    // --- Contas e privilegios -----------------------------------------------------
    std::printf("\nUsers, GRANT, REVOKE\n");
    {
        MysqlNewUser user;
        user.name     = "otter_scratch_u";
        user.host     = "localhost";
        user.password = "Otter#Scratch1";
        runs(mysql_create_user(user), "CREATE USER");

        const ObjectRef account = ref_of(ObjectType::role, "", "otter_scratch_u", "localhost");
        const ObjectInfo info = load_object_info(catalog, *g_holt, account);
        check(info.error.empty() && !info.properties.empty(), "conta: propriedades lidas");
        check(info.ddl.find("CREATE USER") != std::string::npos, "conta: SHOW CREATE USER");
        check(info.ddl.find("Otter#Scratch1") == std::string::npos,
              "conta: a senha nao aparece no DDL");

        const ObjectRef table = ref_of(ObjectType::table, kDb, "cliente");
        const std::string grantee = "'otter_scratch_u'@'localhost'";
        runs(mysql_grant(table, "SELECT", grantee, false), "GRANT SELECT na tabela");
        runs(mysql_grant(table, "UPDATE", grantee, true), "GRANT UPDATE WITH GRANT OPTION");

        ObjectInfo granted = load_object_info(catalog, *g_holt, table);
        bool has_select = false, update_grantable = false;
        for (const ObjectPermission& item : granted.permissions) {
            if (item.grantee != grantee) continue;
            if (item.privilege == "SELECT") has_select = true;
            if (item.privilege == "UPDATE") update_grantable = item.grantable;
        }
        check(has_select, "a aba Permissions ve o SELECT concedido");
        check(update_grantable, "e o WITH GRANT OPTION do UPDATE");

        runs(mysql_revoke(table, "SELECT", grantee), "REVOKE SELECT");
        granted = load_object_info(catalog, *g_holt, table);
        has_select = false;
        for (const ObjectPermission& item : granted.permissions) {
            if (item.grantee == grantee && item.privilege == "SELECT") has_select = true;
        }
        check(!has_select, "o SELECT sumiu da lista");

        const ObjectRef database = ref_of(ObjectType::database, "", kDb);
        runs(mysql_grant(database, "CREATE VIEW", grantee, false), "GRANT no banco");
        const ObjectInfo db_info = load_object_info(catalog, *g_holt, database);
        bool has_db = false;
        for (const ObjectPermission& item : db_info.permissions) {
            if (item.grantee == grantee && item.privilege == "CREATE VIEW") has_db = true;
        }
        check(has_db, "a aba Permissions do banco ve o CREATE VIEW");
        runs(mysql_grant(ref_of(ObjectType::procedure, kDb, "p_fecha"), "EXECUTE", grantee,
                         false),
             "GRANT EXECUTE na procedure");
        runs(mysql_revoke(database, "ALL", grantee), "REVOKE ALL no banco");

        runs(mysql_user_password("otter_scratch_u", "localhost", "Outra#Senha2"),
             "ALTER USER ... IDENTIFIED BY");
        {
            ConnConfig other = config;
            other.user     = "otter_scratch_u";
            other.password = "Outra#Senha2";
            other.database.clear();
            auto second = mysql_driver().connect(other);
            check(second.has_value(), "a conta conecta com a senha NOVA");
            if (second) (*second)->close();
        }

        runs(mysql_object_rename(account, "otter_scratch_v"), "RENAME USER");
        check(scalar("SELECT COUNT(*) FROM mysql.user WHERE User='otter_scratch_v'") == "1",
              "a conta tem o nome novo");
        runs(mysql_object_drop(ref_of(ObjectType::role, "", "otter_scratch_v", "localhost")),
             "DROP USER");
        check(scalar("SELECT COUNT(*) FROM mysql.user WHERE User LIKE 'otter_scratch_%'") == "0",
              "a conta sumiu");

        // A senha da PROPRIA sessao: so' confere que o comando e' aceito na
        // forma -- mudar a senha do root de teste quebraria as outras suites.
        check(mysql_user_password("", "", "x").statements.front() ==
                  "ALTER USER USER() IDENTIFIED BY 'x'",
              "senha da propria conta: ALTER USER USER()");
    }

    // --- Tools --------------------------------------------------------------------
    std::printf("\nTools\n");
    for (const auto& [tool, option, label] :
         {std::tuple{MysqlTableTool::analyze, "", "ANALYZE TABLE"},
          std::tuple{MysqlTableTool::check, "EXTENDED", "CHECK TABLE EXTENDED"},
          std::tuple{MysqlTableTool::optimize, "", "OPTIMIZE TABLE"},
          std::tuple{MysqlTableTool::repair, "QUICK", "REPAIR TABLE QUICK"}}) {
        auto rs = g_holt->query(mysql_table_tool_sql(tool, kDb, "pedido", option));
        check(rs.has_value() && rs->row_count() > 0 && rs->column_count() == 4,
              std::string(label) + ": devolve a tabela de estado" +
                  (rs ? "" : "  -- " + rs.error().to_string()));
    }
    runs(mysql_truncate(kDb, "pedido"), "TRUNCATE TABLE");
    check(scalar("SELECT COUNT(*) FROM `otter_scratch`.`pedido`") == "0", "a tabela ficou vazia");

    // --- System Info ---------------------------------------------------------------
    std::printf("\nSystem Info\n");
    {
        auto privileges = catalog.load_privileges();
        check(privileges.has_value() && privileges->size() > 10, "User privileges: SHOW PRIVILEGES");
        auto plugins = catalog.load_plugins();
        check(plugins.has_value() && !plugins->empty(), "Plugins listados");
    }

    // --- Session Manager ------------------------------------------------------------
    std::printf("\nSession Manager\n");
    {
        auto list = g_holt->query(std::string(mysql_sessions_query()));
        check(list.has_value() && list->column_count() >= 8 &&
                  list->column(0).info().name == "Id" &&
                  list->column(4).info().name == "Command",
              "SHOW FULL PROCESSLIST: Id na 1a coluna, Command na 5a");

        auto victim = mysql_driver().connect(config);
        check(victim.has_value(), "segunda sessao aberta");
        if (victim) {
            auto id = (*victim)->query("SELECT CONNECTION_ID()");
            const std::string pid = id ? std::string(id->text(0, 0)) : std::string{};
            runs(mysql_session_kill(pid, /*connection=*/false), "KILL QUERY aceito");
            check((*victim)->query("SELECT 1").has_value(),
                  "KILL QUERY nao derruba a sessao");
            runs(mysql_session_kill(pid, /*connection=*/true), "KILL CONNECTION aceito");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            check(!(*victim)->query("SELECT 1").has_value(),
                  "a sessao encerrada nao responde mais");
        }
    }

    // --- Drop de filhos da tabela ----------------------------------------------------
    std::printf("\nDrop\n");
    runs(mysql_object_drop(ref_of(ObjectType::foreign_key, kDb, "fk_cliente", "pedido")),
         "DROP FOREIGN KEY");
    check(scalar("SELECT COUNT(*) FROM information_schema.TABLE_CONSTRAINTS WHERE "
                 "TABLE_SCHEMA='otter_scratch' AND CONSTRAINT_TYPE='FOREIGN KEY'") == "0",
          "a chave estrangeira sumiu");
    runs(mysql_object_drop(ref_of(ObjectType::index, kDb, "ix_cliente_nome", "cliente")),
         "DROP INDEX");
    runs(mysql_object_drop(ref_of(ObjectType::column, kDb, "obs", "pedido")), "DROP COLUMN");
    check(scalar("SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA="
                 "'otter_scratch' AND TABLE_NAME='pedido' AND COLUMN_NAME='obs'") == "0",
          "a coluna sumiu");
    runs(mysql_object_drop(ref_of(ObjectType::constraint, kDb, "PRIMARY", "pedido")),
         "DROP PRIMARY KEY");
    runs(mysql_object_drop(ref_of(ObjectType::trigger, kDb, "trg_pedido")), "DROP TRIGGER");
    runs(mysql_object_drop(ref_of(ObjectType::event, kDb, "ev_limpeza")), "DROP EVENT");
    runs(mysql_object_drop(ref_of(ObjectType::function, kDb, "f_total")), "DROP FUNCTION");

    // --- mysqldump / mysql -------------------------------------------------------------
    std::printf("\nCliente nativo\n");
    {
        const std::string dump_tool = find_mysql_tool("mysqldump");
        const std::string mysql_tool = find_mysql_tool("mysql");
        if (dump_tool.empty() || mysql_tool.empty()) {
            std::printf("  [PULA] mysqldump/mysql nao encontrados nesta maquina\n");
        } else {
            namespace fs = std::filesystem;
            const fs::path file = fs::temp_directory_path() / "otter-scratch-dump.sql";
            std::error_code ec;
            fs::remove(file, ec);

            ConnConfig target = config;
            target.database = kDb;

            MysqlDumpOptions options;
            options.file     = file.string();
            options.routines = true;
            const auto dump = mysql_dump_command(target, options, dump_tool);
            check(dump.has_value(), "comando do mysqldump montado");
            if (dump) {
                check(otter::display_command(*dump).find(config.password) == std::string::npos ||
                          config.password.empty(),
                      "a senha nao aparece na linha de comando");
                const auto [code, output] = run_process(*dump);
                check(code == 0, "mysqldump terminou com 0" +
                                     (code == 0 ? std::string{} : "  -- " + output));

                std::ifstream in(file, std::ios::binary);
                std::ostringstream text;
                text << in.rdbuf();
                const std::string sql = text.str();
                check(sql.find("CREATE TABLE `cliente`") != std::string::npos,
                      "o dump tem o CREATE TABLE");
                check(sql.find("'Ana'") != std::string::npos, "e os dados");
                check(sql.find("p_fecha") != std::string::npos, "e a rotina (--routines)");
            }

            // Restaurar noutro banco: e' o "Execute script" / "Restore".
            (void)g_holt->execute("CREATE DATABASE `otter_scratch2`");
            target.database = kDb2;
            const auto restore = mysql_script_command(target, file.string(), mysql_tool);
            check(restore.has_value(), "comando do mysql montado");
            if (restore) {
                const auto [code, output] = run_process(*restore);
                check(code == 0, "mysql terminou com 0" +
                                     (code == 0 ? std::string{} : "  -- " + output));
                check(scalar("SELECT COUNT(*) FROM `otter_scratch2`.`cliente`") == "2",
                      "as linhas chegaram ao banco restaurado");
            }
            fs::remove(file, ec);
        }
    }

    // --- Limpeza ---------------------------------------------------------------------
    std::printf("\nLimpeza\n");
    runs(mysql_object_drop(ref_of(ObjectType::database, "", kDb2)), "DROP DATABASE (restaurado)");
    runs(mysql_object_drop(ref_of(ObjectType::database, "", kDb)), "DROP DATABASE");
    check(scalar("SELECT COUNT(*) FROM information_schema.SCHEMATA WHERE SCHEMA_NAME LIKE "
                 "'otter_scratch%'") == "0",
          "os bancos de rascunho sumiram");

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
