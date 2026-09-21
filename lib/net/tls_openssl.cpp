// C-Otter -- TLS sobre OpenSSL (Linux).
//
// O ADR 0009 escolheu a API do sistema em cada plataforma: Schannel no
// Windows, OpenSSL no Linux. O `otter_net` já liga contra `OpenSSL::Crypto`
// no Linux por causa do `crypto_openssl.cpp`; aqui entra `libssl`.
//
// AINDA NÃO IMPLEMENTADO. Este arquivo existe para que a compilação no Linux
// não quebre por símbolo ausente, e para que `tls_available()` responda
// HONESTAMENTE que não há TLS -- em vez de a caixa "usar TLS" aparecer na
// interface e a conexão falhar depois.
#ifndef _WIN32

#include "net/tls.hpp"

namespace otter::net {

struct TlsChannel::Impl {};

TlsChannel::TlsChannel() = default;
TlsChannel::~TlsChannel() = default;
TlsChannel::TlsChannel(TlsChannel&&) noexcept = default;
TlsChannel& TlsChannel::operator=(TlsChannel&&) noexcept = default;

Status TlsChannel::handshake(Socket&, const TlsOptions&) {
    return fail(Errc::not_supported,
                "TLS is not implemented on this platform yet");
}

Status TlsChannel::write_all(Socket&, std::span<const std::byte>) {
    return fail(Errc::not_supported, "TLS is not implemented on this platform");
}

Status TlsChannel::read_exact(Socket&, std::span<std::byte>) {
    return fail(Errc::not_supported, "TLS is not implemented on this platform");
}

bool TlsChannel::is_open() const noexcept { return false; }
void TlsChannel::close() noexcept {}

bool tls_available() noexcept { return false; }

} // namespace otter::net

#endif // !_WIN32
