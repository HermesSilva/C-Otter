#include "tdswire/browser.hpp"

#include "net/socket.hpp"

#include <charconv>
#include <string>

namespace otter::tdswire {

std::vector<std::byte> build_browser_request(std::string_view instance) {
    std::vector<std::byte> request;
    request.reserve(instance.size() + 2);
    request.push_back(std::byte{0x04});   // CLNT_UCAST_INST
    for (const char c : instance) request.push_back(static_cast<std::byte>(c));
    request.push_back(std::byte{0x00});
    return request;
}

Result<std::uint16_t> parse_browser_response(std::span<const std::byte> response) {
    if (response.size() < 3 || response[0] != std::byte{0x05}) {
        return fail(Errc::protocol_error, "not a SQL Server Browser response");
    }
    const std::size_t declared = static_cast<std::size_t>(response[1]) |
                                 (static_cast<std::size_t>(response[2]) << 8);
    const std::size_t length = std::min(declared, response.size() - 3);
    const std::string_view text(reinterpret_cast<const char*>(response.data()) + 3, length);

    // Pares nome;valor. O que interessa e' "tcp;<porta>" -- e so' como NOME:
    // uma instancia chamada "tcp" apareceria como valor de InstanceName.
    std::size_t at = 0;
    bool is_name = true;
    bool want_port = false;
    while (at <= text.size()) {
        const std::size_t end = text.find(';', at);
        const std::string_view token =
            text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);

        if (want_port) {
            unsigned value = 0;
            const auto [ptr, ec] =
                std::from_chars(token.data(), token.data() + token.size(), value);
            if (ec != std::errc{} || ptr != token.data() + token.size() || value == 0 ||
                value > 65535) {
                return fail(Errc::protocol_error, "invalid TCP port in the Browser response");
            }
            return static_cast<std::uint16_t>(value);
        }
        if (is_name && token == "tcp") want_port = true;
        is_name = !is_name;

        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return fail(Errc::not_found, "the instance does not have TCP/IP enabled");
}

Result<std::uint16_t> resolve_instance_port(std::string_view host, std::string_view instance,
                                            std::chrono::milliseconds timeout,
                                            std::uint16_t browser_port) {
    if (instance.empty()) return fail(Errc::invalid_argument, "the instance name is empty");

    OTTER_ASSIGN_OR_RETURN(
        auto response,
        net::udp_exchange(host, browser_port, build_browser_request(instance), timeout));
    return parse_browser_response(response);
}

} // namespace otter::tdswire
