# Mapa do SQL Anywhere

Quarto SGBD. Decisão de protocolo e alternativas rejeitadas no
[ADR 0026](adr/0026-sql-anywhere-driver.md).

**O DBeaver Community não tem plugin do SQL Anywhere.** O que ele oferece é o driver
genérico *Sybase jConnect* (e o *Sybase jTDS*), declarado em
`plugins/org.jkiss.dbeaver.ext.mssql/plugin.xml` dentro do datasource `mssql`
(`parent="generic"`): a árvore genérica do JDBC, o dialeto `sybase` e nenhum editor
próprio. Por isso este mapa tem **dois** alvos (diretiva 1):

- o que o DBeaver mostra — a árvore `generic` de `org.jkiss.dbeaver.ext.generic` — é o
  denominador da cobertura (diretiva 4);
- o que o **Sybase Central** (a ferramenta do próprio SGBD) oferece é de onde saem as
  pastas e os comandos que o DBeaver não tem ➕.

Legenda: ✅ implementado e conferido · 🟡 parcial · ⬜ não implementado · ➖ não se aplica
ao SQL Anywhere · ➕ o C-Otter tem e o DBeaver não.

## 1. Protocolo de rede

O SQL Anywhere fala dois protocolos: o nativo (*Command Sequence*, fechado, o do ODBC e
do `dbisql`) e o **TDS 5.0** da Sybase (aberto: "TDS 5.0 Functional Specification"). O
C-Otter fala TDS 5.0 direto (`lib/tdswire/tds5.cpp`), sem ODBC, sem jConnect e sem FreeTDS.

O TDS 5.0 **não** é o TDS do SQL Server (`lib/tdswire/connection.cpp`, 7.4): o cabeçalho
do pacote é o mesmo, e quase mais nada.

| | TDS 7.4 (SQL Server) | TDS 5.0 (SQL Anywhere) |
|---|---|---|
| Login | `LOGIN7`, campos por deslocamento, UTF-16 | registro de campos **fixos** (30 bytes + 1 de tamanho), no conjunto de caracteres do cliente |
| O que o cliente sabe ler | implícito na versão | token `CAPABILITY`, bit a bit |
| Comando | pacote `SQLBatch` em UTF-16LE | token `LANGUAGE` (0x21) num pacote 0x0F, em UTF-8 |
| Cifra | TLS dentro do PRELOGIN | **nenhuma** |
| Senha | embaralhada, dentro do TLS | **em claro** |
| `numeric` | sinal + little-endian | sinal + **big-endian** |
| Banco | campo próprio do login | campo "nome do servidor" (o `ServiceName` do jConnect) |

### Pacotes e tokens

| Item | Estado |
|---|---|
| Login: registro fixo + `CAPABILITY` (pedido e recusa) | ✅ |
| `LANGUAGE` (comando), `ATTENTION` 0x06 (cancelar na mesma conexão) | ✅ |
| `ROWFMT` 0xEE e `ROWFMT2` 0x61 (este traz catálogo, dono, tabela e coluna de origem) | ✅ |
| `ROW`, `DONE`, `DONEPROC`, `DONEINPROC` (estado, transação, linhas afetadas) | ✅ |
| `EED` 0xE5, `ERROR`, `INFO` (`MESSAGE ... TO CLIENT` e `PRINT` vão para o painel de saída) | ✅ |
| `ENVCHANGE`, `LOGINACK`, `CAPABILITY`, `ORDERBY`, `CONTROL`, `RETURNSTATUS` | ✅ |
| `PARAMFMT`, `PARAMFMT2`, `PARAMS` | ✅ lidos |
| `RETURNVALUE` 0xAC (parâmetros `OUT` de um `CALL`) | ✅ viram uma linha de resultado quando o lote não tem outro |
| Vários conjuntos de resultado num lote | 🟡 lidos até o fim; a grade mostra o **primeiro** |
| `DYNAMIC` (comando preparado, parâmetros tipados) | ⬜ os valores vão no texto |
| `CURSOR*` (cursor no servidor) · carga em massa | ⬜ |
| Cifra do canal | ⬜ o servidor não cifra o TDS (a cifra dele, `-ec`, é do protocolo nativo) |

### Valores

| Tipo | Estado |
|---|---|
| `tinyint` (sem sinal), `smallint`, `integer`, `bigint`, `unsigned *`, `bit` | ✅ |
| `real`, `double` · `money`, `smallmoney` | ✅ |
| `numeric`, `decimal` até 38 dígitos (sinal + big-endian) | ✅ |
| `date`, `time`, `timestamp` | ✅ pedidos ao servidor como **texto ISO** (ver abaixo) |
| `char`, `varchar`, `long varchar`, `nchar`, `nvarchar`, `xml` (UTF-8) | ✅ |
| `binary`, `varbinary`, `long binary`, `image` → `0x...` | ✅ |
| `uniqueidentifier` (chega como `binary(16)` com tipo de usuário 81) | ✅ |
| `ST_Geometry` e os espaciais | 🟡 mostrados como binário |

### O que só o servidor mostrou

Nada disto está na especificação; tudo foi achado conferindo contra o servidor, e cada
item tem verificação em `tests/integration/test_sqlanywhere_live.cpp`.

- **A conexão TDS nasce com as opções do ASE**: `quoted_identifier` desligado (aspas duplas
  viram *texto*), `allow_nulls_by_default` desligado, `chained` desligado. O driver as repõe
  nos padrões do banco ao conectar (`sqlanywhere_session_options`).
- **`date` chega como `DATETIME`**, que não existe antes de 1753: `0001-01-01` viraria
  `1753-01-01` sem aviso. O driver liga `return_date_time_as_string` e fixa os formatos
  ISO — por conexão, sem tocar o banco.
- **Texto vazio chega como um espaço**: o TDS não tem `''`. O driver devolve vazio para
  uma coluna de texto que é só um espaço; um espaço de verdade fica indistinguível.
- **Coluna longa nula chega com ponteiro e tamanho zero**, não com o marcador de nulo.
- **O comando é convertido para o conjunto de caracteres do banco** (cp1252 no Windows)
  antes de ser lido: um literal com `日本` chega como `??`, mesmo em `N'...'`. O que não é
  ASCII vai em `UNISTR('\uXXXX')` — e a barra, dentro dele, vai como `\`.
- **Banco que não existe não é recusado**: o servidor conecta no padrão. O driver confere
  `db_name()` contra o nome pedido e recusa dizendo qual seria usado.
- **No modo manual (`chained`) o `DONE` diz "em transação" sempre**, até logo depois de um
  `COMMIT`. O estado de verdade vem de `connection_property('TransactionStartTime')`.
- **`sa_get_table_definition` troca opções da conexão** (`chained`, `date_format`…). O
  catálogo as guarda antes e repõe depois.
- **`ROLLBACK TO SAVEPOINT` solta o ponto**: o `RELEASE` seguinte dá -220, que o driver
  trata como "já está solto".
- **`ORDER BY` numa tabela derivada é ignorado**; a paginação põe `TOP n START AT m` no
  próprio `SELECT` do usuário quando pode.
- **O servidor pessoal (`dbeng16`) só escuta TCP se mandado**: `-x tcpip`. Sem isso só
  aceita memória compartilhada, que o TDS não usa.

## 2. Árvore de objetos

### Contra o DBeaver (a árvore `generic`)

| # | Pasta do DBeaver | Estado | Observação |
|---|---|---|---|
| 1 | Catalog (banco) | ➖ | uma conexão TDS fala com **um** banco; o nó da conexão é ele |
| 2 | Schema | ✅ | o "schema" é o **dono** dos objetos (um usuário); o do usuário conectado nasce aberto |
| 3 | Tables | ✅ | base e temporárias globais; contagem de linhas de `SYSTAB.count` |
| 4 | — Columns | ✅ | coluna calculada (`COMPUTE`) no lugar do default |
| 5 | — Unique Keys | 🟡 | junto com os `CHECK`, na pasta "Constraints" (como nos outros SGBDs) |
| 6 | — Foreign Keys | ✅ | ação referencial lida de `SYSTRIGGER` |
| 7 | — Indexes | ✅ | PK, FK, índice e índice de texto |
| 8 | — References | ✅ | |
| 9 | — Triggers | ✅ | |
| 10 | Views | ✅ | |
| 11 | — Columns | ✅ | |
| 12 | Indexes (todos os do schema) | ✅ | |
| 13 | Procedures | ✅ | procedures e funções na mesma pasta |
| 14 | — Procedure columns | ✅ | parâmetros, retorno e colunas do resultado |
| 15 | Functions (pasta própria) | 🟡 | dentro de "Procedures", com `→ tipo` |
| 16 | Sequences | ✅ | |
| 17 | Synonyms | ➖ | o SQL Anywhere não tem |
| 18 | Triggers (do banco) | ➖ | não há trigger de DDL; o equivalente são os eventos |
| 19 | Table Triggers (todos os do schema) | ⬜ | |
| 20 | Data Types | ✅ | os domínios |

**14 ✅ + 2 🟡 de 17 aplicáveis.**

### Do Sybase Central ➕

O que a árvore genérica do DBeaver não mostra e a do C-Otter sim:

| Pasta | Fonte | Editor |
|---|---|---|
| Materialized Views | `SYSTAB.table_type = 2` | ✅ |
| Events (com agenda; desligado sai marcado) | `SYSEVENT`, `SYSSCHEDULE` | ✅ |
| Dependencies (o que a view usa) | `SYSDEPENDENCY` | lista |
| Users (com o que cada um recebeu) | `SYSUSER`, `SYSROLEGRANTS`, `SYSTABLEPERM` | ✅ |
| Roles → Members, Roles | `SYSUSER.user_type`, `SYSROLEGRANTS` | ✅ |
| Login Policies → Options | `SYSLOGINPOLICY`, `SYSLOGINPOLICYOPTION` | lista |
| Storage → Dbspaces (com o arquivo) | `SYSDBSPACE`, `SYSDBFILE` | lista |
| Remote Servers → External Logins | `SYSSERVER`, `SYSEXTERNLOGIN` | lista |
| Web Services | `SYSWEBSERVICE` | lista |
| Publications | `SYSPUBLICATION` | lista |
| Text Configuration Objects | `SYSTEXTCONFIG` | lista |
| External Environments | `SYSEXTERNENV` | lista |
| Spatial Reference Systems | `ST_SPATIAL_REFERENCE_SYSTEMS` | lista |
| Administer → Session Manager, Lock Manager, Checkpoint, Validate database, Backup database | `sa_conn_info`, `sa_locks` | — |
| System Info → Connection / Server / Database properties, Connection options, Database options | `sa_conn_properties`, `sa_eng_properties`, `sa_db_properties`, `SYSOPTIONS` | — |

"Lista" = aparece na árvore, com detalhe e dica, e não tem editor nem formulário — criar e
alterar esses objetos é por SQL.

Do Sybase Central **não** estão na árvore: System Triggers, Text Indexes (aparecem como
índice da tabela), Login Mappings, LDAP Servers, Certificates, Directory Access Servers,
SQL Remote / MobiLink (usuários, assinaturas, perfis de sincronização), Maintenance Plans
e Mirror Servers.

`SYSUSER.user_type`, conferido criando um de cada num banco de rascunho: bit `0x01` =
papel puro (não entra no banco), `0x02` = usuário estendido como papel, `0x08` = usuário.
Os `SYS_..._ROLE` são os privilégios de sistema vistos como papel, e ficam fora das listas.

## 3. Metadados

`src/db/catalog_sqlanywhere.cpp`, tudo das views `SYS.SYS*` — colunas de nome reservado
entre colchetes (`[unique]`, `[option]`). O DDL de tabela é o do servidor
(`sa_get_table_definition`); o de evento é montado de `SYSEVENT` + `SYSSCHEDULE`, porque o
catálogo só guarda o corpo do tratador.

A origem de cada coluna do resultado (para a grade editável) vem de
`sa_describe_query(sql)`: tipo exato, dono, tabela e coluna base — mesmo sob apelido —
e nulidade. Só para `SELECT`/`WITH` **sem `INTO`** (descrever um `SELECT ... INTO` cria a
tabela); para `CALL` o servidor não sabe, e a grade fica somente leitura.

## 4. Editor de objeto, formulários e ferramentas

O DBeaver não tem nada disto para o Sybase: é tudo ➕.

| Objeto | Propriedades | Fonte / DDL | Criar | Renomear | Comentar | Apagar | Permissões |
|---|---|---|---|---|---|---|---|
| Tabela | ✅ | ✅ `sa_get_table_definition` | ✅ | ✅ | ✅ | ✅ | ✅ |
| Coluna · índice · constraint · FK | na tabela | — | ✅ | ✅ | ✅ (constraint não) | ✅ | — |
| View | ✅ | ✅ gravado por `ALTER` | ✅ | ⬜ o SGBD não renomeia | ✅ | ✅ | ✅ |
| View materializada | ✅ | ✅ só leitura | ⬜ por SQL | ⬜ | ✅ | ✅ | ✅ |
| Procedure · função | ✅ | ✅ gravado por `ALTER` | ✅ esqueleto no editor | ⬜ | ✅ | ✅ | ✅ `EXECUTE` |
| Trigger | ✅ | ✅ gravado por `ALTER` | ✅ esqueleto no editor | ⬜ | ✅ | ✅ | — |
| Evento | ✅ | ✅ `CREATE EVENT` montado; gravado por `ALTER` | ✅ esqueleto no editor | ⬜ | ✅ | ✅ | — |
| Sequence | ✅ | ✅ | ✅ | ⬜ | ✅ | ✅ | ✅ `USAGE` |
| Domínio | ✅ | ✅ | ✅ | ⬜ | ⬜ | ✅ | — |
| Usuário | ✅ | — | ✅ nome, senha, política de login | ⬜ | ✅ | ✅ `DROP USER` | lista do que recebeu |
| Papel | ✅ | — | ✅ | ⬜ | ✅ | ✅ `DROP ROLE ... WITH REVOKE` | membros |

O dono de um objeto não se troca (nem o "schema", que é o dono): os dois campos recusam
com o motivo.

**Tools:**

| Onde | Comando |
|---|---|
| Tabela | `VALIDATE TABLE`, `REORGANIZE TABLE`, `CREATE STATISTICS`, `TRUNCATE TABLE` (confirma a transação — a revisão avisa) |
| View | `ALTER VIEW ... ENABLE` / `DISABLE` |
| View materializada | `REFRESH MATERIALIZED VIEW`, `ENABLE` / `DISABLE` |
| Evento | `ALTER EVENT ... ENABLE` / `DISABLE`, `TRIGGER EVENT` |
| Usuário (e o nó do schema, que é um usuário) | `ALTER USER ... IDENTIFIED BY` |
| Banco (pasta Administer) | `CHECKPOINT`, `VALIDATE DATABASE`, `BACKUP DATABASE DIRECTORY` — a pasta é do **servidor**, e a tela diz |

**Sessões:** `sa_conn_info()` numa aba de resultado, com *Terminate session*
(`DROP CONNECTION n`) sobre a linha escolhida; `sa_locks()` no Lock Manager.

**Dashboard:** sessões, pedidos por segundo, transações por segundo, E/S de disco, uso do
cache e tamanho do banco.

## 5. SQL gerado: o que o Watcom SQL tem de diferente

| Assunto | Outros SGBDs | SQL Anywhere |
|---|---|---|
| Citação | `"nome"`, `` `nome` ``, `[nome]` | `"nome"` (e `[nome]` é aceito na leitura) |
| Texto fora do ASCII | `'texto'`, `N'texto'` | `UNISTR('ç...')`; barra como `\` |
| Barra invertida | escape no MySQL | **não** é escape num literal comum |
| Booleano · binário | `TRUE`/`FALSE` · literal | `1`/`0` (`bit`) · `0x...` cru |
| Página da grade | `LIMIT`/`OFFSET`, `OFFSET`/`FETCH` | `SELECT [DISTINCT] TOP n START AT m` no próprio `SELECT`; com filtro ou `UNION`, embrulhado em `SELECT TOP ... * FROM (...) AS otter_page` |
| Limite já escrito | `LIMIT`, `TOP` | `TOP` e `FIRST` |
| Não se pagina | `EXPLAIN`, `EXEC` | `SELECT ... INTO`, `CALL` |
| Coluna nova | nulidade implícita | `NULL` **explícito** (o padrão depende de uma opção da conexão) |
| `ALTER TABLE` | `ADD COLUMN`, `ALTER COLUMN` | `ADD`, `ALTER nome tipo`, `DROP nome`, `RENAME nome TO novo` |
| Apagar índice | `DROP INDEX nome` | `DROP INDEX dono.tabela.indice` |
| Chave estrangeira | `ADD CONSTRAINT nome FOREIGN KEY` · `DROP CONSTRAINT` | `ADD FOREIGN KEY nome (...)` (o nome é o "papel") · `DROP FOREIGN KEY nome` |
| Gravar o fonte | `CREATE OR REPLACE` | `ALTER` (preserva as permissões) |
| Transação do script | `BEGIN` | `BEGIN TRANSACTION` (`BEGIN` sozinho abre um bloco) |
| DDL em transação | reversível no PostgreSQL | **confirma** a transação |
| Comentários | `--`, `/* */` | também `//` |
| Separador de script | `;` | `;` e `go`; o corpo `BEGIN ... END` de rotina, trigger e evento é **um** comando (`END IF`, `END LOOP`, `END FOR`, `END CASE` não o fecham); a forma T-SQL sem bloco vai até o `go` |
| Chamar rotina | `CALL p(:a)` | `CALL p(:a)` (os `OUT` voltam como linha) · função: `SELECT f(:a)` |
| Modo manual | `BEGIN` implícito | `chained = On` |
| Isolamento | `SET TRANSACTION` | `SET TEMPORARY OPTION isolation_level` |

## 6. Diálogo de conexão

O DBeaver usa a `SQLServerConnectionPage` para o Sybase (`configurator dataSource=
"sqlserver,mssql,sybase"`). O que vale aqui:

| Campo | Estado |
|---|---|
| "SQL Anywhere" no catálogo de drivers (categoria SQL, porta 2638, ícone da Sybase) | ✅ |
| Host · Port · Database | ✅ a página explica que *Database* é o nome de um banco **em execução no servidor**, não o arquivo, e que o servidor pessoal precisa de `-x tcpip` |
| Authentication | ✅ só *Database Native*: login integrado e Kerberos são do protocolo nativo |
| User name · Password · Save password | ✅ |
| SSL | ⬜ desabilitado, com o motivo e a saída (túnel SSH) |
| SSH · Proxy (SOCKS5) | ✅ os mesmos dos outros drivers |
| Driver properties | ✅ viram `SET TEMPORARY OPTION nome = valor`; nome desconhecido falha a conexão |
| Default schema · Read-only · Session role | ⬜ não existem por sessão no SQL Anywhere; nada é emitido, e os comandos da tela recusam com o motivo |
| Importar do DBeaver | ✅ perfis com driver `sybase_jconn`, `sybase_jtds` ou `sypase_jconn` abrem pelo driver SQL Anywhere (antes iam ao do SQL Server, por estarem no provider `mssql`) |
| Gravar para o DBeaver | ✅ provider `mssql`, driver `sybase_jconn`, URL `jdbc:sybase:Tds:host:porta?ServiceName=banco` |
| URL | ✅ `sqlanywhere://`, `jdbc:sybase:Tds:...` e `jdbc:jtds:sybase://...` |

## 7. Preparar o servidor

O que o C-Otter precisa do lado do SQL Anywhere é só **TCP ligado**; nada é instalado no
servidor nem no cliente.

| Servidor | Como |
|---|---|
| De rede (`dbsrv16`) | já escuta TCP na 2638 por padrão |
| Pessoal (`dbeng16`), o do banco de demonstração | iniciar com `-x tcpip` — sem isso só aceita memória compartilhada. Só atende conexões da própria máquina |

```
dbspawn -f dbeng16.exe -n demo16 -x tcpip(port=2638) "<caminho>\demo.db"
dbstop -y demo16
```

Para subir sozinho: um serviço do Windows (o utilitário `dbsvc` do SGBD; pede
administrador, e **não foi criado nem testado aqui**) ou, sem administrador, um atalho com a primeira linha na pasta *Inicializar*
do usuário — é o que `build/sa_setup.ps1` cria na máquina de desenvolvimento, junto com a
conexão salva "SQL Anywhere 16 Demo". O DSN ODBC que a instalação cria ("SQL Anywhere 16
Demo") inicia o servidor **sem** TCP, e só quando um cliente ODBC conecta: não serve.

Se o banco estiver com as opções `quoted_identifier`, `date_format` etc. fora do padrão,
nada precisa ser mudado nele: o driver as ajusta só na própria conexão (seção 1).

## 8. Estado

Conferido em 2026-10-01 contra o SQL Anywhere 16.0.0.2043 local (banco `demo`, cp1252,
sem distinção de maiúsculas), iniciado com `-x tcpip(port=2638)`.

| Suíte | Verificações |
|---|---|
| `otter_tests` (`test_sqlanywhere.cpp`) | 30 testes: vetores do TDS 5.0, registro de login, SQL gerado, `UNISTR`, paginação, lotes, URL, mapeamento dos drivers Sybase |
| `otter_tests_sqlanywhere_live` | 337: recusas de conexão, valores, catálogo, listas, `ALTER`, editor, permissões, grade, paginação, importação, transações, ferramentas, `DROP CONNECTION` numa sessão vítima. Só leitura no `demo`; a escrita é num banco de rascunho criado em TEMP e apagado no fim |
| Pelo canal de comandos | `build/sa_channel.ps1` (SQL de cada formulário e ferramenta, sem executar) e `build/sa_exec.ps1` (os mesmos **executados** num banco de rascunho, com o efeito lido do servidor) |
| Na tela | `build/sa_shots.ps1`, nos três temas (escuro, claro, âmbar): catálogo de drivers, página do driver e aba SSL, árvore com as listas, grade, editor de tabela (colunas, permissões, DDL) e de procedure, menus de tabela e de usuário, formulários de usuário, domínio e backup — capturas ampliadas |

### Defeitos que só apareceram nos testes novos

- **`CREATE PROCEDURE ... END; SELECT ...` ia ao servidor como um comando só.** O separador
  tratava o lote inteiro como atômico, como no SQL Server; no Watcom SQL o corpo é um bloco
  e o comando acaba no `END` dele. E contar qualquer `END` fechava o corpo no primeiro
  `END IF`.
- **Perfil do DBeaver com driver Sybase abria pelo driver do SQL Server** (mesmo provider,
  outro protocolo).

### Defeitos que só apareceram na tela

- **O ícone da Sybase sumia no tema escuro**: azul-marinho sobre transparente. O arquivo é
  o original do DBeaver; num fundo escuro ele ganha uma placa clara por trás.
- **Na pasta Users o ícone vinha antes da seta**, e a linha ficava um passo à esquerda das
  pastas irmãs. Vale também para o MySQL, que usa a mesma função.
- **Datas nas propriedades do editor de objeto saíam com a fração inteira**
  (`17:08:28.000000`): a consulta do programa não passava pelo ajuste que a grade faz.

### Aberto, não corrigido

- **Sem cifra**: a senha e os dados trafegam em claro. O diálogo diz, e recusa "Use SSL".
- **SAP ASE não é suportado**: o login TDS 5.0 passa, mas o catálogo é outro. A conexão
  falha dizendo isso (não conferido contra um ASE: não há um aqui).
- Vários conjuntos de resultado: só o primeiro chega à grade.
- Plano de execução (`GRAPHICAL_PLAN`) não é lido: o comando *Explain* fica desabilitado.
- "Execute SQL script natively" (`dbisql`) recusa com o motivo.
- Texto vazio × um espaço são indistinguíveis; binário longo vazio chega como `0x20`.
- As pastas marcadas "lista" não têm editor nem formulário; as do Sybase Central que
  ficaram de fora estão na seção 2.
- Remote Servers, Web Services e Publications estão vazias no `demo`: as consultas
  respondem, mas nenhuma linha foi vista.
- `BACKUP DATABASE` e `VALIDATE DATABASE`: SQL conferido pelo canal, **não executados**.
- A máscara de dias da agenda de um evento foi conferida com um evento só.
- **Não fotografados**: o submenu *Tools* aberto, o Session Manager com o botão, o
  dashboard, as pastas Events e System Info abertas e o editor de view, trigger, evento,
  sequence, domínio, usuário e papel. O que fazem foi conferido pelo canal e na suíte.
- Conferência só pelo canal de comandos: ele prova que o comando funciona, não que o
  clique ou a tecla chegam a ele (diretiva 11-A).
