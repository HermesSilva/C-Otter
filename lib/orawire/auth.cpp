#include "orawire/auth.hpp"

#include "net/crypto.hpp"

#include <span>

namespace otter::orawire {
namespace {

std::span<const std::byte> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

void append(std::vector<std::byte>& out, std::span<const std::byte> more) {
    out.insert(out.end(), more.begin(), more.end());
}

// Cifra com o padding do Oracle: N bytes de valor N, e um bloco INTEIRO
// quando o texto ja' fecha em 16 (o PKCS#7).
Result<std::vector<std::byte>> encrypt_padded(std::span<const std::byte> key,
                                              std::vector<std::byte> plain) {
    const std::size_t pad = 16 - plain.size() % 16;
    plain.insert(plain.end(), pad, static_cast<std::byte>(pad));
    return crypto::aes_cbc_zero_iv_encrypt(key, plain);
}

} // namespace

std::string hex_upper(std::span<const std::byte> data) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (const std::byte b : data) {
        out.push_back(kDigits[std::to_integer<unsigned>(b) >> 4]);
        out.push_back(kDigits[std::to_integer<unsigned>(b) & 0x0F]);
    }
    return out;
}

Result<std::vector<std::byte>> hex_decode(std::string_view text) {
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    if (text.size() % 2 != 0) return fail(Errc::parse_error, "hexadecimal de tamanho impar");

    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low  = nibble(text[i + 1]);
        if (high < 0 || low < 0) return fail(Errc::parse_error, "hexadecimal invalido");
        out.push_back(static_cast<std::byte>(high * 16 + low));
    }
    return out;
}

Result<AuthNonces> make_nonces(const AuthChallenge& challenge) {
    AuthNonces nonces;
    OTTER_ASSIGN_OR_RETURN(nonces.client_key,
                           crypto::random_bytes(challenge.server_key.size() / 2));
    OTTER_ASSIGN_OR_RETURN(nonces.password_salt, crypto::random_bytes(16));
    OTTER_ASSIGN_OR_RETURN(nonces.speedy_salt, crypto::random_bytes(16));
    return nonces;
}

Result<AuthAnswer> answer_challenge(const AuthChallenge& challenge,
                                    std::string_view password,
                                    const AuthNonces& nonces) {
    const bool modern = challenge.verifier_type == kVerifier12c;
    if (!modern && challenge.verifier_type != kVerifier11g1 &&
        challenge.verifier_type != kVerifier11g2) {
        return fail(Errc::not_supported,
                    "Oracle password verifier " + std::to_string(challenge.verifier_type) +
                        " is not supported (only the 11g and 12c verifiers are)");
    }

    OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> verifier,
                           hex_decode(challenge.verifier_data));
    OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> server_key_cipher,
                           hex_decode(challenge.server_key));
    if (server_key_cipher.size() != nonces.client_key.size() ||
        nonces.password_salt.size() != 16) {
        return fail(Errc::invalid_argument, "O5LOGON: aleatorios de tamanho errado");
    }

    // A chave que cifra as duas metades da chave de sessao sai da senha.
    std::vector<std::byte> password_key;   // so' no 12c; reaparece na speedy key
    std::vector<std::byte> password_hash;
    if (modern) {
        std::vector<std::byte> salt = verifier;
        append(salt, bytes_of("AUTH_PBKDF2_SPEEDY_KEY"));
        OTTER_ASSIGN_OR_RETURN(password_key,
                               crypto::pbkdf2_sha512(bytes_of(password), salt,
                                                     challenge.verifier_rounds, 64));
        std::vector<std::byte> hashed = password_key;
        append(hashed, verifier);
        OTTER_ASSIGN_OR_RETURN(const crypto::Sha512Digest digest, crypto::sha512(hashed));
        password_hash.assign(digest.begin(), digest.begin() + 32);   // AES-256
    } else {
        std::vector<std::byte> hashed(bytes_of(password).begin(), bytes_of(password).end());
        append(hashed, verifier);
        OTTER_ASSIGN_OR_RETURN(const crypto::Sha1Digest digest, crypto::sha1(hashed));
        password_hash.assign(digest.begin(), digest.end());
        password_hash.resize(24);                                    // AES-192
    }

    OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> server_key,
                           crypto::aes_cbc_zero_iv_decrypt(password_hash, server_key_cipher));
    OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> client_key_cipher,
                           crypto::aes_cbc_zero_iv_encrypt(password_hash, nonces.client_key));

    AuthAnswer answer;
    answer.session_key = hex_upper(client_key_cipher);

    // A chave combinada mistura as duas metades; e' com ela que a senha viaja.
    if (modern) {
        if (server_key.size() < 32) return fail(Errc::protocol_error, "O5LOGON: chave curta");
        std::vector<std::byte> mixed(nonces.client_key.begin(), nonces.client_key.begin() + 32);
        mixed.insert(mixed.end(), server_key.begin(), server_key.begin() + 32);
        const std::string mixed_hex = hex_upper(mixed);

        OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> salt,
                               hex_decode(challenge.combo_salt));
        OTTER_ASSIGN_OR_RETURN(answer.combo_key,
                               crypto::pbkdf2_sha512(bytes_of(mixed_hex), salt,
                                                     challenge.combo_rounds, 32));

        // A "speedy key" deixa o servidor pular o PBKDF2 nas proximas vezes.
        std::vector<std::byte> speedy = nonces.speedy_salt;
        append(speedy, password_key);
        OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> speedy_cipher,
                               encrypt_padded(answer.combo_key, std::move(speedy)));
        answer.speedy_key = hex_upper(std::span(speedy_cipher).first(80));
    } else {
        if (server_key.size() != 48) return fail(Errc::protocol_error, "O5LOGON: chave curta");
        std::vector<std::byte> mixed(24);
        for (std::size_t i = 16; i < 40; ++i) {
            mixed[i - 16] = server_key[i] ^ nonces.client_key[i];
        }
        OTTER_ASSIGN_OR_RETURN(const crypto::Md5Digest first,
                               crypto::md5(std::span(mixed).first(16)));
        OTTER_ASSIGN_OR_RETURN(const crypto::Md5Digest second,
                               crypto::md5(std::span(mixed).subspan(16)));
        answer.combo_key.assign(first.begin(), first.end());
        answer.combo_key.insert(answer.combo_key.end(), second.begin(), second.end());
        answer.combo_key.resize(24);
    }

    std::vector<std::byte> salted = nonces.password_salt;
    append(salted, bytes_of(password));
    OTTER_ASSIGN_OR_RETURN(const std::vector<std::byte> password_cipher,
                           encrypt_padded(answer.combo_key, std::move(salted)));
    answer.password = hex_upper(password_cipher);
    return answer;
}

bool server_proof_is_valid(std::span<const std::byte> combo_key,
                           std::string_view response_hex) {
    const Result<std::vector<std::byte>> cipher = hex_decode(response_hex);
    if (!cipher || cipher->size() < 32 || cipher->size() % 16 != 0) return false;

    const Result<std::vector<std::byte>> plain =
        crypto::aes_cbc_zero_iv_decrypt(combo_key, *cipher);
    if (!plain) return false;

    // Os 16 primeiros bytes sao sal.
    static constexpr std::string_view kExpected = "SERVER_TO_CLIENT";
    for (std::size_t i = 0; i < kExpected.size(); ++i) {
        if (std::to_integer<char>((*plain)[16 + i]) != kExpected[i]) return false;
    }
    return true;
}

} // namespace otter::orawire
