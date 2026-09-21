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
| Conectar a MySQL / MariaDB | ✅ | ✅ protocolo nativo, sem libmysqlclient |
| Diálogo de conexão com campos básicos | ✅ | ✅ |
| Indicador visual de estado da conexão | ✅ | ✅ |
| Desconectar | ✅ | ✅ |
| Pré-preenchimento por variáveis de ambiente | ✅ | ✅ |
| Múltiplas conexões simultâneas | ✅ | ✅ |
| Salvar/persistir conexões | ✅ | ✅ no formato do DBeaver (ADR 0012) |
| Importar conexões do DBeaver | ➖ | ✅ com senha decifrada |
| Credenciais em cofre do SO (DPAPI/libsecret) | ✅ | ⬜ |
| Pastas de organização de conexões | ✅ | ⬜ |
| Túnel SSH | ✅ | ⬜ |
| TLS/SSL | ✅ | ⬜ |
| Teste de conexão antes de salvar | ✅ | ⬜ |
| Conexão somente-leitura | ✅ | ⬜ |
| ~50 SGBDs | ✅ | ➖ 2 de 5 previstos na v1 |

## 2. Navigator (46 comandos no DBeaver)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Árvore de schemas e tabelas | ✅ | ✅ |
| Colunas com tipo, PK e nullable | ✅ | ✅ |
| Carregamento tardio (lazy) | ✅ | ✅ |
| Tamanho e contagem estimada de linhas | ✅ | ✅ |
| Distinção visual de views | ✅ | ✅ |
| Índices, constraints, triggers, sequences | ✅ | ✅ |
| Funções e procedures | ✅ | ✅ |
| Filtro/busca na árvore | ✅ | ✅ |
| Menu de contexto (DDL, dados, renomear) | ✅ | ✅ exceto renomear |
| Esconder pasta que o SGBD não tem | ✅ `visibleIf` | ✅ |
| Criar/alterar/remover objeto | ✅ | 🟡 coluna e tabela (ADR 0016) |
| Confirmação antes de DDL destrutivo | ✅ aba Persist | ✅ |
| Gerar DDL | ✅ | ✅ |
| Copiar nome qualificado | ✅ | ✅ |
| Navegar para referência (FK) | ✅ | ⬜ |
| Arrastar tabela para o editor | ✅ | ⬜ |
| Renomear objeto | ✅ | 🟡 tabela |

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
| Análise de escopo (FROM → tabelas) | ✅ | ✅ |
| Filtro de colunas por `alias.` | ✅ | ✅ |
| Minimap | ➖ | ✅ |
| Executar script inteiro (`Alt+X`) | ✅ | ✅ |
| Múltiplas abas de editor | ✅ | ✅ |
| Abrir/salvar arquivo `.sql` | ✅ | ✅ |
| Formatar SQL (`Ctrl+Shift+F`) | ✅ | ✅ |
| Comentar/descomentar (`Ctrl+/`) | ✅ | 🟡 do widget, sem mapeamento próprio |
| Plano de execução (`Ctrl+Shift+E`) | ✅ | ✅ ADR 0013 |
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
| Ordenar clicando no cabeçalho | ✅ | ✅ no servidor (ADR 0011) |
| Editar célula | ✅ | ✅ ADR 0014 |
| Inserir/duplicar/excluir linha | ✅ | 🟡 excluir sim; inserir e duplicar não |
| Salvar alterações | ✅ | ✅ em transação |
| Filtro por coluna | ✅ | ✅ no servidor |
| Visão de registro único | ✅ | ⬜ |
| Agrupamento e subtotais (ADR 0005) | ✅ | ✅ |
| Linha de totais | ✅ | ✅ |
| Pivot | ✅ | ✅ local e no servidor |
| Formatação condicional | ✅ | ✅ 12 operadores, célula ou linha, mapa de calor |
| Editores de valor (JSON, hex, data) | ✅ | ⬜ |
| Copiar como CSV/Markdown/SQL | ✅ | ✅ via exportação |
| Exportar resultado | ✅ | ✅ CSV/JSON/Markdown/INSERT |
| Paginação / carregar mais | ✅ | ✅ ADR 0011 |
| Gráficos do resultado | ✅ | ➖ |

## 5. Transações

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Autocommit on/off | ✅ | ✅ |
| Commit / rollback manual | ✅ | ✅ `Ctrl+Shift+C` / `Ctrl+Shift+R` |
| Savepoints | ✅ | 🟡 no driver, sem UI |
| Indicador de transação aberta | ✅ | ✅ na barra |
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
| Plano de execução visual | ✅ | ✅ ADR 0013 |
| Debugger PL/pgSQL | ✅ | ⬜ |

## 7. Fora do escopo da v1 (decisão consciente)

ERD (8 comandos), Dashboards (11), integração Git (6), Tasks (9), DevTools (10), GIS,
Office/Excel, e ~45 dos 50 SGBDs. Ver `docs/PLAN.md` §1.

---

## Resumo quantitativo

| Área | Itens listados | ✅ | 🟡 | ⬜ |
|------|---------------|-----|-----|-----|
| Conexão | 15 | 9 | 0 | 6 |
| Navigator | 17 | 13 | 2 | 2 |
| Editor SQL | 25 | 19 | 2 | 4 |
| Grade | 20 | 17 | 1 | 2 |
| Transações | 5 | 3 | 1 | 1 |
| Diagnóstico | 8 | 5 | 1 | 2 |
| **Total (sem os itens ➖)** | **90** | **66** | **7** | **17** |

**Cobertura atual: 77% dos itens de escopo da v1** (66 de 90, contando parciais
como meio).

Contado em 2026-09-21 varrendo as tabelas acima, e **cada ✅ novo foi
verificado no código** — não de memória. O número anterior (41%) estava
desatualizado por vários commits: paginação, grade editável, agrupamento,
exportação, formatação e plano de execução já funcionavam e ainda constavam
como ⬜. Inventário atrasado dá falsa confiança, que é o que esta seção
existe para evitar.

Para o inventário elemento a elemento — cada botão, menu e atalho, com estado verificado na
aplicação rodando — ver [`ELEMENTS.md`](ELEMENTS.md).

Contra os **275 comandos** do DBeaver o número é bem menor, e a diferença não é
contradição: este inventário lista *funcionalidades*, não *comandos* — "copiar
célula como Markdown" é uma variação de "copiar", e aqui elas contam como uma.

**A cobertura de SGBD é outra conta, e muito menor:** 2 de ~50, contra os quais
esses 77% valem. Ver `docs/MYSQL-MAP.md` (21 de 27 nós da árvore MySQL) e
`docs/NAVIGATOR-TREE.md` (21 de ~70 no PostgreSQL).

## Próximos passos, por impacto

1. **Telas de `CREATE`** — os geradores de tabela, view, sequence, índice e
   constraint já existem e são testados contra o servidor; falta o formulário
2. **Inserir e duplicar linha na grade** — excluir já funciona; o buffer de
   edição já comporta os três
3. **TLS** — destrava o `caching_sha2_password` sem RSA e as conexões remotas
4. **Editores de valor** (JSON, hex, data) — hoje toda célula é texto
5. **Savepoints na UI** — o driver já os implementa

~~Ligar `analyze_scope` à UI~~ — **concluído em 2026-09-21**.
~~Grade editável, transações, paginação, ordenação, filtro, abas, arquivo~~ —
**concluídos em 2026-09-21**.

## Manutenção deste arquivo

Toda funcionalidade concluída deve mudar de ⬜ para ✅ **no mesmo commit** que a implementa.
Um inventário desatualizado é pior que nenhum: dá falsa confiança.
