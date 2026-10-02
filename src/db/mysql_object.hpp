// C-Otter -- db/mysql_object.hpp
//
// O perfil MySQL do editor de objeto, dos dialogos de criacao, das ferramentas
// e do cliente nativo: o que db/object_info.hpp e db/object_ddl.hpp fazem para
// o PostgreSQL.
//
// Mapa do plugin org.jkiss.dbeaver.ext.mysql (docs/MYSQL-MAP.md, secao 5):
//
//   edit/     MySQLDatabaseManager, TableManager, TableColumnManager,
//             ConstraintManager, ForeignKeyManager, IndexManager, ViewManager,
//             ProcedureManager, TriggerManager, EventManager, SequenceManager,
//             MySQLUserManager
//   ui        MySQLCreateDatabaseDialog (Database name, Charset, Collation),
//             MySQLUserEditorGeneral (User Name, Host, Password, Confirm),
//             MySQLUserEditorPrivileges, MySQLSessionEditor (Kill Query /
//             Kill Connection)
//   tasks/    Analyze, Check (FOR UPGRADE, QUICK, FAST, MEDIUM, EXTENDED,
//             CHANGED), Optimize, Repair (QUICK, EXTENDED, USE_FRM), Truncate,
//             MySQLDatabaseExportHandler (mysqldump), MySQLScriptExecuteHandler
//             (mysql)
//
// Os geradores genericos (generate_object_rename, generate_grant...) chamam
// estes quando o dialeto corrente e' o do MySQL -- a tela nao precisa saber
// qual SGBD esta' do outro lado. Como em db/alter.hpp, nada aqui executa.
//
// Um usuario e' um ObjectRef de tipo `role`, com o HOST em `parent`: no MySQL
// a conta e' o par 'usuario'@'host'.
#pragma once

#include "base/process.hpp"
#include "db/alter.hpp"
#include "db/holt.hpp"
#include "db/object_info.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- Nomes --------------------------------------------------------------------------

// `banco`.`nome`; para conta, 'usuario'@'host'; para banco, `nome`.
[[nodiscard]] std::string mysql_object_name(const ObjectRef& ref);

// 'usuario'@'host' a partir de "usuario@host" (ou so' "usuario": host '%').
// Aceita tambem a forma ja' citada que o information_schema devolve.
[[nodiscard]] std::string mysql_account(std::string_view grantee);

// --- Consultas ----------------------------------------------------------------------

// UMA linha; o nome de cada coluna e' o rotulo. Vazio = tipo sem editor.
[[nodiscard]] std::string mysql_properties_query(const ObjectRef& ref);

// O comando SHOW CREATE do objeto e em que coluna da resposta o texto vem.
struct MysqlShowCreate {
    std::string sql;
    std::size_t column = 1;
};
[[nodiscard]] MysqlShowCreate mysql_show_create(const ObjectRef& ref);

// (grantee, privilege, grantable, grantor). Vazio = o tipo nao tem lista.
[[nodiscard]] std::string mysql_permissions_query(const ObjectRef& ref);

[[nodiscard]] ObjectEdit mysql_editable_property(ObjectType type,
                                                 std::string_view label) noexcept;
[[nodiscard]] std::vector<std::string_view> mysql_privileges_for(ObjectType type);

// --- Alteracoes ---------------------------------------------------------------------

[[nodiscard]] AlterScript mysql_object_rename(const ObjectRef& ref,
                                              std::string_view new_name);
[[nodiscard]] AlterScript mysql_object_comment(const ObjectRef& ref,
                                               std::string_view comment);
[[nodiscard]] AlterScript mysql_object_drop(const ObjectRef& ref);

[[nodiscard]] AlterScript mysql_grant(const ObjectRef& ref, std::string_view privilege,
                                      std::string_view grantee, bool with_grant_option);
[[nodiscard]] AlterScript mysql_revoke(const ObjectRef& ref, std::string_view privilege,
                                       std::string_view grantee);

// O fonte editado, como script: view por CREATE OR REPLACE; rotina, trigger e
// evento por DROP + CREATE -- o MySQL nao tem OR REPLACE para eles, e e' o que
// o DBeaver faz. `error` quando o tipo nao se grava assim.
[[nodiscard]] bool mysql_source_editable(ObjectType type) noexcept;
[[nodiscard]] AlterScript mysql_source_script(const ObjectRef& ref,
                                              std::string_view source);

// --- CREATE -------------------------------------------------------------------------

[[nodiscard]] AlterScript mysql_create_database(std::string_view name,
                                                std::string_view charset,
                                                std::string_view collation);

struct MysqlNewUser {
    std::string name;
    std::string host = "%";
    std::string password;
};
[[nodiscard]] AlterScript mysql_create_user(const MysqlNewUser& user);

// Vazia = conta sem senha. `user` vazio = a conta da propria sessao.
[[nodiscard]] AlterScript mysql_user_password(std::string_view user,
                                              std::string_view host,
                                              std::string_view password);

// Os esqueletos que o DBeaver poe no editor ao criar.
[[nodiscard]] std::string mysql_routine_template(std::string_view schema,
                                                 std::string_view name, bool procedure);
[[nodiscard]] std::string mysql_trigger_template(std::string_view schema,
                                                 std::string_view table,
                                                 std::string_view name,
                                                 std::string_view timing,
                                                 std::string_view event);
[[nodiscard]] std::string mysql_event_template(std::string_view schema,
                                               std::string_view name);

// --- Tools --------------------------------------------------------------------------

enum class MysqlTableTool : std::uint8_t { analyze, check, optimize, repair };

// O comando de manutencao. DEVOLVE LINHAS (Table, Op, Msg_type, Msg_text): a
// tela o roda como consulta, para o resultado aparecer. `option` e' a opcao do
// combo do DBeaver ("QUICK", "EXTENDED"...), vazia = nenhuma.
[[nodiscard]] std::string mysql_table_tool_sql(MysqlTableTool tool,
                                               std::string_view schema,
                                               std::string_view table,
                                               std::string_view option = {});
[[nodiscard]] std::vector<std::string_view> mysql_table_tool_options(MysqlTableTool tool);

[[nodiscard]] AlterScript mysql_truncate(std::string_view schema, std::string_view table);

// KILL QUERY <id> interrompe a consulta; KILL CONNECTION <id> derruba a
// sessao. `id` vem da grade -- so' digitos passam.
[[nodiscard]] AlterScript mysql_session_kill(std::string_view id, bool connection);

// A consulta do Session Manager (MySQLSessionManager do DBeaver).
[[nodiscard]] std::string_view mysql_sessions_query() noexcept;

// --- Cliente nativo (mysqldump, mysql) -----------------------------------------------

// As opcoes do assistente "Dump database" (MySQLExportSettings).
struct MysqlDumpOptions {
    enum class Method : std::uint8_t { online, lock_all, normal };
    Method method = Method::online;   // --single-transaction / --lock-all-tables / nenhum
    bool no_create       = false;     // --no-create-info
    bool add_drop        = true;      // --add-drop-table
    bool disable_keys    = true;      // --disable-keys
    bool extended_insert = true;      // --extended-insert
    bool events          = false;     // --events
    bool routines        = false;     // --routines
    bool comments        = true;      // --comments
    bool hex_blob        = false;     // --hex-blob
    bool no_data         = false;     // --no-data
    std::string file;
    std::vector<std::string> tables;  // vazio = o banco inteiro
};

// Onde o programa esta': no PATH, ou no `bin` de uma instalacao do MySQL (o
// instalador do Windows nao o poe no PATH). Vazio se nao achou.
[[nodiscard]] std::string find_mysql_tool(std::string_view name);

// A senha vai em MYSQL_PWD, no AMBIENTE do processo -- na linha de comando ela
// apareceria na lista de processos da maquina.
[[nodiscard]] Result<ProcessOptions> mysql_dump_command(const ConnConfig& connection,
                                                        const MysqlDumpOptions& options,
                                                        std::string program);

// Executa um script (ou restaura um dump): `mysql ... --execute="source <arquivo>"`.
[[nodiscard]] Result<ProcessOptions> mysql_script_command(const ConnConfig& connection,
                                                          std::string_view file,
                                                          std::string program);

} // namespace otter::db
