# Árvore de objetos — mapa do DBeaver e estado do C-Otter

Extraído de `plugins/org.jkiss.dbeaver.ext.postgresql/plugin.xml` (elemento `<tree>`) e
das 90 classes `Postgre*.java` do modelo.

**Legenda:** ✅ implementado · 🟡 parcial · ⬜ ausente · ➖ fora do escopo da v1

---

## Uma árvore só (ADR 0018, 2026-09-30)

Eram dois painéis — **Raft** (conexões) e **Navigator** (schemas da conexão
ativa). O DBeaver tem um, o *Database Navigator*, com a conexão na raiz. O
usuário, comparando com uma captura dele: *"a árvore de objeto está muito
diferente do DBeaver, nele não há duas seções"*.

Agora é um painel, com a forma do `<tree>` do `plugin.xml` de cada driver.
Implementação em `src/ui/navigator.cpp`; decisão e alternativas no ADR 0018.

| Diferença apontada | Estado |
|---|---|
| Nó raiz da conexão, com `host:porta` ao lado | ✅ |
| Pasta `Databases`, com a coluna de tamanho | ✅ |
| Outros bancos do servidor (`Show all databases`) | ✅ uma sessão por banco, aberta ao expandir |
| Nós de servidor: Roles, Extensions, Storage, System Info, Administer | ✅ |
| Tamanho da tabela em coluna alinhada à direita (`72K`, `112K`) | ✅ era texto solto depois do nome (`24 kB`), cortado pela borda |

## A árvore completa do DBeaver (PostgreSQL)

Todas as **59 pastas** do `<tree>` estão na árvore. Verificado contra o
PostgreSQL 18.2 (`otter_tests_live`, 145 verificações) e na tela.

```
Connection                            ✅  nome + host:porta; expandir conecta
├── Databases                         ✅  barra de tamanho proporcional ao maior
│   └── Database                      ✅  sessão própria por banco (ADR 0018)
│       ├── Schemas                   ✅
│       │   └── Schema                ✅
│       │       ├── Tables            ✅  particionada incluída; partições não
│       │       │   └── Table         ✅
│       │       │       ├── Columns           ✅
│       │       │       ├── Constraints       ✅  PK, UNIQUE, CHECK, EXCLUDE
│       │       │       │   └── Constraint columns   🟡 no detalhe, não como nó
│       │       │       ├── Foreign Keys      ✅  com ON UPDATE/DELETE
│       │       │       │   └── FK columns           🟡 no detalhe
│       │       │       ├── Indexes           ✅  método, tamanho, INVALID
│       │       │       │   └── Index columns        🟡 no tooltip
│       │       │       ├── Dependencies      ✅  o que depende da tabela
│       │       │       ├── References        ✅  FKs que apontam para cá
│       │       │       ├── Partitions        ✅  limites, método, tamanho
│       │       │       ├── Child tables      ✅  só quando há herança
│       │       │       ├── Triggers          ✅  timing, eventos, habilitado
│       │       │       ├── Rules             ✅
│       │       │       └── Policies          ✅  RLS: comando, roles, USING
│       │       ├── Foreign Tables    ✅  Columns, Constraints, Dependencies
│       │       ├── Views             ✅
│       │       │   └── View → Columns, Dependencies, Triggers, Rules   ✅ (+ Definition)
│       │       ├── Materialized Views ✅
│       │       │   └── → Columns, Indexes, Dependencies                ✅ (+ Definition)
│       │       ├── Indexes (do schema) ✅
│       │       ├── Functions         ✅  assinatura, retorno, linguagem
│       │       │   ├── Function parameters ✅  nome, tipo, IN/OUT
│       │       │   └── Dependencies        ✅ (+ Definition; agregada inclusive)
│       │       ├── Sequences         ✅  last_value, owned_by
│       │       ├── Data types        ✅  enum, domain, composto, range
│       │       │   └── Attributes         ✅
│       │       └── Aggregate functions ✅
│       ├── Event Triggers            ✅  evento, função, [off]
│       ├── Extensions                ✅  versão, schema
│       ├── Storage                   ✅
│       │   └── Tablespaces           ✅
│       ├── System Info               ✅
│       │   ├── Foreign data wrappers ✅
│       │   ├── Foreign servers       ✅
│       │   │   └── User Mappings     ✅
│       │   └── Settings              ✅  ~350 parâmetros; alterado sai marcado
│       └── Roles                     ✅  usuário × grupo, por ícone
│           └── Role → Members / Roles ✅
├── Administer                        ✅
│   ├── Session Manager               🟡 abre a consulta numa aba; sem "kill"
│   ├── Lock Manager                  🟡 idem (PostgreSQL 9.6+)
│   └── Jobs → Steps / Schedules      🟡 consulta escrita; NÃO verificada — sem pgAgent no servidor de teste
└── System Info                       ✅
    ├── Access Methods                ✅
    │   ├── Operator classes          ✅
    │   └── Operator families         ✅
    ├── Encodings                     ✅
    ├── Collations                    ✅
    ├── Languages                     ✅
    └── Available Extensions          ✅  instalada × disponível, por ícone
```

## Contagem

Denominador: o `<tree>` do DBeaver, contado no `plugin.xml` (diretiva 4).

| | DBeaver | C-Otter | |
|---|---|---|---|
| Pastas (`<folder>`) | **59** | **59** | 100% |
| Listas de itens (`<items>`) | **62** | **55** | 89% — as 7 que faltam são as sublistas de colunas de constraint, FK, índice e referência, mostradas no detalhe |
| Editores abertos pela árvore | 2 | 0 | Session Manager e Lock Manager abrem uma aba de resultado |
| Classes de modelo `Postgre*` | 90 | 8 structs | os tipos que só listam usam `CatalogItem` (ADR 0018) |

As outras três árvores têm mapa próprio, com a mesma contagem: MySQL em
`docs/MYSQL-MAP.md` (21 de 27 nós), SQL Server em `docs/MSSQL-MAP.md` (21 de 30 pastas,
mais 2 parciais) e SQL Anywhere em `docs/SQLANYWHERE-MAP.md` (14 de 17 pastas da árvore
genérica do DBeaver, mais 2 parciais — e, fora da conta, as pastas do Sybase Central).

O que a árvore **mostra** está completo; o que o DBeaver **faz** a partir dela
(editor de propriedades de cada objeto, criar/alterar role, extensão,
tablespace) não — ver `docs/DDL-WRITE.md`.

## Opções que mudam a árvore

| Opção | Onde (igual ao DBeaver) | Estado |
|---|---|---|
| Show all databases | página principal, abaixo de Database | ✅ lida, gravada com a chave do DBeaver |
| Show template databases | página Metadata | ✅ |
| Show databases not available for connection | página Metadata | ✅ |
| Navigator view (Simple / Advanced / Custom) | página General | ⬜ a árvore é sempre a Advanced |

As três primeiras existiam como caixas **que nenhum código lia nem gravava**
(diretiva 6). A chave é `@dbeaver-show-non-default-db@` em
`provider-properties`, conferida num `data-sources.json` real.

## Defeitos que só apareceram na tela

| Defeito | Causa |
|---|---|
| Tabela particionada sumia da árvore | a pasta filtrava por `kind == table` |
| Partições listadas também em Tables | faltava `NOT relispartition` |
| Árvore recuava um nível depois de um nó aberto | `EndGroup` restaura o indent e engole o do `TreePush` |
| Janela do editor nascia flutuando sobre a grade | o `layout.ini` guarda o id do nó de docking, que mudou com a saída do Raft |
| Tamanho da tabela cortado e desalinhado | texto solto depois do nome, em vez de coluna |
| "Ver dados" abria a aba e não executava | o pedido de colunas ocupava a sessão no instante da consulta |
| Dependência sem nome | o `pg_depend` aponta para `pg_policy`, que a consulta do DBeaver não junta |
| Script do banco ERP caía na janela do MySQL | ao fechar a sessão do banco, as abas iam para a conexão ativa |
| Árvore não mostrava a tabela recém-criada | depois do DDL só a tabela era invalidada, não a lista |

## Decisões de design

**Nós-pasta com contagem.** `Tables (32)`, `Indexes (7)` — o número evita expandir para
descobrir que está vazio.

**Carregamento tardio por nó.** Cada pasta consulta o catálogo só quando expandida. Hoje
isso vale para colunas; passa a valer para todos.

**Ícones: os originais do DBeaver (ADR 0019, 2026-09-30).**

A árvore usa os ícones do próprio DBeaver — quem vem de lá reconhece o nó antes de ler o
rótulo. A pasta de cada tipo segue o `icon="#..."` do `<tree>` do `plugin.xml`, resolvido
pelo `DBIcon.java`:

| Pasta | `icon=` no DBeaver | Arquivo |
|---|---|---|
| Databases | `#folder_database` | `folder_database.svg` |
| Schemas | `#folder_schema` | `folder_schema.svg` |
| Tables, Partitions, Child tables | `#folder_table` | `folder_table.svg` |
| Foreign Tables | `#folder_link` | `folder_link.svg` |
| Views, Materialized Views | `#folder_view` | `folder_view.svg` |
| Columns | `#columns` | `columns.svg` |
| Constraints | `#constraints` | `folder_constraint.svg` |
| Roles, User mappings | `#folder_user` | `folder_user.svg` |
| Extensions, Administer | `#folder_admin` | `folder_admin.svg` |
| Storage, System Info | `#folder_info` | `folder_info.svg` |
| Indexes, Functions, Sequences, Data types, Triggers, References... | `#indexes`, `#procedures`... | `folder.svg` — o id não existe no `DBIcon`, e o DBeaver cai na pasta comum |

Conferido na tela contra a captura do DBeaver, nos temas escuro e claro. O conjunto antigo
continua disponível em *Help → Icons → C-Otter*; a tabela abaixo descreve esse conjunto.

**Distâncias da linha** (pedido do usuário, 2026-09-30): metade do espaço entre a borda do
painel e a seta dos itens raiz, metade entre a seta e o ícone, metade entre o ícone e o
título. Os números ficam em `tree_arrow_gap()` / `tree_label_gap()` (`src/ui/icons.cpp`).

**O conjunto C-Otter: um desenho por tipo.** Nenhum tipo de objeto compartilha desenho
(dívida quitada em 2026-09-21).

| Nó | Ícone | Desenho |
|----|-------|---------|
| Schema | `schema` | ✅ grade ramificada |
| Tabela | `table` | ✅ grade com cabeçalho |
| View | `view` | ✅ olho |
| Materialized view | `materialized_view` | ✅ olho sobre disco |
| Coluna | `column` | ✅ célula vertical |
| Chave primária | `key` | ✅ chave |
| Constraint | `constraint` | ✅ escudo |
| Índice | `index` | ✅ páginas com marcador |
| Foreign key | `foreign_key` | ✅ dois elos sobrepostos |
| References | `references` | ✅ três origens convergindo num alvo |
| Sequence | `sequence` | ✅ degraus com seta |
| Função | `function` | ✅ caixa com duas entradas e uma saída |
| Procedure | `procedure` | ✅ bloco com play |
| Trigger | `trigger` | ✅ raio |
| Tipo de dado | `data_type` | ✅ chaves `{}` com três campos |
| Extensão | `extension` | ✅ peça de quebra-cabeça |
| Role | `role` | ✅ silhueta com chave |
| Tablespace | `tablespace` | ✅ gaveta com puxadores |

Verificado na aplicação rodando, nos três temas e no tamanho real da árvore
(Ajuda → Galeria de ícones, ou `OTTER_SHOW_ICONS=1`).

**O teste que impede a dívida de voltar:** `tests/unit/test_icons.cpp` desenha
cada ícone num `ImDrawList` isolado e compara os vértices gerados. Dois ícones
com a mesma geometria fazem o teste falhar **nomeando o par**.

A primeira versão desse teste comparava só os ícones de objeto entre si, e
passou com uma regressão injetada de propósito — porque a dívida real era de
pares *ação↔objeto* (`commit`↔constraint, `filter`↔índice,
`settings`↔função). A versão final compara os 42 ícones entre si.

Quatro desenhos foram refeitos depois de olhar a captura, não o código:

| Ícone | Problema na primeira versão |
|-------|------------------------------|
| `foreign_key` | Dois arcos unidos por retas fundiram-se num oval só |
| `materialized_view` | Olho e disco colados viravam uma forma ambígua |
| `function` | O `(f)` desaparecia no tamanho da árvore — letras não sobrevivem a 14 px |
| `tablespace` | Elipses empilhadas ficavam iguais à materialized view |

**Menu de contexto por tipo de nó.** Extraído dos `plugin.xml` de
`org.jkiss.dbeaver.ui.navigator` e `.ui.editors.sql`. O DBeaver oferece dezenas de
comandos; a coluna de estado diz o que o C-Otter faz hoje:

| Comando | Tabela | View | Coluna | Estado |
|---|:---:|:---:|:---:|---|
| Ver dados (`SELECT *`) | ✔ | ✔ | — | ✅ abre aba com a consulta |
| Gerar SELECT | ✔ | ✔ | ✔ | ✅ lista de colunas explícita |
| Gerar INSERT | ✔ | — | — | ✅ |
| Gerar UPDATE | ✔ | — | — | ✅ com `WHERE` pela PK |
| Gerar DELETE | ✔ | — | — | ✅ com `WHERE` pela PK |
| Gerar DDL (`CREATE TABLE`) | ✔ | ✔ | — | ✅ colunas, PK, constraints, índices |
| Copiar nome | ✔ | ✔ | ✔ | ✅ qualificado com o schema |
| Contar linhas | ✔ | ✔ | — | ✅ `SELECT count(*)` |
| Atualizar (F5) | ✔ | ✔ | ✔ | ✅ descarta o cache do nó |
| Criar / alterar / excluir objeto | ✔ | ✔ | ✔ | ⬜ exige DDL de escrita |
| Filtro de objetos | ✔ | ✔ | — | ⬜ |
| Mover na árvore | ✔ | ✔ | ✔ | ➖ organização do DBeaver |

O **DDL é gerado pelo C-Otter**, não lido do servidor: o PostgreSQL não tem
`SHOW CREATE TABLE`. Montá-lo a partir do catálogo é o que o DBeaver também faz.

**Tooltip com detalhe.** Comentário do objeto, definição da constraint, expressão do
índice — informação que não cabe no rótulo.

**Cada linha vai dentro de `BeginGroup`/`EndGroup`.** `ImGui::IsItemHovered()`
testa apenas o **último** item desenhado. Como as linhas da árvore são montadas com
vários `TextColored` em `SameLine`, os tooltips respondiam só sobre o pedaço final:
o da coluna só sobre o tipo, o do índice só sobre o tamanho. Sete linhas tinham o
defeito e foram corrigidas juntas — ele só apareceu ao passar o mouse na aplicação
rodando, nunca no build.

**Pasta vazia aparece, com `(0)`.** Era escondida, sob o argumento de que o
zero ocuparia uma linha para dizer que não há nada. O DBeaver mostra Tables,
Views e Materialized Views sempre no mesmo lugar; escondida, quem procura
"Views" onde está acostumado não acha, e não sabe se o schema não tem views ou
se o programa não as mostra (diretiva 12). Continua fora o que o **SGBD** não
tem — `Sequences` num MySQL, por exemplo.

**Cada tipo de relação tem as pastas que faz sentido ter.** Extraído do `<tree>` do
`plugin.xml` do DBeaver, não estimado:

| | Columns | Constraints | Indexes | FK / References | Triggers | Definition |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| Tabela | ✅ | ✅ | ✅ | ✅ | ✅ | — |
| View | ✅ | — | — | — | ✅ `INSTEAD OF` | ✅ |
| Materialized view | ✅ | — | ✅ | — | — | ✅ |

Uma view não tem linhas próprias para restringir, então não tem constraint nem FK.
A materialized view tem linhas gravadas — por isso aceita índice — mas é atualizada por
`REFRESH`, não por DML, então não há evento para disparar trigger.

Mostrar `Constraints (0)` numa view seria pior que omitir: sugeriria que ela
*poderia* ter uma. As regras estão em `TableMeta::has_constraints()`,
`has_indexes()` e `has_triggers()`, com teste em `tests/unit/test_catalog.cpp`.

**Tipos de dados: `typtype` decide o que mostrar.** Extraído de
`PostgreDataType.java` e do `<tree>` (`folder … visibleIf="object.hasAttributes()"`):

| `typtype` | O que é | Detalhe na árvore |
|:---:|---|---|
| `e` | ENUM | Valores, na ordem de `enumsortorder` |
| `c` | Composto | Atributos, com tipo de cada um |
| `d` | DOMAIN | Tipo base, `NOT NULL`, `DEFAULT`, `CHECK` |
| `r` | RANGE | Subtipo |
| `b` | Base | — (os escalares do próprio PostgreSQL) |
| `p` | Pseudo | — (`trigger`, `record`, `void`) |

Os tipos de tabela (`typrelid` de uma relação real) e os de array (`_nome`) ficam
**fora**: cada tabela já cria um tipo homônimo, e listá-los duplicaria a árvore.
O DBeaver filtra pelo mesmo critério.

**Fixture do banco de teste.** `tests/integration/fixtures.sql` cria o schema
`otter_test` com views, materialized view, trigger, tipos próprios, função e
procedure. Existe porque o ERP_TID só tem tabelas: implementar o nó de "Views"
sem ter uma view no banco significaria entregar código que nunca rodou.

```powershell
psql -h localhost -U postgres -d ERP_TID -f tests/integration/fixtures.sql
```

