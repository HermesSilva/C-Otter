// Geracao de SQL a partir dos metadados.
//
// O que exige teste aqui nao e' montar a string -- e' o que acontece nos
// casos em que montar a string ingenuamente produz SQL perigoso ou invalido:
// tabela sem chave primaria, nome que colide com palavra reservada, coluna
// com DEFAULT.
#include "test_main.hpp"

#include "db/ddl.hpp"

#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

ColumnMeta column(std::string name, std::string type, bool pk = false,
                  bool nullable = true, std::string default_value = {}) {
    ColumnMeta c;
    c.name          = std::move(name);
    c.type_name     = std::move(type);
    c.primary_key   = pk;
    c.nullable      = nullable;
    c.default_value = std::move(default_value);
    return c;
}

// Tabela tipica: PK serial, colunas obrigatorias e opcionais.
TableMeta sample_table() {
    TableMeta t;
    t.name = "cliente";
    t.columns = {
        column("cliente_id", "integer", /*pk=*/true, /*nullable=*/false,
               "nextval('cliente_cliente_id_seq'::regclass)"),
        column("nome",  "character varying(120)", false, false),
        column("email", "character varying(200)"),
    };
    t.columns_loaded = true;
    return t;
}

} // namespace

// --- Citacao de identificadores ---------------------------------------------

OTTER_TEST(ddl_leaves_simple_names_unquoted) {
    // Aspas em tudo funcionaria, mas o SQL gerado e' para o usuario ler e
    // editar: "SELECT "id" FROM "cliente"" e' ruido em 99% dos casos.
    OTTER_CHECK_EQ(quote_if_needed("cliente"), std::string{"cliente"});
    OTTER_CHECK_EQ(quote_if_needed("cliente_id"), std::string{"cliente_id"});
    OTTER_CHECK_EQ(quote_if_needed("_privado"), std::string{"_privado"});
}

OTTER_TEST(ddl_quotes_names_that_need_it) {
    // Maiuscula: sem aspas o PostgreSQL rebaixa para minusculas e a tabela
    // deixa de ser encontrada. O ERP_TID usa esse estilo em tudo.
    OTTER_CHECK_EQ(quote_if_needed("TIDxAcaoSensivel"),
                   std::string{"\"TIDxAcaoSensivel\""});

    OTTER_CHECK_EQ(quote_if_needed("nome com espaco"),
                   std::string{"\"nome com espaco\""});

    // Palavra reservada como nome de coluna acontece mais do que deveria.
    OTTER_CHECK_EQ(quote_if_needed("order"), std::string{"\"order\""});
    OTTER_CHECK_EQ(quote_if_needed("user"), std::string{"\"user\""});
    OTTER_CHECK_EQ(quote_if_needed("Select"), std::string{"\"Select\""});

    // Aspas dentro do nome dobram.
    OTTER_CHECK_EQ(quote_if_needed("a\"b"), std::string{"\"a\"\"b\""});
}

OTTER_TEST(ddl_qualifies_with_the_schema) {
    OTTER_CHECK_EQ(qualified_name("public", "cliente"),
                   std::string{"public.cliente"});
    OTTER_CHECK_EQ(qualified_name("otter_test", "TIDx"),
                   std::string{"otter_test.\"TIDx\""});
}

// --- SELECT ------------------------------------------------------------------

OTTER_TEST(ddl_select_lists_columns_instead_of_star) {
    // Listar as colunas e' melhor ponto de partida: o usuario apaga as que
    // nao quer, em vez de digitar as que quer.
    const std::string sql = generate_select("public", sample_table());

    OTTER_CHECK(has(sql, "cliente_id"));
    OTTER_CHECK(has(sql, "nome"));
    OTTER_CHECK(has(sql, "email"));
    OTTER_CHECK(has(sql, "FROM public.cliente"));
    OTTER_CHECK(has(sql, "LIMIT 200"));
    OTTER_CHECK(!has(sql, "SELECT *"));
}

OTTER_TEST(ddl_select_falls_back_to_star_when_columns_are_unknown) {
    TableMeta bare;
    bare.name = "ainda_nao_expandida";

    const std::string sql = generate_select("public", bare);
    OTTER_CHECK(has(sql, "SELECT *"));
    // E diz por que, em vez de deixar o usuario supor que a tabela nao tem
    // colunas.
    OTTER_CHECK(has(sql, "expand"));
}

// --- INSERT ------------------------------------------------------------------

OTTER_TEST(ddl_insert_comments_out_columns_with_a_default) {
    const std::string sql = generate_insert("public", sample_table());

    // cliente_id tem DEFAULT nextval(...): preencher a mao e' quase sempre
    // engano, mas remover a linha impediria o caso legitimo.
    OTTER_CHECK(has(sql, "-- cliente_id"));
    OTTER_CHECK(has(sql, "DEFAULT"));

    // As demais entram normalmente.
    OTTER_CHECK(has(sql, "nome"));
    OTTER_CHECK(has(sql, "email"));
    OTTER_CHECK(has(sql, "INSERT INTO public.cliente"));
}

// --- UPDATE e DELETE ---------------------------------------------------------

OTTER_TEST(ddl_update_uses_the_primary_key_in_the_where) {
    const std::string sql = generate_update("public", sample_table());

    OTTER_CHECK(has(sql, "UPDATE public.cliente"));
    OTTER_CHECK(has(sql, "WHERE cliente_id ="));

    // A PK nao entra no SET: alterar a chave por engano e' um jeito comum de
    // quebrar integridade referencial.
    const std::size_t set_pos   = sql.find("SET");
    const std::size_t where_pos = sql.find("WHERE");
    const std::size_t pk_in_set = sql.find("cliente_id", set_pos);
    OTTER_CHECK(pk_in_set > where_pos);
}

OTTER_TEST(ddl_update_without_a_primary_key_warns_and_comments_the_where) {
    // O caso perigoso. Um UPDATE sem WHERE executado por reflexo altera a
    // tabela inteira.
    TableMeta no_key;
    no_key.name = "log";
    no_key.columns = {column("mensagem", "text"), column("nivel", "integer")};
    no_key.columns_loaded = true;

    const std::string sql = generate_update("public", no_key);

    OTTER_CHECK(has(sql, "WARNING"));
    OTTER_CHECK(has(sql, "EVERY row"));

    // O WHERE precisa estar COMENTADO: um WHERE ausente rodaria.
    OTTER_CHECK(has(sql, "-- WHERE"));

    // E o aviso vem antes do comando, onde se le' primeiro.
    OTTER_CHECK(sql.find("WARNING") < sql.find("UPDATE"));
}

OTTER_TEST(ddl_delete_without_a_primary_key_warns_too) {
    TableMeta no_key;
    no_key.name = "log";
    no_key.columns = {column("mensagem", "text")};
    no_key.columns_loaded = true;

    const std::string sql = generate_delete("public", no_key);
    OTTER_CHECK(has(sql, "WARNING"));
    OTTER_CHECK(has(sql, "-- WHERE"));
    OTTER_CHECK(sql.find("WARNING") < sql.find("DELETE"));
}

OTTER_TEST(ddl_handles_a_composite_primary_key) {
    TableMeta composite;
    composite.name = "pedido_item";
    composite.columns = {
        column("pedido_id", "integer", true, false),
        column("item_id",   "integer", true, false),
        column("quantidade", "integer", false, false),
    };
    composite.columns_loaded = true;

    const std::string sql = generate_delete("public", composite);

    // As duas colunas da PK, unidas por AND. Usar so' a primeira apagaria
    // linhas demais.
    OTTER_CHECK(has(sql, "pedido_id ="));
    OTTER_CHECK(has(sql, "AND"));
    OTTER_CHECK(has(sql, "item_id ="));
}

// --- DDL ---------------------------------------------------------------------

OTTER_TEST(ddl_create_table_includes_types_nullability_and_defaults) {
    TableMeta t = sample_table();
    t.comment = "Cadastro de clientes";

    ConstraintMeta pk;
    pk.name       = "cliente_pkey";
    pk.kind       = ObjKind::primary_key;
    pk.definition = "PRIMARY KEY (cliente_id)";
    t.constraints = {pk};
    t.constraints_loaded = true;

    const std::string sql = generate_ddl("public", t);

    OTTER_CHECK(has(sql, "CREATE TABLE public.cliente"));
    OTTER_CHECK(has(sql, "character varying(120)"));
    OTTER_CHECK(has(sql, "NOT NULL"));
    OTTER_CHECK(has(sql, "DEFAULT nextval"));

    // A constraint vai com a definicao do servidor: reescreve-la a partir das
    // colunas perderia EXCLUDE, CHECK e expressoes.
    OTTER_CHECK(has(sql, "CONSTRAINT cliente_pkey PRIMARY KEY (cliente_id)"));
    OTTER_CHECK(has(sql, "COMMENT ON TABLE"));
}

OTTER_TEST(ddl_create_table_skips_the_primary_key_index) {
    TableMeta t = sample_table();

    IndexMeta pk_index;
    pk_index.name       = "cliente_pkey";
    pk_index.primary    = true;
    pk_index.definition = "CREATE UNIQUE INDEX cliente_pkey ON public.cliente "
                          "USING btree (cliente_id)";

    IndexMeta other;
    other.name       = "ix_cliente_email";
    other.definition = "CREATE INDEX ix_cliente_email ON public.cliente "
                       "USING btree (email)";

    t.indexes = {pk_index, other};
    t.indexes_loaded = true;

    const std::string sql = generate_ddl("public", t);

    // O indice da PK e' criado junto com a constraint; inclui-lo produziria
    // erro de duplicata ao executar o script.
    OTTER_CHECK(!has(sql, "CREATE UNIQUE INDEX cliente_pkey"));
    OTTER_CHECK(has(sql, "ix_cliente_email"));
}

OTTER_TEST(ddl_of_a_view_is_its_definition) {
    TableMeta view;
    view.name = "vw_cliente_ativo";
    view.kind = ObjKind::view;
    view.columns = {column("cliente_id", "integer")};
    view.columns_loaded = true;
    view.definition = " SELECT cliente_id\n   FROM cliente\n  WHERE ativo;";

    const std::string sql = generate_ddl("public", view);

    OTTER_CHECK(has(sql, "CREATE OR REPLACE VIEW public.vw_cliente_ativo AS"));
    OTTER_CHECK(has(sql, "FROM cliente"));
    OTTER_CHECK(!has(sql, "CREATE TABLE"));
}

OTTER_TEST(ddl_of_a_materialized_view_says_materialized) {
    TableMeta mview;
    mview.name = "mvw_total";
    mview.kind = ObjKind::materialized_view;
    mview.columns = {column("total", "numeric")};
    mview.columns_loaded = true;
    mview.definition = " SELECT sum(x) AS total FROM y;";

    const std::string sql = generate_ddl("public", mview);
    OTTER_CHECK(has(sql, "CREATE OR REPLACE MATERIALIZED VIEW"));
}

OTTER_TEST(ddl_escapes_a_quote_inside_the_comment) {
    TableMeta t = sample_table();
    t.comment = "cliente do 'varejo'";

    const std::string sql = generate_ddl("public", t);
    // Aspas simples dobram, senao o COMMENT gerado nao compila.
    OTTER_CHECK(has(sql, "''varejo''"));
}

OTTER_TEST(ddl_count_is_trivial_but_qualified) {
    const std::string sql = generate_count("otter_test", sample_table());
    OTTER_CHECK_EQ(sql,
                   std::string{"SELECT count(*) FROM otter_test.cliente;\n"});
}

// --- Dialeto: delimitador de identificador ------------------------------------

OTTER_TEST(ddl_quotes_identifiers_with_the_dialect_of_the_connection) {
    // Aspas duplas num MySQL sao STRING, nao identificador: o UPDATE gerado
    // compararia a coluna com um texto literal em vez de referencia-la -- e
    // um WHERE "id" = 1 nunca casa com linha nenhuma.
    struct Restore {
        QuoteStyle previous = sql_dialect();
        ~Restore() { set_sql_dialect(previous); }
    } restore;

    set_sql_dialect(QuoteStyle::double_quotes);
    OTTER_CHECK_EQ(quote_if_needed("Cliente"), std::string{"\"Cliente\""});
    OTTER_CHECK_EQ(quote_if_needed("select"),  std::string{"\"select\""});

    set_sql_dialect(QuoteStyle::backticks);
    OTTER_CHECK_EQ(quote_if_needed("Cliente"), std::string{"`Cliente`"});
    OTTER_CHECK_EQ(quote_if_needed("select"),  std::string{"`select`"});

    set_sql_dialect(QuoteStyle::brackets);
    OTTER_CHECK_EQ(quote_if_needed("Cliente"), std::string{"[Cliente]"});

    // Nome simples nao ganha delimitador em dialeto nenhum: "SELECT `id` FROM
    // `cliente`" e' ruido visual em 99% dos casos, e o SQL gerado e' para o
    // usuario ler.
    set_sql_dialect(QuoteStyle::backticks);
    OTTER_CHECK_EQ(quote_if_needed("cliente_id"), std::string{"cliente_id"});
}

OTTER_TEST(ddl_doubles_the_closing_delimiter_inside_a_name) {
    struct Restore {
        QuoteStyle previous = sql_dialect();
        ~Restore() { set_sql_dialect(previous); }
    } restore;

    // Um nome hostil nao pode fechar o delimitador e emendar SQL.
    set_sql_dialect(QuoteStyle::backticks);
    OTTER_CHECK_EQ(quote_if_needed("a`b"), std::string{"`a``b`"});

    set_sql_dialect(QuoteStyle::double_quotes);
    OTTER_CHECK_EQ(quote_if_needed("a\"b"), std::string{"\"a\"\"b\""});
}

OTTER_TEST(ddl_picks_the_dialect_from_the_driver_id) {
    struct Restore {
        QuoteStyle previous = sql_dialect();
        ~Restore() { set_sql_dialect(previous); }
    } restore;

    set_sql_dialect_for("mysql");
    OTTER_CHECK(sql_dialect() == QuoteStyle::backticks);

    // MariaDB fala o mesmo SQL do MySQL.
    set_sql_dialect_for("mariadb");
    OTTER_CHECK(sql_dialect() == QuoteStyle::backticks);

    set_sql_dialect_for("postgresql");
    OTTER_CHECK(sql_dialect() == QuoteStyle::double_quotes);

    // Driver desconhecido cai no padrao SQL, que e' o menos surpreendente.
    set_sql_dialect_for("algo-novo");
    OTTER_CHECK(sql_dialect() == QuoteStyle::double_quotes);
}
