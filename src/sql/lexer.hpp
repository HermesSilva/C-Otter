// C-Otter -- sql/lexer.hpp
//
// Tokenizador de SQL dirigido pelo dialeto. E' a fonte unica de verdade para
// realce, splitter, parser e completion -- nunca dois lexers divergentes
// (ADR 0007).
#pragma once

#include "sql/dialect.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace otter::sql {

enum class TokenKind : std::uint8_t {
    end_of_input,
    whitespace,
    line_comment,      // -- ... ou # ... no MySQL
    block_comment,     // /* ... */
    keyword,
    identifier,        // nome nao delimitado
    quoted_identifier, // "nome", `nome`, [nome]
    number,
    string,            // 'literal'
    dollar_string,     // $$ ... $$ do PostgreSQL
    parameter,         // ?, $1, :nome, @nome
    operator_token,
    punctuation,
    semicolon,
    invalid,           // token malformado: string nao fechada, etc.
};

[[nodiscard]] std::string_view to_string(TokenKind kind) noexcept;

struct Token {
    TokenKind        kind = TokenKind::end_of_input;
    std::string_view text;       // fatia da entrada original
    std::size_t      offset = 0; // posicao em bytes
    std::size_t      line = 0;   // base zero
    std::size_t      column = 0; // base zero, em bytes

    // Para identificadores delimitados: o nome sem os delimitadores.
    [[nodiscard]] std::string_view unquoted() const;

    [[nodiscard]] bool is_trivia() const noexcept {
        return kind == TokenKind::whitespace ||
               kind == TokenKind::line_comment ||
               kind == TokenKind::block_comment;
    }
};

// Tokenizador de uma passada. Tolerante a erro: entrada malformada produz
// TokenKind::invalid e o lexer segue -- o editor precisa tokenizar texto
// incompleto enquanto o usuario digita (ADR 0004).
class Lexer {
public:
    Lexer(std::string_view input, const Dialect& dialect);

    [[nodiscard]] Token next();

    // Tokeniza tudo. `skip_trivia` descarta espacos e comentarios.
    [[nodiscard]] std::vector<Token> tokenize_all(bool skip_trivia = false);

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

private:
    [[nodiscard]] char peek(std::size_t ahead = 0) const noexcept;
    [[nodiscard]] bool at_end() const noexcept { return position_ >= input_.size(); }
    void advance(std::size_t count = 1);

    [[nodiscard]] Token make(TokenKind kind, std::size_t start,
                             std::size_t start_line, std::size_t start_column);

    [[nodiscard]] Token lex_whitespace();
    [[nodiscard]] Token lex_line_comment();
    [[nodiscard]] Token lex_block_comment();
    [[nodiscard]] Token lex_number();
    [[nodiscard]] Token lex_string();
    [[nodiscard]] Token lex_dollar_string();
    [[nodiscard]] Token lex_quoted_identifier(char open, char close);
    [[nodiscard]] Token lex_word();
    [[nodiscard]] Token lex_parameter();
    [[nodiscard]] Token lex_operator();

    std::string_view input_;
    const Dialect*   dialect_;
    std::size_t      position_ = 0;
    std::size_t      line_ = 0;
    std::size_t      column_ = 0;
};

} // namespace otter::sql
