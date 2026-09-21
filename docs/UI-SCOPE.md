# Escopo da interface — o que falta estruturalmente

**Criado em 2026-09-21**, depois de uma comparação lado a lado entre o DBeaver 25.3.4 e o
C-Otter que deixou o problema evidente: **o C-Otter tem uma interface mínima, e o alvo é uma
interface completa.**

## O que a comparação mostrou

| No DBeaver | No C-Otter |
|---|---|
| 8 menus (File, Edit, Navigate, Search, SQL Editor, Database, Window, Help) | 4 menus rasos |
| 2 barras de ferramentas densas | nenhuma |
| 6 abas de editor abertas, cada uma com sua conexão | 1 editor fixo |
| Catálogo visual de ~50 drivers, com 10 categorias e busca | diálogo de 5 campos |
| Assistente `< Back / Next > / Finish` + `Test Connection` | botão Conectar |
| 6 conexões na árvore, com filtro por nome | 1 conexão |
| `Commit` / `Rollback` / modo de transação na barra | nada |
| Calha do editor com marcador por statement | numeração apenas |

## Medição do que isso representa

Extraído do repositório do DBeaver por `tools/map_dbeaver.py`. O mapa completo, com **cada
comando, atalho, diálogo e página nomeados um a um**, está em
[`DBEAVER-MAP.md`](DBEAVER-MAP.md) (845 linhas).

| Elemento | Quantidade |
|----------|-----------|
| Comandos | **281** |
| Atalhos de teclado | **147** |
| Handlers de comando (XML) | **350** |
| Handlers (classes Java) | **279** |
| **Managers de objeto (DDL)** | **309** |
| Editores (classes Java) | **126** |
| Diálogos | **151** |
| Assistentes | **112** |
| Páginas de preferências (XML) | **109** |
| Contribuições de menu/toolbar | **110** |
| Pontos de extensão | **85** |
| Actions | **84** |
| **Value handlers (tipos de dado)** | **81** |
| Views e painéis | **77** |
| Editores (XML) | **58** |
| Menus declarados | **34** |
| Barras de ferramentas | **6** |

Duas categorias que eu não havia contado e que mudam a escala do problema:

- **309 managers de objeto** — cada tipo de objeto de cada SGBD (tabela, coluna, índice,
  constraint, view, sequence, trigger, função, role…) tem um manager que gera o DDL de
  criar, alterar e remover. É o que faz o DBeaver ser um *administrador* de banco, não só
  um executor de queries.
- **81 value handlers** — um por tipo de dado, cuidando de exibição, edição e conversão:
  `numeric`, `timestamptz`, `interval`, `jsonb`, arrays, geometria, LOB.

Comandos por categoria: resultados 63, SQL 56, database 54, navigator 37.

## Onde meu inventário anterior errou

`ELEMENTS.md` reporta **~63% de cobertura**. Esse número está **certo para o que mede e
errado como indicador de progresso**: ele conta os elementos que o C-Otter *tem*, não os que
*precisa ter*.

Um diálogo de conexão com 5 campos conta como "10 elementos prontos". O equivalente no
DBeaver é um assistente de múltiplas páginas com catálogo de drivers, teste de conexão,
túnel SSH, SSL, pool, propriedades de driver e scripts de inicialização — algo como 80
elementos.

**Medir a própria interface contra si mesma produz um número que sobe enquanto o produto não
se aproxima do alvo.** É o erro que esta comparação expôs.

### Correção da métrica

Com o mapa completo, o número real é bem menor do que qualquer estimativa anterior:

| Métrica | DBeaver | C-Otter | Cobertura |
|---------|---------|---------|-----------|
| **Comandos** | 281 | **7** | **2,5%** |
| **Atalhos de teclado** | 147 | **6** | **4,1%** |
| Diálogos | 151 | 2 | 1,3% |
| Páginas de preferências | 56 | 0 | 0% |
| Assistentes | 112 | 0 | 0% |
| Barras de ferramentas | 6 | 0 | 0% |
| Managers de objeto (DDL) | 309 | 0 | 0% |
| Value handlers (tipos) | 81 | 0 | 0% |

**~63% → ~2,5%.** A diferença não é detalhe de contagem: é a distância entre "os botões que
existem funcionam" e "a ferramenta faz o que um cliente de banco precisa fazer".

`ELEMENTS.md` continua útil como guia operacional — "este botão funciona?" — mas deixa de
ser indicador de progresso. Para isso, use este arquivo e `PARITY.md`.

## Lacunas estruturais, por ordem de impacto

### 1. Barra de ferramentas — ausente por completo

O DBeaver expõe na barra o que mais se usa: executar, commit, rollback, modo de transação,
conexão ativa, banco ativo, busca. São ações de um clique que hoje exigem menu ou não
existem.

**Bloqueia:** fluxo de trabalho transacional inteiro.

### 2. Múltiplas abas de editor — ausente

Seis scripts abertos ao mesmo tempo, cada um com sua conexão e seu resultado, é o modo normal
de trabalho de quem usa um cliente SQL. O C-Otter tem um único editor fixo.

**Bloqueia:** trabalhar em mais de uma coisa; comparar resultados.

### 3. Múltiplas conexões — ausente

A árvore do DBeaver mostra 6 servidores simultâneos. O `Session` do C-Otter é uma conexão
única (`std::unique_ptr<db::Holt>`), o que exige refatoração para `vector<Session>` com uma
ativa.

**Bloqueia:** o caso de uso mais comum — comparar produção e homologação.

### 4. Diálogo de conexão real — mínimo

Hoje são 5 campos. Falta: seleção de SGBD por catálogo, categorias, busca, teste de conexão,
abas de propriedades (driver, SSH, SSL, pool, inicialização), salvar e nomear.

### 5. Menus rasos

Quatro menus com ~14 itens, contra oito menus com centenas. Faltam inteiros: **Navigate**
(ir para objeto, voltar/avançar), **Search** (busca de metadados, busca em dados),
**Database** (transações, tarefas, gerar DDL), **Window** (layout, painéis, preferências).

### 6. Preferências — inexistente

56 páginas de preferências no DBeaver. O C-Otter não tem nenhuma, nem persiste configuração.

### 7. Menus de contexto — inexistentes

Botão direito no Navigator, na grade e no editor é como se acessa a maior parte das ações do
DBeaver. Nenhum existe.

## Impacto na estimativa

`EFFORT.md` orçava `otter_ui` em 2.400 h/h. Esse número foi calculado a partir de uma
proporção de linhas de código, **não de um inventário de elementos** — e subestima.

| Item | h/h |
|------|-----|
| Barra de ferramentas (2 linhas, estado dinâmico) | 120 |
| Múltiplas abas de editor + ciclo de vida | 180 |
| Múltiplas conexões simultâneas (refatorar `Session`) | 160 |
| Diálogo de conexão completo (catálogo, abas, teste) | 240 |
| Menus completos (8 menus, ~200 itens) | 200 |
| Menus de contexto (Navigator, grade, editor) | 160 |
| Sistema de preferências + persistência | 220 |
| Diálogos de apoio (~40 dos 151 no escopo da v1) | 480 |
| Assistentes (import, export, nova conexão, gerar DDL) | 320 |
| **Subtotal de interface** | **+1.880** |

Duas categorias que o mapa revelou e que **não estavam orçadas em lugar nenhum**:

| Item | h/h | Observação |
|------|-----|------------|
| **Managers de objeto (DDL)** | **900** | ~60 dos 309, cobrindo os tipos do PostgreSQL: criar/alterar/remover tabela, coluna, índice, constraint, view, sequence, trigger, função |
| **Value handlers** | **420** | ~25 dos 81: numeric, timestamptz, interval, jsonb, arrays, geometria, LOB — exibição, edição e conversão de cada tipo |
| **Subtotal** | **+1.320** | |

| Total | h/h |
|-------|-----|
| **Adicional sobre o orçado** | **+3.200** |
| `otter_ui` revisado | 2.400 → **4.280** |
| Novo módulo `otter_ddl` | — → **900** |
| **Total do projeto** | ~16.800 → **≈ 20.000 h/h** |

Com 4–5 desenvolvedores, o calendário vai de ~2,5–2,8 para **~3,5–4 anos**.

Ancoragem: 20.000 h/h ≈ 12 anos-pessoa. O DBeaver tem mais de uma década de trabalho
coletivo — a ordem de grandeza agora bate, o que é sinal de que a estimativa parou de ser
otimista.

## Reordenação do plano

A ordem anterior priorizava profundidade (transações, grade editável). A comparação mostra
que **a estrutura da interface precisa vir antes** — sem abas, barra de ferramentas e
múltiplas conexões, as funcionalidades profundas não têm onde morar.

| # | Entrega | Por quê |
|---|---------|---------|
| 1 | **Múltiplas abas de editor** | Sem isso nada mais escala; cada funcionalidade nova disputa o único editor |
| 2 | **Barra de ferramentas + transações** | Commit/rollback precisa de lugar visível e permanente |
| 3 | **Múltiplas conexões** | Refatorar `Session`; a árvore e as abas dependem disso |
| 4 | **Menus de contexto** | É como se acessa a maioria das ações |
| 5 | **Diálogo de conexão completo** | Catálogo de drivers, abas, salvar |
| 6 | **Preferências + persistência** | Layout, conexões, histórico entre sessões |
| 7 | Grade editável, filtros, export | Profundidade, depois da estrutura |

## Nota sobre o que isso não muda

As decisões de arquitetura seguem válidas: protocolo nativo, formato colunar, escrita única,
completion com escopo. O que muda é a **quantidade de superfície de interface** a construir
sobre elas — e o reconhecimento de que ela foi subestimada.
