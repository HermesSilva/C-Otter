// C-Otter -- base/aes.hpp
//
// AES-128 em modo CBC com padding PKCS#7.
//
// Existe por um motivo especifico: ler e escrever o `credentials-config.json`
// do DBeaver, que usa exatamente AES/CBC/PKCS5Padding com chave de 128 bits
// (ADR 0012). PKCS#5 e PKCS#7 sao o mesmo esquema para blocos de 16 bytes.
//
// NAO use isto para proteger segredo de verdade. A chave do DBeaver e' uma
// constante publica no codigo-fonte dele; a cifragem serve contra leitura
// casual do arquivo, nada alem. O ADR 0012 explica por que aceitamos isso e o
// que a interface diz ao usuario.
#pragma once

#include "base/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace otter::crypto {

inline constexpr std::size_t kAesBlockSize = 16;
inline constexpr std::size_t kAes128KeySize = 16;

using AesKey = std::array<std::uint8_t, kAes128KeySize>;
using AesIv  = std::array<std::uint8_t, kAesBlockSize>;

// Cifra com PKCS#7 e devolve IV || texto cifrado, que e' o layout do arquivo
// do DBeaver: os 16 primeiros bytes sao o IV.
[[nodiscard]] std::vector<std::uint8_t> aes128_cbc_encrypt(
    std::span<const std::uint8_t> plaintext, const AesKey& key, const AesIv& iv);

// Decifra um buffer no formato IV || texto cifrado.
//
// Falha quando o buffer e' curto demais, nao e' multiplo do bloco, ou o
// padding e' invalido -- este ultimo caso normalmente significa chave errada.
[[nodiscard]] Result<std::vector<std::uint8_t>> aes128_cbc_decrypt(
    std::span<const std::uint8_t> input, const AesKey& key);

// IV aleatorio do gerador do sistema. Reutilizar IV em CBC com a mesma chave
// vaza se duas mensagens comecam igual.
[[nodiscard]] Result<AesIv> random_iv();

} // namespace otter::crypto
