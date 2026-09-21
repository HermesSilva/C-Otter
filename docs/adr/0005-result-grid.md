# ADR 0005 — Grade de resultados

**Data:** 2026-09-21
**Status:** Parcialmente implementado (2026-09-21)

> **Implementado:** agregação local com as 7 funções, agrupamento por múltiplas colunas,
> linha de totais, marca de resultado parcial e "calcular no servidor" (`db/aggregate.cpp`).
> **Falta:** pivot, coluna calculada, sparkline, fetch progressivo.
>
> **Formatação condicional: feita** em 2026-09-21. `src/db/coloring.cpp`, com
> 12 operadores, regra por célula ou por linha inteira, e o modo gradiente
> (mapa de calor) que interpola pela faixa da própria coluna. A UI oferece
> presets no menu do cabeçalho -- "Mapa de calor", "Marcar negativos",
> "Destacar linhas onde x = y" --, porque quem abre o menu quer o resultado,
> não escolher operador e duas cores em RGB.
>
> Três decisões que o teste fixou: comparar "10" com "9" como NÚMERO (como
> texto daria 10 < 9); NULL não casa com comparação de valor, só com `is null`
> explícito (é o que o SQL faz); e o mínimo/máximo do gradiente é calculado
> UMA vez por resultado, não por célula -- por célula seria O(n²) e a grade
> engasgaria com 200 linhas.

## Contexto

Requisito declarado: **grade soberba**, com suporte a resultados analíticos — **totais e
grupos**. Junto com o completion (ADR 0004), é o segundo pilar do produto: é onde o usuário
passa a maior parte do tempo.

Referência: `org.jkiss.dbeaver.ui.editors.data` tem **61.863 linhas** no DBeaver
(`docs/ANALYSIS.md` §1) — o segundo maior plugin. A maior parte existe para gerenciar ciclo
de vida de células SWT, custo que o modo imediato elimina.

## Decisão 1 — Colunar até a tela, sem cópia

O `ResultSet` já é colunar (ADR 0001 #3): buffer contíguo tipado + bitmap de nulos, em arena.
A grade **lê direto desse buffer**. Não há objeto por célula, nem cópia intermediária, nem
"modelo de tabela" com linhas materializadas.

Render é O(células visíveis), independente do tamanho do resultado. É o que torna 1M linhas
a 144 fps possível.

## Decisão 2 — Agregação local sobre o buffer colunar

Este é o ponto do requisito "totais e grupos", e a decisão central do ADR.

Existem dois caminhos para agregar, e o produto precisa dos **dois**:

| | Agregação local | Agregação no servidor |
|---|---|---|
| Como | Sobre o buffer já baixado | Reescreve a query com `GROUP BY` |
| Latência | Instantânea (< 50 ms para 1M linhas) | Round-trip ao SGBD |
| Correção | Exata **apenas** sobre o que foi baixado | Exata sobre a tabela inteira |
| Uso | Explorar o resultado atual | Responder a pergunta de verdade |

**A distinção precisa ser visível na interface.** Um total calculado sobre 200 mil linhas
baixadas de uma tabela com 50 milhões é uma mentira se apresentado como "o total". A grade
deve indicar claramente quando a agregação cobre apenas o conjunto carregado, e oferecer
"recalcular no servidor" com um clique.

### Agregação local — implementação

Sobre buffer colunar, agregação é varredura linear vetorizável:

```cpp
namespace otter::grid {

enum class Aggregate { count, count_distinct, sum, avg, min, max,
                       median, stddev, first, last };

struct GroupSpec {
    std::vector<ColumnIndex> group_by;      // colunas de agrupamento, em ordem
    std::vector<AggSpec>     aggregates;    // {coluna, funcao}
};

// Resultado: arvore de grupos com subtotais por nivel + total geral.
// Vive na arena do fetch -- descartado junto com o ResultSet.
class GroupTree { /* ... */ };

} // namespace otter::grid
```

Agrupamento por hash sobre as colunas-chave; agregados acumulados em uma passada. Para 1M
linhas × 3 níveis de agrupamento, alvo de **< 100 ms** em uma thread, com paralelização por
partição quando exceder.

### Recursos analíticos da grade

| Recurso | Descrição |
|---------|-----------|
| **Agrupamento hierárquico** | Arrastar colunas para a faixa de agrupamento, múltiplos níveis, expandir/colapsar |
| **Linha de totais** | Rodapé fixo com agregado por coluna, configurável por coluna |
| **Subtotais por grupo** | Linha de resumo em cada nível de agrupamento |
| **Pivot** | Transpor valores de uma coluna em colunas, com agregação na interseção |
| **Coluna calculada** | Expressão sobre outras colunas, avaliada localmente |
| **Sparkline / barra na célula** | Visualização inline proporcional ao valor |
| **Formatação condicional** | Regras de cor por faixa de valor, mapa de calor |
| **Filtro por coluna** | Local (instantâneo) ou empurrado para o servidor (`WHERE`) |
| **Ordenação** | Local sobre o buffer, ou server-side para resultado completo |
| **Congelar colunas/linhas** | Painéis fixos à esquerda e ao topo |

## Decisão 3 — Fetch progressivo, nunca bloqueante

O usuário vê as primeiras linhas imediatamente; o resto chega em background.

- Fetch em blocos (ex.: 10k linhas), com a grade renderizando o que já chegou
- Indicador de progresso e contagem parcial
- Cancelamento a qualquer momento (jobs canceláveis do `otter_base`)
- Agregação local recalculada conforme blocos chegam, com marcação de "parcial"

## Decisão 4 — Edição preserva a identidade da linha

Grade editável exige saber **qual linha do banco** cada linha da tela representa:

- Chave primária ou identificador único é obrigatório para edição
- Sem chave: grade em modo somente leitura, com aviso explícito do motivo
- Alterações ficam pendentes até commit explícito, visualmente marcadas
- `UPDATE`/`INSERT`/`DELETE` gerados com `WHERE` pela chave, **nunca** por posição
- Detecção de conflito: linha alterada por terceiro entre leitura e escrita

## Decisão 5 — Apresentação por tipo

Cada tipo tem renderização e editor próprios: texto, numérico (alinhado, com separadores do
locale), data/hora (com fuso), booleano, binário (hex), JSON (árvore dobrável), geometria,
array, LOB (carregamento sob demanda). Nulo é visualmente distinto de string vazia — confundir
os dois é um erro clássico de cliente SQL.

## Impacto na estimativa

| Item | Antes | Depois | Δ |
|------|-------|--------|---|
| Grid virtualizado (base) | 560 | 560 | — |
| **Motor de agregação e agrupamento** | — | **240** | +240 |
| **Pivot e colunas calculadas** | — | **140** | +140 |
| **Formatação condicional, sparklines** | — | **90** | +90 |
| Fetch progressivo e cancelamento | (em db) | **80** | +80 |
| **Total do projeto** | ~13.850 | **≈ 14.400 h/h** | **+550** |

## Riscos

| Risco | Mitigação |
|-------|-----------|
| Usuário confunde total local com total real | Marcação visual explícita + ação "recalcular no servidor" |
| Agregação de 1M linhas trava a UI | Roda em worker, com resultado parcial progressivo |
| Memória: agrupar 1M linhas cria árvore grande | Árvore na arena; limite configurável de grupos |
| Pivot com alta cardinalidade gera milhares de colunas | Limite com aviso; sugere agrupar antes |

## Referências

- ADR 0001 #3 — `ResultSet` colunar
- `docs/ANALYSIS.md` §1 — `ui.editors.data` (61.863 linhas no DBeaver)
- `docs/PLAN.md` §4 — metas de performance da grade
