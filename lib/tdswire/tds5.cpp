#include "tdswire/tds5.hpp"

#include "tdswire/value.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace otter::tdswire {
namespace {

// Tipos de pacote (TDS 5.0, "Message Buffer Header").
constexpr std::uint8_t kPacketLogin     = 0x02;
constexpr std::uint8_t kPacketAttention = 0x06;
constexpr std::uint8_t kPacketNormal    = 0x0F;

constexpr std::uint8_t kStatusEom = 0x01;

// Tokens.
constexpr std::uint8_t kTokenParamFmt2    = 0x20;
constexpr std::uint8_t kTokenLanguage     = 0x21;
constexpr std::uint8_t kTokenOrderBy2     = 0x22;
constexpr std::uint8_t kTokenRowFmt2      = 0x61;
constexpr std::uint8_t kTokenDynamic2     = 0x62;
constexpr std::uint8_t kTokenMsg          = 0x65;
constexpr std::uint8_t kTokenReturnStatus = 0x79;
constexpr std::uint8_t kTokenCurInfo      = 0x83;
constexpr std::uint8_t kTokenColInfo      = 0xA5;
constexpr std::uint8_t kTokenTabName      = 0xA8;
constexpr std::uint8_t kTokenOrderBy      = 0xA9;
constexpr std::uint8_t kTokenError        = 0xAA;
constexpr std::uint8_t kTokenInfo         = 0xAB;
constexpr std::uint8_t kTokenReturnValue  = 0xAC;
constexpr std::uint8_t kTokenLoginAck     = 0xAD;
constexpr std::uint8_t kTokenControl      = 0xAE;
constexpr std::uint8_t kTokenRow          = 0xD1;
constexpr std::uint8_t kTokenParams       = 0xD7;
constexpr std::uint8_t kTokenCapability   = 0xE2;
constexpr std::uint8_t kTokenEnvChange    = 0xE3;
constexpr std::uint8_t kTokenEed          = 0xE5;
constexpr std::uint8_t kTokenDynamic      = 0xE7;
constexpr std::uint8_t kTokenParamFmt     = 0xEC;
constexpr std::uint8_t kTokenRowFmt       = 0xEE;
constexpr std::uint8_t kTokenDone         = 0xFD;
constexpr std::uint8_t kTokenDoneProc     = 0xFE;
constexpr std::uint8_t kTokenDoneInProc   = 0xFF;

constexpr std::uint16_t kDoneCount = 0x0010;
constexpr std::uint16_t kDoneAttn  = 0x0020;

// Estado da transacao no DONE.
constexpr std::uint16_t kTranProgress  = 2;
constexpr std::uint16_t kTranStmtAbort = 3;

// Bits de estado de uma coluna em ROWFMT.
constexpr std::uint32_t kRowHidden    = 0x01;
constexpr std::uint32_t kRowKey       = 0x02;
constexpr std::uint32_t kRowUpdatable = 0x10;
constexpr std::uint32_t kRowNullable  = 0x20;
constexpr std::uint32_t kRowIdentity  = 0x40;

// Tipos de usuario que mudam a LEITURA de um tipo do protocolo.
//
// unichar e univarchar chegam como LONGBINARY com o texto em UTF-16. O SQL
// Anywhere 16 nao usa os tipos DATE e TIME do TDS: manda date e time como
// DATETIME e diz qual e' pelo tipo de usuario (37 e 38). O uniqueidentifier
// chega como binary(16) com o tipo 81.
constexpr std::uint32_t kUserUnichar    = 34;
constexpr std::uint32_t kUserUnivarchar = 35;
constexpr std::uint32_t kUserDate       = 37;
constexpr std::uint32_t kUserTime       = 38;
constexpr std::uint32_t kUserGuid       = 81;

// 16 bytes -> "6f9619ff-8b86-d011-b42d-00c04fc964ff", na ordem em que vem: o
// SQL Anywhere guarda o UUID como binario simples, sem a inversao dos tres
// primeiros grupos que o SQL Server faz.
std::string guid_text(std::span<const std::byte> bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out += '-';
        const auto value = static_cast<std::uint8_t>(bytes[i]);
        out += kHex[value >> 4];
        out += kHex[value & 0x0F];
    }
    return out;
}

void put_u16be(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(value & 0xFF));
}

void put_u16le(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFF));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
}

void put_u32le(std::vector<std::byte>& out, std::uint32_t value) {
    put_u16le(out, value & 0xFFFF);
    put_u16le(out, value >> 16);
}

void put_bytes(std::vector<std::byte>& out, std::string_view text) {
    for (const char c : text) out.push_back(static_cast<std::byte>(c));
}

void put_zeros(std::vector<std::byte>& out, std::size_t count) {
    out.insert(out.end(), count, std::byte{0});
}

// Um campo de texto do registro de login: `room` bytes (o texto, cortado e
// completado com zeros) e 1 byte com o tamanho usado.
void put_field(std::vector<std::byte>& out, std::string_view text, std::size_t room) {
    const std::size_t used = std::min(text.size(), room);
    put_bytes(out, text.substr(0, used));
    put_zeros(out, room - used);
    out.push_back(static_cast<std::byte>(used));
}

std::uint64_t le(std::span<const std::byte> bytes, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size && at + i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[at + i]) << (8 * i);
    }
    return value;
}

// Dias desde 1970-01-01 -> data civil (calendario gregoriano proleptico).
void civil_from_days(std::int64_t z, std::int64_t& year, unsigned& month, unsigned& day) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp  = (5 * doy + 2) / 153;
    day   = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year  = static_cast<std::int64_t>(yoe) + era * 400 + (month <= 2 ? 1 : 0);
}

std::string date_text(std::int64_t days_since_1970) {
    std::int64_t year = 0;
    unsigned month = 0, day = 0;
    civil_from_days(days_since_1970, year, month, day);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02u-%02u", static_cast<long long>(year),
                  month, day);
    return buffer;
}

constexpr std::int64_t kDays1900 = -25567;    // 1900-01-01 em dias desde 1970
constexpr std::int64_t kDaysYear0 = -719528;  // 0000-01-01

std::string client_host_name() {
    if (const char* name = std::getenv("COMPUTERNAME")) return name;
    if (const char* name = std::getenv("HOSTNAME")) return name;
    return "c-otter";
}

std::string process_id_text() {
#ifdef _WIN32
    return std::to_string(GetCurrentProcessId());
#else
    return std::to_string(static_cast<long>(getpid()));
#endif
}

Error to_error(const Message& message) {
    char header[64];
    std::snprintf(header, sizeof header, " (Msg %d, Level %u)", message.number,
                  static_cast<unsigned>(message.severity));
    // 4002: "Login failed" no numero do ASE, que o SQL Anywhere tambem usa.
    const Errc code = message.number == 4002 ? Errc::auth_failed : Errc::query_failed;
    return Error{code, message.text + header};
}

// Um bit do mapa de capacidades: o bit 0 e' o MENOS significativo do ULTIMO
// byte.
void set_capability(std::span<std::byte> mask, unsigned bit) {
    const std::size_t index = mask.size() - 1 - bit / 8;
    mask[index] |= static_cast<std::byte>(1u << (bit % 8));
}

} // namespace

// --- Montagem de pacotes ------------------------------------------------------------

std::vector<std::byte> build_tds5_capabilities() {
    constexpr std::size_t kMaskSize = 14;

    // O que o cliente PEDE: os recursos e os tipos que sabe ler. Um tipo que
    // nao esta' aqui o servidor converte para outro que esteja -- sem
    // DATA_INT8, por exemplo, um bigint chega como numeric.
    std::vector<std::byte> request(kMaskSize, std::byte{0});
    for (const unsigned bit : {
             1u,    // REQ_LANG: comandos em texto
             4u,    // REQ_MSTMT: varios comandos num lote
             8u,    // REQ_MSG
             9u,    // REQ_PARAM
             10u, 11u, 12u, 13u,    // INT1, INT2, INT4, BIT
             14u, 15u, 16u, 17u,    // CHAR, VCHAR, BIN, VBIN
             18u, 19u, 20u, 21u,    // MNY8, MNY4, DATE8, DATE4
             22u, 23u, 24u,         // FLT4, FLT8, NUM
             25u, 26u, 27u,         // TEXT, IMAGE, DEC
             28u, 29u,              // LCHAR, LBIN
             30u, 31u, 32u,         // INTN, DATETIMEN, MONEYN
             42u,                   // PROTO_TEXT
             49u, 50u, 51u,         // FLTN, BITN, INT8
             59u,                   // WIDETABLE: ROWFMT2, com a tabela de origem
             61u, 62u, 63u, 64u,    // UINT2, UINT4, UINT8, UINTN
             71u, 72u,              // DATE, TIME
             82u,                   // SINT1
             93u, 94u,              // BIGDATETIME, USECS: timestamp com microssegundos
         }) {
        set_capability(request, bit);
    }

    // O que o cliente RECUSA.
    std::vector<std::byte> response(kMaskSize, std::byte{0});
    for (const unsigned bit : {
             31u, 32u,    // DATA_NOSENSITIVITY, DATA_NOBOUNDARY
             33u,         // RES_NOTDSDEBUG
             36u,         // OBJECT_NOJAVA1
             37u, 39u,    // OBJECT_NOCHAR, OBJECT_NOBINARY
             47u, 48u, 49u,   // BLOB_NONCHAR_16 / _8 / _SCSU
             52u,         // DATA_NOINTERVAL
             58u,         // DATA_NOXML
         }) {
        set_capability(response, bit);
    }

    std::vector<std::byte> out;
    out.push_back(static_cast<std::byte>(kTokenCapability));
    put_u16le(out, static_cast<std::uint32_t>(2 * (2 + kMaskSize)));
    out.push_back(std::byte{0x01});   // TDS_CAP_REQUEST
    out.push_back(static_cast<std::byte>(kMaskSize));
    out.insert(out.end(), request.begin(), request.end());
    out.push_back(std::byte{0x02});   // TDS_CAP_RESPONSE
    out.push_back(static_cast<std::byte>(kMaskSize));
    out.insert(out.end(), response.begin(), response.end());
    return out;
}

std::vector<std::byte> build_tds5_login(const Tds5ConnectParams& params,
                                        std::string_view client_host,
                                        std::string_view process_id) {
    std::vector<std::byte> out;
    out.reserve(640);

    put_field(out, client_host, 30);       // lhostname
    put_field(out, params.user, 30);       // lusername
    put_field(out, params.password, 30);   // lpw
    put_field(out, process_id, 30);        // lhostproc

    // Como o cliente escreve os valores: inteiros little-endian, ASCII,
    // ponto flutuante IEEE little-endian, datas little-endian.
    out.push_back(std::byte{3});     // lint2
    out.push_back(std::byte{1});     // lint4
    out.push_back(std::byte{6});     // lchar
    out.push_back(std::byte{10});    // lflt
    out.push_back(std::byte{9});     // ldate
    out.push_back(std::byte{1});     // lusedb: avisar a troca de banco
    out.push_back(std::byte{1});     // ldmpld
    out.push_back(std::byte{0});     // linterfacespare
    out.push_back(std::byte{0});     // ltype
    put_zeros(out, 4);               // lbufsize
    put_zeros(out, 3);               // lspare

    put_field(out, params.application, 30);   // lappname
    // lservname: no SQL Anywhere e' o BANCO (o ServiceName do jConnect).
    put_field(out, params.database, 30);

    // lrempw: a senha de novo, no formato "servidor remoto": um byte zero (o
    // nome do servidor, vazio), o tamanho da senha e a senha.
    {
        const std::string_view password =
            std::string_view(params.password).substr(0, std::min<std::size_t>(
                                                           params.password.size(), 253));
        out.push_back(std::byte{0});
        out.push_back(static_cast<std::byte>(password.size()));
        put_bytes(out, password);
        put_zeros(out, 253 - password.size());
        out.push_back(static_cast<std::byte>(password.size() + 2));   // lrempwlen
    }

    // ltds: a versao do protocolo.
    out.push_back(std::byte{5});
    put_zeros(out, 3);

    put_field(out, "C-Otter", 10);   // lprogname
    out.push_back(std::byte{1});     // lprogvers
    put_zeros(out, 3);

    out.push_back(std::byte{0});     // lnoshort
    out.push_back(std::byte{13});    // lflt4: IEEE little-endian
    out.push_back(std::byte{17});    // ldate4: little-endian

    put_field(out, {}, 30);          // llanguage: o do login
    out.push_back(std::byte{0});     // lsetlang

    put_zeros(out, 2);               // loldsecure
    out.push_back(std::byte{0});     // lseclogin: senha no registro, sem desafio
    out.push_back(std::byte{0});     // lsecbulk
    out.push_back(std::byte{0});     // lhalogin
    put_zeros(out, 6);               // lhasessionid
    put_zeros(out, 2);               // lsecspare

    // O conjunto de caracteres do CLIENTE: o servidor converte tudo para ele,
    // nomes e valores. E' o que dispensa uma tabela de paginas de codigo aqui.
    put_field(out, "utf8", 30);      // lcharset
    out.push_back(std::byte{1});     // lsetcharset

    put_field(out, "512", 6);        // lpacketsize, em texto
    put_zeros(out, 4);               // ldummy

    const std::vector<std::byte> capabilities = build_tds5_capabilities();
    out.insert(out.end(), capabilities.begin(), capabilities.end());
    return out;
}

std::vector<std::byte> build_tds5_language(std::string_view sql) {
    std::vector<std::byte> out;
    out.reserve(sql.size() + 6);
    out.push_back(static_cast<std::byte>(kTokenLanguage));
    put_u32le(out, static_cast<std::uint32_t>(sql.size() + 1));
    out.push_back(std::byte{0});   // estado: sem parametros
    put_bytes(out, sql);
    return out;
}

// --- Conversoes ------------------------------------------------------------------------

std::string format_tds5_numeric(std::span<const std::byte> bytes, std::uint8_t scale) {
    if (bytes.empty()) return {};
    const bool negative = static_cast<std::uint8_t>(bytes[0]) != 0;

    // A magnitude, big-endian, em palavras de 32 bits (a mais significativa
    // primeiro). Ate' 33 bytes: precisao 77.
    std::vector<std::uint32_t> words((bytes.size() - 1 + 3) / 4, 0);
    {
        std::size_t shift = 0;
        std::size_t word  = words.size();
        for (std::size_t i = bytes.size(); i-- > 1;) {
            if (shift == 0) --word;
            words[word] |= static_cast<std::uint32_t>(bytes[i]) << shift;
            shift = (shift + 8) % 32;
        }
    }

    // Divisoes sucessivas por 10^9: nove digitos por vez, do menos significativo.
    std::string digits;
    const auto is_zero = [&words]() {
        return std::all_of(words.begin(), words.end(), [](std::uint32_t w) { return w == 0; });
    };
    while (!is_zero()) {
        std::uint64_t remainder = 0;
        for (std::uint32_t& word : words) {
            const std::uint64_t current = (remainder << 32) | word;
            word      = static_cast<std::uint32_t>(current / 1000000000u);
            remainder = current % 1000000000u;
        }
        for (int d = 0; d < 9; ++d) {
            digits += static_cast<char>('0' + remainder % 10);
            remainder /= 10;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    if (digits.empty()) digits = "0";
    while (digits.size() <= scale) digits += '0';

    std::string out;
    if (negative && digits != std::string(digits.size(), '0')) out += '-';
    for (std::size_t i = digits.size(); i-- > 0;) {
        out += digits[i];
        if (i == scale && scale > 0) out += '.';
    }
    return out;
}

std::string format_tds5_date(std::span<const std::byte> bytes) {
    if (bytes.size() != 4) return {};
    return date_text(kDays1900 + static_cast<std::int32_t>(le(bytes, 0, 4)));
}

std::string format_tds5_time(std::span<const std::byte> bytes) {
    if (bytes.size() != 4) return {};
    const std::uint64_t ticks   = le(bytes, 0, 4);
    const std::uint64_t seconds = ticks / 300;
    const auto millis = static_cast<unsigned>((ticks % 300) * 10 / 3);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02u:%02u:%02u.%03u",
                  static_cast<unsigned>(seconds / 3600),
                  static_cast<unsigned>(seconds / 60 % 60),
                  static_cast<unsigned>(seconds % 60), millis);
    return buffer;
}

std::string format_tds5_bigtime(std::span<const std::byte> bytes) {
    if (bytes.size() != 8) return {};
    const std::uint64_t micros  = le(bytes, 0, 8);
    const std::uint64_t seconds = micros / 1000000;
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%02u:%02u:%02u.%06u",
                  static_cast<unsigned>(seconds / 3600 % 24),
                  static_cast<unsigned>(seconds / 60 % 60),
                  static_cast<unsigned>(seconds % 60),
                  static_cast<unsigned>(micros % 1000000));
    return buffer;
}

std::string format_tds5_bigdatetime(std::span<const std::byte> bytes) {
    if (bytes.size() != 8) return {};
    const std::uint64_t micros = le(bytes, 0, 8);
    constexpr std::uint64_t kPerDay = 86400ull * 1000000ull;
    const auto days = static_cast<std::int64_t>(micros / kPerDay);

    std::byte rest[8];
    const std::uint64_t in_day = micros % kPerDay;
    for (std::size_t i = 0; i < 8; ++i) {
        rest[i] = static_cast<std::byte>((in_day >> (8 * i)) & 0xFF);
    }
    return date_text(kDaysYear0 + days) + " " + format_tds5_bigtime(rest);
}

std::string tds5_type_name(const Tds5Column& column) {
    const auto with_size = [&column](const char* name) {
        return std::string(name) + "(" + std::to_string(column.max_length) + ")";
    };
    const auto numeric = [&column](const char* name) {
        return std::string(name) + "(" + std::to_string(column.precision) + "," +
               std::to_string(column.scale) + ")";
    };

    switch (column.type) {
        case Tds5Type::void_:      return "void";
        case Tds5Type::image:      return "long binary";
        case Tds5Type::text:       return "long varchar";
        case Tds5Type::unitext:    return "long nvarchar";
        case Tds5Type::xml:        return "xml";
        case Tds5Type::varbinary:
            if (column.user_type == kUserGuid) return "uniqueidentifier";
            return with_size("varbinary");
        case Tds5Type::binary:
            if (column.user_type == kUserGuid) return "uniqueidentifier";
            return with_size("binary");
        // Texto SEM o tamanho: o que chega e' o tamanho em bytes no conjunto de
        // caracteres do cliente (o triplo do declarado, em UTF-8). Um numero
        // errado ao lado do tipo e' pior que nenhum; o certo vem do catalogo.
        case Tds5Type::varchar:    return "varchar";
        case Tds5Type::char_:      return "char";
        case Tds5Type::longchar:   return "varchar";
        case Tds5Type::longbinary:
            if (column.user_type == kUserUnichar)    return "nchar";
            if (column.user_type == kUserUnivarchar) return "nvarchar";
            return with_size("varbinary");
        case Tds5Type::int1:       return "tinyint";
        case Tds5Type::sint1:      return "tinyint";
        case Tds5Type::int2:       return "smallint";
        case Tds5Type::int4:       return "integer";
        case Tds5Type::int8:       return "bigint";
        case Tds5Type::uint2:      return "unsigned smallint";
        case Tds5Type::uint4:      return "unsigned int";
        case Tds5Type::uint8:      return "unsigned bigint";
        case Tds5Type::intn:
            switch (column.max_length) {
                case 1:  return "tinyint";
                case 2:  return "smallint";
                case 8:  return "bigint";
                default: return "integer";
            }
        case Tds5Type::uintn:
            switch (column.max_length) {
                case 1:  return "tinyint";
                case 2:  return "unsigned smallint";
                case 8:  return "unsigned bigint";
                default: return "unsigned int";
            }
        case Tds5Type::bit:        return "bit";
        case Tds5Type::float4:     return "real";
        case Tds5Type::float8:     return "double";
        case Tds5Type::fltn:       return column.max_length == 4 ? "real" : "double";
        case Tds5Type::money:      return "money";
        case Tds5Type::shortmoney: return "smallmoney";
        case Tds5Type::moneyn:     return column.max_length == 4 ? "smallmoney" : "money";
        case Tds5Type::decn:       return numeric("decimal");
        case Tds5Type::numn:       return numeric("numeric");
        case Tds5Type::date:
        case Tds5Type::daten:      return "date";
        case Tds5Type::time:
        case Tds5Type::timen:
        case Tds5Type::bigtimen:   return "time";
        case Tds5Type::shortdate:  return "smalldatetime";
        case Tds5Type::datetime:
        case Tds5Type::bigdatetimen: return "timestamp";
        case Tds5Type::datetimn:
            if (column.user_type == kUserDate) return "date";
            if (column.user_type == kUserTime) return "time";
            return column.max_length == 4 ? "smalldatetime" : "timestamp";
    }
    return "unknown";
}

// --- Ciclo de vida ------------------------------------------------------------------

Tds5Connection::Tds5Connection()
    : send_mutex_(std::make_unique<std::mutex>()),
      attention_pending_(std::make_unique<std::atomic<bool>>(false)) {}

Tds5Connection::~Tds5Connection() { close(); }

Tds5Connection::Tds5Connection(Tds5Connection&&) noexcept = default;
Tds5Connection& Tds5Connection::operator=(Tds5Connection&&) noexcept = default;

void Tds5Connection::close() noexcept { socket_.close(); }

Result<Tds5Connection> Tds5Connection::connect(const Tds5ConnectParams& params) {
    if (const Status status = net::initialize_network(); !status) {
        return std::unexpected(status.error());
    }
    if (params.user.empty()) return fail(Errc::invalid_argument, "the user name is required");

    Result<net::Socket> socket =
        net::connect_to(params.proxy, params.host, params.port, params.timeout);
    if (!socket) {
        return std::unexpected(socket.error().with_context(
            "connecting to " + params.host + ":" + std::to_string(params.port)));
    }

    Tds5Connection connection;
    connection.socket_ = std::move(*socket);
    connection.socket_.set_no_delay(true);
    connection.socket_.set_read_timeout(params.timeout);
    connection.socket_.set_write_timeout(params.timeout);

    OTTER_RETURN_IF_ERROR(connection.login(params));

    // Dai' em diante uma consulta pode demorar o que demorar.
    connection.socket_.set_read_timeout(std::chrono::milliseconds(0));
    return connection;
}

// --- Transporte ---------------------------------------------------------------------

Status Tds5Connection::send_message(std::uint8_t type, std::span<const std::byte> payload) {
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
        put_u16be(packet, 0);              // canal
        packet.push_back(std::byte{0});    // numero do pacote
        packet.push_back(std::byte{0});    // janela
        packet.insert(packet.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                      payload.begin() + static_cast<std::ptrdiff_t>(offset + chunk));

        OTTER_RETURN_IF_ERROR(socket_.write_all(packet));
        offset += chunk;
    } while (offset < payload.size());
    return {};
}

Status Tds5Connection::read_packet() {
    std::byte header[8];
    OTTER_RETURN_IF_ERROR(socket_.read_exact(header));

    const std::size_t length = (static_cast<std::size_t>(header[2]) << 8) |
                               static_cast<std::size_t>(header[3]);
    if (length < 8) return fail(Errc::protocol_error, "invalid TDS packet length");

    if (in_pos_ > 0) {
        inbox_.erase(inbox_.begin(), inbox_.begin() + static_cast<std::ptrdiff_t>(in_pos_));
        in_pos_ = 0;
    }
    const std::size_t offset = inbox_.size();
    inbox_.resize(offset + length - 8);
    if (length > 8) {
        if (Status status = socket_.read_exact(std::span(inbox_).subspan(offset)); !status) {
            inbox_.resize(offset);
            return status;
        }
    }
    message_complete_ = (static_cast<std::uint8_t>(header[1]) & kStatusEom) != 0;
    return {};
}

bool Tds5Connection::at_message_end() const noexcept {
    return message_complete_ && in_pos_ == inbox_.size();
}

bool Tds5Connection::take(std::size_t size, const std::byte*& out) {
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

bool Tds5Connection::skip(std::size_t size) {
    while (size > 0) {
        const std::size_t chunk = std::min<std::size_t>(size, 65536);
        const std::byte* unused = nullptr;
        if (!take(chunk, unused)) return false;
        size -= chunk;
    }
    return true;
}

bool Tds5Connection::read_u8(std::uint8_t& value) {
    const std::byte* p = nullptr;
    if (!take(1, p)) return false;
    value = static_cast<std::uint8_t>(p[0]);
    return true;
}

bool Tds5Connection::read_u16(std::uint16_t& value) {
    const std::byte* p = nullptr;
    if (!take(2, p)) return false;
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[0]) |
                                       (static_cast<std::uint16_t>(p[1]) << 8));
    return true;
}

bool Tds5Connection::read_u32(std::uint32_t& value) {
    const std::byte* p = nullptr;
    if (!take(4, p)) return false;
    value = static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
            (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    return true;
}

bool Tds5Connection::read_string8(std::string& value) {
    std::uint8_t length = 0;
    const std::byte* p = nullptr;
    if (!read_u8(length) || !take(length, p)) return false;
    value.assign(reinterpret_cast<const char*>(p), length);
    return true;
}

// --- LOGIN ---------------------------------------------------------------------------

Status Tds5Connection::login(const Tds5ConnectParams& params) {
    OTTER_RETURN_IF_ERROR(send_message(
        kPacketLogin, build_tds5_login(params, client_host_name(), process_id_text())));

    if (Status status = read_response({}, {}, /*login_phase=*/true); !status) {
        // Um servidor que nao fala TDS (ou a porta errada) fecha a conexao sem
        // responder: o erro cru seria "connection closed".
        if (status.error().code() != Errc::auth_failed &&
            status.error().code() != Errc::query_failed) {
            return std::unexpected(status.error().with_context(
                "reading the login response (is this a SQL Anywhere TCP/IP port with "
                "TDS enabled?)"));
        }
        return status;
    }
    if (!logged_in_) return fail(Errc::auth_failed, "the server did not accept the login");
    return {};
}

// --- Comandos ------------------------------------------------------------------------

Status Tds5Connection::query(std::string_view sql, const ColumnsCallback& on_columns,
                             const RowCallback& on_row) {
    if (!is_open()) return fail(Errc::closed, "connection is closed");

    // Um cancelamento pedido com a conexao parada: o DONE de atencao precisa
    // sair do fluxo antes do proximo lote.
    if (attention_pending_->load(std::memory_order_acquire)) {
        (void)read_response({}, {}, /*login_phase=*/false);
    }

    OTTER_RETURN_IF_ERROR(send_message(kPacketNormal, build_tds5_language(sql)));
    return read_response(on_columns, on_row, /*login_phase=*/false);
}

Status Tds5Connection::cancel() {
    if (!is_open()) return fail(Errc::closed, "connection is closed");

    const std::lock_guard<std::mutex> lock(*send_mutex_);
    attention_pending_->store(true, std::memory_order_release);

    std::vector<std::byte> packet;
    packet.push_back(static_cast<std::byte>(kPacketAttention));
    packet.push_back(static_cast<std::byte>(kStatusEom));
    put_u16be(packet, 8);
    put_u16be(packet, 0);
    packet.push_back(std::byte{0});
    packet.push_back(std::byte{0});
    return socket_.write_all(packet);
}

std::vector<Message> Tds5Connection::take_messages() {
    std::vector<Message> out = std::move(messages_);
    messages_.clear();
    return out;
}

// --- Leitura do fluxo ----------------------------------------------------------------

bool Tds5Connection::read_format(std::vector<Tds5Column>& columns, bool wide) {
    // O tamanho do token: nao e' usado para pular -- cada coluna e' lida campo
    // a campo -- mas precisa sair do fluxo.
    if (wide) {
        std::uint32_t length = 0;
        if (!read_u32(length)) return false;
    } else {
        std::uint16_t length = 0;
        if (!read_u16(length)) return false;
    }

    std::uint16_t count = 0;
    if (!read_u16(count)) return false;

    columns.clear();
    columns.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        Tds5Column column;
        std::uint32_t status = 0;

        if (!read_string8(column.name)) return false;
        if (wide) {
            if (!read_string8(column.catalog) || !read_string8(column.schema) ||
                !read_string8(column.table) || !read_string8(column.column) ||
                !read_u32(status)) {
                return false;
            }
            // Sem alias o rotulo vem vazio: o nome e' o da coluna.
            if (column.name.empty()) column.name = column.column;
        } else {
            std::uint8_t byte = 0;
            if (!read_u8(byte)) return false;
            status = byte;
        }

        std::uint8_t byte = 0;
        if (!read_u32(column.user_type) || !read_type(column)) return false;

        // O "locale" da coluna: nao usado.
        if (!read_u8(byte) || !skip(byte)) return false;

        column.hidden    = (status & kRowHidden) != 0;
        column.key       = (status & kRowKey) != 0;
        column.updatable = (status & kRowUpdatable) != 0;
        column.nullable  = (status & kRowNullable) != 0;
        column.identity  = (status & kRowIdentity) != 0;
        column.type_name = tds5_type_name(column);
        columns.push_back(std::move(column));
    }
    return true;
}

bool Tds5Connection::read_type(Tds5Column& column) {
    std::uint8_t type = 0;
    if (!read_u8(type)) return false;
    column.type = static_cast<Tds5Type>(type);

    {
        std::uint8_t byte = 0;
        switch (column.type) {
            case Tds5Type::void_:      column.max_length = 0; break;
            case Tds5Type::int1:
            case Tds5Type::sint1:
            case Tds5Type::bit:        column.max_length = 1; break;
            case Tds5Type::int2:
            case Tds5Type::uint2:      column.max_length = 2; break;
            case Tds5Type::int4:
            case Tds5Type::uint4:
            case Tds5Type::float4:
            case Tds5Type::shortdate:
            case Tds5Type::shortmoney:
            case Tds5Type::date:
            case Tds5Type::time:       column.max_length = 4; break;
            case Tds5Type::int8:
            case Tds5Type::uint8:
            case Tds5Type::float8:
            case Tds5Type::money:
            case Tds5Type::datetime:   column.max_length = 8; break;

            case Tds5Type::intn:
            case Tds5Type::uintn:
            case Tds5Type::fltn:
            case Tds5Type::moneyn:
            case Tds5Type::datetimn:
            case Tds5Type::daten:
            case Tds5Type::timen:
            case Tds5Type::char_:
            case Tds5Type::varchar:
            case Tds5Type::binary:
            case Tds5Type::varbinary:
                if (!read_u8(byte)) return false;
                column.max_length = byte;
                break;

            case Tds5Type::bigdatetimen:
            case Tds5Type::bigtimen:
                if (!read_u8(byte) || !read_u8(column.scale)) return false;
                column.max_length = byte;
                break;

            case Tds5Type::decn:
            case Tds5Type::numn:
                if (!read_u8(byte) || !read_u8(column.precision) ||
                    !read_u8(column.scale)) {
                    return false;
                }
                column.max_length = byte;
                break;

            case Tds5Type::longchar:
            case Tds5Type::longbinary:
                if (!read_u32(column.max_length)) return false;
                break;

            case Tds5Type::text:
            case Tds5Type::image:
            case Tds5Type::unitext:
            case Tds5Type::xml: {
                // Tamanho maximo e o nome do objeto de origem.
                std::uint16_t name_length = 0;
                if (!read_u32(column.max_length) || !read_u16(name_length) ||
                    !skip(name_length)) {
                    return false;
                }
                break;
            }

            default: {
                char code[8];
                std::snprintf(code, sizeof code, "0x%02X", type);
                stream_error_ =
                    fail(Errc::protocol_error, std::string("unknown TDS 5.0 data type ") + code);
                return false;
            }
        }
    }
    return true;
}

bool Tds5Connection::read_value(const Tds5Column& column, std::string& text, bool& null) {
    null = false;
    text.clear();
    const std::byte* p = nullptr;

    const auto unsigned_text = [](std::span<const std::byte> bytes) {
        return std::to_string(le(bytes, 0, bytes.size()));
    };
    const auto raw = [&text](const std::byte* data, std::size_t size) {
        text.assign(reinterpret_cast<const char*>(data), size);
    };

    // Valor ja' sem o prefixo de tamanho, pelo tipo e pelo tamanho que chegou.
    const auto format = [&](std::span<const std::byte> bytes) {
        switch (column.type) {
            case Tds5Type::int1:
            case Tds5Type::uint2:
            case Tds5Type::uint4:
            case Tds5Type::uint8:
            case Tds5Type::uintn:
                text = unsigned_text(bytes);
                break;
            case Tds5Type::sint1:
                text = std::to_string(static_cast<std::int8_t>(bytes[0]));
                break;
            case Tds5Type::int2:
            case Tds5Type::int4:
            case Tds5Type::int8:
            case Tds5Type::intn:
                text = format_integer(bytes);
                break;
            case Tds5Type::bit:
                text = static_cast<std::uint8_t>(bytes[0]) != 0 ? "1" : "0";
                break;
            case Tds5Type::float4:
            case Tds5Type::float8:
            case Tds5Type::fltn:
                text = format_float(bytes);
                break;
            case Tds5Type::money:
            case Tds5Type::shortmoney:
            case Tds5Type::moneyn:
                text = format_money(bytes);
                break;
            case Tds5Type::datetime:
            case Tds5Type::shortdate:
            case Tds5Type::datetimn:
                text = format_datetime(bytes);
                // date e time viajam como DATETIME: so' a metade que vale.
                if (column.user_type == kUserDate && text.size() > 10) {
                    text.resize(10);
                } else if (column.user_type == kUserTime && text.size() > 11) {
                    text.erase(0, 11);
                }
                break;
            case Tds5Type::date:
            case Tds5Type::daten:
                text = format_tds5_date(bytes);
                break;
            case Tds5Type::time:
            case Tds5Type::timen:
                text = format_tds5_time(bytes);
                break;
            case Tds5Type::bigdatetimen:
                text = format_tds5_bigdatetime(bytes);
                break;
            case Tds5Type::bigtimen:
                text = format_tds5_bigtime(bytes);
                break;
            case Tds5Type::decn:
            case Tds5Type::numn:
                text = format_tds5_numeric(bytes, column.scale);
                break;
            case Tds5Type::binary:
            case Tds5Type::varbinary:
                if (column.user_type == kUserGuid && bytes.size() == 16) {
                    text = guid_text(bytes);
                } else {
                    text = format_binary(bytes);
                }
                break;
            case Tds5Type::image:
                text = format_binary(bytes);
                break;
            case Tds5Type::longbinary:
                if (column.user_type == kUserUnichar || column.user_type == kUserUnivarchar) {
                    text = utf16le_to_utf8(bytes);
                } else {
                    text = format_binary(bytes);
                }
                break;
            case Tds5Type::unitext:
                text = utf16le_to_utf8(bytes);
                break;
            default:
                // char, varchar, longchar, text, xml: ja' no conjunto de
                // caracteres pedido no login (UTF-8).
                raw(bytes.data(), bytes.size());
                break;
        }
    };

    switch (column.type) {
        case Tds5Type::void_:
            null = true;
            return true;

        // Tamanho fixo: sem prefixo, nunca NULL.
        case Tds5Type::int1:
        case Tds5Type::sint1:
        case Tds5Type::bit:
        case Tds5Type::int2:
        case Tds5Type::uint2:
        case Tds5Type::int4:
        case Tds5Type::uint4:
        case Tds5Type::int8:
        case Tds5Type::uint8:
        case Tds5Type::float4:
        case Tds5Type::float8:
        case Tds5Type::money:
        case Tds5Type::shortmoney:
        case Tds5Type::datetime:
        case Tds5Type::shortdate:
        case Tds5Type::date:
        case Tds5Type::time:
            if (!take(column.max_length, p)) return false;
            format({p, column.max_length});
            return true;

        // Quatro bytes de tamanho.
        case Tds5Type::longchar:
        case Tds5Type::longbinary: {
            std::uint32_t length = 0;
            if (!read_u32(length)) return false;
            if (length == 0) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            format({p, length});
            return true;
        }

        // Ponteiro de texto, carimbo, e so' entao o dado.
        case Tds5Type::text:
        case Tds5Type::image:
        case Tds5Type::unitext:
        case Tds5Type::xml: {
            std::uint8_t pointer = 0;
            if (!read_u8(pointer)) return false;
            if (pointer == 0) {
                null = true;
                return true;
            }
            std::uint32_t length = 0;
            if (!skip(static_cast<std::size_t>(pointer) + 8) || !read_u32(length)) return false;
            // O SQL Anywhere manda o NULL de um tipo longo COM ponteiro e
            // tamanho zero. O texto vazio de verdade chega como um espaco (o
            // TDS nao tem como dizer "vazio"), entao zero aqui e' sempre NULL.
            if (length == 0) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            format({p, length});
            return true;
        }

        // Um byte de tamanho; zero e' NULL.
        default: {
            std::uint8_t length = 0;
            if (!read_u8(length)) return false;
            if (length == 0) {
                null = true;
                return true;
            }
            if (!take(length, p)) return false;
            format({p, length});
            return true;
        }
    }
}

bool Tds5Connection::read_eed(Message& message) {
    std::uint16_t length = 0;
    const std::byte* p = nullptr;
    if (!read_u16(length) || !take(length, p)) return false;
    const std::span<const std::byte> body{p, length};

    // numero(4) estado(1) classe(1) sqlstate(1+n) status(1) transtate(2)
    // mensagem(2+n) servidor(1+n) procedimento(1+n) linha(2)
    std::size_t at = 0;
    const auto need = [&body, &at](std::size_t size) { return at + size <= body.size(); };
    if (!need(7)) return true;
    message.number   = static_cast<std::int32_t>(le(body, 0, 4));
    message.state    = static_cast<std::uint8_t>(body[4]);
    message.severity = static_cast<std::uint8_t>(body[5]);
    at = 6;
    const std::size_t state_length = static_cast<std::size_t>(body[at]);
    at += 1 + state_length;
    if (!need(5)) return true;
    at += 3;   // status + transtate
    const std::size_t text_length = static_cast<std::size_t>(le(body, at, 2));
    at += 2;
    if (!need(text_length)) return true;
    message.text.assign(reinterpret_cast<const char*>(body.data() + at), text_length);
    at += text_length;
    if (!need(1)) return true;
    at += 1 + static_cast<std::size_t>(body[at]);          // servidor
    if (!need(1)) return true;
    const std::size_t proc_length = static_cast<std::size_t>(body[at]);
    if (need(1 + proc_length)) {
        message.procedure.assign(reinterpret_cast<const char*>(body.data() + at + 1),
                                 proc_length);
    }
    at += 1 + proc_length;
    if (need(2)) message.line = static_cast<std::int32_t>(le(body, at, 2));
    return true;
}

bool Tds5Connection::read_old_message(Message& message) {
    std::uint16_t length = 0;
    const std::byte* p = nullptr;
    if (!read_u16(length) || !take(length, p)) return false;
    const std::span<const std::byte> body{p, length};
    if (body.size() < 8) return true;
    message.number   = static_cast<std::int32_t>(le(body, 0, 4));
    message.state    = static_cast<std::uint8_t>(body[4]);
    message.severity = static_cast<std::uint8_t>(body[5]);
    const std::size_t text_length = static_cast<std::size_t>(le(body, 6, 2));
    if (8 + text_length <= body.size()) {
        message.text.assign(reinterpret_cast<const char*>(body.data() + 8), text_length);
    }
    return true;
}

Status Tds5Connection::read_response(const ColumnsCallback& on_columns,
                                     const RowCallback& on_row, bool login_phase) {
    inbox_.clear();
    in_pos_ = 0;
    message_complete_ = false;
    stream_error_ = {};
    if (!login_phase) affected_rows_ = -1;

    std::vector<Tds5Column>  columns;
    std::vector<Tds5Column>  parameters;
    std::vector<std::string> texts;
    std::vector<Value>       values;
    std::optional<Message>   first_error;
    bool cancelled = false;
    bool login_failed = false;
    bool statement_has_columns = false;

    // Os parametros OUT de uma procedure chamada por CALL chegam um a um, em
    // tokens RETURNVALUE. Sem conjunto de resultado no lote, viram um: uma
    // linha, uma coluna por parametro -- e' assim que a grade os mostra.
    bool any_result_set = false;
    std::vector<Tds5Column>  returned;
    std::vector<std::string> returned_text;
    std::vector<bool>        returned_null;

    const auto on_message = [&](Message message) {
        // Classe acima de 10 e' erro; ate' 10, aviso ou PRINT/MESSAGE.
        if (message.severity > 10) {
            if (!first_error) first_error = std::move(message);
        } else if (!message.text.empty()) {
            messages_.push_back(std::move(message));
        }
    };

    for (;;) {
        std::uint8_t token = 0;
        if (!read_u8(token)) return stream_error_;

        switch (token) {
            case kTokenRowFmt:
            case kTokenRowFmt2:
                if (!read_format(columns, token == kTokenRowFmt2)) return stream_error_;
                texts.assign(columns.size(), {});
                values.assign(columns.size(), {});
                statement_has_columns = true;
                any_result_set = true;
                if (on_columns) on_columns(columns);
                break;

            case kTokenReturnValue: {
                // tamanho(2) nome(1+n) estado(1) tipo de usuario(4) tipo valor
                std::uint16_t length = 0;
                std::uint8_t  status = 0;
                Tds5Column column;
                std::string text;
                bool null = false;
                if (!read_u16(length) || !read_string8(column.name) || !read_u8(status) ||
                    !read_u32(column.user_type) || !read_type(column) ||
                    !read_value(column, text, null)) {
                    return stream_error_;
                }
                // "@total" -> "total": o nome da coluna e' o do parametro.
                if (!column.name.empty() && column.name.front() == '@') {
                    column.name.erase(0, 1);
                }
                column.type_name = tds5_type_name(column);
                returned.push_back(std::move(column));
                returned_text.push_back(std::move(text));
                returned_null.push_back(null);
                break;
            }

            case kTokenRow:
                for (std::size_t c = 0; c < columns.size(); ++c) {
                    if (!read_value(columns[c], texts[c], values[c].null)) {
                        return stream_error_;
                    }
                }
                if (on_row) {
                    for (std::size_t c = 0; c < columns.size(); ++c) {
                        if (!values[c].null) values[c].text = texts[c];
                    }
                    on_row(values);
                }
                break;

            // Parametros de saida (e os dados extras de um EED): lidos para o
            // fluxo seguir alinhado.
            case kTokenParamFmt:
            case kTokenParamFmt2:
                if (!read_format(parameters, token == kTokenParamFmt2)) return stream_error_;
                break;
            case kTokenParams: {
                std::string text;
                bool null = false;
                for (const Tds5Column& parameter : parameters) {
                    if (!read_value(parameter, text, null)) return stream_error_;
                }
                break;
            }

            case kTokenDone:
            case kTokenDoneProc:
            case kTokenDoneInProc: {
                std::uint16_t status = 0, transaction = 0;
                std::uint32_t rows = 0;
                if (!read_u16(status) || !read_u16(transaction) || !read_u32(rows)) {
                    return stream_error_;
                }
                if ((status & kDoneCount) != 0 && !statement_has_columns && !login_phase) {
                    affected_rows_ = (affected_rows_ < 0 ? 0 : affected_rows_) +
                                     static_cast<std::int64_t>(rows);
                }
                statement_has_columns = false;
                in_transaction_ =
                    transaction == kTranProgress || transaction == kTranStmtAbort;
                if ((status & kDoneAttn) != 0) {
                    cancelled = true;
                    attention_pending_->store(false, std::memory_order_release);
                }
                break;
            }

            case kTokenEed: {
                Message message;
                if (!read_eed(message)) return stream_error_;
                on_message(std::move(message));
                break;
            }
            case kTokenError:
            case kTokenInfo: {
                Message message;
                if (!read_old_message(message)) return stream_error_;
                if (token == kTokenError && message.severity <= 10) message.severity = 16;
                on_message(std::move(message));
                break;
            }

            case kTokenEnvChange: {
                std::uint16_t length = 0;
                const std::byte* p = nullptr;
                if (!read_u16(length) || !take(length, p)) return stream_error_;
                const std::span<const std::byte> body{p, length};
                // Varias mudancas no mesmo token: tipo, valor novo, valor velho.
                std::size_t at = 0;
                while (at + 2 <= body.size()) {
                    const auto type = static_cast<std::uint8_t>(body[at]);
                    const std::size_t size = static_cast<std::size_t>(body[at + 1]);
                    if (at + 2 + size > body.size()) break;
                    const std::string value(
                        reinterpret_cast<const char*>(body.data() + at + 2), size);
                    at += 2 + size;
                    if (at < body.size()) at += 1 + static_cast<std::size_t>(body[at]);

                    if (type == 1) {
                        database_ = value;
                    } else if (type == 4) {
                        const long packet = std::strtol(value.c_str(), nullptr, 10);
                        if (packet >= 512 && packet <= 65535) {
                            packet_size_ = static_cast<std::uint32_t>(packet);
                        }
                    }
                }
                break;
            }

            case kTokenLoginAck: {
                std::uint16_t length = 0;
                const std::byte* p = nullptr;
                if (!read_u16(length) || !take(length, p)) return stream_error_;
                const std::span<const std::byte> body{p, length};
                // estado(1): 5 = aceito, 6 = recusado, 7 = negociar.
                // versao TDS(4), nome do programa(1+n), versao(4).
                if (!body.empty()) {
                    const auto status = static_cast<std::uint8_t>(body[0]);
                    logged_in_   = status == 5;
                    login_failed = status == 6;
                }
                if (body.size() >= 6) {
                    const std::size_t name = static_cast<std::size_t>(body[5]);
                    if (6 + name + 4 <= body.size()) {
                        server_name_.assign(reinterpret_cast<const char*>(body.data() + 6),
                                            name);
                        const std::size_t v = 6 + name;
                        char text[48];
                        std::snprintf(text, sizeof text, "%u.%u.%u.%u",
                                      static_cast<unsigned>(body[v]),
                                      static_cast<unsigned>(body[v + 1]),
                                      static_cast<unsigned>(body[v + 2]),
                                      static_cast<unsigned>(body[v + 3]));
                        server_version_ = text;
                    }
                }
                break;
            }

            case kTokenReturnStatus:
                if (!skip(4)) return stream_error_;
                break;

            case kTokenMsg: {
                std::uint8_t length = 0;
                if (!read_u8(length) || !skip(length)) return stream_error_;
                break;
            }

            case kTokenCapability:
            case kTokenOrderBy:
            case kTokenControl:
            case kTokenTabName:
            case kTokenColInfo:
            case kTokenDynamic:
            case kTokenCurInfo: {
                std::uint16_t length = 0;
                if (!read_u16(length) || !skip(length)) return stream_error_;
                break;
            }

            case kTokenOrderBy2:
            case kTokenDynamic2: {
                std::uint32_t length = 0;
                if (!read_u32(length) || !skip(length)) return stream_error_;
                break;
            }

            default: {
                char code[8];
                std::snprintf(code, sizeof code, "0x%02X", token);
                return fail(Errc::protocol_error,
                            std::string("unknown TDS 5.0 token ") + code);
            }
        }

        if (at_message_end()) {
            if (attention_pending_->load(std::memory_order_acquire) && !login_phase) {
                inbox_.clear();
                in_pos_ = 0;
                message_complete_ = false;
                continue;
            }
            break;
        }
    }

    if (!any_result_set && !returned.empty() && !first_error && !cancelled) {
        if (on_columns) on_columns(returned);
        if (on_row) {
            std::vector<Value> row(returned.size());
            for (std::size_t i = 0; i < returned.size(); ++i) {
                row[i].null = returned_null[i];
                if (!row[i].null) row[i].text = returned_text[i];
            }
            on_row(row);
        }
    }

    if (login_failed || (login_phase && !logged_in_)) {
        return fail(Errc::auth_failed,
                    first_error ? first_error->text : std::string("login failed"));
    }
    if (first_error) return std::unexpected(to_error(*first_error));
    if (cancelled) return fail(Errc::cancelled, "the query was cancelled");
    return {};
}

} // namespace otter::tdswire
