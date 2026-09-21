# C-Otter — Carga de trabalho

**Base da estimativa:** profissionais de nível médio (3–6 anos de C++), padrão de mercado,
sem heroísmo. Unidade: **homem-hora (h/h)** de trabalho efetivo — não horas de calendário.

**Critério de pronto:** paridade funcional verificável contra o DBeaver no escopo acordado
(núcleo + 5 drivers + UI). Ver §6 para o protocolo de comparação.

---

## 1. Premissas explícitas da estimativa

Uma estimativa sem premissas declaradas é um número inventado. Estas são as minhas:

| Premissa | Valor | Impacto se errada |
|----------|-------|-------------------|
| Produtividade em C++ de sistemas, código novo | **12–18 linhas/hora** líquidas | É o maior fator. Inclui design, teste, debug e revisão |
| Código portável (queries de metadados) | **40–60 linhas/hora** | Tradução mecânica com validação |
| Proporção de teste sobre código de produção | 35% do esforço | Abaixo disso, o custo reaparece na Fase 5 dobrado |
| Overhead de revisão/integração | 15% | Já embutido nos números por módulo |
| Perda por troca de contexto e reuniões | **não incluída** | Adicione 10–20% se o time for compartilhado |

A faixa de 12–18 linhas/hora é o consenso da indústria para código de sistemas novo com
qualidade de produção. Quem cita 50–100 linhas/hora está medindo digitação, não engenharia.

**Ancoragem de realidade:** os 5 drivers-alvo do DBeaver somam 101.288 linhas de Java
(postgresql 41.619, oracle 26.143, mysql 15.823, mssql 15.322, sqlite 2.381) e representam
mais de uma década de trabalho acumulado de uma equipe. C-Otter não reproduz isso em 166k
linhas por ser melhor — reproduz porque recorta escopo e porque as queries de metadados,
que são o ativo real, são portáveis.

---

## 2. Decomposição por módulo

### 2.1 `otter_base` — fundação (8.000 linhas)

| Tarefa | h/h | Nota |
|--------|-----|------|
| Camada de OS (arquivo, mmap, socket, path, dylib) × 2 plataformas | 120 | Duas implementações reais, não um wrapper fino |
| Arena allocator + estratégias de memória | 60 | Decisão de performance central |
| Thread pool + jobs com cancelamento cooperativo | 90 | Cancelamento é a parte difícil |
| UTF-8: iteração, validação, conversão de fronteira | 70 | Inclui UTF-16 do Win32 |
| Logging, config, tipo `Error`, `expected` | 50 | |
| Build system, presets, CI Windows+Linux | 80 | Sanitizers, warnings-as-errors |
| Testes unitários | 130 | |
| **Subtotal** | **600** | |

### 2.2 `otter_db` — abstração de conexão (6.000 linhas)

| Tarefa | h/h | Nota |
|--------|-----|------|
| Design do contrato (Holt/Float/Statement/ResultSet) | 80 | Substitui JDBC; erro aqui custa caro depois |
| `ResultSet` colunar + bitmap de nulos + arena | 120 | Núcleo da vantagem de performance |
| Sistema de tipos e mapeamento cross-SGBD | 110 | Subestimado com frequência: decimais, intervalos, arrays, LOBs |
| Registry de drivers, capabilities, pool de conexões | 90 | |
| Transações, savepoints, cancelamento | 70 | |
| Testes | 130 | |
| **Subtotal** | **600** | |

### 2.3 `otter_meta` — modelo de estrutura (12.000 linhas)

| Tarefa | h/h | Nota |
|--------|-----|------|
| `MetaNode`, PropertyBag, carregamento lazy | 130 | Colapsa as 117 interfaces `DBS*` |
| Pocket Rock: cache persistente + invalidação | 140 | Serialização e versionamento de schema |
| Grafo de dependências entre objetos | 90 | Necessário para DDL e refresh correto |
| Refresh incremental e notificação de mudança | 80 | |
| Testes | 160 | |
| **Subtotal** | **600** | |

### 2.4 `otter_sql` — lexer, parser, semântica (25.000 linhas)

O módulo mais denso do projeto. Custa mais por linha que qualquer outro.

| Tarefa | h/h | Nota |
|--------|-----|------|
| Lexer dirigido por dialeto | 110 | |
| **Splitter de script** | 130 | `$$`, `DELIMITER`, `BEGIN/END`, strings aninhadas. Parece trivial, não é |
| Parser recursive-descent tolerante a erro | 420 | Tolerância a erro é o que o torna caro: precisa parsear texto incompleto |
| Análise semântica (resolução de nomes contra metadados) | 380 | Equivale às 19.995 linhas de `semantics` do DBeaver |
| Formatter SQL | 120 | |
| Tabelas de dialeto × 5 SGBDs | 150 | Portável do DBeaver, majoritariamente dados |
| Testes + fuzzing (libFuzzer) | 390 | Parser exposto a entrada arbitrária exige fuzzing |
| **Subtotal** | **1.700** | |

### 2.5 `otter_drivers` — 5 drivers nativos (30.000 linhas)

O esforço **não** é proporcional às linhas do DBeaver: a maior parte daquelas linhas é
infraestrutura JDBC que aqui não existe. O que importa é a complexidade do SGBD.

| Driver | h/h | Justificativa |
|--------|-----|---------------|
| **PostgreSQL** (libpq) | 420 | Primeiro driver: paga o custo de provar a abstração. Fetch binário, tipos ricos (arrays, JSONB, ranges), `pg_catalog` extenso |
| **SQLite** (embarcado) | 140 | Sem rede, tipagem dinâmica, catálogo simples. O mais barato |
| **MySQL/MariaDB** (libmariadb) | 260 | `information_schema`, quirks de charset e collation, diferenças MySQL vs MariaDB |
| **ODBC/MSSQL** | 340 | Duplo: camada ODBC genérica (reutilizável) + especialização MSSQL |
| **Oracle** (OCI) | 420 | API mais hostil do conjunto. Setup de cliente, `ALL_*`/`DBA_*`, tipos proprietários, PL/SQL |
| Testes de integração (Docker, matriz de versões) | 420 | 5 SGBDs × múltiplas versões |
| **Subtotal** | **2.000** | |

### 2.6 `otter_registry` — conexões e credenciais (8.000 linhas)

| Tarefa | h/h | Nota |
|--------|-----|------|
| Modelo de Holt, Raft, pastas, serialização | 120 | |
| Credenciais: DPAPI (Windows) + libsecret (Linux) | 130 | Segurança: não improvisar, é superfície de ataque |
| Túnel SSH e TLS | 150 | Requisito real de mercado; custa mais do que parece |
| Preferências em camadas (global/conexão) | 60 | |
| Testes | 140 | |
| **Subtotal** | **600** | |

### 2.7 `otter_transfer` — import/export (7.000 linhas)

| Tarefa | h/h | Nota |
|--------|-----|------|
| Pipeline de transferência (stream, backpressure) | 110 | |
| Export: CSV, JSON, SQL, Markdown | 130 | |
| Import CSV: detecção de tipo, mapeamento, erros | 160 | Import é ~2× o custo de export |
| Geração de DDL a partir de metadados | 100 | |
| Testes | 150 | |
| **Subtotal** | **650** | |

### 2.8 `otter_ui` — Dear ImGui (45.000 linhas)

Maior módulo em linhas e o de maior risco.

| Tarefa | h/h | Nota |
|--------|-----|------|
| Backends (Win32+DX11, GLFW+GL3), loop, docking, temas | 220 | |
| **Editor SQL** (realce incremental, autocomplete, multi-cursor, undo) | **700** | **Item de maior risco do projeto.** Texto rico em modo imediato é construção manual |
| **Grid virtualizado** (1M linhas, seleção, edição inline, congelar colunas) | 560 | Vantagem de performance central |
| Editores de valor (texto, hex, data/hora, JSON, LOB) | 260 | |
| Navigator: árvore lazy sobre MetaNode | 190 | |
| Raft view: gerenciar conexões, assistentes | 230 | |
| Diálogos, preferências, atalhos, i18n | 210 | |
| Progresso, cancelamento, notificação de erro na UI | 120 | |
| Testes de UI + verificação visual | 310 | Difícil de automatizar; parte é manual |
| **Subtotal** | **2.800** | |

### 2.9 Transversal

| Tarefa | h/h | Nota |
|--------|-----|------|
| Arquitetura, ADRs, spikes da Fase 0 | 240 | Os 3 spikes valem sozinhos ~150 h/h |
| Benchmarks automatizados + detecção de regressão | 160 | Metas sem benchmark são opinião |
| Empacotamento: MSI, AppImage/deb, assinatura | 180 | Sempre subestimado |
| Documentação de usuário e de API | 140 | |
| Hardening final, perfilamento, correção de bugs | 330 | |
| **Subtotal** | **1.050** | |

---

## 3. Totais

| Módulo | h/h | % |
|--------|-----|---|
| `otter_base` | 600 | 5,1% |
| `otter_db` | 600 | 5,1% |
| `otter_meta` | 600 | 5,1% |
| `otter_sql` | 1.700 | 14,5% |
| `otter_drivers` | 2.000 | 17,1% |
| `otter_registry` | 600 | 5,1% |
| `otter_transfer` | 650 | 5,6% |
| `otter_ui` | 2.800 | 23,9% |
| Transversal | 1.050 | 9,0% |
| **Subtotal técnico** | **10.600** | 90,6% |
| Contingência 25% (risco normal de projeto) | 2.650 | |
| **TOTAL** | **≈ 13.250 h/h** | |

**Faixa realista: 11.000 – 16.000 h/h.** Use 13.250 como número de planejamento.

Validação cruzada: 166.000 linhas ÷ 13.250 h = **12,5 linhas/hora**, dentro da faixa
esperada para C++ de sistemas. A estimativa é internamente consistente.

---

## 4. Tradução para calendário

| Configuração | Cálculo | Calendário |
|--------------|---------|------------|
| 1 dev, integral (40h/sem úteis ≈ 32h efetivas) | 13.250 ÷ 32 | **~8 anos** — inviável |
| 2 devs | + 15% overhead de coordenação | ~4,2 anos |
| 4 devs | + 30% overhead | ~2,4 anos |
| 6 devs | + 45% overhead | ~1,9 ano |
| 8 devs | + 60% overhead | ~1,8 ano ← **ponto de saturação** |

**Acima de 6 pessoas o retorno praticamente cessa.** O caminho crítico
`base → db → sql → ui/editor` é inerentemente serial: o editor SQL depende da semântica, que
depende do parser, que depende do lexer e dos metadados. Adicionar gente não encurta essa
corrente.

**Configuração recomendada: 4–5 desenvolvedores, ~2 a 2,5 anos.**

### Composição sugerida do time

| Perfil | Qtd | Responsabilidade |
|--------|-----|------------------|
| Sênior de sistemas C++ | 1 | Arquitetura, `base`, `db`, revisão. Caminho crítico |
| Pleno de linguagens/compiladores | 1 | `sql` integral — é especialidade, não tarefa genérica |
| Pleno de UI/gráficos | 1–2 | `ui`, com foco no editor e no grid |
| Pleno de banco de dados | 1 | Drivers, metadados, testes de integração |

O item `otter_sql` exige alguém com experiência em parsers. Alocar um generalista ali é a
forma mais comum de estourar a estimativa neste tipo de projeto.

---

## 5. Sequenciamento e paralelização

```
Mês 0-2   [SERIAL]     base + spikes Fase 0        ← bloqueia todo o resto
Mês 2-8   [PARALELO]   db+drv_postgres │ sql(lexer/parser) │ ui(grid+editor)
Mês 8-14  [PARALELO]   meta+registry   │ sql(semântica)    │ ui(navigator+raft)
Mês 14-20 [PARALELO]   drivers 2..5    │ transfer          │ ui(edição+valores)
Mês 20-26 [CONVERGE]   integração, benchmarks, hardening, empacotamento
```

Os dois gargalos reais: **`base` no início** (ninguém trabalha antes dele) e **integração no
fim** (não paraleliza). Por isso os 25% de contingência não são gordura — são o custo da
convergência final, historicamente o que mais estoura.

---

## 6. "Entregar rodando com o DBeaver" — protocolo de aceite

Paridade precisa ser verificável, não declarada. Proponho uma suíte de comparação lado a
lado, executada na CI a partir da Fase 1:

### 6.1 Paridade funcional

Para cada SGBD-alvo, o mesmo roteiro nos dois produtos:

| # | Cenário | Critério |
|---|---------|----------|
| 1 | Conectar (senha, SSL, túnel SSH) | Mesmo resultado |
| 2 | Navegar até coluna de tabela | Mesma árvore, mesmos atributos |
| 3 | `SELECT` com 1M linhas | Mesmos dados, mesma formatação de tipos |
| 4 | Editar célula e salvar | Mesmo SQL gerado |
| 5 | Transação: begin, alterar, rollback | Mesmo estado final |
| 6 | Cancelar query longa | Cancela de fato, conexão sobrevive |
| 7 | Gerar DDL de uma tabela | DDL equivalente (normalizado) |
| 8 | Export CSV e reimport | Round-trip sem perda |
| 9 | Autocomplete em query com JOIN | Mesmas sugestões no mesmo contexto |
| 10 | Script multi-statement com `$$`/`DELIMITER` | Mesmo particionamento |

### 6.2 Paridade de performance

Mesma máquina, mesmo SGBD, medição automatizada:

| Métrica | Alvo C-Otter | DBeaver | Razão exigida |
|---------|-------------|---------|---------------|
| Cold start | < 200 ms | 3–8 s | ≥ 15× |
| Fetch 1M × 10 col | < 2 s | 15–40 s | ≥ 7× |
| RSS com 1M linhas | < 350 MB | 1,5–3 GB | ≥ 4× |
| Scroll no grid | 144 fps | 15–60 fps | ≥ 2,4× |
| Instalador | < 25 MB | ~200 MB + JRE | ≥ 8× |

Custo desta suíte: **~180 h/h**, já contabilizado em "Transversal".

---

## 7. Cenários de escopo — o que cada corte economiza

| Cenário | h/h | vs. total | O que você perde |
|---------|-----|-----------|------------------|
| **Núcleo headless + CLI** (sem UI) | 5.700 | −57% | Elimina o módulo mais caro e o maior risco. Vira biblioteca + CLI |
| **1 driver (PostgreSQL) + UI** | 8.900 | −33% | Deixa de ser "universal" |
| **Núcleo + 5 drivers + UI** (acordado) | 13.250 | — | — |
| **Paridade total com DBeaver** | 45.000–60.000 | +240% a +350% | Nada. ~25–35 anos-pessoa |

A última linha merece leitura atenta: **paridade completa com o DBeaver não é um projeto
viável** para um time pequeno. O DBeaver acumula mais de uma década de trabalho coletivo e
~50 SGBDs. O recorte escolhido é o que torna o projeto realizável.

---

## 8. Manutenção recorrente (pós-v1)

O custo não termina no release:

| Item | h/h por ano |
|------|-------------|
| Acompanhar versões de SGBD (5 × ~2 releases/ano) | 400 |
| Correção de bugs reportados | 600 |
| Manutenção de CI, toolchain, dependências | 200 |
| Novos drivers (se houver), ~250 h/h cada | variável |
| **Baseline** | **~1.200 h/h/ano** (≈ 0,75 FTE) |

---

## 9. Onde a estimativa tem maior probabilidade de errar

Ordenado por impacto esperado:

1. **Editor SQL em ImGui (700 h/h)** — se o spike da Fase 0 falhar, ou vira componente de
   terceiros, ou a decisão de UI cai por inteiro. **Variação possível: ±400 h/h.**
2. **Análise semântica (380 h/h)** — autocomplete que realmente funciona sobre SQL real
   costuma custar o dobro do previsto. **±250 h/h.**
3. **Oracle OCI (420 h/h)** — ambiente de cliente e licenciamento podem inviabilizar; o
   fallback é ODBC, mais barato mas menos capaz. **±200 h/h.**
4. **Sistema de tipos cross-SGBD (110 h/h)** — decimais, intervalos, timezones e arrays são
   uma fonte crônica de bugs de cauda longa. **±150 h/h.**
5. **Integração final** — historicamente o maior estouro. É o que a contingência de 25% cobre.

A contingência não é margem de segurança psicológica: ela existe porque os itens 1 e 2 são
genuinamente incertos até que os spikes da Fase 0 sejam executados.
