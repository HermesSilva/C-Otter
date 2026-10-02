// C-Otter -- lib/tdswire/tds5.hpp
//
// Conexao com o SQL Anywhere (e, pelo mesmo protocolo, com o Sybase ASE)
// falando TDS 5.0 diretamente no socket -- sem ODBC, sem o jConnect e sem
// FreeTDS (LGPL, proibido pelo ADR 0002).
//
// O TDS 5.0 e' o ramo da Sybase; o do SQL Server (connection.hpp) e' o 7.x. O
// cabecalho do pacote e' o mesmo e quase mais nada: o login e' um registro de
// campos fixos, o comando vai num token LANGUAGE em vez de um pacote proprio,
// os nomes viajam no conjunto de caracteres do cliente (nao em UTF-16), e os
// inteiros de tamanho dos valores seguem outra tabela de tipos.
//
// Mapa do protocolo em docs/SQLANYWHERE-MAP.md; referencia: "TDS 5.0
// Functional Specification" (Sybase, publica).
//
//   LOGIN      registro fixo + token CAPABILITY (o que o cliente sabe ler)
//   LANGUAGE   o texto do comando, em UTF-8
//   resposta   tokens: ROWFMT/ROWFMT2, ROW, DONE*, EED, ENVCHANGE, LOGINACK...
//   ATTENTION  cancelar o comando em curso, na mesma conexao
#pragma once

#include "base/error.hpp"
#include "net/socket.hpp"
#include "net/socks.hpp"
#include "tdswire/connection.hpp"   // Value e Message: os mesmos dos dois ramos

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

// Os tipos que aparecem em ROWFMT (TDS 5.0, "Datatypes").
enum class Tds5Type : std::uint8_t {
    void_        = 0x1F,
    image        = 0x22,
    text         = 0x23,
    varbinary    = 0x25,
    intn         = 0x26,
    varchar      = 0x27,
    binary       = 0x2D,
    char_        = 0x2F,
    int1         = 0x30,   // sem sinal
    date         = 0x31,
    bit          = 0x32,
    time         = 0x33,
    int2         = 0x34,
    int4         = 0x38,
    shortdate    = 0x3A,   // smalldatetime
    float4       = 0x3B,
    money        = 0x3C,
    datetime     = 0x3D,
    float8       = 0x3E,
    uint2        = 0x41,
    uint4        = 0x42,
    uint8        = 0x43,
    uintn        = 0x44,
    decn         = 0x6A,
    numn         = 0x6C,
    fltn         = 0x6D,
    moneyn       = 0x6E,
    datetimn     = 0x6F,
    shortmoney   = 0x7A,
    daten        = 0x7B,
    timen        = 0x93,
    xml          = 0xA3,
    unitext      = 0xAE,
    longchar     = 0xAF,
    sint1        = 0xB0,
    bigdatetimen = 0xBB,
    bigtimen     = 0xBC,
    int8         = 0xBF,
    longbinary   = 0xE1,
};

struct Tds5ConnectParams {
    std::string   host = "localhost";
    std::uint16_t port = 2638;
    // O banco. No SQL Anywhere vai no campo "nome do servidor" do registro de
    // login -- e' o `ServiceName` do jConnect. Vazio = o banco padrao do
    // servidor (o primeiro que ele abriu).
    std::string   database;
    std::string   user;
    std::string   password;
    std::string   application = "C-Otter";
    std::chrono::milliseconds timeout{10000};

    net::ProxyEndpoint proxy;
};

// Uma coluna do resultado (ROWFMT ou ROWFMT2).
struct Tds5Column {
    std::string   name;            // o rotulo (alias), ou o nome da coluna
    // So' com ROWFMT2: de onde a coluna vem. E' o que deixa a grade editavel
    // sem uma segunda consulta ao servidor.
    std::string   catalog;
    std::string   schema;
    std::string   table;
    std::string   column;

    Tds5Type      type = Tds5Type::void_;
    std::uint32_t user_type = 0;
    std::uint32_t max_length = 0;
    std::uint8_t  precision = 0;
    std::uint8_t  scale = 0;
    std::string   type_name;

    bool nullable  = true;
    bool identity  = false;
    bool key       = false;
    bool hidden    = false;
    bool updatable = false;
};

class Tds5Connection {
public:
    using ColumnsCallback = std::function<void(const std::vector<Tds5Column>&)>;
    using RowCallback     = std::function<void(std::span<const Value>)>;

    Tds5Connection();
    ~Tds5Connection();

    Tds5Connection(const Tds5Connection&)            = delete;
    Tds5Connection& operator=(const Tds5Connection&) = delete;
    Tds5Connection(Tds5Connection&&) noexcept;
    Tds5Connection& operator=(Tds5Connection&&) noexcept;

    [[nodiscard]] static Result<Tds5Connection> connect(const Tds5ConnectParams& params);

    [[nodiscard]] bool is_open() const noexcept { return socket_.is_open(); }
    void close() noexcept;

    // Executa um lote. `on_columns` e' chamado a cada conjunto de resultado.
    [[nodiscard]] Status query(std::string_view sql, const ColumnsCallback& on_columns,
                               const RowCallback& on_row);

    // Pacote ATTENTION na mesma conexao. Seguro de outra thread.
    [[nodiscard]] Status cancel();

    // "SQL Anywhere" (ou "Adaptive Server Enterprise"), do LOGINACK.
    [[nodiscard]] const std::string& server_name() const noexcept { return server_name_; }
    [[nodiscard]] const std::string& server_version() const noexcept {
        return server_version_;
    }
    [[nodiscard]] const std::string& database() const noexcept { return database_; }

    // O estado da transacao que o servidor informa em cada DONE.
    [[nodiscard]] bool in_transaction() const noexcept { return in_transaction_; }

    [[nodiscard]] std::int64_t last_affected_rows() const noexcept { return affected_rows_; }

    // Avisos e PRINT/MESSAGE desde a ultima chamada.
    [[nodiscard]] std::vector<Message> take_messages();

private:
    [[nodiscard]] Status send_message(std::uint8_t type, std::span<const std::byte> payload);
    [[nodiscard]] Status read_packet();
    [[nodiscard]] Status login(const Tds5ConnectParams& params);
    [[nodiscard]] Status read_response(const ColumnsCallback& on_columns,
                                       const RowCallback& on_row, bool login_phase);

    [[nodiscard]] bool take(std::size_t size, const std::byte*& out);
    [[nodiscard]] bool at_message_end() const noexcept;
    bool read_u8(std::uint8_t& value);
    bool read_u16(std::uint16_t& value);
    bool read_u32(std::uint32_t& value);
    bool read_string8(std::string& value);    // 1 byte de tamanho + bytes
    bool skip(std::size_t size);

    bool read_format(std::vector<Tds5Column>& columns, bool wide);
    // O byte do tipo e o que vem com ele (tamanho, precisao, escala).
    bool read_type(Tds5Column& column);
    bool read_value(const Tds5Column& column, std::string& text, bool& null);
    bool read_eed(Message& message);
    bool read_old_message(Message& message);

    net::Socket socket_;

    std::unique_ptr<std::mutex>        send_mutex_;
    std::unique_ptr<std::atomic<bool>> attention_pending_;

    std::vector<std::byte> inbox_;
    std::size_t            in_pos_ = 0;
    bool                   message_complete_ = true;
    Status                 stream_error_;

    std::uint32_t packet_size_ = 512;
    bool          in_transaction_ = false;
    std::int64_t  affected_rows_ = -1;
    bool          logged_in_ = false;

    std::string server_name_;
    std::string server_version_;
    std::string database_;

    std::vector<Message> messages_;
};

// --- Montagem de pacotes e conversoes, publicas para os testes --------------------

// O registro de login do TDS 5.0 (campos de tamanho fixo), seguido do token
// CAPABILITY.
[[nodiscard]] std::vector<std::byte> build_tds5_login(const Tds5ConnectParams& params,
                                                      std::string_view client_host,
                                                      std::string_view process_id);

// O token CAPABILITY: o que o cliente pede (tipos e recursos que sabe ler) e
// o que recusa.
[[nodiscard]] std::vector<std::byte> build_tds5_capabilities();

// O token LANGUAGE com o texto do comando.
[[nodiscard]] std::vector<std::byte> build_tds5_language(std::string_view sql);

// numeric/decimal: 1 byte de sinal (1 = negativo) + a magnitude em BIG-endian.
// E' o contrario do SQL Server nas duas coisas.
[[nodiscard]] std::string format_tds5_numeric(std::span<const std::byte> bytes,
                                              std::uint8_t scale);

// date: dias desde 1900-01-01 (com sinal). time: 1/300 s desde a meia-noite.
[[nodiscard]] std::string format_tds5_date(std::span<const std::byte> bytes);
[[nodiscard]] std::string format_tds5_time(std::span<const std::byte> bytes);

// bigdatetime: microssegundos desde 0000-01-01 00:00. bigtime: desde a
// meia-noite.
[[nodiscard]] std::string format_tds5_bigdatetime(std::span<const std::byte> bytes);
[[nodiscard]] std::string format_tds5_bigtime(std::span<const std::byte> bytes);

// O nome do tipo como o SQL Anywhere o escreve: "integer", "varchar",
// "numeric(10,2)", "timestamp". Os tipos de texto vem sem tamanho (o que o
// protocolo informa e' em bytes do conjunto de caracteres do cliente).
[[nodiscard]] std::string tds5_type_name(const Tds5Column& column);

} // namespace otter::tdswire
