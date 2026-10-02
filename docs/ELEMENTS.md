# Guia de elementos — o que funciona e o que não funciona

**Última verificação: 2026-09-21** · build `win-release`, PostgreSQL 18.2, bancos
`ERP_TID` (schema `public`) e `otter_test` (fixture)

Este é o guia operacional: cada painel, botão, menu e atalho do C-Otter, com o estado
**verificado na aplicação rodando** — não deduzido do código.

Complementa [`PARITY.md`](PARITY.md), que lista funcionalidades em alto nível contra o
DBeaver. Aqui a granularidade é o *elemento de interface*.

**Legenda**

| Símbolo | Significado |
|---------|-------------|
| ✅ | Funciona — verificado na aplicação |
| 🟡 | Existe mas incompleto, ou implementado sem estar ligado à UI |
| ⬜ | Não existe ainda |
| ❌ | Existe e está quebrado |
| ➖ | Fora do escopo da v1 |

---

## 1. Janela e estrutura

| Elemento | Estado | Observação |
|----------|--------|------------|
| Janela principal | ✅ | GLFW + OpenGL 3.3, mesmo código em Windows e Linux |
| Título com nome do produto | ✅ | `C-Otter - every JOIN is an OTTER JOIN` |
| Redimensionar / maximizar / minimizar | ✅ | Minimizada, dorme em vez de renderizar |
| Posicionamento inicial centralizado | ✅ | Respeita a área de trabalho do monitor |
| Escala por DPI | ✅ | `glfwGetMonitorContentScale` |
| **Fonte do sistema na interface** | ✅ | Segoe UI / DejaVu Sans. Até 2026-09-21 a UI inteira era monoespaçada |
| **Fonte monoespaçada no editor SQL** | ✅ | Cascadia Mono / Consolas — só o editor: SQL é código |
| Fonte da grade de resultados | ✅ | A da interface; o alinhamento vem do `ImGuiTable` |
| **Três temas** | ✅ | Escuro (paleta de `Midia/Logo.png`), Claro e Âmbar |
| Troca de tema em tempo real | ✅ | Ajuda → Tema; contraste WCAG testado |
| **Internacionalização** | ✅ | Inglês padrão + pt-BR embutido |
| Detecção do idioma do sistema | ✅ | Idioma de **exibição** do Windows, não o formato regional; `LC_ALL`/`LC_MESSAGES`/`LANG` no POSIX. Fallback por idioma base (`pt-PT` → `pt-BR`) |
| Troca de idioma em tempo real | ✅ | Help → Language, sem reiniciar |
| Idiomas por arquivo `.lang` | ✅ | Sem recompilar; ver `lang/README.md` |
| Relatório de textos sem tradução | ✅ | `missing_translations()` / `export_template()` |
| Docking de painéis (arrastar abas) | ✅ | |
| **Layout persistido** | ✅ | `%APPDATA%\C-Otter\layout.ini`, não no diretório de trabalho |
| **Ícone da janela** | ✅ | Gerado em memória (32x32 RGBA) — decodificar o PNG exigiria trazer um stb_image só para isto |
| Splash screen | ➖ | `Midia/Splash.png` existe, mas exibi-lo pede um decodificador de PNG (o projeto só tem `stb_image_write`). O C-Otter abre em ~300 ms — um splash apareceria depois da janela, o que é pior que não ter |
| Múltiplas janelas | ➖ | O docking do ImGui já põe duas conexões lado a lado na MESMA janela, que é o caso de uso real. Janelas de SO separadas exigiriam viewports múltiplos e um contexto ImGui por janela — custo alto para o que o docking já resolve |

### Dados do programa (ADR 0020, 2026-09-30)

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Pasta `.C-Otter` ao lado do executável** | ✅ | `settings.json`, `layout.ini`, `data-sources.json`, `credentials-config.json`. Conferido iniciando o programa a partir de outra pasta |
| `settings.json` criado com os padrões | ✅ | Todas as opções gravadas; tema padrão **âmbar** |
| Conexões antigas (`%APPDATA%\C-Otter`) copiadas uma vez | ✅ | Com as senhas; o antigo fica intacto |
| Diálogo de conexão no início | ✅ | Só abre sozinho quando **não há** conexão salva; antes abria sempre |
| Sessão vazia como nó solto na raiz da árvore | ✅ corrigido | Ficava escondida atrás do diálogo |

## 2. Barra de menus

| Menu → Item | Atalho | Estado | Observação |
|-------------|--------|--------|------------|
| **Arquivo** → Nova conexão... | `Ctrl+Shift+N` | ✅ | Item e atalho |
| **Arquivo** → Importar do DBeaver... | — | ✅ | Lê os workspaces reais; nunca escreve neles |
| **Arquivo** → Desconectar | — | ✅ | Desabilitado quando não há conexão |
| **Arquivo** → Sair | `Alt+F4` | ✅ | |
| **Arquivo** → Abrir script | `Ctrl+O` | ✅ | Diálogo nativo do sistema; abre em aba nova |
| **Arquivo** → Salvar script | `Ctrl+S` | ✅ | Adianta a gravação automática (sem diálogo); `Ctrl+Shift+S` para "salvar como", que tira o script de `.script` |
| **Editar** → Desfazer | `Ctrl+Z` | ✅ | Atalho global, não só com o editor em foco |
| **Editar** → Refazer | `Ctrl+Y` | ✅ | Idem |
| **Editar** → Selecionar tudo | `Ctrl+A` | ✅ | |
| **Editar** → Localizar | `Ctrl+F` | ✅ | Item abre a janela de busca do editor |
| **SQL** → Executar | `Ctrl+Enter` | ✅ | Item e atalho funcionam |
| **SQL** → Executar script | `Alt+X` | ✅ | Item e atalho; para no primeiro erro |
| **SQL** → Formatar | `Ctrl+Shift+F` | ✅ | Estilo "rio" do psql; preserva strings e comentários |
| **SQL** → Explicar plano | `Ctrl+Shift+E` | ✅ | Árvore com custo, `Seq Scan` em vermelho (ADR 0013) |
| `EXPLAIN ANALYZE` | — | ✅ | Caixa separada, **com aviso**; sempre em transação com rollback |
| **Ajuda** → Demo do ImGui | — | ✅ | Ferramenta de desenvolvimento |
| **Ajuda** → Sobre o C-Otter | — | ✅ | |
| Contador de FPS / ms | — | ✅ | Canto direito da barra |

## 3. Assistente de Conexão

Reescrito em 2026-09-21 seguindo o assistente do DBeaver: catálogo de drivers +
árvore de categorias (era catálogo + 8 abas na primeira versão do mesmo dia).

### Etapa 1 — catálogo de drivers

| Elemento | Estado | Observação |
|----------|--------|------------|
| Lista de 18 drivers | ✅ | Com categoria e estado |
| Coluna de categorias | ✅ | Todos, Popular, SQL, NoSQL, Analítico, Arquivos, Embarcado, Séries temporais |
| Filtro por nome | ✅ | Sem diferenciar maiúsculas |
| Drivers indisponíveis esmaecidos | ✅ | Com o motivo: "protocolo em desenvolvimento", "fase 3", "fora do escopo" |
| Duplo clique avança | ✅ | |
| Botão Avançar / Cancelar | ✅ | Avançar desabilitado sem driver disponível |
| **Ícones dos SGBDs** | ✅ | No catálogo e no Raft. Os 3 drivers com protocolo têm desenho próprio; os 15 indisponíveis usam a torre genérica, esmaecida — reusar o ícone de outro banco seria a dívida da diretriz 5 |

### Etapa 2 — configuração (árvore de categorias)

Eram 8 abas horizontais até 2026-09-21. Passou a ser a **árvore de categorias à
esquerda** do DBeaver, extraída de `EditConnectionWizard.addPages()` por
`tools/map_conn_dialog.py` — mapa em [`DIALOG-PARITY.md`](DIALOG-PARITY.md).
Rótulos oficiais, vindos do `bundle.properties`.

| Página | Elemento | Estado |
|--------|----------|--------|
| **Configurações de conexão** | Host, porta, banco | ✅ |
| | Método de autenticação (5 opções) | ✅ |
| | Usuário, senha, salvar senha | ✅ |
| | Campos de credencial desabilitados quando o método não usa | ✅ |
| | Abas SSH / SSL / Proxy **dentro desta página**, como no DBeaver | ✅ |
| → **Inicialização** | Schema padrão, consultas de bootstrap | ✅ (UI) |
| | Timeout, keep-alive, fechar ociosas | ✅ |
| → **Transações** | Auto-commit, somente leitura | ✅ |
| | **Nível de isolamento** | ✅ Os quatro do padrão SQL, aplicados ao conectar. Verificado: SHOW transaction_isolation devolveu serializable |
| → **Parâmetros internos** | Tabela de propriedades editável | ✅ |
| | Adicionar / remover propriedade | ✅ |
| | **Propriedades aplicadas na conexão** | ✅ PostgreSQL: parâmetros de runtime da StartupMessage. MySQL: SET @@nome. Verificado contra o servidor com pg_stat_activity |
| **Geral** | Nome, descrição, pasta | ✅ |
| | Tipo: Desenvolvimento / Teste / Produção | ✅ |
| | Cor por tipo e resumo do comportamento | ✅ |
| | Produção desliga auto-commit automaticamente | ✅ |
| **Metadados** | Mostrar todos os bancos / templates / sem acesso | ✅ |
| | Ler estatísticas de tamanho | ✅ |
| | Ler todos os tipos / colunas das chaves | ✅ |
| | Prepared statements, role da sessão, fuso legado | ✅ |
| | Página só existe para PostgreSQL; outros drivers avisam | ✅ |
| **Erros e tempos limite** | Tempo limite, keep-alive, fechar ociosas | ✅ moveram-se de "Inicialização", onde ninguém procuraria |
| **Transferência de dados** | Formato, cabeçalho e NULL padrão da exportação | ✅ a janela abre com eles; importação avisada na tela |
| **Editor de dados** → **Grade** | Texto do NULL, alinhar números à direita | ✅ alimenta a grade |
| **Editor de dados** → **Editor binário** | Limite do despejo hexadecimal | ✅ alimenta o painel de valor |
| **Editor de dados** → **Formatos de dados** | — | ➖ valores saem como o servidor os envia; a página explica por quê e para onde ir (DateStyle, TimeZone, cast) |
| **Editor SQL** → **Formatação** | Caixa das palavras-chave, indentação, estilo rio, quebra do SELECT | ✅ alimenta o Ctrl+Shift+F |
| **Editor SQL** → **Completar código** | Sugerir ao digitar, atraso, em comentários/strings, inserir único | ✅ alimenta o popup |
| **Editor SQL** → **Editor de código** | Tab, indentar sozinho, números de linha, parênteses | ✅ alimenta o TextEditor |
| | Mostrar espaços | ❌ chega ao editor, nada é desenhado — **desabilitado na tela**, com o motivo |
| **Editor SQL** → **Processamento SQL** | Linhas por página, parar no primeiro erro | ✅ alimenta a paginação e o `Alt+X` |
| | Delimitador de comando | ➖ segue o dialeto (`;`, `$$...$$`, `DELIMITER`); uma caixa seria uma segunda forma de dizer o mesmo, e as duas poderiam discordar |

As abas de rede continuam existindo, com o mesmo conteúdo de antes:

| Aba (em Configurações de conexão) | Elemento | Estado |
|-----|----------|--------|
| **SSH** | Host, porta, usuário, tipo de autenticação | ✅ (UI) |
| | Senha / chave privada / agente | ✅ (UI) |
| | Túnel efetivamente estabelecido | ⬜ **avisado na tela** |
| **SSL** | Modo (disable→verify-full), certificados | ✅ (UI) |
| | TLS negociado | ✅ Windows; ⬜ Linux — **avisado na tela** |
| **Proxy** | Host, porta, credenciais SOCKS | ✅ (UI) |
| | Proxy usado | ⬜ **avisado na tela** |

### Rodapé

Na ordem do DBeaver desde 2026-09-21: `Test Connection ...` à esquerda,
`OK` e `Close` à direita.

| Elemento | Estado |
|----------|--------|
| `< Voltar` (só em nova conexão) | ✅ |
| `Testar conexão ...` **à esquerda** | ✅ |
| `OK` / `Concluir` **à direita** | ✅ fecha o diálogo ao concluir |
| `Testar conexão` não cria conexão permanente | ✅ reutiliza a ativa |
| `Fechar` **à direita** | ✅ |
| Indicador pulsante durante a conexão | ✅ |
| Erro detalhado / sucesso com contagens | ✅ |
| Faixa colorida do tipo no topo | ✅ |
| **Persistir a conexão em disco** | ✅ formato do DBeaver (ADR 0012) |
| Senha salva | ✅ AES-128-CBC, chave do DBeaver — **proteção fraca, avisada na tela** |
| Senha no cofre do SO (DPAPI) | ⬜ incompatível com o DBeaver por definição |
| **Importar do DBeaver** | ✅ Arquivo → Importar; lê o workspace real, nunca escreve nele |

## 4. Árvore de conexões (antes "Painel Raft")

**Uma árvore só desde 2026-09-30 (ADR 0018).** O painel Raft deixou de
existir: a conexão é a raiz da árvore do Database Navigator, e o que ela
contém fica dentro dela — como no DBeaver. Havia dois painéis, e só a conexão
ativa tinha árvore.

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Conexão como nó raiz** | ✅ | Ícone do SGBD na cor do estado, nome, `host:porta` esmaecido |
| Conexões abertas e salvas na mesma lista | ✅ | Na ordem dos perfis: conectar não faz a linha pular |
| Expandir (seta ou duplo clique) conecta | ✅ | Clique simples só seleciona |
| Estado na linha | ✅ | "connecting..." e a mensagem de erro aparecem dentro do nó |
| Nome efetivo da conexão | ✅ | Nome do usuário ou `banco@host` |
| **Nome único** | ✅ | Repetido ganha `_1`, `_2`… O DBeaver usa ` (2)`; o sufixo `_N` é escolha do usuário |
| Menu: Connect, Invalidate/Reconnect, Disconnect | ✅ | Desconectar fecha as sessões dos bancos e recolhe o nó |
| Menu: New SQL script, Edit connection, Copy name, Refresh | ✅ | |
| Menu: Delete | ✅ | Pede confirmação: remove a senha gravada |
| Menu de contexto na área vazia | ✅ | Nova conexão |
| Versão, usuário, transação, somente leitura, descrição | ✅ | Em tooltip |
| **Pastas de organização** | ✅ | O campo Pasta do diálogo agrupa a lista |
| **Menu de contexto da árvore: `Create ▸ Connection / New Folder`** — na área vazia, na pasta e (só *New Folder*, que já recebe a conexão) na conexão, como no DBeaver | ✅ | Só existia "New connection..." na área vazia, e a pasta só se criava pelo menu Database (relato do usuário, 2026-10-01). Canal (`nav menu`, `nav foldermenu`, `nav connmenu`, `folder new`) numa cópia isolada do programa: capturas dos três menus e da janela; pasta e conexão lidas do `data-sources.json`. Os **submenus abertos** e o clique direito de verdade não foram exercitados |
| **Subpastas** (`Clientes/Producao`), desenhadas aninhadas | ✅ | O caminho com `/` era aceito e aparecia como UMA pasta de nome comprido. Captura; teste das regras de caminho (`folder_*` em `db/app_tools`) |
| **Pasta: Rename e Delete** — apagar não apaga as conexões: elas e as subpastas sobem um nível (texto do DBeaver) | ✅ | Canal (`folder rename`, `folder delete`) com o arquivo lido depois de cada passo; capturas das duas janelas. Recusa nome repetido e pasta para dentro dela mesma |
| **Conexão: `Rename`** no menu de contexto, como no DBeaver | ✅ | Só se trocava o nome pelo diálogo de edição (pedido do usuário, 2026-10-01). Canal (`conn rename`, `app name`, `app ok`) numa cópia isolada: nome lido do `data-sources.json`, árvore e aba da conexão aberta na captura; nome de outra conexão é recusado. O clique no item e a tecla F2 não foram exercitados (F2 não está ligada à conexão) |
| **Conexão: `Move to folder ▸`** (raiz e cada pasta) ➕ | ✅ | No DBeaver mover é arrastar; aqui é um submenu. Canal (`folder move`): arquivo lido. **Arrastar e soltar não existe** |
| Lista `folders` do `data-sources.json` gravada como o DBeaver grava (um item por caminho) | ✅ | Teste `store_writes_the_folder_list_the_way_dbeaver_does`. Não conferido abrindo o arquivo no DBeaver |
| **Ícone do SGBD por conexão** | ✅ | Elefante, golfinho, torre — desenhos próprios |
| Várias conexões, cada uma com a própria subárvore | ✅ | PostgreSQL e MySQL lado a lado, verificado na tela |
| Clicar numa subárvore torna a conexão ativa | ✅ | Barra de status e scripts novos passam a ser dela |

## 5. Painel Navigator

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Ordem seta → ícone → nome** | ✅ | Como no DBeaver. Era ícone → seta, e as setas de um mesmo nível ficavam desalinhadas entre si |
| Árvore de schemas | ✅ | Ícone próprio, um nó por schema |
| **Pasta Tabelas** | ✅ | Com contagem: `Tabelas (32)` |
| **Pasta Views** | ✅ | Separada das tabelas, como no DBeaver |
| **Pasta Views materializadas** | ✅ | Separada, com Índices e sem Triggers |
| Pasta vazia aparece com `(0)` | ✅ | Como no DBeaver, sempre no mesmo lugar; fica fora só o que o SGBD não tem |
| **Coluna de tamanho** | ✅ | `112K` encostado à direita, com barra proporcional à maior da pasta — era texto solto, cortado pela borda |
| Views em cor distinta | ✅ | Teal |
| Expandir → colunas | ✅ | **Carregamento tardio** — só consulta ao expandir |
| Tipo da coluna | ✅ | Via `format_type`: `character varying(80)` |
| Marca `PK` e `NOT NULL` | ✅ | |
| Indicador "carregando..." | ✅ | |
| Constraints | ✅ | PK, UNIQUE, CHECK, EXCLUDE, com definição no tooltip |
| Índices | ✅ | Método, tamanho, UNIQUE, **INVALID** em vermelho |
| Chaves estrangeiras | ✅ | Com `ON UPDATE` / `ON DELETE` |
| Referências | ✅ | Quem aponta para esta tabela |
| Triggers | ✅ | Timing, eventos, estado habilitado |
| Sequences | ✅ | `last_value`, incremento, `owned_by` |
| Funções e procedures | ✅ | Assinatura, retorno, linguagem; ícones distintos |
| **Corpo da view** | ✅ | `pg_get_viewdef` formatado, com Copiar / Abrir no editor |
| Ícone próprio por tipo | ✅ | 46 tipos de nó, nenhum compartilhado (teste compara a geometria) |
| **Corpo da função** | ✅ | `pg_get_functiondef`, com Copiar / Abrir no editor |
| **Tipos de dados** | ✅ | enum com valores ordenados, composto com campos, domain com CHECK |
| **Campo de filtro** | ✅ | Sem diferenciar maiúsculas; as contagens acompanham |
| **Menu de contexto** | ✅ | Ver dados, contar linhas, gerar SQL, copiar nome, atualizar |
| Ver dados | ✅ | `SELECT` das colunas, executado |
| Gerar SELECT / INSERT / UPDATE / DELETE | ✅ | `WHERE` pela PK; **aviso** quando não há PK |
| Gerar DDL | ✅ | `CREATE TABLE` com tipos, constraints e índices |
| INSERT/UPDATE/DELETE em view | ✅ | Desabilitados — exigiriam `INSTEAD OF` |
| Atualizar nó (F5) | ✅ | Descarta o cache e relê o catálogo |
| **Criar / alterar / excluir objeto** | ✅ | Coluna, tabela, índice, constraint, FK, view, sequence, trigger (ADR 0016) |
| **Duplo clique abre dados** | ✅ | Distingue de expandir o nó |
| **Arrastar tabela para o editor** | ✅ | Insere o nome qualificado no cursor; a área de transferência é preservada |
| **Atualizar (F5)** | ✅ | Reexecuta a consulta da aba, na mesma página |
| **Pasta Databases** | ✅ | Todos os bancos do servidor, com a coluna de tamanho (ADR 0018) |
| **Banco com sessão própria** | ✅ | Expandir outro banco abre a conexão dele; "ver dados" roda nela |
| **Foreign Tables** | ✅ | Pasta própria; Columns, Constraints, Dependencies |
| **Indexes do schema** | ✅ | Todos os índices numa lista, com tabela e método |
| **Aggregate functions** | ✅ | Com a definição `CREATE AGGREGATE` |
| **Dependencies** | ✅ | Em tabela, view, materialized view, foreign table e função |
| **Child tables** | ✅ | Só quando a tabela tem herdeiras |
| **Rules e Policies** | ✅ | Definição da regra; comando, roles e `USING` da política |
| **Function parameters** | ✅ | Nome, tipo, IN/OUT |
| **Event Triggers, Extensions** | ✅ | Por banco |
| **Storage → Tablespaces** | ✅ | Com a localização |
| **System Info do banco** | ✅ | Foreign data wrappers, foreign servers → user mappings, settings |
| **Roles** | ✅ | Usuário × grupo por ícone; Members e Roles de cada uma |
| **Administer** | ✅ | Session Manager e Lock Manager abrem a consulta numa aba |
| **Jobs (pgAgent)** | 🟡 | Consulta escrita, **não verificada**: o servidor de teste não tem pgAgent |
| **System Info do servidor** | ✅ | Access methods → operator classes/families, encodings, collations, languages, available extensions |

## 6. Editor SQL

### Edição de texto

| Elemento | Atalho | Estado |
|----------|--------|--------|
| Realce de sintaxe SQL | — | ✅ |
| Numeração de linhas | — | ✅ |
| Destaque da linha atual | — | ✅ |
| Bracket matching colorido | — | ✅ |
| Múltiplos cursores | `Alt+clique` | ✅ |
| Selecionar próxima ocorrência | `Ctrl+D` | ✅ |
| Desfazer / refazer | `Ctrl+Z` / `Ctrl+Y` | ✅ |
| Localizar e substituir | `Ctrl+F` | ✅ |
| Comentar linha | `Ctrl+/` | ✅ |
| Zoom | `Ctrl+roda` | ✅ |
| Minimap | — | ✅ |
| **Word wrap** | — | ✅ Opção por conexão (Editor de código); desligado por padrão — com ele, o número da linha deixa de corresponder ao que o servidor reporta |
| **Code folding** | — | ✅ Opção por conexão; liga os parênteses correspondentes sozinha, que é o que ela usa para achar o bloco |
| Posição do cursor (Ln, Col) | — | ✅ |
| Contagem de linhas | — | ✅ |
| Indicador de modificado (`●`) | — | ✅ Compara `GetUndoIndex()` com o ponto salvo |

### Execução

| Elemento | Atalho | Estado | Observação |
|----------|--------|--------|------------|
| Botão Executar | `Ctrl+Enter` | ✅ | |
| Executar só a seleção | `Ctrl+Enter` | ✅ | |
| Indicador de atividade | — | ✅ | |
| Botão desabilitado sem conexão | — | ✅ | |
| Executar script inteiro | `Alt+X` | ✅ | Para no primeiro erro, dizendo **qual** comando falhou |
| Progresso do script | — | ✅ | `executando comando 12 de 40` na barra de status |
| Resultado do último SELECT | — | ✅ | Não do último comando: um script que termina em `COMMIT` deixaria a grade vazia |
| **Cancelar query** | — | ✅ | Botão na barra, ligado a `Session::cancel_query()`. Pela sessão do documento, não a ativa |

### Comandos, atalhos e botões (2026-09-30)

O mapa completo — 66 comandos, com as teclas dos dois perfis e o que difere do
DBeaver em cada um — está em [`EDITOR-COMMANDS.md`](EDITOR-COMMANDS.md), gerado da
tabela que o próprio programa usa (`src/ui/commands.cpp`, ADR 0019). Aqui ficam os
elementos de tela, com o que foi **conferido na aplicação rodando**.

| Elemento | Atalho (perfil DBeaver) | Estado | Observação |
|----------|------------------------|--------|------------|
| **Barra lateral do editor** | — | ✅ | Os botões do `sqlEditor.side.top` e `side.bottom`: executar, nova aba, script, plano, IA, terminal; embaixo saída, log, variáveis, estrutura |
| Botão IA na barra lateral | — | ➖ | Desabilitado, com a razão na dica (ADR 0004) |
| **Menu "SQL Editor"** | — | ✅ | Na ordem do `SQLEditorMenu` do DBeaver |
| **Menu de contexto do editor** | botão direito | ✅ | Execute / Format / File / Layout / Panels. Nascia na fonte de código, com o atalho colado no rótulo — corrigido |
| Menu de contexto da régua | botão direito nos números | 🟡 | Dobras e "Ir para a linha"; **não conferido na tela** |
| Executar a instrução sob o cursor | `Ctrl+Enter` | ✅ | Separada por `;` ou por linha em branco (modo "smart" do DBeaver) |
| **Executar em nova aba de resultado** | `Ctrl+\` | ✅ | |
| **Abas de resultado** | — | ✅ | Fechar, fixar; o resultado volta à aba que executou |
| Executar script | `Alt+X` | 🟡 | Mostra o resultado da última consulta |
| Executar consultas em abas separadas | `Ctrl+Alt+Shift+X` | 🟡 | Uma aba por consulta, em sequência; o DBeaver abre uma conexão por consulta |
| **Variáveis** `@set` / `${nome}` | — | ✅ | Painel "Variables". `@set n = 7` sem `;` engolia a instrução de baixo — corrigido, com teste |
| `@echo` e saída do servidor | `Ctrl+Shift+O` | 🟡 | NOTICE do PostgreSQL; os warnings do MySQL não são coletados |
| **Terminal SQL** | — | ✅ | Painel com prompt; resultado em texto, até 200 linhas |
| **Estrutura (outline)** | `Ctrl+O` | ✅ | Uma linha por instrução, com o alvo |
| **Ir para a linha** | `Ctrl+L` | ✅ | |
| **Transformar em lista delimitada** | — | ✅ | Diálogo com prévia ao vivo |
| Alternar painel de resultados | `Ctrl+T` | ✅ | Ao voltar, a aba "Result" fica selecionada (voltava no log) |
| Maximizar painel de resultados | `Ctrl+Shift+T` | ✅ | |
| Script novo | `Ctrl+]` | ✅ | O foco vai para o script novo (a aba aparecia e as teclas se perdiam) |
| **Perfis de atalho** | Help → Keymap | ✅ | DBeaver (padrão) e C-Otter; gravado em `settings.json` |
| Janela de atalhos | Help → Keymap → Show shortcuts... | ✅ | Os dois perfis lado a lado |
| Aspas e colchetes de fechamento | — | ✅ | Digitar `'` sobre o `'` já inserido passa por cima; antes `'texto'` virava `'texto''` |
| Demais comandos da tabela | ver `EDITOR-COMMANDS.md` | 🟡 | Maiúsculas/minúsculas, aparar espaços, colchete correspondente, próxima/anterior consulta, contar linhas, todas as linhas, avaliar expressão, carregar plano, exportar, pesquisar na web, copiar consulta, arquivo de script, ir para a declaração, alternar disposição, modelos: **implementados, com teste das regras, e não conferidos um a um na tela** |

### Dicas (hints)

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Cartão de dica** | ✅ | `src/ui/hint.cpp`: título, atalho como tecla, linhas "rótulo … valor", texto e SQL. Esmaecer curto ao aparecer |
| Dica dos botões | ✅ | Rótulo + tecla; no botão desabilitado, o motivo |
| Dica dos objetos da árvore | ✅ | Banco, coluna, constraint, índice, FK, partição, trigger, sequence, rotina, tipo, evento |
| Dica do nó de plano | ✅ | Condições e custo em linhas |

### Abas de editor

**Dois níveis desde 2026-09-21**, como o `SQLEditor` do DBeaver: uma janela
ancorável **por conexão**, com o nome dela no título, e dentro as abas de
script. Antes havia uma janela única "SQL" com todos os scripts misturados —
um de MySQL ao lado de um de PostgreSQL, sem nada distinguindo.

| Elemento | Atalho | Estado | Observação |
|----------|--------|--------|------------|
| **Uma janela por conexão** | — | ✅ | Título = nome da conexão, na cor do tipo |
| **Conexão por aba** | — | ✅ | O script executa contra a base dele, não contra a ativa |
| Janela em foco define a conexão ativa | — | ✅ | Navigator e barra de status acompanham a aba |
| Script novo herda a conexão da janela | — | ✅ | O `+` cria já amarrado |
| Conectar abre a janela com `Script` | — | ✅ | Sem exigir clique no `+` antes. Os nomes seguem a série do DBeaver: `Script`, `Script-1`, `Script-2`… |
| **Fechar a aba da conexão** (`×`) | — | ✅ | Fecha os scripts e editores de objeto dela; a sessão continua conectada, como no DBeaver. Os scripts são gravados e ficam em `.script` (voltam por *Show scripts*); só célula editada ou editor de objeto com alteração pendente pedem confirmação (*Close and discard*) |
| Abre **sem aba** quando não há script | — | ✅ | O script de boas-vindas saiu; a conexão só tem janela enquanto tem documento. Fechar o último script fecha a janela |
| **Scripts gravados sozinhos** em `.script/` (ADR 0025) | — | ✅ | 0,4 s depois de a digitação parar, e na hora ao sair ou fechar a aba. Aba vazia não vira arquivo; script esvaziado é apagado |
| **Abas reabertas ao iniciar** | — | ✅ | Cada script na conexão (e no banco) que tinha, com a que estava na frente de volta à frente. **Não conecta sozinho**: a aba diz *not connected* e tem o botão *Connect* (diverge do DBeaver, que conecta ao ativar o editor — ADR 0025) |
| *Show scripts* lista também os fechados | — | ✅ | Reabre o script na conexão que ele tinha |
| Conexões lado a lado | — | ✅ | Arrastando a janela, pelo docking do ImGui |
| **Múltiplas abas de script** | — | ✅ | Cada uma com editor, resultado e estado próprios |
| Nova aba | `Ctrl+]` (DBeaver) / `Ctrl+T` (C-Otter) | ✅ | Também pelo botão `+` e pelo menu Arquivo. No perfil DBeaver, `Ctrl+T` alterna o painel de resultados |
| Fechar aba | `Ctrl+W` | ✅ | Também pelo `×` da aba |
| **Menu de contexto da aba** | — | ✅ | Nunca abriu ate 2026-09-21: o corpo do editor virava o "ultimo item" e roubava o alvo |
| Fechar outras | — | ✅ | Menu de contexto; respeita abas fixadas |
| Fixar aba | — | ✅ | Fixadas vão para a esquerda e sobrevivem a "fechar outras" |
| Copiar SQL da aba | — | ✅ | Menu de contexto |
| Reordenar arrastando | — | ✅ | |
| Lista suspensa de abas | — | ✅ | Botão `▼` quando não cabem todas |
| Indicador de modificado | — | ✅ | `*` no título e `●` na barra |
| **Resultado isolado por aba** | — | ✅ | Trocar de aba troca a grade |
| **Roteamento do resultado** | — | ✅ | Volta para a aba que executou, mesmo trocando de aba durante a query |
| **Dialeto SQL por aba** | — | ✅ | Realce, formatação e paginação seguem o driver da aba |
| **Renomear aba** | — | ✅ | Menu de contexto → Renomear aba...; vazio volta ao nome padrão |
| Nome da conexão no rótulo do script | — | ➖ | Fica na janela; o DBeaver o repete em cada aba |

### Autocomplete

| Elemento | Estado | Observação |
|----------|--------|------------|
| Disparo ao digitar | ✅ | |
| Disparo manual (`Ctrl+Espaço`) | ✅ | |
| Sugere tabelas reais do banco | ✅ | |
| Sugere colunas reais | ✅ | |
| Keywords do dialeto | ✅ | |
| Fuzzy por subsequência | ✅ | `cliid` casa `cliente_id` |
| Destaque do prefixo casado | ✅ | Do próprio widget |
| Tipo alinhado em coluna | ✅ | Fonte monoespaçada |
| Marca `PK` na sugestão | ✅ | |
| Ranking contextual | ✅ | Colunas de tabelas citadas primeiro |
| Não dispara em comentário/string | ✅ | |
| **Escopo sintático** | ✅ | Após `FROM` só tabelas; após `SELECT`/`WHERE` colunas e keywords |
| **Filtro por `alias.`** | ✅ | Após `u.`, só colunas da tabela do alias `u` |
| **Inferência de JOIN por FK** | ✅ | Depois de `ON`, sugere `i.SYSxMenuGroupID = g.ID` inteiro, com os aliases da query — verificado na tela |
| Ícone por tipo de objeto | ➖ | `suggestions` é `vector<string>` (`TextEditor.h:752`): o popup só aceita texto, e mudar isso é alterar o widget de terceiro. O tipo aparece como sufixo — `tabela`, `view`, `chave estrangeira` |
| Painel de detalhe lateral | ➖ | Mesma limitação do item acima: o popup é uma lista de strings, sem área para detalhe |
| Sugestão por IA | ➖ | Fora do escopo da v1 (ADR 0004). Exigiria chamada de rede a um serviço externo — e mandar o schema do usuário para fora é decisão dele, não padrão |

## 7. Painel Resultado (grade)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Exibição tabular | ✅ | |
| Cabeçalho com nome da coluna | ✅ | |
| Cabeçalho fixo ao rolar | ✅ | |
| Primeira coluna fixa | ✅ | |
| Virtualização | ✅ | `ImGuiListClipper` — só linhas visíveis |
| Redimensionar coluna | ✅ | |
| Reordenar coluna (arrastar) | ✅ | |
| Números à direita | ✅ | Por `DataKind` |
| `[null]` distinto de vazio | ✅ | |
| Contagem de linhas e colunas | ✅ | |
| **Bytes em memória** | ✅ | Não existe no DBeaver |
| Rolagem horizontal | ✅ | |
| Mensagem de comando sem resultado | ✅ | Com linhas afetadas |
| **Ordenar pelo cabeçalho** | ✅ | No **servidor** — com paginação, ordenar no cliente daria a ordem errada |
| Terceiro clique remove a ordenação | ✅ | `SortTristate`; volta à ordem do servidor |
| `ORDER BY` do usuário respeitado | ✅ | Consulta com ordem própria não é sobreposta |
| **Selecionar célula** | ✅ | Clique simples; setas, Home/End, PageUp/PageDown navegam |
| **Copiar célula** | ✅ | Menu de contexto → `Copiar valor` |
| **Copiar a linha inteira** | ✅ | `Ctrl+Alt+R`, separada por tab — cola numa planilha |
| **Copiar a coluna inteira** | ✅ | `Ctrl+Alt+C`, uma linha por valor |
| **Copiar os nomes das colunas** | ✅ | `Alt+Shift+C` — vira o cabeçalho na planilha |
| **Navegar por linha** | ✅ | `Ctrl+Alt+←/→` anda mantendo a coluna; com `Shift`, vai ao extremo |
| **Editar célula** | ✅ | Duplo clique; edição em buffer (ADR 0014) |
| Gravação explícita | ✅ | `Salvar alterações` / `Descartar`; nada vai ao banco antes |
| Célula alterada destacada | ✅ | Fundo âmbar, valor original no tooltip |
| Definir `NULL` | ✅ | Menu de contexto — digitar nada é string vazia, não `NULL` |
| Recusa com motivo | ✅ | `JOIN`, sem PK, chave fora do `SELECT`, view |
| `UPDATE` por linha, em transação | ✅ | Ou tudo, ou nada |
| **Inserir linha** | ✅ | Linha verde no fim; coluna em branco usa o `DEFAULT` |
| **Copiar da linha de cima/de baixo** | ✅ | `Ctrl+D`/`Ctrl+Alt+D`, como no DBeaver, e no menu. Preenche a célula, não insere linha |
| **Excluir linha** | ✅ | Marcada em vermelho até gravar |
| Ordem `INSERT` → `UPDATE` → `DELETE` | ✅ | Evita violar FK ao inserir o que a exclusão removeria |

| **Filtro por coluna** | ✅ | Botão direito no cabeçalho; expressão `WHERE` livre |
| Coluna filtrada marcada | ✅ | Prefixo `*` em âmbar no cabeçalho |
| Menu de contexto do cabeçalho | ✅ | Filtrar, ordenar, copiar nome |
| **Agrupamento e subtotais** | ✅ | Menu do cabeçalho; múltiplos níveis (ADR 0005) |
| **Linha de totais** | ✅ | Destacada, no fim do painel de grupos |
| 7 agregações | ✅ | contagem, não nula, distintos, soma, média, mín., máx. |
| Agregação inaplicável desabilitada | ✅ | `SUM` numa coluna de texto aparece cinza |
| **Aviso de resultado parcial** | ✅ | "apenas sobre esta página (N linhas)" + "Calcular no servidor" |

| **Pivot** | ✅ | Local e no servidor; `SUM(CASE WHEN...)`, não `PIVOT` (ADR 0005) |
| **Formatação condicional** | ✅ | 12 operadores e mapa de calor (ADR 0005) |
| **Barra na célula (sparkline)** | ✅ | 3 ancoragens; **o DBeaver não tem** (ADR 0005) |
| **Painel de valor** (JSON, hexadecimal, booleano) | ✅ | JSON indentado, BLOB em hex, `t`/`f` e `1`/`0` |
| Coluna calculada | ⬜ | Exige um avaliador de expressões sobre o resultado: parser, tipos e propagação de NULL. O agregador atual só aplica funções fixas a UMA coluna |
| **Exportar** | ✅ | CSV, JSON, Markdown, `INSERT`, HTML, XML e TXT, com prévia |
| Proteção contra CSV injection | ✅ | **Ligada por padrão** — valor iniciado por `=`, `+`, `-` ou `@` vira fórmula na planilha |
| Copiar resultado para a área de transferência | ✅ | O resultado inteiro, não só a prévia |
| Escolha entre a consulta inteira e as linhas carregadas | ✅ | Era só um aviso de que exportava a página; ver §13 |
| **Paginação** | ✅ | 200 linhas por página, com primeira/anterior/próxima (ADR 0011) |
| Intervalo de linhas exibido | ✅ | `linhas 401-600 +` — o `+` indica que há mais |
| SQL paginado auditável | ✅ | O inspetor mostra o `LIMIT`/`OFFSET` efetivamente executado |
| `LIMIT` do usuário respeitado | ✅ | Consulta com `LIMIT` próprio não é reescrita |
| **Total exato de linhas** | ✅ | O `+` é botão que conta sob demanda (`resultset.count`). Verificado: "linhas 1-200 **de 1005**" contra o ERP_TID |
| **Visão de registro único** | ✅ | `Tab`; nome/valor/tipo, com a chave primária marcada |
| **Seleção de célula** | ✅ | Clique simples; setas, Home/End, PageUp/PageDown navegam |
| **Teclas de edição na grade** | ✅ | `Enter`, `Alt+Insert`, `Alt+Delete`, `Esc` — 7 de 47 do DBeaver (`docs/GRID-KEYS.md`) |
| Limite de 64 colunas | ⚠️ | Restrição do ImGui — **avisado na tela**, com o total real |

## 8. Painel Queries (inspetor)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Lista de queries da sessão | ✅ | |
| Tempo de execução | ✅ | Precisão de microssegundo |
| Contagem de linhas | ✅ | |
| Estado ok / erro | ✅ | |
| SQL em uma linha | ✅ | |
| Tooltip com SQL completo | ✅ | |
| Mais recentes primeiro | ✅ | |
| **Queries internas de catálogo** | ✅ | **Melhor que o DBeaver**, que as esconde |
| Limite de 2000 entradas | ✅ | Descarta as mais antigas |
| **Copiar SQL** | ✅ | Menu de contexto, como o `QueryLogViewer` do DBeaver |
| **Filtrar por estado** | ✅ | "só as que falharam" e "queries de catálogo"; a contagem mostra `N de M` |
| **Abrir no editor SQL** | ✅ | Aba nova **sem executar** — reexecutar um UPDATE ao inspecionar seria destrutivo |
| **Copiar erro** | ✅ | Desabilitado quando a query não falhou |
| **Limpar log** | ✅ | Sem confirmação: é só histórico de diagnóstico |
| Mensagem de erro no tooltip | ✅ | Junto do SQL; antes era preciso achá-la na barra de status |
| Persistir entre sessões | ⬜ | Exige decidir rotação e **privacidade**: uma query carrega dados (`WHERE cpf = '...'`), e gravá-la em disco sem o usuário pedir é diferente de mantê-la em memória. O DBeaver tem um Query Manager com essa opção desligada por padrão |

## 9. Barra de status

| Elemento | Estado |
|----------|--------|
| Indicador colorido de conexão | ✅ |
| Nome do banco | ✅ |
| Versão do servidor | ✅ |
| Mensagem de estado / erro | ✅ |
| **Estado da transação** | ✅ `Auto` / `Nenhuma` / contagem, como o `TransactionMonitorToolbar` do DBeaver |
| Contagem de alterações pendentes | ✅ o número, em amarelo — é o que decide se dá para fechar |
| Aviso de transação abortada | ✅ em vermelho, com tooltip dizendo que só rollback é aceito |
| **Schema corrente** | ✅ omitido quando o driver não o conhece, em vez de mostrar valor falso |

## 10. Transações

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Alternar autocommit** | ✅ | Botão na barra; o estado aparece ao lado |
| **Commit / Rollback** | ✅ | `Ctrl+Shift+C` / `Ctrl+Shift+R`, com ícone próprio |
| **Indicador de transação aberta** | ✅ | Na barra, com glow quando há transação |
| **Gravação de edições em transação** | ✅ | Ou tudo, ou nada — mesmo em autocommit |
| **Aviso ao fechar com alterações pendentes** | ✅ | Menu Sair, `Alt+F4` e o `X` da janela. **Os scripts não entram**: são gravados sozinhos (ADR 0025). Só o que não dá para gravar sem pedir: células editadas e editores de objeto com alteração pendente |
| Savepoints na UI | ➖ | **O DBeaver também não os expõe** — busca por `savepoint` nos `plugin.xml` não acha comando nenhum; ele os usa internamente e os filtra do log (`SQLLogFilter.java:43`). O driver os implementa (`Holt::savepoint`); criar uma tela aqui seria divergir sem paridade a ganhar |

## 11. Editor de objeto (2026-10-01)

Mapa contra o DBeaver em [`OBJECT-EDITOR.md`](OBJECT-EDITOR.md); decisão no ADR 0022.
Conferido na aplicação pelo canal de comandos, com o efeito lido no banco.

| Elemento | Estado | Observação |
|----------|--------|------------|
| Abrir por duplo clique e `F4` | ✅ | Todo nó de objeto da árvore e toda linha de seção |
| Aba por objeto, sem duplicar | ✅ | Pedir o mesmo objeto traz a aba que já existe |
| Abas `Properties` e `Data` | ✅ | `Data` só para o que tem linhas |
| Abre na última aba usada | ✅ | Por sessão do programa |
| O painel de resultado dá lugar ao editor | ✅ | O nó central do dock passou a ser o do editor (ADR 0022) |
| Formulário de propriedades (26 tipos) | ✅ | Consultas conferidas no servidor |
| Editar Name, Comment, Owner, Schema, Tablespace | ✅ | Owner, Schema e Tablespace por combo |
| Atributos do role por caixa | ✅ | 7 atributos |
| Marca de propriedade alterada | ✅ | Rótulo em âmbar; "changes not saved" no rodapé |
| `Save ...` / `Revert` / `Refresh` | ✅ | `Ctrl+S` grava; sem mudança, avisa em vez de abrir revisão vazia |
| A aba segue o objeto renomeado | ✅ | Conferido: `documento` → `documento_x` e de volta |
| Lista de seções com ícone | ✅ | |
| Seções Columns, Constraints, Foreign Keys, Indexes, References, Partitions, Triggers | ✅ | Do modelo da árvore, carregadas ao abrir |
| Seções Dependencies, Rules, Policies, Parameters, Members, Member of, User Mappings | ✅ | As mesmas listas da árvore |
| Seção Statistics | ✅ | `pg_stat_*` |
| Seção Permissions: roles, caixas, With GRANT, Grant All, Revoke All | ✅ | `GRANT`/`REVOKE` conferidos no banco |
| Seção DDL / Source com realce | ✅ | Copiar e abrir num script |
| Fonte editável (view, função, procedure) | ✅ | Duas gravações seguidas conferidas — a segunda falhava por texto velho em cache |
| Fonte somente leitura dizendo por quê | ✅ | Materialized view, trigger, regra: não há `OR REPLACE` |
| Aba Data: a grade completa | ✅ | Filtro, edição, exportação — o mesmo código do painel de resultado |
| Objeto que não existe mais | ✅ | Mensagem, em vez de formulário vazio |
| Caixas de privilégio visíveis no tema claro | ✅ | Ganharam contorno: a caixa vazia tinha a cor do fundo |
| MySQL: banco, tabela, view, rotina, trigger, evento, conta | ✅ | Propriedades, DDL/fonte editável, Permissions, Rename, Comment, Drop, Tools — `docs/MYSQL-MAP.md` seção 4 |

## 12. Diálogos de objeto, Tools e sessões (2026-10-01)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Menu do nó: View, View Data, Create New, Rename, Delete, Tools, Copy, Refresh | ✅ | Conferido por `objectmenu` |
| `F2` renomeia, `Delete` apaga | ✅ | Com o nó sob o cursor — **a tecla em si não foi conferida** (a automação não entrega teclas) |
| Menu da pasta: Create New | ✅ | **Clique direito não conferido**; o formulário, sim |
| Create database | ✅ | Gerador conferido; execução não (cria um banco de verdade) — o banco de rascunho do teste de restore foi criado por SQL |
| Create schema | ✅ | Executado e conferido |
| Install extensions | ✅ | Lista só as não instaladas |
| Create role | ✅ | Executado e conferido |
| Create tablespace | 🟡 | Gerador testado; **não executado** — exige um diretório no servidor |
| Função / procedure → esqueleto no editor | ✅ | O esqueleto compila em `sql` e `plpgsql` |
| Event trigger, trigger | ✅ | Executados e conferidos |
| Sequência, constraint, chave estrangeira | ✅ | Idem |
| Política, materialized view | ✅ | Idem |
| Rename | ✅ | |
| Delete com Cascade | ✅ | SQL e aviso à vista antes de executar |
| Vacuum com as 7 opções | ✅ | Opções conforme a versão do servidor |
| Truncate (Only, Restart identity, Cascade) | ✅ | Revisão conferida; não executado numa tabela com dados |
| Refresh Materialized View, Analyze, Reindex | ✅ | |
| Enable / Disable trigger | ✅ | |
| Change password (role) | ✅ | Senha digitada duas vezes |
| Diálogos com fundo opaco | ✅ | Eram translúcidos — o texto de trás atravessava |
| Session Manager: Cancel active query / Terminate session | ✅ | Conferido encerrando uma sessão-vítima de verdade |
| Backup (pg_dump) | ✅ | Custom e Plain, de uma tabela |
| Restore (pg_restore / psql) | ✅ | Para um banco de rascunho, conferido por consulta |
| Programa não encontrado, dito na tela | ✅ | |
| Saída do programa ao vivo, com cancelar | ✅ | |

## 13. Transferência de dados (2026-10-01)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Exportar: "All rows of the query" × "Loaded rows only" | ✅ | Abre na consulta inteira quando há paginação |
| Exportação por cursor, com progresso | ✅ | 2.000.000 de linhas, 190 MB, ~5 s |
| Cancelar a exportação | ✅ | O arquivo parcial fica, e a mensagem diz que é parcial |
| Filtro e ordenação da grade valem na exportação | ✅ | JSON de 12.345 linhas, três pedaços, lido de volta |
| Formatos HTML, XML, TXT | ✅ | |
| Export Data no menu da tabela | ✅ | |
| Import Data: arquivo e Browse | ✅ | **Browse (diálogo nativo) não conferido** |
| Delimitador adivinhado, cabeçalho, texto de NULL | ✅ | CSV de planilha com `;`, BOM e acentos |
| Prévia com o destino de cada coluna | ✅ | Por nome; `(skip)` ignora |
| Truncate target table before load | ✅ | |
| Carga em transação única | ✅ | Erro na linha 1.601 de 1.601: nenhuma linha ficou |
| Progresso por lote | ✅ | |
| Erro com a mensagem do servidor | ✅ | E o aviso de que nada foi importado |

## 14. Rede (2026-10-01)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Proxy SOCKS5 | ✅ | `spike_proxy_live` contra proxy real: PostgreSQL e MySQL, com e sem senha, TLS por dentro |
| Erros do proxy distinguíveis | ✅ | Proxy fora do ar, senha recusada, destino recusado — cada um com o seu texto |
| Túnel SSH | 🟡 | Pelo `ssh` do sistema (ADR 0021). **Túnel real não conferido** — sem servidor SSH na máquina de teste; o caminho de falha, sim |
| Aba SSH diz o que o túnel não faz | ✅ | Senha digitada e chave com frase secreta |

---

## Defeitos conhecidos

| # | Onde | Problema |
|---|------|----------|
| 1 | Conexão | Senha em disco tem a proteção fraca do DBeaver (chave pública) — **avisado na tela**, ADR 0012 |
| 2 | Raft | As conexões abertas não são restauradas ao reiniciar — só a última volta preenchida no diálogo |
| 3 | Grade | `OFFSET` alto é lento: o servidor produz e descarta as linhas puladas (custo inerente ao ADR 0011) |
| 4 | Editor | **"Mostrar espaços" não desenha nada.** `SetShowWhitespacesEnabled(true)` é chamada a cada quadro (confirmado com trace), e `TextEditor.cpp:629` tem o código do desenho — mas os pontos não aparecem, nem depois de corrigir a cor. A caixa fica **desabilitada na tela**, com o motivo, em vez de fingir que funciona |

### Corrigidos

| Onde | O que era | Como foi resolvido |
|------|-----------|--------------------|
| Tema | O tema claro deixava a interface clara e o **editor escuro** | O primeiro documento nasce antes de `set_theme()`; a paleta dele é reaplicada depois |
| Grade | `SELECT` sem `LIMIT` numa tabela grande travava a UI até o servidor enviar tudo | Paginação de 200 linhas (ADR 0011). `SELECT *` em 2 M de linhas volta em <1 ms |
| Editor | O `●` de modificado aparecia na primeira edição e nunca mais saía | Existe "salvar"; `mark_saved()` finalmente é chamado |
| Grade | Mais de 64 colunas eram truncadas **em silêncio** | Aviso na barra dizendo quantas colunas ficaram de fora |
| Navigator | Tooltips respondiam só sobre o último trecho de texto da linha | Cada linha dentro de `BeginGroup`/`EndGroup` |
| Catálogo | `load_routine_definition()` montava uma assinatura que o servidor recusava | Localiza pelo OID; coberto por `otter_tests_live` |

## Pronto no núcleo, ausente na UI

Código implementado **e testado** que ainda não tem ponto de entrada na interface.
Esta é a lista de maior retorno por esforço: o trabalho difícil já está feito.

| Capacidade | Onde está | Falta |
|------------|-----------|-------|

| Cancelamento de query | `pgwire/connection.cpp` | Botão durante a execução |
| Dialetos MySQL/MSSQL/SQLite | `sql/dialect.cpp` | Drivers correspondentes |
| Foreign keys do schema | `Session::foreign_keys()` | Inferência de JOIN (camada 4) |
| Comentário de tabela/coluna | `db/catalog.cpp` | Exibir em tooltip |
| Valor default da coluna | `db/catalog.cpp` | Exibir no Navigator |
| Mapeamento de SQLSTATE | `pgwire/connection.cpp` | Apontar o erro na posição do editor — o servidor já informa `posição: 15` |

---

---

## Resumo por área

Contado em 2026-09-30 varrendo as tabelas acima; itens ➖ (fora do escopo)
ficam de fora do total.

| Área | ✅ | 🟡 | ⬜ | ❌ | Total |
|------|-----|-----|-----|-----|-------|
| Janela e estrutura | 23 | 0 | 0 | 0 | 23 |
| Barra de menus | 18 | 0 | 0 | 0 | 18 |
| Assistente de conexão | 52 | 0 | 3 | 1 | 56 |
| Árvore de conexões | 15 | 0 | 0 | 0 | 15 |
| Navigator | 49 | 1 | 0 | 0 | 50 |
| Editor SQL | 79 | 5 | 0 | 0 | 84 |
| Grade | 56 | 0 | 1 | 0 | 57 |
| Inspetor de queries | 15 | 0 | 1 | 0 | 16 |
| Barra de status | 8 | 0 | 0 | 0 | 8 |
| Transações | 5 | 0 | 0 | 0 | 5 |
| Editor de objeto | 22 | 1 | 0 | 0 | 23 |
| Diálogos de objeto, Tools e sessões | 24 | 1 | 0 | 0 | 25 |
| Transferência de dados | 13 | 0 | 0 | 0 | 13 |
| Rede | 3 | 1 | 0 | 0 | 4 |
| **Total** | **382** | **9** | **5** | **1** | **397** |

**382 de 397 elementos existentes funcionam.** Os 26 novos do Editor SQL (21 ✅, 5 🟡)
são os das tabelas "Comandos, atalhos e botões" e "Dicas".

> ### ⚠️ Este número NÃO é indicador de progresso
>
> O denominador aqui são os elementos que o C-Otter **tem**, não os que **precisa ter**.
> Ele mede "o que existe está funcionando?" — não "quanto falta?".
>
> O mapa completo do DBeaver ([`DBEAVER-MAP.md`](DBEAVER-MAP.md)) tem **281 comandos**,
> **147 atalhos**, **151 diálogos**, **112 assistentes**, **109 páginas de preferências**,
> **309 managers de DDL** e **81 value handlers**. Contra esse denominador:
>
> | | DBeaver | C-Otter | |
> |---|---|---|---|
> | Comandos | 281 | 131 | **46,6%** |
> | Atalhos | 147 | 90 | **61,2%** |
>
> Recontado em 2026-10-01 por `tools/map_dbeaver.py`. A grade (70 comandos) e os
> comandos de objeto e de transferência de dados entraram na conta.
>
> O diálogo de conexão conta 51 elementos aqui, com 41 prontos. O do DBeaver tem
> **34 páginas** alcançáveis (`tools/map_conn_dialog.py`), das quais 6 têm
> equivalente com conteúdo — e cada página tem seus próprios campos, que esta
> conta nem enumera. Medir a própria interface contra si mesma produz um número
> que sobe enquanto o produto não se aproxima do alvo.
>
> **Para progresso, use [`UI-SCOPE.md`](UI-SCOPE.md) e [`PARITY.md`](PARITY.md).**
> Este arquivo responde apenas: *"este botão funciona?"*

## Registro visual

| Captura | O que mostra |
|---------|--------------|
| `screenshot.png` | Aplicação conectada, com resultado na grade |
| `query-log.png` | Inspetor de queries, incluindo as de catálogo |
| `completion-scope.png` | Completion após `FROM` — só tabelas |

## Como verificar

O build precisa do ambiente do MSVC carregado. `tools\build.ps1` faz isso:
chamar `cmake --build` de um shell qualquer falha com
`Cannot open include file: 'cstdint'`, porque `INCLUDE` e `LIB` só existem
depois do `vcvars64.bat`.

```powershell
tools\build.ps1                    # alvo padrão
tools\build.ps1 -Target otter_tests
```

```powershell
$env:PGPASSWORD="sua-senha"
$env:PGDATABASE="seu-banco"
build\win-release\bin\c-otter.exe
```

### Variáveis de inspeção

Existem porque automatizar cliques no ImGui é pouco confiável — a entrada é
processada por quadro e o `SendKeys` perde teclas. Uma captura só vale se o
estado que ela mostra foi alcançado de forma determinística.

| Variável | Efeito |
|----------|--------|
| `OTTER_AUTOCONNECT=1` | Conecta na inicialização com o perfil de `PGHOST`/`PGUSER`/… |
| `OTTER_EXPAND_TREE=1` | Abre as pastas da árvore e a primeira tabela |
| `OTTER_SHOW_ICONS=1` | Abre a galeria de ícones direto |
| `OTTER_THEME=light` | Tema inicial (`dark`, `light`, `amber`) |
| `OTTER_SHOW_EXPORT=1` | Abre a exportação assim que o primeiro resultado chega |
| `OTTER_NO_FOCUS=1` | A janela nasce sem tomar o foco de quem está usando a máquina |
| `OTTER_SCRIPT_DIR=<pasta>` | Troca a pasta `.script`: o programa de conferência não abre nem regrava os scripts de quem usa a máquina |
| `OTTER_COMMAND_FILE=<arquivo>` | Canal de comandos: cada linha do arquivo é executada pelo mesmo caminho do menu |

```powershell
# Árvore inteira, conectada, pronta para captura:
$env:OTTER_AUTOCONNECT="1"; $env:OTTER_EXPAND_TREE="1"
build\win-release\bin\c-otter.exe
tools\screenshot.ps1 -Out arvore.png
```

O canal de comandos (`tools\cmd.ps1 "<linha>" ...`) aceita o rótulo de um comando
da tabela (`"Select All"`) e:

| Linha | Efeito |
|-------|--------|
| `sql <texto>` · `filter <condição>` · `select L C` · `extend L C` · `value <texto>` | A grade e o editor |
| `object <tipo>\|<schema>\|<nome>\|<pai>\|<assinatura>` · `objectdata ...` · `objectmenu ...` | Abre o editor do objeto (ou mostra o menu do nó) |
| `page properties\|data` · `section <rótulo>` · `property <rótulo>=<valor>` · `source <texto>` · `save object` | Dentro do editor |
| `grantee <role>` · `grant <privilégio>` · `revoke <privilégio>` | Seção Permissions |
| `form <ação> <objeto>` · `field <campo> <valor>` · `form ok` | Diálogos de criar, renomear, apagar e das ferramentas |
| `ddl dump` · `ddl execute` · `ddl cancel` | A janela "Review SQL"; `dump` grava o script em `<arquivo>.ddl` |
| `export ...` · `import ...` · `tool ...` · `session cancel\|terminate` | Transferência de dados, backup/restore e sessões |

| `nav select <tipo>\|<schema>\|<nome>\|<pai>\|<assinatura>` · `nav clear` | O nó da árvore sobre o qual os comandos de contexto agem |
| `app search <texto>` · `app pick [n]` · `app ok` · `app cancel` | As janelas dos comandos de aplicação (busca, URL, filtro, confirmações) |
| `app url ...` · `app folder ...` · `app filter <incluir>\|<excluir>` · `app chart <id>` | Campos dessas janelas |
| `app dump` | Grava em `<arquivo>.app` o nó selecionado, o schema, o estado, favoritos, pastas, o dashboard e a área de transferência |

O canal prova que o comando funciona e o que ele faz no banco. **Não** prova que a
tecla ou o clique chegam ao comando.

## 15. Comandos de aplicação, banco e navegador (2026-10-01)

Os `core.*`, `ui.navigator.*`, `ui.editors.connection.*` e `ui.app.standalone.*` do
DBeaver: menus **Database**, **Navigate** e **Window**, o menu do nó da árvore e a
paleta. Tabela em `src/ui/commands.cpp`, ações em `src/ui/app_commands.cpp`, janelas em
`src/ui/app_windows.cpp`, regras puras (com teste) em `src/db/app_tools.cpp`.

| Elemento | Onde | Estado | Conferido |
|---|---|---|---|
| Commit · Rollback · Auto-commit (teclas por perfil) | Database, barra | ✅ | suíte |
| Pending transactions | Database | ✅ | — |
| Transaction log | Database | 🟡 do log desta sessão | — |
| Connect · Invalidate/Reconnect · Disconnect · Disconnect All · Disconnect Other | Database | ✅ | — |
| Read-only (alterna no servidor e grava no perfil) | Database → Transaction Mode | ✅ | canal: o servidor recusou `CREATE TABLE` |
| New connection from JDBC URL | Database / File | ✅ | captura; teste |
| New Folder (pasta vazia de conexões) | Database | ✅ | — |
| Select active connection (`Ctrl+9`) · Select active schema (`Ctrl+0`) | Database | ✅ | canal: `schema: otter_test` |
| Select active database — ícone `tree/database.svg` na aba da conexão (o banco do seletor de catálogo do DBeaver, no lugar pedido pelo usuário); PostgreSQL e SQL Server levam o script para a sessão do banco, MySQL faz `USE` | aba da conexão | ✅ | canal `tab database`: `current_database() = DOC`, `db_name() = tempdb`; MySQL só no teste do `USE` (sem senha local) |
| Set as default (schema) | árvore | ✅ | — |
| Open database object (`Ctrl+Shift+D`) | Navigate | ✅ relações sempre; rotinas e sequências das pastas já abertas | — |
| Command palette (`Ctrl+3`) | Navigate | ✅ (além do DBeaver) | captura |
| Edit / Create / Delete / Rename object (`F4`, `Alt+Insert`, `Delete`, `F2`) sobre o nó selecionado | árvore | ✅ | canal |
| View data · Read data in SQL console · Export / Import Data | árvore | ✅ | — |
| Context tools (``Alt+` ``) | árvore | ✅ | — |
| Filtro de objetos: Configure, Toggle, Clear, Show only / Hide selected | Window → Database Navigator | ✅ | canal |
| Show all connections · Focus filter · Link with editor | Window / Navigate | ✅ | — |
| Move up / down / top / bottom | Window → Database Navigator | 🟡 conexões salvas | — |
| Add bookmark (`Ctrl+Alt+Shift+D`) · Navigate to | Navigate | ✅ | canal: `bookmarks: cliente` |
| Next / Previous tab (`Alt+Shift+↓/↑`) · Open source tab | editor de objeto | ✅ | — |
| New index / constraint from selection (coluna) | árvore | ✅ | — |
| Execute stored procedure | árvore | ✅ | canal |
| Advanced copy (`Ctrl+Shift+C`) · with last settings · Advanced paste | grade | ✅ | canal: área de transferência |
| Generate UUID (`Ctrl+Alt+Shift+U`) | editor e grade | ✅ | teste |
| Load / Save resource (valor da célula ↔ arquivo) | grade | ✅ | diálogo nativo não conferido |
| Open results in Excel | grade | 🟡 CSV aberto pela planilha | — |
| Associate with data source · Show scripts · Show in explorer | File / Navigate | ✅ | — |
| Change user password | Database | 🟡 PostgreSQL | — |
| Driver manager | Database | 🟡 lista os drivers embutidos | captura |
| Preferences (`Ctrl+,` no perfil C-Otter) | Window | ✅ | captura |
| Show/Hide view · Log filters · Clear log · Stop processes | Window | ✅ | — |
| Clear History · Reset Settings · Collect diagnostic info | Help | ✅ | canal: `diagnostics.txt` sem senha |
| Dashboard (`Ctrl+Alt+Shift+B`): 7 gráficos PostgreSQL, 4 MySQL; Add / Remove / View / Refresh / Reset / catálogo / configurações | Database | 🟡 gráficos de linha | captura e canal (valores lidos do servidor) |
| Execute SQL script natively (`Alt+N`) | SQL Editor | 🟡 psql | — |
| DDL (do resultado) | SQL Editor | ✅ | canal |
| Move lines up/down · Join lines · Word completion | editor | ✅ | teste das regras |

## 16. Conexão: o que era gravado e não era aplicado (2026-10-01)

Achado ao ler o perfil (diretiva 6): estes campos do diálogo existiam e nada os usava.

| Campo | Antes | Agora |
|---|---|---|
| Read-only connection | só no perfil | `SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY` ao conectar — conferido no servidor |
| Default schema | só no perfil, e nem era gravado | `SET search_path` / `USE`; a barra de estado acompanha |
| Initialization queries · Ignore errors | idem | uma por linha, em ordem; a que falha derruba a conexão nomeando-se |
| Session role | idem | `SET ROLE` |
| Auto-commit do perfil | ignorado | aplicado ao conectar |
| Keep-alive (+intervalo) | idem | `SELECT 1` interno enquanto ociosa; falhando, a conexão passa a "falhou" |
| Close idle connections (+intervalo) | idem | fecha sem transação aberta, com aviso |
| Túnel SSH e proxy | usados, mas **não gravados** | handlers `ssh_tunnel` / `socks_proxy` no formato do DBeaver; senhas só no arquivo cifrado |
| Read size statistics, Read all data types, Read key columns, Use prepared statements, Replace legacy timezone | caixas sem efeito | desabilitadas, com o motivo na tela |

## 17. Perfil MySQL (2026-10-01)

O editor de objeto, os diálogos de criação, as ferramentas, as sessões e o cliente
nativo do MySQL, no ponto em que está o PostgreSQL. Mapa e estado item a item em
`docs/MYSQL-MAP.md`, seção 4.

| Elemento | Estado | Conferido |
|---|---|---|
| Editor de objeto: banco, tabela, view, função, procedure, trigger, evento, conta | ✅ | suíte ao vivo; canal |
| Create database (Charset, Collation) · Create user (Host, Password) | ✅ | canal: lido do servidor; captura |
| Rename · Comment · Delete pelos menus e pelo formulário | ✅ | canal: lido do servidor |
| Fonte: view por `CREATE OR REPLACE`; rotina, trigger e evento por `DROP` + `CREATE`, com aviso | ✅ | suíte ao vivo |
| Permissions (contas, `WITH GRANT OPTION`) em tabela, view e banco | ✅ | canal: `TABLE_PRIVILEGES`; captura |
| Tools: Analyze, Check, Optimize, Repair, Truncate | ✅ | canal: `status OK` na grade |
| Dump database (mysqldump) · Restore / Execute script (mysql) | ✅ | canal: arquivo e linhas de volta; senha fora da linha de comando |
| Administer → Session Manager, Kill Query / Kill Connection | ✅ | suíte ao vivo (sessão vítima); captura |
| System Info → User privileges, Plugins | ✅ | suíte ao vivo |
| Conta: limites, matriz global de privilégios | 🟡 somente leitura / por objeto | — |
| Privilégios de rotina, engine e charset da tabela | ⬜ | — |

## 18. Primeira execução (ADR 0023)

| Elemento | Estado | Conferido |
|---|---|---|
| Importa as conexões do DBeaver (com senhas) | ✅ | cópia portátil em pasta temporária: 30 conexões |
| Importa os servidores do pgAdmin 4, **com a senha** (chave do cofre do Windows, AES-CFB8) | ✅ | banco real do pgAdmin 9.18: a conexão local abriu com a senha importada; vetores do NIST e fixture cifrada pelo `cryptography` |
| *File → Import connections*: as três ferramentas; conexão que já existe sem senha recebe só a senha | ✅ | canal (`dialog import`, `dialog import passwords`, `dialog import run`); captura |
| Importa os servidores recentes do SSMS (sem senha; abrem pelo driver SQL Server, seção 19) | ✅ | idem |
| Não reimporta na segunda abertura | ✅ | idem |
| As do DBeaver entram no grupo **DBeaver**, como as das outras duas | ✅ | canal: 7 conexões da raiz movidas pela janela de importação; teste de `tool_folder` |
| Grupos da árvore reabrem como foram deixados (`closed-folders`) | ✅ | canal (`nav folder close`), programa encerrado à força e reaberto: captura com o grupo fechado |
| Importa as senhas do SSMS 20+ (Gerenciador de Credenciais do Windows) | ✅ | `spike_external_import`: 11 das 18 conexões com senha; teste com cofre simulado. **Não aplicado** ao arquivo de conexões existente — fica a um clique em *Import connections* |
| Conexão sem senha salva: diálogo `'<conexão>' Authentication` (Username, Password, Save Password/Passphrase) | ✅ | canal: login de rascunho no SQL Server local, conectado como ele; captura |
| Menu de contexto das pastas da árvore: *Create New …* e *Refresh* (faltava em Databases e Schemas) | ✅ | canal (`nav foldermenu`): capturas de Databases, Schemas, Tables e System Info |
| Aba da conexão: título com o banco, `PostgreSQL 18 (ERP_TID)`; faixa das abas discreta, sem cobrir a aba selecionada (tema âmbar) | ✅ | captura ampliada no tema âmbar, com a janela do editor em foco |
| Scripts gravados sozinhos em `.script`; sair não pergunta; abas reabertas ao iniciar | ✅ | canal (`type`, `Exit`, `tab connect`, `Show scripts`, `app dump`) numa pasta temporária: arquivo criado 1,5 s após digitar; `Exit` no mesmo lote da digitação saiu sem perguntar e gravou; encerrado à força, o texto estava no arquivo; reaberto, as duas abas voltaram (uma no banco `ERP_TID`), *Connect* conectou e a consulta respondeu `ERP_TID`; capturas. Teclas de verdade e o clique em *Connect* não foram exercitados |
| Aba da conexão com `×`; programa abre sem aba quando não há script | ✅ | canal (`tab close`, `tab cancel`, `tab confirm`, `app dump`): confirmação com célula editada, aba fechada, sessão viva (script novo executou), abertura sem aba; capturas. O clique no `×` em si não foi exercitado |
| Janela principal reabre no tamanho e na posição em que estava (`window`) | ✅ | movida por `SetWindowPos` sem ativar, programa encerrado à força e reaberto: mesmo retângulo. Maximizada e monitor desligado: só no teste unitário |

## 19. Perfil SQL Server (2026-10-01)

Protocolo, catálogo, editor de objeto, formulários, ferramentas, grade e scripts do SQL
Server. Mapa e estado item a item em `docs/MSSQL-MAP.md`; decisões no ADR 0024.

| Elemento | Estado | Conferido |
|---|---|---|
| Conectar: TDS 7.4 próprio, login cifrado, "Use SSL" cifra tudo | ✅ | servidor local 2022; spike `tds_live` |
| Autenticação: SQL Server e Windows (SSPI) | ✅ | suíte ao vivo: Windows em toda a suíte; SQL Server com um login de rascunho (senha errada é recusada) |
| Diálogo: SQL Server no catálogo, Authentication, Trust Server Certificate | ✅ | captura |
| Árvore: Databases → Schemas → Tables, Views, Indexes, Procedures, Sequences, Synonyms, Data types; Database triggers; Security → Logins; Administer | ✅ 21 de 30 pastas (+2 🟡) | captura; listas na suíte ao vivo |
| Editor de objeto: banco, schema, tabela, view, função, procedure, trigger, sequence, login | ✅ | canal: captura; suíte ao vivo |
| DDL da tabela montado do catálogo (identity, default, constraints, comentário) | ✅ | suíte ao vivo: o DDL mostrado **roda** |
| Create database · schema · login · procedure/função · trigger | ✅ | canal: lido do servidor; captura |
| Rename (`sp_rename`) · Comment (`MS_Description`) · mover de schema · Delete | ✅ | canal: lido do servidor |
| ALTER TABLE em T-SQL: coluna, tipo + nulidade, default como constraint, índice, FK | ✅ | suíte ao vivo |
| Fonte de view, rotina e trigger gravado por `ALTER` | ✅ | suíte ao vivo |
| Permissions: usuários e papéis do banco, `WITH GRANT OPTION` | ✅ | canal: `sys.database_permissions`; captura |
| Grade: paginação por `OFFSET`/`FETCH`, ordenação, filtro, contagem, valores distintos | ✅ | suíte ao vivo; captura (`rows 1-200 +`) |
| Grade editável: `N'...'`, `bit` 0/1, `0x...`, INSERT/UPDATE/DELETE em transação | ✅ | canal e suíte ao vivo: lido do servidor |
| Script: lotes `GO`, lote com `DECLARE` e `CREATE PROCEDURE` inteiros | ✅ | canal (`build/ms_script.ps1`): lido do servidor |
| Chamar rotina: `EXEC` com `OUTPUT` declarado e devolvido | ✅ | canal: valor na grade |
| Importar CSV (lotes de até 1000 linhas) | ✅ | suíte ao vivo: 2500 linhas |
| Transações: modo manual, savepoint, isolamento | ✅ | suíte ao vivo |
| Tools: Update statistics, Rebuild / Reorganize indexes, Check table, Truncate, Enable / Disable trigger, senha do login | ✅ | canal (`mstool`) e suíte ao vivo |
| Backup / Restore database (`BACKUP DATABASE ... TO DISK`, arquivo no servidor) | 🟡 | SQL conferido pelo canal (`ddl dump`); **não executado** |
| Session Manager (`KILL`) · Lock Manager | ✅ | suíte ao vivo: `KILL` numa sessão vítima; captura. O **botão** da tela só foi fotografado, não acionado |
| Dashboard: sessões, lotes/s, transações/s, E/S, tamanho | ✅ | captura; consultas na suíte ao vivo |
| Vários resultados por lote | 🟡 só o primeiro | — |
| Instância nomeada (`host\instância`) pelo SQL Server Browser | ✅ | canal, contra um Browser de teste: conectou e respondeu; instância desconhecida não conecta. Browser real: não conferido (desligado nesta máquina) |
| Entra ID · NTLM digitado · TDS 8.0 | ⬜ fora da lista de autenticação | — |
| Explain (plano) · script nativo (`sqlcmd`) · somente leitura · schema padrão | ⬜ desabilitado ou recusado com o motivo | — |
| Partições, tabelas externas, propriedades estendidas, tipos de tabela | ⬜ | — |

## 20. Perfil SQL Anywhere (2026-10-01)

Protocolo, catálogo, árvore, editor de objeto, formulários, ferramentas, grade e scripts
do SQL Anywhere. O DBeaver Community não tem plugin dele (só o driver genérico "Sybase
jConnect"): o mapa, o que vem do DBeaver e o que vem do Sybase Central estão em
`docs/SQLANYWHERE-MAP.md`; decisões no ADR 0026.

| Elemento | Estado | Conferido |
|---|---|---|
| Conectar: TDS 5.0 próprio (`lib/tdswire/tds5.cpp`), sem ODBC nem jConnect | ✅ | servidor local 16.0.0.2043; spike `tds5_live` |
| Recusas ditas na tela: senha errada, banco que não está em execução, "Use SSL" (o servidor não cifra o TDS) | ✅ | suíte ao vivo |
| Diálogo: SQL Anywhere no catálogo (ícone da Sybase), nota do campo Database e do `-x tcpip`, só *Database Native*, aba SSL desabilitada com o motivo | ✅ | capturas nos três temas |
| Driver properties como `SET TEMPORARY OPTION` | ✅ | suíte ao vivo: aplicada só àquela conexão; nome desconhecido falha |
| Perfis Sybase do DBeaver (`sybase_jconn`, `sybase_jtds`, `sypase_jconn`) abrem por este driver; gravação como `mssql`/`sybase_jconn` | ✅ | teste unitário. **Não** conferido com um `data-sources.json` real do DBeaver nem abrindo o perfil gravado no DBeaver |
| Árvore: Schemas (donos) → Tables, Views, Materialized Views, Indexes, Procedures, Sequences, Data types, Events | ✅ 14 de 17 pastas do DBeaver (+2 🟡) | captura; suíte ao vivo |
| Árvore, do Sybase Central: Users, Roles, Login Policies, Storage → Dbspaces, Remote Servers, Web Services, Publications, Text Configuration Objects, External Environments, Spatial Reference Systems, Administer, System Info | ✅ ➕ | captura (Users, Roles, Login Policies, Dbspaces); listas na suíte ao vivo. Remote Servers, Web Services e Publications estão vazias no `demo` |
| Editor de objeto: tabela, view, view materializada, função, procedure, trigger, evento, sequence, domínio, usuário, papel | ✅ | captura (tabela, procedure); suíte ao vivo (todos) |
| DDL da tabela pelo servidor (`sa_get_table_definition`), sem deixar a conexão alterada | ✅ | suíte ao vivo: opções e transação conferidas depois da leitura |
| Create user · role · sequence · domínio · procedure/função · trigger · evento | ✅ | canal com execução (`build/sa_exec.ps1`): lido do servidor; rotina, trigger e evento só na suíte |
| Rename · Comment · Delete (`DROP USER` × `DROP ROLE ... WITH REVOKE`) | ✅ | canal com execução e suíte ao vivo |
| ALTER TABLE em Watcom SQL: coluna com `NULL` explícito, tipo, default, índice, FK pelo nome do papel | ✅ | suíte ao vivo |
| Fonte de view, rotina, trigger e evento gravado por `ALTER` | ✅ | canal com execução (view) e suíte ao vivo |
| Permissions: usuários e papéis, `WITH GRANT OPTION` em tabela e view | ✅ | canal com execução: `SYSTABLEPERM`; captura |
| Grade: paginação por `TOP n START AT m`, ordenação, filtro, contagem, valores distintos | ✅ | suíte ao vivo; captura (126 linhas) |
| Grade editável: `UNISTR` para texto fora do ASCII, `bit` 0/1, `0x...`, data antes de 1753 | ✅ | suíte ao vivo: lido do servidor |
| Script: `;`, `go`, corpo `BEGIN ... END` como um comando (com `END IF` / `END LOOP` dentro), comentário `//` | ✅ | suíte ao vivo: cada pedaço **roda** |
| Chamar rotina: `CALL`, parâmetros `OUT` devolvidos como linha | ✅ | suíte ao vivo |
| Importar CSV · CREATE TABLE pelo resultado | ✅ | suíte ao vivo |
| Transações: modo manual (`chained`), savepoint, isolamento; estado real em `TransactionStartTime` | ✅ | suíte ao vivo |
| Tools: Validate table, Reorganize table, Create statistics, Truncate, Enable/Disable view, Refresh materialized view, Enable/Disable/Trigger event, senha | ✅ | canal (`satool`) com execução e suíte ao vivo |
| Checkpoint | ✅ | canal com execução |
| Backup database · Validate database | 🟡 | SQL conferido pelo canal (`ddl dump`); **não executados** |
| Session Manager (`DROP CONNECTION`) · Lock Manager | ✅ | suíte ao vivo: sessão vítima encerrada. O **botão** da tela não foi acionado nem fotografado |
| Dashboard: sessões, pedidos/s, transações/s, E/S, cache, tamanho | 🟡 | consultas na suíte ao vivo; a tela não foi aberta |
| Vários resultados por lote | 🟡 só o primeiro | — |
| Cifra do canal · login integrado · SAP ASE | ⬜ recusados com o motivo | — |
| Explain (plano) · script nativo (`dbisql`) · somente leitura · schema padrão | ⬜ desabilitado ou recusado com o motivo | — |
| Editor para dbspace, servidor remoto, serviço web, política de login, publicação | ⬜ só lista | — |

Canal de comandos acrescentado para esta conferência: `satool ...`, `form sabackup ...`,
`dialog new [filtro]`, `dialog url <jdbc:...>`, `dialog tab SSH|SSL|Proxy`,
`connect save <nome>|<jdbc:...>` (conecta **gravando** o perfil, como concluir o diálogo), e
`nav folder open|close` passou a valer para pastas e schemas da árvore, além dos grupos.

Testes automatizados (732, todos verdes):

```powershell
build\win-release\bin\otter_tests.exe
```

### Testes contra um banco real

> **As suítes ao vivo só rodam contra `localhost`.** Em 2026-10-01 `otter_tests_live`
> conectou num servidor de **produção**: escolhia "o primeiro perfil PostgreSQL com senha
> salva", e depois que as senhas do pgAdmin foram importadas esse perfil era o de
> produção. Foram quatro execuções, só de leitura de catálogo, mais um `DO` com
> `RAISE NOTICE` e um `DROP TABLE IF EXISTS` de uma tabela que não existe lá — nada foi
> alterado. Agora `live::require_local` (`tests/integration/live_connect.hpp`) recusa
> qualquer host que não seja local, venha de argumento, ambiente ou perfil, e os spikes
> que escrevem (`alter_live*.cpp`) só aceitam perfil local.

Uma consulta sintaticamente válida em C++ pode ser rejeitada pelo servidor, e
nenhum teste unitário pega isso. Foi o que aconteceu com
`load_routine_definition()`: ficou marcado como "pronto" montando uma
assinatura que o PostgreSQL recusava com
`ERRO: o nome do tipo de dados "p_cliente integer" não é válido`.

`otter_tests_live` executa as consultas de verdade — 43 verificações sobre
relações, corpos de view e de função, tipos, filhos de tabela e paginação
contra uma tabela de 2 milhões de linhas.

```powershell
psql -h localhost -U postgres -d ERP_TID -f tests/integration/fixtures.sql
$env:PGDATABASE="ERP_TID"; $env:PGPASSWORD="..."
build\win-release\bin\otter_tests_live.exe
```

Fora do `ctest` por padrão: depende de um servidor externo. Uma suíte que falha
por falta de banco treina a ignorar falha, o que é pior que não ter o teste.
Para incluir, configurar com `-DOTTER_LIVE_TESTS=ON`.

## Manutenção

Este arquivo é verificado **com a aplicação aberta**, não lendo o código. Elemento que muda
de estado deve ser atualizado no mesmo commit da mudança.

Um guia que afirma que algo funciona quando não funciona é pior que nenhum guia.
