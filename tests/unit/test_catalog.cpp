// Regras de quais pastas cada tipo de relacao tem na arvore.
//
// Por que existe: a arvore mostrava views misturadas com tabelas, com as
// mesmas pastas para as duas -- e uma view nao tem constraints nem chaves
// estrangeiras. Sao tres predicados de uma linha cada, faceis de inverter num
// refactor e invisiveis ate' alguem expandir a view certa.
//
// A fonte destas regras e' o plugin.xml do DBeaver
// (plugins/org.jkiss.dbeaver.ext.postgresql), elemento <tree>:
//
//   folderView        -> Columns, Dependencies, Triggers, Rules
//   MaterializedView  -> Columns, Indexes, Dependencies
//   Table             -> Columns, Constraints, FK, Indexes, References, ...
#include "test_main.hpp"

#include "db/catalog.hpp"

using namespace otter::db;

namespace {

TableMeta make(ObjKind kind) {
    TableMeta meta;
    meta.kind = kind;
    return meta;
}

} // namespace

OTTER_TEST(catalog_table_has_every_folder) {
    const TableMeta table = make(ObjKind::table);

    OTTER_CHECK(!table.is_view());
    OTTER_CHECK(table.has_constraints());
    OTTER_CHECK(table.has_indexes());
    OTTER_CHECK(table.has_triggers());
}

OTTER_TEST(catalog_view_has_no_constraints_or_indexes) {
    const TableMeta view = make(ObjKind::view);

    OTTER_CHECK(view.is_view());

    // Uma view nao tem constraints nem chaves estrangeiras: nao ha' linhas
    // proprias para restringir.
    OTTER_CHECK(!view.has_constraints());
    OTTER_CHECK(!view.has_indexes());

    // Mas aceita trigger INSTEAD OF, que e' como se escreve numa view.
    OTTER_CHECK(view.has_triggers());
}

OTTER_TEST(catalog_materialized_view_has_indexes_but_no_triggers) {
    const TableMeta mview = make(ObjKind::materialized_view);

    OTTER_CHECK(mview.is_view());
    OTTER_CHECK(!mview.has_constraints());

    // Indexar a materialized view e' justamente o que a distingue da view
    // comum -- ela tem linhas gravadas.
    OTTER_CHECK(mview.has_indexes());

    // E' atualizada por REFRESH, nao por DML: nao ha' evento para disparar.
    OTTER_CHECK(!mview.has_triggers());
}

OTTER_TEST(catalog_partitioned_table_behaves_like_a_table) {
    // relkind 'p' e' tabela para todos os efeitos da arvore; so' muda a forma
    // de armazenamento.
    const TableMeta partitioned = make(ObjKind::partitioned_table);

    OTTER_CHECK(!partitioned.is_view());
    OTTER_CHECK(partitioned.has_constraints());
    OTTER_CHECK(partitioned.has_indexes());
    OTTER_CHECK(partitioned.has_triggers());
}
