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

// --- AES em modo CFB8, chave de 128, 192 ou 256 bits ------------------------------
//
// O segundo formato de terceiros que o projeto le': as senhas que o pgAdmin 4
// guarda no pgadmin4.db (`pgadmin/utils/crypto.py`: AES + CFB8, IV de 16 bytes
// na frente). Ver db/connection_import.hpp e o ADR 0023.
//
// CFB8 e' um modo de FLUXO: um byte por vez, sem padding -- o texto cifrado
// tem o tamanho do texto. Por isso chave errada NAO da' erro: da' lixo. Quem
// chama precisa conferir o resultado.

// Decifra IV || texto cifrado. Falha so' quando a chave nao tem 16, 24 ou 32
// bytes, ou o buffer nem tem o IV.
[[nodiscard]] Result<std::vector<std::uint8_t>> aes_cfb8_decrypt(
    std::span<const std::uint8_t> input, std::span<const std::uint8_t> key);

// Cifra e devolve IV || texto cifrado. Existe para os testes e para os
// vetores do NIST: o programa nao grava nada neste formato.
[[nodiscard]] Result<std::vector<std::uint8_t>> aes_cfb8_encrypt(
    std::span<const std::uint8_t> plaintext, std::span<const std::uint8_t> key,
    const AesIv& iv);

// IV aleatorio do gerador do sistema. Reutilizar IV em CBC com a mesma chave
// vaza se duas mensagens comecam igual.
[[nodiscard]] Result<AesIv> random_iv();

} // namespace otter::crypto
