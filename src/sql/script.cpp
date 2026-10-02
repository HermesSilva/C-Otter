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

namespace {

// --- SQL Server: lotes -------------------------------------------------------------

// O token e' um `GO` sozinho na linha (`GO` ou `GO 5`)?
bool is_go_line(const std::vector<Token>& tokens, std::size_t index) {
    const Token& token = tokens[index];
    if (!iequals(token.text, "GO")) return false;
    if (token.kind != TokenKind::keyword && token.kind != TokenKind::identifier) return false;

    // Primeiro da linha...
    if (index > 0 && tokens[index - 1].line == token.line) return false;

    // ...e ultimo, salvo pelo numero de repeticoes ("GO 5").
    std::size_t next = index + 1;
    if (next < tokens.size() && tokens[next].kind == TokenKind::number &&
        tokens[next].line == token.line) {
        ++next;
    }
    return next >= tokens.size() || tokens[next].kind == TokenKind::end_of_input ||
           tokens[next].line != token.line;
}

// O lote precisa ir inteiro para o servidor?
bool is_atomic_batch(const std::vector<Token>& tokens, std::size_t first,
                     std::size_t last) {
    if (first >= last) return false;

    // CREATE | ALTER [OR ALTER] PROC[EDURE] | FUNCTION | TRIGGER | VIEW: o
    // corpo vai ate' o fim do lote, com ou sem BEGIN ... END.
    if (iequals(tokens[first].text, "CREATE") || iequals(tokens[first].text, "ALTER")) {
        std::size_t i = first + 1;
        // CREATE OR ALTER (SQL Server), CREATE OR REPLACE (SQL Anywhere).
        if (i + 1 < last && iequals(tokens[i].text, "OR") &&
            (iequals(tokens[i + 1].text, "ALTER") || iequals(tokens[i + 1].text, "REPLACE"))) {
            i += 2;
        }
        if (i < last) {
            const std::string_view kind = tokens[i].text;
            if (iequals(kind, "PROC") || iequals(kind, "PROCEDURE") ||
                iequals(kind, "FUNCTION") || iequals(kind, "TRIGGER") ||
                iequals(kind, "VIEW") ||
                // SQL Anywhere: CREATE EVENT ... HANDLER BEGIN ... END.
                iequals(kind, "EVENT")) {
                return true;
            }
        }
    }

    // DECLARE: a variavel so' existe ate' o fim do lote. Partir no ';' faria
    // o comando seguinte falhar com "Must declare the scalar variable".
    for (std::size_t i = first; i < last; ++i) {
        if (iequals(tokens[i].text, "DECLARE") &&
            (tokens[i].kind == TokenKind::keyword || tokens[i].kind == TokenKind::identifier)) {
            return true;
        }
    }
    return false;
}

// Ha' um BEGIN que abre bloco entre os tokens [first, last)?
bool has_block(const std::vector<Token>& tokens, std::size_t first, std::size_t last) {
    for (std::size_t i = first; i < last; ++i) {
        if (tokens[i].kind != TokenKind::keyword || !iequals(tokens[i].text, "BEGIN")) continue;
        const bool transaction =
            i + 1 < last && (iequals(tokens[i + 1].text, "TRANSACTION") ||
                             iequals(tokens[i + 1].text, "TRAN"));
        if (!transaction) return true;
    }
    return false;
}

std::vector<Statement> split_batches(std::string_view script, const Dialect& dialect) {
    std::vector<Statement> statements;

    // Watcom SQL (SQL Anywhere): o corpo de procedure, funcao, trigger e
    // evento e' SEMPRE um bloco BEGIN ... END, e o comando acaba no END dele --
    // o que vem depois do ';' e' outro comando, como no dbisql. So' a forma
    // T-SQL ("CREATE PROCEDURE p AS ..."), sem bloco, vai ate' o fim do lote.
    //
    // Mandar o lote inteiro, como no SQL Server, fazia "CREATE PROCEDURE ...
    // END; SELECT 1;" chegar ao servidor como um comando so', que ele recusa.
    const bool watcom = dialect.name == "SQL Anywhere";

    Lexer lexer(script, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/true);

    const auto emit = [&](std::size_t begin, std::size_t end, std::size_t line,
                          bool atomic) {
        Statement statement;
        statement.text   = script.substr(begin, end - begin);
        statement.offset = begin;
        statement.line   = line;
        statement.atomic = atomic;
        if (!statement.empty()) statements.push_back(statement);
    };

    // Um lote: os tokens [first, last), o texto [begin, end).
    const auto batch = [&](std::size_t first, std::size_t last, std::size_t begin,
                           std::size_t end) {
        if (first >= last) return;
        const std::size_t line = tokens[first].line;

        if (is_atomic_batch(tokens, first, last) &&
            !(watcom && has_block(tokens, first, last))) {
            emit(begin, end, line, /*atomic=*/true);
            return;
        }

        // Como nos outros SGBDs: ';' fora de BEGIN ... END.
        const std::size_t first_statement = statements.size();
        std::size_t start = begin;
        std::size_t start_line = line;
        std::size_t start_token = first;
        int block_depth = 0;

        const auto piece = [&](std::size_t piece_end) {
            // ELSE depois do ';' continua o IF de antes: "IF x SELECT 1; ELSE
            // SELECT 2;" e' um comando so'.
            if (start_token < last && iequals(tokens[start_token].text, "ELSE") &&
                statements.size() > first_statement) {
                Statement& previous = statements.back();
                previous.text = script.substr(previous.offset, piece_end - previous.offset);
                return;
            }
            emit(start, piece_end, start_line, /*atomic=*/false);
        };

        for (std::size_t i = first; i < last; ++i) {
            const Token& token = tokens[i];
            if (token.kind == TokenKind::keyword) {
                if (iequals(token.text, "BEGIN") || iequals(token.text, "CASE")) {
                    // BEGIN TRANSACTION nao abre bloco.
                    const bool transaction =
                        i + 1 < last && (iequals(tokens[i + 1].text, "TRANSACTION") ||
                                         iequals(tokens[i + 1].text, "TRAN") ||
                                         iequals(tokens[i + 1].text, "DISTRIBUTED"));
                    if (!transaction) ++block_depth;
                } else if (iequals(token.text, "END") && block_depth > 0) {
                    // Watcom SQL: END IF, END LOOP, END FOR e END WHILE fecham
                    // o que nao foi contado; END CASE fecha o CASE -- e o
                    // CASE que o segue nao abre outro.
                    const std::string_view after =
                        i + 1 < last ? tokens[i + 1].text : std::string_view{};
                    if (watcom && (iequals(after, "IF") || iequals(after, "LOOP") ||
                                   iequals(after, "FOR") || iequals(after, "WHILE"))) {
                        continue;
                    }
                    --block_depth;
                    if (watcom && iequals(after, "CASE")) ++i;
                }
            }
            if (token.kind != TokenKind::semicolon || block_depth != 0) continue;

            piece(token.offset);
            start       = token.offset + token.text.size();
            start_line  = token.line;
            start_token = i + 1;
        }
        if (start < end) piece(end);
    };

    std::size_t first = 0;
    std::size_t begin = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].kind == TokenKind::end_of_input) break;
        if (!is_go_line(tokens, i)) continue;

        batch(first, i, begin, tokens[i].offset);

        // Pula o numero de repeticoes, se houver.
        std::size_t next = i + 1;
        if (next < tokens.size() && tokens[next].kind == TokenKind::number &&
            tokens[next].line == tokens[i].line) {
            ++next;
        }
        begin = tokens[next - 1].offset + tokens[next - 1].text.size();
        first = next;
        i     = next - 1;
    }

    std::size_t last = tokens.size();
    while (last > first && tokens[last - 1].kind == TokenKind::end_of_input) --last;
    batch(first, last, begin, script.size());
    return statements;
}

} // namespace

std::vector<Statement> split_script(std::string_view script,
                                    const Dialect& dialect) {
    if (dialect.go_batch_separator) return split_batches(script, dialect);

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
