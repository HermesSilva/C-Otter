#include "sql/script.hpp"

#include <algorithm>
#include <cctype>

namespace otter::sql {
namespace {

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return false;
    }
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// Palavras que introduzem uma tabela.
bool introduces_table(std::string_view word) {
    static constexpr std::string_view kWords[] = {
        "FROM", "JOIN", "INTO", "UPDATE", "TABLE",
    };
    const std::string up = upper(word);
    return std::find(std::begin(kWords), std::end(kWords), up) != std::end(kWords);
}

// Palavras apos as quais se espera uma coluna.
bool introduces_column(std::string_view word) {
    static constexpr std::string_view kWords[] = {
        "SELECT", "WHERE", "ON", "AND", "OR", "BY", "SET", "HAVING",
        "DISTINCT", "USING",
    };
    const std::string up = upper(word);
    return std::find(std::begin(kWords), std::end(kWords), up) != std::end(kWords);
}

// Palavras que NAO podem ser alias de tabela -- "FROM x WHERE" nao faz de
// WHERE um alias.
bool is_clause_boundary(std::string_view word) {
    static constexpr std::string_view kWords[] = {
        "WHERE", "GROUP", "ORDER", "HAVING", "LIMIT", "OFFSET", "JOIN",
        "INNER", "LEFT", "RIGHT", "FULL", "CROSS", "ON", "USING", "SET",
        "VALUES", "UNION", "INTERSECT", "EXCEPT", "RETURNING", "WINDOW",
        "FETCH", "FOR",
    };
    const std::string up = upper(word);
    return std::find(std::begin(kWords), std::end(kWords), up) != std::end(kWords);
}

} // namespace

bool Statement::empty() const noexcept { return trim(text).empty(); }

std::vector<Statement> split_script(std::string_view script,
                                    const Dialect& dialect) {
    std::vector<Statement> statements;

    Lexer lexer(script, dialect);
    std::size_t start = 0;
    std::size_t start_line = 0;
    int block_depth = 0;   // profundidade de BEGIN ... END

    // DELIMITER do MySQL troca o separador em tempo de execucao.
    std::string custom_delimiter;

    for (;;) {
        const Token token = lexer.next();
        if (token.kind == TokenKind::end_of_input) break;

        if (token.kind == TokenKind::keyword) {
            const std::string word = upper(token.text);

            // BEGIN/END delimitam corpo de funcao: o ';' interno nao encerra
            // o statement externo.
            if (word == "BEGIN" || word == "CASE") {
                ++block_depth;
            } else if (word == "END") {
                if (block_depth > 0) --block_depth;
            } else if (word == "DELIMITER") {
                // A proxima palavra vira o novo separador.
                Token next = lexer.next();
                while (next.is_trivia()) next = lexer.next();
                custom_delimiter = std::string(next.text);
                start = lexer.position();
                continue;
            }
        }

        // Separador padrao, so' fora de bloco.
        const bool at_separator =
            token.kind == TokenKind::semicolon && block_depth == 0 &&
            custom_delimiter.empty();

        // Separador customizado do MySQL (ex.: //).
        const bool at_custom =
            !custom_delimiter.empty() && token.text == custom_delimiter;

        if (at_separator || at_custom) {
            Statement statement;
            statement.text   = script.substr(start, token.offset - start);
            statement.offset = start;
            statement.line   = start_line;

            if (!statement.empty()) statements.push_back(statement);

            start = lexer.position();
            start_line = token.line;
        }
    }

    // Resto sem separador final -- o caso mais comum no editor.
    if (start < script.size()) {
        Statement statement;
        statement.text   = script.substr(start);
        statement.offset = start;
        statement.line   = start_line;
        if (!statement.empty()) statements.push_back(statement);
    }

    return statements;
}

std::optional<Statement> statement_at(std::string_view script,
                                      const Dialect& dialect,
                                      std::size_t offset) {
    for (const Statement& statement : split_script(script, dialect)) {
        const std::size_t end = statement.offset + statement.text.size();
        if (offset >= statement.offset && offset <= end) return statement;
    }
    return std::nullopt;
}

ScopeInfo analyze_scope(std::string_view script, const Dialect& dialect,
                        std::size_t cursor_offset) {
    ScopeInfo scope;

    const std::optional<Statement> statement =
        statement_at(script, dialect, cursor_offset);
    if (!statement.has_value()) return scope;

    // Tokeniza o statement inteiro: as tabelas podem estar depois do cursor,
    // como em "SELECT <cursor> FROM pedidos".
    Lexer lexer(statement->text, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/true);

    const std::size_t local_cursor = cursor_offset - statement->offset;

    // --- Tabelas referenciadas, com alias ----------------------------------
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];
        if (token.kind != TokenKind::keyword || !introduces_table(token.text)) {
            continue;
        }

        // O proximo identificador e' a tabela; pode ser schema.tabela.
        std::size_t j = i + 1;
        if (j >= tokens.size()) break;

        const bool is_name = tokens[j].kind == TokenKind::identifier ||
                             tokens[j].kind == TokenKind::quoted_identifier;
        if (!is_name) continue;

        TableRef ref;
        ref.name = std::string(tokens[j].unquoted());
        ++j;

        // Qualificado: schema.tabela
        if (j + 1 < tokens.size() && tokens[j].text == "." &&
            (tokens[j + 1].kind == TokenKind::identifier ||
             tokens[j + 1].kind == TokenKind::quoted_identifier)) {
            ref.schema = ref.name;
            ref.name   = std::string(tokens[j + 1].unquoted());
            j += 2;
        }

        // Alias, com ou sem AS. Uma palavra de clausula nao e' alias.
        if (j < tokens.size()) {
            if (tokens[j].kind == TokenKind::keyword &&
                iequals(tokens[j].text, "AS")) {
                ++j;
            }
            if (j < tokens.size() &&
                (tokens[j].kind == TokenKind::identifier ||
                 tokens[j].kind == TokenKind::quoted_identifier) &&
                !is_clause_boundary(tokens[j].text)) {
                ref.alias = std::string(tokens[j].unquoted());
            }
        }

        scope.tables.push_back(std::move(ref));
    }

    // --- Contexto na posicao do cursor -------------------------------------
    // Ultimo token que termina antes ou no cursor.
    const Token* previous = nullptr;
    const Token* before_previous = nullptr;

    // O token sob o cursor e' a palavra parcial sendo digitada; `previous` e'
    // o que vem ANTES dela, e e' ele que determina o contexto.
    for (const Token& token : tokens) {
        if (token.offset > local_cursor) break;

        // O marcador de fim tem offset igual ao do cursor no fim do texto;
        // deixa-lo virar `previous` esconderia o token real que da' contexto.
        if (token.kind == TokenKind::end_of_input) break;

        const std::size_t end = token.offset + token.text.size();

        // Cursor DENTRO do token (nao apenas encostado). Em "SELECT * FROM |"
        // o cursor vem depois de um espaco, entao FROM nao e' palavra parcial
        // e permanece disponivel como `previous`.
        const bool inside = token.offset < local_cursor && local_cursor <= end;

        if (inside && (token.kind == TokenKind::identifier ||
                       token.kind == TokenKind::keyword)) {
            scope.prefix = std::string(
                token.text.substr(0, local_cursor - token.offset));
            continue;   // nao conta como "token anterior"
        }

        before_previous = previous;
        previous = &token;
    }

    if (previous == nullptr) return scope;

    // "alias." ou "schema." -- o ponto restringe o que sugerir.
    if (previous->text == "." && before_previous != nullptr &&
        (before_previous->kind == TokenKind::identifier ||
         before_previous->kind == TokenKind::quoted_identifier)) {
        scope.qualifier = std::string(before_previous->unquoted());

        // Se casa com um alias ou nome de tabela, sao colunas daquela tabela.
        const bool matches_table = std::any_of(
            scope.tables.begin(), scope.tables.end(), [&](const TableRef& t) {
                return iequals(t.alias, scope.qualifier) ||
                       iequals(t.name, scope.qualifier);
            });

        scope.context = matches_table ? CompletionContext::alias_member
                                      : CompletionContext::schema_member;
        return scope;
    }

    if (previous->kind == TokenKind::keyword) {
        if (introduces_table(previous->text)) {
            scope.context = CompletionContext::table_expected;
        } else if (introduces_column(previous->text)) {
            scope.context = CompletionContext::column_expected;
        }
        return scope;
    }

    // Depois de vírgula, continua o que a clausula pedia.
    if (previous->text == "," && before_previous != nullptr) {
        for (const Token& token : tokens) {
            if (&token == previous) break;
            if (token.kind != TokenKind::keyword) continue;

            if (introduces_table(token.text)) {
                scope.context = CompletionContext::table_expected;
            } else if (introduces_column(token.text)) {
                scope.context = CompletionContext::column_expected;
            }
        }
    }

    return scope;
}

} // namespace otter::sql
