# Editor de objeto, DDL e administração — mapa contra o DBeaver

**Criado em 2026-10-01.** Mapa do que o DBeaver oferece para cada objeto do
PostgreSQL (diretivas 1 e 12), com o estado de cada item no C-Otter. Extraído de
`D:\Tootega\Source\dbeaver`:

| O que | Onde |
|---|---|
| Gerenciadores de objeto (criar/alterar/renomear/remover) | `ext.postgresql/.../edit/Postgre*Manager.java` — 27 |
| Diálogos de criação | `ext.postgresql.ui/.../ui/PostgreCreate*Dialog.java`, `ui/config/*Configurator.java` |
| Rótulos | `ext.postgresql.ui/.../PostgresResources.properties` |
| Ferramentas (Tools) | `ext.postgresql/.../tasks/PostgreTool*.java`, `plugin.xml` do `.ui` |
| Backup/Restore | `tasks/PostgreDatabaseBackupHandler.java`, `PostgreDatabaseRestoreHandler.java` |

**Legenda:** ✅ igual · 🟡 funciona, com a diferença dita · ⬜ falta · ➖ fora, com a razão

Código: regra em `src/db/object_info.cpp`, `object_ddl.cpp`, `object_info_load.cpp`,
`import.cpp`, `export.cpp`, `native_tools.cpp`; tela em `src/ui/object_editor.cpp` e
`data_transfer.cpp`.

---

## 1. O editor (duplo clique ou F4 num nó)

No DBeaver é uma aba da área de edição, ao lado dos scripts, com `Properties`
(formulário em cima; seções numa lista vertical à esquerda) e `Data`. Aqui também.

| Elemento | DBeaver | C-Otter |
|---|---|---|
| Aba por objeto, ao lado dos scripts | ✅ | ✅ o mesmo objeto não abre duas vezes |
| Abre por duplo clique e por `F4` | ✅ | ✅ |
| Abre na última aba usada (Properties/Data) | ✅ | ✅ por sessão do programa |
| Formulário de propriedades no topo | ✅ | ✅ rótulo à esquerda, duas colunas |
| Lista de seções à esquerda | ✅ | ✅ com ícone por seção |
| Aba `Data` com a grade | ✅ | ✅ a mesma grade dos resultados (filtro, edição, exportar) |
| `Save` / `Revert` / `Refresh` no rodapé | ✅ | ✅ `Ctrl+S` também grava |
| Revisão do SQL antes de gravar (`Persist`) | ✅ | ✅ janela "Review SQL", editável |
| A aba segue o objeto renomeado | ✅ | ✅ |
| Aba `ER Diagram` | ✅ | ➖ diagramas ER fora do escopo pedido |
| Seção `Virtual` (chaves virtuais) | ✅ | ⬜ |

### Seções por tipo

| Tipo | Seções no C-Otter |
|---|---|
| Tabela | Columns, Constraints, Foreign Keys, Indexes, Dependencies, References, Partitions, Triggers, Rules, Policies, Statistics, Permissions, DDL |
| View | Source, Columns, Dependencies, Rules, Triggers, Permissions |
| Materialized view | Source, Columns, Indexes, Dependencies, Statistics, Permissions |
| Foreign table | Columns, Constraints, Dependencies, Permissions, DDL |
| Função / procedure | Source, Parameters, Dependencies, Permissions |
| Role | Members, Member of, DDL |
| Foreign server | User Mappings, Permissions, DDL |
| Banco | Statistics, Permissions, DDL |
| Demais | Statistics e Permissions quando o tipo os tem; DDL (ou Source) |

Cada linha de uma seção é um objeto: duplo clique abre o editor dele, o botão
direito abre o menu.

### Propriedades — os 26 tipos

Todos com consulta de propriedades e DDL conferidos no servidor
(`otter_tests_object_live`, 624 verificações): banco, schema, tabela (comum,
particionada e partição), view, materialized view, foreign table, coluna, índice,
constraint, chave estrangeira, trigger, regra, política, sequência, função,
procedure, agregado, tipo (enum, domínio, composto, range), role, extensão,
tablespace, event trigger, foreign server, foreign data wrapper, user mapping,
linguagem.

| Propriedade editável | Comando | Estado |
|---|---|---|
| Name | `ALTER ... RENAME TO` (coluna, constraint e trigger na forma própria) | ✅ |
| Comment | `COMMENT ON ... IS` (vazio remove: `IS NULL`) | ✅ |
| Owner (combo de roles) | `ALTER ... OWNER TO` | ✅ |
| Schema (combo) | `ALTER ... SET SCHEMA` | ✅ |
| Tablespace (combo) | `ALTER ... SET TABLESPACE` | ✅ com o aviso de que reescreve e trava |
| Atributos do role (7 caixas) | `ALTER ROLE ... [NO]LOGIN/SUPERUSER/...` | ✅ |
| Senha do role | `ALTER ROLE ... PASSWORD` | ✅ em Tools → Change password |
| Fonte de view, função e procedure | `CREATE OR REPLACE` | ✅ |
| Fonte de materialized view, trigger, regra | — | 🟡 somente leitura: não há `OR REPLACE`; gravar exigiria apagar e recriar |
| Demais propriedades (tipo da coluna, limites da sequência, opções da tabela) | `ALTER` específico | 🟡 pelos menus Alter da árvore (coluna) ou por SQL; o formulário as mostra somente leitura |

### DDL mostrado

| Tipo | Origem | Conferência |
|---|---|---|
| Tabela | montado de 5 leituras do catálogo (não existe `pg_get_tabledef`) | ✅ apaga e recria 4 tabelas da fixture; constraints e índices conferem |
| — `serial`, `IDENTITY`, coluna gerada | emitidos como tal, não como `DEFAULT nextval(...)` | ✅ defeito achado pelo teste ao vivo |
| — `PARTITION BY` | de `pg_get_partkeydef` | ✅ |
| Foreign table | `CREATE FOREIGN TABLE`, sem `SERVER`/`OPTIONS` | 🟡 dito no próprio DDL |
| View, mview, índice, constraint, trigger, regra, função | `pg_get_*def` | ✅ apaga e recria cada um |
| Política, sequência, agregado, tipos, schema, banco, role, extensão, tablespace, event trigger, servidor | montado em SQL | ✅ idem |
| Role | **sem a senha**, de `pg_roles` | ✅ teste impede `rolpassword` |
| User mapping | **sem as opções** (guardam a senha remota) | ✅ |

### Permissions

| Elemento | DBeaver | C-Otter |
|---|---|---|
| Roles à esquerda, privilégios à direita | ✅ | ✅ `PUBLIC` primeiro, depois quem já tem concessão |
| Caixa por privilégio (`Permission`) | ✅ | ✅ cada marca abre a revisão do `GRANT`/`REVOKE` |
| `With GRANT` | ✅ | ✅ desmarcar gera `REVOKE GRANT OPTION FOR` |
| `Grant All` / `Revoke All` | ✅ | ✅ |
| `Granted by` | — | ✅ |
| ACL nula = padrão do tipo | ✅ | ✅ `acldefault()`: o dono aparece mesmo em objeto recém-criado |
| `With Hierarchy` | ✅ | ⬜ |
| Privilégio `MAINTAIN` (PostgreSQL 17+) | ✅ | ✅ aparece quando concedido |
| Privilégio de coluna | ✅ | ✅ no editor da coluna |

## 2. Menu do nó

| Item | DBeaver | C-Otter |
|---|---|---|
| View \<tipo\> (`F4`) | ✅ | ✅ |
| View Data | ✅ | ✅ abre o editor na aba Data |
| Read data in SQL console | ✅ | ✅ |
| Create New \<tipo\> (no nó e na pasta) | ✅ | ✅ |
| Submenu Create do schema | ✅ | ✅ tabela, view, mview, sequência, função, procedure |
| Rename (`F2`) | ✅ | ✅ |
| Delete (`Delete`), com Cascade | ✅ | ✅ |
| Tools | ✅ | ✅ ver §4 |
| Import Data / Export Data | ✅ | ✅ ver §5 |
| Copy name / Copy qualified name | ✅ | ✅ |
| Refresh (`F5`) | ✅ | ✅ |
| Generate SQL | ✅ | ✅ nas relações (SELECT, INSERT, UPDATE, DELETE, DDL) |
| Filter, Compare/Migrate, View Diagram | ✅ | ➖ comparação e diagramas fora do escopo pedido; filtro da árvore é o campo do topo |
| `Alt+Insert` para criar | ✅ | ⬜ só pelo menu |

## 3. Diálogos de criação

| Diálogo | Campos do DBeaver | C-Otter |
|---|---|---|
| Create database | Database name, Owner · Template database, Encoding, Tablespace | ✅ |
| Create schema | Schema name, Database, Owner | ✅ |
| Install extensions | Database, Schema, tabela Name/Version/Description | ✅ só as ainda não instaladas |
| Create role | Name, Password, Is user | ✅ |
| Create tablespace | Name, Owner, Location, Options | ✅ gerador testado; não executado (exige diretório no servidor) |
| Função / procedure | Name, Type, Language, Return type → editor com o esqueleto | ✅ o esqueleto compila em `sql` e em `plpgsql` (o do DBeaver, `BEGIN END`, só em plpgsql) |
| Create new Event Trigger | Name, Event Type, Trigger function | ✅ |
| Trigger | Name, Trigger function → fonte | 🟡 formulário com Timing e Event em vez do editor de fonte |
| Sequência | nome → propriedades | ✅ Start, Increment, Minimum, Maximum, Cycle no diálogo |
| Constraint | tipo e colunas | ✅ PRIMARY KEY, UNIQUE, CHECK; colunas digitadas, não marcadas numa lista |
| Foreign key | colunas, tabela e colunas referenciadas, ON DELETE/UPDATE, Deferrable | 🟡 sem Deferrable/Deferred |
| Política | nome → propriedades | ✅ Command, Permissive, Roles, Using, With check |
| Materialized view | nome → fonte | ✅ nome, consulta, With data |
| Tabela, view, coluna, índice | — | ✅ já existiam (`docs/DDL-WRITE.md`) |
| Tipo de dado, agregado, regra, foreign table, servidor, FDW, user mapping, linguagem | — | ➖ o DBeaver também não tem diálogo para eles: são criados por SQL |

## 4. Tools

| Ferramenta | Opções do DBeaver | C-Otter |
|---|---|---|
| Analyze | — (`ANALYZE VERBOSE`) | ✅ tabela, mview, banco |
| Vacuum | Full, Freeze, Analyzed, Disable page skipping (9.6+), Skip locked, Index cleanup, Truncate (12+) | ✅ as opções aparecem conforme a versão |
| Truncate | Only, Restart identity, Cascade | ✅ sempre destrutivo, com confirmação |
| Refresh Materialized View | With data | ✅ |
| Enable / Disable trigger | — | ✅ trigger de tabela e event trigger |
| Reindex | — (não é ferramenta no DBeaver) | ✅ índice, tabela, schema, banco |
| Change password (role) | — | ✅ |
| Backup | Format, Compression, Encoding, Use SQL INSERT, Do not backup privileges, Discard objects owner, Add drop/create database statement, arquivo | ✅ banco, schema ou tabela; `pg_dump` achado no PATH ou na instalação |
| Restore | Format, Clean, Discard objects owner, Create database, arquivo | ✅ `pg_restore`; formato Plain pelo `psql` |
| Global Backup (`pg_dumpall`) | — | ⬜ |
| Execute script (cliente nativo) | — | ⬜ o editor SQL executa scripts |
| Foreign data wrappers configurator | assistente | ⬜ |
| Session Manager | lista, Cancel active query, Terminate session | ✅ os dois botões acima da grade de sessões |
| Lock Manager | quem bloqueia quem | ✅ consulta + os mesmos botões |
| Salvar ferramenta como tarefa | ✅ | ➖ tarefas fora do escopo pedido |

A senha do `pg_dump`/`pg_restore` vai em `PGPASSWORD`, no ambiente do processo —
nunca na linha de comando (teste). O programa roda com `--no-password`: sem
terminal, perguntar seria travar.

## 5. Transferência de dados

| Item | DBeaver | C-Otter |
|---|---|---|
| Exportar o resultado **inteiro** | ✅ assistente | ✅ por cursor no PostgreSQL (2 milhões de linhas em ~5 s); LIMIT/OFFSET no MySQL |
| Progresso e cancelar | ✅ | ✅ |
| Formatos de texto | CSV, JSON, SQL, XML, HTML, Markdown, TXT, DbUnit, código-fonte | ✅ os sete primeiros |
| XLSX, Parquet | ✅ | ➖ formatos binários |
| Exportar para outra tabela/banco | ✅ | ⬜ |
| Importar CSV numa tabela | ✅ | ✅ delimitador adivinhado, cabeçalho, texto de NULL |
| Mapeamento de colunas | ✅ | ✅ por nome, ajustável coluna a coluna; "(skip)" ignora |
| Truncate target table before load | ✅ | ✅ |
| Carga em transação única | opção | ✅ sempre: falha em qualquer lote desfaz tudo |
| Criar a tabela de destino a partir do arquivo | ✅ | ⬜ a tabela precisa existir |
| Importar de XLSX / outro banco | ✅ | ⬜ |

## 6. Conexão

| Item | DBeaver | C-Otter |
|---|---|---|
| Proxy SOCKS | ✅ | ✅ SOCKS5 com usuário e senha, DNS pelo proxy; conferido contra proxy real nos dois protocolos, com TLS por dentro |
| Túnel SSH | ✅ implementação própria (JSch/SSHJ) | 🟡 pelo cliente `ssh` do sistema (ADR 0021): chave ou agente; **senha digitada não** — dito na tela. Caminho de falha conferido; túnel de verdade **não conferido** (sem servidor SSH na máquina de teste) |
| Backup/Restore por proxy | — | ➖ `pg_dump` não fala SOCKS: recusado com a razão |

## Como conferir

```powershell
build\win-release\bin\otter_tests.exe                 # 609 testes
$env:PGHOST = "localhost"
build\win-release\bin\otter_tests_object_live.exe     # 624 verificações, tudo em ROLLBACK
python tools\socks5_test_server.py --port 1081        # + spike_proxy_live <perfil>
build\win-release\bin\spike_ssh_live.exe              # caminho de falha do túnel
```

Na tela, sem tomar o mouse nem o teclado: `OTTER_COMMAND_FILE` aceita
`object`, `section`, `property`, `save object`, `form`, `field`, `ddl dump`,
`ddl execute`, `export`, `import`, `tool` e `session` — ver o comentário de
`MainShell::object_command`.
