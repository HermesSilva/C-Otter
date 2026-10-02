# Mapa exaustivo do DBeaver

Gerado por `tools/map_dbeaver.py` a partir de `D:/Tootega/Source/dbeaver` — extraído do código, não estimado.

## Totais

| Elemento | Quantidade |
|---|---|
| Comandos | **281** |
| Atalhos de teclado | **147** |
| Menus declarados | **34** |
| Contribuições de menu/toolbar | **110** |
| Barras de ferramentas | **6** |
| Handlers (XML) | **350** |
| Views / painéis (XML) | **23** |
| Editores (XML) | **58** |
| Assistentes (XML) | **19** |
| Páginas de preferências (XML) | **109** |
| Pontos de extensão | **85** |
| Dialogs (classes Java) | **151** |
| Wizards / páginas de assistente (classes Java) | **112** |
| Páginas de preferências (classes Java) | **56** |
| Handlers de comando (classes Java) | **279** |
| Actions (classes Java) | **84** |
| Views / painéis (classes Java) | **77** |
| Editores (classes Java) | **126** |
| Value handlers (tipos) (classes Java) | **81** |
| Managers de objeto (DDL) (classes Java) | **309** |

## Comandos por categoria

### rs (63)

| Comando | ID |
|---|---|
| Abrir com | `core.resultset.openWith` |
| Add row (insert before) | `core.resultset.row.add.before` |
| Adicionar linha | `core.resultset.row.add` |
| Alternar apresentação | `core.resultset.switchPresentation` |
| Alternar disposição de painéis | `core.resultset.grid.toggleLayout` |
| Alternar ordenação dos resultados | `core.resultset.toggleOrder` |
| Alternar painéis de resultados | `core.resultset.grid.togglePreview` |
| Alternar visualizador de conteúdo | `core.resultset.grid.switchContentViewer` |
| Alternar visualização de grade/registro | `core.resultset.toggleMode` |
| Apagar linha | `core.resultset.row.delete` |
| Aplicar alterações | `core.resultset.applyChanges` |
| Aplicar alterações com COMMIT | `core.resultset.applyAndCommitChanges` |
| Aplicar alterações de célula | `core.resultset.cell.save` |
| Ativar editor de filtro/dados | `core.resultset.focus.filter` |
| Ativar resultados/painel | `core.resultset.grid.activatePreview` |
| Copiar como | `core.resultset.copyAs` |
| Copiar da linha abaixo | `core.resultset.row.copy.from.below` |
| Copiar da linha acima | `core.resultset.row.copy.from.above` |
| Copiar nome da(s) coluna(s) | `core.resultset.grid.copyColumnNames` |
| Copiar número de linha(s) | `core.resultset.grid.copyRowNames` |
| Definir com padrão | `core.resultset.cell.setDefault` |
| Definir como NULL | `core.resultset.cell.setNull` |
| Definir cor na linha | `core.resultset.grid.selectRowColor` |
| Duplicar linha | `core.resultset.row.copy` |
| Duplicate row (insert before) | `core.resultset.row.copy.before` |
| Editar célula | `core.resultset.row.edit` |
| Edição em linha | `core.resultset.row.edit.inline` |
| Exibir colunas | `core.resultset.grid.showColumns` |
| Exibir menu de contexto para a coluna | `core.resultset.grid.showColumnContextMenu` |
| Exportar dados | `core.resultset.export` |
| Filtrar por valor | `core.resultset.filterMenu.distinct` |
| Gerar script | `core.resultset.generateScript` |
| Ir para coluna | `core.resultset.grid.gotoColumn` |
| Ir para linha | `core.resultset.grid.gotoRow` |
| Largura das colunas: ajustar aos valores | `core.resultset.grid.columnsFitValue` |
| Largura das colunas: ajustar à tela | `core.resultset.grid.columnsFitScreen` |
| Linha anterior | `core.resultset.row.previous` |
| Maximizar/restaurar painéis | `core.resultset.grid.togglePanelMaximize` |
| Menu de filtros | `core.resultset.filterMenu` |
| Mostrar confirmação antes de salvar | `core.resultset.toggleConfirmSave` |
| Mostrar/ocultar painel de resultados | `core.resultset.grid.togglePanel` |
| Mover coluna para a direita | `core.resultset.grid.moveColumnRight` |
| Mover coluna para a esquerda | `core.resultset.grid.moveColumnLeft` |
| Navegar no link | `core.resultset.navigateLink` |
| Número de linhas | `core.resultset.count` |
| Ocultar colunas | `core.resultset.grid.hideColumns` |
| Ocultar colunas sem dados | `core.resultset.grid.columnsHideEmpty` |
| Personalizar filtros ... | `core.resultset.filterSettings` |
| Primeira linha | `core.resultset.row.first` |
| Próxima linha | `core.resultset.row.next` |
| Recuperar próxima página | `core.resultset.fetch.page` |
| Recuperar todos os dados | `core.resultset.fetch.all` |
| Redefinir alterações da célula | `core.resultset.cell.reset` |
| Referências | `core.resultset.referencesMenu` |
| Rejeitar alterações | `core.resultset.rejectChanges` |
| Remover todos os filtros/ordenamentos | `core.resultset.filterClear` |
| Reset default filter | `core.resultset.filterReset` |
| Salvar configurações de filtro | `core.resultset.filterSave` |
| Seleciona linha(s) | `core.resultset.grid.selectRow` |
| Selecionar coluna(s) | `core.resultset.grid.selectColumn` |
| Zoom in data grid | `core.resultset.zoomIn` |
| Zoom out data grid | `core.resultset.zoomOut` |
| Última linha | `core.resultset.row.last` |

### sql (56)

| Comando | ID |
|---|---|
| Abrir declaração | `ui.editors.sql.navigate.object` |
| Agrupamentos habilitados | `ui.editors.sql.FoldingsEnabled` |
| Alternar comentário de bloco | `ui.editors.sql.comment.multi` |
| Alternar comentário de linha | `ui.editors.sql.comment.single` |
| Alternar disposição do editor | `ui.editors.sql.toggleLayout` |
| Alternar painel ativo | `ui.editors.sql.switch.panel` |
| Alternar painel de resultados | `ui.editors.sql.toggle.result.panel` |
| Alternar quebra de linha | `ui.editors.sql.word.wrap` |
| Auto-sincronizar conexão com o navegador | `ui.editors.sql.sync.auto` |
| Avaliar expressão SQL | `ui.editors.sql.run.expression` |
| Cancelar consulta ativa | `ui.editors.sql.cancel.query` |
| Carregar plano de execução | `ui.editors.sql.load.plan` |
| Consulta anterior | `ui.editors.sql.query.prev` |
| Consulta seguinte | `ui.editors.sql.query.next` |
| Copiar consulta selecionada | `ui.editors.sql.copy.query` |
| DDL | `ui.editors.sql.generate.ddl.by.resultSet` |
| Definir conexão do navegador | `ui.editors.sql.sync.connection` |
| Desabilitar análise de sintaxe SQL | `ui.editors.sql.disableSQLSyntaxParser` |
| Excluir este script | `ui.editors.sql.deleteThisScript` |
| Executar SQL em uma nova aba | `ui.editors.sql.run.statementNew` |
| Executar instrução SQL | `ui.editors.sql.run.statement` |
| Executar instruções em abas separadas | `ui.editors.sql.run.scriptNew` |
| Executar script SQL | `ui.editors.sql.run.script` |
| Executar script SQL da posição | `ui.editors.sql.run.scriptFromPosition` |
| Executar script SQL nativamente | `core.sql.script.run.scriptNative` |
| Exibir painéis nas abas de resultado | `ui.editors.sql.toggle.extraPanels` |
| Exibir resultados em uma ou múltiplas abas | `ui.editors.sql.multipleResultsPerTab` |
| Expandir todos os agrupamentos | `ui.editors.sql.ExpandAllFoldings` |
| Explicar o plano de execução | `ui.editors.sql.run.explain` |
| Exportar da consulta | `ui.editors.sql.export.data` |
| Exportar script SQL | `ui.editors.sql.save.file` |
| Fechar aba | `ui.editors.sql.close.tab` |
| Fixar/desafixar | `ui.editors.sql.toggle.pinned.tab` |
| Formatação de conteúdo | `ui.editors.text.content.format` |
| Importar script SQL | `ui.editors.sql.open.file` |
| Ir para o colchete correspondente | `ui.editors.sql.gotoMatchingBracket` |
| Maximizar painel resultados | `ui.editors.sql.maximize.result.panel` |
| Mostrar estrutura | `ui.editors.sql.show.outline` |
| Mostrar log de execução | `ui.editors.sql.show.log` |
| Mostrar saída do servidor | `ui.editors.sql.show.output` |
| Mostrar variáveis SQL | `ui.editors.sql.show.variables` |
| Ocultar todos os agrupamentos | `ui.editors.sql.CollapseAllFoldings` |
| Pesquisar na web | `ui.editors.sql.search.web` |
| Preencher nome de modelo | `ui.editors.sql.assist.templates` |
| Refresh all schemas | `ui.editors.sql.refresh.all.schemas` |
| Refresh current schema | `ui.editors.sql.refresh.current.schema` |
| Remover espaços | `ui.editors.sql.trim.spaces` |
| Remover espaços finais | `ui.editors.sql.trim.trailing.spaces` |
| Remover espaços iniciais | `ui.editors.sql.trim.leading.spaces` |
| Renomear script SQL | `ui.editors.sql.rename` |
| Selecionar número de linhas | `ui.editors.sql.run.count` |
| Selecionar todas as linhas | `ui.editors.sql.run.all.rows` |
| Select to the matching bracket | `ui.editors.sql.selectToMatchingBracket` |
| Switch presentation to | `ui.editors.sql.switch.presentation` |
| Terminal SQL | `ui.editors.sql.show.terminalView` |
| Transformar em lista delimitada | `ui.editors.sql.morph.delimited.list` |

### database (54)

| Comando | ID |
|---|---|
| Abrir a aba de fontes | `ui.object.property.source.activate` |
| Abrir console SQL | `core.sql.editor.console` |
| Abrir objeto do banco de dados ... | `core.object.goto` |
| Abrir script SQL | `core.sql.editor.defaultCommand` |
| Abrir script SQL | `core.sql.editor.open` |
| Adicionar gráfico | `ui.dashboard.add` |
| Alternar filtro | `core.object.filter.toggle` |
| Atualizar gráfico | `ui.chart.refresh` |
| Auto-commit | `core.txn.autocommit` |
| Comando padrão de abrir | `core.sql.editor.open.default` |
| Commit | `core.commit` |
| Conectar | `core.connect` |
| Configurar filtro | `core.object.filter.config` |
| Configurações do dashboard | `ui.dashboard.configure` |
| Criar dashboard | `ui.dashboard.create` |
| Criar objeto | `core.object.create` |
| Definir conexão ativa | `ui.tools.select.connection` |
| Desconectar | `core.disconnect` |
| Desconectar | `folder.disconnect` |
| Desconectar de outros | `core.disconnectOther` |
| Desconectar de todos | `core.disconnectAll` |
| Desconectar do projeto | `core.disconnectProject` |
| Editar objeto | `core.object.open` |
| Excluir dashboard | `ui.dashboard.delete` |
| Excluir objeto | `core.object.delete` |
| Executar stored procedure | `core.procedure.execute` |
| Exibir scripts | `core.sql.editor.showScripts` |
| Exibir todas as conexões | `navigator.filter.connected` |
| Ferramentas de contexto | `ui.tools.menu` |
| Gerar UUID | `core.generate.uuid` |
| Invalidar/Reconectar | `core.invalidate` |
| Ler dados no console do SQL | `core.sql.editor.forSelection` |
| Limpar filtro | `core.object.filter.clear` |
| Log de transações | `core.txn.log` |
| Mostrar apenas objeto(s) selecionado(s) | `core.object.filter.add.include` |
| Mostrar catálogo de gráficos | `ui.dashboard.catalog.show` |
| Mover para baixo | `core.object.move.down` |
| Mover para cima | `core.object.move.up` |
| Mover para o fundo | `core.object.move.bottom` |
| Mover para o topo | `core.object.move.top` |
| Novo script SQL | `core.sql.editor.create` |
| Novo vínculo para a seleção | `navigator.create.column.constraint` |
| Novo índice para a seleção | `navigator.create.column.index` |
| Ocultar objeto(s) selecionado(s) | `core.object.filter.add.exclude` |
| Remover gráfico | `ui.dashboard.remove` |
| Rollback | `core.rollback` |
| Script SQL recente | `core.sql.editor.recent` |
| Selecionar esquema ativo | `ui.tools.select.schema` |
| Sincronizar | `core.connection.synchronize` |
| Somente-leitura | `core.connection.readonly` |
| Tipo de objeto de filtro de navegador | `navigator.filter.object.type` |
| Transações pendentes | `core.txn.pending` |
| Ver dados | `ui.editors.data.forSelection` |
| Visualizar gráfico | `ui.dashboard.view` |

### navigator (37)

| Comando | ID |
|---|---|
| Adicionar favorito | `core.navigator.bookmark.add` |
| Agrupar por | `task.group` |
| Alterar senha de usuário | `connection.changeCurrentPassword` |
| Associar à fonte de dados | `core.sql.script.associate` |
| Atualizar projeto | `core.project.refresh` |
| Copiar tarefa | `task.copy` |
| Criar diagrama | `erd.diagram.create` |
| Criar link de arquivo | `core.resource.link.file` |
| Criar link para pasta | `core.resource.link.folder` |
| Criar nova pasta | `core.resource.create.folder` |
| Criar nova tarefa ... | `task.create` |
| Criar novo arquivo | `core.resource.create.file` |
| Criar novo diretório de tarefas | `folder.task.create` |
| Criar projeto | `core.project.create` |
| Definir como padrão | `core.navigator.set.default` |
| Definir projeto ativo | `core.project.active` |
| Editar tarefa | `task.edit` |
| Executar tarefa | `task.run` |
| Exibir EULA | `core.eula.showPopup` |
| Exibir recurso no explorador | `core.show.in.explorer` |
| Experimentar DBeaver PRO | `core.try.pro` |
| Focus Database Navigator Filter | `core.navigator.filter.focus` |
| Gerenciador de driver | `core.driver.manager` |
| Link com editor | `core.navigator.linkeditor` |
| Mostrar "Dica do dia" | `ext.ui.tipoftheday.showPopup` |
| Mudar driver de conexão | `core.migrate.connection` |
| Navegar para | `core.navigator.bookmark.navigate` |
| Navegue daqui | `core.navigator.open.browser` |
| Nova conexão | `core.new.connection` |
| Nova conexão com URL JDBC | `core.new.connection.from.url` |
| Nova pasta | `core.new.folder` |
| OID Navigator | `cubrid.OIDNavigator` |
| Preferências | `core.navigator.preferences` |
| Renomear diretório | `folder.rename` |
| Selecionar projeto ativo | `core.project.select` |
| Show/Hide view | `core.view.toggle` |
| Visualizar diagrama | `erd.diagram.view` |

### sem categoria (26)

| Comando | ID |
|---|---|
| Criar banco de dados de exemplo | `ext.sample.database.commands.create` |
| Depurar o objeto de banco de dados mais recente | `debug.ui.command.debugConfigurationMenu` |
| Exportar Tabela(s) | `ext.exasol.ui.exportTable` |
| Foreign data wrappers configurator | `ext.postgresql.ui.fdw` |
| Importar  Tabela(s) | `ext.exasol.ui.importTable` |
| Recuperar erros de messagem do SQL... | `ext.db2.ui.showError` |
| Release notes | `ui.versionUpdate.releaseNotes` |
| Reorganizar Tabela... | `ext.db2.ui.reorgTable` |
| Update | `ui.versionUpdate` |
| org.jkiss.dbeaver.core.commit.menu | `core.commit.menu` |
| org.jkiss.dbeaver.core.menu.newConnection | `core.menu.newConnection` |
| org.jkiss.dbeaver.core.menu.select.connection | `core.menu.select.connection` |
| org.jkiss.dbeaver.core.menu.select.schema | `core.menu.select.schema` |
| org.jkiss.dbeaver.core.menu.txn | `core.menu.txn` |
| org.jkiss.dbeaver.core.menu.txn.log | `core.menu.txn.log` |
| org.jkiss.dbeaver.core.rollback.menu | `core.rollback.menu` |
| org.jkiss.dbeaver.debug.ui.menu.pulldown | `debug.ui.menu.pulldown` |
| org.jkiss.dbeaver.menu.dashboards | `menu.dashboards` |
| org.jkiss.dbeaver.menu.git | `menu.git` |
| org.jkiss.dbeaver.menu.sql.open | `menu.sql.open` |
| org.jkiss.dbeaver.menu.tasks | `menu.tasks` |
| org.jkiss.dbeaver.navigator.filter.object.type.menu | `navigator.filter.object.type.menu` |
| org.jkiss.dbeaver.resultset.export.pulldown | `resultset.export.pulldown` |
| org.jkiss.dbeaver.resultset.save.pulldown | `resultset.save.pulldown` |
| org.jkiss.dbeaver.ui.dashboard.open | `ui.dashboard.open` |
| org.jkiss.dbeaver.ui.versionUpdate.menu | `ui.versionUpdate.menu` |

### util (15)

| Comando | ID |
|---|---|
| Carregar recurso(s) do disco local | `core.edit.load.resource` |
| Clear History... | `core.util.clearHistory` |
| Colar avançado ... | `core.edit.paste.special` |
| Coletar informações de diagnóstico | `core.util.collectDiagnosticInfo` |
| Comparação de estrutura simples | `core.compare.simple` |
| Copiar avançado | `core.edit.copy.special` |
| Copiar avançado com configurações mais recentes | `core.edit.copy.special.with.last.settings` |
| Exportar dados | `core.export.data` |
| Filtros | `core.qm.filter` |
| Finalizar processos | `core.process.stop` |
| Importar dados | `core.import.data` |
| Limpar logs | `core.qm.clear` |
| Reset Settings... | `core.util.resetSettings` |
| Salvar recurso(s) no disco local | `core.edit.save.resource` |
| Show Product Configuration... | `ui.app.config.showWizard` |

### test (10)

| Comando | ID |
|---|---|
| Connection - Stress Test | `test.connection.stressTest` |
| Connection - Validate | `test.connection.validate` |
| Node - Validate | `test.object.validate` |
| Redshift - Test | `test.redshift.runConcurrent` |
| Show auth code dialog | `test.showAuthCodeDialog` |
| Show colors | `test.showColors` |
| Show dialog | `test.dialog` |
| Show forms | `test.showForms` |
| Show icons | `test.showIcons` |
| Show notification | `test.showNotification` |

### view (6)

| Comando | ID |
|---|---|
| Alternar ferramenta de mão | `erd.toggleHand` |
| ERD: Focalizar na borda | `erd.focus.outline` |
| ERD: Focalizar na paleta | `erd.focus.palette` |
| ERD: Focalizar no diagrama | `erd.focus.diagram` |
| ERD: Focalizar nos parâmetros | `erd.focus.parameter` |
| Salvar diagrama como ... | `erd.diagram.saveAs` |

### category (5)

| Comando | ID |
|---|---|
| Atualizar alterações a partir do Git | `git.commands.update` |
| Compartilhar o projeto no Git | `git.commands.share` |
| Criar projeto a partir do Git | `git.commands.projectFromGit` |
| Enviar alterações para o Git | `git.commands.commit` |
| Exibir histórico do Git | `git.commands.showHistory` |

### oracle (3)

| Comando | ID |
|---|---|
| Compile | `ext.oracle.code.compile` |
| Go to source code | `ext.oracle.code.package.navigate` |
| Run | `ext.oracle.job.run` |

### ai (2)

| Comando | ID |
|---|---|
| AI assistant | `ui.ai.showCompletion` |
| AI configuration | `ui.ai.configuration` |

### propsTab (2)

| Comando | ID |
|---|---|
| Aba anterior | `entity.propsTab.prevPage` |
| Aba seguinte | `entity.propsTab.nextPage` |

### office (1)

| Comando | ID |
|---|---|
| Abrir resultados no Excel | `ext.data.office.results.openSpreadsheet` |

### window (1)

| Comando | ID |
|---|---|
| Minimize Window | `ui.window.minimize` |

## Atalhos de teclado

| Sequência | Comando | Contexto | Plugin |
|---|---|---|---|
| `ALT+0` | zoomIn | resultset | ui.editors.data |
| `ALT+1` | diagram | window | ui.editors.erd |
| `ALT+2` | palette | window | ui.editors.erd |
| `ALT+3` | outline | window | ui.editors.erd |
| `ALT+4` | parameter | window | ui.editors.erd |
| `ALT+9` | zoomOut | resultset | ui.editors.data |
| `ALT+ARROW_DOWN` | next | focused | ui.editors.sql |
| `ALT+ARROW_UP` | prev | focused | ui.editors.sql |
| `ALT+DELETE` | delete | focused | ui.editors.data |
| `ALT+INSERT` | add | focused | ui.editors.data |
| `ALT+INSERT` | create | navigator | ui.navigator |
| `ALT+INSERT` | create | navigator | ui.navigator |
| `ALT+N` | scriptNative | focused | ui.editors.sql |
| `ALT+P` | scriptFromPosition | focused | ui.editors.sql |
| `ALT+SHIFT+ARROW_DOWN` | nextPage |  | ui.navigator |
| `ALT+SHIFT+ARROW_LEFT` | moveColumnLeft | resultset | ui.editors.data |
| `ALT+SHIFT+ARROW_RIGHT` | moveColumnRight | resultset | ui.editors.data |
| `ALT+SHIFT+ARROW_UP` | prevPage |  | ui.navigator |
| `ALT+SHIFT+C` | copyColumnNames | resultset | ui.editors.data |
| `ALT+SHIFT+F` | filterSettings | resultset | ui.editors.data |
| `ALT+SHIFT+H` | hideColumns | resultset | ui.editors.data |
| `ALT+SHIFT+T` | showColumns | resultset | ui.editors.data |
| `ALT+SPACE` | navigateLink | resultset | ui.editors.data |
| `ALT+T` | panel | script | ui.editors.sql |
| `ALT+X` | script | focused | ui.editors.sql |
| `ALT+`` | menu |  | core |
| `ALT+`` | menu |  | core |
| `COMMAND+Enter` | recent | navigator | ui.editors.sql |
| `COMMAND+Enter` | statement | focused | ui.editors.sql |
| `COMMAND+M` | minimize | window | ui.app.standalone |
| `COMMAND+\` | statementNew | focused | ui.editors.sql |
| `CTRL+/` | single | sql | ui.editors.sql |
| `CTRL+0` | schema |  | ui.navigator |
| `CTRL+1` | proposals | sql | ui.app.standalone |
| `CTRL+2` | toggleOrder | resultset | ui.editors.data |
| `CTRL+4` | commit | window | core |
| `CTRL+4` | commit | window | core |
| `CTRL+7` | togglePreview | resultset | ui.editors.data |
| `CTRL+8` | rollback | window | core |
| `CTRL+8` | rollback | window | core |
| `CTRL+9` | connection |  | ui.navigator |
| `CTRL+ALT+'` | expression | focused | ui.editors.sql |
| `CTRL+ALT+ARROW_LEFT` | previous | focused | ui.editors.data |
| `CTRL+ALT+ARROW_RIGHT` | next | focused | ui.editors.data |
| `CTRL+ALT+C` | selectColumn | focused | ui.editors.data |
| `CTRL+ALT+D` | below | focused | ui.editors.data |
| `CTRL+ALT+Enter` | console | navigator | ui.editors.sql |
| `CTRL+ALT+F2` | togglePanel | resultset | ui.editors.data |
| `CTRL+ALT+F3` | togglePanel | resultset | ui.editors.data |
| `CTRL+ALT+F4` | togglePanel | resultset | ui.editors.data |
| `CTRL+ALT+F5` | togglePanel | resultset | ui.editors.data |
| `CTRL+ALT+F6` | togglePanel | resultset | ui.editors.data |
| `CTRL+ALT+INSERT` | copy | focused | ui.editors.data |
| `CTRL+ALT+N` | page | focused | ui.editors.data |
| `CTRL+ALT+R` | selectRow | focused | ui.editors.data |
| `CTRL+ALT+SHIFT+A` | rows | focused | ui.editors.sql |
| `CTRL+ALT+SHIFT+ARROW_LEFT` | first | focused | ui.editors.data |
| `CTRL+ALT+SHIFT+ARROW_RIGHT` | last | focused | ui.editors.data |
| `CTRL+ALT+SHIFT+B` | open | perspective | ui.dashboard |
| `CTRL+ALT+SHIFT+D` | add |  | core |
| `CTRL+ALT+SHIFT+ENTER` | save | edit | ui.editors.data |
| `CTRL+ALT+SHIFT+F` | quicksearchCommand | window | ui.app.standalone |
| `CTRL+ALT+SHIFT+K` | commit | window | core |
| `CTRL+ALT+SHIFT+O` | file | sql | ui.editors.sql |
| `CTRL+ALT+SHIFT+R` | rollback | window | core |
| `CTRL+ALT+SHIFT+T` | filter | resultset | ui.editors.data |
| `CTRL+ALT+SHIFT+W` | wrap | sql | ui.editors.sql |
| `CTRL+ALT+SHIFT+X` | scriptNew | focused | ui.editors.sql |
| `CTRL+ALT+SPACE` | contextInformation | sql | ui.app.standalone |
| `CTRL+BACKSPACE` | setDefault | focused | ui.editors.data |
| `CTRL+D` | above | focused | ui.editors.data |
| `CTRL+Enter` | recent | navigator | ui.editors.sql |
| `CTRL+Enter` | recent | navigator | ui.editors.sql |
| `CTRL+Enter` | statement | focused | ui.editors.sql |
| `CTRL+Enter` | statement | focused | ui.editors.sql |
| `CTRL+F11` | distinct | resultset | ui.editors.data |
| `CTRL+F2` | rename | script | ui.editors.sql |
| `CTRL+F3` | create | perspective | ui.editors.sql |
| `CTRL+F9` | compile |  | ext.oracle |
| `CTRL+G` | gotoRow | resultset | ui.editors.data |
| `CTRL+I` | showCompletion | window | ui.ai |
| `CTRL+O` | openLocalFile |  | ui.app.standalone |
| `CTRL+R` | rejectChanges | focused | ui.editors.data |
| `CTRL+S` | applyChanges | focused | ui.editors.data |
| `CTRL+SHIFT+,` | linkeditor |  | ui.navigator |
| `CTRL+SHIFT+.` | connection | navigator | ui.editors.sql |
| `CTRL+SHIFT+/` | multi | sql | ui.editors.sql |
| `CTRL+SHIFT+1` | referencesMenu | resultset | ui.editors.data |
| `CTRL+SHIFT+7` | activatePreview | resultset | ui.editors.data |
| `CTRL+SHIFT+=` | all | focused | ui.editors.data |
| `CTRL+SHIFT+A` | default |  | ui.navigator |
| `CTRL+SHIFT+ALT+C` | settings |  | core |
| `CTRL+SHIFT+ALT+INSERT` | before | focused | ui.editors.data |
| `CTRL+SHIFT+ALT+U` | uuid | window | ui.editors.data |
| `CTRL+SHIFT+ARROW_DOWN` | moveLineDown | sql | ui.app.standalone |
| `CTRL+SHIFT+ARROW_UP` | moveLineUp | sql | ui.app.standalone |
| `CTRL+SHIFT+C` | special |  | core |
| `CTRL+SHIFT+D` | goto | window | ui.navigator |
| `CTRL+SHIFT+E` | explain | focused | ui.editors.sql |
| `CTRL+SHIFT+ENTER` | view | window | ui.editors.erd |
| `CTRL+SHIFT+F` | format | window | ui.editors.sql |
| `CTRL+SHIFT+G` | gotoColumn | focused | ui.editors.data |
| `CTRL+SHIFT+J` | lines | sql | ui.app.standalone |
| `CTRL+SHIFT+K` | commit |  | team.git.ui |
| `CTRL+SHIFT+N` | connection | window | ui.editors.connection |
| `CTRL+SHIFT+O` | output | script | ui.editors.sql |
| `CTRL+SHIFT+P` | tab | script | ui.editors.sql |
| `CTRL+SHIFT+SPACE` | hippieCompletion | sql | ui.app.standalone |
| `CTRL+SHIFT+T` | panel | script | ui.editors.sql |
| `CTRL+SHIFT+U` | update |  | team.git.ui |
| `CTRL+SHIFT+V` | special |  | core |
| `CTRL+SHIFT+[` | gotoMatchingBracket | sql | ui.editors.sql |
| `CTRL+SHIFT+\` | tab | script | ui.editors.sql |
| `CTRL+SHIFT+]` | selectToMatchingBracket | sql | ui.editors.sql |
| `CTRL+SPACE` | proposals | sql | ui.app.standalone |
| `CTRL+T` | panel | script | ui.editors.sql |
| `CTRL+V` | menu |  | core |
| `CTRL+[` | open | perspective | ui.editors.sql |
| `CTRL+\` | statementNew | focused | ui.editors.sql |
| `CTRL+\` | statementNew | focused | ui.editors.sql |
| `CTRL+\` | statementNew | focused | ui.editors.sql |
| `CTRL+]` | create | perspective | ui.editors.sql |
| `CTRL+`` | switchPresentation | resultset | ui.editors.data |
| `ENTER` | inline | focused | ui.editors.data |
| `ESC` | reset | focused | ui.editors.data |
| `Enter` | edit |  | tasks.ui.view |
| `F1` | dynamicHelp | window | ui.app.standalone |
| `F11` | filterMenu | resultset | ui.editors.data |
| `F3` | open | perspective | ui.editors.sql |
| `F4` | edit |  | tasks.ui.view |
| `F4` | object | focused | ui.editors.sql |
| `F4` | open | navigator | ui.navigator |
| `F7` | togglePreview | resultset | ui.editors.data |
| `M1+Enter` | sendPrompt | chat | ui.ai |
| `M1+L` | focusPrompt | chat | ui.ai |
| `M1+M2+DEL` | deleteConversation | chat | ui.ai |
| `M1+M2+F` | openFilters | chat | ui.ai |
| `M1+M2+M` | focusChat | chat | ui.ai |
| `M1+M2+S` | openSettings | chat | ui.ai |
| `M1+N` | create | navigator | ui.navigator |
| `M1+N` | newConversation | chat | ui.ai |
| `M1+U` | attach | chat | ui.ai |
| `SHIFT+ALT+INSERT` | before | focused | ui.editors.data |
| `SHIFT+ENTER` | edit | focused | ui.editors.data |
| `SHIFT+F11` | showColumnContextMenu | resultset | ui.editors.data |
| `TAB` | toggleHand | window | ui.editors.erd |
| `TAB` | toggleMode | resultset | ui.editors.data |

## Views e painéis (23)

| Nome | Plugin |
|---|---|
| Arquivos | ui.navigator |
| Chat | ui.ai |
| Conexões | ui.navigator |
| Dashboard de banco de dados | ui.dashboard |
| Gerenciador de consulta | core |
| Log de erros | ui.app.standalone |
| Modelos | ui.app.standalone |
| Navegador de banco de dados | ui.navigator |
| Outline | ui.app.standalone |
| Pesquisar | ui.app.standalone |
| Processo | core |
| Projetos | ui.navigator |
| Propriedades | ui.app.standalone |
| SSH tunnel explorer | net.ssh.ui |
| Tarefas de banco de dados | tasks.ui.view |
| Tarefas em plano de fundo | ui.app.standalone |
| com.dbeaver.ai.chat | ui.ai |
| fulltext | ext.cubrid.ui |
| org.eclipse.debug.ui.BreakpointView | debug.ui |
| org.eclipse.debug.ui.DebugView | debug.ui |
| org.eclipse.debug.ui.VariableView | debug.ui |
| org.jkiss.dbeaver.tasks | tasks.ui.view |
| simple | ui.editors.sql |

## Editores (58)

| Nome | Plugin |
|---|---|
| Editor ERD | ui.editors.erd |
| Editor SQL | ui.editors.sql |
| Editor de entidade | ui.navigator |
| Editor de objetos grandes | ui.editors.data |
| Editor de pasta | ui.navigator |
| Gerenciador de aplicação | ext.exasol.ui |
| Gerenciador de travas | ext.altibase.ui |
| Gerenciador de travas | ext.postgresql.ui |
| JSON | ui.editors.json |
| Resultados SQL | ui.editors.sql |
| Session Manager | ext.altibase.ui |
| Session Manager | ext.mssql.ui |
| Session Manager | ext.mysql.ui |
| Session Manager | ext.oracle.ui |
| Session Manager | ext.postgresql.ui |
| XML | ui.editors.xml |
| db-logical-structure | ui.editors.data |
| editor.dashboard.name | ui.dashboard |
| editor.org.jkiss.dbeaver.ext.db2.ui.editors.DB2ServerApplicationEditor.name | ext.db2.ui |
| editor.org.jkiss.dbeaver.ext.exasol.ui.editors.ExasolLockEditor.name | ext.exasol.ui |
| exasol.source.view | ext.exasol.ui |
| generic.source.view | ext.generic.ui |
| generic.table.ddl.view | ext.generic.ui |
| generic.view.source.view | ext.generic.ui |
| mssql.source.view | ext.mssql.ui |
| mssql.table.ddl.view | ext.mssql.ui |
| mssql.view.source.view | ext.mssql.ui |
| mysql.package.body.view | ext.mysql.ui |
| mysql.source.ddl | ext.mysql.ui |
| mysql.source.view | ext.mysql.ui |
| org.jkiss.dbeaver.ext.cubrid.ui.editors.CubridPrivilageEditor | ext.cubrid.ui |
| org.jkiss.dbeaver.ext.iotdb.ui.editors.IoTDBUserEditorGeneral | ext.iotdb.ui |
| org.jkiss.dbeaver.ext.iotdb.ui.editors.IoTDBUserEditorPrivileges | ext.iotdb.ui |
| org.jkiss.dbeaver.ext.mysql.ui.editors.MySQLUserEditorGeneral | ext.mysql.ui |
| org.jkiss.dbeaver.ext.mysql.ui.editors.MySQLUserEditorPrivileges | ext.mysql.ui |
| org.jkiss.dbeaver.ext.oracle.ui.editors.SchedulerJobLogEditor | ext.oracle.ui |
| org.jkiss.dbeaver.ext.postgresql.ui.editors.PostgreScheduleEditor | ext.postgresql.ui |
| org.jkiss.dbeaver.ui.editors.data.DatabaseDataEditor | ui.editors.data |
| org.jkiss.dbeaver.ui.editors.erd.editor.ERDEditorEmbedded | ui.editors.erd |
| postgresql.role.permissions | ext.postgresql.ui |
| postgresql.source.ddl | ext.postgresql.ui |
| postgresql.source.view | ext.gaussdb.ui |
| postgresql.source.view | ext.postgresql.ui |
| postgresql.table.permissions | ext.postgresql.ui |
| schedulerJob.action | ext.oracle.ui |
| source.ddl | ext.dameng.ui |
| source.ddl | ext.db2.ui |
| source.ddl | ext.exasol.ui |
| source.ddl | ext.oracle.ui |
| source.declaration | ext.altibase.ui |
| source.declaration | ext.db2.ui |
| source.declaration | ext.exasol.ui |
| source.declaration | ext.oracle.ui |
| source.declaration.read-only | ext.oracle.ui |
| source.definition | ext.oracle.ui |
| source.routine.ddl | ext.db2.ui |

## Assistentes (19)

| Nome | Plugin |
|---|---|
| Conexão com banco de dados | ui.editors.connection |
| Custom | ui.config.migration |
| DBVisualizer | ui.config.migration |
| Dashboard | ui.dashboard |
| DataGrip and JetBrains IDEs | ui.config.migration |
| Diagrama ER | ui.editors.erd |
| MySQL Workbench | ui.config.migration |
| Navicat | ui.config.migration |
| Oracle SQL Developer | ext.oracle.ui |
| Preferências | core |
| Projeto | core |
| Projeto de banco de dados | ui.navigator |
| SQL Squirrel | ui.config.migration |
| Scripts | core |
| Toad | ext.oracle.ui |
| pgAdmin 4 | ui.config.migration |

## Páginas de preferências (109)

| Nome | Plugin |
|---|---|
| Acessibilidade | core |
| Altibase | ext.altibase.ui |
| Altibase settings | ext.altibase.ui |
| Apresentação | ui.editors.data |
| CData | ext.cdata.ui |
| CUBRID | ext.cubrid.ui |
| Caminho de classe | ui.app.eclipse |
| Caminho de classe | ui.app.standalone |
| Chat | ui.ai |
| Conexões | ui.app.eclipse |
| Conexões | ui.app.standalone |
| Conexões | ui.editors.connection |
| Configurações ERD | ui.editors.erd |
| Configurações avançadas | ui.app.eclipse |
| Configurações avançadas | ui.app.standalone |
| Configurações do DBeaver | ui.navigator |
| Configurações do Oracle | ext.oracle.ui |
| Confirmações | ui.app.eclipse |
| Confirmações | ui.app.standalone |
| Customização da barra de ferramentas | core |
| Dicionários | ui.editors.data |
| Drivers | ui.app.eclipse |
| Drivers | ui.app.standalone |
| Editor HEX | ui.editors.hex |
| Editor SQL | ui.editors.sql |
| Editor de binários | ui.editors.data |
| Editor de código | ui.editors.sql |
| Editor de dados | ui.editors.data |
| Editor de diagrama | ui.editors.erd |
| Editores | core |
| Editores | ui.app.eclipse |
| Editores | ui.app.standalone |
| Erros e timeouts | ui.app.eclipse |
| Erros e timeouts | ui.app.standalone |
| Erros e timeouts | ui.editors.connection |
| Estatísticas de uso | ui.statistics |
| File details | ui.navigator |
| Formatação SQL | ui.editors.sql |
| Formatos de dados | ui.editors.data |
| General | ui.app.standalone |
| Gerenciador de consulta | ui.app.eclipse |
| Gerenciador de consulta | ui.app.standalone |
| Gerenciador de consultas | core |
| Grade | ui.editors.data |
| H2 | ext.h2.ui |
| IA | ui.ai |
| Identificação do cliente | ui.app.eclipse |
| Identificação do cliente | ui.app.standalone |
| Interface de usuário | ui.app.eclipse |
| Interface de usuário | ui.app.standalone |
| Interface de usuário do banco de dados | ui.app.eclipse |
| Logs de erros | ui.app.eclipse |
| Logs de erros | ui.app.standalone |
| Maven | ui.app.eclipse |
| Maven | ui.app.standalone |
| Metadados | ui.app.eclipse |
| Metadados | ui.app.standalone |
| Metadados | ui.editors.connection |
| Miscelânea | ui.app.eclipse |
| Miscelânea | ui.app.standalone |
| Model configurations | ui.ai |
| Modelos | ui.editors.sql |
| Navegador | ui.app.eclipse |
| Navegador | ui.app.standalone |
| Navegador de banco de dados | ui.navigator |
| Network Profiles | ui.app.eclipse |
| Network Profiles | ui.app.standalone |
| Notificações | ui.app.eclipse |
| Notificações | ui.app.standalone |
| Oracle | ext.oracle.ui |
| Perfis de rede | ui.editors.connection |
| PostgreSQL | ext.postgresql.ui |
| Preenchimento de código | ui.editors.sql |
| Processamento de SQL | ui.editors.sql |
| Prompts | ui.ai |
| Scripts | ui.editors.sql |
| Settings | ui.ai |
| Synchronization | ui.datadam |
| Terminal SQL | ui.editors.sql.terminal |
| Texto puro | ui.editors.data |
| Tipos de conexão | ui.app.eclipse |
| Tipos de conexão | ui.app.standalone |
| Tipos de conexão | ui.editors.connection |
| Transações | ui.app.eclipse |
| Transações | ui.app.standalone |
| Transações | ui.editors.connection |
| Transferência de dados | data.transfer.ui |
| Visualizador SIG | data.gis.view |

## Pontos de extensão (85)

| ID | Plugin |
|---|---|
| `chatView` | ui.ai |
| `com.dbeaver.ai.assistant` | model.ai |
| `com.dbeaver.ai.credentialsProvider` | model.ai |
| `com.dbeaver.ai.engine` | model.ai |
| `com.dbeaver.ai.function` | model.ai |
| `com.dbeaver.ai.prompt` | model.ai |
| `com.dbeaver.secretController` | model.sm |
| `org.jkiss.dbeaver.aggregateFunction` | ui.editors.data |
| `org.jkiss.dbeaver.app.config` | model |
| `org.jkiss.dbeaver.application` | model |
| `org.jkiss.dbeaver.clearHistoryHandler` | core |
| `org.jkiss.dbeaver.commandLine` | model.cli |
| `org.jkiss.dbeaver.confirmations` | model.sql |
| `org.jkiss.dbeaver.connectionPageConfigurator` | ui.editors.connection |
| `org.jkiss.dbeaver.dashboard` | model.dashboard |
| `org.jkiss.dbeaver.dashboard.ui` | ui.dashboard |
| `org.jkiss.dbeaver.data.gis.geometryViewer` | data.gis.view |
| `org.jkiss.dbeaver.data.gis.leaflet.tiles` | data.gis.view |
| `org.jkiss.dbeaver.dataFormatter` | model |
| `org.jkiss.dbeaver.dataHintProvider` | model |
| `org.jkiss.dbeaver.dataManager` | ui.editors.data |
| `org.jkiss.dbeaver.dataSourceAuth` | registry |
| `org.jkiss.dbeaver.dataSourceConfigurator` | ui.editors.connection |
| `org.jkiss.dbeaver.dataSourceHandler` | registry |
| `org.jkiss.dbeaver.dataSourceProvider` | registry |
| `org.jkiss.dbeaver.dataSourceStorage` | registry |
| `org.jkiss.dbeaver.dataTransfer` | data.transfer |
| `org.jkiss.dbeaver.dataTransferConfigurator` | data.transfer.ui |
| `org.jkiss.dbeaver.dataTypeProvider` | model |
| `org.jkiss.dbeaver.databaseEditor` | ui.navigator |
| `org.jkiss.dbeaver.debug.ui.configurationPanels` | debug.ui |
| `org.jkiss.dbeaver.driverManager` | registry |
| `org.jkiss.dbeaver.expressions` | model |
| `org.jkiss.dbeaver.fileSystem` | model |
| `org.jkiss.dbeaver.fileTypeHandler` | model |
| `org.jkiss.dbeaver.generic.meta` | ext.generic |
| `org.jkiss.dbeaver.language` | registry |
| `org.jkiss.dbeaver.lsm.dialectSyntax` | model.lsm |
| `org.jkiss.dbeaver.mavenRepository` | registry |
| `org.jkiss.dbeaver.navigator` | model |
| `org.jkiss.dbeaver.navigator.nodeAction` | ui.navigator |
| `org.jkiss.dbeaver.net.ssh` | net.ssh |
| `org.jkiss.dbeaver.networkHandler` | registry |
| `org.jkiss.dbeaver.notifications` | ui |
| `org.jkiss.dbeaver.objectManager` | registry |
| `org.jkiss.dbeaver.pluginService` | registry |
| `org.jkiss.dbeaver.postgresql.fdw.config` | ext.postgresql |
| `org.jkiss.dbeaver.postgresql.serverType` | ext.postgresql |
| `org.jkiss.dbeaver.product.bundles` | registry |
| `org.jkiss.dbeaver.productFeature` | registry |
| `org.jkiss.dbeaver.resourceHandler` | model.rcp |
| `org.jkiss.dbeaver.resourceType` | registry |
| `org.jkiss.dbeaver.resources` | registry |
| `org.jkiss.dbeaver.resultset.error` | ui.editors.data |
| `org.jkiss.dbeaver.resultset.grouping` | ui.editors.data |
| `org.jkiss.dbeaver.resultset.panel` | ui.editors.data |
| `org.jkiss.dbeaver.resultset.presentation` | ui.editors.data |
| `org.jkiss.dbeaver.serialize` | model |
| `org.jkiss.dbeaver.service` | model |
| `org.jkiss.dbeaver.settings` | model |
| `org.jkiss.dbeaver.sql.covert` | ui.editors.sql |
| `org.jkiss.dbeaver.sql.editorAddIns` | ui.editors.sql |
| `org.jkiss.dbeaver.sql.executors` | ui.editors.sql |
| `org.jkiss.dbeaver.sql.plan.view` | ui.editors.sql |
| `org.jkiss.dbeaver.sql.quickFixProcessors` | ui.editors.sql |
| `org.jkiss.dbeaver.sqlBackup` | model.jdbc |
| `org.jkiss.dbeaver.sqlCommand` | model.sql |
| `org.jkiss.dbeaver.sqlDialect` | model.sql |
| `org.jkiss.dbeaver.sqlFormatter` | model.sql |
| `org.jkiss.dbeaver.sqlGenerator` | model.sql |
| `org.jkiss.dbeaver.sqlInsertMethod` | model.sql |
| `org.jkiss.dbeaver.sqlPresentation` | ui.editors.sql |
| `org.jkiss.dbeaver.syncUnit` | model |
| `org.jkiss.dbeaver.task` | registry |
| `org.jkiss.dbeaver.task.ui` | tasks.ui |
| `org.jkiss.dbeaver.toolBarConfiguration` | core |
| `org.jkiss.dbeaver.tools` | tasks.ui |
| `org.jkiss.dbeaver.ui.app.config` | ui.app.config |
| `org.jkiss.dbeaver.ui.editors.erd.export.format` | ui.editors.erd |
| `org.jkiss.dbeaver.ui.editors.erd.notation.style` | ui.editors.erd |
| `org.jkiss.dbeaver.ui.editors.erd.routing` | ui.editors.erd |
| `org.jkiss.dbeaver.ui.propertyConfigurator` | ui |
| `org.jkiss.dbeaver.workbenchHandler` | core |
| `org.jkiss.dbeaver.ws.event` | model.event |
| `org.jkiss.dbeaver.ws.event.handler` | model.event |

## Cobertura do C-Otter

Um comando só conta como implementado quando a **ação existe na interface** — não
quando o código de apoio existe no núcleo.

| Métrica | Valor |
|---|---|
| Comandos do DBeaver | 281 |
| Implementados no C-Otter | **191** |
| Cobertura de comandos | **68.0%** |
| Atalhos do DBeaver | 147 |
| Atalhos no C-Otter | **116** (perfil DBeaver; ver `docs/EDITOR-COMMANDS.md`) |
| Cobertura de atalhos | **78.9%** |
| Diálogos do DBeaver | 151 |
| Diálogos, páginas e assistentes no C-Otter | ver `docs/ELEMENTS.md` e `docs/OBJECT-EDITOR.md` |

## Comandos que faltam

Os comandos do DBeaver sem ação no C-Otter, por plugin. A coluna Tecla traz o
atalho do DBeaver, quando há.

### core (11)

| Comando | ID | Tecla |
|---|---|---|
| Desconectar | `folder.disconnect` |  |
| Desconectar do projeto | `core.disconnectProject` |  |
| Exibir EULA | `core.eula.showPopup` |  |
| Sincronizar | `core.connection.synchronize` |  |
| org.jkiss.dbeaver.core.commit.menu | `core.commit.menu` |  |
| org.jkiss.dbeaver.core.menu.newConnection | `core.menu.newConnection` |  |
| org.jkiss.dbeaver.core.menu.select.connection | `core.menu.select.connection` |  |
| org.jkiss.dbeaver.core.menu.select.schema | `core.menu.select.schema` |  |
| org.jkiss.dbeaver.core.menu.txn | `core.menu.txn` |  |
| org.jkiss.dbeaver.core.menu.txn.log | `core.menu.txn.log` |  |
| org.jkiss.dbeaver.core.rollback.menu | `core.rollback.menu` |  |

### ui.navigator (11)

| Comando | ID | Tecla |
|---|---|---|
| Atualizar projeto | `core.project.refresh` |  |
| Criar link de arquivo | `core.resource.link.file` |  |
| Criar link para pasta | `core.resource.link.folder` |  |
| Criar nova pasta | `core.resource.create.folder` |  |
| Criar novo arquivo | `core.resource.create.file` |  |
| Criar projeto | `core.project.create` |  |
| Definir projeto ativo | `core.project.active` |  |
| Navegue daqui | `core.navigator.open.browser` |  |
| Selecionar projeto ativo | `core.project.select` |  |
| Tipo de objeto de filtro de navegador | `navigator.filter.object.type` |  |
| org.jkiss.dbeaver.navigator.filter.object.type.menu | `navigator.filter.object.type.menu` |  |

### ui.app.devtools (10)

| Comando | ID | Tecla |
|---|---|---|
| Connection - Stress Test | `test.connection.stressTest` |  |
| Connection - Validate | `test.connection.validate` |  |
| Node - Validate | `test.object.validate` |  |
| Redshift - Test | `test.redshift.runConcurrent` |  |
| Show auth code dialog | `test.showAuthCodeDialog` |  |
| Show colors | `test.showColors` |  |
| Show dialog | `test.dialog` |  |
| Show forms | `test.showForms` |  |
| Show icons | `test.showIcons` |  |
| Show notification | `test.showNotification` |  |

### tasks.ui.view (8)

| Comando | ID | Tecla |
|---|---|---|
| Agrupar por | `task.group` |  |
| Copiar tarefa | `task.copy` |  |
| Criar nova tarefa ... | `task.create` |  |
| Criar novo diretório de tarefas | `folder.task.create` |  |
| Editar tarefa | `task.edit` | `Enter, F4` |
| Executar tarefa | `task.run` |  |
| Renomear diretório | `folder.rename` |  |
| org.jkiss.dbeaver.menu.tasks | `menu.tasks` |  |

### ui.editors.erd (8)

| Comando | ID | Tecla |
|---|---|---|
| Alternar ferramenta de mão | `erd.toggleHand` | `TAB` |
| Criar diagrama | `erd.diagram.create` |  |
| ERD: Focalizar na borda | `erd.focus.outline` | `ALT+3` |
| ERD: Focalizar na paleta | `erd.focus.palette` | `ALT+2` |
| ERD: Focalizar no diagrama | `erd.focus.diagram` | `ALT+1` |
| ERD: Focalizar nos parâmetros | `erd.focus.parameter` | `ALT+4` |
| Salvar diagrama como ... | `erd.diagram.saveAs` |  |
| Visualizar diagrama | `erd.diagram.view` | `CTRL+SHIFT+ENTER` |

### team.git.ui (6)

| Comando | ID | Tecla |
|---|---|---|
| Atualizar alterações a partir do Git | `git.commands.update` | `CTRL+SHIFT+U` |
| Compartilhar o projeto no Git | `git.commands.share` |  |
| Criar projeto a partir do Git | `git.commands.projectFromGit` |  |
| Enviar alterações para o Git | `git.commands.commit` | `CTRL+SHIFT+K` |
| Exibir histórico do Git | `git.commands.showHistory` |  |
| org.jkiss.dbeaver.menu.git | `menu.git` |  |

### ui.app.standalone (6)

| Comando | ID | Tecla |
|---|---|---|
| Experimentar DBeaver PRO | `core.try.pro` |  |
| Minimize Window | `ui.window.minimize` | `COMMAND+M` |
| Mostrar "Dica do dia" | `ext.ui.tipoftheday.showPopup` |  |
| Release notes | `ui.versionUpdate.releaseNotes` |  |
| Update | `ui.versionUpdate` |  |
| org.jkiss.dbeaver.ui.versionUpdate.menu | `ui.versionUpdate.menu` |  |

### ui.editors.sql (6)

| Comando | ID | Tecla |
|---|---|---|
| Abrir script SQL | `core.sql.editor.defaultCommand` |  |
| Comando padrão de abrir | `core.sql.editor.open.default` |  |
| Exibir painéis nas abas de resultado | `ui.editors.sql.toggle.extraPanels` |  |
| Exibir resultados em uma ou múltiplas abas | `ui.editors.sql.multipleResultsPerTab` |  |
| Switch presentation to | `ui.editors.sql.switch.presentation` |  |
| org.jkiss.dbeaver.menu.sql.open | `menu.sql.open` |  |

### ui.editors.data (4)

| Comando | ID | Tecla |
|---|---|---|
| Alternar disposição de painéis | `core.resultset.grid.toggleLayout` |  |
| Maximizar/restaurar painéis | `core.resultset.grid.togglePanelMaximize` |  |
| org.jkiss.dbeaver.resultset.export.pulldown | `resultset.export.pulldown` |  |
| org.jkiss.dbeaver.resultset.save.pulldown | `resultset.save.pulldown` |  |

### ext.oracle.ui (3)

| Comando | ID | Tecla |
|---|---|---|
| Compile | `ext.oracle.code.compile` | `CTRL+F9` |
| Go to source code | `ext.oracle.code.package.navigate` |  |
| Run | `ext.oracle.job.run` |  |

### ui.dashboard (3)

| Comando | ID | Tecla |
|---|---|---|
| Criar dashboard | `ui.dashboard.create` |  |
| Excluir dashboard | `ui.dashboard.delete` |  |
| org.jkiss.dbeaver.menu.dashboards | `menu.dashboards` |  |

### debug.ui (2)

| Comando | ID | Tecla |
|---|---|---|
| Depurar o objeto de banco de dados mais recente | `debug.ui.command.debugConfigurationMenu` |  |
| org.jkiss.dbeaver.debug.ui.menu.pulldown | `debug.ui.menu.pulldown` |  |

### ext.db2.ui (2)

| Comando | ID | Tecla |
|---|---|---|
| Recuperar erros de messagem do SQL... | `ext.db2.ui.showError` |  |
| Reorganizar Tabela... | `ext.db2.ui.reorgTable` |  |

### ext.exasol.ui (2)

| Comando | ID | Tecla |
|---|---|---|
| Exportar Tabela(s) | `ext.exasol.ui.exportTable` |  |
| Importar  Tabela(s) | `ext.exasol.ui.importTable` |  |

### ui.ai (2)

| Comando | ID | Tecla |
|---|---|---|
| AI assistant | `ui.ai.showCompletion` | `CTRL+I` |
| AI configuration | `ui.ai.configuration` |  |

### cmp.simple.ui (1)

| Comando | ID | Tecla |
|---|---|---|
| Comparação de estrutura simples | `core.compare.simple` |  |

### ext.cubrid.ui (1)

| Comando | ID | Tecla |
|---|---|---|
| OID Navigator | `cubrid.OIDNavigator` |  |

### ext.postgresql.ui (1)

| Comando | ID | Tecla |
|---|---|---|
| Foreign data wrappers configurator | `ext.postgresql.ui.fdw` |  |

### ui.app.config (1)

| Comando | ID | Tecla |
|---|---|---|
| Show Product Configuration... | `ui.app.config.showWizard` |  |

### ui.config.sample (1)

| Comando | ID | Tecla |
|---|---|---|
| Criar banco de dados de exemplo | `ext.sample.database.commands.create` |  |

### ui.editors.connection (1)

| Comando | ID | Tecla |
|---|---|---|
| Mudar driver de conexão | `core.migrate.connection` |  |

## Atalhos que faltam

| Sequência | Comando | Contexto | Plugin |
|---|---|---|---|
| `ALT+1` | `erd.focus.diagram` | window | ui.editors.erd |
| `ALT+2` | `erd.focus.palette` | window | ui.editors.erd |
| `ALT+3` | `erd.focus.outline` | window | ui.editors.erd |
| `ALT+4` | `erd.focus.parameter` | window | ui.editors.erd |
| `COMMAND+M` | `ui.window.minimize` | window | ui.app.standalone |
| `CTRL+1` | `org.eclipse.jdt.ui.edit.text.java.correction.assist.proposals` | sql | ui.app.standalone |
| `CTRL+ALT+SHIFT+F` | `org.eclipse.text.quicksearch.commands.quicksearchCommand` | window | ui.app.standalone |
| `CTRL+ALT+SPACE` | `org.eclipse.ui.edit.text.contentAssist.contextInformation` | sql | ui.app.standalone |
| `CTRL+F9` | `ext.oracle.code.compile` |  | ext.oracle |
| `CTRL+I` | `ui.ai.showCompletion` | window | ui.ai |
| `CTRL+O` | `org.eclipse.ui.edit.text.openLocalFile` |  | ui.app.standalone |
| `CTRL+SHIFT+ARROW_DOWN` | `org.eclipse.ui.edit.text.moveLineDown` | sql | ui.app.standalone |
| `CTRL+SHIFT+ARROW_UP` | `org.eclipse.ui.edit.text.moveLineUp` | sql | ui.app.standalone |
| `CTRL+SHIFT+ENTER` | `erd.diagram.view` | window | ui.editors.erd |
| `CTRL+SHIFT+J` | `org.eclipse.ui.edit.text.join.lines` | sql | ui.app.standalone |
| `CTRL+SHIFT+K` | `git.commands.commit` |  | team.git.ui |
| `CTRL+SHIFT+SPACE` | `org.eclipse.ui.edit.text.hippieCompletion` | sql | ui.app.standalone |
| `CTRL+SHIFT+U` | `git.commands.update` |  | team.git.ui |
| `CTRL+SPACE` | `org.eclipse.ui.edit.text.contentAssist.proposals` | sql | ui.app.standalone |
| `Enter` | `task.edit` |  | tasks.ui.view |
| `F1` | `org.eclipse.ui.help.dynamicHelp` | window | ui.app.standalone |
| `F4` | `task.edit` |  | tasks.ui.view |
| `M1+Enter` | `com.dbeaver.ai.chat.sendPrompt` | chat | ui.ai |
| `M1+L` | `com.dbeaver.ai.chat.focusPrompt` | chat | ui.ai |
| `M1+M2+DEL` | `com.dbeaver.ai.chat.deleteConversation` | chat | ui.ai |
| `M1+M2+F` | `com.dbeaver.ai.chat.openFilters` | chat | ui.ai |
| `M1+M2+M` | `com.dbeaver.ai.chat.focusChat` | chat | ui.ai |
| `M1+M2+S` | `com.dbeaver.ai.chat.openSettings` | chat | ui.ai |
| `M1+N` | `com.dbeaver.ai.chat.newConversation` | chat | ui.ai |
| `M1+U` | `com.dbeaver.ai.chat.attach` | chat | ui.ai |
| `TAB` | `erd.toggleHand` | window | ui.editors.erd |

