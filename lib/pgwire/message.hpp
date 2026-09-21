// C-Otter -- lib/pgwire/message.hpp
//
// Mensagens do protocolo frontend/backend v3 do PostgreSQL.
//
// Formato: [tipo: 1 byte][tamanho: int32 big-endian][corpo].
// O tamanho INCLUI os 4 bytes dele proprio, mas nao o byte de tipo -- fonte
// classica de erro por um byte.
//
// Referencia: https://www.postgresql.org/docs/current/protocol-message-formats.html
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::pgwire {

// Tipos de mensagem do backend (servidor -> cliente).
enum class BackendType : char {
    authentication      = 'R',
    backend_key_data    = 'K',
    bind_complete       = '2',
    close_complete      = '3',
    command_complete    = 'C',
    copy_in_response    = 'G',
    copy_out_response   = 'H',
    data_row            = 'D',
    empty_query         = 'I',
    error_response      = 'E',
    no_data             = 'n',
    notice_response     = 'N',
    notification        = 'A',
    parameter_description = 't',
    parameter_status    = 'S',
    parse_complete      = '1',
    portal_suspended    = 's',
    ready_for_query     = 'Z',
    row_description     = 'T',
};

// Subtipos da mensagem 'R' (Authentication).
enum class AuthType : std::int32_t {
    ok                 = 0,
    kerberos_v5        = 2,
    cleartext_password = 3,
    md5_password       = 5,
    gss                = 7,
    gss_continue       = 8,
    sspi               = 9,
    sasl               = 10,
    sasl_continue      = 11,
    sasl_final         = 12,
};

// Estado da transacao, vindo de ReadyForQuery.
enum class TransactionStatus : char {
    idle         = 'I',   // fora de transacao
    in_block     = 'T',   // dentro de transacao
    failed       = 'E',   // transacao abortada, exige ROLLBACK
};

// Escreve mensagens no formato do protocolo, cuidando do big-endian e do
// calculo de tamanho.
class MessageWriter {
public:
    // Mensagem com byte de tipo. Use type = 0 para StartupMessage e
    // SSLRequest, que nao tem tipo.
    explicit MessageWriter(char type = 0) : type_(type) {
        if (type_ != 0) buffer_.push_back(static_cast<std::byte>(type_));
        // Espaco reservado para o campo de tamanho.
        for (int i = 0; i < 4; ++i) buffer_.push_back(std::byte{0});
    }

    void put_int8(std::uint8_t value);
    void put_int16(std::int16_t value);
    void put_int32(std::int32_t value);

    // String terminada em nulo, como o protocolo exige.
    void put_string(std::string_view text);

    void put_bytes(std::span<const std::byte> data);

    // Fecha a mensagem preenchendo o campo de tamanho.
    [[nodiscard]] std::span<const std::byte> finish();

private:
    char                   type_;
    std::vector<std::byte> buffer_;
};

// Le campos de uma mensagem ja' recebida.
class MessageReader {
public:
    explicit MessageReader(std::span<const std::byte> body) : body_(body) {}

    [[nodiscard]] std::uint8_t  read_int8();
    [[nodiscard]] std::int16_t  read_int16();
    [[nodiscard]] std::int32_t  read_int32();
    [[nodiscard]] std::string_view read_string();
    [[nodiscard]] std::span<const std::byte> read_bytes(std::size_t count);

    [[nodiscard]] std::size_t remaining() const noexcept {
        return body_.size() - position_;
    }
    [[nodiscard]] bool exhausted() const noexcept { return remaining() == 0; }

    // Indica leitura alem do fim -- protocolo malformado ou bug nosso.
    [[nodiscard]] bool overflowed() const noexcept { return overflow_; }

private:
    std::span<const std::byte> body_;
    std::size_t                position_ = 0;
    bool                       overflow_ = false;
};

// Conteudo de ErrorResponse / NoticeResponse.
struct ErrorInfo {
    std::string severity;
    std::string sqlstate;
    std::string message;
    std::string detail;
    std::string hint;
    std::string position;
    std::string where;

    [[nodiscard]] std::string to_string() const;
};

[[nodiscard]] ErrorInfo parse_error_response(std::span<const std::byte> body);

} // namespace otter::pgwire
