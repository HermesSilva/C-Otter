# Mapa do SQL Server no DBeaver

Extraído de `plugins/org.jkiss.dbeaver.ext.mssql` e `...ext.mssql.ui` do repositório do
DBeaver (diretiva 1), **antes** do código. Decisão de protocolo e alternativas
rejeitadas no [ADR 0024](adr/0024-sql-server-driver.md).

Legenda: ✅ implementado e conferido · 🟡 parcial · ⬜ não implementado · ➕ o C-Otter tem
e o DBeaver (Community) não.

## 1. Protocolo de rede

O DBeaver fala com o SQL Server pelo driver JDBC da Microsoft. O C-Otter fala **TDS 7.4**
direto (`lib/tdswire`, [MS-TDS]), sem ODBC, sem FreeTDS.

### Pacote

Cabeçalho de 8 bytes: tipo, estado (bit 0 = fim da mensagem), tamanho (big-endian), SPID,
número do pacote, janela. Uma mensagem pode ocupar vários pacotes.

| Tipo | Uso | Estado |
|---|---|---|
| `0x12` PRELOGIN | versão, escolha de cifra, instância | ✅ |
| `0x10` LOGIN7 | usuário e senha, ou SSPI | ✅ |
| `0x11` SSPI | continuação do aperto de mão Negotiate | ✅ |
| `0x01` SQLBatch | o texto do comando, em UTF-16LE, com `ALL_HEADERS` (descritor da transação) | ✅ |
| `0x06` ATTENTION | cancelar a consulta em curso, na mesma conexão | ✅ |
| `0x04` resposta | fluxo de tokens | ✅ |
| `0x03` RPC | chamada com parâmetros tipados (`sp_executesql`) | ⬜ os valores vão no texto |
| `0x07` Bulk Load | `INSERT BULK` | ⬜ |
| `0x0E` Transaction Manager | transação distribuída | ⬜ |

### Aperto de mão e cifra

O TLS do TDS é peculiar: o aperto de mão vai **dentro** de pacotes PRELOGIN, e só depois
dele a conexão vira TLS puro. `net::TlsOptions::handshake_write/handshake_read` existem
para isso.

| Caso | Estado |
|---|---|
| Só o pacote de login cifrado (`ENCRYPT_OFF`) — o padrão: a senha nunca vai em claro | ✅ |
| Conexão inteira cifrada (`ENCRYPT_ON`, caixa "Use SSL") | ✅ |
| Conferir o certificado (`verify-ca`/`verify-full`) ou confiar nele ("Trust Server Certificate") | ✅ |
| TDS 8.0 (`Encrypt=strict`, TLS antes do PRELOGIN) | ⬜ |
| Linux: TLS e SSPI | ⬜ `tls_openssl.cpp` é esboço; `sspi_stub.cpp` recusa com o motivo |

### Autenticação

Os oito `authModel` do `plugin.xml`:

| Modelo | Estado |
|---|---|
| SQL Server Authentication | ✅ a senha vai embaralhada (nibbles trocados, XOR `0xA5`) **dentro** do TLS do login |
| Windows Authentication | ✅ SSPI `Negotiate` (Kerberos ou NTLM, quem decide é o Windows), SPN `MSSQLSvc/<fqdn>:<porta>` |
| NTLM (com usuário e senha de domínio digitados) | ⬜ |
| Active Directory — Password / MSI / MFA / Integrated | ⬜ exigem o fluxo OAuth do Entra ID |
| Custom | ⬜ |

O diálogo só oferece os dois que conectam (diretiva 6).

### Fluxo de tokens

| Token | Estado |
|---|---|
| `COLMETADATA`, `ROW`, `NBCROW` | ✅ |
| `DONE`, `DONEPROC`, `DONEINPROC` (linhas afetadas, fim de cancelamento) | ✅ |
| `ERROR`, `INFO` (PRINT e avisos vão para o painel de saída) | ✅ |
| `ENVCHANGE` (banco, tamanho de pacote, início/fim de transação) | ✅ |
| `LOGINACK`, `SSPI`, `ORDER`, `RETURNSTATUS`, `FEATUREEXTACK` | ✅ |
| `RETURNVALUE` (parâmetro de saída de RPC) | ⬜ não há RPC |
| Vários conjuntos de resultado num lote | 🟡 lidos até o fim; a grade mostra o **primeiro** |

### Valores

O TDS manda os valores em **binário**; `lib/tdswire/value.cpp` os converte para o texto
que a grade usa. Conferidos com vetores calculados pela especificação
(`tests/unit/test_mssql.cpp`) e contra o servidor.

| Tipo | Estado |
|---|---|
| `tinyint` (sem sinal), `smallint`, `int`, `bigint`, `bit` | ✅ |
| `real`, `float` | ✅ |
| `money`, `smallmoney` (metade alta primeiro) | ✅ |
| `decimal`, `numeric` até 38 dígitos | ✅ |
| `datetime`, `smalldatetime`, `date`, `time`, `datetime2`, `datetimeoffset` | ✅ |
| `uniqueidentifier` (os três primeiros grupos invertidos) | ✅ |
| `char`, `varchar`, `text` (pela página de código do collation) · `nchar`, `nvarchar`, `ntext` | ✅ |
| `varchar(max)`, `nvarchar(max)`, `varbinary(max)`, `xml` (PLP, em pedaços) | ✅ |
| `binary`, `varbinary`, `image` → `0x...` | ✅ |
| `sql_variant` | ✅ |
| `geometry`, `geography`, `hierarchyid` (UDT CLR) | 🟡 mostrados como binário |

## 2. Árvore de objetos

O `<tree>` do `plugin.xml`, pasta por pasta. Denominador: **30** pastas.

| # | Pasta | Estado | Observação |
|---|---|---|---|
| 1 | Databases | ✅ | um nó por banco, com tamanho; uma sessão por banco, aberta ao expandir (ADR 0018) |
| 2 | Schemas | ✅ | `dbo` nasce aberto |
| 3 | Tables | ✅ | linhas e tamanho de `sys.partitions`, sem varrer |
| 4 | — Columns | ✅ | identity e coluna calculada aparecem no lugar do default |
| 5 | —— Extended Properties (coluna) | ⬜ | só `MS_Description`, como comentário |
| 6 | — Unique Keys | 🟡 | junto com a de baixo, numa pasta "Constraints" (como nos outros dois SGBDs) |
| 7 | — Check constraints | 🟡 | idem |
| 8 | — Foreign Keys | ✅ | |
| 9 | — Indexes | ✅ | |
| 10 | — References | ✅ | |
| 11 | — Triggers | ✅ | desligado sai marcado |
| 12 | — Extended Properties (tabela) | ⬜ | só `MS_Description` |
| 13 | External Tables | ⬜ | PolyBase |
| 14 | — Columns | ⬜ | |
| 15 | Views | ✅ | |
| 16 | — Columns | ✅ | |
| 17 | — Triggers | ✅ | |
| 18 | — Extended Properties | ⬜ | |
| 19 | Indexes (todos os do schema) | ✅ | |
| 20 | Procedures | ✅ | procedures e funções (escalar, de tabela, CLR); parâmetros com `OUTPUT` |
| 21 | Sequences | ✅ | SQL Server 2012+ |
| 22 | Synonyms | ✅ | lista com o alvo; sem editor |
| 23 | Triggers (todos os do schema) | ⬜ | |
| 24 | Data Types | ✅ | alias e tipos de tabela |
| 25 | — Columns (tipo de tabela) | ✅ | como atributos |
| 26 | — Unique Keys (tipo de tabela) | ⬜ | |
| 27 | Database triggers | ✅ | lista; sem editor |
| 28 | Security | ✅ | |
| 29 | — Logins | ✅ | desabilitado sai marcado |
| 30 | Administer → Session Manager | ✅ | abre numa aba de resultado, com o botão acima da grade |

**21 ✅ + 2 🟡 de 30.** Acrescentados: `Dependencies` em tabela, view e rotina ➕
(`sys.sql_expression_dependencies`) e `Lock Manager` ➕ (quem espera por quem).

Divergências conscientes:

- "Show All Databases" do diálogo do DBeaver não existe: a lista de bancos vem sempre. A
  conexão do SQL Server costuma apontar para `master`, onde não há nada do usuário.
- "Show All Schemas" idem: todos os schemas aparecem, menos `sys`,
  `INFORMATION_SCHEMA`, `guest` e os dos papéis fixos (`db_owner`…), que nunca têm objeto.
- A pasta de tipos diz "Data types" (o rótulo comum aos três SGBDs); no DBeaver do SQL
  Server é "Data Types".

## 3. Metadados

`src/db/catalog_mssql.cpp`, tudo das views `sys.*` (não de `INFORMATION_SCHEMA`, que
esconde identity, colunas calculadas e índices). Comentário = a propriedade estendida
`MS_Description`, a mesma que o SSMS e o DBeaver usam.

A origem de cada coluna do resultado (para a grade editável) não vem no `COLMETADATA`:
o driver pergunta ao servidor com `sp_describe_first_result_set ...
@browse_information_mode = 1`, só para `SELECT`/`WITH`. Quando o servidor não sabe
(SQL dinâmico, tabela temporária) a grade fica somente leitura.

## 4. Editor de objeto, formulários e ferramentas

Os `manager` (o que o DBeaver sabe criar, alterar e apagar):

| Manager | Estado | Como |
|---|---|---|
| `SQLServerDatabaseManager` | ✅ | criar (nome; ➕ collation), renomear, apagar |
| `SQLServerTableManager` | ✅ | criar, renomear (`sp_rename`), mover de schema (`ALTER SCHEMA TRANSFER`), comentar, apagar; DDL montado do catálogo |
| `SQLServerTableColumnManager` | ✅ | `ADD` (sem `COLUMN`), `ALTER COLUMN` com tipo **e** nulidade, default como constraint, `sp_rename`, comentário |
| `SQLServerUniqueKeyManager` | ✅ | PK e UNIQUE |
| `SQLServerCheckConstraintManager` | ✅ | |
| `SQLServerForeignKeyManager` | ✅ | `RESTRICT` vira `NO ACTION` |
| `SQLServerIndexManager` | 🟡 | único e não único; sem colunas incluídas nem tipo (columnstore…) |
| `SQLServerViewManager` | ✅ | `CREATE OR ALTER VIEW`; o fonte editado é gravado por `ALTER` |
| `SQLServerProcedureManager` | ✅ | esqueleto no editor; fonte gravado por `ALTER` |
| `SQLServerTableTriggerManager` | ✅ | esqueleto, `ALTER`, `ENABLE`/`DISABLE TRIGGER` |
| `SQLServerLoginManager` | ✅ | criar (➕ banco padrão), renomear, senha, apagar |
| `SQLServerSynonymManager` | 🟡 | gerador testado no servidor; sem formulário |
| `SQLServerExtendedPropertyManager` | 🟡 | só `MS_Description` |
| `SQLServerDataTypeManager` | ⬜ | |
| `SQLServerTableTypeManager` | ⬜ | |
| `SQLServerExternalTableManager` | ⬜ | |

Aba **Permissions**: usuários e papéis do banco (`sys.database_principals`), `GRANT` /
`REVOKE` em `OBJECT::` e `SCHEMA::`, com `WITH GRANT OPTION` — o DBeaver Community não
tem essa aba para o SQL Server ➕.

**Tools** — o plugin do DBeaver não tem submenu de ferramentas para o SQL Server. Os
equivalentes do que PostgreSQL e MySQL oferecem ➕:

| Onde | Comando |
|---|---|
| Tabela | `UPDATE STATISTICS`, `ALTER INDEX ALL ... REBUILD` / `REORGANIZE`, `DBCC CHECKTABLE`, `TRUNCATE TABLE` |
| Trigger | `ENABLE` / `DISABLE TRIGGER` |
| Login | `ALTER LOGIN ... WITH PASSWORD` |
| Banco | `BACKUP DATABASE ... TO DISK` e `RESTORE DATABASE ... FROM DISK` — o arquivo é do **servidor**, e a tela diz |

**Sessões** (`SQLServerSessionEditor`): `KILL` sobre a linha escolhida ✅; o filtro "Only
connections" ⬜.

**Dashboard** ➕: sessões, lotes por segundo, transações por segundo, E/S de arquivo e
tamanho do banco, das DMVs (pedem `VIEW SERVER STATE`).

## 5. SQL gerado: o que o T-SQL tem de diferente

| Assunto | PostgreSQL / MySQL | SQL Server |
|---|---|---|
| Citação | `"nome"` / `` `nome` `` | `[nome]`, com `]` dobrado |
| Texto Unicode | `'texto'` | `N'texto'` — só quando há algo fora do ASCII, para a comparação com `varchar` continuar usando o índice |
| Booleano | `TRUE` / `FALSE` | `1` / `0` (`bit`) |
| Binário | literal de texto | `0x...` cru |
| Página da grade | `LIMIT n OFFSET m` | `ORDER BY ... OFFSET m ROWS FETCH NEXT n ROWS ONLY`; sem `ORDER BY`, `(SELECT NULL)`; com `DISTINCT`/`UNION`, `ORDER BY 1` |
| Limite já escrito | `LIMIT`, `FETCH` | também `TOP` |
| Não se pagina | `EXPLAIN`, `SHOW` | `SELECT ... INTO`, `FOR XML/JSON`, `OPTION (...)`, `EXEC` |
| Subconsulta com `ORDER BY` (filtro, contagem) | aceita | só com `OFFSET 0 ROWS` |
| `WITH` em subconsulta | aceita | não: filtro e contagem recusam, com o motivo |
| Transação do script | `BEGIN` … `COMMIT` | `BEGIN TRANSACTION` … `COMMIT TRANSACTION` |
| Lote de importação | qualquer tamanho | no máximo 1000 linhas por `VALUES` |
| Separador de script | `;` (e `DELIMITER`, `$$`) | `GO` sozinho na linha; lote com `DECLARE` ou `CREATE PROCEDURE/FUNCTION/TRIGGER/VIEW` vai inteiro; `ELSE` fica com o `IF` |
| Chamar rotina | `CALL p(:a)` / `SELECT * FROM f(:a)` | `EXEC [s].[p] @a = :a`, com `DECLARE` e `SELECT` para cada `OUTPUT` |
| Modo manual | `BEGIN` implícito do driver | `SET IMPLICIT_TRANSACTIONS ON` |

## 6. Diálogo de conexão

`SQLServerConnectionPage`:

| Campo | Estado |
|---|---|
| Host · Port (1433) · Database/Schema | ✅ |
| Authentication: SQL Server / Windows | ✅ |
| User name · Password · Save password | ✅ |
| Trust Server Certificate | ✅ ligado ao modo da aba SSL (`require` × `verify-full`) |
| Show All Databases · Show All Schemas | ⬜ sempre ligados (ver seção 2) |
| Encrypt Password | ⬜ sem sentido aqui: o login é sempre cifrado |
| SSL: keystore, Certificate hostname | ⬜ |
| Instância nomeada (`host\instância`) | ✅ a porta é perguntada ao SQL Server Browser do servidor (UDP 1434, `lib/tdswire/browser.cpp`); uma porta diferente de 1433 no perfil dispensa a consulta. A página diz qual dos dois vale. Com túnel SSH ou proxy o UDP não passa: informar a porta |
| Senha não salva | ✅ diálogo `'<conexão>' Authentication` antes de conectar (o `BaseAuthDialog` do DBeaver), para os três SGBDs |
| Driver properties | ⬜ a página diz que não são aplicadas |

## 7. Estado

Conferido em 2026-10-01 contra o SQL Server 2022 (16.0.1200) local, com a conta do
Windows.

| Suíte | Verificações |
|---|---|
| `otter_tests` (`test_mssql.cpp`) | 36 testes: vetores do TDS, pacotes, T-SQL gerado, paginação, lotes |
| `otter_tests_mssql_live` | 187: login pelos dois modelos, catálogo, editor, ALTER, grade, paginação, importação, transações, ferramentas, `KILL` numa sessão vítima |
| Na tela, pelo canal de comandos | `build/ms_flow.ps1`, `build/ms_shots.ps1`, `build/ms_script.ps1` — efeito lido do servidor com `sqlcmd` |

### Defeitos que só apareceram no servidor

- **Pacote SSPI vazio derrubava a conexão.** O último passo do `Negotiate` não produz
  token; mandá-lo mesmo assim fazia o primeiro lote voltar com um `DONE` solto. Agora o
  passo sem token não manda nada.
- **`EXEC('...' + QUOTENAME(@x))` não compila.** O `EXEC` de texto só concatena literais e
  variáveis; o lote que remove a constraint de default monta o comando numa variável.
- **`dashboard_catalog("sqlserver")` devolvia os gráficos do MySQL.** Havia uma sobrecarga
  `bool`, e um literal de texto converte para `bool` antes de converter para
  `string_view`. A sobrecarga saiu; o teste confere o dono de cada gráfico.

### Defeitos que só apareceram na tela

- **Botão direito em "Databases" não abria nada** (relato do usuário, 2026-10-01). Só a
  pasta do MySQL declarava o que cria; a do PostgreSQL e a do SQL Server (a mesma
  função) não. Corrigido, e toda pasta passou a ter menu — as que não criam nada trazem
  só *Refresh*. Conferido por `nav foldermenu <rótulo>`, que abre o mesmo popup no mesmo nó.
- **Conexão sem senha salva só falhava.** Não havia como digitar a senha sem editar o
  perfil; as conexões importadas do SSMS caíam todas nisso.

- **O banco da conexão aparecia sem nome.** Perfil sem banco (o servidor escolhe o padrão
  do login): a árvore desenhava um nó vazio. `Holt::current_database()` passa a dizer o
  que o servidor escolheu.
- **Usuários `##MS_...##` saíam como linhas em branco** na aba Permissions: o ImGui corta
  o rótulo em `##`. O nome agora é texto ao lado de um `Selectable` sem rótulo.
- **O menu de conferência (`objectmenu`) era translúcido**, e a grade de trás
  atravessava os itens na captura.
- **Um script falho deixava a transação aberta** — no SQL Server, com as alterações
  anteriores ao erro aplicadas e as travas presas. `execute_script_async` desfaz a
  transação que o próprio script abriu.

### Aberto, não corrigido

- Vários conjuntos de resultado: só o primeiro chega à grade.
- TDS 8.0; autenticação do Entra ID; NTLM com credenciais digitadas.
- Instância nomeada: conferida contra um Browser **de teste**
  (`tools/sqlbrowser_test_server.py`) — o serviço de verdade está desligado nesta
  máquina, e nenhuma instância nomeada real foi alcançada.
- Parâmetros vão no texto do comando (não há RPC), e não há carga em massa (`BULK`).
- Plano de execução (`SHOWPLAN_XML`) não é lido: o comando *Explain* fica desabilitado.
- "Execute SQL script natively" (`sqlcmd`) recusa com o motivo.
- "Read-only" e "Set as default schema" recusam com o motivo: o SQL Server não tem os
  dois por sessão.
- Partições, tabelas externas, propriedades estendidas além de `MS_Description`, tipos de
  tabela e sinônimo sem formulário (seções 2 e 4).
- `bit` aparece como `1`/`0` na grade.
- Conferência só pelo canal de comandos: ele prova que o comando funciona, não que o
  clique ou a tecla chegam a ele (diretiva 11-A).
