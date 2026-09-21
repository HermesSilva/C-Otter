#include "pgwire/scram.hpp"

#include "net/crypto.hpp"

#include <algorithm>
#include <charconv>

namespace otter::pgwire {
namespace {

std::span<const std::byte> as_bytes(std::string_view text) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
}

// Extrai o valor de um atributo "<chave>=<valor>" numa lista separada por
// virgulas, conforme a gramatica do RFC 5802.
std::string_view attribute(std::string_view message, char key) {
    std::size_t position = 0;
    while (position < message.size()) {
        const std::size_t end = message.find(',', position);
        const std::string_view field = message.substr(
            position, end == std::string_view::npos ? std::string_view::npos
                                                    : end - position);

        if (field.size() >= 2 && field[0] == key && field[1] == '=') {
            return field.substr(2);
        }
        if (end == std::string_view::npos) break;
        position = end + 1;
    }
    return {};
}

// XOR byte a byte -- ClientKey XOR ClientSignature = ClientProof.
std::vector<std::byte> xor_bytes(std::span<const std::byte> a,
                                 std::span<const std::byte> b) {
    std::vector<std::byte> out(std::min(a.size(), b.size()));
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = a[i] ^ b[i];
    return out;
}

// Comparacao em tempo constante: comparar assinaturas com memcmp vaza
// informacao por canal lateral de temporizacao.
bool equal_constant_time(std::span<const std::byte> a,
                         std::span<const std::byte> b) {
    if (a.size() != b.size()) return false;
    std::byte diff{0};
    for (std::size_t i = 0; i < a.size(); ++i) diff |= a[i] ^ b[i];
    return diff == std::byte{0};
}

} // namespace

Result<ScramClient> ScramClient::begin(std::string_view password) {
    ScramClient client;
    client.password_ = std::string(password);

    // Nonce aleatorio de 18 bytes -> 24 caracteres em base64.
    OTTER_ASSIGN_OR_RETURN(auto raw_nonce, crypto::random_bytes(18));
    client.client_nonce_ = crypto::base64_encode(raw_nonce);

    // "n=" vazio: o PostgreSQL usa o usuario da mensagem de startup.
    client.client_first_bare_ = "n=,r=" + client.client_nonce_;

    // "n,," = sem channel binding.
    client.client_first_ = "n,," + client.client_first_bare_;

    return client;
}

Result<std::string> ScramClient::handle_server_first(std::string_view message) {
    const std::string_view server_nonce = attribute(message, 'r');
    const std::string_view salt_b64     = attribute(message, 's');
    const std::string_view iterations   = attribute(message, 'i');

    if (server_nonce.empty() || salt_b64.empty() || iterations.empty()) {
        return fail(Errc::protocol_error,
                    "server-first-message do SCRAM incompleta");
    }

    // O nonce do servidor deve comecar com o nosso: prova que a resposta
    // pertence a esta troca e nao e' replay.
    if (!server_nonce.starts_with(client_nonce_)) {
        return fail(Errc::auth_failed,
                    "nonce do servidor não corresponde ao do cliente");
    }

    std::uint32_t iteration_count = 0;
    const auto [ptr, ec] = std::from_chars(
        iterations.data(), iterations.data() + iterations.size(), iteration_count);
    if (ec != std::errc{} || iteration_count == 0) {
        return fail(Errc::protocol_error, "contagem de iterações inválida");
    }

    OTTER_ASSIGN_OR_RETURN(auto salt, crypto::base64_decode(salt_b64));

    // SaltedPassword = Hi(password, salt, i)
    OTTER_ASSIGN_OR_RETURN(
        auto salted, crypto::pbkdf2_sha256(password_, salt, iteration_count));

    // ClientKey = HMAC(SaltedPassword, "Client Key")
    OTTER_ASSIGN_OR_RETURN(
        auto client_key, crypto::hmac_sha256(salted, as_bytes("Client Key")));

    // StoredKey = SHA256(ClientKey)
    OTTER_ASSIGN_OR_RETURN(auto stored_key, crypto::sha256(client_key));

    // AuthMessage = client-first-bare + "," + server-first + "," + client-final-without-proof
    const std::string client_final_bare = "c=biws,r=" + std::string(server_nonce);
    const std::string auth_message =
        client_first_bare_ + "," + std::string(message) + "," + client_final_bare;

    // ClientSignature = HMAC(StoredKey, AuthMessage)
    OTTER_ASSIGN_OR_RETURN(
        auto client_signature,
        crypto::hmac_sha256(stored_key, as_bytes(auth_message)));

    // ClientProof = ClientKey XOR ClientSignature
    const std::vector<std::byte> proof = xor_bytes(client_key, client_signature);

    // Guardamos a assinatura esperada do servidor para verificar depois.
    OTTER_ASSIGN_OR_RETURN(
        auto server_key, crypto::hmac_sha256(salted, as_bytes("Server Key")));
    OTTER_ASSIGN_OR_RETURN(
        auto server_signature,
        crypto::hmac_sha256(server_key, as_bytes(auth_message)));
    server_signature_.assign(server_signature.begin(), server_signature.end());

    return client_final_bare + ",p=" + crypto::base64_encode(proof);
}

Status ScramClient::handle_server_final(std::string_view message) {
    // 'e' indica erro reportado pelo servidor.
    const std::string_view error = attribute(message, 'e');
    if (!error.empty()) {
        return fail(Errc::auth_failed, "SCRAM rejeitado: " + std::string(error));
    }

    const std::string_view verifier = attribute(message, 'v');
    if (verifier.empty()) {
        return fail(Errc::protocol_error,
                    "server-final-message sem assinatura do servidor");
    }

    OTTER_ASSIGN_OR_RETURN(auto received, crypto::base64_decode(verifier));

    // Autenticacao MUTUA: sem esta verificacao, um servidor falso poderia
    // aceitar qualquer prova e se passar pelo banco real.
    if (!equal_constant_time(received, server_signature_)) {
        return fail(Errc::auth_failed,
                    "assinatura do servidor inválida — o servidor não conhece a senha");
    }
    return {};
}

} // namespace otter::pgwire
