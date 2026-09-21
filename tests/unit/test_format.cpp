// Reindentacao de SQL.
//
// O criterio destes testes nao e' "o resultado e' bonito" -- e' que a
// consulta continua significando a mesma coisa. Um formatador que muda o
// sentido do SQL e' pior que nenhum, e os casos perigosos sao string,
// comentario e identificador citado.
#include "test_main.hpp"

#include "sql/format.hpp"

#include <string>

using namespace otter::sql;

namespace {

std::string fmt(std::string_view sql, FormatOptions options = {}) {
    return format_sql(sql, postgres_dialect(), options);
}

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::size_t count_lines(const std::string& text) {
    if (text.empty()) return 0;
    std::size_t lines = 1;
    for (const char c : text) {
        if (c == '\n') ++lines;
    }
    return lines;
}

} // namespace

// --- O que nao pode mudar ----------------------------------------------------

OTTER_TEST(format_never_touches_string_contents) {
    // Uma string com "select" dentro nao e' palavra-chave, e maiusculizar seu
    // conteudo mudaria o dado consultado.
    const std::string out = fmt("select * from log where msg = 'select from'");

    OTTER_CHECK(has(out, "'select from'"));
    OTTER_CHECK(!has(out, "'SELECT FROM'"));
}

OTTER_TEST(format_preserves_quoted_identifiers) {
    // "Nome" com maiuscula e' um identificador diferente de "nome". Alterar o
    // caso quebraria a consulta contra uma tabela do estilo do ERP_TID.
    const std::string out = fmt("select \"TIDxAcaoSensivel\" from t");

    OTTER_CHECK(has(out, "\"TIDxAcaoSensivel\""));
    OTTER_CHECK(!has(out, "\"TIDXACAOSENSIVEL\""));
}

OTTER_TEST(format_keeps_comments) {
    const std::string out = fmt("select 1 -- o numero um\nfrom t");

    OTTER_CHECK(has(out, "-- o numero um"));
    // O que vem depois precisa estar em OUTRA linha: comentario de linha
    // seguido de codigo na mesma linha comentaria o codigo.
    const std::size_t comment = out.find("--");
    const std::size_t from    = out.find("FROM");
    OTTER_CHECK(comment < from);
    OTTER_CHECK(has(out.substr(comment, from - comment), "\n"));
}

OTTER_TEST(format_returns_malformed_input_untouched) {
    // String nao fechada: reindentar algo que nao foi entendido produz texto
    // pior que o original.
    const std::string_view broken = "select * from t where x = 'nao fecha";
    OTTER_CHECK_EQ(fmt(broken), std::string(broken));
}

// --- O que deve acontecer ----------------------------------------------------

OTTER_TEST(format_breaks_clauses_onto_their_own_lines) {
    const std::string out =
        fmt("select a, b from cliente where credito > 0 order by nome");

    // Estilo "rio": as cláusulas alinham à DIREITA sob SELECT, então a
    // indentação de cada uma é 6 menos o tamanho da palavra.
    //
    //     SELECT a, b
    //       FROM cliente      (FROM tem 4 -> 2 espaços)
    //      WHERE credito > 0  (WHERE tem 5 -> 1 espaço)
    //      ORDER BY nome      (ORDER tem 5 -> 1 espaço)
    OTTER_CHECK(has(out, "SELECT"));
    OTTER_CHECK(has(out, "\n  FROM"));
    OTTER_CHECK(has(out, "\n WHERE"));
    OTTER_CHECK(has(out, "\n ORDER BY"));

    // Uma linha por clausula, nao tudo junto.
    OTTER_CHECK(count_lines(out) >= 4);
}

OTTER_TEST(format_uppercases_keywords_by_default) {
    const std::string out = fmt("select a from t");
    OTTER_CHECK(has(out, "SELECT"));
    OTTER_CHECK(has(out, "FROM"));
    OTTER_CHECK(!has(out, "select"));
}

OTTER_TEST(format_can_lowercase_or_preserve) {
    FormatOptions lower;
    lower.keyword_case = KeywordCase::lower;
    OTTER_CHECK(has(fmt("SELECT a FROM t", lower), "select"));

    FormatOptions keep;
    keep.keyword_case = KeywordCase::preserve;
    OTTER_CHECK(has(fmt("SeLeCt a FROM t", keep), "SeLeCt"));
}

OTTER_TEST(format_does_not_break_group_by_between_the_words) {
    // "GROUP" quebra a linha; "BY" nao -- senao sairia "GROUP\nBY".
    const std::string out = fmt("select a, count(*) from t group by a");
    OTTER_CHECK(has(out, "GROUP BY"));
    OTTER_CHECK(!has(out, "GROUP\nBY"));
}

OTTER_TEST(format_keeps_a_short_select_list_on_one_line) {
    // Lista curta cabe numa linha e fica melhor assim; quebrar tres colunas
    // em tres linhas e' ruido.
    const std::string out = fmt("select a, b, c from t");

    const std::size_t select = out.find("SELECT");
    const std::size_t from   = out.find("FROM");
    const std::string head   = out.substr(select, from - select);

    OTTER_CHECK(has(head, "a, b, c"));
}

OTTER_TEST(format_wraps_a_long_select_list) {
    const std::string out =
        fmt("select a, b, c, d, e, f from t");

    const std::size_t select = out.find("SELECT");
    const std::size_t from   = out.find("FROM");
    const std::string head   = out.substr(select, from - select);

    // Seis colunas passam do limite: uma por linha.
    OTTER_CHECK(count_lines(head) >= 6);
}

OTTER_TEST(format_does_not_separate_a_function_from_its_parenthesis) {
    // "count (*)" e' valido mas feio; e "cast (x as int)" e' ainda pior.
    const std::string out = fmt("select count(*), max(valor) from t");
    OTTER_CHECK(has(out, "count(*)"));
    OTTER_CHECK(has(out, "max(valor)"));
}

OTTER_TEST(format_keeps_qualified_names_together) {
    const std::string out = fmt("select t.a from otter_test.cliente t");
    OTTER_CHECK(has(out, "t.a"));
    OTTER_CHECK(has(out, "otter_test.cliente"));
    OTTER_CHECK(!has(out, "otter_test . cliente"));
}

OTTER_TEST(format_puts_each_statement_on_its_own_line) {
    const std::string out = fmt("select 1; select 2;");

    OTTER_CHECK(has(out, "SELECT 1;"));
    OTTER_CHECK(has(out, "SELECT 2;"));
    // O ';' cola no token anterior.
    OTTER_CHECK(!has(out, "1 ;"));
}

OTTER_TEST(format_keeps_a_join_clause_on_one_line) {
    // "LEFT JOIN" é uma cláusula só. Quebrar entre as duas palavras foi um
    // defeito real, visto na tela antes de virar teste.
    const std::string out = fmt(
        "select * from cliente c left join pedido p on p.cliente_id = c.id");

    OTTER_CHECK(has(out, "LEFT JOIN"));
    OTTER_CHECK(!has(out, "LEFT\n"));
    OTTER_CHECK(has(out, "ON"));
}

OTTER_TEST(format_keeps_a_three_word_join_together) {
    const std::string out = fmt("select * from a full outer join b on a.x = b.x");
    OTTER_CHECK(has(out, "FULL OUTER JOIN"));
}

OTTER_TEST(format_is_idempotent) {
    // Formatar duas vezes precisa dar o mesmo texto. Se nao der, o formatador
    // esta' acumulando espaco ou quebra a cada passagem.
    const std::string once  = fmt("select a, b from cliente where x > 1");
    const std::string twice = fmt(once);
    OTTER_CHECK_EQ(once, twice);
}

OTTER_TEST(format_handles_empty_input) {
    OTTER_CHECK(fmt("").empty());
    OTTER_CHECK(fmt("   \n  ").empty());
}
