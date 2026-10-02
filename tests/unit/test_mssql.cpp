// C-Otter -- testes do perfil SQL Server: o protocolo (lib/tdswire), os
// geradores de T-SQL (db/mssql_object, db/alter, db/edit) e a paginacao por
// OFFSET/FETCH (sql/paging).
//
// O que se protege:
//   - a conversao BINARIO -> texto dos valores do TDS, com vetores calculados
//     a' mao pela especificacao ([MS-TDS] 2.2.5) e conferidos no servidor
//   - a senha fora do pacote de login em claro
//   - o que o T-SQL tem de diferente e que um gerador "quase igual" erraria:
//     ADD sem COLUMN, ALTER COLUMN com tipo E nulidade, default como
//     constraint, TOP/OFFSET no lugar de LIMIT, N'...' e bit 0/1
//
// O efeito de cada comando no servidor e' conferido em
// tests/integration/test_mssql_live.cpp.
#include "test_main.hpp"

#include "db/alter.hpp"
#include "db/app_tools.hpp"
#include "db/catalog_mssql.hpp"
#include "db/ddl.hpp"
#include "db/edit.hpp"
#include "db/grid_ops.hpp"
#include "db/import.hpp"
#include "db/mssql_object.hpp"
#include "db/object_info.hpp"
#include "sql/dialect.hpp"
#include "sql/editing.hpp"
#include "sql/paging.hpp"
#include "sql/script.hpp"
#include "tdswire/browser.hpp"
#include "tdswire/connection.hpp"
#include "tdswire/value.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <vector>

using namespace otter;
using namespace otter::db;

namespace {

// O dialeto do SQL Server durante o teste, e o anterior de volta no fim: os
// outros testes da suite contam com o padrao (aspas duplas).
struct MssqlDialect {
    QuoteStyle previous = sql_dialect();
    MssqlDialect() { set_sql_dialect(QuoteStyle::brackets); }
    ~MssqlDialect() { set_sql_dialect(previous); }
};

std::vector<std::byte> bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (const int value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

bool contains(const std::string& text, std::string_view piece) {
    return text.find(piece) != std::string::npos;
}

bool contains_bytes(const std::vector<std::byte>& haystack, const std::string& needle) {
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

TableMeta sample_table() {
    TableMeta table;
    table.name           = "cliente";
    table.columns_loaded = true;

    ColumnMeta id;
    id.name = "id"; id.type_name = "int"; id.nullable = false;
    id.default_value = "IDENTITY";
    ColumnMeta nome;
    nome.name = "nome"; nome.type_name = "nvarchar(100)"; nome.nullable = false;
    ColumnMeta ativo;
    ativo.name = "ativo"; ativo.type_name = "bit"; ativo.nullable = false;
    ativo.default_value = "((1))";
    table.columns = {id, nome, ativo};
    return table;
}

} // namespace

// --- tdswire: valores ---------------------------------------------------------------

OTTER_TEST(tds_integers_are_little_endian_and_tinyint_is_unsigned) {
    using namespace otter::tdswire;
    OTTER_CHECK_EQ(format_integer(bytes({0xD2, 0x04, 0x00, 0x00})), std::string("1234"));
    OTTER_CHECK_EQ(format_integer(bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})),
                   std::string("-1"));
    OTTER_CHECK_EQ(format_integer(bytes({0xFE, 0xFF})), std::string("-2"));
    // tinyint vai de 0 a 255: 0xFF nao e' -1.
    OTTER_CHECK_EQ(format_integer(bytes({0xFF})), std::string("255"));
}

OTTER_TEST(tds_money_has_the_high_half_first) {
    using namespace otter::tdswire;
    // 12.3456 -> 123456 = 0x0001E240, em dez-milesimos: metade ALTA primeiro.
    OTTER_CHECK_EQ(format_money(bytes({0x00, 0x00, 0x00, 0x00, 0x40, 0xE2, 0x01, 0x00})),
                   std::string("12.3456"));
    // smallmoney: 1.5 -> 15000 = 0x3A98.
    OTTER_CHECK_EQ(format_money(bytes({0x98, 0x3A, 0x00, 0x00})), std::string("1.5000"));
    // -1.0000 -> -10000 = 0xFFFFFFFF FFFFD8F0.
    OTTER_CHECK_EQ(format_money(bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0xD8, 0xFF, 0xFF})),
                   std::string("-1.0000"));
}

OTTER_TEST(tds_decimal_is_sign_plus_scaled_integer) {
    using namespace otter::tdswire;
    // 123.45 com escala 2 -> 12345 = 0x3039; o primeiro byte e' o sinal (1 = +).
    OTTER_CHECK_EQ(format_decimal(bytes({0x01, 0x39, 0x30, 0x00, 0x00}), 2),
                   std::string("123.45"));
    OTTER_CHECK_EQ(format_decimal(bytes({0x00, 0x39, 0x30, 0x00, 0x00}), 2),
                   std::string("-123.45"));
    // O zero a' esquerda da virgula nao pode sumir: -0.05, e nao -.05.
    OTTER_CHECK_EQ(format_decimal(bytes({0x00, 0x05, 0x00, 0x00, 0x00}), 2),
                   std::string("-0.05"));
    OTTER_CHECK_EQ(format_decimal(bytes({0x01, 0x07, 0x00, 0x00, 0x00}), 0), std::string("7"));
}

OTTER_TEST(tds_dates_count_from_1900_and_from_year_one) {
    using namespace otter::tdswire;
    // datetime: 36524 dias desde 1900-01-01 = 2000-01-01; 12:00 = 43200 s * 300.
    OTTER_CHECK_EQ(format_datetime(bytes({0xAC, 0x8E, 0x00, 0x00, 0x00, 0xC1, 0xC5, 0x00})),
                   std::string("2000-01-01 12:00:00.000"));
    // smalldatetime: dias (16 bits) + minutos (16 bits).
    OTTER_CHECK_EQ(format_datetime(bytes({0xAC, 0x8E, 0xD0, 0x02})),
                   std::string("2000-01-01 12:00:00"));
    // date: 730119 dias desde 0001-01-01 = 2000-01-01.
    OTTER_CHECK_EQ(format_date(bytes({0x07, 0x24, 0x0B})), std::string("2000-01-01"));
    // time(0): segundos, em 3 bytes.
    OTTER_CHECK_EQ(format_time(bytes({0xC0, 0xA8, 0x00}), 0), std::string("12:00:00"));
    // datetime2(0): a hora (3 bytes) e depois a data (3 bytes).
    OTTER_CHECK_EQ(format_datetime2(bytes({0xC0, 0xA8, 0x00, 0x07, 0x24, 0x0B}), 0),
                   std::string("2000-01-01 12:00:00"));
}

OTTER_TEST(tds_guid_swaps_the_first_three_groups) {
    using namespace otter::tdswire;
    const std::string text = format_guid(
        bytes({0x33, 0x22, 0x11, 0x00, 0x55, 0x44, 0x77, 0x66, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD,
               0xEE, 0xFF}));
    OTTER_CHECK_EQ(upper(text), std::string("00112233-4455-6677-8899-AABBCCDDEEFF"));
    OTTER_CHECK_EQ(upper(format_binary(bytes({0xCA, 0xFE, 0x01}))), std::string("0XCAFE01"));
}

OTTER_TEST(tds_text_round_trips_through_utf16) {
    using namespace otter::tdswire;
    // Acento, ideograma e um caractere fora do plano basico (par substituto).
    const std::string text = "ação 日本 😀";
    const std::string wide = utf8_to_utf16le(text);
    OTTER_CHECK_EQ(wide.size(), std::size_t{2} * (4 + 1 + 2 + 1 + 2));
    const std::span<const std::byte> view(reinterpret_cast<const std::byte*>(wide.data()),
                                          wide.size());
    OTTER_CHECK_EQ(utf16le_to_utf8(view), text);
}

// --- tdswire: pacotes ---------------------------------------------------------------

OTTER_TEST(tds_prelogin_carries_the_encryption_choice) {
    using namespace otter::tdswire;
    for (const std::uint8_t wanted : {std::uint8_t{0x00}, std::uint8_t{0x01}, std::uint8_t{0x02}}) {
        const std::vector<std::byte> body = build_prelogin(wanted);
        OTTER_CHECK_EQ(static_cast<int>(parse_prelogin_encryption(body)),
                       static_cast<int>(wanted));
    }
    // Resposta truncada: nao inventa um valor.
    OTTER_CHECK_EQ(static_cast<int>(parse_prelogin_encryption(bytes({0x01}))), 0xFF);
}

OTTER_TEST(tds_login_never_carries_the_password_in_clear) {
    using namespace otter::tdswire;
    ConnectParams params;
    params.host     = "db.example";
    params.user     = "ana";
    params.password = "s3gr3d0";
    params.database = "loja";
    const std::vector<std::byte> body = build_login7(params, "estacao", {});

    // O primeiro campo e' o tamanho do proprio pacote, e a versao e' 7.4.
    const std::size_t declared = static_cast<std::size_t>(body[0]) |
                                 (static_cast<std::size_t>(body[1]) << 8) |
                                 (static_cast<std::size_t>(body[2]) << 16) |
                                 (static_cast<std::size_t>(body[3]) << 24);
    OTTER_CHECK_EQ(declared, body.size());
    OTTER_CHECK_EQ(static_cast<int>(body[7]), 0x74);

    OTTER_CHECK(contains_bytes(body, utf8_to_utf16le("ana")));
    OTTER_CHECK(contains_bytes(body, utf8_to_utf16le("loja")));
    OTTER_CHECK(contains_bytes(body, utf8_to_utf16le("estacao")));
    // A senha so' aparece embaralhada, como o protocolo define.
    OTTER_CHECK(!contains_bytes(body, utf8_to_utf16le("s3gr3d0")));
    OTTER_CHECK(contains_bytes(body, obfuscate_password(utf8_to_utf16le("s3gr3d0"))));

    // 'a' = 0x61 0x00: nibbles trocados (0x16, 0x00) e XOR 0xA5.
    const std::string scrambled = obfuscate_password(utf8_to_utf16le("a"));
    OTTER_CHECK_EQ(scrambled.size(), std::size_t{2});
    OTTER_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(scrambled[0])), 0xB3);
    OTTER_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(scrambled[1])), 0xA5);
}

OTTER_TEST(tds_batch_starts_with_the_transaction_descriptor) {
    using namespace otter::tdswire;
    const std::vector<std::byte> body = build_batch("SELECT 1", 0x1122334455667788ULL);

    // ALL_HEADERS: tamanho total 22, um cabecalho de 18 bytes do tipo 2.
    OTTER_CHECK_EQ(static_cast<int>(body[0]), 22);
    OTTER_CHECK_EQ(static_cast<int>(body[4]), 18);
    OTTER_CHECK_EQ(static_cast<int>(body[8]), 2);
    // O descritor da transacao, em little-endian, e 1 pedido em curso.
    OTTER_CHECK_EQ(static_cast<int>(body[10]), 0x88);
    OTTER_CHECK_EQ(static_cast<int>(body[17]), 0x11);
    OTTER_CHECK_EQ(static_cast<int>(body[18]), 1);
    // Depois, o texto em UTF-16LE.
    OTTER_CHECK_EQ(body.size(), std::size_t{22 + 16});
    OTTER_CHECK_EQ(static_cast<int>(body[22]), static_cast<int>('S'));
    OTTER_CHECK_EQ(static_cast<int>(body[23]), 0);
}

// --- Catalogo -----------------------------------------------------------------------

OTTER_TEST(mssql_type_text_shows_characters_not_bytes) {
    // nvarchar guarda dois bytes por caractere: max_length 200 e' nvarchar(100).
    OTTER_CHECK_EQ(mssql_type_text("nvarchar", 200, 0, 0), std::string("nvarchar(100)"));
    OTTER_CHECK_EQ(mssql_type_text("varchar", 50, 0, 0), std::string("varchar(50)"));
    OTTER_CHECK_EQ(mssql_type_text("nvarchar", -1, 0, 0), std::string("nvarchar(max)"));
    OTTER_CHECK_EQ(mssql_type_text("decimal", 9, 12, 2), std::string("decimal(12,2)"));
    OTTER_CHECK_EQ(mssql_type_text("int", 4, 10, 0), std::string("int"));
    // 7 e' a escala padrao: so' a diferente aparece.
    OTTER_CHECK_EQ(mssql_type_text("datetime2", 8, 27, 7), std::string("datetime2"));
    OTTER_CHECK_EQ(mssql_type_text("datetime2", 7, 23, 3), std::string("datetime2(3)"));
}

// --- db/mssql_object ----------------------------------------------------------------

OTTER_TEST(mssql_names_use_brackets_and_double_the_closing_one) {
    OTTER_CHECK_EQ(mssql_quote("a]b"), std::string("[a]]b]"));
    OTTER_CHECK_EQ(mssql_literal("d'agua"), std::string("N'd''agua'"));
    OTTER_CHECK_EQ(mssql_object_name(ref_of(ObjectType::table, "dbo", "Nota Fiscal")),
                   std::string("[dbo].[Nota Fiscal]"));
    OTTER_CHECK_EQ(mssql_object_name(ref_of(ObjectType::column, "dbo", "valor", "nota")),
                   std::string("[dbo].[nota].[valor]"));
    OTTER_CHECK_EQ(mssql_object_name(ref_of(ObjectType::database, "", "loja")),
                   std::string("[loja]"));
}

OTTER_TEST(mssql_rename_goes_through_sp_rename) {
    const AlterScript table = mssql_object_rename(ref_of(ObjectType::table, "dbo", "item"), "produto");
    OTTER_CHECK(table.ok());
    OTTER_CHECK_EQ(table.statements.front(),
                   std::string("EXEC sp_rename N'[dbo].[item]', N'produto', N'OBJECT'"));

    const AlterScript column =
        mssql_object_rename(ref_of(ObjectType::column, "dbo", "nome", "item"), "descricao");
    OTTER_CHECK(contains(column.statements.front(), "N'[dbo].[item].[nome]'"));
    OTTER_CHECK(contains(column.statements.front(), "N'COLUMN'"));

    OTTER_CHECK_EQ(mssql_object_rename(ref_of(ObjectType::database, "", "a"), "b").statements.front(),
                   std::string("ALTER DATABASE [a] MODIFY NAME = [b]"));
    // Schema nao se renomeia no SQL Server: recusa com o motivo.
    OTTER_CHECK(!mssql_object_rename(ref_of(ObjectType::schema, "", "a"), "b").ok());
    OTTER_CHECK(!mssql_object_rename(ref_of(ObjectType::table, "dbo", "a"), "a").ok());

    // Uma view renomeada continua com o nome antigo no texto guardado.
    OTTER_CHECK(!mssql_object_rename(ref_of(ObjectType::view, "dbo", "v"), "w").warnings.empty());
}

OTTER_TEST(mssql_comment_is_the_ms_description_property) {
    const AlterScript set = mssql_object_comment(ref_of(ObjectType::table, "dbo", "item"), "d'agua");
    OTTER_CHECK(set.ok());
    // Um lote so': troca se ja' existe, acrescenta se nao.
    OTTER_CHECK(contains(set.statements.front(), "sp_updateextendedproperty"));
    OTTER_CHECK(contains(set.statements.front(), "ELSE EXEC sys.sp_addextendedproperty"));
    OTTER_CHECK(contains(set.statements.front(), "@value = N'd''agua'"));

    const AlterScript clear = mssql_object_comment(ref_of(ObjectType::table, "dbo", "item"), "");
    OTTER_CHECK(contains(clear.statements.front(), "sp_dropextendedproperty"));

    const AlterScript column =
        mssql_object_comment(ref_of(ObjectType::column, "dbo", "nome", "item"), "x");
    OTTER_CHECK(contains(column.statements.front(), "@level2type = N'COLUMN'"));
    OTTER_CHECK(contains(column.statements.front(), "@level1name = N'item'"));
}

OTTER_TEST(mssql_drop_grant_and_revoke) {
    AlterScript drop = mssql_object_drop(ref_of(ObjectType::index, "dbo", "ix_nome", "item"));
    OTTER_CHECK_EQ(drop.statements.front(), std::string("DROP INDEX [ix_nome] ON [dbo].[item]"));
    OTTER_CHECK(drop.has_destructive());
    OTTER_CHECK_EQ(mssql_object_drop(ref_of(ObjectType::role, "", "ana")).statements.front(),
                   std::string("DROP LOGIN [ana]"));

    const ObjectRef table = ref_of(ObjectType::table, "dbo", "item");
    OTTER_CHECK_EQ(mssql_grant(table, "select", "ana", true).statements.front(),
                   std::string("GRANT SELECT ON OBJECT::[dbo].[item] TO [ana] WITH GRANT OPTION"));
    OTTER_CHECK_EQ(mssql_revoke(table, "SELECT", "ana").statements.front(),
                   std::string("REVOKE SELECT ON OBJECT::[dbo].[item] FROM [ana] CASCADE"));
    OTTER_CHECK_EQ(mssql_grant(ref_of(ObjectType::schema, "", "vendas"), "SELECT", "ana", false)
                       .statements.front(),
                   std::string("GRANT SELECT ON SCHEMA::[vendas] TO [ana]"));
    // O privilegio vai sem aspas: so' letras e espaco passam.
    OTTER_CHECK(!mssql_grant(table, "SELECT; DROP TABLE x", "ana", false).ok());
    OTTER_CHECK(!mssql_grant(table, "SELECT", "", false).ok());
}

OTTER_TEST(mssql_source_is_saved_with_alter) {
    const ObjectRef view = ref_of(ObjectType::view, "dbo", "v");
    // CREATE vira ALTER -- depois dos comentarios do comeco, sem tocar neles.
    const AlterScript script =
        mssql_source_script(view, "-- relatorio\n/* v2 */ CREATE VIEW dbo.v AS SELECT 1 AS n\n");
    OTTER_CHECK(script.ok());
    OTTER_CHECK_EQ(script.statements.front(),
                   std::string("-- relatorio\n/* v2 */ ALTER VIEW dbo.v AS SELECT 1 AS n"));

    // Ja' e' ALTER (ou CREATE OR ALTER): vai como esta'.
    OTTER_CHECK_EQ(mssql_source_script(view, "CREATE OR ALTER VIEW dbo.v AS SELECT 1").statements.front(),
                   std::string("CREATE OR ALTER VIEW dbo.v AS SELECT 1"));
    // "CREATEX" nao e' CREATE.
    OTTER_CHECK(!mssql_source_script(view, "CREATEVIEW dbo.v AS SELECT 1").ok());
    OTTER_CHECK(!mssql_source_script(view, "SELECT 1").ok());
    OTTER_CHECK(!mssql_source_script(view, "   ").ok());
    OTTER_CHECK(!mssql_source_script(ref_of(ObjectType::table, "dbo", "t"), "CREATE TABLE t (a int)").ok());
}

OTTER_TEST(mssql_create_login_database_and_tools) {
    MssqlNewLogin login;
    login.name             = "ana";
    login.password         = "se'nha";
    login.default_database = "loja";
    OTTER_CHECK_EQ(mssql_create_login(login).statements.front(),
                   std::string("CREATE LOGIN [ana] WITH PASSWORD = N'se''nha', "
                               "DEFAULT_DATABASE = [loja]"));
    login.password.clear();
    OTTER_CHECK(!mssql_create_login(login).ok());

    OTTER_CHECK_EQ(mssql_create_database("loja", "Latin1_General_CI_AS").statements.front(),
                   std::string("CREATE DATABASE [loja] COLLATE Latin1_General_CI_AS"));
    // O collation vai sem aspas: so' um nome simples passa.
    OTTER_CHECK(!mssql_create_database("loja", "x; DROP DATABASE y").ok());

    OTTER_CHECK_EQ(mssql_table_tool(MssqlTableTool::rebuild_indexes, "dbo", "item").statements.front(),
                   std::string("ALTER INDEX ALL ON [dbo].[item] REBUILD"));
    OTTER_CHECK(contains(mssql_table_tool(MssqlTableTool::check, "dbo", "item").statements.front(),
                         "DBCC CHECKTABLE (N'[dbo].[item]')"));

    MssqlBackupOptions backup;
    backup.file = "C:\\b\\loja.bak";
    OTTER_CHECK_EQ(mssql_backup_database("loja", backup).statements.front(),
                   std::string("BACKUP DATABASE [loja] TO DISK = N'C:\\b\\loja.bak' "
                               "WITH COPY_ONLY, NOINIT"));
    OTTER_CHECK(mssql_restore_database("loja", "C:\\b\\loja.bak", true).has_destructive());

    // O id da sessao vem de uma celula da grade: so' digitos.
    OTTER_CHECK_EQ(mssql_session_kill("57").statements.front(), std::string("KILL 57"));
    OTTER_CHECK(!mssql_session_kill("57; DROP").ok());
    OTTER_CHECK(!mssql_session_kill("").ok());
}

// --- db/alter em T-SQL ----------------------------------------------------------------

OTTER_TEST(tsql_alter_adds_without_the_column_keyword) {
    const MssqlDialect dialect;
    TableAlteration wanted;
    wanted.schema = "dbo";
    wanted.table  = "cliente";
    NewColumn email;
    email.name = "email"; email.type_name = "varchar(120)"; email.comment = "contato";
    wanted.add_columns = {email};

    const AlterScript script = generate_alter(sample_table(), wanted);
    OTTER_CHECK(script.ok());
    // "ADD COLUMN" e' erro de sintaxe no T-SQL; e a nulidade vai explicita.
    OTTER_CHECK_EQ(script.statements[0],
                   std::string("ALTER TABLE dbo.cliente ADD email varchar(120) NULL"));
    OTTER_CHECK(contains(script.statements[1], "sp_addextendedproperty"));
}

OTTER_TEST(tsql_alter_column_repeats_type_and_nullability) {
    const MssqlDialect dialect;
    TableAlteration wanted;
    wanted.schema = "dbo";
    wanted.table  = "cliente";
    ColumnChange change;
    change.name      = "nome";
    change.type_name = "nvarchar(200)";   // so' o tipo mudou
    wanted.alter_columns = {change};

    const AlterScript script = generate_alter(sample_table(), wanted);
    OTTER_CHECK(script.ok());
    // Sem o NOT NULL a coluna voltaria ao padrao da sessao -- anulavel.
    OTTER_CHECK_EQ(script.statements.front(),
                   std::string("ALTER TABLE dbo.cliente ALTER COLUMN nome nvarchar(200) NOT NULL"));
}

OTTER_TEST(tsql_default_is_a_constraint_found_by_name) {
    const MssqlDialect dialect;
    TableAlteration wanted;
    wanted.schema = "dbo";
    wanted.table  = "cliente";
    ColumnChange change;
    change.name          = "ativo";
    change.default_value = "0";
    wanted.alter_columns = {change};

    const AlterScript script = generate_alter(sample_table(), wanted);
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{2});
    // Primeiro tira a constraint que existe, descobrindo o nome no catalogo.
    OTTER_CHECK(contains(script.statements[0], "sys.default_constraints"));
    OTTER_CHECK(contains(script.statements[0], "EXEC(@sql)"));
    OTTER_CHECK_EQ(script.statements[1],
                   std::string("ALTER TABLE dbo.cliente ADD DEFAULT 0 FOR ativo"));

    // IDENTITY vem do catalogo no campo do default, mas nao e' constraint:
    // trocar o "default" dela nao pode tentar remover uma.
    ColumnChange identity;
    identity.name          = "id";
    identity.default_value = "5";
    wanted.alter_columns = {identity};
    OTTER_CHECK_EQ(generate_alter(sample_table(), wanted).statements.size(), std::size_t{1});
}

OTTER_TEST(tsql_drop_column_removes_its_default_first) {
    const MssqlDialect dialect;
    TableAlteration wanted;
    wanted.schema       = "dbo";
    wanted.table        = "cliente";
    wanted.drop_columns = {"ativo"};

    const AlterScript script = generate_alter(sample_table(), wanted);
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{2});
    OTTER_CHECK(contains(script.statements[0], "DROP CONSTRAINT"));
    OTTER_CHECK_EQ(script.statements[1], std::string("ALTER TABLE dbo.cliente DROP COLUMN ativo"));
    // Destrutivo e' o DROP COLUMN -- o segundo comando, nao o primeiro.
    OTTER_CHECK_EQ(script.destructive.size(), std::size_t{1});
    OTTER_CHECK_EQ(script.destructive.front(), std::size_t{1});
}

OTTER_TEST(tsql_index_view_trigger_and_foreign_key) {
    const MssqlDialect dialect;

    NewIndex index;
    index.name         = "ix_nome";
    index.columns      = {"nome"};
    index.unique       = true;
    index.concurrently = true;
    const AlterScript created = generate_create_index("dbo", "cliente", index);
    OTTER_CHECK_EQ(created.statements.front(),
                   std::string("CREATE UNIQUE INDEX ix_nome ON dbo.cliente (nome)"));
    OTTER_CHECK(!created.warnings.empty());   // CONCURRENTLY nao existe aqui

    OTTER_CHECK_EQ(generate_drop_index("dbo", "cliente", "ix_nome", false).statements.front(),
                   std::string("DROP INDEX ix_nome ON dbo.cliente"));

    OTTER_CHECK(generate_create_view("dbo", "v", "SELECT 1 AS n", true)
                    .statements.front()
                    .starts_with("CREATE OR ALTER VIEW dbo.v AS"));

    NewForeignKey key;
    key.columns        = {"cliente_id"};
    key.target_table   = "cliente";
    key.target_columns = {"id"};
    key.on_delete      = "RESTRICT";
    const std::string fk = generate_add_foreign_key("dbo", "pedido", key).statements.front();
    OTTER_CHECK(contains(fk, "ON DELETE NO ACTION"));
    OTTER_CHECK(!contains(fk, "RESTRICT"));

    NewTrigger trigger;
    trigger.name = "trg"; trigger.table = "pedido"; trigger.timing = "BEFORE";
    trigger.event = "INSERT"; trigger.body = "BEGIN SET NOCOUNT ON; END";
    OTTER_CHECK(!generate_create_trigger("dbo", trigger).ok());
    trigger.timing = "AFTER";
    OTTER_CHECK(contains(generate_create_trigger("dbo", trigger).statements.front(),
                         "ON dbo.pedido\nAFTER INSERT\nAS\n"));

    // CASCADE nao vai para o comando: o T-SQL nem aceita a palavra.
    const AlterScript drop = generate_drop("dbo", "pedido", ObjKind::table, /*cascade=*/true);
    OTTER_CHECK_EQ(drop.statements.front(), std::string("DROP TABLE dbo.pedido"));
    OTTER_CHECK(!drop.warnings.empty());
}

OTTER_TEST(tsql_transaction_and_generic_generators_follow_the_dialect) {
    OTTER_CHECK_EQ(transaction_begin_sql(), std::string_view("BEGIN"));
    {
        const MssqlDialect dialect;
        // "BEGIN" sozinho abre um BLOCO no T-SQL.
        OTTER_CHECK_EQ(transaction_begin_sql(), std::string_view("BEGIN TRANSACTION"));
        OTTER_CHECK_EQ(transaction_commit_sql(), std::string_view("COMMIT TRANSACTION"));

        const ObjectRef table = ref_of(ObjectType::table, "dbo", "item");
        OTTER_CHECK(contains(generate_object_rename(table, "produto").statements.front(),
                             "sp_rename"));
        OTTER_CHECK_EQ(generate_object_drop(table, /*cascade=*/true).statements.front(),
                       std::string("DROP TABLE [dbo].[item]"));
        OTTER_CHECK_EQ(object_sql_name(table), std::string("[dbo].[item]"));

        TableMeta meta;
        meta.name = "item";
        OTTER_CHECK(generate_select("dbo", meta, 50).starts_with("-- expand the table"));
        OTTER_CHECK(contains(generate_select("dbo", meta, 50), "SELECT TOP (50) *"));
        OTTER_CHECK(!contains(generate_select("dbo", meta, 50), "LIMIT"));
    }
    OTTER_CHECK_EQ(transaction_commit_sql(), std::string_view("COMMIT"));
}

// --- Literais -----------------------------------------------------------------------

OTTER_TEST(tsql_literals_keep_unicode_and_use_bit_values) {
    // Fora do dialeto do SQL Server nada muda.
    OTTER_CHECK_EQ(quote_literal("ação"), std::string("'ação'"));

    const MssqlDialect dialect;
    // Sem N o texto e' convertido para a pagina de codigo do banco.
    OTTER_CHECK_EQ(quote_literal("ação"), std::string("N'ação'"));
    // So' ASCII: sem N, para a comparacao com varchar continuar usando o indice.
    OTTER_CHECK_EQ(quote_literal("d'agua"), std::string("'d''agua'"));
    // A barra invertida nao e' escape aqui.
    OTTER_CHECK_EQ(quote_literal("a\\b"), std::string("'a\\b'"));

    ColumnInfo bit;
    bit.kind = DataKind::boolean;
    OTTER_CHECK_EQ(sql_literal(bit, "true", false), std::string("1"));
    OTTER_CHECK_EQ(sql_literal(bit, "1", false), std::string("1"));
    OTTER_CHECK_EQ(sql_literal(bit, "false", false), std::string("0"));
    OTTER_CHECK_EQ(sql_literal(bit, "0", false), std::string("0"));

    ColumnInfo binary;
    binary.kind = DataKind::binary;
    OTTER_CHECK_EQ(sql_literal(binary, "0xCAFE", false), std::string("0xCAFE"));
    // Nao e' hexadecimal: vai citado, e quem recusa e' o servidor.
    OTTER_CHECK_EQ(sql_literal(binary, "0xZZ; DROP", false), std::string("'0xZZ; DROP'"));

    ColumnInfo text;
    text.kind = DataKind::string;
    OTTER_CHECK_EQ(sql_literal(text, "日本", false), std::string("N'日本'"));
    OTTER_CHECK_EQ(sql_literal(text, "x", true), std::string("NULL"));
}

OTTER_TEST(boolean_literal_outside_sql_server_is_unchanged) {
    ColumnInfo flag;
    flag.kind = DataKind::boolean;
    OTTER_CHECK_EQ(sql_literal(flag, "t", false), std::string("TRUE"));
    OTTER_CHECK_EQ(sql_literal(flag, "false", false), std::string("FALSE"));
}

// --- Paginacao ----------------------------------------------------------------------

OTTER_TEST(tsql_paging_uses_offset_fetch_after_an_order_by) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");

    // Sem ORDER BY: OFFSET/FETCH exige um, e "(SELECT NULL)" e' "na ordem que vier".
    sql::PagedQuery q = sql::make_paged_query("SELECT * FROM t", dialect, 0, 200);
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT * FROM t\nORDER BY (SELECT NULL)\n"
                                      "OFFSET 0 ROWS FETCH NEXT 201 ROWS ONLY"));

    // Com ORDER BY do usuario: so' o OFFSET, depois dele -- e sem o ';' do fim.
    q = sql::make_paged_query("SELECT * FROM t ORDER BY id;", dialect, 2, 100);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT * FROM t ORDER BY id\n"
                                      "OFFSET 200 ROWS FETCH NEXT 101 ROWS ONLY"));
    OTTER_CHECK(!contains(q.sql, "LIMIT"));

    // DISTINCT e UNION so' aceitam ORDER BY do que esta' na lista: a posicao.
    q = sql::make_paged_query("SELECT DISTINCT a FROM t", dialect, 0, 10);
    OTTER_CHECK(contains(q.sql, "\nORDER BY 1\nOFFSET 0 ROWS"));
    q = sql::make_paged_query("SELECT a FROM t UNION SELECT a FROM u", dialect, 0, 10);
    OTTER_CHECK(contains(q.sql, "\nORDER BY 1\nOFFSET 0 ROWS"));
}

OTTER_TEST(tsql_paging_quotes_with_brackets_and_wraps_the_filter) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");

    sql::SortOrder sort;
    sort.column = "Nome ]x";
    sql::PagedQuery q = sql::make_paged_query("SELECT * FROM t", dialect, 0, 10, sort);
    OTTER_CHECK(contains(q.sql, "ORDER BY [Nome ]]x] ASC\nOFFSET 0 ROWS"));

    // Filtro sobre consulta com ORDER BY: a subconsulta so' aceita ORDER BY
    // acompanhado de OFFSET.
    sql::ColumnFilter filter;
    filter.set_column("total", "> 100");
    q = sql::make_paged_query("SELECT * FROM t ORDER BY id", dialect, 0, 10, {}, filter);
    OTTER_CHECK(contains(q.sql, "ORDER BY id\nOFFSET 0 ROWS\n) AS otter_filter"));
    OTTER_CHECK(contains(q.sql, "WHERE [total] > 100"));
    OTTER_CHECK(contains(q.sql, "ORDER BY (SELECT NULL)\nOFFSET 0 ROWS FETCH NEXT 11 ROWS ONLY"));

    // Um WITH nao cabe em subconsulta: com filtro, recusa em vez de mandar
    // um SQL que o servidor devolve com erro.
    q = sql::make_paged_query("WITH x AS (SELECT 1 AS n) SELECT n FROM x", dialect, 0, 10, {},
                              filter);
    OTTER_CHECK(!q.rewritten);
    OTTER_CHECK(q.refusal == sql::PagingRefusal::unsupported_form);
    // Sem filtro o WITH pagina normalmente.
    OTTER_CHECK(sql::make_paged_query("WITH x AS (SELECT 1 AS n) SELECT n FROM x", dialect, 0, 10)
                    .rewritten);
}

OTTER_TEST(tsql_paging_refuses_what_cannot_take_an_offset) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");
    const auto refusal = [&dialect](const char* text) {
        return sql::make_paged_query(text, dialect, 0, 10).refusal;
    };

    OTTER_CHECK(refusal("SELECT TOP 5 * FROM t") == sql::PagingRefusal::already_limited);
    OTTER_CHECK(refusal("SELECT TOP (5) * FROM t ORDER BY a") == sql::PagingRefusal::already_limited);
    OTTER_CHECK(refusal("SELECT * FROM t ORDER BY a OFFSET 5 ROWS") ==
                sql::PagingRefusal::already_limited);
    OTTER_CHECK(refusal("SELECT * INTO #tmp FROM t") == sql::PagingRefusal::not_a_query);
    OTTER_CHECK(refusal("SELECT * FROM t FOR XML AUTO") == sql::PagingRefusal::unsupported_form);
    OTTER_CHECK(refusal("SELECT * FROM t OPTION (RECOMPILE)") ==
                sql::PagingRefusal::unsupported_form);
    OTTER_CHECK(refusal("EXEC sp_who") == sql::PagingRefusal::not_a_query);
    // TOP DENTRO de uma subconsulta nao limita a externa.
    OTTER_CHECK(refusal("SELECT * FROM (SELECT TOP 5 * FROM t) s") == sql::PagingRefusal::none);

    // No PostgreSQL "top" e "into" podem ser nomes: nada disso vale la'.
    const sql::Dialect& postgres = sql::dialect_for("postgresql");
    OTTER_CHECK(sql::make_paged_query("SELECT top FROM t", postgres, 0, 10).rewritten);
}

OTTER_TEST(tsql_count_query_keeps_an_inner_order_by_valid) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");

    sql::PagedQuery q = sql::make_count_query("SELECT * FROM t ORDER BY id", dialect, {});
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK_EQ(q.sql, std::string("SELECT COUNT(*) FROM (\nSELECT * FROM t ORDER BY id\n"
                                      "OFFSET 0 ROWS\n) AS otter_count"));

    // Com TOP o ORDER BY ja' e' valido na subconsulta: nada a acrescentar.
    q = sql::make_count_query("SELECT TOP 7 * FROM t ORDER BY id", dialect, {});
    OTTER_CHECK(q.rewritten);
    OTTER_CHECK(!contains(q.sql, "OFFSET"));

    q = sql::make_count_query("WITH x AS (SELECT 1 AS n) SELECT n FROM x", dialect, {});
    OTTER_CHECK(!q.rewritten);
}

OTTER_TEST(mysql_sort_and_filter_are_quoted_with_backticks) {
    // "nome" entre aspas duplas e' um TEXTO para o MySQL: ORDER BY "nome"
    // ordenava por uma constante, isto e', nao ordenava.
    const sql::Dialect& dialect = sql::dialect_for("mysql");
    sql::SortOrder sort;
    sort.column = "nome";
    sql::ColumnFilter filter;
    filter.set_column("total", "> 1");
    const sql::PagedQuery q = sql::make_paged_query("SELECT * FROM t", dialect, 0, 10, sort, filter);
    OTTER_CHECK(contains(q.sql, "ORDER BY `nome` ASC"));
    OTTER_CHECK(contains(q.sql, "WHERE `total` > 1"));
    OTTER_CHECK(contains(q.sql, "LIMIT 11"));
}

// --- Grade, importacao, rotinas ---------------------------------------------------------

OTTER_TEST(tsql_distinct_query_uses_top_and_groups_by_name) {
    const MssqlDialect dialect;
    const std::string query = distinct_query("SELECT * FROM t;", "Nome", 50);
    OTTER_CHECK(query.starts_with("SELECT TOP (50) [Nome], COUNT(*)"));
    // GROUP BY 1 nao existe no T-SQL.
    OTTER_CHECK(contains(query, "GROUP BY [Nome]"));
    OTTER_CHECK(!contains(query, "LIMIT"));
    OTTER_CHECK(!contains(query, ";"));
}

OTTER_TEST(tsql_import_never_exceeds_a_thousand_rows_per_insert) {
    const MssqlDialect dialect;
    std::string csv = "a\n";
    for (int i = 0; i < 2100; ++i) csv += std::to_string(i) + "\n";

    ImportPlan plan;
    plan.schema     = "dbo";
    plan.table      = "t";
    plan.columns    = {"a"};
    plan.batch_rows = 5000;
    const ImportScript script = generate_import(parse_csv(csv, CsvOptions{}), plan);
    OTTER_CHECK(script.error.empty());
    OTTER_CHECK_EQ(script.rows, std::size_t{2100});
    // 1000 + 1000 + 100: o servidor recusa um VALUES maior.
    OTTER_CHECK_EQ(script.statements.size(), std::size_t{3});
}

OTTER_TEST(tsql_routine_call_declares_output_parameters) {
    const MssqlDialect dialect;

    RoutineMeta procedure;
    procedure.name      = "p_total";
    procedure.kind      = ObjKind::procedure;
    procedure.arguments = "@cliente int, @total decimal OUTPUT, @nome nvarchar OUTPUT";
    OTTER_CHECK_EQ(routine_call_sql("vendas", procedure, false),
                   std::string("DECLARE @total decimal;\nDECLARE @nome nvarchar(max);\n"
                               "EXEC [vendas].[p_total] @cliente = :cliente, "
                               "@total = @total OUTPUT, @nome = @nome OUTPUT;\n"
                               "SELECT @total AS [total], @nome AS [nome];"));

    RoutineMeta scalar;
    scalar.name        = "f_dobro";
    scalar.kind        = ObjKind::function;
    scalar.arguments   = "@n int";
    scalar.return_type = "int";
    OTTER_CHECK_EQ(routine_call_sql("dbo", scalar, false), std::string("SELECT [dbo].[f_dobro](:n);"));

    scalar.return_type = "TABLE";
    OTTER_CHECK_EQ(routine_call_sql("dbo", scalar, false),
                   std::string("SELECT * FROM [dbo].[f_dobro](:n);"));

    RoutineMeta bare;
    bare.name = "p_limpa";
    bare.kind = ObjKind::procedure;
    OTTER_CHECK_EQ(routine_call_sql("dbo", bare, false), std::string("EXEC [dbo].[p_limpa];"));
}

// --- Lotes (GO) ------------------------------------------------------------------------

OTTER_TEST(tsql_script_splits_on_go_lines) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");
    const std::string script =
        "CREATE TABLE t (a int);\nGO\n"
        "INSERT INTO t VALUES (1); INSERT INTO t VALUES (2)\ngo 2\n"
        "SELECT a AS go FROM t\n";
    const auto ranges = sql::statement_ranges(script, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{4});
    const auto text = [&](std::size_t i) {
        return script.substr(ranges[i].begin, ranges[i].end - ranges[i].begin);
    };
    OTTER_CHECK_EQ(text(0), std::string("CREATE TABLE t (a int)"));
    OTTER_CHECK_EQ(text(1), std::string("INSERT INTO t VALUES (1)"));
    OTTER_CHECK_EQ(text(2), std::string("INSERT INTO t VALUES (2)"));
    // "go" no MEIO de uma linha e' um nome, nao o separador.
    OTTER_CHECK_EQ(text(3), std::string("SELECT a AS go FROM t"));
}

OTTER_TEST(tsql_batch_with_variables_is_not_split) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");
    // A variavel so' existe dentro do lote: partir no ';' (ou na linha em
    // branco) faria o SELECT falhar com "Must declare the scalar variable".
    const std::string script =
        "DECLARE @n int;\nSET @n = 5;\n\nSELECT @n AS n;\nGO\nSELECT 1;\nSELECT 2;";
    const auto ranges = sql::statement_ranges(script, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{3});
    OTTER_CHECK_EQ(script.substr(ranges[0].begin, ranges[0].end - ranges[0].begin),
                   std::string("DECLARE @n int;\nSET @n = 5;\n\nSELECT @n AS n;"));

    // O cursor em qualquer linha do lote executa o lote inteiro.
    const auto at = sql::statement_at(script, dialect, script.find("SET @n"));
    OTTER_CHECK(at.has_value());
    OTTER_CHECK(at->atomic);
    OTTER_CHECK(at->text.find("SELECT @n") != std::string_view::npos);
}

OTTER_TEST(tsql_routine_body_runs_to_the_end_of_the_batch) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");
    // Sem BEGIN ... END o corpo vai ate' o GO: os ';' de dentro nao encerram.
    const std::string script =
        "CREATE OR ALTER PROCEDURE dbo.p AS\n  SET NOCOUNT ON;\n\n  SELECT 1;\n  SELECT 2;\nGO\n"
        "EXEC dbo.p;";
    const auto ranges = sql::statement_ranges(script, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});
    OTTER_CHECK(script.substr(ranges[0].begin, ranges[0].end - ranges[0].begin)
                    .ends_with("SELECT 2;"));
    OTTER_CHECK_EQ(script.substr(ranges[1].begin, ranges[1].end - ranges[1].begin),
                   std::string("EXEC dbo.p"));
}

OTTER_TEST(tsql_else_stays_with_its_if_and_transactions_are_not_blocks) {
    const sql::Dialect& dialect = sql::dialect_for("sqlserver");
    const std::string script = "IF 1 = 1 SELECT 1; ELSE SELECT 2; SELECT 3;";
    auto ranges = sql::statement_ranges(script, dialect);
    OTTER_CHECK_EQ(ranges.size(), std::size_t{2});
    OTTER_CHECK_EQ(script.substr(ranges[0].begin, ranges[0].end - ranges[0].begin),
                   std::string("IF 1 = 1 SELECT 1; ELSE SELECT 2"));

    // BEGIN TRANSACTION nao abre bloco: o ';' depois dele separa.
    const std::string txn = "BEGIN TRANSACTION; UPDATE t SET a = 1; COMMIT;";
    OTTER_CHECK_EQ(sql::statement_ranges(txn, dialect).size(), std::size_t{3});

    // BEGIN ... END abre: os ';' de dentro nao.
    const std::string block = "IF 1 = 1 BEGIN SELECT 1; SELECT 2; END; SELECT 3;";
    OTTER_CHECK_EQ(sql::statement_ranges(block, dialect).size(), std::size_t{2});

    // Nos outros SGBDs "GO" nao significa nada.
    OTTER_CHECK_EQ(sql::statement_ranges("SELECT 1\nGO\nSELECT 2", sql::dialect_for("postgresql")).size(),
                   std::size_t{1});
}

// --- SQL Server Browser (instancia nomeada) ---------------------------------------------

OTTER_TEST(browser_request_is_the_instance_name_after_0x04) {
    using namespace otter::tdswire;
    // CLNT_UCAST_INST: 0x04, o nome e um zero no fim.
    const std::vector<std::byte> request = build_browser_request("SITTAX");
    OTTER_CHECK_EQ(request.size(), std::size_t{8});
    OTTER_CHECK_EQ(static_cast<int>(request[0]), 0x04);
    OTTER_CHECK_EQ(static_cast<int>(request[1]), static_cast<int>('S'));
    OTTER_CHECK_EQ(static_cast<int>(request[6]), static_cast<int>('X'));
    OTTER_CHECK_EQ(static_cast<int>(request[7]), 0x00);
}

OTTER_TEST(browser_response_gives_the_tcp_port) {
    using namespace otter::tdswire;
    const auto response = [](std::string_view text) {
        std::vector<std::byte> out;
        out.push_back(std::byte{0x05});
        out.push_back(static_cast<std::byte>(text.size() & 0xFF));
        out.push_back(static_cast<std::byte>(text.size() >> 8));
        for (const char c : text) out.push_back(static_cast<std::byte>(c));
        return out;
    };

    const auto port = parse_browser_response(response(
        "ServerName;HERMESWS;InstanceName;SITTAX;IsClustered;No;Version;15.0.2000.5;"
        "tcp;51433;np;\\\\HERMESWS\\pipe\\MSSQL$SITTAX\\sql\\query;;"));
    OTTER_CHECK(port.has_value());
    OTTER_CHECK_EQ(static_cast<int>(*port), 51433);

    // Uma instancia CHAMADA "tcp": o nome e' valor, nao o par da porta.
    const auto tricky = parse_browser_response(response(
        "ServerName;S;InstanceName;tcp;IsClustered;No;Version;16.0.1000.6;tcp;1500;;"));
    OTTER_CHECK(tricky.has_value());
    OTTER_CHECK_EQ(static_cast<int>(*tricky), 1500);

    // So' named pipes: nao ha' porta para conectar.
    OTTER_CHECK(!parse_browser_response(response(
        "ServerName;S;InstanceName;X;IsClustered;No;Version;16.0.1000.6;np;\\\\S\\pipe\\x;;"))
                     .has_value());
    // Porta que nao e' numero, resposta de outro tipo, resposta cortada.
    OTTER_CHECK(!parse_browser_response(response("ServerName;S;InstanceName;X;tcp;abc;;"))
                     .has_value());
    OTTER_CHECK(!parse_browser_response(response("ServerName;S;InstanceName;X;tcp;70000;;"))
                     .has_value());
    std::vector<std::byte> wrong = response("tcp;1433;;");
    wrong[0] = std::byte{0x04};
    OTTER_CHECK(!parse_browser_response(wrong).has_value());
    OTTER_CHECK(!parse_browser_response(std::span<const std::byte>{}).has_value());
}
