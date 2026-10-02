// C-Otter -- lib/orawire/value.hpp
//
// Os valores do Oracle como chegam no fio, virados em texto.
//
// O servidor manda cada tipo no formato INTERNO dele -- NUMBER em base 100 com
// expoente, DATE em sete bytes com seculo e ano deslocados de 100 --, e nao em
// texto como o PostgreSQL. Funcoes puras: o teste confere cada formato sem
// servidor.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace otter::orawire {

// Os numeros de tipo do Oracle que o driver conhece.
enum class OraType : std::uint8_t {
    varchar       = 1,
    number        = 2,
    binary_integer = 3,
    long_         = 8,
    rowid         = 11,
    date          = 12,
    raw           = 23,
    long_raw      = 24,
    char_         = 96,
    binary_float  = 100,
    binary_double = 101,
    cursor        = 102,
    object        = 109,
    clob          = 112,
    blob          = 113,
    bfile         = 114,
    json          = 119,
    vector        = 127,
    timestamp     = 180,
    timestamp_tz  = 181,
    interval_ym   = 182,
    interval_ds   = 183,
    urowid        = 208,
    timestamp_ltz = 231,
    boolean       = 252,
};

// Forma do conjunto de caracteres: 1 = o do banco, 2 = o nacional (NCHAR).
inline constexpr std::uint8_t kCharsetFormImplicit = 1;
inline constexpr std::uint8_t kCharsetFormNational = 2;

// NUMBER: "0", "-12.5", "0.001". Ate' 40 digitos, sem notacao cientifica --
// e' um decimal exato, e passar por double o estragaria.
[[nodiscard]] std::string decode_number(std::span<const std::byte> data);

// DATE (7 bytes), TIMESTAMP (11) e TIMESTAMP WITH TIME ZONE (13):
// "2024-01-31 13:45:00", com ".123456" quando ha' fracao e " +02:00" quando
// ha' fuso. O valor com fuso chega em UTC e e' mostrado na hora LOCAL dele,
// como o SQL*Plus mostra.
[[nodiscard]] std::string decode_datetime(std::span<const std::byte> data);

// INTERVAL DAY TO SECOND: "+1 02:03:04.5"; YEAR TO MONTH: "+1-06".
[[nodiscard]] std::string decode_interval_ds(std::span<const std::byte> data);
[[nodiscard]] std::string decode_interval_ym(std::span<const std::byte> data);

[[nodiscard]] std::string decode_binary_float(std::span<const std::byte> data);
[[nodiscard]] std::string decode_binary_double(std::span<const std::byte> data);

// ROWID fisico, nos 18 caracteres em que o Oracle o escreve.
[[nodiscard]] std::string encode_rowid(std::uint32_t object, std::uint16_t file,
                                       std::uint32_t block, std::uint16_t slot);

// UROWID como chega (o primeiro byte diz se e' fisico ou logico).
[[nodiscard]] std::string decode_urowid(std::span<const std::byte> data);

// Os tipos NCHAR chegam em UTF-16 big-endian (AL16UTF16).
[[nodiscard]] std::string utf16be_to_utf8(std::span<const std::byte> data);

// O nome do tipo como o Oracle o escreve: "VARCHAR2", "NUMBER", "NCLOB".
[[nodiscard]] std::string_view type_name(OraType type, std::uint8_t charset_form) noexcept;

} // namespace otter::orawire
