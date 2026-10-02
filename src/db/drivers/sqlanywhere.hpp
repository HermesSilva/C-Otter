// C-Otter -- db/drivers/sqlanywhere.hpp
//
// Driver do SQL Anywhere sobre lib/tdswire (TDS 5.0). Decisao e alternativas
// no ADR 0026; mapa em docs/SQLANYWHERE-MAP.md.
#pragma once

#include "db/holt.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

[[nodiscard]] Driver& sqlanywhere_driver();

// As opcoes que o driver ajusta logo depois de conectar, uma instrucao por
// item. Publica para o teste: e' regra pura.
//
// A conexao TDS nasce com o ambiente "compativel com o Sybase ASE"
// (sp_tsql_environment): "nome" entre aspas e' TEXTO, coluna sem NULL
// declarado e' NOT NULL, e data viaja num formato que nao vai antes de 1753.
// Isto devolve o que as ferramentas do proprio SQL Anywhere usam.
[[nodiscard]] std::vector<std::string> sqlanywhere_session_options();

// "13:45:00.000000" -> "13:45:00"; "…30.500000" -> "…30.5". O servidor manda
// sempre os seis digitos (e' o formato pedido); os zeros do fim sao ruido.
[[nodiscard]] std::string sqlanywhere_trim_fraction(std::string_view text);

// A consulta que descreve as colunas de um SELECT: tipo exato, tabela e coluna
// de origem (mesmo sob apelido), chave e nulidade.
[[nodiscard]] std::string sqlanywhere_describe_query(std::string_view sql);

} // namespace otter::db
