#include "db/native_tools.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace otter::db {
namespace {

// O que todo cliente nativo precisa para achar o servidor. Nunca a senha.
Status add_connection(ProcessOptions& command, const ConnConfig& connection) {
    // pg_dump nao fala SOCKS: pelo proxy ele nao alcancaria o servidor, e
    // tentar direto conectaria (se conectasse) por fora do proxy que o
    // usuario configurou.
    if (!connection.proxy_host.empty()) {
        return fail(Errc::not_supported,
                    "the PostgreSQL client tools cannot connect through a SOCKS proxy");
    }
    if (connection.database.empty()) {
        return fail(Errc::invalid_argument, "the database name is required");
    }

    command.arguments.push_back("--host=" + connection.host);
    command.arguments.push_back("--port=" + std::to_string(connection.port));
    if (!connection.user.empty()) {
        command.arguments.push_back("--username=" + connection.user);
    }
    command.arguments.emplace_back("--no-password");

    command.environment.emplace_back("PGPASSWORD", connection.password);
    command.environment.emplace_back("PGSSLMODE", to_string(connection.ssl_mode));
    // As mensagens do cliente em ingles: e' o que da' para procurar depois.
    command.environment.emplace_back("LC_MESSAGES", "C");
    return {};
}

} // namespace

std::string_view dump_format_id(DumpFormat format) noexcept {
    switch (format) {
        case DumpFormat::custom:    return "c";
        case DumpFormat::directory: return "d";
        case DumpFormat::tar:       return "t";
        case DumpFormat::plain:     return "p";
    }
    return "c";
}

std::string_view to_string(DumpFormat format) noexcept {
    switch (format) {
        case DumpFormat::custom:    return "Custom";
        case DumpFormat::directory: return "Directory";
        case DumpFormat::tar:       return "Tar";
        case DumpFormat::plain:     return "Plain";
    }
    return "Custom";
}

std::string_view dump_file_extension(DumpFormat format) noexcept {
    switch (format) {
        case DumpFormat::custom:    return ".backup";
        case DumpFormat::directory: return "";
        case DumpFormat::tar:       return ".tar";
        case DumpFormat::plain:     return ".sql";
    }
    return ".backup";
}

std::string_view restore_program(DumpFormat format) noexcept {
    return format == DumpFormat::plain ? "psql" : "pg_restore";
}

std::vector<std::string> postgres_bin_directories() {
    namespace fs = std::filesystem;
    std::vector<std::pair<int, std::string>> found;   // (versao, bin)

    std::vector<fs::path> roots;
#ifdef _WIN32
    for (const char* variable : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)"}) {
        if (const char* base = std::getenv(variable)) {
            roots.push_back(fs::path(base) / "PostgreSQL");
        }
    }
#else
    roots = {"/usr/lib/postgresql", "/usr/pgsql", "/opt/homebrew/opt", "/usr/local/opt",
             "/Library/PostgreSQL"};
#endif

    for (const fs::path& root : roots) {
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (const fs::directory_entry& entry : fs::directory_iterator(root, ec)) {
            const fs::path bin = entry.path() / "bin";
            if (!fs::is_directory(bin, ec)) continue;

            // "18", "16", "postgresql@15": os digitos do nome sao a versao.
            int version = 0;
            for (const char c : entry.path().filename().string()) {
                if (c >= '0' && c <= '9') version = version * 10 + (c - '0');
                else if (version > 0)     break;
            }
            const std::string text = bin.string();
            if (std::none_of(found.begin(), found.end(),
                             [&text](const auto& item) { return item.second == text; })) {
                found.emplace_back(version, text);
            }
        }
    }

    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<std::string> out;
    for (auto& [version, bin] : found) out.push_back(std::move(bin));
    return out;
}

std::string find_postgres_tool(std::string_view name) {
    // As instalacoes ANTES do PATH: nelas a ordem e' a da versao, e um
    // pg_dump antigo esquecido no PATH recusaria o servidor mais novo.
    return find_program(name, postgres_bin_directories());
}

Result<ProcessOptions> backup_command(const ConnConfig& connection,
                                      const BackupOptions& options,
                                      std::string program) {
    if (program.empty()) {
        return fail(Errc::not_found,
                    "pg_dump was not found: install the PostgreSQL client tools");
    }
    if (options.file.empty()) {
        return fail(Errc::invalid_argument, "the output file is required");
    }
    if (options.compression > 9) {
        return fail(Errc::invalid_argument, "compression goes from 0 to 9");
    }

    ProcessOptions command;
    command.program = std::move(program);
    OTTER_RETURN_IF_ERROR(add_connection(command, connection));

    // A mesma ordem do DBeaver.
    command.arguments.push_back("--format=" + std::string(dump_format_id(options.format)));
    // Compressao nao existe no formato Tar; o pg_dump recusaria.
    if (options.compression >= 0 && options.format != DumpFormat::tar) {
        command.arguments.push_back("--compress=" + std::to_string(options.compression));
    }
    if (!options.encoding.empty()) {
        command.arguments.push_back("--encoding=" + options.encoding);
    }
    if (options.use_inserts)   command.arguments.emplace_back("--inserts");
    if (options.no_privileges) command.arguments.emplace_back("--no-privileges");
    if (options.no_owner)      command.arguments.emplace_back("--no-owner");
    if (options.clean)         command.arguments.emplace_back("--clean");
    if (options.create)        command.arguments.emplace_back("--create");
    command.arguments.emplace_back("--verbose");

    command.arguments.emplace_back("--file");
    command.arguments.push_back(options.file);

    // -n e -t sao PADROES do pg_dump (`*` e `?` valem): o nome vai como o
    // servidor o conhece. Com -t presente o pg_dump ignora -n -- por isso os
    // dois nao se misturam aqui.
    if (!options.tables.empty()) {
        for (const std::string& table : options.tables) {
            command.arguments.emplace_back("-t");
            command.arguments.push_back(table);
        }
    } else {
        for (const std::string& schema : options.schemas) {
            command.arguments.emplace_back("-n");
            command.arguments.push_back(schema);
        }
    }

    command.arguments.push_back(connection.database);
    return command;
}

Result<ProcessOptions> restore_command(const ConnConfig& connection,
                                       const RestoreOptions& options,
                                       std::string program) {
    if (program.empty()) {
        return fail(Errc::not_found,
                    std::string(restore_program(options.format)) +
                        " was not found: install the PostgreSQL client tools");
    }
    if (options.file.empty()) {
        return fail(Errc::invalid_argument, "the backup file is required");
    }

    ProcessOptions command;
    command.program = std::move(program);
    OTTER_RETURN_IF_ERROR(add_connection(command, connection));
    command.arguments.push_back("--dbname=" + connection.database);

    if (options.format == DumpFormat::plain) {
        // Um dump em texto e' um script: quem o roda e' o psql. As opcoes de
        // limpar e criar ja' estao (ou nao) DENTRO do arquivo.
        command.arguments.emplace_back("--set=ON_ERROR_STOP=1");
        command.arguments.push_back("--file=" + options.file);
        return command;
    }

    if (options.clean)    command.arguments.emplace_back("--clean");
    if (options.no_owner) command.arguments.emplace_back("--no-owner");
    if (options.create)   command.arguments.emplace_back("--create");
    command.arguments.push_back("--format=" + std::string(dump_format_id(options.format)));
    command.arguments.emplace_back("--verbose");
    command.arguments.push_back(options.file);
    return command;
}

} // namespace otter::db
