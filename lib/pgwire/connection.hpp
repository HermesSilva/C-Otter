// C-Otter -- lib/pgwire/connection.hpp
//
// Conexao PostgreSQL falando o protocolo v3 diretamente no socket, sem libpq
// (ADR 0009).
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"
#include "net/tls.hpp"
#include "pgwire/message.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace otter::pgwire {

struct ConnectParams {
    std::string   host = "localhost";
    std::uint16_t port = 5432;
    std::string   database;
    std::string   user;
    std::string   password;
    std::string   application_name = "C-Otter";
    std::chrono::milliseconds timeout{10000};

    // TLS. `require` faz a conexao FALHAR quando o servidor recusa -- o
    // contrario daria ao usuario a impressao de estar protegido sem estar.
    bool          use_tls = false;
    bool          require_tls = false;
    bool          allow_invalid_certificate = false;
};

// Descricao de uma coluna, vinda de RowDescription.
struct FieldDescription {
    std::string   name;
    std::uint32_t table_oid = 0;
    std::int16_t  column_id = 0;
    std::uint32_t type_oid  = 0;
    std::int16_t  type_size = 0;
    std::int32_t  type_modifier = 0;
    std::int16_t  format = 0;          // 0 = texto, 1 = binario
};

// Uma linha crua. Valor ausente (nulo) e' representado por span vazio com
// `null` verdadeiro -- distinguir nulo de string vazia importa.
struct RawValue {
    std::span<const std::byte> data;
    bool                       null = false;
};

class Connection {
public:
    // Chamado por linha recebida, permitindo consumo em fluxo sem materializar
    // o resultado inteiro.
    using RowCallback = std::function<void(std::span<const RawValue>)>;

    Connection() = default;
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    [[nodiscard]] static Result<Connection> connect(const ConnectParams& params);

    [[nodiscard]] bool is_open() const noexcept { return socket_.is_open(); }
    void close() noexcept;

    // Query simples (protocolo 'Q'). Chama `on_row` por linha.
    [[nodiscard]] Status query(std::string_view sql,
                               const RowCallback& on_row,
                               std::vector<FieldDescription>* fields = nullptr);

    // Cancela a query em andamento. Abre uma segunda conexao, como o protocolo
    // exige -- a conexao original esta' ocupada esperando resposta.
    [[nodiscard]] Status cancel_current_query() const;

    [[nodiscard]] const std::map<std::string, std::string>& parameters() const noexcept {
        return parameters_;
    }
    [[nodiscard]] std::string server_version() const;
    [[nodiscard]] TransactionStatus transaction_status() const noexcept {
        return transaction_status_;
    }
    [[nodiscard]] std::int64_t last_affected_rows() const noexcept {
        return affected_rows_;
    }

    // A conexao esta' cifrada? A barra de status precisa dizer.
    [[nodiscard]] bool tls_active() const noexcept { return tls_active_; }
    [[nodiscard]] const net::TlsInfo& tls_info() const noexcept {
        return tls_.info();
    }

private:
    struct Incoming {
        char                   type = 0;
        std::vector<std::byte> body;
    };

    [[nodiscard]] Status send(std::span<const std::byte> data);
    [[nodiscard]] Status write_raw(std::span<const std::byte> data);
    [[nodiscard]] Status read_raw(std::span<std::byte> buffer);
    [[nodiscard]] Status start_tls(const ConnectParams& params);
    [[nodiscard]] Result<Incoming> receive();
    [[nodiscard]] Status authenticate(const ConnectParams& params);
    [[nodiscard]] Status handle_sasl(const ConnectParams& params,
                                     std::span<const std::byte> body);
    [[nodiscard]] Status handle_md5(const ConnectParams& params,
                                    std::span<const std::byte> body);

    net::Socket     socket_;

    // Com TLS ativo todo trafego passa pelo canal; o socket segue dono da
    // conexao e so' transporta os bytes cifrados.
    net::TlsChannel tls_;
    bool            tls_active_ = false;
    std::map<std::string, std::string> parameters_;
    TransactionStatus transaction_status_ = TransactionStatus::idle;

    std::int32_t backend_pid_    = 0;
    std::int32_t backend_secret_ = 0;
    std::int64_t affected_rows_  = -1;

    // Guardados para abrir a conexao de cancelamento.
    std::string   host_;
    std::uint16_t port_ = 0;
};

} // namespace otter::pgwire
