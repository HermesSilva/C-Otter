// C-Otter -- lib/tdswire/connection.hpp
//
// Conexao com o SQL Server falando TDS 7.4 diretamente no socket -- sem ODBC,
// sem o driver JDBC e sem FreeTDS (LGPL, proibido pelo ADR 0002).
//
// Mapa do protocolo em docs/MSSQL-MAP.md; referencia: [MS-TDS].
//
//   PRELOGIN   versao e negociacao de criptografia
//   TLS        o aperto de mao viaja DENTRO de pacotes TDS; depois dele o TLS
//              corre cru sobre o socket (so' o pacote de login, ou tudo)
//   LOGIN7     usuario e senha, ou o token SSPI da conta do Windows
//   SQLBatch   o texto do comando em UTF-16LE
//   resposta   um fluxo de tokens: COLMETADATA, ROW, NBCROW, DONE, ERROR,
//              INFO, ENVCHANGE, LOGINACK, SSPI
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"
#include "net/socks.hpp"
#include "net/tls.hpp"
#include "tdswire/value.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::tdswire {

struct ConnectParams {
    std::string   host = "localhost";
    std::uint16_t port = 1433;
    std::string   database;          // vazio = o banco padrao do login
    std::string   user;
    std::string   password;

    // Autenticacao do Windows: a conta que roda o programa, por SSPI. Usuario
    // e senha sao ignorados.
    bool          integrated = false;

    std::string   application = "C-Otter";
    std::chrono::milliseconds timeout{10000};

    // Cifrar a conexao INTEIRA. Desligado, so' o pacote de login e' cifrado
    // (a senha nunca vai em claro) -- e' o "Encrypt=false" de todo cliente do
    // SQL Server. Com o servidor exigindo criptografia, tudo e' cifrado de
    // qualquer modo.
    bool          encrypt = false;
    // Validar o certificado do servidor (so' vale com `encrypt`).
    bool          verify_certificate = false;

    net::ProxyEndpoint proxy;
};

// Uma coluna do resultado (COLMETADATA).
struct Column {
    std::string name;
    TypeInfo    type;
    std::string type_name;        // "int", "nvarchar", ou o nome do UDT
    bool        nullable = true;
    bool        identity = false;
    bool        computed = false;
    bool        updatable = false;
    bool        hidden = false;   // coluna de chave acrescentada pelo modo browse
    bool        key = false;
};

struct Value {
    std::string_view text;
    bool             null = false;
};

// ERROR ou INFO do servidor.
struct Message {
    std::int32_t number = 0;
    std::uint8_t state = 0;
    std::uint8_t severity = 0;    // classe; > 10 e' erro
    std::string  text;
    std::string  procedure;
    std::int32_t line = 0;
};

class Connection {
public:
    // Chamado a cada conjunto de resultado (um lote pode devolver varios).
    using ColumnsCallback = std::function<void(const std::vector<Column>&)>;
    using RowCallback     = std::function<void(std::span<const Value>)>;

    Connection();
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    [[nodiscard]] static Result<Connection> connect(const ConnectParams& params);

    [[nodiscard]] bool is_open() const noexcept { return socket_.is_open(); }
    void close() noexcept;

    // Executa um lote. `on_columns` e `on_row` podem ser vazios.
    [[nodiscard]] Status query(std::string_view sql, const ColumnsCallback& on_columns,
                               const RowCallback& on_row);

    // Pede ao servidor que interrompa o lote em curso (pacote ATTENTION, na
    // MESMA conexao). Seguro de outra thread: e' justamente quando se usa.
    [[nodiscard]] Status cancel();

    [[nodiscard]] const std::string& server_version() const noexcept {
        return server_version_;
    }
    [[nodiscard]] const std::string& server_name() const noexcept { return server_name_; }
    [[nodiscard]] int version_major() const noexcept { return version_major_; }
    [[nodiscard]] const std::string& database() const noexcept { return database_; }

    // Ha' transacao aberta? O servidor avisa por ENVCHANGE a cada BEGIN,
    // COMMIT e ROLLBACK -- nao e' palpite do cliente.
    [[nodiscard]] bool in_transaction() const noexcept { return transaction_ != 0; }

    // Linhas afetadas pelo ultimo lote (soma dos DONE com contagem); -1 = nenhuma.
    [[nodiscard]] std::int64_t last_affected_rows() const noexcept { return affected_rows_; }

    // As mensagens INFO (PRINT, avisos) desde a ultima chamada.
    [[nodiscard]] std::vector<Message> take_messages();

    [[nodiscard]] bool tls_active() const noexcept { return tls_full_; }
    [[nodiscard]] bool login_encrypted() const noexcept { return login_encrypted_; }
    [[nodiscard]] const net::TlsInfo& tls_info() const noexcept { return tls_.info(); }

private:
    [[nodiscard]] Status send_message(std::uint8_t type, std::span<const std::byte> payload);
    [[nodiscard]] Status write_raw(std::span<const std::byte> data);
    [[nodiscard]] Status read_raw(std::span<std::byte> buffer);
    [[nodiscard]] Status read_packet();

    [[nodiscard]] Status prelogin(const ConnectParams& params);
    [[nodiscard]] Status start_tls(const ConnectParams& params);
    [[nodiscard]] Status login(const ConnectParams& params);
    [[nodiscard]] Status read_response(const ColumnsCallback& on_columns,
                                       const RowCallback& on_row, bool login_phase);

    // --- leitura do fluxo de tokens ---
    //
    // `take` garante `size` bytes contiguos; o ponteiro vale ate' a proxima
    // chamada. Falso = a conexao caiu (o erro fica em `stream_error_`).
    [[nodiscard]] bool take(std::size_t size, const std::byte*& out);
    [[nodiscard]] bool at_message_end() const noexcept;
    bool read_u8(std::uint8_t& value);
    bool read_u16(std::uint16_t& value);
    bool read_u32(std::uint32_t& value);
    bool read_u64(std::uint64_t& value);
    bool read_b_varchar(std::string& value);    // 1 byte de tamanho, em caracteres
    bool read_us_varchar(std::string& value);   // 2 bytes de tamanho
    bool skip(std::size_t size);

    bool read_type_info(TypeInfo& type, std::string& udt_name);
    bool read_columns(std::vector<Column>& columns);
    bool read_value(const TypeInfo& type, std::string& text, bool& null);
    bool read_plp(std::string& bytes, bool& null);
    bool read_message(Message& message);

    net::Socket     socket_;
    net::TlsChannel tls_;
    bool            tls_on_ = false;        // os bytes passam pelo TLS agora
    bool            tls_full_ = false;      // a conexao inteira e' cifrada
    bool            login_encrypted_ = false;
    std::uint8_t    server_encryption_ = 0x02;

    // `cancel()` escreve de outra thread: um pacote ATTENTION no meio de um
    // lote em envio corromperia o fluxo. Em ponteiros porque a Connection e'
    // movida para dentro do Holt, e mutex e atomico nao se movem.
    std::unique_ptr<std::mutex>        send_mutex_;
    std::unique_ptr<std::atomic<bool>> attention_pending_;

    std::vector<std::byte> inbox_;          // corpo dos pacotes da mensagem atual
    std::size_t            in_pos_ = 0;
    bool                   message_complete_ = true;   // o ultimo pacote tinha EOM
    Status                 stream_error_;

    std::uint8_t  packet_id_   = 1;
    std::uint32_t packet_size_ = 4096;
    std::uint64_t transaction_ = 0;         // descritor; 0 = fora de transacao
    std::int64_t  affected_rows_ = -1;

    std::string server_version_;
    std::string server_name_;
    int         version_major_ = 0;
    std::string database_;

    std::vector<Message> messages_;

    // Estado do login: o ultimo desafio SSPI do servidor, e se o LOGINACK veio.
    std::vector<std::byte> sspi_token_;
    bool                   logged_in_ = false;
};

// --- Montagem de pacotes, publica para os testes ----------------------------------

// O corpo do PRELOGIN: VERSION, ENCRYPTION, INSTOPT, THREADID, MARS.
[[nodiscard]] std::vector<std::byte> build_prelogin(std::uint8_t encryption);

// O byte ENCRYPTION da resposta do servidor ao PRELOGIN; 0xFF se nao veio.
[[nodiscard]] std::uint8_t parse_prelogin_encryption(std::span<const std::byte> body);

// O corpo do LOGIN7. `sspi` vazio = login por usuario e senha.
[[nodiscard]] std::vector<std::byte> build_login7(const ConnectParams& params,
                                                  std::string_view client_host,
                                                  std::span<const std::byte> sspi);

// O corpo de um SQLBatch: ALL_HEADERS (descritor da transacao) + texto UTF-16LE.
[[nodiscard]] std::vector<std::byte> build_batch(std::string_view sql,
                                                 std::uint64_t transaction);

} // namespace otter::tdswire
