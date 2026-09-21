#include "test_main.hpp"

#include "db/result_set.hpp"

#include <string>

using namespace otter::db;

namespace {

ColumnInfo make_column(std::string name, DataKind kind) {
    ColumnInfo info;
    info.name = std::move(name);
    info.kind = kind;
    return info;
}

} // namespace

OTTER_TEST(result_set_stores_values_columnar) {
    ResultSetBuilder builder;
    builder.add_column(make_column("id", DataKind::integer));
    builder.add_column(make_column("name", DataKind::string));

    builder.append_text(0, "1");
    builder.append_text(1, "Nina");
    builder.append_text(0, "2");
    builder.append_text(1, "Oslo");
    builder.set_row_count(2);

    const ResultSet rs = builder.take();

    OTTER_CHECK_EQ(rs.column_count(), std::size_t{2});
    OTTER_CHECK_EQ(rs.row_count(), std::size_t{2});
    OTTER_CHECK_EQ(rs.text(0, 1), std::string_view{"Nina"});
    OTTER_CHECK_EQ(rs.text(1, 1), std::string_view{"Oslo"});
    OTTER_CHECK_EQ(rs.text(1, 0), std::string_view{"2"});
}

OTTER_TEST(result_set_distinguishes_null_from_empty_string) {
    // Confundir nulo com string vazia e' erro classico de cliente SQL.
    ResultSetBuilder builder;
    builder.add_column(make_column("comment", DataKind::string));

    builder.append_null(0);
    builder.append_text(0, "");
    builder.set_row_count(2);

    const ResultSet rs = builder.take();

    OTTER_CHECK(rs.is_null(0, 0));
    OTTER_CHECK(!rs.is_null(1, 0));
    OTTER_CHECK(rs.text(0, 0).empty());
    OTTER_CHECK(rs.text(1, 0).empty());
}

OTTER_TEST(result_set_nulls_consume_no_bytes) {
    // O ganho do formato colunar: uma coluna toda nula nao ocupa espaco.
    ResultSetBuilder builder;
    builder.add_column(make_column("empty", DataKind::string));

    for (int i = 0; i < 1000; ++i) builder.append_null(0);
    builder.set_row_count(1000);

    const ResultSet rs = builder.take();
    OTTER_CHECK_EQ(rs.column(0).bytes_used(), std::size_t{0});
    OTTER_CHECK_EQ(rs.row_count(), std::size_t{1000});
}

OTTER_TEST(result_set_find_column_is_case_insensitive) {
    ResultSetBuilder builder;
    builder.add_column(make_column("RaftName", DataKind::string));

    const ResultSet rs = builder.take();

    OTTER_CHECK(rs.find_column("raftname").has_value());
    OTTER_CHECK(rs.find_column("RAFTNAME").has_value());
    OTTER_CHECK_EQ(*rs.find_column("RaftName"), std::size_t{0});
    OTTER_CHECK(!rs.find_column("missing").has_value());
}

OTTER_TEST(result_set_tracks_affected_rows) {
    ResultSetBuilder builder;
    builder.set_affected_rows(7);
    const ResultSet rs = builder.take();

    OTTER_CHECK_EQ(rs.affected_rows(), std::int64_t{7});
    OTTER_CHECK_EQ(rs.column_count(), std::size_t{0});
}

OTTER_TEST(result_set_right_alignment_by_kind) {
    // Numeros alinham a direita; texto, a esquerda.
    OTTER_CHECK(is_right_aligned(DataKind::integer));
    OTTER_CHECK(is_right_aligned(DataKind::numeric));
    OTTER_CHECK(is_right_aligned(DataKind::floating));
    OTTER_CHECK(!is_right_aligned(DataKind::string));
    OTTER_CHECK(!is_right_aligned(DataKind::timestamp));
}
