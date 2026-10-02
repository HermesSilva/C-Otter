// C-Otter -- db/object_info.hpp
//
// Um objeto do banco visto de perto: as propriedades, o DDL, as permissoes e
// as estatisticas dele -- o que o editor de objeto do DBeaver mostra ao dar
// duplo clique num no' da arvore.
//
// A arvore (db/catalog.hpp) responde "o que existe"; isto responde "como e'
// este aqui". Sao consultas diferentes: a arvore le o minimo de muitos
// objetos, o editor le tudo de um.
//
// Mapa do DBeaver (docs/OBJECT-EDITOR.md): 27 gerenciadores de objeto no
// PostgreSQL, cada um com ate' quatro operacoes (criar, alterar, renomear,
// remover), e quatro abas de editor -- Properties, DDL/Source, Permissions,
// Data.
#pragma once

#include "db/alter.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

enum class ObjectType : std::uint8_t {
    database,
    schema,
    table,
    view,
    materialized_view,
    foreign_table,
    column,
    index,
    constraint,       // PRIMARY KEY, UNIQUE, CHECK, EXCLUDE
    foreign_key,
    trigger,
    rule,
    policy,
    sequence,
    function,
    procedure,
    aggregate,
    data_type,
    role,
    extension,
    tablespace,
    event_trigger,
    foreign_server,
    foreign_data_wrapper,
    user_mapping,
    language,
    event,            // MySQL: evento agendado (CREATE EVENT)
};

// Quantos tipos ha': para percorrer o enum (nome -> tipo).
inline constexpr int kObjectTypeCount = static_cast<int>(ObjectType::event) + 1;

// "table", "materialized view" -- em ingles, chave de traducao.
[[nodiscard]] std::string_view to_string(ObjectType type) noexcept;

// Como o comando SQL chama o tipo: "TABLE", "MATERIALIZED VIEW", "FUNCTION".
[[nodiscard]] std::string_view sql_keyword(ObjectType type) noexcept;

// Quem e' o objeto. Nem todo campo vale para todo tipo:
//
//   schema     -- o schema, para o que mora em schema
//   name       -- o nome
//   parent     -- a tabela, para coluna, indice, constraint, trigger, regra e
//                 politica; o servidor, para user mapping
//   signature  -- os argumentos, para funcao, procedure e agregado: sobrecargas
//                 compartilham o nome, e so' a assinatura as distingue
struct ObjectRef {
    ObjectType  type = ObjectType::table;
    std::string schema;
    std::string name;
    std::string parent;
    std::string signature;

    // Identidade para cache e para achar a aba ja' aberta.
    [[nodiscard]] std::string key() const;

    // O rotulo da aba: "cliente", "fn_total(integer)".
    [[nodiscard]] std::string title() const;
};

// O que o editor deixa mudar numa propriedade. Cada valor e' UM comando
// ALTER -- nao ha' "editar qualquer coisa".
enum class ObjectEdit : std::uint8_t {
    none,
    name,         // ALTER ... RENAME TO
    comment,      // COMMENT ON ... IS
    owner,        // ALTER ... OWNER TO
    schema,       // ALTER ... SET SCHEMA
    tablespace,   // ALTER ... SET TABLESPACE
};

struct ObjectProperty {
    std::string name;      // rotulo em ingles (chave de traducao)
    std::string value;
    ObjectEdit  edit = ObjectEdit::none;
};

// Uma concessao: quem pode o que. `column` so' para privilegio de coluna.
struct ObjectPermission {
    std::string grantee;   // "PUBLIC" para o pseudo-papel
    std::string privilege;
    std::string grantor;
    bool        grantable = false;
};

struct ObjectInfo {
    std::vector<ObjectProperty>   properties;
    std::vector<ObjectProperty>   statistics;    // pg_stat_*: pares nome/valor
    std::string                   ddl;
    std::vector<ObjectPermission> permissions;

    // O tipo tem lista de permissoes? (Um trigger nao tem; uma tabela tem.)
    bool has_permissions = false;

    // O que falhou ao ler. As partes que deram certo continuam validas: sem
    // privilegio para ler as permissoes, as propriedades ainda aparecem.
    std::string error;
};

// --- Consultas (PostgreSQL) --------------------------------------------------------
//
// SQL puro, sem executar: quem roda e' o leitor de catalogo. Separado para o
// teste conferir o que e' montado -- citacao de nomes, assinatura de funcao --
// sem servidor.

// `'"schema"."nome"'::regclass` -- a relacao como o catalogo a resolve. As
// aspas duplas sao obrigatorias: sem elas "TIDxAcao" vira "tidxacao".
[[nodiscard]] std::string pg_regclass(std::string_view schema, std::string_view name);

// UMA linha; o nome de cada coluna e' o rotulo da propriedade. Vazio quando o
// tipo nao tem consulta de propriedades.
[[nodiscard]] std::string pg_properties_query(const ObjectRef& ref);

// O DDL, em uma ou mais linhas de texto (concatenadas com quebra de linha).
// Vazio para TABELA: la' o DDL e' montado no cliente (db/ddl.hpp), porque o
// PostgreSQL nao tem um pg_get_tabledef.
[[nodiscard]] std::string pg_ddl_query(const ObjectRef& ref);

// (grantee, privilege, grantable, grantor). Vazio quando o tipo nao tem ACL.
[[nodiscard]] std::string pg_permissions_query(const ObjectRef& ref);

// UMA linha de estatisticas (pg_stat_*), rotulos nos nomes das colunas.
[[nodiscard]] std::string pg_statistics_query(const ObjectRef& ref);

// Quais propriedades o editor deixa alterar, pelo rotulo e pelo tipo.
[[nodiscard]] ObjectEdit editable_property(ObjectType type,
                                           std::string_view label) noexcept;

// Os privilegios que se pode conceder sobre o tipo: SELECT, INSERT... para
// tabela; USAGE, CREATE para schema; EXECUTE para funcao. Vazio = nao tem.
[[nodiscard]] std::vector<std::string_view> privileges_for(ObjectType type);

// --- Como o objeto e' citado num comando -------------------------------------------

// "schema.nome", "schema.fn(args)", "nome" (papel, extensao, banco).
[[nodiscard]] std::string object_sql_name(const ObjectRef& ref);

// --- Alteracoes --------------------------------------------------------------------
//
// Todas devolvem um script para a UI mostrar e o usuario confirmar (mesma
// regra do db/alter.hpp: nada aqui executa). `error` preenchido quando o tipo
// nao aceita a operacao -- em vez de gerar um comando que o servidor recusa.

[[nodiscard]] AlterScript generate_object_rename(const ObjectRef& ref,
                                                 std::string_view new_name);
[[nodiscard]] AlterScript generate_object_comment(const ObjectRef& ref,
                                                  std::string_view comment);
[[nodiscard]] AlterScript generate_object_owner(const ObjectRef& ref,
                                                std::string_view owner);
[[nodiscard]] AlterScript generate_object_schema(const ObjectRef& ref,
                                                 std::string_view new_schema);
[[nodiscard]] AlterScript generate_object_tablespace(const ObjectRef& ref,
                                                     std::string_view tablespace);

// DROP. Sempre destrutivo, sempre com aviso. `cascade` leva junto o que
// depende do objeto -- e o aviso diz isso.
[[nodiscard]] AlterScript generate_object_drop(const ObjectRef& ref,
                                               bool cascade = false);

// GRANT / REVOKE de UM privilegio a UM papel.
[[nodiscard]] AlterScript generate_grant(const ObjectRef& ref,
                                         std::string_view privilege,
                                         std::string_view grantee,
                                         bool with_grant_option = false);
[[nodiscard]] AlterScript generate_revoke(const ObjectRef& ref,
                                          std::string_view privilege,
                                          std::string_view grantee);

} // namespace otter::db
