# Mapa do MySQL no DBeaver

Levantado de `D:\Tootega\Source\dbeaver\plugins\org.jkiss.dbeaver.ext.mysql\` —
`plugin.xml` (`<tree>`), `MySQL*.java` do modelo e `OSGI-INF/l10n/bundle.properties`.
Diretiva 1: medir o alvo antes de implementar.

Vale para MySQL 5.5+ e MariaDB 5.5+ (ADR 0010).

---

## 1. Protocolo de rede

Sem `libmysqlclient` — é GPL e ADR 0002 proíbe. O protocolo é implementado em
`lib/mywire`, como `lib/pgwire` foi para o PostgreSQL.

### Pacote

Todo tráfego é uma sequência de pacotes com cabeçalho de 4 bytes:

| Bytes | Campo |
|---|---|
| 0..2 | comprimento do corpo, **little-endian**, 3 bytes |
| 3 | número de sequência, reinicia a cada comando |

Corpo maior que `0xFFFFFF` (16 MB − 1) é **partido**: emite-se um pacote cheio
seguido de outro, e o último pacote da cadeia é o que tem comprimento menor que
o máximo — inclusive um pacote de corpo vazio, quando o total é múltiplo exato
de 16 MB. Ignorar isso trunca resultados grandes em silêncio.

Diferença de fundo para o PostgreSQL: lá o comprimento é **big-endian**, tem 4
bytes e **inclui a si mesmo**; aqui é little-endian, 3 bytes e conta só o corpo.

### Aperto de mão

1. Servidor envia `HandshakeV10`: versão do protocolo, versão do servidor,
   id da conexão, `auth-plugin-data` (20 bytes em duas partes), flags de
   capacidade (também em duas partes), charset, nome do plugin de autenticação.
2. Cliente responde `HandshakeResponse41`: capacidades, tamanho máximo de
   pacote, charset, usuário, resposta de autenticação, banco inicial, plugin.
3. Servidor responde `OK`, `ERR` ou `AuthSwitchRequest`.

### Autenticação

| Plugin | Onde | Cálculo |
|---|---|---|
| `mysql_native_password` | 5.7, MariaDB | `SHA1(senha) XOR SHA1(desafio ‖ SHA1(SHA1(senha)))` |
| `caching_sha2_password` | 8.0 padrão | `SHA256(senha) XOR SHA256(SHA256(SHA256(senha)) ‖ desafio)` |
| `sha256_password` | 5.7 opcional | RSA ou canal seguro |

`caching_sha2_password` tem **dois desfechos**: `fast auth` (o servidor já tem o
hash em cache) termina o aperto de mão; `full auth` exige senha em claro sobre
TLS, ou cifrada com a chave pública RSA do servidor. Sem TLS e sem RSA, a
primeira conexão de uma sessão do servidor **falha** — e o erro precisa dizer
isso, não "senha inválida".

SHA-1 entra em `lib/net/crypto` só por causa do `mysql_native_password`, que é
definido em termos dele. Vai pela API do sistema (`BCRYPT_SHA1_ALGORITHM` /
OpenSSL), como manda o cabeçalho de `crypto.hpp` — nada de implementação própria.

### Comandos

| Byte | Comando | Uso |
|---|---|---|
| `0x03` | `COM_QUERY` | executar SQL |
| `0x01` | `COM_QUIT` | encerrar |
| `0x02` | `COM_INIT_DB` | trocar de banco |
| `0x0E` | `COM_PING` | manter vivo |
| `0x16` | `COM_STMT_PREPARE` | preparar |
| `0x17` | `COM_STMT_EXECUTE` | executar preparado |
| `0x19` | `COM_STMT_CLOSE` | liberar |

Cancelamento **não** é um comando: abre-se outra conexão e emite-se
`KILL QUERY <id>`. É o mesmo desenho do PostgreSQL, onde o cancelamento vai por
uma conexão separada.

### Resposta a `COM_QUERY`

- Primeiro byte `0x00` → `OK` (linhas afetadas, último id inserido)
- Primeiro byte `0xFF` → `ERR` (código, `SQLSTATE`, mensagem)
- Primeiro byte `0xFB` → `LOCAL INFILE` (recusamos)
- Qualquer outro → contagem de colunas, seguida de N `ColumnDefinition41`,
  depois as linhas, até um `EOF`/`OK` final

O valor `0xFB` **dentro de uma linha** é NULL; fora dela, na primeira posição,
é `LOCAL INFILE`. O mesmo byte com dois sentidos conforme a posição.

---

## 2. Árvore de objetos

Extraída do `<tree>` do `plugin.xml`. 27 tipos de nó, **21 implementados**.

Cobertura contra o DBeaver (diretiva 4): **21 / 27 = 78%** da árvore MySQL.

Faltam: Packages (só MariaDB), Administer, User privileges, Plugins, e os dois
nós "virtuais" de índice e trigger no nível do banco -- que só repetem o que
já aparece dentro de cada tabela.

| Nó | Caminho | Estado |
|---|---|:---:|
| Databases (catálogos) | `catalogs` | ✅ |
| └ Database | `database` | ✅ |
| ⠀⠀└ Tables | `tables` | ✅ |
| ⠀⠀⠀⠀└ Table | `table` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ Columns | `attributes` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ Constraints | `constraints` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ Foreign Keys | `associations` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ References (virtual) | `references` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ Triggers | `triggers` | ✅ |
| ⠀⠀⠀⠀⠀⠀├ Indexes | `indexes` | ✅ |
| ⠀⠀⠀⠀⠀⠀└ Partitions → Subpartitions | `partitions` | ✅ |
| ⠀⠀├ Views → Columns | `views` | ✅ |
| ⠀⠀├ Indexes (virtual, do banco) | `indexes` | ⬜ |
| ⠀⠀├ Procedures → Parameters | `procedures` | ✅ |
| ⠀⠀├ Packages (só MariaDB) | `packages` | ⬜ |
| ⠀⠀├ Sequences (MariaDB 10.3+) | `sequences` | ✅ |
| ⠀⠀├ Triggers (virtual, do banco) | `triggers` | ⬜ |
| ⠀⠀└ Events | `events` | ✅ |
| Users → Grants | `users` | ✅ |
| Administer | — | ⬜ |
| System Info | — | ✅ |
| ├ Session status / Global status | `sessionStatus` | ✅ |
| ├ Session variables / Global variables | `sessionVariables` | ✅ |
| ├ Engines | `engines` | ✅ |
| ├ Charsets → Collations | `charsets` | ✅ |
| ├ User privileges | `privileges` | ⬜ |
| └ Plugins | `plugins` | ⬜ |

**Diferença estrutural para o PostgreSQL:** lá a hierarquia é
`banco → schema → objeto`; aqui é `banco → objeto`, sem schema. No MySQL
"database" e "schema" são sinônimos. O modelo do C-Otter precisa comportar as
duas formas sem fingir um nível que não existe — um nó "public" inventado seria
exatamente o campo que finge funcionar da diretiva 6.

Vários nós dependem da versão: `visibleIf="object.dataSource.supportsSequences()"`,
`supportsEvents()`, `supportsPartitions()`, `supportsCheckConstraints()`,
`mariaDB`. A detecção vem da string de versão do aperto de mão — MariaDB se
anuncia como `5.5.5-10.x.y-MariaDB` por compatibilidade com clientes antigos, e
o `5.5.5-` na frente é um prefixo falso que precisa ser descartado antes de
comparar versões.

---

## 3. Metadados

O MySQL expõe tudo por `information_schema`, que é padrão SQL — ao contrário dos
`pg_catalog` do PostgreSQL. As consultas são portanto mais simples, mas há
armadilhas:

- `information_schema` é notoriamente **lento** em instâncias com muitas tabelas;
  o DBeaver usa `SHOW` onde equivale (`SHOW CREATE TABLE`, `SHOW INDEX`).
- Nomes de tabela são **sensíveis a maiúsculas no Linux e não no Windows**
  (`lower_case_table_names`). Comparar nome sem saber disso dá resultado errado.
- `SHOW CREATE TABLE` é o caminho para o DDL, e não a reconstrução a partir das
  colunas: só ele traz `AUTO_INCREMENT`, engine, charset e comentário fielmente.

---

## 4. Estado

| Etapa | Estado |
|---|:---:|
| SHA-1 em `lib/net/crypto`, com vetores do FIPS 180-4 | ✅ |
| `lib/mywire`: pacote, aperto de mão, `COM_QUERY` | ✅ |
| `mysql_native_password` | ✅ |
| `caching_sha2_password`, caminho rápido (cache) | ✅ |
| `caching_sha2_password`, `full auth` via RSA-OAEP | ✅ |
| `MysqlHolt` implementando `db::Holt` | ✅ |
| Catálogo: bancos, tabelas, colunas, chaves, índices, triggers, rotinas | ✅ |
| Grade **editável** (origem por nome, sem OID) | ✅ |
| Dialeto de identificador (crase) no SQL gerado | ✅ |
| Importação do DBeaver com senha | ✅ 6 de 6 conexões |
| **Particionamento e eventos** | ✅ |
| **System Info**, com filtro sobre 633 variáveis | ✅ |
| **Usuários e GRANTs** | ✅ sem nunca ler a senha |
| TLS (`CLIENT_SSL`, Schannel) | ✅ no Windows; Linux pendente (ADR 0017) |
| `caching_sha2_password` sobre TLS, sem RSA | ✅ |
| `mysql_clear_password` **sob TLS** | ✅ recusado em claro |
| Certificado de cliente no aperto de mão | ⬜ gravado e lido, não usado |
| Protocolo preparado (`COM_STMT_*`) | ⬜ |

Verificado contra um **MySQL 8.0.46 real** em 2026-09-21:

- `tests/integration/test_catalog_mysql_live.cpp` — 108 verificações, 0 falhas
- `spikes/myconnect` — aperto de mão, 10.000 linhas, erros com SQLSTATE
- Na tela: importar do DBeaver, conectar, navegar, consultar, **editar e gravar**

O que o trabalho destravou: a importação do workspace do usuário listava
**6 conexões MySQL como indisponíveis**; agora as 6 conectam.

### Defeitos que só apareceram na tela

Nenhum destes quebrava o build, e nenhum apareceria num teste unitário:

| Sintoma | Causa |
|---|---|
| `Access denied (using password: NO)` numa conexão com senha salva | `save_profiles` gerava o id numa variável local; a senha ia para a chave `""` |
| Barra de status dizia `PostgreSQL 8.0.46` num MySQL | literal fixo em dois pontos da tela |
| `url` do perfil gravada como `jdbc:postgresql://` | prefixo fixo |
| Pastas `Sequences` e `Tipos de dados` vazias em todo banco | sem o equivalente aos `visibleIf` do DBeaver |
| `somente leitura: o resultado não vem de uma tabela` num `SELECT * FROM cliente` | origem procurada só por OID, que o MySQL não tem |
| Grade exibindo o valor **antigo** depois de gravar | a releitura reusava `executing_document_id_`, que era zerado no mesmo quadro |
| Ícone do cadeado lendo como **envelope** na barra de status | `PathArcTo(π, 0)` desenha a metade de BAIXO; a alça caía dentro da caixa. O intervalo certo é `π → 2π`, como em `draw_role` |
| `require` falhando com "aperto de mão TLS (0x00090320)" | o MySQL pede certificado de cliente de forma OPCIONAL; `SEC_I_INCOMPLETE_CREDENTIALS` não é erro, e o passo seguinte do laço responde "não tenho" |
| `not a socket` na primeira consulta **depois** de um TLS bem-sucedido | o `TlsChannel` guardava um `Socket*`, e a `Connection` é movida para dentro do `Holt` |

### Aberto, não corrigido

`otter_tests_mysql_live` com `MYSQL_PASSWORD` vazio falha com **"BCryptEncrypt
falhou"** em vez de dizer que a senha está vazia. A primeira chamada de
`BCryptEncrypt` (a que só mede o tamanho) passa e a segunda falha, o que sugere
divergência entre `out_size` medido e o buffer. Não reproduzido de forma
controlada — não há conta de senha vazia no servidor de teste — e por isso
**não corrigido às cegas**. O caminho normal, com senha, funciona: é o que as
108 verificações de `test_catalog_mysql_live.cpp` exercitam.
