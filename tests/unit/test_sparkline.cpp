// Barra na célula (ADR 0005). O DBeaver não tem isto -- é um dos poucos
// pontos em que o C-Otter vai além dele.
//
// O que estes testes cobrem é a MATEMÁTICA das três ancoragens, que é onde
// uma barra mente: uma fração errada não quebra nada, não aparece em log
// nenhum, e o usuário lê a coluna como se a barra dissesse a verdade.
#include "test_main.hpp"

#include "db/sparkline.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace otter::db;

namespace {

bool near(float a, float b, float tolerance = 0.001f) {
    return std::abs(a - b) <= tolerance;
}

ResultSet make_result(const std::vector<std::string>& values,
                      const std::vector<std::size_t>& null_rows = {}) {
    ResultSetBuilder builder;

    ColumnInfo info;
    info.name = "valor";
    info.kind = DataKind::numeric;
    builder.add_column(std::move(info));

    for (std::size_t r = 0; r < values.size(); ++r) {
        const bool is_null =
            std::find(null_rows.begin(), null_rows.end(), r) != null_rows.end();
        if (is_null) builder.append_null(0);
        else         builder.append_text(0, values[r]);
    }
    builder.set_row_count(values.size());
    return builder.take();
}

BarRules rules_for(const ResultSet& rs, BarBaseline baseline) {
    BarSpec spec;
    spec.column   = "valor";
    spec.baseline = baseline;

    BarRules rules;
    rules.add(std::move(spec));
    rules.prepare(rs);
    return rules;
}

} // namespace

OTTER_TEST(bar_from_zero_scales_by_the_largest_value) {
    const ResultSet rs = make_result({"0", "50", "100"});
    const BarRules rules = rules_for(rs, BarBaseline::from_zero);

    OTTER_CHECK(near(rules.bar_for(rs, 0, 0).fraction, 0.0f));
    OTTER_CHECK(near(rules.bar_for(rs, 1, 0).fraction, 0.5f));
    OTTER_CHECK(near(rules.bar_for(rs, 2, 0).fraction, 1.0f));
}

OTTER_TEST(bar_from_zero_uses_the_largest_absolute_value) {
    // Coluna de -900 a 100. Escalar pelo MAXIMO (100) daria ao -900 uma barra
    // nove vezes maior que a régua -- ela sairia da célula, ou seria cortada
    // e mentiria sobre a proporção.
    const ResultSet rs = make_result({"-900", "100"});
    const BarRules rules = rules_for(rs, BarBaseline::from_zero);

    OTTER_CHECK(near(rules.bar_for(rs, 0, 0).fraction, 1.0f));
    OTTER_CHECK(near(rules.bar_for(rs, 1, 0).fraction, 100.0f / 900.0f));
    OTTER_CHECK(rules.bar_for(rs, 0, 0).negative);
    OTTER_CHECK(!rules.bar_for(rs, 1, 0).negative);
}

OTTER_TEST(bar_from_minimum_spreads_a_narrow_range) {
    // 36,1 a 36,9 ancorado no zero daria 200 barras visualmente identicas --
    // todas em 99% da largura. Ancorado no minimo, a variacao aparece.
    const ResultSet rs = make_result({"36.1", "36.5", "36.9"});
    const BarRules rules = rules_for(rs, BarBaseline::from_minimum);

    OTTER_CHECK(near(rules.bar_for(rs, 0, 0).fraction, 0.0f));
    OTTER_CHECK(near(rules.bar_for(rs, 1, 0).fraction, 0.5f));
    OTTER_CHECK(near(rules.bar_for(rs, 2, 0).fraction, 1.0f));
}

OTTER_TEST(bar_from_minimum_fills_a_constant_column) {
    // Todos iguais: span zero. Fracao zero deixaria a coluna com aparencia de
    // vazia, como se nao houvesse dado -- e ha'.
    const ResultSet rs = make_result({"7", "7", "7"});
    const BarRules rules = rules_for(rs, BarBaseline::from_minimum);

    OTTER_CHECK(near(rules.bar_for(rs, 0, 0).fraction, 1.0f));
    OTTER_CHECK(near(rules.bar_for(rs, 2, 0).fraction, 1.0f));
}

OTTER_TEST(bar_centered_puts_zero_in_the_middle) {
    const ResultSet rs = make_result({"-100", "0", "100"});
    const BarRules rules = rules_for(rs, BarBaseline::centered_on_zero);

    const CellBar negative = rules.bar_for(rs, 0, 0);
    OTTER_CHECK(near(negative.fraction, 0.5f));
    OTTER_CHECK(near(negative.origin, 0.0f));     // parte da borda esquerda
    OTTER_CHECK(negative.negative);

    const CellBar zero = rules.bar_for(rs, 1, 0);
    OTTER_CHECK(near(zero.fraction, 0.0f));
    OTTER_CHECK(near(zero.origin, 0.5f));         // no meio, sem comprimento

    const CellBar positive = rules.bar_for(rs, 2, 0);
    OTTER_CHECK(near(positive.fraction, 0.5f));
    OTTER_CHECK(near(positive.origin, 0.5f));     // parte do meio
    OTTER_CHECK(!positive.negative);
}

OTTER_TEST(bar_centered_keeps_both_sides_on_the_same_scale) {
    // De -50 a 100: o -50 deve ocupar METADE do que o 100 ocupa, cada um para
    // o seu lado. Escalar cada lado pelo proprio extremo faria -50 e 100
    // parecerem iguais -- e o sinal e' justamente a informacao deste modo.
    const ResultSet rs = make_result({"-50", "100"});
    const BarRules rules = rules_for(rs, BarBaseline::centered_on_zero);

    OTTER_CHECK(near(rules.bar_for(rs, 0, 0).fraction, 0.25f));
    OTTER_CHECK(near(rules.bar_for(rs, 1, 0).fraction, 0.5f));
}

OTTER_TEST(bar_skips_null) {
    // Barra de comprimento zero num NULL seria indistinguivel de um valor
    // minimo legitimo -- e sao coisas diferentes.
    const ResultSet rs = make_result({"10", "", "30"}, /*null_rows=*/{1});
    const BarRules rules = rules_for(rs, BarBaseline::from_zero);

    OTTER_CHECK(!rules.bar_for(rs, 1, 0).visible);
    OTTER_CHECK(rules.bar_for(rs, 0, 0).visible);
}

OTTER_TEST(bar_ignores_a_text_column) {
    ResultSetBuilder builder;
    ColumnInfo info;
    info.name = "nome";
    info.kind = DataKind::string;
    builder.add_column(std::move(info));
    builder.append_text(0, "Alfa");
    builder.append_text(0, "Beta");
    builder.set_row_count(2);
    const ResultSet rs = builder.take();

    BarSpec spec;
    spec.column = "nome";
    BarRules rules;
    rules.add(std::move(spec));
    rules.prepare(rs);

    OTTER_CHECK(!rules.bar_for(rs, 0, 0).visible);
}

OTTER_TEST(bar_ignores_a_column_that_is_not_in_the_result) {
    // A regra sobrevive a uma troca de consulta. Apontar para uma coluna que
    // sumiu nao pode desenhar barra na coluna de INDICE equivalente -- seria
    // uma barra sobre dados de outra coisa.
    const ResultSet rs = make_result({"10", "20"});

    BarSpec spec;
    spec.column = "coluna_que_nao_existe";
    BarRules rules;
    rules.add(std::move(spec));
    rules.prepare(rs);

    OTTER_CHECK(!rules.bar_for(rs, 0, 0).visible);
}

OTTER_TEST(bar_replaces_the_rule_of_the_same_column) {
    // Duas barras na mesma coluna desenhariam uma por cima da outra, e a de
    // baixo seria invisivel -- o usuario veria a regra na lista e nada na
    // tela.
    BarSpec first;
    first.column   = "valor";
    first.baseline = BarBaseline::from_zero;

    BarSpec second;
    second.column   = "valor";
    second.baseline = BarBaseline::centered_on_zero;

    BarRules rules;
    rules.add(std::move(first));
    rules.add(std::move(second));

    OTTER_CHECK_EQ(rules.specs().size(), std::size_t{1});
    OTTER_CHECK(rules.specs()[0].baseline == BarBaseline::centered_on_zero);
}

OTTER_TEST(bar_disabled_rule_draws_nothing) {
    const ResultSet rs = make_result({"10", "20"});

    BarSpec spec;
    spec.column  = "valor";
    spec.enabled = false;

    BarRules rules;
    rules.add(std::move(spec));
    rules.prepare(rs);

    OTTER_CHECK(!rules.bar_for(rs, 1, 0).visible);
    OTTER_CHECK(!rules.affects_column("valor"));
}
