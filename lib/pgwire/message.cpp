#include "pgwire/message.hpp"

#include <cstring>

namespace otter::pgwire {
namespace {

// O protocolo usa big-endian ("network byte order") em todos os inteiros.
void write_be32(std::byte* out, std::int32_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    out[0] = static_cast<std::byte>((v >> 24) & 0xFF);
    out[1] = static_cast<std::byte>((v >> 16) & 0xFF);
    out[2] = static_cast<std::byte>((v >> 8) & 0xFF);
    out[3] = static_cast<std::byte>(v & 0xFF);
}

} // namespace

void MessageWriter::put_int8(std::uint8_t value) {
    buffer_.push_back(static_cast<std::byte>(value));
}

void MessageWriter::put_int16(std::int16_t value) {
    const auto v = static_cast<std::uint16_t>(value);
    buffer_.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buffer_.push_back(static_cast<std::byte>(v & 0xFF));
}

void MessageWriter::put_int32(std::int32_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    buffer_.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    buffer_.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    buffer_.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    buffer_.push_back(static_cast<std::byte>(v & 0xFF));
}

void MessageWriter::put_string(std::string_view text) {
    for (char c : text) buffer_.push_back(static_cast<std::byte>(c));
    buffer_.push_back(std::byte{0});   // terminador exigido pelo protocolo
}

void MessageWriter::put_bytes(std::span<const std::byte> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::span<const std::byte> MessageWriter::finish() {
    // O campo de tamanho conta a si proprio e o corpo, mas NAO o byte de tipo.
    const std::size_t offset = (type_ != 0) ? 1 : 0;
    const auto length = static_cast<std::int32_t>(buffer_.size() - offset);
    write_be32(buffer_.data() + offset, length);
    return buffer_;
}

std::uint8_t MessageReader::read_int8() {
    if (remaining() < 1) { overflow_ = true; return 0; }
    return static_cast<std::uint8_t>(body_[position_++]);
}

std::int16_t MessageReader::read_int16() {
    if (remaining() < 2) { overflow_ = true; return 0; }
    const auto hi = static_cast<std::uint16_t>(body_[position_]);
    const auto lo = static_cast<std::uint16_t>(body_[position_ + 1]);
    position_ += 2;
    return static_cast<std::int16_t>((hi << 8) | lo);
}

std::int32_t MessageReader::read_int32() {
    if (remaining() < 4) { overflow_ = true; return 0; }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<std::uint32_t>(body_[position_ + static_cast<std::size_t>(i)]);
    }
    position_ += 4;
    return static_cast<std::int32_t>(value);
}

std::string_view MessageReader::read_string() {
    const std::size_t start = position_;
    while (position_ < body_.size() && body_[position_] != std::byte{0}) {
        ++position_;
    }
    if (position_ >= body_.size()) {
        overflow_ = true;
        return {};
    }
    const std::string_view text(
        reinterpret_cast<const char*>(body_.data() + start), position_ - start);
    ++position_;   // consome o terminador
    return text;
}

std::span<const std::byte> MessageReader::read_bytes(std::size_t count) {
    if (remaining() < count) { overflow_ = true; return {}; }
    const auto result = body_.subspan(position_, count);
    position_ += count;
    return result;
}

std::string ErrorInfo::to_string() const {
    std::string out = message;
    if (!sqlstate.empty()) out += " [" + sqlstate + "]";
    if (!detail.empty())   out += "\n  detalhe: " + detail;
    if (!hint.empty())     out += "\n  dica: " + hint;
    if (!position.empty()) out += "\n  posição: " + position;
    return out;
}

ErrorInfo parse_error_response(std::span<const std::byte> body) {
    // Sequencia de campos [codigo: 1 byte][valor: string], terminada por 0.
    ErrorInfo info;
    MessageReader reader(body);

    while (!reader.exhausted()) {
        const auto field = static_cast<char>(reader.read_int8());
        if (field == '\0' || reader.overflowed()) break;

        const std::string_view value = reader.read_string();
        switch (field) {
            case 'S': info.severity = value; break;
            case 'C': info.sqlstate = value; break;
            case 'M': info.message  = value; break;
            case 'D': info.detail   = value; break;
            case 'H': info.hint     = value; break;
            case 'P': info.position = value; break;
            case 'W': info.where    = value; break;
            default:  break;   // campos nao usados sao ignorados por design
        }
    }
    return info;
}

} // namespace otter::pgwire
