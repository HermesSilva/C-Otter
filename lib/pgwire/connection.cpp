#include "pgwire/connection.hpp"

#include "net/crypto.hpp"
#include "pgwire/scram.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <optional>
#include <utility>

namespace otter::pgwire {
namespace {

// Versao 3.0 do protocolo, codificada como 0x00030000.
constexpr std::int32_t kProtocolVersion3 = 196608;

// Codigo magico da mensagem CancelRequest.
constexpr std::int32_t kCancelRequestCode = 80877102;

std::span<const std::byte> as_bytes(std::string_view text) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
}

std::string_view as_text(std::span<const std::byte> data) {
    return std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
}

// Mapeia SQLSTATE para o nosso codigo de erro: a UI reage diferente a cada
// familia de falha.
Errc errc_from_sqlstate(std::string_view sqlstate) noexcept {
    if (sqlstate.starts_with("28")) return Errc::auth_failed;
    if (sqlstate.starts_with("08")) return Errc::connection_failed;
    if (sqlstate.starts_with("42")) return Errc::parse_error;
    if (sqlstate == "57014")        return Errc::cancelled;
    return Errc::query_failed;
}

} // namespace

Connection::~Connection() { close(); }

Connection::Connection(Connection&& other) noexcept
    : socket_(std::move(other.socket_)),
      parameters_(std::move(other.parameters_)),
      transaction_status_(other.transaction_status_),
      backend_pid_(other.backend_pid_),
      backend_secret_(other.backend_secret_),
      affected_rows_(other.affected_rows_),
      host_(std::move(other.host_)),
      port_(other.port_) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        socket_             = std::move(other.socket_);
        parameters_         = std::move(other.parameters_);
        transaction_status_ = other.transaction_status_;
        backend_pid_        = other.backend_pid_;
        backend_secret_     = other.backend_secret_;
        affected_rows_      = other.affected_rows_;
        host_               = std::move(other.host_);
        port_               = other.port_;
    }
    return *this;
}

void Connection::close() noexcept {
    if (socket_.is_open()) {
        // Terminate ('X'): encerramento limpo. Se falhar, fechamos assim mesmo.
        MessageWriter writer('X');
        (void)socket_.write_all(writer.finish());
        socket_.close();
    }
}

Status Connection::send(std::span<const std::byte> data) {
    return socket_.write_all(data);
}

Result<Connection::Incoming> Connection::receive() {
    // Cabecalho: [tipo: 1][tamanho: 4]. O tamanho inclui os proprios 4 bytes.
    std::array<std::byte, 5> header{};
    OTTER_RETURN_IF_ERROR(socket_.read_exact(header));

    Incoming message;
    message.type = static_cast<char>(header[0]);

    std::uint32_t length = 0;
    for (std::size_t i = 1; i < 5; ++i) {
        length = (length << 8) | static_cast<std::uint32_t>(header[i]);
    }

    if (length < 4) {
        return fail(Errc::protocol_error,
                    "tamanho de mensagem inválido: " + std::to_string(length));
    }

    // Guarda contra mensagem absurda: 1 GB e' muito mais que qualquer resposta
    // legitima de protocolo e sinaliza fluxo corrompido.
    constexpr std::uint32_t kMaxMessage = 1u << 30;
    if (length > kMaxMessage) {
        return fail(Errc::protocol_error, "mensagem grande demais");
    }

    message.body.resize(length - 4);
    if (!message.body.empty()) {
        OTTER_RETURN_IF_ERROR(socket_.read_exact(message.body));
    }
    return message;
}

Result<Connection> Connection::connect(const ConnectParams& params) {
    OTTER_ASSIGN_OR_RETURN(
        auto socket, net::Socket::connect(params.host, params.port, params.timeout));

    Connection conn;
    conn.socket_ = std::move(socket);
    conn.host_   = params.host;
    conn.port_   = params.port;

    // StartupMessage nao tem byte de tipo.
    MessageWriter startup(0);
    startup.put_int32(kProtocolVersion3);
    startup.put_string("user");
    startup.put_string(params.user);
    if (!params.database.empty()) {
        startup.put_string("database");
        startup.put_string(params.database);
    }
    startup.put_string("application_name");
    startup.put_string(params.application_name);
    startup.put_string("client_encoding");
    startup.put_string("UTF8");
    startup.put_string("");   // terminador da lista de parametros

    OTTER_RETURN_IF_ERROR(conn.send(startup.finish()));
    OTTER_RETURN_IF_ERROR(conn.authenticate(params));

    return conn;
}

Status Connection::authenticate(const ConnectParams& params) {
    for (;;) {
        OTTER_ASSIGN_OR_RETURN(auto message, receive());

        switch (static_cast<BackendType>(message.type)) {
            case BackendType::authentication: {
                MessageReader reader(message.body);
                const auto kind = static_cast<AuthType>(reader.read_int32());

                switch (kind) {
                    case AuthType::ok:
                        break;   // segue ate' ReadyForQuery

                    case AuthType::cleartext_password: {
                        MessageWriter writer('p');
                        writer.put_string(params.password);
                        OTTER_RETURN_IF_ERROR(send(writer.finish()));
                        break;
                    }

                    case AuthType::md5_password: {
                        OTTER_RETURN_IF_ERROR(handle_md5(
                            params,
                            std::span<const std::byte>(message.body).subspan(4)));
                        break;
                    }

                    case AuthType::sasl:
                    case AuthType::sasl_continue:
                    case AuthType::sasl_final: {
                        OTTER_RETURN_IF_ERROR(handle_sasl(params, message.body));
                        break;
                    }

                    default:
                        return fail(Errc::not_supported,
                                    "método de autenticação não suportado: " +
                                        std::to_string(static_cast<int>(kind)));
                }
                break;
            }

            case BackendType::parameter_status: {
                MessageReader reader(message.body);
                const std::string key   = std::string(reader.read_string());
                const std::string value = std::string(reader.read_string());
                parameters_[key] = value;
                break;
            }

            case BackendType::backend_key_data: {
                MessageReader reader(message.body);
                backend_pid_    = reader.read_int32();
                backend_secret_ = reader.read_int32();
                break;
            }

            case BackendType::ready_for_query: {
                MessageReader reader(message.body);
                transaction_status_ =
                    static_cast<TransactionStatus>(reader.read_int8());
                return {};   // handshake concluido
            }

            case BackendType::error_response: {
                const ErrorInfo info = parse_error_response(message.body);
                return fail(errc_from_sqlstate(info.sqlstate), info.to_string());
            }

            case BackendType::notice_response:
                break;   // avisos nao interrompem o handshake

            default:
                return fail(Errc::protocol_error,
                            std::string("mensagem inesperada durante o handshake: '") +
                                message.type + "'");
        }
    }
}

Status Connection::handle_sasl(const ConnectParams& params,
                               std::span<const std::byte> body) {
    MessageReader reader(body);
    const auto kind = static_cast<AuthType>(reader.read_int32());

    // A instancia persiste entre as tres mensagens da troca SASL.
    static thread_local std::optional<ScramClient> client;

    if (kind == AuthType::sasl) {
        // O servidor lista os mecanismos suportados.
        bool supports_scram_sha256 = false;
        while (!reader.exhausted()) {
            const std::string_view mechanism = reader.read_string();
            if (mechanism.empty()) break;
            if (mechanism == "SCRAM-SHA-256") supports_scram_sha256 = true;
        }

        if (!supports_scram_sha256) {
            return fail(Errc::not_supported,
                        "servidor não oferece SCRAM-SHA-256");
        }

        OTTER_ASSIGN_OR_RETURN(auto started, ScramClient::begin(params.password));
        client = std::move(started);

        MessageWriter writer('p');
        writer.put_string("SCRAM-SHA-256");
        writer.put_int32(static_cast<std::int32_t>(client->client_first().size()));
        writer.put_bytes(as_bytes(client->client_first()));
        return send(writer.finish());
    }

    if (!client.has_value()) {
        return fail(Errc::protocol_error, "SASL continuado sem início");
    }

    if (kind == AuthType::sasl_continue) {
        const std::string_view server_first = as_text(body.subspan(4));
        OTTER_ASSIGN_OR_RETURN(auto client_final,
                               client->handle_server_first(server_first));

        MessageWriter writer('p');
        writer.put_bytes(as_bytes(client_final));
        return send(writer.finish());
    }

    if (kind == AuthType::sasl_final) {
        const std::string_view server_final = as_text(body.subspan(4));
        const Status verified = client->handle_server_final(server_final);
        client.reset();
        return verified;
    }

    return fail(Errc::protocol_error, "mensagem SASL inesperada");
}

Status Connection::handle_md5(const ConnectParams& params,
                              std::span<const std::byte> salt) {
    // md5(md5(password + user) + salt), conforme o protocolo.
    // Legado e fraco -- aceito apenas porque servidores antigos ainda o usam.
    const std::string inner_text = params.password + params.user;
    OTTER_ASSIGN_OR_RETURN(auto inner, crypto::md5(as_bytes(inner_text)));

    std::string stage = crypto::hex_encode(inner);

    std::vector<std::byte> outer_input;
    outer_input.reserve(stage.size() + salt.size());
    for (char c : stage) outer_input.push_back(static_cast<std::byte>(c));
    outer_input.insert(outer_input.end(), salt.begin(), salt.end());

    OTTER_ASSIGN_OR_RETURN(auto outer, crypto::md5(outer_input));

    MessageWriter writer('p');
    writer.put_string("md5" + crypto::hex_encode(outer));
    return send(writer.finish());
}

Status Connection::query(std::string_view sql, const RowCallback& on_row,
                         std::vector<FieldDescription>* fields) {
    if (!is_open()) return fail(Errc::closed, "conexão fechada");

    affected_rows_ = -1;

    MessageWriter writer('Q');
    writer.put_string(sql);
    OTTER_RETURN_IF_ERROR(send(writer.finish()));

    std::vector<FieldDescription> local_fields;
    std::vector<RawValue> row;
    std::optional<Error> pending_error;

    for (;;) {
        OTTER_ASSIGN_OR_RETURN(auto message, receive());

        switch (static_cast<BackendType>(message.type)) {
            case BackendType::row_description: {
                MessageReader reader(message.body);
                const auto count = reader.read_int16();

                local_fields.clear();
                local_fields.reserve(static_cast<std::size_t>(std::max<std::int16_t>(count, 0)));

                for (std::int16_t i = 0; i < count; ++i) {
                    FieldDescription field;
                    field.name          = std::string(reader.read_string());
                    field.table_oid     = static_cast<std::uint32_t>(reader.read_int32());
                    field.column_id     = reader.read_int16();
                    field.type_oid      = static_cast<std::uint32_t>(reader.read_int32());
                    field.type_size     = reader.read_int16();
                    field.type_modifier = reader.read_int32();
                    field.format        = reader.read_int16();
                    local_fields.push_back(std::move(field));
                }
                if (fields != nullptr) *fields = local_fields;
                break;
            }

            case BackendType::data_row: {
                MessageReader reader(message.body);
                const auto count = reader.read_int16();

                row.clear();
                row.reserve(static_cast<std::size_t>(std::max<std::int16_t>(count, 0)));

                for (std::int16_t i = 0; i < count; ++i) {
                    const std::int32_t length = reader.read_int32();
                    if (length < 0) {
                        // -1 e' NULL, distinto de comprimento zero.
                        row.push_back(RawValue{{}, true});
                    } else {
                        row.push_back(RawValue{
                            reader.read_bytes(static_cast<std::size_t>(length)), false});
                    }
                }

                if (reader.overflowed()) {
                    return fail(Errc::protocol_error, "DataRow truncada");
                }
                if (on_row) on_row(row);
                break;
            }

            case BackendType::command_complete: {
                MessageReader reader(message.body);
                const std::string_view tag = reader.read_string();

                // "INSERT 0 3", "UPDATE 7", "SELECT 12": o ultimo numero e' a
                // contagem de linhas.
                const std::size_t space = tag.find_last_of(' ');
                if (space != std::string_view::npos) {
                    const std::string_view number = tag.substr(space + 1);
                    std::int64_t value = 0;
                    const auto [ptr, ec] = std::from_chars(
                        number.data(), number.data() + number.size(), value);
                    if (ec == std::errc{}) affected_rows_ = value;
                }
                break;
            }

            case BackendType::error_response: {
                // Nao retornamos aqui: o servidor ainda enviara ReadyForQuery, e
                // abandonar a leitura deixaria a conexao dessincronizada.
                const ErrorInfo info = parse_error_response(message.body);
                pending_error = Error(errc_from_sqlstate(info.sqlstate), info.to_string());
                break;
            }

            case BackendType::ready_for_query: {
                MessageReader reader(message.body);
                transaction_status_ =
                    static_cast<TransactionStatus>(reader.read_int8());

                if (pending_error.has_value()) {
                    return std::unexpected(*pending_error);
                }
                return {};
            }

            case BackendType::empty_query:
            case BackendType::notice_response:
            case BackendType::parameter_status:
            case BackendType::notification:
                break;   // informativas, nao alteram o fluxo

            default:
                break;   // mensagens desconhecidas sao ignoradas por design
        }
    }
}

Status Connection::cancel_current_query() const {
    if (backend_pid_ == 0) {
        return fail(Errc::not_supported, "sem dados de cancelamento do servidor");
    }

    // O protocolo exige uma conexao NOVA: a original esta' ocupada aguardando
    // a resposta da query que queremos interromper.
    OTTER_ASSIGN_OR_RETURN(
        auto socket,
        net::Socket::connect(host_, port_, std::chrono::seconds(5)));

    MessageWriter writer(0);   // CancelRequest nao tem byte de tipo
    writer.put_int32(kCancelRequestCode);
    writer.put_int32(backend_pid_);
    writer.put_int32(backend_secret_);

    OTTER_RETURN_IF_ERROR(socket.write_all(writer.finish()));

    // O servidor nao responde: fecha a conexao apos processar o pedido.
    socket.close();
    return {};
}

std::string Connection::server_version() const {
    const auto it = parameters_.find("server_version");
    return it != parameters_.end() ? it->second : std::string();
}

} // namespace otter::pgwire
