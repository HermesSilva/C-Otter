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
| Tema escuro da lontra | ✅ | Paleta extraída de `Midia/Logo.png` |
| Tema claro | ⬜ | |
| **Internacionalização** | ✅ | Inglês padrão + pt-BR embutido |
| Detecção do idioma do sistema | ✅ | Com fallback por idioma base (`pt-PT` → `pt-BR`) |
| Troca de idioma em tempo real | ✅ | Help → Language, sem reiniciar |
| Idiomas por arquivo `.lang` | ✅ | Sem recompilar; ver `lang/README.md` |
| Relatório de textos sem tradução | ✅ | `missing_translations()` / `export_template()` |
| Docking de painéis (arrastar abas) | ✅ | |
| Layout persistido entre execuções | ⬜ | Volta ao padrão a cada início |
| Ícone da janela | ⬜ | Usa o ícone padrão do sistema |
| Splash screen | ⬜ | `Midia/Splash.png` existe, não é usado |
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
| **Editar** → Desfazer | `Ctrl+Z` | 🟡 | Item funciona; atalho vem do widget, não do menu |
| **Editar** → Refazer | `Ctrl+Y` | 🟡 | Idem |
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

Reescrito em 2026-09-21 seguindo o assistente do DBeaver: catálogo de drivers + 8 abas.

### Etapa 1 — catálogo de drivers

| Elemento | Estado | Observação |
|----------|--------|------------|
| Lista de 18 drivers | ✅ | Com categoria e estado |
| Coluna de categorias | ✅ | Todos, Popular, SQL, NoSQL, Analítico, Arquivos, Embarcado, Séries temporais |
| Filtro por nome | ✅ | Sem diferenciar maiúsculas |
| Drivers indisponíveis esmaecidos | ✅ | Com o motivo: "protocolo em desenvolvimento", "fase 3", "fora do escopo" |
| Duplo clique avança | ✅ | |
| Botão Avançar / Cancelar | ✅ | Avançar desabilitado sem driver disponível |
| Ícones dos SGBDs | ⬜ | O DBeaver mostra o logo de cada banco |

### Etapa 2 — configuração (8 abas)

| Aba | Elemento | Estado |
|-----|----------|--------|
| **Principal** | Host, porta, banco | ✅ |
| | Método de autenticação (5 opções) | ✅ |
| | Usuário, senha, salvar senha | ✅ |
| | Campos de credencial desabilitados quando o método não usa | ✅ |
| **PostgreSQL** | Mostrar todos os bancos / templates / sem acesso | ✅ |
| | Ler estatísticas de tamanho | ✅ |
| | Ler todos os tipos / colunas das chaves | ✅ |
| | Prepared statements, role da sessão, fuso legado | ✅ |
| | Tooltips explicando cada opção | ✅ |
| **Driver** | Tabela de propriedades editável | ✅ |
| | Adicionar / remover propriedade | ✅ |
| | Propriedades aplicadas na conexão | ⬜ |
| **SSH** | Host, porta, usuário, tipo de autenticação | ✅ (UI) |
| | Senha / chave privada / agente | ✅ (UI) |
| | Túnel efetivamente estabelecido | ⬜ **avisado na tela** |
| **SSL** | Modo (disable→verify-full), certificados | ✅ (UI) |
| | TLS negociado | ⬜ **avisado na tela** |
| **Proxy** | Host, porta, credenciais SOCKS | ✅ (UI) |
| | Proxy usado | ⬜ **avisado na tela** |
| **Inicialização** | Auto-commit, somente leitura | ✅ |
| | Schema padrão, consultas de bootstrap | ✅ (UI) |
| | Timeout, keep-alive, fechar ociosas | ✅ |
| **Geral** | Nome, descrição, pasta | ✅ |
| | Tipo: Desenvolvimento / Teste / Produção | ✅ |
| | Cor por tipo e resumo do comportamento | ✅ |
| | Produção desliga auto-commit automaticamente | ✅ |

### Rodapé

| Elemento | Estado |
|----------|--------|
| `< Voltar` (só em nova conexão) | ✅ |
| `Testar conexão` | ✅ |
| `Concluir` / `Salvar` | ✅ fecha o diálogo ao concluir |
| `Testar conexão` não cria conexão permanente | ✅ reutiliza a ativa |
| `Cancelar` | ✅ |
| Indicador pulsante durante a conexão | ✅ |
| Erro detalhado / sucesso com contagens | ✅ |
| Faixa colorida do tipo no topo | ✅ |
| **Persistir a conexão em disco** | ✅ formato do DBeaver (ADR 0012) |
| Senha salva | ✅ AES-128-CBC, chave do DBeaver — **proteção fraca, avisada na tela** |
| Senha no cofre do SO (DPAPI) | ⬜ incompatível com o DBeaver por definição |
| **Importar do DBeaver** | ✅ Arquivo → Importar; lê o workspace real, nunca escreve nele |

## 4. Painel Raft (conexões)

| Elemento | Estado | Observação |
|----------|--------|------------|
| Botão "Nova conexão" | ✅ | |
| Botão "Editar" | ✅ | Reabre o assistente com o perfil ativo |
| Indicador colorido de estado | ✅ | Verde conectado, vermelho falha, amarelo conectando |
| Nome efetivo da conexão | ✅ | Nome do usuário ou `banco@host` |
| Tipo de conexão colorido | ✅ | Desenvolvimento / Teste / Produção |
| Versão do servidor | ✅ | `PostgreSQL 18.2` |
| Host e porta | ✅ | |
| Modo de transação | ✅ | `auto-commit` ou `transação manual` |
| Aviso de somente leitura | ✅ | |
| Descrição da conexão | ✅ | Quando preenchida |
| Menu de contexto | ✅ | Editar, desconectar, copiar nome |
| **Lista de várias conexões** | ✅ | Simultâneas; clique na linha troca a ativa |
| Indicador de estado por conexão | ✅ | Verde/vermelho/amarelo por linha |
| Fechar conexão | ✅ | Menu de contexto → Fechar conexão |
| Detalhe só da conexão ativa | ✅ | Evita repetir host e versão em cada linha |
| Pastas de organização | ⬜ | Campo existe, árvore não agrupa |

## 5. Painel Navigator

| Elemento | Estado | Observação |
|----------|--------|------------|
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
| Campo de filtro/busca | ⬜ | |
| **Menu de contexto** | ✅ | Ver dados, contar linhas, gerar SQL, copiar nome, atualizar |
| Ver dados | ✅ | `SELECT` das colunas, executado |
| Gerar SELECT / INSERT / UPDATE / DELETE | ✅ | `WHERE` pela PK; **aviso** quando não há PK |
| Gerar DDL | ✅ | `CREATE TABLE` com tipos, constraints e índices |
| INSERT/UPDATE/DELETE em view | ✅ | Desabilitados — exigiriam `INSTEAD OF` |
| Atualizar nó (F5) | ✅ | Descarta o cache e relê o catálogo |
| Criar / alterar / excluir objeto | ⬜ | Exige DDL de escrita |
| Duplo clique abre dados | ⬜ | |
| Arrastar tabela para o editor | ⬜ | |
| Atualizar (F5) | ⬜ | |

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
| Word wrap | — | 🟡 Suportado pelo widget, sem comando |
| Code folding | — | 🟡 Idem |
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
| Cancelar query | — | 🟡 | `cancel_current_query()` implementado, **sem botão** |

### Abas de editor

| Elemento | Atalho | Estado | Observação |
|----------|--------|--------|------------|
| **Múltiplas abas** | — | ✅ | Cada uma com editor, resultado e estado próprios |
| Nova aba | `Ctrl+T` | ✅ | Também pelo botão `+` e pelo menu Arquivo |
| Fechar aba | `Ctrl+W` | ✅ | Também pelo `×` da aba |
| Fechar outras | — | ✅ | Menu de contexto; respeita abas fixadas |
| Fixar aba | — | ✅ | Fixadas vão para a esquerda e sobrevivem a "fechar outras" |
| Copiar SQL da aba | — | ✅ | Menu de contexto |
| Reordenar arrastando | — | ✅ | |
| Lista suspensa de abas | — | ✅ | Botão `▼` quando não cabem todas |
| Indicador de modificado | — | ✅ | `*` no título e `●` na barra |
| **Resultado isolado por aba** | — | ✅ | Trocar de aba troca a grade |
| **Roteamento do resultado** | — | ✅ | Volta para a aba que executou, mesmo trocando de aba durante a query |
| Renomear aba | — | ⬜ | `set_title()` existe, sem UI |
| Conexão por aba | — | ⬜ | Todas usam a conexão ativa |

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
| Inferência de JOIN por FK | ⬜ | FKs já carregadas, não usadas |
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
| Selecionar célula / linha | ⬜ | |
| Copiar célula | ⬜ | |
| **Editar célula** | ✅ | Duplo clique; edição em buffer (ADR 0014) |
| Gravação explícita | ✅ | `Salvar alterações` / `Descartar`; nada vai ao banco antes |
| Célula alterada destacada | ✅ | Fundo âmbar, valor original no tooltip |
| Definir `NULL` | ✅ | Menu de contexto — digitar nada é string vazia, não `NULL` |
| Recusa com motivo | ✅ | `JOIN`, sem PK, chave fora do `SELECT`, view |
| `UPDATE` por linha, em transação | ✅ | Ou tudo, ou nada |
| **Inserir linha** | ✅ | Linha verde no fim; coluna em branco usa o `DEFAULT` |
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

| Pivot | ⬜ | ADR 0005 |
| Formatação condicional | ⬜ | |
| Editores de valor (JSON, hex, data) | ⬜ | |
| **Exportar** | ✅ | CSV, JSON, Markdown e `INSERT`, com prévia |
| Proteção contra CSV injection | ✅ | **Ligada por padrão** — valor iniciado por `=`, `+`, `-` ou `@` vira fórmula na planilha |
| Copiar resultado para a área de transferência | ✅ | O resultado inteiro, não só a prévia |
| Aviso de que exporta só a página | ✅ | Evita abrir o arquivo e achar 200 de 2 milhões |
| **Paginação** | ✅ | 200 linhas por página, com primeira/anterior/próxima (ADR 0011) |
| Intervalo de linhas exibido | ✅ | `linhas 401-600 +` — o `+` indica que há mais |
| SQL paginado auditável | ✅ | O inspetor mostra o `LIMIT`/`OFFSET` efetivamente executado |
| `LIMIT` do usuário respeitado | ✅ | Consulta com `LIMIT` próprio não é reescrita |
| Total exato de linhas | ⬜ | Exigiria `COUNT(*)`; a grade mostra `+` em vez de número falso |
| Visão de registro único | ⬜ | |
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
| Copiar SQL | ⬜ | |
| Filtrar por estado | ⬜ | |
| Reexecutar do histórico | ⬜ | |
| Persistir entre sessões | ⬜ | |

## 9. Barra de status

| Elemento | Estado |
|----------|--------|
| Indicador colorido de conexão | ✅ |
| Nome do banco | ✅ |
| Versão do servidor | ✅ |
| Mensagem de estado / erro | ✅ |
| Estado da transação | ⬜ |
| Schema corrente | ⬜ |

## 10. Transações

**Nenhum elemento existe.** É a lacuna mais grave do produto: sem isso, o C-Otter é um
visualizador, não uma ferramenta de trabalho.

| Elemento | Estado |
|----------|--------|
| Alternar autocommit | ⬜ |
| Commit / Rollback | ⬜ |
| Savepoints | ⬜ |
| Indicador de transação aberta | ⬜ |
| Aviso ao fechar com alterações pendentes | ⬜ |

---

## Defeitos conhecidos

| # | Onde | Problema |
|---|------|----------|
| 1 | Conexão | Senha em disco tem a proteção fraca do DBeaver (chave pública) — **avisado na tela**, ADR 0012 |

| 4 | Raft | As conexões abertas não são restauradas ao reiniciar — só a última volta preenchida no diálogo |
| 3 | Grade | `OFFSET` alto é lento: o servidor produz e descarta as linhas puladas (custo inerente ao ADR 0011) |

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

| Área | ✅ | 🟡 | ⬜ | ❌ | Total |
|------|-----|-----|-----|-----|-------|
| Janela e estrutura | 7 | 0 | 5 | 0 | 12 |
| Barra de menus | 8 | 3 | 4 | 0 | 15 |
| Diálogo de conexão | 10 | 0 | 5 | 0 | 15 |
| Painel Raft | 4 | 0 | 3 | 0 | 7 |
| Navigator | 9 | 0 | 8 | 0 | 17 |
| Editor — texto | 13 | 2 | 0 | 0 | 15 |
| Editor — execução | 4 | 1 | 2 | 0 | 7 |
| Editor — autocomplete | 13 | 0 | 3 | 0 | 16 |
| Grade | 13 | 0 | 14 | 0 | 27 |
| Inspetor de queries | 9 | 0 | 4 | 0 | 13 |
| Barra de status | 4 | 0 | 2 | 0 | 6 |
| Transações | 0 | 0 | 5 | 0 | 5 |
| **Total** | **94** | **6** | **55** | **0** | **155** |

**94 de 155 elementos existentes funcionam.**

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
> | Atalhos | 147 | 6 | **4,1%** |
>
> Um diálogo de conexão com 5 campos conta como 10 elementos prontos aqui; o equivalente no
> DBeaver tem catálogo de drivers, abas de propriedades, SSH, SSL e teste de conexão — algo
> como 80. Medir a própria interface contra si mesma produz um número que sobe enquanto o
> produto não se aproxima do alvo.
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

Testes automatizados (261, todos verdes):

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
