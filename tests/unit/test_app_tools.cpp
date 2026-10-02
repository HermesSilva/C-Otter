// C-Otter -- testes de db/app_tools e da inicializacao de sessao: as regras
// por tras dos comandos de aplicacao e do navegador.
//
// O que se protege aqui: a URL que vira perfil (uma porta lida errado conecta
// ao servidor errado), a copia avancada (aspas que nao dobram quebram a
// colagem), o filtro de objetos, e -- o defeito que motivou o arquivo -- os
// campos do dialogo de conexao que eram gravados e nunca aplicados.
#include "test_main.hpp"

#include "db/app_tools.hpp"
#include "db/connection_config.hpp"
#include "db/edit.hpp"
#include "db/holt.hpp"
#include "db/result_set.hpp"

#include <string>
#include <vector>

using namespace otter;
using namespace otter::db;

namespace {

ResultSet two_by_two() {
    ResultSetBuilder builder;
    for (const char* name : {"id", "nome"}) {
        ColumnInfo info;
        info.name = name;
        info.kind = DataKind::string;
        builder.add_column(std::move(info));
    }
    builder.append_text(0, "1");
    builder.append_text(1, "Ana \"A\"");
    builder.append_text(0, "2");
    builder.append_null(1);
    builder.set_row_count(2);
    return builder.take();
}

GridSelection whole() {
    GridSelection selection;
    selection.row_first = 0;
    selection.row_last  = 1;
    selection.columns   = {0, 1};
    return selection;
}

} // namespace

// --- URL -----------------------------------------------------------------------

OTTER_TEST(url_jdbc_postgres_fills_the_profile) {
    const auto profile = profile_from_url(
        "jdbc:postgresql://db.example.com:5433/vendas?user=ana&password=p%40ss&sslmode=require&ApplicationName=x");
    OTTER_CHECK(profile.has_value());
    OTTER_CHECK_EQ(profile->driver_id, std::string{"postgresql"});
    OTTER_CHECK_EQ(profile->host, std::string{"db.example.com"});
    OTTER_CHECK_EQ(profile->port, std::uint16_t{5433});
    OTTER_CHECK_EQ(profile->database, std::string{"vendas"});
    OTTER_CHECK_EQ(profile->user, std::string{"ana"});
    OTTER_CHECK_EQ(profile->password, std::string{"p@ss"});
    OTTER_CHECK(profile->ssl.enabled);
    OTTER_CHECK(profile->ssl.mode == SslMode::require);
    OTTER_CHECK_EQ(profile->driver_properties.at("ApplicationName"), std::string{"x"});
}

OTTER_TEST(url_libpq_form_takes_credentials_before_the_host) {
    // A senha contem '@': vale o ULTIMO, senao o host sairia errado.
    const auto profile = profile_from_url("postgres://ana:se%40nha@10.0.0.5/erp");
    OTTER_CHECK(profile.has_value());
    OTTER_CHECK_EQ(profile->host, std::string{"10.0.0.5"});
    OTTER_CHECK_EQ(profile->port, std::uint16_t{5432});
    OTTER_CHECK_EQ(profile->user, std::string{"ana"});
    OTTER_CHECK_EQ(profile->password, std::string{"se@nha"});
}

OTTER_TEST(url_mysql_uses_the_mysql_driver_and_port) {
    const auto profile = profile_from_url("jdbc:mysql://localhost/loja");
    OTTER_CHECK(profile.has_value());
    OTTER_CHECK_EQ(profile->driver_id, std::string{"mysql"});
    OTTER_CHECK_EQ(profile->port, std::uint16_t{3306});
}

OTTER_TEST(url_refuses_what_it_cannot_connect_to) {
    OTTER_CHECK(!profile_from_url("jdbc:db2://h/db").has_value());
    OTTER_CHECK(!profile_from_url("not a url").has_value());
    OTTER_CHECK(!profile_from_url("postgresql://h:99999/db").has_value());
}

// --- Copia avancada ---------------------------------------------------------------

OTTER_TEST(advanced_copy_default_is_tab_separated) {
    const ResultSet rs = two_by_two();
    const EditBuffer edits;
    CopyOptions options;
    options.null_text = "NULL";
    // A aspa no valor obriga a citar, e ela dobra.
    OTTER_CHECK_EQ(advanced_copy(rs, edits, whole(), options),
                   std::string{"1\t\"Ana \"\"A\"\"\"\n2\tNULL"});
}

OTTER_TEST(advanced_copy_header_row_numbers_and_delimiter) {
    const ResultSet rs = two_by_two();
    const EditBuffer edits;
    CopyOptions options;
    options.column_delimiter = ";";
    options.quote.clear();
    options.copy_header      = true;
    options.copy_row_numbers = true;
    options.first_row_number = 201;   // segunda pagina
    OTTER_CHECK_EQ(advanced_copy(rs, edits, whole(), options),
                   std::string{"#;id;nome\n201;1;Ana \"A\"\n202;2;"});
}

OTTER_TEST(delimiter_escapes_round_trip) {
    OTTER_CHECK_EQ(unescape_delimiter("\\t"), std::string{"\t"});
    OTTER_CHECK_EQ(unescape_delimiter("\\r\\n"), std::string{"\r\n"});
    OTTER_CHECK_EQ(escape_delimiter("\t|\n"), std::string{"\\t|\\n"});
}

// --- UUID ---------------------------------------------------------------------------

OTTER_TEST(uuid_v4_has_version_and_variant_bits) {
    std::array<std::uint8_t, 16> bytes{};
    bytes.fill(0xFF);
    const std::string uuid = format_uuid_v4(bytes);
    OTTER_CHECK_EQ(uuid, std::string{"ffffffff-ffff-4fff-bfff-ffffffffffff"});
    OTTER_CHECK(generate_uuid() != generate_uuid());
    OTTER_CHECK_EQ(generate_uuid().size(), std::size_t{36});
}

// --- Chamada de rotina ---------------------------------------------------------------

OTTER_TEST(routine_call_skips_out_parameters_and_names_the_rest) {
    RoutineMeta routine;
    routine.name      = "fn_total";
    routine.kind      = ObjKind::function;
    routine.arguments = "cliente_id integer, OUT total numeric, desde date DEFAULT now()";
    OTTER_CHECK_EQ(routine_call_sql("otter_test", routine, false),
                   std::string{"SELECT * FROM otter_test.fn_total(:cliente_id, :desde);"});

    routine.kind      = ObjKind::procedure;
    routine.name      = "Fecha Mes";
    routine.arguments = "integer";   // sem nome: marcador numerado
    OTTER_CHECK_EQ(routine_call_sql("otter_test", routine, false),
                   std::string{"CALL otter_test.\"Fecha Mes\"(:p1);"});
}

// --- DDL pelo resultado ----------------------------------------------------------------

OTTER_TEST(create_table_from_result_uses_the_server_types) {
    ResultSetBuilder builder;
    ColumnInfo id;
    id.name = "id"; id.type_name = "int4"; id.nullable = false;
    builder.add_column(std::move(id));
    ColumnInfo valor;
    valor.name = "Valor Total"; valor.type_name = "numeric"; valor.precision = 12; valor.scale = 2;
    builder.add_column(std::move(valor));
    builder.set_row_count(0);
    const ResultSet rs = builder.take();

    OTTER_CHECK_EQ(create_table_from_result(rs, "new_table", false),
                   std::string{"CREATE TABLE new_table (\n"
                               "    id int4 NOT NULL,\n"
                               "    \"Valor Total\" numeric(12,2)\n"
                               ");"});
}

// --- Filtro de objetos ------------------------------------------------------------------

OTTER_TEST(object_filter_include_then_exclude) {
    ObjectFilter filter;
    filter.include = split_masks("cli*, ped%");
    filter.exclude = split_masks("*_tmp");

    OTTER_CHECK(filter_accepts(filter, "cliente"));
    OTTER_CHECK(filter_accepts(filter, "PEDIDO"));          // sem diferenciar caixa
    OTTER_CHECK(!filter_accepts(filter, "documento"));      // fora da inclusao
    OTTER_CHECK(!filter_accepts(filter, "cliente_tmp"));    // excluido

    filter.enabled = false;
    OTTER_CHECK(filter_accepts(filter, "documento"));       // desligado aceita tudo

    OTTER_CHECK(filter_accepts(ObjectFilter{}, "qualquer"));
    OTTER_CHECK_EQ(join_masks(split_masks(" a ;b,\n c ")), std::string{"a, b, c"});
}

// --- Editor -----------------------------------------------------------------------------

OTTER_TEST(join_lines_replaces_the_break_with_one_space) {
    OTTER_CHECK_EQ(join_lines("SELECT a,  ", "    b"), std::string{"SELECT a, b"});
    OTTER_CHECK_EQ(join_lines("", "  x"), std::string{"x"});
}

OTTER_TEST(word_completion_cycles_through_the_matches) {
    const std::string text = "select cliente_id, cliente_nome from cli";
    OTTER_CHECK_EQ(complete_word(text, "cli", "cli"), std::string{"cliente_id"});
    OTTER_CHECK_EQ(complete_word(text, "cli", "cliente_id"), std::string{"cliente_nome"});
    OTTER_CHECK_EQ(complete_word(text, "cli", "cliente_nome"), std::string{"cliente_id"});
    OTTER_CHECK_EQ(complete_word(text, "xyz", "xyz"), std::string{});
}

// --- Dashboard ----------------------------------------------------------------------------

OTTER_TEST(dashboard_delta_is_a_rate_and_never_negative) {
    OTTER_CHECK(dashboard_value(true, 100.0, 150.0, 5.0) == 10.0);
    // O servidor reiniciou e o contador zerou: nao e' uma taxa negativa.
    OTTER_CHECK(dashboard_value(true, 100.0, 3.0, 5.0) == 0.0);
    OTTER_CHECK(dashboard_value(false, 100.0, 42.0, 5.0) == 42.0);
    OTTER_CHECK(!dashboard_catalog("postgresql").empty());
    OTTER_CHECK(!dashboard_catalog("mysql").empty());
    // Um grafico de cada SGBD: o id diz de quem e'. Com a sobrecarga `bool`
    // que existia, "sqlserver" devolvia os do MySQL.
    OTTER_CHECK(std::string_view(dashboard_catalog("postgresql").front().id).starts_with("pg."));
    OTTER_CHECK(std::string_view(dashboard_catalog("mariadb").front().id).starts_with("my."));
    OTTER_CHECK(std::string_view(dashboard_catalog("sqlserver").front().id).starts_with("ms."));
}

// --- Inicializacao da sessao (os campos que nao eram aplicados) ---------------------------

OTTER_TEST(session_setup_runs_role_schema_queries_then_read_only) {
    SessionSetup setup;
    setup.session_role      = "relatorio";
    setup.default_schema    = "Vendas";
    setup.bootstrap_queries = "SET statement_timeout = 5000;\n\n-- comentario\nSET TimeZone = 'UTC'";
    setup.read_only         = true;

    const std::vector<std::string> pg = session_setup_statements("postgresql", setup);
    OTTER_CHECK_EQ(pg.size(), std::size_t{5});
    OTTER_CHECK_EQ(pg[0], std::string{"SET ROLE \"relatorio\""});
    OTTER_CHECK_EQ(pg[1], std::string{"SET search_path TO \"Vendas\", public"});
    OTTER_CHECK_EQ(pg[2], std::string{"SET statement_timeout = 5000"});
    OTTER_CHECK_EQ(pg[3], std::string{"SET TimeZone = 'UTC'"});
    OTTER_CHECK_EQ(pg[4], std::string{"SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY"});

    // MySQL: sem SET ROLE (o campo e' da pagina do PostgreSQL), USE no lugar
    // de search_path.
    const std::vector<std::string> my = session_setup_statements("mysql", setup);
    OTTER_CHECK_EQ(my.front(), std::string{"USE `Vendas`"});
    OTTER_CHECK_EQ(my.back(), std::string{"SET SESSION TRANSACTION READ ONLY"});

    OTTER_CHECK(session_setup_statements("postgresql", SessionSetup{}).empty());
}

OTTER_TEST(profile_carries_the_setup_to_the_connection) {
    ConnectionProfile profile;
    profile.default_schema = "otter_test";
    profile.read_only      = true;
    profile.auto_commit    = false;
    profile.ignore_bootstrap_errors = true;

    const ConnConfig config = profile.to_conn_config();
    OTTER_CHECK_EQ(config.init_statements.size(), std::size_t{2});
    OTTER_CHECK(!config.auto_commit);
    OTTER_CHECK(config.ignore_init_errors);
}

// --- Pastas de conexao ------------------------------------------------------------------

OTTER_TEST(connection_folder_paths_split_and_join) {
    OTTER_CHECK_EQ(folder_parent("Clientes/Producao/Sul"), std::string("Clientes/Producao"));
    OTTER_CHECK_EQ(folder_parent("Clientes"), std::string(""));
    OTTER_CHECK_EQ(folder_leaf("Clientes/Producao"), std::string("Producao"));
    OTTER_CHECK_EQ(folder_leaf("Clientes"), std::string("Clientes"));
    OTTER_CHECK_EQ(folder_join("Clientes", "Producao"), std::string("Clientes/Producao"));
    // Na raiz nao ha' barra na frente.
    OTTER_CHECK_EQ(folder_join("", "Producao"), std::string("Producao"));

    // O que o usuario digita: espacos nas pontas, barra sobrando, parte vazia.
    OTTER_CHECK_EQ(folder_normalize("  Clientes // Producao / "), std::string("Clientes/Producao"));
    OTTER_CHECK_EQ(folder_normalize(" / "), std::string(""));
    OTTER_CHECK_EQ(folder_normalize("Meu banco"), std::string("Meu banco"));
}

OTTER_TEST(connection_folder_contains_only_whole_names) {
    OTTER_CHECK(folder_contains("Clientes", "Clientes"));
    OTTER_CHECK(folder_contains("Clientes", "Clientes/Producao"));
    // "ClientesAntigos" comeca por "Clientes" e NAO esta' dentro dela: sem a
    // barra na comparacao, renomear uma pasta arrastava a vizinha.
    OTTER_CHECK(!folder_contains("Clientes", "ClientesAntigos"));
    OTTER_CHECK(!folder_contains("Clientes/Producao", "Clientes"));
    OTTER_CHECK(folder_contains("", "qualquer"));
}

OTTER_TEST(connection_folder_rebase_renames_moves_and_deletes) {
    // Renomear: a pasta e tudo o que ha' dentro.
    OTTER_CHECK_EQ(folder_rebase("Clientes", "Clientes", "Contas"), std::string("Contas"));
    OTTER_CHECK_EQ(folder_rebase("Clientes/Producao", "Clientes", "Contas"),
                   std::string("Contas/Producao"));
    // Fora dela, nada muda -- nem a vizinha de nome parecido.
    OTTER_CHECK_EQ(folder_rebase("ClientesAntigos", "Clientes", "Contas"),
                   std::string("ClientesAntigos"));
    OTTER_CHECK_EQ(folder_rebase("", "Clientes", "Contas"), std::string(""));

    // Apagar e' trocar pelo pai: o conteudo sobe um nivel.
    OTTER_CHECK_EQ(folder_rebase("Clientes/Producao", "Clientes/Producao", "Clientes"),
                   std::string("Clientes"));
    OTTER_CHECK_EQ(folder_rebase("Clientes/Producao/Sul", "Clientes/Producao", "Clientes"),
                   std::string("Clientes/Sul"));
    // Apagar uma pasta do primeiro nivel: o conteudo vai para a raiz.
    OTTER_CHECK_EQ(folder_rebase("Clientes", "Clientes", ""), std::string(""));
    OTTER_CHECK_EQ(folder_rebase("Clientes/Producao", "Clientes", ""), std::string("Producao"));

    // Sem pasta de origem nao ha' o que trocar.
    OTTER_CHECK_EQ(folder_rebase("Clientes", "", "Contas"), std::string("Clientes"));
}
