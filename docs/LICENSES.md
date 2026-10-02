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

## nanosvg

Leitor e rasterizador de SVG, em dois cabeçalhos. `third_party/nanosvg/`
Licença: **zlib** — Copyright (c) 2013-14 Mikko Mononen

Fixado no commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`. Rasteriza os ícones do
DBeaver no tamanho em que aparecem (ADR 0019).

Texto integral: `third_party/nanosvg/LICENSE.txt`

---

## Ícones do DBeaver

`assets/icons/dbeaver/` — os ícones originais do DBeaver, **sem modificação** (só o nome
do arquivo muda), embutidos no executável por `tools/embed_icons.py`.
Licença: **Apache License 2.0** — Copyright (C) 2010-2025 DBeaver Corp and others

`assets/icons/dbeaver/NOTICE` lista cada arquivo e o caminho de origem, e **acompanha
qualquer distribuição** — é a exigência do Apache 2.0 para obra que redistribui partes.

**Marcas.** Os ícones `pg_server`, `my_server`, `ms_server` e `sa_server` são os logotipos
do PostgreSQL, do MySQL, do SQL Server e da Sybase (hoje SAP — é o que o DBeaver usa para o
driver com que se chega a um SQL Anywhere).
A licença do DBeaver cobre o arquivo, não a marca: o uso aqui é o nominativo — identificar
o SGBD a que uma conexão fala —, o mesmo que o DBeaver faz. Antes de uma distribuição
comercial, conferir a política de marca de cada um (a do MySQL, da Oracle, é a mais
restritiva).

---

## Alteração local no ImGuiColorTextEdit

`third_party/texteditor/C-OTTER-CHANGES.md` lista o que foi alterado no widget (hoje, um
trecho de `handleCharacter`). A licença MIT permite; o registro existe para a alteração
não se perder numa atualização.

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
| nanosvg | zlib | **Sim** |
| Ícones do DBeaver | Apache 2.0 | **Sim**, com o `NOTICE` |
| SQLite | Domínio público | **Sim** |
| PostgreSQL (libpq) | PostgreSQL License | **Sim** |
| ODBC | API do sistema | **Sim** |
| MySQL (libmysqlclient) | GPL | **Não** — bloqueado |
| FreeTDS | LGPL | **Não** com link estático — o TDS é próprio (`lib/tdswire`, ADR 0024) |
| Oracle Instant Client (OCI) | Proprietária | **Não** — só dinâmica; o TNS/TTC é próprio (`lib/orawire`, ADR 0027) |
| python-oracledb (Oracle) | UPL 1.0 / Apache 2.0 | **Sim** — referência de leitura; dele vem só a tabela de tipos `lib/orawire/data_types.inc`, com a origem no cabeçalho |
| go-ora | MIT | Referência de leitura; nada copiado |
| SSPI / Schannel (Windows) | API do sistema | **Sim** — nada é redistribuído |
| MariaDB Connector/C | LGPL | **Não** com link estático |
| Qt / QScintilla | LGPL / GPL | **Não** — rejeitado (ADR 0003) |
| GTK | LGPL | **Não** com link estático — evitado pelo ADR 0007 |
| Oracle OCI | Proprietária | Verificar antes da Fase 3 |

**Todo o stack de UI é permissivo (MIT/zlib)** e compatível com link estático — nenhuma
dependência LGPL restou após o ADR 0007.
