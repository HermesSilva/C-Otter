// C-Otter -- lib/orawire/connection.hpp
//
// Conexao com o Oracle falando TNS/TTC diretamente no socket -- sem o Instant
// Client (proprietario, so' dinamico) e sem ODPI-C, que o carrega (ADR 0027).
//
// A Oracle nao publica a especificacao do protocolo. A referencia e' o driver
// "thin" dela, de codigo aberto (python-oracledb, UPL/Apache 2.0), conferido
// contra um servidor real. Mapa em docs/ORACLE-MAP.md.
//
//   CONNECT    descritor de conexao em texto; o listener responde ACCEPT,
//              REFUSE (com o ORA-12xxx) ou RESEND
//   PROTOCOL   versao do TTC, conjunto de caracteres e capacidades do servidor
//   DATA TYPES as capacidades do cliente e a tabela de tipos que ele entende
//   AUTH       O5LOGON em duas idas (orawire/auth.hpp)
//   EXECUTE    parse + execucao + primeiras linhas numa ida so'; FETCH busca
//              as seguintes
//
// O que esta versao NAO faz, e diz ao ser pedido: TCPS (TLS), criptografia
// nativa de rede, redirecionamento do listener, variaveis de ligacao,
// conteudo de JSON/VECTOR/objeto (a coluna aparece, o valor e' um marcador).
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"
#include "net/socks.hpp"
#include "orawire/value.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::orawire {

struct ConnectParams {
    std::string   host = "localhost";
    std::uint16_t port = 1521;

    // O banco se nomeia por SERVICE_NAME (o caminho atual) ou por SID (o
    // antigo). `sid` so' vale com `service_name` vazio.
    std::string   service_name;
    std::string   sid;

    std::string   user;
    std::string   password;

    // Como a sessao aparece em V$SESSION.PROGRAM.
    std::string   program = "C-Otter";
    std::chrono::milliseconds timeout{10000};

    net::ProxyEndpoint proxy;
};

// Uma coluna do resultado.
struct Column {
    std::string   name;
    OraType       type = OraType::varchar;
    std::uint8_t  charset_form = 0;
    std::int8_t   precision = 0;
    std::int8_t   scale = 0;
    std::uint32_t buffer_size = 0;   // 0 = a coluna e' nula por descricao
    std::uint32_t max_size = 0;
    bool          nullable = true;
};

struct Value {
    std::string_view text;
    bool             null = false;
};

class Connection {
public:
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

    // Executa UMA instrucao (o Oracle nao aceita lote, nem ';' no fim).
    // `on_columns` e `on_row` podem ser vazios.
    [[nodiscard]] Status query(std::string_view sql, const ColumnsCallback& on_columns,
                               const RowCallback& on_row);

    [[nodiscard]] Status commit();
    [[nodiscard]] Status rollback();
    [[nodiscard]] Status ping();

    // Pede ao servidor que interrompa a instrucao em curso (marcador de
    // interrupcao, na MESMA conexao). Seguro de outra thread.
    [[nodiscard]] Status cancel();

    // Com auto-commit, cada instrucao leva o pedido de commit junto: nao ha'
    // ida extra ao servidor.
    void set_auto_commit(bool enabled) noexcept { auto_commit_ = enabled; }
    [[nodiscard]] bool auto_commit() const noexcept { return auto_commit_; }

    // Ha' transacao aberta? Vem do servidor, no fim de cada chamada.
    [[nodiscard]] bool in_transaction() const noexcept { return in_transaction_; }

    // Linhas afetadas pela ultima instrucao; -1 = nenhuma contagem.
    [[nodiscard]] std::int64_t last_affected_rows() const noexcept { return affected_rows_; }

    // "23.26.0.0.0"
    [[nodiscard]] const std::string& server_version() const noexcept { return server_version_; }
    [[nodiscard]] int version_major() const noexcept { return version_major_; }
    // A faixa que o servidor anuncia: "Oracle AI Database 26ai Free ...".
    [[nodiscard]] const std::string& server_banner() const noexcept { return server_banner_; }

    // O schema corrente, quando o servidor o informou (ALTER SESSION SET
    // CURRENT_SCHEMA avisa o cliente). Vazio = o do usuario.
    [[nodiscard]] const std::string& current_schema() const noexcept { return current_schema_; }

    // Um dado da sessao dito pelo servidor no logon: "AUTH_SESSION_ID",
    // "AUTH_SC_SERVICE_NAME", "AUTH_SC_DBUNIQUE_NAME"... Vazio se nao veio.
    [[nodiscard]] std::string session_value(std::string_view key) const;

private:
    struct Call;

    // --- transporte ---
    [[nodiscard]] Status send_packet(std::uint8_t type, std::span<const std::byte> body);
    [[nodiscard]] Status send_data(std::span<const std::byte> payload);
    [[nodiscard]] Status send_marker(std::uint8_t marker);
    [[nodiscard]] Status read_packet(std::uint8_t& type, std::vector<std::byte>& body);

    [[nodiscard]] Status handshake(const ConnectParams& params);
    [[nodiscard]] Status negotiate();
    [[nodiscard]] Status authenticate(const ConnectParams& params);

    [[nodiscard]] Status run_call(std::span<const std::byte> payload, Call& call);
    [[nodiscard]] Status simple_call(std::uint8_t function);
    [[nodiscard]] Status read_response(Call& call);

    // --- leitura do fluxo TTC ---
    //
    // Falha "grudenta": depois do primeiro erro toda leitura devolve zero e
    // `failed_` fica ligado. Quem le em laco confere `failed_`; assim o
    // interpretador das mensagens nao vira uma escada de `if`.
    [[nodiscard]] bool fill();
    std::uint8_t  r_u8();
    std::uint16_t r_u16be();
    std::uint16_t r_u16le();
    void          r_raw(std::byte* out, std::size_t size);
    void          r_skip(std::size_t size);
    std::uint64_t r_int(unsigned max_size, bool* negative);
    std::uint16_t r_ub2() { return static_cast<std::uint16_t>(r_int(2, nullptr)); }
    std::uint32_t r_ub4() { return static_cast<std::uint32_t>(r_int(4, nullptr)); }
    std::uint64_t r_ub8() { return r_int(8, nullptr); }
    std::int32_t  r_sb4();
    bool          r_bytes(std::string& out);            // falso = nulo
    bool          r_bytes_with_length(std::string& out);
    void          r_skip_bytes();
    void          r_skip_bytes_with_length();
    void          stream_fail(std::string message);

    void read_error_info(Call& call);
    void read_describe_info(std::vector<Column>& columns);
    void read_column(Column& column);
    void read_row(Call& call);
    void read_value(const Column& column, OraType fetch_type, std::string& text, bool& null);
    void read_return_parameters(Call& call);
    void read_key_value_pairs(std::uint16_t count);
    void read_server_piggyback();

    void write_call_header(std::vector<std::byte>& out, std::uint8_t function);

    net::Socket socket_;

    // `cancel()` escreve de outra thread. Em ponteiro porque a Connection e'
    // movida para dentro do Holt, e mutex nao se move.
    std::unique_ptr<std::mutex> send_mutex_;

    bool          large_sdu_ = false;     // tamanho do pacote em 4 bytes (12.1+)
    std::uint32_t sdu_ = 8192;
    std::uint8_t  sequence_ = 0;
    std::uint8_t  field_version_ = 0;         // a negociada: a menor das duas pontas
    std::uint8_t  server_field_version_ = 0;

    std::vector<std::byte> packet_;       // o pacote DATA em leitura
    std::size_t            position_ = 0;
    bool                   failed_ = false;
    Error                  failure_;

    // Cursores a fechar: vao de carona na proxima chamada.
    std::vector<std::uint32_t> cursors_to_close_;

    bool         auto_commit_ = true;
    bool         in_transaction_ = false;
    std::int64_t affected_rows_ = -1;

    std::string server_version_;
    std::string server_banner_;
    int         version_major_ = 0;
    std::string current_schema_;
    std::map<std::string, std::string, std::less<>> session_;
    std::uint16_t national_charset_ = 0;
};

// O descritor de conexao que vai no pacote CONNECT. Publica para o teste.
[[nodiscard]] std::string connect_descriptor(const ConnectParams& params,
                                             std::string_view machine,
                                             std::string_view os_user);

// O que a instrucao e', pela primeira palavra: decide as opcoes do EXECUTE.
enum class StatementKind : std::uint8_t { query, dml, ddl, plsql, other };
[[nodiscard]] StatementKind classify_statement(std::string_view sql) noexcept;

// "23.26.0.0.0" a partir do AUTH_VERSION_NO. O formato mudou no 18c.
[[nodiscard]] std::string format_server_version(std::uint32_t number, bool modern);

} // namespace otter::orawire
