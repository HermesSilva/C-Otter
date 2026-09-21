# ADR 0008 — Debug e observabilidade

**Data:** 2026-09-21
**Status:** Proposto

## Contexto

Requisito: **debug soberbo**, tão rico quanto o suportado por cada SGBD, tendo o **SQL
Anywhere** como referência de qualidade.

São duas coisas distintas que costumam ser confundidas:

1. **Observabilidade do C-Otter** — enxergar o que a ferramenta faz (queries emitidas,
   tempo, memória, estado da conexão). Barata e útil desde já.
2. **Debugger de SQL procedural** — breakpoints e step dentro de stored procedures no
   servidor. Cara e dependente de suporte do SGBD.

## Referência: o que torna o SQL Anywhere a melhor da categoria

| Recurso | Descrição |
|---------|-----------|
| Breakpoints com **condição** | Para apenas quando a expressão é verdadeira |
| Step into / over / out | Navegação linha a linha |
| **Watch e edição de variáveis** | Inspecionar *e alterar* valores durante a parada |
| Variáveis de linha (`OLD`/`NEW`) | Em triggers, inspecionáveis e modificáveis |
| **Executar query no breakpoint** | O diferencial real: consultar temp tables e base tables no meio da execução |
| Ver plano de execução parado | Diagnóstico sem sair da pausa |
| Call stack | Cadeia de chamadas entre procedures |

O item decisivo é **executar query durante a pausa**. Sem isso, o debugger vira um
visualizador de variáveis; com isso, vira investigação real.

## Decisão 1 — Observabilidade do C-Otter (Fases 1–2)

Construída desde o início, porque também acelera o próprio desenvolvimento:

| Painel | Conteúdo |
|--------|----------|
| **Console de log** | Níveis, filtro, origem (`otter_db`, `otter_sql`, driver), copiável |
| **Inspetor de queries** | Toda query emitida, inclusive as internas de metadados — com SQL, parâmetros, duração, linhas, erro. Espelha o Query Manager do DBeaver |
| **Plano de execução** | `EXPLAIN (ANALYZE, BUFFERS)` renderizado como árvore, com custo e tempo por nó |
| **Métricas** | Frame time, memória da arena, bytes reservados vs. usados, conexões ativas, cache do Pocket Rock |
| **Inspetor de `ResultSet`** | Tipo e bitmap de nulos por coluna, bytes por buffer — valida a decisão colunar (ADR 0001 #3) |
| **Diagnóstico de conexão** | Estado, transação aberta, `search_path`, encoding, versão do servidor |

**Toda query interna é visível.** Se o C-Otter consulta `pg_catalog` para popular o
Navigator, isso aparece no inspetor. Ferramenta que esconde o que faz é difícil de confiar e
de depurar.

## Decisão 2 — Debugger de SQL procedural (Fase 4+)

Escopo por SGBD, em ordem de viabilidade:

| SGBD | Mecanismo | Viabilidade |
|------|-----------|-------------|
| **PostgreSQL** | Extensão `pldbgapi` (pgAdmin usa) | **Boa** — é a implementação de referência |
| **SQL Server** | T-SQL debugger foi **removido** no SSMS 18+ | Limitada — alternativa é log/`PRINT` |
| **Oracle** | `DBMS_DEBUG_JDWP` | Boa, exige privilégio `DEBUG CONNECT SESSION` |
| **MySQL/MariaDB** | Sem suporte nativo | Inviável — só instrumentação manual |
| **SQLite** | Sem procedures | Não se aplica |

Realidade a declarar: **o debugger de PL/pgSQL depende de extensão instalada no servidor**
(`pldbgapi`), que raramente existe em produção. Não é algo que o C-Otter possa garantir
sozinho.

### Alvo de paridade com SQL Anywhere (PostgreSQL primeiro)

| Recurso | Suportado por `pldbgapi` |
|---------|-------------------------|
| Breakpoints por linha | Sim |
| Step into / over / continue | Sim |
| Watch de variáveis | Sim (leitura) |
| Editar variável na pausa | Parcial |
| Call stack | Sim |
| **Executar query na pausa** | **Não** — a sessão está bloqueada no breakpoint |
| Breakpoint condicional | Não nativamente — avaliável no cliente |

**Conclusão honesta:** a paridade plena com o SQL Anywhere **não é alcançável** em
PostgreSQL, porque o recurso mais valioso — consultar o banco durante a pausa — exige suporte
do servidor que o `pldbgapi` não oferece. Podemos mitigar com uma segunda conexão, mas ela
não enxerga as temp tables da sessão pausada.

Prometer "debug tão rico quanto SQL Anywhere" para PostgreSQL seria falso. O que dá para
prometer é: **o melhor que cada SGBD permite**, com a limitação exposta na interface.

## Decisão 3 — Diagnóstico local, que não depende do servidor

Aqui não há teto imposto pelo SGBD, e é onde o C-Otter pode de fato se destacar:

- **Erro apontado no editor** — posição exata via `squiggle`, com a mensagem do servidor
  mapeada para a linha e coluna do statement
- **Explicação de erro** — código do SGBD traduzido para linguagem clara, com correção
  sugerida (camada de IA do ADR 0004, opcional)
- **Preview de impacto** — antes de `UPDATE`/`DELETE`, roda o `SELECT` equivalente e mostra
  quantas linhas seriam afetadas
- **Histórico com diff** — o que mudou entre duas execuções da mesma query
- **Timeline de transação** — o que está aberto, há quanto tempo, quais locks

## Impacto na estimativa

| Item | h/h |
|------|-----|
| Observabilidade (console, inspetor de queries, métricas) | 180 |
| Plano de execução como árvore | 120 |
| Diagnóstico local (erro no editor, preview de impacto) | 160 |
| Debugger PL/pgSQL via `pldbgapi` | 320 |
| **Total adicional** | **780 h/h** |
| **Total do projeto** | ~14.250 → **≈ 15.000 h/h** |

## Referências

- [The SQL Anywhere debugger](https://dcx.sap.com/sa160/en/dbusage/ug-debugging.html)
- [Transact-SQL Debugger](https://learn.microsoft.com/en-us/sql/ssdt/debugger/transact-sql-debugger) — removido no SSMS 18+
- ADR 0004 — camada de IA para explicação de erro
