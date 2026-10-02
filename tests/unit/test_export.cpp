// Exportacao do resultado para CSV, JSON, Markdown e INSERT.
//
// O caso que mais importa aqui e' seguranca, nao formatacao: um valor vindo
// do banco que comeca por '=' vira FORMULA ao abrir numa planilha, e formula
// executa. Os demais testes cobrem o que quebraria o arquivo -- delimitador
// dentro do valor, aspas, quebra de linha, NULL.
#include "test_main.hpp"

#include "db/export.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// --- Exportacao em pedacos ----------------------------------------------------
//
// A consulta inteira e' lida do servidor aos poucos, e o arquivo sai de
// varios ResultSets. O que quebra ai' e' a COSTURA: cabecalho repetido a
// cada pedaco, virgula faltando (ou sobrando) entre dois pedacos do JSON.

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::string temp_file(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// O mesmo conteudo de uma vez e em dois pedacos: os arquivos tem de ser
// iguais byte a byte.
bool same_in_chunks(ExportFormat format) {
    const std::vector<std::pair<std::string, DataKind>> columns = {
        {"id", DataKind::integer}, {"nome", DataKind::string}};

    ExportOptions options;
    options.format                = format;
    options.one_statement_per_row = true;

    const ResultSet whole  = make_result(columns, {{"1", "ana"}, {"2", "bia"}, {"3", "caio"}});
    const ResultSet first  = make_result(columns, {{"1", "ana"}, {"2", "bia"}});
    const ResultSet second = make_result(columns, {{"3", "caio"}});

    const std::string path = temp_file("otter-export-chunks.tmp");
    ExportStream stream(options);
    if (!stream.open(path)) return false;
    if (!stream.write(first) || !stream.write(second) || !stream.finish()) return false;
    if (stream.rows() != 3) return false;

    const std::string chunked = read_file(path);
    std::filesystem::remove(path);
    return chunked == export_to_string(whole, options);
}

} // namespace

OTTER_TEST(export_stream_chunks_make_one_file) {
    OTTER_CHECK(same_in_chunks(ExportFormat::csv));
    OTTER_CHECK(same_in_chunks(ExportFormat::json));
    OTTER_CHECK(same_in_chunks(ExportFormat::sql_insert));
    OTTER_CHECK(same_in_chunks(ExportFormat::html));
    OTTER_CHECK(same_in_chunks(ExportFormat::xml));
}

OTTER_TEST(export_stream_json_of_an_empty_result_is_valid) {
    ExportOptions options;
    options.format = ExportFormat::json;

    const std::string path = temp_file("otter-export-empty.tmp");
    ExportStream stream(options);
    OTTER_CHECK(stream.open(path).has_value());
    OTTER_CHECK(stream.write(make_result({{"id", DataKind::integer}}, {})).has_value());
    OTTER_CHECK(stream.finish().has_value());

    OTTER_CHECK(read_file(path) == "[\n]\n");
    std::filesystem::remove(path);
}

OTTER_TEST(export_stream_reports_a_path_it_cannot_write) {
    ExportStream stream(ExportOptions{});
    // Um DIRETORIO no lugar do arquivo: abrir para escrita falha.
    OTTER_CHECK(!stream.open(std::filesystem::temp_directory_path().string()).has_value());
}

// --- HTML, XML e TXT ----------------------------------------------------------

OTTER_TEST(export_html_escapes_markup) {
    // Um valor com <script> nao pode virar script no relatorio.
    ExportOptions options;
    options.format = ExportFormat::html;

    const std::string html = export_to_string(
        make_result({{"a<b", DataKind::string}, {"n", DataKind::integer}},
                    {{"<script>alert(1)</script> & \"x\"", "7"}, {"", ""}},
                    {{1, 0}, {1, 1}}),
        options);

    OTTER_CHECK(has(html, "<th>a&lt;b</th>"));
    OTTER_CHECK(has(html, "&lt;script&gt;alert(1)&lt;/script&gt; &amp; &quot;x&quot;"));
    OTTER_CHECK(!has(html, "<script>"));
    OTTER_CHECK(has(html, "<td class=\"n\">7</td>"));       // numero a' direita
    OTTER_CHECK(has(html, "<td class=\"null\">NULL</td>"));  // NULL nao e' ''
    OTTER_CHECK(has(html, "</table>"));
    OTTER_CHECK(has(html, "charset=\"utf-8\""));
}

OTTER_TEST(export_xml_is_well_formed) {
    ExportOptions options;
    options.format = ExportFormat::xml;

    const std::string xml = export_to_string(
        make_result({{"valor total", DataKind::numeric}, {"1a", DataKind::string},
                     {"obs", DataKind::string}},
                    {{"10.5", "a & b <c>", ""}},
                    {{0, 2}}),
        options);

    OTTER_CHECK(xml.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
    // Nome de coluna com espaco, ou comecando por digito, nao e' nome de
    // elemento: um XML assim nao abre em leitor nenhum.
    OTTER_CHECK(has(xml, "<valor_total>10.5</valor_total>"));
    OTTER_CHECK(has(xml, "<_a>a &amp; b &lt;c&gt;</_a>"));
    OTTER_CHECK(has(xml, "<obs null=\"true\"/>"));
    OTTER_CHECK(has(xml, "<DATA_RECORD>"));
    OTTER_CHECK(xml.ends_with("</data>\n"));
}

OTTER_TEST(export_xml_drops_control_characters) {
    // U+0001 e' invalido em XML 1.0, mesmo escapado.
    ExportOptions options;
    options.format = ExportFormat::xml;
    const std::string xml = export_to_string(
        make_result({{"t", DataKind::string}}, {{std::string("a\x01" "b")}}), options);
    OTTER_CHECK(has(xml, "<t>a b</t>"));
}

OTTER_TEST(export_txt_aligns_by_characters_not_bytes) {
    ExportOptions options;
    options.format = ExportFormat::txt;

    const std::string txt = export_to_string(
        make_result({{"nome", DataKind::string}, {"n", DataKind::integer}},
                    {{"a\xC3\xA7\xC3\xA3o", "5"}, {"xy", "123"}}),
        options);

    // "ação" tem 4 letras e 6 bytes: medida em bytes, a borda da linha de
    // "xy" sairia duas colunas a' direita.
    OTTER_CHECK(has(txt, "| a\xC3\xA7\xC3\xA3o |   5 |\n"));
    OTTER_CHECK(has(txt, "| xy   | 123 |\n"));
    OTTER_CHECK(has(txt, "+------+-----+\n"));
}

OTTER_TEST(export_every_format_has_a_name_and_an_extension) {
    for (int f = 0; f <= static_cast<int>(ExportFormat::txt); ++f) {
        const auto format = static_cast<ExportFormat>(f);
        OTTER_CHECK(to_string(format) != "unknown");
        OTTER_CHECK(file_extension(format).starts_with("."));
    }
}
