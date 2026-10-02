#include "sql/lexer.hpp"

#include <cctype>

namespace otter::sql {
namespace {

bool is_ident_start(char c) {
    // '_' e caracteres acima de 0x7F: identificadores UTF-8 sao validos em
    // PostgreSQL e MySQL.
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool is_ident_char(char c) {
    return is_ident_start(c) || std::isdigit(static_cast<unsigned char>(c)) != 0 ||
           c == '$';   // MySQL e Oracle aceitam $ em identificadores
}

// A tag de um dollar-quoted string NAO pode conter '$' -- senao o '$' de
// fechamento da abertura seria engolido como parte da tag, e $$ nunca seria
// reconhecido.
bool is_dollar_tag_char(char c) {
    return is_ident_start(c) || std::isdigit(static_cast<unsigned char>(c)) != 0;
}

char closing_for(QuoteStyle style) {
    switch (style) {
        case QuoteStyle::backticks:     return '`';
        case QuoteStyle::brackets:      return ']';
        case QuoteStyle::double_quotes: return '"';
    }
    return '"';
}

char opening_for(QuoteStyle style) {
    switch (style) {
        case QuoteStyle::backticks:     return '`';
        case QuoteStyle::brackets:      return '[';
        case QuoteStyle::double_quotes: return '"';
    }
    return '"';
}

} // namespace

std::string_view to_string(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::end_of_input:      return "end";
        case TokenKind::whitespace:        return "whitespace";
        case TokenKind::line_comment:      return "line comment";
        case TokenKind::block_comment:     return "block comment";
        case TokenKind::keyword:           return "keyword";
        case TokenKind::identifier:        return "identifier";
        case TokenKind::quoted_identifier: return "quoted identifier";
        case TokenKind::number:            return "number";
        case TokenKind::string:            return "string";
        case TokenKind::dollar_string:     return "dollar string";
        case TokenKind::parameter:         return "parameter";
        case TokenKind::operator_token:    return "operator";
        case TokenKind::punctuation:       return "punctuation";
        case TokenKind::semicolon:         return "semicolon";
        case TokenKind::invalid:           return "invalid";
    }
    return "unknown";
}

std::string_view Token::unquoted() const {
    if (kind != TokenKind::quoted_identifier || text.size() < 2) return text;
    return text.substr(1, text.size() - 2);
}

Lexer::Lexer(std::string_view input, const Dialect& dialect)
    : input_(input), dialect_(&dialect) {}

char Lexer::peek(std::size_t ahead) const noexcept {
    const std::size_t index = position_ + ahead;
    return index < input_.size() ? input_[index] : '\0';
}

void Lexer::advance(std::size_t count) {
    for (std::size_t i = 0; i < count && position_ < input_.size(); ++i) {
        if (input_[position_] == '\n') {
            ++line_;
            column_ = 0;
        } else {
            ++column_;
        }
        ++position_;
    }
}

Token Lexer::make(TokenKind kind, std::size_t start, std::size_t start_line,
                  std::size_t start_column) {
    Token token;
    token.kind   = kind;
    token.text   = input_.substr(start, position_ - start);
    token.offset = start;
    token.line   = start_line;
    token.column = start_column;
    return token;
}

Token Lexer::next() {
    if (at_end()) {
        return make(TokenKind::end_of_input, position_, line_, column_);
    }

    const char c = peek();

    if (std::isspace(static_cast<unsigned char>(c))) return lex_whitespace();

    // Comentario de linha: -- ou # (MySQL).
    if (c == '-' && peek(1) == '-') return lex_line_comment();
    if (c == '#' && dialect_->hash_line_comments) return lex_line_comment();
    if (c == '/' && peek(1) == '/' && dialect_->slash_line_comments) {
        return lex_line_comment();
    }

    if (c == '/' && peek(1) == '*') return lex_block_comment();

    if (c == '\'') return lex_string();

    // $$ ... $$ do PostgreSQL. Precisa vir antes do tratamento de parametro,
    // porque $1 tambem comeca com '$'.
    if (c == '$' && dialect_->dollar_quoted_strings) {
        // $tag$ ou $$: apos o '$' vem tag alfanumerica e outro '$'.
        std::size_t scan = position_ + 1;
        while (scan < input_.size() && is_dollar_tag_char(input_[scan])) ++scan;
        if (scan < input_.size() && input_[scan] == '$') return lex_dollar_string();
    }

    if (c == '?' || c == ':' || c == '@' ||
        (c == '$' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
        return lex_parameter();
    }

    // Identificador delimitado, conforme os estilos do dialeto.
    for (QuoteStyle style : dialect_->identifier_quotes) {
        if (c == opening_for(style)) {
            return lex_quoted_identifier(c, closing_for(style));
        }
    }

    if (std::isdigit(static_cast<unsigned char>(c))) return lex_number();

    // .5 e' numero; . sozinho e' separador de qualificacao.
    if (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        return lex_number();
    }

    if (is_ident_start(c)) return lex_word();

    if (c == ';') {
        const std::size_t start = position_;
        const std::size_t l = line_, col = column_;
        advance();
        return make(TokenKind::semicolon, start, l, col);
    }

    return lex_operator();
}

Token Lexer::lex_whitespace() {
    const std::size_t start = position_, l = line_, col = column_;
    while (!at_end() && std::isspace(static_cast<unsigned char>(peek()))) advance();
    return make(TokenKind::whitespace, start, l, col);
}

Token Lexer::lex_line_comment() {
    const std::size_t start = position_, l = line_, col = column_;
    while (!at_end() && peek() != '\n') advance();
    return make(TokenKind::line_comment, start, l, col);
}

Token Lexer::lex_block_comment() {
    const std::size_t start = position_, l = line_, col = column_;
    advance(2);   // consome /*

    int depth = 1;
    while (!at_end() && depth > 0) {
        if (peek() == '*' && peek(1) == '/') {
            advance(2);
            --depth;
        } else if (dialect_->nested_block_comments && peek() == '/' && peek(1) == '*') {
            // PostgreSQL aninha comentarios; a maioria dos SGBDs nao.
            advance(2);
            ++depth;
        } else {
            advance();
        }
    }

    // Comentario nao fechado: token invalido, mas o lexer segue -- o usuario
    // pode estar no meio de digitar.
    return make(depth == 0 ? TokenKind::block_comment : TokenKind::invalid,
                start, l, col);
}

Token Lexer::lex_number() {
    const std::size_t start = position_, l = line_, col = column_;

    while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();

    if (peek() == '.') {
        advance();
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }

    // Notacao cientifica: 1e10, 1.5E-3.
    if (peek() == 'e' || peek() == 'E') {
        const std::size_t save = position_;
        advance();
        if (peek() == '+' || peek() == '-') advance();

        if (std::isdigit(static_cast<unsigned char>(peek()))) {
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else {
            position_ = save;   // nao era expoente: rebobina
        }
    }

    return make(TokenKind::number, start, l, col);
}

Token Lexer::lex_string() {
    const std::size_t start = position_, l = line_, col = column_;
    advance();   // consome a aspa inicial

    bool closed = false;
    while (!at_end()) {
        const char c = peek();

        if (c == '\\' && dialect_->backslash_escapes) {
            advance(2);   // MySQL: \' nao fecha a string
            continue;
        }
        if (c == '\'') {
            if (peek(1) == '\'') {
                advance(2);   // '' e' aspa escapada, padrao SQL
                continue;
            }
            advance();
            closed = true;
            break;
        }
        advance();
    }

    return make(closed ? TokenKind::string : TokenKind::invalid, start, l, col);
}

Token Lexer::lex_dollar_string() {
    const std::size_t start = position_, l = line_, col = column_;

    // Captura a tag: $tag$ ou $$.
    advance();   // consome o '$' inicial
    const std::size_t tag_start = position_;
    while (!at_end() && is_dollar_tag_char(peek())) advance();
    const std::string_view tag = input_.substr(tag_start, position_ - tag_start);

    if (peek() != '$') {
        // Nao era dollar-quoting; trata como operador de um caractere.
        position_ = start;
        return lex_operator();
    }
    advance();   // consome o '$' de fechamento da abertura

    // Procura o delimitador de fechamento identico.
    const std::string closing = "$" + std::string(tag) + "$";
    const std::size_t found = input_.find(closing, position_);

    if (found == std::string_view::npos) {
        // Nao fechado: consome o resto. Comum enquanto se digita uma funcao.
        while (!at_end()) advance();
        return make(TokenKind::invalid, start, l, col);
    }

    advance(found + closing.size() - position_);
    return make(TokenKind::dollar_string, start, l, col);
}

Token Lexer::lex_quoted_identifier(char open, char close) {
    const std::size_t start = position_, l = line_, col = column_;
    advance();   // consome o delimitador de abertura

    bool closed = false;
    while (!at_end()) {
        if (peek() == close) {
            // Delimitador dobrado e' escape: "a""b" -> a"b.
            if (peek(1) == close && open != '[') {
                advance(2);
                continue;
            }
            advance();
            closed = true;
            break;
        }
        advance();
    }

    return make(closed ? TokenKind::quoted_identifier : TokenKind::invalid,
                start, l, col);
}

Token Lexer::lex_word() {
    const std::size_t start = position_, l = line_, col = column_;
    while (!at_end() && is_ident_char(peek())) advance();

    const std::string_view word = input_.substr(start, position_ - start);

    // Prefixos de literal: N'...' (Unicode), B'...' (bit), X'...' (hex),
    // E'...' (escape do PostgreSQL). O prefixo pertence a string.
    if ((word.size() == 1) && peek() == '\'') {
        const char prefix = static_cast<char>(
            std::toupper(static_cast<unsigned char>(word[0])));
        if (prefix == 'N' || prefix == 'B' || prefix == 'X' || prefix == 'E') {
            const Token string_token = lex_string();
            return make(string_token.kind == TokenKind::invalid
                            ? TokenKind::invalid
                            : TokenKind::string,
                        start, l, col);
        }
    }

    return make(dialect_->is_keyword(word) ? TokenKind::keyword
                                           : TokenKind::identifier,
                start, l, col);
}

Token Lexer::lex_parameter() {
    const std::size_t start = position_, l = line_, col = column_;
    const char c = peek();
    advance();

    if (c == '?') return make(TokenKind::parameter, start, l, col);

    // $1 (PostgreSQL), :nome (Oracle), @nome (SQL Server).
    if (c == '$') {
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
    } else {
        // ':' seguido de ':' e' o cast do PostgreSQL, nao um parametro.
        if (c == ':' && peek() == ':') {
            advance();
            return make(TokenKind::operator_token, start, l, col);
        }
        while (!at_end() && is_ident_char(peek())) advance();
    }

    // ':' ou '@' sozinho nao e' parametro valido.
    if (position_ - start == 1) return make(TokenKind::operator_token, start, l, col);

    return make(TokenKind::parameter, start, l, col);
}

Token Lexer::lex_operator() {
    const std::size_t start = position_, l = line_, col = column_;

    // Operadores de dois caracteres, verificados antes dos de um.
    static constexpr std::string_view kTwoChar[] = {
        "<>", "!=", "<=", ">=", "||", "::", "->", "=>", ":=", "<<", ">>",
    };

    const char c = peek();
    const char n = peek(1);
    for (std::string_view op : kTwoChar) {
        if (c == op[0] && n == op[1]) {
            advance(2);
            return make(TokenKind::operator_token, start, l, col);
        }
    }

    advance();

    const bool punctuation = c == '(' || c == ')' || c == ',' || c == '.' ||
                             c == '[' || c == ']';
    return make(punctuation ? TokenKind::punctuation : TokenKind::operator_token,
                start, l, col);
}

std::vector<Token> Lexer::tokenize_all(bool skip_trivia) {
    std::vector<Token> tokens;
    for (;;) {
        Token token = next();
        const bool done = token.kind == TokenKind::end_of_input;

        if (!done && skip_trivia && token.is_trivia()) continue;
        tokens.push_back(std::move(token));
        if (done) break;
    }
    return tokens;
}

} // namespace otter::sql
