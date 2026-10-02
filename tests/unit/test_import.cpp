// C-Otter -- testes de db/import: ler CSV e gerar a carga.
//
// O leitor de CSV erra em silencio: uma quebra de linha dentro de aspas vira
// duas linhas, um campo vazio vira string onde era NULL, um arquivo de
// planilha em portugues (';') vira uma coluna so'. Nenhum desses da' erro --
// o dado entra errado no banco.
#include "test_main.hpp"

#include "db/ddl.hpp"
#include "db/export.hpp"
#include "db/import.hpp"

#include <string>

using namespace otter::db;

namespace {

struct DialectGuard {
    QuoteStyle previous = sql_dialect();
    explicit DialectGuard(QuoteStyle style) { set_sql_dialect(style); }
    ~DialectGuard() { set_sql_dialect(previous); }
};

} // namespace

OTTER_TEST(import_csv_basic) {
    const CsvTable table = parse_csv("id,nome\r\n1,ana\r\n2,bia\r\n", {});
    OTTER_CHECK(table.error.empty());
    OTTER_CHECK(table.header.size() == 2 && table.header[1] == "nome");
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(*table.rows[1][1] == "bia");
}

OTTER_TEST(import_csv_quotes_delimiters_and_newlines) {
    // Tudo o que o RFC 4180 permite dentro de aspas.
    const CsvTable table =
        parse_csv("a,b\n\"Silva, Ana\",\"linha 1\nlinha 2\"\n\"diz \"\"oi\"\"\",x\n", {});
    OTTER_CHECK(table.error.empty());
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(*table.rows[0][0] == "Silva, Ana");
    OTTER_CHECK(*table.rows[0][1] == "linha 1\nlinha 2");
    OTTER_CHECK(*table.rows[1][0] == "diz \"oi\"");
}

OTTER_TEST(import_csv_null_is_only_the_unquoted_empty) {
    // `a,,b` tem NULL no meio; `a,"",b` tem string vazia. Confundir os dois
    // troca NULL por '' numa coluna NOT NULL -- ou o contrario.
    const CsvTable table = parse_csv("a,b,c\n1,,x\n2,\"\",y\n", {});
    OTTER_CHECK(!table.rows[0][1].has_value());
    OTTER_CHECK(table.rows[1][1].has_value() && table.rows[1][1]->empty());

    CsvOptions options;
    options.null_text = "\\N";
    const CsvTable marked = parse_csv("a,b\n\\N,\"\\N\"\n,z\n", options);
    OTTER_CHECK(!marked.rows[0][0].has_value());        // \N sem aspas: NULL
    OTTER_CHECK(*marked.rows[0][1] == "\\N");            // entre aspas: o texto
    OTTER_CHECK(marked.rows[1][0].has_value());          // vazio ja' nao e' NULL
}

OTTER_TEST(import_csv_bom_blank_lines_and_missing_final_newline) {
    const CsvTable table = parse_csv("\xEF\xBB\xBFid;v\n1;a\n\n2;b", {';', '"', true, {}});
    OTTER_CHECK(table.error.empty());
    OTTER_CHECK(table.header.front() == "id");   // sem o BOM grudado no nome
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(*table.rows[1][1] == "b");
}

OTTER_TEST(import_csv_without_header_names_the_columns) {
    CsvOptions options;
    options.header = false;
    const CsvTable table = parse_csv("1,ana\n2,bia\n", options);
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(table.header.size() == 2 && table.header[0] == "column1");
}

OTTER_TEST(import_csv_reports_the_malformed_line) {
    // Parar e dizer a linha: carregar "o que deu" deixaria a tabela com
    // metade do arquivo, sem ninguem saber qual metade.
    CsvTable table = parse_csv("a,b\n1,2\n3\n4,5\n", {});
    OTTER_CHECK(table.error == "line 3 has 1 field(s), expected 2");
    OTTER_CHECK(table.rows.size() == 1);

    table = parse_csv("a,b\n1,\"aberto\n", {});
    OTTER_CHECK(!table.error.empty());
}

OTTER_TEST(import_csv_preview_limit) {
    const CsvTable table = parse_csv("a\n1\n2\n3\n4\n", {}, 2);
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(table.truncated);
    OTTER_CHECK(table.error.empty());
}

OTTER_TEST(import_detects_the_delimiter) {
    OTTER_CHECK(detect_delimiter("a,b,c\n1,2,3") == ',');
    OTTER_CHECK(detect_delimiter("a;b;c\n1,5;2;3") == ';');
    OTTER_CHECK(detect_delimiter("a\tb\n") == '\t');
    OTTER_CHECK(detect_delimiter("a|b|c") == '|');
    // A virgula dentro de aspas nao conta.
    OTTER_CHECK(detect_delimiter("\"Silva, Ana, Jr\";idade\n") == ';');
    OTTER_CHECK(detect_delimiter("") == ',');
}

OTTER_TEST(import_matches_columns_by_name) {
    const auto mapping = match_columns({"ID", "Nome", "extra"}, {"id", "nome", "email"});
    OTTER_CHECK(mapping.size() == 3);
    OTTER_CHECK(mapping[0] == "id" && mapping[1] == "nome");
    OTTER_CHECK(mapping[2].empty());   // sem par: ignorada, nao adivinhada
}

OTTER_TEST(import_generates_batched_inserts) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    const CsvTable data = parse_csv("id,nome,lixo\n1,O'Brien,x\n2,,y\n3,\"\",z\n", {});

    ImportPlan plan;
    plan.schema     = "s";
    plan.table      = "Cliente";
    plan.columns    = {"id", "nome", ""};   // a terceira e' ignorada
    plan.batch_rows = 2;

    const ImportScript script = generate_import(data, plan);
    OTTER_CHECK(script.error.empty());
    OTTER_CHECK(script.rows == 3);
    OTTER_CHECK(script.statements.size() == 2);   // 2 + 1
    OTTER_CHECK(script.statements[0] ==
                "INSERT INTO s.\"Cliente\" (id, nome) VALUES\n"
                "    ('1', 'O''Brien'),\n"
                "    ('2', NULL)");
    OTTER_CHECK(script.statements[1] ==
                "INSERT INTO s.\"Cliente\" (id, nome) VALUES\n    ('3', '')");
}

OTTER_TEST(import_truncate_comes_first) {
    const DialectGuard guard(QuoteStyle::double_quotes);

    ImportPlan plan;
    plan.schema         = "s";
    plan.table          = "t";
    plan.columns        = {"a"};
    plan.truncate_first = true;

    const ImportScript script = generate_import(parse_csv("a\n1\n", {}), plan);
    OTTER_CHECK(script.statements.size() == 2);
    OTTER_CHECK(script.statements.front() == "TRUNCATE TABLE s.t");
}

OTTER_TEST(import_refuses_what_would_fail_midway) {
    ImportPlan plan;
    plan.table   = "t";
    plan.columns = {"", ""};
    OTTER_CHECK(!generate_import(parse_csv("a,b\n1,2\n", {}), plan).error.empty());

    plan.columns = {"x", "x"};   // duas para a mesma coluna
    OTTER_CHECK(!generate_import(parse_csv("a,b\n1,2\n", {}), plan).error.empty());

    plan.columns = {"x", "y"};
    plan.table.clear();
    OTTER_CHECK(!generate_import(parse_csv("a,b\n1,2\n", {}), plan).error.empty());

    // Arquivo com erro nao gera carga parcial.
    plan.table = "t";
    OTTER_CHECK(!generate_import(parse_csv("a,b\n1\n", {}), plan).error.empty());
}

OTTER_TEST(import_mysql_literal_escapes_the_backslash) {
    // No MySQL a barra invertida e' escape: 'C:\' fecharia a string errado.
    const DialectGuard guard(QuoteStyle::backticks);

    ImportPlan plan;
    plan.table   = "t";
    plan.columns = {"caminho"};
    const ImportScript script = generate_import(parse_csv("p\nC:\\tmp\\\n", {}), plan);
    OTTER_CHECK(script.statements.size() == 1);
    OTTER_CHECK(script.statements.front().find("'C:\\\\tmp\\\\'") != std::string::npos);
}

OTTER_TEST(import_reads_back_what_export_wrote) {
    // A volta completa: o CSV que o exportador escreve e' o que o importador
    // le'. Um par que nao fecha perde dado sem avisar.
    ResultSetBuilder builder;
    for (const char* name : {"id", "texto"}) {
        ColumnInfo info;
        info.name = name;
        info.kind = DataKind::string;
        builder.add_column(std::move(info));
    }
    builder.append_text(0, "1");
    builder.append_text(1, "a,b \"c\"\nd");
    builder.append_text(0, "2");
    builder.append_null(1);
    builder.set_row_count(2);
    const ResultSet rs = builder.take();

    ExportOptions options;
    options.escape_formulas = false;
    const CsvTable table = parse_csv(export_to_string(rs, options), {});

    OTTER_CHECK(table.error.empty());
    OTTER_CHECK(table.rows.size() == 2);
    OTTER_CHECK(*table.rows[0][1] == "a,b \"c\"\nd");
    OTTER_CHECK(!table.rows[1][1].has_value());
}
