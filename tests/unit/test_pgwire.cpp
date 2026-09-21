#include "test_main.hpp"

#include "net/crypto.hpp"
#include "pgwire/message.hpp"

#include <string>
#include <vector>

using namespace otter;

namespace {

std::span<const std::byte> bytes_of(std::string_view text) {
    return std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size());
}

std::string to_text(std::span<const std::byte> data) {
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

} // namespace

// --- base64: vetores do RFC 4648 --------------------------------------------

OTTER_TEST(base64_rfc4648_vectors) {
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("")), std::string{""});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("f")), std::string{"Zg=="});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("fo")), std::string{"Zm8="});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("foo")), std::string{"Zm9v"});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("foob")), std::string{"Zm9vYg=="});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("fooba")), std::string{"Zm9vYmE="});
    OTTER_CHECK_EQ(crypto::base64_encode(bytes_of("foobar")), std::string{"Zm9vYmFy"});
}

OTTER_TEST(base64_round_trip) {
    const std::string original = "C-Otter: every JOIN is an OTTER JOIN";
    const std::string encoded = crypto::base64_encode(bytes_of(original));

    auto decoded = crypto::base64_decode(encoded);
    OTTER_CHECK(decoded.has_value());
    OTTER_CHECK_EQ(to_text(*decoded), original);
}

OTTER_TEST(base64_rejects_invalid_character) {
    auto result = crypto::base64_decode("abc$def");
    OTTER_CHECK(!result.has_value());
    OTTER_CHECK(result.error().code() == Errc::parse_error);
}

OTTER_TEST(base64_decodes_all_byte_values) {
    std::vector<std::byte> original(256);
    for (int i = 0; i < 256; ++i) original[static_cast<std::size_t>(i)] =
        static_cast<std::byte>(i);

    auto decoded = crypto::base64_decode(crypto::base64_encode(original));
    OTTER_CHECK(decoded.has_value());
    OTTER_CHECK_EQ(decoded->size(), std::size_t{256});
    OTTER_CHECK(*decoded == original);
}

// --- SHA-256 e HMAC: vetores conhecidos -------------------------------------

OTTER_TEST(sha256_known_vector) {
    // SHA-256("abc") -- vetor do NIST.
    auto digest = crypto::sha256(bytes_of("abc"));
    OTTER_CHECK(digest.has_value());
    OTTER_CHECK_EQ(
        crypto::hex_encode(*digest),
        std::string{"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"});
}

OTTER_TEST(sha256_empty_input) {
    auto digest = crypto::sha256({});
    OTTER_CHECK(digest.has_value());
    OTTER_CHECK_EQ(
        crypto::hex_encode(*digest),
        std::string{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"});
}

OTTER_TEST(hmac_sha256_rfc4231_case2) {
    // RFC 4231, caso 2: key="Jefe", data="what do ya want for nothing?"
    auto mac = crypto::hmac_sha256(bytes_of("Jefe"),
                                   bytes_of("what do ya want for nothing?"));
    OTTER_CHECK(mac.has_value());
    OTTER_CHECK_EQ(
        crypto::hex_encode(*mac),
        std::string{"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"});
}

OTTER_TEST(md5_known_vector) {
    // MD5("abc") -- necessario para autenticacao legada (ADR 0010).
    auto digest = crypto::md5(bytes_of("abc"));
    OTTER_CHECK(digest.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*digest),
                   std::string{"900150983cd24fb0d6963f7d28e17f72"});
}

OTTER_TEST(pbkdf2_sha256_known_vector) {
    // RFC 7914 secao 11: P="passwd", S="salt", c=1, dkLen=64 (usamos 32).
    auto derived = crypto::pbkdf2_sha256("passwd", bytes_of("salt"), 1);
    OTTER_CHECK(derived.has_value());
    OTTER_CHECK_EQ(
        crypto::hex_encode(*derived).substr(0, 32),
        std::string{"55ac046e56e3089fec1691c22544b605"});
}

OTTER_TEST(random_bytes_are_distinct) {
    auto a = crypto::random_bytes(32);
    auto b = crypto::random_bytes(32);
    OTTER_CHECK(a.has_value() && b.has_value());
    OTTER_CHECK_EQ(a->size(), std::size_t{32});
    // Dois nonces iguais indicariam gerador quebrado -- falha de seguranca.
    OTTER_CHECK(*a != *b);
}

// --- Mensagens do protocolo --------------------------------------------------

OTTER_TEST(message_writer_encodes_length_and_type) {
    pgwire::MessageWriter writer('Q');
    writer.put_string("SELECT 1");
    const auto encoded = writer.finish();

    // 'Q' + int32(tamanho) + "SELECT 1\0" = 1 + 4 + 9 = 14
    OTTER_CHECK_EQ(encoded.size(), std::size_t{14});
    OTTER_CHECK_EQ(static_cast<char>(encoded[0]), 'Q');

    // O tamanho inclui a si proprio (4) mais o corpo (9), mas nao o tipo.
    const auto length = (static_cast<std::uint32_t>(encoded[1]) << 24) |
                        (static_cast<std::uint32_t>(encoded[2]) << 16) |
                        (static_cast<std::uint32_t>(encoded[3]) << 8) |
                        static_cast<std::uint32_t>(encoded[4]);
    OTTER_CHECK_EQ(length, std::uint32_t{13});
}

OTTER_TEST(message_writer_without_type_for_startup) {
    // StartupMessage nao tem byte de tipo.
    pgwire::MessageWriter writer(0);
    writer.put_int32(196608);   // protocolo 3.0
    const auto encoded = writer.finish();

    OTTER_CHECK_EQ(encoded.size(), std::size_t{8});
    const auto length = (static_cast<std::uint32_t>(encoded[0]) << 24) |
                        (static_cast<std::uint32_t>(encoded[1]) << 16) |
                        (static_cast<std::uint32_t>(encoded[2]) << 8) |
                        static_cast<std::uint32_t>(encoded[3]);
    OTTER_CHECK_EQ(length, std::uint32_t{8});
}

OTTER_TEST(message_reader_round_trip) {
    pgwire::MessageWriter writer('X');
    writer.put_int16(-2);
    writer.put_int32(123456);
    writer.put_string("otter");
    writer.put_int8(7);

    const auto encoded = writer.finish();
    // Pula tipo (1) e tamanho (4) para ler so' o corpo.
    pgwire::MessageReader reader(encoded.subspan(5));

    OTTER_CHECK_EQ(reader.read_int16(), static_cast<std::int16_t>(-2));
    OTTER_CHECK_EQ(reader.read_int32(), 123456);
    OTTER_CHECK_EQ(reader.read_string(), std::string_view{"otter"});
    OTTER_CHECK_EQ(reader.read_int8(), static_cast<std::uint8_t>(7));
    OTTER_CHECK(reader.exhausted());
    OTTER_CHECK(!reader.overflowed());
}

OTTER_TEST(message_reader_detects_overflow) {
    // Ler alem do fim deve sinalizar, nao corromper memoria.
    const std::byte data[2] = {std::byte{0}, std::byte{1}};
    pgwire::MessageReader reader(data);

    OTTER_CHECK_EQ(reader.read_int16(), static_cast<std::int16_t>(1));
    OTTER_CHECK(!reader.overflowed());

    (void)reader.read_int32();
    OTTER_CHECK(reader.overflowed());
}

OTTER_TEST(message_reader_unterminated_string_overflows) {
    // String sem terminador nulo e' mensagem malformada.
    const std::byte data[3] = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}};
    pgwire::MessageReader reader(data);

    (void)reader.read_string();
    OTTER_CHECK(reader.overflowed());
}

OTTER_TEST(error_response_parsing) {
    // Campos [codigo][valor\0], terminados por byte zero.
    std::vector<std::byte> body;
    auto append = [&body](char field, std::string_view value) {
        body.push_back(static_cast<std::byte>(field));
        for (char c : value) body.push_back(static_cast<std::byte>(c));
        body.push_back(std::byte{0});
    };

    append('S', "ERROR");
    append('C', "42P01");
    append('M', "relation \"nao_existe\" does not exist");
    append('P', "15");
    body.push_back(std::byte{0});

    const pgwire::ErrorInfo info = pgwire::parse_error_response(body);
    OTTER_CHECK_EQ(info.severity, std::string{"ERROR"});
    OTTER_CHECK_EQ(info.sqlstate, std::string{"42P01"});
    OTTER_CHECK_EQ(info.position, std::string{"15"});
    OTTER_CHECK(info.to_string().find("42P01") != std::string::npos);
}
