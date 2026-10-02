// C-Otter -- testes de db/object_info: o editor de objeto.
//
// As consultas ao catalogo sao conferidas contra o servidor em
// tests/integration/test_object_info_live.cpp. Aqui fica o que NAO precisa de
// servidor e tem um jeito errado silencioso: citar o nome (uma tabela
// "TIDxAcao" sem aspas vira "tidxacao" e a consulta nao acha nada), a
// assinatura da sobrecarga, o comando que cada tipo aceita.
#include "test_main.hpp"

#include "db/ddl.hpp"
#include "db/object_info.hpp"

#include <set>
#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

struct DialectGuard {
    QuoteStyle previous = sql_dialect();
    explicit DialectGuard(QuoteStyle style) { set_sql_dialect(style); }
    ~DialectGuard() { set_sql_dialect(previous); }
};

ObjectRef object(ObjectType type, std::string name, std::string schema = "otter_test",
                 std::string parent = {}, std::string signature = {}) {
    ObjectRef ref;
    ref.type      = type;
    ref.schema    = std::move(schema);
    ref.name      = std::move(name);
    ref.parent    = std::move(parent);
    ref.signature = std::move(signature);
    return ref;
}

constexpr ObjectType kAllTypes[] = {
    ObjectType::database,       ObjectType::schema,
    ObjectType::table,          ObjectType::view,
    ObjectType::materialized_view, ObjectType::foreign_table,
    ObjectType::column,         ObjectType::index,
    ObjectType::constraint,     ObjectType::foreign_key,
    ObjectType::trigger,        ObjectType::rule,
    ObjectType::policy,         ObjectType::sequence,
    ObjectType::function,       ObjectType::procedure,
    ObjectType::aggregate,      ObjectType::data_type,
    ObjectType::role,           ObjectType::extension,
    ObjectType::tablespace,     ObjectType::event_trigger,
    ObjectType::foreign_server, ObjectType::foreign_data_wrapper,
    ObjectType::user_mapping,   ObjectType::language,
};

} // namespace

// --- Todo tipo tem editor ----------------------------------------------------------

OTTER_TEST(object_info_every_type_has_a_properties_query) {
    // Um tipo sem consulta abriria um editor vazio -- e o `switch` sem
    // `default` so' avisa na compilacao se o aviso estiver ligado.
    for (const ObjectType type : kAllTypes) {
        const ObjectRef ref = object(type, "x", "s", "t", "integer");
        OTTER_CHECK(!pg_properties_query(ref).empty());
        OTTER_CHECK(!to_string(type).empty());
        OTTER_CHECK(!sql_keyword(type).empty());
    }
}

OTTER_TEST(object_info_only_the_table_ddl_is_built_on_the_client) {
    for (const ObjectType type : kAllTypes) {
        const bool client_side =
            type == ObjectType::table || type == ObjectType::foreign_table;
        const ObjectRef ref = object(type, "x", "s", "t", "integer");
        OTTER_CHECK(pg_ddl_query(ref).empty() == client_side);
    }
}

OTTER_TEST(object_info_type_names_are_distinct) {
    std::set<std::string_view> names;
    for (const ObjectType type : kAllTypes) names.insert(to_string(type));
    OTTER_CHECK(names.size() == std::size(kAllTypes));
}

// --- Citacao -----------------------------------------------------------------------

OTTER_TEST(object_info_query_quotes_mixed_case_names) {
    // 'TIDxAcao'::regclass rebaixa para minusculas e falha; precisa ser
    // '"TIDxAcao"'::regclass.
    const std::string sql =
        pg_properties_query(object(ObjectType::table, "TIDxAcao", "Vendas"));
    OTTER_CHECK(has(sql, "'\"Vendas\".\"TIDxAcao\"'::regclass"));
}

OTTER_TEST(object_info_query_escapes_quotes_in_names) {
    // Um nome com aspa simples nao pode fechar o literal da consulta.
    const std::string sql =
        pg_properties_query(object(ObjectType::schema, "o'brien", ""));
    OTTER_CHECK(has(sql, "'o''brien'"));
    OTTER_CHECK(!has(sql, "'o'brien'"));

    const std::string relation =
        pg_properties_query(object(ObjectType::table, "a\"b'c", "s"));
    OTTER_CHECK(has(relation, "'\"s\".\"a\"\"b''c\"'::regclass"));
}

OTTER_TEST(object_info_routine_query_uses_the_signature) {
    // Sobrecargas compartilham o nome: sem os argumentos, regprocedure falha
    // com "mais de uma funcao" -- ou pior, acha a errada.
    const std::string sql = pg_ddl_query(
        object(ObjectType::function, "fn_total", "otter_test", {}, "integer, text"));
    OTTER_CHECK(has(sql, "'\"otter_test\".\"fn_total\"(integer, text)'::regprocedure"));
    OTTER_CHECK(has(sql, "pg_get_functiondef"));
}

OTTER_TEST(object_info_aggregate_ddl_does_not_use_functiondef) {
    // pg_get_functiondef recusa agregado ("e' uma funcao de agregacao").
    const std::string sql = pg_ddl_query(
        object(ObjectType::aggregate, "soma_total", "otter_test", {}, "numeric"));
    OTTER_CHECK(!has(sql, "pg_get_functiondef"));
    OTTER_CHECK(has(sql, "pg_aggregate"));
}

OTTER_TEST(object_info_role_ddl_never_reads_the_password) {
    const std::string sql = pg_ddl_query(object(ObjectType::role, "ana", ""));
    OTTER_CHECK(!has(sql, "rolpassword"));
    OTTER_CHECK(!has(sql, "pg_authid "));
    OTTER_CHECK(has(sql, "pg_roles"));
}

OTTER_TEST(object_info_user_mapping_ddl_hides_the_options) {
    // As opcoes do mapeamento guardam a senha do servidor remoto.
    const std::string sql =
        pg_ddl_query(object(ObjectType::user_mapping, "public", "", "srv"));
    OTTER_CHECK(!has(sql, "umoptions"));
}

// --- Permissoes --------------------------------------------------------------------

OTTER_TEST(object_info_permissions_fall_back_to_the_default_acl) {
    // relacl nulo significa "o padrao", nao "ninguem". Sem acldefault a aba
    // de permissoes de uma tabela recem-criada apareceria vazia.
    const std::string sql =
        pg_permissions_query(object(ObjectType::table, "cliente"));
    OTTER_CHECK(has(sql, "acldefault('r', c.relowner)"));
    OTTER_CHECK(has(sql, "aclexplode"));

    // Sequencia e servidor externo diferem so' na caixa da letra.
    OTTER_CHECK(has(pg_permissions_query(object(ObjectType::sequence, "s")),
                    "acldefault('s', "));
    OTTER_CHECK(has(pg_permissions_query(object(ObjectType::foreign_server, "s")),
                    "acldefault('S', "));
}

OTTER_TEST(object_info_types_without_acl_have_no_permissions) {
    for (const ObjectType type :
         {ObjectType::index, ObjectType::trigger, ObjectType::rule, ObjectType::policy,
          ObjectType::constraint, ObjectType::role, ObjectType::extension,
          ObjectType::event_trigger, ObjectType::user_mapping}) {
        const ObjectRef ref = object(type, "x", "s", "t");
        OTTER_CHECK(pg_permissions_query(ref).empty());
        OTTER_CHECK(privileges_for(type).empty());
        OTTER_CHECK(!generate_grant(ref, "SELECT", "ana").ok());
    }
}

OTTER_TEST(object_info_permission_query_and_privilege_list_agree) {
    // Tipo com lista de privilegios e sem consulta (ou o contrario) daria uma
    // aba que concede e nao mostra o que concedeu.
    for (const ObjectType type : kAllTypes) {
        const ObjectRef ref = object(type, "x", "s", "t", "integer");
        OTTER_CHECK(pg_permissions_query(ref).empty() == privileges_for(type).empty());
    }
}

OTTER_TEST(object_info_grant_and_revoke) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    const ObjectRef table = object(ObjectType::table, "cliente");
    AlterScript script = generate_grant(table, "SELECT", "ana");
    OTTER_CHECK(script.ok());
    OTTER_CHECK(script.statements.front() ==
                "GRANT SELECT ON otter_test.cliente TO ana;");

    script = generate_grant(table, "UPDATE", "Ana Maria", true);
    OTTER_CHECK(script.statements.front() ==
                "GRANT UPDATE ON otter_test.cliente TO \"Ana Maria\" WITH GRANT OPTION;");

    script = generate_revoke(table, "SELECT", "PUBLIC");
    OTTER_CHECK(script.statements.front() ==
                "REVOKE SELECT ON otter_test.cliente FROM PUBLIC;");

    // PUBLIC e' palavra, nao papel: sem aspas, e sem WITH GRANT OPTION.
    script = generate_grant(table, "SELECT", "PUBLIC", true);
    OTTER_CHECK(script.statements.front() ==
                "GRANT SELECT ON otter_test.cliente TO PUBLIC;");
}

OTTER_TEST(object_info_grant_targets_by_type) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    OTTER_CHECK(generate_grant(object(ObjectType::schema, "vendas", ""), "USAGE", "ana")
                    .statements.front() == "GRANT USAGE ON SCHEMA vendas TO ana;");
    OTTER_CHECK(generate_grant(object(ObjectType::function, "fn", "s", {}, "integer"),
                               "EXECUTE", "ana")
                    .statements.front() ==
                "GRANT EXECUTE ON FUNCTION s.fn(integer) TO ana;");
    OTTER_CHECK(generate_grant(object(ObjectType::procedure, "sp", "s", {}, ""),
                               "EXECUTE", "ana")
                    .statements.front() == "GRANT EXECUTE ON PROCEDURE s.sp() TO ana;");
    OTTER_CHECK(generate_grant(object(ObjectType::sequence, "sq", "s"), "USAGE", "ana")
                    .statements.front() == "GRANT USAGE ON SEQUENCE s.sq TO ana;");
    // Privilegio de coluna: a lista vai DEPOIS do privilegio, antes do ON.
    OTTER_CHECK(generate_grant(object(ObjectType::column, "email", "s", "cliente"),
                               "UPDATE", "ana")
                    .statements.front() == "GRANT UPDATE (email) ON s.cliente TO ana;");
    OTTER_CHECK(generate_grant(object(ObjectType::database, "erp", ""), "CONNECT", "ana")
                    .statements.front() == "GRANT CONNECT ON DATABASE erp TO ana;");
}

OTTER_TEST(object_info_grant_refuses_unknown_privilege) {
    // O privilegio entra no comando SEM aspas: texto livre ali seria injecao.
    const ObjectRef table = object(ObjectType::table, "cliente");
    OTTER_CHECK(!generate_grant(table, "SELECT ON x TO y; DROP TABLE z; --", "ana").ok());
    OTTER_CHECK(!generate_grant(table, "EXECUTE", "ana").ok());
    OTTER_CHECK(!generate_revoke(table, "USAGE", "ana").ok());
    OTTER_CHECK(generate_grant(table, "ALL", "ana").ok());
    OTTER_CHECK(!generate_grant(table, "SELECT", "").ok());
}

// --- Renomear, comentar, mover -----------------------------------------------------

OTTER_TEST(object_info_rename_by_type) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    const auto first = [](const AlterScript& script) {
        return script.statements.empty() ? std::string{} : script.statements.front();
    };

    OTTER_CHECK(first(generate_object_rename(object(ObjectType::table, "cliente"), "Cli")) ==
                "ALTER TABLE otter_test.cliente RENAME TO \"Cli\";");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::column, "email", "s", "cliente"), "e_mail")) ==
                "ALTER TABLE s.cliente RENAME COLUMN email TO e_mail;");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::constraint, "pk", "s", "cliente"), "pk2")) ==
                "ALTER TABLE s.cliente RENAME CONSTRAINT pk TO pk2;");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::trigger, "tg", "s", "pedido"), "tg2")) ==
                "ALTER TRIGGER tg ON s.pedido RENAME TO tg2;");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::function, "fn", "s", {}, "integer"), "fn2")) ==
                "ALTER FUNCTION s.fn(integer) RENAME TO fn2;");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::materialized_view, "mv", "s"), "mv2")) ==
                "ALTER MATERIALIZED VIEW s.mv RENAME TO mv2;");
    OTTER_CHECK(first(generate_object_rename(object(ObjectType::schema, "a", ""), "b")) ==
                "ALTER SCHEMA a RENAME TO b;");
    OTTER_CHECK(first(generate_object_rename(object(ObjectType::data_type, "t", "s"), "u")) ==
                "ALTER TYPE s.t RENAME TO u;");
    OTTER_CHECK(first(generate_object_rename(
                    object(ObjectType::foreign_server, "srv", ""), "srv2")) ==
                "ALTER SERVER srv RENAME TO srv2;");
}

OTTER_TEST(object_info_rename_refusals_and_warnings) {
    const ObjectRef table = object(ObjectType::table, "cliente");
    OTTER_CHECK(!generate_object_rename(table, "").ok());
    OTTER_CHECK(!generate_object_rename(table, "cliente").ok());
    OTTER_CHECK(!generate_object_rename(object(ObjectType::extension, "x", ""), "y").ok());

    // Renomear papel apaga a senha MD5; o usuario precisa saber ANTES.
    const AlterScript role =
        generate_object_rename(object(ObjectType::role, "ana", ""), "ana2");
    OTTER_CHECK(role.ok());
    OTTER_CHECK(!role.warnings.empty());
}

OTTER_TEST(object_info_comment) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    OTTER_CHECK(generate_object_comment(object(ObjectType::table, "cliente"), "It's ok")
                    .statements.front() ==
                "COMMENT ON TABLE otter_test.cliente IS 'It''s ok';");
    // Vazio remove: IS NULL, e nao IS ''.
    OTTER_CHECK(generate_object_comment(object(ObjectType::view, "v", "s"), "")
                    .statements.front() == "COMMENT ON VIEW s.v IS NULL;");
    OTTER_CHECK(generate_object_comment(object(ObjectType::column, "email", "s", "cliente"),
                                        "x")
                    .statements.front() == "COMMENT ON COLUMN s.cliente.email IS 'x';");
    OTTER_CHECK(generate_object_comment(object(ObjectType::trigger, "tg", "s", "pedido"), "x")
                    .statements.front() == "COMMENT ON TRIGGER tg ON s.pedido IS 'x';");
    OTTER_CHECK(generate_object_comment(object(ObjectType::foreign_key, "fk", "s", "pedido"),
                                        "x")
                    .statements.front() == "COMMENT ON CONSTRAINT fk ON s.pedido IS 'x';");
    OTTER_CHECK(generate_object_comment(object(ObjectType::function, "fn", "s", {}, "integer"),
                                        "x")
                    .statements.front() == "COMMENT ON FUNCTION s.fn(integer) IS 'x';");
    OTTER_CHECK(
        !generate_object_comment(object(ObjectType::user_mapping, "u", "", "srv"), "x").ok());
}

OTTER_TEST(object_info_comment_ignores_the_mysql_escape) {
    // O literal do COMMENT e' sempre do padrao SQL -- o comando so' existe no
    // PostgreSQL --, mesmo se o dialeto corrente ficou em MySQL por engano.
    const DialectGuard guard(QuoteStyle::double_quotes);
    OTTER_CHECK(generate_object_comment(object(ObjectType::table, "t", "s"), "a\\b")
                    .statements.front() == "COMMENT ON TABLE s.t IS 'a\\b';");
}

OTTER_TEST(object_info_owner_schema_tablespace) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    OTTER_CHECK(generate_object_owner(object(ObjectType::table, "cliente"), "ana")
                    .statements.front() == "ALTER TABLE otter_test.cliente OWNER TO ana;");
    OTTER_CHECK(!generate_object_owner(object(ObjectType::index, "ix", "s"), "ana").ok());
    OTTER_CHECK(!generate_object_owner(object(ObjectType::table, "cliente"), "").ok());

    OTTER_CHECK(generate_object_schema(object(ObjectType::table, "cliente"), "vendas")
                    .statements.front() ==
                "ALTER TABLE otter_test.cliente SET SCHEMA vendas;");
    OTTER_CHECK(!generate_object_schema(object(ObjectType::table, "cliente"), "otter_test")
                     .ok());
    OTTER_CHECK(!generate_object_schema(object(ObjectType::trigger, "tg", "s", "t"), "x").ok());

    const AlterScript moved =
        generate_object_tablespace(object(ObjectType::index, "ix", "s"), "rapido");
    OTTER_CHECK(moved.ok());
    OTTER_CHECK(moved.statements.front() == "ALTER INDEX s.ix SET TABLESPACE rapido;");
    OTTER_CHECK(!moved.warnings.empty());   // reescreve o objeto, com trava
    OTTER_CHECK(!generate_object_tablespace(object(ObjectType::view, "v", "s"), "x").ok());
}

OTTER_TEST(object_info_editable_properties) {
    OTTER_CHECK(editable_property(ObjectType::table, "Name") == ObjectEdit::name);
    OTTER_CHECK(editable_property(ObjectType::table, "Comment") == ObjectEdit::comment);
    OTTER_CHECK(editable_property(ObjectType::table, "Owner") == ObjectEdit::owner);
    OTTER_CHECK(editable_property(ObjectType::table, "Schema") == ObjectEdit::schema);
    OTTER_CHECK(editable_property(ObjectType::table, "Tablespace") == ObjectEdit::tablespace);
    OTTER_CHECK(editable_property(ObjectType::table, "Object ID") == ObjectEdit::none);
    OTTER_CHECK(editable_property(ObjectType::table, "Total size") == ObjectEdit::none);

    OTTER_CHECK(editable_property(ObjectType::extension, "Name") == ObjectEdit::none);
    OTTER_CHECK(editable_property(ObjectType::trigger, "Owner") == ObjectEdit::none);
    OTTER_CHECK(editable_property(ObjectType::view, "Tablespace") == ObjectEdit::none);
    OTTER_CHECK(editable_property(ObjectType::index, "Schema") == ObjectEdit::none);
}

OTTER_TEST(object_info_what_is_editable_generates_a_command) {
    // Propriedade marcada como editavel cujo gerador recusa seria um campo
    // que aceita digitacao e falha ao gravar (diretiva 6).
    for (const ObjectType type : kAllTypes) {
        const ObjectRef ref = object(type, "x", "s", "t", "integer");
        if (editable_property(type, "Name") != ObjectEdit::none) {
            OTTER_CHECK(generate_object_rename(ref, "y").ok());
        }
        if (editable_property(type, "Comment") != ObjectEdit::none) {
            OTTER_CHECK(generate_object_comment(ref, "c").ok());
        }
        if (editable_property(type, "Owner") != ObjectEdit::none) {
            OTTER_CHECK(generate_object_owner(ref, "ana").ok());
        }
        if (editable_property(type, "Schema") != ObjectEdit::none) {
            OTTER_CHECK(generate_object_schema(ref, "outro").ok());
        }
        if (editable_property(type, "Tablespace") != ObjectEdit::none) {
            OTTER_CHECK(generate_object_tablespace(ref, "ts").ok());
        }
    }
}

// --- DROP --------------------------------------------------------------------------

OTTER_TEST(object_info_drop_by_type) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    const auto first = [](const AlterScript& script) {
        return script.statements.empty() ? std::string{} : script.statements.front();
    };

    OTTER_CHECK(first(generate_object_drop(object(ObjectType::materialized_view, "mv", "s"))) ==
                "DROP MATERIALIZED VIEW s.mv;");
    OTTER_CHECK(first(generate_object_drop(
                    object(ObjectType::function, "fn", "s", {}, "integer, text"))) ==
                "DROP FUNCTION s.fn(integer, text);");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::policy, "pl", "s", "doc"))) ==
                "DROP POLICY pl ON s.doc;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::rule, "rl", "s", "doc"), true)) ==
                "DROP RULE rl ON s.doc CASCADE;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::column, "email", "s", "cli"))) ==
                "ALTER TABLE s.cli DROP COLUMN email;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::foreign_key, "fk", "s", "ped"))) ==
                "ALTER TABLE s.ped DROP CONSTRAINT fk;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::user_mapping, "public", "",
                                                  "srv"))) ==
                "DROP USER MAPPING FOR PUBLIC SERVER srv;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::event_trigger, "ev", ""))) ==
                "DROP EVENT TRIGGER ev;");

    // Papel e banco nao tem CASCADE: pedir nao pode gerar comando invalido.
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::role, "ana", ""), true)) ==
                "DROP ROLE ana;");
    OTTER_CHECK(first(generate_object_drop(object(ObjectType::database, "erp", ""), true)) ==
                "DROP DATABASE erp;");
}

OTTER_TEST(object_info_drop_is_always_destructive) {
    for (const ObjectType type : kAllTypes) {
        const AlterScript script = generate_object_drop(object(type, "x", "s", "t", ""));
        OTTER_CHECK(script.ok());
        OTTER_CHECK(script.has_destructive());
    }
    OTTER_CHECK(!generate_object_drop(object(ObjectType::table, "")).ok());

    const AlterScript cascade =
        generate_object_drop(object(ObjectType::table, "cliente"), true);
    OTTER_CHECK(has(cascade.statements.front(), "CASCADE"));
    OTTER_CHECK(!cascade.warnings.empty());
}

// --- Identidade --------------------------------------------------------------------

OTTER_TEST(object_info_key_distinguishes_overloads_and_parents) {
    const ObjectRef a = object(ObjectType::function, "fn", "s", {}, "integer");
    const ObjectRef b = object(ObjectType::function, "fn", "s", {}, "text");
    OTTER_CHECK(a.key() != b.key());

    // A mesma coluna `id` em duas tabelas; e um indice e uma tabela homonimos.
    OTTER_CHECK(object(ObjectType::column, "id", "s", "cliente").key() !=
                object(ObjectType::column, "id", "s", "pedido").key());
    OTTER_CHECK(object(ObjectType::table, "x", "s").key() !=
                object(ObjectType::index, "x", "s").key());

    // "a" + "bc" nao pode colidir com "ab" + "c".
    OTTER_CHECK(object(ObjectType::table, "bc", "a").key() !=
                object(ObjectType::table, "c", "ab").key());

    OTTER_CHECK(a.title() == "fn(integer)");
    OTTER_CHECK(object(ObjectType::table, "cliente").title() == "cliente");
}
