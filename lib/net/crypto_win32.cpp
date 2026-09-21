// C-Otter -- primitivas criptograficas sobre CNG (Windows).
#ifdef _WIN32

#include "net/crypto.hpp"

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>

#pragma comment(lib, "bcrypt.lib")

namespace otter::crypto {
namespace {

constexpr bool succeeded(NTSTATUS status) { return status >= 0; }

// RAII para o handle de algoritmo do CNG.
class AlgorithmHandle {
public:
    AlgorithmHandle() = default;
    ~AlgorithmHandle() {
        if (handle_ != nullptr) BCryptCloseAlgorithmProvider(handle_, 0);
    }

    AlgorithmHandle(const AlgorithmHandle&)            = delete;
    AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;

    [[nodiscard]] NTSTATUS open(LPCWSTR algorithm, bool hmac) {
        return BCryptOpenAlgorithmProvider(
            &handle_, algorithm, nullptr,
            hmac ? BCRYPT_ALG_HANDLE_HMAC_FLAG : 0);
    }

    [[nodiscard]] BCRYPT_ALG_HANDLE get() const noexcept { return handle_; }

private:
    BCRYPT_ALG_HANDLE handle_ = nullptr;
};

template <std::size_t N>
Result<std::array<std::byte, N>> hash_with(LPCWSTR algorithm,
                                           std::span<const std::byte> key,
                                           std::span<const std::byte> data,
                                           bool hmac) {
    AlgorithmHandle provider;
    if (!succeeded(provider.open(algorithm, hmac))) {
        return fail(Errc::internal, "BCryptOpenAlgorithmProvider falhou");
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    NTSTATUS status = BCryptCreateHash(
        provider.get(), &hash, nullptr, 0,
        hmac ? const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(key.data()))
             : nullptr,
        hmac ? static_cast<ULONG>(key.size()) : 0,
        0);

    if (!succeeded(status)) {
        return fail(Errc::internal, "BCryptCreateHash falhou");
    }

    std::array<std::byte, N> digest{};

    status = BCryptHashData(
        hash,
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(data.data())),
        static_cast<ULONG>(data.size()), 0);

    if (succeeded(status)) {
        status = BCryptFinishHash(
            hash, reinterpret_cast<PUCHAR>(digest.data()),
            static_cast<ULONG>(digest.size()), 0);
    }

    BCryptDestroyHash(hash);

    if (!succeeded(status)) return fail(Errc::internal, "cálculo de hash falhou");
    return digest;
}

} // namespace

Result<Sha256Digest> sha256(std::span<const std::byte> data) {
    return hash_with<32>(BCRYPT_SHA256_ALGORITHM, {}, data, /*hmac=*/false);
}

Result<Md5Digest> md5(std::span<const std::byte> data) {
    return hash_with<16>(BCRYPT_MD5_ALGORITHM, {}, data, /*hmac=*/false);
}

Result<Sha256Digest> hmac_sha256(std::span<const std::byte> key,
                                 std::span<const std::byte> data) {
    return hash_with<32>(BCRYPT_SHA256_ALGORITHM, key, data, /*hmac=*/true);
}

Result<Sha256Digest> pbkdf2_sha256(std::string_view password,
                                   std::span<const std::byte> salt,
                                   std::uint32_t iterations) {
    AlgorithmHandle provider;
    if (!succeeded(provider.open(BCRYPT_SHA256_ALGORITHM, /*hmac=*/true))) {
        return fail(Errc::internal, "BCryptOpenAlgorithmProvider falhou");
    }

    Sha256Digest derived{};

    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        provider.get(),
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(salt.data())),
        static_cast<ULONG>(salt.size()),
        iterations,
        reinterpret_cast<PUCHAR>(derived.data()),
        static_cast<ULONG>(derived.size()),
        0);

    if (!succeeded(status)) {
        return fail(Errc::internal, "BCryptDeriveKeyPBKDF2 falhou");
    }
    return derived;
}

Result<std::vector<std::byte>> random_bytes(std::size_t count) {
    std::vector<std::byte> out(count);
    const NTSTATUS status = BCryptGenRandom(
        nullptr, reinterpret_cast<PUCHAR>(out.data()),
        static_cast<ULONG>(out.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (!succeeded(status)) {
        return fail(Errc::internal, "BCryptGenRandom falhou");
    }
    return out;
}

} // namespace otter::crypto

#endif // _WIN32
