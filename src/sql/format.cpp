#include "sql/format.hpp"

#include <algorithm>
#include <cctype>
#include <span>
#include <vector>

namespace otter::sql {
namespace {

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string apply_case(std::string_view text, KeywordCase mode) {
    std::string out(text);
    if (mode == KeywordCase::preserve) return out;

    const bool upper = mode == KeywordCase::upper;
    std::transform(out.begin(), out.end(), out.begin(), [upper](unsigned char c) {
        return static_cast<char>(upper ? std::toupper(c) : std::tolower(c));
    });
    return out;
}

// Clausulas que comecam uma linha nova, na largura do "rio" do psql.
//
// A largura 6 vem de "SELECT", a mais longa do grupo principal: e' o que faz
// FROM, WHERE e GROUP alinharem a direita sob ela.
constexpr std::string_view kRiverClauses[] = {
    "SELECT", "FROM", "WHERE", "HAVING", "UNION", "EXCEPT", "INTERSECT",
    "VALUES", "RETURNING", "INSERT", "UPDATE", "DELETE", "SET",
};

// Duas palavras que formam uma clausula: "GROUP BY", "ORDER BY", "LEFT JOIN".
constexpr std::string_view kTwoWordStarts[] = {
    "GROUP", "ORDER", "PARTITION",
};

constexpr std::string_view kJoinWords[] = {
    "JOIN", "LEFT", "RIGHT", "INNER", "OUTER", "FULL", "CROSS", "NATURAL",
};

bool is_in(std::string_view word, std::span<const std::string_view> list) {
    for (const std::string_view entry : list) {
        if (iequals(word, entry)) return true;
    }
    return false;
}

bool starts_new_line(std::string_view word) {
    return is_in(word, kRiverClauses) || is_in(word, kTwoWordStarts) ||
           is_in(word, kJoinWords) || iequals(word, "ON") ||
           iequals(word, "LIMIT") || iequals(word, "OFFSET");
}

// Precisa de espaco antes deste token?
bool needs_space_before(std::string_view text, TokenKind kind,
                        std::string_view previous) {
    if (previous.empty()) return false;

    // Pontuacao que cola na esquerda.
    if (text == "," || text == ")" || text == ";" || text == "::") return false;

    // Abre parenteses cola quando e' chamada de funcao -- count(*), nao
    // count (*). Depois de palavra-chave, separa: IN (1,2).
    if (text == "(") return false;

    // Depois de '(' nada leva espaco.
    if (previous == "(") return false;

    // Ponto de qualificacao: schema.tabela, nunca schema . tabela.
    if (text == "." || previous == ".") return false;

    (void)kind;
    return true;
}

} // namespace

std::string format_sql(std::string_view sql, const Dialect& dialect,
                       const FormatOptions& options) {
    Lexer lexer(sql, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/false);

    // Token invalido significa string nao fechada ou lixo: reindentar algo
    // que nao foi entendido produz texto pior que o original.
    for (const Token& token : tokens) {
        if (token.kind == TokenKind::invalid) return std::string(sql);
    }

    // Largura do rio: alinha as clausulas a' direita sob "SELECT".
    constexpr std::size_t kRiverWidth = 6;

    std::string out;
    out.reserve(sql.size() + sql.size() / 4);

    // Quantas virgulas ha' na lista do SELECT de nivel zero. Precisa ser
    // contado ANTES de formatar: a decisao de quebrar a primeira virgula
    // depende do tamanho total da lista, que so' se conhece no fim.
    std::size_t select_commas = 0;
    {
        int probe_depth = 0;
        bool counting = false;
        for (const Token& token : tokens) {
            if (token.is_trivia() || token.kind == TokenKind::end_of_input) {
                continue;
            }
            if (token.text == "(") { ++probe_depth; continue; }
            if (token.text == ")") { probe_depth = std::max(0, probe_depth - 1); continue; }
            if (probe_depth != 0) continue;

            if (iequals(token.text, "SELECT")) { counting = true; select_commas = 0; }
            else if (iequals(token.text, "FROM") || token.text == ";") counting = false;
            else if (counting && token.text == ",") ++select_commas;
        }
    }

    std::string_view previous;
    int  depth = 0;             // profundidade de parenteses
    bool at_line_start = true;
    bool in_select_list = false;

    const auto newline = [&](std::size_t indent) {
        while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
            out.pop_back();
        }
        if (!out.empty()) out.push_back('\n');
        out.append(indent, ' ');
        at_line_start = true;
    };

    for (const Token& token : tokens) {
        if (token.kind == TokenKind::end_of_input) break;

        // Comentario de linha preserva-se inteiro e forca quebra depois: um
        // comentario seguido de codigo na mesma linha comentaria o codigo.
        if (token.kind == TokenKind::line_comment) {
            if (!at_line_start) out.push_back(' ');
            out += token.text;
            newline(static_cast<std::size_t>(depth) *
                    static_cast<std::size_t>(options.indent_width));
            previous = {};
            continue;
        }
        if (token.kind == TokenKind::block_comment) {
            if (needs_space_before(token.text, token.kind, previous)) {
                out.push_back(' ');
            }
            out += token.text;
            previous = token.text;
            at_line_start = false;
            continue;
        }
        if (token.is_trivia()) continue;   // espacos originais sao descartados

        const std::string_view text = token.text;

        // --- Quebras de linha por estrutura ----------------------------------
        const bool keyword_like = token.kind == TokenKind::keyword ||
                                  token.kind == TokenKind::identifier;

        if (keyword_like && depth == 0 && starts_new_line(text) &&
            !out.empty()) {
            // Nao quebra quando a palavra continua a clausula anterior:
            //
            //   GROUP BY, ORDER BY        -- ja' quebrou no GROUP/ORDER
            //   LEFT JOIN, INNER JOIN     -- ja' quebrou no LEFT/INNER
            //   LEFT OUTER JOIN           -- tres palavras, uma clausula
            //
            // Sem isto, "LEFT JOIN" saia em duas linhas.
            const bool continues_clause =
                iequals(previous, "GROUP") || iequals(previous, "ORDER") ||
                iequals(previous, "PARTITION") ||
                (is_in(previous, kJoinWords) && is_in(text, kJoinWords));

            if (!continues_clause) {

                const std::size_t indent =
                    options.river_style && text.size() < kRiverWidth
                        ? kRiverWidth - text.size()
                        : 0;
                newline(indent);
            }
        }

        if (text == ";") {
            // ';' cola no token anterior, e o proximo comando comeca numa
            // linha em branco depois.
            out += text;
            out.push_back('\n');
            previous = {};
            at_line_start = true;
            depth = 0;
            in_select_list = false;
            continue;
        }

        // Virgula na lista do SELECT: quebra quando a lista e' longa.
        if (text == "," && depth == 0 && in_select_list &&
            select_commas + 1 > options.wrap_select_after) {
            out += ",";
            newline(kRiverWidth + 1);
            previous = ",";
            continue;
        }

        // --- Escreve o token --------------------------------------------------
        if (!at_line_start && needs_space_before(text, token.kind, previous)) {
            out.push_back(' ');
        }

        if (token.kind == TokenKind::keyword) {
            out += apply_case(text, options.keyword_case);
        } else {
            out += text;
        }

        if (text == "(") ++depth;
        else if (text == ")") depth = std::max(0, depth - 1);

        if (keyword_like && iequals(text, "SELECT")) {
            in_select_list = true;
        } else if (keyword_like && depth == 0 &&
                   (iequals(text, "FROM") || iequals(text, "WHERE"))) {
            in_select_list = false;
        }

        previous = text;
        at_line_start = false;
    }

    // Uma quebra final, sem linhas em branco sobrando.
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) {
        out.pop_back();
    }
    if (!out.empty()) out.push_back('\n');
    return out;
}

} // namespace otter::sql
