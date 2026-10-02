// C-Otter -- lib/net/sspi.hpp
//
// Autenticacao integrada do Windows (SSPI, pacote Negotiate: Kerberos com
// queda para NTLM) -- a "Windows Authentication" do SQL Server.
//
// O protocolo de banco so' transporta os tokens; quem os produz e' o sistema.
// A credencial e' a da conta que esta' rodando o programa: nenhuma senha
// passa por aqui.
//
// So' existe no Windows. Nas outras plataformas `available()` e' falso e a
// tela nao oferece a opcao (diretiva 6).
#pragma once

#include "base/error.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::net {

class SspiClient {
public:
    SspiClient();
    ~SspiClient();

    SspiClient(const SspiClient&)            = delete;
    SspiClient& operator=(const SspiClient&) = delete;
    SspiClient(SspiClient&&) noexcept;
    SspiClient& operator=(SspiClient&&) noexcept;

    [[nodiscard]] static bool available() noexcept;

    // O proximo token a enviar. `input` vazio na primeira chamada; depois, o
    // token que o servidor devolveu. `target` e' o SPN ("MSSQLSvc/host:1433").
    [[nodiscard]] Result<std::vector<std::byte>> step(std::string_view target,
                                                      std::span<const std::byte> input);

    // O sistema considera a negociacao terminada (o servidor ainda decide).
    [[nodiscard]] bool complete() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// O nome DNS completo do host, para montar o SPN. Devolve o proprio `host`
// quando nao consegue resolver.
[[nodiscard]] std::string fully_qualified_host(std::string_view host);

} // namespace otter::net
