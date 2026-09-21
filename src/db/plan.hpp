// C-Otter -- db/plan.hpp
//
// Arvore do plano de execucao, lida de EXPLAIN (FORMAT JSON).
//
// Motivo e cuidados em docs/adr/0013-query-plan.md. O essencial: EXPLAIN
// ANALYZE EXECUTA a consulta, entao o comando e' montado aqui com transacao e
// rollback garantidos.
#pragma once

#include "base/error.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

struct PlanNode {
    // "Seq Scan", "Index Scan", "Hash Join", "Sort", "Aggregate"...
    std::string type;

    // Relacao acessada, quando o no' acessa uma.
    std::string relation;
    std::string alias;
    std::string index_name;

    // Condicoes: filtro, condicao de indice, chave de join, chave de ordenacao.
    std::string filter;
    std::string index_condition;
    std::string join_condition;
    std::string sort_keys;

    // Estimativas do planejador.
    double        startup_cost = 0.0;
    double        total_cost = 0.0;
    std::int64_t  estimated_rows = 0;
    std::int32_t  row_width = 0;

    // Medidas reais -- so' existem com ANALYZE.
    bool          has_actuals = false;
    double        actual_time = 0.0;     // ms, do ultimo loop
    std::int64_t  actual_rows = 0;
    std::int64_t  loops = 1;

    std::vector<PlanNode> children;

    // Quao errada estava a estimativa. 1.0 = exata; 50.0 = o planejador
    // esperava 50x menos linhas do que vieram.
    //
    // E' o numero que mais explica plano ruim: o planejador escolhe Nested
    // Loop porque acha que vem 1 linha, e vem 50 mil.
    [[nodiscard]] double estimation_error() const noexcept;

    // Sequential scan numa tabela e' a causa mais comum de lentidao. Nao e'
    // sempre errado -- em tabela pequena e' o certo --, mas merece destaque.
    [[nodiscard]] bool is_sequential_scan() const noexcept;

    // Custo proprio: o total menos o dos filhos. Um no' caro com filhos
    // baratos e' onde otimizar.
    [[nodiscard]] double self_cost() const noexcept;
};

struct QueryPlan {
    PlanNode root;

    // Tempo de planejamento e de execucao, quando o servidor informa.
    double planning_time = 0.0;
    double execution_time = 0.0;

    bool   analyzed = false;   // veio de EXPLAIN ANALYZE
    std::string raw_json;      // guardado para copiar/depurar

    [[nodiscard]] bool empty() const noexcept { return root.type.empty(); }

    // Maior custo total entre todos os nos, para normalizar as barras.
    [[nodiscard]] double max_total_cost() const noexcept;
};

// Monta o comando EXPLAIN.
//
// Com `analyze`, o chamador DEVE envolver em transacao e dar rollback: o
// comando executa a consulta de verdade (ADR 0013). explain_statements()
// entrega os tres comandos prontos, justamente para nao depender de quem
// chama lembrar disso.
[[nodiscard]] std::string explain_command(std::string_view sql, bool analyze,
                                          bool buffers = true);

// Os comandos a executar em sequencia. Com `analyze`, sao tres:
// BEGIN, EXPLAIN, ROLLBACK. Sem, apenas o EXPLAIN.
[[nodiscard]] std::vector<std::string> explain_statements(std::string_view sql,
                                                          bool analyze);

// Le' a arvore do JSON devolvido pelo servidor.
[[nodiscard]] Result<QueryPlan> parse_plan_json(std::string_view json);

} // namespace otter::db
