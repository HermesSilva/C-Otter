// Reescrita de consulta para paginacao (ADR 0011).
//
// A parte que exige teste nao e' acrescentar "LIMIT 200" -- e' saber QUANDO
// nao acrescentar. Reescrever a consulta errada faz a grade mostrar algo
// diferente do que o usuario escreveu, que e' pior que a espera que a
// paginacao veio resolver.
#include "test_main.hpp"

#include "sql/paging.hpp"

#include <string>

using namespace otter::sql;

namespace {

PagedQuery page_of(std::string_view sql, std::size_t page = 0,
                   std::size_t size = 200) {
    return make_paged_query(sql, postgres_dialect(), page, size);
}

bool contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

OTTER_TEST(paging_rewrites_a_plain_select) {
    const PagedQuery q = page_of("SELECT * FROM cliente");

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "SELECT * FROM cliente"));

    // 201, nao 200: a linha extra responde "ha' proxima pagina?" sem um
    // COUNT(*), que custaria outra varredura completa.
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));
    OTTER_CHECK_EQ(q.requested, std::size_t{201});

    // Primeira pagina nao leva OFFSET: "OFFSET 0" e' ruido no inspetor.
    OTTER_CHECK(!contains(q.sql, "OFFSET"));
}

OTTER_TEST(paging_adds_offset_from_second_page_on) {
    const PagedQuery q = page_of("SELECT * FROM cliente", /*page=*/2, 50);

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "LIMIT 51"));
    OTTER_CHECK(contains(q.sql, "OFFSET 100"));
}

OTTER_TEST(paging_refuses_when_user_wrote_a_limit) {
    // O usuario decidiu quantas linhas quer. Sobrepor seria executar algo
    // diferente do que esta' na tela.
    const PagedQuery q = page_of("SELECT * FROM cliente LIMIT 10");

    OTTER_CHECK(!q.rewritten);
    OTTER_CHECK(q.refusal == PagingRefusal::already_limited);
    OTTER_CHECK_EQ(q.sql, std::string{"SELECT * FROM cliente LIMIT 10"});
}

OTTER_TEST(paging_refuses_fetch_first_and_bare_offset) {
    OTTER_CHECK(page_of("SELECT * FROM t FETCH FIRST 5 ROWS ONLY").refusal ==
                PagingRefusal::already_limited);

    // OFFSET sozinho tambem indica que o usuario esta' paginando por conta.
    OTTER_CHECK(page_of("SELECT * FROM t OFFSET 20").refusal ==
                PagingRefusal::already_limited);
}

OTTER_TEST(paging_ignores_limit_inside_a_subquery) {
    // O LIMIT pertence a subconsulta; a consulta externa continua sem limite.
    // Recusar por causa dele deixaria a UI travando exatamente no caso que a
    // paginacao veio resolver.
    const PagedQuery q =
        page_of("SELECT * FROM (SELECT * FROM evento LIMIT 10) s");

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));
}

OTTER_TEST(paging_ignores_the_word_limit_inside_a_string) {
    // O lexer distingue palavra-chave de conteudo de string. Uma busca textual
    // por "limit" acharia este e recusaria a consulta.
    const PagedQuery q =
        page_of("SELECT * FROM log WHERE msg = 'rate limit exceeded'");

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));
}

OTTER_TEST(paging_refuses_statements_that_produce_no_rows) {
    OTTER_CHECK(page_of("UPDATE cliente SET nome = 'x'").refusal ==
                PagingRefusal::not_a_query);
    OTTER_CHECK(page_of("DELETE FROM cliente").refusal ==
                PagingRefusal::not_a_query);
    OTTER_CHECK(page_of("INSERT INTO cliente (nome) VALUES ('x')").refusal ==
                PagingRefusal::not_a_query);
    OTTER_CHECK(page_of("CREATE TABLE t (id int)").refusal ==
                PagingRefusal::not_a_query);
}

OTTER_TEST(paging_refuses_forms_where_it_adds_nothing) {
    OTTER_CHECK(page_of("EXPLAIN SELECT * FROM t").refusal ==
                PagingRefusal::unsupported_form);
    OTTER_CHECK(page_of("SHOW search_path").refusal ==
                PagingRefusal::unsupported_form);
}

OTTER_TEST(paging_refuses_more_than_one_command) {
    // Sem separador de script ligado, reescrever aqui poria o LIMIT no comando
    // errado.
    OTTER_CHECK(page_of("SELECT 1; SELECT 2").refusal ==
                PagingRefusal::multiple_commands);

    // Mas um ';' terminador no fim e' normal e nao impede a reescrita.
    const PagedQuery terminated = page_of("SELECT * FROM cliente;");
    OTTER_CHECK(terminated.rewritten);
    OTTER_CHECK(contains(terminated.sql, "LIMIT 201"));

    // E o LIMIT vai DEPOIS do texto, nao depois do ';' -- senao a sintaxe
    // quebraria.
    OTTER_CHECK(!contains(terminated.sql, "; \nLIMIT"));
}

OTTER_TEST(paging_handles_with_clause) {
    const PagedQuery q = page_of(
        "WITH recentes AS (SELECT * FROM pedido) SELECT * FROM recentes");

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));
}

OTTER_TEST(paging_puts_limit_outside_a_trailing_comment) {
    // Acrescentar o LIMIT apos um comentario de linha o colocaria DENTRO do
    // comentario, e a consulta rodaria sem limite nenhum -- travando a UI de
    // novo, silenciosamente.
    const PagedQuery q = page_of("SELECT * FROM cliente -- todos eles");

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(!contains(q.sql, "todos eles"));
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));

    // A parte util da consulta precisa sobreviver.
    OTTER_CHECK(contains(q.sql, "SELECT * FROM cliente"));
}

OTTER_TEST(paging_refuses_empty_input) {
    OTTER_CHECK(page_of("").refusal == PagingRefusal::empty);
    OTTER_CHECK(page_of("   \n  -- so um comentario\n").refusal ==
                PagingRefusal::empty);
}

// --- Ordenacao pelo cabecalho -----------------------------------------------

OTTER_TEST(paging_adds_order_by_before_the_limit) {
    // A ordem importa duas vezes: a gramatica exige ORDER antes de LIMIT, e o
    // sentido tambem -- limitar primeiro daria as 200 primeiras linhas na
    // ordem do banco, depois reordenadas entre si.
    const PagedQuery q = make_paged_query("SELECT * FROM cliente",
                                          postgres_dialect(), 0, 200,
                                          SortOrder{"nome", false});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "ORDER BY \"nome\" ASC"));

    const std::size_t order_pos = q.sql.find("ORDER BY");
    const std::size_t limit_pos = q.sql.find("LIMIT");
    OTTER_CHECK(order_pos < limit_pos);
}

OTTER_TEST(paging_sorts_descending_when_asked) {
    const PagedQuery q = make_paged_query("SELECT * FROM cliente",
                                          postgres_dialect(), 0, 200,
                                          SortOrder{"credito", true});
    OTTER_CHECK(contains(q.sql, "ORDER BY \"credito\" DESC"));
}

OTTER_TEST(paging_quotes_the_sort_column) {
    // Coluna chamada "order" ou com maiuscula quebraria a consulta sem aspas.
    const PagedQuery reserved = make_paged_query(
        "SELECT * FROM t", postgres_dialect(), 0, 200, SortOrder{"order"});
    OTTER_CHECK(contains(reserved.sql, "ORDER BY \"order\" ASC"));

    const PagedQuery mixed = make_paged_query(
        "SELECT * FROM t", postgres_dialect(), 0, 200,
        SortOrder{"TIDxAcaoSensivelID"});
    OTTER_CHECK(contains(mixed.sql, "\"TIDxAcaoSensivelID\""));
}

OTTER_TEST(paging_keeps_the_users_own_order_by) {
    // Dois ORDER BY na mesma consulta sao erro de sintaxe, e sobrepor o do
    // usuario executaria algo diferente do que esta' na tela.
    const PagedQuery q = make_paged_query(
        "SELECT * FROM cliente ORDER BY criado_em DESC",
        postgres_dialect(), 0, 200, SortOrder{"nome"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "ORDER BY criado_em DESC"));
    OTTER_CHECK(!contains(q.sql, "\"nome\""));

    // E continua paginando: so' a ordenacao da grade foi ignorada.
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));
}

OTTER_TEST(paging_ignores_order_by_inside_a_subquery) {
    // O ORDER BY e' da subconsulta; a externa continua sem ordem, e a
    // ordenacao da grade e' legitima.
    const PagedQuery q = make_paged_query(
        "SELECT * FROM (SELECT * FROM t ORDER BY id) s",
        postgres_dialect(), 0, 200, SortOrder{"nome"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "ORDER BY \"nome\" ASC"));
}

OTTER_TEST(paging_without_sort_produces_no_order_by) {
    const PagedQuery q = page_of("SELECT * FROM cliente");
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(!contains(q.sql, "ORDER BY"));
}

// --- Filtro por coluna -------------------------------------------------------

OTTER_TEST(paging_wraps_the_query_to_apply_a_filter) {
    // Anexar "WHERE ..." ao fim seria errado de tres jeitos: a consulta pode
    // ja' ter WHERE, pode terminar em GROUP BY (WHERE depois e' invalido), e
    // filtraria ANTES da agregacao. Envolver e' correto em todos os casos.
    const PagedQuery q = make_paged_query(
        "SELECT categoria, count(*) AS total FROM evento GROUP BY categoria",
        postgres_dialect(), 0, 200, {}, ColumnFilter{"total", "> 100"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "SELECT * FROM ("));
    OTTER_CHECK(contains(q.sql, ") AS otter_filter"));
    OTTER_CHECK(contains(q.sql, "WHERE \"total\" > 100"));
    OTTER_CHECK(contains(q.sql, "LIMIT 201"));

    // O WHERE vem ANTES do LIMIT, e a consulta original fica intacta dentro.
    OTTER_CHECK(q.sql.find("WHERE") < q.sql.find("LIMIT"));
    OTTER_CHECK(contains(q.sql, "GROUP BY categoria"));
}

OTTER_TEST(paging_filter_survives_an_existing_where) {
    // Dois WHERE na mesma consulta nao compilam; envolver resolve.
    const PagedQuery q = make_paged_query(
        "SELECT * FROM cliente WHERE ativo",
        postgres_dialect(), 0, 200, {}, ColumnFilter{"credito", ">= 1000"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "WHERE ativo"));           // o do usuario
    OTTER_CHECK(contains(q.sql, "WHERE \"credito\" >= 1000"));  // o da grade
}

OTTER_TEST(paging_filter_quotes_the_column_name) {
    const PagedQuery q = make_paged_query(
        "SELECT * FROM t", postgres_dialect(), 0, 200, {},
        ColumnFilter{"order", "IS NULL"});
    OTTER_CHECK(contains(q.sql, "WHERE \"order\" IS NULL"));
}

OTTER_TEST(paging_filter_and_sort_work_together) {
    const PagedQuery q = make_paged_query(
        "SELECT * FROM cliente", postgres_dialect(), 1, 50,
        SortOrder{"nome", true}, ColumnFilter{"credito", "> 0"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "WHERE \"credito\" > 0"));
    OTTER_CHECK(contains(q.sql, "ORDER BY \"nome\" DESC"));
    OTTER_CHECK(contains(q.sql, "LIMIT 51"));
    OTTER_CHECK(contains(q.sql, "OFFSET 50"));

    // Ordem obrigatoria: WHERE (dentro do wrap), ORDER BY, LIMIT, OFFSET.
    OTTER_CHECK(q.sql.find("WHERE") < q.sql.find("ORDER BY"));
    OTTER_CHECK(q.sql.find("ORDER BY") < q.sql.find("LIMIT"));
}

OTTER_TEST(paging_filter_frees_the_grid_to_sort) {
    // Com filtro, o ORDER BY do usuario passa para DENTRO da subconsulta: a
    // externa fica sem ordem, e a da grade deixa de ser conflito.
    const PagedQuery q = make_paged_query(
        "SELECT * FROM cliente ORDER BY criado_em",
        postgres_dialect(), 0, 200, SortOrder{"nome"},
        ColumnFilter{"credito", "> 0"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "ORDER BY criado_em"));      // dentro
    OTTER_CHECK(contains(q.sql, "ORDER BY \"nome\" ASC"));   // fora
}

OTTER_TEST(paging_ignores_an_incomplete_filter) {
    // Coluna sem expressao, ou expressao sem coluna, nao produz WHERE -- um
    // "WHERE coluna" solto seria erro de sintaxe.
    OTTER_CHECK(!contains(
        make_paged_query("SELECT * FROM t", postgres_dialect(), 0, 200, {},
                         ColumnFilter{"col", ""}).sql, "WHERE"));
    OTTER_CHECK(!contains(
        make_paged_query("SELECT * FROM t", postgres_dialect(), 0, 200, {},
                         ColumnFilter{"", "> 5"}).sql, "WHERE"));
}

// --- Contagem sob demanda (resultset.count) ---------------------------------
//
// Por que existe: o COUNT vai para o servidor como SQL gerado, e um numero
// errado e' pior que a ausencia dele -- o usuario decide se pagina ou refina
// a consulta com base nele.

OTTER_TEST(count_wraps_the_query_in_a_subquery) {
    // Envolver, e nao anexar: a consulta pode ter WHERE, GROUP BY ou HAVING,
    // e contar antes da agregacao daria um numero diferente do que a grade
    // mostra.
    const PagedQuery q = make_count_query("SELECT * FROM cliente",
                                          postgres_dialect());

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "COUNT(*)"));
    OTTER_CHECK(contains(q.sql, "SELECT * FROM cliente"));
}

OTTER_TEST(count_survives_group_by) {
    // Com GROUP BY, o total e' o numero de GRUPOS -- que e' o que a grade
    // exibe. Anexar WHERE aqui seria sintaxe invalida.
    const PagedQuery q = make_count_query(
        "SELECT pais, COUNT(*) FROM cliente GROUP BY pais", postgres_dialect());

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "GROUP BY pais"));
}

OTTER_TEST(count_applies_the_grid_filter) {
    // Contar sem o filtro daria o total da tabela enquanto a tela mostra o
    // subconjunto -- dois numeros discordando na mesma janela.
    const PagedQuery q = make_count_query("SELECT * FROM cliente",
                                          postgres_dialect(),
                                          ColumnFilter{"credito", "> 0"});

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "\"credito\" > 0"));
}

OTTER_TEST(count_accepts_a_query_the_user_limited) {
    // already_limited impede PAGINAR, nao CONTAR: "quantas linhas ha'"
    // continua valida, e a resposta e' o tamanho do resultado exibido.
    const PagedQuery q = make_count_query("SELECT * FROM cliente LIMIT 50",
                                          postgres_dialect());

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(contains(q.sql, "LIMIT 50"));
}

OTTER_TEST(count_refuses_what_paging_refuses) {
    // Mesmas recusas: o que nao produz linhas nao tem o que contar.
    OTTER_CHECK(!make_count_query("UPDATE t SET a = 1",
                                  postgres_dialect()).rewritten);
    OTTER_CHECK(!make_count_query("SELECT 1; SELECT 2",
                                  postgres_dialect()).rewritten);
    OTTER_CHECK(!make_count_query("", postgres_dialect()).rewritten);
}

OTTER_TEST(count_drops_the_trailing_semicolon) {
    // Envolver "SELECT ... ;" numa subconsulta produz "... ;\n) AS
    // otter_count", que o PostgreSQL recusa com 42601 -- "erro de sintaxe em
    // ou proximo a ';'".
    //
    // Encontrado na tela: o paged_sql guarda a consulta como o usuario a
    // escreveu, e o texto de boas-vindas termina com ';'.
    const PagedQuery q = make_count_query("SELECT * FROM cliente;",
                                          postgres_dialect());

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(!contains(q.sql, ";\n)"));
    OTTER_CHECK(contains(q.sql, "cliente\n)"));
}

OTTER_TEST(count_drops_a_trailing_comment) {
    // Mesma razao: o ") AS otter_count" cairia DENTRO do comentario de linha,
    // e a subconsulta ficaria sem fechar.
    const PagedQuery q = make_count_query(
        "SELECT * FROM cliente -- ativos\n", postgres_dialect());

    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(!contains(q.sql, "-- ativos"));
}
