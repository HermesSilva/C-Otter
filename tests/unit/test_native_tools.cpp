// C-Otter -- testes de db/native_tools: os comandos do pg_dump e do pg_restore.
//
// O risco aqui e' a senha: na linha de comando ela apareceria na lista de
// processos de qualquer usuario da maquina. E o comando que para perguntando
// por uma, num processo sem terminal, fica parado para sempre.
#include "test_main.hpp"

#include "db/native_tools.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace otter;
using namespace otter::db;

namespace {

bool has(const ProcessOptions& command, std::string_view argument) {
    return std::find(command.arguments.begin(), command.arguments.end(), argument) !=
           command.arguments.end();
}

std::string env(const ProcessOptions& command, std::string_view name) {
    for (const auto& [key, value] : command.environment) {
        if (key == name) return value;
    }
    return "(ausente)";
}

ConnConfig connection() {
    ConnConfig config;
    config.host     = "db.example";
    config.port     = 5433;
    config.database = "Vendas 2026";
    config.user     = "ana";
    config.password = "s3gr3d0 com espaco";
    return config;
}

} // namespace

OTTER_TEST(native_backup_never_puts_the_password_on_the_command_line) {
    BackupOptions options;
    options.file = "C:\\backup\\vendas.backup";

    const auto command = backup_command(connection(), options, "pg_dump");
    OTTER_CHECK(command.has_value());
    if (!command) return;

    for (const std::string& argument : command->arguments) {
        OTTER_CHECK(argument.find("s3gr3d0") == std::string::npos);
    }
    OTTER_CHECK(display_command(*command).find("s3gr3d0") == std::string::npos);
    OTTER_CHECK(env(*command, "PGPASSWORD") == "s3gr3d0 com espaco");

    // Sem terminal, perguntar a senha e' travar.
    OTTER_CHECK(has(*command, "--no-password"));
}

OTTER_TEST(native_backup_arguments_match_dbeaver) {
    BackupOptions options;
    options.format        = DumpFormat::plain;
    options.compression   = 5;
    options.encoding      = "UTF8";
    options.use_inserts   = true;
    options.no_privileges = true;
    options.no_owner      = true;
    options.clean         = true;
    options.create        = true;
    options.file          = "out.sql";

    const auto command = backup_command(connection(), options, "pg_dump");
    OTTER_CHECK(command.has_value());
    if (!command) return;

    for (const char* expected :
         {"--host=db.example", "--port=5433", "--username=ana", "--format=p",
          "--compress=5", "--encoding=UTF8", "--inserts", "--no-privileges",
          "--no-owner", "--clean", "--create", "--file", "out.sql"}) {
        OTTER_CHECK(has(*command, expected));
    }
    // O banco e' o ULTIMO argumento, sozinho: com espaco no nome, e' um
    // argumento so' (nao passa por shell).
    OTTER_CHECK(command->arguments.back() == "Vendas 2026");
}

OTTER_TEST(native_backup_tables_win_over_schemas) {
    // O pg_dump ignora -n quando ha' -t: misturar os dois faria um backup
    // menor do que o pedido, sem erro.
    BackupOptions options;
    options.file    = "out.backup";
    options.schemas = {"vendas"};
    options.tables  = {"vendas.pedido", "vendas.cliente"};

    auto command = backup_command(connection(), options, "pg_dump");
    OTTER_CHECK(command && has(*command, "-t") && has(*command, "vendas.pedido"));
    OTTER_CHECK(command && !has(*command, "-n"));

    options.tables.clear();
    command = backup_command(connection(), options, "pg_dump");
    OTTER_CHECK(command && has(*command, "-n") && has(*command, "vendas"));
}

OTTER_TEST(native_backup_tar_has_no_compression) {
    BackupOptions options;
    options.format      = DumpFormat::tar;
    options.compression = 9;
    options.file        = "out.tar";
    const auto command = backup_command(connection(), options, "pg_dump");
    OTTER_CHECK(command && !has(*command, "--compress=9"));
}

OTTER_TEST(native_backup_refusals) {
    BackupOptions options;
    OTTER_CHECK(!backup_command(connection(), options, "pg_dump").has_value());   // sem arquivo

    options.file = "x";
    OTTER_CHECK(!backup_command(connection(), options, "").has_value());   // sem programa

    ConnConfig proxied = connection();
    proxied.proxy_host = "proxy";
    const auto refused = backup_command(proxied, options, "pg_dump");
    OTTER_CHECK(!refused.has_value());
    OTTER_CHECK(!refused && refused.error().to_string().find("SOCKS") != std::string::npos);

    ConnConfig nameless = connection();
    nameless.database.clear();
    OTTER_CHECK(!backup_command(nameless, options, "pg_dump").has_value());
}

OTTER_TEST(native_restore_uses_psql_for_plain_dumps) {
    OTTER_CHECK(restore_program(DumpFormat::plain) == "psql");
    OTTER_CHECK(restore_program(DumpFormat::custom) == "pg_restore");
    OTTER_CHECK(restore_program(DumpFormat::tar) == "pg_restore");

    RestoreOptions options;
    options.format = DumpFormat::plain;
    options.file   = "dump.sql";
    options.clean  = true;   // nao se aplica: esta' (ou nao) dentro do script

    const auto command = restore_command(connection(), options, "psql");
    OTTER_CHECK(command.has_value());
    if (!command) return;
    OTTER_CHECK(has(*command, "--file=dump.sql"));
    OTTER_CHECK(has(*command, "--dbname=Vendas 2026"));
    // Parar no primeiro erro: seguir carregando depois de um CREATE que
    // falhou multiplica o estrago.
    OTTER_CHECK(has(*command, "--set=ON_ERROR_STOP=1"));
    OTTER_CHECK(!has(*command, "--clean"));
    OTTER_CHECK(has(*command, "--no-password"));
}

OTTER_TEST(native_restore_archive_arguments) {
    RestoreOptions options;
    options.format   = DumpFormat::custom;
    options.clean    = true;
    options.no_owner = true;
    options.create   = true;
    options.file     = "C:\\backup\\vendas.backup";

    const auto command = restore_command(connection(), options, "pg_restore");
    OTTER_CHECK(command.has_value());
    if (!command) return;
    for (const char* expected : {"--clean", "--no-owner", "--create", "--format=c",
                                 "--dbname=Vendas 2026"}) {
        OTTER_CHECK(has(*command, expected));
    }
    OTTER_CHECK(command->arguments.back() == "C:\\backup\\vendas.backup");
    OTTER_CHECK(env(*command, "PGPASSWORD") == "s3gr3d0 com espaco");

    options.file.clear();
    OTTER_CHECK(!restore_command(connection(), options, "pg_restore").has_value());
}

OTTER_TEST(native_formats) {
    OTTER_CHECK(dump_format_id(DumpFormat::custom) == "c");
    OTTER_CHECK(dump_format_id(DumpFormat::directory) == "d");
    OTTER_CHECK(dump_format_id(DumpFormat::tar) == "t");
    OTTER_CHECK(dump_format_id(DumpFormat::plain) == "p");
    OTTER_CHECK(dump_file_extension(DumpFormat::directory).empty());
    OTTER_CHECK(to_string(DumpFormat::plain) == "Plain");
}
