// C-Otter -- lib/net/socks.hpp
//
// Conexao TCP por um proxy SOCKS5 (RFC 1928), com autenticacao por usuario e
// senha (RFC 1929) -- a aba "Proxy" do dialogo de conexao do DBeaver.
//
// As mensagens sao montadas por funcoes puras, separadas do socket: o teste
// confere os bytes sem precisar de um proxy de pe'.
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace otter::net {

struct ProxyEndpoint {
    std::string   host;          // vazio = sem proxy
    std::uint16_t port = 1080;
    std::string   user;          // vazio = sem autenticacao
    std::string   password;

    [[nodiscard]] bool enabled() const noexcept { return !host.empty(); }
};

// Saudacao: versao 5 e os metodos oferecidos -- "sem autenticacao" sempre, e
// "usuario/senha" quando ha' usuario.
[[nodiscard]] std::vector<std::byte> socks5_greeting(bool offer_password);

// Subnegociacao de usuario/senha. Cada um cabe num byte de tamanho: mais que
// 255 e' recusado aqui, e nao truncado em silencio.
[[nodiscard]] Result<std::vector<std::byte>> socks5_auth_request(
    std::string_view user, std::string_view password);

// CONNECT para `host:port`. O host vai como NOME (tipo 3): quem resolve o DNS
// e' o proxy -- e' o que deixa alcancar um servidor que so' a rede dele
// conhece.
[[nodiscard]] Result<std::vector<std::byte>> socks5_connect_request(
    std::string_view host, std::uint16_t port);

// O que o codigo de resposta do proxy quer dizer.
[[nodiscard]] std::string_view socks5_reply_message(std::uint8_t code) noexcept;

// Conecta ao proxy, negocia e pede o tunel. O socket devolvido ja' fala com o
// destino: quem o recebe segue como se tivesse conectado direto.
[[nodiscard]] Result<Socket> connect_through(
    const ProxyEndpoint& proxy, std::string_view host, std::uint16_t port,
    std::chrono::milliseconds timeout = std::chrono::seconds(10));

// Direto, ou pelo proxy quando ha' um. O ponto unico que os protocolos usam.
[[nodiscard]] Result<Socket> connect_to(
    const ProxyEndpoint& proxy, std::string_view host, std::uint16_t port,
    std::chrono::milliseconds timeout = std::chrono::seconds(10));

} // namespace otter::net
