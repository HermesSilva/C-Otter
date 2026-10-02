#include "base/os_secret.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>

#include <string>
#endif

namespace otter {

#ifdef _WIN32
namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

std::string narrow(const wchar_t* text, std::size_t length) {
    if (length == 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), out.data(), size,
                        nullptr, nullptr);
    return out;
}

} // namespace

Result<std::string> read_keyring_secret(std::string_view service, std::string_view user) {
    const std::wstring wanted_user = widen(user);

    // O `keyring` grava com o SERVICO como alvo. Quando ja' existe outra
    // credencial desse servico com outro usuario, ele passa a usar
    // "usuario@servico" -- por isso as duas tentativas, e a conferencia do
    // usuario na primeira.
    const std::wstring targets[] = {
        widen(std::string(user) + "@" + std::string(service)),
        widen(service),
    };

    for (const std::wstring& target : targets) {
        PCREDENTIALW credential = nullptr;
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) continue;

        const bool same_user =
            credential->UserName != nullptr && wanted_user == credential->UserName;
        std::string secret;
        if (same_user && credential->CredentialBlob != nullptr) {
            // O segredo e' texto em UTF-16LE, sem terminador.
            secret = narrow(reinterpret_cast<const wchar_t*>(credential->CredentialBlob),
                            credential->CredentialBlobSize / sizeof(wchar_t));
        }
        CredFree(credential);
        if (same_user) return secret;
    }
    return fail(Errc::not_found, "no such entry in the Windows Credential Manager");
}

Result<std::string> read_generic_credential(std::string_view target) {
    const std::wstring wide = widen(target);

    PCREDENTIALW credential = nullptr;
    if (!CredReadW(wide.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        return fail(Errc::not_found, "no such entry in the Windows Credential Manager");
    }
    std::string secret;
    if (credential->CredentialBlob != nullptr) {
        secret = narrow(reinterpret_cast<const wchar_t*>(credential->CredentialBlob),
                        credential->CredentialBlobSize / sizeof(wchar_t));
    }
    CredFree(credential);
    return secret;
}

#else

Result<std::string> read_generic_credential(std::string_view) {
    return fail(Errc::not_supported, "the system secret store is only read on Windows");
}

Result<std::string> read_keyring_secret(std::string_view, std::string_view) {
    // Secret Service (Linux) e Keychain (macOS) ainda nao sao lidos.
    return fail(Errc::not_supported, "the system secret store is only read on Windows");
}

#endif

} // namespace otter
