# ADR 0003 — Componente do editor SQL

**Data:** 2026-09-21
**Status:** Aceito — Scintilla + Lexilla, com fronteira abstrata

## Contexto

O editor SQL é o item mais caro e de maior risco do projeto: **700 h/h** estimadas
originalmente, com variação de ±400 h/h (`docs/EFFORT.md` §9).

A investigação passou por três perguntas, nesta ordem:

1. Usar o **Notepad++** como biblioteca, e aceitar seus plugins?
2. Existe **algo melhor** que Scintilla?
3. Muda algo o fato de editarmos **apenas SQL**, de vários sabores?

## 1. Notepad++ — rejeitado

Notepad++ e Scintilla são coisas distintas, com licenças opostas.

| | Notepad++ | Scintilla / Lexilla |
|---|---|---|
| O que é | Aplicação completa (Win32) | **Componente de edição embutível** |
| Licença | **GPL v3+** | **HPND** (permissiva) |
| Embutível | Não — é um `.exe` | **Sim, foi feito para isso** |
| Plataformas | Windows | Win32, GTK, macOS, Qt |
| Link estático em produto fechado | **Proibido** | **Permitido** |

Toda a capacidade de edição que se admira no Notepad++ — realce, folding, múltiplos cursores
— **é do Scintilla**. O Notepad++ acrescenta janela, abas, sessões e plugins.

**Plugins do Notepad++: rejeitados.** São DLLs Win32 que assumem a arquitetura interna do
programa (`HWND` da janela principal, dois controles Scintilla, API `NPPM_*` via
`SendMessage`). Colidem com três decisões já tomadas: link estático (ADR 0002 — plugins são
DLLs por definição), portabilidade Linux (a API é Win32 pura) e UI em ImGui (plugins esperam
`HWND` e diálogos nativos). Replicar a API de um programa GPL para executar seus plugins
também é zona jurídica cinzenta quanto a obra derivada. O benefício seria baixo: os plugins
do Notepad++ são de edição genérica (comparar arquivos, FTP, formatar XML), não de banco de
dados.

## 2. Alternativas avaliadas

| Opção | Tipo | Licença | Veredito |
|-------|------|---------|----------|
| **Scintilla + Lexilla** | Componente nativo | HPND | **Escolhido** |
| **ImGuiColorTextEdit** (fork goossens) | Widget ImGui | MIT | Forte candidato — ver §3 |
| Editor próprio em ImGui | — | — | Rejeitado: 700 h/h, maior risco do projeto |
| QScintilla | Scintilla + Qt | GPL/comercial | Rejeitado: Qt é LGPL, incompatível com ADR 0002 |
| GtkSourceView | Widget GTK | LGPL | Rejeitado: LGPL + GTK no Windows |
| Monaco / CodeMirror | JavaScript | MIT | Rejeitado: exigiria embutir um browser |
| Zep | Widget ImGui | MIT | Rejeitado: manutenção parada (2018–2023) |
| Tree-sitter | Parser incremental | MIT | **Não é editor** — é parser. Ver §4 |

## 3. O fator decisivo: editamos apenas SQL

Isto reduz drasticamente o valor da maturidade generalista do Scintilla. Medição dos dois
candidatos:

| | Scintilla + Lexilla | ImGuiColorTextEdit |
|---|---|---|
| Linhas de terceiros a compilar | **51.285** | **~12.500** |
| Integração com ImGui | Janela nativa ao lado | **Nativa, mesmo loop de render** |
| Portabilidade | Win32 **+** GTK: duas integrações | **Uma só** |
| Multi-cursor / undo / find-replace | Sim | Sim |
| Autocomplete | Sim (`SCI_AUTOCSHOW`) | Sim (framework + LSP bridge) |
| Code folding | Sim | **Não** |
| Acessibilidade nativa | **Sim** | Não |
| IME (CJK) | **Maduro** | Limitado |
| Arquivos gigantes | **Sim** (CellBuffer) | Declaradamente não |
| Tema da lontra aplicável | Difícil (controle nativo) | **Trivial** |
| Licença | HPND | MIT |

### O que "apenas SQL" elimina como vantagem do Scintilla

- **Os ~100 lexers do Lexilla são irrelevantes** — usaríamos um (`LexSQL.cxx`, 877 linhas).
- **Folding importa pouco** em SQL: scripts são sequências de statements, não árvores
  profundas de blocos. O splitter de `otter_sql` já particiona statements.
- **Arquivos gigantes são raros** em SQL interativo; o caso de 1M linhas é o *grid*, não o
  editor.

### O que "vários sabores" de SQL revela

Aqui está o ponto que inverte a comparação. O `LexSQL` do Scintilla trata dialetos apenas
por **conjuntos de keywords** e algumas properties (`sql.backslash.escapes`,
`lexer.sql.backticks.identifier`). Ele **não** distingue de fato:

- `$$ ... $$` do PostgreSQL vs `DELIMITER //` do MySQL
- `[colchetes]` do MSSQL vs `` `crases` `` do MySQL vs `"aspas"` do padrão
- `N'literal'`, `q'{...}'` do Oracle, tipos e funções por SGBD

Mas `otter_sql` **já vai implementar exatamente isso** — lexer dirigido por tabela de
dialeto e splitter de script ciente de `$$`/`DELIMITER` (`docs/PLAN.md` §2.4, 240 h/h já
orçadas e no caminho crítico de qualquer forma, pois alimentam autocomplete e execução).

Isso muda a natureza da escolha: **o realce não precisa vir do componente.** Com um lexer
próprio já obrigatório, adotar o Scintilla significaria manter **dois** lexers SQL — o nosso,
para semântica e execução, e o `LexSQL`, para pintar a tela — e mantê-los coerentes entre si.
Essa duplicação é uma fonte permanente de divergência: o editor realça uma coisa, o executor
entende outra.

## Decisão

**Scintilla + Lexilla**, com o realce alimentado pelo **lexer próprio** do `otter_sql` via
`ILexer5` customizado, atrás de uma fronteira abstrata:

```cpp
namespace otter::ui {
class SqlEditor {                       // interface estavel, implementacao trocavel
public:
    virtual ~SqlEditor() = default;
    virtual void set_text(std::string_view) = 0;
    virtual std::string text() const = 0;
    virtual void set_dialect(const sql::Dialect&) = 0;
    virtual void set_completions(std::span<const Completion>) = 0;
    virtual void mark_diagnostics(std::span<const Diagnostic>) = 0;
};
} // implementacoes: ScintillaSqlEditor | ImGuiSqlEditor
```

### Por que Scintilla, e não ImGuiColorTextEdit

Apesar de o ImGuiColorTextEdit ser ~4× menor e integrar-se melhor ao ImGui, três fatores
pesam mais para um produto que se pretende profissional:

1. **Acessibilidade** — Scintilla expõe interfaces de acessibilidade do SO. Isso é requisito
   em ambiente corporativo, hoje listado como débito da v1 em `docs/ANALYSIS.md` §7. O
   componente ImGui não tem como oferecer isso: modo imediato não produz árvore de UI.
2. **IME maduro** — entrada CJK e composição de acentos funcionam de verdade. Um cliente de
   banco de dados é usado no mundo todo.
3. **Maturidade sob estresse** — 25+ anos em Notepad++, Geany, SciTE, CodeLite. Casos de
   borda de edição de texto (seleção retangular, undo em multi-cursor, wrap com tabs) estão
   resolvidos e testados por milhões de usuários.

O custo aceito é real e deve ser dito: **duas integrações de plataforma** (Win32 e GTK), uma
região da janela que não é ImGui e não segue o tema da lontra automaticamente, e 51k linhas
de terceiros no build.

**A fronteira `SqlEditor` existe justamente por isso.** Se o spike mostrar que a costura
visual entre controle nativo e ImGui ficou ruim, trocar para `ImGuiSqlEditor` custa a
implementação da interface, não o retrabalho do editor inteiro.

## 4. Tree-sitter — considerado e rejeitado para esta função

Tree-sitter é **parser incremental, não editor**: não desenha nem trata entrada. Seria
alternativa ao *lexer* de `otter_sql`, não ao componente. Rejeitado porque:

- Introduz dependência em C + geração de parser por gramática
- Um parser recursive-descent próprio já está orçado (`docs/PLAN.md` §2.4) e é necessário
  de qualquer modo para resolução de nomes e execução
- Dialetos SQL exigem controle fino que gramáticas de terceiros não entregam

Existe `tslexia` (ponte Tree-sitter → Scintilla) caso a decisão se revele errada mais tarde.

## Impacto na estimativa

| Item | Antes | Depois |
|------|-------|--------|
| Editor SQL | 700 h/h | **300 h/h** |
| — integração Scintilla (Win32 + GTK) | | 160 |
| — `ILexer5` sobre o lexer do `otter_sql` | | 60 |
| — autocomplete, diagnósticos, tema | | 80 |
| `otter_ui` | 2.800 h/h | **2.400 h/h** |
| **Total do projeto** | 13.250 h/h | **≈ 12.750 h/h** |

Redução de ~500 h/h (contingência incluída) e — mais importante — **remoção do maior risco
isolado do projeto**.

## Validação — medido, não estimado

Spike `spikes/sizecheck` (2026-09-21), Release com LTO, `/OPT:REF /OPT:ICF`:

| Métrica | Valor |
|---------|-------|
| `scintilla.lib` (estático, com IR de LTO) | 69,7 MB |
| **Executável final com Scintilla ativo** | **0,93 MB** |
| DLLs importadas | Apenas do Windows: `KERNEL32`, `USER32`, `GDI32`, `IMM32`, `ole32`, `OLEAUT32`, `ADVAPI32` |
| Funcionalidade | Controle criado, texto SQL carregado e lido de volta |

O `.lib` de 69 MB é enganoso: contém IR de LTO e todos os símbolos. Após o descarte do
linker, **o custo real do Scintilla é inferior a 1 MB** — folgadamente dentro da meta de
instalador < 25 MB (`docs/PLAN.md` §4). Nenhum redistribuível da MSVC foi introduzido.

## Consequências

- `third_party/scintilla` (5.6.6) e `third_party/lexilla` (5.5.3) entram no repositório,
  compilados estaticamente (ADR 0002). `License.txt` de ambos deve ser preservado na
  distribuição — única obrigação do HPND. Ver `docs/LICENSES.md`.
- Build com `STATIC_BUILD` (sem `DllMain`) e `NO_CXX11_REGEX` (evita `<regex>`, que é
  volumoso e lento). Registro do controle via `Scintilla_RegisterClasses()`.
- O `LexSQL` do Lexilla **não** é usado: o realce vem do lexer próprio via `ILexer5`.
  Compilamos `lexlib` (infraestrutura), não os ~100 lexers.
- A UI deixa de ser "ImGui puro" — decisão consciente, isolada atrás de `SqlEditor`.

## Referências

- [Scintilla](https://www.scintilla.org/) · [License](https://www.scintilla.org/License.txt) · [Scintilla 5 Migration](https://www.scintilla.org/Scintilla5Migration.html)
- [Lexilla](https://scintilla.org/LexillaDoc.html)
- [ImGuiColorTextEdit (goossens)](https://github.com/goossens/ImGuiColorTextEdit)
- [Notepad++ Plugin Communication](https://npp-user-manual.org/docs/plugin-communication/)
- [Tree-sitter](https://github.com/tree-sitter/tree-sitter) · [tslexia](https://github.com/orbitalquark/tslexia)
