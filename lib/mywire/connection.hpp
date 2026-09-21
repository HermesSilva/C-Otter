// C-Otter -- lib/mywire/connection.hpp
//
// Conexao MySQL/MariaDB falando o protocolo diretamente no socket, sem
// libmysqlclient -- que e' GPL e esta' proibida pelo ADR 0002.
//
// Mapa do protocolo em docs/MYSQL-MAP.md.
#pragma once

#include "base/error.hpp"
#include "mywire/packet.hpp"
#include "net/socket.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace otter::mywire {

struct ConnectParams {
    std::string   host = "localhost";
    std::uint16_t port = 3306;
    std::string   database;
    std::string   user;
    std::string   password;
    std::chrono::milliseconds timeout{10000};
};

// Tipos de coluna do MySQL, de `enum_field_types`. Precisamos deles para
// classificar a coluna na grade -- o protocolo nao diz "numero" ou "texto",
// diz o tipo exato.
enum class FieldType : std::uint8_t {
    decimal     = 0x00,
    tiny        = 0x01,
    short_      = 0x02,
    long_       = 0x03,
    float_      = 0x04,
    double_     = 0x05,
    null        = 0x06,
    timestamp   = 0x07,
    longlong    = 0x08,
    int24       = 0x09,
    date        = 0x0A,
    time        = 0x0B,
    datetime    = 0x0C,
    year        = 0x0D,
    newdate     = 0x0E,
    varchar     = 0x0F,
    bit         = 0x10,
    json        = 0xF5,
    newdecimal  = 0xF6,
    enum_       = 0xF7,
    set         = 0xF8,
    tiny_blob   = 0xF9,
    medium_blob = 0xFA,
    long_blob   = 0xFB,
    blob        = 0xFC,
    var_string  = 0xFD,
    string      = 0xFE,
    geometry    = 0xFF,
};

// Flags de coluna, de `ColumnDefinition41`.
enum ColumnFlag : std::uint16_t {
    flag_not_null       = 0x0001,
    flag_primary_key    = 0x0002,
    flag_unique_key     = 0x0004,
    flag_multiple_key   = 0x0008,
    flag_blob           = 0x0010,
    flag_unsigned       = 0x0020,
    flag_auto_increment = 0x0200,
};

// Descricao de coluna, de ColumnDefinition41.
//
// `table` e' a tabela REAL e `table_alias` o nome usado na consulta. A grade
// editavel precisa da real: um UPDATE contra o apelido nao existe.
struct FieldDescription {
    std::string   catalog;
    std::string   database;
    std::string   table_alias;
    std::string   table;
    std::string   name;          // apelido, o que aparece na grade
    std::string   original_name; // nome da coluna na tabela
    std::uint16_t charset = 0;
    std::uint32_t length  = 0;
    FieldType     type    = FieldType::null;
    std::uint16_t flags   = 0;
    std::uint8_t  decimals = 0;

    // Charset 63 (binary) e' o que distingue BLOB de TEXT: os dois chegam com
    // o mesmo `type`, e so' o charset diz qual e' qual.
    [[nodiscard]] bool is_binary() const noexcept { return charset == 63; }
};

// Um valor cru. Como no pgwire, nulo e' marcado em vez de virar string vazia.
struct RawValue {
    std::span<const std::byte> data;
    bool                       null = false;
};

class Connection {
public:
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

    [[nodiscard]] Status query(std::string_view sql,
                               const RowCallback& on_row,
                               std::vector<FieldDescription>* fields = nullptr);

    [[nodiscard]] Status use_database(std::string_view name);
    [[nodiscard]] Status ping();

    // Cancela a consulta em andamento. Como no PostgreSQL, vai por OUTRA
    // conexao: esta esta' ocupada esperando resposta. No MySQL nao ha' pacote
    // de cancelamento -- emite-se `KILL QUERY <id>`.
    [[nodiscard]] Status cancel_current_query() const;

    [[nodiscard]] const std::string& server_version() const noexcept {
        return server_version_;
    }
    [[nodiscard]] bool is_mariadb() const noexcept { return is_mariadb_; }

    // Versao normalizada: 80400 para 8.4.0, 100611 para MariaDB 10.6.11.
    // E' o que os `visibleIf` da arvore consultam.
    [[nodiscard]] std::uint32_t version_number() const noexcept {
        return version_number_;
    }
    [[nodiscard]] std::uint32_t connection_id() const noexcept {
        return connection_id_;
    }
    [[nodiscard]] std::uint16_t server_status() const noexcept { return status_; }
    [[nodiscard]] std::int64_t last_affected_rows() const noexcept {
        return affected_rows_;
    }
    [[nodiscard]] std::uint64_t last_insert_id() const noexcept {
        return last_insert_id_;
    }

private:
    [[nodiscard]] Status  send_packet(std::span<const std::byte> body);
    [[nodiscard]] Status  send_command(Command command, std::string_view argument);
    [[nodiscard]] Result<std::vector<std::byte>> receive_packet();

    [[nodiscard]] Status handshake(const ConnectParams& params);
    [[nodiscard]] Status authenticate(const ConnectParams& params,
                                      std::string_view plugin,
                                      std::span<const std::byte> challenge);
    [[nodiscard]] Status finish_caching_sha2(const ConnectParams& params,
                                             std::span<const std::byte> challenge);

    // Transforma um pacote ERR em erro nosso, preservando o codigo do MySQL.
    [[nodiscard]] Error error_from(std::span<const std::byte> body) const;

    net::Socket socket_;
    std::uint8_t  sequence_     = 0;
    std::uint32_t capabilities_ = 0;
    std::uint32_t connection_id_ = 0;
    std::uint16_t status_        = 0;
    std::int64_t  affected_rows_ = -1;
    std::uint64_t last_insert_id_ = 0;

    std::string   server_version_;
    std::uint32_t version_number_ = 0;
    bool          is_mariadb_ = false;

    // Guardados para abrir a conexao de cancelamento.
    std::string   host_;
    std::uint16_t port_ = 0;
    std::string   user_;
    std::string   password_;
};

// Extrai o numero de versao da string do aperto de mao.
//
// Publica para poder ser testada: o MariaDB se anuncia como
// "5.5.5-10.6.11-MariaDB" por compatibilidade com clientes antigos, e o
// "5.5.5-" na frente e' um prefixo FALSO. Quem compara sem descartar acha que
// esta' falando com um MySQL 5.5 e desliga metade dos recursos.
struct ServerVersion {
    std::uint32_t number = 0;     // 100611 para 10.6.11
    bool          mariadb = false;
};

[[nodiscard]] ServerVersion parse_server_version(std::string_view text);

} // namespace otter::mywire
