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

enum class ObjKind : std::uint8_t {
    database, schema, table, view, materialized_view, column,
    index, primary_key, foreign_key, sequence, function, trigger,
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
};

struct TableMeta {
    std::string  name;
    ObjKind      kind = ObjKind::table;
    std::string  comment;
    std::int64_t estimated_rows = 0;
    std::string  size_pretty;

    // Carregamento tardio: navegar ate' uma tabela nao pode disparar a leitura
    // do catalogo inteiro.
    std::vector<ColumnMeta> columns;
    bool columns_loaded = false;
};

struct SchemaMeta {
    std::string            name;
    std::vector<TableMeta> tables;
    bool                   tables_loaded = false;
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

    [[nodiscard]] ServerVersion version() const noexcept { return version_; }

private:
    Holt&         holt_;
    ServerVersion version_;
};

} // namespace otter::db
