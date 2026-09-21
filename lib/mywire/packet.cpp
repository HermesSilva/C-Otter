#include "mywire/packet.hpp"

#include <algorithm>
#include <cstring>

namespace otter::mywire {
namespace {

// Todo inteiro do protocolo e' little-endian. As funcoes abaixo existem para
// que isso apareca UMA vez, em vez de espalhado em deslocamentos manuais.
void append_le(std::vector<std::byte>& out, std::uint64_t value,
               std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
    }
}

} // namespace

// --- PacketWriter -------------------------------------------------------------

void PacketWriter::put_u8(std::uint8_t value)   { append_le(buffer_, value, 1); }
void PacketWriter::put_u16(std::uint16_t value) { append_le(buffer_, value, 2); }
void PacketWriter::put_u24(std::uint32_t value) { append_le(buffer_, value, 3); }
void PacketWriter::put_u32(std::uint32_t value) { append_le(buffer_, value, 4); }
void PacketWriter::put_u64(std::uint64_t value) { append_le(buffer_, value, 8); }

void PacketWriter::put_length(std::uint64_t value) {
    // O limiar de 251 (e nao 256) e' do protocolo: 0xFB..0xFF sao prefixos
    // reservados, entao um byte so' cobre ate' 250.
    if (value < 251) {
        put_u8(static_cast<std::uint8_t>(value));
    } else if (value <= 0xFFFF) {
        put_u8(0xFC);
        put_u16(static_cast<std::uint16_t>(value));
    } else if (value <= 0xFFFFFF) {
        put_u8(0xFD);
        put_u24(static_cast<std::uint32_t>(value));
    } else {
        put_u8(0xFE);
        put_u64(value);
    }
}

void PacketWriter::put_string(std::string_view text) {
    put_bytes({reinterpret_cast<const std::byte*>(text.data()), text.size()});
    put_u8(0);
}

void PacketWriter::put_length_string(std::string_view text) {
    put_length(text.size());
    put_bytes({reinterpret_cast<const std::byte*>(text.data()), text.size()});
}

void PacketWriter::put_bytes(std::span<const std::byte> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

void PacketWriter::fill(std::size_t count, std::byte value) {
    buffer_.insert(buffer_.end(), count, value);
}

// --- Enquadramento ------------------------------------------------------------

std::vector<std::byte> frame(std::span<const std::byte> body,
                             std::uint8_t& sequence) {
    std::vector<std::byte> out;
    out.reserve(body.size() + 4 * (body.size() / kMaxPayload + 1));

    std::size_t offset = 0;
    for (;;) {
        const std::size_t chunk = std::min(kMaxPayload, body.size() - offset);

        append_le(out, chunk, 3);
        out.push_back(static_cast<std::byte>(sequence++));
        out.insert(out.end(), body.begin() + static_cast<std::ptrdiff_t>(offset),
                   body.begin() + static_cast<std::ptrdiff_t>(offset + chunk));
        offset += chunk;

        // Para quando este pacote NAO estava cheio. Se estava, ha' mais por
        // vir -- ainda que o que falte seja nada, caso em que emitimos um
        // pacote de corpo vazio. E' assim que o servidor sabe que acabou.
        if (chunk < kMaxPayload) break;
    }
    return out;
}

// --- PacketReader -------------------------------------------------------------

std::uint8_t PacketReader::peek() const noexcept {
    if (position_ >= body_.size()) return 0;
    return static_cast<std::uint8_t>(body_[position_]);
}

std::span<const std::byte> PacketReader::read_bytes(std::size_t count) {
    if (remaining() < count) {
        overflow_ = true;
        position_ = body_.size();
        return {};
    }
    const std::span<const std::byte> out = body_.subspan(position_, count);
    position_ += count;
    return out;
}

void PacketReader::skip(std::size_t count) { (void)read_bytes(count); }

namespace {

std::uint64_t read_le(PacketReader& reader, std::size_t bytes) {
    const std::span<const std::byte> data = reader.read_bytes(bytes);
    if (data.size() != bytes) return 0;

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

} // namespace

std::uint8_t  PacketReader::read_u8()  { return static_cast<std::uint8_t>(read_le(*this, 1)); }
std::uint16_t PacketReader::read_u16() { return static_cast<std::uint16_t>(read_le(*this, 2)); }
std::uint32_t PacketReader::read_u24() { return static_cast<std::uint32_t>(read_le(*this, 3)); }
std::uint32_t PacketReader::read_u32() { return static_cast<std::uint32_t>(read_le(*this, 4)); }
std::uint64_t PacketReader::read_u64() { return read_le(*this, 8); }

std::uint64_t PacketReader::read_length(bool* null) {
    if (null) *null = false;

    const std::uint8_t first = read_u8();
    if (first < 251) return first;

    switch (first) {
        case 0xFC: return read_u16();
        case 0xFD: return read_u24();
        case 0xFE: return read_u64();
        case 0xFB:
            // NULL. Devolver 0 sem a marca transformaria uma coluna nula numa
            // string vazia -- sao coisas diferentes na grade e no UPDATE.
            if (null) *null = true;
            return 0;
        default:
            // 0xFF so' aparece como primeiro byte de um pacote ERR, nunca
            // aqui. Chegar aqui e' protocolo malformado.
            overflow_ = true;
            return 0;
    }
}

std::string_view PacketReader::read_string() {
    const std::size_t start = position_;
    while (position_ < body_.size() &&
           body_[position_] != std::byte{0}) {
        ++position_;
    }
    const std::size_t length = position_ - start;

    if (position_ < body_.size()) ++position_;   // consome o nulo
    else                          overflow_ = true;

    return {reinterpret_cast<const char*>(body_.data()) + start, length};
}

std::string_view PacketReader::read_fixed_string(std::size_t count) {
    const std::span<const std::byte> data = read_bytes(count);
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

std::string_view PacketReader::read_length_string(bool* null) {
    bool is_null = false;
    const std::uint64_t length = read_length(&is_null);
    if (null) *null = is_null;
    if (is_null) return {};

    return read_fixed_string(static_cast<std::size_t>(length));
}

std::string_view PacketReader::read_rest() {
    return read_fixed_string(remaining());
}

// --- OK e ERR -----------------------------------------------------------------

OkPacket parse_ok(std::span<const std::byte> body, std::uint32_t capabilities) {
    OkPacket ok;
    PacketReader reader(body);

    reader.skip(1);   // 0x00, ou 0xFE quando OK substitui o EOF
    ok.affected_rows  = reader.read_length();
    ok.last_insert_id = reader.read_length();

    if (capabilities & cap_protocol_41) {
        ok.status   = reader.read_u16();
        ok.warnings = reader.read_u16();
    } else if (capabilities & cap_transactions) {
        ok.status = reader.read_u16();
    }

    // O resto e' informativo. Com cap_session_track vem em length-encoded, mas
    // so' lemos o texto humano -- o rastreio de sessao nao e' usado.
    ok.info = std::string(reader.read_rest());
    return ok;
}

ErrPacket parse_err(std::span<const std::byte> body, std::uint32_t capabilities) {
    ErrPacket err;
    PacketReader reader(body);

    reader.skip(1);   // 0xFF
    err.code = reader.read_u16();

    if (capabilities & cap_protocol_41) {
        reader.skip(1);   // o marcador '#'
        err.sqlstate = std::string(reader.read_fixed_string(5));
    }
    err.message = std::string(reader.read_rest());
    return err;
}

std::string ErrPacket::to_string() const {
    std::string out = message;
    if (!sqlstate.empty()) out += " (SQLSTATE " + sqlstate + ")";
    if (code != 0)         out += " [" + std::to_string(code) + "]";
    return out;
}

} // namespace otter::mywire
