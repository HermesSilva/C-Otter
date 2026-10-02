# ADR 0024 — Driver SQL Server: TDS próprio, SSPI do sistema e lotes `GO`

**Data:** 2026-10-01
**Status:** Aceito

## Contexto

O usuário pediu "o perfil do SQL Server". As conexões importadas do SSMS na primeira
execução (ADR 0023) apareciam todas desabilitadas, com "o C-Otter ainda não fala com o
SQL Server". É o terceiro SGBD, e o primeiro cujo protocolo **não** manda os
valores em texto.

Mapa do alvo em [`docs/MSSQL-MAP.md`](../MSSQL-MAP.md).

## Decisão 1: protocolo próprio (`lib/tdswire`), não ODBC nem FreeTDS

| Candidato | Licença | Veredito |
|---|---|---|
| FreeTDS (`db-lib`, `ct-lib`) | LGPL-2.0 | Proibida com link estático (ADR 0002) |
| Microsoft ODBC Driver 17/18 | Proprietária, redistribuível só pelo instalador | Rejeitada: exige instalar um driver no sistema — o produto deixa de ser portátil (ADR 0020) |
| ODBC do Windows (`SQL Server` legado) | parte do sistema | Rejeitada: só TLS 1.0, sem `datetime2`/`date` de verdade, e não existe fora do Windows |
| OLE DB / ADO | parte do sistema | Rejeitada pelos mesmos motivos, mais COM |
| **`lib/tdswire` próprio** | — | **Escolhida** |

O TDS é especificado publicamente ([MS-TDS], Open Specifications). `lib/pgwire` e
`lib/mywire` já tinham mostrado que um protocolo de banco escrito à mão é viável e não
vira fonte de defeito depois de estável. O que o TDS tem de mais trabalhoso — e por isso
vale registrar — é:

- **os valores chegam em binário**, tipo por tipo (`lib/tdswire/value.cpp`). É o grosso do
  código e o que mais pede teste: os vetores de `tests/unit/test_mssql.cpp` foram
  calculados pela especificação e conferidos no servidor;
- **o TLS começa dentro do protocolo**: o aperto de mão vai embrulhado em pacotes
  PRELOGIN, e só depois a conexão vira TLS puro. `net::TlsOptions` ganhou
  `handshake_write`/`handshake_read` para o Schannel escrever e ler por essa via;
- **cancelar é um pacote na mesma conexão** (ATTENTION), não uma conexão nova como no
  PostgreSQL — o envio é protegido por mutex.

## Decisão 2: Windows Authentication pelo SSPI do sistema

`Negotiate` (Kerberos, com NTLM de reserva) pelo SSPI — a mesma API que o próprio SSMS
usa. Não entra código de Kerberos nem de NTLM no produto, nem biblioteca: é API do
Windows, como o Schannel do ADR 0017. No Linux `sspi_stub.cpp` recusa dizendo por quê.

Fora, por enquanto: NTLM com usuário e senha digitados e os quatro modelos do Active
Directory/Entra ID (exigem OAuth). O diálogo **não os lista** — oferecer o que não
conecta é o campo que finge funcionar (diretiva 6).

## Decisão 3: só o login cifrado por padrão; "Use SSL" cifra tudo

O TDS permite cifrar apenas o pacote de login. É o padrão do protocolo e o do C-Otter: a
senha nunca vai em claro, e um servidor de desenvolvimento com certificado autoassinado
conecta sem configuração. A caixa "Use SSL" cifra a conexão inteira; "Trust Server
Certificate" decide se o certificado é conferido.

A barra de estado só mostra o cadeado quando a conexão **inteira** está cifrada — com só o
login cifrado os dados trafegam em claro, e o cadeado diria o contrário.

## Decisão 4: a origem das colunas é perguntada ao servidor

Para a grade ser editável é preciso saber de que tabela cada coluna veio. O PostgreSQL
manda o OID e o MySQL o nome; o `COLMETADATA` do TDS não manda nada. O driver chama
`sp_describe_first_result_set ... @browse_information_mode = 1` antes de cada
`SELECT`/`WITH` do usuário.

Custo: uma ida ao servidor a mais por consulta. Alternativa rejeitada: `SET FMTONLY ON` /
`FOR BROWSE` — obsoleto um, e o outro altera o plano e acrescenta colunas ao resultado.
Quando o servidor não sabe responder (SQL dinâmico, tabela temporária), a grade fica
somente leitura, que é a resposta honesta.

## Decisão 5: script separado por lotes, como o `sqlcmd`

`GO` não é T-SQL: é do cliente. O divisor de scripts (`sql/script.cpp`) parte o texto nas
linhas `GO` e, dentro de cada lote:

- se o lote tem `DECLARE`, ou é um `CREATE`/`ALTER` de procedure, função, trigger ou
  view, vai **inteiro** — as variáveis só existem dentro do lote, e o corpo de uma rotina
  sem `BEGIN … END` vai até o fim dele;
- senão é partido no `;`, como nos outros SGBDs, com o `ELSE` colado ao `IF` anterior.

Alternativas rejeitadas:

- **Partir sempre no `;`** (o que o DBeaver faz): `DECLARE @n int; SELECT @n` falha com
  "Must declare the scalar variable" — o defeito mais conhecido de quem usa o DBeaver com
  SQL Server. É onde "evoluir, não regredir" (diretiva 13) se aplica.
- **Nunca partir no `;`** (o que o SSMS faz): um script de três `SELECT` viraria um lote
  só, e a grade mostra apenas o primeiro resultado.

## Decisão 6: três SGBDs deixam de ser "tem nível de banco ou não"

A interface decidia PostgreSQL × MySQL por `has_database_level()`. O SQL Server também
tem bancos acima dos schemas, e quase nada mais em comum com o PostgreSQL (`VACUUM`,
`CASCADE`, `PUBLIC`, `CREATE OR REPLACE`). `Session::Engine { postgres, mysql, mssql }`
passa a ser a pergunta; `has_database_level()` fica só para a forma da árvore.

O dialeto do SQL gerado continua por thread (`db/ddl.cpp`); os geradores genéricos
desviam para `db/mssql_object.cpp` quando ele é o de colchetes.

## Consequências

- Conexões do SSMS importadas na primeira execução passam a abrir (as com Windows
  Authentication, direto; as com senha pedem a senha — ela não é importada, ADR 0023).
- Instância nomeada (`host\instância`): a porta é perguntada ao SQL Server Browser
  (UDP 1434, protocolo [MC-SQLR]) — `lib/tdswire/browser.cpp`. Sem resposta a conexão
  **falha dizendo o que fazer**; nunca cai na 1433, que é a instância padrão do mesmo
  servidor, outro banco. Uma porta diferente de 1433 no perfil vale mais que o nome.
  (Na primeira versão deste ADR a instância nomeada era recusada.)
- Um script que falha no meio da transação que ele próprio abriu é desfeito
  (`Session::execute_script_async`). Vale para os três SGBDs: no PostgreSQL a sessão
  ficava em "transação abortada" até alguém dar ROLLBACK.
- O que fica aberto está em `docs/MSSQL-MAP.md`, seção 7.
