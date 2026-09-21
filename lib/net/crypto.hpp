// C-Otter -- lib/net/crypto.hpp
//
// Primitivas criptograficas para autenticacao de protocolo.
//
// Usamos a API do sistema operacional (CNG no Windows, OpenSSL no Linux) --
// nunca implementacao propria de algoritmo criptografico. Escrever MD5 ou
// SHA-256 a mao e' como bugs sutis de seguranca nascem.
#pragma once

#include "base/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::crypto {

using Sha256Digest = std::array<std::byte, 32>;
using Sha1Digest   = std::array<std::byte, 20>;
using Md5Digest    = std::array<std::byte, 16>;

[[nodiscard]] Result<Sha256Digest> sha256(std::span<const std::byte> data);
[[nodiscard]] Result<Md5Digest>    md5(std::span<const std::byte> data);

// SHA-1 existe SO' para o mysql_native_password, que e' definido em termos
// dele. Nao usar em nada novo: SHA-1 tem colisao pratica desde 2017.
[[nodiscard]] Result<Sha1Digest>   sha1(std::span<const std::byte> data);

[[nodiscard]] Result<Sha256Digest> hmac_sha256(std::span<const std::byte> key,
                                               std::span<const std::byte> data);

// PBKDF2-HMAC-SHA256, exigido pelo SCRAM.
[[nodiscard]] Result<Sha256Digest> pbkdf2_sha256(std::string_view password,
                                                 std::span<const std::byte> salt,
                                                 std::uint32_t iterations);

// Bytes aleatorios de qualidade criptografica, para o nonce do SCRAM.
[[nodiscard]] Result<std::vector<std::byte>> random_bytes(std::size_t count);

[[nodiscard]] std::string base64_encode(std::span<const std::byte> data);
[[nodiscard]] Result<std::vector<std::byte>> base64_decode(std::string_view text);

[[nodiscard]] std::string hex_encode(std::span<const std::byte> data);

// Cifra com uma chave publica RSA em PEM, usando OAEP-SHA1.
//
// Existe para o caching_sha2_password do MySQL 8: no "full auth" o servidor
// manda sua chave publica e espera a senha cifrada com ela. O padding e' OAEP
// com SHA-1 porque e' o que o servidor exige -- nao e' escolha nossa, e nao
// afeta a forca do RSA (o SHA-1 aqui e' usado como MGF, nao como assinatura,
// onde as colisoes importariam).
//
// `data` nao pode passar de (tamanho_da_chave - 42) bytes, limite do OAEP.
[[nodiscard]] Result<std::vector<std::byte>> rsa_encrypt_oaep(
    std::string_view public_key_pem, std::span<const std::byte> data);

} // namespace otter::crypto
