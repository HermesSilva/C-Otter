#include "db/catalog_mssql.hpp"

#include <algorithm>
#include <charconv>
#include <string>

namespace otter::db {
namespace {

std::int64_t to_int64(std::string_view text) {
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::string text_of(const ResultSet& rs, std::size_t row, std::size_t column) {
    return rs.is_null(row, column) ? std::string{} : std::string(rs.text(row, column));
}

// A lista de colunas de uma chave, na ordem dela. FOR XML PATH em vez de
// STRING_AGG: vale desde o SQL Server 2005, e o `.value()` desfaz o escape de
// XML que um nome com '&' ou '<' ganharia.
//
// `source` e' a subconsulta que devolve o nome e a ordem ("SELECT nome AS n,
// ordem AS o FROM ...").
std::string column_list(const std::string& source) {
    return "STUFF((SELECT ',' + x.n FROM (" + source +
           ") x ORDER BY x.o FOR XML PATH(''), TYPE).value('.', 'nvarchar(max)'), 1, 1, '')";
}

} // namespace

// --- Citacao -----------------------------------------------------------------------

std::string mssql_quote(std::string_view identifier) {
    std::string out = "[";
    for (const char c : identifier) {
        out += c;
        if (c == ']') out += c;
    }
    out += ']';
    return out;
}

std::string mssql_literal(std::string_view text) {
    std::string out = "N'";
    for (const char c : text) {
        out += c;
        if (c == '\'') out += c;
    }
    out += '\'';
    return out;
}

std::string mssql_object_id(std::string_view schema, std::string_view name) {
    return "OBJECT_ID(" + mssql_literal(mssql_quote(schema) + "." + mssql_quote(name)) + ")";
}

std::string mssql_type_text(std::string_view type, int max_length, int precision,
                            int scale) {
    std::string out(type);
    const auto with_length = [&out, max_length](int divisor) {
        out += max_length < 0 ? "(max)" : "(" + std::to_string(max_length / divisor) + ")";
    };

    if (type == "varchar" || type == "char" || type == "varbinary" || type == "binary") {
        with_length(1);
    } else if (type == "nvarchar" || type == "nchar") {
        with_length(2);   // dois bytes por caractere
    } else if (type == "decimal" || type == "numeric") {
        out += "(" + std::to_string(precision) + "," + std::to_string(scale) + ")";
    } else if (type == "datetime2" || type == "time" || type == "datetimeoffset") {
        // 7 e' o padrao: so' a escala diferente dele aparece.
        if (scale != 7) out += "(" + std::to_string(scale) + ")";
    } else if (type == "float") {
        if (precision != 53) out += "(" + std::to_string(precision) + ")";
    }
    return out;
}

DataKind mssql_kind(std::string_view type) noexcept {
    if (type == "tinyint" || type == "smallint" || type == "int" || type == "bigint") {
        return DataKind::integer;
    }
    if (type == "bit") return DataKind::boolean;
    if (type == "real" || type == "float") return DataKind::floating;
    if (type == "decimal" || type == "numeric" || type == "money" ||
        type == "smallmoney") {
        return DataKind::numeric;
    }
    if (type == "date") return DataKind::date;
    if (type == "time") return DataKind::time;
    if (type == "datetime" || type == "datetime2" || type == "smalldatetime" ||
        type == "datetimeoffset") {
        return DataKind::timestamp;
    }
    if (type == "uniqueidentifier") return DataKind::uuid;
    if (type == "varbinary" || type == "binary" || type == "image" ||
        type == "timestamp" || type == "rowversion") {
        return DataKind::binary;
    }
    if (type == "geometry" || type == "geography") return DataKind::geometry;
    if (type == "char" || type == "varchar" || type == "nchar" || type == "nvarchar" ||
        type == "text" || type == "ntext" || type == "xml" || type == "sysname" ||
        type == "sql_variant") {
        return DataKind::string;
    }
    return DataKind::unknown;
}

// --- Catalogo ----------------------------------------------------------------------

MssqlCatalog::MssqlCatalog(Holt& holt) : holt_(holt) {
    version_ = ServerVersion::parse(holt.server_version());
}

Result<std::vector<DatabaseMeta>> MssqlCatalog::load_databases(bool, bool unavailable) {
    // sys.master_files exige VIEW ANY DEFINITION: sem ela a subconsulta
    // devolve NULL para os bancos dos outros, e o tamanho fica desconhecido.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT d.name, SUSER_SNAME(d.owner_sid), d.collation_name,"
            "       (SELECT SUM(CAST(f.size AS bigint)) * 8192 FROM sys.master_files f"
            "         WHERE f.database_id = d.database_id),"
            "       d.state, HAS_DBACCESS(d.name)"
            "  FROM sys.databases d"
            " ORDER BY d.name"));

    std::vector<DatabaseMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        DatabaseMeta database;
        database.name     = text_of(rs, row, 0);
        database.owner    = text_of(rs, row, 1);
        database.encoding = text_of(rs, row, 2);   // o collation faz esse papel
        if (!rs.is_null(row, 3)) {
            database.size_bytes  = to_int64(rs.text(row, 3));
            database.size_pretty = format_size(database.size_bytes);
        }
        // state 0 = ONLINE; HAS_DBACCESS = 1 quando o login entra no banco.
        database.allow_connect = rs.text(row, 4) == "0" && rs.text(row, 5) == "1";
        if (!database.allow_connect && !unavailable) continue;
        out.push_back(std::move(database));
    }
    return out;
}

Result<std::vector<SchemaMeta>> MssqlCatalog::load_schemas() {
    // Os schemas dos papeis fixos (db_owner...db_denydatawriter) tem id entre
    // 16384 e 16399: existem em todo banco e nunca tem objeto.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT s.name, USER_NAME(s.principal_id)"
            "  FROM sys.schemas s"
            " WHERE s.name NOT IN (N'sys', N'INFORMATION_SCHEMA', N'guest')"
            "   AND (s.schema_id < 16384 OR s.schema_id > 16399)"
            " ORDER BY CASE WHEN s.name = N'dbo' THEN 0 ELSE 1 END, s.name"));

    std::vector<SchemaMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SchemaMeta schema;
        schema.name  = text_of(rs, row, 0);
        schema.owner = text_of(rs, row, 1);
        out.push_back(std::move(schema));
    }
    return out;
}

Result<std::vector<TableMeta>> MssqlCatalog::load_tables(std::string_view schema) {
    // Linhas e tamanho vem das estatisticas de particao, sem varrer a tabela
    // (index_id 0 = heap, 1 = indice clusterizado: um dos dois E' a tabela).
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT o.name, o.type, CAST(ep.value AS nvarchar(4000)),"
            "       (SELECT SUM(p.rows) FROM sys.partitions p"
            "         WHERE p.object_id = o.object_id AND p.index_id IN (0, 1)),"
            "       (SELECT SUM(a.total_pages) * 8192"
            "          FROM sys.partitions p"
            "          JOIN sys.allocation_units a"
            "            ON a.container_id IN (p.hobt_id, p.partition_id)"
            "         WHERE p.object_id = o.object_id)"
            "  FROM sys.objects o"
            "  LEFT JOIN sys.extended_properties ep"
            "    ON ep.class = 1 AND ep.major_id = o.object_id AND ep.minor_id = 0"
            "   AND ep.name = N'MS_Description'"
            " WHERE o.schema_id = SCHEMA_ID(" + mssql_literal(schema) + ")"
            "   AND o.type IN ('U', 'V') AND o.is_ms_shipped = 0"
            " ORDER BY o.name"));

    std::vector<TableMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TableMeta table;
        table.name    = text_of(rs, row, 0);
        // `type` e' char(2): "U " e "V ", com o espaco.
        table.kind    = rs.text(row, 1).starts_with("V") ? ObjKind::view : ObjKind::table;
        table.comment = text_of(rs, row, 2);
        if (!rs.is_null(row, 3)) table.estimated_rows = to_int64(rs.text(row, 3));
        if (!rs.is_null(row, 4)) {
            table.size_bytes  = to_int64(rs.text(row, 4));
            table.size_pretty = format_size(table.size_bytes);
        }
        out.push_back(std::move(table));
    }
    return out;
}

Result<std::vector<ColumnMeta>> MssqlCatalog::load_columns(std::string_view schema,
                                                           std::string_view table) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT c.name, tp.name, c.max_length, c.precision, c.scale, c.is_nullable,"
            "       CASE WHEN EXISTS (SELECT 1 FROM sys.indexes i"
            "                           JOIN sys.index_columns ic"
            "                             ON ic.object_id = i.object_id"
            "                            AND ic.index_id = i.index_id"
            "                          WHERE i.object_id = c.object_id"
            "                            AND i.is_primary_key = 1"
            "                            AND ic.column_id = c.column_id)"
            "            THEN 1 ELSE 0 END,"
            "       dc.definition, CAST(ep.value AS nvarchar(4000)), c.column_id,"
            "       c.is_identity, cc.definition, bt.name"
            "  FROM sys.columns c"
            "  JOIN sys.types tp ON tp.user_type_id = c.user_type_id"
            "  LEFT JOIN sys.types bt"
            "    ON bt.user_type_id = tp.system_type_id AND bt.is_user_defined = 0"
            "  LEFT JOIN sys.default_constraints dc ON dc.object_id = c.default_object_id"
            "  LEFT JOIN sys.computed_columns cc"
            "    ON cc.object_id = c.object_id AND cc.column_id = c.column_id"
            "  LEFT JOIN sys.extended_properties ep"
            "    ON ep.class = 1 AND ep.major_id = c.object_id"
            "   AND ep.minor_id = c.column_id AND ep.name = N'MS_Description'"
            " WHERE c.object_id = " + mssql_object_id(schema, table) +
            " ORDER BY c.column_id"));

    std::vector<ColumnMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ColumnMeta column;
        column.name = text_of(rs, row, 0);

        const std::string type = text_of(rs, row, 1);
        column.type_name = mssql_type_text(type, static_cast<int>(to_int64(rs.text(row, 2))),
                                           static_cast<int>(to_int64(rs.text(row, 3))),
                                           static_cast<int>(to_int64(rs.text(row, 4))));
        // Tipo de usuario (alias): classifica pelo tipo de sistema por baixo.
        const std::string base = text_of(rs, row, 12);
        column.kind        = mssql_kind(base.empty() ? type : base);
        column.nullable    = rs.text(row, 5) == "1";
        column.primary_key = rs.text(row, 6) == "1";
        column.default_value = text_of(rs, row, 7);
        column.comment     = text_of(rs, row, 8);
        column.position    = static_cast<std::int32_t>(to_int64(rs.text(row, 9)));

        // Identity e coluna calculada nao sao "default", mas e' ali que quem
        // le' a arvore procura de onde o valor vem.
        if (rs.text(row, 10) == "1") column.default_value = "IDENTITY";
        if (!rs.is_null(row, 11)) column.default_value = "AS " + text_of(rs, row, 11);
        out.push_back(std::move(column));
    }
    return out;
}

Result<std::vector<ForeignKeyMeta>> MssqlCatalog::foreign_keys_where(
    const std::string& condition) {
    const std::string source_columns = column_list(
        "SELECT COL_NAME(k.parent_object_id, k.parent_column_id) AS n,"
        "       k.constraint_column_id AS o"
        "  FROM sys.foreign_key_columns k WHERE k.constraint_object_id = fk.object_id");
    const std::string target_columns = column_list(
        "SELECT COL_NAME(k.referenced_object_id, k.referenced_column_id) AS n,"
        "       k.constraint_column_id AS o"
        "  FROM sys.foreign_key_columns k WHERE k.constraint_object_id = fk.object_id");

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT fk.name, OBJECT_NAME(fk.parent_object_id), " + source_columns + ","
            "       OBJECT_NAME(fk.referenced_object_id), " + target_columns + ","
            "       REPLACE(fk.update_referential_action_desc, '_', ' '),"
            "       REPLACE(fk.delete_referential_action_desc, '_', ' '),"
            "       OBJECT_SCHEMA_NAME(fk.parent_object_id),"
            "       OBJECT_SCHEMA_NAME(fk.referenced_object_id)"
            "  FROM sys.foreign_keys fk"
            " WHERE " + condition +
            " ORDER BY fk.name"));

    std::vector<ForeignKeyMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ForeignKeyMeta key;
        key.name          = text_of(rs, row, 0);
        key.source_table  = text_of(rs, row, 1);
        key.source_column = text_of(rs, row, 2);
        key.target_table  = text_of(rs, row, 3);
        key.target_column = text_of(rs, row, 4);
        key.on_update     = text_of(rs, row, 5);
        key.on_delete     = text_of(rs, row, 6);
        key.source_schema = text_of(rs, row, 7);
        key.target_schema = text_of(rs, row, 8);

        // O texto da constraint, como iria num ALTER TABLE ... ADD.
        const auto bracketed = [](const std::string& list) {
            std::string result;
            std::size_t start = 0;
            while (start <= list.size()) {
                const std::size_t comma = list.find(',', start);
                const std::string name = list.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!result.empty()) result += ", ";
                result += mssql_quote(name);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
            return result;
        };
        key.definition = "FOREIGN KEY (" + bracketed(key.source_column) + ") REFERENCES " +
                         mssql_quote(key.target_schema) + "." + mssql_quote(key.target_table) +
                         " (" + bracketed(key.target_column) + ")";
        if (key.on_delete != "NO ACTION") key.definition += " ON DELETE " + key.on_delete;
        if (key.on_update != "NO ACTION") key.definition += " ON UPDATE " + key.on_update;
        out.push_back(std::move(key));
    }
    return out;
}

Result<std::vector<ForeignKeyMeta>> MssqlCatalog::load_foreign_keys(
    std::string_view schema) {
    return foreign_keys_where("fk.schema_id = SCHEMA_ID(" + mssql_literal(schema) + ")");
}

Result<std::vector<ForeignKeyMeta>> MssqlCatalog::load_table_foreign_keys(
    std::string_view schema, std::string_view table) {
    return foreign_keys_where("fk.parent_object_id = " + mssql_object_id(schema, table));
}

Result<std::vector<ForeignKeyMeta>> MssqlCatalog::load_references(
    std::string_view schema, std::string_view table) {
    return foreign_keys_where("fk.referenced_object_id = " + mssql_object_id(schema, table));
}

Result<std::vector<ConstraintMeta>> MssqlCatalog::load_constraints(
    std::string_view schema, std::string_view table) {
    const std::string object = mssql_object_id(schema, table);
    const std::string key_columns = column_list(
        "SELECT COL_NAME(ic.object_id, ic.column_id) AS n, ic.key_ordinal AS o"
        "  FROM sys.index_columns ic"
        " WHERE ic.object_id = kc.parent_object_id AND ic.index_id = kc.unique_index_id"
        "   AND ic.is_included_column = 0");

    // Chaves (PRIMARY KEY, UNIQUE) e depois as CHECK.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT kc.name, kc.type, " + key_columns + ", NULL"
            "  FROM sys.key_constraints kc WHERE kc.parent_object_id = " + object +
            " UNION ALL "
            "SELECT cc.name, 'C', COL_NAME(cc.parent_object_id, cc.parent_column_id),"
            "       cc.definition"
            "  FROM sys.check_constraints cc WHERE cc.parent_object_id = " + object +
            " ORDER BY 2 DESC, 1"));

    std::vector<ConstraintMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ConstraintMeta constraint;
        constraint.name    = text_of(rs, row, 0);
        constraint.columns = text_of(rs, row, 2);

        const std::string_view type = rs.text(row, 1);
        if (type.starts_with("PK")) {
            constraint.kind       = ObjKind::primary_key;
            constraint.definition = "PRIMARY KEY (" + constraint.columns + ")";
        } else if (type.starts_with("UQ")) {
            constraint.kind       = ObjKind::unique_key;
            constraint.definition = "UNIQUE (" + constraint.columns + ")";
        } else {
            constraint.kind       = ObjKind::check_constraint;
            constraint.definition = "CHECK " + text_of(rs, row, 3);
        }
        out.push_back(std::move(constraint));
    }
    return out;
}

Result<std::vector<IndexMeta>> MssqlCatalog::load_indexes(std::string_view schema,
                                                          std::string_view table) {
    const std::string columns = column_list(
        "SELECT COL_NAME(ic.object_id, ic.column_id) +"
        "       CASE WHEN ic.is_descending_key = 1 THEN ' DESC' ELSE '' END AS n,"
        "       ic.key_ordinal AS o"
        "  FROM sys.index_columns ic"
        " WHERE ic.object_id = i.object_id AND ic.index_id = i.index_id"
        "   AND ic.is_included_column = 0");

    // index_id 0 e' o heap (a propria tabela sem indice clusterizado): nao e'
    // um indice que se possa nomear ou apagar.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT i.name, i.type_desc, i.is_unique, i.is_primary_key, i.is_disabled, " +
            columns + ","
            "       (SELECT SUM(a.total_pages) * 8192 FROM sys.partitions p"
            "          JOIN sys.allocation_units a"
            "            ON a.container_id IN (p.hobt_id, p.partition_id)"
            "         WHERE p.object_id = i.object_id AND p.index_id = i.index_id)"
            "  FROM sys.indexes i"
            " WHERE i.object_id = " + mssql_object_id(schema, table) +
            "   AND i.index_id > 0 AND i.is_hypothetical = 0"
            " ORDER BY i.name"));

    std::vector<IndexMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        IndexMeta index;
        index.name    = text_of(rs, row, 0);
        index.method  = text_of(rs, row, 1);   // CLUSTERED, NONCLUSTERED, ...
        index.unique  = rs.text(row, 2) == "1";
        index.primary = rs.text(row, 3) == "1";
        index.valid   = rs.text(row, 4) != "1";   // desabilitado nao e' usado
        index.columns = text_of(rs, row, 5);
        if (!rs.is_null(row, 6)) index.size_pretty = format_size(to_int64(rs.text(row, 6)));

        index.definition = std::string("CREATE ") + (index.unique ? "UNIQUE " : "") +
                           index.method + " INDEX " + mssql_quote(index.name) + " ON " +
                           mssql_quote(schema) + "." + mssql_quote(table) + " (" +
                           index.columns + ")";
        out.push_back(std::move(index));
    }
    return out;
}

Result<std::vector<TriggerMeta>> MssqlCatalog::load_triggers(std::string_view schema,
                                                             std::string_view table) {
    const std::string events = column_list(
        "SELECT te.type_desc AS n, te.type AS o FROM sys.trigger_events te"
        " WHERE te.object_id = t.object_id");

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.name, t.is_instead_of_trigger, " + events + ","
            "       OBJECT_DEFINITION(t.object_id), t.is_disabled"
            "  FROM sys.triggers t"
            " WHERE t.parent_id = " + mssql_object_id(schema, table) +
            " ORDER BY t.name"));

    std::vector<TriggerMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TriggerMeta trigger;
        trigger.name       = text_of(rs, row, 0);
        trigger.table      = std::string(table);
        trigger.timing     = rs.text(row, 1) == "1" ? "INSTEAD OF" : "AFTER";
        trigger.events     = text_of(rs, row, 2);
        trigger.definition = text_of(rs, row, 3);
        trigger.enabled    = rs.text(row, 4) != "1";
        out.push_back(std::move(trigger));
    }
    return out;
}

Result<std::vector<SequenceMeta>> MssqlCatalog::load_sequences(std::string_view schema) {
    if (!version_.at_least(11)) return std::vector<SequenceMeta>{};   // SQL Server 2012

    // Os limites sao sql_variant (o tipo da sequence varia): CAST para bigint.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT s.name, CAST(s.current_value AS bigint), CAST(s.start_value AS bigint),"
            "       CAST(s.increment AS bigint), CAST(s.minimum_value AS bigint),"
            "       CAST(s.maximum_value AS bigint), s.is_cycling,"
            "       CAST(ep.value AS nvarchar(4000))"
            "  FROM sys.sequences s"
            "  LEFT JOIN sys.extended_properties ep"
            "    ON ep.class = 1 AND ep.major_id = s.object_id AND ep.minor_id = 0"
            "   AND ep.name = N'MS_Description'"
            " WHERE s.schema_id = SCHEMA_ID(" + mssql_literal(schema) + ")"
            " ORDER BY s.name"));

    std::vector<SequenceMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SequenceMeta sequence;
        sequence.name        = text_of(rs, row, 0);
        sequence.last_value  = to_int64(rs.text(row, 1));
        sequence.start_value = to_int64(rs.text(row, 2));
        sequence.increment   = to_int64(rs.text(row, 3));
        sequence.min_value   = to_int64(rs.text(row, 4));
        sequence.max_value   = to_int64(rs.text(row, 5));
        sequence.cycles      = rs.text(row, 6) == "1";
        sequence.comment     = text_of(rs, row, 7);
        out.push_back(std::move(sequence));
    }
    return out;
}

Result<std::vector<RoutineMeta>> MssqlCatalog::load_routines(std::string_view schema) {
    // P = procedure; FN/IF/TF = funcao escalar, de tabela em linha e de tabela;
    // PC/FS/FT = as mesmas, escritas em CLR. O parametro 0 e' o RETORNO.
    const std::string arguments = column_list(
        "SELECT pa.name + ' ' + TYPE_NAME(pa.user_type_id) +"
        "       CASE WHEN pa.is_output = 1 THEN ' OUTPUT' ELSE '' END AS n,"
        "       pa.parameter_id AS o"
        "  FROM sys.parameters pa"
        " WHERE pa.object_id = o.object_id AND pa.parameter_id > 0");

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT o.name, o.type, REPLACE(" + arguments + ", ',', ', '),"
            "       (SELECT TYPE_NAME(pa.user_type_id) FROM sys.parameters pa"
            "         WHERE pa.object_id = o.object_id AND pa.parameter_id = 0),"
            "       CAST(ep.value AS nvarchar(4000))"
            "  FROM sys.objects o"
            "  LEFT JOIN sys.extended_properties ep"
            "    ON ep.class = 1 AND ep.major_id = o.object_id AND ep.minor_id = 0"
            "   AND ep.name = N'MS_Description'"
            " WHERE o.schema_id = SCHEMA_ID(" + mssql_literal(schema) + ")"
            "   AND o.type IN ('P', 'FN', 'IF', 'TF', 'PC', 'FS', 'FT')"
            "   AND o.is_ms_shipped = 0"
            " ORDER BY o.name"));

    std::vector<RoutineMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        RoutineMeta routine;
        routine.name = text_of(rs, row, 0);

        std::string type = text_of(rs, row, 1);
        while (!type.empty() && type.back() == ' ') type.pop_back();
        routine.kind      = type == "P" || type == "PC" ? ObjKind::procedure
                                                        : ObjKind::function;
        routine.arguments = text_of(rs, row, 2);
        // Nao ha' sobrecarga no SQL Server: o nome identifica a rotina.
        routine.signature.clear();
        routine.return_type = type == "IF" || type == "TF" || type == "FT"
                                  ? std::string("TABLE")
                                  : text_of(rs, row, 3);
        routine.language = type == "PC" || type == "FS" || type == "FT" ? "CLR" : "T-SQL";
        routine.comment  = text_of(rs, row, 4);
        out.push_back(std::move(routine));
    }
    return out;
}

Result<std::vector<DataTypeMeta>> MssqlCatalog::load_types(std::string_view schema) {
    // Tipos de usuario: os "alias" (CREATE TYPE x FROM varchar(10)), que sao o
    // dominio do PostgreSQL, e os tipos de TABELA.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.name, t.is_table_type, bt.name, t.max_length, t.precision, t.scale,"
            "       t.is_nullable, USER_NAME(t.principal_id), tt.type_table_object_id"
            "  FROM sys.types t"
            "  LEFT JOIN sys.types bt"
            "    ON bt.user_type_id = t.system_type_id AND bt.is_user_defined = 0"
            "  LEFT JOIN sys.table_types tt ON tt.user_type_id = t.user_type_id"
            " WHERE t.is_user_defined = 1"
            "   AND t.schema_id = SCHEMA_ID(" + mssql_literal(schema) + ")"
            " ORDER BY t.name"));

    std::vector<DataTypeMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        DataTypeMeta type;
        type.name  = text_of(rs, row, 0);
        type.owner = text_of(rs, row, 7);

        if (rs.text(row, 1) == "1") {
            type.kind = TypeKind::composite;
            // As colunas do tipo de tabela.
            if (auto columns = holt_.query_internal(
                    "SELECT c.name, tp.name, c.max_length, c.precision, c.scale,"
                    "       c.is_nullable"
                    "  FROM sys.columns c"
                    "  JOIN sys.types tp ON tp.user_type_id = c.user_type_id"
                    " WHERE c.object_id = " + text_of(rs, row, 8) +
                    " ORDER BY c.column_id")) {
                for (std::size_t c = 0; c < columns->row_count(); ++c) {
                    TypeAttributeMeta attribute;
                    attribute.name      = text_of(*columns, c, 0);
                    attribute.type_name = mssql_type_text(
                        columns->text(c, 1), static_cast<int>(to_int64(columns->text(c, 2))),
                        static_cast<int>(to_int64(columns->text(c, 3))),
                        static_cast<int>(to_int64(columns->text(c, 4))));
                    attribute.nullable = columns->text(c, 5) == "1";
                    type.attributes.push_back(std::move(attribute));
                }
            }
        } else {
            type.kind      = TypeKind::domain;
            type.base_type = mssql_type_text(text_of(rs, row, 2),
                                             static_cast<int>(to_int64(rs.text(row, 3))),
                                             static_cast<int>(to_int64(rs.text(row, 4))),
                                             static_cast<int>(to_int64(rs.text(row, 5))));
            type.not_null = rs.text(row, 6) != "1";
        }
        out.push_back(std::move(type));
    }
    return out;
}

Result<std::string> MssqlCatalog::load_routine_definition(std::string_view schema,
                                                          std::string_view name,
                                                          std::string_view) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs, holt_.query_internal("SELECT OBJECT_DEFINITION(" +
                                      mssql_object_id(schema, name) + ")"));
    if (rs.row_count() == 0 || rs.is_null(0, 0)) {
        // NULL = objeto cifrado (WITH ENCRYPTION), CLR, ou sem permissao.
        return fail(Errc::not_found,
                    "the definition is not available (encrypted, CLR, or no permission)");
    }
    return std::string(rs.text(0, 0));
}

Result<std::string> MssqlCatalog::load_view_definition(std::string_view schema,
                                                       std::string_view name) {
    return load_routine_definition(schema, name, {});
}

Result<std::vector<PartitionMeta>> MssqlCatalog::load_partitions(std::string_view,
                                                                 std::string_view) {
    return std::vector<PartitionMeta>{};
}

Result<std::vector<EventMeta>> MssqlCatalog::load_events(std::string_view) {
    return std::vector<EventMeta>{};
}

// --- Listas ------------------------------------------------------------------------

Result<std::vector<CatalogItem>> MssqlCatalog::load_list(CatalogList list,
                                                         std::string_view a,
                                                         std::string_view b,
                                                         std::string_view) {
    // (nome, detalhe, dica, flag): toda lista devolve essas quatro colunas.
    std::string sql;
    switch (list) {
        case CatalogList::schema_indexes:
            sql = "SELECT i.name, OBJECT_NAME(i.object_id), i.type_desc, i.is_unique"
                  "  FROM sys.indexes i"
                  "  JOIN sys.objects o ON o.object_id = i.object_id"
                  " WHERE o.schema_id = SCHEMA_ID(" + mssql_literal(a) + ")"
                  "   AND o.type = 'U' AND i.index_id > 0 AND i.is_hypothetical = 0"
                  "   AND i.name IS NOT NULL"
                  " ORDER BY i.name";
            break;

        case CatalogList::routine_parameters:
            sql = "SELECT CASE WHEN pa.parameter_id = 0 THEN N'(return)' ELSE pa.name END,"
                  "       TYPE_NAME(pa.user_type_id),"
                  "       CASE WHEN pa.is_output = 1 THEN N'OUTPUT' ELSE N'' END, pa.is_output"
                  "  FROM sys.parameters pa"
                  " WHERE pa.object_id = " + mssql_object_id(a, b) +
                  " ORDER BY pa.parameter_id";
            break;

        case CatalogList::dependencies:
        case CatalogList::routine_dependencies:
            // O que ESTE objeto usa.
            sql = "SELECT COALESCE(d.referenced_schema_name + N'.', N'') +"
                  "       d.referenced_entity_name,"
                  "       COALESCE(ro.type_desc, N''), N'', 0"
                  "  FROM sys.sql_expression_dependencies d"
                  "  LEFT JOIN sys.objects ro ON ro.object_id = d.referenced_id"
                  " WHERE d.referencing_id = " + mssql_object_id(a, b) +
                  " ORDER BY 1";
            break;

        case CatalogList::roles:
            // Quem pode receber privilegio no BANCO: usuarios e papeis. flag =
            // e' usuario (entra no banco), e nao papel.
            sql = "SELECT p.name, p.type_desc, COALESCE(p.default_schema_name, N''),"
                  "       CASE WHEN p.type = 'R' THEN 0 ELSE 1 END"
                  "  FROM sys.database_principals p"
                  " WHERE p.type IN ('S', 'U', 'G', 'R', 'E', 'X')"
                  "   AND p.name NOT IN (N'sys', N'INFORMATION_SCHEMA')"
                  "   AND (p.is_fixed_role = 0 OR p.name = N'public')"
                  " ORDER BY p.name";
            break;

        case CatalogList::role_members:
            sql = "SELECT m.name, m.type_desc, N'', CASE WHEN m.type = 'R' THEN 0 ELSE 1 END"
                  "  FROM sys.database_role_members rm"
                  "  JOIN sys.database_principals r ON r.principal_id = rm.role_principal_id"
                  "  JOIN sys.database_principals m ON m.principal_id = rm.member_principal_id"
                  " WHERE r.name = " + mssql_literal(a) + " ORDER BY m.name";
            break;

        case CatalogList::role_belongs:
            sql = "SELECT r.name, r.type_desc, N'', 0"
                  "  FROM sys.database_role_members rm"
                  "  JOIN sys.database_principals r ON r.principal_id = rm.role_principal_id"
                  "  JOIN sys.database_principals m ON m.principal_id = rm.member_principal_id"
                  " WHERE m.name = " + mssql_literal(a) + " ORDER BY r.name";
            break;

        case CatalogList::synonyms:
            sql = "SELECT s.name, s.base_object_name, N'', 0"
                  "  FROM sys.synonyms s"
                  " WHERE s.schema_id = SCHEMA_ID(" + mssql_literal(a) + ")"
                  " ORDER BY s.name";
            break;

        case CatalogList::database_triggers:
            // Triggers de DDL do banco (parent_class 0), nao os de tabela.
            sql = "SELECT t.name, CASE WHEN t.is_disabled = 1 THEN N'disabled' ELSE N'' END,"
                  "       OBJECT_DEFINITION(t.object_id), CASE WHEN t.is_disabled = 1"
                  "                                             THEN 0 ELSE 1 END"
                  "  FROM sys.triggers t WHERE t.parent_class = 0 ORDER BY t.name";
            break;

        case CatalogList::logins:
            // Os logins do SERVIDOR. flag = habilitado.
            sql = "SELECT p.name, p.type_desc, COALESCE(p.default_database_name, N''),"
                  "       CASE WHEN p.is_disabled = 1 THEN 0 ELSE 1 END"
                  "  FROM sys.server_principals p"
                  " WHERE p.type IN ('S', 'U', 'G', 'E', 'X')"
                  "   AND p.name NOT LIKE N'##%'"
                  " ORDER BY p.name";
            break;

        default:
            // As demais listas sao do `<tree>` do PostgreSQL (extensoes,
            // tablespaces, politicas...): nao existem aqui.
            return std::vector<CatalogItem>{};
    }

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query_internal(sql));

    std::vector<CatalogItem> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        CatalogItem item;
        item.name    = text_of(rs, row, 0);
        item.detail  = text_of(rs, row, 1);
        item.tooltip = text_of(rs, row, 2);
        item.flag    = rs.text(row, 3) == "1";
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace otter::db
