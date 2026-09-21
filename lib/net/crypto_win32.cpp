// C-Otter -- primitivas criptograficas sobre CNG (Windows).
#ifdef _WIN32

#include "net/crypto.hpp"

#include <windows.h>

#include <bcrypt.h>

// WIN32_LEAN_AND_MEAN (posto pelo CMake) exclui o wincrypt.h que o windows.h
// puxaria sozinho. Precisamos dele para CryptStringToBinaryA e
// CryptDecodeObjectEx, usados ao ler a chave publica RSA em PEM.
#include <wincrypt.h>

#include <algorithm>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

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

Result<Sha1Digest> sha1(std::span<const std::byte> data) {
    return hash_with<20>(BCRYPT_SHA1_ALGORITHM, {}, data, /*hmac=*/false);
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

Result<std::vector<std::byte>> rsa_encrypt_oaep(std::string_view public_key_pem,
                                                std::span<const std::byte> data) {
    // PEM -> DER. CryptStringToBinaryA entende o envelope BEGIN/END sozinho,
    // o que evita escrever um decodificador base64 com casos de borda.
    DWORD der_size = 0;
    if (!CryptStringToBinaryA(public_key_pem.data(),
                              static_cast<DWORD>(public_key_pem.size()),
                              CRYPT_STRING_BASE64HEADER, nullptr, &der_size,
                              nullptr, nullptr)) {
        return fail(Errc::parse_error, "chave publica RSA nao esta' em PEM valido");
    }

    std::vector<BYTE> der(der_size);
    if (!CryptStringToBinaryA(public_key_pem.data(),
                              static_cast<DWORD>(public_key_pem.size()),
                              CRYPT_STRING_BASE64HEADER, der.data(), &der_size,
                              nullptr, nullptr)) {
        return fail(Errc::parse_error, "falha ao decodificar o PEM da chave RSA");
    }

    // O MySQL manda a chave no formato SubjectPublicKeyInfo ("BEGIN PUBLIC
    // KEY"), que embrulha o RSAPublicKey. Sao dois passos de decodificacao:
    // quem tenta importar o SPKI direto como RSAPublicKey recebe
    // NTE_BAD_DATA sem explicacao.
    CERT_PUBLIC_KEY_INFO* info = nullptr;
    DWORD info_size = 0;

    std::vector<BYTE> info_buffer;
    const BYTE*       rsa_blob = der.data();
    DWORD             rsa_blob_size = der_size;

    if (CryptDecodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO,
                            der.data(), der_size, CRYPT_DECODE_ALLOC_FLAG,
                            nullptr, &info, &info_size)) {
        rsa_blob      = info->PublicKey.pbData;
        rsa_blob_size = info->PublicKey.cbData;
    }
    // Se falhou, `der` ja' e' um RSAPublicKey cru -- e' o formato
    // "BEGIN RSA PUBLIC KEY", que algumas versoes emitem.

    BCRYPT_KEY_HANDLE key = nullptr;
    DWORD             blob_size = 0;
    BYTE*             cng_blob = nullptr;

    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, CNG_RSA_PUBLIC_KEY_BLOB,
                             rsa_blob, rsa_blob_size, CRYPT_DECODE_ALLOC_FLAG,
                             nullptr, &cng_blob, &blob_size)) {
        if (info) LocalFree(info);
        return fail(Errc::parse_error, "chave RSA com estrutura inesperada");
    }

    AlgorithmHandle algorithm;
    NTSTATUS status = algorithm.open(BCRYPT_RSA_ALGORITHM, /*hmac=*/false);
    if (!succeeded(status)) {
        LocalFree(cng_blob);
        if (info) LocalFree(info);
        return fail(Errc::internal, "BCryptOpenAlgorithmProvider(RSA) falhou");
    }

    status = BCryptImportKeyPair(algorithm.get(), nullptr, BCRYPT_RSAPUBLIC_BLOB,
                                 &key, cng_blob, blob_size, 0);
    LocalFree(cng_blob);
    if (info) LocalFree(info);

    if (!succeeded(status)) {
        return fail(Errc::parse_error, "BCryptImportKeyPair falhou");
    }

    BCRYPT_OAEP_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA1_ALGORITHM;
    padding.pbLabel  = nullptr;
    padding.cbLabel  = 0;

    DWORD out_size = 0;
    status = BCryptEncrypt(key, const_cast<PUCHAR>(
                               reinterpret_cast<const UCHAR*>(data.data())),
                           static_cast<ULONG>(data.size()), &padding, nullptr, 0,
                           nullptr, 0, &out_size, BCRYPT_PAD_OAEP);
    if (!succeeded(status)) {
        BCryptDestroyKey(key);
        return fail(Errc::internal, "BCryptEncrypt (tamanho) falhou");
    }

    std::vector<std::byte> out(out_size);
    status = BCryptEncrypt(key, const_cast<PUCHAR>(
                               reinterpret_cast<const UCHAR*>(data.data())),
                           static_cast<ULONG>(data.size()), &padding, nullptr, 0,
                           reinterpret_cast<PUCHAR>(out.data()),
                           static_cast<ULONG>(out.size()), &out_size,
                           BCRYPT_PAD_OAEP);
    BCryptDestroyKey(key);

    if (!succeeded(status)) {
        return fail(Errc::internal, "BCryptEncrypt falhou");
    }
    out.resize(out_size);
    return out;
}

} // namespace otter::crypto

#endif // _WIN32
