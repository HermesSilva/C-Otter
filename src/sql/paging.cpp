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

namespace {

// O SQL Server pagina por OFFSET ... FETCH, que so' existe DEPOIS de um
// ORDER BY, nao aceita ORDER BY em subconsulta sem TOP/OFFSET, nem WITH
// dentro de subconsulta. E' outro formato, nao uma variacao do LIMIT.
bool is_tsql(const Dialect& dialect) noexcept {
    return !dialect.identifier_quotes.empty() &&
           dialect.identifier_quotes.front() == QuoteStyle::brackets;
}

// O SQL Anywhere pagina por "SELECT TOP n START AT m", que fica no COMECO da
// consulta -- nem LIMIT (desligado por padrao la'), nem OFFSET/FETCH.
bool is_anywhere(const Dialect& dialect) noexcept {
    return dialect.name == "SQL Anywhere";
}

// O que a varredura dos tokens descobre sobre a consulta.
struct Shape {
    PagingRefusal refusal = PagingRefusal::none;
    bool outer_order = false;   // ORDER BY de nivel externo
    bool cte = false;           // comeca por WITH
    // DISTINCT, UNION, EXCEPT, INTERSECT: o ORDER BY so' aceita o que esta' na
    // lista do SELECT -- "(SELECT NULL)" nao serve, a posicao serve.
    bool select_list_order = false;

    // UNION, EXCEPT ou INTERSECT de nivel externo: um TOP no primeiro SELECT
    // limitaria so' aquele bloco.
    bool set_operation = false;
    // Onde termina "SELECT [DISTINCT | ALL]" do SELECT principal (o primeiro
    // de nivel externo): e' ali que o TOP do SQL Anywhere entra.
    std::size_t select_end = 0;
    // Onde comeca o ORDER BY de nivel externo.
    std::size_t order_offset = 0;
};

Shape analyze(const Dialect& dialect, const std::vector<Token>& tokens) {
    Shape shape;
    const bool anywhere = is_anywhere(dialect);
    // TOP, SELECT ... INTO e FOR XML existem nos dois.
    const bool tsql = is_tsql(dialect) || anywhere;
    bool after_main_select = false;

    // --- Primeiro token significativo decide se ha' o que paginar ------------
    const Token* first = nullptr;
    for (const Token& token : tokens) {
        if (token.is_trivia() || token.kind == TokenKind::end_of_input) continue;
        first = &token;
        break;
    }

    if (first == nullptr) {
        shape.refusal = PagingRefusal::empty;
        return shape;
    }

    const std::string_view head = first->text;
    const bool is_select = iequals(head, "SELECT") || iequals(head, "WITH") ||
                           iequals(head, "TABLE");
    if (!is_select) {
        // EXPLAIN e SHOW produzem linhas, mas poucas e sem ORDER BY estavel:
        // paginar seria custo sem beneficio. VALUES aceita LIMIT, mas quem
        // escreve VALUES a mao ja' sabe quantas linhas quer.
        shape.refusal = iequals(head, "EXPLAIN") || iequals(head, "SHOW") ||
                        iequals(head, "VALUES")
                            ? PagingRefusal::unsupported_form
                            : PagingRefusal::not_a_query;
        return shape;
    }
    shape.cte = iequals(head, "WITH");

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
    bool creates_table = false;
    bool has_tail_clause = false;

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

        // "SELECT [DISTINCT | ALL]" do SELECT principal: o primeiro de nivel
        // externo (os de um WITH estao entre parenteses).
        if (after_main_select) {
            after_main_select = false;
            if (iequals(token.text, "DISTINCT") || iequals(token.text, "ALL")) {
                shape.select_end = token.offset + token.text.size();
            }
        }
        if (shape.select_end == 0 && iequals(token.text, "SELECT")) {
            shape.select_end  = token.offset + token.text.size();
            after_main_select = true;
        }

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
                shape.outer_order  = true;
                shape.order_offset = token.offset;
            }
            if (iequals(token.text, "DISTINCT") || iequals(token.text, "UNION") ||
                iequals(token.text, "EXCEPT") || iequals(token.text, "INTERSECT")) {
                shape.select_list_order = true;
            }
            if (iequals(token.text, "UNION") || iequals(token.text, "EXCEPT") ||
                iequals(token.text, "INTERSECT")) {
                shape.set_operation = true;
            }
            // SELECT FIRST e' o "TOP 1" do SQL Anywhere.
            if (anywhere && iequals(token.text, "FIRST")) has_outer_limit = true;

            if (tsql) {
                // TOP e' o limite do T-SQL, e fica no COMECO: so' aqui ele e'
                // palavra reservada (no PostgreSQL "top" pode ser uma coluna).
                if (iequals(token.text, "TOP")) has_outer_limit = true;
                // SELECT ... INTO cria uma tabela: nao e' consulta de grade.
                if (iequals(token.text, "INTO")) creates_table = true;
                // FOR XML / FOR JSON / OPTION (...) vem DEPOIS do ORDER BY:
                // anexar o OFFSET atras deles e' erro de sintaxe.
                if (iequals(token.text, "FOR") || iequals(token.text, "OPTION")) {
                    has_tail_clause = true;
                }
            }
        }
    }

    if (saw_statement_end) {
        shape.refusal = PagingRefusal::multiple_commands;
    } else if (creates_table) {
        shape.refusal = PagingRefusal::not_a_query;
    } else if (has_tail_clause) {
        shape.refusal = PagingRefusal::unsupported_form;
    } else if (has_outer_limit) {
        // O usuario pediu um limite explicito. Sobrepo-lo faria a grade mostrar
        // algo diferente do que foi escrito.
        shape.refusal = PagingRefusal::already_limited;
    }
    return shape;
}

// Miolo comum de make_paged_query e make_unpaged_query.
//
// `page_size == 0` significa SEM LIMITE -- o filtro e a ordenacao da grade
// continuam sendo aplicados, so' o LIMIT/OFFSET e' que nao entra. As duas
// funcoes publicas diferem apenas nisso, e duplicar a analise de tokens entre
// elas deixaria as recusas podendo divergir.
PagedQuery build_query(std::string_view sql, const Dialect& dialect,
                       std::size_t page, std::size_t page_size,
                       const SortOrder& sort, const ColumnFilter& filter) {
    PagedQuery result;
    result.sql = std::string(sql);

    const bool unlimited = page_size == 0;
    const bool tsql      = is_tsql(dialect);
    const bool anywhere  = is_anywhere(dialect);

    Lexer lexer(sql, dialect);
    const std::vector<Token> tokens = lexer.tokenize_all(/*skip_trivia=*/false);

    const Shape shape = analyze(dialect, tokens);
    if (shape.refusal != PagingRefusal::none) {
        result.refusal = shape.refusal;
        return result;
    }
    // T-SQL: um WITH nao cabe em subconsulta, e o filtro da grade e' uma.
    // Recusar e' melhor que mandar um SQL que o servidor devolve com erro.
    if (tsql && shape.cte && !filter.empty()) {
        result.refusal = PagingRefusal::unsupported_form;
        return result;
    }

    // --- Reescreve -----------------------------------------------------------
    //
    // Uma linha a mais que a pagina: se ela vier, existe proxima pagina. Um
    // COUNT(*) daria o total exato ao custo de outra varredura completa.
    const std::size_t requested = unlimited ? 0 : page_size + 1;

    std::string paged(trim_trailing(sql, tokens));

    if (anywhere) {
        // "TOP n START AT m" (m comeca em 1), no SELECT que manda no resultado.
        const std::string top =
            unlimited ? std::string{}
                      : " TOP " + std::to_string(requested) + " START AT " +
                            std::to_string(page * page_size + 1);

        // Dois formatos. Sem filtro e sem UNION, o TOP entra no proprio
        // SELECT do usuario -- o ORDER BY dele continua valendo. Com filtro ou
        // com UNION/EXCEPT/INTERSECT a consulta e' envolvida, e o TOP vai no
        // SELECT de fora.
        const bool wrap = !filter.empty() || shape.set_operation;
        if (!wrap) {
            if (!top.empty() && shape.select_end > 0 && shape.select_end <= paged.size()) {
                paged.insert(shape.select_end, top);
            }
            if (!sort.empty() && !shape.outer_order) {
                paged += "\nORDER BY " + dialect.quote_identifier(sort.column);
                paged += sort.descending ? " DESC" : " ASC";
            }
        } else {
            // O ORDER BY de uma subconsulta nao ordena o resultado de fora (o
            // servidor o ignora). O de um UNION e' sobre as colunas do
            // resultado e pode sair para o SELECT externo; o de um SELECT
            // comum pode citar o que nao esta' na lista, e fica onde esta'.
            std::string outer_order;
            if (shape.set_operation && shape.outer_order &&
                shape.order_offset < paged.size()) {
                outer_order = paged.substr(shape.order_offset);
                paged.erase(shape.order_offset);
                while (!paged.empty() &&
                       std::isspace(static_cast<unsigned char>(paged.back())) != 0) {
                    paged.pop_back();
                }
            }
            paged = "SELECT" + top + " * FROM (\n" + paged + "\n) AS otter_page";
            if (!filter.empty()) paged += "\n WHERE " + filter.where_clause(&dialect);
            if (!sort.empty()) {
                paged += "\nORDER BY " + dialect.quote_identifier(sort.column);
                paged += sort.descending ? " DESC" : " ASC";
            } else if (!outer_order.empty()) {
                paged += "\n" + outer_order;
            }
        }

        result.sql       = std::move(paged);
        result.rewritten = true;
        result.requested = requested;
        return result;
    }

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
        // T-SQL so' aceita ORDER BY em subconsulta acompanhado de TOP ou
        // OFFSET: o "OFFSET 0 ROWS" o torna valido sem tirar linha nenhuma.
        if (tsql && shape.outer_order) paged += "\nOFFSET 0 ROWS";
        paged = "SELECT * FROM (\n" + paged + "\n) AS otter_filter\n WHERE " +
                filter.where_clause(&dialect);
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
    const bool order_is_free = !shape.outer_order || !filter.empty();
    bool       ordered       = shape.outer_order && filter.empty();

    if (!sort.empty() && order_is_free) {
        // O nome vem do cabecalho da grade, portanto do servidor -- mas citar
        // e' barato e protege contra uma coluna chamada "order" ou "Nome".
        // Com o delimitador do DIALETO: "nome" entre aspas duplas e' um texto
        // literal para o MySQL, e ordenar por uma constante nao ordena nada.
        paged += "\nORDER BY " + dialect.quote_identifier(sort.column);
        paged += sort.descending ? " DESC" : " ASC";
        ordered = true;
    }

    if (!unlimited && tsql) {
        // OFFSET/FETCH exige ORDER BY. Sem um, "(SELECT NULL)" e' a forma de
        // dizer "na ordem em que vier"; com DISTINCT ou UNION o servidor so'
        // aceita o que esta' na lista do SELECT, e ai' vai a primeira coluna.
        if (!ordered) {
            paged += shape.select_list_order && filter.empty()
                         ? "\nORDER BY 1"
                         : "\nORDER BY (SELECT NULL)";
        }
        paged += "\nOFFSET " + std::to_string(page * page_size) +
                 " ROWS FETCH NEXT " + std::to_string(requested) + " ROWS ONLY";
    } else if (!unlimited) {
        paged += "\nLIMIT " + std::to_string(requested);
        if (page > 0) {
            paged += " OFFSET " + std::to_string(page * page_size);
        }
    }

    result.sql       = std::move(paged);
    result.rewritten = true;
    result.requested = requested;
    return result;
}

}   // namespace

std::string_view ColumnFilter::expression_for(std::string_view name) const noexcept {
    if (column == name) return expression;
    for (const Criterion& criterion : more) {
        if (criterion.column == name) return criterion.expression;
    }
    return {};
}

void ColumnFilter::set_column(std::string_view name, std::string_view new_expression) {
    if (name.empty()) return;

    // Tira o que houver para a coluna, onde estiver.
    if (column == name) {
        column.clear();
        expression.clear();
    }
    std::erase_if(more, [name](const Criterion& criterion) {
        return criterion.column == name;
    });

    // O primeiro lugar nunca fica vago com criterios na fila: `column` e' o
    // que os chamadores antigos olham.
    if (column.empty() && !more.empty()) {
        column     = std::move(more.front().column);
        expression = std::move(more.front().expression);
        more.erase(more.begin());
    }

    if (new_expression.empty()) return;

    if (column.empty()) {
        column     = std::string(name);
        expression = std::string(new_expression);
    } else {
        more.push_back({std::string(name), std::string(new_expression)});
    }
}

std::size_t ColumnFilter::column_count() const noexcept {
    std::size_t count = !column.empty() && !expression.empty() ? 1 : 0;
    for (const Criterion& criterion : more) {
        if (!criterion.column.empty() && !criterion.expression.empty()) ++count;
    }
    return count;
}

std::string ColumnFilter::where_clause(const Dialect* dialect) const {
    std::string clause;

    // Sem dialeto, aspas duplas -- o padrao SQL.
    const Dialect& quoting = dialect != nullptr ? *dialect : standard_dialect();

    const auto add = [&clause, &quoting](std::string_view name, std::string_view expr) {
        if (name.empty() || expr.empty()) return;
        if (!clause.empty()) clause += "\n   AND ";
        clause += quoting.quote_identifier(name);
        clause += ' ';
        clause += expr;
    };

    add(column, expression);
    for (const Criterion& criterion : more) add(criterion.column, criterion.expression);

    // Entre parenteses: um OR dentro da condicao livre nao pode desfazer os
    // criterios das colunas.
    if (!condition.empty()) {
        if (!clause.empty()) clause += "\n   AND ";
        clause += "(" + condition + ")";
    }
    return clause;
}

PagedQuery make_paged_query(std::string_view sql, const Dialect& dialect,
                            std::size_t page, std::size_t page_size,
                            const SortOrder& sort, const ColumnFilter& filter) {
    // Aqui 0 nao e' "sem limite", e' pedido sem sentido: uma pagina de zero
    // linhas. `make_unpaged_query` e' a porta para o resultado inteiro.
    if (page_size == 0) {
        PagedQuery result;
        result.sql     = std::string(sql);
        result.refusal = PagingRefusal::not_a_query;
        return result;
    }
    return build_query(sql, dialect, page, page_size, sort, filter);
}

PagedQuery make_unpaged_query(std::string_view sql, const Dialect& dialect,
                              const SortOrder& sort,
                              const ColumnFilter& filter) {
    return build_query(sql, dialect, /*page=*/0, /*page_size=*/0, sort, filter);
}

PagedQuery make_count_query(std::string_view sql, const Dialect& dialect,
                            const ColumnFilter& filter) {
    // A MESMA analise de make_paged_query: o que nao da' para paginar tambem
    // nao da' para contar, e duplicar as regras faria as duas divergirem ao
    // primeiro ajuste.
    Lexer count_lexer(sql, dialect);
    const std::vector<Token> count_tokens =
        count_lexer.tokenize_all(/*skip_trivia=*/false);
    const Shape shape = analyze(dialect, count_tokens);
    const bool  tsql  = is_tsql(dialect);

    PagedQuery result;
    result.sql     = std::string(sql);
    result.refusal = shape.refusal;

    // `already_limited` nao impede CONTAR: o usuario escreveu LIMIT 50, e
    // "quantas linhas ha' no total" continua sendo uma pergunta valida --
    // a resposta e' o que existe antes do limite dele.
    //
    // A subconsulta preserva o LIMIT, entao a contagem devolve no maximo 50.
    // E' o numero certo: e' o tamanho do resultado que a grade esta'
    // mostrando, que e' o que "contar o resultado" quer dizer.
    if (shape.refusal != PagingRefusal::none &&
        shape.refusal != PagingRefusal::already_limited) {
        return result;
    }
    // T-SQL: um WITH nao cabe na subconsulta da contagem.
    if (tsql && shape.cte) {
        result.refusal = PagingRefusal::unsupported_form;
        return result;
    }

    // Sem o ';' e sem os comentarios do fim: envolver "SELECT ... ;" numa
    // subconsulta produz "... ;\n) AS otter_count", que o servidor recusa
    // com "erro de sintaxe em ou proximo a ';'".
    //
    // O mesmo trim_trailing que make_paged_query usa -- e pela mesma razao:
    // o texto do usuario termina como ele escreveu, nao como o SQL gerado
    // precisa.
    std::string inner(trim_trailing(sql, count_tokens));

    // T-SQL: ORDER BY em subconsulta so' com TOP ou OFFSET. Quando o usuario
    // ja' limitou (already_limited), o dele ja' esta' la'.
    if (tsql && shape.outer_order && shape.refusal == PagingRefusal::none) {
        inner += "\nOFFSET 0 ROWS";
    }

    std::string counted = "SELECT COUNT(*) FROM (\n" + inner +
                          "\n) AS otter_count";

    // O filtro entra na MESMA subconsulta, para a contagem bater com o que a
    // grade exibe: contar sem filtrar daria o total da tabela enquanto a
    // tela mostra o subconjunto.
    if (!filter.empty()) counted += "\n WHERE " + filter.where_clause(&dialect);

    result.refusal   = shape.refusal;
    result.sql       = std::move(counted);
    result.rewritten = true;
    result.requested = 1;   // uma linha, uma coluna
    return result;
}

} // namespace otter::sql
