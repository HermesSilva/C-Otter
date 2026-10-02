// Oracle acima do protocolo: o texto que vai ao servidor, a divisao de um
// script com PL/SQL, a paginacao e os tipos do dicionario. O protocolo esta'
// em test_orawire.cpp.
#include "test_main.hpp"

#include "db/app_tools.hpp"
#include "db/catalog_oracle.hpp"
#include "db/drivers/oracle.hpp"
#include "db/registry.hpp"
#include "sql/dialect.hpp"
#include "sql/paging.hpp"
#include "sql/script.hpp"

#include <string>
#include <vector>

using namespace otter;

namespace {

std::vector<std::string> split(std::string_view script) {
    std::vector<std::string> out;
    for (const sql::Statement& statement :
         sql::split_script(script, sql::oracle_dialect())) {
        out.emplace_back(statement.text);
    }
    return out;
}

} // namespace

OTTER_TEST(oracle_driver_is_registered) {
    db::Driver* driver = db::find_driver("oracle");
    OTTER_CHECK(driver != nullptr);
    OTTER_CHECK_EQ(driver->default_port(), std::uint16_t{1521});
    OTTER_CHECK_EQ(std::string(sql::dialect_for("oracle").name), std::string{"Oracle"});
}

// O servidor recusa "SELECT ... ;" (ORA-00933): o ';' de quem vem de outro
// cliente sai. Mas ele E' da linguagem num bloco PL/SQL.
OTTER_TEST(oracle_statement_drops_the_terminator_sql_does_not_have) {
    OTTER_CHECK_EQ(db::oracle_statement_text("select 1 from dual;"),
                   std::string{"select 1 from dual"});
    OTTER_CHECK_EQ(db::oracle_statement_text("select 1 from dual ;  \n"),
                   std::string{"select 1 from dual"});
    OTTER_CHECK_EQ(db::oracle_statement_text("update t set a = 1;;"),
                   std::string{"update t set a = 1"});
    OTTER_CHECK_EQ(db::oracle_statement_text("call p();"), std::string{"call p()"});
    // A barra do SQL*Plus, sozinha na ultima linha.
    OTTER_CHECK_EQ(db::oracle_statement_text("select 1 from dual\n/\n"),
                   std::string{"select 1 from dual"});
    // Uma divisao NAO e' a barra do SQL*Plus.
    OTTER_CHECK_EQ(db::oracle_statement_text("select 4 /\n 2 from dual"),
                   std::string{"select 4 /\n 2 from dual"});
}

OTTER_TEST(oracle_statement_keeps_the_terminator_plsql_needs) {
    OTTER_CHECK_EQ(db::oracle_statement_text("begin null; end;"),
                   std::string{"begin null; end;"});
    OTTER_CHECK_EQ(db::oracle_statement_text("DECLARE x number; BEGIN x := 1; END;\n/"),
                   std::string{"DECLARE x number; BEGIN x := 1; END;"});
    OTTER_CHECK_EQ(db::oracle_statement_text(
                       "create or replace procedure p is begin null; end;"),
                   std::string{"create or replace procedure p is begin null; end;"});
    // O divisor de scripts entrega o bloco sem o ';' final: ele volta.
    OTTER_CHECK_EQ(db::oracle_statement_text("begin null; end"),
                   std::string{"begin null; end;"});
    OTTER_CHECK_EQ(db::oracle_statement_text(
                       "CREATE OR REPLACE EDITIONABLE TRIGGER t BEFORE INSERT ON x "
                       "BEGIN NULL; END"),
                   std::string{"CREATE OR REPLACE EDITIONABLE TRIGGER t BEFORE INSERT ON x "
                               "BEGIN NULL; END;"});
    // CREATE TABLE nao e' codigo armazenado.
    OTTER_CHECK_EQ(db::oracle_statement_text("create table t (a number);"),
                   std::string{"create table t (a number)"});
}

OTTER_TEST(oracle_quote_and_literal_double_their_delimiter) {
    OTTER_CHECK_EQ(db::oracle_quote("Nome"), std::string{"\"Nome\""});
    OTTER_CHECK_EQ(db::oracle_quote("a\"b"), std::string{"\"a\"\"b\""});
    OTTER_CHECK_EQ(db::oracle_literal("O'Brien"), std::string{"'O''Brien'"});
}

OTTER_TEST(oracle_url_has_the_service_form_and_the_sid_form) {
    // A forma "thin" com nome de servico, como o DBeaver a grava.
    const auto service = db::profile_from_url("jdbc:oracle:thin:@//db.example:1522/FREEPDB1");
    OTTER_CHECK(service.has_value());
    OTTER_CHECK_EQ(service->driver_id, std::string{"oracle"});
    OTTER_CHECK_EQ(service->host, std::string{"db.example"});
    OTTER_CHECK_EQ(service->port, std::uint16_t{1522});
    OTTER_CHECK_EQ(service->database, std::string{"FREEPDB1"});

    // Com usuario/senha, e a porta padrao.
    const auto credentials = db::profile_from_url("jdbc:oracle:thin:ana/segredo@//h/XEPDB1");
    OTTER_CHECK(credentials.has_value());
    OTTER_CHECK_EQ(credentials->user, std::string{"ana"});
    OTTER_CHECK_EQ(credentials->password, std::string{"segredo"});
    OTTER_CHECK_EQ(credentials->port, std::uint16_t{1521});

    // A forma antiga, por SID: vira a propriedade "sid" do driver.
    const auto sid = db::profile_from_url("jdbc:oracle:thin:@h:1521:XE");
    OTTER_CHECK(sid.has_value());
    OTTER_CHECK_EQ(sid->host, std::string{"h"});
    OTTER_CHECK(sid->database.empty());
    OTTER_CHECK(sid->driver_properties.contains("sid"));
    OTTER_CHECK_EQ(sid->driver_properties.at("sid"), std::string{"XE"});
}

OTTER_TEST(oracle_session_setup_uses_alter_session) {
    db::SessionSetup setup;
    setup.default_schema = "HR";
    setup.session_role   = "ignorado";     // SET ROLE do PostgreSQL nao vale aqui
    setup.read_only      = true;           // nao ha' sessao somente leitura
    const std::vector<std::string> statements = db::session_setup_statements("oracle", setup);
    OTTER_CHECK_EQ(statements.size(), std::size_t{1});
    OTTER_CHECK_EQ(statements[0], std::string{"ALTER SESSION SET CURRENT_SCHEMA = \"HR\""});
}

// --- script -----------------------------------------------------------------------

OTTER_TEST(oracle_script_splits_plain_sql_on_semicolons) {
    const std::vector<std::string> parts =
        split("select 1 from dual;\nselect 2 from dual;\n");
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
    OTTER_CHECK_EQ(parts[0], std::string{"select 1 from dual"});
}

OTTER_TEST(oracle_script_keeps_an_anonymous_block_whole) {
    // Os ';' de dentro nao separam; END IF e END LOOP nao fecham o bloco.
    const std::vector<std::string> parts = split(
        "begin\n"
        "  if 1 = 1 then null; end if;\n"
        "  for i in 1..3 loop null; end loop;\n"
        "  x := case when 1 = 1 then 2 else 3 end;\n"
        "end;\n"
        "select 1 from dual;\n");
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
    OTTER_CHECK(parts[0].find("end loop") != std::string::npos);
    OTTER_CHECK(parts[0].ends_with("end"));
    OTTER_CHECK(parts[1].find("select 1") != std::string::npos);
}

OTTER_TEST(oracle_script_ends_a_plsql_unit_at_the_slash) {
    // DECLARE tem ';' antes do BEGIN; um CREATE PROCEDURE tambem. So' a barra
    // sozinha na linha encerra.
    const std::vector<std::string> parts = split(
        "declare\n  x number;\nbegin\n  x := 4 / 2;\nend;\n/\n"
        "create or replace procedure p is\n  v number;\nbegin\n  null;\nend;\n/\n"
        "select 1 from dual;\n");
    OTTER_CHECK_EQ(parts.size(), std::size_t{3});
    OTTER_CHECK(parts[0].starts_with("declare"));
    OTTER_CHECK(parts[0].find("4 / 2") != std::string::npos);   // divisao nao e' a barra
    // O pedaco vai ate' a barra (com a quebra de linha); o driver o apara.
    OTTER_CHECK(db::oracle_statement_text(parts[0]).ends_with("end;"));
    OTTER_CHECK(parts[1].find("procedure p") != std::string::npos);
    OTTER_CHECK(parts[2].find("select 1") != std::string::npos);
}

OTTER_TEST(oracle_script_slash_also_ends_plain_sql) {
    const std::vector<std::string> parts = split("select 1 from dual\n/\nselect 2 from dual\n/\n");
    OTTER_CHECK_EQ(parts.size(), std::size_t{2});
}

// --- paginacao --------------------------------------------------------------------

OTTER_TEST(oracle_paging_uses_offset_fetch_without_as) {
    const sql::Dialect& dialect = sql::oracle_dialect();

    const sql::PagedQuery first = sql::make_paged_query("select * from t", dialect, 0, 200);
    OTTER_CHECK(first.rewritten);
    OTTER_CHECK(first.sql.find("FETCH NEXT 201 ROWS ONLY") != std::string::npos);
    OTTER_CHECK(first.sql.find("LIMIT") == std::string::npos);
    OTTER_CHECK(first.sql.find("OFFSET") == std::string::npos);

    const sql::PagedQuery third = sql::make_paged_query("select * from t", dialect, 2, 200);
    OTTER_CHECK(third.sql.find("OFFSET 400 ROWS\nFETCH NEXT 201 ROWS ONLY") !=
                std::string::npos);

    // "FROM (...) AS x" e' ORA-00933: o apelido vai sem AS.
    const sql::PagedQuery count = sql::make_count_query("select * from t", dialect);
    OTTER_CHECK(count.sql.find(") otter_count") != std::string::npos);
    OTTER_CHECK(count.sql.find(" AS ") == std::string::npos);
}

// --- dicionario -------------------------------------------------------------------

OTTER_TEST(oracle_type_text_follows_the_dictionary) {
    OTTER_CHECK_EQ(db::oracle_type_text("VARCHAR2", 30, -1, -1, 30, false),
                   std::string{"VARCHAR2(30)"});
    // Semantica de caractere: 30 caracteres ocupam ate' 120 bytes.
    OTTER_CHECK_EQ(db::oracle_type_text("VARCHAR2", 120, -1, -1, 30, true),
                   std::string{"VARCHAR2(30 CHAR)"});
    OTTER_CHECK_EQ(db::oracle_type_text("NVARCHAR2", 60, -1, -1, 30, true),
                   std::string{"NVARCHAR2(30)"});
    OTTER_CHECK_EQ(db::oracle_type_text("NUMBER", 22, 10, 2, 0, false),
                   std::string{"NUMBER(10,2)"});
    OTTER_CHECK_EQ(db::oracle_type_text("NUMBER", 22, 10, 0, 0, false),
                   std::string{"NUMBER(10)"});
    OTTER_CHECK_EQ(db::oracle_type_text("NUMBER", 22, -1, -1, 0, false),
                   std::string{"NUMBER"});
    OTTER_CHECK_EQ(db::oracle_type_text("RAW", 16, -1, -1, 0, false), std::string{"RAW(16)"});
    OTTER_CHECK_EQ(db::oracle_type_text("TIMESTAMP(6)", 11, -1, 6, 0, false),
                   std::string{"TIMESTAMP(6)"});
    OTTER_CHECK_EQ(db::oracle_type_text("DATE", 7, -1, -1, 0, false), std::string{"DATE"});
}

OTTER_TEST(oracle_kind_separates_integer_from_decimal) {
    OTTER_CHECK(db::oracle_kind("NUMBER", 10, 0) == db::DataKind::integer);
    OTTER_CHECK(db::oracle_kind("NUMBER", 10, 2) == db::DataKind::numeric);
    // NUMBER sem precisao aceita qualquer coisa: decimal exato.
    OTTER_CHECK(db::oracle_kind("NUMBER", -1, -1) == db::DataKind::numeric);
    OTTER_CHECK(db::oracle_kind("DATE", -1, -1) == db::DataKind::timestamp);
    OTTER_CHECK(db::oracle_kind("TIMESTAMP(6) WITH TIME ZONE", -1, 6) ==
                db::DataKind::timestamp);
    OTTER_CHECK(db::oracle_kind("BLOB", -1, -1) == db::DataKind::binary);
    OTTER_CHECK(db::oracle_kind("VARCHAR2", -1, -1) == db::DataKind::string);
}
