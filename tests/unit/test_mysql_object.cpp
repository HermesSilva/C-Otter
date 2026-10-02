// C-Otter -- testes de db/mysql_object: o perfil MySQL do editor de objeto,
// dos dialogos de criacao, das ferramentas e do cliente nativo.
//
// O que se protege: a citacao (crase em nome, aspa em conta -- trocar uma pela
// outra gera um comando que o servidor aceita e faz OUTRA coisa), a senha fora
// da linha de comando, e o desvio dos geradores genericos pelo dialeto.
#include "test_main.hpp"

#include "db/ddl.hpp"
#include "db/mysql_object.hpp"
#include "db/object_info.hpp"

#include <algorithm>
#include <string>

using namespace otter;
using namespace otter::db;

namespace {

ObjectRef ref_of(ObjectType type, const char* schema, const char* name,
                 const char* parent = "") {
    ObjectRef ref;
    ref.type   = type;
    ref.schema = schema;
    ref.name   = name;
    ref.parent = parent;
    return ref;
}

bool has(const ProcessOptions& command, std::string_view argument) {
    return std::find(command.arguments.begin(), command.arguments.end(), argument) !=
           command.arguments.end();
}

ConnConfig connection() {
    ConnConfig config;
    config.driver_id = "mysql";
    config.host      = "db.example";
    config.port      = 3307;
    config.database  = "loja";
    config.user      = "ana";
    config.password  = "s3gr3d0";
    return config;
}

// O dialeto e' por thread; cada teste que depende dele o define e repoe.
struct MysqlDialect {
    QuoteStyle previous = sql_dialect();
    MysqlDialect() { set_sql_dialect(QuoteStyle::backticks); }
    ~MysqlDialect() { set_sql_dialect(previous); }
};

} // namespace

OTTER_TEST(mysql_names_use_backticks_and_accounts_use_quotes) {
    OTTER_CHECK_EQ(mysql_object_name(ref_of(ObjectType::table, "loja", "Pedido Item")),
                   std::string{"`loja`.`Pedido Item`"});
    OTTER_CHECK_EQ(mysql_object_name(ref_of(ObjectType::database, "", "loja")),
                   std::string{"`loja`"});
    // A conta e' o par usuario@host, cada parte como LITERAL.
    OTTER_CHECK_EQ(mysql_object_name(ref_of(ObjectType::role, "", "ana", "10.0.%")),
                   std::string{"'ana'@'10.0.%'"});
    OTTER_CHECK_EQ(mysql_object_name(ref_of(ObjectType::role, "", "ana")),
                   std::string{"'ana'@'%'"});

    OTTER_CHECK_EQ(mysql_account("ana@localhost"), std::string{"'ana'@'localhost'"});
    OTTER_CHECK_EQ(mysql_account("ana"), std::string{"'ana'@'%'"});
    // Ja' citada, como o information_schema devolve: passa como esta'.
    OTTER_CHECK_EQ(mysql_account("'ana'@'%'"), std::string{"'ana'@'%'"});
    // Aspa no nome nao pode fechar o literal.
    OTTER_CHECK_EQ(mysql_account("o'brien@h"), std::string{"'o\\'brien'@'h'"});
}

OTTER_TEST(mysql_rename_covers_what_the_server_can_rename) {
    OTTER_CHECK_EQ(
        mysql_object_rename(ref_of(ObjectType::table, "loja", "pedido"), "venda")
            .statements.front(),
        std::string{"RENAME TABLE `loja`.`pedido` TO `loja`.`venda`"});
    OTTER_CHECK_EQ(
        mysql_object_rename(ref_of(ObjectType::event, "loja", "limpa"), "limpeza")
            .statements.front(),
        std::string{"ALTER EVENT `loja`.`limpa` RENAME TO `loja`.`limpeza`"});
    OTTER_CHECK_EQ(
        mysql_object_rename(ref_of(ObjectType::index, "loja", "ix_a", "pedido"), "ix_b")
            .statements.front(),
        std::string{"ALTER TABLE `loja`.`pedido` RENAME INDEX `ix_a` TO `ix_b`"});
    OTTER_CHECK_EQ(
        mysql_object_rename(ref_of(ObjectType::role, "", "ana", "localhost"), "anna")
            .statements.front(),
        std::string{"RENAME USER 'ana'@'localhost' TO 'anna'@'localhost'"});

    // O que o MySQL nao renomeia e' recusado com o motivo, nao gerado errado.
    OTTER_CHECK(!mysql_object_rename(ref_of(ObjectType::database, "", "loja"), "x").ok());
    OTTER_CHECK(!mysql_object_rename(ref_of(ObjectType::function, "loja", "f"), "g").ok());
    OTTER_CHECK(!mysql_object_rename(ref_of(ObjectType::table, "loja", "t"), "").ok());
}

OTTER_TEST(mysql_comment_and_drop) {
    OTTER_CHECK_EQ(
        mysql_object_comment(ref_of(ObjectType::table, "loja", "pedido"), "it's ok")
            .statements.front(),
        std::string{"ALTER TABLE `loja`.`pedido` COMMENT = 'it\\'s ok'"});
    OTTER_CHECK_EQ(
        mysql_object_comment(ref_of(ObjectType::procedure, "loja", "fecha"), "x")
            .statements.front(),
        std::string{"ALTER PROCEDURE `loja`.`fecha` COMMENT 'x'"});
    OTTER_CHECK(!mysql_object_comment(ref_of(ObjectType::view, "loja", "v"), "x").ok());

    const AlterScript drop = mysql_object_drop(ref_of(ObjectType::table, "loja", "pedido"));
    OTTER_CHECK_EQ(drop.statements.front(), std::string{"DROP TABLE `loja`.`pedido`"});
    OTTER_CHECK(drop.has_destructive());

    OTTER_CHECK_EQ(
        mysql_object_drop(ref_of(ObjectType::foreign_key, "loja", "fk_c", "pedido"))
            .statements.front(),
        std::string{"ALTER TABLE `loja`.`pedido` DROP FOREIGN KEY `fk_c`"});
    // A chave primaria chama-se sempre PRIMARY, e tem sintaxe propria.
    OTTER_CHECK_EQ(
        mysql_object_drop(ref_of(ObjectType::constraint, "loja", "PRIMARY", "pedido"))
            .statements.front(),
        std::string{"ALTER TABLE `loja`.`pedido` DROP PRIMARY KEY"});
    OTTER_CHECK_EQ(mysql_object_drop(ref_of(ObjectType::role, "", "ana", "%"))
                       .statements.front(),
                   std::string{"DROP USER 'ana'@'%'"});
    OTTER_CHECK_EQ(mysql_object_drop(ref_of(ObjectType::event, "loja", "limpa"))
                       .statements.front(),
                   std::string{"DROP EVENT `loja`.`limpa`"});
}

OTTER_TEST(mysql_grant_and_revoke) {
    const ObjectRef table = ref_of(ObjectType::table, "loja", "pedido");
    OTTER_CHECK_EQ(mysql_grant(table, "SELECT", "'ana'@'%'", false).statements.front(),
                   std::string{"GRANT SELECT ON `loja`.`pedido` TO 'ana'@'%'"});
    OTTER_CHECK_EQ(
        mysql_grant(table, "ALL", "ana@localhost", true).statements.front(),
        std::string{"GRANT ALL PRIVILEGES ON `loja`.`pedido` TO 'ana'@'localhost' "
                    "WITH GRANT OPTION"});
    OTTER_CHECK_EQ(
        mysql_revoke(ref_of(ObjectType::database, "", "loja"), "CREATE VIEW", "ana")
            .statements.front(),
        std::string{"REVOKE CREATE VIEW ON `loja`.* FROM 'ana'@'%'"});
    OTTER_CHECK_EQ(
        mysql_grant(ref_of(ObjectType::procedure, "loja", "fecha"), "EXECUTE", "ana", false)
            .statements.front(),
        std::string{"GRANT EXECUTE ON PROCEDURE `loja`.`fecha` TO 'ana'@'%'"});

    // O MySQL nao tem PUBLIC, e o nome do privilegio vai sem aspas: so' letras.
    OTTER_CHECK(!mysql_grant(table, "SELECT", "PUBLIC", false).ok());
    OTTER_CHECK(!mysql_grant(table, "SELECT; DROP TABLE x", "ana", false).ok());
    OTTER_CHECK(!mysql_grant(ref_of(ObjectType::trigger, "loja", "t"), "SELECT", "ana",
                             false).ok());
}

OTTER_TEST(mysql_source_view_replaces_and_routine_is_recreated) {
    // SHOW CREATE VIEW devolve "CREATE ALGORITHM=...": o OR REPLACE entra.
    const AlterScript view = mysql_source_script(
        ref_of(ObjectType::view, "loja", "v"),
        "CREATE ALGORITHM=UNDEFINED VIEW `loja`.`v` AS select 1;\n");
    OTTER_CHECK_EQ(view.statements.size(), std::size_t{1});
    OTTER_CHECK_EQ(view.statements.front(),
                   std::string{"CREATE OR REPLACE ALGORITHM=UNDEFINED VIEW `loja`.`v` "
                               "AS select 1"});
    OTTER_CHECK(!view.has_destructive());

    // Rotina: DROP + CREATE, marcado como destrutivo e com o aviso.
    const AlterScript routine = mysql_source_script(
        ref_of(ObjectType::procedure, "loja", "fecha"),
        "CREATE PROCEDURE `fecha`()\nBEGIN\n  SELECT 1;\nEND;\n");
    OTTER_CHECK_EQ(routine.statements.size(), std::size_t{2});
    OTTER_CHECK_EQ(routine.statements[0],
                   std::string{"DROP PROCEDURE IF EXISTS `loja`.`fecha`"});
    // O ';' de DENTRO do corpo fica; so' o do fim sai. E o BANCO entra no
    // nome: SHOW CREATE o omite, e sem ele a rotina nasceria no banco
    // corrente da sessao (defeito achado no teste contra o servidor).
    OTTER_CHECK_EQ(routine.statements[1],
                   std::string{"CREATE PROCEDURE `loja`.`fecha`()\nBEGIN\n  SELECT 1;\nEND"});

    // Como o servidor devolve: com DEFINER, e a palavra PROCEDURE dentro de
    // um nome citado nao confunde.
    const AlterScript definer = mysql_source_script(
        ref_of(ObjectType::function, "loja", "PROCEDURE"),
        "CREATE DEFINER=`root`@`localhost` FUNCTION `PROCEDURE`() RETURNS int\nRETURN 1");
    OTTER_CHECK_EQ(definer.statements[1],
                   std::string{"CREATE DEFINER=`root`@`localhost` FUNCTION "
                               "`loja`.`PROCEDURE`() RETURNS int\nRETURN 1"});

    // Ja' qualificado: fica como esta'.
    const AlterScript qualified = mysql_source_script(
        ref_of(ObjectType::event, "loja", "limpa"),
        "CREATE EVENT `loja`.`limpa` ON SCHEDULE EVERY 1 DAY DO SELECT 1");
    OTTER_CHECK_EQ(qualified.statements[1],
                   std::string{"CREATE EVENT `loja`.`limpa` ON SCHEDULE EVERY 1 DAY DO "
                               "SELECT 1"});

    // Trigger: o nome E a tabela -- ela tem de estar no banco do trigger.
    const AlterScript trigger = mysql_source_script(
        ref_of(ObjectType::trigger, "loja", "trg"),
        "CREATE DEFINER=`root`@`%` TRIGGER `trg` BEFORE INSERT ON `pedido` FOR EACH ROW "
        "SET NEW.total = 0");
    OTTER_CHECK_EQ(trigger.statements[1],
                   std::string{"CREATE DEFINER=`root`@`%` TRIGGER `loja`.`trg` BEFORE "
                               "INSERT ON `loja`.`pedido` FOR EACH ROW SET NEW.total = 0"});
    OTTER_CHECK(routine.has_destructive());
    OTTER_CHECK(!routine.warnings.empty());

    OTTER_CHECK(mysql_source_editable(ObjectType::trigger));
    OTTER_CHECK(mysql_source_editable(ObjectType::event));
    OTTER_CHECK(!mysql_source_editable(ObjectType::table));
    OTTER_CHECK(!mysql_source_script(ref_of(ObjectType::view, "loja", "v"), " ;\n").ok());
}

OTTER_TEST(mysql_create_database_user_and_password) {
    OTTER_CHECK_EQ(
        mysql_create_database("Loja Nova", "utf8mb4", "utf8mb4_0900_ai_ci")
            .statements.front(),
        std::string{"CREATE DATABASE `Loja Nova` DEFAULT CHARACTER SET utf8mb4 "
                    "DEFAULT COLLATE utf8mb4_0900_ai_ci"});
    OTTER_CHECK_EQ(mysql_create_database("x", "", "").statements.front(),
                   std::string{"CREATE DATABASE `x`"});
    // Charset vai sem aspas: so' palavra passa.
    OTTER_CHECK(!mysql_create_database("x", "utf8; DROP DATABASE y", "").ok());
    OTTER_CHECK(!mysql_create_database("", "", "").ok());

    MysqlNewUser user;
    user.name     = "ana";
    user.host     = "localhost";
    user.password = "p'w";
    OTTER_CHECK_EQ(mysql_create_user(user).statements.front(),
                   std::string{"CREATE USER 'ana'@'localhost' IDENTIFIED BY 'p\\'w'"});

    OTTER_CHECK_EQ(mysql_user_password("ana", "%", "nova").statements.front(),
                   std::string{"ALTER USER 'ana'@'%' IDENTIFIED BY 'nova'"});
    // Sem nome: a conta da propria sessao.
    OTTER_CHECK_EQ(mysql_user_password("", "", "nova").statements.front(),
                   std::string{"ALTER USER USER() IDENTIFIED BY 'nova'"});
}

OTTER_TEST(mysql_templates_open_in_the_editor) {
    OTTER_CHECK_EQ(mysql_routine_template("loja", "fecha", true),
                   std::string{"CREATE PROCEDURE `loja`.`fecha`()\nBEGIN\n\nEND"});
    OTTER_CHECK(mysql_routine_template("loja", "total", false)
                    .starts_with("CREATE FUNCTION `loja`.`total`()\nRETURNS INT"));
    OTTER_CHECK_EQ(
        mysql_trigger_template("loja", "pedido", "trg", "AFTER", "UPDATE"),
        std::string{"CREATE TRIGGER `loja`.`trg`\nAFTER UPDATE\nON `loja`.`pedido` "
                    "FOR EACH ROW\nBEGIN\n\nEND"});
    OTTER_CHECK(mysql_event_template("loja", "limpa")
                    .starts_with("CREATE EVENT `loja`.`limpa`\nON SCHEDULE"));
}

OTTER_TEST(mysql_table_tools_and_sessions) {
    OTTER_CHECK_EQ(mysql_table_tool_sql(MysqlTableTool::analyze, "loja", "pedido"),
                   std::string{"ANALYZE TABLE `loja`.`pedido`"});
    OTTER_CHECK_EQ(mysql_table_tool_sql(MysqlTableTool::check, "loja", "pedido", "EXTENDED"),
                   std::string{"CHECK TABLE `loja`.`pedido` EXTENDED"});
    // Opcao fora da lista do combo nao entra no comando.
    OTTER_CHECK_EQ(
        mysql_table_tool_sql(MysqlTableTool::repair, "loja", "pedido", "; DROP TABLE x"),
        std::string{"REPAIR TABLE `loja`.`pedido`"});
    OTTER_CHECK_EQ(mysql_table_tool_sql(MysqlTableTool::optimize, "loja", "pedido", "QUICK"),
                   std::string{"OPTIMIZE TABLE `loja`.`pedido`"});

    const AlterScript truncate = mysql_truncate("loja", "pedido");
    OTTER_CHECK_EQ(truncate.statements.front(), std::string{"TRUNCATE TABLE `loja`.`pedido`"});
    OTTER_CHECK(truncate.has_destructive());

    OTTER_CHECK_EQ(mysql_session_kill("42", false).statements.front(),
                   std::string{"KILL QUERY 42"});
    OTTER_CHECK_EQ(mysql_session_kill("42", true).statements.front(),
                   std::string{"KILL CONNECTION 42"});
    // O id vem da grade: so' digitos passam.
    OTTER_CHECK(!mysql_session_kill("42; DROP", true).ok());
    OTTER_CHECK(!mysql_session_kill("", false).ok());
}

OTTER_TEST(mysqldump_keeps_the_password_out_of_the_command_line) {
    MysqlDumpOptions options;
    options.file     = "C:/tmp/loja.sql";
    options.routines = true;
    options.no_data  = true;
    options.tables   = {"pedido"};

    const auto command = mysql_dump_command(connection(), options, "mysqldump");
    OTTER_CHECK(command.has_value());
    OTTER_CHECK(has(*command, "--host=db.example"));
    OTTER_CHECK(has(*command, "--port=3307"));
    OTTER_CHECK(has(*command, "--user=ana"));
    OTTER_CHECK(has(*command, "--single-transaction"));
    OTTER_CHECK(has(*command, "--routines"));
    OTTER_CHECK(has(*command, "--no-data"));
    OTTER_CHECK(has(*command, "--result-file=C:/tmp/loja.sql"));
    // O banco e depois a tabela, por ultimo.
    OTTER_CHECK_EQ(command->arguments[command->arguments.size() - 2], std::string{"loja"});
    OTTER_CHECK_EQ(command->arguments.back(), std::string{"pedido"});

    // A senha: no ambiente, nunca num argumento.
    for (const std::string& argument : command->arguments) {
        OTTER_CHECK(argument.find("s3gr3d0") == std::string::npos);
    }
    bool in_environment = false;
    for (const auto& [name, value] : command->environment) {
        in_environment |= name == "MYSQL_PWD" && value == "s3gr3d0";
    }
    OTTER_CHECK(in_environment);

    OTTER_CHECK(!mysql_dump_command(connection(), MysqlDumpOptions{}, "mysqldump").has_value());
    OTTER_CHECK(!mysql_dump_command(connection(), options, "").has_value());

    ConnConfig proxied = connection();
    proxied.proxy_host = "proxy";
    OTTER_CHECK(!mysql_dump_command(proxied, options, "mysqldump").has_value());
}

OTTER_TEST(mysql_script_runs_through_source) {
    const auto command =
        mysql_script_command(connection(), "C:\\scripts\\carga inicial.sql", "mysql");
    OTTER_CHECK(command.has_value());
    // Barras normais: a invertida e' escape dentro do comando do cliente.
    OTTER_CHECK(has(*command, "--execute=source C:/scripts/carga inicial.sql"));
    OTTER_CHECK_EQ(command->arguments.back(), std::string{"loja"});
    OTTER_CHECK(!mysql_script_command(connection(), "", "mysql").has_value());
}

OTTER_TEST(generic_generators_follow_the_dialect) {
    const ObjectRef table = ref_of(ObjectType::table, "loja", "pedido");

    // O mesmo nome de funcao, o comando de cada SGBD.
    set_sql_dialect(QuoteStyle::double_quotes);
    OTTER_CHECK(generate_object_rename(table, "venda")
                    .statements.front()
                    .starts_with("ALTER TABLE"));
    OTTER_CHECK(editable_property(ObjectType::table, "Owner") == ObjectEdit::owner);

    {
        const MysqlDialect mysql;
        OTTER_CHECK_EQ(generate_object_rename(table, "venda").statements.front(),
                       std::string{"RENAME TABLE `loja`.`pedido` TO `loja`.`venda`"});
        OTTER_CHECK_EQ(generate_object_drop(table).statements.front(),
                       std::string{"DROP TABLE `loja`.`pedido`"});
        OTTER_CHECK_EQ(object_sql_name(table), std::string{"`loja`.`pedido`"});
        // Dono, schema e tablespace nao existem la': recusados com o motivo.
        OTTER_CHECK(editable_property(ObjectType::table, "Owner") == ObjectEdit::none);
        OTTER_CHECK(!generate_object_owner(table, "ana").ok());
        OTTER_CHECK(!generate_object_schema(table, "outro").ok());
        OTTER_CHECK(generate_grant(table, "SELECT", "ana", false)
                        .statements.front()
                        .ends_with("TO 'ana'@'%'"));
        OTTER_CHECK(!privileges_for(ObjectType::database).empty());
    }

    // O dialeto voltou: o teste seguinte nao herda o do MySQL.
    OTTER_CHECK(sql_dialect() == QuoteStyle::double_quotes);
}
