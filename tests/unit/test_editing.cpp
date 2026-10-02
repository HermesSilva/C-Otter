// Regras de texto por tras dos comandos do editor SQL (sql/editing.hpp).
//
// Sao as que decidem O QUE Ctrl+Enter executa -- errar aqui e' rodar a
// consulta que o usuario nao estava olhando.
#include "test_main.hpp"

#include "sql/editing.hpp"

#include <string>

using namespace otter::sql;

namespace {

const Dialect& pg() { return dialect_for("postgresql"); }

std::string slice(std::string_view script, StatementRange range) {
    return std::string(script.substr(range.begin, range.end - range.begin));
}

} // namespace

// --- Posicao ---------------------------------------------------------------------

OTTER_TEST(editing_offset_counts_characters_not_bytes) {
    // "ç" ocupa dois bytes. O editor enderea o cursor por caractere; contar
    // bytes deixaria o cursor no meio da letra.
    const std::string text = "ação\nxy";
    OTTER_CHECK_EQ(offset_of(text, {0, 2}), std::size_t{3});   // depois de "aç"
    OTTER_CHECK_EQ(offset_of(text, {1, 1}), std::size_t{8});

    const TextPosition back = position_of(text, 8);
    OTTER_CHECK_EQ(back.line, std::size_t{1});
    OTTER_CHECK_EQ(back.column, std::size_t{1});
}

OTTER_TEST(editing_offset_stops_at_the_end_of_the_line) {
    OTTER_CHECK_EQ(offset_of("ab\ncd", {0, 99}), std::size_t{2});
}

// --- Instrucao sob o cursor --------------------------------------------------------

OTTER_TEST(editing_statement_at_cursor_is_delimited_by_semicolons) {
    const std::string script = "select 1;\nselect 2;\nselect 3;";
    const auto range = statement_range_at(script, pg(), 14);
    OTTER_CHECK(range.has_value());
    OTTER_CHECK_EQ(slice(script, *range), std::string{"select 2"});
}

OTTER_TEST(editing_statement_at_cursor_excludes_surrounding_blank_space) {
    // O defeito que motivou o teste: Ctrl+Enter mandava o SCRIPT INTEIRO.
    const std::string script = "\n\n  select a\n    from t  \n\n;\n";
    const auto range = statement_range_at(script, pg(), 6);
    OTTER_CHECK(range.has_value());
    OTTER_CHECK_EQ(slice(script, *range), std::string{"select a\n    from t"});
}

OTTER_TEST(editing_blank_line_separates_statements_without_semicolon) {
    // O modo "smart" do DBeaver: linha em branco seguida de palavra que
    // comeca instrucao.
    const std::string script = "select 1\n\nselect 2";
    const auto ranges = statement_ranges(script, pg());
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});
    OTTER_CHECK_EQ(slice(script, ranges[1]), std::string{"select 2"});
}

OTTER_TEST(editing_blank_line_inside_a_statement_does_not_split_it) {
    // Seguida de FROM, a linha em branco e' so' respiro.
    const std::string script = "select a\n\nfrom t\n\nwhere a = 1";
    OTTER_CHECK_EQ(statement_ranges(script, pg()).size(), std::size_t{1});
}

OTTER_TEST(editing_blank_line_inside_parentheses_does_not_split) {
    const std::string script = "select * from (\n\nselect 1\n\n) x";
    OTTER_CHECK_EQ(statement_ranges(script, pg()).size(), std::size_t{1});
}

OTTER_TEST(editing_blank_line_inside_a_routine_body_does_not_split) {
    const std::string script =
        "create procedure p() begin\n\nselect 1;\n\nselect 2;\nend";
    OTTER_CHECK_EQ(statement_ranges(script, dialect_for("mysql")).size(),
                   std::size_t{1});
}

OTTER_TEST(editing_leading_comment_stays_with_its_statement) {
    const std::string script = "-- total por cliente\n\nselect 1";
    const auto ranges = statement_ranges(script, pg());
    OTTER_CHECK_EQ(ranges.size(), std::size_t{1});
}

OTTER_TEST(editing_cursor_between_statements_takes_the_previous_one) {
    // Logo depois do ';', na linha em branco: e' onde o cursor para ao
    // terminar de digitar. Executar a de BAIXO seria rodar outra consulta.
    const std::string script = "select 1;\n\nselect 2;";
    const auto range = statement_range_at(script, pg(), 10);
    OTTER_CHECK(range.has_value());
    OTTER_CHECK_EQ(slice(script, *range), std::string{"select 1"});
}

OTTER_TEST(editing_cursor_before_everything_takes_the_first) {
    const std::string script = "\n\nselect 1;";
    const auto range = statement_range_at(script, pg(), 0);
    OTTER_CHECK(range.has_value());
    OTTER_CHECK_EQ(slice(script, *range), std::string{"select 1"});
}

OTTER_TEST(editing_empty_script_has_no_statement) {
    OTTER_CHECK(!statement_range_at("  \n ", pg(), 1).has_value());
}

OTTER_TEST(editing_semicolon_inside_a_string_does_not_split) {
    const std::string script = "select ';' as a; select 2";
    OTTER_CHECK_EQ(statement_ranges(script, pg()).size(), std::size_t{2});
}

OTTER_TEST(editing_control_command_ends_at_the_end_of_its_line) {
    // O defeito visto na tela: `@set n = 7` sem ';' engolia o SELECT de
    // baixo, e a variavel passava a valer "7\nselect ...".
    const std::string script =
        "@set n = 7\nselect ${n} as sete;\nselect 'dois' as b;\n@echo fim";
    const auto ranges = statement_ranges(script, pg());
    OTTER_CHECK_EQ(ranges.size(), std::size_t{4});
    if (ranges.size() == 4) {
        OTTER_CHECK_EQ(slice(script, ranges[0]), std::string{"@set n = 7"});
        OTTER_CHECK_EQ(slice(script, ranges[1]), std::string{"select ${n} as sete"});
        OTTER_CHECK_EQ(slice(script, ranges[2]), std::string{"select 'dois' as b"});
        OTTER_CHECK_EQ(slice(script, ranges[3]), std::string{"@echo fim"});
    }
}

OTTER_TEST(editing_control_command_splits_unterminated_sql_and_drops_semicolon) {
    const std::string script = "select 1\n  @set x = a b;\nselect 2";
    const auto ranges = statement_ranges(script, pg());
    OTTER_CHECK_EQ(ranges.size(), std::size_t{3});
    if (ranges.size() == 3) {
        OTTER_CHECK_EQ(slice(script, ranges[0]), std::string{"select 1"});
        OTTER_CHECK_EQ(slice(script, ranges[1]), std::string{"@set x = a b"});
        OTTER_CHECK_EQ(slice(script, ranges[2]), std::string{"select 2"});
    }
}

OTTER_TEST(editing_control_command_apostrophe_does_not_swallow_what_follows) {
    // Lido como SQL, o apostrofo abriria uma string ate' o fim do script.
    const std::string script = "@echo it's done\nselect 1;\nselect 2;";
    const auto ranges = statement_ranges(script, pg());
    OTTER_CHECK_EQ(ranges.size(), std::size_t{3});
    if (ranges.size() == 3) {
        OTTER_CHECK_EQ(slice(script, ranges[0]), std::string{"@echo it's done"});
        OTTER_CHECK_EQ(slice(script, ranges[2]), std::string{"select 2"});
    }
}

OTTER_TEST(editing_at_sign_that_is_not_a_control_command_does_not_split) {
    // Variavel de usuario do MySQL no comeco de uma linha, e `@set` dentro
    // de uma string ou de um comentario.
    const Dialect& my = dialect_for("mysql");
    OTTER_CHECK_EQ(statement_ranges("select\n  @a,\n  @b", my).size(), std::size_t{1});
    OTTER_CHECK_EQ(statement_ranges("select '\n@set x = 1\n' as t", pg()).size(),
                   std::size_t{1});
    OTTER_CHECK_EQ(statement_ranges("select 1 /*\n@echo oi\n*/ + 2", pg()).size(),
                   std::size_t{1});
}

// --- Proxima / anterior ------------------------------------------------------------

OTTER_TEST(editing_next_and_previous_statement) {
    const std::string script = "select 1;\nselect 2;\nselect 3;";
    //                          0         10        20

    OTTER_CHECK_EQ(next_statement_start(script, pg(), 3).value_or(999),
                   std::size_t{10});
    OTTER_CHECK_EQ(next_statement_start(script, pg(), 12).value_or(999),
                   std::size_t{20});
    OTTER_CHECK(!next_statement_start(script, pg(), 22).has_value());

    // Do meio da segunda, a anterior e' a PRIMEIRA -- nao o inicio da propria.
    OTTER_CHECK_EQ(previous_statement_start(script, pg(), 14).value_or(999),
                   std::size_t{0});
    OTTER_CHECK(!previous_statement_start(script, pg(), 3).has_value());
}

// --- Colchetes -------------------------------------------------------------------

OTTER_TEST(editing_matching_bracket_forward_and_backward) {
    const std::string script = "select f(a, (b + c))";
    //                          0       8   12     18 19
    OTTER_CHECK_EQ(matching_bracket(script, pg(), 8).value_or(999),
                   std::size_t{19});
    OTTER_CHECK_EQ(matching_bracket(script, pg(), 19).value_or(999),
                   std::size_t{8});
    OTTER_CHECK_EQ(matching_bracket(script, pg(), 12).value_or(999),
                   std::size_t{18});
}

OTTER_TEST(editing_matching_bracket_uses_the_one_before_the_cursor) {
    // Cursor logo DEPOIS do ')': o caso comum ao terminar de digitar.
    const std::string script = "f(a)";
    OTTER_CHECK_EQ(matching_bracket(script, pg(), 4).value_or(999),
                   std::size_t{1});
}

OTTER_TEST(editing_bracket_inside_a_string_is_not_a_bracket) {
    const std::string script = "select (')') as x";
    //                          0      7  10 11
    OTTER_CHECK_EQ(matching_bracket(script, pg(), 7).value_or(999),
                   std::size_t{11});
}

OTTER_TEST(editing_unbalanced_bracket_has_no_match) {
    OTTER_CHECK(!matching_bracket("select (1", pg(), 7).has_value());
    OTTER_CHECK(!matching_bracket("select 1", pg(), 3).has_value());
}

// --- Selecao ---------------------------------------------------------------------

OTTER_TEST(editing_block_comment_wraps_and_unwraps) {
    OTTER_CHECK_EQ(toggle_block_comment("a + b"), std::string{"/*a + b*/"});
    OTTER_CHECK_EQ(toggle_block_comment("/*a + b*/"), std::string{"a + b"});
    // O espaco das pontas da selecao sobrevive ao desfazer.
    OTTER_CHECK_EQ(toggle_block_comment(" /*x*/\n"), std::string{" x\n"});
}

OTTER_TEST(editing_trim_lines) {
    const std::string text = "  a  \n\tb\t\n c";
    OTTER_CHECK_EQ(trim_lines(text, true, true), std::string{"a\nb\nc"});
    OTTER_CHECK_EQ(trim_lines(text, true, false), std::string{"a  \nb\t\nc"});
    OTTER_CHECK_EQ(trim_lines(text, false, true), std::string{"  a\n\tb\n c"});
    // O fim de linha do Windows nao e' espaco a remover.
    OTTER_CHECK_EQ(trim_lines("a  \r\nb ", false, true), std::string{"a\r\nb"});
}

OTTER_TEST(editing_morph_delimited_list_with_the_dbeaver_defaults) {
    // Uma coluna colada de uma planilha vira lista para um IN (...).
    MorphOptions options;
    options.quote = "'";
    OTTER_CHECK_EQ(morph_delimited_list("10\n20\r\n 30 ", options),
                   std::string{"'10','20','30'"});
}

OTTER_TEST(editing_morph_wraps_and_adds_leading_and_trailing_text) {
    MorphOptions options;
    options.quote         = "";
    options.wrap_line     = 6;
    options.leading_text  = "(";
    options.trailing_text = ")";
    OTTER_CHECK_EQ(morph_delimited_list("aa,bb,cc", options),
                   std::string{"(aa,\nbb,cc)"});
}

// --- Variaveis ---------------------------------------------------------------------

OTTER_TEST(editing_control_commands_are_recognized) {
    const ControlCommand set = parse_control_command("@set cliente = 42");
    OTTER_CHECK(set.kind == ControlCommand::Kind::set);
    OTTER_CHECK_EQ(set.name, std::string{"cliente"});
    OTTER_CHECK_EQ(set.value, std::string{"42"});

    const ControlCommand unset = parse_control_command("  @unset cliente ");
    OTTER_CHECK(unset.kind == ControlCommand::Kind::unset);
    OTTER_CHECK_EQ(unset.name, std::string{"cliente"});

    const ControlCommand echo = parse_control_command("@echo pronto");
    OTTER_CHECK(echo.kind == ControlCommand::Kind::echo);
    OTTER_CHECK_EQ(echo.value, std::string{"pronto"});

    // SQL comum NAO e' comando de controle -- nem o `@var` do MySQL.
    OTTER_CHECK(parse_control_command("select @x").kind ==
                ControlCommand::Kind::none);
    OTTER_CHECK(parse_control_command("@set sem_igual").kind ==
                ControlCommand::Kind::none);
}

OTTER_TEST(editing_variables_expand_and_unknown_ones_are_kept) {
    Variables variables;
    variables["id"] = "42";
    OTTER_CHECK_EQ(expand_variables("select ${id}, ${outra}", variables),
                   std::string{"select 42, ${outra}"});
}

// --- Templates e estrutura ---------------------------------------------------------

OTTER_TEST(editing_default_templates_are_the_dbeaver_ones) {
    const auto& templates = default_templates();
    OTTER_CHECK_EQ(templates.size(), std::size_t{5});

    const ExpandedTemplate sf = expand_template(templates.back());
    OTTER_CHECK_EQ(sf.text, std::string{"select * from table;"});
    // O cursor fica sobre a primeira variavel, selecionada para ser trocada.
    OTTER_CHECK_EQ(sf.cursor, std::size_t{14});
    OTTER_CHECK_EQ(sf.select, std::size_t{5});
}

OTTER_TEST(editing_outline_names_the_statement_and_its_target) {
    const std::string script =
        "select * from public.cliente;\nupdate pedido set a = 1;\ncommit;";
    const auto entries = outline(script, pg());
    OTTER_CHECK_EQ(entries.size(), std::size_t{3});
    OTTER_CHECK_EQ(entries[0].label, std::string{"SELECT public.cliente"});
    OTTER_CHECK_EQ(entries[1].label, std::string{"UPDATE pedido"});
    OTTER_CHECK_EQ(entries[1].line, std::size_t{1});
    OTTER_CHECK_EQ(entries[2].label, std::string{"COMMIT"});
}
