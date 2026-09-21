// Tabela dinâmica (ADR 0005).
//
// As duas armadilhas estão no próprio ADR, e as duas são de mentira
// silenciosa:
//
//   1. Alta cardinalidade gera milhares de colunas. Truncar é inevitável;
//      truncar SEM DIZER faria o usuário concluir que os dados não existem.
//   2. Pivotar a PÁGINA não é pivotar o resultado. A célula (sul, jan)
//      somaria só o que veio nas 200 linhas em memória -- e pareceria a soma.
#include "test_main.hpp"

#include "db/pivot.hpp"

#include <string>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

ResultSet make_result(const std::vector<std::string>& columns,
                      const std::vector<std::vector<std::string>>& rows,
                      const std::vector<std::pair<std::size_t, std::size_t>>& nulls = {}) {
    ResultSetBuilder builder;
    for (const std::string& name : columns) {
        ColumnInfo info;
        info.name = name;
        info.kind = DataKind::string;
        builder.add_column(std::move(info));
    }
    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            const bool is_null = std::find(nulls.begin(), nulls.end(),
                                           std::make_pair(r, c)) != nulls.end();
            if (is_null) builder.append_null(c);
            else         builder.append_text(c, rows[r][c]);
        }
    }
    builder.set_row_count(rows.size());
    return builder.take();
}

// Vendas por região e mês -- o exemplo do cabeçalho de pivot.hpp.
ResultSet vendas() {
    return make_result({"regiao", "mes", "valor"},
                       {{"sul",   "jan", "100"},
                        {"norte", "jan", "200"},
                        {"sul",   "fev", "300"},
                        {"norte", "mar", "150"},
                        {"sul",   "jan", "50"}});   // segunda venda sul/jan
}

PivotSpec spec_of(std::size_t value_column = 2) {
    PivotSpec spec;
    spec.rows     = {0};   // regiao
    spec.column   = 1;     // mes
    spec.value    = value_column;
    spec.function = Aggregate::sum;
    return spec;
}

// Acha a coluna gerada pelo cabeçalho.
std::size_t header_index(const PivotResult& result, std::string_view name) {
    for (std::size_t i = 0; i < result.headers.size(); ++i) {
        if (result.headers[i] == name) return i;
    }
    return result.headers.size();
}

} // namespace

// --- Transposição -----------------------------------------------------------------

OTTER_TEST(pivot_turns_values_into_columns) {
    const PivotResult result = pivot(vendas(), spec_of(), false);

    // Três meses distintos viram três colunas, em ordem alfabética estável.
    OTTER_CHECK_EQ(result.headers.size(), std::size_t{3});
    OTTER_CHECK_EQ(result.headers[0], std::string{"fev"});
    OTTER_CHECK_EQ(result.headers[1], std::string{"jan"});
    OTTER_CHECK_EQ(result.headers[2], std::string{"mar"});

    // Duas regiões viram duas linhas.
    OTTER_CHECK_EQ(result.rows.size(), std::size_t{2});
    OTTER_CHECK_EQ(result.rows[0].keys[0], std::string{"norte"});
    OTTER_CHECK_EQ(result.rows[1].keys[0], std::string{"sul"});
}

OTTER_TEST(pivot_aggregates_at_the_intersection) {
    const PivotResult result = pivot(vendas(), spec_of(), false);

    const std::size_t jan = header_index(result, "jan");

    // sul/jan tem DUAS vendas: 100 + 50. Pegar só a primeira seria o defeito
    // mais fácil de não notar, porque a maioria das combinações tem uma linha.
    OTTER_CHECK_EQ(result.rows[1].cells[jan].text, std::string{"150"});

    // norte/jan tem uma.
    OTTER_CHECK_EQ(result.rows[0].cells[jan].text, std::string{"200"});
}

OTTER_TEST(pivot_shows_missing_combinations_as_absent_not_zero) {
    // norte não vendeu em fevereiro. Mostrar "0" seria MENTIRA: zero é um
    // valor (vendeu e deu zero), ausente é outra coisa.
    const PivotResult result = pivot(vendas(), spec_of(), false);

    const std::size_t fev = header_index(result, "fev");
    OTTER_CHECK_EQ(result.rows[0].cells[fev].text, std::string{"-"});   // norte
    OTTER_CHECK_EQ(result.rows[1].cells[fev].text, std::string{"300"}); // sul
}

OTTER_TEST(pivot_totals_cover_every_row_of_the_column) {
    const PivotResult result = pivot(vendas(), spec_of(), false);

    const std::size_t jan = header_index(result, "jan");

    // jan: 100 + 200 + 50, de ambas as regiões.
    OTTER_CHECK_EQ(result.totals[jan].text, std::string{"350"});

    const std::size_t mar = header_index(result, "mar");
    OTTER_CHECK_EQ(result.totals[mar].text, std::string{"150"});
}

OTTER_TEST(pivot_groups_nulls_together_like_sql) {
    // Dois nulos na coluna pivotada são a MESMA coluna, como no GROUP BY --
    // ainda que NULL <> NULL numa comparação.
    const ResultSet rs = make_result(
        {"r", "m", "v"},
        {{"sul", "x", "10"}, {"sul", "y", "20"}, {"norte", "jan", "5"}},
        {{0, 1}, {1, 1}});

    const PivotResult result = pivot(rs, spec_of(), false);

    const std::size_t null_column = header_index(result, "[null]");
    OTTER_CHECK(null_column < result.headers.size());

    // sul tem 10 + 20 na coluna dos nulos.
    for (const PivotRow& row : result.rows) {
        if (row.keys[0] != "sul") continue;
        OTTER_CHECK_EQ(row.cells[null_column].text, std::string{"30"});
    }
}

OTTER_TEST(pivot_supports_a_composite_row_key) {
    const ResultSet rs = make_result(
        {"regiao", "ano", "mes", "valor"},
        {{"sul", "2025", "jan", "10"},
         {"sul", "2026", "jan", "20"},
         {"sul", "2025", "fev", "30"}});

    PivotSpec spec;
    spec.rows     = {0, 1};
    spec.column   = 2;
    spec.value    = 3;
    spec.function = Aggregate::sum;

    const PivotResult result = pivot(rs, spec, false);

    OTTER_CHECK_EQ(result.rows.size(), std::size_t{2});
    OTTER_CHECK_EQ(result.rows[0].keys.size(), std::size_t{2});
    OTTER_CHECK_EQ(result.rows[0].keys[0], std::string{"sul"});
    OTTER_CHECK_EQ(result.rows[0].keys[1], std::string{"2025"});
}

OTTER_TEST(pivot_honours_the_aggregate_function) {
    PivotSpec spec = spec_of();
    spec.function = Aggregate::count;

    const PivotResult result = pivot(vendas(), spec, false);
    const std::size_t jan = header_index(result, "jan");

    // sul/jan tem duas linhas: a CONTAGEM é 2, não a soma 150.
    OTTER_CHECK_EQ(result.rows[1].cells[jan].text, std::string{"2"});
}

// --- As duas armadilhas -------------------------------------------------------------

OTTER_TEST(pivot_marks_a_partial_result_as_partial) {
    // Pivotar 200 linhas de 2 milhões e apresentar como se fosse o resultado
    // é a mesma mentira da agregação (db/aggregate.hpp).
    OTTER_CHECK(pivot(vendas(), spec_of(), /*partial=*/true).partial);
    OTTER_CHECK(!pivot(vendas(), spec_of(), /*partial=*/false).partial);

    OTTER_CHECK_EQ(pivot(vendas(), spec_of(), true).rows_covered,
                   std::size_t{5});
}

OTTER_TEST(pivot_truncates_high_cardinality_and_says_so) {
    // 100 valores distintos gerariam 100 colunas: a grade não desenha, o
    // ImGui nem aceita mais de 64. Truncar é inevitável -- truncar EM
    // SILÊNCIO faria o usuário concluir que os dados não existem.
    std::vector<std::vector<std::string>> rows;
    for (int i = 0; i < 100; ++i) {
        rows.push_back({"sul", "m" + std::to_string(i), "1"});
    }
    const ResultSet rs = make_result({"r", "m", "v"}, rows);

    const PivotResult result = pivot(rs, spec_of(), false, /*max_columns=*/10);

    OTTER_CHECK(result.truncated);
    OTTER_CHECK_EQ(result.headers.size(), std::size_t{10});
    OTTER_CHECK_EQ(result.distinct_values, std::size_t{100});   // diz quantos há

    // E o que sobrou continua correto: nada de célula com lixo.
    OTTER_CHECK_EQ(result.rows.size(), std::size_t{1});
    for (const AggregateValue& cell : result.rows[0].cells) {
        OTTER_CHECK_EQ(cell.text, std::string{"1"});
    }
}

OTTER_TEST(pivot_within_the_limit_is_not_marked_truncated) {
    const PivotResult result = pivot(vendas(), spec_of(), false);
    OTTER_CHECK(!result.truncated);
    OTTER_CHECK_EQ(result.distinct_values, std::size_t{3});
}

// --- Recusas ---------------------------------------------------------------------------

OTTER_TEST(pivot_refuses_an_incomplete_spec) {
    PivotSpec empty;
    OTTER_CHECK(pivot(vendas(), empty, false).empty());

    // Coluna fora da faixa: recusa em vez de ler memória alheia.
    PivotSpec bad = spec_of();
    bad.column = 99;
    OTTER_CHECK(pivot(vendas(), bad, false).empty());

    bad = spec_of();
    bad.value = 99;
    OTTER_CHECK(pivot(vendas(), bad, false).empty());

    bad = spec_of();
    bad.rows = {99};
    OTTER_CHECK(pivot(vendas(), bad, false).empty());
}

// --- Pivot no servidor ---------------------------------------------------------------

OTTER_TEST(pivot_query_uses_conditional_aggregates_not_the_pivot_keyword) {
    // PIVOT só existe no SQL Server e no Oracle. O CASE é padrão SQL e
    // funciona nos dois SGBDs que o C-Otter fala.
    const ResultSet rs = vendas();
    const PivotResult local = pivot(rs, spec_of(), true);

    const std::string sql = build_pivot_query(
        "SELECT regiao, mes, valor FROM vendas", rs, spec_of(), local.headers);

    OTTER_CHECK(!sql.empty());
    OTTER_CHECK(!has(sql, "PIVOT"));
    OTTER_CHECK(has(sql, "sum(CASE WHEN mes = 'jan' THEN valor END)"));
    OTTER_CHECK(has(sql, "GROUP BY regiao"));

    // Envolve, não anexa: a consulta original pode já ter GROUP BY ou LIMIT.
    OTTER_CHECK(has(sql, "FROM ("));
    OTTER_CHECK(has(sql, ") AS otter_pivot"));
}

OTTER_TEST(pivot_query_compares_null_with_is_null) {
    // "[null]" é a NOSSA representação, não um valor do banco. Gerar
    // `mes = '[null]'` não casaria com linha nenhuma -- e a coluna viria
    // vazia sem erro nenhum.
    const ResultSet rs = make_result({"r", "m", "v"},
                                     {{"sul", "x", "10"}}, {{0, 1}});

    const PivotResult local = pivot(rs, spec_of(), false);
    const std::string sql =
        build_pivot_query("SELECT * FROM t", rs, spec_of(), local.headers);

    OTTER_CHECK(has(sql, "m IS NULL"));
    OTTER_CHECK(!has(sql, "'[null]'"));
}

OTTER_TEST(pivot_query_is_empty_without_headers) {
    OTTER_CHECK(build_pivot_query("SELECT * FROM t", vendas(), spec_of(), {})
                    .empty());
    OTTER_CHECK(build_pivot_query({}, vendas(), spec_of(), {"jan"}).empty());
}

OTTER_TEST(pivot_query_honours_the_aggregate_function) {
    PivotSpec spec = spec_of();
    spec.function = Aggregate::average;

    const std::string sql =
        build_pivot_query("SELECT * FROM t", vendas(), spec, {"jan"});
    OTTER_CHECK(has(sql, "avg(CASE WHEN"));
}

// --- O ';' do usuario ---------------------------------------------------------------

OTTER_TEST(pivot_query_drops_the_trailing_semicolon) {
    // A consulta do usuario quase sempre termina em ';'. Envolvida numa
    // subconsulta, esse ';' fica NO MEIO -- e o servidor recusa apontando
    // para o ')', o que manda procurar no lugar errado.
    //
    // Foi o que apareceu na tela ao pivotar no servidor:
    //   You have an error ... near ';\n\n) AS otter_pivot'
    const ResultSet rs = vendas();
    const PivotResult local = pivot(rs, spec_of(), true);

    const std::string sql = build_pivot_query(
        "SELECT regiao, mes, valor FROM vendas;", rs, spec_of(), local.headers);

    OTTER_CHECK(!has(sql, ";\n"));
    OTTER_CHECK(has(sql, "FROM vendas\n) AS otter_pivot"));
}

OTTER_TEST(pivot_query_drops_the_semicolon_with_trailing_whitespace) {
    const ResultSet rs = vendas();
    const PivotResult local = pivot(rs, spec_of(), true);

    const std::string sql = build_pivot_query(
        "SELECT * FROM vendas ;  \n\n", rs, spec_of(), local.headers);

    OTTER_CHECK(has(sql, "FROM vendas\n) AS otter_pivot"));
}
