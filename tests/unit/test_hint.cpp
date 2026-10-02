// C-Otter -- testes de ui/hint: como o texto de uma dica e' repartido em
// titulo, atalho e corpo.
//
// O desenho do cartao se confere na tela. O que da' para errar em silencio e'
// a leitura da convencao "Rotulo (Ctrl+X)": tomar por atalho um parentese que
// nao e' ("3 row(s)"), ou deixar de reconhecer um que e'.
#include "test_main.hpp"

#include "ui/hint.hpp"

#include <string>

using namespace otter::ui;

OTTER_TEST(hint_label_with_shortcut_is_split) {
    const HintParts parts = parse_hint_text("Execute (Ctrl+Enter)");
    OTTER_CHECK_EQ(parts.title, std::string{"Execute"});
    OTTER_CHECK_EQ(parts.shortcut, std::string{"Ctrl+Enter"});
    OTTER_CHECK(parts.body.empty());
}

OTTER_TEST(hint_note_after_the_first_line_is_the_body) {
    // O formato da barra lateral do editor: dois espacos antes do parentese,
    // e a nota (o que difere do DBeaver) na linha de baixo.
    const HintParts parts = parse_hint_text(
        "Execute SQL script  (Alt+X)\nShows the result of the last query");
    OTTER_CHECK_EQ(parts.title, std::string{"Execute SQL script"});
    OTTER_CHECK_EQ(parts.shortcut, std::string{"Alt+X"});
    OTTER_CHECK_EQ(parts.body, std::string{"Shows the result of the last query"});
}

OTTER_TEST(hint_recognizes_the_shapes_of_a_shortcut) {
    OTTER_CHECK_EQ(parse_hint_text("Open Declaration (F4, F12)").shortcut,
                   std::string{"F4, F12"});
    OTTER_CHECK_EQ(parse_hint_text("New tab (Ctrl+\\)").shortcut, std::string{"Ctrl+\\"});
    OTTER_CHECK_EQ(parse_hint_text("Zoom in (Ctrl++)").shortcut, std::string{"Ctrl++"});
    OTTER_CHECK_EQ(parse_hint_text("Single record view (Tab)").shortcut,
                   std::string{"Tab"});
    OTTER_CHECK_EQ(parse_hint_text("Separate tabs (Ctrl+Alt+Shift+X)").shortcut,
                   std::string{"Ctrl+Alt+Shift+X"});
}

OTTER_TEST(hint_parenthesis_that_is_not_a_shortcut_stays_in_the_text) {
    // Plural, unidade, explicacao: nada disso e' tecla.
    for (const char* text : {
             "exports the current page only (200 rows)",
             "Count the whole result (one more full scan)",
             "3 change(s)",
             "Connect timeout (s)",
             "Sending the schema is out of scope (ADR 0004)",
         }) {
        const HintParts parts = parse_hint_text(text);
        OTTER_CHECK(parts.title.empty());
        OTTER_CHECK(parts.shortcut.empty());
        OTTER_CHECK_EQ(parts.body, std::string{text});
    }
}

OTTER_TEST(hint_plain_text_keeps_its_lines) {
    const std::string text = "The grid holds one page at a time.\nRun it again.";
    const HintParts parts = parse_hint_text(text);
    OTTER_CHECK(parts.title.empty());
    OTTER_CHECK_EQ(parts.body, text);
}

OTTER_TEST(hint_shortcut_only_counts_at_the_end_of_the_first_line) {
    // "(Ctrl+C)" no meio da frase e' prosa.
    const HintParts parts = parse_hint_text("Press (Ctrl+C) to copy the cell");
    OTTER_CHECK(parts.title.empty());
    OTTER_CHECK(parts.shortcut.empty());
}
