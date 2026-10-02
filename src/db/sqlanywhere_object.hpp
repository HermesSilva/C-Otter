// C-Otter -- db/sqlanywhere_object.hpp
//
// O perfil SQL Anywhere do editor de objeto, dos dialogos de criacao, das
// ferramentas e das sessoes: o que db/object_info.hpp faz para o PostgreSQL,
// db/mysql_object.hpp para o MySQL e db/mssql_object.hpp para o SQL Server.
//
// O DBeaver Community nao tem plugin do SQL Anywhere -- so' o driver generico
// "Sybase jConnect", com a arvore generica do JDBC. O alvo aqui e' o que o
// Sybase Central (a ferramenta do proprio SGBD) oferece para cada objeto;
// mapa em docs/SQLANYWHERE-MAP.md.
//
// Os geradores genericos (generate_object_rename, generate_grant...) chamam
// estes quando o dialeto corrente e' o do SQL Anywhere. Como em db/alter.hpp,
// nada aqui executa.
//
// O "schema" de um objeto e' o DONO dele (um usuario). Um usuario ou papel e'
// um ObjectRef de tipo `role`; o dono visto como no' da arvore, de tipo
// `schema`.
#pragma once

#include "db/alter.hpp"
#include "db/catalog.hpp"
#include "db/object_info.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- Nomes --------------------------------------------------------------------------

// "dono"."nome"; para coluna e trigger, "dono"."tabela"."nome"; "usuario".
[[nodiscard]] std::string sqlanywhere_object_name(const ObjectRef& ref);

// --- Consultas ----------------------------------------------------------------------

// UMA linha; o nome de cada coluna e' o rotulo. Vazio = tipo sem editor.
[[nodiscard]] std::string sqlanywhere_properties_query(const ObjectRef& ref);

// O texto do objeto, em uma linha de uma coluna. Para tabela e' o
// sa_get_table_definition do servidor: CREATE TABLE, comentarios, permissoes
// e triggers.
[[nodiscard]] std::string sqlanywhere_definition_query(const ObjectRef& ref);

// (grantee, privilege, grantable, grantor). Vazio = o tipo nao tem lista.
[[nodiscard]] std::string sqlanywhere_permissions_query(const ObjectRef& ref);

[[nodiscard]] ObjectEdit sqlanywhere_editable_property(ObjectType type,
                                                       std::string_view label) noexcept;
[[nodiscard]] std::vector<std::string_view> sqlanywhere_privileges_for(ObjectType type);

// --- Alteracoes ---------------------------------------------------------------------

[[nodiscard]] AlterScript sqlanywhere_object_rename(const ObjectRef& ref,
                                                    std::string_view new_name);
// COMMENT ON ... IS 'texto'; texto vazio remove (IS NULL).
[[nodiscard]] AlterScript sqlanywhere_object_comment(const ObjectRef& ref,
                                                     std::string_view comment);
[[nodiscard]] AlterScript sqlanywhere_object_drop(const ObjectRef& ref);

[[nodiscard]] AlterScript sqlanywhere_grant(const ObjectRef& ref, std::string_view privilege,
                                            std::string_view grantee,
                                            bool with_grant_option);
[[nodiscard]] AlterScript sqlanywhere_revoke(const ObjectRef& ref,
                                             std::string_view privilege,
                                             std::string_view grantee);

// O fonte editado: view, procedure, funcao, trigger e evento se gravam por
// ALTER, que preserva as permissoes (ao contrario de apagar e criar).
[[nodiscard]] bool sqlanywhere_source_editable(ObjectType type) noexcept;
[[nodiscard]] AlterScript sqlanywhere_source_script(const ObjectRef& ref,
                                                    std::string_view source);

// --- CREATE -------------------------------------------------------------------------

struct SqlAnywhereNewUser {
    std::string name;
    std::string password;       // vazio = usuario que nao entra (so' dono de objetos)
    std::string login_policy;   // vazio = a politica padrao
};
[[nodiscard]] AlterScript sqlanywhere_create_user(const SqlAnywhereNewUser& user);
[[nodiscard]] AlterScript sqlanywhere_create_role(std::string_view name);
[[nodiscard]] AlterScript sqlanywhere_user_password(std::string_view user,
                                                    std::string_view password);

struct SqlAnywhereNewSequence {
    std::string schema;
    std::string name;
    std::string start;       // vazios = os padroes do servidor
    std::string increment;
    std::string minimum;
    std::string maximum;
    bool        cycle = false;
};
[[nodiscard]] AlterScript sqlanywhere_create_sequence(const SqlAnywhereNewSequence& sequence);

// CREATE DOMAIN nome tipo [NOT NULL] [DEFAULT ...] [CHECK (...)].
[[nodiscard]] AlterScript sqlanywhere_create_domain(std::string_view name,
                                                    std::string_view base_type,
                                                    bool not_null,
                                                    std::string_view default_value,
                                                    std::string_view check);

// Os esqueletos que o dialogo de criacao abre no editor.
[[nodiscard]] std::string sqlanywhere_routine_template(std::string_view schema,
                                                       std::string_view name,
                                                       bool procedure);
[[nodiscard]] std::string sqlanywhere_trigger_template(std::string_view schema,
                                                       std::string_view table,
                                                       std::string_view name,
                                                       std::string_view timing,
                                                       std::string_view event);
[[nodiscard]] std::string sqlanywhere_event_template(std::string_view name);

// --- Tools --------------------------------------------------------------------------

enum class SqlAnywhereTableTool : std::uint8_t {
    validate,            // VALIDATE TABLE: confere paginas e indices
    reorganize,          // REORGANIZE TABLE: desfragmenta
    create_statistics,   // CREATE STATISTICS: refaz os histogramas do otimizador
};
[[nodiscard]] AlterScript sqlanywhere_table_tool(SqlAnywhereTableTool tool,
                                                 std::string_view schema,
                                                 std::string_view table);
[[nodiscard]] AlterScript sqlanywhere_truncate(std::string_view schema,
                                               std::string_view table);

// REFRESH MATERIALIZED VIEW (recalcula) e ALTER ... ENABLE | DISABLE.
[[nodiscard]] AlterScript sqlanywhere_refresh_view(std::string_view schema,
                                                   std::string_view view);
[[nodiscard]] AlterScript sqlanywhere_view_enable(const ObjectRef& ref, bool enable);

// ALTER EVENT ... ENABLE | DISABLE, e TRIGGER EVENT (dispara agora).
[[nodiscard]] AlterScript sqlanywhere_event_enable(std::string_view name, bool enable);
[[nodiscard]] AlterScript sqlanywhere_event_trigger(std::string_view name);

// CHECKPOINT e VALIDATE DATABASE, sobre o banco da conexao.
[[nodiscard]] AlterScript sqlanywhere_checkpoint();
[[nodiscard]] AlterScript sqlanywhere_validate_database();

// BACKUP DATABASE DIRECTORY '<pasta>'. A pasta e' NO SERVIDOR: quem grava e' o
// processo do servidor de banco.
[[nodiscard]] AlterScript sqlanywhere_backup_database(std::string_view directory);

// --- Sessoes ------------------------------------------------------------------------

// Uma linha por conexao do banco (sa_conn_info), com quem a bloqueia.
[[nodiscard]] std::string_view sqlanywhere_sessions_query() noexcept;
// As travas em vigor (sa_locks).
[[nodiscard]] std::string_view sqlanywhere_locks_query() noexcept;
// DROP CONNECTION <numero>. O numero vem da grade -- so' digitos passam.
[[nodiscard]] AlterScript sqlanywhere_session_kill(std::string_view id);

} // namespace otter::db
