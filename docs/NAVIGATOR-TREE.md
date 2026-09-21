# Árvore de objetos — mapa do DBeaver e estado do C-Otter

Extraído de `plugins/org.jkiss.dbeaver.ext.postgresql/plugin.xml` (elemento `<tree>`) e
das 90 classes `Postgre*.java` do modelo.

**Legenda:** ✅ implementado · 🟡 parcial · ⬜ ausente · ➖ fora do escopo da v1

---

## A árvore completa do DBeaver (PostgreSQL)

```
Connection
└── Databases                         ⬜
    └── Database                      🟡 (só o conectado)
        ├── Schemas                   ✅
        │   └── Schema                ✅
        │       ├── Tables            ✅
        │       │   └── Table         ✅
        │       │       ├── Columns           ✅
        │       │       ├── Constraints       ✅  PK, UNIQUE, CHECK, EXCLUDE
        │       │       │   └── Constraint Columns
        │       │       ├── Foreign Keys      ✅  com ON UPDATE/DELETE
        │       │       │   └── FK Columns
        │       │       ├── Indexes           ✅  método, tamanho, INVALID
        │       │       │   └── Index Columns
        │       │       ├── References        ✅  FKs que apontam para cá
        │       │       ├── Dependencies      ⬜
        │       │       ├── Partitions        ⬜
        │       │       ├── Table Children    ⬜  herança
        │       │       ├── Triggers          ✅  timing, eventos, habilitado
        │       │       ├── Rules             ⬜
        │       │       └── Policies          ⬜  RLS
        │       ├── Foreign Tables    ⬜
        │       ├── Views             🟡 (aparecem junto das tabelas)
        │       │   └── View
        │       │       ├── Columns
        │       │       ├── Dependencies
        │       │       ├── Triggers
        │       │       └── Rules
        │       ├── Materialized Views ⬜
        │       ├── Indexes (do schema) ⬜
        │       ├── Sequences         ✅  last_value, owned_by
        │       ├── Procedures/Functions ✅  assinatura, retorno, linguagem
        │       ├── Data Types        ⬜  enums, domains, compostos
        │       └── Aggregates        ⬜
        ├── Event Triggers            ⬜
        ├── Extensions                ⬜
        ├── Storage                   ⬜
        │   └── Tablespaces
        ├── Foreign Data Wrappers     ➖
        │   ├── Foreign Servers
        │   └── User Mappings
        ├── Settings                  ⬜
        ├── Roles                     ⬜
        │   └── Role → Members / Belongs to
        ├── Administer → Jobs         ➖  pgAgent
        │   └── Job → Steps / Schedules
        └── Information               ⬜
            ├── Access Methods        ➖
            ├── Operator Classes      ➖
            ├── Operator Families     ➖
            ├── Encodings             ⬜
            ├── Collations            ⬜
            ├── Languages             ⬜
            └── Available Extensions  ⬜
```

## Contagem

| | DBeaver | C-Otter |
|---|---|---|
| Tipos de nó na árvore | **~70** | **13** |
| Classes de modelo `Postgre*` | **90** | 5 structs |

## Ordem de implementação

Por valor de uso, não por ordem na árvore.

**Concluídos em 2026-09-21** (validados pelo `spikes/catalog` contra o ERP_TID):

| Item | Detalhe exposto |
|------|-----------------|
| ✅ Constraints | PK, UNIQUE, CHECK, EXCLUDE, com `pg_get_constraintdef` no tooltip |
| ✅ Foreign Keys | Origem → destino, `ON UPDATE`/`ON DELETE` |
| ✅ **References** | Quem aponta para esta tabela — responde "o que depende disto?" |
| ✅ Indexes | Método (btree/gin/...), tamanho, UNIQUE, e **INVALID** em vermelho |
| ✅ Sequences | `last_value`, incremento, `owned_by` |
| ✅ Functions/Procedures | Assinatura, retorno, linguagem |
| ✅ Triggers | Timing, eventos, estado habilitado |

**Pendentes:**

| # | Item | Por quê |
|---|------|---------|
| 1 | **Views separadas de tabelas** | Hoje se misturam na mesma pasta |
| 2 | **Materialized Views** | |
| 3 | **Data Types** (enum, domain, composto) | |
| 4 | **Corpo de função** | `load_routine_definition()` pronto, sem UI |
| 5 | **Partições e herança** | |
| 6 | **Dependencies** | Grafo de dependências |
| 7 | **Rules e Policies (RLS)** | |
| 8 | Roles, Extensions, Settings, Tablespaces | Nível de servidor |
| 9 | Encodings, Collations, Languages | Referência |

## Decisões de design

**Nós-pasta com contagem.** `Tables (32)`, `Indexes (7)` — o número evita expandir para
descobrir que está vazio.

**Carregamento tardio por nó.** Cada pasta consulta o catálogo só quando expandida. Hoje
isso vale para colunas; passa a valer para todos.

**Ícone próprio por tipo de nó — requisito, não enfeite.**

Cada tipo de objeto precisa de um ícone **sugestivo, elegante e futurista**, distinguível de
relance. Reaproveitar um desenho genérico para dois tipos diferentes é dívida a pagar.

Estado atual (`src/ui/icons.cpp`):

| Nó | Ícone | Situação |
|----|-------|----------|
| Tabela | `table` | ✅ próprio |
| View | `view` | ✅ próprio |
| Coluna | `column` | ✅ próprio |
| Chave primária | `key` | ✅ próprio |
| Sequence | `refresh` | ⚠️ **compartilhado** com auto-commit |
| Índice | `filter` | ⚠️ **compartilhado** com filtro |
| Constraint | `commit` | ⚠️ **compartilhado** com commit |
| Função/Procedure | `settings` | ⚠️ **compartilhado** com preferências |
| References | `copy` | ⚠️ **compartilhado** com copiar |
| Trigger | `clock` | ⚠️ **compartilhado** com histórico |
| Materialized view | — | ⬜ ausente |
| Tipo de dado | — | ⬜ ausente |
| Extensão, role, tablespace | — | ⬜ ausente |

Desenhar os faltantes no mesmo traço fino dos existentes, na caixa normalizada
−0.5..0.5 de `Canvas`.

**Tooltip com detalhe.** Comentário do objeto, definição da constraint, expressão do
índice — informação que não cabe no rótulo.

