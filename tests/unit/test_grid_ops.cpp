// C-Otter -- testes de db/grid_ops: o que a grade faz com uma selecao.
//
// Cada funcao aqui tem um jeito errado que nao da' erro: copiar o valor do
// banco quando ha' edicao pendente, gerar DELETE com metade da chave, somar
// uma coluna que tem texto. Na tela isso so' apareceria com o dado errado ja'
// colado em algum lugar.
#include "test_main.hpp"

#include "db/ddl.hpp"
#include "db/grid_ops.hpp"

#include <string>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

// pedido(id, cliente_id, total, obs), vindo da tabela de OID 20.
ResultSet orders() {
    ResultSetBuilder builder;
    const auto column = [&builder](const char* name, DataKind kind,
                                   std::uint32_t oid = 20) {
        ColumnInfo info;
        info.name             = name;
        info.kind             = kind;
        info.source_table_oid = oid;
        builder.add_column(std::move(info));
    };
    column("id", DataKind::integer);
    column("cliente_id", DataKind::integer);
    column("total", DataKind::numeric);
    column("obs", DataKind::string);

    const char* rows[][4] = {
        {"1", "7", "10.50", "primeiro"},
        {"2", "7", "20",    nullptr},
        {"3", "9", "5.25",  "O'Brien"},
    };
    for (const auto& row : rows) {
        for (std::size_t c = 0; c < 4; ++c) {
            if (row[c] == nullptr) builder.append_null(c);
            else                   builder.append_text(c, row[c]);
        }
    }
    builder.set_row_count(3);
    return builder.take();
}

EditTarget target_with_key() {
    EditTarget target;
    target.schema      = "public";
    target.table       = "pedido";
    target.key_columns = {0};
    return target;
}

GridSelection select(std::size_t first, std::size_t last,
                     std::vector<std::size_t> columns) {
    GridSelection selection;
    selection.row_first = first;
    selection.row_last  = last;
    selection.columns   = std::move(columns);
    return selection;
}

} // namespace

// --- Copiar ------------------------------------------------------------------------

OTTER_TEST(grid_single_cell_copies_just_the_value) {
    const ResultSet rs = orders();
    OTTER_CHECK_EQ(selection_to_text(rs, {}, select(2, 2, {3})), std::string{"O'Brien"});
}

OTTER_TEST(grid_block_copies_as_tab_separated_rows) {
    const ResultSet rs = orders();
    // NULL sai vazio, e a ordem das colunas e' a da selecao (a da tela).
    OTTER_CHECK_EQ(selection_to_text(rs, {}, select(0, 1, {3, 0})),
                   std::string{"primeiro\t1\n\t2"});
    OTTER_CHECK_EQ(selection_to_text(rs, {}, select(0, 0, {0, 2}), /*header=*/true),
                   std::string{"id\ttotal\n1\t10.50"});
}

OTTER_TEST(grid_copy_takes_the_pending_edit_not_the_stored_value) {
    const ResultSet rs = orders();
    EditBuffer edits;
    edits.set(0, 2, "99");
    edits.set_null(2, 3);

    OTTER_CHECK_EQ(selection_to_text(rs, edits, select(0, 0, {2})), std::string{"99"});
    OTTER_CHECK(cell_value(rs, edits, 2, 3).is_null);

    // "Set to default" ainda nao tem valor na tela.
    edits.set_default(1, 2);
    OTTER_CHECK(cell_value(rs, edits, 1, 2).is_null);
}

OTTER_TEST(grid_slice_keeps_types_for_the_exporters) {
    const ResultSet rs = orders();
    const ResultSet part = slice(rs, {}, select(1, 2, {2, 3}));
    OTTER_CHECK_EQ(part.row_count(), std::size_t{2});
    OTTER_CHECK_EQ(part.column_count(), std::size_t{2});
    OTTER_CHECK(part.column(0).info().kind == DataKind::numeric);
    OTTER_CHECK(part.is_null(0, 1));
    OTTER_CHECK_EQ(std::string(part.text(1, 1)), std::string{"O'Brien"});
}

// --- Colar -------------------------------------------------------------------------

OTTER_TEST(grid_clipboard_splits_rows_and_cells) {
    const auto rows = parse_clipboard("a\tb\r\nc\t\r\n");
    OTTER_CHECK_EQ(rows.size(), std::size_t{2});
    OTTER_CHECK_EQ(rows[0].size(), std::size_t{2});
    OTTER_CHECK_EQ(rows[0][1], std::string{"b"});
    // A celula vazia depois do tab existe; a linha vazia do fim nao.
    OTTER_CHECK_EQ(rows[1].size(), std::size_t{2});
    OTTER_CHECK(rows[1][1].empty());

    OTTER_CHECK_EQ(parse_clipboard("so um valor").size(), std::size_t{1});
    OTTER_CHECK(parse_clipboard("").empty());

    // Linha vazia NO MEIO e' uma linha.
    OTTER_CHECK_EQ(parse_clipboard("a\n\nb").size(), std::size_t{3});
}

// --- Script das linhas -------------------------------------------------------------

OTTER_TEST(grid_row_script_generates_one_statement_per_row) {
    set_sql_dialect(QuoteStyle::double_quotes);
    const ResultSet rs = orders();
    const EditTarget target = target_with_key();

    const auto deletes = row_script(rs, {}, target, select(0, 1, {0}),
                                    RowScript::delete_by_key);
    OTTER_CHECK(deletes.has_value());
    OTTER_CHECK(has(*deletes, "DELETE FROM public.pedido\n WHERE id = 1;"));
    OTTER_CHECK(has(*deletes, "WHERE id = 2;"));

    const auto insert = row_script(rs, {}, target, select(2, 2, {0}), RowScript::insert);
    OTTER_CHECK(insert.has_value());
    OTTER_CHECK(has(*insert, "INSERT INTO public.pedido (id, cliente_id, total, obs)"));
    OTTER_CHECK(has(*insert, "VALUES (3, 9, 5.25, 'O''Brien');"));

    // UPDATE: a chave fica so' no WHERE, e NULL sai como NULL.
    const auto update = row_script(rs, {}, target, select(1, 1, {0}), RowScript::update);
    OTTER_CHECK(update.has_value());
    OTTER_CHECK(has(*update, "SET cliente_id = 7"));
    OTTER_CHECK(has(*update, "obs = NULL"));
    OTTER_CHECK(!has(*update, "SET id ="));
    OTTER_CHECK(has(*update, "WHERE id = 2;"));
}

OTTER_TEST(grid_row_script_without_a_key_refuses_everything_but_insert) {
    const ResultSet rs = orders();
    EditTarget target = target_with_key();
    target.key_columns.clear();
    target.refusal = EditRefusal::no_key;

    // Sem chave, um DELETE gerado alcancaria linhas demais.
    OTTER_CHECK(!row_script(rs, {}, target, select(0, 0, {0}),
                            RowScript::delete_by_key).has_value());
    OTTER_CHECK(!row_script(rs, {}, target, select(0, 0, {0}),
                            RowScript::update).has_value());
    OTTER_CHECK(row_script(rs, {}, target, select(0, 0, {0}),
                           RowScript::insert).has_value());
}

// --- Calculo -----------------------------------------------------------------------

OTTER_TEST(grid_stats_sum_a_numeric_selection) {
    const ResultSet rs = orders();
    const SelectionStats stats = selection_stats(rs, {}, select(0, 2, {2}));
    OTTER_CHECK_EQ(stats.cells, std::size_t{3});
    OTTER_CHECK(stats.numeric);
    OTTER_CHECK(stats.sum > 35.74 && stats.sum < 35.76);
    OTTER_CHECK_EQ(stats.minimum, std::string{"5.25"});
    OTTER_CHECK_EQ(stats.maximum, std::string{"20"});
    OTTER_CHECK_EQ(stats.distinct, std::size_t{3});
}

OTTER_TEST(grid_stats_do_not_sum_text) {
    const ResultSet rs = orders();
    // Coluna de texto com um NULL: conta, distingue, mas nao soma.
    const SelectionStats stats = selection_stats(rs, {}, select(0, 2, {3}));
    OTTER_CHECK(!stats.numeric);
    OTTER_CHECK_EQ(stats.nulls, std::size_t{1});
    OTTER_CHECK_EQ(stats.distinct, std::size_t{2});
    OTTER_CHECK_EQ(stats.minimum, std::string{"O'Brien"});

    // Misturar numero e texto tambem nao soma.
    OTTER_CHECK(!selection_stats(rs, {}, select(0, 0, {2, 3})).numeric);
}

// --- Distintos ---------------------------------------------------------------------

OTTER_TEST(grid_distinct_values_come_most_frequent_first) {
    const ResultSet rs = orders();
    const auto values = distinct_values(rs, 1);
    OTTER_CHECK_EQ(values.size(), std::size_t{2});
    OTTER_CHECK_EQ(values[0].text, std::string{"7"});
    OTTER_CHECK_EQ(values[0].count, std::size_t{2});

    const auto with_null = distinct_values(rs, 3);
    bool saw_null = false;
    for (const DistinctValue& value : with_null) saw_null |= value.is_null;
    OTTER_CHECK(saw_null);
}

OTTER_TEST(grid_distinct_query_wraps_the_statement) {
    const std::string sql = distinct_query("select * from pedido;", "cliente_id", 50);
    // O ';' do usuario nao pode ficar dentro da subconsulta.
    OTTER_CHECK(!has(sql, "pedido;"));
    OTTER_CHECK(has(sql, "SELECT \"cliente_id\", COUNT(*)"));
    OTTER_CHECK(has(sql, "GROUP BY 1"));
    OTTER_CHECK(has(sql, "LIMIT 50"));
}

OTTER_TEST(grid_equals_expression_follows_the_type) {
    const ResultSet rs = orders();
    OTTER_CHECK_EQ(equals_expression(rs.column(0).info(), "7", false), std::string{"= 7"});
    OTTER_CHECK_EQ(equals_expression(rs.column(3).info(), "O'Brien", false),
                   std::string{"= 'O''Brien'"});
    OTTER_CHECK_EQ(equals_expression(rs.column(3).info(), "", true), std::string{"IS NULL"});
}

// --- Chaves estrangeiras -----------------------------------------------------------

OTTER_TEST(grid_navigate_link_follows_the_foreign_key_of_the_column) {
    set_sql_dialect(QuoteStyle::double_quotes);
    const ResultSet rs = orders();

    TableMeta table;
    table.name = "pedido";
    ForeignKeyMeta key;
    key.source_table  = "pedido";
    key.source_column = "cliente_id";
    key.target_table  = "cliente";
    key.target_column = "id";
    key.target_schema = "crm";
    table.foreign_keys = {key};

    const LinkQuery link = navigate_link_query(rs, {}, table, "public", 2, 1);
    OTTER_CHECK_EQ(link.title, std::string{"cliente"});
    // O schema do DESTINO, nao o da tabela de origem.
    OTTER_CHECK(has(link.sql, "FROM crm.cliente"));
    OTTER_CHECK(has(link.sql, "WHERE id = 9"));

    // Coluna que nao e' de chave nenhuma: nao ha' para onde ir.
    OTTER_CHECK(navigate_link_query(rs, {}, table, "public", 0, 2).sql.empty());
}

OTTER_TEST(grid_navigate_link_uses_every_column_of_a_composite_key) {
    set_sql_dialect(QuoteStyle::double_quotes);
    const ResultSet rs = orders();

    TableMeta table;
    ForeignKeyMeta key;
    key.source_column = "cliente_id,id";
    key.target_table  = "saldo";
    key.target_column = "cliente,pedido";
    table.foreign_keys = {key};

    const LinkQuery link = navigate_link_query(rs, {}, table, "public", 0, 1);
    OTTER_CHECK(has(link.sql, "cliente = 7"));
    OTTER_CHECK(has(link.sql, "AND pedido = 1"));
}

OTTER_TEST(grid_references_list_the_tables_that_point_here) {
    set_sql_dialect(QuoteStyle::double_quotes);
    const ResultSet rs = orders();

    TableMeta table;
    ForeignKeyMeta item;
    item.source_table  = "item";
    item.source_column = "pedido_id";
    item.target_table  = "pedido";
    item.target_column = "id";

    ForeignKeyMeta other;   // aponta para uma coluna que NAO esta' no resultado
    other.source_table  = "nota";
    other.source_column = "pedido_codigo";
    other.target_column = "codigo";
    table.references = {item, other};

    const auto links = reference_queries(rs, {}, table, "public", 1);
    OTTER_CHECK_EQ(links.size(), std::size_t{1});
    OTTER_CHECK_EQ(links[0].title, std::string{"item (pedido_id)"});
    OTTER_CHECK(has(links[0].sql, "FROM public.item"));
    OTTER_CHECK(has(links[0].sql, "WHERE pedido_id = 2"));
}
