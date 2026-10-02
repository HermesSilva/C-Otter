#include "tdswire/connection.hpp"

#include "net/sspi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace otter::tdswire {
namespace {

// Tipos de pacote ([MS-TDS] 2.2.3.1.1).
constexpr std::uint8_t kPacketBatch     = 0x01;
constexpr std::uint8_t kPacketAttention = 0x06;
constexpr std::uint8_t kPacketLogin7    = 0x10;
constexpr std::uint8_t kPacketSspi      = 0x11;
constexpr std::uint8_t kPacketPrelogin  = 0x12;

constexpr std::uint8_t kStatusEom = 0x01;

// Negociacao de criptografia do PRELOGIN.
constexpr std::uint8_t kEncryptOff    = 0x00;   // so' o pacote de login
constexpr std::uint8_t kEncryptOn     = 0x01;
constexpr std::uint8_t kEncryptNotSup = 0x02;
constexpr std::uint8_t kEncryptReq    = 0x03;

// Tokens da resposta ([MS-TDS] 2.2.7).
constexpr std::uint8_t kTokenReturnStatus = 0x79;
constexpr std::uint8_t kTokenColMetadata  = 0x81;
constexpr std::uint8_t kTokenTabName      = 0xA4;
constexpr std::uint8_t kTokenColInfo      = 0xA5;
constexpr std::uint8_t kTokenOrder        = 0xA9;
constexpr std::uint8_t kTokenError        = 0xAA;
constexpr std::uint8_t kTokenInfo         = 0xAB;
constexpr std::uint8_t kTokenReturnValue  = 0xAC;
constexpr std::uint8_t kTokenLoginAck     = 0xAD;
constexpr std::uint8_t kTokenFeatureAck   = 0xAE;
constexpr std::uint8_t kTokenRow          = 0xD1;
constexpr std::uint8_t kTokenNbcRow       = 0xD2;
constexpr std::uint8_t kTokenEnvChange    = 0xE3;
constexpr std::uint8_t kTokenSspi         = 0xED;
constexpr std::uint8_t kTokenFedAuthInfo  = 0xEE;
constexpr std::uint8_t kTokenDone         = 0xFD;
constexpr std::uint8_t kTokenDoneProc     = 0xFE;
constexpr std::uint8_t kTokenDoneInProc   = 0xFF;

constexpr std::uint16_t kDoneCount = 0x0010;
constexpr std::uint16_t kDoneAttn  = 0x0020;

void put_u16le(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void put_u32le(std::vector<std::byte>& out, std::uint32_t value) {
    put_u16le(out, value & 0xFFFF);
    put_u16le(out, value >> 16);
}

void put_u16be(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(value & 0xFF));
}

void set_u16le(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
    out[at]     = static_cast<std::byte>(value & 0xFF);
    out[at + 1] = static_cast<std::byte>((value >> 8) & 0xFF);
}

void set_u32le(std::vector<std::byte>& out, std::size_t at, std::uint32_t value) {
    set_u16le(out, at, value & 0xFFFF);
    set_u16le(out, at + 2, value >> 16);
}

std::span<const std::byte> as_bytes(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

std::string client_host_name() {
    if (const char* name = std::getenv("COMPUTERNAME")) return name;
    if (const char* name = std::getenv("HOSTNAME")) return name;
    return "c-otter";
}

// "Msg 208, Level 16, State 1, Line 3" -- o cabecalho que toda ferramenta do
// SQL Server mostra, e por onde se procura o erro.
Error to_error(const Message& message) {
    char header[96];
    std::snprintf(header, sizeof header, " (Msg %d, Level %u, State %u, Line %d)",
                  message.number, static_cast<unsigned>(message.severity),
                  static_cast<unsigned>(message.state), message.line);
    const Errc code = message.number == 18456 || message.number == 18452
                          ? Errc::auth_failed
                          : Errc::query_failed;
    return Error{code, message.text + header};
}

} // namespace

// --- Montagem de pacotes ------------------------------------------------------------

std::vector<std::byte> build_prelogin(std::uint8_t encryption) {
    // Cinco opcoes, cada uma (token, deslocamento, tamanho), e o terminador.
    struct Option {
        std::uint8_t           token;
        std::vector<std::byte> data;
    };
    std::vector<Option> options;
    // VERSION: versao do cliente (major, minor, build) + subbuild.
    options.push_back({0x00, {std::byte{0x10}, std::byte{0x00}, std::byte{0x00},
                              std::byte{0x00}, std::byte{0x00}, std::byte{0x00}}});
    options.push_back({0x01, {static_cast<std::byte>(encryption)}});
    options.push_back({0x02, {std::byte{0x00}}});   // INSTOPT: instancia padrao
    options.push_back({0x03, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}}});
    options.push_back({0x04, {std::byte{0x00}}});   // MARS desligado

    std::vector<std::byte> out;
    std::uint32_t offset = static_cast<std::uint32_t>(options.size() * 5 + 1);
    for (const Option& option : options) {
        out.push_back(static_cast<std::byte>(option.token));
        put_u16be(out, offset);
        put_u16be(out, static_cast<std::uint32_t>(option.data.size()));
        offset += static_cast<std::uint32_t>(option.data.size());
    }
    out.push_back(std::byte{0xFF});
    for (const Option& option : options) {
        out.insert(out.end(), option.data.begin(), option.data.end());
    }
    return out;
}

std::uint8_t parse_prelogin_encryption(std::span<const std::byte> body) {
    for (std::size_t at = 0; at + 5 <= body.size(); at += 5) {
        const auto token = static_cast<std::uint8_t>(body[at]);
        if (token == 0xFF) break;
        const std::size_t offset = (static_cast<std::size_t>(body[at + 1]) << 8) |
                                   static_cast<std::size_t>(body[at + 2]);
        const std::size_t length = (static_cast<std::size_t>(body[at + 3]) << 8) |
                                   static_cast<std::size_t>(body[at + 4]);
        if (token == 0x01 && length >= 1 && offset < body.size()) {
            return static_cast<std::uint8_t>(body[offset]);
        }
    }
    return 0xFF;
}

std::vector<std::byte> build_login7(const ConnectParams& params,
                                    std::string_view client_host,
                                    std::span<const std::byte> sspi) {
    const bool integrated = !sspi.empty();

    const std::string host     = utf8_to_utf16le(client_host);
    const std::string user     = integrated ? std::string{} : utf8_to_utf16le(params.user);
    const std::string password =
        integrated ? std::string{} : obfuscate_password(utf8_to_utf16le(params.password));
    const std::string app      = utf8_to_utf16le(params.application);
    const std::string server   = utf8_to_utf16le(params.host);
    const std::string library  = utf8_to_utf16le("C-Otter");
    const std::string database = utf8_to_utf16le(params.database);

    constexpr std::size_t kFixed = 94;
    std::vector<std::byte> out(kFixed, std::byte{0});

    set_u32le(out, 4, 0x74000004);    // TDS 7.4
    set_u32le(out, 8, 4096);          // tamanho de pacote pedido
    set_u32le(out, 12, 0x00000100);   // versao do cliente
    set_u32le(out, 16, 0);            // PID
    set_u32le(out, 20, 0);            // ConnectionID

    // OptionFlags1: USE_DB (avisa a troca de banco), falha se o banco inicial
    // nao existir, SET_LANG.
    out[24] = std::byte{0xE0};
    // OptionFlags2: falha se o idioma nao existir, ODBC (liga ANSI_NULLS,
    // QUOTED_IDENTIFIER e companhia -- o que toda ferramenta espera), e
    // seguranca integrada quando ha' token SSPI.
    out[25] = static_cast<std::byte>(0x03 | (integrated ? 0x80 : 0x00));
    out[26] = std::byte{0x00};        // TypeFlags
    out[27] = std::byte{0x00};        // OptionFlags3
    set_u32le(out, 28, 0);            // fuso do cliente
    set_u32le(out, 32, 0x00000409);   // LCID

    // Os pares (deslocamento, tamanho em CARACTERES), na ordem do protocolo.
    std::size_t field = 36;
    const auto place = [&out, &field](std::string_view utf16) {
        set_u16le(out, field, static_cast<std::uint32_t>(out.size()));
        set_u16le(out, field + 2, static_cast<std::uint32_t>(utf16.size() / 2));
        field += 4;
        const std::span<const std::byte> bytes = as_bytes(utf16);
        out.insert(out.end(), bytes.begin(), bytes.end());
    };

    place(host);
    place(user);
    place(password);
    place(app);
    place(server);
    place({});          // extensao: nao usada
    place(library);
    place({});          // idioma: o do login
    place(database);
    field += 6;         // ClientID (MAC): zeros

    // SSPI: deslocamento e tamanho em BYTES.
    set_u16le(out, field, static_cast<std::uint32_t>(out.size()));
    set_u16le(out, field + 2, static_cast<std::uint32_t>(std::min<std::size_t>(sspi.size(), 0xFFFF)));
    field += 4;
    out.insert(out.end(), sspi.begin(), sspi.end());

    place({});          // AtchDBFile
    place({});          // ChangePassword
    set_u32le(out, field, sspi.size() > 0xFFFF ? static_cast<std::uint32_t>(sspi.size()) : 0);

    set_u32le(out, 0, static_cast<std::uint32_t>(out.size()));
    return out;
}

std::vector<std::byte> build_batch(std::string_view sql, std::uint64_t transaction) {
    std::vector<std::byte> out;
    // ALL_HEADERS com UM cabecalho: o descritor da transacao. Sem ele, um
    // comando enviado dentro de uma transacao aberta e' recusado.
    put_u32le(out, 22);
    put_u32le(out, 18);
    put_u16le(out, 0x0002);
    put_u32le(out, static_cast<std::uint32_t>(transaction & 0xFFFFFFFF));
    put_u32le(out, static_cast<std::uint32_t>(transaction >> 32));
    put_u32le(out, 1);   // OutstandingRequestCount

    const std::string text = utf8_to_utf16le(sql);
    const std::span<const std::byte> bytes = as_bytes(text);
    out.insert(out.end(), bytes.begin(), bytes.end());
    return out;
}

// --- Ciclo de vida ------------------------------------------------------------------

Connection::Connection()
    : send_mutex_(std::make_unique<std::mutex>()),
      attention_pending_(std::make_unique<std::atomic<bool>>(false)) {}

Connection::~Connection() { close(); }

Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

void Connection::close() noexcept {
    tls_.close();
    socket_.close();
    tls_on_ = false;
}

Result<Connection> Connection::connect(const ConnectParams& params) {
    if (const Status status = net::initialize_network(); !status) {
        return std::unexpected(status.error());
    }
    if (params.integrated && !net::SspiClient::available()) {
        return fail(Errc::not_supported,
                    "Windows authentication is only available on Windows");
    }

    Result<net::Socket> socket =
        net::connect_to(params.proxy, params.host, params.port, params.timeout);
    if (!socket) {
        return std::unexpected(socket.error().with_context(
            "connecting to " + params.host + ":" + std::to_string(params.port)));
    }

    Connection connection;
    connection.socket_ = std::move(*socket);
    connection.socket_.set_no_delay(true);
    connection.socket_.set_read_timeout(params.timeout);
    connection.socket_.set_write_timeout(params.timeout);

    OTTER_RETURN_IF_ERROR(connection.prelogin(params));
    OTTER_RETURN_IF_ERROR(connection.login(params));

    // Dai' em diante uma consulta pode demorar o que demorar: o tempo limite
    // era o da CONEXAO, nao o de cada leitura.
    connection.socket_.set_read_timeout(std::chrono::milliseconds(0));
    return connection;
}

// --- Transporte ---------------------------------------------------------------------

Status Connection::write_raw(std::span<const std::byte> data) {
    return tls_on_ ? tls_.write_all(socket_, data) : socket_.write_all(data);
}

Status Connection::read_raw(std::span<std::byte> buffer) {
    return tls_on_ ? tls_.read_exact(socket_, buffer) : socket_.read_exact(buffer);
}

Status Connection::send_message(std::uint8_t type, std::span<const std::byte> payload) {
    const std::lock_guard<std::mutex> lock(*send_mutex_);

    const std::size_t room = packet_size_ - 8;
    std::size_t offset = 0;
    std::vector<std::byte> packet;

    do {
        const std::size_t chunk = std::min(room, payload.size() - offset);
        const bool last = offset + chunk == payload.size();

        packet.clear();
        packet.push_back(static_cast<std::byte>(type));
        packet.push_back(static_cast<std::byte>(last ? kStatusEom : 0x00));
        put_u16be(packet, static_cast<std::uint32_t>(chunk + 8));
        put_u16be(packet, 0);                                  // SPID
        packet.push_back(static_cast<std::byte>(packet_id_++));
        packet.push_back(std::byte{0});                        // janela
        packet.insert(packet.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                      payload.begin() + static_cast<std::ptrdiff_t>(offset + chunk));

        OTTER_RETURN_IF_ERROR(write_raw(packet));
        offset += chunk;
    } while (offset < payload.size());
    return {};
}

Status Connection::read_packet() {
    std::byte header[8];
    OTTER_RETURN_IF_ERROR(read_raw(header));

    const std::size_t length = (static_cast<std::size_t>(header[2]) << 8) |
                               static_cast<std::size_t>(header[3]);
    if (length < 8) return fail(Errc::protocol_error, "invalid TDS packet length");

    // O que ja' foi consumido sai antes de crescer: uma resposta de um milhao
    // de linhas nao pode ficar inteira na memoria.
    if (in_pos_ > 0) {
        inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<std::ptrdiff_t>(in_pos_));
        in_pos_ = 0;
    }
    const std::size_t offset = inbox_.size();
    inbox_.resize(offset + length - 8);
    if (length > 8) {
        if (Status status = read_raw(std::span(inbox_).subspan(offset)); !status) {
            inbox_.resize(offset);
            return status;
        }
    }
    message_complete_ = (static_cast<std::uint8_t>(header[1]) & kStatusEom) != 0;
    return {};
}

bool Connection::at_message_end() const noexcept {
    return message_complete_ && in_pos_ == inbox_.size();
}

bool Connection::take(std::size_t size, const std::byte*& out) {
    while (inbox_.size() - in_pos_ < size) {
        if (message_complete_) {
            stream_error_ = fail(Errc::protocol_error, "truncated TDS response");
            return false;
        }
        if (Status status = read_packet(); !status) {
            stream_error_ = status;
            return false;
        }
    }
    out = inbox_.data() + in_pos_;
    in_pos_ += size;
    return true;
}

bool Connection::skip(std::size_t size) {
    // Em pedacos: um valor ignorado pode ser maior que a memoria que vale a
    // pena reservar para ele.
    while (size > 0) {
        const std::size_t chunk = std::min<std::size_t>(size, 65536);
        const std::byte* unused = nullptr;
        if (!take(chunk, unused)) return false;
        size -= chunk;
    }
    return true;
}

bool Connection::read_u8(std::uint8_t& value) {
    const std::byte* p = nullptr;
    if (!take(1, p)) return false;
    value = static_cast<std::uint8_t>(p[0]);
    return true;
}

bool Connection::read_u16(std::uint16_t& value) {
    const std::byte* p = nullptr;
    if (!take(2, p)) return false;
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                       (static_cast<std::uint16_t>(p[1]) << 8));
    return true;
}

bool Connection::read_u32(std::uint32_t& value) {
    const std::byte* p = nullptr;
    if (!take(4, p)) return false;
    value = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    return true;
}

bool Connection::read_u64(std::uint64_t& value) {
    std::uint32_t low = 0, high = 0;
    if (!read_u32(low) || !read_u32(high)) return false;
    value = (static_cast<std::uint64_t>(high) << 32) | low;
    return true;
}

bool Connection::read_b_varchar(std::string& value) {
    std::uint8_t length = 0;
    const std::byte* p = nullptr;
    if (!read_u8(length) || !take(static_cast<std::size_t>(length) * 2, p)) return false;
    value = utf16le_to_utf8({p, static_cast<std::size_t>(length) * 2});
    return true;
}

bool Connection::read_us_varchar(std::string& value) {
    std::uint16_t length = 0;
    const std::byte* p = nullptr;
    if (!read_u16(length) || !take(static_cast<std::size_t>(length) * 2, p)) return false;
    value = utf16le_to_utf8({p, static_cast<std::size_t>(length) * 2});
    return true;
}

// --- PRELOGIN e TLS -----------------------------------------------------------------

Status Connection::prelogin(const ConnectParams& params) {
    // O que o cliente oferece: tudo cifrado, ou so' o login. Sem TLS nesta
    // compilacao, nada -- e o servidor decide se aceita.
    const std::uint8_t wanted = !net::tls_available() ? kEncryptNotSup
                                : params.encrypt      ? kEncryptOn
                                                      : kEncryptOff;

    OTTER_RETURN_IF_ERROR(send_message(kPacketPrelogin, build_prelogin(wanted)));

    inbox_.clear();
    in_pos_ = 0;
    message_complete_ = false;
    while (!message_complete_) {
        if (Status status = read_packet(); !status) {
            return std::unexpected(status.error().with_context(
                "reading the PRELOGIN response (is this a SQL Server port?)"));
        }
    }
    server_encryption_ = parse_prelogin_encryption(inbox_);
    in_pos_ = inbox_.size();

    if (server_encryption_ == kEncryptNotSup) {
        if (params.encrypt) {
            return fail(Errc::connection_failed,
                        "the server does not support encryption, and the connection "
                        "requires it");
        }
        return {};   // login em claro: a senha vai so' embaralhada
    }
    if (wanted == kEncryptNotSup) {
        if (server_encryption_ == kEncryptReq || server_encryption_ == kEncryptOn) {
            return fail(Errc::not_supported,
                        "the server requires encryption, and this build has no TLS");
        }
        return {};
    }

    tls_full_ = params.encrypt || server_encryption_ == kEncryptOn ||
                server_encryption_ == kEncryptReq;
    return start_tls(params);
}

Status Connection::start_tls(const ConnectParams& params) {
    net::TlsOptions options;
    options.host = params.host;
    // Com so' o login cifrado o certificado nunca e' validado -- e' o
    // comportamento de todo cliente do SQL Server com Encrypt=false, e o
    // certificado autoassinado que o servidor gera sozinho nao passaria.
    const bool verify = params.encrypt && params.verify_certificate;
    options.allow_invalid_certificate = !verify;
    options.allow_host_mismatch       = !verify;

    // O aperto de mao viaja dentro de pacotes PRELOGIN.
    options.handshake_write = [this](std::span<const std::byte> token) {
        return send_message(kPacketPrelogin, token);
    };
    auto pending = std::make_shared<std::vector<std::byte>>();
    options.handshake_read = [this, pending](std::span<std::byte> room) -> Result<std::size_t> {
        if (pending->empty()) {
            std::byte header[8];
            OTTER_RETURN_IF_ERROR(socket_.read_exact(header));
            const std::size_t length = (static_cast<std::size_t>(header[2]) << 8) |
                                       static_cast<std::size_t>(header[3]);
            if (length < 8) return fail(Errc::protocol_error, "invalid TDS packet length");
            pending->resize(length - 8);
            if (!pending->empty()) OTTER_RETURN_IF_ERROR(socket_.read_exact(*pending));
        }
        const std::size_t count = std::min(room.size(), pending->size());
        std::memcpy(room.data(), pending->data(), count);
        pending->erase(pending->begin(), pending->begin() + static_cast<std::ptrdiff_t>(count));
        return count;
    };

    if (Status status = tls_.handshake(socket_, options); !status) {
        return std::unexpected(status.error().with_context("TLS"));
    }
    tls_on_ = true;
    return {};
}

// --- LOGIN ---------------------------------------------------------------------------

Status Connection::login(const ConnectParams& params) {
    net::SspiClient sspi;
    const std::string spn = "MSSQLSvc/" + net::fully_qualified_host(params.host) + ":" +
                            std::to_string(params.port);

    std::vector<std::byte> token;
    if (params.integrated) {
        OTTER_ASSIGN_OR_RETURN(token, sspi.step(spn, {}));
    } else if (params.user.empty()) {
        return fail(Errc::invalid_argument, "the user name is required");
    }

    login_encrypted_ = tls_on_;
    OTTER_RETURN_IF_ERROR(
        send_message(kPacketLogin7, build_login7(params, client_host_name(), token)));

    // "Encrypt=false": so' o pacote de login foi cifrado. O resto da conversa
    // -- a comecar pela resposta dele -- segue em claro.
    if (!tls_full_) tls_on_ = false;

    for (int round = 0; round < 8; ++round) {
        sspi_token_.clear();
        OTTER_RETURN_IF_ERROR(read_response({}, {}, /*login_phase=*/true));
        if (logged_in_) return {};

        // O servidor devolveu um desafio SSPI: responde e le de novo.
        if (sspi_token_.empty() || !params.integrated) break;
        OTTER_ASSIGN_OR_RETURN(token, sspi.step(spn, sspi_token_));
        // O ultimo passo do Negotiate so' CONFERE o token do servidor e nao
        // produz outro: nao ha' o que enviar, e o LOGINACK ja' vem a caminho.
        // Mandar um pacote SSPI vazio aqui fazia o servidor responder com um
        // DONE solto, que a primeira consulta lia como se fosse a resposta dela.
        if (token.empty()) continue;
        OTTER_RETURN_IF_ERROR(send_message(kPacketSspi, token));
    }
    return fail(Errc::auth_failed, "the server did not complete the login");
}

// --- Comandos ------------------------------------------------------------------------

Status Connection::query(std::string_view sql, const ColumnsCallback& on_columns,
                         const RowCallback& on_row) {
    if (!is_open()) return fail(Errc::closed, "connection is closed");

    // Um cancelamento pedido com a conexao parada: o servidor ja' respondeu
    // (ou vai responder) com um DONE de atencao, que precisa sair do fluxo
    // antes do proximo lote.
    if (attention_pending_->load(std::memory_order_acquire)) {
        (void)read_response({}, {}, /*login_phase=*/false);
    }

    OTTER_RETURN_IF_ERROR(send_message(kPacketBatch, build_batch(sql, transaction_)));
    return read_response(on_columns, on_row, /*login_phase=*/false);
}

Status Connection::cancel() {
    if (!is_open()) return fail(Errc::closed, "connection is closed");

    const std::lock_guard<std::mutex> lock(*send_mutex_);
    attention_pending_->store(true, std::memory_order_release);

    std::vector<std::byte> packet;
    packet.push_back(static_cast<std::byte>(kPacketAttention));
    packet.push_back(static_cast<std::byte>(kStatusEom));
    put_u16be(packet, 8);
    put_u16be(packet, 0);
    packet.push_back(static_cast<std::byte>(packet_id_++));
    packet.push_back(std::byte{0});
    return write_raw(packet);
}

std::vector<Message> Connection::take_messages() {
    std::vector<Message> out = std::move(messages_);
    messages_.clear();
    return out;
}

// --- Leitura do fluxo ----------------------------------------------------------------

bool Connection::read_type_info(TypeInfo& type, std::string& udt_name) {
    std::uint8_t id = 0;
    if (!read_u8(id)) return false;
    type = {};
    type.id = static_cast<TypeId>(id);

    std::uint8_t  byte = 0;
    std::uint16_t word = 0;
    std::string   ignored;

    const auto collation = [this, &type]() {
        std::uint32_t value = 0;
        std::uint8_t  sort = 0;
        if (!read_u32(value) || !read_u8(sort)) return false;
        type.collation = value;
        type.sort_id   = sort;
        return true;
    };
    // text, ntext e image trazem o nome da tabela de origem (em partes).
    const auto table_name = [this, &ignored]() {
        std::uint8_t parts = 0;
        if (!read_u8(parts)) return false;
        for (std::uint8_t i = 0; i < parts; ++i) {
            if (!read_us_varchar(ignored)) return false;
        }
        return true;
    };

    switch (type.id) {
        case TypeId::null_:     type.max_length = 0; return true;
        case TypeId::int1:
        case TypeId::bit:       type.max_length = 1; return true;
        case TypeId::int2:      type.max_length = 2; return true;
        case TypeId::int4:
        case TypeId::datetime4:
        case TypeId::float4:
        case TypeId::money4:    type.max_length = 4; return true;
        case TypeId::money:
        case TypeId::datetime:
        case TypeId::float8:
        case TypeId::int8:      type.max_length = 8; return true;

        case TypeId::guid:
        case TypeId::intn:
        case TypeId::bitn:
        case TypeId::floatn:
        case TypeId::moneyn:
        case TypeId::datetimen:
            if (!read_u8(byte)) return false;
            type.max_length = byte;
            return true;

        case TypeId::decimal:
        case TypeId::numeric:
        case TypeId::decimaln:
        case TypeId::numericn:
            if (!read_u8(byte)) return false;
            type.max_length = byte;
            return read_u8(type.precision) && read_u8(type.scale);

        case TypeId::daten:
            type.max_length = 3;
            return true;
        case TypeId::timen:
        case TypeId::datetime2n:
        case TypeId::datetimeoffsetn:
            return read_u8(type.scale);

        case TypeId::bigvarbinary:
        case TypeId::bigbinary:
            if (!read_u16(word)) return false;
            type.max_length = word;
            type.plp        = word == 0xFFFF;
            return true;

        case TypeId::bigvarchar:
        case TypeId::bigchar:
        case TypeId::nvarchar:
        case TypeId::nchar:
            if (!read_u16(word)) return false;
            type.max_length = word;
            type.plp        = word == 0xFFFF;
            return collation();

        case TypeId::image:
            return read_u32(type.max_length) && table_name();
        case TypeId::text:
        case TypeId::ntext:
            return read_u32(type.max_length) && collation() && table_name();

        case TypeId::variant:
            return read_u32(type.max_length);

        case TypeId::xml:
            type.plp = true;
            if (!read_u8(byte)) return false;
            if (byte != 0) {
                return read_b_varchar(ignored) && read_b_varchar(ignored) &&
                       read_us_varchar(ignored);
            }
            return true;

        case TypeId::udt:
            type.plp = true;
            if (!read_u16(word)) return false;
            type.max_length = word;
            return read_b_varchar(ignored) && read_b_varchar(ignored) &&
                   read_b_varchar(udt_name) && read_us_varchar(ignored);
    }

    char code[8];
    std::snprintf(code, sizeof code, "0x%02X", id);
    stream_error_ = fail(Errc::protocol_error, std::string("unknown TDS data type ") + code);
    return false;
}

bool Connection::read_columns(std::vector<Column>& columns) {
    std::uint16_t count = 0;
    if (!read_u16(count)) return false;
    // 0xFFFF: "sem metadados" -- as colunas anteriores continuam valendo.
    if (count == 0xFFFF) return true;

    columns.clear();
    columns.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        std::uint32_t user_type = 0;
        std::uint16_t flags = 0;
        Column column;
        std::string udt_name;
        if (!read_u32(user_type) || !read_u16(flags) ||
            !read_type_info(column.type, udt_name) || !read_b_varchar(column.name)) {
            return false;
        }
        column.nullable  = (flags & 0x0001) != 0;
        column.updatable = (flags & 0x000C) != 0;
        column.identity  = (flags & 0x0010) != 0;
        column.computed  = (flags & 0x0020) != 0;
        column.hidden    = (flags & 0x2000) != 0;
        column.key       = (flags & 0x4000) != 0;
        column.type_name = udt_name.empty() ? type_name(column.type) : udt_name;
        // rowversion chega como binary(8) com o tipo de usuario 80.
        if (user_type == 80) column.type_name = "timestamp";
        columns.push_back(std::move(column));
    }
    return true;
}

bool Connection::read_plp(std::string& bytes, bool& null) {
    std::uint64_t total = 0;
    if (!read_u64(total)) return false;
    if (total == 0xFFFFFFFFFFFFFFFFull) {
        null = true;
        return true;
    }
    bytes.clear();
    for (;;) {
        std::uint32_t chunk = 0;
        if (!read_u32(chunk)) return false;
        if (chunk == 0) break;
        const std::byte* p = nullptr;
        if (!take(chunk, p)) return false;
        bytes.append(reinterpret_cast<const char*>(p), chunk);
    }
    return true;
}

bool Connection::read_value(const TypeInfo& type, std::string& text, bool& null) {
    null = false;
    text.clear();
    const std::byte* p = nullptr;

    if (type.plp) {
        std::string bytes;
        if (!read_plp(bytes, null)) return false;
        if (!null) {
            text = format_value(type, {reinterpret_cast<const std::byte*>(bytes.data()),
                                       bytes.size()});
        }
        return true;
    }

    switch (type.id) {
        case TypeId::null_:
            null = true;
            return true;

        // Tamanho fixo: sem prefixo.
        case TypeId::int1:
        case TypeId::bit:
        case TypeId::int2:
        case TypeId::int4:
        case TypeId::int8:
        case TypeId::datetime4:
        case TypeId::datetime:
        case TypeId::float4:
        case TypeId::float8:
        case TypeId::money4:
        case TypeId::money:
            if (!take(type.max_length, p)) return false;
            text = format_value(type, {p, type.max_length});
            return true;

        // Um byte de tamanho; zero e' NULL.
        case TypeId::guid:
        case TypeId::intn:
        case TypeId::bitn:
        case TypeId::floatn:
        case TypeId::moneyn:
        case TypeId::datetimen:
        case TypeId::decimal:
        case TypeId::numeric:
        case TypeId::decimaln:
        case TypeId::numericn:
        case TypeId::daten:
        case TypeId::timen:
        case TypeId::datetime2n:
        case TypeId::datetimeoffsetn: {
            std::uint8_t length = 0;
            if (!read_u8(length)) return false;
            if (length == 0) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            text = format_value(type, {p, length});
            return true;
        }

        // Dois bytes de tamanho; 0xFFFF e' NULL.
        case TypeId::bigvarbinary:
        case TypeId::bigbinary:
        case TypeId::bigvarchar:
        case TypeId::bigchar:
        case TypeId::nvarchar:
        case TypeId::nchar: {
            std::uint16_t length = 0;
            if (!read_u16(length)) return false;
            if (length == 0xFFFF) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            text = format_value(type, {p, length});
            return true;
        }

        // text, ntext, image: ponteiro de texto, timestamp, e so' entao o dado.
        case TypeId::image:
        case TypeId::text:
        case TypeId::ntext: {
            std::uint8_t pointer = 0;
            if (!read_u8(pointer)) return false;
            if (pointer == 0) {
                null = true;
                return true;
            }
            std::uint32_t length = 0;
            if (!skip(static_cast<std::size_t>(pointer) + 8) || !read_u32(length) ||
                !take(length, p)) {
                return false;
            }
            text = format_value(type, {p, length});
            return true;
        }

        case TypeId::variant: {
            std::uint32_t length = 0;
            if (!read_u32(length)) return false;
            if (length == 0) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            text = format_variant({p, length});
            return true;
        }

        case TypeId::xml:
        case TypeId::udt:
            break;   // sempre PLP, tratado acima
    }
    stream_error_ = fail(Errc::protocol_error, "value of an unexpected TDS type");
    return false;
}

bool Connection::read_message(Message& message) {
    std::uint16_t length = 0;
    std::uint32_t number = 0, line = 0;
    std::string server;
    if (!read_u16(length) || !read_u32(number) || !read_u8(message.state) ||
        !read_u8(message.severity) || !read_us_varchar(message.text) ||
        !read_b_varchar(server) || !read_b_varchar(message.procedure) || !read_u32(line)) {
        return false;
    }
    message.number = static_cast<std::int32_t>(number);
    message.line   = static_cast<std::int32_t>(line);
    return true;
}

Status Connection::read_response(const ColumnsCallback& on_columns,
                                 const RowCallback& on_row, bool login_phase) {
    inbox_.clear();
    in_pos_ = 0;
    message_complete_ = false;
    stream_error_ = {};
    if (!login_phase) affected_rows_ = -1;

    std::vector<Column>      columns;
    std::vector<std::string> texts;
    std::vector<Value>       values;
    std::optional<Message>   first_error;
    bool cancelled = false;
    bool statement_has_columns = false;

    const auto emit_row = [&]() {
        if (!on_row) return;
        for (std::size_t c = 0; c < columns.size(); ++c) {
            if (!values[c].null) values[c].text = texts[c];
        }
        on_row(values);
    };

    for (;;) {
        std::uint8_t token = 0;
        if (!read_u8(token)) return stream_error_;

        switch (token) {
            case kTokenColMetadata:
                if (!read_columns(columns)) return stream_error_;
                texts.assign(columns.size(), {});
                values.assign(columns.size(), {});
                statement_has_columns = true;
                if (on_columns) on_columns(columns);
                break;

            case kTokenRow:
                for (std::size_t c = 0; c < columns.size(); ++c) {
                    if (!read_value(columns[c].type, texts[c], values[c].null)) {
                        return stream_error_;
                    }
                }
                emit_row();
                break;

            case kTokenNbcRow: {
                // Os NULL vem num mapa de bits na frente; so' os outros
                // valores estao na linha.
                const std::size_t bytes = (columns.size() + 7) / 8;
                const std::byte* p = nullptr;
                if (!take(bytes, p)) return stream_error_;
                const std::vector<std::byte> bitmap(p, p + bytes);
                for (std::size_t c = 0; c < columns.size(); ++c) {
                    const bool is_null =
                        (static_cast<std::uint8_t>(bitmap[c / 8]) & (1u << (c % 8))) != 0;
                    if (is_null) {
                        values[c].null = true;
                        texts[c].clear();
                    } else if (!read_value(columns[c].type, texts[c], values[c].null)) {
                        return stream_error_;
                    }
                }
                emit_row();
                break;
            }

            case kTokenDone:
            case kTokenDoneProc:
            case kTokenDoneInProc: {
                std::uint16_t status = 0, command = 0;
                std::uint64_t rows = 0;
                if (!read_u16(status) || !read_u16(command) || !read_u64(rows)) {
                    return stream_error_;
                }
                // A contagem de um SELECT e' o numero de linhas LIDAS, nao
                // alteradas: so' entra a de comando sem conjunto de resultado.
                if ((status & kDoneCount) != 0 && !statement_has_columns && !login_phase) {
                    affected_rows_ = (affected_rows_ < 0 ? 0 : affected_rows_) +
                                     static_cast<std::int64_t>(rows);
                }
                statement_has_columns = false;
                if ((status & kDoneAttn) != 0) {
                    cancelled = true;
                    attention_pending_->store(false, std::memory_order_release);
                }
                break;
            }

            case kTokenError: {
                Message message;
                if (!read_message(message)) return stream_error_;
                if (!first_error) first_error = std::move(message);
                break;
            }
            case kTokenInfo: {
                Message message;
                if (!read_message(message)) return stream_error_;
                // "Changed database context" e "Changed language setting":
                // ruido de todo login e de todo USE.
                if (message.number != 5701 && message.number != 5703) {
                    messages_.push_back(std::move(message));
                }
                break;
            }

            case kTokenEnvChange: {
                std::uint16_t length = 0;
                const std::byte* p = nullptr;
                if (!read_u16(length) || !take(length, p)) return stream_error_;
                if (length == 0) break;
                const std::span<const std::byte> body{p, length};
                const auto type = static_cast<std::uint8_t>(body[0]);
                // Valor novo: 1 byte de tamanho e o dado.
                const std::size_t size = length > 1 ? static_cast<std::size_t>(body[1]) : 0;
                switch (type) {
                    case 1:   // banco
                        if (2 + size * 2 <= body.size()) {
                            database_ = utf16le_to_utf8(body.subspan(2, size * 2));
                        }
                        break;
                    case 4:   // tamanho do pacote, em texto
                        if (2 + size * 2 <= body.size()) {
                            const std::string text = utf16le_to_utf8(body.subspan(2, size * 2));
                            const long value = std::strtol(text.c_str(), nullptr, 10);
                            if (value >= 512 && value <= 32767) {
                                packet_size_ = static_cast<std::uint32_t>(value);
                            }
                        }
                        break;
                    case 8:    // BEGIN TRANSACTION: o descritor novo
                    case 11:   // alistada numa transacao distribuida
                        transaction_ = 0;
                        if (size == 8 && 2 + size <= body.size()) {
                            for (std::size_t i = 0; i < 8; ++i) {
                                transaction_ |=
                                    static_cast<std::uint64_t>(body[2 + i]) << (8 * i);
                            }
                        }
                        break;
                    case 9:    // COMMIT
                    case 10:   // ROLLBACK
                    case 12:   // saiu da transacao distribuida
                    case 17:   // transacao encerrada pelo servidor
                        transaction_ = 0;
                        break;
                    default:
                        break;
                }
                break;
            }

            case kTokenLoginAck: {
                std::uint16_t length = 0;
                const std::byte* p = nullptr;
                if (!read_u16(length) || !take(length, p)) return stream_error_;
                const std::span<const std::byte> body{p, length};
                // interface(1), versao TDS(4), nome do programa, versao(4).
                if (body.size() >= 6) {
                    const std::size_t name = static_cast<std::size_t>(body[5]) * 2;
                    if (6 + name + 4 <= body.size()) {
                        server_name_ = utf16le_to_utf8(body.subspan(6, name));
                        const std::size_t v = 6 + name;
                        version_major_ = static_cast<int>(body[v]);
                        char text[48];
                        std::snprintf(text, sizeof text, "%u.%u.%u",
                                      static_cast<unsigned>(body[v]),
                                      static_cast<unsigned>(body[v + 1]),
                                      (static_cast<unsigned>(body[v + 2]) << 8) |
                                          static_cast<unsigned>(body[v + 3]));
                        server_version_ = text;
                    }
                }
                logged_in_ = true;
                break;
            }

            case kTokenSspi: {
                std::uint16_t length = 0;
                const std::byte* p = nullptr;
                if (!read_u16(length) || !take(length, p)) return stream_error_;
                sspi_token_.assign(p, p + length);
                break;
            }

            case kTokenReturnStatus:
                if (!skip(4)) return stream_error_;
                break;

            case kTokenReturnValue: {
                // Parametro de saida de uma chamada RPC. Nao usamos RPC, mas o
                // servidor pode manda-lo: lido para o fluxo seguir alinhado.
                std::uint16_t ordinal = 0, flags = 0;
                std::uint32_t user_type = 0;
                std::uint8_t  status = 0;
                std::string name, udt, text;
                TypeInfo type;
                bool null = false;
                if (!read_u16(ordinal) || !read_b_varchar(name) || !read_u8(status) ||
                    !read_u32(user_type) || !read_u16(flags) || !read_type_info(type, udt) ||
                    !read_value(type, text, null)) {
                    return stream_error_;
                }
                break;
            }

            case kTokenOrder:
            case kTokenTabName:
            case kTokenColInfo: {
                std::uint16_t length = 0;
                if (!read_u16(length) || !skip(length)) return stream_error_;
                break;
            }

            case kTokenFeatureAck:
                for (;;) {
                    std::uint8_t feature = 0;
                    std::uint32_t length = 0;
                    if (!read_u8(feature)) return stream_error_;
                    if (feature == 0xFF) break;
                    if (!read_u32(length) || !skip(length)) return stream_error_;
                }
                break;

            case kTokenFedAuthInfo: {
                std::uint32_t length = 0;
                if (!read_u32(length) || !skip(length)) return stream_error_;
                break;
            }

            default: {
                char code[8];
                std::snprintf(code, sizeof code, "0x%02X", token);
                return fail(Errc::protocol_error,
                            std::string("unknown TDS token ") + code);
            }
        }

        if (at_message_end()) {
            // O servidor SEMPRE confirma um pedido de atencao com um DONE
            // proprio; se o lote terminou antes de o pedido chegar, a
            // confirmacao vem numa mensagem a seguir.
            if (attention_pending_->load(std::memory_order_acquire) && !login_phase) {
                inbox_.clear();
                in_pos_ = 0;
                message_complete_ = false;
                continue;
            }
            break;
        }
    }

    if (first_error) return std::unexpected(to_error(*first_error));
    if (cancelled) return fail(Errc::cancelled, "the query was cancelled");
    return {};
}

} // namespace otter::tdswire
