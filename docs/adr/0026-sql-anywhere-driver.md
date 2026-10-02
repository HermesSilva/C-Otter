# ADR 0026 — Driver SQL Anywhere: TDS 5.0 próprio, sem cifra, e o Sybase Central como alvo

**Data:** 2026-10-01
**Status:** Aceito

## Contexto

O usuário instalou o SQL Anywhere 16 com o banco de demonstração e pediu "o perfil dele,
o mais completo possível". É o quarto SGBD, e o primeiro para o qual o DBeaver Community
**não tem plugin**: só o driver genérico "Sybase jConnect", com a árvore genérica do JDBC.

Mapa do alvo, do protocolo e do estado em [`docs/SQLANYWHERE-MAP.md`](../SQLANYWHERE-MAP.md).

## Decisão 1: TDS 5.0 próprio (`lib/tdswire/tds5.cpp`)

O SQL Anywhere atende dois protocolos na mesma porta.

| Candidato | Veredito |
|---|---|
| Protocolo nativo (*Command Sequence*) | Rejeitado: fechado, sem especificação pública. É o único que cifra (`-ec`) e o único com login integrado |
| ODBC do SQL Anywhere (`dbodbc16.dll`) | Rejeitado: exige o cliente do SGBD instalado na máquina — o produto deixa de ser portátil (ADR 0020) — e a licença de redistribuição é a do SGBD |
| jConnect | Rejeitado: é Java |
| FreeTDS (fala TDS 5.0) | Rejeitado: LGPL com link estático (ADR 0002) |
| **TDS 5.0 escrito à mão** | **Escolhido**: especificação pública ("TDS 5.0 Functional Specification", Sybase) |

Fica em `lib/tdswire`, ao lado do TDS do SQL Server, e reaproveita dele só o cabeçalho do
pacote, `Value` e `Message`. Uma classe comum aos dois ramos foi considerada e rejeitada:
login, comando, descrição de coluna e valores diferem em tudo, e a parte comum caberia em
vinte linhas — o custo seria um `if (tds5)` em cada função.

## Decisão 2: sem cifra, e a tela diz

O servidor não cifra o TDS, e o login do TDS 5.0 leva a senha em claro. Não há o que o
cliente possa fazer: a cifra do SQL Anywhere é do protocolo nativo.

- O driver **recusa** um perfil com "Use SSL" ligado, com o motivo — conectar em claro
  quando o perfil pede cifra seria pior que falhar.
- A aba SSL do diálogo fica desabilitada para este driver, dizendo por quê e qual é a
  saída (o túnel SSH do ADR 0021, que serve a qualquer driver).
- O teste `tds5_login_record_has_fixed_fields_with_the_length_after` **afirma** que a senha
  está em claro no pacote: se um dia o login mudar, o aviso da tela muda junto.

Alternativa não seguida: o login cifrado do TDS 5.0 (`TDS_MSG_SEC_ENCRYPT`, RSA), que é do
ASE. **Não foi tentado** contra o SQL Anywhere — e, se funcionasse, protegeria só a senha:
os dados continuariam em claro.

## Decisão 3: a sessão é reposta nos padrões do banco

Uma conexão TDS nasce com as opções de compatibilidade com o ASE (aspas duplas como texto,
coluna `NOT NULL` por padrão, `= NULL` verdadeiro). O SQL que o C-Otter gera — e o que o
usuário copia do `dbisql` — conta com o contrário. O driver emite `SET TEMPORARY OPTION`
para sete opções ao conectar: valem para a conexão e não tocam o banco.

Entre elas, datas e horas **como texto ISO**: o TDS manda `date` como `DATETIME`, que não
existe antes de 1753, e `0001-01-01` chegaria como `1753-01-01` sem aviso. O custo é que
`date_format` e `timestamp_format` do usuário não valem na grade — o que é coerente com os
outros três drivers, que também mostram ISO.

As *Driver properties* do perfil viram mais `SET TEMPORARY OPTION`, depois dessas: quem
quiser outro formato, ou outra opção, tem onde pôr.

## Decisão 4: o "schema" é o dono, e não há nível de banco

Uma conexão TDS fala com **um** banco — o que foi nomeado no login; trocar exige outra
conexão. A árvore não tem pasta "Databases": o nó da conexão é o banco. (Listar os outros
bancos em execução no servidor, cada um como uma sessão, à maneira do PostgreSQL, fica em
aberto.)

O dono dos objetos faz o papel do schema (`DBA.tabela`). Consequências assumidas:

- "Create New Schema" não existe: cria-se um **usuário** (pasta Users).
- O nó do schema é um usuário: *Delete* nele é `DROP USER`, e *Tools → Change password*
  funciona.
- "Set as default schema" e "Read-only" recusam com o motivo: o SQL Anywhere não tem nenhum
  dos dois por sessão.
- Usuário e papel são o mesmo tipo de objeto (`ObjectType::role`), como no PostgreSQL. O
  papel puro vai com `parent = "role"`, que é o que decide `DROP ROLE ... WITH REVOKE` ×
  `DROP USER` e o rótulo do menu.

`Session::Engine` ganha `sqlanywhere`; `has_database_level()` continua falso, mas a forma
da árvore deixou de ser decidida só por ele (o MySQL também não tem nível de banco, e a
árvore dele é outra).

## Decisão 5: o alvo é o Sybase Central, e a conta continua contra o DBeaver

A diretiva 12 manda parecer com o DBeaver, e o DBeaver não tem o que copiar aqui além da
árvore genérica. Copiar só ela seria regredir (diretiva 13): sem eventos, sem views
materializadas, sem usuários, sem editor.

- As pastas que o DBeaver tem ficam com o rótulo e a ordem dele.
- As demais vêm do Sybase Central, com os rótulos dele ("Dbspaces", "Login Policies",
  "Remote Servers"...).
- A cobertura reportada é contra a árvore genérica do DBeaver (14 ✅ + 2 🟡 de 17); o que
  vem do Sybase Central é listado à parte e **não** entra no número (diretiva 4).

## Decisão 6: o corpo `BEGIN ... END` é um comando

O divisor de scripts do SQL Server manda o lote de um `CREATE PROCEDURE` inteiro até o
`GO` (ADR 0024). No Watcom SQL o corpo é **sempre** um bloco, e o `dbisql` aceita
`CREATE PROCEDURE ... END; SELECT ...` sem `go`. Com a regra do SQL Server esse script
chegava ao servidor como um comando só, e era recusado.

Para o dialeto do SQL Anywhere, um lote com bloco é partido no `;` de fora do bloco. A
contagem conhece `END IF`, `END LOOP`, `END FOR`, `END WHILE` (não fecham o bloco) e
`END CASE`. A forma T-SQL (`CREATE PROCEDURE p AS ...`, sem bloco) continua indo até o
`go`, e um lote com `DECLARE` solto continua inteiro.

## Decisão 7: perfis Sybase do DBeaver

No DBeaver os drivers `sybase_jconn`, `sybase_jtds` e `sypase_jconn` moram no provider
`mssql`. O mapeamento do C-Otter era por provider, e mandava esses perfis ao driver do SQL
Server (TDS 7.4), que nem passa do login. Agora o **driver** decide: os três abrem pelo
driver SQL Anywhere.

Um perfil criado aqui é gravado como `mssql` / `sybase_jconn`, com a URL do jConnect
(`jdbc:sybase:Tds:host:porta?ServiceName=banco`) — é o par com que o DBeaver chega a um
SQL Anywhere.

O que isso não resolve: esses três drivers também servem ao **SAP ASE**, que o C-Otter não
suporta (o catálogo é outro). A conexão falha logo no primeiro `SET TEMPORARY OPTION`, com
uma mensagem que diz isso — não conferida contra um ASE de verdade.

## Consequências

- Quatro SGBDs. O que cada tela decide por SGBD passa por `Session::Engine`; os geradores
  genéricos (`generate_object_rename`, `generate_grant`...) desviam para
  `db/sqlanywhere_object.cpp` quando o dialeto do thread é `QuoteStyle::anywhere`.
- O ícone é o `sybase_icon.png` do DBeaver. Azul-marinho sobre transparente: num tema
  escuro ganha uma placa clara por trás (o arquivo não muda).
- O servidor pessoal precisa ser iniciado com `-x tcpip`; o diálogo diz.
- O que fica aberto está em `docs/SQLANYWHERE-MAP.md`, seção 8; como preparar o servidor,
  na seção 7.
