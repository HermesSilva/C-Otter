// C-Otter -- db/object_info_load.hpp
//
// Le' um objeto do servidor para o editor: propriedades, DDL, permissoes e
// estatisticas numa passagem.
//
// Separado de object_info.hpp porque aquele so' MONTA texto, e e' testado sem
// servidor; este executa, e depende dos dois leitores de catalogo.
#pragma once

#include "db/catalog.hpp"
#include "db/catalog_mysql.hpp"
#include "db/object_info.hpp"

namespace otter::db {

// Nao devolve Result: cada parte falha sozinha. Sem privilegio para ler as
// permissoes, as propriedades ainda aparecem -- e `error` diz o que faltou.
[[nodiscard]] ObjectInfo load_object_info(PostgresCatalog& catalog, Holt& holt,
                                          const ObjectRef& ref);

// MySQL e MariaDB: tabela, view, rotina, trigger e evento. Os demais tipos
// sao do PostgreSQL e voltam com `error` dizendo isso.
class MssqlCatalog;
[[nodiscard]] ObjectInfo load_object_info(MssqlCatalog& catalog, Holt& holt,
                                          const ObjectRef& ref);
// SQL Anywhere: o DDL de uma tabela vem pronto do servidor
// (sa_get_table_definition).
class SqlAnywhereCatalog;
[[nodiscard]] ObjectInfo load_object_info(SqlAnywhereCatalog& catalog, Holt& holt,
                                          const ObjectRef& ref);
[[nodiscard]] ObjectInfo load_object_info(MysqlCatalog& catalog, Holt& holt,
                                          const ObjectRef& ref);
// Oracle: propriedades de ALL_OBJECTS, DDL de DBMS_METADATA.GET_DDL. Somente
// leitura por enquanto.
class OracleCatalog;
[[nodiscard]] ObjectInfo load_object_info(OracleCatalog& catalog, Holt& holt,
                                          const ObjectRef& ref);

// O DDL de uma tabela do PostgreSQL, que nao tem pg_get_tabledef: colunas,
// constraints, chaves estrangeiras, indices e comentario, lidos do catalogo.
// `partition_by` vem das propriedades (pg_get_partkeydef), vazio se nao ha'.
[[nodiscard]] Result<std::string> load_pg_table_ddl(PostgresCatalog& catalog,
                                                    Holt& holt,
                                                    const ObjectRef& ref,
                                                    std::string_view partition_by,
                                                    std::string_view comment);

} // namespace otter::db
