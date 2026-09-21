// Agrupamento e totais (ADR 0005).
//
// O risco aqui nao e' errar a conta -- e' apresentar uma conta PARCIAL como
// se fosse o total. Com paginacao, o que esta' em memoria sao 200 linhas de
// um resultado que pode ter milhoes, e somar essas 200 e chamar de "total"
// e' mentira. Por isso `partial` e' testado junto com cada agregacao.
#include "test_main.hpp"

#include "db/aggregate.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

ResultSet make_result(
    const std::vector<std::pair<std::string, DataKind>>& columns,
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<std::pair<std::size_t, std::size_t>>& nulls = {}) {

    ResultSetBuilder builder;
    for (const auto& [name, kind] : columns) {
        ColumnInfo info;
        info.name = name;
        info.kind = kind;
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

// Vendas por região, com um NULL em valor.
ResultSet sales() {
    return make_result(
        {{"regiao", DataKind::string}, {"valor", DataKind::numeric}},
        {{"sul",   "100"},
         {"norte", "200"},
         {"sul",   "300"},
         {"norte", "50"},
         {"sul",   "x"}},          // valor nulo
        {{4, 1}});
}

} // namespace

// --- Honestidade sobre o escopo ----------------------------------------------

OTTER_TEST(aggregate_marks_a_partial_result_as_partial) {
    // Uma soma sobre a pagina atual NAO e' a soma do resultado. A marca
    // acompanha o valor para que a interface possa dizer isso.
    GroupSpec spec;
    spec.aggregates = {{1, Aggregate::sum}};

    const GroupResult partial =
        group_and_aggregate(sales(), spec, /*partial=*/true);
    OTTER_CHECK(partial.partial);
    OTTER_CHECK_EQ(partial.rows_covered, std::size_t{5});

    const GroupResult complete =
        group_and_aggregate(sales(), spec, /*partial=*/false);
    OTTER_CHECK(!complete.partial);
}

// --- Contas -------------------------------------------------------------------

OTTER_TEST(aggregate_sums_ignoring_nulls) {
    const AggregateValue total =
        aggregate_column(sales(), 1, Aggregate::sum);

    OTTER_CHECK(total.numeric);
    OTTER_CHECK_EQ(total.text, std::string{"650"});   // 100+200+300+50
}

OTTER_TEST(aggregate_average_divides_by_non_null_like_sql) {
    // AVG do SQL ignora nulos. Dividir por 5 (contando o nulo) daria 130,
    // que e' outro numero e nao o que o usuario espera.
    const AggregateValue average =
        aggregate_column(sales(), 1, Aggregate::average);

    OTTER_CHECK_EQ(average.text, std::string{"162.5"});   // 650 / 4
}

OTTER_TEST(aggregate_counts_distinguish_null_from_row) {
    const ResultSet rs = sales();

    OTTER_CHECK_EQ(aggregate_column(rs, 1, Aggregate::count).text,
                   std::string{"5"});            // todas as linhas
    OTTER_CHECK_EQ(aggregate_column(rs, 1, Aggregate::count_non_null).text,
                   std::string{"4"});            // sem o nulo
    OTTER_CHECK_EQ(aggregate_column(rs, 0, Aggregate::count_distinct).text,
                   std::string{"2"});            // sul, norte
}

OTTER_TEST(aggregate_min_max_work_on_numbers_and_text) {
    const ResultSet rs = sales();

    OTTER_CHECK_EQ(aggregate_column(rs, 1, Aggregate::minimum).text,
                   std::string{"50"});
    OTTER_CHECK_EQ(aggregate_column(rs, 1, Aggregate::maximum).text,
                   std::string{"300"});

    // Texto tem ordem lexicografica: min e max fazem sentido.
    OTTER_CHECK_EQ(aggregate_column(rs, 0, Aggregate::minimum).text,
                   std::string{"norte"});
    OTTER_CHECK_EQ(aggregate_column(rs, 0, Aggregate::maximum).text,
                   std::string{"sul"});
}

OTTER_TEST(aggregate_does_not_treat_unparseable_text_as_zero) {
    // Somar "N/A" como zero produziria um total errado em silencio -- o pior
    // tipo de defeito numa planilha.
    const ResultSet rs = make_result(
        {{"v", DataKind::string}}, {{"10"}, {"N/A"}, {"20"}});

    const AggregateValue total = aggregate_column(rs, 0, Aggregate::sum);
    OTTER_CHECK_EQ(total.text, std::string{"30"});   // nao 30 + 0
}

// --- Aplicabilidade -----------------------------------------------------------

OTTER_TEST(aggregate_refuses_to_sum_text) {
    // Oferecer SUM numa coluna de nomes seria um campo que finge funcionar.
    OTTER_CHECK(!aggregate_applies(Aggregate::sum, DataKind::string));
    OTTER_CHECK(!aggregate_applies(Aggregate::average, DataKind::date));

    OTTER_CHECK(aggregate_applies(Aggregate::sum, DataKind::numeric));
    OTTER_CHECK(aggregate_applies(Aggregate::sum, DataKind::integer));

    // Contar vale sempre.
    OTTER_CHECK(aggregate_applies(Aggregate::count, DataKind::string));
    OTTER_CHECK(aggregate_applies(Aggregate::count, DataKind::binary));

    // Min e max valem onde ha' ordem.
    OTTER_CHECK(aggregate_applies(Aggregate::minimum, DataKind::date));
    OTTER_CHECK(!aggregate_applies(Aggregate::minimum, DataKind::json));
}

// --- Agrupamento --------------------------------------------------------------

OTTER_TEST(aggregate_groups_by_one_column) {
    GroupSpec spec;
    spec.group_by   = {0};
    spec.aggregates = {{1, Aggregate::sum}, {1, Aggregate::count}};

    const GroupResult result = group_and_aggregate(sales(), spec, false);

    OTTER_CHECK_EQ(result.groups.size(), std::size_t{2});

    // Ordem alfabetica: norte antes de sul.
    OTTER_CHECK_EQ(result.groups[0].key_values[0], std::string{"norte"});
    OTTER_CHECK_EQ(result.groups[0].aggregates[0].text, std::string{"250"});
    OTTER_CHECK_EQ(result.groups[0].rows.size(), std::size_t{2});

    OTTER_CHECK_EQ(result.groups[1].key_values[0], std::string{"sul"});
    OTTER_CHECK_EQ(result.groups[1].aggregates[0].text, std::string{"400"});
    OTTER_CHECK_EQ(result.groups[1].aggregates[1].text, std::string{"3"});

    // O total geral continua sendo a soma de tudo.
    OTTER_CHECK_EQ(result.totals[0].text, std::string{"650"});
}

OTTER_TEST(aggregate_groups_nulls_together_like_sql) {
    // Duas linhas com NULL na coluna de agrupamento pertencem ao MESMO grupo,
    // como no GROUP BY do SQL -- ainda que NULL <> NULL numa comparacao.
    const ResultSet rs = make_result(
        {{"cat", DataKind::string}, {"v", DataKind::integer}},
        {{"a", "1"}, {"x", "2"}, {"y", "3"}},
        {{1, 0}, {2, 0}});

    GroupSpec spec;
    spec.group_by   = {0};
    spec.aggregates = {{1, Aggregate::sum}};

    const GroupResult result = group_and_aggregate(rs, spec, false);
    OTTER_CHECK_EQ(result.groups.size(), std::size_t{2});

    // O grupo dos nulos soma 2 + 3.
    bool found_null_group = false;
    for (const Group& group : result.groups) {
        if (group.key_values[0] == "[null]") {
            found_null_group = true;
            OTTER_CHECK_EQ(group.aggregates[0].text, std::string{"5"});
        }
    }
    OTTER_CHECK(found_null_group);
}

OTTER_TEST(aggregate_groups_by_two_columns) {
    const ResultSet rs = make_result(
        {{"regiao", DataKind::string},
         {"ano",    DataKind::integer},
         {"valor",  DataKind::numeric}},
        {{"sul", "2025", "10"},
         {"sul", "2026", "20"},
         {"sul", "2025", "30"},
         {"norte", "2026", "40"}});

    GroupSpec spec;
    spec.group_by   = {0, 1};
    spec.aggregates = {{2, Aggregate::sum}};

    const GroupResult result = group_and_aggregate(rs, spec, false);

    // (norte,2026), (sul,2025), (sul,2026)
    OTTER_CHECK_EQ(result.groups.size(), std::size_t{3});
    OTTER_CHECK_EQ(result.groups[1].key_values[0], std::string{"sul"});
    OTTER_CHECK_EQ(result.groups[1].key_values[1], std::string{"2025"});
    OTTER_CHECK_EQ(result.groups[1].aggregates[0].text, std::string{"40"});
}

OTTER_TEST(aggregate_without_group_by_still_computes_totals) {
    // A linha de totais nao precisa de agrupamento.
    GroupSpec spec;
    spec.aggregates = {{1, Aggregate::sum}, {1, Aggregate::maximum}};

    const GroupResult result = group_and_aggregate(sales(), spec, false);
    OTTER_CHECK(result.groups.empty());
    OTTER_CHECK_EQ(result.totals.size(), std::size_t{2});
    OTTER_CHECK_EQ(result.totals[0].text, std::string{"650"});
    OTTER_CHECK_EQ(result.totals[1].text, std::string{"300"});
}

// --- Agregação no servidor ----------------------------------------------------

OTTER_TEST(aggregate_builds_a_server_side_group_query) {
    // A resposta honesta quando o resultado e' paginado: em vez de somar a
    // pagina, perguntar ao banco.
    GroupSpec spec;
    spec.group_by   = {0};
    spec.aggregates = {{1, Aggregate::sum}};

    const std::string sql = build_group_query(
        "SELECT regiao, valor FROM vendas", sales(), spec);

    OTTER_CHECK(has(sql, "GROUP BY regiao"));
    OTTER_CHECK(has(sql, "sum(valor)"));
    OTTER_CHECK(has(sql, "SELECT regiao, valor FROM vendas"));

    // Envolve, nao anexa: a consulta original pode ja' ter GROUP BY ou LIMIT.
    OTTER_CHECK(has(sql, "FROM ("));
    OTTER_CHECK(has(sql, ") AS otter_group"));

    // Apelido, senao o servidor devolve "sum" para todas as somas.
    OTTER_CHECK(has(sql, "AS"));
}

OTTER_TEST(aggregate_server_query_uses_count_star_for_count) {
    GroupSpec spec;
    spec.group_by   = {0};
    spec.aggregates = {{1, Aggregate::count}, {0, Aggregate::count_distinct}};

    const std::string sql = build_group_query("SELECT * FROM t", sales(), spec);
    OTTER_CHECK(has(sql, "count(*)"));
    OTTER_CHECK(has(sql, "count(DISTINCT regiao)"));
}

OTTER_TEST(aggregate_server_query_is_empty_without_grouping) {
    GroupSpec spec;
    spec.aggregates = {{1, Aggregate::sum}};
    OTTER_CHECK(build_group_query("SELECT * FROM t", sales(), spec).empty());
}
