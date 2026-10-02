#include "orawire/connection.hpp"

#include "orawire/auth.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#  include <process.h>
#else
#  include <unistd.h>
#endif

namespace otter::orawire {
namespace {

// --- TNS: o envelope ------------------------------------------------------------

constexpr std::uint8_t kPacketConnect  = 1;
constexpr std::uint8_t kPacketAccept   = 2;
constexpr std::uint8_t kPacketRefuse   = 4;
constexpr std::uint8_t kPacketRedirect = 5;
constexpr std::uint8_t kPacketData     = 6;
constexpr std::uint8_t kPacketResend   = 11;
constexpr std::uint8_t kPacketMarker   = 12;
constexpr std::uint8_t kPacketControl  = 14;

constexpr std::uint8_t kMarkerReset     = 2;
constexpr std::uint8_t kMarkerInterrupt = 3;

constexpr std::uint16_t kVersionDesired  = 319;
constexpr std::uint16_t kVersionMinimum  = 300;
constexpr std::uint16_t kVersionLargeSdu = 315;   // 12.1: o minimo que se aceita
constexpr std::uint16_t kDataFlagsEof    = 0x0040;

// O descritor cabe no pacote CONNECT ate' este tamanho; maior, vai num DATA.
constexpr std::size_t kMaxConnectData = 230;

// --- TTC: as mensagens ----------------------------------------------------------

constexpr std::uint8_t kMsgProtocol         = 1;
constexpr std::uint8_t kMsgDataTypes        = 2;
constexpr std::uint8_t kMsgFunction         = 3;
constexpr std::uint8_t kMsgError            = 4;
constexpr std::uint8_t kMsgRowHeader        = 6;
constexpr std::uint8_t kMsgRowData          = 7;
constexpr std::uint8_t kMsgParameter        = 8;
constexpr std::uint8_t kMsgStatus           = 9;
constexpr std::uint8_t kMsgIoVector         = 11;
constexpr std::uint8_t kMsgWarning          = 15;
constexpr std::uint8_t kMsgDescribeInfo     = 16;
constexpr std::uint8_t kMsgPiggyback        = 17;
constexpr std::uint8_t kMsgFlushOutBinds    = 19;
constexpr std::uint8_t kMsgBitVector        = 21;
constexpr std::uint8_t kMsgServerPiggyback  = 23;
constexpr std::uint8_t kMsgImplicitResults  = 27;
constexpr std::uint8_t kMsgEndOfResponse    = 29;

constexpr std::uint8_t kFuncFetch        = 5;
constexpr std::uint8_t kFuncLogoff       = 9;
constexpr std::uint8_t kFuncCommit       = 14;
constexpr std::uint8_t kFuncRollback     = 15;
constexpr std::uint8_t kFuncExecute      = 94;
constexpr std::uint8_t kFuncCloseCursors = 105;
constexpr std::uint8_t kFuncAuthPhaseTwo = 115;
constexpr std::uint8_t kFuncAuthPhaseOne = 118;
constexpr std::uint8_t kFuncPing         = 147;

constexpr std::uint32_t kExecParse    = 0x01;
constexpr std::uint32_t kExecDefine   = 0x10;
constexpr std::uint32_t kExecExecute  = 0x20;
constexpr std::uint32_t kExecFetch    = 0x40;
constexpr std::uint32_t kExecCommit   = 0x100;
constexpr std::uint32_t kExecNotPlsql = 0x8000;
constexpr std::uint32_t kExecFlagImplicitResults = 0x8000;

constexpr std::uint32_t kAuthLogon        = 0x001;
constexpr std::uint32_t kAuthWithPassword = 0x100;

constexpr std::uint32_t kCallStatusInTransaction = 0x2;

constexpr std::uint32_t kErrNoDataFound = 1403;   // fim das linhas, nao e' erro

constexpr std::uint8_t  kLongLength = 254;        // o valor vem em pedacos
constexpr std::uint8_t  kNullLength = 255;
constexpr std::size_t   kMaxShortLength = 252;
constexpr std::size_t   kChunkSize = 32767;
constexpr std::uint32_t kMaxLongLength = 0x7FFFFFFF;
constexpr std::uint32_t kLobPrefetchFlag = 0x2000000;

constexpr std::uint16_t kCharsetUtf8 = 873;       // AL32UTF8

// Versao dos campos do TTC que o cliente declara: 19.1. O servidor nunca
// manda campos mais novos que o declarado -- declarar o do 23ai obrigaria a
// ler anotacoes de coluna, dominios e vetores, que este driver nao usa. Se o
// servidor for mais velho, vale o dele.
constexpr std::uint8_t kFieldVersion19     = 13;
constexpr std::uint8_t kFieldVersion12_2   = 8;
constexpr std::uint8_t kFieldVersion18Ext1 = 11;
constexpr std::uint8_t kFieldVersion20     = 14;

constexpr std::uint32_t kPrefetchRows = 200;
constexpr std::uint32_t kFetchRows    = 500;

// A tabela de tipos da negociacao (gerada: tools/ora_datatypes.py).
struct DataType {
    std::uint16_t type;
    std::uint16_t conversion;
    std::uint16_t representation;
};
constexpr DataType kDataTypes[] = {
#include "orawire/data_types.inc"
};

// --- escrita --------------------------------------------------------------------

void w_u8(std::vector<std::byte>& out, unsigned value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
}
void w_u16be(std::vector<std::byte>& out, unsigned value) {
    w_u8(out, value >> 8);
    w_u8(out, value);
}
void w_u16le(std::vector<std::byte>& out, unsigned value) {
    w_u8(out, value);
    w_u8(out, value >> 8);
}
void w_u32be(std::vector<std::byte>& out, std::uint32_t value) {
    w_u16be(out, value >> 16);
    w_u16be(out, value & 0xFFFF);
}
void w_zeros(std::vector<std::byte>& out, std::size_t count) {
    out.insert(out.end(), count, std::byte{0});
}
void w_raw(std::vector<std::byte>& out, std::string_view text) {
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), data, data + text.size());
}

// Inteiro no formato "universal" do TTC: um byte de tamanho e so' os bytes
// significativos, do mais alto para o mais baixo. Zero e' o byte 0.
void w_ub8(std::vector<std::byte>& out, std::uint64_t value) {
    unsigned size = 0;
    for (std::uint64_t v = value; v != 0; v >>= 8) ++size;
    w_u8(out, size);
    for (unsigned i = size; i > 0; --i) w_u8(out, static_cast<unsigned>(value >> (8 * (i - 1))));
}
void w_ub4(std::vector<std::byte>& out, std::uint32_t value) { w_ub8(out, value); }
void w_ub2(std::vector<std::byte>& out, std::uint16_t value) { w_ub8(out, value); }

// Bytes com tamanho: um byte ate' 252; acima disso, o marcador 254 e pedacos,
// cada um com o tamanho na frente, fechados por um pedaco de tamanho zero.
void w_bytes(std::vector<std::byte>& out, std::string_view data) {
    if (data.size() <= kMaxShortLength) {
        w_u8(out, static_cast<unsigned>(data.size()));
        w_raw(out, data);
        return;
    }
    w_u8(out, kLongLength);
    while (!data.empty()) {
        const std::size_t chunk = (std::min)(data.size(), kChunkSize);
        w_ub4(out, static_cast<std::uint32_t>(chunk));
        w_raw(out, data.substr(0, chunk));
        data.remove_prefix(chunk);
    }
    w_ub4(out, 0);
}

// O tamanho duas vezes: o do "ponteiro" e o do conteudo.
void w_bytes_two_lengths(std::vector<std::byte>& out, std::string_view data) {
    w_ub4(out, static_cast<std::uint32_t>(data.size()));
    if (!data.empty()) w_bytes(out, data);
}

void w_key_value(std::vector<std::byte>& out, std::string_view key, std::string_view value,
                 std::uint32_t flags = 0) {
    w_bytes_two_lengths(out, key);
    w_bytes_two_lengths(out, value);
    w_ub4(out, flags);
}

std::uint16_t be16(std::span<const std::byte> data, std::size_t at) {
    return static_cast<std::uint16_t>((std::to_integer<unsigned>(data[at]) << 8) |
                                      std::to_integer<unsigned>(data[at + 1]));
}
std::uint32_t be32(std::span<const std::byte> data, std::size_t at) {
    return (static_cast<std::uint32_t>(be16(data, at)) << 16) | be16(data, at + 2);
}

// --- quem somos, para o V$SESSION -----------------------------------------------

std::string environment(const char* first, const char* second, const char* fallback) {
    for (const char* name : {first, second}) {
        if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
            return value;
        }
    }
    return fallback;
}

std::string process_id() {
#ifdef _WIN32
    return std::to_string(_getpid());
#else
    return std::to_string(getpid());
#endif
}

// "+02:00": o fuso da maquina, para o servidor mostrar TIMESTAMP WITH LOCAL
// TIME ZONE na hora de quem esta' olhando.
std::string local_time_zone() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    std::tm utc{};
#ifdef _WIN32
    localtime_s(&local, &now);
    gmtime_s(&utc, &now);
#else
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
#endif
    // mktime le `utc` como hora local: a diferenca e' o deslocamento. O
    // horario de verao tem de ser o mesmo nos dois, senao some uma hora.
    utc.tm_isdst = local.tm_isdst;
    const long offset = static_cast<long>(std::difftime(std::mktime(&local), std::mktime(&utc))) / 60;
    char text[16];
    std::snprintf(text, sizeof(text), "%c%02ld:%02ld", offset < 0 ? '-' : '+',
                  (offset < 0 ? -offset : offset) / 60, (offset < 0 ? -offset : offset) % 60);
    return text;
}

// OTTER_ORA_TRACE=1 despeja cada pacote em hexadecimal no stderr. Sem
// especificacao do protocolo, comparar os bytes com os de outro cliente e' o
// unico jeito de achar um campo fora do lugar (docs/ORACLE-MAP.md).
void trace_packet(const char* direction, std::uint8_t type, std::span<const std::byte> body) {
    static const bool enabled = std::getenv("OTTER_ORA_TRACE") != nullptr;
    if (!enabled) return;

    std::fprintf(stderr, "%s tipo %u, %zu bytes\n", direction, type, body.size());
    for (std::size_t i = 0; i < body.size(); i += 16) {
        std::fprintf(stderr, "  %04zx ", i);
        for (std::size_t j = i; j < i + 16; ++j) {
            if (j < body.size()) std::fprintf(stderr, "%02x ", std::to_integer<unsigned>(body[j]));
            else                 std::fprintf(stderr, "   ");
        }
        std::fprintf(stderr, " ");
        for (std::size_t j = i; j < i + 16 && j < body.size(); ++j) {
            const unsigned c = std::to_integer<unsigned>(body[j]);
            std::fputc(c >= 32 && c < 127 ? static_cast<int>(c) : '.', stderr);
        }
        std::fputc('\n', stderr);
    }
}

bool is_character_type(OraType type) {
    return type == OraType::varchar || type == OraType::char_ || type == OraType::long_ ||
           type == OraType::clob;
}

// As colunas que so' vem inteiras se o cliente as "definir": o LOB chega como
// localizador (um ponteiro, sem o conteudo) e precisaria de uma ida ao
// servidor por valor. Definido como LONG, o servidor manda o texto direto.
bool needs_define(OraType type) {
    return type == OraType::clob || type == OraType::blob || type == OraType::json ||
           type == OraType::vector;
}

OraType fetch_type_for(OraType type) {
    if (type == OraType::clob) return OraType::long_;
    if (type == OraType::blob) return OraType::long_raw;
    return type;
}

} // namespace

// O estado de uma chamada: o que a resposta vai preenchendo.
struct Connection::Call {
    enum class Kind : std::uint8_t { simple, auth, query };
    Kind kind = Kind::simple;

    const ColumnsCallback* on_columns = nullptr;
    const RowCallback*     on_row = nullptr;

    std::vector<Column>  columns;
    std::vector<OraType> fetch_types;     // como cada coluna CHEGA no fio

    // A linha anterior fica guardada: o servidor nao repete a coluna que nao
    // mudou, so' marca no vetor de bits.
    std::vector<std::string> texts;
    std::vector<char>        nulls;
    std::vector<Value>       values;
    std::vector<std::byte>   bit_vector;
    bool                     has_bit_vector = false;

    std::uint32_t error_number = 0;
    std::string   error_message;
    std::uint16_t cursor_id = 0;
    std::uint64_t row_count = 0;
    std::uint32_t call_status = 0;

    // Logon: os pares que o servidor devolve.
    std::map<std::string, std::string, std::less<>> parameters;
    std::uint32_t verifier_type = 0;
};

Connection::Connection() : send_mutex_(std::make_unique<std::mutex>()) {}
Connection::~Connection() { close(); }
Connection::Connection(Connection&&) noexcept            = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

std::string Connection::session_value(std::string_view key) const {
    const auto found = session_.find(key);
    return found == session_.end() ? std::string{} : found->second;
}

// --- Conexao ----------------------------------------------------------------------

Result<Connection> Connection::connect(const ConnectParams& params) {
    if (const Status status = net::initialize_network(); !status) {
        return std::unexpected(status.error());
    }
    if (params.service_name.empty() && params.sid.empty()) {
        return fail(Errc::invalid_argument,
                    "Oracle needs a service name (the Database field), e.g. FREEPDB1");
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

    OTTER_RETURN_IF_ERROR(connection.handshake(params));
    OTTER_RETURN_IF_ERROR(connection.negotiate());
    OTTER_RETURN_IF_ERROR(connection.authenticate(params));

    // Dai' em diante uma consulta pode demorar o que demorar: o tempo limite
    // era o da CONEXAO, nao o de cada leitura.
    connection.socket_.set_read_timeout(std::chrono::milliseconds(0));
    return connection;
}

void Connection::close() noexcept {
    if (!socket_.is_open()) return;
    if (!failed_) {
        // Despedida por educacao: sem ela o servidor registra a sessao como
        // perdida. Com prazo curto -- fechar nao pode travar o programa.
        socket_.set_read_timeout(std::chrono::milliseconds(2000));
        (void)simple_call(kFuncLogoff);
    }
    socket_.close();
}

// --- Transporte -------------------------------------------------------------------

Status Connection::send_packet(std::uint8_t type, std::span<const std::byte> body) {
    std::vector<std::byte> packet;
    packet.reserve(body.size() + 8);
    const std::size_t size = body.size() + 8;
    if (large_sdu_) {
        w_u32be(packet, static_cast<std::uint32_t>(size));
    } else {
        w_u16be(packet, static_cast<unsigned>(size));
        w_u16be(packet, 0);          // soma de verificacao: nao usada
    }
    w_u8(packet, type);
    w_u8(packet, 0);                 // flags
    w_u16be(packet, 0);              // soma do cabecalho: nao usada
    packet.insert(packet.end(), body.begin(), body.end());
    trace_packet(">>", type, body);

    const std::lock_guard<std::mutex> lock(*send_mutex_);
    return socket_.write_all(packet);
}

// Uma mensagem TTC, partida em quantos pacotes DATA o SDU exigir.
Status Connection::send_data(std::span<const std::byte> payload) {
    const std::size_t room = sdu_ - 8 - 2;   // cabecalho e flags de dados
    do {
        const std::size_t chunk = (std::min)(payload.size(), room);
        std::vector<std::byte> body;
        body.reserve(chunk + 2);
        w_u16be(body, 0);            // flags de dados
        body.insert(body.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(chunk));
        OTTER_RETURN_IF_ERROR(send_packet(kPacketData, body));
        payload = payload.subspan(chunk);
    } while (!payload.empty());
    return {};
}

Status Connection::send_marker(std::uint8_t marker) {
    const std::byte body[3] = {std::byte{1}, std::byte{0}, static_cast<std::byte>(marker)};
    return send_packet(kPacketMarker, body);
}

Status Connection::read_packet(std::uint8_t& type, std::vector<std::byte>& body) {
    std::byte header[8];
    OTTER_RETURN_IF_ERROR(socket_.read_exact(header));

    const std::size_t size = large_sdu_ ? be32(header, 0) : be16(header, 0);
    if (size < 8 || size > 16u * 1024 * 1024) {
        return fail(Errc::protocol_error, "Oracle: invalid packet size");
    }
    type = std::to_integer<std::uint8_t>(header[4]);
    body.resize(size - 8);
    if (!body.empty()) OTTER_RETURN_IF_ERROR(socket_.read_exact(body));
    trace_packet("<<", type, body);
    return {};
}

Status Connection::cancel() {
    if (!socket_.is_open()) return fail(Errc::closed, "connection closed");
    // O servidor responde com um marcador; quem esta' lendo a resposta faz a
    // troca de "reset" (fill) e recebe o ORA-01013.
    return send_marker(kMarkerInterrupt);
}

// --- CONNECT ----------------------------------------------------------------------

Status Connection::handshake(const ConnectParams& params) {
    const std::string descriptor =
        connect_descriptor(params, environment("COMPUTERNAME", "HOSTNAME", "localhost"),
                           environment("USERNAME", "USER", "unknown"));
    const bool separate = descriptor.size() > kMaxConnectData;

    std::vector<std::byte> body;
    w_u16be(body, kVersionDesired);
    w_u16be(body, kVersionMinimum);
    w_u16be(body, 0x0001);                       // opcoes de servico: nenhuma
    w_u16be(body, sdu_);                         // SDU
    w_u16be(body, sdu_);                         // TDU
    w_u16be(body, 0x4f98);                       // caracteristicas do protocolo
    w_u16be(body, 0);                            // "line turnaround"
    w_u16be(body, 1);                            // ordem dos bytes
    w_u16be(body, static_cast<unsigned>(descriptor.size()));
    w_u16be(body, 74);                           // onde o descritor comeca
    w_u32be(body, 0);                            // maximo a receber
    // 0x80: aceita renegociacao; 0x04: SEM os servicos de rede nativos
    // (criptografia e checksum do Oracle Net), que este driver nao fala.
    w_u8(body, 0x84);
    w_u8(body, 0x84);
    w_zeros(body, 24);
    w_u32be(body, sdu_);                         // SDU e TDU, em 32 bits
    w_u32be(body, sdu_);
    w_u32be(body, 0);
    w_u32be(body, 0);
    if (!separate) w_raw(body, descriptor);

    for (int attempt = 0; attempt < 4; ++attempt) {
        OTTER_RETURN_IF_ERROR(send_packet(kPacketConnect, body));
        if (separate) {
            std::vector<std::byte> data;
            w_u16be(data, 0);
            w_raw(data, descriptor);
            OTTER_RETURN_IF_ERROR(send_packet(kPacketData, data));
        }

        std::uint8_t type = 0;
        std::vector<std::byte> reply;
        OTTER_RETURN_IF_ERROR(read_packet(type, reply));

        if (type == kPacketResend) continue;

        if (type == kPacketAccept) {
            if (reply.size() < 28) return fail(Errc::protocol_error, "Oracle: short ACCEPT");
            const std::uint16_t version = be16(reply, 0);
            if (version < kVersionLargeSdu) {
                return fail(Errc::not_supported,
                            "this Oracle server is older than 12.1, the oldest supported");
            }
            if ((std::to_integer<unsigned>(reply[14]) & 0x10) != 0) {
                return fail(Errc::not_supported,
                            "the server requires Oracle Native Network Encryption, "
                            "which is not supported");
            }
            sdu_ = (std::clamp)(be32(reply, 24), std::uint32_t{512}, std::uint32_t{65535});
            large_sdu_ = true;
            return {};
        }

        if (type == kPacketRefuse) {
            std::string message;
            if (reply.size() >= 4) {
                const std::size_t size = (std::min<std::size_t>)(be16(reply, 2), reply.size() - 4);
                message.assign(reinterpret_cast<const char*>(reply.data()) + 4, size);
            }
            // "(DESCRIPTION=(TMP=)(VSNNUM=...)(ERR=12514)(ERROR_STACK=...))"
            int code = 0;
            if (const std::size_t at = message.find("(ERR="); at != std::string::npos) {
                code = std::atoi(message.c_str() + at + 5);
            }
            const std::string name = !params.service_name.empty() ? params.service_name
                                                                  : params.sid;
            if (code == 12514) {
                return fail(Errc::connection_failed,
                            "ORA-12514: the listener does not know the service \"" + name +
                                "\"");
            }
            if (code == 12505) {
                return fail(Errc::connection_failed,
                            "ORA-12505: the listener does not know the SID \"" + name + "\"");
            }
            return fail(Errc::connection_failed,
                        "the Oracle listener refused the connection" +
                            (code != 0 ? " (ORA-" + std::to_string(code) + ")"
                                       : std::string{}));
        }

        if (type == kPacketRedirect) {
            return fail(Errc::not_supported,
                        "the Oracle listener redirected the connection (shared server or "
                        "RAC), which is not supported yet");
        }
        return fail(Errc::protocol_error,
                    "Oracle: unexpected packet " + std::to_string(type) + " during connect");
    }
    return fail(Errc::connection_failed, "the Oracle listener kept asking to resend");
}

// --- PROTOCOL e DATA TYPES --------------------------------------------------------

Status Connection::negotiate() {
    field_version_ = kFieldVersion19;

    Call call;

    // PROTOCOL: a versao do TTC e o nome do driver.
    {
        std::vector<std::byte> out;
        w_u8(out, kMsgProtocol);
        w_u8(out, 6);                // versao do protocolo (8.1 em diante)
        w_u8(out, 0);                // fim da lista de versoes
        w_raw(out, "C-Otter");
        w_u8(out, 0);

        packet_.clear();
        position_ = 0;
        OTTER_RETURN_IF_ERROR(send_data(out));

        if (r_u8() != kMsgProtocol && !failed_) {
            stream_fail("Oracle: the server did not answer the protocol message");
        }
        (void)r_u8();                // versao do servidor
        (void)r_u8();
        for (std::uint8_t c = r_u8(); c != 0 && !failed_; c = r_u8()) {
            server_banner_.push_back(static_cast<char>(c));
        }
        r_skip(2);                   // conjunto de caracteres do banco
        (void)r_u8();                // flags
        const unsigned elements = r_u16le();
        r_skip(elements * 5u);

        std::vector<std::byte> fdo(r_u16be());
        r_raw(fdo.data(), fdo.size());
        // O conjunto de caracteres NACIONAL fica dentro do FDO, depois de
        // dois blocos de tamanho variavel.
        if (fdo.size() >= 7) {
            const std::size_t at = 6 + std::to_integer<std::size_t>(fdo[5]) +
                                   std::to_integer<std::size_t>(fdo[6]);
            if (fdo.size() >= at + 5) national_charset_ = be16(fdo, at + 3);
        }

        std::string compile_caps;
        std::string runtime_caps;
        (void)r_bytes(compile_caps);
        (void)r_bytes(runtime_caps);
        if (failed_) return std::unexpected(failure_);

        // Vale a versao de campos mais baixa das duas pontas (indice 7).
        if (compile_caps.size() > 7) {
            server_field_version_ = static_cast<std::uint8_t>(compile_caps[7]);
            field_version_ = (std::min)(field_version_, server_field_version_);
        }
    }

    // DATA TYPES: o que o cliente sabe fazer e os tipos que entende.
    {
        // Capacidades "de compilacao". So' o que este driver realmente trata:
        // um bit ligado sem o codigo por tras faz o servidor mandar o que
        // ninguem vai saber ler.
        std::vector<std::byte> compile(55, std::byte{0});
        compile[0]  = std::byte{6};      // versao do SQL
        compile[4]  = std::byte{0xEA};   // tipos de logon: O5LOGON, O7, O8, O9
        compile[7]  = static_cast<std::byte>(field_version_);
        compile[8]  = std::byte{1};      // o servidor converte na definicao
        compile[9]  = std::byte{1};
        compile[15] = std::byte{0x29};   // vetor de bits rapido, status no fim da chamada
        compile[16] = std::byte{0x90};
        compile[17] = std::byte{3};      // versao do TDS (o do Oracle, nao o da Sybase)
        compile[18] = std::byte{7};      // versao do RPC
        compile[19] = std::byte{3};
        compile[21] = std::byte{1};
        compile[23] = std::byte{0xCF};   // LOB: tamanho em 8 bytes, pre-busca
        compile[26] = std::byte{0x04};
        compile[27] = std::byte{1};      // tipos em 2 bytes
        compile[31] = std::byte{0x10};
        compile[34] = std::byte{12};
        compile[37] = std::byte{0xB8};   // resultados implicitos, pedacos grandes
        compile[39] = std::byte{8};
        compile[42] = std::byte{0x05};

        std::vector<std::byte> runtime(11, std::byte{0});
        runtime[0] = std::byte{2};       // compatibilidade 8.1
        runtime[6] = std::byte{0x05};    // textos de ate' 32 K

        std::vector<std::byte> out;
        w_u8(out, kMsgDataTypes);
        w_u16le(out, kCharsetUtf8);      // o cliente fala UTF-8: o servidor converte
        w_u16le(out, kCharsetUtf8);
        w_u8(out, 0x03);                 // multibyte, com conversao de tamanho
        w_bytes(out, {reinterpret_cast<const char*>(compile.data()), compile.size()});
        w_bytes(out, {reinterpret_cast<const char*>(runtime.data()), runtime.size()});
        for (const DataType& type : kDataTypes) {
            w_u16be(out, type.type);
            w_u16be(out, type.conversion);
            w_u16be(out, type.representation);
            w_u16be(out, 0);
        }
        w_u16be(out, 0);

        packet_.clear();
        position_ = 0;
        OTTER_RETURN_IF_ERROR(send_data(out));

        if (r_u8() != kMsgDataTypes && !failed_) {
            stream_fail("Oracle: the server did not answer the data types message");
        }
        while (!failed_) {
            if (r_u16be() == 0) break;
            if (r_u16be() != 0) r_skip(4);       // tipo de conversao e representacao
        }
        if (failed_) return std::unexpected(failure_);
    }
    return {};
}

// --- AUTH -------------------------------------------------------------------------

Status Connection::authenticate(const ConnectParams& params) {
    const std::string machine = environment("COMPUTERNAME", "HOSTNAME", "localhost");
    const std::string os_user = environment("USERNAME", "USER", "unknown");

    const auto write_header = [&](std::vector<std::byte>& out, std::uint8_t function,
                                  std::uint32_t mode, std::uint32_t pairs) {
        write_call_header(out, function);
        w_u8(out, params.user.empty() ? 0 : 1);
        w_ub4(out, static_cast<std::uint32_t>(params.user.size()));
        w_ub4(out, mode);
        w_u8(out, 1);
        w_ub4(out, pairs);
        w_u8(out, 1);
        w_u8(out, 1);
        if (!params.user.empty()) w_bytes(out, params.user);
    };

    const auto auth_error = [](const Call& call) {
        // ORA-01017: usuario ou senha; ORA-28000: conta bloqueada.
        return fail(Errc::auth_failed, call.error_message.empty()
                                           ? "ORA-" + std::to_string(call.error_number)
                                           : call.error_message);
    };

    // Fase um: quem somos. O servidor devolve o desafio.
    Call first;
    first.kind = Call::Kind::auth;
    {
        std::vector<std::byte> out;
        write_header(out, kFuncAuthPhaseOne, kAuthLogon, 5);
        w_key_value(out, "AUTH_TERMINAL", "unknown");
        w_key_value(out, "AUTH_PROGRAM_NM", params.program);
        w_key_value(out, "AUTH_MACHINE", machine);
        w_key_value(out, "AUTH_PID", process_id());
        w_key_value(out, "AUTH_SID", os_user);
        OTTER_RETURN_IF_ERROR(run_call(out, first));
    }
    if (first.error_number != 0) return auth_error(first);

    const auto parameter = [](const Call& call, std::string_view key) -> std::string {
        const auto found = call.parameters.find(key);
        return found == call.parameters.end() ? std::string{} : found->second;
    };

    AuthChallenge challenge;
    challenge.verifier_type   = first.verifier_type;
    challenge.verifier_data   = parameter(first, "AUTH_VFR_DATA");
    challenge.server_key      = parameter(first, "AUTH_SESSKEY");
    challenge.combo_salt      = parameter(first, "AUTH_PBKDF2_CSK_SALT");
    challenge.verifier_rounds = static_cast<std::uint32_t>(
        std::strtoul(parameter(first, "AUTH_PBKDF2_VGEN_COUNT").c_str(), nullptr, 10));
    challenge.combo_rounds = static_cast<std::uint32_t>(
        std::strtoul(parameter(first, "AUTH_PBKDF2_SDER_COUNT").c_str(), nullptr, 10));
    if (challenge.server_key.empty()) {
        return fail(Errc::auth_failed, "Oracle: the server sent no logon challenge");
    }

    OTTER_ASSIGN_OR_RETURN(const AuthNonces nonces, make_nonces(challenge));
    OTTER_ASSIGN_OR_RETURN(const AuthAnswer answer,
                           answer_challenge(challenge, params.password, nonces));

    // Fase dois: a nossa metade da chave e a senha cifrada.
    Call second;
    second.kind = Call::Kind::auth;
    {
        const bool modern = challenge.verifier_type == kVerifier12c;
        const std::string time_zone =
            "ALTER SESSION SET TIME_ZONE='" + local_time_zone() + "'" + std::string(1, '\0');

        std::vector<std::byte> out;
        write_header(out, kFuncAuthPhaseTwo, kAuthLogon | kAuthWithPassword, modern ? 7 : 6);
        w_key_value(out, "AUTH_SESSKEY", answer.session_key, 1);
        if (modern) w_key_value(out, "AUTH_PBKDF2_SPEEDY_KEY", answer.speedy_key);
        w_key_value(out, "AUTH_PASSWORD", answer.password);
        w_key_value(out, "SESSION_CLIENT_CHARSET", std::to_string(kCharsetUtf8));
        w_key_value(out, "SESSION_CLIENT_DRIVER_NAME", "C-Otter thin");
        w_key_value(out, "SESSION_CLIENT_VERSION", "1048576");
        w_key_value(out, "AUTH_ALTER_SESSION", time_zone, 1);
        OTTER_RETURN_IF_ERROR(run_call(out, second));
    }
    if (second.error_number != 0) return auth_error(second);

    if (!server_proof_is_valid(answer.combo_key, parameter(second, "AUTH_SVR_RESPONSE"))) {
        return fail(Errc::auth_failed,
                    "Oracle: the server could not prove it knows the password");
    }

    session_ = std::move(second.parameters);
    const auto number = static_cast<std::uint32_t>(
        std::strtoul(session_value("AUTH_VERSION_NO").c_str(), nullptr, 10));
    server_version_ = format_server_version(number, field_version_ >= kFieldVersion18Ext1);
    version_major_ = static_cast<int>(number >> 24);
    return {};
}

// --- Chamadas ---------------------------------------------------------------------

// O cabecalho de uma funcao TTC. Os cursores a fechar vao ANTES, de carona:
// nao merecem uma ida ao servidor so' para eles.
void Connection::write_call_header(std::vector<std::byte>& out, std::uint8_t function) {
    const auto next_sequence = [this] {
        if (++sequence_ == 0) sequence_ = 1;
        return sequence_;
    };

    if (!cursors_to_close_.empty()) {
        w_u8(out, kMsgPiggyback);
        w_u8(out, kFuncCloseCursors);
        w_u8(out, next_sequence());
        w_u8(out, 1);
        w_ub4(out, static_cast<std::uint32_t>(cursors_to_close_.size()));
        for (const std::uint32_t cursor : cursors_to_close_) w_ub4(out, cursor);
        cursors_to_close_.clear();
    }
    w_u8(out, kMsgFunction);
    w_u8(out, function);
    w_u8(out, next_sequence());
}

Status Connection::run_call(std::span<const std::byte> payload, Call& call) {
    if (!socket_.is_open()) return fail(Errc::closed, "connection closed");
    if (failed_) return std::unexpected(failure_);

    packet_.clear();
    position_ = 0;
    OTTER_RETURN_IF_ERROR(send_data(payload));
    return read_response(call);
}

Status Connection::simple_call(std::uint8_t function) {
    std::vector<std::byte> out;
    write_call_header(out, function);

    Call call;
    OTTER_RETURN_IF_ERROR(run_call(out, call));
    if (call.error_number != 0) return fail(Errc::query_failed, call.error_message);
    return {};
}

Status Connection::commit()   { return simple_call(kFuncCommit); }
Status Connection::rollback() { return simple_call(kFuncRollback); }
Status Connection::ping()     { return simple_call(kFuncPing); }

Status Connection::query(std::string_view sql, const ColumnsCallback& on_columns,
                         const RowCallback& on_row) {
    affected_rows_ = -1;

    const StatementKind kind = classify_statement(sql);
    const bool is_query = kind == StatementKind::query;

    Call call;
    call.kind = Call::Kind::query;
    call.on_columns = on_columns ? &on_columns : nullptr;
    call.on_row     = on_row ? &on_row : nullptr;

    // `define`: a segunda ida de uma consulta com LOB -- so' diz como as
    // colunas devem chegar, sem reexecutar.
    const auto write_execute = [&](std::vector<std::byte>& out, bool define) {
        std::uint32_t options = 0;
        if (define) {
            options |= kExecDefine;
        } else {
            options |= kExecParse | kExecExecute;
            if (is_query) options |= kExecFetch;
        }
        if (kind != StatementKind::plsql) options |= kExecNotPlsql;
        if (auto_commit_) options |= kExecCommit;

        const std::uint32_t rows = is_query ? kPrefetchRows : 1;

        write_call_header(out, kFuncExecute);
        w_ub4(out, options);
        w_ub4(out, define ? call.cursor_id : 0);
        if (define) {
            w_u8(out, 0);
            w_ub4(out, 0);
        } else {
            w_u8(out, 1);                        // ha' texto de SQL
            w_ub4(out, static_cast<std::uint32_t>(sql.size()));
        }
        w_u8(out, 1);                            // o vetor de 13 inteiros abaixo
        w_ub4(out, 13);
        w_u8(out, 0);
        w_u8(out, 0);
        w_ub4(out, 0);                           // tamanho do buffer de pre-busca
        w_ub4(out, rows);                        // linhas de pre-busca
        w_ub4(out, kMaxLongLength);              // tamanho maximo de um LONG
        w_u8(out, 0);                            // sem variaveis de ligacao
        w_ub4(out, 0);
        w_zeros(out, 5);
        if (define) {
            w_u8(out, 1);
            w_ub4(out, static_cast<std::uint32_t>(call.columns.size()));
        } else {
            w_u8(out, 0);
            w_ub4(out, 0);
        }
        w_ub4(out, 0);                           // registro de notificacao
        w_u8(out, 0);
        w_u8(out, 1);
        w_u8(out, 0);
        w_ub4(out, 0);
        w_u8(out, 0);
        w_ub4(out, 0);
        w_ub4(out, 0);
        w_u8(out, 0);                            // contagens por linha de DML: nao
        w_ub4(out, 0);
        w_u8(out, 0);
        if (field_version_ >= kFieldVersion12_2) {
            w_u8(out, 0);                        // assinatura e id do SQL: nao
            w_ub4(out, 0);
            w_u8(out, 0);
            w_ub4(out, 0);
            w_u8(out, 0);
            if (field_version_ > kFieldVersion12_2) {
                w_u8(out, 0);
                w_ub4(out, 0);
            }
        }
        if (!define) {
            w_bytes(out, sql);
            w_ub4(out, 1);                       // [0] fazer o parse
        } else {
            w_ub4(out, 0);
        }
        // [1] quantas execucoes; numa consulta nova e' zero.
        w_ub4(out, is_query ? (define ? rows : 0) : 1);
        w_zeros(out, 5);                         // [2]..[6]
        w_ub4(out, is_query ? 1 : 0);            // [7] e' consulta
        w_ub4(out, 0);                           // [8]
        w_ub4(out, define ? 0 : kExecFlagImplicitResults);   // [9]
        w_zeros(out, 3);                         // [10]..[12]

        if (!define) return;
        for (std::size_t i = 0; i < call.columns.size(); ++i) {
            const Column& column = call.columns[i];
            OraType type = call.fetch_types[i];
            std::uint32_t buffer_size = column.buffer_size;
            std::uint32_t prefetch = 0;
            std::uint64_t continuation = 0;

            if (type == OraType::long_ || type == OraType::long_raw) {
                buffer_size = kMaxLongLength;
            } else if (type == OraType::rowid || type == OraType::urowid) {
                type = OraType::varchar;
                buffer_size = 5267;
            } else if (type == OraType::json) {
                continuation = kLobPrefetchFlag;
                buffer_size = prefetch = 32u * 1024 * 1024;
            } else if (type == OraType::vector) {
                continuation = kLobPrefetchFlag;
                buffer_size = prefetch = 1024 * 1024;
            }
            const std::uint8_t form =
                !is_character_type(type) ? std::uint8_t{0}
                : column.charset_form == kCharsetFormNational ? kCharsetFormNational
                                                              : kCharsetFormImplicit;

            w_u8(out, static_cast<unsigned>(type));
            w_u8(out, 1);                        // usa indicadores de nulo
            w_u8(out, 0);                        // precisao e escala: sempre zero
            w_u8(out, 0);
            w_ub4(out, buffer_size);
            w_ub4(out, 0);
            w_ub8(out, continuation);
            w_ub4(out, 0);
            w_ub2(out, 0);
            w_ub2(out, form != 0 ? kCharsetUtf8 : std::uint16_t{0});
            w_u8(out, form);
            w_ub4(out, prefetch);
            if (field_version_ >= kFieldVersion12_2) w_ub4(out, 0);
        }
    };

    const auto finish = [&](Status status) -> Status {
        // O cursor fica aberto no servidor ate' alguem fecha'-lo; sem isto,
        // ORA-01000 (maximo de cursores) depois de algumas centenas de
        // consultas.
        if (call.cursor_id != 0) cursors_to_close_.push_back(call.cursor_id);
        return status;
    };
    const auto server_error = [&]() -> Status {
        return fail(Errc::query_failed, call.error_message.empty()
                                            ? "ORA-" + std::to_string(call.error_number)
                                            : call.error_message);
    };

    {
        std::vector<std::byte> out;
        write_execute(out, /*define=*/false);
        OTTER_RETURN_IF_ERROR(run_call(out, call));
    }
    bool more = is_query && call.error_number == 0;
    if (call.error_number != 0 && !(is_query && call.error_number == kErrNoDataFound)) {
        return finish(server_error());
    }

    if (!is_query) {
        if (kind == StatementKind::dml) affected_rows_ = static_cast<std::int64_t>(call.row_count);
        return finish({});
    }

    if (std::ranges::any_of(call.columns,
                            [](const Column& column) { return needs_define(column.type); })) {
        for (std::size_t i = 0; i < call.columns.size(); ++i) {
            call.fetch_types[i] = fetch_type_for(call.columns[i].type);
        }
        std::vector<std::byte> out;
        write_execute(out, /*define=*/true);
        call.error_number = 0;
        if (const Status status = run_call(out, call); !status) return finish(status);
        if (call.error_number != 0 && call.error_number != kErrNoDataFound) {
            return finish(server_error());
        }
        more = call.error_number == 0;
    }

    while (more) {
        std::vector<std::byte> out;
        write_call_header(out, kFuncFetch);
        w_ub4(out, call.cursor_id);
        w_ub4(out, kFetchRows);

        call.error_number = 0;
        if (const Status status = run_call(out, call); !status) return finish(status);
        if (call.error_number == kErrNoDataFound) break;
        if (call.error_number != 0) return finish(server_error());
    }
    return finish({});
}

// --- Leitura do fluxo --------------------------------------------------------------

void Connection::stream_fail(std::string message) {
    if (failed_) return;
    failed_ = true;
    failure_ = Error{Errc::protocol_error, std::move(message)};
    // O fluxo perdeu o passo: nao ha' como achar o comeco da proxima mensagem.
    socket_.close();
}

// Carrega o proximo pacote DATA. Os outros tipos sao tratados aqui mesmo.
bool Connection::fill() {
    if (failed_) return false;
    bool reset_done = false;

    for (;;) {
        std::uint8_t type = 0;
        std::vector<std::byte> body;
        if (const Status status = read_packet(type, body); !status) {
            failed_ = true;
            failure_ = status.error();
            socket_.close();
            return false;
        }

        if (type == kPacketData) {
            if (body.size() < 2) { stream_fail("Oracle: short DATA packet"); return false; }
            if (be16(body, 0) == kDataFlagsEof) {
                failed_ = true;
                failure_ = Error{Errc::closed, "the Oracle server closed the session"};
                socket_.close();
                return false;
            }
            if (body.size() == 2) continue;
            packet_ = std::move(body);
            position_ = 2;
            return true;
        }

        if (type == kPacketMarker) {
            // O servidor interrompeu a resposta (erro ou cancelamento). O
            // combinado: o cliente manda "reset", le ate' o servidor devolver
            // "reset", e so' entao chega o pacote com o erro.
            if (reset_done) continue;
            if (const Status status = send_marker(kMarkerReset); !status) {
                failed_ = true;
                failure_ = status.error();
                return false;
            }
            while (!(type == kPacketMarker && body.size() >= 3 &&
                     std::to_integer<std::uint8_t>(body[2]) == kMarkerReset)) {
                if (const Status status = read_packet(type, body); !status) {
                    failed_ = true;
                    failure_ = status.error();
                    socket_.close();
                    return false;
                }
            }
            reset_done = true;
            continue;
        }

        if (type == kPacketControl) continue;

        stream_fail("Oracle: unexpected packet " + std::to_string(type));
        return false;
    }
}

std::uint8_t Connection::r_u8() {
    if (position_ >= packet_.size() && !fill()) return 0;
    return std::to_integer<std::uint8_t>(packet_[position_++]);
}

// Dois bytes crus. Em instrucoes separadas: a ordem de avaliacao dos operandos
// de `|` nao e' garantida, e os bytes sairiam trocados conforme o compilador.
std::uint16_t Connection::r_u16be() {
    const unsigned high = r_u8();
    const unsigned low  = r_u8();
    return static_cast<std::uint16_t>((high << 8) | low);
}

std::uint16_t Connection::r_u16le() {
    const unsigned low  = r_u8();
    const unsigned high = r_u8();
    return static_cast<std::uint16_t>((high << 8) | low);
}

void Connection::r_raw(std::byte* out, std::size_t size) {
    while (size > 0) {
        if (position_ >= packet_.size() && !fill()) return;
        const std::size_t chunk = (std::min)(size, packet_.size() - position_);
        std::memcpy(out, packet_.data() + position_, chunk);
        position_ += chunk;
        out += chunk;
        size -= chunk;
    }
}

void Connection::r_skip(std::size_t size) {
    while (size > 0) {
        if (position_ >= packet_.size() && !fill()) return;
        const std::size_t chunk = (std::min)(size, packet_.size() - position_);
        position_ += chunk;
        size -= chunk;
    }
}

std::uint64_t Connection::r_int(unsigned max_size, bool* negative) {
    unsigned size = r_u8();
    if (negative != nullptr) *negative = (size & 0x80) != 0;
    if ((size & 0x80) != 0) {
        if (negative == nullptr) { stream_fail("Oracle: unexpected negative integer"); return 0; }
        size &= 0x7F;
    }
    if (size > max_size) { stream_fail("Oracle: integer too large"); return 0; }

    std::uint64_t value = 0;
    for (unsigned i = 0; i < size && !failed_; ++i) value = (value << 8) | r_u8();
    return value;
}

std::int32_t Connection::r_sb4() {
    bool negative = false;
    const auto value = static_cast<std::int32_t>(r_int(4, &negative));
    return negative ? -value : value;
}

bool Connection::r_bytes(std::string& out) {
    out.clear();
    const std::uint8_t size = r_u8();
    if (size == 0 || size == kNullLength) return false;

    if (size != kLongLength) {
        out.resize(size);
        r_raw(reinterpret_cast<std::byte*>(out.data()), out.size());
        return true;
    }
    // Em pedacos, ate' o de tamanho zero.
    while (!failed_) {
        const std::uint32_t chunk = r_ub4();
        if (chunk == 0) break;
        const std::size_t at = out.size();
        out.resize(at + chunk);
        r_raw(reinterpret_cast<std::byte*>(out.data()) + at, chunk);
    }
    return true;
}

bool Connection::r_bytes_with_length(std::string& out) {
    out.clear();
    return r_ub4() > 0 && r_bytes(out);
}

void Connection::r_skip_bytes() {
    std::string ignored;
    (void)r_bytes(ignored);
}

void Connection::r_skip_bytes_with_length() {
    if (r_ub4() > 0) r_skip_bytes();
}

// --- Mensagens ---------------------------------------------------------------------

Status Connection::read_response(Call& call) {
    bool done = false;
    while (!done && !failed_) {
        const std::uint8_t type = r_u8();
        if (failed_) break;

        switch (type) {
            case kMsgError:
                read_error_info(call);
                done = true;
                break;

            case kMsgStatus:
                call.call_status = r_ub4();
                (void)r_ub2();
                done = true;
                break;

            case kMsgWarning: {
                const std::uint16_t number = r_ub2();
                const std::uint16_t size = r_ub2();
                (void)r_ub2();
                if (number != 0 && size > 0) r_skip_bytes();
                break;
            }

            case kMsgParameter:
                if (call.kind == Call::Kind::auth) {
                    const std::uint16_t count = r_ub2();
                    for (std::uint16_t i = 0; i < count && !failed_; ++i) {
                        std::string key;
                        std::string value;
                        (void)r_bytes_with_length(key);
                        (void)r_bytes_with_length(value);
                        const std::uint32_t flags = r_ub4();
                        // No sal do verificador, as "flags" sao o TIPO dele.
                        if (key == "AUTH_VFR_DATA") call.verifier_type = flags;
                        call.parameters[std::move(key)] = std::move(value);
                    }
                } else {
                    read_return_parameters(call);
                }
                break;

            case kMsgServerPiggyback:
                read_server_piggyback();
                break;

            case kMsgDescribeInfo:
                r_skip_bytes();
                read_describe_info(call.columns);
                call.fetch_types.clear();
                for (const Column& column : call.columns) call.fetch_types.push_back(column.type);
                call.texts.assign(call.columns.size(), {});
                call.nulls.assign(call.columns.size(), 1);
                call.values.assign(call.columns.size(), {});
                if (!failed_ && call.on_columns != nullptr) (*call.on_columns)(call.columns);
                break;

            case kMsgRowHeader: {
                (void)r_u8();
                (void)r_ub2();
                (void)r_ub4();
                (void)r_ub4();
                (void)r_ub2();
                const std::uint32_t size = r_ub4();
                if (size > 0) {
                    (void)r_u8();
                    call.bit_vector.resize(size);
                    r_raw(call.bit_vector.data(), size);
                    call.has_bit_vector = true;
                }
                r_skip_bytes_with_length();
                break;
            }

            case kMsgBitVector: {
                (void)r_ub2();                   // quantas colunas vieram
                call.bit_vector.resize((call.columns.size() + 7) / 8);
                r_raw(call.bit_vector.data(), call.bit_vector.size());
                call.has_bit_vector = true;
                break;
            }

            case kMsgRowData:
                read_row(call);
                break;

            case kMsgImplicitResults: {
                // DBMS_SQL.RETURN_RESULT: cursores que o bloco devolveu. Nao
                // sao mostrados ainda; sao lidos para manter o passo e
                // fechados.
                const std::uint32_t count = r_ub4();
                for (std::uint32_t i = 0; i < count && !failed_; ++i) {
                    r_skip(r_u8());
                    std::vector<Column> ignored;
                    read_describe_info(ignored);
                    cursors_to_close_.push_back(r_ub2());
                }
                break;
            }

            case kMsgFlushOutBinds:
            case kMsgEndOfResponse:
                done = true;
                break;

            case kMsgIoVector:
                stream_fail("Oracle: bind variables are not supported yet");
                break;

            default:
                stream_fail("Oracle: unknown message type " + std::to_string(type));
                break;
        }
    }

    if (failed_) return std::unexpected(failure_);
    in_transaction_ = (call.call_status & kCallStatusInTransaction) != 0;
    return {};
}

void Connection::read_error_info(Call& call) {
    call.call_status = r_ub4();
    (void)r_ub2();                               // sequencia fim a fim
    (void)r_ub4();                               // linha corrente
    (void)r_ub2();                               // numero do erro (curto)
    (void)r_ub2();
    (void)r_ub2();
    call.cursor_id = r_ub2();
    bool negative = false;
    (void)r_int(2, &negative);                   // posicao do erro no SQL
    r_skip(5);
    (void)r_u8();                                // flags
    (void)r_ub4();                               // rowid: objeto, arquivo, bloco, linha
    (void)r_ub2();
    (void)r_u8();
    (void)r_ub4();
    (void)r_ub2();
    (void)r_ub4();                               // erro do sistema operacional
    r_skip(2);
    (void)r_ub2();
    (void)r_ub4();
    r_skip_bytes_with_length();

    // Erros por linha de um DML em lote: nao usados, mas estao no caminho.
    const std::uint16_t codes = r_ub2();
    if (codes > 0) {
        const std::uint8_t first = r_u8();
        for (std::uint16_t i = 0; i < codes && !failed_; ++i) {
            if (first == kLongLength) (void)r_ub4();
            (void)r_ub2();
        }
        if (first == kLongLength) r_skip(1);
    }
    const std::uint32_t offsets = r_ub4();
    if (offsets > 0) {
        const std::uint8_t first = r_u8();
        for (std::uint32_t i = 0; i < offsets && !failed_; ++i) {
            if (first == kLongLength) (void)r_ub4();
            (void)r_ub4();
        }
        if (first == kLongLength) r_skip(1);
    }
    const std::uint16_t messages = r_ub2();
    if (messages > 0) {
        r_skip(1);
        for (std::uint16_t i = 0; i < messages && !failed_; ++i) {
            (void)r_ub2();
            r_skip_bytes();
            r_skip(2);
        }
    }

    call.error_number = r_ub4();
    call.row_count = r_ub8();
    // Tipo do SQL e soma de verificacao, do 20c em diante. Pela versao DO
    // SERVIDOR, e nao pela negociada: conferido no 23.26, que manda os dois
    // mesmo a um cliente que declarou a versao de campos do 19c (com a
    // negociada, a mensagem de erro saia vazia).
    if (server_field_version_ >= kFieldVersion20) {
        (void)r_ub4();
        (void)r_ub4();
    }
    call.error_message.clear();
    if (call.error_number != 0) {
        (void)r_bytes(call.error_message);
        while (!call.error_message.empty() &&
               (call.error_message.back() == '\n' || call.error_message.back() == ' ')) {
            call.error_message.pop_back();
        }
    }
}

void Connection::read_describe_info(std::vector<Column>& columns) {
    (void)r_ub4();                               // tamanho maximo da linha
    const std::uint32_t count = r_ub4();
    if (count > 4096) { stream_fail("Oracle: too many columns"); return; }
    if (count > 0) (void)r_u8();

    columns.assign(count, {});
    for (Column& column : columns) {
        if (failed_) return;
        read_column(column);
    }
    r_skip_bytes_with_length();                  // data corrente
    (void)r_ub4();
    (void)r_ub4();
    (void)r_ub4();
    (void)r_ub4();
    r_skip_bytes_with_length();
}

void Connection::read_column(Column& column) {
    column.type = static_cast<OraType>(r_u8());
    (void)r_u8();                                // flags
    column.precision = static_cast<std::int8_t>(r_u8());
    column.scale = static_cast<std::int8_t>(r_u8());
    column.buffer_size = r_ub4();
    (void)r_ub4();                               // maximo de elementos de array
    (void)r_ub8();
    r_skip_bytes_with_length();                  // OID do tipo
    (void)r_ub2();
    (void)r_ub2();                               // conjunto de caracteres
    column.charset_form = r_u8();
    column.max_size = r_ub4();
    if (column.type == OraType::raw) column.max_size = column.buffer_size;
    if (field_version_ >= kFieldVersion12_2) (void)r_ub4();
    column.nullable = r_u8() != 0;
    (void)r_u8();
    (void)r_bytes_with_length(column.name);
    r_skip_bytes_with_length();                  // schema do tipo
    r_skip_bytes_with_length();                  // nome do tipo
    (void)r_ub2();                               // posicao
    (void)r_ub4();                               // flags (JSON, OSON)
    // Dominios, anotacoes e dimensoes de vetor so' existem a partir da versao
    // de campos do 23ai, que este cliente nao declara (kFieldVersion19).
}

void Connection::read_row(Call& call) {
    for (std::size_t i = 0; i < call.columns.size() && !failed_; ++i) {
        // Bit desligado = a coluna repete o valor da linha anterior.
        const bool repeated =
            call.has_bit_vector && i / 8 < call.bit_vector.size() &&
            (std::to_integer<unsigned>(call.bit_vector[i / 8]) & (1u << (i % 8))) == 0;
        if (repeated) continue;

        bool null = false;
        read_value(call.columns[i], call.fetch_types[i], call.texts[i], null);
        call.nulls[i] = null ? 1 : 0;
    }
    call.has_bit_vector = false;
    if (failed_ || call.on_row == nullptr) return;

    for (std::size_t i = 0; i < call.columns.size(); ++i) {
        call.values[i].text = call.texts[i];
        call.values[i].null = call.nulls[i] != 0;
    }
    (*call.on_row)(call.values);
}

void Connection::read_value(const Column& column, OraType type, std::string& text,
                            bool& null) {
    text.clear();
    null = true;

    const bool long_type = type == OraType::long_ || type == OraType::long_raw;
    if (column.buffer_size == 0 && !long_type && type != OraType::urowid) return;

    std::string raw;
    const auto bytes = [&raw] {
        return std::span(reinterpret_cast<const std::byte*>(raw.data()), raw.size());
    };

    switch (type) {
        case OraType::rowid: {
            const std::uint8_t size = r_u8();
            if (size == 0 || size == kNullLength) return;
            const std::uint32_t object = r_ub4();
            const std::uint16_t file = r_ub2();
            (void)r_u8();
            const std::uint32_t block = r_ub4();
            const std::uint16_t slot = r_ub2();
            text = encode_rowid(object, file, block, slot);
            null = false;
            return;
        }
        case OraType::urowid:
            if (!r_bytes(raw)) return;           // o primeiro so' traz o tamanho
            if (!r_bytes(raw)) return;
            text = decode_urowid(bytes());
            null = false;
            return;

        case OraType::cursor: {
            // Cursor aninhado (CURSOR(...) na lista do SELECT): descrito aqui
            // mesmo. O conteudo nao e' buscado; o cursor e' fechado.
            (void)r_u8();
            std::vector<Column> ignored;
            read_describe_info(ignored);
            cursors_to_close_.push_back(r_ub2());
            text = "[cursor]";
            null = false;
            return;
        }
        case OraType::clob:
        case OraType::blob:
        case OraType::bfile: {
            // So' chega aqui o localizador (BFILE, ou LOB que nao passou
            // pela definicao): o conteudo exigiria uma ida por valor.
            if (r_ub4() == 0) return;
            if (type != OraType::bfile) {
                (void)r_ub8();
                (void)r_ub4();
            }
            r_skip_bytes();
            text = type == OraType::bfile ? "[BFILE]" : "[LOB]";
            null = false;
            return;
        }
        case OraType::json:
        case OraType::vector: {
            if (r_ub4() == 0) return;
            (void)r_ub8();
            (void)r_ub4();
            r_skip_bytes();                      // o valor, em OSON ou binario
            r_skip_bytes();                      // o localizador
            text = type == OraType::json ? "[JSON]" : "[VECTOR]";
            null = false;
            return;
        }
        case OraType::object: {
            r_skip_bytes_with_length();          // OID do tipo
            r_skip_bytes_with_length();          // OID do objeto
            r_skip_bytes_with_length();          // snapshot
            (void)r_ub2();
            const std::uint32_t size = r_ub4();
            (void)r_ub2();
            if (size == 0) return;
            r_skip_bytes();
            text = "[object]";
            null = false;
            return;
        }
        default:
            break;
    }

    null = !r_bytes(raw);
    if (long_type) {
        (void)r_sb4();                           // indicador de nulo
        (void)r_ub4();                           // codigo de retorno
    }
    if (null || failed_) return;

    switch (type) {
        case OraType::varchar:
        case OraType::char_:
        case OraType::long_:
            text = column.charset_form == kCharsetFormNational ? utf16be_to_utf8(bytes())
                                                               : std::move(raw);
            break;
        case OraType::number:
        case OraType::binary_integer:
            text = decode_number(bytes());
            break;
        case OraType::date:
        case OraType::timestamp:
        case OraType::timestamp_tz:
        case OraType::timestamp_ltz:
            text = decode_datetime(bytes());
            break;
        case OraType::interval_ds:   text = decode_interval_ds(bytes()); break;
        case OraType::interval_ym:   text = decode_interval_ym(bytes()); break;
        case OraType::binary_float:  text = decode_binary_float(bytes()); break;
        case OraType::binary_double: text = decode_binary_double(bytes()); break;
        case OraType::boolean:
            text = !raw.empty() && raw[0] == 1 ? "true" : "false";
            break;
        default:
            text = std::move(raw);               // RAW e LONG RAW: os bytes como sao
            break;
    }
}

void Connection::read_return_parameters(Call& call) {
    (void)call;
    const std::uint16_t count = r_ub2();
    for (std::uint16_t i = 0; i < count && !failed_; ++i) (void)r_ub4();
    r_skip(r_ub2());
    read_key_value_pairs(r_ub2());
    r_skip(r_ub2());                             // registro de notificacao
}

void Connection::read_key_value_pairs(std::uint16_t count) {
    for (std::uint16_t i = 0; i < count && !failed_; ++i) {
        std::string text;
        if (r_ub2() > 0) (void)r_bytes(text);
        if (r_ub2() > 0) r_skip_bytes();
        const std::uint16_t keyword = r_ub2();
        // 168: o schema corrente mudou (ALTER SESSION SET CURRENT_SCHEMA).
        if (keyword == 168) current_schema_ = std::move(text);
    }
}

// Avisos que o servidor poe de carona na resposta. Nenhum interessa ainda,
// mas todos tem de ser lidos inteiros para o fluxo nao perder o passo.
void Connection::read_server_piggyback() {
    const std::uint8_t code = r_u8();
    switch (code) {
        case 1:                                  // invalidacao de cache de consulta
        case 3:                                  // evento de trace
            break;
        case 2:                                  // PID do servidor
            (void)r_ub2();
            r_skip_bytes();
            break;
        case 4: {                                // retorno de sessao (DRCP)
            (void)r_ub2();
            (void)r_u8();
            const std::uint16_t count = r_ub2();
            if (count > 0) {
                (void)r_u8();
                for (std::uint16_t i = 0; i < count && !failed_; ++i) {
                    if (r_ub2() > 0) r_skip_bytes();
                    if (r_ub2() > 0) r_skip_bytes();
                    (void)r_ub2();
                }
            }
            (void)r_ub4();
            (void)r_ub4();
            (void)r_ub2();
            break;
        }
        case 5: {                                // sincronizacao: pares chave/valor
            (void)r_ub2();
            (void)r_u8();
            const std::uint16_t count = r_ub2();
            (void)r_u8();
            read_key_value_pairs(count);
            (void)r_ub4();
            break;
        }
        case 7:                                  // id logico da transacao
            r_skip_bytes_with_length();
            break;
        case 8:                                  // contexto de replay
            (void)r_ub2();
            (void)r_u8();
            (void)r_ub4();
            (void)r_ub4();
            (void)r_u8();
            r_skip_bytes_with_length();
            break;
        case 9:
            (void)r_ub2();
            (void)r_u8();
            break;
        case 10:                                 // assinatura de sessao
            (void)r_ub2();
            (void)r_u8();
            (void)r_ub8();
            (void)r_ub8();
            (void)r_ub8();
            break;
        default:
            stream_fail("Oracle: unknown server piggyback " + std::to_string(code));
            break;
    }
}

// --- Funcoes puras -----------------------------------------------------------------

std::string connect_descriptor(const ConnectParams& params, std::string_view machine,
                               std::string_view os_user) {
    std::string out = "(DESCRIPTION=(ADDRESS=(PROTOCOL=tcp)(HOST=";
    out += params.host;
    out += ")(PORT=";
    out += std::to_string(params.port);
    out += "))(CONNECT_DATA=";
    if (!params.service_name.empty()) {
        out += "(SERVICE_NAME=" + params.service_name + ")";
    } else {
        out += "(SID=" + params.sid + ")";
    }
    out += "(CID=(PROGRAM=";
    out += params.program;
    out += ")(HOST=";
    out += machine;
    out += ")(USER=";
    out += os_user;
    out += "))))";
    return out;
}

StatementKind classify_statement(std::string_view sql) noexcept {
    // Pula espacos, comentarios e parenteses ate' a primeira palavra.
    std::size_t i = 0;
    while (i < sql.size()) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '(') {
            ++i;
        } else if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            while (i < sql.size() && sql[i] != '\n') ++i;
        } else if (c == '/' && i + 1 < sql.size() && sql[i + 1] == '*') {
            const std::size_t end = sql.find("*/", i + 2);
            i = end == std::string_view::npos ? sql.size() : end + 2;
        } else {
            break;
        }
    }

    std::string word;
    while (i < sql.size() && word.size() < 16) {
        const char c = sql[i];
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (!alpha) break;
        word.push_back(static_cast<char>(c >= 'a' ? c - 32 : c));
        ++i;
    }

    if (word == "SELECT" || word == "WITH") return StatementKind::query;
    if (word == "DECLARE" || word == "BEGIN" || word == "CALL") return StatementKind::plsql;
    if (word == "INSERT" || word == "UPDATE" || word == "DELETE" || word == "MERGE") {
        return StatementKind::dml;
    }
    for (const std::string_view ddl : {"CREATE", "ALTER", "DROP", "GRANT", "REVOKE", "ANALYZE",
                                       "AUDIT", "COMMENT", "TRUNCATE"}) {
        if (word == ddl) return StatementKind::ddl;
    }
    return StatementKind::other;
}

std::string format_server_version(std::uint32_t number, bool modern) {
    char text[40];
    if (modern) {
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u.%u", (number >> 24) & 0xFF,
                      (number >> 16) & 0xFF, (number >> 12) & 0x0F, (number >> 4) & 0xFF,
                      number & 0x0F);
    } else {
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u.%u", (number >> 24) & 0xFF,
                      (number >> 20) & 0x0F, (number >> 12) & 0x0F, (number >> 8) & 0x0F,
                      number & 0x0F);
    }
    return text;
}

} // namespace otter::orawire
