// C-Otter -- lib/net/socket.hpp
//
// Socket TCP com timeout, sobre Winsock ou BSD sockets. Camada minima: os
// protocolos wire (lib/pgwire, lib/mywire) so' precisam de conectar, ler,
// escrever e fechar.
//
// lib/ nao conhece src/: sao bibliotecas independentes (ADR 0009).
#pragma once

#include "base/error.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace otter::net {

class Socket {
public:
    Socket() = default;
    ~Socket();

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] static Result<Socket> connect(
        std::string_view host,
        std::uint16_t port,
        std::chrono::milliseconds timeout = std::chrono::seconds(10));

    // Escreve tudo ou falha -- send() parcial e' tratado internamente.
    [[nodiscard]] Status write_all(std::span<const std::byte> data);

    // Le exatamente `buffer.size()` bytes. Fim de conexao antes disso e' erro:
    // uma mensagem de protocolo truncada nao tem interpretacao valida.
    [[nodiscard]] Status read_exact(std::span<std::byte> buffer);

    // Le o que estiver disponivel; devolve quantos bytes leu (0 = fim).
    [[nodiscard]] Result<std::size_t> read_some(std::span<std::byte> buffer);

    [[nodiscard]] bool is_open() const noexcept;
    void close() noexcept;

    void set_read_timeout(std::chrono::milliseconds timeout);
    void set_write_timeout(std::chrono::milliseconds timeout);

    // Desabilita o algoritmo de Nagle. Protocolo de banco e' request/response:
    // agrupar pacotes so' adiciona latencia.
    void set_no_delay(bool enabled);

    // Handle nativo, para a camada de TLS.
    [[nodiscard]] std::uintptr_t native_handle() const noexcept { return handle_; }

private:
    explicit Socket(std::uintptr_t handle) : handle_(handle) {}

    std::uintptr_t handle_ = kInvalid;

    static constexpr std::uintptr_t kInvalid = static_cast<std::uintptr_t>(-1);
};

// Uma porta TCP livre em 127.0.0.1, escolhida pelo sistema. Para o tunel
// SSH, que precisa dizer ao `ssh` em que porta local escutar.
//
// Ha' uma janela entre devolver a porta e alguem usa-la em que outro
// processo pode pega-la; o `ssh` falha nesse caso (ExitOnForwardFailure), e o
// erro chega ao usuario -- raro o bastante para nao valer um protocolo de
// reserva.
[[nodiscard]] Result<std::uint16_t> free_local_port();

// Um datagrama UDP de ida e a resposta (ate' 64 KiB). Para protocolos de
// pergunta e resposta unicas -- o SQL Server Browser (UDP 1434), que diz em
// que porta TCP uma instancia nomeada escuta. Sem resposta dentro de
// `timeout`, `timeout`: UDP nao avisa quando ninguem esta' ouvindo.
[[nodiscard]] Result<std::vector<std::byte>> udp_exchange(
    std::string_view host, std::uint16_t port, std::span<const std::byte> request,
    std::chrono::milliseconds timeout);

// Inicializacao da pilha de rede (Winsock exige; POSIX nao faz nada).
// Idempotente e thread-safe.
Status initialize_network();

} // namespace otter::net
