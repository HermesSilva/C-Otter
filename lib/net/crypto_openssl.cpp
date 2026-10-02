// C-Otter -- primitivas criptograficas sobre OpenSSL (Linux).
//
// Par do crypto_win32.cpp: mesma interface, mesmas mensagens de erro, API do
// sistema em vez de algoritmo escrito a mao (ver crypto.hpp).
//
// So' a API EVP. As funcoes por algoritmo (SHA256_Init, HMAC_CTX_*, RSA_*)
// foram marcadas obsoletas no OpenSSL 3.0, e o aviso de obsolescencia vira
// erro com o nosso -Werror.
#ifndef _WIN32

#include "net/crypto.hpp"

#include <openssl/bio.h>
#include <openssl/decoder.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <climits>
#include <memory>

namespace otter::crypto {
namespace {

const unsigned char* bytes(std::span<const std::byte> data) {
    // Um span vazio pode trazer data() nulo, e o OpenSSL nao promete aceitar
    // ponteiro nulo nem com tamanho zero. O literal da' um endereco valido.
    static const unsigned char kEmpty = 0;
    return data.empty() ? &kEmpty
                        : reinterpret_cast<const unsigned char*>(data.data());
}

template <std::size_t N>
Result<std::array<std::byte, N>> hash_with(const EVP_MD* algorithm,
                                           std::span<const std::byte> data) {
    std::array<std::byte, N> digest{};
    unsigned int size = 0;

    if (EVP_MD_get_size(algorithm) != static_cast<int>(N) ||
        EVP_Digest(bytes(data), data.size(),
                   reinterpret_cast<unsigned char*>(digest.data()), &size,
                   algorithm, nullptr) != 1 ||
        size != N) {
        return fail(Errc::internal, "cálculo de hash falhou");
    }
    return digest;
}

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct PkeyCtxDeleter {
    void operator()(EVP_PKEY_CTX* ctx) const noexcept { EVP_PKEY_CTX_free(ctx); }
};
struct DecoderDeleter {
    void operator()(OSSL_DECODER_CTX* ctx) const noexcept {
        OSSL_DECODER_CTX_free(ctx);
    }
};
struct BioDeleter {
    void operator()(BIO* bio) const noexcept { BIO_free(bio); }
};

} // namespace

Result<Sha256Digest> sha256(std::span<const std::byte> data) {
    return hash_with<32>(EVP_sha256(), data);
}

Result<Md5Digest> md5(std::span<const std::byte> data) {
    return hash_with<16>(EVP_md5(), data);
}

Result<Sha1Digest> sha1(std::span<const std::byte> data) {
    return hash_with<20>(EVP_sha1(), data);
}

Result<Sha256Digest> hmac_sha256(std::span<const std::byte> key,
                                 std::span<const std::byte> data) {
    if (key.size() > static_cast<std::size_t>(INT_MAX)) {
        return fail(Errc::internal, "chave de HMAC grande demais");
    }

    Sha256Digest digest{};
    unsigned int size = 0;

    if (HMAC(EVP_sha256(), bytes(key), static_cast<int>(key.size()),
             bytes(data), data.size(),
             reinterpret_cast<unsigned char*>(digest.data()), &size) == nullptr ||
        size != digest.size()) {
        return fail(Errc::internal, "cálculo de hash falhou");
    }
    return digest;
}

Result<Sha256Digest> pbkdf2_sha256(std::string_view password,
                                   std::span<const std::byte> salt,
                                   std::uint32_t iterations) {
    if (password.size() > static_cast<std::size_t>(INT_MAX) ||
        salt.size() > static_cast<std::size_t>(INT_MAX) ||
        iterations > static_cast<std::uint32_t>(INT_MAX)) {
        return fail(Errc::internal, "parametros de PBKDF2 fora do limite");
    }

    Sha256Digest derived{};

    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          bytes(salt), static_cast<int>(salt.size()),
                          static_cast<int>(iterations), EVP_sha256(),
                          static_cast<int>(derived.size()),
                          reinterpret_cast<unsigned char*>(derived.data())) != 1) {
        return fail(Errc::internal, "PKCS5_PBKDF2_HMAC falhou");
    }
    return derived;
}

Result<Sha512Digest> sha512(std::span<const std::byte> data) {
    return hash_with<64>(EVP_sha512(), data);
}

Result<std::vector<std::byte>> pbkdf2_sha512(std::span<const std::byte> password,
                                             std::span<const std::byte> salt,
                                             std::uint32_t iterations,
                                             std::size_t length) {
    if (password.size() > static_cast<std::size_t>(INT_MAX) ||
        salt.size() > static_cast<std::size_t>(INT_MAX) ||
        iterations > static_cast<std::uint32_t>(INT_MAX) ||
        length > static_cast<std::size_t>(INT_MAX)) {
        return fail(Errc::internal, "parametros de PBKDF2 fora do limite");
    }

    std::vector<std::byte> derived(length);

    if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char*>(bytes(password)),
                          static_cast<int>(password.size()),
                          bytes(salt), static_cast<int>(salt.size()),
                          static_cast<int>(iterations), EVP_sha512(),
                          static_cast<int>(derived.size()),
                          reinterpret_cast<unsigned char*>(derived.data())) != 1) {
        return fail(Errc::internal, "PKCS5_PBKDF2_HMAC falhou");
    }
    return derived;
}

namespace {

struct CipherCtxDeleter {
    void operator()(EVP_CIPHER_CTX* ctx) const noexcept { EVP_CIPHER_CTX_free(ctx); }
};

Result<std::vector<std::byte>> aes_cbc_zero_iv(std::span<const std::byte> key,
                                               std::span<const std::byte> data,
                                               bool encrypt) {
    const EVP_CIPHER* cipher = nullptr;
    switch (key.size()) {
        case 16: cipher = EVP_aes_128_cbc(); break;
        case 24: cipher = EVP_aes_192_cbc(); break;
        case 32: cipher = EVP_aes_256_cbc(); break;
        default: return fail(Errc::invalid_argument, "chave AES de tamanho invalido");
    }
    if (data.size() % 16 != 0 || data.size() > static_cast<std::size_t>(INT_MAX)) {
        return fail(Errc::invalid_argument, "AES-CBC sem padding exige blocos de 16 bytes");
    }

    const std::unique_ptr<EVP_CIPHER_CTX, CipherCtxDeleter> ctx{EVP_CIPHER_CTX_new()};
    const unsigned char iv[16] = {};
    if (!ctx || EVP_CipherInit_ex(ctx.get(), cipher, nullptr, bytes(key), iv,
                                  encrypt ? 1 : 0) != 1 ||
        // Sem padding: os blocos saem como entraram.
        EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1) {
        return fail(Errc::internal, "EVP_CipherInit_ex(AES) falhou");
    }

    std::vector<std::byte> out(data.size() + 16);
    int written = 0;
    int tail = 0;
    if (EVP_CipherUpdate(ctx.get(), reinterpret_cast<unsigned char*>(out.data()),
                         &written, bytes(data), static_cast<int>(data.size())) != 1 ||
        EVP_CipherFinal_ex(ctx.get(),
                           reinterpret_cast<unsigned char*>(out.data()) + written,
                           &tail) != 1 ||
        static_cast<std::size_t>(written + tail) != data.size()) {
        return fail(Errc::internal, "AES-CBC falhou");
    }
    out.resize(data.size());
    return out;
}

} // namespace

Result<std::vector<std::byte>> aes_cbc_zero_iv_encrypt(std::span<const std::byte> key,
                                                       std::span<const std::byte> data) {
    return aes_cbc_zero_iv(key, data, /*encrypt=*/true);
}

Result<std::vector<std::byte>> aes_cbc_zero_iv_decrypt(std::span<const std::byte> key,
                                                       std::span<const std::byte> data) {
    return aes_cbc_zero_iv(key, data, /*encrypt=*/false);
}

Result<std::vector<std::byte>> random_bytes(std::size_t count) {
    if (count > static_cast<std::size_t>(INT_MAX)) {
        return fail(Errc::internal, "pedido de bytes aleatorios grande demais");
    }

    std::vector<std::byte> out(count);
    if (count == 0) return out;

    if (RAND_bytes(reinterpret_cast<unsigned char*>(out.data()),
                   static_cast<int>(count)) != 1) {
        return fail(Errc::internal, "RAND_bytes falhou");
    }
    return out;
}

Result<std::vector<std::byte>> rsa_encrypt_oaep(std::string_view public_key_pem,
                                                std::span<const std::byte> data) {
    if (public_key_pem.size() > static_cast<std::size_t>(INT_MAX)) {
        return fail(Errc::parse_error, "chave publica RSA nao esta' em PEM valido");
    }

    const std::unique_ptr<BIO, BioDeleter> bio{BIO_new_mem_buf(
        public_key_pem.data(), static_cast<int>(public_key_pem.size()))};
    if (!bio) return fail(Errc::internal, "BIO_new_mem_buf falhou");

    // O decodificador aceita as duas estruturas que o MySQL emite sem que se
    // diga qual: SubjectPublicKeyInfo ("BEGIN PUBLIC KEY") e RSAPublicKey cru
    // ("BEGIN RSA PUBLIC KEY"). PEM_read_bio_PUBKEY so' le a primeira, e
    // PEM_read_bio_RSAPublicKey, que le a segunda, e' obsoleta no 3.0.
    EVP_PKEY* raw_key = nullptr;
    const std::unique_ptr<OSSL_DECODER_CTX, DecoderDeleter> decoder{
        OSSL_DECODER_CTX_new_for_pkey(&raw_key, "PEM", nullptr, "RSA",
                                      EVP_PKEY_PUBLIC_KEY, nullptr, nullptr)};
    if (!decoder) return fail(Errc::internal, "OSSL_DECODER_CTX_new_for_pkey falhou");

    const int decoded = OSSL_DECODER_from_bio(decoder.get(), bio.get());
    const std::unique_ptr<EVP_PKEY, PkeyDeleter> key{raw_key};
    if (decoded != 1 || !key) {
        return fail(Errc::parse_error, "chave publica RSA nao esta' em PEM valido");
    }

    const std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDeleter> ctx{
        EVP_PKEY_CTX_new(key.get(), nullptr)};
    if (!ctx || EVP_PKEY_encrypt_init(ctx.get()) <= 0) {
        return fail(Errc::parse_error, "chave RSA com estrutura inesperada");
    }

    // OAEP com SHA-1 tambem no MGF1: e' o que o servidor MySQL espera (ver
    // crypto.hpp). O OpenSSL ja' usa SHA-1 por padrao; fica explicito para nao
    // depender de um padrao que a politica de seguranca da distro pode trocar.
    if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0 ||
        EVP_PKEY_CTX_set_rsa_oaep_md(ctx.get(), EVP_sha1()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_mgf1_md(ctx.get(), EVP_sha1()) <= 0) {
        return fail(Errc::internal, "configuracao do OAEP falhou");
    }

    std::size_t out_size = 0;
    if (EVP_PKEY_encrypt(ctx.get(), nullptr, &out_size, bytes(data),
                         data.size()) <= 0) {
        return fail(Errc::internal, "EVP_PKEY_encrypt (tamanho) falhou");
    }

    std::vector<std::byte> out(out_size);
    if (EVP_PKEY_encrypt(ctx.get(), reinterpret_cast<unsigned char*>(out.data()),
                         &out_size, bytes(data), data.size()) <= 0) {
        return fail(Errc::internal, "EVP_PKEY_encrypt falhou");
    }
    out.resize(out_size);
    return out;
}

} // namespace otter::crypto

#endif // !_WIN32
