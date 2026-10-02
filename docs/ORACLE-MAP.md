# Oracle — mapa do protocolo, da árvore e do que falta

Decisão no [ADR 0027](adr/0027-oracle-wire-protocol.md). Este documento é o inventário:
o que o DBeaver oferece para o Oracle, o que o C-Otter já faz e o que não faz. A cobertura
é contra o DBeaver (diretiva 4).

**Estado em 2026-10-02: prova de conceito.** Conecta, autentica, consulta, navega e mostra
dados. **Somente leitura na árvore** — nada de criar, alterar ou apagar objeto por
formulário. Conferido contra o Oracle AI Database 26ai Free (23.26.3) num contêiner local;
nenhuma outra versão do servidor foi testada.

## 1. Protocolo (TNS / TTC)

A Oracle não publica a especificação. A referência é o driver *thin* dela própria, de
código aberto — [python-oracledb](https://github.com/oracle/python-oracledb) (UPL 1.0 /
Apache 2.0) —, com o [go-ora](https://github.com/sijms/go-ora) (MIT) como segunda leitura.
O código de `lib/orawire/` foi escrito a partir da leitura deles e conferido contra o
servidor; a única coisa copiada é a tabela de tipos da negociação, gerada por
`tools/ora_datatypes.py`.

| Etapa | Mensagem | Estado |
|---|---|---|
| Listener | `CONNECT` com o descritor; `ACCEPT`, `REFUSE` (ORA-12514/12505 traduzidos), `RESEND` | ✅ |
| Listener | `REDIRECT` (shared server, RAC) | ⬜ recusado com mensagem |
| Negociação | `PROTOCOL` (versão, charset, capacidades do servidor) | ✅ |
| Negociação | `DATA TYPES` (capacidades do cliente + 320 tipos) | ✅ |
| Logon | O5LOGON, verificador 12c (PBKDF2-SHA512 + AES-256) | ✅ conferido no servidor |
| Logon | O5LOGON, verificador 11g (SHA-1 + AES-192) | 🟡 escrito, sem servidor para conferir |
| Logon | *Fast auth* do 23ai (uma ida a menos) | ⬜ não usado; o caminho clássico funciona |
| Logon | SYSDBA / SYSOPER, troca de senha expirada, proxy, token, Kerberos, wallet | ⬜ |
| Execução | `EXECUTE` com parse + execução + pré-busca de 200 linhas | ✅ |
| Execução | `FETCH` das linhas seguintes (500 por ida) | ✅ |
| Execução | Definição das colunas LOB como LONG (segunda ida) | ✅ CLOB, NCLOB, BLOB |
| Execução | Variáveis de ligação (`:x`) | ⬜ a mensagem `IO VECTOR` derruba a conexão com aviso |
| Execução | Fechamento de cursores de carona na chamada seguinte | ✅ |
| Transação | `COMMIT`, `ROLLBACK`, commit junto da instrução (auto-commit), estado dito pelo servidor | ✅ |
| Sessão | `LOGOFF` ao fechar; `PING` | ✅ |
| Cancelamento | Marcador de interrupção + troca de *reset* | 🟡 escrito, não exercitado |
| Rede | TCPS (TLS) | ⬜ recusado com mensagem |
| Rede | Criptografia nativa do Oracle Net (NNE) | ⬜ recusado com mensagem |
| Rede | Proxy SOCKS5 | 🟡 passa pelo `net::connect_to`, não conferido |
| Diagnóstico | `OTTER_ORA_TRACE=1` despeja cada pacote em hexadecimal | ✅ |

### Tipos de valor

| Tipo | Estado | Como aparece |
|---|---|---|
| VARCHAR2, CHAR, LONG | ✅ | texto (UTF-8 pedido ao servidor) |
| NVARCHAR2, NCHAR, NCLOB | ✅ | convertido de UTF-16BE |
| NUMBER | ✅ | decimal exato, sem passar por `double` |
| DATE, TIMESTAMP | ✅ | `2024-02-29 13:45:10.123456` |
| TIMESTAMP WITH TIME ZONE | ✅ | hora local do valor + `-03:00`; fuso por **nome de região** sai em UTC |
| TIMESTAMP WITH LOCAL TIME ZONE | ✅ | na hora da máquina (`ALTER SESSION SET TIME_ZONE` no logon) |
| INTERVAL DAY TO SECOND / YEAR TO MONTH | ✅ | `+1 02:03:04.5`, `+1-06` |
| BINARY_FLOAT, BINARY_DOUBLE | ✅ | |
| RAW, LONG RAW, BLOB | ✅ | bytes |
| CLOB | ✅ | texto inteiro (definido como LONG) |
| ROWID, UROWID | ✅ | os 18 caracteres do Oracle |
| BOOLEAN (23ai) | 🟡 | lido; não conferido numa coluna de tabela |
| JSON, VECTOR | 🟡 | a coluna aparece, o valor é `[JSON]` / `[VECTOR]` (falta o decodificador OSON) |
| Objeto, XMLTYPE, REF CURSOR, BFILE | 🟡 | marcador (`[object]`, `[cursor]`, `[BFILE]`); o fluxo não perde o passo |

### Achados que a referência não diz

Conferidos no 23.26; ficam aqui porque custaram tempo e não estão escritos em lugar nenhum.

- **O servidor manda dois campos a mais no fim da mensagem de erro** (tipo do SQL e soma de
  verificação) pela versão de campos **dele**, não pela negociada. Declarando a versão do
  19c, a mensagem de erro saía vazia.
- **A tabela de tipos tem de ir inteira.** Faltando as trincas de TIMESTAMP WITH TIME ZONE,
  INTERVAL e BINARY_FLOAT/DOUBLE, qualquer consulta com esses tipos volta `ORA-03115`.
- **Coluna LONG não vem na pré-busca**: a primeira resposta traz só a descrição, e as
  linhas chegam no `FETCH`.
- **`ALL_VIEWS.TEXT` é nulo para as views do SYS** a quem não é DBA (o `sqlplus` mostra o
  mesmo); `TEXT_VC` traz os primeiros 4000 caracteres.
- **Juntar views do dicionário numa consulta só é lento**: `ALL_OBJECTS` + `ALL_TABLES` +
  `ALL_TAB_COMMENTS` + `ALL_MVIEWS` levou 10 s para 41 linhas; cada uma sozinha, 0,1 s. Por
  isso `catalog_oracle.cpp` lê uma view por consulta e junta no cliente.
- **A imagem `free:latest-lite` não tem o XDB**: `DBMS_METADATA.GET_DDL` falha com
  `ORA-00600 [unable to load XDB library]`. O ambiente de teste usa a `free:latest`.

## 2. SQL

| Item | Estado |
|---|---|
| Uma instrução por vez; o `;` final sai, salvo em bloco PL/SQL e `CREATE` de código | ✅ `oracle_statement_text` |
| Script: unidade PL/SQL (`DECLARE`, `CREATE PROCEDURE`...) vai até a `/` sozinha na linha | ✅ `Dialect::plsql_units` |
| Script: bloco `BEGIN ... END;` anônimo inteiro, com `END IF` / `END LOOP` / `END CASE` | ✅ |
| Paginação da grade: `OFFSET n ROWS FETCH NEXT m ROWS ONLY`, apelido sem `AS` | ✅ |
| Realce: palavras-chave, funções e tipos do Oracle | ✅ |
| Grade editável | ⬜ o resultado não diz a tabela de origem: sempre somente leitura |
| `EXPLAIN PLAN` | ⬜ |
| `DBMS_OUTPUT` no painel de saída | ⬜ |
| Agrupar, pivot e "valores distintos" da grade | ⬜ ainda geram `) AS apelido`, que o Oracle recusa |
| `SET ROLE`, schema padrão do perfil | ✅ `ALTER SESSION SET CURRENT_SCHEMA` |

## 3. Árvore de objetos

Extraída do `<tree>` de `org.jkiss.dbeaver.ext.oracle/plugin.xml` (159 nós com rótulo).

| Nó do DBeaver | Estado | Observação |
|---|---|---|
| **Schemas** › Schema | ✅ | os mantidos pela Oracle ficam de fora, salvo o da conexão |
| › Tables › Table | ✅ | sem tamanho em disco (exige `DBA_SEGMENTS`) |
| ›› Columns | ✅ | tipo, nulo, chave, default, comentário |
| ›› Constraints | ✅ | PK, UNIQUE, CHECK (os `NOT NULL` gerados ficam na coluna) |
| ›› Foreign Keys, References | ✅ | |
| ›› Triggers | ✅ | |
| ›› Indexes | ✅ | |
| ›› Partitions | 🟡 | lidas, não conferidas; ›› Subpartitions ⬜ |
| ›› Privileges | 🟡 | no editor do objeto (aba Permissions), não na árvore |
| ›› Dependencies | ⬜ | |
| › Views › View | ✅ | |
| › Materialized Views | ✅ | |
| › Indexes (do schema) | ✅ | |
| › Sequences | ✅ | |
| › Queues | ⬜ | |
| › Types › Attributes | 🟡 | atributos lidos; › Methods ⬜ |
| › Packages (› Procedure, Function, Dependencies) | ⬜ | |
| › Procedures, › Functions | 🟡 | numa pasta só ("Procedures"), como no MySQL; no DBeaver são duas |
| › Synonyms | ⬜ | |
| › Schema Triggers, › Table Triggers | ⬜ | os de tabela aparecem dentro da tabela |
| › Database Links | ⬜ | |
| › Scheduler (Jobs, Programs) | ⬜ | |
| › Recycle Bin | ⬜ | os `BIN$` são escondidos |
| **Global metadata** (Types, Public Synonyms, Public Database Links, User Recycle Bin) | ⬜ | |
| **Storage** › Tablespaces (Files, Objects) | ⬜ | |
| **Security** › Users, Roles, Profiles | ⬜ | |
| **Administer** (Session Manager, Lock Manager) | ⬜ | |

Nós cobertos, contados pelo rótulo de pasta: **12 de 46** pastas (26%).

### Editor de objeto

| Aba | Estado |
|---|---|
| Properties (nome, dono, tipo, estado, datas, comentário) | ✅ somente leitura |
| Columns, Constraints, Foreign Keys, Indexes, References, Triggers | ✅ |
| DDL | ✅ `DBMS_METADATA.GET_DDL`, somente leitura |
| Permissions | ✅ `ALL_TAB_PRIVS`, somente leitura |
| Data | ✅ paginado |
| Criar, renomear, apagar, alterar | ⬜ os menus dizem "not implemented for Oracle yet" |

## 4. Diálogo de conexão

O `OracleConnectionPage` do DBeaver tem três formas de conexão (Basic, TNS, Custom), a
escolha **Service name / SID**, o papel (Normal, SYSDBA, SYSOPER) e o cliente OCI.

| Campo do DBeaver | Estado |
|---|---|
| Host, Port, Database (service name), usuário, senha | ✅ a página geral, com a nota do que falta |
| Service name / SID | 🟡 SID só pela propriedade de driver `sid` (e pela URL `@host:porta:SID`) |
| Role (SYSDBA / SYSOPER) | ⬜ |
| Connection type TNS (alias do `tnsnames.ora`) e Custom (URL) | ⬜ |
| Client (OCI) | não se aplica: não há cliente Oracle |
| Importar conexão Oracle do DBeaver | 🟡 o provider é aceito; não conferido com um perfil real |
| Ícone do driver | ⬜ usa o genérico; falta o original do DBeaver (diretiva 5) |

## 5. Como conferir

```powershell
# Uma vez: o banco de teste, no Docker do WSL (só escuta em 127.0.0.1)
wsl -d Ubuntu -e docker run -d --name otter-oracle -p 127.0.0.1:1521:1521 `
    -e ORACLE_PWD=<senha> -v otter-oracle-data:/opt/oracle/oradata `
    container-registry.oracle.com/database/free:latest

$env:ORA_PASSWORD = "<senha>"
build\win-release\bin\spike_oraconnect.exe     # o protocolo: cada tipo de valor
build\win-release\bin\spike_oracatalog.exe     # driver + catálogo: 67 verificações
build\ora_flow.ps1                             # a tela, pelo canal de comandos
```

Os testes unitários (`test_orawire.cpp`, `test_oracle.cpp`) conferem o que não precisa de
servidor: as primitivas criptográficas contra os vetores públicos, o O5LOGON contra uma
implementação independente (`tools/ora_auth_vector.py`), o formato de cada tipo, a divisão
de scripts e a paginação.
