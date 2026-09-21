// C-Otter -- sql/dialect.hpp
//
// Dialeto SQL como DADOS, nao codigo. Cada SGBD preenche esta estrutura; o
// lexer e o parser sao os mesmos para todos.
//
// E' a peca que o LexSQL do Scintilla nao tinha: ele trata dialetos so' por
// conjuntos de keywords e nao distingue $$ do PostgreSQL de DELIMITER do
// MySQL, nem [colchetes] do MSSQL de `crases` do MySQL (ADR 0003/0007).
#pragma once

#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace otter::sql {

// Como identificadores sao delimitados quando precisam de escape.
enum class QuoteStyle {
    double_quotes,   // "nome"    -- padrao SQL, PostgreSQL, Oracle
    backticks,       // `nome`    -- MySQL, MariaDB
    brackets,        // [nome]    -- SQL Server, Access
};

struct Dialect {
    std::string_view name;

    // Identificadores nao delimitados: PostgreSQL dobra para minusculas,
    // Oracle para maiusculas, MySQL depende do sistema de arquivos.
    enum class FoldCase { lower, upper, none };
    FoldCase unquoted_case = FoldCase::lower;

    // Comparacao de identificadores delimitados diferencia maiusculas?
    bool quoted_case_sensitive = true;

    std::vector<QuoteStyle> identifier_quotes{QuoteStyle::double_quotes};

    // Delimitador de statement. MySQL permite trocar com DELIMITER.
    char statement_separator = ';';

    // Corpo de funcao delimitado por tag: $$ ... $$ ou $tag$ ... $tag$.
    // Exclusivo do PostgreSQL, e a razao pela qual splitters ingenuos quebram.
    bool dollar_quoted_strings = false;

    // \n dentro de string literal e' escape? Padrao SQL diz que nao.
    bool backslash_escapes = false;

    // Comentario aninhado /* /* */ */ -- PostgreSQL aceita, muitos nao.
    bool nested_block_comments = false;

    // MySQL aceita # como comentario de linha, alem de --.
    bool hash_line_comments = false;

    std::unordered_set<std::string_view> keywords;
    std::unordered_set<std::string_view> functions;
    std::unordered_set<std::string_view> types;

    [[nodiscard]] bool is_keyword(std::string_view word) const;
    [[nodiscard]] bool is_function(std::string_view word) const;
    [[nodiscard]] bool is_type(std::string_view word) const;

    // Delimita um identificador conforme as regras deste dialeto.
    [[nodiscard]] std::string quote_identifier(std::string_view identifier) const;

    // Identificador precisa de delimitador? (palavra reservada, caractere
    // especial, comeca com digito, caixa nao canonica)
    [[nodiscard]] bool needs_quoting(std::string_view identifier) const;
};

[[nodiscard]] const Dialect& postgres_dialect();
[[nodiscard]] const Dialect& mysql_dialect();
[[nodiscard]] const Dialect& mssql_dialect();
[[nodiscard]] const Dialect& sqlite_dialect();
[[nodiscard]] const Dialect& standard_dialect();

// Procura por identificador de driver ("postgresql", "mysql", ...).
[[nodiscard]] const Dialect& dialect_for(std::string_view driver_id);

} // namespace otter::sql
