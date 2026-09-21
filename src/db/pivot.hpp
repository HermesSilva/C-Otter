// C-Otter -- db/pivot.hpp
//
// Tabela dinâmica: transpõe os valores de uma coluna em COLUNAS, agregando na
// interseção (ADR 0005).
//
//     regiao | mes | valor            regiao | jan | fev | mar
//     -------|-----|------            -------|-----|-----|----
//     sul    | jan | 100      ==>     sul    | 100 | 300 |  -
//     norte  | jan | 200              norte  | 200 |   - | 150
//     sul    | fev | 300
//     norte  | mar | 150
//
// Duas armadilhas, as duas registradas no ADR:
//
//   1. **Cardinalidade.** Pivotar por uma coluna com 5000 valores distintos
//      gera 5000 colunas. A grade não desenha, o ImGui nem aceita mais de 64,
//      e o usuário fica sem saber por quê. Há um limite, e ele é DITO.
//
//   2. **Paginação.** Pivotar a PÁGINA não é pivotar o resultado: a célula
//      (sul, jan) somaria só o que veio nas 200 linhas em memória. O mesmo
//      problema da agregação (db/aggregate.hpp), e a mesma resposta -- a
//      marca `partial` acompanha, e há `build_pivot_query()` para perguntar
//      ao servidor.
#pragma once

#include "db/aggregate.hpp"
#include "db/result_set.hpp"

#include <string>
#include <vector>

namespace otter::db {

struct PivotSpec {
    // Colunas que permanecem como LINHAS (o eixo vertical). Várias formam uma
    // chave composta, como no GROUP BY.
    std::vector<std::size_t> rows;

    // A coluna cujos VALORES viram colunas.
    std::size_t column = 0;

    // A coluna agregada na interseção, e como.
    std::size_t value = 0;
    Aggregate   function = Aggregate::sum;

    [[nodiscard]] bool empty() const noexcept { return rows.empty(); }
};

// Uma linha do resultado pivotado.
struct PivotRow {
    std::vector<std::string>    keys;    // uma por coluna de `rows`
    std::vector<AggregateValue> cells;   // uma por coluna gerada, na ordem
};

struct PivotResult {
    // Os cabeçalhos gerados, em ordem. É o que vira coluna na grade.
    std::vector<std::string> headers;

    std::vector<PivotRow> rows;

    // Totais por coluna gerada, na mesma ordem de `headers`.
    std::vector<AggregateValue> totals;

    // Verdadeiro quando o ResultSet cobria só parte do resultado -- ou seja,
    // sempre que houver paginação. A interface PRECISA dizer isso: uma célula
    // que soma 200 de 2 milhões de linhas não é a soma.
    bool partial = false;

    std::size_t rows_covered = 0;

    // A coluna pivotada tinha mais valores distintos que o limite. Os
    // excedentes ficaram de FORA, e isso não pode ser silencioso.
    bool        truncated = false;
    std::size_t distinct_values = 0;

    [[nodiscard]] bool empty() const noexcept { return rows.empty(); }
};

// Limite de colunas geradas.
//
// 48 e não 64 (o teto do ImGui numa tabela) porque as colunas de linha também
// ocupam espaço, e uma tabela com 64 colunas já é ilegível muito antes de ser
// impossível.
inline constexpr std::size_t kMaxPivotColumns = 48;

// Pivota o que está em memória.
//
// `partial` propaga para o resultado: quem chama sabe se o ResultSet é uma
// página ou o resultado inteiro, e essa informação não pode se perder.
[[nodiscard]] PivotResult pivot(const ResultSet& rs, const PivotSpec& spec,
                                bool partial,
                                std::size_t max_columns = kMaxPivotColumns);

// Reescreve a consulta para pivotar NO SERVIDOR.
//
// É a resposta honesta quando o resultado é paginado. Não usa `PIVOT` (que só
// existe no SQL Server e no Oracle): gera `GROUP BY` com um agregado
// condicional por valor -- `SUM(CASE WHEN mes = 'jan' THEN valor END)` --,
// que é padrão SQL e funciona nos dois SGBDs que o C-Otter fala.
//
// Precisa dos valores distintos, que vêm do resultado já em memória: o SQL
// não gera colunas dinamicamente. Por isso o pivot no servidor é exato para
// as colunas que ele CONHECE, e a interface avisa quando a página não viu
// todos os valores.
[[nodiscard]] std::string build_pivot_query(std::string_view sql,
                                            const ResultSet& rs,
                                            const PivotSpec& spec,
                                            const std::vector<std::string>& headers);

} // namespace otter::db
