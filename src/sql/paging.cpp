#include "sql/paging.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace otter::sql {
namespace {

// Compara sem diferenciar maiusculas. As palavras-chave vem do lexer com o
// texto original, e `Select` e `SELECT` sao a mesma coisa.
bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

// Remove trivia do fim: comentarios e espacos depois do ultimo token real.
// Acrescentar "LIMIT 200" depois de um comentario de linha colocaria o limite
// DENTRO do comentario, e a consulta rodaria sem limite nenhum.
std::string_view trim_trailing(std::string_view sql,
                               const std::vector<Token>& tokens) {
    std::size_t end = sql.size();
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (it->kind == TokenKind::end_of_input) continue;
        if (it->is_trivia() || it->kind == TokenKind::semicolon) {
            end = it->offset;
            continue;
        }
        end = it->offset + it->text.size();
        break;
    }
    return sql.substr(0, end);
}

} // namespace

std::string_view to_string(PagingRefusal refusal) noexcept {
    switch (refusal) {
        case PagingRefusal::none:              return "paged";
        case PagingRefusal::not_a_query:       return "not a query";
        case PagingRefusal::already_limited:   return "already limited";
        case PagingRefusal::multiple_commands: return "multiple commands";
        case PagingRefusal::unsupported_form:  return "unsupported form";
        case PagingRefusal::empty:             return "empty";
    }
    return "unknown";
}

PagedQuery make_paged_query(std::string_view sql, const Dialect& dialect,
                            std::size_t page, std::size_t page_size) {
    PagedQuery result;
    result.sql = std::string(sql);

    if (page_size == 0) {
        result.refusal = PagingRefusal::not_a_query;
        return result;
    }

    Lexer lexer(sql, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/false);

    // --- Primeiro token significativo decide se ha' o que paginar ------------
    const Token* first = nullptr;
    for (const Token& token : tokens) {
        if (token.is_trivia() || token.kind == TokenKind::end_of_input) continue;
        first = &token;
        break;
    }

    if (first == nullptr) {
        result.refusal = PagingRefusal::empty;
        return result;
    }

    const std::string_view head = first->text;
    const bool is_select = iequals(head, "SELECT") || iequals(head, "WITH") ||
                           iequals(head, "TABLE");
    if (!is_select) {
        // EXPLAIN e SHOW produzem linhas, mas poucas e sem ORDER BY estavel:
        // paginar seria custo sem beneficio. VALUES aceita LIMIT, mas quem
        // escreve VALUES a mao ja' sabe quantas linhas quer.
        result.refusal = iequals(head, "EXPLAIN") || iequals(head, "SHOW") ||
                         iequals(head, "VALUES")
                             ? PagingRefusal::unsupported_form
                             : PagingRefusal::not_a_query;
        return result;
    }

    // --- Varredura: LIMIT de nivel externo, e ';' no meio --------------------
    //
    // A profundidade de parenteses importa: em
    //
    //     SELECT * FROM (SELECT * FROM t LIMIT 10) s
    //
    // o LIMIT pertence a subconsulta. Acrescentar outro no fim e' correto, e
    // recusar por causa dele deixaria a consulta externa sem limite.
    int depth = 0;
    bool has_outer_limit = false;
    bool saw_statement_end = false;

    for (const Token& token : tokens) {
        if (token.is_trivia() || token.kind == TokenKind::end_of_input) continue;

        if (token.kind == TokenKind::punctuation) {
            if (token.text == "(") ++depth;
            else if (token.text == ")") depth = std::max(0, depth - 1);
            continue;
        }

        if (token.kind == TokenKind::semicolon) {
            // Um ';' no fim e' so' terminador. No meio, ha' mais de um comando.
            for (const Token& rest : tokens) {
                if (rest.offset <= token.offset) continue;
                if (rest.is_trivia() || rest.kind == TokenKind::end_of_input ||
                    rest.kind == TokenKind::semicolon) {
                    continue;
                }
                saw_statement_end = true;
                break;
            }
            continue;
        }

        if (depth != 0) continue;

        if (token.kind == TokenKind::keyword || token.kind == TokenKind::identifier) {
            // LIMIT (PostgreSQL/MySQL), FETCH (SQL padrao) e OFFSET sozinho,
            // que tambem indica que o usuario esta' paginando por conta.
            if (iequals(token.text, "LIMIT") || iequals(token.text, "FETCH") ||
                iequals(token.text, "OFFSET")) {
                has_outer_limit = true;
            }
        }
    }

    if (saw_statement_end) {
        result.refusal = PagingRefusal::multiple_commands;
        return result;
    }
    if (has_outer_limit) {
        // O usuario pediu um limite explicito. Sobrepo-lo faria a grade mostrar
        // algo diferente do que foi escrito.
        result.refusal = PagingRefusal::already_limited;
        return result;
    }

    // --- Reescreve -----------------------------------------------------------
    //
    // Uma linha a mais que a pagina: se ela vier, existe proxima pagina. Um
    // COUNT(*) daria o total exato ao custo de outra varredura completa.
    const std::size_t requested = page_size + 1;

    std::string paged(trim_trailing(sql, tokens));
    paged += "\nLIMIT " + std::to_string(requested);
    if (page > 0) {
        paged += " OFFSET " + std::to_string(page * page_size);
    }

    result.sql       = std::move(paged);
    result.rewritten = true;
    result.requested = requested;
    return result;
}

} // namespace otter::sql
