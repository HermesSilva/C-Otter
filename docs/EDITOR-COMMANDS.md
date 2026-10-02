# Comandos do editor SQL e da grade de resultados

> **Gerado** por `tools/commands_doc.py` a partir de `src/ui/commands.cpp`. Não editar à
> mão: alterar a tabela e gerar de novo.

O denominador é o DBeaver (diretiva 4). Cada linha é um comando dele, com a tecla nos
dois perfis de atalho e o que difere quando difere.

| Área | ✅ igual | 🟡 com diferença | ⬜ falta | ➖ fora | Total |
|---|---|---|---|---|---|
| Editor SQL | 126 | 19 | 1 | 3 | 149 |
| Grade de resultados | 63 | 5 | 0 | 2 | 70 |
| **Total** | **189** | **24** | **1** | **5** | **219** |

✅ = feito e com o mesmo efeito; 🟡 = funciona, com a diferença dita na nota;
⬜ = não implementado; ➖ = decisão registrada de não fazer, com a razão na nota.

## Perfis de atalho

Dois perfis, escolhidos em *Help → Keymap* e gravados em `settings.json`:

- **DBeaver** (padrão): as teclas do DBeaver, para quem vem de lá.
- **C-Otter**: onde o DBeaver herda uma tecla estranha do Eclipse, a tecla que os editores
  atuais usam. As diferenças são as linhas em que as duas colunas não coincidem.

A janela *Help → Keymap → Show shortcuts...* mostra esta mesma tabela dentro do programa.

As teclas da **grade** só valem com a grade em foco — o contexto `resultset.focused` do
DBeaver. É por isso que `Ctrl+S` grava as alterações da grade ali e o script no editor,
e que `Ctrl+D` copia da linha de cima num e apaga a linha no outro.

## Editor SQL

Fonte: `plugins/org.jkiss.dbeaver.ui.editors.sql/plugin.xml`, mais os do editor de texto do Eclipse que o editor SQL herda (ir para a linha, maiúsculas/minúsculas, excluir linha).

Dos 149 comandos, 145 respondem (✅ + 🟡). Dos 62 que têm tecla no DBeaver, 62 respondem
à mesma tecla no perfil **DBeaver**.

| | Comando | Contexto | DBeaver | C-Otter | Nota |
|---|---|---|---|---|---|
| ✅ | Execute SQL query | editor | `Ctrl+Enter` | `Ctrl+Enter` |  |
| ✅ | Execute SQL in new tab | editor | `Ctrl+\` | `Ctrl+\`, `Ctrl+Shift+Enter` |  |
| 🟡 | Execute SQL script | editor | `Alt+X` | `Alt+X` | Shows the result of the last query; DBeaver opens one result tab per query |
| ✅ | Execute SQL script from the position | editor | `Alt+P` | `Alt+P` |  |
| 🟡 | Execute queries in separate tabs | editor | `Ctrl+Alt+Shift+X` | `Ctrl+Alt+Shift+X` | Runs the queries one after another; DBeaver opens a connection per query |
| 🟡 | Execute SQL script natively | editor | `Alt+N` |  | Runs the script through the psql or mysql client of the system |
| ✅ | Cancel active query | editor |  |  |  |
| ✅ | Select row count | editor |  |  |  |
| ✅ | Select all rows | editor | `Ctrl+Alt+Shift+A` | `Ctrl+Alt+Shift+A` |  |
| ✅ | Evaluate SQL expression | editor | `Ctrl+Alt+'` | `Ctrl+Alt+'` |  |
| ✅ | Explain Execution Plan | editor | `Ctrl+Shift+E` | `Ctrl+Shift+E` |  |
| 🟡 | Load Execution Plan | editor |  |  | Loads a plan saved as EXPLAIN (FORMAT JSON); the DBeaver .dbplan format is not read |
| 🟡 | Export from Query | editor |  |  | Runs the query without a row limit and opens the export window; DBeaver streams to the file |
| ✅ | Next query | editor | `Alt+Down` | `Ctrl+Alt+Down` |  |
| ✅ | Previous query | editor | `Alt+Up` | `Ctrl+Alt+Up` |  |
| ✅ | Go to matching bracket | editor | `Ctrl+Shift+[` | `Ctrl+Shift+[` |  |
| ✅ | Select to the matching bracket | editor | `Ctrl+Shift+]` | `Ctrl+Shift+]` |  |
| 🟡 | Open Declaration | editor | `F4` | `F4`, `F12` | Reveals the object in the navigator tree; DBeaver opens its property editor |
| ✅ | Go to Line... | editor | `Ctrl+L` | `Ctrl+L` |  |
| ✅ | Foldings Enabled | editor |  |  |  |
| ✅ | Expand All Foldings | editor |  |  |  |
| ✅ | Collapse All Foldings | editor |  |  |  |
| 🟡 | Show server output | global | `Ctrl+Shift+O` | `Ctrl+Shift+O` | PostgreSQL notices; MySQL warnings are not collected yet |
| ✅ | Show execution log | global |  |  |  |
| ✅ | Show SQL variables | global |  |  |  |
| ✅ | Toggle outline | global |  |  |  |
| ✅ | SQL Terminal | global |  |  |  |
| ➖ | Show panels in result tabs | global |  |  | The panels are dockable windows: dragging one onto the results does this |
| ⬜ | Show multiple results in a single tab | global |  |  | The grid shows one result per tab |
| ✅ | Toggle results panel | global | `Ctrl+T` | `Ctrl+J` |  |
| ✅ | Maximize results panel | global | `Ctrl+Shift+T` | `Ctrl+Shift+T` |  |
| ✅ | Switch active panel | global | `Alt+T` | `Alt+T` |  |
| ✅ | Toggle editor layout | global |  |  |  |
| ➖ | Switch presentation to | global |  |  | Presentations are plugin extensions (visual query builder); none exists here |
| ✅ | Toggle Line Comment | editor | `Ctrl+/` | `Ctrl+/` |  |
| ✅ | Toggle Block Comment | editor | `Ctrl+Shift+/` | `Ctrl+Shift+/` |  |
| ✅ | Toggle Word Wrap | editor | `Ctrl+Alt+Shift+W` | `Ctrl+Alt+Shift+W`, `Alt+Z` |  |
| ✅ | Content Format | global | `Ctrl+Shift+F` | `Ctrl+Shift+F` |  |
| ✅ | Morph to delimited list | editor |  |  |  |
| ✅ | Trim spaces | editor |  |  |  |
| ✅ | Trim leading spaces | editor |  |  |  |
| ✅ | Trim trailing spaces | editor |  |  |  |
| ✅ | To Upper Case | editor | `Ctrl+Shift+X` | `Ctrl+Shift+U` |  |
| ✅ | To Lower Case | editor | `Ctrl+Shift+Y` | `Ctrl+Shift+L` |  |
| ✅ | Delete Line | editor | `Ctrl+D` | `Ctrl+Shift+K` |  |
| 🟡 | Complete template name | editor |  | `Ctrl+Alt+Space` | The five default templates; template variables are plain placeholders |
| ✅ | Copy selected query | editor |  |  |  |
| ✅ | Search in web | editor |  |  |  |
| ✅ | Disable SQL syntax parser | editor |  |  |  |
| ✅ | New SQL script | global | `Ctrl+]`, `Ctrl+F3` | `Ctrl+T` |  |
| 🟡 | Open SQL script | global | `F3`, `Ctrl+[`, `Ctrl+O` | `Ctrl+O` | Opens a file; DBeaver lists the scripts saved in the project |
| ✅ | Last edited SQL script | navegador | `Ctrl+Enter` | `Ctrl+Enter` |  |
| ✅ | Open SQL console | navegador | `Ctrl+Alt+Enter` | `Ctrl+Alt+Enter` |  |
| ✅ | Import SQL script | editor | `Ctrl+Alt+Shift+O` | `Ctrl+Alt+Shift+O` |  |
| ✅ | Export SQL script | editor |  |  |  |
| ✅ | Rename SQL Script | editor | `Ctrl+F2` | `Ctrl+F2`, `F2` |  |
| ✅ | Delete this script | editor |  |  |  |
| ✅ | Set connection from navigator | global | `Ctrl+Shift+.` | `Ctrl+Shift+.` |  |
| ✅ | Auto-sync connection with navigator | global |  |  |  |
| 🟡 | Refresh current schema | editor |  |  | Reloads the whole catalog of the connection, not only one schema |
| ✅ | Refresh all schemas | editor |  |  |  |
| ✅ | Close | global | `Ctrl+Shift+\` | `Ctrl+Shift+\` |  |
| ✅ | Pin/unpin | global | `Ctrl+Shift+P` | `Ctrl+Shift+P` |  |
| ✅ | Export result... | global |  | `Ctrl+Shift+X` |  |
| ✅ | DDL | global |  |  |  |
| ➖ | AI Assistant | editor |  |  | Sending the schema to an external service is out of scope (ADR 0004) |
| ✅ | Commit | global | `Ctrl+Alt+Shift+K`, `Ctrl+4` | `Ctrl+Shift+C` |  |
| ✅ | Rollback | global | `Ctrl+Alt+Shift+R`, `Ctrl+8` | `Ctrl+Shift+R` |  |
| ✅ | Auto-commit | global |  | `Ctrl+Shift+A` |  |
| ✅ | Pending transactions | global |  |  |  |
| 🟡 | Transaction log | global |  |  | Lists the statements of the open transaction from the query log of this session |
| ✅ | Connect | navegador |  |  |  |
| ✅ | Disconnect | global |  |  |  |
| ✅ | Disconnect All | global |  |  |  |
| ✅ | Disconnect Other | global |  |  |  |
| ✅ | Invalidate/Reconnect | global |  |  |  |
| ✅ | Read-only | global |  |  |  |
| ✅ | New Connection | global | `Ctrl+Shift+N` | `Ctrl+Shift+N` |  |
| ✅ | New connection from JDBC URL | global |  |  |  |
| ✅ | New Folder | navegador |  |  |  |
| ✅ | Select active connection | global | `Ctrl+9` | `Ctrl+9` |  |
| ✅ | Select active schema | global | `Ctrl+0` |  |  |
| ✅ | Set as default | navegador | `Ctrl+Shift+A` |  |  |
| ✅ | Open database object ... | global | `Ctrl+Shift+D` | `Ctrl+P` |  |
| ✅ | Edit object | navegador | `F4` | `F4`, `Enter` |  |
| ✅ | Create object | navegador | `Alt+Insert`, `Ctrl+N` | `Alt+Insert`, `Ctrl+N` |  |
| ✅ | Delete object | navegador | `Delete` | `Delete` |  |
| ✅ | Rename object | navegador | `F2` | `F2` |  |
| ✅ | View data | navegador |  |  |  |
| ✅ | Read data in SQL console | navegador |  |  |  |
| ✅ | Context tools | global | `Alt+`` | `Alt+`` |  |
| ✅ | Export Data | navegador |  |  |  |
| 🟡 | Import Data | navegador |  |  | Imports a CSV file; DBeaver also imports XLSX, XML and from another table |
| ✅ | Toggle filter | navegador |  |  |  |
| ✅ | Configure filter | navegador |  |  |  |
| ✅ | Clear filter | navegador |  |  |  |
| ✅ | Show only selected object(s) | navegador |  |  |  |
| ✅ | Hide selected object(s) | navegador |  |  |  |
| ✅ | Show all connections | navegador |  |  |  |
| ✅ | Focus Database Navigator Filter | global |  | `Ctrl+Alt+F` |  |
| ✅ | Link with editor | global | `Ctrl+Shift+,` | `Ctrl+Shift+,` |  |
| 🟡 | Move up | navegador |  |  | Reorders saved connections; DBeaver also reorders the columns of a new table |
| 🟡 | Move down | navegador |  |  | Reorders saved connections; DBeaver also reorders the columns of a new table |
| 🟡 | Move to top | navegador |  |  | Reorders saved connections; DBeaver also reorders the columns of a new table |
| 🟡 | Move to bottom | navegador |  |  | Reorders saved connections; DBeaver also reorders the columns of a new table |
| ✅ | Add bookmark | navegador | `Ctrl+Alt+Shift+D` | `Ctrl+Alt+B` |  |
| ✅ | Navigate to | global |  |  |  |
| ✅ | Next tab | global | `Alt+Shift+Down` | `Alt+Shift+Down` |  |
| ✅ | Previous tab | global | `Alt+Shift+Up` | `Alt+Shift+Up` |  |
| ✅ | Open source tab | global |  |  |  |
| ✅ | New index from selection | navegador |  |  |  |
| ✅ | New constraint from selection | navegador |  |  |  |
| ✅ | Execute stored procedure | navegador |  |  |  |
| ✅ | Advanced copy | grade | `Ctrl+Shift+C` |  |  |
| ✅ | Advanced copy with the most recent settings | grade | `Ctrl+Alt+Shift+C` | `Ctrl+Alt+Shift+C` |  |
| ✅ | Advanced paste ... | grade | `Ctrl+Shift+V` | `Ctrl+Shift+V` |  |
| ✅ | Generate UUID | global | `Ctrl+Alt+Shift+U` | `Ctrl+Alt+Shift+U` |  |
| ✅ | Load resource(s) from local disk | grade |  |  |  |
| ✅ | Save resource(s) to local disk | grade |  |  |  |
| 🟡 | Open results in Excel | grade |  |  | Writes a CSV and opens it with the spreadsheet application; DBeaver writes XLSX |
| ✅ | Associate with data source | global |  |  |  |
| ✅ | Show scripts | global |  |  |  |
| ✅ | Show resource in explorer | global |  |  |  |
| ✅ | Change user password | global |  |  |  |
| 🟡 | Driver manager | global |  |  | Lists the built-in drivers; C-Otter speaks the wire protocols itself and has no JDBC drivers to download or configure |
| ✅ | Preferences | global |  | `Ctrl+,` |  |
| ✅ | Show/Hide view | global |  |  |  |
| ✅ | Clear log | global |  |  |  |
| ✅ | Log filters | global |  |  |  |
| ✅ | Stop processes | global |  |  |  |
| ✅ | Clear History... | global |  |  |  |
| ✅ | Reset Settings... | global |  |  |  |
| ✅ | Collect diagnostic info | global |  |  |  |
| 🟡 | Dashboard | global | `Ctrl+Alt+Shift+B` | `Ctrl+Alt+Shift+B` | Line charts of the server statistics; DBeaver also has bar and pie views and user-defined charts |
| ✅ | Add chart | global |  |  |  |
| ✅ | Remove chart | global |  |  |  |
| ✅ | Show chart catalog | global |  |  |  |
| ✅ | Dashboard settings | global |  |  |  |
| ✅ | Refresh chart | global |  |  |  |
| ✅ | View chart | global |  |  |  |
| ✅ | Reset dashboard | global |  |  |  |
| ✅ | Command palette | global | `Ctrl+3` | `Ctrl+3`, `Ctrl+Alt+Shift+P` |  |
| ✅ | Keyboard shortcuts | global | `F1` | `F1` |  |
| ✅ | Exit | global |  |  |  |
| ✅ | Move lines up | editor | `Ctrl+Shift+Up` |  |  |
| ✅ | Move lines down | editor | `Ctrl+Shift+Down` |  |  |
| ✅ | Join lines | editor | `Ctrl+Shift+J` | `Ctrl+Shift+J` |  |
| ✅ | Word completion | editor | `Ctrl+Shift+Space` | `Ctrl+Shift+Space` |  |
| ✅ | Open file... | global |  |  |  |

## Grade de resultados

Fonte: `plugins/org.jkiss.dbeaver.ui.editors.data/plugin.xml` (`core.resultset.*`, 63 comandos), mais copiar, colar e selecionar tudo, que no DBeaver são do Eclipse.

Dos 70 comandos, 68 respondem (✅ + 🟡). Dos 51 que têm tecla no DBeaver, 51 respondem
à mesma tecla no perfil **DBeaver**.

| | Comando | Contexto | DBeaver | C-Otter | Nota |
|---|---|---|---|---|---|
| ✅ | Apply changes | grade | `Ctrl+S` | `Ctrl+S` |  |
| ✅ | Apply and commit changes | grade |  |  |  |
| ✅ | Reject changes | grade | `Ctrl+R` | `Ctrl+R` |  |
| ✅ | Show confirmation before save | grade |  |  |  |
| 🟡 | Apply cell changes | grade | `Ctrl+Alt+Shift+Enter` | `Ctrl+Alt+Shift+Enter` | Saves every pending change of the result, not only the current cell |
| ✅ | Reset cell changes | grade | `Esc` | `Esc` |  |
| ✅ | Set to NULL | grade |  | `Ctrl+0` |  |
| ✅ | Set to default | grade | `Ctrl+Backspace` | `Ctrl+Backspace` |  |
| ✅ | Inline edit | grade | `Enter` | `Enter`, `F2` |  |
| ✅ | Edit cell | grade | `Shift+Enter` | `Shift+Enter` |  |
| ✅ | Add row | grade | `Alt+Insert` | `Alt+Insert` |  |
| ✅ | Add row (insert before) | grade | `Alt+Shift+Insert` | `Alt+Shift+Insert` |  |
| ✅ | Duplicate row | grade | `Ctrl+Alt+Insert` | `Ctrl+Alt+Insert` |  |
| ✅ | Duplicate row (insert before) | grade | `Ctrl+Alt+Shift+Insert` | `Ctrl+Alt+Shift+Insert` |  |
| ✅ | Copy from row above | grade | `Ctrl+D` | `Ctrl+D` |  |
| ✅ | Copy from row below | grade | `Ctrl+Alt+D` | `Ctrl+Alt+D` |  |
| ✅ | Delete current row | grade | `Alt+Delete` | `Alt+Delete` |  |
| ✅ | First row | grade | `Ctrl+Alt+Shift+Left` | `Ctrl+Alt+Shift+Left` |  |
| ✅ | Previous row | grade | `Ctrl+Alt+Left` | `Ctrl+Alt+Left` |  |
| ✅ | Next row | grade | `Ctrl+Alt+Right` | `Ctrl+Alt+Right` |  |
| ✅ | Last row | grade | `Ctrl+Alt+Shift+Right` | `Ctrl+Alt+Shift+Right` |  |
| ✅ | Go To Row | grade | `Ctrl+G` | `Ctrl+G` |  |
| ✅ | Go To Column | grade | `Ctrl+Shift+G` | `Ctrl+Shift+G` |  |
| ✅ | Navigate link | grade | `Alt+Space` | `Alt+Space` |  |
| ✅ | References | grade | `Ctrl+Shift+1` | `Ctrl+Shift+1` |  |
| ✅ | Select row(s) | grade | `Ctrl+Alt+R` | `Ctrl+Alt+R` |  |
| ✅ | Select column(s) | grade | `Ctrl+Alt+C` | `Ctrl+Alt+C` |  |
| ✅ | Select All | grade | `Ctrl+A` | `Ctrl+A` |  |
| ✅ | Copy | grade | `Ctrl+C` | `Ctrl+C` |  |
| ✅ | Paste | grade | `Ctrl+V` | `Ctrl+V` |  |
| ✅ | Copy as | grade |  |  |  |
| ✅ | Copy column name(s) | grade | `Alt+Shift+C` | `Alt+Shift+C` |  |
| ✅ | Copy row number(s) | grade |  |  |  |
| ✅ | Move column left | grade | `Alt+Shift+Left` | `Alt+Shift+Left` |  |
| ✅ | Move column right | grade | `Alt+Shift+Right` | `Alt+Shift+Right` |  |
| ✅ | Hide columns | grade | `Alt+Shift+H` | `Alt+Shift+H` |  |
| ✅ | Show columns | grade | `Alt+Shift+T` | `Alt+Shift+T` |  |
| 🟡 | Hide columns with no data | grade |  |  | Looks at the rows that are loaded, not at the whole result |
| ✅ | Columns width: fit values | grade |  |  |  |
| ✅ | Columns width: fit screen | grade |  |  |  |
| ✅ | Show context menu for column | grade | `Shift+F11` | `Shift+F11` |  |
| ✅ | Set row color | grade |  |  |  |
| ✅ | Filter menu | grade | `F11` | `F11` |  |
| ✅ | Filter by value | grade | `Ctrl+F11` | `Ctrl+F11` |  |
| ✅ | Customize filters ... | grade | `Alt+Shift+F` | `Alt+Shift+F` |  |
| ✅ | Activate filter/data editor | grade | `Ctrl+Alt+Shift+T` | `Ctrl+Alt+Shift+T` |  |
| ✅ | Remove all filters/orderings | grade |  |  |  |
| ✅ | Reset default filter | grade |  |  |  |
| ✅ | Save as default filter | grade |  |  |  |
| ✅ | Toggle results sort order | grade | `Ctrl+2` | `Ctrl+2` |  |
| ✅ | Fetch Next Page | global | `Ctrl+Alt+N` | `Ctrl+Alt+N` |  |
| ✅ | Fetch All Data | global | `Ctrl+Shift+=` | `Ctrl+Shift+=` |  |
| ✅ | Row Count | grade |  |  |  |
| 🟡 | Export data | grade |  |  | Exports the rows that are loaded; DBeaver opens the data transfer wizard |
| 🟡 | Open with | grade |  |  | Writes a CSV to the temporary folder and opens it with the system default application |
| ✅ | Generate script | grade |  |  |  |
| ✅ | Toggle Grid/Record view | grade | `Tab` | `Tab` |  |
| 🟡 | Switch presentation | grade | `Ctrl+`` | `Ctrl+`` | Grid and plain text; DBeaver also has chart and spatial presentations |
| ✅ | Toggle result panels | grade | `Ctrl+7`, `F7` | `Ctrl+7`, `F7` |  |
| ✅ | Activate results/panel | grade | `Ctrl+Shift+7` | `Ctrl+Shift+7` |  |
| ✅ | Value panel | grade | `Ctrl+Alt+F2` | `Ctrl+Alt+F2` |  |
| ✅ | References panel | grade | `Ctrl+Alt+F3` | `Ctrl+Alt+F3` |  |
| ✅ | Metadata panel | grade | `Ctrl+Alt+F4` | `Ctrl+Alt+F4` |  |
| ✅ | Grouping panel | grade | `Ctrl+Alt+F5` | `Ctrl+Alt+F5` |  |
| ✅ | Calc panel | grade | `Ctrl+Alt+F6` | `Ctrl+Alt+F6` |  |
| ➖ | Maximize/restore panels | grade |  |  | The panels are dockable windows: drag one out or double-click its tab |
| ➖ | Toggle panels layout | grade |  |  | The panels are dockable windows: drag one to the side or to the bottom |
| ✅ | Switch content viewer | grade |  |  |  |
| ✅ | Zoom in data grid | grade | `Alt+0` | `Alt+0`, `Ctrl+=` |  |
| ✅ | Zoom out data grid | grade | `Alt+9` | `Alt+9`, `Ctrl+-` |  |
