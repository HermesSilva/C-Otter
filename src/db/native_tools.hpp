// C-Otter -- db/native_tools.hpp
//
// Backup e Restore do PostgreSQL pelo cliente nativo (pg_dump, pg_restore,
// psql) -- como o DBeaver, que tambem delega a eles: o formato de arquivo do
// pg_dump e' do PostgreSQL, e reescreve-lo seria reimplementar o servidor.
//
// Mapa do assistente do DBeaver (PostgreDatabaseBackupHandler /
// PostgreDatabaseRestoreHandler):
//
//   Backup    Format (Custom, Directory, Tar, Plain), Compression, Encoding,
//             Use SQL INSERT instead of COPY for rows, Do not backup
//             privileges, Discard objects owner, Add drop database statement,
//             Add create database statement, o arquivo de saida, e os
//             schemas/tabelas escolhidos
//   Restore   Format, Clean (drop) database objects before recreating them,
//             Discard objects owner, Create database, Backup file
//
// Aqui so' se MONTA o comando (funcao pura, com teste); quem executa e' a UI,
// com base/process.
#pragma once

#include "base/process.hpp"
#include "db/holt.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

enum class DumpFormat : std::uint8_t { custom, directory, tar, plain };

// A letra do `--format` do pg_dump: c, d, t, p.
[[nodiscard]] std::string_view dump_format_id(DumpFormat format) noexcept;
// "Custom", "Directory", "Tar", "Plain" -- os rotulos do DBeaver.
[[nodiscard]] std::string_view to_string(DumpFormat format) noexcept;
// Extensao sugerida para o arquivo; vazio para Directory.
[[nodiscard]] std::string_view dump_file_extension(DumpFormat format) noexcept;

struct BackupOptions {
    DumpFormat  format = DumpFormat::custom;
    int         compression = -1;   // -1 = padrao do pg_dump; 0..9
    std::string encoding;           // vazio = o do banco
    bool        use_inserts = false;
    bool        no_privileges = false;
    bool        no_owner = false;
    bool        clean = false;
    bool        create = false;
    std::string file;               // arquivo (ou diretorio, no formato Directory)

    // Vazios = o banco inteiro. Tabela como "schema.tabela".
    std::vector<std::string> schemas;
    std::vector<std::string> tables;
};

struct RestoreOptions {
    DumpFormat  format = DumpFormat::custom;
    bool        clean = false;
    bool        no_owner = false;
    bool        create = false;
    std::string file;
};

// Onde o programa esta': no PATH, ou no `bin` de uma instalacao do PostgreSQL
// (o instalador do Windows nao o poe no PATH). Vazio se nao achou. A versao
// mais nova instalada vem primeiro -- um pg_dump mais velho que o servidor
// recusa o backup.
[[nodiscard]] std::string find_postgres_tool(std::string_view name);

// Os diretorios `bin` conhecidos, do mais novo para o mais velho.
[[nodiscard]] std::vector<std::string> postgres_bin_directories();

// O comando do backup. A senha vai em PGPASSWORD, no AMBIENTE do processo:
// na linha de comando ela apareceria na lista de processos da maquina.
// `--no-password` impede o programa de parar perguntando por uma.
[[nodiscard]] Result<ProcessOptions> backup_command(const ConnConfig& connection,
                                                    const BackupOptions& options,
                                                    std::string program);

// O comando do restore: pg_restore para Custom/Directory/Tar, psql para Plain
// (um dump em texto e' um script SQL). `program` e' o que serve ao formato.
[[nodiscard]] Result<ProcessOptions> restore_command(const ConnConfig& connection,
                                                     const RestoreOptions& options,
                                                     std::string program);

// Qual programa o restore deste formato usa: "pg_restore" ou "psql".
[[nodiscard]] std::string_view restore_program(DumpFormat format) noexcept;

} // namespace otter::db
