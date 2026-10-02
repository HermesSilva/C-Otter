#include "tdswire/value.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace otter::tdswire {
namespace {

std::uint8_t u8(std::span<const std::byte> b, std::size_t at) {
    return static_cast<std::uint8_t>(b[at]);
}

std::uint64_t le(std::span<const std::byte> b, std::size_t at, std::size_t size) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value |= static_cast<std::uint64_t>(u8(b, at + i)) << (8 * i);
    }
    return value;
}

void append_utf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

// Dias desde 0001-01-01 -> (ano, mes, dia), calendario gregoriano proleptico.
void civil_from_days(std::int64_t days, int& year, unsigned& month, unsigned& day) {
    // Algoritmo de Howard Hinnant, com a epoca deslocada de 1970 para 0001.
    std::int64_t z = days - 719162 + 719468;   // dias desde 0000-03-01
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day   = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year  = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

std::string date_text(std::int64_t days_since_0001) {
    int year = 1;
    unsigned month = 1, day = 1;
    civil_from_days(days_since_0001, year, month, day);
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u", year, month, day);
    return buffer;
}

// `units` em 10^-scale segundos desde a meia-noite.
std::string time_text(std::uint64_t units, std::uint8_t scale) {
    std::uint64_t per_second = 1;
    for (std::uint8_t i = 0; i < scale; ++i) per_second *= 10;

    const std::uint64_t seconds  = units / per_second;
    const std::uint64_t fraction = units % per_second;

    char buffer[32];
    int length = std::snprintf(buffer, sizeof buffer, "%02u:%02u:%02u",
                               static_cast<unsigned>(seconds / 3600),
                               static_cast<unsigned>(seconds / 60 % 60),
                               static_cast<unsigned>(seconds % 60));
    if (scale > 0) {
        length += std::snprintf(buffer + length, sizeof buffer - static_cast<std::size_t>(length),
                                ".%0*llu", static_cast<int>(scale),
                                static_cast<unsigned long long>(fraction));
    }
    return std::string(buffer, static_cast<std::size_t>(length));
}

// Bytes do `time(n)` conforme a escala: 3 (0-2), 4 (3-4), 5 (5-7).
std::size_t time_bytes(std::uint8_t scale) {
    return scale <= 2 ? 3 : scale <= 4 ? 4 : 5;
}

constexpr std::int64_t kDaysTo1900 = 693595;   // de 0001-01-01 a 1900-01-01

} // namespace

std::string type_name(const TypeInfo& type) {
    switch (type.id) {
        case TypeId::null_:     return "null";
        case TypeId::int1:      return "tinyint";
        case TypeId::bit:
        case TypeId::bitn:      return "bit";
        case TypeId::int2:      return "smallint";
        case TypeId::int4:      return "int";
        case TypeId::int8:      return "bigint";
        case TypeId::intn:
            return type.max_length == 1 ? "tinyint" : type.max_length == 2 ? "smallint"
                 : type.max_length == 8 ? "bigint" : "int";
        case TypeId::datetime4: return "smalldatetime";
        case TypeId::datetime:  return "datetime";
        case TypeId::datetimen: return type.max_length == 4 ? "smalldatetime" : "datetime";
        case TypeId::float4:    return "real";
        case TypeId::float8:    return "float";
        case TypeId::floatn:    return type.max_length == 4 ? "real" : "float";
        case TypeId::money:     return "money";
        case TypeId::money4:    return "smallmoney";
        case TypeId::moneyn:    return type.max_length == 4 ? "smallmoney" : "money";
        case TypeId::guid:      return "uniqueidentifier";
        case TypeId::decimal:
        case TypeId::decimaln:  return "decimal";
        case TypeId::numeric:
        case TypeId::numericn:  return "numeric";
        case TypeId::daten:     return "date";
        case TypeId::timen:     return "time";
        case TypeId::datetime2n: return "datetime2";
        case TypeId::datetimeoffsetn: return "datetimeoffset";
        case TypeId::bigvarbinary: return "varbinary";
        case TypeId::bigbinary: return "binary";
        case TypeId::bigvarchar: return "varchar";
        case TypeId::bigchar:   return "char";
        case TypeId::nvarchar:  return "nvarchar";
        case TypeId::nchar:     return "nchar";
        case TypeId::image:     return "image";
        case TypeId::text:      return "text";
        case TypeId::ntext:     return "ntext";
        case TypeId::variant:   return "sql_variant";
        case TypeId::xml:       return "xml";
        case TypeId::udt:       return "udt";
    }
    return "unknown";
}

// --- Texto -------------------------------------------------------------------------

std::string utf16le_to_utf8(std::span<const std::byte> bytes) {
    std::string out;
    out.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        std::uint32_t code = static_cast<std::uint32_t>(le(bytes, i, 2));
        // Par substituto: dois codigos de 16 bits formam um ponto acima de U+FFFF.
        if (code >= 0xD800 && code <= 0xDBFF && i + 3 < bytes.size()) {
            const auto low = static_cast<std::uint32_t>(le(bytes, i + 2, 2));
            if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, code);
    }
    return out;
}

std::string utf8_to_utf16le(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    const auto put = [&out](std::uint32_t unit) {
        out += static_cast<char>(unit & 0xFF);
        out += static_cast<char>((unit >> 8) & 0xFF);
    };

    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t code = 0xFFFD;
        std::size_t   size = 1;
        if (lead < 0x80) {
            code = lead;
        } else if ((lead & 0xE0) == 0xC0 && i + 1 < text.size()) {
            code = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            size = 2;
        } else if ((lead & 0xF0) == 0xE0 && i + 2 < text.size()) {
            code = ((lead & 0x0Fu) << 12) |
                   ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            size = 3;
        } else if ((lead & 0xF8) == 0xF0 && i + 3 < text.size()) {
            code = ((lead & 0x07u) << 18) |
                   ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
                   ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
                   (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
            size = 4;
        }
        i += size;

        if (code >= 0x10000) {
            code -= 0x10000;
            put(0xD800 + (code >> 10));
            put(0xDC00 + (code & 0x3FF));
        } else {
            put(code);
        }
    }
    return out;
}

std::string ansi_to_utf8(std::span<const std::byte> bytes, std::uint32_t collation) {
    if (bytes.empty()) return {};

    // Collation UTF-8 (SQL Server 2019+): os bytes ja' sao UTF-8.
    constexpr std::uint32_t kUtf8Flag = 0x04000000;
    if ((collation & kUtf8Flag) != 0) {
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    // So' ASCII: igual em toda pagina de codigo, nao precisa converter.
    const bool ascii = std::all_of(bytes.begin(), bytes.end(), [](std::byte b) {
        return static_cast<unsigned char>(b) < 0x80;
    });
    if (ascii) return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());

#ifdef _WIN32
    // A pagina de codigo vem do LCID do collation (os 20 bits de baixo).
    const LCID lcid = collation & 0x000FFFFF;
    UINT code_page = 1252;
    DWORD value = 0;
    if (lcid != 0 &&
        GetLocaleInfoW(lcid, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                       reinterpret_cast<LPWSTR>(&value),
                       sizeof(value) / sizeof(wchar_t)) > 0 &&
        value != 0) {
        code_page = value;
    }

    const int wide_size = MultiByteToWideChar(
        code_page, 0, reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_size > 0) {
        std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
        MultiByteToWideChar(code_page, 0, reinterpret_cast<const char*>(bytes.data()),
                            static_cast<int>(bytes.size()), wide.data(), wide_size);
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, nullptr, 0,
                                             nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_size, out.data(), size, nullptr,
                            nullptr);
        return out;
    }
#endif

    // Sem tabela de paginas de codigo: Latin-1, que acerta o caso mais comum
    // (1252) fora da faixa 0x80-0x9F.
    std::string out;
    for (const std::byte b : bytes) append_utf8(out, static_cast<unsigned char>(b));
    return out;
}

// --- Valores -----------------------------------------------------------------------

std::string format_integer(std::span<const std::byte> bytes) {
    switch (bytes.size()) {
        case 1: return std::to_string(u8(bytes, 0));   // tinyint e' sem sinal
        case 2: return std::to_string(static_cast<std::int16_t>(le(bytes, 0, 2)));
        case 4: return std::to_string(static_cast<std::int32_t>(le(bytes, 0, 4)));
        case 8: return std::to_string(static_cast<std::int64_t>(le(bytes, 0, 8)));
        default: return {};
    }
}

std::string format_float(std::span<const std::byte> bytes) {
    char buffer[40];
    if (bytes.size() == 4) {
        float value = 0;
        std::memcpy(&value, bytes.data(), 4);
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
        return std::string(buffer, result.ptr);
    }
    if (bytes.size() == 8) {
        double value = 0;
        std::memcpy(&value, bytes.data(), 8);
        const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
        return std::string(buffer, result.ptr);
    }
    return {};
}

std::string format_money(std::span<const std::byte> bytes) {
    std::int64_t value = 0;
    if (bytes.size() == 4) {
        value = static_cast<std::int32_t>(le(bytes, 0, 4));
    } else if (bytes.size() == 8) {
        // Os 4 bytes ALTOS vem primeiro, depois os baixos -- cada um em
        // little-endian. Ler os 8 de uma vez da' um valor absurdo.
        const std::uint64_t high = le(bytes, 0, 4);
        const std::uint64_t low  = le(bytes, 4, 4);
        value = static_cast<std::int64_t>((high << 32) | low);
    } else {
        return {};
    }

    const bool negative = value < 0;
    const std::uint64_t magnitude =
        negative ? 0 - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%s%llu.%04llu", negative ? "-" : "",
                  static_cast<unsigned long long>(magnitude / 10000),
                  static_cast<unsigned long long>(magnitude % 10000));
    return buffer;
}

std::string format_decimal(std::span<const std::byte> bytes, std::uint8_t scale) {
    if (bytes.empty()) return {};

    // Sinal (1 = positivo) e a magnitude: inteiro little-endian de 4 a 16 bytes.
    const bool negative = u8(bytes, 0) == 0;
    std::array<std::uint32_t, 4> words{};
    for (std::size_t i = 1; i < bytes.size() && i <= 16; ++i) {
        words[(i - 1) / 4] |= static_cast<std::uint32_t>(u8(bytes, i)) << (8 * ((i - 1) % 4));
    }

    // Divisoes sucessivas por 10^9: nove digitos por vez, do menos significativo.
    std::string digits;
    while (words[0] != 0 || words[1] != 0 || words[2] != 0 || words[3] != 0) {
        std::uint64_t remainder = 0;
        for (int i = 3; i >= 0; --i) {
            const std::uint64_t current = (remainder << 32) | words[static_cast<std::size_t>(i)];
            words[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(current / 1000000000u);
            remainder = current % 1000000000u;
        }
        for (int d = 0; d < 9; ++d) {
            digits += static_cast<char>('0' + remainder % 10);
            remainder /= 10;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    if (digits.empty()) digits = "0";
    // `digits` esta' invertido; garante ao menos um digito antes da virgula.
    while (digits.size() <= scale) digits += '0';

    std::string out;
    if (negative && digits != std::string(digits.size(), '0')) out += '-';
    for (std::size_t i = digits.size(); i-- > 0;) {
        out += digits[i];
        if (i == scale && scale > 0) out += '.';
    }
    return out;
}

std::string format_datetime(std::span<const std::byte> bytes) {
    char buffer[40];
    if (bytes.size() == 4) {
        // smalldatetime: dias desde 1900 (sem sinal) e minutos do dia.
        const std::uint64_t days    = le(bytes, 0, 2);
        const std::uint64_t minutes = le(bytes, 2, 2);
        std::snprintf(buffer, sizeof buffer, "%s %02u:%02u:00",
                      date_text(kDaysTo1900 + static_cast<std::int64_t>(days)).c_str(),
                      static_cast<unsigned>(minutes / 60), static_cast<unsigned>(minutes % 60));
        return buffer;
    }
    if (bytes.size() == 8) {
        // datetime: dias desde 1900 (COM sinal) e tercos de milissegundo.
        const auto days = static_cast<std::int32_t>(le(bytes, 0, 4));
        const std::uint64_t ticks = le(bytes, 4, 4);
        const std::uint64_t seconds = ticks / 300;
        // 1/300 s -> milissegundos, arredondado como o servidor mostra (.000,
        // .003, .007).
        const auto millis = static_cast<unsigned>((ticks % 300) * 10 / 3);
        std::snprintf(buffer, sizeof buffer, "%s %02u:%02u:%02u.%03u",
                      date_text(kDaysTo1900 + days).c_str(),
                      static_cast<unsigned>(seconds / 3600),
                      static_cast<unsigned>(seconds / 60 % 60),
                      static_cast<unsigned>(seconds % 60), millis);
        return buffer;
    }
    return {};
}

std::string format_date(std::span<const std::byte> bytes) {
    if (bytes.size() != 3) return {};
    return date_text(static_cast<std::int64_t>(le(bytes, 0, 3)));
}

std::string format_time(std::span<const std::byte> bytes, std::uint8_t scale) {
    if (bytes.size() < 3 || bytes.size() > 5) return {};
    return time_text(le(bytes, 0, bytes.size()), scale);
}

std::string format_datetime2(std::span<const std::byte> bytes, std::uint8_t scale) {
    const std::size_t size = time_bytes(scale);
    if (bytes.size() != size + 3) return {};
    return date_text(static_cast<std::int64_t>(le(bytes, size, 3))) + " " +
           time_text(le(bytes, 0, size), scale);
}

std::string format_datetimeoffset(std::span<const std::byte> bytes, std::uint8_t scale) {
    const std::size_t size = time_bytes(scale);
    if (bytes.size() != size + 5) return {};

    // A hora vem em UTC; o deslocamento (minutos) diz a hora LOCAL de quem
    // gravou, que e' o que o servidor mostra.
    std::uint64_t per_second = 1;
    for (std::uint8_t i = 0; i < scale; ++i) per_second *= 10;
    const std::uint64_t per_day = per_second * 86400;

    auto units = static_cast<std::int64_t>(le(bytes, 0, size));
    auto days  = static_cast<std::int64_t>(le(bytes, size, 3));
    const auto offset = static_cast<std::int16_t>(le(bytes, size + 3, 2));

    units += static_cast<std::int64_t>(offset) * 60 * static_cast<std::int64_t>(per_second);
    while (units < 0)                                    { units += static_cast<std::int64_t>(per_day); --days; }
    while (units >= static_cast<std::int64_t>(per_day))  { units -= static_cast<std::int64_t>(per_day); ++days; }

    char zone[16];
    const int magnitude = offset < 0 ? -offset : offset;
    std::snprintf(zone, sizeof zone, " %c%02d:%02d", offset < 0 ? '-' : '+', magnitude / 60,
                  magnitude % 60);
    return date_text(days) + " " + time_text(static_cast<std::uint64_t>(units), scale) + zone;
}

std::string format_guid(std::span<const std::byte> bytes) {
    if (bytes.size() != 16) return {};
    // Os tres primeiros campos sao little-endian; os dois ultimos, na ordem.
    char buffer[40];
    std::snprintf(buffer, sizeof buffer,
                  "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                  static_cast<unsigned>(le(bytes, 0, 4)), static_cast<unsigned>(le(bytes, 4, 2)),
                  static_cast<unsigned>(le(bytes, 6, 2)), u8(bytes, 8), u8(bytes, 9),
                  u8(bytes, 10), u8(bytes, 11), u8(bytes, 12), u8(bytes, 13), u8(bytes, 14),
                  u8(bytes, 15));
    return buffer;
}

std::string format_binary(std::span<const std::byte> bytes) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "0x";
    out.reserve(bytes.size() * 2 + 2);
    for (const std::byte b : bytes) {
        const auto value = static_cast<unsigned char>(b);
        out += kHex[value >> 4];
        out += kHex[value & 0x0F];
    }
    return out;
}

std::string format_value(const TypeInfo& type, std::span<const std::byte> bytes) {
    switch (type.id) {
        case TypeId::null_:
            return {};
        case TypeId::int1:
        case TypeId::int2:
        case TypeId::int4:
        case TypeId::int8:
        case TypeId::intn:
            return format_integer(bytes);
        case TypeId::bit:
        case TypeId::bitn:
            return !bytes.empty() && u8(bytes, 0) != 0 ? "1" : "0";
        case TypeId::float4:
        case TypeId::float8:
        case TypeId::floatn:
            return format_float(bytes);
        case TypeId::money:
        case TypeId::money4:
        case TypeId::moneyn:
            return format_money(bytes);
        case TypeId::datetime:
        case TypeId::datetime4:
        case TypeId::datetimen:
            return format_datetime(bytes);
        case TypeId::decimal:
        case TypeId::numeric:
        case TypeId::decimaln:
        case TypeId::numericn:
            return format_decimal(bytes, type.scale);
        case TypeId::daten:
            return format_date(bytes);
        case TypeId::timen:
            return format_time(bytes, type.scale);
        case TypeId::datetime2n:
            return format_datetime2(bytes, type.scale);
        case TypeId::datetimeoffsetn:
            return format_datetimeoffset(bytes, type.scale);
        case TypeId::guid:
            return format_guid(bytes);
        case TypeId::bigvarchar:
        case TypeId::bigchar:
        case TypeId::text:
            return ansi_to_utf8(bytes, type.collation);
        case TypeId::nvarchar:
        case TypeId::nchar:
        case TypeId::ntext:
        case TypeId::xml:
            return utf16le_to_utf8(bytes);
        case TypeId::bigvarbinary:
        case TypeId::bigbinary:
        case TypeId::image:
        case TypeId::udt:
            return format_binary(bytes);
        case TypeId::variant:
            return format_variant(bytes);
    }
    return format_binary(bytes);
}

std::string format_variant(std::span<const std::byte> bytes) {
    if (bytes.size() < 2) return {};

    TypeInfo base;
    base.id = static_cast<TypeId>(u8(bytes, 0));
    const std::size_t properties = u8(bytes, 1);
    if (2 + properties > bytes.size()) return {};
    const std::span<const std::byte> props = bytes.subspan(2, properties);
    const std::span<const std::byte> data  = bytes.subspan(2 + properties);

    switch (base.id) {
        case TypeId::decimaln:
        case TypeId::numericn:
        case TypeId::decimal:
        case TypeId::numeric:
            if (props.size() >= 2) base.scale = u8(props, 1);
            break;
        case TypeId::timen:
        case TypeId::datetime2n:
        case TypeId::datetimeoffsetn:
            if (!props.empty()) base.scale = u8(props, 0);
            break;
        case TypeId::bigvarchar:
        case TypeId::bigchar:
        case TypeId::nvarchar:
        case TypeId::nchar:
            if (props.size() >= 4) base.collation = static_cast<std::uint32_t>(le(props, 0, 4));
            break;
        default:
            break;
    }
    return format_value(base, data);
}

std::string obfuscate_password(std::string_view utf16le) {
    std::string out(utf16le);
    for (char& c : out) {
        const auto byte = static_cast<unsigned char>(c);
        c = static_cast<char>(static_cast<unsigned char>((byte << 4) | (byte >> 4)) ^ 0xA5);
    }
    return out;
}

} // namespace otter::tdswire
