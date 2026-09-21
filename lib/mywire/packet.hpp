// C-Otter -- lib/mywire/packet.hpp
//
// Camada de pacote do protocolo cliente/servidor do MySQL (ADR 0009).
//
// Formato: [tamanho: 3 bytes LITTLE-endian][sequencia: 1 byte][corpo].
//
// Duas diferencas para o PostgreSQL que ja' custaram tempo em outros projetos:
//
//  1. O tamanho e' little-endian, tem 3 bytes e conta SO' o corpo. No
//     PostgreSQL e' big-endian, 4 bytes, e inclui a si mesmo.
//  2. Corpo maior que 0xFFFFFF e' PARTIDO. A cadeia termina no primeiro
//     pacote com tamanho menor que o maximo -- inclusive um pacote VAZIO,
//     quando o total e' multiplo exato de 16 MB. Quem para no primeiro
//     pacote trunca resultados grandes em silencio.
//
// Referencia: https://dev.mysql.com/doc/dev/mysql-server/latest/PAGE_PROTOCOL.html
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::mywire {

// Corpo maximo de um unico pacote. Acima disso, parte-se.
inline constexpr std::size_t kMaxPayload = 0xFFFFFF;

// Flags de capacidade, negociadas no aperto de mao. So' as que usamos.
enum Capability : std::uint32_t {
    cap_long_password        = 0x00000001,
    cap_found_rows           = 0x00000002,
    cap_long_flag            = 0x00000004,
    cap_connect_with_db      = 0x00000008,
    cap_no_schema            = 0x00000010,
    cap_local_files          = 0x00000080,
    cap_ignore_space         = 0x00000100,
    cap_protocol_41          = 0x00000200,
    cap_interactive          = 0x00000400,
    cap_ssl                  = 0x00000800,
    cap_transactions         = 0x00002000,
    cap_secure_connection    = 0x00008000,
    cap_multi_statements     = 0x00010000,
    cap_multi_results        = 0x00020000,
    cap_ps_multi_results     = 0x00040000,
    cap_plugin_auth          = 0x00080000,
    cap_connect_attrs        = 0x00100000,
    cap_plugin_auth_lenenc   = 0x00200000,
    cap_session_track        = 0x00800000,
    cap_deprecate_eof        = 0x01000000,
};

// Flags de estado do servidor, vindas de OK/EOF.
enum ServerStatus : std::uint16_t {
    status_in_transaction  = 0x0001,
    status_autocommit      = 0x0002,
    status_more_results    = 0x0008,
    status_no_index_used   = 0x0020,
    status_cursor_exists   = 0x0040,
};

// Comandos. So' os que emitimos.
enum class Command : std::uint8_t {
    quit         = 0x01,
    init_db      = 0x02,
    query        = 0x03,
    ping         = 0x0E,
    stmt_prepare = 0x16,
    stmt_execute = 0x17,
    stmt_close   = 0x19,
};

// Monta o corpo de um pacote. O cabecalho e' posto por `frame()`, porque a
// sequencia so' e' conhecida na hora do envio.
class PacketWriter {
public:
    void put_u8(std::uint8_t value);
    void put_u16(std::uint16_t value);
    void put_u24(std::uint32_t value);
    void put_u32(std::uint32_t value);
    void put_u64(std::uint64_t value);

    // Inteiro de tamanho variavel. Vale para contagens e comprimentos.
    void put_length(std::uint64_t value);

    // String terminada em nulo.
    void put_string(std::string_view text);

    // String precedida do comprimento em length-encoded.
    void put_length_string(std::string_view text);

    // Bytes crus, sem comprimento.
    void put_bytes(std::span<const std::byte> data);

    // N bytes de zero. Usado no preenchimento de 23 bytes do HandshakeResponse.
    void fill(std::size_t count, std::byte value = std::byte{0});

    [[nodiscard]] std::span<const std::byte> body() const noexcept {
        return buffer_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return buffer_.size(); }

private:
    std::vector<std::byte> buffer_;
};

// Envolve um corpo em um ou mais pacotes, partindo em 16 MB.
//
// `sequence` entra como a sequencia do PRIMEIRO pacote e sai apontando para a
// proxima livre -- por isso e' referencia.
[[nodiscard]] std::vector<std::byte> frame(std::span<const std::byte> body,
                                           std::uint8_t& sequence);

// Le campos de um corpo ja' recebido.
//
// Toda leitura alem do fim marca `overflowed()` e devolve zero, em vez de ler
// memoria alheia: um servidor hostil (ou um bug nosso) nao deve virar leitura
// fora dos limites.
class PacketReader {
public:
    explicit PacketReader(std::span<const std::byte> body) : body_(body) {}

    [[nodiscard]] std::uint8_t  read_u8();
    [[nodiscard]] std::uint16_t read_u16();
    [[nodiscard]] std::uint32_t read_u24();
    [[nodiscard]] std::uint32_t read_u32();
    [[nodiscard]] std::uint64_t read_u64();

    // Inteiro de tamanho variavel.
    //
    // O prefixo 0xFB significa NULL dentro de uma linha; aqui devolvemos 0 com
    // `null` verdadeiro, porque 0 e NULL sao coisas diferentes e confundi-los
    // transformaria uma coluna nula numa string vazia.
    [[nodiscard]] std::uint64_t read_length(bool* null = nullptr);

    [[nodiscard]] std::string_view read_string();        // ate' o nulo
    [[nodiscard]] std::string_view read_length_string(bool* null = nullptr);
    [[nodiscard]] std::string_view read_fixed_string(std::size_t count);
    [[nodiscard]] std::span<const std::byte> read_bytes(std::size_t count);

    // O resto do corpo, como string. Usado onde o protocolo diz "ate' o fim do
    // pacote" -- a mensagem de erro, por exemplo.
    [[nodiscard]] std::string_view read_rest();

    void skip(std::size_t count);

    [[nodiscard]] std::size_t remaining() const noexcept {
        return position_ < body_.size() ? body_.size() - position_ : 0;
    }
    [[nodiscard]] bool exhausted() const noexcept { return remaining() == 0; }
    [[nodiscard]] bool overflowed() const noexcept { return overflow_; }

    // Primeiro byte sem consumir. Decide se a resposta e' OK, ERR ou dados.
    [[nodiscard]] std::uint8_t peek() const noexcept;

private:
    std::span<const std::byte> body_;
    std::size_t                position_ = 0;
    bool                       overflow_ = false;
};

// Pacote OK. Tambem chega no lugar do EOF quando cap_deprecate_eof esta' ativo.
struct OkPacket {
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::uint16_t status = 0;
    std::uint16_t warnings = 0;
    std::string   info;
};

// Pacote de erro. `sqlstate` so' existe quando cap_protocol_41 foi negociado.
struct ErrPacket {
    std::uint16_t code = 0;
    std::string   sqlstate;
    std::string   message;

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] OkPacket  parse_ok(std::span<const std::byte> body,
                                 std::uint32_t capabilities);
[[nodiscard]] ErrPacket parse_err(std::span<const std::byte> body,
                                  std::uint32_t capabilities);

} // namespace otter::mywire
