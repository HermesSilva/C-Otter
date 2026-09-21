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

// Escapa um IDENTIFICADOR entre aspas duplas, dobrando as que houver dentro.
//
// Diferente de quote_literal: sem as aspas duplas o PostgreSQL rebaixa o nome
// para minusculas, e uma tabela criada como "TIDxAcaoSensivel" deixa de ser
// encontrada. O ERP_TID usa esse estilo em todas as tabelas.
std::string quote_ident(std::string_view name) {
    std::string out;
    out.reserve(name.size() + 2);
    out.push_back('"');
    for (char c : name) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
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
        case ObjKind::foreign_table:     return "foreign table";
        case ObjKind::partitioned_table: return "partitioned table";
        case ObjKind::column:            return "column";
        case ObjKind::index:             return "index";
        case ObjKind::constraint:        return "constraint";
        case ObjKind::primary_key:       return "primary key";
        case ObjKind::unique_key:        return "unique";
        case ObjKind::check_constraint:  return "check";
        case ObjKind::foreign_key:       return "foreign key";
        case ObjKind::sequence:          return "sequence";
        case ObjKind::function:          return "function";
        case ObjKind::procedure:         return "procedure";
        case ObjKind::trigger:           return "trigger";
        case ObjKind::data_type:         return "type";
        case ObjKind::extension:         return "extension";
        case ObjKind::role:              return "role";
        case ObjKind::tablespace:        return "tablespace";
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
        else if (relkind == "f") table.kind = ObjKind::foreign_table;
        else if (relkind == "p") table.kind = ObjKind::partitioned_table;
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

Result<std::vector<ConstraintMeta>> PostgresCatalog::load_constraints(
    std::string_view schema, std::string_view table) {
    // contype: p=primary, u=unique, c=check, x=exclude. 'f' (foreign) tem
    // consulta propria, com origem e destino.
    //
    // pg_get_constraintdef devolve o texto exato que recriaria a constraint --
    // melhor que remontar a partir das colunas do catalogo.
    const std::string sql =
        "SELECT con.conname,"
        "       con.contype,"
        "       pg_get_constraintdef(con.oid),"
        "       COALESCE((SELECT string_agg(a.attname, ', ' ORDER BY k.ord)"
        "                   FROM unnest(con.conkey) WITH ORDINALITY AS k(attnum, ord)"
        "                   JOIN pg_attribute a ON a.attrelid = con.conrelid"
        "                                      AND a.attnum = k.attnum), ''),"
        "       con.condeferrable"
        "  FROM pg_constraint con"
        "  JOIN pg_class c ON c.oid = con.conrelid"
        "  JOIN pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = " + quote_literal(schema) +
        "   AND c.relname = " + quote_literal(table) +
        "   AND con.contype IN ('p', 'u', 'c', 'x')"
        " ORDER BY con.contype, con.conname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<ConstraintMeta> constraints;
    constraints.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        ConstraintMeta constraint;
        constraint.name       = std::string(rs.text(r, 0));
        constraint.definition = std::string(rs.text(r, 2));
        constraint.columns    = std::string(rs.text(r, 3));
        constraint.deferrable = rs.text(r, 4) == "t";

        const std::string_view type = rs.text(r, 1);
        if      (type == "p") constraint.kind = ObjKind::primary_key;
        else if (type == "u") constraint.kind = ObjKind::unique_key;
        else if (type == "c") constraint.kind = ObjKind::check_constraint;
        else                  constraint.kind = ObjKind::constraint;

        constraints.push_back(std::move(constraint));
    }
    return constraints;
}

Result<std::vector<IndexMeta>> PostgresCatalog::load_indexes(
    std::string_view schema, std::string_view table) {
    // indisvalid=false acontece quando CREATE INDEX CONCURRENTLY falha: o
    // indice existe mas nao e' usado pelo planejador. Sinalizar isso importa.
    const std::string sql =
        "SELECT i.relname,"
        "       pg_get_indexdef(idx.indexrelid),"
        "       am.amname,"
        "       pg_size_pretty(pg_relation_size(idx.indexrelid)),"
        "       idx.indisunique,"
        "       idx.indisprimary,"
        "       idx.indisvalid,"
        "       COALESCE((SELECT string_agg(a.attname, ', ' ORDER BY k.ord)"
        "                   FROM unnest(idx.indkey) WITH ORDINALITY AS k(attnum, ord)"
        "                   JOIN pg_attribute a ON a.attrelid = idx.indrelid"
        "                                      AND a.attnum = k.attnum), '')"
        "  FROM pg_index idx"
        "  JOIN pg_class i ON i.oid = idx.indexrelid"
        "  JOIN pg_class t ON t.oid = idx.indrelid"
        "  JOIN pg_namespace n ON n.oid = t.relnamespace"
        "  JOIN pg_am am ON am.oid = i.relam"
        " WHERE n.nspname = " + quote_literal(schema) +
        "   AND t.relname = " + quote_literal(table) +
        " ORDER BY idx.indisprimary DESC, i.relname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<IndexMeta> indexes;
    indexes.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        IndexMeta index;
        index.name        = std::string(rs.text(r, 0));
        index.definition  = std::string(rs.text(r, 1));
        index.method      = std::string(rs.text(r, 2));
        index.size_pretty = std::string(rs.text(r, 3));
        index.unique      = rs.text(r, 4) == "t";
        index.primary     = rs.text(r, 5) == "t";
        index.valid       = rs.text(r, 6) == "t";
        index.columns     = std::string(rs.text(r, 7));
        indexes.push_back(std::move(index));
    }
    return indexes;
}

namespace {

// Origem e destino de uma foreign key, com as acoes referenciais. Serve tanto
// para "FKs desta tabela" quanto para "FKs que apontam para ca'": muda apenas
// a clausula WHERE.
std::string foreign_key_query(std::string_view where_clause) {
    // confupdtype/confdeltype: a=NO ACTION, r=RESTRICT, c=CASCADE,
    // n=SET NULL, d=SET DEFAULT.
    return
        "SELECT con.conname,"
        "       src.relname,"
        "       sa.attname,"
        "       tgt.relname,"
        "       ta.attname,"
        "       CASE con.confupdtype WHEN 'c' THEN 'CASCADE'"
        "                            WHEN 'n' THEN 'SET NULL'"
        "                            WHEN 'd' THEN 'SET DEFAULT'"
        "                            WHEN 'r' THEN 'RESTRICT'"
        "                            ELSE 'NO ACTION' END,"
        "       CASE con.confdeltype WHEN 'c' THEN 'CASCADE'"
        "                            WHEN 'n' THEN 'SET NULL'"
        "                            WHEN 'd' THEN 'SET DEFAULT'"
        "                            WHEN 'r' THEN 'RESTRICT'"
        "                            ELSE 'NO ACTION' END,"
        "       pg_get_constraintdef(con.oid)"
        "  FROM pg_constraint con"
        "  JOIN pg_class src ON src.oid = con.conrelid"
        "  JOIN pg_class tgt ON tgt.oid = con.confrelid"
        "  JOIN pg_namespace sn ON sn.oid = src.relnamespace"
        "  JOIN pg_namespace tn ON tn.oid = tgt.relnamespace"
        "  JOIN pg_attribute sa ON sa.attrelid = con.conrelid"
        "                      AND sa.attnum = con.conkey[1]"
        "  JOIN pg_attribute ta ON ta.attrelid = con.confrelid"
        "                      AND ta.attnum = con.confkey[1]"
        " WHERE con.contype = 'f' AND " + std::string(where_clause) +
        " ORDER BY con.conname";
}

std::vector<ForeignKeyMeta> read_foreign_keys(const ResultSet& rs) {
    std::vector<ForeignKeyMeta> keys;
    keys.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        ForeignKeyMeta key;
        key.name          = std::string(rs.text(r, 0));
        key.source_table  = std::string(rs.text(r, 1));
        key.source_column = std::string(rs.text(r, 2));
        key.target_table  = std::string(rs.text(r, 3));
        key.target_column = std::string(rs.text(r, 4));
        key.on_update     = std::string(rs.text(r, 5));
        key.on_delete     = std::string(rs.text(r, 6));
        key.definition    = std::string(rs.text(r, 7));
        keys.push_back(std::move(key));
    }
    return keys;
}

} // namespace

Result<std::vector<ForeignKeyMeta>> PostgresCatalog::load_table_foreign_keys(
    std::string_view schema, std::string_view table) {
    const std::string where = "sn.nspname = " + quote_literal(schema) +
                              " AND src.relname = " + quote_literal(table);
    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(foreign_key_query(where)));
    return read_foreign_keys(rs);
}

Result<std::vector<ForeignKeyMeta>> PostgresCatalog::load_references(
    std::string_view schema, std::string_view table) {
    // Inverte o lado: FKs cujo DESTINO e' esta tabela.
    const std::string where = "tn.nspname = " + quote_literal(schema) +
                              " AND tgt.relname = " + quote_literal(table);
    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(foreign_key_query(where)));
    return read_foreign_keys(rs);
}

Result<std::vector<TriggerMeta>> PostgresCatalog::load_triggers(
    std::string_view schema, std::string_view table) {
    // tgtype e' um bitmask: bit 0 = ROW, bit 1 = BEFORE, bit 2 = INSERT,
    // bit 3 = DELETE, bit 4 = UPDATE, bit 6 = INSTEAD OF.
    // tgisinternal exclui os triggers que implementam foreign keys.
    const std::string sql =
        "SELECT tg.tgname,"
        "       CASE WHEN (tg.tgtype::int & 64) <> 0 THEN 'INSTEAD OF'"
        "            WHEN (tg.tgtype::int & 2)  <> 0 THEN 'BEFORE'"
        "            ELSE 'AFTER' END,"
        "       ARRAY_TO_STRING(ARRAY_REMOVE(ARRAY["
        "           CASE WHEN (tg.tgtype::int & 4)  <> 0 THEN 'INSERT' END,"
        "           CASE WHEN (tg.tgtype::int & 8)  <> 0 THEN 'DELETE' END,"
        "           CASE WHEN (tg.tgtype::int & 16) <> 0 THEN 'UPDATE' END,"
        "           CASE WHEN (tg.tgtype::int & 32) <> 0 THEN 'TRUNCATE' END"
        "       ], NULL), ', '),"
        "       pg_get_triggerdef(tg.oid),"
        "       tg.tgenabled <> 'D'"
        "  FROM pg_trigger tg"
        "  JOIN pg_class c ON c.oid = tg.tgrelid"
        "  JOIN pg_namespace n ON n.oid = c.relnamespace"
        " WHERE n.nspname = " + quote_literal(schema) +
        "   AND c.relname = " + quote_literal(table) +
        "   AND NOT tg.tgisinternal"
        " ORDER BY tg.tgname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<TriggerMeta> triggers;
    triggers.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        TriggerMeta trigger;
        trigger.name       = std::string(rs.text(r, 0));
        trigger.table      = std::string(table);
        trigger.timing     = std::string(rs.text(r, 1));
        trigger.events     = std::string(rs.text(r, 2));
        trigger.definition = std::string(rs.text(r, 3));
        trigger.enabled    = rs.text(r, 4) == "t";
        triggers.push_back(std::move(trigger));
    }
    return triggers;
}

Result<std::vector<SequenceMeta>> PostgresCatalog::load_sequences(
    std::string_view schema) {
    // pg_sequences existe desde o PostgreSQL 10; antes disso era preciso
    // consultar cada sequence individualmente (ADR 0010).
    if (!version_.at_least(10)) {
        const std::string legacy =
            "SELECT c.relname, 0::bigint, 1::bigint, 1::bigint,"
            "       0::bigint, 0::bigint, false, '',"
            "       COALESCE(obj_description(c.oid, 'pg_class'), '')"
            "  FROM pg_class c"
            "  JOIN pg_namespace n ON n.oid = c.relnamespace"
            " WHERE c.relkind = 'S' AND n.nspname = " + quote_literal(schema) +
            " ORDER BY c.relname";

        OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(legacy));

        std::vector<SequenceMeta> sequences;
        for (std::size_t r = 0; r < rs.row_count(); ++r) {
            SequenceMeta sequence;
            sequence.name    = std::string(rs.text(r, 0));
            sequence.comment = std::string(rs.text(r, 8));
            sequences.push_back(std::move(sequence));
        }
        return sequences;
    }

    const std::string sql =
        "SELECT s.sequencename,"
        "       COALESCE(s.last_value, 0),"
        "       s.start_value,"
        "       s.increment_by,"
        "       s.min_value,"
        "       s.max_value,"
        "       s.cycle,"
        "       COALESCE(("
        "           SELECT quote_ident(dt.relname) || '.' || quote_ident(da.attname)"
        "             FROM pg_depend d"
        "             JOIN pg_class dc ON dc.oid = d.objid"
        "             JOIN pg_class dt ON dt.oid = d.refobjid"
        "             JOIN pg_attribute da ON da.attrelid = d.refobjid"
        "                                 AND da.attnum = d.refobjsubid"
        "            WHERE dc.relname = s.sequencename"
        "              AND d.deptype = 'a' LIMIT 1), ''),"
        "       COALESCE(obj_description("
        "           (quote_ident(s.schemaname) || '.' ||"
        "            quote_ident(s.sequencename))::regclass, 'pg_class'), '')"
        "  FROM pg_sequences s"
        " WHERE s.schemaname = " + quote_literal(schema) +
        " ORDER BY s.sequencename";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<SequenceMeta> sequences;
    sequences.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        SequenceMeta sequence;
        sequence.name        = std::string(rs.text(r, 0));
        sequence.last_value  = to_int64(rs.text(r, 1));
        sequence.start_value = to_int64(rs.text(r, 2));
        sequence.increment   = to_int64(rs.text(r, 3));
        sequence.min_value   = to_int64(rs.text(r, 4));
        sequence.max_value   = to_int64(rs.text(r, 5));
        sequence.cycles      = rs.text(r, 6) == "t";
        sequence.owned_by    = std::string(rs.text(r, 7));
        sequence.comment     = std::string(rs.text(r, 8));
        sequences.push_back(std::move(sequence));
    }
    return sequences;
}

Result<std::vector<RoutineMeta>> PostgresCatalog::load_routines(
    std::string_view schema) {
    // prokind existe desde o PostgreSQL 11; antes, proisagg/proiswindow
    // distinguiam os tipos (ADR 0010).
    const std::string kind_expr =
        version_.at_least(11)
            ? "p.prokind"
            : "CASE WHEN p.proisagg THEN 'a' WHEN p.proiswindow THEN 'w' "
              "ELSE 'f' END";

    const std::string sql =
        "SELECT p.proname,"
        "       " + kind_expr + ","
        "       pg_get_function_arguments(p.oid),"
        "       pg_get_function_result(p.oid),"
        "       l.lanname,"
        "       COALESCE(obj_description(p.oid, 'pg_proc'), '')"
        "  FROM pg_proc p"
        "  JOIN pg_namespace n ON n.oid = p.pronamespace"
        "  JOIN pg_language l ON l.oid = p.prolang"
        " WHERE n.nspname = " + quote_literal(schema) +
        " ORDER BY p.proname";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));

    std::vector<RoutineMeta> routines;
    routines.reserve(rs.row_count());

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        RoutineMeta routine;
        routine.name        = std::string(rs.text(r, 0));
        routine.arguments   = std::string(rs.text(r, 2));
        routine.return_type = std::string(rs.text(r, 3));
        routine.language    = std::string(rs.text(r, 4));
        routine.comment     = std::string(rs.text(r, 5));

        // 'p' = procedure; 'f', 'a' (agregada) e 'w' (janela) sao funcoes.
        routine.kind = rs.text(r, 1) == "p" ? ObjKind::procedure
                                            : ObjKind::function;
        routines.push_back(std::move(routine));
    }
    return routines;
}

Result<std::string> PostgresCatalog::load_routine_definition(
    std::string_view schema, std::string_view name, std::string_view arguments) {
    // Identifica pela assinatura: sobrecargas compartilham o nome.
    const std::string signature =
        std::string(schema) + "." + std::string(name) +
        "(" + std::string(arguments) + ")";

    const std::string sql =
        "SELECT pg_get_functiondef(" + quote_literal(signature) + "::regprocedure)";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));
    if (rs.row_count() == 0) return std::string{};
    return std::string(rs.text(0, 0));
}

Result<std::string> PostgresCatalog::load_view_definition(
    std::string_view schema, std::string_view name) {
    // O segundo argumento pede a versao "pretty": quebras de linha e recuo.
    // Sem ele, pg_get_viewdef devolve tudo numa linha so', o que torna uma
    // view de relatorio ilegivel.
    //
    // O cast para ::regclass resolve o nome ja' qualificado pelo schema e
    // falha alto se o objeto sumiu entre listar e abrir.
    const std::string qualified =
        quote_ident(schema) + "." + quote_ident(name);

    const std::string sql =
        "SELECT pg_get_viewdef(" + quote_literal(qualified) + "::regclass, true)";

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query(sql));
    if (rs.row_count() == 0) return std::string{};
    return std::string(rs.text(0, 0));
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
