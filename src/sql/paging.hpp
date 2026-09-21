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
    already_limited,   // ja' tem LIMIT ou FETCH FIRST; o usuario decidiu
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

// Monta a consulta de uma pagina.
//
// `page` e' base zero. Quando a consulta nao pode ser reescrita com seguranca,
// devolve o SQL original com `rewritten = false` e o motivo em `refusal`:
// executar algo diferente do que o usuario escreveu e' pior que uma espera.
[[nodiscard]] PagedQuery make_paged_query(std::string_view sql,
                                          const Dialect& dialect,
                                          std::size_t page,
                                          std::size_t page_size = kDefaultPageSize);

} // namespace otter::sql
