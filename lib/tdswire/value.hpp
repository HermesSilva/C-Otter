// C-Otter -- lib/tdswire/value.hpp
//
// Os tipos do TDS e a conversao dos valores para texto.
//
// O TDS manda os valores em BINARIO (ao contrario do protocolo simples do
// PostgreSQL e do MySQL, que mandam texto): um `int` chega como 4 bytes, um
// `decimal` como sinal + inteiro de ate' 16 bytes, um `datetime` como dias
// desde 1900 + tercos de milissegundo. A grade do C-Otter trabalha com texto
// -- e' aqui que a conversao acontece, e e' o que os testes conferem com os
// vetores do proprio servidor.
//
// Referencia: [MS-TDS] 2.2.5 (Data Type Definitions).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace otter::tdswire {

// Os tipos que aparecem em COLMETADATA (TYPE_INFO).
enum class TypeId : std::uint8_t {
    null_         = 0x1F,
    int1          = 0x30,
    bit           = 0x32,
    int2          = 0x34,
    int4          = 0x38,
    datetime4     = 0x3A,   // smalldatetime
    float4        = 0x3B,
    money         = 0x3C,
    datetime      = 0x3D,
    float8        = 0x3E,
    money4        = 0x7A,
    int8          = 0x7F,

    guid          = 0x24,
    intn          = 0x26,
    decimal       = 0x37,
    numeric       = 0x3F,
    bitn          = 0x68,
    decimaln      = 0x6A,
    numericn      = 0x6C,
    floatn        = 0x6D,
    moneyn        = 0x6E,
    datetimen     = 0x6F,

    daten         = 0x28,
    timen         = 0x29,
    datetime2n    = 0x2A,
    datetimeoffsetn = 0x2B,

    bigvarbinary  = 0xA5,
    bigvarchar    = 0xA7,
    bigbinary     = 0xAD,
    bigchar       = 0xAF,
    nvarchar      = 0xE7,
    nchar         = 0xEF,

    image         = 0x22,
    text          = 0x23,
    ntext         = 0x63,
    variant       = 0x62,
    xml           = 0xF1,
    udt           = 0xF0,
};

// Como o valor de uma coluna e' lido do fluxo.
struct TypeInfo {
    TypeId        id = TypeId::null_;
    std::uint32_t max_length = 0;     // bytes; 0xFFFF nos tipos (MAX)
    std::uint8_t  precision = 0;
    std::uint8_t  scale = 0;
    std::uint32_t collation = 0;      // LCID (20 bits) + flags
    std::uint8_t  sort_id = 0;
    bool          plp = false;        // (MAX), xml, udt: valor em pedacos
};

// O nome do tipo como o SQL Server o escreve: "int", "nvarchar", "datetime2".
[[nodiscard]] std::string type_name(const TypeInfo& type);

// --- Texto -------------------------------------------------------------------------

// UTF-8 <-> UTF-16LE (o TDS manda todo nome e todo nvarchar em UTF-16LE).
[[nodiscard]] std::string utf16le_to_utf8(std::span<const std::byte> bytes);
[[nodiscard]] std::string utf8_to_utf16le(std::string_view text);

// varchar/char/text: bytes na pagina de codigo do collation -> UTF-8.
[[nodiscard]] std::string ansi_to_utf8(std::span<const std::byte> bytes,
                                       std::uint32_t collation);

// --- Valores (os bytes do valor, ja' sem o prefixo de tamanho) ----------------------

[[nodiscard]] std::string format_integer(std::span<const std::byte> bytes);   // 1,2,4,8
[[nodiscard]] std::string format_float(std::span<const std::byte> bytes);     // 4, 8
[[nodiscard]] std::string format_money(std::span<const std::byte> bytes);     // 4, 8
[[nodiscard]] std::string format_decimal(std::span<const std::byte> bytes,
                                         std::uint8_t scale);
[[nodiscard]] std::string format_datetime(std::span<const std::byte> bytes);  // 4, 8
[[nodiscard]] std::string format_date(std::span<const std::byte> bytes);      // 3
[[nodiscard]] std::string format_time(std::span<const std::byte> bytes,
                                      std::uint8_t scale);
[[nodiscard]] std::string format_datetime2(std::span<const std::byte> bytes,
                                           std::uint8_t scale);
[[nodiscard]] std::string format_datetimeoffset(std::span<const std::byte> bytes,
                                                std::uint8_t scale);
[[nodiscard]] std::string format_guid(std::span<const std::byte> bytes);      // 16
[[nodiscard]] std::string format_binary(std::span<const std::byte> bytes);    // 0x...

// O valor de uma coluna de tamanho fixo ou com prefixo ja' retirado, pelo tipo.
[[nodiscard]] std::string format_value(const TypeInfo& type,
                                       std::span<const std::byte> bytes);

// sql_variant: tipo base + propriedades + dados.
[[nodiscard]] std::string format_variant(std::span<const std::byte> bytes);

// A senha do LOGIN7: cada byte com os nibbles trocados e XOR 0xA5. NAO e'
// criptografia -- e' o que o protocolo define; quem protege a senha e' o TLS
// do pacote de login.
[[nodiscard]] std::string obfuscate_password(std::string_view utf16le);

} // namespace otter::tdswire
