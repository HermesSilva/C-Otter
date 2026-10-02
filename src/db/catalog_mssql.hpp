// C-Otter -- db/catalog_mssql.hpp
//
// Leitura de metadados do SQL Server. Mapa em docs/MSSQL-MAP.md.
//
// A mesma forma do PostgresCatalog e do MysqlCatalog, e as mesmas estruturas
// de db/catalog.hpp. O que e' proprio daqui:
//
//   1. Tres niveis, como no PostgreSQL: servidor -> banco -> schema. Cada
//      banco e' navegado por uma sessao propria (o banco vai no LOGIN7), como
//      no ADR 0018.
//
//   2. As views de catalogo (`sys.*`) sao do banco CORRENTE: nao ha' como ler
//      as tabelas de outro banco sem trocar de contexto.
//
//   3. Nao ha' OID que o resultado de uma consulta carregue: a origem de uma
//      coluna e' identificada por (schema, tabela), como no MySQL.
//
//   4. Comentario de objeto e' uma "extended property" chamada MS_Description
//      -- convencao do SSMS que o DBeaver tambem segue.
//
// As consultas valem do SQL Server 2012 (11.x) em diante: nada de STRING_AGG
// (2017), as listas de colunas saem de FOR XML PATH.
#pragma once

#include "db/catalog.hpp"

namespace otter::db {

class MssqlCatalog {
public:
    explicit MssqlCatalog(Holt& holt);

    // Os bancos do servidor. `unavailable` inclui os que estao offline ou sem
    // acesso para o login; `templates` nao tem significado aqui (o SQL Server
    // tem o `model`, que e' um banco como os outros).
    [[nodiscard]] Result<std::vector<DatabaseMeta>> load_databases(bool templates,
                                                                   bool unavailable);

    // Os schemas do banco corrente. Os de papel fixo (db_owner, db_datareader...)
    // e os de sistema ficam de fora, como no DBeaver sem "Show All Schemas".
    [[nodiscard]] Result<std::vector<SchemaMeta>> load_schemas();

    [[nodiscard]] Result<std::vector<TableMeta>>  load_tables(std::string_view schema);
    [[nodiscard]] Result<std::vector<ColumnMeta>> load_columns(std::string_view schema,
                                                               std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema);
    [[nodiscard]] Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<IndexMeta>> load_indexes(std::string_view schema,
                                                              std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<TriggerMeta>> load_triggers(std::string_view schema,
                                                                 std::string_view table);
    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(std::string_view schema);
    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(std::string_view schema);
    [[nodiscard]] Result<std::vector<DataTypeMeta>> load_types(std::string_view schema);

    [[nodiscard]] Result<std::string> load_routine_definition(std::string_view schema,
                                                              std::string_view name,
                                                              std::string_view arguments);
    [[nodiscard]] Result<std::string> load_view_definition(std::string_view schema,
                                                           std::string_view name);

    // O SQL Server tem particionamento, mas por FUNCAO e ESQUEMA de particao,
    // nao por tabela-filha: ainda nao lido. Vazio = a pasta nao aparece.
    [[nodiscard]] Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table);
    // Nao ha' evento agendado no servidor (e' o SQL Server Agent, no msdb).
    [[nodiscard]] Result<std::vector<EventMeta>> load_events(std::string_view schema);

    // As listas genericas: indices do schema, parametros de rotina,
    // dependencias, papeis, sinonimos, triggers de banco, logins.
    [[nodiscard]] Result<std::vector<CatalogItem>> load_list(CatalogList list,
                                                             std::string_view a,
                                                             std::string_view b,
                                                             std::string_view c);

    [[nodiscard]] ServerVersion version() const noexcept { return version_; }

private:
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> foreign_keys_where(
        const std::string& condition);

    Holt&         holt_;
    ServerVersion version_;
};

// [nome], com o colchete de fechamento dobrado.
[[nodiscard]] std::string mssql_quote(std::string_view identifier);

// N'texto', com a aspa dobrada. O N importa: sem ele um nome fora da pagina de
// codigo do banco viraria '?'.
[[nodiscard]] std::string mssql_literal(std::string_view text);

// OBJECT_ID(N'[schema].[nome]') -- como as consultas de catalogo acham o objeto.
[[nodiscard]] std::string mssql_object_id(std::string_view schema, std::string_view name);

// "varchar(50)", "nvarchar(max)", "decimal(10,2)", "datetime2(3)": o tipo como
// se escreve num CREATE TABLE, a partir do que sys.columns guarda.
// `max_length` e' em BYTES (o dobro dos caracteres em nchar/nvarchar).
[[nodiscard]] std::string mssql_type_text(std::string_view type, int max_length,
                                          int precision, int scale);

[[nodiscard]] DataKind mssql_kind(std::string_view type) noexcept;

} // namespace otter::db
