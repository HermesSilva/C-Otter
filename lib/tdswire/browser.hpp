// C-Otter -- lib/tdswire/browser.hpp
//
// SQL Server Browser: descobre a porta TCP de uma INSTANCIA NOMEADA.
//
// "SERVIDOR\INSTANCIA" nao diz a porta: cada instancia nomeada escuta numa
// porta dinamica, escolhida ao subir, e quem a informa e' o servico SQL
// Server Browser, por UDP na 1434 ([MC-SQLR], SQL Server Resolution
// Protocol). Sem esta consulta so' se conecta a uma instancia nomeada sabendo
// a porta de cor -- e tentar a 1433 conectaria na instancia PADRAO do mesmo
// servidor, que e' outro banco de dados.
#pragma once

#include "base/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace otter::tdswire {

inline constexpr std::uint16_t kBrowserPort = 1434;

// CLNT_UCAST_INST: 0x04 + o nome da instancia + 0x00.
[[nodiscard]] std::vector<std::byte> build_browser_request(std::string_view instance);

// SVR_RESP: 0x05, tamanho (2 bytes, little-endian) e o texto
// "ServerName;S;InstanceName;I;IsClustered;No;Version;16.0.1000.6;tcp;52345;;".
// Devolve a porta do par "tcp". Falha quando a resposta nao e' um SVR_RESP ou
// a instancia nao tem TCP habilitado.
[[nodiscard]] Result<std::uint16_t> parse_browser_response(std::span<const std::byte> response);

// Pergunta ao Browser de `host` a porta de `instance`. `browser_port` so'
// muda nos testes.
[[nodiscard]] Result<std::uint16_t> resolve_instance_port(
    std::string_view host, std::string_view instance, std::chrono::milliseconds timeout,
    std::uint16_t browser_port = kBrowserPort);

} // namespace otter::tdswire
