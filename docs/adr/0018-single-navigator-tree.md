# ADR 0018 — Uma árvore só: a conexão é a raiz, e cada banco tem a própria sessão

**Data:** 2026-09-30
**Status:** Aceito

## Contexto

O C-Otter tinha dois painéis à esquerda: **Raft** (a lista de conexões) e
**Navigator** (os schemas da conexão *ativa*). O DBeaver tem um só — o
*Database Navigator* —, onde a conexão é a raiz e o que ela contém fica dentro
dela:

```
postgres  localhost:5432
├── Databases
│   ├── ERP → Schemas → public → Tables, Views, …
│   │       → Event Triggers, Extensions, Storage, System Info, Roles
│   └── ERP_TID, DOC, …
├── Administer
└── System Info
```

A divisão em dois painéis nunca foi decidida: não há ADR, e o
`docs/NAVIGATOR-TREE.md` já a registrava como diferença ("os schemas penduram
na raiz do painel"). O usuário, comparando com uma captura do DBeaver:

> "A árvore de objeto está muito diferente do DBeaver, nele não há duas seções."

É a divergência sem perceber que a diretiva 12 proíbe. Custava três coisas a
quem vem do DBeaver:

1. Só a conexão ativa tinha árvore. Comparar duas bases exigia trocar a ativa.
2. Não havia onde listar os outros bancos do servidor: as caixas "Show all
   databases" e "Show template databases" existiam no diálogo e **não eram
   lidas por código nenhum** — nem gravadas (diretiva 6).
3. Os nós de servidor (Roles, Extensions, Tablespaces…) não tinham lugar.

## Decisão

**Um painel, "Database Navigator", com uma árvore.** Cada conexão — aberta ou
só salva — é um nó raiz. A forma abaixo dela é a do `<tree>` do `plugin.xml` do
driver no DBeaver: no PostgreSQL, `Databases → banco → Schemas → schema`; no
MySQL, `Databases → banco`, sem nível de schema.

**Cada banco PostgreSQL é uma sessão.** Expandir um banco que não é o da
conexão abre uma segunda sessão para ele, sob demanda, com as credenciais do
perfil.

**O painel Raft deixa de existir.** O nome continua sendo o do conjunto de
conexões na documentação; na tela, o rótulo é o do DBeaver.

## Justificativa

### Por que uma sessão por banco

É restrição do protocolo, não escolha: no PostgreSQL o banco é fixado no
`StartupMessage`, e não existe `USE`. Consultar o catálogo de `DOC` estando
conectado em `ERP` é impossível — `pg_class` é por banco. O DBeaver faz o
mesmo (`PostgreDatabase` é um `DBSInstance` com `PostgreExecutionContext`
próprio), e também só conecta quando o nó é expandido.

No MySQL uma conexão enxerga todos os bancos (`information_schema`), e não há
sessão extra.

### Por que a sessão do banco é uma `Connection` filha

O banco extra vira uma entrada em `connections_` com `parent_id` apontando
para a raiz. Assim tudo o que já funciona por conexão — documento vinculado
pelo id, barra de status, transação, completion, DDL — vale para ele sem
caminho novo. Um `SELECT` aberto a partir de uma tabela de `DOC` roda na
sessão de `DOC`, e a aba diz isso no título.

A alternativa era a `Session` guardar várias conexões e cada chamada receber o
nome do banco. Tocaria nos ~70 pontos que usam `session()`, e cada ponto
esquecido executaria no banco errado — o defeito mais caro que este cliente
pode ter.

### Por que o desenho troca a conexão "corrente" durante a própria subárvore

As funções da árvore usam `session()`, a conexão ativa. Com várias conexões na
mesma árvore, cada subárvore precisa falar com a sessão **dela**. Enquanto a
subárvore de uma conexão é desenhada, ela é a corrente; ao fim, a anterior
volta — a menos que o usuário tenha clicado dentro dela, e aí ela se torna a
ativa de fato (é o que o DBeaver faz ao selecionar um nó).

Passar a sessão por parâmetro a cada função seria mais explícito e mexeria em
quarenta assinaturas; os menus de contexto, que abrem formulários desenhados
fora da árvore, continuariam precisando da conexão ativa.

### Listas genéricas para os nós de servidor

Os ~25 tipos novos (roles, extensões, tablespaces, encodings, políticas,
dependências…) são todos "lista de itens com nome, detalhe e descrição". Um
struct e um método por tipo seriam 25 cópias do mesmo carregador. Há um
`CatalogItem` e um seletor `CatalogList` — o mesmo desenho que `ServerInfoKind`
já usava para as seis pastas do MySQL.

Os tipos que a UI **edita** (tabela, coluna, índice, constraint) continuam com
struct próprio: ali os campos são lidos pelo gerador de DDL, não só exibidos.

## Rejeitado

| Alternativa | Por quê |
|---|---|
| Manter dois painéis e só acrescentar os nós | Não resolve a queixa: quem procura a tabela *dentro* da conexão não acha |
| Uma conexão PostgreSQL por banco, todas na raiz | É o que havia. Não mostra que os bancos são do mesmo servidor, e exige um perfil salvo por banco |
| Abrir todas as sessões ao conectar | Um servidor com 40 bancos abriria 40 conexões para uma árvore que talvez nem seja expandida; e esgota `max_connections` |
| `dblink`/`postgres_fdw` para ler o catálogo de outro banco | Exige extensão instalada e privilégio; o cliente não pode depender disso |

## Divergências conscientes do DBeaver

| Ponto | DBeaver | C-Otter | Razão |
|---|---|---|---|
| Contagem na pasta | sem número | `Tables (32)` | Evita expandir para descobrir que está vazio; decisão anterior, mantida |
| Ícones | um desenho de pasta para várias | um por tipo | Diretiva 5 |
| Session Manager / Lock Manager | editor próprio, com "kill" | abre uma aba com a mesma consulta | O resultado é o mesmo; encerrar sessão fica para quando houver o editor |

## Consequências

- O layout salvo (`layout.ini`) referencia `###RaftPanel`; a janela deixa de
  existir e o nó de docking dela fica vazio. O ImGui o recolhe sozinho.
- Um banco expandido consome uma conexão do servidor até a raiz ser
  desconectada. Desconectar a raiz fecha as filhas.
- `Show all databases` passa a ser lido e gravado, com a chave do DBeaver
  (`@dbeaver-show-non-default-db@` em `provider-properties`) — um perfil
  importado traz a opção como estava lá.
