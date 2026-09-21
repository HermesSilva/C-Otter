# Inventário de paridade com o DBeaver

**Criado em 2026-09-21**, respondendo a uma pergunta direta: *"foi enumerado cada
funcionalidade, cada elemento da UI, cada tecla de atalho do DBeaver, para conferir o que
está ou não pronto?"*

**A resposta honesta era NÃO.** Até aqui o projeto vinha afirmando "paridade" com base em
medições de tamanho de código (`docs/ANALYSIS.md`), não num inventário verificável do que a
ferramenta faz. Este arquivo corrige isso.

## Método

Extraído do próprio repositório do DBeaver (`D:\Tootega\Source\dbeaver`), não de memória:

| Métrica | Valor | Como foi obtido |
|---------|-------|-----------------|
| Comandos declarados | **275** | `<command id="org.jkiss.dbeaver.*">` em `plugin.xml` |
| Atalhos de teclado | **157** | `sequence="..."` em `plugin.xml` |
| Plugins | 156 | contagem de diretórios |
| Linhas de Java | 916.759 | ver `docs/ANALYSIS.md` |

Distribuição dos 275 comandos por área:

| Plugin | Comandos |
|--------|----------|
| `ui.editors.data` (grade) | **67** |
| `ui.editors.sql` (editor) | **63** |
| `ui.navigator` | **46** |
| `core` | 25 |
| `ui.dashboard` | 11 |
| `ui.app.devtools` | 10 |
| `tasks.ui.view` | 9 |
| `ui.editors.erd` | 8 |
| demais (git, transfer, drivers, AI) | 36 |

**Legenda de estado:** ✅ pronto · 🟡 parcial · ⬜ não iniciado · ➖ fora do escopo da v1

---

## 1. Conexão e gerenciamento (Raft)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Conectar a PostgreSQL | ✅ | ✅ |
| Diálogo de conexão com campos básicos | ✅ | ✅ |
| Indicador visual de estado da conexão | ✅ | ✅ |
| Desconectar | ✅ | ✅ |
| Pré-preenchimento por variáveis de ambiente | ✅ | ✅ |
| Múltiplas conexões simultâneas | ✅ | ⬜ |
| Salvar/persistir conexões | ✅ | ⬜ |
| Credenciais em cofre do SO (DPAPI/libsecret) | ✅ | ⬜ |
| Pastas de organização de conexões | ✅ | ⬜ |
| Túnel SSH | ✅ | ⬜ |
| TLS/SSL | ✅ | ⬜ |
| Teste de conexão antes de salvar | ✅ | ⬜ |
| Conexão somente-leitura | ✅ | ⬜ |
| ~50 SGBDs | ✅ | ➖ (5 na v1) |

## 2. Navigator (46 comandos no DBeaver)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Árvore de schemas e tabelas | ✅ | ✅ |
| Colunas com tipo, PK e nullable | ✅ | ✅ |
| Carregamento tardio (lazy) | ✅ | ✅ |
| Tamanho e contagem estimada de linhas | ✅ | ✅ |
| Distinção visual de views | ✅ | ✅ |
| Índices, constraints, triggers, sequences | ✅ | ⬜ |
| Funções e procedures | ✅ | ⬜ |
| Filtro/busca na árvore | ✅ | ⬜ |
| Menu de contexto (DDL, dados, renomear) | ✅ | ⬜ |
| Criar/alterar/remover objeto | ✅ | ⬜ |
| Gerar DDL | ✅ | ⬜ |
| Copiar nome qualificado | ✅ | ⬜ |
| Navegar para referência (FK) | ✅ | ⬜ |
| Arrastar tabela para o editor | ✅ | ⬜ |

## 3. Editor SQL (63 comandos no DBeaver)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Realce de sintaxe | ✅ | ✅ |
| Numeração de linhas | ✅ | ✅ |
| Múltiplos cursores | ✅ | ✅ |
| Undo/redo | ✅ | ✅ |
| Localizar e substituir | ✅ | ✅ |
| Bracket matching | ✅ | ✅ |
| Executar statement (`Ctrl+Enter`) | ✅ | ✅ |
| Executar só a seleção | ✅ | ✅ |
| Autocomplete (`Ctrl+Espaço`) | ✅ | ✅ |
| Completion sobre metadados reais | ✅ | ✅ |
| Ranking contextual de sugestões | ✅ | ✅ |
| Análise de escopo (FROM → tabelas) | ✅ | 🟡 pronto em `otter_sql`, não ligado à UI |
| Minimap | ➖ | ✅ |
| Executar script inteiro (`Alt+X`) | ✅ | ⬜ |
| Múltiplas abas de editor | ✅ | ⬜ |
| Abrir/salvar arquivo `.sql` | ✅ | ⬜ |
| Formatar SQL (`Ctrl+Shift+F`) | ✅ | ⬜ |
| Comentar/descomentar (`Ctrl+/`) | ✅ | 🟡 do widget, sem mapeamento próprio |
| Plano de execução (`Ctrl+Shift+E`) | ✅ | ⬜ |
| Histórico de queries | ✅ | 🟡 painel Queries mostra a sessão atual |
| Templates/snippets | ✅ | ⬜ |
| Ir para declaração (`F3`) | ✅ | ⬜ |
| Terminal SQL | ✅ | ⬜ |
| Geração NL→SQL por IA (`Ctrl+I`) | ✅ | ⬜ |

## 4. Grade de resultados (67 comandos no DBeaver)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Exibição tabular | ✅ | ✅ |
| Virtualização (só linhas visíveis) | ✅ | ✅ |
| Cabeçalho e primeira coluna fixos | ✅ | ✅ |
| Redimensionar e reordenar colunas | ✅ | ✅ |
| Alinhamento numérico à direita | ✅ | ✅ |
| `[null]` distinto de string vazia | ✅ | ✅ |
| Ordenar clicando no cabeçalho | ✅ | ⬜ |
| Editar célula | ✅ | ⬜ |
| Inserir/duplicar/excluir linha | ✅ | ⬜ |
| Salvar alterações (`Ctrl+S`) | ✅ | ⬜ |
| Filtro por coluna | ✅ | ⬜ |
| Visão de registro único | ✅ | ⬜ |
| Agrupamento e subtotais (ADR 0005) | ✅ | ⬜ |
| Linha de totais | ✅ | ⬜ |
| Pivot | ✅ | ⬜ |
| Formatação condicional | ✅ | ⬜ |
| Editores de valor (JSON, hex, data) | ✅ | ⬜ |
| Copiar como CSV/Markdown/SQL | ✅ | ⬜ |
| Exportar resultado | ✅ | ⬜ |
| Paginação / carregar mais | ✅ | ⬜ |
| Gráficos do resultado | ✅ | ➖ |

## 5. Transações

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Autocommit on/off | ✅ | ⬜ |
| Commit / rollback manual | ✅ | ⬜ |
| Savepoints | ✅ | ⬜ |
| Indicador de transação aberta | ✅ | ⬜ |
| Log de transação | ✅ | ⬜ |

## 6. Diagnóstico e observabilidade (ADR 0008)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Inspetor de queries com tempo | ✅ | ✅ |
| Queries internas de catálogo visíveis | 🟡 | ✅ |
| Contador de FPS | ➖ | ✅ |
| Bytes do resultado em memória | ➖ | ✅ |
| Cancelar query em execução | ✅ | 🟡 protocolo pronto, sem botão |
| Erro apontado na posição do editor | ✅ | ⬜ |
| Plano de execução visual | ✅ | ⬜ |
| Debugger PL/pgSQL | ✅ | ⬜ |

## 7. Fora do escopo da v1 (decisão consciente)

ERD (8 comandos), Dashboards (11), integração Git (6), Tasks (9), DevTools (10), GIS,
Office/Excel, e ~45 dos 50 SGBDs. Ver `docs/PLAN.md` §1.

---

## Resumo quantitativo

| Área | Itens listados | ✅ | 🟡 | ⬜ |
|------|---------------|-----|-----|-----|
| Conexão | 14 | 5 | 0 | 8 |
| Navigator | 14 | 5 | 0 | 9 |
| Editor SQL | 24 | 12 | 3 | 9 |
| Grade | 21 | 6 | 0 | 14 |
| Transações | 5 | 0 | 0 | 5 |
| Diagnóstico | 8 | 4 | 1 | 3 |
| **Total (sem os itens ➖)** | **86** | **32** | **4** | **48** |

**Cobertura atual: ~37% dos itens de escopo da v1** (32 de 86, contando parciais como meio).

Contra os **275 comandos** do DBeaver, o C-Otter implementa hoje cerca de **20**. A diferença
entre 37% e 7% é que este inventário lista *funcionalidades*, não *comandos* — um comando do
DBeaver como "copiar célula como Markdown" é uma variação de outro, e o inventário agrupa.

## Próximos passos, por impacto

1. **Grade editável + transações** (19 itens) — é o que separa visualizador de ferramenta
2. **Ligar `analyze_scope` à UI** (1 item, já implementado e testado) — completion com escopo
3. **Ordenação e filtro na grade** (2 itens) — alto uso, custo baixo
4. **Múltiplas abas e abrir/salvar arquivo** (3 itens) — fluxo básico de trabalho
5. **Persistir conexões + cofre de credenciais** (3 itens) — hoje se redigita a cada execução

## Manutenção deste arquivo

Toda funcionalidade concluída deve mudar de ⬜ para ✅ **no mesmo commit** que a implementa.
Um inventário desatualizado é pior que nenhum: dá falsa confiança.
