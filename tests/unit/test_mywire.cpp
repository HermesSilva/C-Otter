// Protocolo MySQL: pacote, autenticacao e versao do servidor.
//
// O que se testa aqui sao as tres armadilhas mapeadas em docs/MYSQL-MAP.md,
// todas de falha SILENCIOSA -- nenhuma delas quebra o build, e duas so'
// apareceriam em producao:
//
//   1. Pacote partido em 16 MB: parar no primeiro trunca um resultado grande.
//   2. Prefixo falso "5.5.5-" do MariaDB: desliga metade da arvore.
//   3. Ordem do desafio no hash: produz bytes plausiveis que o servidor
//      recusa com um "access denied" que nao diz nada.
#include "test_main.hpp"

#include "mywire/auth.hpp"
#include "mywire/connection.hpp"
#include "mywire/packet.hpp"
#include "db/catalog_mysql.hpp"
#include "db/drivers/mysql.hpp"
#include "db/registry.hpp"
#include "net/crypto.hpp"

#include <string>
#include <vector>

using namespace otter;
using namespace otter::mywire;

namespace {

std::vector<std::byte> from_hex(std::string_view hex) {
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::vector<std::byte> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::byte>((digit(hex[i]) << 4) | digit(hex[i + 1])));
    }
    return out;
}

std::span<const std::byte> bytes_of(std::string_view text) {
    return {reinterpret_cast<const std::byte*>(text.data()), text.size()};
}

} // namespace

// --- SHA-1 --------------------------------------------------------------------

OTTER_TEST(sha1_matches_the_fips180_vectors) {
    // Vetores oficiais do FIPS 180-4 (apendice A). Validar contra o padrao,
    // e nao contra nossa propria saida, e' o que impede um hash "estavel e
    // errado" -- que passaria num round-trip e falharia contra o servidor.
    const Result<crypto::Sha1Digest> abc = crypto::sha1(bytes_of("abc"));
    OTTER_CHECK(abc.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*abc),
                   std::string{"a9993e364706816aba3e25717850c26c9cd0d89d"});

    const Result<crypto::Sha1Digest> longer = crypto::sha1(bytes_of(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"));
    OTTER_CHECK(longer.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*longer),
                   std::string{"84983e441c3bd26ebaae4aa1f95129e5e54670f1"});

    const Result<crypto::Sha1Digest> empty = crypto::sha1({});
    OTTER_CHECK(empty.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*empty),
                   std::string{"da39a3ee5e6b4b0d3255bfef95601890afd80709"});
}

// --- Autenticacao --------------------------------------------------------------

OTTER_TEST(native_password_matches_the_reference_calculation) {
    // SHA1(senha) XOR SHA1( desafio ‖ SHA1(SHA1(senha)) ), recalculado aqui
    // passo a passo a partir das primitivas -- se a ordem da concatenacao
    // inverter no auth.cpp, este teste separa os dois.
    const std::vector<std::byte> challenge =
        from_hex("0102030405060708090a0b0c0d0e0f1011121314");

    const auto stage1 = crypto::sha1(bytes_of("secret"));
    const auto stage2 = crypto::sha1(*stage1);

    std::vector<std::byte> combined;
    combined.insert(combined.end(), challenge.begin(), challenge.end());
    combined.insert(combined.end(), stage2->begin(), stage2->end());
    const auto scrambled = crypto::sha1(combined);

    std::vector<std::byte> expected(20);
    for (std::size_t i = 0; i < 20; ++i) expected[i] = (*stage1)[i] ^ (*scrambled)[i];

    const Result<std::vector<std::byte>> actual =
        native_password_response("secret", challenge);
    OTTER_CHECK(actual.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*actual), crypto::hex_encode(expected));
    OTTER_CHECK_EQ(actual->size(), std::size_t{20});
}

OTTER_TEST(caching_sha2_puts_the_challenge_after_the_double_hash) {
    // A ordem e' INVERTIDA em relacao ao native_password. Este teste existe
    // porque trocar as duas produz 32 bytes plausiveis, e o servidor responde
    // so' "access denied" -- sem pista de que o erro foi de ordem.
    const std::vector<std::byte> challenge =
        from_hex("0102030405060708090a0b0c0d0e0f1011121314");

    const auto stage1 = crypto::sha256(bytes_of("secret"));
    const auto stage2 = crypto::sha256(*stage1);

    std::vector<std::byte> right;    // hash duplo, DEPOIS o desafio
    right.insert(right.end(), stage2->begin(), stage2->end());
    right.insert(right.end(), challenge.begin(), challenge.end());

    std::vector<std::byte> wrong;    // a ordem do native_password
    wrong.insert(wrong.end(), challenge.begin(), challenge.end());
    wrong.insert(wrong.end(), stage2->begin(), stage2->end());

    const auto right_hash = crypto::sha256(right);
    const auto wrong_hash = crypto::sha256(wrong);

    std::vector<std::byte> expected(32);
    for (std::size_t i = 0; i < 32; ++i) expected[i] = (*stage1)[i] ^ (*right_hash)[i];

    const Result<std::vector<std::byte>> actual =
        caching_sha2_response("secret", challenge);
    OTTER_CHECK(actual.has_value());
    OTTER_CHECK_EQ(crypto::hex_encode(*actual), crypto::hex_encode(expected));

    // E a ordem errada da' outra coisa -- ou seja, o teste discrimina.
    std::vector<std::byte> other(32);
    for (std::size_t i = 0; i < 32; ++i) other[i] = (*stage1)[i] ^ (*wrong_hash)[i];
    OTTER_CHECK(crypto::hex_encode(other) != crypto::hex_encode(*actual));
}

OTTER_TEST(empty_password_answers_with_an_empty_response) {
    // Conta sem senha responde VAZIO, nao o hash de "". Mandar 20 bytes ali
    // faz o servidor recusar.
    const std::vector<std::byte> challenge = from_hex("00112233445566778899");

    OTTER_CHECK(native_password_response("", challenge)->empty());
    OTTER_CHECK(caching_sha2_response("", challenge)->empty());
}

// --- Versao do servidor ---------------------------------------------------------

OTTER_TEST(mariadb_version_ignores_the_fake_5_5_5_prefix) {
    // O MariaDB 10+ se anuncia como "5.5.5-10.6.11-MariaDB" para nao ser
    // recusado por clientes antigos. Ler o prefixo como versao faria o
    // C-Otter concluir "MySQL 5.5" e esconder sequences, eventos e
    // particoes -- uma arvore empobrecida sem nenhum erro aparente.
    const ServerVersion maria = parse_server_version("5.5.5-10.6.11-MariaDB-1:10.6.11");
    OTTER_CHECK(maria.mariadb);
    OTTER_CHECK_EQ(maria.number, std::uint32_t{100611});

    // MariaDB antigo, sem o prefixo.
    const ServerVersion old_maria = parse_server_version("5.5.68-MariaDB");
    OTTER_CHECK(old_maria.mariadb);
    OTTER_CHECK_EQ(old_maria.number, std::uint32_t{50568});
}

OTTER_TEST(mysql_version_parses_the_usual_shapes) {
    OTTER_CHECK_EQ(parse_server_version("8.0.35").number, std::uint32_t{80035});
    OTTER_CHECK_EQ(parse_server_version("8.4.0").number,  std::uint32_t{80400});
    OTTER_CHECK_EQ(parse_server_version("5.7.44-log").number, std::uint32_t{50744});
    OTTER_CHECK(!parse_server_version("8.0.35").mariadb);

    // Versao sem patch nao pode virar lixo.
    OTTER_CHECK_EQ(parse_server_version("8.0").number, std::uint32_t{80000});
}

// --- Banco corrente -------------------------------------------------------------

OTTER_TEST(mysql_use_target_reads_the_database_of_a_use) {
    // A aba da conexao mostra o banco corrente; sem ler o USE ela continuava
    // com o do perfil depois de trocar de banco pelo icone dela.
    OTTER_CHECK_EQ(otter::db::mysql_use_target("USE sakila"), std::string("sakila"));
    OTTER_CHECK_EQ(otter::db::mysql_use_target("  use `my db`;\n"), std::string("my db"));
    OTTER_CHECK_EQ(otter::db::mysql_use_target("USE `a``b`"), std::string("a`b"));
    OTTER_CHECK_EQ(otter::db::mysql_use_target("Use\tshop ;"), std::string("shop"));

    // Nao e' um USE (so'): nada muda.
    OTTER_CHECK(otter::db::mysql_use_target("SELECT 1").empty());
    OTTER_CHECK(otter::db::mysql_use_target("USER").empty());
    OTTER_CHECK(otter::db::mysql_use_target("USE a; SELECT 1").empty());
    OTTER_CHECK(otter::db::mysql_use_target("USE").empty());
}

// --- Pacote ---------------------------------------------------------------------

OTTER_TEST(packet_header_is_little_endian_and_excludes_itself) {
    // Ao contrario do PostgreSQL, onde o tamanho e' big-endian e INCLUI os
    // proprios 4 bytes.
    PacketWriter writer;
    writer.put_u8(0x03);
    writer.put_bytes(bytes_of("SELECT 1"));

    std::uint8_t sequence = 0;
    const std::vector<std::byte> framed = frame(writer.body(), sequence);

    OTTER_CHECK_EQ(framed.size(), std::size_t{4 + 9});
    OTTER_CHECK_EQ(static_cast<int>(framed[0]), 9);    // byte baixo primeiro
    OTTER_CHECK_EQ(static_cast<int>(framed[1]), 0);
    OTTER_CHECK_EQ(static_cast<int>(framed[2]), 0);
    OTTER_CHECK_EQ(static_cast<int>(framed[3]), 0);    // sequencia
    OTTER_CHECK_EQ(sequence, std::uint8_t{1});         // avancou
}

OTTER_TEST(packet_splits_bodies_larger_than_sixteen_megabytes) {
    const std::vector<std::byte> body(kMaxPayload + 10, std::byte{0x41});

    std::uint8_t sequence = 0;
    const std::vector<std::byte> framed = frame(body, sequence);

    // Dois pacotes: um cheio e um com 10 bytes.
    OTTER_CHECK_EQ(framed.size(), body.size() + 8);
    OTTER_CHECK_EQ(sequence, std::uint8_t{2});

    // Cabecalho do segundo pacote, logo depois do primeiro corpo.
    const std::size_t second = 4 + kMaxPayload;
    OTTER_CHECK_EQ(static_cast<int>(framed[second + 0]), 10);
    OTTER_CHECK_EQ(static_cast<int>(framed[second + 3]), 1);
}

OTTER_TEST(packet_emits_an_empty_trailer_on_an_exact_multiple) {
    // O caso que engana: corpo com EXATAMENTE 16 MB - 1 precisa de um segundo
    // pacote VAZIO, senao o servidor fica esperando a continuacao. Quem so'
    // testa "maior que o maximo" nao pega isso.
    const std::vector<std::byte> body(kMaxPayload, std::byte{0x42});

    std::uint8_t sequence = 0;
    const std::vector<std::byte> framed = frame(body, sequence);

    OTTER_CHECK_EQ(framed.size(), kMaxPayload + 8);   // dois cabecalhos
    OTTER_CHECK_EQ(sequence, std::uint8_t{2});

    const std::size_t trailer = 4 + kMaxPayload;
    OTTER_CHECK_EQ(static_cast<int>(framed[trailer + 0]), 0);
    OTTER_CHECK_EQ(static_cast<int>(framed[trailer + 1]), 0);
    OTTER_CHECK_EQ(static_cast<int>(framed[trailer + 2]), 0);
}

OTTER_TEST(length_encoded_integers_round_trip_at_the_boundaries) {
    // Os limiares sao 251, 0xFFFF e 0xFFFFFF -- nao potencias de dois, porque
    // 0xFB..0xFF sao prefixos reservados.
    const std::uint64_t values[] = {0, 250, 251, 0xFFFF, 0x10000,
                                    0xFFFFFF, 0x1000000, 0xFFFFFFFFFFULL};

    for (const std::uint64_t value : values) {
        PacketWriter writer;
        writer.put_length(value);

        PacketReader reader(writer.body());
        bool null = true;
        OTTER_CHECK_EQ(reader.read_length(&null), value);
        OTTER_CHECK(!null);
        OTTER_CHECK(reader.exhausted());
    }
}

OTTER_TEST(length_encoded_null_is_not_zero) {
    // 0xFB dentro de uma linha e' NULL. Devolver 0 sem a marca transformaria
    // uma coluna nula em string vazia, e o UPDATE seguinte gravaria '' no
    // lugar de NULL -- perda de dado silenciosa.
    const std::byte packet[] = {std::byte{0xFB}};

    PacketReader reader(packet);
    bool null = false;
    OTTER_CHECK_EQ(reader.read_length(&null), std::uint64_t{0});
    OTTER_CHECK(null);
}

OTTER_TEST(reader_marks_overflow_instead_of_reading_past_the_end) {
    // Um servidor hostil (ou um bug nosso) nao deve virar leitura fora dos
    // limites.
    const std::byte packet[] = {std::byte{0x01}, std::byte{0x02}};

    PacketReader reader(packet);
    OTTER_CHECK_EQ(reader.read_u32(), std::uint32_t{0});
    OTTER_CHECK(reader.overflowed());
}

OTTER_TEST(err_packet_carries_code_and_sqlstate) {
    PacketWriter writer;
    writer.put_u8(0xFF);
    writer.put_u16(1045);
    writer.put_u8('#');
    writer.put_bytes(bytes_of("28000"));
    writer.put_bytes(bytes_of("Access denied for user 'x'@'localhost'"));

    const ErrPacket err = parse_err(writer.body(), cap_protocol_41);
    OTTER_CHECK_EQ(err.code, std::uint16_t{1045});
    OTTER_CHECK_EQ(err.sqlstate, std::string{"28000"});
    OTTER_CHECK(err.message.starts_with("Access denied"));

    // A representacao mostra os tres -- diagnosticar sem o codigo custa caro.
    OTTER_CHECK(err.to_string().find("28000") != std::string::npos);
    OTTER_CHECK(err.to_string().find("1045")  != std::string::npos);
}

OTTER_TEST(ok_packet_reads_affected_rows_and_insert_id) {
    PacketWriter writer;
    writer.put_u8(0x00);
    writer.put_length(3);        // linhas afetadas
    writer.put_length(42);       // ultimo id inserido
    writer.put_u16(status_autocommit);
    writer.put_u16(0);           // avisos

    const OkPacket ok = parse_ok(writer.body(), cap_protocol_41);
    OTTER_CHECK_EQ(ok.affected_rows, std::uint64_t{3});
    OTTER_CHECK_EQ(ok.last_insert_id, std::uint64_t{42});
    OTTER_CHECK((ok.status & status_autocommit) != 0);
}

OTTER_TEST(binary_columns_are_told_apart_by_charset_not_type) {
    // BLOB e TEXT chegam com o MESMO `type`. So' o charset 63 distingue, e
    // mostrar um BLOB como texto encheria a grade de bytes ilegiveis.
    FieldDescription blob;
    blob.type = FieldType::blob;
    blob.charset = 63;
    OTTER_CHECK(blob.is_binary());

    FieldDescription text;
    text.type = FieldType::blob;
    text.charset = 45;          // utf8mb4
    OTTER_CHECK(!text.is_binary());
}

// --- Registro de drivers --------------------------------------------------------

OTTER_TEST(registry_finds_mysql_and_maps_mariadb_to_it) {
    // MariaDB fala o mesmo protocolo: um driver serve os dois. Sem este
    // mapeamento, um perfil importado com provider "mariadb" nao acharia
    // driver nenhum e a conexao falharia antes de sair do lugar.
    otter::db::Driver* mysql = otter::db::find_driver("mysql");
    OTTER_CHECK(mysql != nullptr);
    OTTER_CHECK_EQ(mysql->default_port(), std::uint16_t{3306});

    OTTER_CHECK_EQ(otter::db::find_driver("mariadb"), mysql);

    OTTER_CHECK(otter::db::find_driver("postgresql") != nullptr);

    // Driver desconhecido devolve NULO, e nao o primeiro da lista: conectar
    // a um Db2 falando o protocolo do PostgreSQL daria um erro de
    // protocolo que nao ajuda ninguem a entender o que houve. (Era "oracle"
    // ate' o Oracle ganhar driver -- ADR 0027.)
    OTTER_CHECK_EQ(otter::db::find_driver("db2"), nullptr);
    OTTER_CHECK_EQ(otter::db::find_driver(""), nullptr);
}

OTTER_TEST(mysql_identifiers_use_backticks_not_double_quotes) {
    // Aspas duplas so' funcionam com ANSI_QUOTES ligado, que NAO e' o padrao:
    // usa-las faria o servidor tratar o nome da tabela como uma string.
    OTTER_CHECK_EQ(otter::db::mysql_quote("cliente"), std::string{"`cliente`"});

    // Crase dentro do nome dobra, senao um nome hostil fecharia o
    // identificador e o resto viraria SQL.
    OTTER_CHECK_EQ(otter::db::mysql_quote("a`b"), std::string{"`a``b`"});
}

OTTER_TEST(mysql_literals_escape_the_backslash_too) {
    // A barra invertida e' caractere de escape no MySQL por padrao, ao
    // contrario do padrao SQL. Escapar so' a aspa deixaria passar `\'`, que
    // fecha a string e abre caminho para injecao.
    OTTER_CHECK_EQ(otter::db::mysql_literal("x"),    std::string{"'x'"});
    // Uma aspa simples no nome vira \' -- em literal C++, "\\'".
    OTTER_CHECK_EQ(otter::db::mysql_literal("o'x"), std::string{"'o\\'x'"});

    // Entrada com barra E aspa: os dois escapam, e a barra vem primeiro.
    // O nome tem 3 caracteres (a, \, ') e sai como 'a\\\''.
    OTTER_CHECK_EQ(otter::db::mysql_literal("a\\'"), std::string{"'a\\\\\\''"});
}
