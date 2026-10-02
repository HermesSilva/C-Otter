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
| Comandos declarados | **281** | `tools/map_dbeaver.py`, ver `docs/DBEAVER-MAP.md` |
| Atalhos de teclado | **147** | `tools/map_dbeaver.py`, ver `docs/DBEAVER-MAP.md` |
| Plugins | 156 | contagem de diretórios |
| Linhas de Java | 916.759 | ver `docs/ANALYSIS.md` |

Os números anteriores (275 e 157) vinham de um `grep` direto no `plugin.xml`, que
contava `<command>` sem `defaultHandler` e sequências únicas em vez de bindings.
`map_dbeaver.py` é a fonte canônica; os três documentos agora usam a mesma conta.

Distribuição dos comandos por área:

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
| demais (git, transfer, drivers, AI) | 42 |

**Legenda de estado:** ✅ pronto · 🟡 parcial · ⬜ não iniciado · ➖ fora do escopo da v1

---

## 1. Conexão e gerenciamento (Raft)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Conectar a PostgreSQL | ✅ | ✅ |
| Conectar a MySQL / MariaDB | ✅ | ✅ protocolo nativo, sem libmysqlclient |
| Conectar a SQL Server | ✅ | ✅ TDS nativo, sem ODBC nem FreeTDS (ADR 0024); SQL Server e Windows Authentication. Instância nomeada e Entra ID ⬜ |
| Conectar a SQL Anywhere | 🟡 só o driver genérico "Sybase jConnect" | ✅ TDS 5.0 nativo, sem ODBC nem jConnect (ADR 0026). Sem cifra (o servidor não cifra o TDS) e sem login integrado; SAP ASE ⬜ |
| Diálogo de conexão com campos básicos | ✅ | ✅ |
| Indicador visual de estado da conexão | ✅ | ✅ |
| Desconectar | ✅ | ✅ |
| Pré-preenchimento por variáveis de ambiente | ✅ | ✅ |
| Múltiplas conexões simultâneas | ✅ | ✅ |
| Salvar/persistir conexões | ✅ | ✅ no formato do DBeaver (ADR 0012) |
| Importar conexões do DBeaver | ➖ | ✅ com senha decifrada |
| Credenciais em cofre do SO (DPAPI/libsecret) | ✅ | ⬜ |
| Pastas de organização de conexões | ✅ | ✅ criar (na área vazia, na pasta, a partir da conexão), subpastas, renomear, apagar, mover a conexão por menu. Arrastar e soltar ⬜ |
| Túnel SSH | ✅ | 🟡 pelo `ssh` do sistema, chave ou agente; senha digitada não (ADR 0021). Túnel real **não conferido**: sem servidor SSH na máquina de teste |
| Proxy SOCKS | ✅ | ✅ SOCKS5 com autenticação; conferido contra proxy real, com TLS por dentro |
| TLS/SSL | ✅ | ✅ Schannel no Windows; Linux pendente (ADR 0009) |
| Teste de conexão antes de salvar | ✅ | ⬜ |
| Conexão somente-leitura | ✅ | ⬜ |
| ~50 SGBDs | ✅ | ➖ 2 de 5 previstos na v1 |

## 2. Navigator (46 comandos no DBeaver)

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Árvore única: conexão → bancos → schemas → objetos | ✅ | ✅ ADR 0018 |
| Outros bancos do servidor (Show all databases) | ✅ | ✅ sessão por banco |
| Roles, Extensions, Storage, System Info, Administer | ✅ | ✅ com editor, criar, renomear e apagar; Session/Lock Manager com cancelar consulta e encerrar sessão |
| Árvore de schemas e tabelas | ✅ | ✅ |
| Colunas com tipo, PK e nullable | ✅ | ✅ |
| Carregamento tardio (lazy) | ✅ | ✅ |
| Tamanho e contagem estimada de linhas | ✅ | ✅ |
| Distinção visual de views | ✅ | ✅ |
| Índices, constraints, triggers, sequences | ✅ | ✅ |
| Funções e procedures | ✅ | ✅ |
| Filtro/busca na árvore | ✅ | ✅ |
| Menu de contexto (View, View Data, Create New, Rename, Delete, Tools) | ✅ | ✅ em todo nó de objeto |
| Esconder pasta que o SGBD não tem | ✅ `visibleIf` | ✅ |
| Editor de objeto (duplo clique / `F4`): Properties, seções, Data | ✅ | ✅ 26 tipos — `docs/OBJECT-EDITOR.md`, ADR 0022 |
| Criar/alterar/remover objeto | ✅ | ✅ 19 tipos com diálogo; os demais por SQL, como no DBeaver |
| Permissões (GRANT/REVOKE por caixa) | ✅ | ✅ sem `With Hierarchy` |
| Tools: Analyze, Vacuum, Truncate, Refresh MView, Enable/Disable trigger | ✅ | ✅ mais Reindex |
| Backup / Restore | ✅ | ✅ `pg_dump` / `pg_restore` / `psql` (ADR 0021); sem Global Backup |
| Confirmação antes de DDL destrutivo | ✅ aba Persist | ✅ |
| Gerar DDL | ✅ | ✅ |
| Copiar nome qualificado | ✅ | ✅ |
| Navegar para referência (FK) | ✅ | ✅ na grade (Navigate link) e na seção References do editor |
| Arrastar tabela para o editor | ✅ | ✅ |
| Renomear objeto | ✅ | ✅ `F2`, em todo tipo que o servidor deixa renomear |

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
| Comentar/descomentar (`Ctrl+/`, bloco `Ctrl+Shift+/`) | ✅ | ✅ |
| Plano de execução (`Ctrl+Shift+E`) | ✅ | ✅ ADR 0013 |
| Histórico de queries | ✅ | 🟡 painel Queries mostra a sessão atual |
| Templates/snippets | ✅ | 🟡 os cinco modelos padrão; variáveis são marcadores simples |
| Ir para declaração (`F4`) | ✅ | 🟡 no editor SQL revela o objeto na árvore; na árvore, `F4` abre o editor do objeto |
| Terminal SQL | ✅ | ✅ |
| Executar em nova aba de resultado (`Ctrl+\`) | ✅ | ✅ |
| Executar consultas em abas separadas | ✅ | 🟡 em sequência, numa conexão só |
| Variáveis (`@set`, `${nome}`) e painel | ✅ | ✅ |
| Saída do servidor (`Ctrl+Shift+O`) | ✅ | 🟡 NOTICE do PostgreSQL; MySQL não |
| Estrutura do script (outline) | ✅ | ✅ |
| Ir para a linha (`Ctrl+L`) | ✅ | ✅ |
| Transformar em lista delimitada | ✅ | ✅ |
| Maiúsculas/minúsculas, aparar espaços | ✅ | ✅ |
| Contar linhas / todas as linhas / avaliar expressão | ✅ | ✅ |
| Alternar, maximizar e trocar de painel; disposição lado a lado | ✅ | ✅ |
| Barra lateral, menu "SQL Editor" e menu de contexto | ✅ | ✅ |
| Perfis de atalho (DBeaver / C-Otter) | 🟡 esquemas do Eclipse | ✅ ADR 0019 |

Comando a comando, com as teclas: [`EDITOR-COMMANDS.md`](EDITOR-COMMANDS.md) — 66
mapeados, 51 ✅, 9 🟡, 2 ⬜ (vários resultados numa aba, painéis nas abas de resultado),
4 ➖.
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
| Inserir/duplicar/excluir linha | ✅ | ✅ 5 de 7 comandos — ver abaixo |
| Salvar alterações | ✅ | ✅ em transação |
| Filtro por coluna | ✅ | ✅ no servidor |
| Visão de registro único | ✅ | ✅ `Tab`, com nome/valor/tipo e a chave marcada |
| Agrupamento e subtotais (ADR 0005) | ✅ | ✅ |
| Linha de totais | ✅ | ✅ |
| **Barra na célula (sparkline)** | ➖ não tem | ✅ 3 ancoragens (ADR 0005) |
| Pivot | ✅ | ✅ local e no servidor |
| Formatação condicional | ✅ | ✅ 12 operadores, célula ou linha, mapa de calor |
| Editores de valor (JSON, hex, booleano) | ✅ | 🟡 visualização; edição segue na célula |
| Copiar como CSV/Markdown/SQL | ✅ | ✅ via exportação |
| Exportar resultado | ✅ | ✅ CSV, JSON, Markdown, INSERT, HTML, XML, TXT |
| Exportar a consulta inteira (não só a página) | ✅ | ✅ por cursor, com progresso e cancelar — 2 M de linhas em ~5 s |
| Importar CSV numa tabela | ✅ | ✅ mapeamento de colunas, truncate, transação única |
| Exportar/importar XLSX; transferir entre bancos | ✅ | ⬜ |
| Paginação / carregar mais | ✅ | ✅ ADR 0011 |
| Gráficos do resultado | ✅ | ➖ |

## 5. Transações

| Funcionalidade | DBeaver | C-Otter |
|---|---|---|
| Autocommit on/off | ✅ | ✅ |
| Commit / rollback manual | ✅ | ✅ `Ctrl+Shift+C` / `Ctrl+Shift+R` |
| Savepoints | ✅ | 🟡 no driver (verificado contra PostgreSQL 18.2), sem UI |
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
| Conexão | 16 | 11 | 1 | 4 |
| Navigator | 24 | 24 | 0 | 0 |
| Editor SQL | 37 | 31 | 5 | 1 |
| Grade | 23 | 19 | 1 | 3 |
| Transações | 5 | 3 | 1 | 1 |
| Diagnóstico | 8 | 5 | 1 | 2 |
| **Total (sem os itens ➖)** | **113** | **93** | **9** | **11** |

**97,5 de 113 itens** desta tabela estão prontos (contando parciais como
meio). Recontado em 2026-10-01 varrendo as tabelas acima; os ✅ novos foram
conferidos na aplicação rodando e contra o servidor (`docs/OBJECT-EDITOR.md`).

> ### ⚠️ Isso não é 82% de paridade
>
> O denominador acima são os **90 itens que este documento escolheu listar**, não
> os que o DBeaver tem. Medido assim, o número sobe sempre que se conclui um item
> da própria lista — e sobe também se a lista encolher. É o defeito que a
> diretriz 4 do `CLAUDE.md` nomeia, e que já fez `ELEMENTS.md` reportar 63%
> quando a cobertura real era 2,5%.
>
> Contra o denominador do DBeaver, extraído por `tools/map_dbeaver.py` dos
> `plugin.xml` de `D:\Tootega\Source\dbeaver` (ver `docs/DBEAVER-MAP.md`):
>
> | | DBeaver | C-Otter | |
> |---|---|---|---|
> | Comandos | 281 | 191 | **68,0%** |
> | Atalhos | 147 | 116 | **78,9%** |
> | Tipos de objeto do PostgreSQL com editor | 27 gerenciadores | 26 | **96%** |
> | Pastas da árvore (PostgreSQL) | 59 | 59 | **100%** |
> | Listas de itens da árvore | 62 | 55 | **89%** |
> | SGBDs | ~50 | 2 | **4%** |
>
> Os 62/76 respondem *"o que esta lista promete está feito?"*. Não respondem
> *"quanto falta para o DBeaver?"* — para isso valem os números do quadro.

Este inventário lista *funcionalidades*, não *comandos* — "copiar célula como
Markdown" é uma variação de "copiar", e aqui elas contam como uma. Isso explica
a distância entre 76 itens e 270 comandos, mas não a elimina: os 270 são o alvo.

Para o inventário elemento a elemento — cada botão, menu e atalho, com estado verificado na
aplicação rodando — ver [`ELEMENTS.md`](ELEMENTS.md), `docs/MYSQL-MAP.md`
(21 de 27 nós da árvore MySQL) e `docs/NAVIGATOR-TREE.md` (59 de 59 pastas no PostgreSQL).

### Atualização de 2026-10-01

Entraram os comandos de aplicação, banco e navegador (`docs/ELEMENTS.md`, seção 15):
**68,0%** dos comandos e **78,9%** dos atalhos do DBeaver, medidos por
`python tools/regen_docs.py` contra os `plugin.xml` — não contra o próprio produto.

O que falta para 100% está nominalmente em `docs/DBEAVER-MAP.md` ("Comandos que
faltam"). São 90 comandos; a maior parte é de plugins fora do escopo decidido:
tarefas, diagramas ER, comparação e busca no banco (excluídos pelo usuário), Git,
IA (ADR 0004), outros SGBDs, ferramentas de desenvolvimento, projetos/recursos do
Eclipse e telas de licença/atualização. Dentro do escopo ainda faltam: múltiplos
resultados na mesma aba, alguns pulldowns de barra, e os itens ⬜ de
`docs/OBJECT-EDITOR.md` (backup global, FDW, XLSX, edição de objetos no MySQL).

### Perfil SQL Server (2026-10-01)

Terceiro SGBD. O denominador é o plugin `org.jkiss.dbeaver.ext.mssql`, contado em
`docs/MSSQL-MAP.md`: **21 de 30** pastas da árvore (mais 2 parciais), **10 de 16**
managers de edição (mais 3 parciais) e **2 de 8** modelos de autenticação. O que o
C-Otter tem a mais ali — aba Permissions, submenu Tools, Lock Manager, dashboard — não
entra na conta: cobertura é contra o DBeaver.

### Perfil SQL Anywhere (2026-10-01)

Quarto SGBD. O DBeaver Community não tem plugin dele: o denominador é a árvore
**genérica** que ele mostra pelo driver "Sybase jConnect", contada em
`docs/SQLANYWHERE-MAP.md` — **14 de 17** pastas aplicáveis (mais 2 parciais). Editor de
objeto, formulários, permissões, ferramentas, sessões e as pastas do Sybase Central
(usuários, papéis, eventos, dbspaces, servidores remotos…) o DBeaver não tem para o
Sybase, e por isso não entram na conta: cobertura é contra o DBeaver.

## Próximos passos, por impacto

> **2026-10-01:** os itens 2 a 4 abaixo foram concluídos na fase da grade
> (`docs/EDITOR-COMMANDS.md`: 63 dos 70 comandos da grade iguais ao DBeaver) e ficam
> como registro. O que falta de fato, por área, está nas linhas ⬜ e 🟡 de
> [`OBJECT-EDITOR.md`](OBJECT-EDITOR.md): túnel SSH conferido contra servidor real,
> Global Backup, transferência entre bancos, XLSX, `With Hierarchy`, chaves virtuais.

1. **TLS no Linux** — `lib/net/tls_openssl.cpp` ainda é esboço, e
   `tls_available()` responde `false` lá: a caixa "usar SSL" aparece desligada
   em vez de falhar na conexão
2. **Atalhos de teclado da grade** — 5 de 47, mais a navegação por setas.
   Mapa completo em `docs/GRID-KEYS.md`.

3. **Comandos de linha que faltam** — o DBeaver tem **sete**, extraídos de
   `plugin.xml:1073-1079` do `ui.editors.data`; o C-Otter tem três:

   | Comando | Atalho | Estado |
   |---|---|:---:|
   | Add row | `Alt+Insert` | ✅ como "Nova linha", sem atalho |
   | Duplicate row | `Ctrl+Alt+Insert` | ✅ sem atalho |
   | Delete current row | `Alt+Delete` | ✅ sem atalho |
   | Add row (insert before) | `Shift+Alt+Insert` | ⬜ |
   | Duplicate row (insert before) | `Ctrl+Shift+Alt+Insert` | ⬜ |
   | Copy from row above | `Ctrl+D` | ✅ sem atalho |
   | Copy from row below | `Ctrl+Alt+D` | ✅ sem atalho |

   As variantes "insert before" dependem de a linha nova ter POSIÇÃO. Hoje as
   inserções vão sempre para o fim, num vetor separado — mudá-las exige
   decidir como a grade ordena linha nova no meio de uma página paginada.
4. **Editar no painel de valor** — hoje ele só mostra; JSON e texto longo
   mereciam edição multilinha, que a célula não comporta
5. **Savepoints na UI** — o driver já os implementa

~~TLS no MySQL e no PostgreSQL~~ — **concluído em 2026-09-21** no Windows.
Verificado contra o MySQL local (`spikes/dbeaver_import/tls_live.cpp`): o modo
`require` negocia TLS 1.2 com `TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384` e
`verify-full` recusa o certificado autoassinado. **O caminho do PostgreSQL
(`SSLRequest`) compila e segue o protocolo, mas o aperto de mão NÃO foi
verificado contra servidor.** Em 2026-09-30 o perfil voltou a existir, e o
`spike_tls_live` mostrou o motivo: o PostgreSQL local roda com `ssl = off`. O
cliente lê a resposta `'N'` ao `SSLRequest` e recusa o `require`, como deve;
negociar TLS exige ligar o SSL no servidor.

~~Ligar `analyze_scope` à UI~~ — **concluído em 2026-09-21**.
~~Grade editável, transações, paginação, ordenação, filtro, abas, arquivo~~ —
**concluídos em 2026-09-21**.

## Manutenção deste arquivo

Toda funcionalidade concluída deve mudar de ⬜ para ✅ **no mesmo commit** que a implementa.
Um inventário desatualizado é pior que nenhum: dá falsa confiança.
