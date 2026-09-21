// C-Otter -- lib/mywire/auth.hpp
//
// Plugins de autenticacao do MySQL.
//
// Os dois que importam sao definidos como XOR entre dois hashes -- desenho
// que evita mandar a senha, mas NAO evita que quem capturou o trafego e o
// hash armazenado se autentique. Por isso o MySQL 8 empurra o
// caching_sha2_password, que exige TLS ou RSA na primeira autenticacao.
//
// Referencia: https://dev.mysql.com/doc/dev/mysql-server/latest/page_protocol_connection_phase_authentication_methods.html
#pragma once

#include "base/error.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace otter::mywire {

inline constexpr std::string_view kNativePassword = "mysql_native_password";
inline constexpr std::string_view kCachingSha2    = "caching_sha2_password";
inline constexpr std::string_view kSha256Password = "sha256_password";
inline constexpr std::string_view kClearPassword  = "mysql_clear_password";

// mysql_native_password:
//   SHA1(senha) XOR SHA1( desafio ‖ SHA1(SHA1(senha)) )
//
// Senha vazia manda resposta VAZIA, nao o hash de "". E' o que o servidor
// espera para contas sem senha; mandar 20 bytes ali seria recusado.
[[nodiscard]] Result<std::vector<std::byte>> native_password_response(
    std::string_view password, std::span<const std::byte> challenge);

// caching_sha2_password, caminho rapido:
//   SHA256(senha) XOR SHA256( SHA256(SHA256(senha)) ‖ desafio )
//
// Repare na ordem: aqui o desafio vem DEPOIS do hash duplo; no native ele vem
// ANTES. Trocar os dois produz 32 bytes plausiveis que o servidor recusa, e o
// erro resultante diz apenas "access denied" -- daí o comentario.
[[nodiscard]] Result<std::vector<std::byte>> caching_sha2_response(
    std::string_view password, std::span<const std::byte> challenge);

// Respostas do servidor no meio do caching_sha2_password, apos a resposta
// rapida. Chegam como um pacote 0x01 seguido de um destes bytes.
enum class Sha2Stage : std::uint8_t {
    fast_auth_ok = 0x03,   // o hash estava em cache: acabou
    full_auth    = 0x04,   // exige senha em claro sobre canal seguro, ou RSA
};

} // namespace otter::mywire
