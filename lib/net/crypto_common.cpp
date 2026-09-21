// Codificacoes portaveis: nao dependem da API criptografica do sistema.
#include "net/crypto.hpp"

namespace otter::crypto {
namespace {

constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

constexpr int decode_base64_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::string base64_encode(std::span<const std::byte> data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const auto b0 = static_cast<std::uint32_t>(data[i]);
        const auto b1 = static_cast<std::uint32_t>(data[i + 1]);
        const auto b2 = static_cast<std::uint32_t>(data[i + 2]);
        const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
        out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
        out.push_back(kBase64Alphabet[triple & 0x3F]);
        i += 3;
    }

    const std::size_t left = data.size() - i;
    if (left == 1) {
        const auto b0 = static_cast<std::uint32_t>(data[i]);
        out.push_back(kBase64Alphabet[(b0 >> 2) & 0x3F]);
        out.push_back(kBase64Alphabet[(b0 << 4) & 0x3F]);
        out.append("==");
    } else if (left == 2) {
        const auto b0 = static_cast<std::uint32_t>(data[i]);
        const auto b1 = static_cast<std::uint32_t>(data[i + 1]);
        out.push_back(kBase64Alphabet[(b0 >> 2) & 0x3F]);
        out.push_back(kBase64Alphabet[((b0 << 4) | (b1 >> 4)) & 0x3F]);
        out.push_back(kBase64Alphabet[(b1 << 2) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

Result<std::vector<std::byte>> base64_decode(std::string_view text) {
    std::vector<std::byte> out;
    out.reserve((text.size() / 4) * 3);

    std::uint32_t accumulator = 0;
    int bits = 0;

    for (char c : text) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;

        const int value = decode_base64_char(c);
        if (value < 0) {
            return fail(Errc::parse_error,
                        std::string("caractere inválido em base64: '") + c + "'");
        }

        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

std::string hex_encode(std::span<const std::byte> data) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (std::byte b : data) {
        const auto value = static_cast<std::uint8_t>(b);
        out.push_back(kHex[value >> 4]);
        out.push_back(kHex[value & 0x0F]);
    }
    return out;
}

} // namespace otter::crypto
