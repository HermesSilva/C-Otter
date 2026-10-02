// C-Otter -- testes do perfil SQL Anywhere: o protocolo TDS 5.0 (lib/tdswire/
// tds5), os geradores de SQL (db/sqlanywhere_object, db/alter, db/edit), a
// paginacao por TOP ... START AT (sql/paging) e o armazenamento do perfil.
//
// O que se protege:
//   - a conversao BINARIO -> texto dos valores do TDS 5.0, que e' o contrario
//     do TDS do SQL Server em quase tudo (numeric em big-endian, sinal no
//     primeiro byte, datas em outra epoca)
//   - o registro de login: campos de tamanho fixo, com o tamanho DEPOIS do
//     campo, e o banco no campo do "nome do servidor"
//   - o que o Watcom SQL tem de diferente e que um gerador "quase T-SQL"
//     erraria: TOP sem parenteses e START AT, UNISTR para texto fora do ASCII,
//     DROP INDEX dono.tabela.indice, DROP ROLE ... WITH REVOKE, ALTER no lugar
//     de CREATE para gravar o fonte
//   - o mapeamento dos drivers Sybase do DBeaver, que moram no provider do SQL
//     Server e falam outro protocolo
//
// O efeito de cada comando no servidor e' conferido em
// tests/integration/test_sqlanywhere_live.cpp.
#include "test_main.hpp"

#include "db/alter.hpp"
#include "db/app_tools.hpp"
#include "db/catalog_sqlanywhere.hpp"
#include "db/connection_config.hpp"
#include "db/connection_store.hpp"
#include "db/ddl.hpp"
#include "db/drivers/sqlanywhere.hpp"
#include "db/edit.hpp"
#include "db/grid_ops.hpp"
#include "db/object_info.hpp"
#include "db/sqlanywhere_object.hpp"
#include "sql/dialect.hpp"
#include "sql/editing.hpp"
#include "sql/paging.hpp"
#include "sql/script.hpp"
#include "tdswire/tds5.hpp"
#include "ui/icons.hpp"

#include <initializer_list>
#include <string>
#include <vector>

using namespace otter;
using namespace otter::db;

namespace {

// O dialeto do SQL Anywhere durante o teste, e o anterior de volta no fim.
struct AnywhereDialect {
    QuoteStyle previous = sql_dialect();
    AnywhereDialect() { set_sql_dialect(QuoteStyle::anywhere); }
    ~AnywhereDialect() { set_sql_dialect(previous); }
};

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

bool contains(const std::string& text, std::string_view piece) {
    return text.find(piece) != std::string::npos;
}

bool contains_bytes(const std::vector<std::byte>& haystack, std::string_view needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool same = true;
        for (std::size_t k = 0; k < needle.size() && same; ++k) {
            same = haystack[i + k] == static_cast<std::byte>(needle[k]);
        }
        if (same) return true;
    }
    return false;
}

ObjectRef ref_of(ObjectType type, const char* schema, const char* name,
                 const char* parent = "") {
    ObjectRef ref;
    ref.type   = type;
    ref.schema = schema;
    ref.name   = name;
    ref.parent = parent;
    return ref;
}

} // namespace

// --- tdswire: valores do TDS 5.0 ------------------------------------------------------

OTTER_TEST(tds5_numeric_is_sign_byte_plus_big_endian_magnitude) {
    using namespace otter::tdswire;
    // 123.45 com escala 2 -> 12345 = 0x3039, o byte ALTO primeiro; o primeiro
    // byte e' o sinal, e 1 e' NEGATIVO (no SQL Server 1 e' positivo).
    OTTER_CHECK_EQ(format_tds5_numeric(bytes({0x00, 0x30, 0x39}), 2), std::string("123.45"));
    OTTER_CHECK_EQ(format_tds5_numeric(bytes({0x01, 0x30, 0x39}), 2), std::string("-123.45"));
    // O zero a' esquerda da virgula nao pode sumir.
    OTTER_CHECK_EQ(format_tds5_numeric(bytes({0x00, 0x00, 0x05}), 2), std::string("0.05"));
    OTTER_CHECK_EQ(format_tds5_numeric(bytes({0x00, 0x00, 0x00, 0x07}), 0), std::string("7"));
}

OTTER_TEST(tds5_dates_count_from_1900_and_times_in_300ths) {
    using namespace otter::tdswire;
    OTTER_CHECK_EQ(format_tds5_date(bytes({0x00, 0x00, 0x00, 0x00})), std::string("1900-01-01"));
    // 1900 nao e' bissexto: 365 dias depois e' o primeiro dia de 1901.
    OTTER_CHECK_EQ(format_tds5_date(bytes({0x6D, 0x01, 0x00, 0x00})), std::string("1901-01-01"));
    // Um dia ANTES da epoca: o contador tem sinal.
    OTTER_CHECK_EQ(format_tds5_date(bytes({0xFF, 0xFF, 0xFF, 0xFF})), std::string("1899-12-31"));

    // 01:00:00 = 3600 s * 300 = 1.080.000 = 0x00107AC0.
    OTTER_CHECK(format_tds5_time(bytes({0xC0, 0x7A, 0x10, 0x00})).starts_with("01:00:00"));
}

OTTER_TEST(tds5_bigdatetime_counts_microseconds_from_year_zero) {
    using namespace otter::tdswire;
    // 1970-01-01 00:00:00 = 719.528 dias desde 0000-01-01
    //                     = 62.167.219.200.000.000 us = 0x00DCDCC1A9170000.
    OTTER_CHECK(format_tds5_bigdatetime(
                    bytes({0x00, 0x00, 0x17, 0xA9, 0xC1, 0xDC, 0xDC, 0x00}))
                    .starts_with("1970-01-01 00:00:00"));
    // 01:01:01.5 = 3.661.500.000 us = 0x00000000DA3E0E60.
    OTTER_CHECK(format_tds5_bigtime(bytes({0x60, 0x0E, 0x3E, 0xDA, 0x00, 0x00, 0x00, 0x00}))
                    .starts_with("01:01:01.5"));
}

OTTER_TEST(tds5_type_names_are_the_ones_sql_anywhere_writes) {
    using namespace otter::tdswire;
    Tds5Column column;
    column.type = Tds5Type::int4;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("integer"));

    column.type      = Tds5Type::numn;
    column.precision = 10;
    column.scale     = 2;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("numeric(10,2)"));

    // O tamanho que o protocolo informa e' em bytes do UTF-8 (3 por caractere):
    // mostra-lo diria char(60) de uma coluna char(20).
    column = Tds5Column{};
    column.type       = Tds5Type::varchar;
    column.max_length = 60;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("varchar"));

    // date e time chegam como DATETIME, e o uniqueidentifier como binary(16):
    // quem diz o que sao e' o tipo de usuario.
    column = Tds5Column{};
    column.type      = Tds5Type::datetimn;
    column.user_type = 37;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("date"));
    column.user_type = 38;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("time"));
    column.type      = Tds5Type::binary;
    column.user_type = 81;
    OTTER_CHECK_EQ(tds5_type_name(column), std::string("uniqueidentifier"));
}

// --- tdswire: login e comando ---------------------------------------------------------

OTTER_TEST(tds5_login_record_has_fixed_fields_with_the_length_after) {
    using namespace otter::tdswire;
    Tds5ConnectParams params;
    params.user     = "ana";
    params.password = "s3gr3d0";
    params.database = "loja";
    const std::vector<std::byte> body = build_tds5_login(params, "estacao", "1234");

    // Cada campo de texto ocupa 30 bytes, e o byte seguinte e' o tamanho usado:
    // host (0..29, tamanho em 30), usuario (31..60, 61), senha (62..91, 92).
    OTTER_CHECK(body.size() > 512);
    OTTER_CHECK_EQ(static_cast<int>(body[0]), static_cast<int>('e'));
    OTTER_CHECK_EQ(static_cast<int>(body[30]), 7);
    OTTER_CHECK_EQ(static_cast<int>(body[31]), static_cast<int>('a'));
    OTTER_CHECK_EQ(static_cast<int>(body[61]), 3);
    OTTER_CHECK_EQ(static_cast<int>(body[92]), 7);

    // O banco vai no campo do "nome do servidor" (o ServiceName do jConnect).
    OTTER_CHECK(contains_bytes(body, "loja"));
    OTTER_CHECK(contains_bytes(body, "utf8"));

    // O TDS 5.0 manda a senha EM CLARO, e o SQL Anywhere nao cifra o canal. O
    // teste registra o fato: e' o motivo do aviso no dialogo de conexao, e se
    // um dia o login passar a cifra-la, o aviso muda junto.
    OTTER_CHECK(contains_bytes(body, "s3gr3d0"));
}

OTTER_TEST(tds5_command_goes_in_a_language_token) {
    using namespace otter::tdswire;
    const std::vector<std::byte> body = build_tds5_language("SELECT 1");
    // 0x21, tamanho (4 bytes, o estado + o texto), estado 0, o texto em UTF-8.
    OTTER_CHECK_EQ(body.size(), std::size_t{1 + 4 + 1 + 8});
    OTTER_CHECK_EQ(static_cast<int>(body[0]), 0x21);
    OTTER_CHECK_EQ(static_cast<int>(body[1]), 9);
    OTTER_CHECK_EQ(static_cast<int>(body[5]), 0);
    OTTER_CHECK_EQ(static_cast<int>(body[6]), static_cast<int>('S'));

    // O token CAPABILITY abre com 0xE2 e traz os dois blocos: pedido e recusa.
    const std::vector<std::byte> caps = build_tds5_capabilities();
    OTTER_CHECK_EQ(static_cast<int>(caps[0]), 0xE2);
    OTTER_CHECK_EQ(static_cast<int>(caps[3]), 1);   // CAP_REQUEST
}

// --- Driver ---------------------------------------------------------------------------

OTTER_TEST(sqlanywhere_session_starts_with_the_database_defaults_not_the_ase_ones) {
    // O TDS liga as opcoes de compatibilidade com o ASE: aspas duplas viram
    // TEXTO e coluna sem NULL declarado vira NOT NULL. O SQL gerado aqui conta
    // com o contrario.
    const std::vector<std::string> options = sqlanywhere_session_options();
    const auto has = [&options](std::string_view piece) {
        for (const std::string& option : options) {
            if (option.find(piece) != std::string::npos) return true;
        }
        return false;
    };
    OTTER_CHECK(has("quoted_identifier = 'On'"));
    OTTER_CHECK(has("allow_nulls_by_default = 'On'"));
    // Datas como texto ISO: o DATETIME do TDS nao existe antes de 1753.
    OTTER_CHECK(has("return_date_time_as_string = 'On'"));
    OTTER_CHECK(has("date_format = 'YYYY-MM-DD'"));
    for (const std::string& option : options) {
        OTTER_CHECK(option.starts_with("SET TEMPORARY OPTION "));
    }
}

OTTER_TEST(sqlanywhere_fraction_is_trimmed_but_never_the_seconds) {
    OTTER_CHECK_EQ(sqlanywhere_trim_fraction("2024-05-06 07:08:09.000000"),
                   std::string("2024-05-06 07:08:09"));
    OTTER_CHECK_EQ(sqlanywhere_trim_fraction("2024-05-06 07:08:09.120000"),
                   std::string("2024-05-06 07:08:09.12"));
    OTTER_CHECK_EQ(sqlanywhere_trim_fraction("07:08:00"), std::string("07:08:00"));
    OTTER_CHECK_EQ(sqlanywhere_trim_fraction("2024-05-06"), std::string("2024-05-06"));
}

// --- Catalogo ---------------------------------------------------------------------------

OTTER_TEST(sqlanywhere_type_names_map_to_kinds) {
    OTTER_CHECK(sqlanywhere_kind("integer") == DataKind::integer);
    OTTER_CHECK(sqlanywhere_kind("unsigned bigint") == DataKind::integer);
    OTTER_CHECK(sqlanywhere_kind("numeric(10,2)") == DataKind::numeric);
    OTTER_CHECK(sqlanywhere_kind("bit") == DataKind::boolean);
    OTTER_CHECK(sqlanywhere_kind("char(20)") == DataKind::string);
    OTTER_CHECK(sqlanywhere_kind("long varchar") == DataKind::string);
    OTTER_CHECK(sqlanywhere_kind("long binary") == DataKind::binary);
    OTTER_CHECK(sqlanywhere_kind("timestamp") == DataKind::timestamp);
    OTTER_CHECK(sqlanywhere_kind("uniqueidentifier") == DataKind::uuid);
    OTTER_CHECK(sqlanywhere_kind("ST_Geometry") == DataKind::geometry);
}

OTTER_TEST(sqlanywhere_names_and_literals) {
    OTTER_CHECK_EQ(sqlanywhere_quote("a\"b"), std::string("\"a\"\"b\""));
    OTTER_CHECK_EQ(sqlanywhere_literal("d'agua"), std::string("'d''agua'"));
    OTTER_CHECK_EQ(sqlanywhere_object_name(ref_of(ObjectType::table, "DBA", "Nota Fiscal")),
                   std::string("\"DBA\".\"Nota Fiscal\""));
    OTTER_CHECK_EQ(sqlanywhere_object_name(ref_of(ObjectType::column, "DBA", "valor", "nota")),
                   std::string("\"DBA\".\"nota\".\"valor\""));
    // Evento e usuario nao tem dono no nome.
    OTTER_CHECK_EQ(sqlanywhere_object_name(ref_of(ObjectType::event, "DBA", "noturno")),
                   std::string("\"noturno\""));
    OTTER_CHECK_EQ(sqlanywhere_object_name(ref_of(ObjectType::role, "", "ana")),
                   std::string("\"ana\""));
}

// --- db/sqlanywhere_object --------------------------------------------------------------

OTTER_TEST(sqlanywhere_rename_comment_and_drop) {
    const AlterScript table =
        sqlanywhere_object_rename(ref_of(ObjectType::table, "DBA", "item"), "produto");
    OTTER_CHECK(table.ok());
    OTTER_CHECK_EQ(table.statements.front(),
                   std::string("ALTER TABLE \"DBA\".\"item\" RENAME \"produto\""));
    OTTER_CHECK(contains(
        sqlanywhere_object_rename(ref_of(ObjectType::column, "DBA", "nome", "item"), "descricao")
            .statements.front(),
        "RENAME \"nome\" TO \"descricao\""));
    // View e rotina nao se renomeiam: recusa com o motivo.
    OTTER_CHECK(!sqlanywhere_object_rename(ref_of(ObjectType::view, "DBA", "v"), "w").ok());
    OTTER_CHECK(!sqlanywhere_object_rename(ref_of(ObjectType::table, "DBA", "a"), "a").ok());

    OTTER_CHECK_EQ(sqlanywhere_object_comment(ref_of(ObjectType::table, "DBA", "item"), "d'agua")
                       .statements.front(),
                   std::string("COMMENT ON TABLE \"DBA\".\"item\" IS 'd''agua'"));
    // Vazio REMOVE o comentario.
    OTTER_CHECK(sqlanywhere_object_comment(ref_of(ObjectType::table, "DBA", "item"), "")
                    .statements.front()
                    .ends_with(" IS NULL"));
    // O indice e' citado pela tabela: dono.tabela.indice.
    OTTER_CHECK(contains(
        sqlanywhere_object_comment(ref_of(ObjectType::index, "DBA", "ix_nome", "item"), "x")
            .statements.front(),
        "COMMENT ON INDEX \"DBA\".\"item\".\"ix_nome\""));

    const AlterScript drop =
        sqlanywhere_object_drop(ref_of(ObjectType::index, "DBA", "ix_nome", "item"));
    OTTER_CHECK_EQ(drop.statements.front(),
                   std::string("DROP INDEX \"DBA\".\"item\".\"ix_nome\""));
    OTTER_CHECK(drop.has_destructive());
    OTTER_CHECK_EQ(sqlanywhere_object_drop(ref_of(ObjectType::foreign_key, "DBA", "fk", "item"))
                       .statements.front(),
                   std::string("ALTER TABLE \"DBA\".\"item\" DROP FOREIGN KEY \"fk\""));
    // Um dominio nao e' qualificado pelo dono.
    OTTER_CHECK_EQ(sqlanywhere_object_drop(ref_of(ObjectType::data_type, "DBA", "dinheiro"))
                       .statements.front(),
                   std::string("DROP DOMAIN \"dinheiro\""));
}

OTTER_TEST(sqlanywhere_user_and_role_are_dropped_differently) {
    // O usuario sai por DROP USER; o papel puro, por DROP ROLE -- e so' com
    // WITH REVOKE o servidor aceita enquanto ele estiver concedido a alguem.
    OTTER_CHECK_EQ(sqlanywhere_object_drop(ref_of(ObjectType::role, "", "ana")).statements.front(),
                   std::string("DROP USER \"ana\""));
    const AlterScript role =
        sqlanywhere_object_drop(ref_of(ObjectType::role, "", "leitores", "role"));
    OTTER_CHECK_EQ(role.statements.front(), std::string("DROP ROLE \"leitores\" WITH REVOKE"));
    OTTER_CHECK(!role.warnings.empty());

    SqlAnywhereNewUser user;
    user.name     = "ana";
    user.password = "se'nha";
    OTTER_CHECK_EQ(sqlanywhere_create_user(user).statements.front(),
                   std::string("CREATE USER \"ana\" IDENTIFIED BY 'se''nha'"));
    user.login_policy = "root";
    OTTER_CHECK(sqlanywhere_create_user(user).statements.front().ends_with(
        " LOGIN POLICY \"root\""));
    // Sem senha o usuario existe e nao entra: vale, com aviso.
    user.password.clear();
    OTTER_CHECK(sqlanywhere_create_user(user).ok());
    OTTER_CHECK(!sqlanywhere_create_user(user).warnings.empty());
    user.name.clear();
    OTTER_CHECK(!sqlanywhere_create_user(user).ok());

    OTTER_CHECK_EQ(sqlanywhere_create_role("leitores").statements.front(),
                   std::string("CREATE ROLE \"leitores\""));
    OTTER_CHECK_EQ(sqlanywhere_user_password("ana", "nova").statements.front(),
                   std::string("ALTER USER \"ana\" IDENTIFIED BY 'nova'"));
    OTTER_CHECK(!sqlanywhere_user_password("ana", "").ok());
}

OTTER_TEST(sqlanywhere_grant_and_revoke) {
    const ObjectRef table = ref_of(ObjectType::table, "DBA", "item");
    OTTER_CHECK_EQ(sqlanywhere_grant(table, "select", "ana", true).statements.front(),
                   std::string("GRANT SELECT ON \"DBA\".\"item\" TO \"ana\" WITH GRANT OPTION"));
    OTTER_CHECK_EQ(sqlanywhere_revoke(table, "SELECT", "ana").statements.front(),
                   std::string("REVOKE SELECT ON \"DBA\".\"item\" FROM \"ana\""));

    const ObjectRef sequence = ref_of(ObjectType::sequence, "DBA", "s");
    OTTER_CHECK_EQ(sqlanywhere_grant(sequence, "USAGE", "ana", false).statements.front(),
                   std::string("GRANT USAGE ON SEQUENCE \"DBA\".\"s\" TO \"ana\""));
    OTTER_CHECK(!sqlanywhere_grant(sequence, "SELECT", "ana", false).ok());

    // WITH GRANT OPTION so' existe para tabela e view.
    OTTER_CHECK(!sqlanywhere_grant(ref_of(ObjectType::procedure, "DBA", "p"), "EXECUTE", "ana", true)
                     .ok());
    // O privilegio vai sem aspas: so' letras passam.
    OTTER_CHECK(!sqlanywhere_grant(table, "SELECT; DROP TABLE x", "ana", false).ok());
    OTTER_CHECK(!sqlanywhere_grant(table, "SELECT", "", false).ok());
}

OTTER_TEST(sqlanywhere_source_is_saved_with_alter) {
    const ObjectRef view = ref_of(ObjectType::view, "DBA", "v");
    // CREATE vira ALTER -- depois dos comentarios do comeco (os tres estilos),
    // sem tocar neles.
    const AlterScript script = sqlanywhere_source_script(
        view, "-- relatorio\n// v2\n/* x */ CREATE VIEW DBA.v AS SELECT 1 AS n;\n");
    OTTER_CHECK(script.ok());
    OTTER_CHECK_EQ(script.statements.front(),
                   std::string("-- relatorio\n// v2\n/* x */ ALTER VIEW DBA.v AS SELECT 1 AS n"));

    OTTER_CHECK_EQ(sqlanywhere_source_script(view, "CREATE OR REPLACE VIEW DBA.v AS SELECT 1")
                       .statements.front(),
                   std::string("CREATE OR REPLACE VIEW DBA.v AS SELECT 1"));
    OTTER_CHECK(!sqlanywhere_source_script(view, "CREATEVIEW DBA.v AS SELECT 1").ok());
    OTTER_CHECK(!sqlanywhere_source_script(view, "SELECT 1").ok());
    OTTER_CHECK(!sqlanywhere_source_script(view, "   ").ok());

    // A view materializada nao aceita outra consulta por ALTER.
    OTTER_CHECK(!sqlanywhere_source_editable(ObjectType::materialized_view));
    OTTER_CHECK(sqlanywhere_source_editable(ObjectType::event));
    OTTER_CHECK(sqlanywhere_source_editable(ObjectType::trigger));
}

OTTER_TEST(sqlanywhere_create_sequence_domain_and_templates) {
    SqlAnywhereNewSequence sequence;
    sequence.schema    = "DBA";
    sequence.name      = "s";
    sequence.start     = "10";
    sequence.increment = "5";
    sequence.cycle     = true;
    OTTER_CHECK_EQ(sqlanywhere_create_sequence(sequence).statements.front(),
                   std::string("CREATE SEQUENCE \"DBA\".\"s\" INCREMENT BY 5 START WITH 10 CYCLE"));
    // Os numeros vao sem aspas: so' inteiros passam.
    sequence.maximum = "9; DROP";
    OTTER_CHECK(!sqlanywhere_create_sequence(sequence).ok());

    OTTER_CHECK_EQ(sqlanywhere_create_domain("dinheiro", "numeric(15,2)", true, "0", "@v >= 0")
                       .statements.front(),
                   std::string("CREATE DOMAIN \"dinheiro\" numeric(15,2) NOT NULL DEFAULT 0 "
                               "CHECK (@v >= 0)"));
    OTTER_CHECK(!sqlanywhere_create_domain("dinheiro", "", false, "", "").ok());

    OTTER_CHECK(sqlanywhere_routine_template("DBA", "p", true)
                    .starts_with("CREATE PROCEDURE \"DBA\".\"p\"()"));
    OTTER_CHECK(contains(sqlanywhere_routine_template("DBA", "f", false), "RETURNS integer"));
    OTTER_CHECK(contains(sqlanywhere_trigger_template("DBA", "item", "trg", "BEFORE", "UPDATE"),
                         "BEFORE UPDATE\nON \"DBA\".\"item\"\nREFERENCING NEW AS new_row"));
    // O evento nao tem dono no nome.
    OTTER_CHECK(sqlanywhere_event_template("noturno").starts_with("CREATE EVENT \"noturno\"\n"));
}

OTTER_TEST(sqlanywhere_tools_and_sessions) {
    OTTER_CHECK_EQ(sqlanywhere_table_tool(SqlAnywhereTableTool::validate, "DBA", "item")
                       .statements.front(),
                   std::string("VALIDATE TABLE \"DBA\".\"item\""));
    OTTER_CHECK_EQ(sqlanywhere_table_tool(SqlAnywhereTableTool::reorganize, "DBA", "item")
                       .statements.front(),
                   std::string("REORGANIZE TABLE \"DBA\".\"item\""));
    OTTER_CHECK_EQ(sqlanywhere_table_tool(SqlAnywhereTableTool::create_statistics, "DBA", "item")
                       .statements.front(),
                   std::string("CREATE STATISTICS \"DBA\".\"item\""));

    // TRUNCATE confirma a transacao: destrutivo, e com o aviso.
    const AlterScript truncate = sqlanywhere_truncate("DBA", "item");
    OTTER_CHECK(truncate.has_destructive());
    OTTER_CHECK(!truncate.warnings.empty());

    OTTER_CHECK_EQ(sqlanywhere_view_enable(ref_of(ObjectType::view, "DBA", "v"), false)
                       .statements.front(),
                   std::string("ALTER VIEW \"DBA\".\"v\" DISABLE"));
    OTTER_CHECK_EQ(
        sqlanywhere_view_enable(ref_of(ObjectType::materialized_view, "DBA", "mv"), true)
            .statements.front(),
        std::string("ALTER MATERIALIZED VIEW \"DBA\".\"mv\" ENABLE"));
    OTTER_CHECK(!sqlanywhere_view_enable(ref_of(ObjectType::table, "DBA", "t"), true).ok());

    OTTER_CHECK_EQ(sqlanywhere_event_enable("noturno", false).statements.front(),
                   std::string("ALTER EVENT \"noturno\" DISABLE"));
    OTTER_CHECK_EQ(sqlanywhere_event_trigger("noturno").statements.front(),
                   std::string("TRIGGER EVENT \"noturno\""));
    OTTER_CHECK_EQ(sqlanywhere_backup_database("C:\\b").statements.front(),
                   std::string("BACKUP DATABASE DIRECTORY 'C:\\b'"));
    OTTER_CHECK(!sqlanywhere_backup_database("").ok());

    // O numero da conexao vem de uma celula da grade: so' digitos.
    OTTER_CHECK_EQ(sqlanywhere_session_kill("57").statements.front(),
                   std::string("DROP CONNECTION 57"));
    OTTER_CHECK(!sqlanywhere_session_kill("57; DROP").ok());
    OTTER_CHECK(!sqlanywhere_session_kill("").ok());
}

OTTER_TEST(sqlanywhere_generic_generators_follow_the_dialect) {
    OTTER_CHECK_EQ(transaction_begin_sql(), std::string_view("BEGIN"));
    {
        const AnywhereDialect dialect;
        // "BEGIN" sozinho abre um BLOCO no Watcom SQL.
        OTTER_CHECK_EQ(transaction_begin_sql(), std::string_view("BEGIN TRANSACTION"));

        const ObjectRef table = ref_of(ObjectType::table, "DBA", "item");
        OTTER_CHECK_EQ(object_sql_name(table), std::string("\"DBA\".\"item\""));
        OTTER_CHECK(contains(generate_object_rename(table, "produto").statements.front(),
                             "RENAME \"produto\""));
        OTTER_CHECK_EQ(generate_object_drop(table, /*cascade=*/true).statements.front(),
                       std::string("DROP TABLE \"DBA\".\"item\""));
        // O dono nao se troca, nem o "schema" (que e' o dono).
        OTTER_CHECK(!generate_object_owner(table, "ana").ok());
        OTTER_CHECK(!generate_object_schema(table, "ana").ok());

        TableMeta meta;
        meta.name = "item";
        // TOP sem parenteses, e nada de LIMIT.
        OTTER_CHECK(contains(generate_select("DBA", meta, 50), "SELECT TOP 50 *"));
        OTTER_CHECK(!contains(generate_select("DBA", meta, 50), "LIMIT"));
    }
    OTTER_CHECK_EQ(transaction_begin_sql(), std::string_view("BEGIN"));
}

// --- Literais -----------------------------------------------------------------------

OTTER_TEST(sqlanywhere_literals_outside_ascii_go_through_unistr) {
    const AnywhereDialect dialect;
    // O servidor converte o COMANDO para o conjunto de caracteres do banco
    // (cp1252): um literal comum com "日本" chegaria como "??".
    OTTER_CHECK_EQ(quote_literal("ação"), std::string("UNISTR('a\\u00e7\\u00e3o')"));
    // So' ASCII: literal comum, e a barra invertida nao e' escape.
    OTTER_CHECK_EQ(quote_literal("d'agua"), std::string("'d''agua'"));
    OTTER_CHECK_EQ(quote_literal("a\\b"), std::string("'a\\b'"));
    // Dentro do UNISTR a barra E' o escape: vai pelo codigo dela.
    OTTER_CHECK_EQ(quote_literal("é\\'"), std::string("UNISTR('\\u00e9\\u005c''')"));
    // Fora do plano basico: par substituto.
    OTTER_CHECK_EQ(quote_literal("\xF0\x9F\x98\x80"), std::string("UNISTR('\\ud83d\\ude00')"));

    ColumnInfo bit;
    bit.kind = DataKind::boolean;
    OTTER_CHECK_EQ(sql_literal(bit, "true", false), std::string("1"));
    OTTER_CHECK_EQ(sql_literal(bit, "false", false), std::string("0"));

    ColumnInfo binary;
    binary.kind = DataKind::binary;
    OTTER_CHECK_EQ(sql_literal(binary, "0xCAFE", false), std::string("0xCAFE"));
    OTTER_CHECK_EQ(sql_literal(binary, "0xZZ; DROP", false), std::string("'0xZZ; DROP'"));
}

// --- Paginacao ----------------------------------------------------------------------

OTTER_TEST(sqlanywhere_paging_puts_top_start_at_in_the_select) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");

    // Uma linha a mais que a pagina, para saber se ha' proxima; START AT
    // comeca em 1.
    sql::PagedQuery q = sql::make_paged_query("SELECT * FROM t ORDER BY id;", dialect, 0, 200);
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT TOP 201 START AT 1 * FROM t ORDER BY id"));

    q = sql::make_paged_query("SELECT * FROM t ORDER BY id", dialect, 2, 100);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT TOP 101 START AT 201 * FROM t ORDER BY id"));

    // DISTINCT vem antes do TOP.
    q = sql::make_paged_query("SELECT DISTINCT a FROM t", dialect, 0, 10);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT DISTINCT TOP 11 START AT 1 a FROM t"));
    OTTER_CHECK(!contains(q.sql, "LIMIT"));
    OTTER_CHECK(!contains(q.sql, "OFFSET"));
}

OTTER_TEST(sqlanywhere_paging_wraps_unions_and_filters) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");

    // Num UNION o TOP do primeiro SELECT limitaria so' ele: embrulha.
    sql::PagedQuery q =
        sql::make_paged_query("SELECT a FROM t UNION SELECT a FROM u", dialect, 0, 10);
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(q.sql.starts_with("SELECT TOP 11 START AT 1 * FROM (\n"));
    OTTER_CHECK(contains(q.sql, "\n) AS otter_page"));

    sql::SortOrder sort;
    sort.column = "Nome";
    sql::ColumnFilter filter;
    filter.set_column("total", "> 100");
    q = sql::make_paged_query("SELECT * FROM t", dialect, 0, 10, sort, filter);
    OTTER_CHECK(contains(q.sql, "WHERE \"total\" > 100"));
    OTTER_CHECK(contains(q.sql, "ORDER BY \"Nome\" ASC"));
}

OTTER_TEST(sqlanywhere_paging_refuses_what_is_already_limited) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");
    const auto refusal = [&dialect](const char* text) {
        return sql::make_paged_query(text, dialect, 0, 10).refusal;
    };
    OTTER_CHECK(refusal("SELECT TOP 5 * FROM t") == sql::PagingRefusal::already_limited);
    // FIRST e' o "TOP 1" do Watcom SQL.
    OTTER_CHECK(refusal("SELECT FIRST * FROM t ORDER BY a") ==
                sql::PagingRefusal::already_limited);
    OTTER_CHECK(refusal("SELECT * INTO #tmp FROM t") == sql::PagingRefusal::not_a_query);
    OTTER_CHECK(refusal("CALL sa_conn_info()") == sql::PagingRefusal::not_a_query);
    OTTER_CHECK(refusal("SELECT * FROM (SELECT TOP 5 * FROM t ORDER BY a) s") ==
                sql::PagingRefusal::none);
}

OTTER_TEST(sqlanywhere_distinct_query_uses_top_without_parentheses) {
    const AnywhereDialect dialect;
    const std::string query = distinct_query("SELECT * FROM t;", "Nome", 50);
    OTTER_CHECK(query.starts_with("SELECT TOP 50 \"Nome\", COUNT(*)"));
    OTTER_CHECK(!contains(query, "LIMIT"));
    OTTER_CHECK(!contains(query, ";"));
}

// --- Dialeto e lotes ----------------------------------------------------------------------

OTTER_TEST(sqlanywhere_dialect_has_three_comment_styles_and_go) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");
    OTTER_CHECK_EQ(dialect.name, std::string_view("SQL Anywhere"));
    OTTER_CHECK(dialect.go_batch_separator);
    OTTER_CHECK(dialect.slash_line_comments);
    OTTER_CHECK_EQ(dialect.quote_identifier("a\"b"), std::string("\"a\"\"b\""));

    // "//" comenta ate' o fim da linha: o ';' de dentro nao separa.
    const std::string script = "SELECT 1; // nota; com ponto e virgula\nSELECT 2;";
    OTTER_CHECK_EQ(sql::statement_ranges(script, dialect).size(), std::size_t{2});
    // Nos outros SGBDs "//" nao e' comentario.
    OTTER_CHECK(!sql::dialect_for("postgresql").slash_line_comments);
}

OTTER_TEST(sqlanywhere_routine_and_event_bodies_are_one_statement) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");

    const std::string procedure =
        "CREATE OR REPLACE PROCEDURE DBA.p()\nBEGIN\n  DECLARE n integer;\n  SET n = 1;\n"
        "  SELECT n;\nEND;\nSELECT 2;";
    auto ranges = sql::statement_ranges(procedure, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});
    OTTER_CHECK(procedure.substr(ranges[0].begin, ranges[0].end - ranges[0].begin)
                    .ends_with("END"));

    const std::string event =
        "CREATE EVENT noturno SCHEDULE START TIME '00:00' EVERY 24 HOURS\nHANDLER\nBEGIN\n"
        "  MESSAGE 'a' TO CONSOLE;\n  MESSAGE 'b' TO CONSOLE;\nEND;\nSELECT 1;";
    ranges = sql::statement_ranges(event, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});

    // BEGIN TRANSACTION nao abre bloco.
    const std::string txn = "BEGIN TRANSACTION; UPDATE t SET a = 1; COMMIT;";
    OTTER_CHECK_EQ(sql::statement_ranges(txn, dialect).size(), std::size_t{3});
}

OTTER_TEST(sqlanywhere_end_if_and_end_loop_do_not_close_the_body) {
    const sql::Dialect& dialect = sql::dialect_for("sqlanywhere");
    // END IF, END LOOP, END FOR e END CASE nao sao o END do bloco: contar
    // qualquer END fechava o corpo no primeiro IF, e o ';' seguinte partia a
    // procedure ao meio.
    const std::string script =
        "CREATE PROCEDURE DBA.p(IN n integer)\nBEGIN\n"
        "  IF n > 0 THEN\n    MESSAGE 'a' TO CONSOLE;\n  END IF;\n"
        "  WHILE n > 0 LOOP\n    SET n = n - 1;\n  END LOOP;\n"
        "  CASE n WHEN 0 THEN MESSAGE 'z' TO CONSOLE; END CASE;\n"
        "  SELECT CASE WHEN n = 0 THEN 'zero' ELSE 'outro' END;\n"
        "END;\nCALL DBA.p(3);";
    const auto ranges = sql::statement_ranges(script, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});
    OTTER_CHECK(script.substr(ranges[0].begin, ranges[0].end - ranges[0].begin).ends_with("END"));
    OTTER_CHECK_EQ(script.substr(ranges[1].begin, ranges[1].end - ranges[1].begin),
                   std::string("CALL DBA.p(3)"));

    // A forma T-SQL, sem bloco, continua indo ate' o fim do lote (ou o GO).
    const std::string tsql = "CREATE PROCEDURE DBA.q AS\n  SELECT 1;\n  SELECT 2;\nGO\nSELECT 3;";
    const auto batches = sql::statement_ranges(tsql, dialect);
    OTTER_CHECK_EQ(batches.size(), std::size_t{2});
    OTTER_CHECK(tsql.substr(batches[0].begin, batches[0].end - batches[0].begin)
                    .ends_with("SELECT 2;"));
}

// --- db/alter em Watcom SQL ---------------------------------------------------------------

OTTER_TEST(sqlanywhere_index_and_foreign_key_statements) {
    const AnywhereDialect dialect;

    NewIndex index;
    index.name    = "ix_nome";
    index.columns = {"nome"};
    index.unique  = true;
    OTTER_CHECK_EQ(generate_create_index("vendas", "cliente", index).statements.front(),
                   std::string("CREATE UNIQUE INDEX ix_nome ON vendas.cliente (nome)"));
    // O indice e' citado pela tabela, e nao ha' "ON".
    OTTER_CHECK_EQ(generate_drop_index("vendas", "cliente", "ix_nome", false).statements.front(),
                   std::string("DROP INDEX vendas.cliente.ix_nome"));

    NewForeignKey key;
    key.name           = "fk_cliente";
    key.columns        = {"cliente_id"};
    key.target_table   = "cliente";
    key.target_columns = {"id"};
    const std::string fk = generate_add_foreign_key("vendas", "pedido", key).statements.front();
    // O nome e' o "papel" da chave, logo depois de FOREIGN KEY -- e' por ele
    // que DROP FOREIGN KEY a acha.
    OTTER_CHECK(contains(fk, "ADD FOREIGN KEY fk_cliente (cliente_id)"));
    OTTER_CHECK(contains(fk, "REFERENCES vendas.cliente (id)"));
}

// --- Conexao: URL, preparo da sessao, armazenamento -----------------------------------------

OTTER_TEST(sqlanywhere_url_forms_reach_the_same_profile) {
    auto own = profile_from_url("jdbc:sqlanywhere://ana@servidor:2639/loja");
    OTTER_CHECK(own.has_value());
    OTTER_CHECK_EQ(own->driver_id, std::string("sqlanywhere"));
    OTTER_CHECK_EQ(own->host, std::string("servidor"));
    OTTER_CHECK_EQ(static_cast<int>(own->port), 2639);
    OTTER_CHECK_EQ(own->database, std::string("loja"));
    OTTER_CHECK_EQ(own->user, std::string("ana"));

    // A do jConnect, que e' a que o DBeaver grava: sem "//", banco em ServiceName.
    auto jconnect = profile_from_url("jdbc:sybase:Tds:servidor:2638?ServiceName=loja");
    OTTER_CHECK(jconnect.has_value());
    OTTER_CHECK_EQ(jconnect->driver_id, std::string("sqlanywhere"));
    OTTER_CHECK_EQ(jconnect->host, std::string("servidor"));
    OTTER_CHECK_EQ(static_cast<int>(jconnect->port), 2638);
    OTTER_CHECK_EQ(jconnect->database, std::string("loja"));

    // A do jTDS.
    auto jtds = profile_from_url("jdbc:jtds:sybase://servidor/loja");
    OTTER_CHECK(jtds.has_value());
    OTTER_CHECK_EQ(jtds->driver_id, std::string("sqlanywhere"));
    OTTER_CHECK_EQ(static_cast<int>(jtds->port), 2638);
    OTTER_CHECK_EQ(jtds->database, std::string("loja"));
}

OTTER_TEST(sqlanywhere_session_setup_emits_only_what_the_server_has) {
    SessionSetup setup;
    setup.default_schema    = "vendas";
    setup.session_role      = "leitor";
    setup.read_only         = true;
    setup.bootstrap_queries = "SET TEMPORARY OPTION blocking = 'Off'\n";

    // Sem SET ROLE, sem search_path e sem READ ONLY: nada disso existe la', e
    // um comando recusado derrubaria a conexao.
    const std::vector<std::string> statements = session_setup_statements("sqlanywhere", setup);
    OTTER_CHECK_EQ(statements.size(), std::size_t{1});
    OTTER_CHECK_EQ(statements.front(), std::string("SET TEMPORARY OPTION blocking = 'Off'"));
}

OTTER_TEST(sybase_drivers_of_dbeaver_map_to_sqlanywhere_not_to_sql_server) {
    // No DBeaver os drivers Sybase moram no provider "mssql", junto do SQL
    // Server -- e falam TDS 5.0. Mapear pelo provider os mandava ao driver do
    // SQL Server, que nem passa do login.
    for (const char* driver : {"sybase_jconn", "sybase_jtds", "sypase_jconn"}) {
        StoredProfile stored;
        stored.provider = "mssql";
        stored.driver   = driver;
        resolve_driver(stored);
        OTTER_CHECK(stored.supported);
        OTTER_CHECK_EQ(stored.profile.driver_id, std::string("sqlanywhere"));
    }

    // O SQL Server continua onde estava.
    StoredProfile microsoft;
    microsoft.provider = "mssql";
    microsoft.driver   = "microsoft";
    resolve_driver(microsoft);
    OTTER_CHECK_EQ(microsoft.profile.driver_id, std::string("sqlserver"));

    // E um perfil criado aqui e' gravado como o DBeaver o alcanca.
    const ProviderNames names = provider_for_driver("sqlanywhere");
    OTTER_CHECK_EQ(std::string(names.provider), std::string("mssql"));
    OTTER_CHECK_EQ(std::string(names.driver), std::string("sybase_jconn"));
}

OTTER_TEST(sqlanywhere_has_its_own_icon) {
    OTTER_CHECK(ui::driver_icon("sqlanywhere") == ui::Icon::sa_server);
    OTTER_CHECK(ui::driver_icon("sqlanywhere") != ui::driver_icon("sqlserver"));
    OTTER_CHECK(ui::driver_icon("sqlanywhere") != ui::Icon::generic_server);
}
