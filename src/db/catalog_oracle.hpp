// C-Otter -- db/catalog_oracle.hpp
//
// Leitura de metadados do Oracle. Mapa em docs/ORACLE-MAP.md.
//
// A mesma forma dos outros catalogos, e as mesmas estruturas de
// db/catalog.hpp. O que e' proprio daqui:
//
//   1. Nao ha' banco acima do schema, e schema e' USUARIO: a arvore e'
//      conexao -> schemas -> objetos, como no MySQL.
//
//   2. As consultas leem as views ALL_* do dicionario: o que a conta ENXERGA,
//      sem exigir privilegio de DBA. O custo e' nao ter o que so' as DBA_* e
//      as V$ mostram (tamanho em disco, sessoes) -- fica vazio, nao inventado.
//
//   3. Os schemas mantidos pela Oracle (SYS, SYSTEM, XDB, ... -- dezenas) ficam
//      de fora, salvo o da propria conexao: e' o que o DBeaver faz sem "Show
//      system objects". ALL_USERS.ORACLE_MAINTAINED existe desde o 12.1, que e'
//      o servidor mais velho que o protocolo aceita.
//
//   4. Nomes sem aspas sao guardados em MAIUSCULAS; o catalogo compara pelo
//      nome exato, como esta' guardado.
//
//   5. O resultado de uma consulta nao diz de que tabela a coluna veio.
#pragma once

#include "db/catalog.hpp"

namespace otter::db {

class OracleCatalog {
public:
    explicit OracleCatalog(Holt& holt);

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
    [[nodiscard]] Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table);
    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(std::string_view schema);
    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(std::string_view schema);
    [[nodiscard]] Result<std::vector<DataTypeMeta>> load_types(std::string_view schema);

    [[nodiscard]] Result<std::string> load_routine_definition(std::string_view schema,
                                                              std::string_view name,
                                                              std::string_view arguments);
    [[nodiscard]] Result<std::string> load_view_definition(std::string_view schema,
                                                           std::string_view name);

    // O agendador do Oracle (DBMS_SCHEDULER) ainda nao e' lido.
    [[nodiscard]] Result<std::vector<EventMeta>> load_events(std::string_view schema);

    // As listas genericas da arvore: so' os indices do schema, por enquanto.
    [[nodiscard]] Result<std::vector<CatalogItem>> load_list(CatalogList list,
                                                             std::string_view a,
                                                             std::string_view b,
                                                             std::string_view c);

    // O schema da conexao: onde a arvore comeca.
    [[nodiscard]] std::string user() const { return holt_.current_schema(); }

private:
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> foreign_keys_where(
        const std::string& condition);

    Holt& holt_;
};

// 'texto', com a aspa dobrada.
[[nodiscard]] std::string oracle_literal(std::string_view text);

// "VARCHAR2(30 CHAR)", "NUMBER(10,2)", "RAW(16)": o tipo como se escreve num
// CREATE TABLE, a partir do que ALL_TAB_COLS guarda. `precision` e `scale`
// negativos = nulos no dicionario (NUMBER sem precisao).
[[nodiscard]] std::string oracle_type_text(std::string_view type, int length, int precision,
                                           int scale, int char_length, bool char_semantics);

[[nodiscard]] DataKind oracle_kind(std::string_view type, int precision, int scale) noexcept;

} // namespace otter::db
