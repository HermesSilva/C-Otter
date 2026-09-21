#include "test_main.hpp"

#include "sql/lexer.hpp"
#include "sql/script.hpp"

#include <string>

using namespace otter::sql;

namespace {

std::vector<Token> lex(std::string_view sql, const Dialect& dialect) {
    Lexer lexer(sql, dialect);
    return lexer.tokenize_all(/*skip_trivia=*/true);
}

} // namespace

// --- Lexer -------------------------------------------------------------------

OTTER_TEST(lexer_basic_select) {
    const auto tokens = lex("SELECT id FROM otters", postgres_dialect());

    OTTER_CHECK_EQ(tokens.size(), std::size_t{5});   // 4 + end
    OTTER_CHECK(tokens[0].kind == TokenKind::keyword);
    OTTER_CHECK(tokens[1].kind == TokenKind::identifier);
    OTTER_CHECK(tokens[2].kind == TokenKind::keyword);
    OTTER_CHECK(tokens[3].kind == TokenKind::identifier);
    OTTER_CHECK_EQ(tokens[3].text, std::string_view{"otters"});
}

OTTER_TEST(lexer_tracks_line_and_column) {
    const auto tokens = lex("SELECT\n  id", postgres_dialect());

    OTTER_CHECK_EQ(tokens[0].line, std::size_t{0});
    OTTER_CHECK_EQ(tokens[1].line, std::size_t{1});
    OTTER_CHECK_EQ(tokens[1].column, std::size_t{2});
}

OTTER_TEST(lexer_string_with_doubled_quote) {
    // '' dentro de string e' aspa escapada, nao fim da string.
    const auto tokens = lex("'it''s'", postgres_dialect());
    OTTER_CHECK(tokens[0].kind == TokenKind::string);
    OTTER_CHECK_EQ(tokens[0].text, std::string_view{"'it''s'"});
}

OTTER_TEST(lexer_unterminated_string_is_invalid_not_crash) {
    // O editor tokeniza texto incompleto o tempo todo.
    const auto tokens = lex("SELECT 'abc", postgres_dialect());
    OTTER_CHECK(tokens[1].kind == TokenKind::invalid);
}

OTTER_TEST(lexer_backslash_escape_differs_by_dialect) {
    // MySQL trata \' como escape; PostgreSQL (moderno) nao.
    const auto mysql = lex("'a\\'b'", mysql_dialect());
    OTTER_CHECK(mysql[0].kind == TokenKind::string);

    const auto pg = lex("'a\\'", postgres_dialect());
    OTTER_CHECK(pg[0].kind == TokenKind::string);
}

OTTER_TEST(lexer_quoted_identifier_by_dialect) {
    const auto pg = lex("\"Minha Tabela\"", postgres_dialect());
    OTTER_CHECK(pg[0].kind == TokenKind::quoted_identifier);
    OTTER_CHECK_EQ(pg[0].unquoted(), std::string_view{"Minha Tabela"});

    const auto mysql = lex("`minha tabela`", mysql_dialect());
    OTTER_CHECK(mysql[0].kind == TokenKind::quoted_identifier);
    OTTER_CHECK_EQ(mysql[0].unquoted(), std::string_view{"minha tabela"});

    const auto mssql = lex("[minha tabela]", mssql_dialect());
    OTTER_CHECK(mssql[0].kind == TokenKind::quoted_identifier);
    OTTER_CHECK_EQ(mssql[0].unquoted(), std::string_view{"minha tabela"});
}

OTTER_TEST(lexer_backtick_is_not_quote_in_postgres) {
    // A crase nao delimita identificador no PostgreSQL.
    const auto tokens = lex("`x`", postgres_dialect());
    OTTER_CHECK(tokens[0].kind != TokenKind::quoted_identifier);
}

OTTER_TEST(lexer_dollar_quoted_string) {
    const auto tokens = lex("$$ SELECT 1; $$", postgres_dialect());
    OTTER_CHECK(tokens[0].kind == TokenKind::dollar_string);
}

OTTER_TEST(lexer_dollar_quoted_with_tag) {
    const auto tokens = lex("$body$ x'y; $body$", postgres_dialect());
    OTTER_CHECK(tokens[0].kind == TokenKind::dollar_string);
    OTTER_CHECK_EQ(tokens[0].text.size(), std::size_t{18});
}

OTTER_TEST(lexer_dollar_parameter_not_confused_with_dollar_string) {
    const auto tokens = lex("WHERE id = $1", postgres_dialect());
    OTTER_CHECK(tokens[3].kind == TokenKind::parameter);
    OTTER_CHECK_EQ(tokens[3].text, std::string_view{"$1"});
}

OTTER_TEST(lexer_numbers) {
    const auto tokens = lex("1 2.5 1e10 1.5E-3 .75", postgres_dialect());
    for (std::size_t i = 0; i < 5; ++i) {
        OTTER_CHECK(tokens[i].kind == TokenKind::number);
    }
    OTTER_CHECK_EQ(tokens[3].text, std::string_view{"1.5E-3"});
}

OTTER_TEST(lexer_cast_operator_not_parameter) {
    // :: e' cast no PostgreSQL; : sozinho seria parametro.
    const auto tokens = lex("id::text", postgres_dialect());
    OTTER_CHECK(tokens[1].kind == TokenKind::operator_token);
    OTTER_CHECK_EQ(tokens[1].text, std::string_view{"::"});
}

OTTER_TEST(lexer_nested_block_comment_postgres_only) {
    // PostgreSQL aninha comentarios de bloco; MySQL nao.
    const auto pg = lex("/* a /* b */ c */ SELECT", postgres_dialect());
    OTTER_CHECK(pg[0].kind == TokenKind::keyword);   // comentario consumido

    const auto mysql = lex("/* a /* b */ SELECT", mysql_dialect());
    OTTER_CHECK(mysql[0].kind == TokenKind::keyword);
}

OTTER_TEST(lexer_hash_comment_mysql_only) {
    const auto mysql = lex("# comentário\nSELECT", mysql_dialect());
    OTTER_CHECK(mysql[0].kind == TokenKind::keyword);

    const auto pg = lex("# x", postgres_dialect());
    OTTER_CHECK(pg[0].kind != TokenKind::line_comment);
}

OTTER_TEST(lexer_unicode_identifier) {
    const auto tokens = lex("SELECT preço FROM produtos", postgres_dialect());
    OTTER_CHECK(tokens[1].kind == TokenKind::identifier);
}

// --- Dialeto -----------------------------------------------------------------

OTTER_TEST(dialect_quote_identifier_per_dialect) {
    OTTER_CHECK_EQ(postgres_dialect().quote_identifier("x"), std::string{"\"x\""});
    OTTER_CHECK_EQ(mysql_dialect().quote_identifier("x"), std::string{"`x`"});
    OTTER_CHECK_EQ(mssql_dialect().quote_identifier("x"), std::string{"[x]"});
}

OTTER_TEST(dialect_quote_escapes_delimiter) {
    OTTER_CHECK_EQ(postgres_dialect().quote_identifier("a\"b"),
                   std::string{"\"a\"\"b\""});
}

OTTER_TEST(dialect_needs_quoting) {
    const Dialect& pg = postgres_dialect();
    OTTER_CHECK(!pg.needs_quoting("otters"));
    OTTER_CHECK(pg.needs_quoting("select"));      // palavra reservada
    OTTER_CHECK(pg.needs_quoting("TIDxUsuario")); // caixa nao canonica
    OTTER_CHECK(pg.needs_quoting("com espaco"));
    OTTER_CHECK(pg.needs_quoting("2fast"));
}

// --- Splitter ----------------------------------------------------------------

OTTER_TEST(split_simple_statements) {
    const auto parts = split_script("SELECT 1; SELECT 2; SELECT 3",
                                    postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{3});
}

OTTER_TEST(split_ignores_semicolon_inside_string) {
    const auto parts = split_script("SELECT 'a;b'; SELECT 2", postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

OTTER_TEST(split_ignores_semicolon_inside_comment) {
    const auto parts = split_script("SELECT 1 -- ;\n; SELECT 2",
                                    postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

OTTER_TEST(split_respects_dollar_quoted_body) {
    // O caso que quebra splitters ingenuos: ';' dentro de $$ ... $$.
    constexpr std::string_view script =
        "CREATE FUNCTION f() RETURNS int AS $$\n"
        "  SELECT 1;\n"
        "  SELECT 2;\n"
        "$$ LANGUAGE sql;\n"
        "SELECT 3;";

    const auto parts = split_script(script, postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

OTTER_TEST(split_respects_begin_end_block) {
    constexpr std::string_view script =
        "BEGIN\n"
        "  UPDATE t SET x = 1;\n"
        "  UPDATE t SET y = 2;\n"
        "END;\n"
        "SELECT 1;";

    const auto parts = split_script(script, postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

OTTER_TEST(split_handles_mysql_delimiter) {
    // DELIMITER troca o separador em tempo de execucao.
    constexpr std::string_view script =
        "DELIMITER //\n"
        "CREATE PROCEDURE p() BEGIN SELECT 1; SELECT 2; END//\n";

    const auto parts = split_script(script, mysql_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{1});
}

OTTER_TEST(split_trailing_statement_without_semicolon) {
    // O caso mais comum no editor: o ultimo statement sem ';'.
    const auto parts = split_script("SELECT 1; SELECT 2", postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

OTTER_TEST(split_skips_empty_statements) {
    const auto parts = split_script(";;; SELECT 1 ;;;", postgres_dialect());
    OTTER_CHECK_EQ(parts.size(), std::size_t{1});
}

OTTER_TEST(statement_at_finds_enclosing) {
    constexpr std::string_view script = "SELECT 1; SELECT 2; SELECT 3";

    const auto found = statement_at(script, postgres_dialect(), 12);
    OTTER_CHECK(found.has_value());
    OTTER_CHECK(found->text.find('2') != std::string_view::npos);
}

// --- Analise de escopo (completion, ADR 0004 camada 2) ----------------------

OTTER_TEST(scope_detects_table_context_after_from) {
    constexpr std::string_view sql = "SELECT * FROM ";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());
    OTTER_CHECK(scope.context == CompletionContext::table_expected);
}

OTTER_TEST(scope_detects_column_context_after_select) {
    constexpr std::string_view sql = "SELECT ";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());
    OTTER_CHECK(scope.context == CompletionContext::column_expected);
}

OTTER_TEST(scope_detects_column_context_after_where) {
    constexpr std::string_view sql = "SELECT * FROM otters WHERE ";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());
    OTTER_CHECK(scope.context == CompletionContext::column_expected);
}

OTTER_TEST(scope_collects_tables_with_aliases) {
    constexpr std::string_view sql =
        "SELECT * FROM otters o JOIN rafts AS r ON r.id = o.raft_id WHERE ";

    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());

    OTTER_CHECK_EQ(scope.tables.size(), std::size_t{2});
    OTTER_CHECK_EQ(scope.tables[0].name, std::string{"otters"});
    OTTER_CHECK_EQ(scope.tables[0].alias, std::string{"o"});
    OTTER_CHECK_EQ(scope.tables[1].name, std::string{"rafts"});
    OTTER_CHECK_EQ(scope.tables[1].alias, std::string{"r"});
}

OTTER_TEST(scope_does_not_treat_clause_word_as_alias) {
    // "FROM otters WHERE" -- WHERE nao e' alias de otters.
    constexpr std::string_view sql = "SELECT * FROM otters WHERE ";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());

    OTTER_CHECK_EQ(scope.tables.size(), std::size_t{1});
    OTTER_CHECK(scope.tables[0].alias.empty());
}

OTTER_TEST(scope_detects_alias_member_after_dot) {
    constexpr std::string_view sql = "SELECT o. FROM otters o";
    // Cursor logo apos o ponto.
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), 9);

    OTTER_CHECK(scope.context == CompletionContext::alias_member);
    OTTER_CHECK_EQ(scope.qualifier, std::string{"o"});
}

OTTER_TEST(scope_sees_tables_declared_after_cursor) {
    // "SELECT <cursor> FROM otters" -- a tabela vem depois do cursor.
    constexpr std::string_view sql = "SELECT  FROM otters";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), 7);

    OTTER_CHECK(scope.context == CompletionContext::column_expected);
    OTTER_CHECK_EQ(scope.tables.size(), std::size_t{1});
    OTTER_CHECK_EQ(scope.tables[0].name, std::string{"otters"});
}

OTTER_TEST(scope_captures_partial_prefix) {
    constexpr std::string_view sql = "SELECT * FROM ott";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());
    OTTER_CHECK_EQ(scope.prefix, std::string{"ott"});
}

OTTER_TEST(scope_handles_qualified_table_name) {
    constexpr std::string_view sql = "SELECT * FROM public.otters o WHERE ";
    const ScopeInfo scope = analyze_scope(sql, postgres_dialect(), sql.size());

    OTTER_CHECK_EQ(scope.tables.size(), std::size_t{1});
    OTTER_CHECK_EQ(scope.tables[0].schema, std::string{"public"});
    OTTER_CHECK_EQ(scope.tables[0].name, std::string{"otters"});
    OTTER_CHECK_EQ(scope.tables[0].alias, std::string{"o"});
}

OTTER_TEST(scope_on_empty_input_does_not_crash) {
    const ScopeInfo scope = analyze_scope("", postgres_dialect(), 0);
    OTTER_CHECK(scope.context == CompletionContext::unknown);
    OTTER_CHECK(scope.tables.empty());
}
