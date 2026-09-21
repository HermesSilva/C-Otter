// C-Otter -- db/catalog.hpp
//
// Leitura de metadados do servidor. As consultas sao o ativo de maior valor
// por linha portado do DBeaver (docs/ANALYSIS.md secao 4), e declaram a faixa
// de versao em que valem (ADR 0010).
#pragma once

#include "db/holt.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace otter::db {

// Tipos de objeto da arvore. Subconjunto dos ~70 nos do DBeaver
// (ver docs/NAVIGATOR-TREE.md), na ordem de implementacao definida la'.
enum class ObjKind : std::uint8_t {
    database, schema,
    table, view, materialized_view, foreign_table, partitioned_table,
    column,
    index, constraint, primary_key, unique_key, check_constraint, foreign_key,
    sequence, function, procedure, trigger,
    data_type, extension, role, tablespace,
};

[[nodiscard]] std::string_view to_string(ObjKind kind) noexcept;

struct ColumnMeta {
    std::string   name;
    std::string   type_name;
    DataKind      kind = DataKind::unknown;
    bool          nullable = true;
    bool          primary_key = false;
    std::string   default_value;
    std::string   comment;
    std::int32_t  position = 0;
};

struct ForeignKeyMeta {
    std::string name;
    std::string source_table;
    std::string source_column;
    std::string target_table;
    std::string target_column;
    std::string on_update;      // NO ACTION, CASCADE, SET NULL...
    std::string on_delete;
    std::string definition;     // texto completo, para tooltip e DDL
};

// PRIMARY KEY, UNIQUE, CHECK e EXCLUDE. Foreign keys tem estrutura propria.
struct ConstraintMeta {
    std::string name;
    ObjKind     kind = ObjKind::constraint;
    std::string definition;     // pg_get_constraintdef
    std::string columns;        // lista separada por virgula
    bool        deferrable = false;
};

struct IndexMeta {
    std::string name;
    std::string definition;     // pg_get_indexdef
    std::string columns;
    std::string method;         // btree, hash, gin, gist...
    std::string size_pretty;
    bool        unique = false;
    bool        primary = false;
    bool        valid = true;   // indice invalido apos CREATE INDEX falho
};

struct SequenceMeta {
    std::string  name;
    std::int64_t last_value = 0;
    std::int64_t start_value = 1;
    std::int64_t increment = 1;
    std::int64_t min_value = 0;
    std::int64_t max_value = 0;
    bool         cycles = false;
    std::string  owned_by;       // tabela.coluna que a usa como default
    std::string  comment;
};

struct RoutineMeta {
    std::string name;
    ObjKind     kind = ObjKind::function;   // function ou procedure
    std::string arguments;       // assinatura formatada
    std::string return_type;
    std::string language;        // sql, plpgsql, c...
    std::string comment;
    std::string definition;      // corpo, carregado sob demanda
    bool        definition_loaded = false;
};

struct TriggerMeta {
    std::string name;
    std::string table;
    std::string timing;          // BEFORE, AFTER, INSTEAD OF
    std::string events;          // INSERT, UPDATE, DELETE
    std::string definition;
    bool        enabled = true;
};

struct TableMeta {
    std::string  name;
    ObjKind      kind = ObjKind::table;
    std::string  comment;
    std::int64_t estimated_rows = 0;
    std::string  size_pretty;

    // Carregamento tardio por pasta: expandir "Colunas" nao deve consultar
    // indices, e navegar ate' a tabela nao deve ler o catalogo inteiro.
    std::vector<ColumnMeta>     columns;
    std::vector<ConstraintMeta> constraints;
    std::vector<IndexMeta>      indexes;
    std::vector<ForeignKeyMeta> foreign_keys;
    std::vector<ForeignKeyMeta> references;   // FKs que apontam para ca'
    std::vector<TriggerMeta>    triggers;

    // Corpo da view (`pg_get_viewdef`). Vazio para tabelas; carregado sob
    // demanda, porque uma view de relatorio pode ter varios KB de SQL.
    std::string definition;

    bool columns_loaded     = false;
    bool constraints_loaded = false;
    bool indexes_loaded     = false;
    bool keys_loaded        = false;
    bool triggers_loaded    = false;
    bool definition_loaded  = false;

    [[nodiscard]] bool is_view() const noexcept {
        return kind == ObjKind::view || kind == ObjKind::materialized_view;
    }

    // Uma view nao tem constraints nem chaves estrangeiras -- o DBeaver nem
    // mostra as pastas. Uma materialized view tem indices, mas nao triggers.
    [[nodiscard]] bool has_constraints() const noexcept { return !is_view(); }
    [[nodiscard]] bool has_indexes() const noexcept {
        return kind != ObjKind::view;
    }
    [[nodiscard]] bool has_triggers() const noexcept {
        return kind != ObjKind::materialized_view;
    }
};

struct SchemaMeta {
    std::string               name;
    std::string               comment;
    std::string               owner;

    std::vector<TableMeta>    tables;
    std::vector<SequenceMeta> sequences;
    std::vector<RoutineMeta>  routines;

    bool tables_loaded    = false;
    bool sequences_loaded = false;
    bool routines_loaded  = false;
};

// Versao do servidor, para selecionar a consulta correta (ADR 0010).
struct ServerVersion {
    int major = 0;
    int minor = 0;

    [[nodiscard]] constexpr bool at_least(int m, int n = 0) const noexcept {
        return major > m || (major == m && minor >= n);
    }

    [[nodiscard]] static ServerVersion parse(std::string_view text);
};

// Leitor de catalogo do PostgreSQL.
class PostgresCatalog {
public:
    explicit PostgresCatalog(Holt& holt);

    [[nodiscard]] Result<std::vector<SchemaMeta>> load_schemas();
    [[nodiscard]] Result<std::vector<TableMeta>>  load_tables(std::string_view schema);
    [[nodiscard]] Result<std::vector<ColumnMeta>> load_columns(std::string_view schema,
                                                               std::string_view table);
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema);

    // --- Por tabela, carregados sob demanda ---------------------------------

    [[nodiscard]] Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<IndexMeta>> load_indexes(
        std::string_view schema, std::string_view table);

    // Foreign keys DESTA tabela.
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table);

    // Foreign keys de OUTRAS tabelas que apontam para esta. E' o que mais falta
    // num cliente SQL: responder "quem depende desta tabela?".
    [[nodiscard]] Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table);

    [[nodiscard]] Result<std::vector<TriggerMeta>> load_triggers(
        std::string_view schema, std::string_view table);

    // --- Por schema ----------------------------------------------------------

    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(
        std::string_view schema);

    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(
        std::string_view schema);

    // Corpo de uma funcao, carregado so' quando pedido: pode ter milhares de
    // linhas e raramente e' necessario.
    [[nodiscard]] Result<std::string> load_routine_definition(
        std::string_view schema, std::string_view name,
        std::string_view arguments);

    // Corpo de uma view (`pg_get_viewdef`), pelo mesmo motivo: uma view de
    // relatorio pode ter varios KB de SQL.
    [[nodiscard]] Result<std::string> load_view_definition(
        std::string_view schema, std::string_view name);

    [[nodiscard]] ServerVersion version() const noexcept { return version_; }

private:
    Holt&         holt_;
    ServerVersion version_;
};

} // namespace otter::db
