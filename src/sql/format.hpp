// C-Otter -- sql/format.hpp
//
// Reindenta SQL usando o lexer existente.
//
// O objetivo e' deixar legivel uma consulta colada de uma linha so', nao
// impor um estilo. Um formatador que reescreve tudo do jeito dele e' motivo
// para o usuario desligar o formatador.
#pragma once

#include "sql/lexer.hpp"

#include <string>
#include <string_view>

namespace otter::sql {

enum class KeywordCase : std::uint8_t {
    preserve,   // como o usuario escreveu
    upper,      // SELECT
    lower,      // select
};

struct FormatOptions {
    KeywordCase keyword_case = KeywordCase::upper;
    int         indent_width = 4;

    // Alinha a' direita as clausulas principais, como o estilo do psql:
    //
    //     SELECT a, b
    //       FROM t
    //      WHERE x
    //
    // Desligado produz o estilo mais comum em codigo:
    //
    //     SELECT a, b
    //     FROM t
    //     WHERE x
    bool        river_style = true;

    // Quebra a lista do SELECT em uma coluna por linha quando ela passa de
    // `wrap_after` itens. Listas curtas cabem numa linha e ficam melhores
    // assim.
    std::size_t wrap_select_after = 3;
};

// Reformata o SQL. Entrada malformada e' devolvida como veio: reindentar algo
// que nao foi entendido produziria texto pior que o original.
[[nodiscard]] std::string format_sql(std::string_view sql,
                                     const Dialect& dialect,
                                     const FormatOptions& options = {});

} // namespace otter::sql
