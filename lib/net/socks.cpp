#include "net/socks.hpp"

#include <array>

namespace otter::net {
namespace {

constexpr std::uint8_t kVersion        = 0x05;
constexpr std::uint8_t kNoAuth         = 0x00;
constexpr std::uint8_t kUserPassword   = 0x02;
constexpr std::uint8_t kNoneAcceptable = 0xFF;
constexpr std::uint8_t kCommandConnect = 0x01;
constexpr std::uint8_t kAddressIPv4    = 0x01;
constexpr std::uint8_t kAddressName    = 0x03;
constexpr std::uint8_t kAddressIPv6    = 0x04;

void put(std::vector<std::byte>& out, std::uint8_t value) {
    out.push_back(static_cast<std::byte>(value));
}

void put(std::vector<std::byte>& out, std::string_view text) {
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
}

} // namespace

std::vector<std::byte> socks5_greeting(bool offer_password) {
    std::vector<std::byte> out;
    put(out, kVersion);
    put(out, static_cast<std::uint8_t>(offer_password ? 2 : 1));
    put(out, kNoAuth);
    if (offer_password) put(out, kUserPassword);
    return out;
}

Result<std::vector<std::byte>> socks5_auth_request(std::string_view user,
                                                   std::string_view password) {
    if (user.empty() || user.size() > 255 || password.size() > 255) {
        return fail(Errc::invalid_argument,
                    "proxy user and password must have at most 255 bytes");
    }
    std::vector<std::byte> out;
    put(out, std::uint8_t{0x01});   // versao da SUBNEGOCIACAO, nao do SOCKS
    put(out, static_cast<std::uint8_t>(user.size()));
    put(out, user);
    put(out, static_cast<std::uint8_t>(password.size()));
    put(out, password);
    return out;
}

Result<std::vector<std::byte>> socks5_connect_request(std::string_view host,
                                                      std::uint16_t port) {
    if (host.empty() || host.size() > 255) {
        return fail(Errc::invalid_argument,
                    "the host name must have 1 to 255 bytes to go through a proxy");
    }
    std::vector<std::byte> out;
    put(out, kVersion);
    put(out, kCommandConnect);
    put(out, std::uint8_t{0x00});   // reservado
    put(out, kAddressName);
    put(out, static_cast<std::uint8_t>(host.size()));
    put(out, host);
    put(out, static_cast<std::uint8_t>(port >> 8));     // ordem de rede
    put(out, static_cast<std::uint8_t>(port & 0xFF));
    return out;
}

std::string_view socks5_reply_message(std::uint8_t code) noexcept {
    switch (code) {
        case 0x00: return "succeeded";
        case 0x01: return "general failure of the proxy server";
        case 0x02: return "connection not allowed by the proxy rules";
        case 0x03: return "network unreachable from the proxy";
        case 0x04: return "host unreachable from the proxy";
        case 0x05: return "connection refused by the destination";
        case 0x06: return "TTL expired";
        case 0x07: return "command not supported by the proxy";
        case 0x08: return "address type not supported by the proxy";
        default:   return "unknown reply from the proxy";
    }
}

Result<Socket> connect_through(const ProxyEndpoint& proxy, std::string_view host,
                               std::uint16_t port, std::chrono::milliseconds timeout) {
    const std::string where = proxy.host + ":" + std::to_string(proxy.port);

    auto connected = Socket::connect(proxy.host, proxy.port, timeout);
    if (!connected) {
        return fail(Errc::connection_failed,
                    "SOCKS proxy " + connected.error().to_string());
    }
    Socket socket = std::move(*connected);

    // --- Metodo de autenticacao ---------------------------------------------------
    const bool offer_password = !proxy.user.empty();
    OTTER_RETURN_IF_ERROR(socket.write_all(socks5_greeting(offer_password)));

    std::array<std::byte, 2> choice{};
    OTTER_RETURN_IF_ERROR(socket.read_exact(choice));
    if (static_cast<std::uint8_t>(choice[0]) != kVersion) {
        // Um proxy HTTP, ou um servico qualquer naquela porta: dizer isso em
        // vez de seguir e falhar com uma mensagem de protocolo do banco.
        return fail(Errc::protocol_error, where + " is not a SOCKS5 proxy");
    }

    const auto method = static_cast<std::uint8_t>(choice[1]);
    if (method == kNoneAcceptable) {
        return fail(Errc::auth_failed,
                    offer_password
                        ? "the SOCKS proxy accepts none of the authentication methods"
                        : "the SOCKS proxy requires a user and a password");
    }
    if (method == kUserPassword) {
        OTTER_ASSIGN_OR_RETURN(auto request,
                               socks5_auth_request(proxy.user, proxy.password));
        OTTER_RETURN_IF_ERROR(socket.write_all(request));

        std::array<std::byte, 2> verdict{};
        OTTER_RETURN_IF_ERROR(socket.read_exact(verdict));
        if (static_cast<std::uint8_t>(verdict[1]) != 0x00) {
            return fail(Errc::auth_failed, "the SOCKS proxy refused the user or the password");
        }
    } else if (method != kNoAuth) {
        return fail(Errc::protocol_error,
                    "the SOCKS proxy chose an authentication method that was not offered");
    }

    // --- Pedido de conexao ----------------------------------------------------------
    OTTER_ASSIGN_OR_RETURN(auto request, socks5_connect_request(host, port));
    OTTER_RETURN_IF_ERROR(socket.write_all(request));

    std::array<std::byte, 4> head{};
    OTTER_RETURN_IF_ERROR(socket.read_exact(head));
    if (static_cast<std::uint8_t>(head[0]) != kVersion) {
        return fail(Errc::protocol_error, where + " sent an invalid SOCKS5 reply");
    }
    if (const auto code = static_cast<std::uint8_t>(head[1]); code != 0x00) {
        return fail(Errc::connection_failed,
                    std::string(host) + ":" + std::to_string(port) + " via SOCKS proxy " +
                        where + " — " + std::string(socks5_reply_message(code)));
    }

    // O endereco a que o proxy se ligou: lido para ESVAZIAR o canal -- o que
    // vier depois ja' e' do banco.
    std::size_t rest = 0;
    switch (static_cast<std::uint8_t>(head[3])) {
        case kAddressIPv4: rest = 4; break;
        case kAddressIPv6: rest = 16; break;
        case kAddressName: {
            std::array<std::byte, 1> length{};
            OTTER_RETURN_IF_ERROR(socket.read_exact(length));
            rest = static_cast<std::uint8_t>(length[0]);
            break;
        }
        default:
            return fail(Errc::protocol_error, where + " sent an invalid SOCKS5 reply");
    }
    std::vector<std::byte> bound(rest + 2);   // + a porta
    OTTER_RETURN_IF_ERROR(socket.read_exact(bound));

    return socket;
}

Result<Socket> connect_to(const ProxyEndpoint& proxy, std::string_view host,
                          std::uint16_t port, std::chrono::milliseconds timeout) {
    return proxy.enabled() ? connect_through(proxy, host, port, timeout)
                           : Socket::connect(host, port, timeout);
}

} // namespace otter::net
