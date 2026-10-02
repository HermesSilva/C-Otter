# Atalhos da grade de resultado

> **Substituído em 2026-10-01** por [`EDITOR-COMMANDS.md`](EDITOR-COMMANDS.md), gerado da
> tabela de comandos do programa: 70 comandos da grade, 63 iguais ao DBeaver. Este
> arquivo fica como registro do levantamento original das teclas.

Extraído de `plugin.xml` do `org.jkiss.dbeaver.ui.editors.data` (elementos
`<key sequence="...">`), em 2026-09-21. **50 associações**, 47 comandos
distintos — três têm mais de uma tecla.

Este é o denominador: a cobertura abaixo é contra o DBeaver, não contra o que
o C-Otter já tem (diretiva 4).

## O pré-requisito — resolvido

A grade agora tem **célula selecionada**: clique simples seleciona, e a célula
selecionada é o que dá aos atalhos uma "linha atual" sobre a qual agir.

Três obstáculos apareceram, nenhum deles visível no build:

1. **As 9 células compartilhavam o id `##cellhit`.** O `PushID` vinha DEPOIS
   do `InvisibleButton`. Com duplo clique e menu de contexto não dava para
   notar; bastou pedir `IsItemClicked` para o ImGui acusar "conflicting ID"
   em vermelho na tela.

2. **Clicar numa célula não dava foco à janela.** Um `InvisibleButton` não
   propaga foco para a janela que o contém, e sem foco `IsWindowFocused`
   respondia falso — o que desligava todas as teclas. Corrigido com
   `FocusWindow` no clique.

3. **A navegação por teclado do ImGui consome as setas dentro do `NewFrame`**,
   antes de qualquer código nosso rodar. Nem `SetKeyOwner` nem
   `Shortcut(RouteFocused)` resolvem: os dois disputam a rota, e a tecla já
   foi consumida antes da disputa. A saída é desligar
   `ConfigFlags_NavEnableKeyboard` **no início do quadro**, com base no que a
   grade observou no quadro anterior, e religá-la ao sair da grade.

## Navegação

| Comando | Tecla | Estado |
|---|---|:---:|
| Mover seleção (setas) | — | ✅ |
| Home/End na linha, `Ctrl` no resultado | — | ✅ |
| PageUp/PageDown | — | ✅ uma tela, não uma página do servidor |
| `resultset.row.first` | `Ctrl+Alt+Shift+←` | ✅ |
| `resultset.row.previous` | `Ctrl+Alt+←` | ✅ |
| `resultset.row.next` | `Ctrl+Alt+→` | ✅ |
| `resultset.row.last` | `Ctrl+Alt+Shift+→` | ✅ |
| `resultset.grid.gotoRow` | `Ctrl+G` | ⬜ |
| `resultset.grid.gotoColumn` | `Ctrl+Shift+G` | ⬜ |
| `resultset.navigateLink` | `Alt+Espaço` | ⬜ segue a FK |

## Edição

| Comando | Tecla | Estado |
|---|---|:---:|
| `resultset.row.edit.inline` | `Enter` | ✅ |
| `resultset.row.edit` | `Shift+Enter` | ⬜ |
| `resultset.row.add` | `Alt+Insert` | ✅ |
| `resultset.row.add.before` | `Shift+Alt+Insert` | ⬜ |
| `resultset.row.copy` | `Ctrl+Alt+Insert` | ✅ mesma regra do menu |
| `resultset.row.copy.before` | `Ctrl+Shift+Alt+Insert` | ⬜ |
| `resultset.row.copy.from.above` | `Ctrl+D` | ✅ verificado na tela |
| `resultset.row.copy.from.below` | `Ctrl+Alt+D` | ✅ |
| `resultset.row.delete` | `Alt+Delete` | ✅ implementado, **não verificado na tela** |
| `resultset.cell.setDefault` | `Ctrl+Backspace` | ⬜ falta o DEFAULT da coluna — ver abaixo |
| `resultset.cell.reset` | `Esc` | ✅ implementado, **não verificado na tela** |
| `resultset.cell.save` | `Ctrl+Alt+Shift+Enter` | ✅ grava o buffer inteiro |
| `resultset.applyChanges` | `Ctrl+S` | ⬜ o `Ctrl+S` daqui salva o SCRIPT |
| `resultset.rejectChanges` | `Ctrl+R` | ⬜ |
| `generate.uuid` | `Ctrl+Shift+Alt+U` | ⬜ |

## Seleção e colunas

| Comando | Tecla | Estado |
|---|---|:---:|
| `resultset.grid.selectRow` | `Ctrl+Alt+R` | ✅ copia a linha |
| `resultset.grid.selectColumn` | `Ctrl+Alt+C` | ✅ copia a coluna |
| `resultset.grid.copyColumnNames` | `Alt+Shift+C` | ✅ separados por tab |
| `resultset.grid.moveColumnLeft` | `Alt+Shift+←` | ⬜ arrastar já funciona |
| `resultset.grid.moveColumnRight` | `Alt+Shift+→` | ⬜ |
| `resultset.grid.hideColumns` | `Alt+Shift+H` | ⬜ |
| `resultset.grid.showColumns` | `Alt+Shift+T` | ⬜ |
| `resultset.grid.showColumnContextMenu` | `Shift+F11` | ⬜ |

## Busca, filtro e ordenação

| Comando | Tecla | Estado |
|---|---|:---:|
| `resultset.filterMenu` | `F11` | ⬜ filtro existe, sem tecla |
| `resultset.filterMenu.distinct` | `Ctrl+F11` | ⬜ |
| `resultset.filterSettings` | `Alt+Shift+F` | ⬜ |
| `resultset.focus.filter` | `Ctrl+Alt+Shift+T` | ⬜ |
| `resultset.toggleOrder` | `Ctrl+2` | ⬜ ordenar existe, sem tecla |
| `resultset.referencesMenu` | `Ctrl+Shift+1` | ⬜ |

## Paginação

| Comando | Tecla | Estado |
|---|---|:---:|
| `resultset.fetch.page` | `Ctrl+Alt+N` | ✅ |
| `resultset.fetch.all` | `Ctrl+Shift+=` | ⬜ |
| `resultset.count` | — | ✅ contagem existe |

## Apresentação

| Comando | Tecla | Estado |
|---|---|:---:|
| `resultset.toggleMode` | `Tab` | ✅ grade ↔ registro único |
| `resultset.switchPresentation` | ``Ctrl+` `` | ⬜ |
| `resultset.grid.togglePreview` | `Ctrl+7` e `F7` | ✅ com `F7` |
| `resultset.grid.activatePreview` | `Ctrl+Shift+7` | ⬜ |
| `resultset.grid.togglePanel` | `Ctrl+Alt+F2`..`F6` | ⬜ cinco painéis |
| `resultset.zoomIn` / `zoomOut` | `Alt+0` / `Alt+9` | ⬜ |

## Cobertura

**19 de 47 comandos têm tecla** (40%), mais a navegação por setas, Home/End e
PageUp/PageDown, que o DBeaver trata como comportamento da grade e não como
comando nomeado.

Eram 7 até 2026-09-21. Os 12 acrescentados são, na maioria, **ações que já
existiam** e estavam só no menu ou num botão: duplicar linha, painel de valor,
paginação, gravar o buffer. Ligar a tecla ao que já funciona é o caminho mais
barato de cobertura — e o que mais muda o uso diário, porque uma ação escondida
num menu de contexto não é encontrada.

### Onde a tecla teve de divergir

| Comando | No DBeaver | Aqui | Por quê |
|---|---|---|---|
| `resultset.applyChanges` | `Ctrl+S` | `Ctrl+Alt+Shift+Enter` | `Ctrl+S` salva o script |
| `resultset.rejectChanges` | `Ctrl+R` | — | `Ctrl+R` recarrega |
| Definir NULL na célula | não tem | `Ctrl+0` | `Ctrl+Shift+N` é "nova conexão" |

A terceira linha é uma armadilha que só apareceu ao escrever o código: os
atalhos globais usam `IsKeyChordPressed`, que **não respeita rota**. Um
`Ctrl+Shift+N` na grade abriria o diálogo de conexão junto com a ação da
célula — a grade estar em foco não impede.

Restam sem tecla nove ações que existem: filtro (`F11`), ordenação (`Ctrl+2`),
mover e esconder colunas, e os painéis laterais.

## O que não foi verificado na tela

Duas teclas estão implementadas e **não foram verificadas na tela**:
`Alt+Delete` e `Esc`. Nenhuma das duas chega à aplicação pela automação — um
trace com `IsKeyDown`, que não depende de rota nem de foco, não as registra.

O que FOI verificado, e de onde vem a confiança no resto: `Ctrl+D` copia (a
barra passa a dizer "1 alteração(oes) em 1 linha(s), não salvas", e a célula
muda de valor na captura), as setas movem exatamente uma célula, e `Tab`
alterna para a visão de registro. As três percorrem o mesmo `handle_grid_keys`
que `Alt+Delete` e `Esc` — mas isso é inferência, não observação.

Descoberta de automação, que custou várias tentativas de "corrigir" código
correto: **`SendKeys` não entrega teclas de SETA a esta aplicação.** Letras
chegam, setas não; `PostMessage` com `WM_KEYDOWN`/`WM_KEYUP` entrega. Está
tratado em `tools/demo_grid.ps1`. `Alt+Delete` e `Esc` não chegam nem por um
nem por outro.

## Por que `Ctrl+Backspace` não entrou

`cell.setDefault` põe a célula no **DEFAULT da coluna**, e o `ResultSet` não
carrega essa informação — ela está no catálogo (`ColumnMeta::default_value`),
que a grade não consulta. Implementá-lo exige ligar o resultado ao catálogo da
tabela de origem, o que é maior que uma tecla.

Um `Ctrl+Backspace` que limpasse a célula em vez de aplicar o DEFAULT seria
pior que não ter a tecla: numa coluna `NOT NULL DEFAULT 0`, limpar produz um
INSERT recusado onde o DEFAULT produziria zero.

## Sobre conflitos

Três teclas do DBeaver colidem com atalhos globais que o C-Otter já usa:

| Tecla | No DBeaver | No C-Otter hoje |
|---|---|---|
| `Ctrl+S` | aplicar alterações da grade | salvar o script |
| `Ctrl+R` | descartar alterações da grade | recarregar |
| `Esc` | reverter a célula | fechar diálogo |

No DBeaver isso se resolve por *contexto*: o atalho só vale com a grade em
foco (`contextId="org.jkiss.dbeaver.ui.context.resultset.focused"`). Qualquer
implementação aqui precisa da mesma noção de foco — sem ela, `Ctrl+S` salvaria
o script quando o usuário quis gravar a linha.
