# C-Otter — Plano de implementação

Premissas fechadas com o usuário:

- **Escopo:** núcleo + 5 drivers principais + UI. Fora: ERD, AI, GIS, dashboards, ~40 drivers de nicho.
- **Linguagem:** C++20, zero dependências no núcleo.
- **UI:** Dear ImGui (modo imediato).
- **Drivers:** bibliotecas nativas de cliente.
- **Plataformas:** Windows + Linux, CMake + MSVC/Clang.

Ambiente verificado: i9-14900K (28 threads), 64 GB RAM, VS 2022 BuildTools + VS 18 Community,
CMake 4.2.0, Git. **Ninja ausente** — instalar na Fase 0.

---

## 1. Arquitetura em camadas

A regra estrutural do DBeaver que se mantém: **o núcleo não conhece a UI**. Aqui isso é
verificável pelo linker — nenhum alvo abaixo de `otter_ui` pode linkar ImGui.

```
┌───────────────────────────────────────────────────────────┐
│  otter_app        binário: raft, janelas, atalhos, config │
├───────────────────────────────────────────────────────────┤
│  otter_ui         ImGui: grid, editor SQL, navigator      │  ← única camada com ImGui
├───────────────────────────────────────────────────────────┤
│  otter_transfer   import/export CSV, SQL, Parquet-lite    │
│  otter_registry   holts (datasources), credenciais, prefs │
├───────────────────────────────────────────────────────────┤
│  otter_sql        lexer, parser, semântica, formatter     │
│  otter_meta       modelo de estrutura + pocket rock cache │
├───────────────────────────────────────────────────────────┤
│  otter_db         abstração de conexão/execução/resultado │
│     ├ drv_postgres (libpq)      ├ drv_sqlite (embarcado)  │
│     ├ drv_mysql (libmariadb)    ├ drv_odbc (MSSQL, genér.)│
│     └ drv_oracle (OCI, opcional)                          │
├───────────────────────────────────────────────────────────┤
│  otter_base       platform, io, threads, arena, log, utf8 │
└───────────────────────────────────────────────────────────┘
```

Dependências apontam só para baixo. `otter_base` não depende de nada além da libc++.

## 2. Decisões de design do núcleo

### 2.1 `otter_base`

- **Arena allocator** por unidade de trabalho (um fetch, uma análise de query). Resultado:
  nenhuma alocação por célula. É o substituto direto da pressão de GC do Java.
- **UTF-8 sempre** internamente. `std::u8string` não; `std::string` com invariante de UTF-8 e
  utilitários de iteração por codepoint. Conversão só na fronteira de driver e de OS.
- **Thread pool** de tamanho fixo + fila de jobs com cancelamento cooperativo — equivale a
  `DBRProgressMonitor`/`AbstractJob` do DBeaver. Toda operação de I/O de banco roda fora do
  thread de UI, sem exceção.
- **Camada de OS** fina: arquivo, mmap, socket, dynamic loading, paths. Duas implementações
  (`win32.cpp`, `posix.cpp`) atrás de um header único.
- **Erros:** `std::expected<T, Error>` no núcleo. Exceções só na fronteira da UI. Sem exceções
  atravessando o limite de driver.

### 2.2 `otter_db` — a substituição do JDBC

O contrato é definido pelo que o núcleo precisa, não pelo que o JDBC oferece:

```cpp
namespace otter::db {

class Holt;        // conexão (datasource)
class Float;       // sessão de execução sobre um Holt
class Statement;   // query preparada
class ResultSet;   // resultado colunar, não linha-a-linha

// Driver: interface virtual pura, auto-registrada estaticamente
class Driver {
public:
    virtual ~Driver() = default;
    virtual std::string_view id() const = 0;
    virtual Capabilities capabilities() const = 0;
    virtual std::expected<std::unique_ptr<Holt>, Error> connect(const ConnConfig&) = 0;
    virtual const Dialect& dialect() const = 0;
    virtual MetadataReader& metadata() = 0;
};

} // namespace otter::db
```

**`ResultSet` é colunar.** Cada coluna é um buffer contíguo tipado + bitmap de nulos, dentro
da arena do fetch. É a decisão de performance mais importante do projeto: o grid lê direto
desse buffer sem cópia nem boxing, e o export para CSV/Parquet fica trivial.

`Capabilities` é bitmask: transações, savepoints, DDL transacional, cursores server-side,
fetch binário, múltiplos result sets, arrays, LOBs por stream.

### 2.3 `otter_meta` — modelo de estrutura

As 117 interfaces `DBS*` do DBeaver colapsam. Em vez de uma hierarquia de tipos por espécie
de objeto, um nó de metadados com discriminante:

```cpp
enum class ObjKind { Catalog, Schema, Table, View, Column, Index, ForeignKey,
                     Procedure, Sequence, Trigger, Type, /* ... */ };

struct MetaNode {
    ObjKind              kind;
    std::string          name;
    MetaNode*            parent;
    std::vector<MetaNode*> children;   // lazy
    PropertyBag          props;        // atributos específicos do SGBD
    LoadState            state;        // NotLoaded | Loading | Loaded | Error
};
```

Carregamento **lazy** — navegar até uma tabela não pode disparar leitura do catálogo inteiro.
O **Pocket Rock** é o cache persistente desses nós em disco, com invalidação por versão do
servidor, para que reabrir o app não recarregue metadados.

### 2.4 `otter_sql`

Escrito do zero, em quatro estágios:

1. **Lexer** dirigido por tabela de dialeto (keywords, regras de quoting, prefixos de
   comentário, delimitador de statement).
2. **Splitter de script** — separa statements respeitando strings, blocos `BEGIN/END`,
   `$$` do Postgres e `DELIMITER` do MySQL. Precisa ser robusto antes de qualquer parser.
3. **Parser recursive-descent** produzindo AST, tolerante a erro (precisa parsear texto
   incompleto enquanto o usuário digita).
4. **Análise semântica** — resolve nomes contra o `otter_meta` para alimentar autocomplete,
   realce e navegação. É o equivalente às 20k linhas de `semantics` do DBeaver e o subsistema
   mais caro depois do editor.

O `Dialect` é dado, não código: tabela por SGBD com keywords, funções, caracteres de quoting,
sensibilidade a maiúsculas, mapeamento de tipos. Portável quase literalmente do DBeaver.

### 2.5 `otter_ui`

- **Grid virtualizado** sobre o `ResultSet` colunar. Render O(células visíveis). Alvo: 1M
  linhas com scroll a 144 fps.
- **Editor SQL** com realce incremental via lexer, autocomplete via semântica, múltiplos
  cursores. Item de maior risco — protótipo obrigatório na Fase 0.
- **Navigator** — árvore lazy sobre `MetaNode`.
- **Raft view** — gerenciador de conexões.

Backend ImGui: Win32+DX11 no Windows, GLFW+OpenGL3 no Linux.

## 3. Fases

### Fase 0 — Fundação e validação de risco (2–3 semanas)

O objetivo é matar os riscos antes de construir sobre eles.

| Entrega | Critério de aceite |
|---------|--------------------|
| CMake + Ninja + presets, CI Windows/Linux | Build limpo nos dois, warnings como erro |
| `otter_base`: arena, thread pool, utf8, OS layer | Testes unitários passando |
| **Spike: editor de texto em ImGui** | 10k linhas com realce, scroll fluido, seleção e undo |
| **Spike: grid virtualizado** | 1M×20 linhas sintéticas, scroll a 144 fps |
| **Spike: libpq binário** | Conectar, `SELECT` 100k linhas em buffer colunar, medir vs. DBeaver |

Se o spike do editor falhar, a decisão de UI é reavaliada aqui — não depois.

### Fase 1 — Vertical slice PostgreSQL (4–6 semanas)

Fatia fina end-to-end, com tudo funcionando de verdade para um SGBD só.

- `otter_db` completo + `drv_postgres` (libpq, fetch binário, transações, cancelamento)
- `otter_meta` + leitura de catálogo do Postgres (`pg_catalog`, portado do `ext.postgresql`)
- `otter_sql`: lexer + splitter + parser de `SELECT`/DML
- UI: raft, navigator, editor SQL, grid somente leitura
- Persistência de conexões + credenciais (DPAPI no Windows, libsecret no Linux)

**Aceite:** conectar num Postgres real, navegar o schema, rodar query, ver resultado no grid,
cancelar query longa, reabrir o app com a conexão intacta.

### Fase 2 — Edição e transações (3–4 semanas)

- Grid editável com geração de `UPDATE`/`INSERT`/`DELETE` a partir da chave primária
- Controle de transação explícito, savepoints, rollback
- Editores de valor: texto, binário/hex, data/hora, JSON
- Análise semântica + autocomplete contextual

### Fase 3 — Multi-driver (5–7 semanas)

Um driver por vez, cada um validando a abstração:

1. **SQLite** — embarcado, sem rede, valida o caminho simples
2. **MySQL/MariaDB** — valida protocolo e `information_schema`
3. **ODBC/MSSQL** — valida a camada genérica e cobre a cauda longa
4. **Oracle (OCI)** — opcional, valida o caso mais hostil

Cada driver entra com: conexão, metadados, execução, mapeamento de tipos, dialeto e suíte de
testes de integração em container.

### Fase 4 — Data transfer e produtividade (4–5 semanas)

- Export: CSV, JSON, SQL `INSERT`, Markdown
- Import: CSV com mapeamento de colunas e detecção de tipos
- Geração de DDL a partir de metadados
- Filtros, ordenação server-side, paginação
- Histórico de queries, favoritos, múltiplas abas

### Fase 5 — Polimento e release (3–4 semanas)

- Benchmarks formais contra DBeaver (startup, fetch de 1M linhas, RSS, scroll)
- Perfilamento e otimização dirigida por medição
- Instaladores Windows (MSI) e Linux (AppImage/deb)
- Documentação de usuário

**Total estimado: 21–29 semanas** para um desenvolvedor em tempo integral, ~166k linhas.

## 4. Metas de performance (verificáveis, não aspiracionais)

| Métrica | Alvo C-Otter | DBeaver (referência) |
|---------|-------------|----------------------|
| Cold start até janela utilizável | < 200 ms | 3–8 s |
| Conectar + carregar schema (100 tabelas) | < 300 ms | 1–3 s |
| `SELECT` 1M linhas × 10 colunas (fetch total) | < 2 s | 15–40 s |
| RSS com 1M linhas carregadas | < 350 MB | 1,5–3 GB |
| Scroll no grid | 144 fps constante | 15–60 fps |
| Tamanho do instalador | < 25 MB | ~200 MB + JRE |

Cada meta vira um benchmark automatizado na CI desde a Fase 1. Meta sem benchmark é opinião.

## 5. Estratégia de teste

| Nível | Cobertura |
|-------|-----------|
| Unitário | `base`, `sql` (lexer/parser/formatter), `meta` — framework próprio leve, sem dependência |
| Integração | Cada driver contra o SGBD real em Docker, matriz de versões |
| Fuzzing | Parser SQL com libFuzzer — precisa ser à prova de entrada arbitrária |
| Performance | Benchmarks da seção 4 na CI, com detecção de regressão |
| Sanitizers | ASan/UBSan no Linux, ASan no MSVC, em todo PR |

## 6. Layout do repositório

```
C-Otter/
├── CMakeLists.txt
├── CMakePresets.json
├── docs/
│   ├── ANALYSIS.md          análise do DBeaver
│   ├── PLAN.md              este arquivo
│   └── adr/                 architecture decision records
├── src/
│   ├── base/                otter_base
│   ├── db/                  otter_db + drivers/
│   ├── meta/                otter_meta
│   ├── sql/                 otter_sql
│   ├── registry/            otter_registry
│   ├── transfer/            otter_transfer
│   ├── ui/                  otter_ui
│   └── app/                 otter_app (main)
├── tests/
│   ├── unit/
│   ├── integration/
│   └── bench/
├── third_party/             imgui, drivers de cliente
└── tools/                   scripts de build e CI
```

## 7. Convenções

- `snake_case` para funções e variáveis, `PascalCase` para tipos, `namespace otter::<módulo>`
- Headers `.hpp`, implementação `.cpp`; um tipo público por header
- Sem exceções no núcleo — `std::expected<T, Error>`
- Sem herança a não ser para interfaces virtuais puras (drivers, backends de UI)
- `clang-format` + `clang-tidy` obrigatórios no pre-commit
- Um ADR em `docs/adr/` para cada decisão arquitetural irreversível

## 8. Primeiros passos concretos

1. `git init` + esqueleto de diretórios
2. Instalar Ninja (`winget install Ninja-build.Ninja`)
3. CMakeLists raiz + presets para MSVC e Clang
4. `otter_base`: arena, logging, camada de OS
5. Os três spikes da Fase 0 — **antes** de qualquer outra linha de produto
