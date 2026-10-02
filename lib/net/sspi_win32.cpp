// C-Otter -- SSPI (Windows): autenticacao integrada.
#ifdef _WIN32

#include "net/sspi.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define SECURITY_WIN32
#include <security.h>

#include <cstdio>

#pragma comment(lib, "secur32.lib")

namespace otter::net {
namespace {

std::wstring to_utf16(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

} // namespace

struct SspiClient::Impl {
    CredHandle credentials{};
    CtxtHandle context{};
    bool       has_credentials = false;
    bool       has_context     = false;
    bool       complete        = false;

    ~Impl() {
        if (has_context)     DeleteSecurityContext(&context);
        if (has_credentials) FreeCredentialsHandle(&credentials);
    }
};

SspiClient::SspiClient() : impl_(std::make_unique<Impl>()) {}
SspiClient::~SspiClient() = default;
SspiClient::SspiClient(SspiClient&&) noexcept = default;
SspiClient& SspiClient::operator=(SspiClient&&) noexcept = default;

bool SspiClient::available() noexcept { return true; }

bool SspiClient::complete() const noexcept { return impl_ != nullptr && impl_->complete; }

Result<std::vector<std::byte>> SspiClient::step(std::string_view target,
                                                std::span<const std::byte> input) {
    if (impl_ == nullptr) return fail(Errc::internal, "invalid SSPI client");

    if (!impl_->has_credentials) {
        // Credencial da conta corrente (pAuthData nulo): nenhuma senha e'
        // pedida nem guardada.
        const SECURITY_STATUS status = AcquireCredentialsHandleW(
            nullptr, const_cast<LPWSTR>(L"Negotiate"), SECPKG_CRED_OUTBOUND, nullptr,
            nullptr, nullptr, nullptr, &impl_->credentials, nullptr);
        if (status != SEC_E_OK) {
            return fail(Errc::auth_failed,
                        "Windows authentication is not available for this account");
        }
        impl_->has_credentials = true;
    }

    const std::wstring spn = to_utf16(target);

    SecBuffer     in_buffer{};
    SecBufferDesc in_desc{SECBUFFER_VERSION, 1, &in_buffer};
    in_buffer.BufferType = SECBUFFER_TOKEN;
    in_buffer.pvBuffer   = const_cast<std::byte*>(input.data());
    in_buffer.cbBuffer   = static_cast<unsigned long>(input.size());

    SecBuffer     out_buffer{};
    SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out_buffer};
    out_buffer.BufferType = SECBUFFER_TOKEN;

    DWORD attributes = 0;
    const SECURITY_STATUS status = InitializeSecurityContextW(
        &impl_->credentials, impl_->has_context ? &impl_->context : nullptr,
        spn.empty() ? nullptr : const_cast<LPWSTR>(spn.c_str()),
        ISC_REQ_CONNECTION | ISC_REQ_ALLOCATE_MEMORY, 0, SECURITY_NATIVE_DREP,
        input.empty() ? nullptr : &in_desc, 0, &impl_->context, &out_desc, &attributes,
        nullptr);

    if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED) {
        char code[32];
        std::snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(status));
        return fail(Errc::auth_failed,
                    std::string("Windows authentication failed (") + code + ")");
    }
    impl_->has_context = true;
    impl_->complete    = status == SEC_E_OK;

    std::vector<std::byte> token;
    if (out_buffer.pvBuffer != nullptr) {
        const auto* bytes = static_cast<const std::byte*>(out_buffer.pvBuffer);
        token.assign(bytes, bytes + out_buffer.cbBuffer);
        FreeContextBuffer(out_buffer.pvBuffer);
    }
    return token;
}

std::string fully_qualified_host(std::string_view host) {
    // "localhost" e "." sao a propria maquina: o SPN usa o nome dela.
    std::string name(host);
    if (name == "localhost" || name == "127.0.0.1" || name == "." || name == "::1") {
        wchar_t buffer[256];
        DWORD size = 256;
        if (GetComputerNameExW(ComputerNameDnsFullyQualified, buffer, &size)) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size),
                                                  nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(size), out.data(),
                                bytes, nullptr, nullptr);
            return out;
        }
        return name;
    }

    addrinfo hints{};
    hints.ai_flags = AI_CANONNAME;
    addrinfo* result = nullptr;
    if (getaddrinfo(name.c_str(), nullptr, &hints, &result) == 0 && result != nullptr) {
        if (result->ai_canonname != nullptr && result->ai_canonname[0] != '\0') {
            name = result->ai_canonname;
        }
        freeaddrinfo(result);
    }
    return name;
}

} // namespace otter::net

#endif // _WIN32
