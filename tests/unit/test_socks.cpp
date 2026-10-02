// C-Otter -- testes de lib/net/socks: as mensagens do SOCKS5.
//
// Os bytes sao do RFC 1928 (SOCKS5) e do RFC 1929 (usuario/senha). Um byte
// fora do lugar e o proxy fecha a conexao sem dizer por que -- e o usuario ve'
// "falha ao receber" numa conexao que parecia configurada certo.
#include "test_main.hpp"

#include "net/socks.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace otter::net;

namespace {

std::vector<std::uint8_t> bytes(const std::vector<std::byte>& data) {
    std::vector<std::uint8_t> out;
    for (const std::byte b : data) out.push_back(static_cast<std::uint8_t>(b));
    return out;
}

} // namespace

OTTER_TEST(socks5_greeting_offers_the_methods) {
    // VER=5, NMETHODS, METHODS...
    OTTER_CHECK(bytes(socks5_greeting(false)) == (std::vector<std::uint8_t>{5, 1, 0}));
    OTTER_CHECK(bytes(socks5_greeting(true)) == (std::vector<std::uint8_t>{5, 2, 0, 2}));
}

OTTER_TEST(socks5_auth_request_layout) {
    // RFC 1929: VER=1 (da subnegociacao, NAO 5), ULEN, UNAME, PLEN, PASSWD.
    const auto request = socks5_auth_request("ana", "s3");
    OTTER_CHECK(request.has_value());
    OTTER_CHECK(bytes(*request) ==
                (std::vector<std::uint8_t>{1, 3, 'a', 'n', 'a', 2, 's', '3'}));

    // Senha vazia e' valida; usuario vazio nao.
    OTTER_CHECK(socks5_auth_request("ana", "").has_value());
    OTTER_CHECK(!socks5_auth_request("", "x").has_value());

    // O tamanho cabe num byte: 256 nao pode virar 0 em silencio.
    OTTER_CHECK(socks5_auth_request(std::string(255, 'u'), std::string(255, 'p')).has_value());
    OTTER_CHECK(!socks5_auth_request(std::string(256, 'u'), "p").has_value());
    OTTER_CHECK(!socks5_auth_request("u", std::string(256, 'p')).has_value());
}

OTTER_TEST(socks5_connect_request_layout) {
    // VER=5, CMD=1 (CONNECT), RSV=0, ATYP=3 (nome), LEN, nome, porta em ordem
    // de rede. 5432 = 0x1538.
    const auto request = socks5_connect_request("db.local", 5432);
    OTTER_CHECK(request.has_value());
    OTTER_CHECK(bytes(*request) ==
                (std::vector<std::uint8_t>{5, 1, 0, 3, 8, 'd', 'b', '.', 'l', 'o', 'c', 'a',
                                           'l', 0x15, 0x38}));

    // Porta acima de 255: o byte alto nao pode se perder.
    const auto high = socks5_connect_request("h", 65535);
    OTTER_CHECK(bytes(*high) == (std::vector<std::uint8_t>{5, 1, 0, 3, 1, 'h', 0xFF, 0xFF}));

    OTTER_CHECK(!socks5_connect_request("", 5432).has_value());
    OTTER_CHECK(!socks5_connect_request(std::string(256, 'h'), 5432).has_value());
}

OTTER_TEST(socks5_reply_codes_have_distinct_messages) {
    // "connection refused" e "host unreachable" pedem providencias
    // diferentes: nao podem sair com o mesmo texto.
    OTTER_CHECK(socks5_reply_message(0x00) == "succeeded");
    OTTER_CHECK(socks5_reply_message(0x05) != socks5_reply_message(0x04));
    OTTER_CHECK(socks5_reply_message(0x02) != socks5_reply_message(0x01));
    OTTER_CHECK(!socks5_reply_message(0x77).empty());
}

OTTER_TEST(socks5_endpoint_is_off_without_a_host) {
    ProxyEndpoint proxy;
    OTTER_CHECK(!proxy.enabled());
    proxy.host = "127.0.0.1";
    OTTER_CHECK(proxy.enabled());
}

OTTER_TEST(socks5_proxy_that_is_down_is_named_in_the_error) {
    // A mensagem tem de dizer que quem falhou foi o PROXY, e qual: "conexao
    // recusada" sozinho mandaria o usuario olhar o banco.
    ProxyEndpoint proxy;
    proxy.host = "127.0.0.1";
    proxy.port = 1;   // nada escuta aqui
    const auto socket = connect_through(proxy, "db.local", 5432, std::chrono::seconds(2));
    OTTER_CHECK(!socket.has_value());
    OTTER_CHECK(socket.error().to_string().find("SOCKS proxy") != std::string::npos);
    OTTER_CHECK(socket.error().to_string().find("127.0.0.1:1") != std::string::npos);
}
