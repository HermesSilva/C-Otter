# ADR 0007 — Editor: ImGuiColorTextEdit substitui Scintilla

**Data:** 2026-09-21
**Status:** Aceito — **revoga a decisão do ADR 0003**

## Contexto

O ADR 0003 escolheu Scintilla, ponderando acessibilidade nativa e maturidade acima do
tamanho e da integração. Dois requisitos posteriores mudaram os pesos:

1. **ADR 0006** — backend único GLFW+OpenGL, uma implementação para todas as plataformas.
2. **Requisito do usuário** — *"simples, mas elegante e rico em UI e recursos, de edição,
   com escrita única"*.

**Escrita única é exatamente o que o Scintilla não oferece.** Ele é um controle *nativo*:
`ScintillaWin` no Windows, `ScintillaGTK` no Linux. São duas integrações, duas dependências
(Win32 e GTK/LGPL) e um retângulo estrangeiro dentro de uma UI em modo imediato.

## Reavaliação com dados atuais

A avaliação do ADR 0003 usava informação desatualizada sobre o ImGuiColorTextEdit. O fork
mantido por Johan Goossens foi reescrito por completo em dezembro de 2024 e segue ativo em
2026 (727 commits, CI em Windows, Linux e macOS).

| | Scintilla + Lexilla | ImGuiColorTextEdit |
|---|---|---|
| **Escrita única** | **Não** — Win32 + GTK | **Sim** — um código |
| Linhas de terceiros | 51.285 | **13.692** |
| Licença | HPND | **MIT** |
| Vive dentro do ImGui | Não — janela nativa | **Sim** |
| Tema da lontra | Parcial (cores sim, geometria não) | **Total** |
| Dependência no Linux | **GTK (LGPL)** | Nenhuma |
| Lexer SQL embutido | `LexSQL` | **`Language::Sql()`** |
| Multi-cursor, undo/redo | Sim | Sim |
| **Code folding** | Sim | **Sim** |
| **Minimap** | Não | **Sim** |
| **Word wrap** | Sim | **Sim** |
| **Squiggle underlining** | Via indicators | **Sim** (API de marcadores) |
| Find/replace com UI | Via aplicação | **Embutido** |
| Bracket matching colorido | Sim | **Sim** (estilo VS Code) |
| Autocomplete | `SCI_AUTOCSHOW` | **Framework + ponte LSP** |
| UTF-8 | Sim | Sim |
| Acessibilidade nativa | **Sim** | Não |
| IME (CJK) | **Maduro** | Limitado |
| Arquivos gigantes | **Sim** | Declaradamente não |

### O que se perde, declarado sem disfarce

1. **Acessibilidade nativa.** Leitores de tela não enxergam um editor em modo imediato. Era
   o argumento mais forte do ADR 0003 e continua válido — apenas deixou de ser decisivo
   frente ao requisito de escrita única. Volta a ser débito conhecido da v1.
2. **IME maduro para CJK.** Entrada de japonês/chinês/coreano fica limitada.
3. **Arquivos muito grandes.** O widget não se propõe a gigabytes. Para SQL interativo é
   irrelevante — o caso de 1M linhas é a *grade*, não o editor.

### O que se ganha

- **Uma implementação** em vez de duas, sem GTK no Linux
- **13.692 linhas** de terceiros em vez de 51.285 (−73%)
- Integração visual completa: mesma fonte, mesma paleta, mesmo loop de render
- Minimap e find/replace com UI, que o Scintilla exigiria construir por cima
- Ponte LSP pronta, útil para o completion do ADR 0004

## A objeção do ADR 0003 permanece válida — e a solução também

O ADR 0003 argumentou, corretamente, que usar o `LexSQL` do Scintilla criaria **dois lexers
divergentes**: um para pintar a tela, outro para executar. Isso vale igualmente aqui.

A `Language` do ImGuiColorTextEdit é **estrutura de dados, não código**:

```cpp
struct Language {
    std::string name;
    bool        caseSensitive;
    std::string singleLineComment, commentStart, commentEnd;
    bool        hasSingleQuotedStrings, hasDoubleQuotedStrings;
    std::unordered_set<std::string> keywords, declarations, identifiers;
    // ...
};
```

Isso é **melhor** que o `ILexer5` do Scintilla para o nosso caso: o dialeto do `otter_sql`
preenche esses campos diretamente, e `identifiers` recebe tabelas e colunas vindas do
Pocket Rock — realce de metadados reais, de graça. Uma fonte só de verdade, como o ADR 0003
exigia.

Para construções que a `Language` não cobre (`$$` do PostgreSQL, `DELIMITER` do MySQL,
`q'{...}'` do Oracle), a API de marcadores e `squiggle` permite ao `otter_sql` sobrepor
decoração própria.

## Decisão

- **Adotar ImGuiColorTextEdit** (fork goossens, MIT) como editor SQL
- **Remover Scintilla e Lexilla** do repositório
- Dialeto SQL alimentado por `TextEditor::Language` a partir do `otter_sql`
- `identifiers` populado com metadados do Pocket Rock
- A fronteira `SqlEditor` do ADR 0003 permanece útil: isola o widget caso ele precise ser
  trocado

## Impacto na estimativa

| Item | ADR 0003 | ADR 0007 | Δ |
|------|----------|----------|---|
| Integração do editor | 160 (Win32+GTK) | **60** (uma só) | −100 |
| Lexer/dialeto para o widget | 60 (`ILexer5`) | **40** (`Language`) | −20 |
| Autocomplete, diagnósticos, tema | 80 | **80** | — |
| **Editor SQL** | 300 | **180** | **−120** |
| **Total do projeto** | ~14.400 | **≈ 14.250 h/h** | −150 |

## Validação — medido

Integrado e rodando em 2026-09-21:

- Realce SQL funcionando: keywords, strings, comentários, bracket matching
- Linha atual destacada, calha de números, posição do cursor
- Paleta da lontra aplicada às 24 cores do editor
- **144,0 fps** com o editor ativo
- Compilou de primeira, sem ajuste no código de terceiros

## Referências

- ADR 0003 — decisão revogada (análise de Notepad++ e das alternativas permanece válida)
- ADR 0006 — backend único, origem do requisito de escrita única
- [ImGuiColorTextEdit (goossens)](https://github.com/goossens/ImGuiColorTextEdit) — MIT
