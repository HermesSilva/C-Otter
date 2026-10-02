# ADR 0027 — Driver Oracle: TNS/TTC próprio, escrito a partir do driver *thin* de código aberto

**Data:** 2026-10-02
**Status:** Aceito — como prova de conceito; ver "O que isto custa"

## Contexto

O Oracle é o quinto SGBD, e o primeiro cujo protocolo **não tem especificação pública**.
PostgreSQL, MySQL, SQL Server (MS-TDS) e Sybase (TDS 5.0) publicam a deles; a Oracle, não.

O usuário perguntou se havia biblioteca que se pudesse ligar estaticamente, e depois:

> "Este projeto é uma POC, tu consegue fazer engenharia reversa, para implementar, sem
> quebrar ou haquear nada, apenas acessando de forma segura?"

Mapa do protocolo, da árvore e do estado em [`docs/ORACLE-MAP.md`](../ORACLE-MAP.md).

## Decisão 1: protocolo próprio (`lib/orawire`)

| Candidato | Veredito |
|---|---|
| Oracle Instant Client (OCI, `libclntsh` / `oci.dll`) | Rejeitado: proprietário, só existe como biblioteca dinâmica, dezenas de MB — o produto deixa de ser um executável portátil (ADR 0020) e de ligar tudo estaticamente (ADR 0002) |
| ODPI-C, OCILIB | Rejeitados: são camadas sobre o OCI; carregam o Instant Client em tempo de execução, então a dependência continua |
| go-ora compilado como arquivo estático | Rejeitado: traz o runtime do Go para dentro do binário e o link com MSVC é problemático |
| Carregar o Instant Client só se o usuário o tiver | Adiado: resolveria rápido, mas quem não tem o cliente Oracle não conecta — o contrário de "sem nada para instalar". Pode entrar depois, como opção |
| **TNS/TTC escrito à mão** | **Escolhido** |

## Decisão 2: a referência é código aberto, e o servidor de teste é local

"Engenharia reversa" aqui não é desmontar binário da Oracle nem contornar proteção:

1. **A referência é o driver *thin* da própria Oracle**, o
   [python-oracledb](https://github.com/oracle/python-oracledb), publicado sob UPL 1.0 e
   Apache 2.0, com o [go-ora](https://github.com/sijms/go-ora) (MIT) como segunda leitura.
   O código de `lib/orawire` foi escrito a partir da leitura deles. A única coisa
   **copiada** é a tabela de tipos da negociação — 320 trincas de números —, gerada por
   `tools/ora_datatypes.py`; a origem está no cabeçalho do arquivo gerado e em
   `docs/LICENSES.md`.
2. **O servidor de teste é um contêiner local** (Oracle Database Free, imagem oficial), que
   só escuta em `127.0.0.1`, com usuário e dados criados para o teste. Nenhuma conexão sai
   da máquina; os spikes recusam host que não seja `localhost`.
3. **Onde a referência e o servidor discordam, vale o servidor** — e a diferença fica
   escrita (ORACLE-MAP, "Achados que a referência não diz").

## Decisão 3: declarar só as capacidades que o código trata

Na negociação o cliente diz ao servidor o que sabe fazer; o servidor então manda o que foi
declarado. O python-oracledb declara tudo do 23ai — anotações de coluna, domínios, vetores,
*pipelining*, marcador de fim de resposta. Este driver declara a **versão de campos do
19c** e zera os bits do que não implementa.

Declarar mais do que se trata faria o servidor mandar mensagens que ninguém sabe ler, e num
protocolo sem delimitador de mensagem um campo não lido desalinha tudo o que vem depois. O
custo é uma ida a mais no logon (sem o *fast auth* do 23ai) e não ter os recursos novos.

## Decisão 4: LOB como LONG

Uma coluna CLOB/BLOB chega como **localizador** — um ponteiro, sem o conteúdo —, e ler o
valor exigiria uma ida ao servidor por célula. O driver refaz a chamada pedindo a coluna
como LONG / LONG RAW (a "definição"), e o servidor manda o conteúdo direto. É o que o
python-oracledb faz com `fetch_lobs=False`.

Custo: o valor inteiro vem para a memória. Para um cliente de grade, que mostra o valor,
é o comportamento desejado; para exportar um BLOB de 2 GB, não — fica registrado.

## Decisão 5: somente leitura na interface, e dito na tela

Os geradores de `ALTER`, `DROP`, `CREATE` e de DML do programa têm os ramos de PostgreSQL,
MySQL, SQL Server e SQL Anywhere. Nenhum tem o do Oracle. Em vez de oferecer um formulário
que geraria SQL que o servidor recusa, os menus do Oracle mostram só o que funciona e a
linha "Creating and altering objects is not implemented for Oracle yet" (diretiva 6).

O editor SQL não tem essa restrição: qualquer instrução vai ao servidor como digitada.

## O que isto custa

- **Sem especificação, só o servidor testado é garantido.** Foi conferido contra o 23.26
  (26ai Free). O caminho do verificador 11g está escrito e não foi conferido; 12c, 19c e
  21c não foram testados.
- **A senha viaja protegida pelo O5LOGON, mas o resto da sessão vai em claro**: TCPS (TLS)
  e a criptografia nativa do Oracle Net não estão implementados, e o driver recusa a
  conexão quando o perfil pede TLS ou o servidor exige criptografia.
- **É o maior dos protocolos**: ~1.900 linhas para a prova de conceito, sem variáveis de
  ligação, sem JSON/OSON, sem objetos, sem `DBMS_OUTPUT`.
- **Quebras entre versões são prováveis**: a Oracle muda o TTC sem aviso, e quem mantém a
  referência é ela.

## Alternativas para o futuro

- Carregar o Instant Client quando presente (o caminho "grosso"), para o que o *thin* não
  cobre — a Oracle faz o mesmo no próprio python-oracledb.
- TCPS: o TLS já existe em `lib/net` (Windows) e é esboço no Linux.
