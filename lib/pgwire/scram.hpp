// C-Otter -- lib/pgwire/scram.hpp
//
// SCRAM-SHA-256 (RFC 5802 / RFC 7677), o metodo de autenticacao padrao do
// PostgreSQL desde a versao 10 e obrigatorio em instalacoes modernas.
//
// A senha NUNCA trafega: o cliente prova que a conhece e verifica que o
// servidor tambem a conhece -- autenticacao mutua.
#pragma once

#include "base/error.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace otter::pgwire {

class ScramClient {
public:
    // Inicia a troca. `password` fica na memoria apenas durante o handshake.
    [[nodiscard]] static Result<ScramClient> begin(std::string_view password);

    // Primeira mensagem: "n,,n=,r=<nonce>"
    [[nodiscard]] const std::string& client_first() const noexcept {
        return client_first_;
    }

    // Processa "r=<nonce>,s=<salt>,i=<iteracoes>" e produz
    // "c=biws,r=<nonce>,p=<prova>".
    [[nodiscard]] Result<std::string> handle_server_first(std::string_view message);

    // Verifica "v=<assinatura>" -- prova que o servidor conhece a senha.
    // Ignorar esta etapa abre caminho para servidor falso.
    [[nodiscard]] Status handle_server_final(std::string_view message);

private:
    ScramClient() = default;

    std::string            password_;
    std::string            client_nonce_;
    std::string            client_first_;
    std::string            client_first_bare_;
    std::vector<std::byte> server_signature_;
};

} // namespace otter::pgwire
