#include "db/plan.hpp"

#include "base/json.hpp"

#include <algorithm>
#include <cmath>

namespace otter::db {
namespace {

// O JSON do EXPLAIN usa chaves com espaco e maiuscula: "Node Type",
// "Startup Cost", "Plan Rows". Nomes assim aparecem literais no codigo.
PlanNode read_node(const json::Value& value) {
    PlanNode node;

    node.type       = std::string(value["Node Type"].as_string());
    node.relation   = std::string(value["Relation Name"].as_string());
    node.alias      = std::string(value["Alias"].as_string());
    node.index_name = std::string(value["Index Name"].as_string());

    node.filter          = std::string(value["Filter"].as_string());
    node.index_condition = std::string(value["Index Cond"].as_string());

    // A condicao de join tem nome diferente por tipo de join: Hash Cond,
    // Merge Cond, Join Filter. Pega a primeira que existir.
    for (const char* key : {"Hash Cond", "Merge Cond", "Join Filter",
                            "Recheck Cond"}) {
        const std::string_view text = value[key].as_string();
        if (!text.empty()) { node.join_condition = std::string(text); break; }
    }

    // Sort Key e' um array de expressoes.
    for (const json::Value& key : value["Sort Key"].as_array()) {
        if (!node.sort_keys.empty()) node.sort_keys += ", ";
        node.sort_keys += std::string(key.as_string());
    }

    node.startup_cost   = value["Startup Cost"].as_number();
    node.total_cost     = value["Total Cost"].as_number();
    node.estimated_rows = value["Plan Rows"].as_int();
    node.row_width      = static_cast<std::int32_t>(value["Plan Width"].as_int());

    // Campos de ANALYZE. Ausentes num EXPLAIN simples -- por isso a flag, e
    // nao um zero que seria confundido com "mediu zero".
    if (value["Actual Total Time"].kind() != json::Kind::null) {
        node.has_actuals = true;
        node.actual_time = value["Actual Total Time"].as_number();
        node.actual_rows = value["Actual Rows"].as_int();
        node.loops       = std::max<std::int64_t>(1, value["Actual Loops"].as_int());
    }

    for (const json::Value& child : value["Plans"].as_array()) {
        node.children.push_back(read_node(child));
    }
    return node;
}

void collect_max_cost(const PlanNode& node, double& maximum) {
    maximum = std::max(maximum, node.total_cost);
    for (const PlanNode& child : node.children) collect_max_cost(child, maximum);
}

} // namespace

double PlanNode::estimation_error() const noexcept {
    if (!has_actuals) return 1.0;

    // actual_rows e' por loop; o total e' rows * loops. Comparar sem
    // multiplicar faria um Nested Loop com 1000 iteracoes parecer exato.
    const double actual = static_cast<double>(actual_rows) *
                          static_cast<double>(loops);
    const double estimated = static_cast<double>(estimated_rows);

    if (estimated <= 0.0 && actual <= 0.0) return 1.0;

    // Zero de um lado: usa 1 para nao dividir por zero e ainda refletir a
    // ordem de grandeza do erro.
    const double a = std::max(actual, 1.0);
    const double e = std::max(estimated, 1.0);
    return a > e ? a / e : e / a;
}

bool PlanNode::is_sequential_scan() const noexcept {
    return type == "Seq Scan";
}

double PlanNode::self_cost() const noexcept {
    double children_cost = 0.0;
    for (const PlanNode& child : children) {
        children_cost = std::max(children_cost, child.total_cost);
    }
    return std::max(0.0, total_cost - children_cost);
}

double QueryPlan::max_total_cost() const noexcept {
    double maximum = 0.0;
    collect_max_cost(root, maximum);
    return maximum;
}

std::string explain_command(std::string_view sql, bool analyze, bool buffers) {
    std::string options = "FORMAT JSON";
    if (analyze) {
        options = "ANALYZE, " + options;
        if (buffers) options = "BUFFERS, " + options;
    }
    return "EXPLAIN (" + options + ") " + std::string(sql);
}

std::vector<std::string> explain_statements(std::string_view sql, bool analyze) {
    if (!analyze) {
        // EXPLAIN simples nao executa nada: nao precisa de transacao.
        return {explain_command(sql, /*analyze=*/false)};
    }

    // ANALYZE executa a consulta de verdade. Envolver em transacao e dar
    // ROLLBACK e' o que impede um EXPLAIN ANALYZE UPDATE de alterar linhas
    // (ADR 0013).
    return {
        "BEGIN",
        explain_command(sql, /*analyze=*/true),
        "ROLLBACK",
    };
}

Result<QueryPlan> parse_plan_json(std::string_view json_text) {
    auto parsed = json::parse(json_text);
    if (!parsed) {
        return std::unexpected(
            parsed.error().with_context("reading the EXPLAIN output"));
    }

    // O servidor devolve um array com um objeto: [ { "Plan": {...} } ].
    const json::Value& entry =
        parsed->is_array() ? parsed->at(0) : *parsed;

    const json::Value& plan = entry["Plan"];
    if (plan.is_null()) {
        return fail(Errc::parse_error, "no \"Plan\" object in the EXPLAIN output");
    }

    QueryPlan result;
    result.root           = read_node(plan);
    result.planning_time  = entry["Planning Time"].as_number();
    result.execution_time = entry["Execution Time"].as_number();
    result.analyzed       = result.root.has_actuals;
    result.raw_json       = std::string(json_text);
    return result;
}

} // namespace otter::db
