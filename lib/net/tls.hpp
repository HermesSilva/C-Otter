// C-Otter -- lib/net/tls.hpp
//
// TLS sobre um socket já conectado (ADR 0009: Schannel no Windows, OpenSSL no
// Linux -- API do sistema, sem dependência nova).
//
// Por que envolver um socket já aberto, em vez de conectar por dentro: os
// dois protocolos de banco NEGOCIAM o TLS dentro da própria conversa. O
// PostgreSQL manda um `SSLRequest` e lê um byte de resposta ANTES do aperto
// de mão TLS; o MySQL lê o `Handshake` do servidor em claro, responde com a
// flag `CLIENT_SSL`, e só então inicia o TLS. Em nenhum dos dois o TLS começa
// no primeiro byte da conexão -- um `TlsSocket::connect(host, port)` não
// serviria para nenhum deles.
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"

#include <functional>
#include <memory>
#include <span>
#include <string>

namespace otter::net {

struct TlsOptions {
    // Nome esperado no certificado. Vem do host da conexão, e é o que impede
    // que um servidor com certificado válido de OUTRO domínio seja aceito.
    std::string host;

    // Aceitar certificado que a cadeia do sistema não valida.
    //
    // Existe porque servidor de desenvolvimento quase sempre usa certificado
    // autoassinado, e recusá-lo tornaria o TLS inútil onde ele mais aparece.
    // Mas é uma escolha do USUÁRIO, por conexão -- nunca o padrão, e a UI
    // precisa dizer o que ele está abrindo mão.
    bool allow_invalid_certificate = false;

    // Aceitar certificado cujo nome não casa com o host. Separado do anterior
    // porque são falhas diferentes: um certificado expirado é um problema de
    // manutenção; um certificado de outro domínio pode ser um ataque.
    bool allow_host_mismatch = false;

    // Por onde os bytes do APERTO DE MÃO passam, quando não é direto no
    // socket. O SQL Server embrulha cada registro do aperto de mão num pacote
    // TDS (PRELOGIN); só depois dele o TLS corre cru sobre o socket. Vazios =
    // direto no socket, como no PostgreSQL e no MySQL.
    std::function<Status(std::span<const std::byte>)>          handshake_write;
    std::function<Result<std::size_t>(std::span<std::byte>)>   handshake_read;
};

// Resultado da verificação, para a UI mostrar o que aceitou.
struct TlsInfo {
    std::string protocol;      // "TLS 1.3"
    std::string cipher;        // "AES_256_GCM"
    std::string subject;       // CN do certificado do servidor
    std::string issuer;

    bool certificate_trusted = true;
    bool host_matches = true;
};

// Canal TLS sobre um socket. O socket continua vivo e pertence a quem o
// passou -- este objeto só cifra o que passa por ele.
class TlsChannel {
public:
    TlsChannel();
    ~TlsChannel();

    TlsChannel(const TlsChannel&)            = delete;
    TlsChannel& operator=(const TlsChannel&) = delete;
    TlsChannel(TlsChannel&&) noexcept;
    TlsChannel& operator=(TlsChannel&&) noexcept;

    // Faz o aperto de mão sobre o socket. O socket precisa estar conectado e
    // no ponto em que o protocolo de banco já negociou o upgrade.
    [[nodiscard]] Status handshake(Socket& socket, const TlsOptions& options);

    // O socket vai em CADA chamada, e nao guardado no canal.
    //
    // Guardar um `Socket*` parecia simples e estava errado: a `Connection` que
    // contem os dois e' MOVIDA para dentro do Holt depois de conectar, o
    // socket muda de endereco, e o ponteiro no canal passa a apontar para o
    // objeto esvaziado pelo move. O sintoma foi "An operation was attempted on
    // something that is not a socket" na primeira consulta -- depois de um
    // aperto de mao bem-sucedido.
    [[nodiscard]] Status write_all(Socket& socket,
                                   std::span<const std::byte> data);
    [[nodiscard]] Status read_exact(Socket& socket, std::span<std::byte> buffer);

    [[nodiscard]] bool is_open() const noexcept;
    void close() noexcept;

    [[nodiscard]] const TlsInfo& info() const noexcept { return info_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TlsInfo               info_;
};

// O TLS está disponível nesta compilação?
//
// Existe para a UI: sem isso, a caixa "usar TLS" apareceria num binário que
// não o suporta -- e falharia na conexão em vez de na hora de marcar.
[[nodiscard]] bool tls_available() noexcept;

} // namespace otter::net
