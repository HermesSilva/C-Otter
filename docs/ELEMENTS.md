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
| Detecção do idioma do sistema | ✅ | Com fallback por idioma base (`pt-PT` → `pt-BR`) |
| Troca de idioma em tempo real | ✅ | Help → Language, sem reiniciar |
| Idiomas por arquivo `.lang` | ✅ | Sem recompilar; ver `lang/README.md` |
| Relatório de textos sem tradução | ✅ | `missing_translations()` / `export_template()` |
| Docking de painéis (arrastar abas) | ✅ | |
| **Layout persistido** | ✅ | `%APPDATA%\C-Otter\layout.ini`, não no diretório de trabalho |
| **Ícone da janela** | ✅ | Gerado em memória (32x32 RGBA) — decodificar o PNG exigiria trazer um stb_image só para isto |
| Splash screen | ➖ | `Midia/Splash.png` existe, mas exibi-lo pede um decodificador de PNG (o projeto só tem `stb_image_write`). O C-Otter abre em ~300 ms — um splash apareceria depois da janela, o que é pior que não ter |
| Múltiplas janelas | ⬜ | |

## 2. Barra de menus

| Menu → Item | Atalho | Estado | Observação |
|-------------|--------|--------|------------|
| **Arquivo** → Nova conexão... | `Ctrl+Shift+N` | ✅ | Item e atalho |
| **Arquivo** → Importar do DBeaver... | — | ✅ | Lê os workspaces reais; nunca escreve neles |
| **Arquivo** → Desconectar | — | ✅ | Desabilitado quando não há conexão |
| **Arquivo** → Sair | `Alt+F4` | ✅ | |
| **Arquivo** → Abrir script | `Ctrl+O` | ✅ | Diálogo nativo do sistema; abre em aba nova |
| **Arquivo** → Salvar script | `Ctrl+S` | ✅ | `Ctrl+Shift+S` para "salvar como" |
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
| **Ícones dos SGBDs** | 🟡 | Existem 3 (`pg_server`, `my_server`, `generic_server`); o catálogo lista 18 drivers, mas só 3 têm protocolo |

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
| **Editor de dados** → Editor binário, Formatos de dados | — | ⬜ **avisado na tela** |
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

## 4. Painel Raft (conexões)

**Lista única desde 2026-09-21.** Tinha três blocos — os botões do topo, as
conexões *abertas*, e as *salvas* sob um rótulo esmaecido. O DBeaver não
divide: cada perfil é uma linha só, conectada ou não, com o estado no ponto à
esquerda. A mesma conexão aparecia ora em cima ora embaixo conforme houvesse
sessão, e "salvas" em caixa baixa parecia item da lista, não cabeçalho.

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Lista única, conectadas e salvas juntas** | ✅ | Sem cabeçalho de seção, como no DBeaver |
| Indicador colorido de estado | ✅ | `●` verde conectado, vermelho falha, amarelo conectando; `○` salvo |
| Nome efetivo da conexão | ✅ | Nome do usuário ou `banco@host` |
| Duplo clique conecta um perfil salvo | ✅ | Clique simples só seleciona |
| Menu de contexto na conexão | ✅ | Editar, desconectar, fechar, copiar nome |
| Menu de contexto na área vazia | ✅ | Nova conexão — substitui os botões do topo |
| **Lista de várias conexões** | ✅ | Simultâneas; clique na linha troca a ativa |
| Fechar conexão | ✅ | Menu de contexto → Fechar conexão |
| Versão, host, transação, somente leitura, descrição | ✅ | Em **tooltip**, não empilhados na lista |
| Tipo de conexão colorido | ✅ | No tooltip; a faixa colorida fica no diálogo |
| Botão "Nova conexão" no topo | ➖ | Removido: o DBeaver usa barra e menu de contexto |
| Botão "Editar" no topo | ➖ | Removido: menu de contexto da conexão |
| **Pastas de organização** | ✅ | O campo Pasta do diálogo agrupa a lista; conexões sem pasta ficam na raiz |
| **Ícone do SGBD por conexão** | ✅ | Elefante (PostgreSQL), golfinho (MySQL/MariaDB), torre (demais) — desenhos próprios, não logos |

## 5. Painel Navigator

| Elemento | Estado | Observação |
|----------|--------|------------|
| **Ordem seta → ícone → nome** | ✅ | Como no DBeaver. Era ícone → seta, e as setas de um mesmo nível ficavam desalinhadas entre si |
| Árvore de schemas | ✅ | Ícone próprio, um nó por schema |
| **Pasta Tabelas** | ✅ | Com contagem: `Tabelas (32)` |
| **Pasta Views** | ✅ | Separada das tabelas, como no DBeaver |
| **Pasta Views materializadas** | ✅ | Separada, com Índices e sem Triggers |
| Pasta vazia fica oculta | ✅ | Schema sem views não mostra `Views (0)` |
| Tamanho da relação | ✅ | `112 kB` em tom apagado |
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
| Ícone próprio por tipo | ✅ | 18 tipos, nenhum compartilhado |
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
| Conectar abre a janela com `Script 1` | — | ✅ | Sem exigir clique no `+` antes |
| Conexões lado a lado | — | ✅ | Arrastando a janela, pelo docking do ImGui |
| **Múltiplas abas de script** | — | ✅ | Cada uma com editor, resultado e estado próprios |
| Nova aba | `Ctrl+T` | ✅ | Também pelo botão `+` e pelo menu Arquivo |
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
| Ícone por tipo de objeto | ⬜ | Popup só aceita texto (ADR 0004) |
| Painel de detalhe lateral | ⬜ | |
| Sugestão por IA | ⬜ | |

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
| Coluna calculada | ⬜ | Resto do ADR 0005 |
| **Exportar** | ✅ | CSV, JSON, Markdown e `INSERT`, com prévia |
| Proteção contra CSV injection | ✅ | **Ligada por padrão** — valor iniciado por `=`, `+`, `-` ou `@` vira fórmula na planilha |
| Copiar resultado para a área de transferência | ✅ | O resultado inteiro, não só a prévia |
| Aviso de que exporta só a página | ✅ | Evita abrir o arquivo e achar 200 de 2 milhões |
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
| **Aviso ao fechar com alterações pendentes** | ✅ | Menu Sair, `Alt+F4` e o `X` da janela; diz quantos scripts e quantas células |
| Savepoints na UI | ➖ | **O DBeaver também não os expõe** — busca por `savepoint` nos `plugin.xml` não acha comando nenhum; ele os usa internamente e os filtra do log (`SQLLogFilter.java:43`). O driver os implementa (`Holt::savepoint`); criar uma tela aqui seria divergir sem paridade a ganhar |

---

## Defeitos conhecidos

| # | Onde | Problema |
|---|------|----------|
| 1 | Conexão | Senha em disco tem a proteção fraca do DBeaver (chave pública) — **avisado na tela**, ADR 0012 |

| 4 | Raft | As conexões abertas não são restauradas ao reiniciar — só a última volta preenchida no diálogo |
| 3 | Grade | `OFFSET` alto é lento: o servidor produz e descarta as linhas puladas (custo inerente ao ADR 0011) |
| 5 | Editor | **"Mostrar espaços" não desenha nada.** `SetShowWhitespacesEnabled(true)` é chamada a cada quadro (confirmado com trace), e `TextEditor.cpp:629` tem o código do desenho — mas os pontos não aparecem, nem depois de corrigir a cor. A caixa fica **desabilitada na tela**, com o motivo, em vez de fingir que funciona |

### Corrigidos

| Onde | O que era | Como foi resolvido |
|------|-----------|--------------------|
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

Contado em 2026-09-21 varrendo as tabelas acima; itens ➖ (fora do escopo)
ficam de fora do total.

| Área | ✅ | 🟡 | ⬜ | ❌ | Total |
|------|-----|-----|-----|-----|-------|
| Janela e estrutura | 18 | 0 | 1 | 0 | 19 |
| Barra de menus | 18 | 0 | 0 | 0 | 18 |
| Assistente de conexão | 50 | 1 | 4 | 1 | 56 |
| Painel Raft | 12 | 0 | 0 | 0 | 12 |
| Navigator | 34 | 0 | 0 | 0 | 34 |
| Editor SQL | 58 | 0 | 3 | 0 | 61 |
| Grade | 56 | 0 | 1 | 0 | 57 |
| Inspetor de queries | 15 | 0 | 1 | 0 | 16 |
| Barra de status | 8 | 0 | 0 | 0 | 8 |
| Transações | 5 | 0 | 0 | 0 | 5 |
| **Total** | **274** | **1** | **10** | **1** | **286** |

**274 de 286 elementos existentes funcionam.**

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
> | Comandos | 281 | 7 | **2,5%** |
> | Atalhos | 147 | 24 | **16,3%** |
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

```powershell
# Árvore inteira, conectada, pronta para captura:
$env:OTTER_AUTOCONNECT="1"; $env:OTTER_EXPAND_TREE="1"
build\win-release\bin\c-otter.exe
tools\screenshot.ps1 -Out arvore.png
```

Testes automatizados (438, todos verdes):

```powershell
build\win-release\bin\otter_tests.exe
```

### Testes contra um banco real

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
