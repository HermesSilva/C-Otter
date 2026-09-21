# Licenças de terceiros

Este arquivo deve acompanhar qualquer distribuição binária do C-Otter. É a obrigação legal
que as licenças abaixo impõem em troca do uso.

---

## Dear ImGui 1.93 (branch docking)

Toolkit de UI em modo imediato. `third_party/imgui/`
Licença: **MIT** — Copyright (c) 2014-2026 Omar Cornut

---

## GLFW 3.4

Janela, contexto OpenGL e entrada. `third_party/glfw/`
Licença: **zlib/libpng** — Copyright (c) 2002-2006 Marcus Geelnard,
Copyright (c) 2006-2019 Camilla Löwy

Compatível com link estático (ADR 0002). Backend único para todas as plataformas (ADR 0006).

---

## ImGuiColorTextEdit

Widget de edição de código com realce de sintaxe. `third_party/texteditor/`
Licença: **MIT** — Copyright (c) 2024-2026 Johan A. Goossens

Texto integral: `third_party/texteditor/LICENSE`

Inclui `dtl.h` (diff template library) sob **BSD**, usado apenas por `TextDiff.cpp`, que não
compilamos.

**Nota histórica:** Scintilla e Lexilla foram vendorados e removidos em 2026-09-21, quando o
ADR 0007 revogou o ADR 0003. Motivo: Scintilla é um controle nativo por plataforma, o que
contraria o requisito de escrita única.

---

## Atribuição ao DBeaver

O C-Otter é uma reescrita independente, **não** um port de código. Contudo, as consultas SQL
de metadados dos plugins `org.jkiss.dbeaver.ext.*` são portadas (ver `docs/ANALYSIS.md` §4).

O DBeaver é licenciado sob **Apache License 2.0**, que permite obra derivada desde que
preservados o aviso de copyright, o texto da licença e um `NOTICE` indicando as modificações.

**Pendência:** antes da Fase 1, incluir `NOTICE` com a atribuição formal ao projeto DBeaver
e ao Apache 2.0 para as porções derivadas.

---

## Verificação de compatibilidade (ADR 0002 — link estático)

| Componente | Licença | Link estático em produto fechado |
|------------|---------|----------------------------------|
| Dear ImGui | MIT | **Sim** |
| GLFW | zlib/libpng | **Sim** |
| ImGuiColorTextEdit | MIT | **Sim** |
| SQLite | Domínio público | **Sim** |
| PostgreSQL (libpq) | PostgreSQL License | **Sim** |
| ODBC | API do sistema | **Sim** |
| MySQL (libmysqlclient) | GPL | **Não** — bloqueado |
| MariaDB Connector/C | LGPL | **Não** com link estático |
| Qt / QScintilla | LGPL / GPL | **Não** — rejeitado (ADR 0003) |
| GTK | LGPL | **Não** com link estático — evitado pelo ADR 0007 |
| Oracle OCI | Proprietária | Verificar antes da Fase 3 |

**Todo o stack de UI é permissivo (MIT/zlib)** e compatível com link estático — nenhuma
dependência LGPL restou após o ADR 0007.
