# Guia de elementos — o que funciona e o que não funciona

**Última verificação: 2026-09-21** · build `win-debug`, PostgreSQL 18.2, banco `ERP_TID`

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
| **Arquivo** → Desconectar | — | ✅ | Desabilitado quando não há conexão |
| **Arquivo** → Sair | `Alt+F4` | ✅ | |
| **Arquivo** → Abrir script | `Ctrl+O` | ⬜ | |
| **Arquivo** → Salvar script | `Ctrl+S` | ⬜ | |
| **Editar** → Desfazer | `Ctrl+Z` | 🟡 | Item funciona; atalho vem do widget, não do menu |
| **Editar** → Refazer | `Ctrl+Y` | 🟡 | Idem |
| **Editar** → Selecionar tudo | `Ctrl+A` | ✅ | |
| **Editar** → Localizar | `Ctrl+F` | ✅ | Item abre a janela de busca do editor |
| **SQL** → Executar | `Ctrl+Enter` | ✅ | Item e atalho funcionam |
| **SQL** → Executar script | `Alt+X` | ⬜ | Splitter pronto em `otter_sql`, não ligado |
| **SQL** → Formatar | `Ctrl+Shift+F` | ⬜ | |
| **SQL** → Explicar plano | `Ctrl+Shift+E` | ⬜ | |
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
| `Concluir` / `Salvar` | ✅ |
| `Cancelar` | ✅ |
| Indicador pulsante durante a conexão | ✅ |
| Erro detalhado / sucesso com contagens | ✅ |
| Faixa colorida do tipo no topo | ✅ |
| **Persistir a conexão em disco** | ⬜ **redigitar a cada execução** |
| Senha no cofre do SO | ⬜ |

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
| Lista de várias conexões | ⬜ | Uma por vez |
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
| Corpo da função | 🟡 | `load_routine_definition()` pronto, sem UI |
| **Tipos de dados** | ✅ | enum com valores ordenados, composto com campos, domain com CHECK |
| Campo de filtro/busca | ⬜ | |
| Menu de contexto | ⬜ | Sem "ver dados", "gerar DDL", "renomear" |
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
| Executar script inteiro | `Alt+X` | ⬜ | |
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
| Ordenar pelo cabeçalho | ⬜ | |
| Selecionar célula / linha | ⬜ | |
| Copiar célula | ⬜ | |
| **Editar célula** | ⬜ | |
| Inserir / excluir linha | ⬜ | |
| Salvar alterações | ⬜ | |
| Filtro por coluna | ⬜ | |
| Agrupamento e subtotais | ⬜ | ADR 0005 |
| Linha de totais | ⬜ | ADR 0005 |
| Pivot | ⬜ | ADR 0005 |
| Formatação condicional | ⬜ | |
| Editores de valor (JSON, hex, data) | ⬜ | |
| Exportar (CSV, JSON, SQL) | ⬜ | |
| Paginação / carregar mais | ⬜ | Carrega tudo de uma vez |
| Visão de registro único | ⬜ | |
| Limite de 64 colunas | ⚠️ | Restrição do ImGui; resultados maiores são truncados |

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
| 1 | Grade | Resultados com mais de 64 colunas são truncados **sem aviso** (limite do ImGui) |
| 2 | Conexão | Não persiste — redigitar host/banco/usuário a cada execução |
| 3 | Grade | Carrega o resultado inteiro de uma vez; um `SELECT` sem `LIMIT` numa tabela grande trava a UI até terminar |
| 4 | Editor | O ponto salvo nunca muda porque não há "salvar"; o `●` aparece na primeira edição e fica |

## Pronto no núcleo, ausente na UI

Código implementado **e testado** que ainda não tem ponto de entrada na interface.
Esta é a lista de maior retorno por esforço: o trabalho difícil já está feito.

| Capacidade | Onde está | Falta |
|------------|-----------|-------|
| Separação de script | `sql/script.cpp` | Comando "executar script" (`Alt+X`) |
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

```powershell
# Árvore inteira, conectada, pronta para captura:
$env:OTTER_AUTOCONNECT="1"; $env:OTTER_EXPAND_TREE="1"
build\win-release\bin\c-otter.exe
tools\screenshot.ps1 -Out arvore.png
```

Testes automatizados (113, todos verdes):

```powershell
build\win-release\bin\otter_tests.exe
```

## Manutenção

Este arquivo é verificado **com a aplicação aberta**, não lendo o código. Elemento que muda
de estado deve ser atualizado no mesmo commit da mudança.

Um guia que afirma que algo funciona quando não funciona é pior que nenhum guia.
