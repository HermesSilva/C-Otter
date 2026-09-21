# Diálogo de conexão: paridade de *funcionamento* com o DBeaver

Pedido do usuário, 2026-09-21:

> "Precisamos de paridade de funcionamento com o DBeaver, porque quem está
> acostumado com ele terá dificuldade se for totalmente diferente."

Isso muda o critério. Até aqui a paridade era medida em **funcionalidades
presentes**; passa a incluir **onde elas estão** e **como se chega nelas**. Um
campo que existe mas está na oitava aba não tem paridade com um campo que fica
no topo — foi exatamente o que aconteceu com o nome da conexão.

## A diferença estrutural

O diálogo do DBeaver (`Connection "postgres" configuration`) é uma **árvore de
categorias à esquerda**, com a página escolhida à direita:

```
Connection settings          <- categoria, expansível
  General
  Metadata
  Errors and timeouts
Data Editor
  Binary Editor
  Data Formats
  Appearance
    Grid
    Plain text
SQL Editor                   <- selecionada
  Code Editor
  Code Completion
  Formatting
  SQL Processing
```

Rodapé: `Test Connection ...` à esquerda, `OK` e `Close` à direita.

O C-Otter usa **abas horizontais**: Principal, Driver, SSH, SSL, Proxy,
Inicialização, Geral. Sete abas contra uma árvore de dezenas de páginas.

## O tamanho do alvo

Extraído de `plugin.xml` dos plugins do DBeaver, em 2026-09-21:

| Métrica | DBeaver |
|---|---|
| Páginas de preferência (`org.jkiss.dbeaver.preferences.*`) | **61** |
| Dessas, alcançáveis pelo diálogo de conexão | **31** |
| Abas no diálogo do C-Otter | **7** |

As 31 do diálogo, por categoria:

| Categoria | Páginas |
|---|---|
| `main.connections`, `main.meta`, `main.errorHandle`, `main.errorLogs`, `main.transactions`, `main.qm` | Connection settings |
| `main.dataviewer`, `main.dataformat`, `main.resultset*` (5 páginas) | Data Editor |
| `main.sqleditor`, `main.sql.codeeditor`, `main.sql.completion`, `main.sql.format`, `main.sql.dialects`, `main.sql.templates`, `main.sqlexecute` | SQL Editor |
| `main.common`, `main.confirmations`, `main.misc`, `main.notifications` | General |

## O que já foi corrigido por causa deste pedido

| Defeito | Correção |
|---|---|
| Nome da conexão escondido na aba "Geral", a oitava | campo editável **no topo**, sempre visível, com o nome derivado como texto-fantasma |
| Aba "PostgreSQL" aparecia em conexão MySQL, oferecendo `template0`/`template1` | a aba do SGBD só aparece no driver a que pertence |

O primeiro era o mais grave dos dois em termos de paridade: o usuário via o
nome na faixa do topo, clicava nele, nada acontecia, e concluía que a
aplicação não deixava renomear. **O campo funcionava** — estava no lugar
errado.

## O que NÃO está feito

Nada da estrutura em árvore. As 7 abas continuam abas, e as 31 páginas do
DBeaver não têm equivalente aqui — a maioria das opções simplesmente não
existe no C-Otter ainda.

Converter as abas em árvore é barato (é um `BeginChild` com a lista à esquerda
e o conteúdo à direita). O caro é **preencher as páginas**: sem conteúdo, uma
árvore de sete itens é pior que sete abas — tem a mesma informação com mais
cliques.

Por isso a ordem sugerida é: primeiro as opções que faltam, depois a árvore
que as organiza.

## Outros pontos de paridade levantados pelo usuário

Da captura da árvore do Navigator (ver `docs/NAVIGATOR-TREE.md`):

| Item | Estado |
|---|---|
| Tamanho da tabela à direita do nome | ✅ já existia (`32 kB`, `40 kB`) |
| Nó raiz da conexão (`postgres  localhost:5432`) | ⬜ |
| Pasta `Databases` agrupando os bancos | ⬜ |
