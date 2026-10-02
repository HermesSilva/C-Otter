// C-Otter -- db/drivers/oracle.hpp
//
// Driver do Oracle sobre lib/orawire (TNS/TTC). Decisao e alternativas no
// ADR 0027; mapa em docs/ORACLE-MAP.md.
#pragma once

#include "db/holt.hpp"

#include <string>
#include <string_view>

namespace otter::db {

[[nodiscard]] Driver& oracle_driver();

// O texto como o servidor o aceita. Publica para o teste: e' regra pura.
//
// O Oracle recebe UMA instrucao, e o terminador nao faz parte dela: um
// "SELECT ... ;" volta ORA-00933. Quem vem de qualquer outro cliente digita o
// ponto e virgula, entao ele sai aqui -- menos onde e' da linguagem: um bloco
// PL/SQL (BEGIN/DECLARE) e um CREATE de procedure, funcao, pacote, trigger ou
// tipo TERMINAM em ';'. A barra sozinha na ultima linha, o "execute" do
// SQL*Plus, sai sempre.
[[nodiscard]] std::string oracle_statement_text(std::string_view sql);

// "nome" com a aspa dobrada.
[[nodiscard]] std::string oracle_quote(std::string_view identifier);

} // namespace otter::db
