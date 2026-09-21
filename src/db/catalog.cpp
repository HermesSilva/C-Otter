#include "db/catalog.hpp"

#include <charconv>
#include <string>

namespace otter::db {
namespace {

// Escapa um literal SQL dobrando as aspas simples. Nomes de schema vem do
// proprio catalogo, mas nunca confie em entrada ao montar SQL.
std::string quote_literal(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('\'');
    for (char c : text) {
        if (c == '\'') out.push_back('\'');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::int64_t to_int64(std::string_view text) {
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Mapeia o nome do tipo do PostgreSQL para nossa categoria logica.
DataKind kind_from_type_name(std::string_view name) noexcept {
    if (name == "boolean" || name == "bool") return DataKind::boolean;
    if (name.starts_with("int") || name == "smallint" || name == "bigint" ||
        name == "serial" || name == "bigserial" || name == "oid") {
        return DataKind::integer;
    }
    if (name == "real" || name == "double precision" || name.starts_with("float")) {
        return DataKind::floating;
    }
    if (name.starts_with("numeric") || name.starts_with("decimal") ||
        name == "money") {
        return DataKind::numeric;
    }
    if (name == "bytea") return DataKind::binary;
    if (name == "date") return DataKind::date;
    if (name.starts_with("time without") || name.starts_with("time with")) {
        return DataKind::time;
    }
    if (name.starts_with("timestamp")) return DataKind::timestamp;
    if (name == "interval") return DataKind::interval;
    if (name == "uuid") return DataKind::uuid;
    if (name == "json" || name == "jsonb") return DataKind::json;
    if (name.ends_with("[]")) return DataKind::array;
    return DataKind::string;
}

} // namespace

std::string_view to_string(ObjKind kind) noexcept {
    switch (kind) {
        case ObjKind::database:          return "database";
        case ObjKind::schema:            return "schema";
        case ObjKind::table:             return "table";
        case ObjKind::view:              return "view";
        case ObjKind::materialized_view: return "materialized view";
        case ObjKind::column:            return "column";
        case ObjKind::index:             return "index";
        case ObjKind::primary_key:       return "primary key";
        case ObjKind::foreign_key:       return "foreign key";
        case ObjKind::sequence:          return "sequence";
        case ObjKind::function:          return "function";
        case ObjKind::trigger:           return "trigger";
    }
    return "object";
}

ServerVersion ServerVersion::parse(std::string_view text) {
    // Formatos: "18.2", "9.6.24", "13.4 (Debian ...)".
    ServerVersion version;

    const auto first_dot = text.find('.');
    std::from_chars(text.data(),
                    text.data() + std::min(first_dot, text.size()),
                    version.major);

    if (first_dot != std::string_view::npos) {
        const std::string_view rest = text.substr(first_dot + 1);
        const auto end = rest.find_first_not_of("0123456789");
        std::from_chars(rest.data(),
                        rest.data() + std::min(end, rest.size()),
                        version.minor);
    }
    return version;
}

PostgresCatalog::PostgresCatalog(Holt& holt)
    : holt_(holt), version_(ServerVersion::parse(holt.server_version())) {}

Result<std::vector<SchemaMeta>> PostgresCatalog::load_schemas() {
    // Esconde os schemas do sistema: pg_catalog, information_schema, pg_toast.
    constexpr const char* kSql = R"(
        SELECT nspname
          FROM pg_namespace
         WHERE nspname NOT LIKE 'pg\_%'
           AND nspname <> 'information_schema'
         ORDER BY nspname
    )";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(kSql));

    std::vector<SchemaMeta> schemas;
    schemas.reserve(rs.row_count());
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        SchemaMeta schema;
        schema.name = std::string(rs.text(r, 0));
        schemas.push_back(std::move(schema));
    }
    return schemas;
}

Result<std::vector<TableMeta>> PostgresCatalog::load_tables(std::string_view schema) {
    // relkind: r=tabela, v=view, m=view materializada, p=tabela particionada.
    //
    // 'p' (particionada) so' existe a partir do PostgreSQL 10; incluir a letra
    // na lista e' inofensivo em versoes antigas, que simplesmente nao a usam
    // (ADR 0010).
    const std::string sql =
        "SELECT c.relname,"
        "       c.relkind,"
        "       COALESCE(obj_description(c.oid, 'pg_class'), ''),"
        "       c.reltuples::bigint,"
        "       pg_size_pretty(pg_total_relation_size(c.oid))"
        "  FROM pg_class c"
        "  JOIN pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = " + quote_literal(schema) +
        "   AND c.relkind IN ('r', 'v', 'm', 'p')"
        " ORDER BY c.relname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<TableMeta> tables;
    tables.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        TableMeta table;
        table.name    = std::string(rs.text(r, 0));
        table.comment = std::string(rs.text(r, 2));

        const std::string_view relkind = rs.text(r, 1);
        if      (relkind == "v") table.kind = ObjKind::view;
        else if (relkind == "m") table.kind = ObjKind::materialized_view;
        else                     table.kind = ObjKind::table;

        // reltuples = -1 significa "nunca analisada", nao "vazia".
        const std::int64_t estimate = to_int64(rs.text(r, 3));
        table.estimated_rows = estimate < 0 ? 0 : estimate;
        table.size_pretty    = std::string(rs.text(r, 4));

        tables.push_back(std::move(table));
    }
    return tables;
}

Result<std::vector<ColumnMeta>> PostgresCatalog::load_columns(std::string_view schema,
                                                              std::string_view table) {
    // format_type() devolve o tipo como o usuario o escreveria: "varchar(80)",
    // "numeric(12,2)" -- melhor que remontar a partir de typname e typmod.
    const std::string sql =
        "SELECT a.attname,"
        "       format_type(a.atttypid, a.atttypmod),"
        "       a.attnotnull,"
        "       COALESCE(pg_get_expr(d.adbin, d.adrelid), ''),"
        "       COALESCE(col_description(c.oid, a.attnum), ''),"
        "       a.attnum,"
        "       COALESCE((SELECT true"
        "                   FROM pg_constraint pk"
        "                  WHERE pk.conrelid = c.oid"
        "                    AND pk.contype = 'p'"
        "                    AND a.attnum = ANY(pk.conkey)), false)"
        "  FROM pg_attribute a"
        "  JOIN pg_class c ON c.oid = a.attrelid"
        "  JOIN pg_namespace n ON n.oid = c.relnamespace"
        "  LEFT JOIN pg_attrdef d ON d.adrelid = c.oid AND d.adnum = a.attnum"
        " WHERE n.nspname = " + quote_literal(schema) +
        "   AND c.relname = " + quote_literal(table) +
        "   AND a.attnum > 0"          // colunas de sistema tem attnum negativo
        "   AND NOT a.attisdropped"    // colunas removidas permanecem no catalogo
        " ORDER BY a.attnum";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<ColumnMeta> columns;
    columns.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        ColumnMeta column;
        column.name          = std::string(rs.text(r, 0));
        column.type_name     = std::string(rs.text(r, 1));
        column.nullable      = rs.text(r, 2) != "t";
        column.default_value = std::string(rs.text(r, 3));
        column.comment       = std::string(rs.text(r, 4));
        column.position      = static_cast<std::int32_t>(to_int64(rs.text(r, 5)));
        column.primary_key   = rs.text(r, 6) == "t";
        column.kind          = kind_from_type_name(column.type_name);
        columns.push_back(std::move(column));
    }
    return columns;
}

Result<std::vector<ForeignKeyMeta>> PostgresCatalog::load_foreign_keys(
    std::string_view schema) {
    // Alimenta a inferencia de JOIN do completion (ADR 0004, camada 4).
    //
    // conkey[1] e confkey[1] pegam a PRIMEIRA coluna da chave: FKs compostas
    // aparecem so' pela coluna inicial. Suficiente para sugerir o JOIN; o
    // tratamento completo entra quando a camada 4 for implementada de fato.
    const std::string sql =
        "SELECT con.conname,"
        "       src.relname,"
        "       sa.attname,"
        "       tgt.relname,"
        "       ta.attname"
        "  FROM pg_constraint con"
        "  JOIN pg_class src ON src.oid = con.conrelid"
        "  JOIN pg_class tgt ON tgt.oid = con.confrelid"
        "  JOIN pg_namespace n ON n.oid = src.relnamespace"
        "  JOIN pg_attribute sa ON sa.attrelid = con.conrelid"
        "                      AND sa.attnum = con.conkey[1]"
        "  JOIN pg_attribute ta ON ta.attrelid = con.confrelid"
        "                      AND ta.attnum = con.confkey[1]"
        " WHERE con.contype = 'f'"
        "   AND n.nspname = " + quote_literal(schema) +
        " ORDER BY src.relname, sa.attname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<ForeignKeyMeta> keys;
    keys.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        keys.push_back(ForeignKeyMeta{
            std::string(rs.text(r, 0)),
            std::string(rs.text(r, 1)),
            std::string(rs.text(r, 2)),
            std::string(rs.text(r, 3)),
            std::string(rs.text(r, 4)),
        });
    }
    return keys;
}

} // namespace otter::db
