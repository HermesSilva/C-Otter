// Edicao de dados pela grade (ADR 0014).
//
// Esta e' a operacao mais perigosa da ferramenta: um UPDATE com WHERE errado
// altera linhas que ninguem pediu. Por isso a maior parte destes testes cobre
// os casos em que a grade deve RECUSAR editar, nao os em que ela edita.
#include "test_main.hpp"

#include "db/edit.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

struct ColumnSpec {
    std::string   name;
    DataKind      kind = DataKind::string;
    std::uint32_t table_oid = 0;
};

ResultSet make_result(const std::vector<ColumnSpec>& columns,
                      const std::vector<std::vector<std::string>>& rows,
                      const std::vector<std::pair<std::size_t, std::size_t>>& nulls = {}) {
    ResultSetBuilder builder;
    for (const ColumnSpec& spec : columns) {
        ColumnInfo info;
        info.name             = spec.name;
        info.kind             = spec.kind;
        info.source_table_oid = spec.table_oid;
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

// Catalogo com uma tabela 'cliente' de OID 16400 e PK em cliente_id.
std::vector<SchemaMeta> catalog_with_pk() {
    ConstraintMeta pk;
    pk.name    = "cliente_pkey";
    pk.kind    = ObjKind::primary_key;
    pk.columns = "cliente_id";

    TableMeta table;
    table.name = "cliente";
    table.oid  = 16400;
    table.constraints = {pk};
    table.constraints_loaded = true;

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {table};
    return {schema};
}

// O mesmo resultado, mas identificando a origem por NOME em vez de OID --
// como o MySQL faz, porque o ColumnDefinition41 nao tem OID.
ResultSet make_result_named(const std::vector<ColumnSpec>& columns,
                            std::string_view schema, std::string_view table,
                            const std::vector<std::vector<std::string>>& rows) {
    ResultSetBuilder builder;
    for (const ColumnSpec& spec : columns) {
        ColumnInfo info;
        info.name = spec.name;
        info.kind = spec.kind;

        // table_oid != 0 no spec significa "esta coluna vem da tabela"; aqui
        // isso vira o par (schema, tabela) em vez do OID.
        if (spec.table_oid != 0) {
            info.source_schema = std::string(schema);
            info.source_table  = std::string(table);
            info.source_column_name = spec.name;
        }
        builder.add_column(std::move(info));
    }
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < row.size(); ++c) builder.append_text(c, row[c]);
    }
    builder.set_row_count(rows.size());
    return builder.take();
}

} // namespace

// --- Origem por nome, sem OID (MySQL) ----------------------------------------

OTTER_TEST(edit_finds_the_table_by_name_when_there_is_no_oid) {
    // O MySQL nao tem OID: a origem vem como (banco, tabela) no
    // ColumnDefinition41. Olhar so' o OID fazia TODO resultado de MySQL ser
    // recusado com "nao vem de uma tabela" -- inclusive um SELECT * numa
    // tabela com chave primaria. Foi o que a captura de tela mostrou.
    const ResultSet rs = make_result_named(
        {{"cliente_id", DataKind::integer, 1}, {"nome", DataKind::string, 1}},
        "otter_test", "cliente",
        {{"1", "Alfa"}, {"2", "Beta"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(target.editable());
    OTTER_CHECK_EQ(target.table, std::string{"cliente"});
    OTTER_CHECK_EQ(target.schema, std::string{"otter_test"});
}

OTTER_TEST(edit_by_name_still_refuses_a_join) {
    // Duas tabelas diferentes continuam sendo recusadas, mesmo sem OID: um
    // UPDATE precisa saber QUAL tabela alterar.
    ResultSetBuilder builder;
    for (const auto& [column, table] : {std::pair{"cliente_id", "cliente"},
                                        std::pair{"total", "pedido"}}) {
        ColumnInfo info;
        info.name          = column;
        info.source_schema = "otter_test";
        info.source_table  = table;
        builder.add_column(std::move(info));
    }
    builder.append_text(0, "1");
    builder.append_text(1, "10");
    builder.set_row_count(1);

    const EditTarget target = find_edit_target(builder.take(), catalog_with_pk());
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::multiple_tables);
}

OTTER_TEST(edit_by_name_ignores_expressions) {
    // Uma coluna calculada nao tem origem e NAO desqualifica o resultado:
    // "SELECT id, nome, NOW()" continua editavel nas duas primeiras.
    const ResultSet rs = make_result_named(
        {{"cliente_id", DataKind::integer, 1},
         {"nome",       DataKind::string, 1},
         {"agora",      DataKind::timestamp, 0}},    // sem origem
        "otter_test", "cliente",
        {{"1", "Alfa", "2026-01-01"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(target.editable());
    OTTER_CHECK_EQ(target.table, std::string{"cliente"});
}

// --- Quando a grade NAO deve editar ------------------------------------------

OTTER_TEST(edit_refuses_a_table_without_a_key) {
    // Sem PK nao ha' como escrever o WHERE. A grade diz isso em vez de
    // oferecer um campo que falha na hora de salvar.
    TableMeta no_key;
    no_key.name = "log";
    no_key.oid  = 16500;
    no_key.constraints_loaded = true;

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {no_key};

    const ResultSet rs = make_result(
        {{"mensagem", DataKind::string, 16500}}, {{"algo"}});

    const EditTarget target = find_edit_target(rs, {schema});
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::no_key);
}

OTTER_TEST(edit_refuses_a_join) {
    // Duas tabelas no resultado: nao ha' para onde escrever.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"pedido_id",  DataKind::integer, 16401}},
        {{"1", "7"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::multiple_tables);
}

OTTER_TEST(edit_refuses_a_result_of_pure_expressions) {
    // SELECT now(), 1+1 -- nenhuma coluna vem de tabela.
    const ResultSet rs = make_result(
        {{"agora", DataKind::timestamp, 0}, {"soma", DataKind::integer, 0}},
        {{"2026-01-01", "2"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::no_source_table);
}

OTTER_TEST(edit_refuses_when_the_key_is_not_selected) {
    // "SELECT nome FROM cliente": ha' PK, mas ela nao veio no resultado.
    const ResultSet rs = make_result(
        {{"nome", DataKind::string, 16400}}, {{"Lontra"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::key_not_selected);
}

OTTER_TEST(edit_refuses_a_view) {
    // Escrever numa view exige trigger INSTEAD OF; nao presumimos que exista.
    ConstraintMeta pk;
    pk.kind    = ObjKind::primary_key;
    pk.columns = "id";

    TableMeta view;
    view.name = "vw_cliente";
    view.oid  = 16600;
    view.kind = ObjKind::view;
    view.constraints = {pk};
    view.constraints_loaded = true;

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {view};

    const ResultSet rs = make_result({{"id", DataKind::integer, 16600}}, {{"1"}});
    OTTER_CHECK(!find_edit_target(rs, {schema}).editable());
}

// --- Quando pode ------------------------------------------------------------

OTTER_TEST(edit_accepts_a_single_table_with_its_key) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "Lontra"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());
    OTTER_CHECK(target.editable());
    OTTER_CHECK_EQ(target.schema, std::string{"otter_test"});
    OTTER_CHECK_EQ(target.table, std::string{"cliente"});
    OTTER_CHECK_EQ(target.key_columns.size(), std::size_t{1});
    OTTER_CHECK_EQ(target.key_columns[0], std::size_t{0});
}

OTTER_TEST(edit_ignores_expression_columns_alongside_table_columns) {
    // "SELECT cliente_id, nome, now()" continua editavel nas duas primeiras.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer,   16400},
         {"nome",       DataKind::string,    16400},
         {"agora",      DataKind::timestamp, 0}},
        {{"7", "Lontra", "2026-01-01"}});

    OTTER_CHECK(find_edit_target(rs, catalog_with_pk()).editable());
}

// --- Geracao do UPDATE -------------------------------------------------------

OTTER_TEST(edit_generates_one_update_per_row) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400},
         {"credito",    DataKind::numeric, 16400}},
        {{"7", "Lontra", "100"}, {"8", "Rio", "200"}});

    const EditTarget target = find_edit_target(rs, catalog_with_pk());

    EditBuffer buffer;
    buffer.set(0, 1, "Lontra Marinha");
    buffer.set(0, 2, "500");
    buffer.set(1, 1, "Rio Corrente");

    // Tres celulas, duas linhas: DOIS comandos. Um por celula faria tres.
    OTTER_CHECK_EQ(buffer.size(), std::size_t{3});
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{2});

    const auto updates = generate_updates(rs, target, buffer);
    OTTER_CHECK(updates.has_value());
    OTTER_CHECK_EQ(updates->size(), std::size_t{2});

    // As duas colunas da mesma linha entram no mesmo SET.
    OTTER_CHECK(has((*updates)[0], "nome = 'Lontra Marinha'"));
    OTTER_CHECK(has((*updates)[0], "credito = 500"));       // numero sem aspas
    OTTER_CHECK(has((*updates)[0], "WHERE cliente_id = 7"));

    OTTER_CHECK(has((*updates)[1], "WHERE cliente_id = 8"));
}

OTTER_TEST(edit_escapes_quotes_in_the_value) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "x"}});

    EditBuffer buffer;
    buffer.set(0, 1, "O'Brien");

    const auto updates =
        generate_updates(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(updates.has_value());
    OTTER_CHECK(has((*updates)[0], "'O''Brien'"));
}

OTTER_TEST(edit_writes_null_not_the_text) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"email",      DataKind::string,  16400}},
        {{"7", "a@b.c"}});

    EditBuffer buffer;
    buffer.set_null(0, 1);

    const auto updates =
        generate_updates(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(updates.has_value());
    OTTER_CHECK(has((*updates)[0], "email = NULL"));
    OTTER_CHECK(!has((*updates)[0], "'NULL'"));
}

OTTER_TEST(edit_treats_an_emptied_number_as_null) {
    // Apagar o conteudo de um campo numerico significa ausencia, nao zero --
    // e "SET credito = " seria erro de sintaxe.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"credito",    DataKind::numeric, 16400}},
        {{"7", "100"}});

    EditBuffer buffer;
    buffer.set(0, 1, "");

    const auto updates =
        generate_updates(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(updates.has_value());
    OTTER_CHECK(has((*updates)[0], "credito = NULL"));
}

OTTER_TEST(edit_refuses_a_row_whose_key_is_null) {
    // "WHERE id = NULL" nunca casa: o UPDATE alteraria zero linhas em
    // silencio, e o usuario acharia que gravou.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "x"}}, {{0, 0}});

    EditBuffer buffer;
    buffer.set(0, 1, "novo");

    const auto updates =
        generate_updates(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(!updates.has_value());
    OTTER_CHECK(has(updates.error().message(), "NULL"));
}

OTTER_TEST(edit_refuses_to_change_a_key_column) {
    // Alterar a chave mudaria a propria linha que o WHERE identifica.
    // E' possivel em SQL, mas nao por acidente numa grade.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "x"}});

    EditBuffer buffer;
    buffer.set(0, 0, "99");

    const auto updates =
        generate_updates(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(!updates.has_value());
}

OTTER_TEST(edit_handles_a_composite_key) {
    ConstraintMeta pk;
    pk.kind    = ObjKind::primary_key;
    pk.columns = "pedido_id, item_id";

    TableMeta table;
    table.name = "pedido_item";
    table.oid  = 16700;
    table.constraints = {pk};
    table.constraints_loaded = true;

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {table};

    const ResultSet rs = make_result(
        {{"pedido_id",  DataKind::integer, 16700},
         {"item_id",    DataKind::integer, 16700},
         {"quantidade", DataKind::integer, 16700}},
        {{"1", "2", "10"}});

    const EditTarget target = find_edit_target(rs, {schema});
    OTTER_CHECK(target.editable());
    OTTER_CHECK_EQ(target.key_columns.size(), std::size_t{2});

    EditBuffer buffer;
    buffer.set(0, 2, "20");

    const auto updates = generate_updates(rs, target, buffer);
    OTTER_CHECK(updates.has_value());

    // As duas colunas da chave, unidas por AND. Usar so' a primeira alteraria
    // linhas demais.
    OTTER_CHECK(has((*updates)[0], "pedido_id = 1"));
    OTTER_CHECK(has((*updates)[0], "AND"));
    OTTER_CHECK(has((*updates)[0], "item_id = 2"));
}

OTTER_TEST(edit_falls_back_to_a_unique_constraint) {
    // Sem PK, uma constraint unica serve: tambem identifica uma linha so'.
    ConstraintMeta unique;
    unique.kind    = ObjKind::unique_key;
    unique.columns = "email";

    TableMeta table;
    table.name = "cliente";
    table.oid  = 16400;
    table.constraints = {unique};
    table.constraints_loaded = true;

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {table};

    const ResultSet rs = make_result(
        {{"email", DataKind::string, 16400},
         {"nome",  DataKind::string, 16400}},
        {{"a@b.c", "Lontra"}});

    const EditTarget target = find_edit_target(rs, {schema});
    OTTER_CHECK(target.editable());
    OTTER_CHECK_EQ(target.key_columns[0], std::size_t{0});
}

// --- Buffer -------------------------------------------------------------------

OTTER_TEST(edit_buffer_tracks_and_reverts) {
    EditBuffer buffer;
    OTTER_CHECK(buffer.empty());

    buffer.set(0, 1, "a");
    buffer.set(0, 2, "b");
    OTTER_CHECK_EQ(buffer.size(), std::size_t{2});
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{1});

    // Reeditar a mesma celula substitui, nao acumula.
    buffer.set(0, 1, "c");
    OTTER_CHECK_EQ(buffer.size(), std::size_t{2});
    OTTER_CHECK_EQ(buffer.find(0, 1)->value, std::string{"c"});

    buffer.revert(0, 1);
    OTTER_CHECK_EQ(buffer.size(), std::size_t{1});
    OTTER_CHECK(buffer.find(0, 1) == nullptr);

    buffer.clear();
    OTTER_CHECK(buffer.empty());
}

OTTER_TEST(edit_distinguishes_no_key_from_key_not_loaded) {
    // Dizer "a tabela nao tem chave primaria" de uma tabela que TEM seria
    // mentira: o no' simplesmente ainda nao foi expandido. Foi o defeito
    // visto na tela -- a mensagem culpava a tabela pelo que era estado da UI.
    TableMeta not_loaded;
    not_loaded.name = "cliente";
    not_loaded.oid  = 16400;
    not_loaded.constraints_loaded = false;   // ninguem expandiu ainda

    SchemaMeta schema;
    schema.name   = "otter_test";
    schema.tables = {not_loaded};

    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400}}, {{"1"}});

    const EditTarget target = find_edit_target(rs, {schema});
    OTTER_CHECK(!target.editable());
    OTTER_CHECK(target.refusal == EditRefusal::key_not_loaded);

    // E o nome da tabela ja' esta' preenchido, para a UI saber o que pedir.
    OTTER_CHECK_EQ(target.table, std::string{"cliente"});
    OTTER_CHECK_EQ(target.schema, std::string{"otter_test"});
}

// --- Exclusao ----------------------------------------------------------------

OTTER_TEST(edit_generates_a_delete_with_the_key) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "Lontra"}, {"8", "Rio"}});

    EditBuffer buffer;
    buffer.mark_deleted(1);

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());
    OTTER_CHECK_EQ(changes->size(), std::size_t{1});
    OTTER_CHECK(has((*changes)[0], "DELETE FROM otter_test.cliente"));
    OTTER_CHECK(has((*changes)[0], "WHERE cliente_id = 8"));
}

OTTER_TEST(edit_marking_a_row_deleted_drops_its_pending_edits) {
    // Alterar e depois excluir a mesma linha: o UPDATE seria executado e
    // imediatamente descartado pelo DELETE. Melhor nem gerar.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "Lontra"}});

    EditBuffer buffer;
    buffer.set(0, 1, "novo nome");
    OTTER_CHECK_EQ(buffer.size(), std::size_t{1});

    buffer.mark_deleted(0);
    OTTER_CHECK_EQ(buffer.size(), std::size_t{0});
    OTTER_CHECK(buffer.is_deleted(0));

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());
    OTTER_CHECK_EQ(changes->size(), std::size_t{1});
    OTTER_CHECK(has((*changes)[0], "DELETE"));
}

OTTER_TEST(edit_refuses_to_delete_a_row_with_a_null_key) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "x"}}, {{0, 0}});

    EditBuffer buffer;
    buffer.mark_deleted(0);

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(!changes.has_value());
}

// --- Insercao ----------------------------------------------------------------

OTTER_TEST(edit_generates_an_insert_with_only_the_filled_columns) {
    // Coluna deixada em branco fica FORA do INSERT, para a tabela aplicar o
    // DEFAULT -- e' o que o usuario espera ao nao preencher um serial.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400},
         {"credito",    DataKind::numeric, 16400}},
        {{"7", "Lontra", "100"}});

    EditBuffer buffer;
    const std::size_t novo = buffer.add_row();
    buffer.set_new_value(novo, 1, "Nova Lontra");
    buffer.set_new_value(novo, 2, "999");
    // cliente_id nao preenchido: serial cuida.

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());
    OTTER_CHECK_EQ(changes->size(), std::size_t{1});

    OTTER_CHECK(has((*changes)[0], "INSERT INTO otter_test.cliente"));
    OTTER_CHECK(has((*changes)[0], "nome"));
    OTTER_CHECK(has((*changes)[0], "'Nova Lontra'"));
    OTTER_CHECK(has((*changes)[0], "999"));
    OTTER_CHECK(!has((*changes)[0], "cliente_id"));
}

OTTER_TEST(edit_insert_distinguishes_blank_from_explicit_null) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400},
         {"email",      DataKind::string,  16400}},
        {{"7", "x", "y"}});

    EditBuffer buffer;
    const std::size_t novo = buffer.add_row();
    buffer.set_new_value(novo, 1, "Lontra");
    buffer.set_new_null(novo, 2);   // NULL de proposito

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());

    // email entra como NULL explicito; cliente_id, nao preenchido, fica fora.
    OTTER_CHECK(has((*changes)[0], "email"));
    OTTER_CHECK(has((*changes)[0], "NULL"));
    OTTER_CHECK(!has((*changes)[0], "cliente_id"));
}

OTTER_TEST(edit_ignores_a_blank_new_row) {
    // Clicar em "nova linha" e desistir nao deve gerar INSERT vazio.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "x"}});

    EditBuffer buffer;
    buffer.add_row();

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());
    OTTER_CHECK(changes->empty());
}

OTTER_TEST(edit_orders_insert_before_update_before_delete) {
    // A ordem importa: uma linha nova pode referenciar algo que a exclusao
    // removeria, e a ordem inversa violaria a chave estrangeira.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"7", "a"}, {"8", "b"}, {"9", "c"}});

    EditBuffer buffer;
    buffer.set(0, 1, "alterado");
    buffer.mark_deleted(1);
    const std::size_t novo = buffer.add_row();
    buffer.set_new_value(novo, 1, "inserido");

    const auto changes =
        generate_changes(rs, find_edit_target(rs, catalog_with_pk()), buffer);
    OTTER_CHECK(changes.has_value());
    OTTER_CHECK_EQ(changes->size(), std::size_t{3});

    OTTER_CHECK(has((*changes)[0], "INSERT"));
    OTTER_CHECK(has((*changes)[1], "UPDATE"));
    OTTER_CHECK(has((*changes)[2], "DELETE"));
}

OTTER_TEST(edit_buffer_counts_every_kind_of_change) {
    EditBuffer buffer;
    OTTER_CHECK(!buffer.has_changes());

    buffer.set(0, 1, "x");
    OTTER_CHECK(buffer.has_changes());
    OTTER_CHECK_EQ(buffer.change_count(), std::size_t{1});

    buffer.mark_deleted(5);
    buffer.add_row();
    OTTER_CHECK_EQ(buffer.change_count(), std::size_t{3});

    buffer.clear();
    // clear() precisa limpar TUDO, nao so' as alteracoes de celula: um
    // "Descartar" que deixa exclusoes pendentes seria pior que nenhum.
    OTTER_CHECK(!buffer.has_changes());
}

OTTER_TEST(edit_touched_rows_counts_deletes_and_inserts) {
    // "1 alteracao em 0 linhas" era o que a barra mostrava ao inserir --
    // touched_rows() so' contava as celulas alteradas.
    EditBuffer buffer;
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{0});

    buffer.add_row();
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{1});

    buffer.mark_deleted(3);
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{2});

    // Duas celulas da MESMA linha contam como uma linha so'.
    buffer.set(7, 1, "a");
    buffer.set(7, 2, "b");
    OTTER_CHECK_EQ(buffer.touched_rows(), std::size_t{3});
}

// --- Copiar de uma linha vizinha ------------------------------------------

OTTER_TEST(edit_copies_cell_from_another_row) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"cidade",     DataKind::string,  16400}},
        {{"1", "Curitiba"}, {"2", "Londrina"}, {"3", "Maringá"}});

    EditBuffer buffer;
    buffer.copy_cell_from(rs, 0, 1, 1);   // "de cima" para a linha do meio

    const CellEdit* edit = buffer.find(1, 1);
    OTTER_CHECK(edit != nullptr);
    OTTER_CHECK(edit->value == "Curitiba");
    OTTER_CHECK(!edit->is_null);
}

OTTER_TEST(edit_copy_from_row_prefers_the_pending_edit) {
    // A origem foi editada e ainda nao gravada. Copiar o valor do BANCO faria
    // aparecer na tela um valor que nao esta' em lugar nenhum -- nem no banco,
    // porque a edicao esta' pendente, nem na origem, que mostra outro.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"cidade",     DataKind::string,  16400}},
        {{"1", "Curitiba"}, {"2", "Londrina"}});

    EditBuffer buffer;
    buffer.set(0, 1, "Ponta Grossa");
    buffer.copy_cell_from(rs, 0, 1, 1);

    const CellEdit* edit = buffer.find(1, 1);
    OTTER_CHECK(edit != nullptr);
    OTTER_CHECK(edit->value == "Ponta Grossa");
}

OTTER_TEST(edit_copy_from_row_carries_null_as_null) {
    // Um NULL copiado como string vazia seria outro valor: no banco os dois
    // sao distintos, e a diferenca aparece em qualquer WHERE.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"cidade",     DataKind::string,  16400}},
        {{"1", ""}, {"2", "Londrina"}}, /*nulls=*/{{0, 1}});

    EditBuffer buffer;
    buffer.copy_cell_from(rs, 0, 1, 1);

    const CellEdit* edit = buffer.find(1, 1);
    OTTER_CHECK(edit != nullptr);
    OTTER_CHECK(edit->is_null);
}

OTTER_TEST(edit_copy_from_row_ignores_out_of_range_and_self) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"cidade",     DataKind::string,  16400}},
        {{"1", "Curitiba"}, {"2", "Londrina"}});

    EditBuffer buffer;

    // Fora do resultado: a linha acima da primeira da PAGINA existe no banco,
    // mas nao esta' carregada. Inventar um valor dela seria pior que nao fazer
    // nada.
    buffer.copy_cell_from(rs, 99, 0, 1);
    buffer.copy_cell_from(rs, 0, 99, 1);
    buffer.copy_cell_from(rs, 0, 1, 99);   // coluna inexistente
    buffer.copy_cell_from(rs, 1, 1, 1);    // de si mesma

    OTTER_CHECK(buffer.empty());
}

// --- Reverter (a tecla Esc da grade) ---------------------------------------

OTTER_TEST(edit_revert_removes_only_that_cell) {
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"1", "Alfa"}, {"2", "Beta"}});

    EditBuffer buffer;
    buffer.set(0, 1, "Gama");
    buffer.set(1, 1, "Delta");

    buffer.revert(0, 1);

    OTTER_CHECK(buffer.find(0, 1) == nullptr);
    OTTER_CHECK(buffer.find(1, 1) != nullptr);   // a outra sobrevive
}

OTTER_TEST(edit_unmark_deleted_is_what_esc_means_on_a_deleted_row) {
    // Na linha marcada para exclusao, reverter a CELULA nao diria nada -- a
    // linha inteira e' que esta' pendente. Desfazer a marca e' o que o
    // usuario quer dizer com Esc ali, e e' o que a grade faz.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"1", "Alfa"}});

    EditBuffer buffer;
    buffer.mark_deleted(0);
    OTTER_CHECK(buffer.is_deleted(0));
    OTTER_CHECK(buffer.has_changes());

    buffer.unmark_deleted(0);
    OTTER_CHECK(!buffer.is_deleted(0));
    OTTER_CHECK(!buffer.has_changes());
}

OTTER_TEST(edit_revert_on_an_untouched_cell_does_nothing) {
    // Esc numa celula intacta nao deve criar entrada nenhuma no buffer: a
    // barra passaria a dizer "1 alteracao nao salva" sem nada ter mudado.
    const ResultSet rs = make_result(
        {{"cliente_id", DataKind::integer, 16400},
         {"nome",       DataKind::string,  16400}},
        {{"1", "Alfa"}});

    EditBuffer buffer;
    buffer.revert(0, 1);

    OTTER_CHECK(buffer.empty());
    OTTER_CHECK(!buffer.has_changes());
}
