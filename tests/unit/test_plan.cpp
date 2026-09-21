// Plano de execucao (ADR 0013).
//
// O teste que mais importa aqui e' o do ROLLBACK: EXPLAIN ANALYZE EXECUTA a
// consulta, entao um "explicar" sobre um UPDATE alteraria as linhas. Os
// demais cobrem a leitura da arvore e o calculo do erro de estimativa, que e'
// o numero que mais explica plano ruim.
#include "test_main.hpp"

#include "db/plan.hpp"

#include <string>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// Saida real de EXPLAIN (FORMAT JSON), recortada.
constexpr const char* kSimplePlan = R"JSON([
  {
    "Plan": {
      "Node Type": "Seq Scan",
      "Relation Name": "evento_volume",
      "Alias": "evento_volume",
      "Startup Cost": 0.00,
      "Total Cost": 45833.00,
      "Plan Rows": 2000000,
      "Plan Width": 45,
      "Filter": "(categoria = 3)"
    }
  }
])JSON";

constexpr const char* kJoinPlan = R"JSON([
  {
    "Plan": {
      "Node Type": "Hash Join",
      "Hash Cond": "(p.cliente_id = c.cliente_id)",
      "Startup Cost": 1.07,
      "Total Cost": 2.15,
      "Plan Rows": 4,
      "Plan Width": 40,
      "Actual Total Time": 0.041,
      "Actual Rows": 4,
      "Actual Loops": 1,
      "Plans": [
        {
          "Node Type": "Seq Scan",
          "Relation Name": "pedido",
          "Alias": "p",
          "Startup Cost": 0.00,
          "Total Cost": 1.04,
          "Plan Rows": 4,
          "Plan Width": 12,
          "Actual Total Time": 0.008,
          "Actual Rows": 4,
          "Actual Loops": 1
        },
        {
          "Node Type": "Index Scan",
          "Relation Name": "cliente",
          "Index Name": "cliente_pkey",
          "Index Cond": "(cliente_id = 1)",
          "Startup Cost": 0.15,
          "Total Cost": 1.02,
          "Plan Rows": 1,
          "Plan Width": 36,
          "Actual Total Time": 0.004,
          "Actual Rows": 3,
          "Actual Loops": 1
        }
      ]
    },
    "Planning Time": 0.183,
    "Execution Time": 0.077
  }
])JSON";

} // namespace

// --- O que impede estrago ----------------------------------------------------

OTTER_TEST(plan_analyze_is_wrapped_in_a_rollback) {
    // EXPLAIN ANALYZE UPDATE altera as linhas de verdade. Sem transacao e
    // rollback, "explicar" viraria "executar" -- o pior defeito possivel
    // neste recurso.
    const std::vector<std::string> statements =
        explain_statements("UPDATE cliente SET credito = 0", /*analyze=*/true);

    OTTER_CHECK_EQ(statements.size(), std::size_t{3});
    OTTER_CHECK_EQ(statements.front(), std::string{"BEGIN"});
    OTTER_CHECK_EQ(statements.back(), std::string{"ROLLBACK"});
    OTTER_CHECK(has(statements[1], "EXPLAIN"));
    OTTER_CHECK(has(statements[1], "ANALYZE"));
    OTTER_CHECK(has(statements[1], "UPDATE cliente SET credito = 0"));
}

OTTER_TEST(plan_without_analyze_needs_no_transaction) {
    // EXPLAIN simples nao executa nada: abrir transacao seria custo inutil e
    // mudaria o estado transacional da sessao do usuario.
    const std::vector<std::string> statements =
        explain_statements("SELECT * FROM cliente", /*analyze=*/false);

    OTTER_CHECK_EQ(statements.size(), std::size_t{1});
    OTTER_CHECK(has(statements[0], "EXPLAIN"));
    OTTER_CHECK(!has(statements[0], "ANALYZE"));
}

OTTER_TEST(plan_always_asks_for_json) {
    // O texto do EXPLAIN mudou de forma entre versoes; o JSON e' estavel
    // desde a 9.0 e ja' vem em arvore.
    OTTER_CHECK(has(explain_command("SELECT 1", false), "FORMAT JSON"));
    OTTER_CHECK(has(explain_command("SELECT 1", true), "FORMAT JSON"));
}

// --- Leitura da arvore -------------------------------------------------------

OTTER_TEST(plan_reads_a_single_node) {
    const auto plan = parse_plan_json(kSimplePlan);
    OTTER_CHECK(plan.has_value());

    const PlanNode& root = plan->root;
    OTTER_CHECK_EQ(root.type, std::string{"Seq Scan"});
    OTTER_CHECK_EQ(root.relation, std::string{"evento_volume"});
    OTTER_CHECK_EQ(root.filter, std::string{"(categoria = 3)"});
    OTTER_CHECK_EQ(root.estimated_rows, std::int64_t{2000000});
    OTTER_CHECK(root.total_cost > 45000.0);

    // Sem ANALYZE nao ha' medida real -- e a flag diz isso, em vez de um
    // zero que seria confundido com "mediu zero".
    OTTER_CHECK(!root.has_actuals);
    OTTER_CHECK(!plan->analyzed);
}

OTTER_TEST(plan_reads_a_tree_with_children) {
    const auto plan = parse_plan_json(kJoinPlan);
    OTTER_CHECK(plan.has_value());

    OTTER_CHECK_EQ(plan->root.type, std::string{"Hash Join"});
    OTTER_CHECK_EQ(plan->root.join_condition,
                   std::string{"(p.cliente_id = c.cliente_id)"});
    OTTER_CHECK_EQ(plan->root.children.size(), std::size_t{2});

    OTTER_CHECK_EQ(plan->root.children[0].type, std::string{"Seq Scan"});
    OTTER_CHECK_EQ(plan->root.children[1].type, std::string{"Index Scan"});
    OTTER_CHECK_EQ(plan->root.children[1].index_name,
                   std::string{"cliente_pkey"});

    OTTER_CHECK(plan->analyzed);
    OTTER_CHECK(plan->planning_time > 0.0);
    OTTER_CHECK(plan->execution_time > 0.0);
}

OTTER_TEST(plan_marks_sequential_scans) {
    const auto plan = parse_plan_json(kSimplePlan);
    OTTER_CHECK(plan.has_value());
    OTTER_CHECK(plan->root.is_sequential_scan());

    const auto join = parse_plan_json(kJoinPlan);
    OTTER_CHECK(!join->root.is_sequential_scan());          // Hash Join
    OTTER_CHECK(join->root.children[0].is_sequential_scan()); // Seq Scan
    OTTER_CHECK(!join->root.children[1].is_sequential_scan()); // Index Scan
}

// --- Erro de estimativa ------------------------------------------------------

OTTER_TEST(plan_computes_the_estimation_error) {
    const auto plan = parse_plan_json(kJoinPlan);
    OTTER_CHECK(plan.has_value());

    // O Index Scan estimou 1 linha e trouxe 3: erro de 3x. E' o numero que
    // explica por que o planejador escolheu este plano.
    const PlanNode& index_scan = plan->root.children[1];
    OTTER_CHECK(index_scan.estimation_error() > 2.5);
    OTTER_CHECK(index_scan.estimation_error() < 3.5);

    // O Seq Scan acertou: 4 estimadas, 4 reais.
    OTTER_CHECK(plan->root.children[0].estimation_error() < 1.1);
}

OTTER_TEST(plan_estimation_error_accounts_for_loops) {
    // actual_rows e' POR LOOP. Um Nested Loop com 1000 iteracoes de 1 linha
    // produz 1000 linhas; comparar sem multiplicar faria parecer exato.
    const auto plan = parse_plan_json(R"JSON([{"Plan": {
        "Node Type": "Index Scan",
        "Plan Rows": 10,
        "Total Cost": 1.0,
        "Actual Total Time": 0.5,
        "Actual Rows": 2,
        "Actual Loops": 1000
    }}])JSON");

    OTTER_CHECK(plan.has_value());
    // 2 x 1000 = 2000 reais contra 10 estimadas: erro de 200x.
    OTTER_CHECK(plan->root.estimation_error() > 150.0);
}

OTTER_TEST(plan_estimation_error_is_one_without_analyze) {
    const auto plan = parse_plan_json(kSimplePlan);
    OTTER_CHECK(plan.has_value());

    // Sem medida real nao ha' erro a calcular; 1.0 significa "sem
    // divergencia conhecida", nao "estimativa perfeita".
    OTTER_CHECK_EQ(plan->root.estimation_error(), 1.0);
}

// --- Custo -------------------------------------------------------------------

OTTER_TEST(plan_self_cost_excludes_children) {
    const auto plan = parse_plan_json(kJoinPlan);
    OTTER_CHECK(plan.has_value());

    // O Hash Join custa 2.15 no total, e o filho mais caro custa 1.04. O que
    // o proprio no' acrescenta e' a diferenca -- e' onde otimizar.
    const double self = plan->root.self_cost();
    OTTER_CHECK(self > 1.0);
    OTTER_CHECK(self < 1.2);
}

OTTER_TEST(plan_max_cost_walks_the_whole_tree) {
    const auto plan = parse_plan_json(kJoinPlan);
    OTTER_CHECK(plan.has_value());
    OTTER_CHECK(plan->max_total_cost() > 2.0);
}

// --- Entrada ruim ------------------------------------------------------------

OTTER_TEST(plan_rejects_output_without_a_plan_object) {
    // Um EXPLAIN que falhou, ou JSON de outra coisa.
    OTTER_CHECK(!parse_plan_json("[{}]").has_value());
    OTTER_CHECK(!parse_plan_json("{\"erro\": 1}").has_value());
    OTTER_CHECK(!parse_plan_json("nao e json").has_value());
}
