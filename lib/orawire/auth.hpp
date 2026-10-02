// C-Otter -- lib/orawire/auth.hpp
//
// O5LOGON: o logon por usuario e senha do Oracle, em duas idas ao servidor.
//
//   fase um   o cliente diz quem e'; o servidor devolve um desafio: o sal do
//             verificador da senha e a metade DELE da chave de sessao, cifrada
//             com uma chave derivada da senha
//   fase dois o cliente devolve a metade dele da chave, cifrada do mesmo
//             jeito, e a senha cifrada com a chave combinada
//
// A senha nunca viaja em claro, e quem nao a conhece nao decifra a chave do
// servidor. As contas sao funcoes puras, com os aleatorios por parametro: o
// teste confere o resultado contra uma implementacao independente sem
// precisar de um servidor (ADR 0027).
#pragma once

#include "base/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::orawire {

// AUTH_VFR_DATA diz que verificador a conta tem no servidor.
inline constexpr std::uint32_t kVerifier11g1 = 0xb152;
inline constexpr std::uint32_t kVerifier11g2 = 0x1b25;
inline constexpr std::uint32_t kVerifier12c  = 0x4815;

// O que o servidor manda na fase um. Os textos chegam em hexadecimal.
struct AuthChallenge {
    std::uint32_t verifier_type = 0;
    std::string   verifier_data;       // AUTH_VFR_DATA
    std::string   server_key;          // AUTH_SESSKEY
    std::string   combo_salt;          // AUTH_PBKDF2_CSK_SALT   (12c)
    std::uint32_t verifier_rounds = 0; // AUTH_PBKDF2_VGEN_COUNT (12c)
    std::uint32_t combo_rounds = 0;    // AUTH_PBKDF2_SDER_COUNT (12c)
};

// Os aleatorios do cliente.
struct AuthNonces {
    std::vector<std::byte> client_key;      // do tamanho da chave do servidor
    std::vector<std::byte> password_salt;   // 16 bytes
    std::vector<std::byte> speedy_salt;     // 16 bytes (12c)
};

// O que o cliente manda na fase dois, ja' em hexadecimal maiusculo.
struct AuthAnswer {
    std::string session_key;   // AUTH_SESSKEY
    std::string speedy_key;    // AUTH_PBKDF2_SPEEDY_KEY; vazio no 11g
    std::string password;      // AUTH_PASSWORD

    // A chave combinada: confere a resposta do servidor (AUTH_SVR_RESPONSE).
    std::vector<std::byte> combo_key;
};

[[nodiscard]] Result<AuthNonces> make_nonces(const AuthChallenge& challenge);

[[nodiscard]] Result<AuthAnswer> answer_challenge(const AuthChallenge& challenge,
                                                  std::string_view password,
                                                  const AuthNonces& nonces);

// O servidor prova que tambem conhece a senha: AUTH_SVR_RESPONSE, decifrado
// com a chave combinada, traz "SERVER_TO_CLIENT". Sem isto um servidor falso
// aceitaria qualquer senha e o cliente nao perceberia.
[[nodiscard]] bool server_proof_is_valid(std::span<const std::byte> combo_key,
                                         std::string_view response_hex);

// Hexadecimal maiusculo, como o Oracle escreve, e a volta.
[[nodiscard]] std::string hex_upper(std::span<const std::byte> data);
[[nodiscard]] Result<std::vector<std::byte>> hex_decode(std::string_view text);

} // namespace otter::orawire
