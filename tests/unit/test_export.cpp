// Exportacao do resultado para CSV, JSON, Markdown e INSERT.
//
// O caso que mais importa aqui e' seguranca, nao formatacao: um valor vindo
// do banco que comeca por '=' vira FORMULA ao abrir numa planilha, e formula
// executa. Os demais testes cobrem o que quebraria o arquivo -- delimitador
// dentro do valor, aspas, quebra de linha, NULL.
#include "test_main.hpp"

#include "db/export.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// Monta um ResultSet pequeno. Cada coluna recebe seus valores em ordem;
// `null_at` marca quais linhas sao NULL.
ResultSet make_result(
    const std::vector<std::pair<std::string, DataKind>>& columns,
    const std::vector<std::vector<std::string>>& rows,
    const std::vector<std::pair<std::size_t, std::size_t>>& nulls = {}) {

    ResultSetBuilder builder;
    for (const auto& [name, kind] : columns) {
        ColumnInfo info;
        info.name = name;
        info.kind = kind;
        builder.add_column(std::move(info));
    }

    for (std::size_t r = 0; r < rows.size(); ++r) {
        for (std::size_t c = 0; c < rows[r].size(); ++c) {
            const bool is_null = std::find(nulls.begin(), nulls.end(),
                                           std::make_pair(r, c)) != nulls.end();
            if (is_null) builder.append_null(c);
            else         builder.append_text(c, rows[r][c]);
        }
    }
    builder.set_row_count(rows.size());
    return builder.take();
}

} // namespace

// --- Seguranca: CSV injection ------------------------------------------------

OTTER_TEST(export_csv_neutralizes_formula_injection) {
    // https://owasp.org/www-community/attacks/CSV_Injection
    //
    // Um valor gravado no banco como =HYPERLINK(...) ou =cmd|...  vira
    // formula executavel quando a planilha abre o CSV. Quem exporta dados de
    // um banco raramente controla o que ha' neles.
    const ResultSet rs = make_result(
        {{"nota", DataKind::string}},
        {{"=1+1"}, {"+55 11 99999"}, {"-500"}, {"@usuario"}, {"normal"}});

    ExportOptions options;
    options.format = ExportFormat::csv;

    const std::string csv = export_to_string(rs, options);

    // Apostrofo na frente: a planilha mostra o valor e nao o avalia.
    OTTER_CHECK(has(csv, "'=1+1"));
    OTTER_CHECK(has(csv, "'+55 11 99999"));
    OTTER_CHECK(has(csv, "'-500"));
    OTTER_CHECK(has(csv, "'@usuario"));

    // Valor inofensivo passa intacto -- prefixar tudo estragaria os dados.
    OTTER_CHECK(has(csv, "normal"));
    OTTER_CHECK(!has(csv, "'normal"));
}

OTTER_TEST(export_csv_formula_escaping_can_be_turned_off) {
    // Quem reimporta o CSV noutro sistema pode precisar do valor cru. A opcao
    // existe, mas o padrao e' proteger.
    const ResultSet rs = make_result({{"f", DataKind::string}}, {{"=SOMA(A1)"}});

    ExportOptions options;
    options.format = ExportFormat::csv;
    options.escape_formulas = false;

    const std::string csv = export_to_string(rs, options);
    OTTER_CHECK(has(csv, "=SOMA(A1)"));
    OTTER_CHECK(!has(csv, "'=SOMA"));
}

// --- CSV ---------------------------------------------------------------------

OTTER_TEST(export_csv_quotes_only_what_needs_quoting) {
    const ResultSet rs = make_result(
        {{"a", DataKind::string}, {"b", DataKind::string}},
        {{"simples", "com,virgula"},
         {"com\"aspas", "com\nquebra"}});

    ExportOptions options;
    const std::string csv = export_to_string(rs, options);

    // Citar tudo seria mais simples e produziria arquivo maior e menos
    // legivel.
    OTTER_CHECK(has(csv, "simples,"));
    OTTER_CHECK(!has(csv, "\"simples\""));

    OTTER_CHECK(has(csv, "\"com,virgula\""));
    OTTER_CHECK(has(csv, "\"com\"\"aspas\""));   // RFC 4180: aspas dobram
    OTTER_CHECK(has(csv, "\"com\nquebra\""));
}

OTTER_TEST(export_csv_writes_null_as_empty_by_default) {
    // Vazio e' o certo para reimportar. "[null]" seria confundido com o
    // literal de mesmo texto.
    const ResultSet rs = make_result(
        {{"a", DataKind::string}, {"b", DataKind::string}},
        {{"x", "y"}}, {{0, 1}});

    const std::string csv = export_to_string(rs, ExportOptions{});
    OTTER_CHECK(has(csv, "x,\r\n"));
}

OTTER_TEST(export_csv_header_is_optional_and_uses_crlf) {
    const ResultSet rs = make_result({{"coluna", DataKind::string}}, {{"v"}});

    ExportOptions with_header;
    OTTER_CHECK(has(export_to_string(rs, with_header), "coluna\r\n"));

    ExportOptions without;
    without.write_header = false;
    OTTER_CHECK(!has(export_to_string(rs, without), "coluna"));
}

OTTER_TEST(export_csv_honours_a_custom_delimiter) {
    const ResultSet rs = make_result(
        {{"a", DataKind::string}, {"b", DataKind::string}},
        {{"um;dois", "tres"}});

    ExportOptions options;
    options.delimiter = ';';

    const std::string csv = export_to_string(rs, options);
    // Com ';' como separador, o valor que o contem passa a precisar de aspas
    // -- e a virgula deixa de precisar.
    OTTER_CHECK(has(csv, "\"um;dois\";tres"));
}

// --- JSON --------------------------------------------------------------------

OTTER_TEST(export_json_distinguishes_null_from_the_string_null) {
    const ResultSet rs = make_result(
        {{"a", DataKind::string}, {"b", DataKind::string}},
        {{"null", "x"}}, {{0, 1}});

    ExportOptions options;
    options.format = ExportFormat::json;

    const std::string json = export_to_string(rs, options);

    // "a" tem a STRING "null"; "b" e' NULL de verdade. Confundi-los produz um
    // JSON que mente.
    OTTER_CHECK(has(json, "\"a\": \"null\""));
    OTTER_CHECK(has(json, "\"b\": null"));
}

OTTER_TEST(export_json_writes_numbers_without_quotes) {
    const ResultSet rs = make_result(
        {{"id", DataKind::integer},
         {"valor", DataKind::numeric},
         {"ativo", DataKind::boolean},
         {"nome", DataKind::string}},
        {{"42", "1234.56", "t", "lontra"}});

    ExportOptions options;
    options.format = ExportFormat::json;

    const std::string json = export_to_string(rs, options);

    // Numero citado vira string ao ler de volta.
    OTTER_CHECK(has(json, "\"id\": 42"));
    OTTER_CHECK(has(json, "\"valor\": 1234.56"));
    OTTER_CHECK(has(json, "\"ativo\": true"));
    OTTER_CHECK(has(json, "\"nome\": \"lontra\""));
}

OTTER_TEST(export_json_escapes_control_characters) {
    const ResultSet rs = make_result(
        {{"t", DataKind::string}}, {{"linha1\nlinha2\ttab \"aspas\""}});

    ExportOptions options;
    options.format = ExportFormat::json;

    const std::string json = export_to_string(rs, options);
    OTTER_CHECK(has(json, "\\n"));
    OTTER_CHECK(has(json, "\\t"));
    OTTER_CHECK(has(json, "\\\""));
}

// --- Markdown ----------------------------------------------------------------

OTTER_TEST(export_markdown_escapes_the_pipe) {
    // '|' dentro do valor quebraria a tabela.
    const ResultSet rs = make_result(
        {{"expr", DataKind::string}}, {{"a | b"}});

    ExportOptions options;
    options.format = ExportFormat::markdown;

    const std::string md = export_to_string(rs, options);
    OTTER_CHECK(has(md, "a \\| b"));
}

OTTER_TEST(export_markdown_aligns_numbers_right) {
    const ResultSet rs = make_result(
        {{"nome", DataKind::string}, {"total", DataKind::numeric}},
        {{"lontra", "10"}});

    ExportOptions options;
    options.format = ExportFormat::markdown;

    const std::string md = export_to_string(rs, options);
    // A linha separadora leva ':' na coluna numerica.
    OTTER_CHECK(has(md, "-: |"));
}

// --- SQL ---------------------------------------------------------------------

OTTER_TEST(export_sql_quotes_strings_and_leaves_numbers_bare) {
    const ResultSet rs = make_result(
        {{"id", DataKind::integer},
         {"nome", DataKind::string},
         {"ativo", DataKind::boolean}},
        {{"7", "Lontra", "t"}});

    ExportOptions options;
    options.format     = ExportFormat::sql_insert;
    options.table_name = "otter_test.cliente";

    const std::string sql = export_to_string(rs, options);

    OTTER_CHECK(has(sql, "INSERT INTO otter_test.cliente (id, nome, ativo)"));
    OTTER_CHECK(has(sql, "(7, 'Lontra', TRUE)"));
}

OTTER_TEST(export_sql_doubles_single_quotes) {
    // Sem dobrar, o INSERT gerado nao compila -- e, com valor controlado por
    // terceiro, seria injecao.
    const ResultSet rs = make_result(
        {{"nome", DataKind::string}}, {{"O'Brien"}});

    ExportOptions options;
    options.format = ExportFormat::sql_insert;

    const std::string sql = export_to_string(rs, options);
    OTTER_CHECK(has(sql, "'O''Brien'"));
}

OTTER_TEST(export_sql_writes_null_not_the_text) {
    const ResultSet rs = make_result(
        {{"a", DataKind::string}}, {{"x"}}, {{0, 0}});

    ExportOptions options;
    options.format = ExportFormat::sql_insert;

    const std::string sql = export_to_string(rs, options);
    OTTER_CHECK(has(sql, "(NULL)"));
    OTTER_CHECK(!has(sql, "'NULL'"));
}

OTTER_TEST(export_sql_can_bundle_rows_in_one_statement) {
    const ResultSet rs = make_result(
        {{"id", DataKind::integer}}, {{"1"}, {"2"}, {"3"}});

    ExportOptions options;
    options.format = ExportFormat::sql_insert;
    options.one_statement_per_row = false;

    const std::string sql = export_to_string(rs, options);

    // Um INSERT so', com tres tuplas -- bem mais rapido de carregar.
    std::size_t inserts = 0;
    for (std::size_t pos = sql.find("INSERT"); pos != std::string::npos;
         pos = sql.find("INSERT", pos + 1)) {
        ++inserts;
    }
    OTTER_CHECK_EQ(inserts, std::size_t{1});
    OTTER_CHECK(has(sql, "(1),\n    (2),\n    (3);"));
}

OTTER_TEST(export_handles_an_empty_result) {
    const ResultSet rs = make_result({{"a", DataKind::string}}, {});

    // Nenhum formato pode produzir lixo ou quebrar com zero linhas.
    ExportOptions csv;
    OTTER_CHECK(has(export_to_string(rs, csv), "a\r\n"));

    ExportOptions json;
    json.format = ExportFormat::json;
    OTTER_CHECK_EQ(export_to_string(rs, json), std::string{"[\n]\n"});

    ExportOptions sql;
    sql.format = ExportFormat::sql_insert;
    OTTER_CHECK(export_to_string(rs, sql).empty());
}
