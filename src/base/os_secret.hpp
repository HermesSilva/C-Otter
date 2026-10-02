// C-Otter -- base/os_secret.hpp
//
// LER um segredo do cofre do sistema operacional (no Windows, o Gerenciador de
// Credenciais).
//
// Existe por um motivo so': importar as senhas do pgAdmin na primeira
// execucao (ADR 0023). O pgAdmin cifra as senhas dos servidores com uma chave
// que ele guarda no cofre, pela biblioteca `keyring` do Python -- sem essa
// chave o que esta' no pgadmin4.db nao serve para nada.
//
// So' leitura, e so' do que pertence ao usuario logado: o cofre e' dele, e o
// sistema entrega a qualquer programa que rode na conta dele. O C-Otter NAO
// grava nada ali -- as senhas dele ficam em `.C-Otter/` (ADR 0020).
#pragma once

#include "base/error.hpp"

#include <string>
#include <string_view>

namespace otter {

// O segredo guardado pela biblioteca `keyring` do Python para (servico,
// usuario). `not_found` quando nao existe; `not_supported` fora do Windows.
[[nodiscard]] Result<std::string> read_keyring_secret(std::string_view service,
                                                      std::string_view user);

// Uma credencial GENERICA do Gerenciador de Credenciais do Windows, pelo alvo
// exato -- e' como o SSMS 20+ guarda a senha de cada servidor
// ("Microsoft:SSMS:20:<servidor>:<usuario>:<tipo>:<autenticacao>"). O segredo
// volta como texto UTF-8 (no cofre e' UTF-16). Mesmos erros de cima.
[[nodiscard]] Result<std::string> read_generic_credential(std::string_view target);

} // namespace otter
