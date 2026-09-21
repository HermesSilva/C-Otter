# Análise: DBeaver como referência arquitetural

Medições feitas em `D:\Tootega\Source\dbeaver` em 2026-09-20.

## 1. Dimensão do código-fonte

| Métrica | Valor |
|---------|-------|
| Plugins (bundles OSGi) | 156 |
| Arquivos `.java` | 6.474 |
| Linhas de Java | 916.759 |
| Arquivos `plugin.xml` (extension points) | 149 |
| Arquivos dependentes de SWT/JFace/Eclipse UI | 1.620 (~334.817 linhas, **37% do total**) |
| Arquivos dependentes de `java.sql` / `javax.sql` | 756 |

### Distribuição por peso (top 15)

| Plugin | Linhas | Papel |
|--------|--------|-------|
| `org.jkiss.dbeaver.model` | 113.535 | Núcleo de abstrações (DBP/DBS/DBC/DBD/DBR) |
| `org.jkiss.dbeaver.ui.editors.data` | 61.863 | Grid de resultados, edição de valores |
| `org.jkiss.dbeaver.ui.editors.sql` | 46.991 | Editor SQL, autocomplete, execução |
| `org.jkiss.dbeaver.ext.postgresql` | 41.619 | Driver PostgreSQL |
| `org.jkiss.dbeaver.model.sql` | 40.992 | Parser, formatter, semântica SQL |
| `org.jkiss.dbeaver.ui` | 40.146 | Widgets base, diálogos |
| `org.jkiss.dbeaver.ui.navigator` | 31.469 | Árvore de navegação de metadados |
| `org.jkiss.dbeaver.ext.db2` | 28.191 | Driver DB2 |
| `org.jkiss.dbeaver.registry` | 27.770 | Registro de datasources e drivers |
| `org.jkiss.dbeaver.ext.oracle` | 26.143 | Driver Oracle |
| `org.jkiss.dbeaver.model.jdbc` | 24.180 | Ponte JDBC → modelo |
| `org.jkiss.dbeaver.ui.editors.erd` | 20.491 | Diagramas ER |
| `org.jkiss.dbeaver.ui.editors.connection` | 20.327 | Assistentes de conexão |
| `org.jkiss.dbeaver.model.ai` | 19.721 | Integração com LLMs |
| `org.jkiss.dbeaver.core` | 17.092 | Bootstrap da aplicação |

Dentro de `model.sql`, o subpacote `semantics` sozinho tem **19.995 linhas** — é o analisador
semântico que sustenta autocomplete e realce. É o componente mais subestimado do sistema.

## 2. Princípios arquiteturais herdados

Do `AGENTS-Architecture.md` do DBeaver, três regras valem a pena preservar:

1. **Separação model/UI rígida.** Bundles de modelo nunca dependem de SWT/JFace. Nenhum SQL
   é executado a partir da camada de UI. Em C-Otter isso vira uma fronteira de link: as
   bibliotecas de núcleo não podem referenciar ImGui — verificável no build.

2. **Design por extension points.** Cada driver contribui capacidades via `plugin.xml`, sem
   o núcleo conhecê-los. Em C-Otter isso vira um registry com interfaces virtuais puras e
   auto-registro estático por driver.

3. **Nomenclatura por camada.** O prefixo diz a camada:

   | Prefixo | Camada | Contagem de tipos |
   |---------|--------|-------------------|
   | `DBP*` | Capacidade de plataforma | 150 |
   | `DBS*` | Estrutura/metadados | 117 |
   | `DBD*` | Valores e formatação de dados | 85 |
   | `DBC*` | Conectividade e execução | 63 |
   | `DBE*` | Edição de objetos | 32 |
   | `DBR*` | Runtime (progresso, jobs) | 24 |

   Total: **471 interfaces** só no núcleo. Este é o contrato a portar, e a maior parte delas
   existe por causa da tipagem nominal do Java — em C++20 boa parte colapsa em concepts e
   em composição, o que é a principal fonte de redução de tamanho.

## 3. As três barreiras da conversão

### 3.1 JDBC não existe em C++

756 arquivos falam `java.sql`. O JDBC entrega uma API uniforme (`Connection`, `Statement`,
`ResultSet`, `DatabaseMetaData`) que em C++ não tem equivalente universal. A decisão tomada
é **drivers nativos**: `libpq`, `libmariadb`, `sqlite3`, ODBC para MSSQL, OCI para Oracle.

Consequência: a camada `model.jdbc` (24.180 linhas) não é portada — é **substituída** por
uma abstração própria (`otter::db`) cujo contrato é definido pelo que o núcleo precisa, não
pelo que o JDBC oferece. Isso é uma vantagem: JDBC força materialização de `ResultSet` em
objetos; `libpq` em modo binário com `PQsetSingleRowMode` entrega dados sem essa taxa.

`DatabaseMetaData` é o ponto mais doloroso: cada driver nativo expõe metadados de forma
diferente (catálogos do sistema no Postgres, `information_schema` no MySQL, `ALL_*` no
Oracle). O DBeaver já resolve isso — os plugins `ext.*` contêm justamente as consultas de
metadados específicas. **Essas queries SQL são portáveis diretamente**, e são o ativo de
maior valor por linha em todo o repositório.

### 3.2 Eclipse RCP / SWT: 335 mil linhas

37% do código está acoplado ao Eclipse. Isso inclui workbench, perspectivas, views, editores,
commands, preferências e o `NavigatorTree`. Nada disso se porta — reimplanta-se.

Com Dear ImGui (modo imediato), o modelo muda de fundo: não há hierarquia de widgets com
estado nem listeners. Um grid de um milhão de linhas vira um loop virtualizado sobre um
buffer colunar. Isso elimina praticamente toda a complexidade de `editors.data` (61.863
linhas) que existe para gerenciar ciclo de vida de células SWT. Estimo que 335k linhas de UI
Java correspondam a **40–60k linhas de C++/ImGui** para funcionalidade equivalente no escopo
da v1.

Custo do modo imediato: sem acessibilidade nativa, sem IME robusto de graça, sem look nativo,
e edição de texto rica (o editor SQL) precisa ser construída à mão. O editor SQL com realce
e autocomplete é o item de UI mais caro da v1.

### 3.3 OSGi: carregamento dinâmico de plugins

O DBeaver resolve dependências e carrega bundles em runtime. C-Otter não precisa disso na v1:
**link estático com registry em tempo de compilação**. Drivers se auto-registram via objeto
estático global. Se plugins dinâmicos forem necessários depois, entram como DLL/`.so` com uma
ABI C estável — e aí a escolha de C++20 puro no núcleo compensa, porque a fronteira já é
estreita.

## 4. O que se porta, o que se reescreve, o que se descarta

| Categoria | Origem | Tratamento |
|-----------|--------|------------|
| Consultas SQL de metadados (`ext.*`) | ~15k linhas úteis | **Portar quase literal** — maior valor/esforço |
| Dialetos SQL (keywords, quoting, tipos) | `model.sql/registry` | **Portar como tabelas de dados** |
| Modelo de estrutura (DBS*) | 117 interfaces | **Redesenhar** em C++20, colapsando hierarquia |
| Parser/semântica SQL | 19.995 linhas | **Reescrever** — recursive descent próprio |
| Formatter SQL | `model.sql/format` | Reescrever, lógica é portável |
| Ponte JDBC | 24.180 linhas | **Descartar** — substituída por `otter::db` |
| Toda a camada UI | 334.817 linhas | **Reescrever** em ImGui |
| ERD, AI, GIS, dashboards, office | ~50k linhas | **Fora da v1** |
| ~40 drivers de nicho | ~120k linhas | **Fora da v1** (ODBC genérico cobre parcialmente) |

## 5. Estimativa de tamanho do alvo

| Módulo C-Otter | Linhas estimadas |
|----------------|------------------|
| `otter::base` (platform, io, threads, logging) | 8.000 |
| `otter::db` (abstração de conexão/execução) | 6.000 |
| `otter::meta` (modelo de estrutura) | 12.000 |
| `otter::sql` (lexer, parser, semântica, formatter) | 25.000 |
| `otter::drivers` (5 drivers) | 30.000 |
| `otter::registry` (datasources, credenciais, config) | 8.000 |
| `otter::transfer` (import/export) | 7.000 |
| `otter::ui` (ImGui, grid, editor, navigator) | 45.000 |
| Testes | 25.000 |
| **Total** | **~166.000 linhas** |

Contra 917k de Java — redução de ~82%, coerente com o escopo recortado (sem 40 drivers, sem
ERD/AI/GIS) e com a densidade maior de C++20 frente a Java com OSGi.

## 6. Onde o ganho de performance realmente está

Não é "C++ é mais rápido que Java" — a JVM com JIT compete bem em código de negócio. O ganho
vem de quatro pontos estruturais:

1. **Startup.** DBeaver leva segundos para subir JVM + resolver OSGi. C-Otter é um binário:
   alvo de < 200 ms até janela utilizável.
2. **Memória por linha.** JDBC materializa cada valor como objeto no heap. Armazenamento
   colunar com arena por fetch elimina alocação por célula e a pressão de GC.
3. **Fetch binário.** `libpq` em modo binário evita o round-trip texto→parse→objeto.
4. **Grid em modo imediato.** Sem árvore de widgets, o custo de render é O(células visíveis),
   independente do tamanho do resultado.

## 7. Riscos identificados

| Risco | Severidade | Mitigação |
|-------|-----------|-----------|
| Editor SQL em ImGui (texto rico é caro) | Alta | Protótipo na Fase 0; plano B é integrar um componente de edição dedicado |
| Oracle OCI e cliente MSSQL: setup pesado e licenças | Média | ODBC como caminho padrão para ambos; nativo só se medir ganho |
| 471 interfaces de modelo geram over-engineering se portadas 1:1 | Alta | Redesenhar a partir dos casos de uso, nunca traduzir interface a interface |
| Encoding/charset por SGBD (UTF-8, Latin-1, UTF-16) | Média | UTF-8 internamente sempre; conversão na fronteira do driver |
| Sem acessibilidade no modo imediato | Média | Aceito na v1; documentado como limitação conhecida |
