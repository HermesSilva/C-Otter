// Formatação condicional da grade (ADR 0005).
//
// Três armadilhas aqui, todas de resultado silenciosamente errado:
//
//   1. Comparar "10" com "9" como TEXTO dá "10 < 9".
//   2. NULL numa comparação de valor não é falso nem verdadeiro -- pintar
//      como se fosse inventa uma resposta que o SQL não dá.
//   3. Calcular o mínimo e o máximo da coluna POR CÉLULA é O(n²): com 200
//      linhas a grade engasga, e o defeito aparece como "ficou lento" sem
//      ninguém saber por quê.
#include "test_main.hpp"

#include "db/coloring.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace otter::db;

namespace {

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

// Pedidos com situação e valor.
ResultSet pedidos() {
    return make_result({"situacao", "valor"},
                       {{"ativo",     "100"},
                        {"cancelado", "9"},
                        {"ativo",     "10"},
                        {"pendente",  "x"}},    // valor nulo
                       {{3, 1}});
}

constexpr std::uint32_t kRed   = 0xFF0000FFu;
constexpr std::uint32_t kGreen = 0xFF00FF00u;
constexpr std::uint32_t kBlue  = 0xFFFF0000u;

} // namespace

// --- Comparação numérica vs textual ----------------------------------------------

OTTER_TEST(coloring_compares_numbers_as_numbers) {
    // "10" > "9" é VERDADE numericamente e FALSO como texto. Comparar como
    // texto é o defeito mais comum de uma comparação frouxa, e passa
    // despercebido porque a maioria dos valores de teste tem o mesmo número
    // de dígitos.
    ColorRules rules;
    ColorRule rule;
    rule.column     = "valor";
    rule.op         = ColorOp::greater;
    rule.value      = "9";
    rule.background = kRed;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, kRed);   // 100 > 9
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 1).background, 0u);     // 9 > 9 não
    OTTER_CHECK_EQ(rules.color_for(rs, 2, 1).background, kRed);   // 10 > 9 SIM
}

OTTER_TEST(coloring_falls_back_to_text_comparison) {
    ColorRules rules;
    ColorRule rule;
    rule.column     = "situacao";
    rule.op         = ColorOp::equals;
    rule.value      = "cancelado";
    rule.background = kRed;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, kRed);
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 0).background, 0u);
}

// --- NULL -------------------------------------------------------------------------

OTTER_TEST(coloring_does_not_match_null_against_a_value) {
    // NULL > 10 é DESCONHECIDO em SQL, não falso. Pintar como se fosse
    // verdadeiro ou falso inventa uma resposta que o banco não dá.
    ColorRules rules;
    ColorRule greater;
    greater.column     = "valor";
    greater.op         = ColorOp::greater;
    greater.value      = "0";
    greater.background = kRed;
    rules.add(greater);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    // Linha 3 tem valor nulo.
    OTTER_CHECK_EQ(rules.color_for(rs, 3, 1).background, 0u);
}

OTTER_TEST(coloring_has_explicit_null_operators) {
    // Para pintar o nulo é preciso pedir explicitamente -- que é como o SQL
    // também exige IS NULL em vez de = NULL.
    ColorRules rules;
    ColorRule rule;
    rule.column     = "valor";
    rule.op         = ColorOp::is_null;
    rule.background = kBlue;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 3, 1).background, kBlue);
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, 0u);
}

// --- Gradiente ----------------------------------------------------------------------

OTTER_TEST(coloring_gradient_interpolates_across_the_column) {
    // O mapa de calor não compara com um valor fixo: compara com o RESTO da
    // coluna. Os extremos recebem as cores das pontas.
    ColorRules rules;
    ColorRule rule;
    rule.column     = "valor";
    rule.op         = ColorOp::range;
    rule.foreground = kGreen;   // no modo range, as duas PONTAS do degradê
    rule.background = kRed;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    // A coluna tem 9, 10 e 100 (o nulo não entra). O menor fica verde, o
    // maior fica vermelho.
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 1).background, kGreen);  // 9
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, kRed);    // 100

    // E o do meio fica ENTRE os dois, não igual a nenhum.
    const std::uint32_t middle = rules.color_for(rs, 2, 1).background;
    OTTER_CHECK(middle != kGreen);
    OTTER_CHECK(middle != kRed);

    // Nulo não entra no gradiente.
    OTTER_CHECK_EQ(rules.color_for(rs, 3, 1).background, 0u);
}

OTTER_TEST(coloring_gradient_survives_a_column_with_one_value) {
    // Faixa degenerada: todos os valores iguais. Dividir por zero daria NaN e
    // uma cor aleatória -- o tipo de defeito que só aparece com dados reais.
    const ResultSet rs = make_result({"v"}, {{"5"}, {"5"}, {"5"}});

    ColorRules rules;
    ColorRule rule;
    rule.column     = "v";
    rule.op         = ColorOp::range;
    rule.foreground = kGreen;
    rule.background = kRed;
    rules.add(rule);
    rules.prepare(rs);

    // Todas recebem a cor do início, e nenhuma recebe lixo.
    for (std::size_t row = 0; row < 3; ++row) {
        OTTER_CHECK_EQ(rules.color_for(rs, row, 0).background, kGreen);
    }
}

OTTER_TEST(coloring_blend_clamps_outside_the_range) {
    // Fora de [0,1] seria extrapolação: uma cor que nenhuma das pontas tem.
    OTTER_CHECK_EQ(blend(kGreen, kRed, -1.0), kGreen);
    OTTER_CHECK_EQ(blend(kGreen, kRed, 2.0), kRed);
    OTTER_CHECK_EQ(blend(kGreen, kRed, 0.0), kGreen);
    OTTER_CHECK_EQ(blend(kGreen, kRed, 1.0), kRed);
}

OTTER_TEST(coloring_blend_mixes_each_channel) {
    // Metade do caminho entre preto e branco é cinza médio nos quatro canais.
    OTTER_CHECK_EQ(blend(0x00000000u, 0xFFFFFFFFu, 0.5), 0x80808080u);
}

// --- Escopo das regras ---------------------------------------------------------------

OTTER_TEST(coloring_cell_rule_only_paints_its_own_column) {
    ColorRules rules;
    ColorRule rule;
    rule.column     = "situacao";
    rule.op         = ColorOp::equals;
    rule.value      = "cancelado";
    rule.background = kRed;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, kRed);  // situacao
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 1).background, 0u);    // valor, não
}

OTTER_TEST(coloring_row_rule_paints_every_column) {
    // É assim que se marca "pedido cancelado" sem repetir a regra em cada
    // coluna da grade.
    ColorRules rules;
    ColorRule rule;
    rule.column     = "situacao";
    rule.op         = ColorOp::equals;
    rule.value      = "cancelado";
    rule.background = kRed;
    rule.whole_row  = true;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK(rules.has_row_rules());
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, kRed);
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 1).background, kRed);

    // E só na linha que casou.
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, 0u);
}

OTTER_TEST(coloring_first_matching_rule_wins) {
    // Misturar as cores de duas regras daria um tom que ninguém escolheu.
    ColorRules rules;

    ColorRule first;
    first.column     = "valor";
    first.op         = ColorOp::greater;
    first.value      = "5";
    first.background = kRed;
    rules.add(first);

    ColorRule second;
    second.column     = "valor";
    second.op         = ColorOp::greater;
    second.value      = "50";
    second.background = kBlue;
    rules.add(second);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    // 100 casa com as DUAS; a primeira vence.
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, kRed);
}

OTTER_TEST(coloring_disabled_rule_is_skipped) {
    ColorRules rules;
    ColorRule rule;
    rule.column     = "situacao";
    rule.op         = ColorOp::equals;
    rule.value      = "cancelado";
    rule.background = kRed;
    rule.enabled    = false;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, 0u);
    OTTER_CHECK(!rules.affects_column("situacao"));
}

OTTER_TEST(coloring_rule_on_a_missing_column_is_ignored) {
    // A consulta mudou e a coluna sumiu. Ignorar é o correto: apagar a regra
    // perderia o trabalho do usuário, e casar com outra coluna coloriria a
    // coisa errada.
    ColorRules rules;
    ColorRule rule;
    rule.column     = "coluna_que_nao_existe";
    rule.op         = ColorOp::is_not_null;
    rule.background = kRed;
    rules.add(rule);

    const ResultSet rs = pedidos();
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 0, 0).background, 0u);
    OTTER_CHECK_EQ(rules.color_for(rs, 0, 1).background, 0u);

    // Mas a regra CONTINUA na lista, para voltar a valer quando a coluna
    // reaparecer.
    OTTER_CHECK_EQ(rules.rules().size(), std::size_t{1});
}

// --- Operadores de texto ---------------------------------------------------------------

OTTER_TEST(coloring_text_operators_ignore_case) {
    // Quem digita "cancel" espera achar "CANCELADO". Exigir a caixa exata
    // tornaria a regra inútil na prática.
    const ResultSet rs = make_result({"s"}, {{"CANCELADO"}, {"ativo"}});

    ColorRules rules;
    ColorRule contains;
    contains.column     = "s";
    contains.op         = ColorOp::contains;
    contains.value      = "cancel";
    contains.background = kRed;
    rules.add(contains);
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 0, 0).background, kRed);
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, 0u);
}

OTTER_TEST(coloring_between_includes_both_ends) {
    const ResultSet rs = make_result({"v"}, {{"9"}, {"10"}, {"20"}, {"21"}});

    ColorRules rules;
    ColorRule rule;
    rule.column     = "v";
    rule.op         = ColorOp::between;
    rule.value      = "10";
    rule.value2     = "20";
    rule.background = kGreen;
    rules.add(rule);
    rules.prepare(rs);

    OTTER_CHECK_EQ(rules.color_for(rs, 0, 0).background, 0u);      // 9
    OTTER_CHECK_EQ(rules.color_for(rs, 1, 0).background, kGreen);  // 10
    OTTER_CHECK_EQ(rules.color_for(rs, 2, 0).background, kGreen);  // 20
    OTTER_CHECK_EQ(rules.color_for(rs, 3, 0).background, 0u);      // 21
}

// --- Gerência da lista ------------------------------------------------------------------

OTTER_TEST(coloring_remove_keeps_the_row_flag_in_sync) {
    // has_row_rules() decide se a grade varre as outras colunas ao pintar.
    // Deixá-la desatualizada faria a grade pagar esse custo para sempre.
    ColorRules rules;

    ColorRule cell;
    cell.column = "a";
    rules.add(cell);

    ColorRule row;
    row.column    = "b";
    row.whole_row = true;
    rules.add(row);

    OTTER_CHECK(rules.has_row_rules());

    rules.remove(1);
    OTTER_CHECK(!rules.has_row_rules());
    OTTER_CHECK_EQ(rules.rules().size(), std::size_t{1});
}

OTTER_TEST(coloring_operand_count_matches_the_operator) {
    // A UI usa isto para mostrar um campo, dois, ou nenhum. Errar aqui
    // pediria um valor que o operador ignora, ou esconderia um que ele exige.
    OTTER_CHECK_EQ(operand_count(ColorOp::is_null), std::size_t{0});
    OTTER_CHECK_EQ(operand_count(ColorOp::is_not_null), std::size_t{0});
    OTTER_CHECK_EQ(operand_count(ColorOp::equals), std::size_t{1});
    OTTER_CHECK_EQ(operand_count(ColorOp::between), std::size_t{2});
    OTTER_CHECK_EQ(operand_count(ColorOp::range), std::size_t{2});
}
