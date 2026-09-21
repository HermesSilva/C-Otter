// AES-128-CBC e JSON: as duas pecas que sustentam a persistencia de conexoes
// e a importacao do DBeaver (ADR 0012).
//
// O AES e' verificado contra os vetores oficiais do FIPS-197 e do NIST
// SP 800-38A. Uma implementacao de cifra que "parece funcionar" porque
// decifra o que ela mesma cifrou pode estar errada de um jeito que so'
// aparece ao ler o arquivo de outro programa -- exatamente o nosso caso.
#include "test_main.hpp"

#include "base/aes.hpp"
#include "base/json.hpp"

#include <array>
#include <string>
#include <vector>

using namespace otter;

namespace {

std::vector<std::uint8_t> from_hex(std::string_view hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto digit = [](char c) -> std::uint8_t {
            if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
            return static_cast<std::uint8_t>(c - 'A' + 10);
        };
        out.push_back(static_cast<std::uint8_t>((digit(hex[i]) << 4) |
                                                 digit(hex[i + 1])));
    }
    return out;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

crypto::AesKey key_from_hex(std::string_view hex) {
    const std::vector<std::uint8_t> bytes = from_hex(hex);
    crypto::AesKey key{};
    for (std::size_t i = 0; i < key.size() && i < bytes.size(); ++i) {
        key[i] = bytes[i];
    }
    return key;
}

} // namespace

// --- AES --------------------------------------------------------------------

OTTER_TEST(aes_matches_the_fips197_vector) {
    // FIPS-197, apendice C.1: o vetor canonico do AES-128.
    //
    // Testa o bloco cru, sem CBC nem padding. Se ele falha, o problema esta'
    // na cifra; se ele passa e o CBC falha, esta' no encadeamento.
    const crypto::AesKey key = key_from_hex("000102030405060708090a0b0c0d0e0f");
    const std::vector<std::uint8_t> plain =
        from_hex("00112233445566778899aabbccddeeff");

    // IV de zeros faz o primeiro bloco do CBC ser o AES puro do texto.
    const crypto::AesIv iv{};
    const std::vector<std::uint8_t> out =
        crypto::aes128_cbc_encrypt(plain, key, iv);

    // out = IV (16) || bloco cifrado (16) || bloco de padding (16).
    OTTER_CHECK_EQ(out.size(), std::size_t{48});

    const std::string cipher_block =
        to_hex(std::span<const std::uint8_t>(out).subspan(16, 16));
    OTTER_CHECK_EQ(cipher_block, std::string{"69c4e0d86a7b0430d8cdb78070b4c55a"});
}

OTTER_TEST(aes_matches_the_nist_cbc_vector) {
    // NIST SP 800-38A, F.2.1 (CBC-AES128.Encrypt), primeiros dois blocos.
    const crypto::AesKey key = key_from_hex("2b7e151628aed2a6abf7158809cf4f3c");

    crypto::AesIv iv{};
    const std::vector<std::uint8_t> iv_bytes =
        from_hex("000102030405060708090a0b0c0d0e0f");
    for (std::size_t i = 0; i < iv.size(); ++i) iv[i] = iv_bytes[i];

    const std::vector<std::uint8_t> plain = from_hex(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51");

    const std::vector<std::uint8_t> out =
        crypto::aes128_cbc_encrypt(plain, key, iv);

    const std::string blocks =
        to_hex(std::span<const std::uint8_t>(out).subspan(16, 32));
    OTTER_CHECK_EQ(blocks,
                   std::string{"7649abac8119b246cee98e9b12e9197d"
                               "5086cb9b507219ee95db113a917678b2"});
}

OTTER_TEST(aes_round_trips_text) {
    const crypto::AesKey key = key_from_hex("ffeeddccbbaa99887766554433221100");
    const crypto::AesIv iv = *crypto::random_iv();

    const std::string secret = "senha com acento: ção, e um emoji 🦦";
    const std::vector<std::uint8_t> plain(
        reinterpret_cast<const std::uint8_t*>(secret.data()),
        reinterpret_cast<const std::uint8_t*>(secret.data()) + secret.size());

    const std::vector<std::uint8_t> cipher =
        crypto::aes128_cbc_encrypt(plain, key, iv);
    const auto back = crypto::aes128_cbc_decrypt(cipher, key);

    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(std::string(back->begin(), back->end()), secret);
}

OTTER_TEST(aes_pads_even_when_size_is_a_multiple_of_the_block) {
    // PKCS#7 acrescenta um bloco inteiro de padding quando o texto ja' e'
    // multiplo de 16. Sem isso, nao haveria como distinguir padding de dado.
    const crypto::AesKey key{};
    const crypto::AesIv iv{};
    const std::vector<std::uint8_t> exact(32, 0x41);   // 'A' x 32

    const std::vector<std::uint8_t> cipher =
        crypto::aes128_cbc_encrypt(exact, key, iv);

    // IV + 2 blocos de dado + 1 bloco de padding.
    OTTER_CHECK_EQ(cipher.size(), std::size_t{16 + 48});

    const auto back = crypto::aes128_cbc_decrypt(cipher, key);
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(back->size(), std::size_t{32});
}

OTTER_TEST(aes_rejects_the_wrong_key_instead_of_returning_garbage) {
    // Chave errada quase sempre produz padding invalido. Detectar aqui da'
    // uma mensagem util; deixar passar produziria "JSON malformado" adiante,
    // que aponta para o lugar errado.
    const crypto::AesKey right = key_from_hex("00112233445566778899aabbccddeeff");
    const crypto::AesKey wrong = key_from_hex("00112233445566778899aabbccddee00");
    const crypto::AesIv iv{};

    const std::vector<std::uint8_t> plain(40, 0x7A);
    const std::vector<std::uint8_t> cipher =
        crypto::aes128_cbc_encrypt(plain, right, iv);

    OTTER_CHECK(!crypto::aes128_cbc_decrypt(cipher, wrong).has_value());
}

OTTER_TEST(aes_rejects_truncated_input) {
    const crypto::AesKey key{};
    const std::vector<std::uint8_t> too_short(8, 0);
    OTTER_CHECK(!crypto::aes128_cbc_decrypt(too_short, key).has_value());

    // Tem IV mas o resto nao fecha em blocos.
    const std::vector<std::uint8_t> ragged(16 + 5, 0);
    OTTER_CHECK(!crypto::aes128_cbc_decrypt(ragged, key).has_value());
}

// --- JSON -------------------------------------------------------------------

OTTER_TEST(json_parses_a_dbeaver_shaped_document) {
    // Recorte real de data-sources.json, com a porta como STRING -- e' assim
    // que o DBeaver grava.
    const auto root = json::parse(R"({
        "folders": {},
        "connections": {
            "postgres-jdbc-19f2": {
                "provider": "postgresql",
                "driver": "postgres-jdbc",
                "name": "ERP_TID",
                "save-password": true,
                "configuration": {
                    "host": "localhost",
                    "port": "5432",
                    "database": "ERP_TID",
                    "type": "dev"
                }
            }
        }
    })");

    OTTER_CHECK(root.has_value());

    const json::Value& conn = (*root)["connections"]["postgres-jdbc-19f2"];
    OTTER_CHECK_EQ(std::string(conn["name"].as_string()), std::string{"ERP_TID"});
    OTTER_CHECK(conn["save-password"].as_bool());

    const json::Value& config = conn["configuration"];
    OTTER_CHECK_EQ(std::string(config["host"].as_string()),
                   std::string{"localhost"});

    // as_int aceita a porta como string ou numero: o DBeaver usa string.
    OTTER_CHECK_EQ(config["port"].as_int(), std::int64_t{5432});
}

OTTER_TEST(json_missing_keys_chain_without_crashing) {
    // Acessar um caminho inexistente precisa devolver nulo, nao explodir:
    // arquivo escrito por outra versao do DBeaver tera' campos que nao
    // conhecemos, e campos que conhecemos podem faltar.
    const auto root = json::parse(R"({"a": {"b": 1}})");
    OTTER_CHECK(root.has_value());

    const json::Value& deep = (*root)["x"]["y"]["z"];
    OTTER_CHECK(deep.is_null());
    OTTER_CHECK_EQ(std::string(deep.as_string("fallback")),
                   std::string{"fallback"});
    OTTER_CHECK_EQ(deep.as_int(-1), std::int64_t{-1});
}

OTTER_TEST(json_decodes_unicode_escapes) {
    // O DBeaver escapa acentos. Sem decodificar, um nome de conexao com
    // cedilha voltaria como "conexão" literal.
    const auto root = json::parse(R"({"name": "conexão de produção"})");
    OTTER_CHECK(root.has_value());
    OTTER_CHECK_EQ(std::string((*root)["name"].as_string()),
                   std::string{"conexão de produção"});
}

OTTER_TEST(json_decodes_surrogate_pairs) {
    const auto root = json::parse(R"({"emoji": "🦦"})");
    OTTER_CHECK(root.has_value());
    OTTER_CHECK_EQ(std::string((*root)["emoji"].as_string()), std::string{"🦦"});
}

OTTER_TEST(json_round_trips_through_serialize) {
    const std::string original = R"({
        "connections": {
            "id-1": {"name": "um", "port": 5432, "ok": true, "nada": null}
        }
    })";

    const auto first = json::parse(original);
    OTTER_CHECK(first.has_value());

    const std::string text = json::serialize(*first);
    const auto second = json::parse(text);
    OTTER_CHECK(second.has_value());

    const json::Value& conn = (*second)["connections"]["id-1"];
    OTTER_CHECK_EQ(std::string(conn["name"].as_string()), std::string{"um"});
    OTTER_CHECK_EQ(conn["port"].as_int(), std::int64_t{5432});
    OTTER_CHECK(conn["ok"].as_bool());
    OTTER_CHECK(conn["nada"].is_null());
}

OTTER_TEST(json_writes_integers_without_a_decimal_point) {
    // Porta gravada como 5432.0 e' valida e visivelmente errada para quem le'
    // o arquivo -- e o DBeaver poderia recusar.
    json::Object object;
    object["port"] = json::Value(std::int64_t{5432});

    const std::string text = json::serialize(json::Value(std::move(object)), 0);
    OTTER_CHECK(text.find("5432") != std::string::npos);
    OTTER_CHECK(text.find("5432.0") == std::string::npos);
}

OTTER_TEST(json_escapes_what_would_break_the_document) {
    json::Object object;
    object["quebra"] = json::Value(std::string("aspas \" barra \\ nova\nlinha"));

    const std::string text = json::serialize(json::Value(std::move(object)), 0);
    const auto back = json::parse(text);

    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(std::string((*back)["quebra"].as_string()),
                   std::string{"aspas \" barra \\ nova\nlinha"});
}

OTTER_TEST(json_reports_where_the_error_is) {
    const auto bad = json::parse(R"({"a": 1,
  "b": })");

    OTTER_CHECK(!bad.has_value());
    // A mensagem precisa apontar a linha -- "parse error" sozinho obriga a
    // procurar a olho num arquivo de milhares de linhas.
    OTTER_CHECK(bad.error().message().find("line 2") != std::string::npos);
}

OTTER_TEST(json_rejects_deeply_nested_input) {
    // Arquivo malformado com milhares de '[' estouraria a pilha antes de
    // qualquer verificacao de tamanho.
    std::string bomb(5000, '[');
    OTTER_CHECK(!json::parse(bomb).has_value());
}
