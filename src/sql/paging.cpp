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
                            std::size_t page, std::size_t page_size,
                            const SortOrder& sort, const ColumnFilter& filter) {
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
    bool has_outer_order = false;
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
            // ORDER de nivel externo: a ordenacao da grade seria acrescentada
            // depois, e dois ORDER BY na mesma consulta sao erro de sintaxe.
            if (iequals(token.text, "ORDER")) {
                has_outer_order = true;
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

    // O filtro envolve a consulta numa subconsulta, em vez de anexar WHERE.
    //
    // Anexar seria errado de tres jeitos: a consulta pode ja' ter WHERE (dois
    // na mesma nao compilam), pode terminar em GROUP BY ou HAVING (WHERE
    // depois deles e' sintaxe invalida), e mesmo quando compilasse filtraria
    // ANTES da agregacao -- resultado diferente do que a grade mostra.
    //
    // Envolver custa uma subconsulta que o planejador quase sempre achata,
    // e e' correto em todos os casos.
    if (!filter.empty()) {
        std::string inner = "SELECT * FROM (\n" + paged + "\n) AS otter_filter\n WHERE \"";
        for (const char c : filter.column) {
            if (c == '"') inner += "\"\"";
            else          inner.push_back(c);
        }
        inner += "\" " + filter.expression;
        paged = std::move(inner);
    }

    // ORDER BY antes do LIMIT -- e' a ordem exigida pela gramatica, e o
    // sentido tambem: limitar primeiro daria as 200 primeiras linhas na ordem
    // do banco, depois reordenadas entre si.
    //
    // Um ORDER BY que o usuario escreveu tem precedencia: dois ORDER BY na
    // mesma consulta sao erro de sintaxe, e sobrepor o dele seria executar
    // algo diferente do que esta' na tela.
    // Com filtro, o ORDER BY do usuario ficou DENTRO da subconsulta -- a
    // externa nao tem ordem, e a da grade passa a ser legitima.
    const bool order_is_free = !has_outer_order || !filter.empty();

    if (!sort.empty() && order_is_free) {
        // O nome vem do cabecalho da grade, portanto do servidor -- mas citar
        // e' barato e protege contra uma coluna chamada "order" ou "Nome".
        paged += "\nORDER BY \"";
        for (const char c : sort.column) {
            if (c == '"') paged += "\"\"";
            else          paged.push_back(c);
        }
        paged += sort.descending ? "\" DESC" : "\" ASC";
    }

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
