# C-Otter

> **Why "C-Otter"?** Say it out loud: *sea otter*. It's a playful nod to DBeaver, from a
> lighter, faster cousin that shares the same river, and it's written in C. Sea otters hold
> hands while they sleep so they never drift apart, keep a favorite rock in a pocket, and
> float together in a raft. Basically, they were born for databases.
> Here, every JOIN is an **OTTER JOIN**.

Um gerenciador universal de bancos de dados escrito em C++20, inspirado na arquitetura do
[DBeaver](https://github.com/dbeaver/dbeaver) — mesmo rio, corrente mais rápida.

## O que é

C-Otter é um cliente de banco de dados multiplataforma: conecta, navega metadados, edita
dados em grid, executa SQL e transfere dados entre SGBDs. A diferença em relação ao ancestral
é o custo de execução — sem JVM, sem OSGi, sem JDBC, com drivers nativos e uma UI em modo
imediato.

## Alvos da v1

| Eixo | Decisão |
|------|---------|
| Linguagem | C++20, zero dependências no núcleo |
| UI | Dear ImGui (modo imediato, grid virtualizado próprio) |
| Drivers | libpq, MariaDB/MySQL, SQLite, ODBC (MSSQL), OCI (Oracle) |
| Plataformas | Windows e Linux — CMake + MSVC/Clang |
| Build | CMake 4.x, Ninja ou MSBuild |

## Glossário da lontra

Termos do projeto que apareceram no código e nos docs:

| Termo | Significado |
|-------|-------------|
| **Raft** | O workspace: o conjunto de conexões que flutuam juntas |
| **Holt** | Uma conexão/datasource individual (a toca da lontra) |
| **Pocket Rock** | Cache local de metadados — a pedra favorita que a lontra guarda no bolso |
| **Otter Join** | Um JOIN. Sempre. |
| **Float** | Sessão de execução sobre um Holt |

## Estado

**Em desenvolvimento — a aplicação roda e conecta a bancos reais.**

Funciona hoje: conexão PostgreSQL via protocolo v3 nativo (SCRAM-SHA-256, sem libpq),
navegação de schema com carregamento tardio, editor SQL com realce e autocomplete sobre
metadados reais, grade virtualizada e inspetor de queries. 144 fps, 1,8 MB em Release, sem
dependência de DLL redistribuível.

**Cobertura frente ao DBeaver: ~37%** dos itens no escopo da v1 — ver
[`docs/PARITY.md`](docs/PARITY.md) para o inventário item a item.

```powershell
$env:PGPASSWORD="..."; $env:PGDATABASE="..."
build\win-release\bin\c-otter.exe
```

### Documentação

- [`docs/ANALYSIS.md`](docs/ANALYSIS.md) — análise do DBeaver como referência arquitetural
- [`docs/PLAN.md`](docs/PLAN.md) — roteiro de implementação em fases
- [`docs/EFFORT.md`](docs/EFFORT.md) — carga de trabalho em homem-hora
- [`docs/PARITY.md`](docs/PARITY.md) — **inventário do que está e do que não está pronto**
- [`docs/adr/`](docs/adr/) — decisões arquiteturais, com as revogadas marcadas

## Licença

A definir. O DBeaver é **Apache 2.0** (confirmado em `LICENSE.md`), o que permite trabalho
derivado desde que preservados aviso de copyright, texto da licença e um `NOTICE` indicando
as modificações. Como o plano prevê portar consultas de metadados dos plugins `ext.*`
(ver [`docs/ANALYSIS.md`](docs/ANALYSIS.md) §4), a atribuição é **obrigatória** mesmo sendo
C-Otter uma reescrita — decidir antes da Fase 1.
