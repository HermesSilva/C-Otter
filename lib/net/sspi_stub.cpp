// C-Otter -- SSPI fora do Windows: nao ha'. A tela consulta available() e nao
// oferece a autenticacao integrada (diretiva 6).
#ifndef _WIN32

#include "net/sspi.hpp"

namespace otter::net {

struct SspiClient::Impl {};

SspiClient::SspiClient() = default;
SspiClient::~SspiClient() = default;
SspiClient::SspiClient(SspiClient&&) noexcept = default;
SspiClient& SspiClient::operator=(SspiClient&&) noexcept = default;

bool SspiClient::available() noexcept { return false; }
bool SspiClient::complete() const noexcept { return false; }

Result<std::vector<std::byte>> SspiClient::step(std::string_view,
                                                std::span<const std::byte>) {
    return fail(Errc::not_supported,
                "Windows authentication is only available on Windows");
}

std::string fully_qualified_host(std::string_view host) { return std::string(host); }

} // namespace otter::net

#endif // !_WIN32
