// C-Otter -- db/mssql_object.hpp
//
// O perfil SQL Server do editor de objeto, dos dialogos de criacao, das
// ferramentas e das sessoes: o que db/object_info.hpp faz para o PostgreSQL e
// db/mysql_object.hpp para o MySQL.
//
// Mapa do plugin org.jkiss.dbeaver.ext.mssql (docs/MSSQL-MAP.md, secao 4):
//
//   edit/   SQLServerDatabaseManager, TableManager, TableColumnManager,
//           UniqueKeyManager, CheckConstraintManager, ForeignKeyManager,
//           IndexManager, ViewManager, ProcedureManager, TableTriggerManager,
//           SynonymManager, DataTypeManager, LoginManager,
//           ExtendedPropertyManager
//   ui      SQLServerCreateDatabaseDialog, SQLServerLoginConfigurator,
//           SQLServerSessionEditor (Kill session)
//
// Os geradores genericos (generate_object_rename, generate_grant...) chamam
// estes quando o dialeto corrente e' o do SQL Server (colchetes). Como em
// db/alter.hpp, nada aqui executa.
//
// Um login do servidor e' um ObjectRef de tipo `role`.
#pragma once

#include "db/alter.hpp"
#include "db/catalog.hpp"
#include "db/object_info.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- Nomes --------------------------------------------------------------------------

// [schema].[nome]; [banco]; [login]; para coluna e indice, [schema].[tabela].[nome].
[[nodiscard]] std::string mssql_object_name(const ObjectRef& ref);

// --- Consultas ----------------------------------------------------------------------

// UMA linha; o nome de cada coluna e' o rotulo. Vazio = tipo sem editor.
[[nodiscard]] std::string mssql_properties_query(const ObjectRef& ref);

// O texto do objeto (OBJECT_DEFINITION). Vazio para tabela, cujo DDL e'
// montado no cliente (mssql_table_ddl) -- o servidor nao tem um
// "SHOW CREATE TABLE".
[[nodiscard]] std::string mssql_definition_query(const ObjectRef& ref);

// (grantee, privilege, grantable, grantor). Vazio = o tipo nao tem lista.
[[nodiscard]] std::string mssql_permissions_query(const ObjectRef& ref);

[[nodiscard]] ObjectEdit mssql_editable_property(ObjectType type,
                                                 std::string_view label) noexcept;
[[nodiscard]] std::vector<std::string_view> mssql_privileges_for(ObjectType type);

// O CREATE TABLE de uma tabela, com identity, colunas calculadas, constraints,
// indices, chaves estrangeiras e o comentario. `identity` traz, por coluna,
// "IDENTITY(semente,incremento)" -- vazio para as que nao sao.
struct MssqlIdentity {
    std::string column;
    std::string clause;
};
[[nodiscard]] std::string mssql_table_ddl(std::string_view schema, const TableMeta& table,
                                          const std::vector<MssqlIdentity>& identity);

// --- Alteracoes ---------------------------------------------------------------------

[[nodiscard]] AlterScript mssql_object_rename(const ObjectRef& ref,
                                              std::string_view new_name);
// MS_Description: acrescenta, troca ou (texto vazio) remove a propriedade.
[[nodiscard]] AlterScript mssql_object_comment(const ObjectRef& ref,
                                               std::string_view comment);
// ALTER SCHEMA ... TRANSFER.
[[nodiscard]] AlterScript mssql_object_schema(const ObjectRef& ref,
                                              std::string_view new_schema);
[[nodiscard]] AlterScript mssql_object_drop(const ObjectRef& ref);

[[nodiscard]] AlterScript mssql_grant(const ObjectRef& ref, std::string_view privilege,
                                      std::string_view grantee, bool with_grant_option);
[[nodiscard]] AlterScript mssql_revoke(const ObjectRef& ref, std::string_view privilege,
                                       std::string_view grantee);

// O fonte editado: view, procedure, funcao e trigger se gravam por ALTER, que
// preserva as permissoes (ao contrario de apagar e criar).
[[nodiscard]] bool mssql_source_editable(ObjectType type) noexcept;
[[nodiscard]] AlterScript mssql_source_script(const ObjectRef& ref,
                                              std::string_view source);

// --- CREATE -------------------------------------------------------------------------

[[nodiscard]] AlterScript mssql_create_database(std::string_view name,
                                                std::string_view collation);
[[nodiscard]] AlterScript mssql_create_schema(std::string_view name,
                                              std::string_view owner);

struct MssqlNewLogin {
    std::string name;
    std::string password;
    std::string default_database;   // vazio = master
};
[[nodiscard]] AlterScript mssql_create_login(const MssqlNewLogin& login);
[[nodiscard]] AlterScript mssql_login_password(std::string_view login,
                                               std::string_view password);

[[nodiscard]] AlterScript mssql_create_synonym(std::string_view schema,
                                               std::string_view name,
                                               std::string_view target);

// Os esqueletos que o dialogo de criacao abre no editor.
[[nodiscard]] std::string mssql_routine_template(std::string_view schema,
                                                 std::string_view name, bool procedure);
[[nodiscard]] std::string mssql_trigger_template(std::string_view schema,
                                                 std::string_view table,
                                                 std::string_view name,
                                                 std::string_view timing,
                                                 std::string_view event);

// --- Tools --------------------------------------------------------------------------

enum class MssqlTableTool : std::uint8_t {
    update_statistics,   // UPDATE STATISTICS
    rebuild_indexes,     // ALTER INDEX ALL ... REBUILD
    reorganize_indexes,  // ALTER INDEX ALL ... REORGANIZE
    check,               // DBCC CHECKTABLE
};
[[nodiscard]] AlterScript mssql_table_tool(MssqlTableTool tool, std::string_view schema,
                                           std::string_view table);
[[nodiscard]] AlterScript mssql_truncate(std::string_view schema, std::string_view table);
[[nodiscard]] AlterScript mssql_trigger_enable(const ObjectRef& ref, bool enable);

// BACKUP DATABASE / RESTORE DATABASE. O caminho e' NO SERVIDOR: quem grava o
// arquivo e' o servico do SQL Server, com a conta dele.
struct MssqlBackupOptions {
    std::string file;
    bool copy_only   = true;    // nao quebra a cadeia de backups diferenciais
    bool compression = false;
    bool overwrite   = false;   // INIT: substitui os backups que ja' estao no arquivo
};
[[nodiscard]] AlterScript mssql_backup_database(std::string_view database,
                                                const MssqlBackupOptions& options);
[[nodiscard]] AlterScript mssql_restore_database(std::string_view database,
                                                 std::string_view file, bool replace);

// --- Sessoes ------------------------------------------------------------------------

// A consulta do Session Manager (SQLServerSessionManager do DBeaver): uma
// linha por sessao de usuario, com o comando em curso e quem a bloqueia.
[[nodiscard]] std::string_view mssql_sessions_query() noexcept;
// Quem espera por quem.
[[nodiscard]] std::string_view mssql_locks_query() noexcept;
// KILL <id>. O id vem da grade -- so' digitos passam.
[[nodiscard]] AlterScript mssql_session_kill(std::string_view id);

} // namespace otter::db
