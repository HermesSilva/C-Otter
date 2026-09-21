# ADR 0002 — Linkagem estática total

**Data:** 2026-09-20
**Status:** Aceito

## Contexto

C-Otter compete com o DBeaver em custo de distribuição e de inicialização. O DBeaver embarca
uma JRE e resolve bundles OSGi em runtime: ~200 MB de instalador e 3–8 s de cold start.

## Decisão

**Tudo é linkado estaticamente**, incluindo a runtime do C++:

- MSVC: `/MT` e `/MTd` (`CMAKE_MSVC_RUNTIME_LIBRARY = MultiThreaded[Debug]`)
- GCC/Clang: `-static-libgcc -static-libstdc++`
- `BUILD_SHARED_LIBS=OFF`, busca de bibliotecas restrita a `.lib`/`.a`
- Drivers de SGBD embarcados no binário, não carregados como DLL

## Consequências

### Positivas

- Binário único, sem VCRUNTIME140.dll nem redistribuível — sustenta a meta de instalador < 25 MB
- Elimina resolução de símbolos em runtime, ajudando a meta de cold start < 200 ms
- LTO entre módulos passa a ser possível (inlining através de fronteiras de biblioteca)
- Sem "DLL hell" de versões conflitantes de cliente de SGBD na máquina do usuário

### Negativas e restrições

- **Licenciamento é a consequência séria.** Link estático de biblioteca **LGPL** obriga a
  distribuir objetos relinkáveis ou o código-fonte. Isso elimina, para produto fechado:
  - **Qt** (LGPL) — já descartado; a UI é Dear ImGui (MIT)
  - **MariaDB Connector/C** (LGPL) — ver mitigação abaixo
- **libmysqlclient da Oracle é GPL** — incompatível com produto proprietário em qualquer
  forma de link. Não usar.
- Correção de segurança em dependência exige recompilar e redistribuir o binário inteiro,
  em vez de trocar uma DLL.
- Binário maior que o executável equivalente com link dinâmico (compensado pela ausência de
  runtime externa).

### Licenças dos drivers-alvo

| Driver | Licença | Link estático viável |
|--------|---------|---------------------|
| SQLite | Domínio público | Sim — amalgamação embarcada |
| PostgreSQL (libpq) | PostgreSQL License (BSD-like) | Sim |
| ODBC / MSSQL | API do sistema operacional | Sim (import lib do SO) |
| Oracle OCI | Proprietária, redistribuição restrita | Verificar antes da Fase 3 |
| MySQL/MariaDB | GPL / LGPL | **Bloqueado** — decidir na Fase 3 |

**Mitigação para MySQL/MariaDB:** implementar o protocolo wire nativamente. O protocolo do
MySQL é documentado e estável; o custo estimado (~200 h/h adicionais sobre as 260 h/h do
driver) compra independência total de licença. Decisão adiada para a Fase 3, quando a
abstração `otter_db` já estiver validada por PostgreSQL e SQLite.

## Alternativas rejeitadas

- **Link dinâmico com drivers como plugins** — traria flexibilidade de licença, mas custa
  startup, contraria a meta de binário único e reintroduz a complexidade de resolução que
  motivou abandonar o OSGi.
- **Híbrido (núcleo estático, drivers dinâmicos)** — mantido como plano B caso o
  licenciamento do Oracle OCI force carregamento dinâmico. A fronteira `Driver` já é uma
  interface virtual pura, então a mudança seria localizada.
