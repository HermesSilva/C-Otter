# C-Otter

> **Why "C-Otter"?** Say it out loud: *sea otter*. It's a playful nod to DBeaver, from a
> lighter, faster cousin that shares the same river, and it's written in C. Sea otters hold
> hands while they sleep so they never drift apart, keep a favorite rock in a pocket, and
> float together in a raft. Basically, they were born for databases.
> Here, every JOIN is an **OTTER JOIN**.

Um gerenciador universal de bancos de dados escrito em C++23, inspirado na arquitetura do
[DBeaver](https://github.com/dbeaver/dbeaver) — mesmo rio, corrente mais rápida.

## O que é

C-Otter é um cliente de banco de dados multiplataforma: conecta, navega metadados, edita
dados em grid, executa SQL e transfere dados entre SGBDs. A diferença em relação ao ancestral
é o custo de execução — sem JVM, sem OSGi, sem JDBC, com drivers nativos e uma UI em modo
imediato.

## Alvos da v1

| Eixo | Decisão |
|------|---------|
| Linguagem | C++23, zero dependências no núcleo |
| UI | Dear ImGui + GLFW/OpenGL 3.3, grid virtualizado próprio, ícones vetoriais |
| Drivers | Protocolo próprio, sem biblioteca cliente: PostgreSQL, MySQL/MariaDB, SQL Server e SQL Anywhere prontos; SQLite e Oracle a fazer |
| Plataformas | Windows e Linux — CMake + MSVC/Clang |
| Build | CMake 4.x + Ninja, link estático total (`/MT`); pacotes dos dois sistemas pelo GitHub Actions |

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

Quatro SGBDs, todos por protocolo escrito no projeto (`lib/`), sem libpq, libmysqlclient,
FreeTDS ou ODBC. Várias conexões simultâneas, editor SQL com realce e autocomplete sobre
metadados reais, grade virtualizada **editável** com paginação, transações, editor de
objeto, exportação e importação, três temas e i18n (EN + pt-BR). As conexões persistem no
formato do DBeaver; na primeira execução as do DBeaver, do pgAdmin e do SSMS são copiadas.

**Cobertura frente ao DBeaver: 68,0% dos 281 comandos e 78,9% dos 147 atalhos**, medidos
por `tools/regen_docs.py` contra os `plugin.xml` dele — ver
[`docs/DBEAVER-MAP.md`](docs/DBEAVER-MAP.md) e [`docs/PARITY.md`](docs/PARITY.md).

### Por SGBD

Legenda: ✅ pronto · 🟡 funciona, com a diferença dita · ⬜ falta. "Conferido" quer dizer
executado contra o servidor, não só compilado.

| | PostgreSQL | MySQL / MariaDB | SQL Server | SQL Anywhere |
|---|---|---|---|---|
| Protocolo | wire v3 (`lib/pgwire`) | `lib/mywire` | TDS 7.4 (`lib/tdswire`) | TDS 5.0 (`lib/tdswire`) |
| Autenticação | SCRAM-SHA-256, MD5 | `mysql_native_password`, `caching_sha2_password` | SQL Server e Windows (SSPI) | nativa do banco |
| TLS (Windows) | 🟡 implementado, aperto de mão não conferido | ✅ | ✅ | ⬜ sem cifra |
| Árvore, contra a do DBeaver | ✅ 59 de 59 pastas | 🟡 24 de 27 nós | 🟡 21 de 30 pastas | 🟡 14 de 17 pastas |
| Consulta, paginação, grade editável | ✅ | ✅ | ✅ | ✅ |
| Editor de objeto (criar, alterar, apagar) | ✅ 26 de 27 tipos | ✅ | 🟡 10 de 16 tipos | ✅ |
| Permissões (`GRANT` / `REVOKE`) | ✅ | 🟡 por objeto | ✅ | ✅ |
| Ferramentas de manutenção | ✅ | ✅ | ✅ | ✅ |
| Backup e restore | ✅ `pg_dump` / `pg_restore` | ✅ `mysqldump` / `mysql` | ✅ `BACKUP` / `RESTORE DATABASE` | 🟡 SQL gerado, não executado |
| Sessões e travas | ✅ | ✅ sessões | ✅ | ✅ |
| Plano de execução (*Explain*) | ✅ | ⬜ não conferido | ⬜ | ⬜ |
| Vários resultados num lote | — | — | 🟡 só o primeiro | 🟡 só o primeiro |
| Conferido contra | PostgreSQL 18 | MySQL 8.0.46 | SQL Server 2022 | SQL Anywhere 16 |
| Suíte ao vivo | `otter_tests_live` | 108 + 111 verificações | 187 verificações | 337 verificações |
| Mapa completo | [`NAVIGATOR-TREE`](docs/NAVIGATOR-TREE.md), [`OBJECT-EDITOR`](docs/OBJECT-EDITOR.md) | [`MYSQL-MAP`](docs/MYSQL-MAP.md) | [`MSSQL-MAP`](docs/MSSQL-MAP.md) | [`SQLANYWHERE-MAP`](docs/SQLANYWHERE-MAP.md) |

#### PostgreSQL — o que falta

- **TLS conferido**: o `SSLRequest` está implementado, mas o servidor de teste roda com
  `ssl = off` e o aperto de mão nunca foi exercitado.
- Global Backup (`pg_dumpall`) e o assistente de foreign data wrappers.
- Chaves virtuais e `With Hierarchy` na aba de permissões.
- Fonte de materialized view, trigger e regra é somente leitura; chave estrangeira sem
  `DEFERRABLE` no formulário.
- 7 das 62 listas da árvore: as colunas de constraint, FK, índice e referência aparecem no
  detalhe, não como subnós.
- *Navigator view* Simple / Custom: a árvore é sempre a Advanced.

#### MySQL / MariaDB — o que falta

- **MariaDB não foi conferido** (sequences, packages): só há MySQL na máquina de teste.
- Na árvore: índices e triggers "do banco" (as pastas virtuais) e packages.
- Certificado de cliente no TLS (gravado e lido, não usado) e protocolo preparado
  (`COM_STMT_*`).
- Privilégios: a matriz global e por schema (hoje é por objeto) e a lista dos de rotina.
- Limites da conta, engine, charset e auto-increment da tabela: mostrados, alterados só
  por SQL.
- *Explain*: o leitor de plano entende o formato do PostgreSQL; no MySQL não foi conferido.
- Defeito aberto: senha vazia falha com uma mensagem de criptografia em vez de dizer que
  a senha está vazia.

#### SQL Server — o que falta

- **Autenticação**: 2 dos 8 modelos do DBeaver. Faltam NTLM com credenciais digitadas e
  os quatro do Entra ID (Active Directory).
- TDS 8.0 (`Encrypt=strict`).
- Vários conjuntos de resultado: só o primeiro chega à grade.
- Parâmetros vão no texto do comando (não há RPC) e não há carga em massa (`BULK`).
- *Explain* (`SHOWPLAN_XML`) e *Execute SQL script natively* (`sqlcmd`) ficam desabilitados,
  com o motivo.
- Na árvore: tabelas externas, partições, triggers do schema e propriedades estendidas
  além de `MS_Description`.
- No editor: tipo de dado, tipo de tabela e tabela externa; índice sem colunas incluídas
  nem columnstore; sinônimo sem formulário.
- `geometry`, `geography` e `hierarchyid` aparecem como binário; `bit` como `1`/`0`.
- Instância nomeada conferida só contra um SQL Server Browser de teste.

#### SQL Anywhere — o que falta

- **Sem cifra**: senha e dados trafegam em claro. O diálogo avisa e recusa "Use SSL"; a
  saída é o túnel SSH.
- SAP ASE não é suportado (mesmo protocolo, outro catálogo): a conexão falha dizendo isso.
- Vários conjuntos de resultado: só o primeiro chega à grade.
- *Explain* e *Execute SQL script natively* (`dbisql`) ficam desabilitados, com o motivo.
- Renomear rotina, trigger, evento, sequence e domínio; criar view materializada pelo
  formulário.
- Pastas que só listam, sem editor: políticas de login, dbspaces, servidores remotos, web
  services, publicações. Ficaram fora da árvore: SQL Remote / MobiLink, LDAP, certificados,
  planos de manutenção e espelhamento.
- `BACKUP DATABASE` e `VALIDATE DATABASE`: SQL conferido, não executados.

#### Comum aos quatro

- **Linux**: compila, passa nos testes unitários e abre, mas **sem TLS** e sem
  autenticação integrada; as suítes ao vivo nunca rodaram lá
  ([ADR 0006](docs/adr/0006-rendering-backend.md)).
- Túnel SSH pelo cliente `ssh` do sistema: chave ou agente, sem senha digitada; não
  conferido contra um servidor SSH de verdade.
- Transferência direta entre bancos, XLSX e criar a tabela de destino na importação.
- Demais SGBDs do catálogo de drivers: SQLite e Oracle estão planejados; os outros ficam
  fora da v1 e a tela diz qual é qual.

### Executar e publicar

```powershell
build test                      # Windows: compila o win-release e roda os testes
build\win-release\bin\c-otter.exe
```

```bash
cmake --preset linux-release && cmake --build build/linux-release    # Linux, Clang 19+
build/linux-release/bin/c-otter
```

O pacote de cada sistema sai de `tools\package.ps1` e `tools/package.sh`. Publicar é
empurrar uma tag: `git tag v0.1.0 && git push origin v0.1.0` dispara
[`.github/workflows/release.yml`](.github/workflows/release.yml), que compila, testa,
empacota os dois e cria a Release. A tag tem de bater com o `VERSION` do `CMakeLists.txt`.

### Documentação

- [`CLAUDE.md`](CLAUDE.md) — **diretivas de trabalho; ler primeiro**
- [`docs/DBEAVER-MAP.md`](docs/DBEAVER-MAP.md) — mapa exaustivo do DBeaver, gerado por `tools/map_dbeaver.py`
- [`docs/NAVIGATOR-TREE.md`](docs/NAVIGATOR-TREE.md) — árvore de objetos: ~70 nós e o estado de cada
- [`docs/ELEMENTS.md`](docs/ELEMENTS.md) — guia operacional: cada botão, menu e atalho
- [`docs/PARITY.md`](docs/PARITY.md) — funcionalidades frente ao DBeaver
- [`docs/UI-SCOPE.md`](docs/UI-SCOPE.md) — lacunas estruturais e ordem de implementação
- [`docs/ANALYSIS.md`](docs/ANALYSIS.md) — análise do DBeaver como referência arquitetural
- [`docs/PLAN.md`](docs/PLAN.md) — roteiro em fases · [`docs/EFFORT.md`](docs/EFFORT.md) — esforço em homem-hora
- [`docs/adr/`](docs/adr/) — 26 decisões arquiteturais, com as revogadas marcadas
- [`lang/README.md`](lang/README.md) — como acrescentar um idioma

## Licença

A definir. O DBeaver é **Apache 2.0** (confirmado em `LICENSE.md`), o que permite trabalho
derivado desde que preservados aviso de copyright, texto da licença e um `NOTICE` indicando
as modificações. Como o plano prevê portar consultas de metadados dos plugins `ext.*`
(ver [`docs/ANALYSIS.md`](docs/ANALYSIS.md) §4), a atribuição é **obrigatória** mesmo sendo
C-Otter uma reescrita — decidir antes da Fase 1.
