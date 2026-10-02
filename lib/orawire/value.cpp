#include "orawire/value.hpp"

#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>

namespace otter::orawire {
namespace {

unsigned u8(std::span<const std::byte> data, std::size_t at) {
    return std::to_integer<unsigned>(data[at]);
}

std::uint32_t be32(std::span<const std::byte> data, std::size_t at) {
    return (u8(data, at) << 24) | (u8(data, at + 1) << 16) | (u8(data, at + 2) << 8) |
           u8(data, at + 3);
}

std::uint16_t be16(std::span<const std::byte> data, std::size_t at) {
    return static_cast<std::uint16_t>((u8(data, at) << 8) | u8(data, at + 1));
}

// Dias desde 1970-01-01 e a volta (calendario gregoriano proleptico): so'
// para deslocar um TIMESTAMP WITH TIME ZONE de UTC para a hora local dele.
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

// ".123456": a fracao em nanossegundos, sem os zeros do fim.
void append_fraction(std::string& out, std::uint32_t nanos) {
    if (nanos == 0) return;
    char digits[16];
    std::snprintf(digits, sizeof(digits), ".%09u", nanos);
    std::size_t size = std::strlen(digits);
    while (size > 1 && digits[size - 1] == '0') --size;
    out.append(digits, size);
}

template <typename Float>
std::string format_float(Float value) {
    std::array<char, 40> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

constexpr std::string_view kRowidAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void append_base64(std::string& out, std::uint32_t value, int size) {
    const std::size_t start = out.size();
    out.resize(start + static_cast<std::size_t>(size));
    for (int i = size - 1; i >= 0; --i) {
        out[start + static_cast<std::size_t>(i)] = kRowidAlphabet[value & 0x3F];
        value >>= 6;
    }
}

} // namespace

std::string decode_number(std::span<const std::byte> data) {
    // 1 byte de expoente e ate' 20 de mantissa.
    if (data.empty() || data.size() > 21) return {};

    // O bit alto do expoente e' o sinal; no negativo os bits vem invertidos.
    const bool positive = (u8(data, 0) & 0x80) != 0;
    const int exponent = static_cast<int>(positive ? u8(data, 0) : (~u8(data, 0) & 0xFF)) - 193;
    int decimal_point = exponent * 2 + 2;

    // So' o expoente: zero, ou o menor valor possivel (-1e126).
    if (data.size() == 1) return positive ? "0" : "-1e126";

    std::size_t size = data.size();
    // O negativo termina com um byte 102, que nao e' digito.
    if (!positive && u8(data, size - 1) == 102) --size;

    // Cada byte da mantissa e' um digito em base 100.
    std::array<std::uint8_t, 44> digits{};
    int count = 0;
    for (std::size_t i = 1; i < size; ++i) {
        const unsigned pair = positive ? u8(data, i) - 1 : 101 - u8(data, i);

        unsigned digit = pair / 10;
        if (digit == 0 && count == 0) {
            --decimal_point;                       // zero 'a esquerda
        } else if (digit == 10) {
            digits[static_cast<std::size_t>(count++)] = 1;
            digits[static_cast<std::size_t>(count++)] = 0;
            ++decimal_point;
        } else {
            digits[static_cast<std::size_t>(count++)] = static_cast<std::uint8_t>(digit);
        }

        digit = pair % 10;
        if (digit != 0 || i < size - 1) {          // zero 'a direita nao conta
            digits[static_cast<std::size_t>(count++)] = static_cast<std::uint8_t>(digit);
        }
    }

    std::string out;
    if (!positive) out.push_back('-');
    if (decimal_point <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-decimal_point), '0');
    }
    for (int i = 0; i < count; ++i) {
        if (i > 0 && i == decimal_point) out.push_back('.');
        out.push_back(static_cast<char>('0' + digits[static_cast<std::size_t>(i)]));
    }
    if (decimal_point > count) out.append(static_cast<std::size_t>(decimal_point - count), '0');
    return out;
}

std::string decode_datetime(std::span<const std::byte> data) {
    if (data.size() < 7) return {};

    std::int64_t year = (static_cast<int>(u8(data, 0)) - 100) * 100 +
                        (static_cast<int>(u8(data, 1)) - 100);
    unsigned month  = u8(data, 2);
    unsigned day    = u8(data, 3);
    int      hour   = static_cast<int>(u8(data, 4)) - 1;
    int      minute = static_cast<int>(u8(data, 5)) - 1;
    const int second = static_cast<int>(u8(data, 6)) - 1;
    const std::uint32_t nanos = data.size() >= 11 ? be32(data, 7) : 0;

    std::string zone;
    if (data.size() >= 13 && u8(data, 11) != 0 && u8(data, 12) != 0) {
        if ((u8(data, 11) & 0x80) != 0) {
            // Fuso por NOME de regiao: o deslocamento depende de uma tabela
            // que o cliente nao tem. Fica a hora em UTC, dita como tal.
            zone = " UTC";
        } else {
            const int zone_hour   = static_cast<int>(u8(data, 11)) - 20;
            const int zone_minute = static_cast<int>(u8(data, 12)) - 60;
            const int offset = zone_hour * 60 + zone_minute;

            std::int64_t minutes =
                days_from_civil(year, month, day) * 1440 + hour * 60 + minute + offset;
            std::int64_t days = minutes / 1440;
            minutes %= 1440;
            if (minutes < 0) { minutes += 1440; --days; }
            civil_from_days(days, year, month, day);
            hour   = static_cast<int>(minutes / 60);
            minute = static_cast<int>(minutes % 60);

            char text[16];
            std::snprintf(text, sizeof(text), " %c%02d:%02d", offset < 0 ? '-' : '+',
                          (offset < 0 ? -offset : offset) / 60,
                          (offset < 0 ? -offset : offset) % 60);
            zone = text;
        }
    }

    char text[40];
    std::snprintf(text, sizeof(text), "%04lld-%02u-%02u %02d:%02d:%02d",
                  static_cast<long long>(year), month, day, hour, minute, second);
    std::string out = text;
    append_fraction(out, nanos);
    out += zone;
    return out;
}

std::string decode_interval_ds(std::span<const std::byte> data) {
    if (data.size() < 11) return {};
    // Cada campo vem deslocado; o sinal e' o mesmo em todos.
    const std::int64_t days  = static_cast<std::int64_t>(be32(data, 0)) - 0x80000000LL;
    const int hours   = static_cast<int>(u8(data, 4)) - 60;
    const int minutes = static_cast<int>(u8(data, 5)) - 60;
    const int seconds = static_cast<int>(u8(data, 6)) - 60;
    const std::int64_t nanos = static_cast<std::int64_t>(be32(data, 7)) - 0x80000000LL;

    const bool negative = days < 0 || hours < 0 || minutes < 0 || seconds < 0 || nanos < 0;
    const auto abs = [](std::int64_t v) { return v < 0 ? -v : v; };

    char text[48];
    std::snprintf(text, sizeof(text), "%c%lld %02d:%02d:%02d", negative ? '-' : '+',
                  static_cast<long long>(abs(days)), static_cast<int>(abs(hours)),
                  static_cast<int>(abs(minutes)), static_cast<int>(abs(seconds)));
    std::string out = text;
    append_fraction(out, static_cast<std::uint32_t>(abs(nanos)));
    return out;
}

std::string decode_interval_ym(std::span<const std::byte> data) {
    if (data.size() < 5) return {};
    const std::int64_t years = static_cast<std::int64_t>(be32(data, 0)) - 0x80000000LL;
    const int months = static_cast<int>(u8(data, 4)) - 60;
    const bool negative = years < 0 || months < 0;

    char text[32];
    std::snprintf(text, sizeof(text), "%c%lld-%02d", negative ? '-' : '+',
                  static_cast<long long>(years < 0 ? -years : years),
                  months < 0 ? -months : months);
    return text;
}

std::string decode_binary_float(std::span<const std::byte> data) {
    if (data.size() < 4) return {};
    // O Oracle guarda o IEEE 754 de modo que os bytes ordenem como o numero:
    // positivo com o bit de sinal ligado, negativo com tudo invertido.
    std::uint32_t bits = be32(data, 0);
    bits = (bits & 0x80000000u) != 0 ? bits & 0x7FFFFFFFu : ~bits;
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return format_float(value);
}

std::string decode_binary_double(std::span<const std::byte> data) {
    if (data.size() < 8) return {};
    std::uint64_t bits = (static_cast<std::uint64_t>(be32(data, 0)) << 32) | be32(data, 4);
    bits = (bits & 0x8000000000000000ull) != 0 ? bits & 0x7FFFFFFFFFFFFFFFull : ~bits;
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return format_float(value);
}

std::string encode_rowid(std::uint32_t object, std::uint16_t file, std::uint32_t block,
                         std::uint16_t slot) {
    std::string out;
    out.reserve(18);
    append_base64(out, object, 6);
    append_base64(out, file, 3);
    append_base64(out, block, 6);
    append_base64(out, slot, 3);
    return out;
}

std::string decode_urowid(std::span<const std::byte> data) {
    if (data.empty()) return {};

    if (u8(data, 0) == 1 && data.size() >= 13) {
        return encode_rowid(be32(data, 1), be16(data, 5), be32(data, 7), be16(data, 11));
    }

    // Logico (tabela organizada por indice): '*' e a chave em base 64, sem
    // preenchimento no fim.
    std::string out = "*";
    for (std::size_t i = 1; i < data.size(); i += 3) {
        const std::size_t left = data.size() - i;
        out.push_back(kRowidAlphabet[u8(data, i) >> 2]);
        unsigned pos = (u8(data, i) & 0x3) << 4;
        if (left == 1) { out.push_back(kRowidAlphabet[pos]); break; }
        pos |= (u8(data, i + 1) & 0xF0) >> 4;
        out.push_back(kRowidAlphabet[pos]);
        pos = (u8(data, i + 1) & 0xF) << 2;
        if (left == 2) { out.push_back(kRowidAlphabet[pos]); break; }
        pos |= (u8(data, i + 2) & 0xC0) >> 6;
        out.push_back(kRowidAlphabet[pos]);
        out.push_back(kRowidAlphabet[u8(data, i + 2) & 0x3F]);
    }
    return out;
}

std::string utf16be_to_utf8(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size());

    const auto emit = [&out](std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };

    for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
        std::uint32_t unit = be16(data, i);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < data.size()) {
            const std::uint32_t low = be16(data, i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        // Um substituto solto nao tem UTF-8 valido: vira U+FFFD.
        if (unit >= 0xD800 && unit <= 0xDFFF) unit = 0xFFFD;
        emit(unit);
    }
    return out;
}

std::string_view type_name(OraType type, std::uint8_t charset_form) noexcept {
    const bool national = charset_form == kCharsetFormNational;
    switch (type) {
        case OraType::varchar:        return national ? "NVARCHAR2" : "VARCHAR2";
        case OraType::char_:          return national ? "NCHAR" : "CHAR";
        case OraType::clob:           return national ? "NCLOB" : "CLOB";
        case OraType::number:         return "NUMBER";
        case OraType::binary_integer: return "BINARY_INTEGER";
        case OraType::long_:          return "LONG";
        case OraType::rowid:          return "ROWID";
        case OraType::date:           return "DATE";
        case OraType::raw:            return "RAW";
        case OraType::long_raw:       return "LONG RAW";
        case OraType::binary_float:   return "BINARY_FLOAT";
        case OraType::binary_double:  return "BINARY_DOUBLE";
        case OraType::cursor:         return "REF CURSOR";
        case OraType::object:         return "OBJECT";
        case OraType::blob:           return "BLOB";
        case OraType::bfile:          return "BFILE";
        case OraType::json:           return "JSON";
        case OraType::vector:         return "VECTOR";
        case OraType::timestamp:      return "TIMESTAMP";
        case OraType::timestamp_tz:   return "TIMESTAMP WITH TIME ZONE";
        case OraType::timestamp_ltz:  return "TIMESTAMP WITH LOCAL TIME ZONE";
        case OraType::interval_ym:    return "INTERVAL YEAR TO MONTH";
        case OraType::interval_ds:    return "INTERVAL DAY TO SECOND";
        case OraType::urowid:         return "UROWID";
        case OraType::boolean:        return "BOOLEAN";
    }
    return "UNKNOWN";
}

} // namespace otter::orawire
