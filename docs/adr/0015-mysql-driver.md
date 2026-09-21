# ADR 0015 — Driver MySQL: protocolo próprio e origem de coluna por nome

**Data:** 2026-09-21
**Status:** Aceito

## Contexto

As 6 conexões do workspace real do usuário são MySQL, e todas apareciam como
"driver não implementado" na importação. O segundo SGBD também é o primeiro
teste de verdade da abstração `Holt`/`Driver`: até aqui ela tinha um
implementador só, e uma abstração com um implementador não provou nada.

## Decisão 1: protocolo próprio, não `libmysqlclient`

`libmysqlclient` é GPL. O ADR 0002 proíbe: com link estático, GPL contamina o
produto inteiro. As alternativas consideradas:

| Candidato | Licença | Veredito |
|---|---|---|
| `libmysqlclient` | GPL-2.0 com exceção FOSS | Proibida (ADR 0002) |
| MariaDB Connector/C | LGPL-2.1 | Proibida: link estático de LGPL exige relink |
| `mysql-connector-c++` | GPL | Proibida |
| **`lib/mywire` próprio** | — | **Escolhida** |

O protocolo é público e estável desde 2001 (versão 4.1). O `lib/pgwire` já
provou que escrever um protocolo de banco à mão é viável: 1028 linhas, e
nenhum defeito de protocolo depois de estável.

## Decisão 2: `caching_sha2_password` completo, inclusive o `full auth`

O MySQL 8 usa `caching_sha2_password` por padrão. Ele tem dois caminhos:

- **fast auth** — o servidor tem o hash em cache e o aperto de mão termina;
- **full auth** — o cache está vazio (primeira conexão da conta desde que o
  servidor subiu, ou após `FLUSH PRIVILEGES`), e é preciso mandar a senha
  sobre TLS ou cifrada com a chave pública RSA do servidor.

A primeira versão implementou só o caminho rápido, com uma mensagem honesta
explicando a limitação. **Isso não bastava:** o caminho lento é alcançável
sempre que o servidor reinicia, e o usuário veria um erro incompreensível
numa conexão que funcionava ontem.

Decisão: implementar o RSA-OAEP, usando a API do sistema (CNG no Windows),
como manda o cabeçalho de `net/crypto.hpp` — nada de RSA escrito à mão.

Rejeitado: **pedir ao usuário que rode `ALTER USER ... mysql_native_password`**.
Seria empurrar para o usuário uma redução de segurança para contornar uma
limitação nossa.

## Decisão 3: origem da coluna por NOME, além de por OID

O `ColumnInfo` identificava a tabela de origem por `source_table_oid`, que vem
do `RowDescription` do PostgreSQL. **O MySQL não tem OID.** O
`ColumnDefinition41` traz `(banco, tabela, coluna)` como strings.

Com a identificação só por OID, todo resultado de MySQL era recusado pela
grade com "o resultado não vem de uma tabela" — inclusive um `SELECT * FROM
cliente` numa tabela com chave primária.

Decisão: `ColumnInfo` carrega as duas formas, e `has_source()` cobre ambas.
`find_edit_target()` procura por OID quando há, por nome quando não há.

O campo é a tabela **real**, nunca o apelido: um `UPDATE` contra o apelido `c`
de `FROM cliente c` não existe.

Rejeitado: **analisar o `FROM` da consulta**. Quebra com apelido, subconsulta,
CTE e `JOIN` — é o mesmo raciocínio do ADR 0014.

## Decisão 4: dialeto de identificador como estado da conexão

O SQL gerado (UPDATE da grade, DDL, agregação no servidor) delimitava
identificadores com aspas duplas, fixas em `quote_if_needed()`. No MySQL,
aspas duplas são **string**, não identificador: `WHERE "id" = 1` compara a
constante `'id'` com `1` e nunca casa com linha nenhuma — um `UPDATE` que não
falha e não altera nada.

Decisão: o delimitador é estado de módulo (`set_sql_dialect_for(driver_id)`),
definido uma vez por conexão.

Rejeitado: **passar o dialeto como parâmetro** nas ~20 funções que geram SQL.
O dialeto é propriedade da conexão, não de cada chamada, e repassá-lo por toda
a cadeia (UI → ddl → edit → aggregate) só criaria pontos onde esquecer.

## Consequências

- Cinco tipos de defeito só apareceram **na tela**, nenhum quebrava o build.
  Estão listados em `docs/MYSQL-MAP.md` §"Defeitos que só apareceram na tela".
- A abstração `Holt` sobreviveu ao segundo SGBD sem mudar de forma; o que
  precisou crescer foi o `ColumnInfo` (origem por nome) e a `Session`
  (qual leitor de catálogo, quais pastas existem).
- `CatalogReader` nasceu aqui: com dois SGBDs, `PostgresCatalog` chamado por
  nome viraria um `if` em cada ponto que lê metadados.
- SHA-1 entrou em `net/crypto` **só** para o `mysql_native_password`, com um
  comentário dizendo para não usar em nada novo.
