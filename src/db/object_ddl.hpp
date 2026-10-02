// C-Otter -- db/object_ddl.hpp
//
// CREATE dos objetos que o DBeaver cria por dialogo, e as ferramentas de
// manutencao do menu "Tools" (PostgreSQL).
//
// Mapa do DBeaver (docs/OBJECT-EDITOR.md):
//
//   PostgreCreateDatabaseDialog    Database name, Owner, Template database,
//                                  Encoding, Tablespace
//   PostgreCreateSchemaDialog      Schema name, Owner
//   PostgreCreateExtensionDialog   Schema, Extension (lista das disponiveis)
//   PostgreCreateRoleDialog        Name, Password, Is user
//   PostgreCreateTablespaceDialog  Name, Owner, Location, Options
//   PostgreProcedureConfigurator   Name, Type, Language, Return type
//   PostgreEventTriggerConfigurator  Name, Event Type, Trigger function
//
//   Tools: Analyze, Vacuum (Full, Freeze, Analyzed, Disable page skipping,
//          Skip locked, Index cleanup, Truncate), Truncate (Only, Restart
//          identity, Cascade), Refresh Materialized View (With data),
//          Enable/Disable trigger
//
// Como em db/alter.hpp: nada aqui executa. Cada funcao devolve o script para
// a janela de confirmacao mostrar.
#pragma once

#include "db/alter.hpp"
#include "db/object_info.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- CREATE ------------------------------------------------------------------------

struct NewDatabase {
    std::string name;
    std::string owner;         // vazio = quem cria
    std::string template_db;   // vazio = template1
    std::string encoding;      // vazio = o do template
    std::string tablespace;    // vazio = o padrao
};
[[nodiscard]] AlterScript generate_create_database(const NewDatabase& database);

[[nodiscard]] AlterScript generate_create_schema(std::string_view name,
                                                 std::string_view owner);

[[nodiscard]] AlterScript generate_create_extension(std::string_view name,
                                                    std::string_view schema);

struct NewRole {
    std::string name;
    std::string password;   // so' vale com is_user
    bool        is_user = true;   // LOGIN
};
[[nodiscard]] AlterScript generate_create_role(const NewRole& role);

struct NewTablespace {
    std::string name;
    std::string owner;
    std::string location;   // diretorio NO SERVIDOR
    std::string options;    // "seq_page_cost=1, random_page_cost=1.1"
};
[[nodiscard]] AlterScript generate_create_tablespace(const NewTablespace& tablespace);

struct NewRoutine {
    std::string schema;
    std::string name;
    bool        procedure = false;
    std::string language = "sql";
    std::string return_type = "int4";   // so' para funcao
};
// O esqueleto que o DBeaver poe no editor ao criar uma rotina: nao e'
// executado direto, o usuario escreve o corpo e grava.
[[nodiscard]] std::string routine_template(const NewRoutine& routine);

// Os eventos de um event trigger, na ordem do combo do DBeaver.
[[nodiscard]] const std::vector<std::string_view>& event_trigger_events();

[[nodiscard]] AlterScript generate_create_event_trigger(std::string_view name,
                                                        std::string_view event,
                                                        std::string_view function);

struct NewPolicy {
    std::string schema;
    std::string table;
    std::string name;
    std::string command = "ALL";   // ALL, SELECT, INSERT, UPDATE, DELETE
    bool        permissive = true;
    std::string roles;             // "PUBLIC" ou lista; vazio = PUBLIC
    std::string using_expression;
    std::string check_expression;
};
[[nodiscard]] AlterScript generate_create_policy(const NewPolicy& policy);

[[nodiscard]] AlterScript generate_create_materialized_view(std::string_view schema,
                                                            std::string_view name,
                                                            std::string_view definition,
                                                            bool with_data = true);

// --- Papel -------------------------------------------------------------------------

// Os atributos booleanos de um papel, pelo rotulo que a consulta de
// propriedades devolve ("Superuser", "Can login"...). Vazio = nao e' atributo.
[[nodiscard]] std::string_view role_option_keyword(std::string_view label,
                                                   bool enabled) noexcept;

[[nodiscard]] AlterScript generate_role_option(std::string_view role,
                                               std::string_view label, bool enabled);

// A senha NAO aparece no texto que a janela de confirmacao mostra por padrao;
// quem chama decide. Vazia = remove a senha (PASSWORD NULL).
[[nodiscard]] AlterScript generate_role_password(std::string_view role,
                                                 std::string_view password);

// GRANT grupo TO membro / REVOKE grupo FROM membro.
[[nodiscard]] AlterScript generate_role_membership(std::string_view group,
                                                   std::string_view member,
                                                   bool grant);

// --- Tools -------------------------------------------------------------------------

struct VacuumOptions {
    bool full = false;
    bool freeze = false;
    bool analyze = false;
    bool disable_page_skipping = false;   // 9.6+
    bool skip_locked = false;             // 12+
    bool index_cleanup = false;           // 12+
    bool truncate = false;                // 12+
};
// `table` vazio = o banco inteiro.
[[nodiscard]] AlterScript generate_vacuum(std::string_view schema,
                                          std::string_view table,
                                          const VacuumOptions& options);

[[nodiscard]] AlterScript generate_analyze(std::string_view schema,
                                           std::string_view table);

struct TruncateOptions {
    bool only = false;
    bool restart_identity = false;
    bool cascade = false;
};
[[nodiscard]] AlterScript generate_truncate(std::string_view schema,
                                            std::string_view table,
                                            const TruncateOptions& options);

[[nodiscard]] AlterScript generate_refresh_materialized_view(std::string_view schema,
                                                             std::string_view name,
                                                             bool with_data = true);

// Trigger de tabela (`ref.parent` = a tabela) ou event trigger.
[[nodiscard]] AlterScript generate_trigger_enable(const ObjectRef& ref, bool enable);

[[nodiscard]] AlterScript generate_reindex(const ObjectRef& ref);

// --- Sessoes (Session Manager / Lock Manager) --------------------------------------

// pg_cancel_backend interrompe a consulta; pg_terminate_backend derruba a
// sessao. `pid` vem da grade -- so' digitos passam.
[[nodiscard]] AlterScript generate_session_kill(std::string_view pid, bool terminate);

} // namespace otter::db
