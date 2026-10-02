#include "sql/editing.hpp"

#include "sql/lexer.hpp"
#include "sql/script.hpp"

#include <algorithm>
#include <cctype>

namespace otter::sql {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Byte de continuacao UTF-8 (10xxxxxx): faz parte do caractere anterior.
bool is_continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

// Palavras que COMECAM uma instrucao. Uma linha em branco seguida de uma
// delas separa duas instrucoes mesmo sem ';' -- o modo "smart" do DBeaver
// (SQLPreferenceConstants.STATEMENT_DELIMITER_BLANK). Seguida de outra coisa
// (AND, FROM, um nome de coluna), a linha em branco e' so' respiro dentro da
// mesma consulta.
bool starts_statement(std::string_view word) {
    static constexpr std::string_view kStarters[] = {
        "SELECT", "INSERT", "UPDATE", "DELETE", "MERGE", "WITH", "VALUES",
        "CREATE", "ALTER", "DROP", "TRUNCATE", "GRANT", "REVOKE", "COMMENT",
        "BEGIN", "START", "COMMIT", "ROLLBACK", "SAVEPOINT", "RELEASE",
        "EXPLAIN", "ANALYZE", "VACUUM", "SHOW", "SET", "RESET", "USE",
        "CALL", "DO", "EXECUTE", "PREPARE", "DEALLOCATE", "COPY", "LOCK",
        "REFRESH", "REINDEX", "CLUSTER", "DESCRIBE", "DESC", "TABLE",
    };
    const std::string name = upper(word);
    return std::any_of(std::begin(kStarters), std::end(kStarters),
                       [&name](std::string_view starter) { return starter == name; });
}

// Tira o espaco em branco das pontas de [begin, end).
StatementRange trimmed(std::string_view script, std::size_t begin,
                       std::size_t end) {
    while (begin < end && is_space(script[begin])) ++begin;
    while (end > begin && is_space(script[end - 1])) --end;
    return {begin, end};
}

// Parte uma instrucao (ja' separada por ';') nas linhas em branco que
// antecedem uma palavra de inicio de instrucao.
void split_on_blank_lines(std::string_view script, const Dialect& dialect,
                          StatementRange whole,
                          std::vector<StatementRange>& out) {
    const std::string_view text = script.substr(whole.begin, whole.end - whole.begin);

    Lexer lexer(text, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/false);

    std::size_t start = 0;
    int parens = 0;
    int blocks = 0;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& token = tokens[i];

        if (token.kind == TokenKind::punctuation ||
            token.kind == TokenKind::operator_token) {
            if (token.text == "(") ++parens;
            else if (token.text == ")" && parens > 0) --parens;
        } else if (token.kind == TokenKind::keyword) {
            const std::string word = upper(token.text);
            // Dentro de BEGIN ... END (corpo de rotina) e de CASE, a linha em
            // branco nunca separa: partiria um CREATE PROCEDURE ao meio.
            if (word == "CASE" || word == "BEGIN") ++blocks;
            else if (word == "END" && blocks > 0) --blocks;
        }

        if (token.kind != TokenKind::whitespace || parens > 0 || blocks > 0) {
            continue;
        }

        // Linha em branco = dois '\n' no mesmo trecho de espaco.
        if (std::count(token.text.begin(), token.text.end(), '\n') < 2) continue;

        // O que vem depois, pulando comentarios.
        std::size_t next = i + 1;
        while (next < tokens.size() && tokens[next].is_trivia()) ++next;
        if (next >= tokens.size()) continue;
        if (tokens[next].kind != TokenKind::keyword ||
            !starts_statement(tokens[next].text)) {
            continue;
        }

        // So' parte se ja' ha' ALGO antes: um comentario solto no topo,
        // seguido de linha em branco, pertence a' instrucao de baixo.
        bool has_code = false;
        for (std::size_t k = 0; k < i; ++k) {
            if (tokens[k].offset >= start && !tokens[k].is_trivia()) {
                has_code = true;
                break;
            }
        }
        if (!has_code) continue;

        const StatementRange piece =
            trimmed(script, whole.begin + start, whole.begin + token.offset);
        if (!piece.empty()) out.push_back(piece);
        start = token.offset + token.text.size();
    }

    const StatementRange last = trimmed(script, whole.begin + start, whole.end);
    if (!last.empty()) out.push_back(last);
}

char closing_of(char open) noexcept {
    switch (open) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default:  return 0;
    }
}

char opening_of(char close) noexcept {
    switch (close) {
        case ')': return '(';
        case ']': return '[';
        case '}': return '{';
        default:  return 0;
    }
}

} // namespace

// --- Posicao ---------------------------------------------------------------------

std::size_t offset_of(std::string_view text, TextPosition position) {
    std::size_t offset = 0;
    std::size_t line = 0;
    while (line < position.line && offset < text.size()) {
        if (text[offset] == '\n') ++line;
        ++offset;
    }

    // `column` conta caracteres; avanca um ponto de codigo por vez, sem
    // passar do fim da linha.
    std::size_t column = 0;
    while (column < position.column && offset < text.size() &&
           text[offset] != '\n') {
        ++offset;
        while (offset < text.size() && is_continuation(text[offset])) ++offset;
        ++column;
    }
    return offset;
}

TextPosition position_of(std::string_view text, std::size_t offset) {
    TextPosition position;
    offset = std::min(offset, text.size());
    for (std::size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++position.line;
            position.column = 0;
        } else if (!is_continuation(text[i])) {
            ++position.column;
        }
    }
    return position;
}

// --- Instrucoes ------------------------------------------------------------------

namespace {

// As instrucoes SQL de [begin, end): separadas por ';' e por linha em branco.
void append_sql_ranges(std::string_view script, const Dialect& dialect,
                       std::size_t begin, std::size_t end,
                       std::vector<StatementRange>& out) {
    const std::string_view part = script.substr(begin, end - begin);
    for (const Statement& statement : split_script(part, dialect)) {
        const StatementRange whole =
            trimmed(script, begin + statement.offset,
                    begin + statement.offset + statement.text.size());
        if (whole.empty()) continue;
        // Um lote indivisivel do SQL Server (DECLARE, CREATE PROCEDURE): a
        // linha em branco dentro dele e' so' respiro.
        if (statement.atomic) {
            out.push_back(whole);
            continue;
        }
        split_on_blank_lines(script, dialect, whole, out);
    }
}

// Onde comeca a primeira LINHA de comando de controle (`@set`, `@unset`,
// `@echo`) de `text`, se houver.
//
// So' vale o `@` que o lexer viu como token proprio e que e' o primeiro da
// linha: o de dentro de uma string ou de um comentario chega embutido noutro
// token. E so' os comandos conhecidos -- `@a` no comeco de uma linha e' uma
// variavel de usuario do MySQL no meio de um SELECT.
std::optional<std::size_t> first_control_line(std::string_view text,
                                              const Dialect& dialect) {
    Lexer lexer(text, dialect);
    for (const Token& token : lexer.tokenize_all(/*skip_trivia=*/true)) {
        if (token.text.empty() || token.text.front() != '@') continue;

        bool first_in_line = true;
        for (std::size_t k = token.offset; k > 0 && text[k - 1] != '\n'; --k) {
            if (!is_space(text[k - 1])) {
                first_in_line = false;
                break;
            }
        }
        if (!first_in_line) continue;

        std::size_t eol = text.find('\n', token.offset);
        if (eol == std::string_view::npos) eol = text.size();
        const ControlCommand command =
            parse_control_command(text.substr(token.offset, eol - token.offset));
        if (command.kind != ControlCommand::Kind::none) return token.offset;
    }
    return std::nullopt;
}

} // namespace

std::vector<StatementRange> statement_ranges(std::string_view script,
                                             const Dialect& dialect) {
    std::vector<StatementRange> ranges;

    // O comando de controle termina no FIM DA LINHA, com ou sem ';' -- como
    // no DBeaver. Tratado como SQL, `@set n = 7` sem ';' engolia a instrucao
    // de baixo, e o valor da variavel virava "7" mais o SELECT inteiro.
    //
    // O texto e' analisado de novo a partir do fim de cada comando: o que
    // vem depois de `@echo it's done` nao pode ser lido como continuacao de
    // uma string aberta pelo apostrofo.
    std::size_t base = 0;
    while (base < script.size()) {
        const std::string_view rest = script.substr(base);
        const std::optional<std::size_t> control = first_control_line(rest, dialect);

        append_sql_ranges(script, dialect, base,
                          base + (control ? *control : rest.size()), ranges);
        if (!control) break;

        std::size_t eol = rest.find('\n', *control);
        if (eol == std::string_view::npos) eol = rest.size();

        StatementRange command = trimmed(script, base + *control, base + eol);
        // `@set x = 1;` -- o ';' e' terminador, nao parte do valor.
        while (command.end > command.begin &&
               (script[command.end - 1] == ';' || is_space(script[command.end - 1]))) {
            --command.end;
        }
        if (!command.empty()) ranges.push_back(command);

        base += eol;
    }
    return ranges;
}

std::optional<StatementRange> statement_range_at(std::string_view script,
                                                 const Dialect& dialect,
                                                 std::size_t offset) {
    const std::vector<StatementRange> ranges = statement_ranges(script, dialect);
    if (ranges.empty()) return std::nullopt;

    // Dentro de uma (o fim e' inclusivo: cursor colado no ultimo caractere).
    for (const StatementRange& range : ranges) {
        if (offset >= range.begin && offset <= range.end) return range;
    }

    // Entre duas: a anterior. Antes de todas: a primeira.
    const StatementRange* previous = nullptr;
    for (const StatementRange& range : ranges) {
        if (range.end < offset) previous = &range;
    }
    return previous != nullptr ? *previous : ranges.front();
}

std::optional<std::size_t> next_statement_start(std::string_view script,
                                                const Dialect& dialect,
                                                std::size_t offset) {
    for (const StatementRange& range : statement_ranges(script, dialect)) {
        if (range.begin > offset) return range.begin;
    }
    return std::nullopt;
}

std::optional<std::size_t> previous_statement_start(std::string_view script,
                                                    const Dialect& dialect,
                                                    std::size_t offset) {
    const std::vector<StatementRange> ranges = statement_ranges(script, dialect);

    // Com o cursor DENTRO de uma instrucao, a anterior e' a que vem antes
    // dela -- parar no comeco da propria exigiria um segundo toque para sair.
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        if (offset >= ranges[i].begin && offset <= ranges[i].end) {
            if (i == 0) return std::nullopt;
            return ranges[i - 1].begin;
        }
    }

    // Entre duas (ou depois da ultima): a que termina antes do cursor.
    std::optional<std::size_t> found;
    for (const StatementRange& range : ranges) {
        if (range.end < offset) found = range.begin;
    }
    return found;
}

// --- Colchetes -------------------------------------------------------------------

std::optional<std::size_t> matching_bracket(std::string_view script,
                                            const Dialect& dialect,
                                            std::size_t offset) {
    Lexer lexer(script, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/true);

    // So' os colchetes que o lexer viu como pontuacao: os de dentro de
    // string e comentario chegam embutidos noutro token, e ficam de fora.
    struct Bracket {
        char        glyph;
        std::size_t offset;
    };
    std::vector<Bracket> brackets;
    for (const Token& token : tokens) {
        if (token.kind != TokenKind::punctuation &&
            token.kind != TokenKind::operator_token) {
            continue;
        }
        for (std::size_t i = 0; i < token.text.size(); ++i) {
            const char c = token.text[i];
            if (closing_of(c) != 0 || opening_of(c) != 0) {
                brackets.push_back({c, token.offset + i});
            }
        }
    }

    // O colchete NO cursor; senao, o logo antes dele -- o cursor depois de
    // ")" e' o caso comum ao terminar de digitar.
    std::size_t index = brackets.size();
    for (std::size_t i = 0; i < brackets.size(); ++i) {
        if (brackets[i].offset == offset) { index = i; break; }
    }
    if (index == brackets.size() && offset > 0) {
        for (std::size_t i = 0; i < brackets.size(); ++i) {
            if (brackets[i].offset == offset - 1) { index = i; break; }
        }
    }
    if (index == brackets.size()) return std::nullopt;

    const char glyph = brackets[index].glyph;
    int depth = 0;

    if (const char close = closing_of(glyph); close != 0) {
        for (std::size_t i = index; i < brackets.size(); ++i) {
            if (brackets[i].glyph == glyph) ++depth;
            else if (brackets[i].glyph == close && --depth == 0) {
                return brackets[i].offset;
            }
        }
        return std::nullopt;
    }

    const char open = opening_of(glyph);
    for (std::size_t i = index + 1; i-- > 0;) {
        if (brackets[i].glyph == glyph) ++depth;
        else if (brackets[i].glyph == open && --depth == 0) {
            return brackets[i].offset;
        }
    }
    return std::nullopt;
}

// --- Transformacoes de selecao -----------------------------------------------------

std::string toggle_block_comment(std::string_view text) {
    const StatementRange inner = trimmed(text, 0, text.size());
    const std::string_view core = text.substr(inner.begin, inner.end - inner.begin);

    if (core.size() >= 4 && core.starts_with("/*") && core.ends_with("*/")) {
        // Desfaz, preservando o espaco em branco das pontas da selecao.
        std::string out(text.substr(0, inner.begin));
        out.append(core.substr(2, core.size() - 4));
        out.append(text.substr(inner.end));
        return out;
    }
    return "/*" + std::string(text) + "*/";
}

std::string trim_lines(std::string_view text, bool leading, bool trailing) {
    std::string out;
    out.reserve(text.size());

    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        const bool last = end == std::string_view::npos;
        if (last) end = text.size();

        std::string_view line = text.substr(start, end - start);

        // O '\r' de um fim de linha do Windows fica onde esta'.
        const bool carriage = !line.empty() && line.back() == '\r';
        if (carriage) line.remove_suffix(1);

        if (leading) {
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1);
            }
        }
        if (trailing) {
            while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
                line.remove_suffix(1);
            }
        }

        out.append(line);
        if (carriage) out.push_back('\r');
        if (last) break;
        out.push_back('\n');
        start = end + 1;
    }
    return out;
}

std::string morph_delimited_list(std::string_view text,
                                 const MorphOptions& options) {
    // '\n' como separador leva o '\r' junto: uma coluna copiada no Windows
    // deixaria um '\r' colado em cada valor.
    std::string delimiters = options.source_delimiters;
    if (delimiters.find('\n') != std::string::npos &&
        delimiters.find('\r') == std::string::npos) {
        delimiters.push_back('\r');
    }

    std::vector<std::string_view> tokens;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find_first_of(delimiters, start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;

        const StatementRange inner = trimmed(text, start, stop);
        if (!inner.empty()) {
            tokens.push_back(text.substr(inner.begin, inner.end - inner.begin));
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }

    std::string out = options.leading_text;
    std::size_t line_length = out.size();

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        std::string piece = options.quote + std::string(tokens[i]) + options.quote;
        if (i + 1 < tokens.size()) piece += options.target_delimiter;

        // Quebra ANTES do item que estouraria a coluna, nunca no meio dele.
        if (options.wrap_line > 0 && line_length > 0 &&
            line_length + piece.size() > options.wrap_line) {
            out.push_back('\n');
            line_length = 0;
        }
        out += piece;
        line_length += piece.size();
    }

    out += options.trailing_text;
    return out;
}

// --- Variaveis ---------------------------------------------------------------------

ControlCommand parse_control_command(std::string_view statement) {
    ControlCommand command;

    const StatementRange inner = trimmed(statement, 0, statement.size());
    std::string_view text = statement.substr(inner.begin, inner.end - inner.begin);
    if (text.empty() || text.front() != '@') return command;
    text.remove_prefix(1);

    // A palavra do comando, ate' o primeiro espaco.
    std::size_t split = 0;
    while (split < text.size() && !is_space(text[split])) ++split;
    const std::string word = upper(text.substr(0, split));

    std::string_view rest = text.substr(split);
    while (!rest.empty() && is_space(rest.front())) rest.remove_prefix(1);

    if (word == "ECHO") {
        command.kind  = ControlCommand::Kind::echo;
        command.value = std::string(rest);
    } else if (word == "UNSET") {
        command.kind = ControlCommand::Kind::unset;
        command.name = std::string(rest);
    } else if (word == "SET") {
        const std::size_t equals = rest.find('=');
        if (equals == std::string_view::npos) return command;

        const StatementRange name = trimmed(rest, 0, equals);
        const StatementRange value = trimmed(rest, equals + 1, rest.size());
        if (name.empty()) return command;

        command.kind  = ControlCommand::Kind::set;
        command.name  = std::string(rest.substr(name.begin, name.end - name.begin));
        command.value = std::string(rest.substr(value.begin, value.end - value.begin));
    }
    return command;
}

std::string expand_variables(std::string_view sql, const Variables& variables) {
    std::string out;
    out.reserve(sql.size());

    std::size_t i = 0;
    while (i < sql.size()) {
        if (sql[i] == '$' && i + 1 < sql.size() && sql[i + 1] == '{') {
            const std::size_t close = sql.find('}', i + 2);
            if (close != std::string_view::npos) {
                const std::string name(sql.substr(i + 2, close - i - 2));
                if (const auto it = variables.find(name); it != variables.end()) {
                    out += it->second;
                    i = close + 1;
                    continue;
                }
            }
        }
        out.push_back(sql[i]);
        ++i;
    }
    return out;
}

// --- Templates -------------------------------------------------------------------

const std::vector<Template>& default_templates() {
    // templates/default-templates.xml do org.jkiss.dbeaver.ui.editors.sql.
    static const std::vector<Template> kTemplates = {
        {"scount", "select row count", "select count(*) from ${table};"},
        {"swhere", "select with condition",
         "select * from ${table} where ${column}='${value}';"},
        {"scgb", "select count with group by",
         "select ${col},count(*)\nfrom ${table} t group by ${col};"},
        {"sob", "select with order by",
         "select * from ${table} t order by ${column};"},
        {"sf", "select * from ", "select * from ${table};"},
    };
    return kTemplates;
}

ExpandedTemplate expand_template(const Template& entry) {
    ExpandedTemplate expanded;
    bool first = true;

    const std::string_view pattern = entry.pattern;
    std::size_t i = 0;
    while (i < pattern.size()) {
        if (pattern[i] == '$' && i + 1 < pattern.size() && pattern[i + 1] == '{') {
            const std::size_t close = pattern.find('}', i + 2);
            if (close != std::string_view::npos) {
                // O NOME da variavel fica no texto, como marcador a trocar.
                const std::string_view name = pattern.substr(i + 2, close - i - 2);
                if (first) {
                    expanded.cursor = expanded.text.size();
                    expanded.select = name.size();
                    first = false;
                }
                expanded.text.append(name);
                i = close + 1;
                continue;
            }
        }
        expanded.text.push_back(pattern[i]);
        ++i;
    }
    if (first) expanded.cursor = expanded.text.size();
    return expanded;
}

// --- Estrutura ---------------------------------------------------------------------

std::vector<OutlineEntry> outline(std::string_view script, const Dialect& dialect) {
    std::vector<OutlineEntry> entries;

    for (const StatementRange& range : statement_ranges(script, dialect)) {
        const std::string_view text = script.substr(range.begin, range.end - range.begin);

        Lexer lexer(text, dialect);
        const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/true);
        if (tokens.empty()) continue;

        OutlineEntry entry;
        entry.offset = range.begin + tokens.front().offset;
        entry.line   = position_of(script, entry.offset).line;
        entry.label  = upper(tokens.front().text);

        // O alvo: o primeiro nome depois de FROM / INTO / UPDATE / TABLE /
        // JOIN. "SELECT cliente" diz mais que "SELECT" numa lista de vinte.
        for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
            if (tokens[i].kind != TokenKind::keyword) continue;
            const std::string word = upper(tokens[i].text);
            const bool introduces =
                word == "FROM" || word == "INTO" || word == "UPDATE" ||
                word == "TABLE" || word == "VIEW" || word == "JOIN" ||
                word == "FUNCTION" || word == "PROCEDURE" || word == "INDEX";
            if (!introduces) continue;

            // Monta schema.nome a partir dos tokens seguintes.
            std::string target;
            for (std::size_t j = i + 1; j < tokens.size(); ++j) {
                const Token& t = tokens[j];
                const bool is_name = t.kind == TokenKind::identifier ||
                                     t.kind == TokenKind::quoted_identifier;
                if (is_name) {
                    target += std::string(t.unquoted());
                } else if (t.text == "." && !target.empty()) {
                    target += ".";
                } else {
                    break;
                }
                if (j + 1 >= tokens.size() || tokens[j + 1].text != ".") {
                    if (is_name) break;
                }
            }
            if (!target.empty()) {
                entry.label += " " + target;
                break;
            }
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace otter::sql
