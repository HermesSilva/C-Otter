#include "mywire/connection.hpp"

#include "mywire/auth.hpp"
#include "net/crypto.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace otter::mywire {
namespace {

std::span<const std::byte> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

// Codigos de erro do MySQL que valem distinguir. A UI reage diferente a cada
// familia: senha errada pede outra senha, banco inexistente pede outro nome.
Errc errc_from_mysql(std::uint16_t code) noexcept {
    switch (code) {
        case 1045:   // ER_ACCESS_DENIED_ERROR
        case 1044:   // ER_DBACCESS_DENIED_ERROR
        case 1698:   // ER_ACCESS_DENIED_NO_PASSWORD_ERROR
            return Errc::auth_failed;
        case 1049:   // ER_BAD_DB_ERROR
        case 1146:   // ER_NO_SUCH_TABLE
        case 1054:   // ER_BAD_FIELD_ERROR
            return Errc::not_found;
        case 1064:   // ER_PARSE_ERROR
            return Errc::parse_error;
        case 1317:   // ER_QUERY_INTERRUPTED -- o KILL QUERY chegou
            return Errc::cancelled;
        case 1040:   // ER_CON_COUNT_ERROR
        case 2002: case 2003: case 2006: case 2013:
            return Errc::connection_failed;
        default:
            return Errc::query_failed;
    }
}

// Capacidades que pedimos. cap_deprecate_eof faz o servidor 5.7.5+ mandar OK
// no lugar do EOF, o que elimina um caso especial no laco de linhas -- mas o
// laco ainda trata o EOF, porque o 5.5 e o 5.6 nao tem essa capacidade.
constexpr std::uint32_t kClientCapabilities =
    cap_long_password | cap_long_flag | cap_protocol_41 | cap_transactions |
    cap_secure_connection | cap_plugin_auth | cap_plugin_auth_lenenc |
    cap_deprecate_eof | cap_multi_results;

// utf8mb4_general_ci. O utf8 do MySQL guarda no maximo 3 bytes por caractere
// e NAO cobre emoji nem varios ideogramas; utf8mb4 e' o UTF-8 de verdade.
constexpr std::uint8_t kCharsetUtf8Mb4 = 45;

} // namespace

// --- Versao do servidor --------------------------------------------------------

ServerVersion parse_server_version(std::string_view text) {
    ServerVersion out;
    out.mariadb = text.find("MariaDB") != std::string_view::npos ||
                  text.find("mariadb") != std::string_view::npos;

    // O MariaDB 10+ se anuncia como "5.5.5-10.6.11-MariaDB": os clientes
    // antigos recusavam versao que nao comecasse com 5. O "5.5.5-" e' um
    // prefixo FALSO e precisa sair, senao concluiriamos "MySQL 5.5" e
    // desligariamos sequences, eventos e o resto.
    if (out.mariadb && text.starts_with("5.5.5-")) text.remove_prefix(6);

    std::uint32_t parts[3] = {0, 0, 0};
    std::size_t   index = 0;
    std::size_t   position = 0;

    while (index < 3 && position < text.size()) {
        std::uint32_t value = 0;
        const char*   begin = text.data() + position;
        const auto    result = std::from_chars(begin, text.data() + text.size(), value);

        if (result.ec != std::errc{}) break;
        parts[index++] = value;
        position = static_cast<std::size_t>(result.ptr - text.data());

        if (position < text.size() && text[position] == '.') ++position;
        else break;
    }

    // 10.6.11 -> 100611. Cada componente ganha duas casas, o que basta:
    // nenhum MySQL ou MariaDB passou de 99 em patch dentro de uma serie.
    out.number = parts[0] * 10000 + parts[1] * 100 + std::min(parts[2], 99u);
    return out;
}

// --- Ciclo de vida -------------------------------------------------------------

Connection::~Connection() { close(); }

Connection::Connection(Connection&& other) noexcept
    : socket_(std::move(other.socket_)),
      sequence_(other.sequence_),
      capabilities_(other.capabilities_),
      connection_id_(other.connection_id_),
      status_(other.status_),
      affected_rows_(other.affected_rows_),
      last_insert_id_(other.last_insert_id_),
      server_version_(std::move(other.server_version_)),
      version_number_(other.version_number_),
      is_mariadb_(other.is_mariadb_),
      host_(std::move(other.host_)),
      port_(other.port_),
      user_(std::move(other.user_)),
      password_(std::move(other.password_)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        socket_         = std::move(other.socket_);
        sequence_       = other.sequence_;
        capabilities_   = other.capabilities_;
        connection_id_  = other.connection_id_;
        status_         = other.status_;
        affected_rows_  = other.affected_rows_;
        last_insert_id_ = other.last_insert_id_;
        server_version_ = std::move(other.server_version_);
        version_number_ = other.version_number_;
        is_mariadb_     = other.is_mariadb_;
        host_           = std::move(other.host_);
        port_           = other.port_;
        user_           = std::move(other.user_);
        password_       = std::move(other.password_);
    }
    return *this;
}

void Connection::close() noexcept {
    if (!socket_.is_open()) return;

    // COM_QUIT e' cortesia: avisa o servidor para nao registrar "aborted
    // connection" no log. Falha aqui e' irrelevante -- vamos fechar de todo
    // jeito.
    sequence_ = 0;
    PacketWriter writer;
    writer.put_u8(static_cast<std::uint8_t>(Command::quit));
    (void)send_packet(writer.body());

    socket_.close();
}

// --- Transporte ----------------------------------------------------------------

Status Connection::send_packet(std::span<const std::byte> body) {
    const std::vector<std::byte> framed = frame(body, sequence_);
    return socket_.write_all(framed);
}

Result<std::vector<std::byte>> Connection::receive_packet() {
    std::vector<std::byte> body;

    // Laco porque um corpo maior que 16 MB chega partido. A cadeia so' acaba
    // num pacote MENOR que o maximo -- parar no primeiro truncaria em
    // silencio um resultado grande, que e' exatamente o tipo de defeito que
    // so' aparece em producao.
    for (;;) {
        std::array<std::byte, 4> header{};
        if (const Status status = socket_.read_exact(header); !status) {
            return std::unexpected(status.error().with_context("reading packet header"));
        }

        const std::size_t length =
            static_cast<std::size_t>(header[0]) |
            (static_cast<std::size_t>(header[1]) << 8) |
            (static_cast<std::size_t>(header[2]) << 16);

        sequence_ = static_cast<std::uint8_t>(header[3]) + 1;

        const std::size_t offset = body.size();
        body.resize(offset + length);

        if (length > 0) {
            if (const Status status =
                    socket_.read_exact(std::span(body).subspan(offset, length));
                !status) {
                return std::unexpected(status.error().with_context("reading packet body"));
            }
        }
        if (length < kMaxPayload) break;
    }
    return body;
}

Error Connection::error_from(std::span<const std::byte> body) const {
    const ErrPacket err = parse_err(body, capabilities_);
    return Error{errc_from_mysql(err.code), err.to_string()};
}

// --- Conexao e aperto de mao ---------------------------------------------------

Result<Connection> Connection::connect(const ConnectParams& params) {
    if (const Status status = net::initialize_network(); !status) {
        return std::unexpected(status.error());
    }

    Result<net::Socket> socket =
        net::Socket::connect(params.host, params.port, params.timeout);
    if (!socket) {
        return std::unexpected(socket.error().with_context(
            "connecting to " + params.host + ":" + std::to_string(params.port)));
    }

    Connection connection;
    connection.socket_ = std::move(*socket);
    connection.socket_.set_no_delay(true);
    connection.socket_.set_read_timeout(params.timeout);
    connection.socket_.set_write_timeout(params.timeout);
    connection.host_     = params.host;
    connection.port_     = params.port;
    connection.user_     = params.user;
    connection.password_ = params.password;

    if (const Status status = connection.handshake(params); !status) {
        return std::unexpected(status.error());
    }
    return connection;
}

Status Connection::handshake(const ConnectParams& params) {
    const Result<std::vector<std::byte>> greeting = receive_packet();
    if (!greeting) return std::unexpected(greeting.error());

    PacketReader reader(*greeting);

    if (reader.peek() == 0xFF) return std::unexpected(error_from(*greeting));

    const std::uint8_t protocol = reader.read_u8();
    if (protocol != 10) {
        return std::unexpected(Error{
            Errc::protocol_error,
            "unsupported MySQL protocol version " + std::to_string(protocol) +
            " (only HandshakeV10 is implemented)"});
    }

    server_version_ = std::string(reader.read_string());
    const ServerVersion version = parse_server_version(server_version_);
    version_number_ = version.number;
    is_mariadb_     = version.mariadb;

    connection_id_ = reader.read_u32();

    // O desafio vem PARTIDO em duas: 8 bytes aqui, 12 mais adiante. E' resto
    // de compatibilidade com o protocolo antigo, onde so' havia os 8.
    std::vector<std::byte> challenge;
    const std::span<const std::byte> first = reader.read_bytes(8);
    challenge.insert(challenge.end(), first.begin(), first.end());

    reader.skip(1);   // preenchimento

    std::uint32_t server_capabilities = reader.read_u16();
    std::string   plugin;

    if (!reader.exhausted()) {
        reader.skip(1);            // charset padrao do servidor
        status_ = reader.read_u16();

        // A metade ALTA das capacidades. Ler so' a baixa esconderia
        // cap_plugin_auth e cap_deprecate_eof, que estao aqui em cima.
        server_capabilities |= static_cast<std::uint32_t>(reader.read_u16()) << 16;

        const std::uint8_t auth_data_length = reader.read_u8();
        reader.skip(10);           // reservado

        if (server_capabilities & cap_secure_connection) {
            // O campo declara o tamanho TOTAL do desafio, incluindo os 8 ja'
            // lidos, e o servidor escreve um nulo a mais no fim. O max(13)
            // cobre servidores que declaram 0 aqui.
            const std::size_t rest =
                std::max<std::size_t>(13, auth_data_length >= 8
                                              ? auth_data_length - 8u
                                              : 13u);
            const std::span<const std::byte> second = reader.read_bytes(rest);

            // Descarta o nulo final: inclui-lo faria o desafio ter 21 bytes e
            // todo hash sair errado, com o servidor respondendo apenas
            // "access denied" -- sem pista nenhuma da causa.
            const std::size_t useful =
                !second.empty() && second.back() == std::byte{0} ? second.size() - 1
                                                                 : second.size();
            challenge.insert(challenge.end(), second.begin(),
                             second.begin() + static_cast<std::ptrdiff_t>(useful));
        }
        if (server_capabilities & cap_plugin_auth) {
            plugin = std::string(reader.read_string());
        }
    }

    if (plugin.empty()) plugin = std::string(kNativePassword);

    // So' pedimos o que o servidor tambem oferece.
    capabilities_ = kClientCapabilities & server_capabilities;
    if (!params.database.empty()) {
        capabilities_ |= (server_capabilities & cap_connect_with_db);
    }

    if (!(capabilities_ & cap_protocol_41)) {
        return std::unexpected(Error{
            Errc::not_supported,
            "server does not support the 4.1 protocol; MySQL 4.0 and older "
            "are out of scope (ADR 0010 targets 5.5+)"});
    }

    return authenticate(params, plugin, challenge);
}

Status Connection::authenticate(const ConnectParams& params,
                                std::string_view plugin,
                                std::span<const std::byte> challenge) {
    Result<std::vector<std::byte>> response = std::vector<std::byte>{};

    if (plugin == kNativePassword) {
        response = native_password_response(params.password, challenge);
    } else if (plugin == kCachingSha2) {
        response = caching_sha2_response(params.password, challenge);
    } else if (plugin == kClearPassword) {
        // Senha em claro so' faz sentido sobre TLS, que ainda nao temos.
        return std::unexpected(Error{
            Errc::not_supported,
            "server requested mysql_clear_password, which sends the password "
            "in the clear; C-Otter refuses it until TLS is implemented"});
    } else {
        return std::unexpected(Error{
            Errc::not_supported,
            "unsupported authentication plugin '" + std::string(plugin) + "'"});
    }
    if (!response) return std::unexpected(response.error());

    PacketWriter writer;
    writer.put_u32(capabilities_);
    writer.put_u32(static_cast<std::uint32_t>(kMaxPayload));
    writer.put_u8(kCharsetUtf8Mb4);
    writer.fill(23);                  // reservado pelo protocolo
    writer.put_string(params.user);

    if (capabilities_ & cap_plugin_auth_lenenc) {
        writer.put_length(response->size());
        writer.put_bytes(*response);
    } else {
        writer.put_u8(static_cast<std::uint8_t>(response->size()));
        writer.put_bytes(*response);
    }

    if (capabilities_ & cap_connect_with_db) writer.put_string(params.database);
    if (capabilities_ & cap_plugin_auth)     writer.put_string(plugin);

    if (const Status status = send_packet(writer.body()); !status) {
        return std::unexpected(status.error());
    }

    for (;;) {
        const Result<std::vector<std::byte>> reply = receive_packet();
        if (!reply) return std::unexpected(reply.error());
        if (reply->empty()) {
            return std::unexpected(Error{Errc::protocol_error,
                                         "empty reply during authentication"});
        }

        switch (static_cast<std::uint8_t>((*reply)[0])) {
            case 0x00: {   // OK
                const OkPacket ok = parse_ok(*reply, capabilities_);
                status_ = ok.status;
                return {};
            }
            case 0xFF:
                return std::unexpected(error_from(*reply));

            case 0xFE: {
                // AuthSwitchRequest: o servidor quer outro plugin. Acontece
                // quando a conta foi criada com plugin diferente do padrao --
                // comum num MySQL 8 com contas herdadas do 5.7.
                PacketReader switcher(*reply);
                switcher.skip(1);
                const std::string next(switcher.read_string());

                std::vector<std::byte> new_challenge;
                const std::span<const std::byte> data =
                    switcher.read_bytes(switcher.remaining());
                const std::size_t useful =
                    !data.empty() && data.back() == std::byte{0} ? data.size() - 1
                                                                 : data.size();
                new_challenge.assign(data.begin(),
                                     data.begin() + static_cast<std::ptrdiff_t>(useful));

                return authenticate(params, next, new_challenge);
            }
            case 0x01: {
                // Etapa intermediaria do caching_sha2_password.
                PacketReader stage(*reply);
                stage.skip(1);
                const std::uint8_t what = stage.read_u8();

                if (what == static_cast<std::uint8_t>(Sha2Stage::fast_auth_ok)) {
                    continue;   // vem um OK logo atras
                }
                if (what == static_cast<std::uint8_t>(Sha2Stage::full_auth)) {
                    return finish_caching_sha2(params, challenge);
                }
                return std::unexpected(Error{
                    Errc::protocol_error,
                    "unexpected caching_sha2_password stage " + std::to_string(what)});
            }
            default:
                return std::unexpected(Error{
                    Errc::protocol_error,
                    "unexpected packet during authentication"});
        }
    }
}

Status Connection::finish_caching_sha2(const ConnectParams& params,
                                       std::span<const std::byte> challenge) {
    // "Full auth": o cache de hash do servidor nao tem esta conta. Acontece na
    // PRIMEIRA conexao dela depois que o servidor sobe. Sem tratar este caso,
    // o MySQL 8 -- onde caching_sha2_password e' o padrao -- so' conectaria
    // depois de outro cliente ter conectado antes.
    //
    // A senha vai cifrada com a chave publica RSA do servidor. Sobre TLS
    // bastaria mandar em claro, mas ainda nao temos TLS, e mandar em claro
    // sem ele seria expor a senha na rede.

    // 0x02 pede a chave publica. O servidor so' aceita o pedido quando NAO
    // ofereceu a chave junto -- que e' o nosso caso, por nao usarmos TLS.
    PacketWriter request;
    request.put_u8(0x02);
    if (const Status status = send_packet(request.body()); !status) {
        return std::unexpected(status.error());
    }

    const Result<std::vector<std::byte>> reply = receive_packet();
    if (!reply) return std::unexpected(reply.error());
    if (reply->empty()) {
        return std::unexpected(Error{Errc::protocol_error,
                                     "empty reply to public key request"});
    }
    if (static_cast<std::uint8_t>((*reply)[0]) == 0xFF) {
        return std::unexpected(error_from(*reply));
    }

    PacketReader reader(*reply);
    reader.skip(1);   // marcador 0x01
    const std::string pem(reader.read_rest());

    if (pem.find("BEGIN") == std::string::npos) {
        return std::unexpected(Error{
            Errc::protocol_error,
            "server did not return an RSA public key; it may require TLS "
            "(caching_sha2_password full authentication)"});
    }

    // A senha vai com o terminador nulo INCLUIDO -- o servidor conta com ele
    // ao desfazer o XOR. Omiti-lo faz a autenticacao falhar com um "access
    // denied" que nao diz nada sobre a causa.
    std::vector<std::byte> scrambled;
    scrambled.reserve(params.password.size() + 1);
    for (const char c : params.password) scrambled.push_back(static_cast<std::byte>(c));
    scrambled.push_back(std::byte{0});

    // XOR com o desafio, repetido ciclicamente. Sem isso, duas sessoes com a
    // mesma senha produziriam o mesmo texto cifrado, e um observador poderia
    // repetir o pacote.
    if (!challenge.empty()) {
        for (std::size_t i = 0; i < scrambled.size(); ++i) {
            scrambled[i] ^= challenge[i % challenge.size()];
        }
    }

    const Result<std::vector<std::byte>> encrypted =
        crypto::rsa_encrypt_oaep(pem, scrambled);
    if (!encrypted) {
        return std::unexpected(encrypted.error().with_context(
            "encrypting the password with the server's RSA public key"));
    }

    PacketWriter answer;
    answer.put_bytes(*encrypted);
    if (const Status status = send_packet(answer.body()); !status) {
        return std::unexpected(status.error());
    }

    const Result<std::vector<std::byte>> final_reply = receive_packet();
    if (!final_reply) return std::unexpected(final_reply.error());
    if (final_reply->empty()) {
        return std::unexpected(Error{Errc::protocol_error,
                                     "empty reply after RSA authentication"});
    }
    if (static_cast<std::uint8_t>((*final_reply)[0]) == 0xFF) {
        return std::unexpected(error_from(*final_reply));
    }

    const OkPacket ok = parse_ok(*final_reply, capabilities_);
    status_ = ok.status;
    return {};
}

// --- Comandos -------------------------------------------------------------------

Status Connection::send_command(Command command, std::string_view argument) {
    if (!socket_.is_open()) return fail(Errc::closed);

    // Toda sequencia recomeca em zero a cada comando. Nao zerar faz o
    // servidor ver pacote fora de ordem e derrubar a conexao.
    sequence_ = 0;

    PacketWriter writer;
    writer.put_u8(static_cast<std::uint8_t>(command));
    writer.put_bytes(as_bytes(argument));
    return send_packet(writer.body());
}

Status Connection::use_database(std::string_view name) {
    if (const Status status = send_command(Command::init_db, name); !status) {
        return std::unexpected(status.error());
    }
    const Result<std::vector<std::byte>> reply = receive_packet();
    if (!reply) return std::unexpected(reply.error());
    if (!reply->empty() && static_cast<std::uint8_t>((*reply)[0]) == 0xFF) {
        return std::unexpected(error_from(*reply));
    }
    return {};
}

Status Connection::ping() {
    if (const Status status = send_command(Command::ping, {}); !status) {
        return std::unexpected(status.error());
    }
    const Result<std::vector<std::byte>> reply = receive_packet();
    if (!reply) return std::unexpected(reply.error());
    if (!reply->empty() && static_cast<std::uint8_t>((*reply)[0]) == 0xFF) {
        return std::unexpected(error_from(*reply));
    }
    return {};
}

Status Connection::query(std::string_view sql, const RowCallback& on_row,
                         std::vector<FieldDescription>* fields) {
    affected_rows_  = -1;
    last_insert_id_ = 0;

    if (const Status status = send_command(Command::query, sql); !status) {
        return std::unexpected(status.error());
    }

    const Result<std::vector<std::byte>> first = receive_packet();
    if (!first) return std::unexpected(first.error());
    if (first->empty()) {
        return std::unexpected(Error{Errc::protocol_error, "empty query reply"});
    }

    const std::uint8_t kind = static_cast<std::uint8_t>((*first)[0]);

    if (kind == 0xFF) return std::unexpected(error_from(*first));

    if (kind == 0x00) {   // OK: comando sem resultado (INSERT, UPDATE, DDL)
        const OkPacket ok = parse_ok(*first, capabilities_);
        affected_rows_  = static_cast<std::int64_t>(ok.affected_rows);
        last_insert_id_ = ok.last_insert_id;
        status_         = ok.status;
        return {};
    }

    if (kind == 0xFB) {
        // LOCAL INFILE: o servidor pede um arquivo do NOSSO disco. Um servidor
        // hostil pode pedir qualquer caminho, e responder seria vazamento de
        // arquivo local. Recusamos com corpo vazio, como manda o protocolo.
        if (const Status status = send_packet({}); !status) {
            return std::unexpected(status.error());
        }
        // Drena a resposta ao nosso "nao" para deixar o socket em estado
        // limpo. O conteudo nao interessa: ja' decidimos falhar.
        const Result<std::vector<std::byte>> drained = receive_packet();
        (void)drained.has_value();

        return std::unexpected(Error{
            Errc::not_supported,
            "server requested LOCAL INFILE; refused (it would read a local file "
            "chosen by the server)"});
    }

    PacketReader counter(*first);
    const std::uint64_t column_count = counter.read_length();
    if (column_count == 0 || column_count > 4096) {
        return std::unexpected(Error{
            Errc::protocol_error,
            "implausible column count " + std::to_string(column_count)});
    }

    std::vector<FieldDescription> descriptions;
    descriptions.reserve(static_cast<std::size_t>(column_count));

    for (std::uint64_t i = 0; i < column_count; ++i) {
        const Result<std::vector<std::byte>> packet = receive_packet();
        if (!packet) return std::unexpected(packet.error());

        PacketReader reader(*packet);
        FieldDescription field;
        field.catalog       = std::string(reader.read_length_string());
        field.database      = std::string(reader.read_length_string());
        field.table_alias   = std::string(reader.read_length_string());
        field.table         = std::string(reader.read_length_string());
        field.name          = std::string(reader.read_length_string());
        field.original_name = std::string(reader.read_length_string());
        // Campo de comprimento fixo, sempre 0x0C, que o protocolo manda e
        // ninguem usa. Consumir e' obrigatorio; ler o valor, nao.
        const std::uint64_t fixed_length = reader.read_length();
        (void)fixed_length;

        field.charset  = reader.read_u16();
        field.length   = reader.read_u32();
        field.type     = static_cast<FieldType>(reader.read_u8());
        field.flags    = reader.read_u16();
        field.decimals = reader.read_u8();

        if (reader.overflowed()) {
            return std::unexpected(Error{Errc::protocol_error,
                                         "truncated column definition"});
        }
        descriptions.push_back(std::move(field));
    }

    // Sem cap_deprecate_eof vem um EOF aqui, separando colunas de linhas.
    if (!(capabilities_ & cap_deprecate_eof)) {
        const Result<std::vector<std::byte>> eof = receive_packet();
        if (!eof) return std::unexpected(eof.error());
    }

    if (fields) *fields = descriptions;

    std::vector<RawValue> row(static_cast<std::size_t>(column_count));
    std::int64_t          rows_read = 0;

    for (;;) {
        const Result<std::vector<std::byte>> packet = receive_packet();
        if (!packet) return std::unexpected(packet.error());
        if (packet->empty()) {
            return std::unexpected(Error{Errc::protocol_error,
                                         "empty packet in result set"});
        }

        const std::uint8_t marker = static_cast<std::uint8_t>((*packet)[0]);

        if (marker == 0xFF) return std::unexpected(error_from(*packet));

        // 0xFE marca o fim -- mas so' quando o pacote e' CURTO. Uma linha cuja
        // primeira coluna tem 0xFE bytes comeca com o mesmo byte, e confundir
        // os dois cortaria o resultado no meio. O limite de 9 e' o tamanho
        // maximo de um EOF/OK, e e' como o protocolo resolve a ambiguidade.
        if (marker == 0xFE && packet->size() < 9) {
            const OkPacket ok = parse_ok(*packet, capabilities_);
            status_ = ok.status;
            break;
        }

        PacketReader reader(*packet);
        for (std::size_t column = 0; column < row.size(); ++column) {
            bool is_null = false;
            const std::uint64_t length = reader.read_length(&is_null);

            row[column].null = is_null;
            row[column].data = is_null
                                   ? std::span<const std::byte>{}
                                   : reader.read_bytes(static_cast<std::size_t>(length));
        }
        if (reader.overflowed()) {
            return std::unexpected(Error{Errc::protocol_error,
                                         "truncated data row"});
        }

        if (on_row) on_row(row);
        ++rows_read;
    }

    affected_rows_ = rows_read;
    return {};
}

Status Connection::cancel_current_query() const {
    if (connection_id_ == 0) return fail(Errc::invalid_argument);

    // Conexao separada: esta esta' bloqueada esperando a resposta da consulta
    // que queremos matar. Mesmo desenho do CancelRequest do PostgreSQL.
    ConnectParams params;
    params.host     = host_;
    params.port     = port_;
    params.user     = user_;
    params.password = password_;
    params.timeout  = std::chrono::seconds(5);

    Result<Connection> killer = Connection::connect(params);
    if (!killer) return std::unexpected(killer.error().with_context(
        "opening a second connection to cancel the query"));

    // KILL QUERY mata a CONSULTA; KILL CONNECTION mataria a sessao, perdendo
    // a transacao aberta junto.
    return killer->query("KILL QUERY " + std::to_string(connection_id_), nullptr);
}

} // namespace otter::mywire
