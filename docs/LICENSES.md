# Licenças de terceiros

Este arquivo deve acompanhar qualquer distribuição binária do C-Otter. É a obrigação legal
que as licenças abaixo impõem em troca do uso.

---

## Scintilla 5.6.6

Componente de edição de texto. `third_party/scintilla/`
Licença: **HPND** (Historical Permission Notice and Disclaimer)

> Copyright 1998-2021 by Neil Hodgson <neilh@scintilla.org>
>
> All Rights Reserved
>
> Permission to use, copy, modify, and distribute this software and its documentation for
> any purpose and without fee is hereby granted, provided that the above copyright notice
> appear in all copies and that both that copyright notice and this permission notice appear
> in supporting documentation.
>
> NEIL HODGSON DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL IMPLIED
> WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL NEIL HODGSON BE LIABLE FOR
> ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
> LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
> TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
> SOFTWARE.

Texto integral: `third_party/scintilla/License.txt`

---

## Lexilla 5.5.3

Infraestrutura de lexers (`lexlib`). `third_party/lexilla/`
Licença: **HPND**, mesmos termos acima.

Texto integral: `third_party/lexilla/License.txt`

**Nota:** compilamos apenas `lexlib` — a infraestrutura para registrar um `ILexer5` próprio.
Os ~100 lexers de linguagens do Lexilla não são compilados nem distribuídos.

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
| Scintilla | HPND | **Sim** |
| Lexilla | HPND | **Sim** |
| SQLite | Domínio público | **Sim** |
| PostgreSQL (libpq) | PostgreSQL License | **Sim** |
| ODBC | API do sistema | **Sim** |
| Dear ImGui | MIT | **Sim** |
| MySQL (libmysqlclient) | GPL | **Não** — bloqueado |
| MariaDB Connector/C | LGPL | **Não** com link estático |
| Qt / QScintilla | LGPL / GPL | **Não** — rejeitado (ADR 0003) |
| Oracle OCI | Proprietária | Verificar antes da Fase 3 |
