// C-Otter -- sql/paging.hpp
//
// Reescrita de uma consulta para trazer uma pagina de resultado.
//
// Motivo e alternativas rejeitadas em docs/adr/0011-result-paging.md. Em
// resumo: um SELECT sem LIMIT numa tabela grande travava a UI ate' o servidor
// terminar de enviar tudo.
#pragma once

#include "sql/lexer.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace otter::sql {

// Tamanho de pagina padrao. Mesmo valor do DBeaver
// (ModelPreferences.RESULT_SET_MAX_ROWS): grande o bastante para preencher a
// tela, pequeno o bastante para voltar rapido.
inline constexpr std::size_t kDefaultPageSize = 200;

// Por que uma consulta NAO pode ser paginada. Vai para a interface: uma grade
// que mostra resultado parcial sem dizer por que e' o defeito que a diretiva 6
// proibe.
enum class PagingRefusal : std::uint8_t {
    none,              // pode paginar
    not_a_query,       // INSERT/UPDATE/DELETE/DDL -- nao produz linhas
    already_limited,   // ja' tem LIMIT, FETCH FIRST ou TOP; o usuario decidiu
    multiple_commands, // varios statements num texto so'
    unsupported_form,  // EXPLAIN, SHOW, VALUES, ...
    empty,
};

[[nodiscard]] std::string_view to_string(PagingRefusal refusal) noexcept;

struct PagedQuery {
    std::string   sql;            // o que sera' executado
    bool          rewritten = false;
    PagingRefusal refusal = PagingRefusal::none;

    // Quantas linhas foram pedidas: page_size + 1. A linha extra responde
    // "existe proxima pagina?" sem um COUNT(*), que custaria outra varredura.
    std::size_t requested = 0;
};

// Ordenacao pedida pelo cabecalho da grade.
//
// Vai para o SERVIDOR, nao para o cliente: com paginacao, ordenar as 200
// linhas da pagina daria a ordem errada -- seriam as 200 primeiras linhas na
// ordem do banco, reordenadas entre si, e nao as 200 menores do resultado.
struct SortOrder {
    std::string column;         // vazio = sem ordenacao
    bool        descending = false;

    [[nodiscard]] bool empty() const noexcept { return column.empty(); }
};

// Filtro pedido no cabecalho da grade.
//
// Vai para o servidor pelo mesmo motivo da ordenacao: filtrar as 200 linhas
// visiveis mostraria as que sobram de UMA pagina, nao as que atendem ao
// criterio no resultado inteiro.
//
// A expressao e' escrita pelo usuario e vai para o SQL como digitada -- e'
// uma clausula WHERE, nao um valor. Quem digita "1=1 OR TRUE" esta' apenas
// consultando o proprio banco com as proprias credenciais; nao ha' elevacao
// de privilegio a impedir. A UI deixa isso explicito chamando o campo de
// "expressao", nao de "valor".
struct ColumnFilter {
    std::string column;         // vazio = sem filtro
    std::string expression;     // "> 100", "LIKE '%lontra%'", "IS NULL"

    // Criterios de OUTRAS colunas, alem do primeiro. O DBeaver filtra por
    // varias colunas ao mesmo tempo ("Filter settings", ou "Filter by value"
    // numa coluna depois de outra); com um criterio so', filtrar a segunda
    // desfazia o filtro da primeira.
    struct Criterion {
        std::string column;
        std::string expression;
    };
    std::vector<Criterion> more;

    // Condicao livre, da barra de filtro acima da grade -- o campo "Enter a
    // SQL expression to filter results" do DBeaver. Entra entre parenteses.
    std::string condition;

    [[nodiscard]] bool empty() const noexcept {
        if (!column.empty() && !expression.empty()) return false;
        for (const Criterion& criterion : more) {
            if (!criterion.column.empty() && !criterion.expression.empty()) {
                return false;
            }
        }
        return condition.empty();
    }

    // A expressao em vigor para a coluna; vazio se nao ha'.
    [[nodiscard]] std::string_view expression_for(std::string_view name) const noexcept;

    // Define (ou, com expressao vazia, remove) o criterio de UMA coluna, sem
    // tocar nos das outras.
    void set_column(std::string_view name, std::string_view new_expression);

    // Quantas colunas tem criterio.
    [[nodiscard]] std::size_t column_count() const noexcept;

    // `"a" > 1 AND "b" IS NULL AND (condicao)` -- o que vai depois do WHERE.
    // Vazio quando nao ha' filtro. Os nomes saem com o delimitador do
    // dialeto ([a] no SQL Server, `a` no MySQL); sem dialeto, aspas duplas.
    [[nodiscard]] std::string where_clause(const Dialect* dialect = nullptr) const;
};

// Monta a consulta de uma pagina.
//
// `page` e' base zero. Quando a consulta nao pode ser reescrita com seguranca,
// devolve o SQL original com `rewritten = false` e o motivo em `refusal`:
// executar algo diferente do que o usuario escreveu e' pior que uma espera.
[[nodiscard]] PagedQuery make_paged_query(std::string_view sql,
                                          const Dialect& dialect,
                                          std::size_t page,
                                          std::size_t page_size = kDefaultPageSize,
                                          const SortOrder& sort = {},
                                          const ColumnFilter& filter = {});

// Monta a consulta SEM limite -- o `resultset.fetch.all` do DBeaver.
//
// Preserva o filtro e a ordenacao escolhidos na grade (e' o que esta' na
// tela), e omite apenas o LIMIT/OFFSET. Um `page_size` enorme daria quase o
// mesmo SQL, mas com um LIMIT fingido: a grade diria "sem limite" e o
// servidor receberia um numero.
//
// As recusas sao as mesmas: o que nao da' para paginar sai inalterado, com
// `rewritten = false`.
//
// Contraria o ADR 0011 de proposito, a pedido explicito do usuario. Quem
// varre uma tabela inteira paga a espera; o ADR recomenda exportar.
[[nodiscard]] PagedQuery make_unpaged_query(std::string_view sql,
                                            const Dialect& dialect,
                                            const SortOrder& sort = {},
                                            const ColumnFilter& filter = {});

// Monta a consulta que conta o resultado INTEIRO -- o `resultset.count` do
// DBeaver.
//
// Sob demanda, nunca automatica: e' outra varredura completa da tabela, e
// dispara-la a cada consulta transformaria toda paginacao no custo que a
// paginacao existe para evitar (ADR 0011). A grade mostra "+" ate' que se
// peca o numero.
//
// Envolve a consulta numa subconsulta, pelas mesmas tres razoes do filtro:
// a original pode ter WHERE, pode terminar em GROUP BY, e contar antes da
// agregacao daria um numero diferente do que a grade mostra.
//
// O ORDER BY do usuario fica DENTRO da subconsulta. E' inofensivo para a
// contagem, e remove-lo exigiria reescrever o SQL dele.
//
// Usa as MESMAS recusas de make_paged_query: o que nao da' para paginar
// tambem nao da' para contar.
[[nodiscard]] PagedQuery make_count_query(std::string_view sql,
                                          const Dialect& dialect,
                                          const ColumnFilter& filter = {});

} // namespace otter::sql
