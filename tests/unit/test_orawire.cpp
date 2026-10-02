// lib/orawire: o que se confere SEM servidor -- as contas do logon, o formato
// de cada tipo de valor, as funcoes puras da conexao.
//
// O protocolo em si (ordem das mensagens, campos de cada uma) so' o servidor
// confere: spike_oraconnect e a suite ao vivo.
#include "test_main.hpp"

#include "net/crypto.hpp"
#include "orawire/auth.hpp"
#include "orawire/connection.hpp"
#include "orawire/value.hpp"

#include <string>
#include <vector>

using namespace otter;
using namespace otter::orawire;

namespace {

std::vector<std::byte> hex(std::string_view text) {
    Result<std::vector<std::byte>> bytes = hex_decode(text);
    OTTER_CHECK(bytes.has_value());
    return bytes ? *bytes : std::vector<std::byte>{};
}

std::span<const std::byte> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

} // namespace

// --- as primitivas que o logon usa, contra os vetores publicos ------------------

OTTER_TEST(sha512_known_vector) {
    // SHA-512("abc") -- vetor do NIST (FIPS 180-4).
    const auto digest = crypto::sha512(bytes_of("abc"));
    OTTER_CHECK(digest.has_value());
    OTTER_CHECK_EQ(hex_upper(*digest),
                   std::string{"DDAF35A193617ABACC417349AE20413112E6FA4E89A97EA20A9EEEE64B55D39A"
                               "2192992A274FC1A836BA3C23A3FEEBBD454D4423643CE80E2A9AC94FA54CA49F"});
}

OTTER_TEST(pbkdf2_sha512_known_vector) {
    // PBKDF2-HMAC-SHA512("password", "salt", 1 rodada, 64 bytes).
    const auto key = crypto::pbkdf2_sha512(bytes_of("password"), bytes_of("salt"), 1, 64);
    OTTER_CHECK(key.has_value());
    OTTER_CHECK_EQ(hex_upper(*key),
                   std::string{"867F70CF1ADE02CFF3752599A3A53DC4AF34C7A669815AE5D513554E1C8CF252"
                               "C02D470A285A0501BAD999BFE943C08F050235D7D68B1DA55E63F73B60A57FCE"});
}

OTTER_TEST(aes_cbc_zero_iv_matches_fips197) {
    // Um bloco, IV zero: o CBC coincide com o ECB dos vetores do FIPS-197
    // (apendices C.2 e C.3) -- as duas chaves que o Oracle usa, 192 e 256.
    const std::vector<std::byte> plain = hex("00112233445566778899AABBCCDDEEFF");

    const auto with_192 = crypto::aes_cbc_zero_iv_encrypt(
        hex("000102030405060708090A0B0C0D0E0F1011121314151617"), plain);
    OTTER_CHECK(with_192.has_value());
    OTTER_CHECK_EQ(hex_upper(*with_192), std::string{"DDA97CA4864CDFE06EAF70A0EC0D7191"});

    const std::vector<std::byte> key_256 =
        hex("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
    const auto with_256 = crypto::aes_cbc_zero_iv_encrypt(key_256, plain);
    OTTER_CHECK(with_256.has_value());
    OTTER_CHECK_EQ(hex_upper(*with_256), std::string{"8EA2B7CA516745BFEAFC49904B496089"});

    const auto back = crypto::aes_cbc_zero_iv_decrypt(key_256, *with_256);
    OTTER_CHECK(back.has_value());
    OTTER_CHECK_EQ(hex_upper(*back), hex_upper(plain));
}

OTTER_TEST(aes_cbc_zero_iv_refuses_what_it_cannot_do) {
    const std::vector<std::byte> key = hex("000102030405060708090A0B0C0D0E0F");
    // Sem padding, o texto tem de fechar em blocos.
    OTTER_CHECK(!crypto::aes_cbc_zero_iv_encrypt(key, hex("0011223344")).has_value());
    OTTER_CHECK(!crypto::aes_cbc_zero_iv_encrypt(hex("00112233"),
                                                 hex("00112233445566778899AABBCCDDEEFF"))
                     .has_value());
}

// --- O5LOGON -----------------------------------------------------------------------

// Os valores esperados saem de tools/ora_auth_vector.py, que refaz a conta
// com o hashlib do Python e a linha de comando do OpenSSL.
OTTER_TEST(o5logon_12c_matches_the_independent_implementation) {
    AuthChallenge challenge;
    challenge.verifier_type   = kVerifier12c;
    challenge.verifier_data   = "000102030405060708090A0B0C0D0E0F";
    challenge.server_key      = "E500A90C78CDB844C42BA5D07AD675FC3A4122AF161CD4D59CA42AD12B24A6D2";
    challenge.combo_salt      = "606162636465666768696A6B6C6D6E6F";
    challenge.verifier_rounds = 4096;
    challenge.combo_rounds    = 3;

    AuthNonces nonces;
    nonces.client_key    = hex("404142434445464748494A4B4C4D4E4F505152535455565758595A5B5C5D5E5F");
    nonces.password_salt = hex("A0A1A2A3A4A5A6A7A8A9AAABACADAEAF");
    nonces.speedy_salt   = hex("B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF");

    const Result<AuthAnswer> answer = answer_challenge(challenge, "OtterTest1", nonces);
    OTTER_CHECK(answer.has_value());
    if (!answer) return;

    OTTER_CHECK_EQ(answer->session_key,
                   std::string{"1E6CE62F3E302BC98404DC2D0E591F8CD5710F7EFD76F3553F3E2DC71B43AC7B"});
    OTTER_CHECK_EQ(answer->speedy_key,
                   std::string{"BB487F4D733BDCD3AD5BDC052FCC019ABF3C65D0F093907A5C4CEE94EF06B454"
                               "76C1BED2836E2CB4AC36F1A13C93D0077ED7EA9CD56C7001027E7BF45E9F52AC"
                               "44167E14F0530CAF1FEA8EA2993DC868"});
    OTTER_CHECK_EQ(answer->password,
                   std::string{"A7F5472E608B62FD670BC1E51DFFA46A47C71291DCF967F0A088F0DB804FCC31"});
    OTTER_CHECK_EQ(hex_upper(answer->combo_key),
                   std::string{"83943FFD6B8F952F0F9B6C665AC878B45CFD0F7E952FB0E0F5E86C795659BE64"});

    // E a prova do servidor: so' quem tem a chave combinada a produz.
    OTTER_CHECK(server_proof_is_valid(
        answer->combo_key, "2616592B68A6104B51CEAA49459D7B5DA1143085F7D621BFC2CD4B91CC81407F"));
    OTTER_CHECK(!server_proof_is_valid(
        answer->combo_key, "0016592B68A6104B51CEAA49459D7B5DA1143085F7D621BFC2CD4B91CC81407F"));
    OTTER_CHECK(!server_proof_is_valid(answer->combo_key, ""));
}

OTTER_TEST(o5logon_wrong_password_gives_another_answer) {
    AuthChallenge challenge;
    challenge.verifier_type   = kVerifier12c;
    challenge.verifier_data   = "000102030405060708090A0B0C0D0E0F";
    challenge.server_key      = "E500A90C78CDB844C42BA5D07AD675FC3A4122AF161CD4D59CA42AD12B24A6D2";
    challenge.combo_salt      = "606162636465666768696A6B6C6D6E6F";
    challenge.verifier_rounds = 4096;
    challenge.combo_rounds    = 3;

    AuthNonces nonces;
    nonces.client_key    = hex("404142434445464748494A4B4C4D4E4F505152535455565758595A5B5C5D5E5F");
    nonces.password_salt = hex("A0A1A2A3A4A5A6A7A8A9AAABACADAEAF");
    nonces.speedy_salt   = hex("B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF");

    const Result<AuthAnswer> right = answer_challenge(challenge, "OtterTest1", nonces);
    const Result<AuthAnswer> wrong = answer_challenge(challenge, "OtterTest2", nonces);
    OTTER_CHECK(right.has_value() && wrong.has_value());
    if (!right || !wrong) return;
    OTTER_CHECK(right->session_key != wrong->session_key);
    OTTER_CHECK(right->combo_key != wrong->combo_key);
}

OTTER_TEST(o5logon_11g_runs_and_refuses_unknown_verifiers) {
    // 11g: chave de 48 bytes, AES-192 de SHA-1. Sem vetor independente; o
    // teste garante que o caminho fecha e produz os tamanhos do protocolo.
    AuthChallenge challenge;
    challenge.verifier_type = kVerifier11g1;
    challenge.verifier_data = "0102030405060708090A";
    challenge.server_key    = std::string(96, 'A');

    AuthNonces nonces;
    nonces.client_key    = std::vector<std::byte>(48, std::byte{7});
    nonces.password_salt = std::vector<std::byte>(16, std::byte{9});

    const Result<AuthAnswer> answer = answer_challenge(challenge, "secret", nonces);
    OTTER_CHECK(answer.has_value());
    if (answer) {
        OTTER_CHECK_EQ(answer->session_key.size(), std::size_t{96});
        OTTER_CHECK_EQ(answer->combo_key.size(), std::size_t{24});
        OTTER_CHECK(answer->speedy_key.empty());
    }

    challenge.verifier_type = 0x939;   // o verificador do 10g, que nao e' aceito
    OTTER_CHECK(!answer_challenge(challenge, "secret", nonces).has_value());
}

// --- valores -----------------------------------------------------------------------

OTTER_TEST(oracle_number_decodes_the_base_100_format) {
    OTTER_CHECK_EQ(decode_number(hex("80")), std::string{"0"});
    OTTER_CHECK_EQ(decode_number(hex("C102")), std::string{"1"});
    OTTER_CHECK_EQ(decode_number(hex("C202")), std::string{"100"});
    OTTER_CHECK_EQ(decode_number(hex("C10B")), std::string{"10"});
    OTTER_CHECK_EQ(decode_number(hex("C302182E4451")), std::string{"12345.678"});
    OTTER_CHECK_EQ(decode_number(hex("C033")), std::string{"0.5"});
    OTTER_CHECK_EQ(decode_number(hex("CB02")), std::string{"100000000000000000000"});

    // Negativo: expoente invertido, digitos subtraidos de 101 e o 102 no fim.
    OTTER_CHECK_EQ(decode_number(hex("3E6466")), std::string{"-1"});
    OTTER_CHECK_EQ(decode_number(hex("405B66")), std::string{"-0.001"});
}

OTTER_TEST(oracle_date_and_timestamp_decode) {
    // 2024-02-29 13:45:10 -- seculo e ano somados de 100, hora/min/seg de 1.
    OTTER_CHECK_EQ(decode_datetime(hex("787C021D0E2E0B")), std::string{"2024-02-29 13:45:10"});
    // Com fracao: nanossegundos, sem os zeros do fim.
    OTTER_CHECK_EQ(decode_datetime(hex("787C021D0E2E0B075BCA00")),
                   std::string{"2024-02-29 13:45:10.123456"});
    // Fracao zero nao deixa um ponto solto.
    OTTER_CHECK_EQ(decode_datetime(hex("787C021D0E2E0B00000000")),
                   std::string{"2024-02-29 13:45:10"});
}

OTTER_TEST(oracle_timestamp_with_time_zone_shows_local_time) {
    // No fio vai a hora em UTC (16:45) e o fuso (-03:00): mostra-se 13:45.
    OTTER_CHECK_EQ(decode_datetime(hex("787C021D112E0B1DCD6500113C")),
                   std::string{"2024-02-29 13:45:10.5 -03:00"});
    // O deslocamento atravessa a meia-noite E o fim do mes de um ano bissexto:
    // 2024-03-01 01:30 UTC e' 2024-02-29 22:30 em -03:00.
    OTTER_CHECK_EQ(decode_datetime(hex("787C0301021F0100000000113C")),
                   std::string{"2024-02-29 22:30:00 -03:00"});
    // E para frente: 23:30 UTC em +05:30 e' 05:00 do dia seguinte.
    OTTER_CHECK_EQ(decode_datetime(hex("787C0C1F181F0100000000195A")),
                   std::string{"2025-01-01 05:00:00 +05:30"});
}

OTTER_TEST(oracle_intervals_decode) {
    OTTER_CHECK_EQ(decode_interval_ds(hex("800000013E3F409DCD6500")),
                   std::string{"+1 02:03:04.5"});
    OTTER_CHECK_EQ(decode_interval_ym(hex("8000000142")), std::string{"+1-06"});
    // Negativo: todos os campos abaixo do deslocamento.
    OTTER_CHECK_EQ(decode_interval_ym(hex("7FFFFFFF36")), std::string{"-1-06"});
}

OTTER_TEST(oracle_binary_floats_undo_the_sortable_encoding) {
    // IEEE 754 com o bit de sinal invertido (positivo) ou tudo invertido
    // (negativo), para os bytes ordenarem como os numeros.
    OTTER_CHECK_EQ(decode_binary_double(hex("BFF8000000000000")), std::string{"1.5"});
    OTTER_CHECK_EQ(decode_binary_float(hex("3FEFFFFF")), std::string{"-2.25"});
}

OTTER_TEST(oracle_rowid_uses_its_own_base64) {
    // O ROWID de DUAL no banco de teste: objeto 149, arquivo 0, bloco 1185.
    OTTER_CHECK_EQ(encode_rowid(149, 0, 1185, 0), std::string{"AAAACVAAAAAAAShAAA"});
    // UROWID fisico: o mesmo, com o marcador 1 na frente.
    OTTER_CHECK_EQ(decode_urowid(hex("01000000950000000004A10000")),
                   std::string{"AAAACVAAAAAAAShAAA"});
    // Logico (tabela organizada por indice): '*' e a chave em base 64.
    OTTER_CHECK_EQ(decode_urowid(hex("02C102")), std::string{"*wQI"});
}

OTTER_TEST(oracle_national_text_converts_from_utf16) {
    // "ação" em UTF-16BE.
    OTTER_CHECK_EQ(utf16be_to_utf8(hex("006100E700E3006F")), std::string{"a\xC3\xA7\xC3\xA3o"});
    // Par substituto: U+1F9A6 (a lontra).
    OTTER_CHECK_EQ(utf16be_to_utf8(hex("D83EDDA6")), std::string{"\xF0\x9F\xA6\xA6"});
    // Substituto solto nao vira UTF-8 invalido.
    OTTER_CHECK_EQ(utf16be_to_utf8(hex("D83E")), std::string{"\xEF\xBF\xBD"});
}

// --- conexao -----------------------------------------------------------------------

OTTER_TEST(oracle_statement_kind_follows_the_first_word) {
    OTTER_CHECK(classify_statement("select 1 from dual") == StatementKind::query);
    OTTER_CHECK(classify_statement("  WITH t AS (select 1 from dual) select * from t") ==
                StatementKind::query);
    OTTER_CHECK(classify_statement("(select 1 from dual) union all (select 2 from dual)") ==
                StatementKind::query);
    OTTER_CHECK(classify_statement("-- comentario\n/* outro */ Select 1 from dual") ==
                StatementKind::query);
    OTTER_CHECK(classify_statement("update t set a = 1") == StatementKind::dml);
    OTTER_CHECK(classify_statement("merge into t using s on (1=1)") == StatementKind::dml);
    OTTER_CHECK(classify_statement("create table t (a number)") == StatementKind::ddl);
    OTTER_CHECK(classify_statement("begin null; end;") == StatementKind::plsql);
    OTTER_CHECK(classify_statement("call p()") == StatementKind::plsql);
    OTTER_CHECK(classify_statement("commit") == StatementKind::other);
    // "selection" nao e' SELECT.
    OTTER_CHECK(classify_statement("selection") == StatementKind::other);
    OTTER_CHECK(classify_statement("") == StatementKind::other);
}

OTTER_TEST(oracle_connect_descriptor_names_the_service_or_the_sid) {
    ConnectParams params;
    params.host = "db.example";
    params.port = 1522;
    params.service_name = "FREEPDB1";
    OTTER_CHECK_EQ(connect_descriptor(params, "maquina", "pessoa"),
                   std::string{"(DESCRIPTION=(ADDRESS=(PROTOCOL=tcp)(HOST=db.example)(PORT=1522))"
                               "(CONNECT_DATA=(SERVICE_NAME=FREEPDB1)"
                               "(CID=(PROGRAM=C-Otter)(HOST=maquina)(USER=pessoa))))"});

    // SID so' quando nao ha' nome de servico.
    params.sid = "XE";
    OTTER_CHECK(connect_descriptor(params, "m", "p").find("(SID=") == std::string::npos);
    params.service_name.clear();
    OTTER_CHECK(connect_descriptor(params, "m", "p").find("(SID=XE)") != std::string::npos);
}

OTTER_TEST(oracle_server_version_has_two_formats) {
    // 23.26.3.0.0, no formato do 18c em diante.
    OTTER_CHECK_EQ(format_server_version(387592192u, /*modern=*/true),
                   std::string{"23.26.3.0.0"});
    // 12.2.0.1.0, no formato antigo (campos de 4 bits).
    OTTER_CHECK_EQ(format_server_version(0x0C200100u, /*modern=*/false),
                   std::string{"12.2.0.1.0"});
}

OTTER_TEST(oracle_type_names_distinguish_the_national_ones) {
    OTTER_CHECK_EQ(std::string(type_name(OraType::varchar, kCharsetFormImplicit)),
                   std::string{"VARCHAR2"});
    OTTER_CHECK_EQ(std::string(type_name(OraType::varchar, kCharsetFormNational)),
                   std::string{"NVARCHAR2"});
    OTTER_CHECK_EQ(std::string(type_name(OraType::clob, kCharsetFormNational)),
                   std::string{"NCLOB"});
    OTTER_CHECK_EQ(std::string(type_name(OraType::timestamp_tz, 0)),
                   std::string{"TIMESTAMP WITH TIME ZONE"});
}
