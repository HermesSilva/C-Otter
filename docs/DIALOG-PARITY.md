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
categorias à esquerda**, com a página escolhida à direita.

A árvore não é uma lista fixa: `EditConnectionWizard.addPages()`
(`plugins/org.jkiss.dbeaver.ui.editors.connection`) monta as páginas fixas, e o
resto vem das `<page>` declaradas nos `plugin.xml` e ligadas por `category`.
Extraída das duas fontes por `tools/map_conn_dialog.py`, com os rótulos vindos
de `UIConnectionMessages.properties` e dos `bundle.properties` — **não
transcritos da captura**:

```
Connection settings              ConnectionPageSettings     <- a página do driver
  Initialization                 ConnectionPageInitialization
  Transactions                   PrefPageTransactions
  Internal parameters            ConnectionPageInternalParameters
General                          ConnectionPageGeneral      <- nome, tipo, pasta
Metadata                         PrefPageMetaData
Errors and timeouts              PrefPageErrorHandle
Data Transfer                    PrefPageDataTransfer
Data Editor                      main.resultset
  Appearance                     main.resultset.presentation
    Grid                         main.resultset.grid
    Plain text                   main.resultset.plain.text
  Binary Editor                  main.resultset.editors
  Data Formats                   main.dataformat
  Dictionaries                   main.dataviewer
SQL Editor                       main.sqleditor
  Code Completion                main.sql.completion
  Code Editor                    main.sql.codeeditor
  Formatting                     main.sql.format
  SQL Processing                 main.sqlexecute
  Scripts                        main.sql.resources
  Templates                      main.sql.templates
```

Rodapé: `Test Connection ...` à esquerda, `OK` e `Close` à direita.

O C-Otter usava **abas horizontais**: Principal, Driver, SSH, SSL, Proxy,
Inicialização, Geral. Sete abas contra a árvore acima — até 2026-09-21, quando
passou a usar a mesma árvore (ver "O que já foi corrigido", abaixo).

Note onde o DBeaver **não** põe uma página: SSH, SSL e Proxy não são irmãs de
"Main". Elas vivem dentro de `Connection settings`, como abas da página do
driver — que é o que `ConnectionPageSettings` desenha. Herdar isso importa:
quem procura SSL procura dentro das configurações de conexão, não no primeiro
nível.

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
| **7 abas horizontais** no lugar da árvore de categorias | **árvore à esquerda**, na hierarquia de `EditConnectionWizard` |
| SSH/SSL/Proxy como abas de primeiro nível | abas **dentro** de "Configurações de conexão", como no DBeaver |
| Rodapé `Testar \| Salvar \| Cancelar`, tudo à esquerda | `Test Connection ...` à esquerda, `OK`/`Close` à direita |
| Transações eram uma seção de "Inicialização" | página própria, irmã dela (`PrefPageTransactions`) |
| Combo de autenticação com literais em português | passa por `TR()`; traduzia errado com a UI em inglês |
| Tooltips de ajuda em português, fora do `TR()` | 12 textos sob `TR()`, com o pt-BR no catálogo |

O primeiro era o mais grave dos dois em termos de paridade: o usuário via o
nome na faixa do topo, clicava nele, nada acontecia, e concluía que a
aplicação não deixava renomear. **O campo funcionava** — estava no lugar
errado.

### Por que a árvore veio antes das opções

A versão anterior deste documento adiava a árvore: *"sem conteúdo, uma árvore
de sete itens é pior que sete abas — tem a mesma informação com mais cliques"*.

Isso estava errado, e a correção veio de uma captura lado a lado. O que
desorienta quem vem do DBeaver não é a **quantidade** de opções, é **não
achar onde procurou**. Uma página que existe e diz "ainda não implementado"
responde a pergunta; uma aba que não existe deixa o usuário concluir que o
produto não tem o recurso — o mesmo erro do nome da conexão, em escala maior.

As páginas sem conteúdo dizem isso na tela, como manda a diretriz 6.

## O que NÃO está feito

A estrutura está; o conteúdo da maioria das páginas não:

| Página | Estado |
|---|---|
| Configurações de conexão, Inicialização, Transações, Parâmetros internos | ✅ |
| Geral, Metadados | ✅ |
| Erros e tempos limite | ⬜ avisado na tela |
| Transferência de dados | ⬜ avisado na tela |
| Editor de dados (+ Editor binário, Formatos, Grade) | ⬜ avisado na tela |
| Editor SQL (+ Editor de código, Completar, Formatação, Processamento) | ⬜ avisado na tela |

Também faltam, da árvore do DBeaver: `Client Identification`,
`Connection Types`, `Drivers`, `Network Profiles`, `Dictionaries` e
`GIS Viewer` — nem como página vazia, porque não há no C-Otter o conceito que
elas configuram.

## Outros pontos de paridade levantados pelo usuário

Da captura da árvore do Navigator (ver `docs/NAVIGATOR-TREE.md`):

| Item | Estado |
|---|---|
| Tamanho da tabela à direita do nome | ✅ já existia (`32 kB`, `40 kB`) |
| Nó raiz da conexão (`postgres  localhost:5432`) | ⬜ |
| Pasta `Databases` agrupando os bancos | ⬜ |

Do painel Raft, 2026-09-21 — *"a árvore de conexões no DBeaver não tem seções,
neste projeto foi colocado 3, com muitas coisas desnecessárias"*:

| Item | Estado |
|---|---|
| Lista única, sem separar "abertas" de "salvas" | ✅ |
| Detalhes (versão, host, transação) fora da lista, em tooltip | ✅ |
| Botões "Nova conexão"/"Editar" no topo do painel | ✅ removidos; menu de contexto no lugar |
| Rótulo do tipo de conexão sob o nome | ✅ removido; vai no tooltip |
| Ícone do SGBD por conexão | ⬜ |

Da tipografia, mesma data:

| Item | Estado |
|---|---|
| Fonte do sistema na interface (Segoe UI / DejaVu Sans) | ✅ |
| Monoespaçada só no editor SQL | ✅ |

Do editor, 2026-09-21 — *"a aba SQL deve ter o nome da conexão, e ao abrir uma
nova query deve abrir na conexão correta"*:

A captura do usuário mostrava um `Script 3` com **crase do MySQL e `public.`
do PostgreSQL na mesma query**. Não era erro de digitação: a janela "SQL" era
uma só, os scripts de todas as conexões conviviam nela, e tanto a execução
quanto o realce usavam a conexão **ativa no Raft** — não a do script.

No DBeaver cada `SQLEditor` carrega seu próprio `DBPDataSourceContainer`
(`SQLEditor.java:445`), e o título de fábrica é
`<${connectionName}> ${fileName}` (`SQLEditor.java:180`).

| Item | Estado |
|---|---|
| Uma janela ancorável por conexão, com o nome dela | ✅ |
| Abas de script dentro da janela da conexão | ✅ |
| Execução pela sessão do documento, não pela ativa | ✅ |
| Dialeto (realce, formatação, paginação) pelo driver da aba | ✅ |
| Janela em foco define Navigator e barra de status | ✅ |
| Conectar já abre a janela com `Script 1` | ✅ |
| Resultado colhido da sessão que executou | ✅ |

Divergência consciente: o DBeaver repete o nome da conexão **em cada aba de
script**, porque lá elas são abas de topo do Eclipse. Aqui o nome fica na
janela que as contém — repeti-lo em cada aba seria redundante, já que todas as
abas de uma janela pertencem à mesma conexão.
