#include "db/catalog_sqlanywhere.hpp"

#include <algorithm>
#include <cctype>
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

// char(n) vem completado com espacos; as letras de SYSIDX e SYSTRIGGER sao
// char(1) e chegam como "A  ".
std::string_view trimmed(std::string_view text) {
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// "a,b" -> "\"a\", \"b\"".
std::string quoted_list(const std::string& list) {
    std::string result;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::string name = list.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!result.empty()) result += ", ";
        result += sqlanywhere_quote(name);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

// As colunas de um indice, na ordem dele. `table` e `index` sao as expressoes
// do id da tabela e do indice na consulta de fora; `descending` acrescenta
// " DESC" as que sao.
std::string index_columns(const std::string& table, const std::string& index,
                          bool descending) {
    return std::string("(SELECT list(xc.column_name") +
           (descending ? " || CASE WHEN xi.[order] = 'D' THEN ' DESC' ELSE '' END" : "") +
           ", ',' ORDER BY xi.sequence)"
           "   FROM SYS.SYSIDXCOL xi"
           "   JOIN SYS.SYSTABCOL xc"
           "     ON xc.table_id = xi.table_id AND xc.column_id = xi.column_id"
           "  WHERE xi.table_id = " + table + " AND xi.index_id = " + index + ")";
}

} // namespace

// --- Citacao -----------------------------------------------------------------------

std::string sqlanywhere_quote(std::string_view identifier) {
    std::string out = "\"";
    for (const char c : identifier) {
        out += c;
        if (c == '"') out += c;
    }
    out += '"';
    return out;
}

std::string sqlanywhere_literal(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        out += c;
        if (c == '\'') out += c;
    }
    out += '\'';
    return out;
}

std::string sqlanywhere_table_filter(std::string_view alias, std::string_view schema,
                                     std::string_view table) {
    const std::string a(alias);
    return a + ".creator = user_id(" + sqlanywhere_literal(schema) + ") AND " + a +
           ".table_name = " + sqlanywhere_literal(table);
}

DataKind sqlanywhere_kind(std::string_view type) noexcept {
    // "char(20)", "numeric(10,2)": so' o nome.
    std::string name = lower(type.substr(0, type.find('(')));
    while (!name.empty() && name.back() == ' ') name.pop_back();

    if (name == "integer" || name == "int" || name == "smallint" || name == "tinyint" ||
        name == "bigint" || name.starts_with("unsigned")) {
        return DataKind::integer;
    }
    if (name == "bit") return DataKind::boolean;
    if (name == "real" || name == "float" || name == "double") return DataKind::floating;
    if (name == "numeric" || name == "decimal" || name == "money" || name == "smallmoney") {
        return DataKind::numeric;
    }
    if (name == "date") return DataKind::date;
    if (name == "time") return DataKind::time;
    if (name == "timestamp" || name == "datetime" || name == "smalldatetime" ||
        name == "timestamp with time zone" || name == "datetimeoffset") {
        return DataKind::timestamp;
    }
    if (name == "uniqueidentifier") return DataKind::uuid;
    if (name == "binary" || name == "varbinary" || name == "long binary" ||
        name == "image" || name == "varbit" || name == "long varbit") {
        return DataKind::binary;
    }
    if (name.starts_with("st_")) return DataKind::geometry;
    if (name == "char" || name == "varchar" || name == "long varchar" || name == "nchar" ||
        name == "nvarchar" || name == "long nvarchar" || name == "text" || name == "ntext" ||
        name == "xml" || name == "uniqueidentifierstr" || name == "sysname") {
        return DataKind::string;
    }
    return DataKind::unknown;
}

std::string_view sqlanywhere_trigger_timing(std::string_view code) noexcept {
    code = trimmed(code);
    if (code == "B") return "BEFORE";
    if (code == "A" || code == "S") return "AFTER";   // S = AFTER, por comando
    if (code == "I" || code == "K") return "INSTEAD OF";
    if (code == "R") return "RESOLVE";
    return "AFTER";
}

std::string_view sqlanywhere_trigger_events(std::string_view code) noexcept {
    code = trimmed(code);
    if (code == "A") return "INSERT, DELETE";
    if (code == "B") return "INSERT, UPDATE";
    if (code == "C") return "UPDATE";           // UPDATE OF <colunas>
    if (code == "D") return "DELETE";
    if (code == "E") return "DELETE, UPDATE";
    if (code == "I") return "INSERT";
    if (code == "U") return "UPDATE";
    if (code == "M") return "INSERT, DELETE, UPDATE";
    return "";
}

std::string_view sqlanywhere_referential_action(std::string_view code) noexcept {
    code = trimmed(code);
    if (code == "C") return "CASCADE";
    if (code == "D") return "SET DEFAULT";
    if (code == "N") return "SET NULL";
    return "RESTRICT";
}

// --- Catalogo ----------------------------------------------------------------------

SqlAnywhereCatalog::SqlAnywhereCatalog(Holt& holt) : holt_(holt) {
    version_ = ServerVersion::parse(holt.server_version());
    user_    = holt.current_schema();
}

Result<std::vector<SchemaMeta>> SqlAnywhereCatalog::load_schemas() {
    // table_type 5 sao as tabelas internas dos indices de texto: nao contam.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT u.user_name, r.remarks"
            "  FROM SYS.SYSUSER u"
            "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = u.object_id"
            " WHERE u.user_name = user_name()"
            "    OR EXISTS (SELECT 1 FROM SYS.SYSTAB t"
            "                WHERE t.creator = u.user_id AND t.table_type IN (1, 2, 3, 21))"
            "    OR EXISTS (SELECT 1 FROM SYS.SYSPROCEDURE p WHERE p.creator = u.user_id)"
            "    OR EXISTS (SELECT 1 FROM SYS.SYSSEQUENCE s WHERE s.owner = u.user_id)"
            "    OR (u.user_id >= 100 AND EXISTS (SELECT 1 FROM SYS.SYSUSERTYPE ut"
            "                                      WHERE ut.creator = u.user_id))"
            " ORDER BY CASE WHEN u.user_name = user_name() THEN 0"
            "               WHEN u.user_id < 100 THEN 2 ELSE 1 END, u.user_name"));

    std::vector<SchemaMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SchemaMeta schema;
        schema.name    = text_of(rs, row, 0);
        schema.comment = text_of(rs, row, 1);
        schema.owner   = schema.name;   // o dono E' o schema
        out.push_back(std::move(schema));
    }
    return out;
}

Result<std::vector<TableMeta>> SqlAnywhereCatalog::load_tables(std::string_view schema) {
    // Linhas e paginas vem do proprio catalogo, sem varrer a tabela. 1 = base,
    // 2 = view materializada, 3 = temporaria global, 21 = view.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.table_name, t.table_type, r.remarks, t.[count],"
            "       (CAST(t.table_page_count AS bigint) + t.ext_page_count)"
            "         * CAST(db_property('PageSize') AS bigint),"
            "       t.table_id"
            "  FROM SYS.SYSTAB t"
            "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = t.object_id"
            " WHERE t.creator = user_id(" + sqlanywhere_literal(schema) + ")"
            "   AND t.table_type IN (1, 2, 3, 21)"
            " ORDER BY t.table_name"));

    std::vector<TableMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TableMeta table;
        table.name = text_of(rs, row, 0);

        const std::string_view type = rs.text(row, 1);
        table.kind = type == "21"  ? ObjKind::view
                     : type == "2" ? ObjKind::materialized_view
                                   : ObjKind::table;
        table.comment = text_of(rs, row, 2);
        table.oid     = static_cast<std::uint32_t>(to_int64(rs.text(row, 5)));
        if (table.kind != ObjKind::view) {
            if (!rs.is_null(row, 3)) table.estimated_rows = to_int64(rs.text(row, 3));
            if (!rs.is_null(row, 4)) {
                table.size_bytes  = to_int64(rs.text(row, 4));
                table.size_pretty = format_size(table.size_bytes);
            }
        }
        out.push_back(std::move(table));
    }
    return out;
}

Result<std::vector<ColumnMeta>> SqlAnywhereCatalog::load_columns(std::string_view schema,
                                                                 std::string_view table) {
    // base_type_str e' o tipo como se escreve ("char(20)", "numeric(15,2)").
    // index_category 1 = a chave primaria.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT c.column_name, c.base_type_str, d.domain_name, c.nulls,"
            "       CASE WHEN EXISTS (SELECT 1 FROM SYS.SYSIDX i"
            "                           JOIN SYS.SYSIDXCOL ic"
            "                             ON ic.table_id = i.table_id"
            "                            AND ic.index_id = i.index_id"
            "                          WHERE i.table_id = c.table_id"
            "                            AND i.index_category = 1"
            "                            AND ic.column_id = c.column_id)"
            "            THEN 1 ELSE 0 END,"
            "       c.[default], r.remarks, c.column_id, c.column_type"
            "  FROM SYS.SYSTABCOL c"
            "  JOIN SYS.SYSTAB t ON t.table_id = c.table_id"
            "  JOIN SYS.SYSDOMAIN d ON d.domain_id = c.domain_id"
            "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = c.object_id"
            " WHERE " + sqlanywhere_table_filter("t", schema, table) +
            " ORDER BY c.column_id"));

    std::vector<ColumnMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ColumnMeta column;
        column.name      = text_of(rs, row, 0);
        column.type_name = text_of(rs, row, 1);
        if (column.type_name.empty()) column.type_name = text_of(rs, row, 2);
        column.kind        = sqlanywhere_kind(rs.text(row, 2));
        column.nullable    = trimmed(rs.text(row, 3)) == "Y";
        column.primary_key = rs.text(row, 4) == "1";
        column.default_value = text_of(rs, row, 5);
        column.comment     = text_of(rs, row, 6);
        column.position    = static_cast<std::int32_t>(to_int64(rs.text(row, 7)));

        // Coluna calculada: o "default" guarda a expressao.
        if (trimmed(rs.text(row, 8)) == "C" && !column.default_value.empty()) {
            column.default_value = "COMPUTE (" + column.default_value + ")";
        }
        out.push_back(std::move(column));
    }
    return out;
}

Result<std::vector<ForeignKeyMeta>> SqlAnywhereCatalog::foreign_keys_where(
    const std::string& condition) {
    // As colunas do lado referenciado saem de SYSIDXCOL.primary_column_id, na
    // mesma ordem das do indice da chave.
    const std::string target_columns =
        "(SELECT list(pc.column_name, ',' ORDER BY xi.sequence)"
        "   FROM SYS.SYSIDXCOL xi"
        "   JOIN SYS.SYSTABCOL pc"
        "     ON pc.table_id = k.primary_table_id AND pc.column_id = xi.primary_column_id"
        "  WHERE xi.table_id = k.foreign_table_id AND xi.index_id = k.foreign_index_id)";
    const auto action = [](const char* event) {
        return std::string("(SELECT max(tr.referential_action) FROM SYS.SYSTRIGGER tr"
                           "  WHERE tr.foreign_table_id = k.foreign_table_id"
                           "    AND tr.foreign_key_id = k.foreign_index_id"
                           "    AND tr.event = '") + event + "')";
    };

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT fi.index_name, ft.table_name, " +
            index_columns("k.foreign_table_id", "k.foreign_index_id", false) + ","
            "       pt.table_name, " + target_columns + ", " + action("C") + ", " +
            action("D") + ", fu.user_name, pu.user_name"
            "  FROM SYS.SYSFKEY k"
            "  JOIN SYS.SYSTAB ft ON ft.table_id = k.foreign_table_id"
            "  JOIN SYS.SYSIDX fi"
            "    ON fi.table_id = k.foreign_table_id AND fi.index_id = k.foreign_index_id"
            "  JOIN SYS.SYSTAB pt ON pt.table_id = k.primary_table_id"
            "  JOIN SYS.SYSUSER fu ON fu.user_id = ft.creator"
            "  JOIN SYS.SYSUSER pu ON pu.user_id = pt.creator"
            " WHERE " + condition +
            " ORDER BY ft.table_name, fi.index_name"));

    std::vector<ForeignKeyMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ForeignKeyMeta key;
        key.name          = text_of(rs, row, 0);
        key.source_table  = text_of(rs, row, 1);
        key.source_column = text_of(rs, row, 2);
        key.target_table  = text_of(rs, row, 3);
        key.target_column = text_of(rs, row, 4);
        key.on_update     = std::string(sqlanywhere_referential_action(rs.text(row, 5)));
        key.on_delete     = std::string(sqlanywhere_referential_action(rs.text(row, 6)));
        key.source_schema = text_of(rs, row, 7);
        key.target_schema = text_of(rs, row, 8);

        key.definition = "FOREIGN KEY (" + quoted_list(key.source_column) + ") REFERENCES " +
                         sqlanywhere_quote(key.target_schema) + "." +
                         sqlanywhere_quote(key.target_table) + " (" +
                         quoted_list(key.target_column) + ")";
        // RESTRICT e' o padrao: so' o que difere dele aparece.
        if (key.on_update != "RESTRICT") key.definition += " ON UPDATE " + key.on_update;
        if (key.on_delete != "RESTRICT") key.definition += " ON DELETE " + key.on_delete;
        out.push_back(std::move(key));
    }
    return out;
}

Result<std::vector<ForeignKeyMeta>> SqlAnywhereCatalog::load_foreign_keys(
    std::string_view schema) {
    return foreign_keys_where("ft.creator = user_id(" + sqlanywhere_literal(schema) + ")");
}

Result<std::vector<ForeignKeyMeta>> SqlAnywhereCatalog::load_table_foreign_keys(
    std::string_view schema, std::string_view table) {
    return foreign_keys_where(sqlanywhere_table_filter("ft", schema, table));
}

Result<std::vector<ForeignKeyMeta>> SqlAnywhereCatalog::load_references(
    std::string_view schema, std::string_view table) {
    return foreign_keys_where(sqlanywhere_table_filter("pt", schema, table));
}

Result<std::vector<ConstraintMeta>> SqlAnywhereCatalog::load_constraints(
    std::string_view schema, std::string_view table) {
    // P e U apontam para o INDICE que as sustenta; C (de coluna) e T (de
    // tabela) tem o texto em SYSCHECK. F (chave estrangeira) tem lista propria.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT c.constraint_name, c.constraint_type,"
            "       (SELECT list(xc.column_name, ',' ORDER BY xi.sequence)"
            "          FROM SYS.SYSIDX i"
            "          JOIN SYS.SYSIDXCOL xi"
            "            ON xi.table_id = i.table_id AND xi.index_id = i.index_id"
            "          JOIN SYS.SYSTABCOL xc"
            "            ON xc.table_id = xi.table_id AND xc.column_id = xi.column_id"
            "         WHERE i.object_id = c.ref_object_id),"
            "       ck.check_defn"
            "  FROM SYS.SYSCONSTRAINT c"
            "  JOIN SYS.SYSTAB t ON t.object_id = c.table_object_id"
            "  LEFT JOIN SYS.SYSCHECK ck ON ck.check_id = c.constraint_id"
            " WHERE " + sqlanywhere_table_filter("t", schema, table) +
            "   AND c.constraint_type IN ('P', 'U', 'C', 'T')"
            " ORDER BY CASE c.constraint_type WHEN 'P' THEN 0 WHEN 'U' THEN 1 ELSE 2 END,"
            "          c.constraint_name"));

    std::vector<ConstraintMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ConstraintMeta constraint;
        constraint.name    = text_of(rs, row, 0);
        constraint.columns = text_of(rs, row, 2);

        const std::string_view type = trimmed(rs.text(row, 1));
        if (type == "P") {
            constraint.kind       = ObjKind::primary_key;
            constraint.definition = "PRIMARY KEY (" + quoted_list(constraint.columns) + ")";
        } else if (type == "U") {
            constraint.kind       = ObjKind::unique_key;
            constraint.definition = "UNIQUE (" + quoted_list(constraint.columns) + ")";
        } else {
            constraint.kind = ObjKind::check_constraint;
            // O servidor guarda "check(...)" inteiro, em minusculas.
            std::string definition = text_of(rs, row, 3);
            if (lower(definition).starts_with("check")) definition.replace(0, 5, "CHECK ");
            constraint.definition = std::move(definition);
        }
        out.push_back(std::move(constraint));
    }
    return out;
}

Result<std::vector<IndexMeta>> SqlAnywhereCatalog::load_indexes(std::string_view schema,
                                                                std::string_view table) {
    // index_category: 1 chave primaria, 2 chave estrangeira, 3 indice, 4 indice
    // de texto. `unique`: 1 indice unico, 2 constraint UNIQUE (ou a PK), 4 e 5
    // nao unico.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT i.index_name, i.index_category, i.[unique], " +
            index_columns("i.table_id", "i.index_id", true) + ","
            "       CASE WHEN t.clustered_index_id = i.index_id THEN 1 ELSE 0 END,"
            "       (SELECT CAST(p.leaf_page_count AS bigint)"
            "                 * CAST(db_property('PageSize') AS bigint)"
            "          FROM SYS.SYSPHYSIDX p"
            "         WHERE p.table_id = i.table_id AND p.phys_index_id = i.phys_index_id)"
            "  FROM SYS.SYSIDX i"
            "  JOIN SYS.SYSTAB t ON t.table_id = i.table_id"
            " WHERE " + sqlanywhere_table_filter("t", schema, table) +
            " ORDER BY i.index_category, i.index_name"));

    std::vector<IndexMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        IndexMeta index;
        index.name = text_of(rs, row, 0);

        const std::string_view category = rs.text(row, 1);
        const std::string_view unique   = rs.text(row, 2);
        index.primary = category == "1";
        index.unique  = unique == "1" || unique == "2";
        index.columns = text_of(rs, row, 3);
        index.method  = category == "1"   ? "PRIMARY KEY"
                        : category == "2" ? "FOREIGN KEY"
                        : category == "4" ? "TEXT"
                        : unique == "2"   ? "UNIQUE CONSTRAINT"
                                          : "INDEX";
        if (rs.text(row, 4) == "1") index.method += ", CLUSTERED";
        if (!rs.is_null(row, 5)) index.size_pretty = format_size(to_int64(rs.text(row, 5)));

        // So' o indice comum se cria por CREATE INDEX: o de chave primaria, de
        // chave estrangeira e de UNIQUE nasce com a constraint, e o de texto
        // tem comando proprio.
        if (category == "3" && unique != "2") {
            std::string columns;
            {
                std::size_t start = 0;
                while (start <= index.columns.size()) {
                    const std::size_t comma = index.columns.find(',', start);
                    std::string part = index.columns.substr(
                        start, comma == std::string::npos ? std::string::npos : comma - start);
                    const bool desc = part.ends_with(" DESC");
                    if (desc) part.resize(part.size() - 5);
                    if (!columns.empty()) columns += ", ";
                    columns += sqlanywhere_quote(part) + (desc ? " DESC" : "");
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            index.definition = std::string("CREATE ") + (index.unique ? "UNIQUE " : "") +
                               "INDEX " + sqlanywhere_quote(index.name) + " ON " +
                               sqlanywhere_quote(schema) + "." + sqlanywhere_quote(table) +
                               " (" + columns + ")";
        }
        out.push_back(std::move(index));
    }
    return out;
}

Result<std::vector<TriggerMeta>> SqlAnywhereCatalog::load_triggers(std::string_view schema,
                                                                   std::string_view table) {
    // As linhas com foreign_table_id sao as acoes referenciais das chaves
    // estrangeiras (ON DELETE CASCADE...), que o servidor guarda como
    // triggers sem nome: nao sao triggers do usuario.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT tr.trigger_name, tr.trigger_time, tr.event,"
            "       COALESCE(tr.source, tr.trigger_defn)"
            "  FROM SYS.SYSTRIGGER tr"
            "  JOIN SYS.SYSTAB t ON t.table_id = tr.table_id"
            " WHERE " + sqlanywhere_table_filter("t", schema, table) +
            "   AND tr.trigger_name IS NOT NULL AND tr.foreign_table_id IS NULL"
            " ORDER BY tr.trigger_name"));

    std::vector<TriggerMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TriggerMeta trigger;
        trigger.name       = text_of(rs, row, 0);
        trigger.table      = std::string(table);
        trigger.timing     = std::string(sqlanywhere_trigger_timing(rs.text(row, 1)));
        trigger.events     = std::string(sqlanywhere_trigger_events(rs.text(row, 2)));
        trigger.definition = text_of(rs, row, 3);
        out.push_back(std::move(trigger));
    }
    return out;
}

Result<std::vector<SequenceMeta>> SqlAnywhereCatalog::load_sequences(
    std::string_view schema) {
    // resume_at e' o proximo valor que a sequence vai entregar.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT s.sequence_name, s.resume_at, s.start_with, s.increment_by,"
            "       s.min_value, s.max_value, s.cycle, r.remarks"
            "  FROM SYS.SYSSEQUENCE s"
            "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = s.object_id"
            " WHERE s.owner = user_id(" + sqlanywhere_literal(schema) + ")"
            " ORDER BY s.sequence_name"));

    std::vector<SequenceMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SequenceMeta sequence;
        sequence.name        = std::string(trimmed(rs.text(row, 0)));
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

Result<std::vector<RoutineMeta>> SqlAnywhereCatalog::load_routines(std::string_view schema) {
    // parm_type: 0 = parametro, 1 = coluna do resultado, 4 = o valor de
    // retorno (so' funcao tem).
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT p.proc_name,"
            "       (SELECT pp.base_type_str FROM SYS.SYSPROCPARM pp"
            "         WHERE pp.proc_id = p.proc_id AND pp.parm_type = 4),"
            "       (SELECT list(CASE WHEN pp.parm_mode_in = 'Y' AND pp.parm_mode_out = 'Y'"
            "                         THEN 'INOUT '"
            "                         WHEN pp.parm_mode_out = 'Y' THEN 'OUT ' ELSE 'IN ' END"
            "                    || pp.parm_name || ' ' || pp.base_type_str,"
            "                    ', ' ORDER BY pp.parm_id)"
            "          FROM SYS.SYSPROCPARM pp"
            "         WHERE pp.proc_id = p.proc_id AND pp.parm_type = 0),"
            "       COALESCE(r.remarks, p.remarks),"
            "       CASE WHEN EXISTS (SELECT 1 FROM SYS.SYSPROCPARM pp"
            "                          WHERE pp.proc_id = p.proc_id AND pp.parm_type = 1)"
            "            THEN 1 ELSE 0 END"
            "  FROM SYS.SYSPROCEDURE p"
            "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = p.object_id"
            " WHERE p.creator = user_id(" + sqlanywhere_literal(schema) + ")"
            " ORDER BY p.proc_name"));

    std::vector<RoutineMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        RoutineMeta routine;
        routine.name      = text_of(rs, row, 0);
        routine.kind      = rs.is_null(row, 1) ? ObjKind::procedure : ObjKind::function;
        routine.arguments = text_of(rs, row, 2);
        // Nao ha' sobrecarga: o nome identifica a rotina.
        routine.signature.clear();
        routine.return_type = rs.is_null(row, 1)
                                  ? (rs.text(row, 4) == "1" ? std::string("TABLE")
                                                            : std::string{})
                                  : text_of(rs, row, 1);
        routine.language = "SQL";
        routine.comment  = text_of(rs, row, 3);
        out.push_back(std::move(routine));
    }
    return out;
}

Result<std::vector<DataTypeMeta>> SqlAnywhereCatalog::load_types(std::string_view schema) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT ut.type_name, ut.base_type_str, ut.nulls, ut.[default], ut.[check]"
            "  FROM SYS.SYSUSERTYPE ut"
            " WHERE ut.creator = user_id(" + sqlanywhere_literal(schema) + ")"
            " ORDER BY ut.type_name"));

    std::vector<DataTypeMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        DataTypeMeta type;
        type.name          = text_of(rs, row, 0);
        type.kind          = TypeKind::domain;
        type.owner         = std::string(schema);
        type.base_type     = text_of(rs, row, 1);
        // 'U' = nao declarado (vale o padrao da conexao).
        type.not_null      = trimmed(rs.text(row, 2)) == "N";
        type.default_value = text_of(rs, row, 3);
        type.check_constraint = text_of(rs, row, 4);
        out.push_back(std::move(type));
    }
    return out;
}

Result<std::string> SqlAnywhereCatalog::load_routine_definition(std::string_view schema,
                                                                std::string_view name,
                                                                std::string_view) {
    // `source` e' o texto como foi escrito (comentarios e formato); proc_defn
    // e' o que o servidor reescreveu. O primeiro so' existe com a opcao
    // preserve_source_format ligada, que e' o padrao.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT COALESCE(p.source, p.proc_defn)"
            "  FROM SYS.SYSPROCEDURE p"
            " WHERE p.creator = user_id(" + sqlanywhere_literal(schema) + ")"
            "   AND p.proc_name = " + sqlanywhere_literal(name)));
    if (rs.row_count() == 0 || rs.is_null(0, 0)) {
        return fail(Errc::not_found,
                    "the definition is not available (hidden with ALTER ... SET HIDDEN, "
                    "or no permission)");
    }
    return std::string(rs.text(0, 0));
}

Result<std::string> SqlAnywhereCatalog::load_view_definition(std::string_view schema,
                                                             std::string_view name) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT COALESCE(s.source, v.view_def)"
            "  FROM SYS.SYSTAB t"
            "  JOIN SYS.SYSVIEW v ON v.view_object_id = t.object_id"
            "  LEFT JOIN SYS.SYSSOURCE s ON s.object_id = t.object_id"
            " WHERE " + sqlanywhere_table_filter("t", schema, name)));
    if (rs.row_count() == 0 || rs.is_null(0, 0)) {
        return fail(Errc::not_found, "the view definition is not available");
    }
    return std::string(rs.text(0, 0));
}

Result<std::vector<PartitionMeta>> SqlAnywhereCatalog::load_partitions(std::string_view,
                                                                       std::string_view) {
    return std::vector<PartitionMeta>{};
}

Result<std::vector<EventMeta>> SqlAnywhereCatalog::load_events(std::string_view schema) {
    // Um evento dispara por AGENDA (SYSSCHEDULE) ou por um evento do sistema
    // (SYSEVENTTYPE: "BackupEnd", "DiskSpace"...), nunca pelos dois.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT e.event_name, u.user_name, et.name, e.enabled, e.condition,"
            "       COALESCE(e.source, e.action), e.remarks,"
            "       (SELECT list(s.sched_name || CASE WHEN s.recurring = 1"
            "                                         THEN ' (recurring)' ELSE '' END, ', ')"
            "          FROM SYS.SYSSCHEDULE s WHERE s.event_id = e.event_id),"
            "       (SELECT min(s.start_date) FROM SYS.SYSSCHEDULE s"
            "         WHERE s.event_id = e.event_id)"
            "  FROM SYS.SYSEVENT e"
            "  JOIN SYS.SYSUSER u ON u.user_id = e.creator"
            "  LEFT JOIN SYS.SYSEVENTTYPE et ON et.event_type_id = e.event_type_id"
            " WHERE e.creator = user_id(" + sqlanywhere_literal(schema) + ")"
            " ORDER BY e.event_name"));

    std::vector<EventMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        EventMeta event;
        event.name    = text_of(rs, row, 0);
        event.definer = text_of(rs, row, 1);

        const std::string system_event = text_of(rs, row, 2);
        const std::string schedules    = text_of(rs, row, 7);
        if (!system_event.empty()) {
            event.type     = "SYSTEM EVENT";
            event.schedule = system_event;
            const std::string condition = text_of(rs, row, 4);
            if (!condition.empty()) event.schedule += " WHERE " + condition;
        } else if (!schedules.empty()) {
            event.type     = schedules.find("(recurring)") != std::string::npos ? "RECURRING"
                                                                                : "ONE TIME";
            event.schedule = schedules;
        } else {
            event.type = "MANUAL";   // so' roda por TRIGGER EVENT
        }
        event.starts     = text_of(rs, row, 8);
        event.status     = trimmed(rs.text(row, 3)) == "Y" ? "ENABLED" : "DISABLED";
        event.definition = text_of(rs, row, 5);
        event.comment    = text_of(rs, row, 6);
        out.push_back(std::move(event));
    }
    return out;
}

// --- Listas ------------------------------------------------------------------------

Result<std::vector<CatalogItem>> SqlAnywhereCatalog::load_list(CatalogList list,
                                                               std::string_view a,
                                                               std::string_view b,
                                                               std::string_view) {
    // (nome, detalhe, dica, flag): toda lista devolve essas quatro colunas.
    std::string sql;
    switch (list) {
        case CatalogList::schema_indexes:
            sql = "SELECT i.index_name, t.table_name,"
                  "       CASE i.index_category WHEN 1 THEN 'PRIMARY KEY'"
                  "            WHEN 2 THEN 'FOREIGN KEY' WHEN 4 THEN 'TEXT' ELSE 'INDEX' END,"
                  "       CASE WHEN i.[unique] IN (1, 2) THEN 1 ELSE 0 END"
                  "  FROM SYS.SYSIDX i"
                  "  JOIN SYS.SYSTAB t ON t.table_id = i.table_id"
                  " WHERE t.creator = user_id(" + sqlanywhere_literal(a) + ")"
                  "   AND t.table_type IN (1, 2, 3)"
                  " ORDER BY i.index_name, t.table_name";
            break;

        case CatalogList::routine_parameters:
            // Os parametros, o retorno e as colunas do conjunto de resultado.
            sql = "SELECT CASE WHEN pp.parm_type = 4 THEN '(return)' ELSE pp.parm_name END,"
                  "       pp.base_type_str,"
                  "       CASE WHEN pp.parm_type = 1 THEN 'RESULT'"
                  "            WHEN pp.parm_type = 4 THEN 'RETURN'"
                  "            WHEN pp.parm_mode_in = 'Y' AND pp.parm_mode_out = 'Y' THEN 'INOUT'"
                  "            WHEN pp.parm_mode_out = 'Y' THEN 'OUT' ELSE 'IN' END,"
                  "       CASE WHEN pp.parm_mode_out = 'Y' THEN 1 ELSE 0 END"
                  "  FROM SYS.SYSPROCPARM pp"
                  "  JOIN SYS.SYSPROCEDURE p ON p.proc_id = pp.proc_id"
                  " WHERE p.creator = user_id(" + sqlanywhere_literal(a) + ")"
                  "   AND p.proc_name = " + sqlanywhere_literal(b) +
                  "   AND pp.parm_type IN (0, 1, 4)"
                  " ORDER BY pp.parm_id";
            break;

        case CatalogList::dependencies:
            // O que ESTA view usa. O servidor so' registra dependencia de
            // view: tabela e rotina nao tem linha aqui.
            sql = "SELECT ru.user_name || '.' || rt.table_name, rt.table_type_str, '', 0"
                  "  FROM SYS.SYSDEPENDENCY d"
                  "  JOIN SYS.SYSTAB t ON t.object_id = d.dep_object_id"
                  "  JOIN SYS.SYSTAB rt ON rt.object_id = d.ref_object_id"
                  "  JOIN SYS.SYSUSER ru ON ru.user_id = rt.creator"
                  " WHERE " + sqlanywhere_table_filter("t", a, b) +
                  " ORDER BY 1";
            break;

        case CatalogList::roles:
            // Quem pode receber privilegio: usuarios e papeis. flag = entra no
            // banco (e' usuario). O bit 1 de user_type marca o papel puro.
            sql = "SELECT u.user_name,"
                  "       CASE WHEN (u.user_type & 1) = 0 AND (u.user_type & 2) <> 0"
                  "            THEN 'user, role'"
                  "            WHEN (u.user_type & 1) = 0 THEN 'user' ELSE 'role' END,"
                  "       COALESCE(r.remarks, ''),"
                  "       CASE WHEN (u.user_type & 1) = 0 THEN 1 ELSE 0 END"
                  "  FROM SYS.SYSUSER u"
                  "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = u.object_id"
                  " WHERE u.user_name NOT LIKE 'SYS[_]%[_]ROLE' AND u.user_name <> 'SYS'"
                  " ORDER BY u.user_name";
            break;

        case CatalogList::role_members:
            sql = "SELECT g.grantee_name, '', '', 0 FROM SYS.SYSROLEGRANTS g"
                  " WHERE g.role_name = " + sqlanywhere_literal(a) +
                  " ORDER BY g.grantee_name";
            break;

        case CatalogList::role_belongs:
            sql = "SELECT g.role_name, '', '', 0 FROM SYS.SYSROLEGRANTS g"
                  " WHERE g.grantee_name = " + sqlanywhere_literal(a) +
                  " ORDER BY g.role_name";
            break;

        case CatalogList::tablespaces:
            // Os dbspaces: cada um e' um arquivo do banco.
            sql = "SELECT d.dbspace_name, f.file_name, '', 0"
                  "  FROM SYS.SYSDBSPACE d"
                  "  LEFT JOIN SYS.SYSDBFILE f ON f.dbspace_id = d.dbspace_id"
                  " ORDER BY d.dbspace_id";
            break;

        case CatalogList::settings:
            // As opcoes do banco (as de PUBLIC valem para todos).
            sql = "SELECT o.[option], o.setting, '', 0 FROM SYS.SYSOPTIONS o"
                  " WHERE o.user_name = 'PUBLIC' ORDER BY o.[option]";
            break;

        case CatalogList::pure_roles:
            // So' os papeis: o bit 1 de user_type (quem nao entra no banco).
            // Os SYS_..._ROLE sao os privilegios de sistema vistos como papel.
            sql = "SELECT u.user_name, '', COALESCE(r.remarks, ''), 0"
                  "  FROM SYS.SYSUSER u"
                  "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = u.object_id"
                  " WHERE (u.user_type & 1) <> 0"
                  "   AND u.user_name NOT LIKE 'SYS[_]%[_]ROLE' AND u.user_name <> 'SYS'"
                  " ORDER BY u.user_name";
            break;

        case CatalogList::foreign_servers:
            // Os servidores remotos (CREATE SERVER): a classe e a conexao.
            sql = "SELECT s.srvname, s.srvclass, s.srvinfo,"
                  "       CASE WHEN s.srvreadonly = 'Y' THEN 1 ELSE 0 END"
                  "  FROM SYS.SYSSERVER s ORDER BY s.srvname";
            break;

        case CatalogList::user_mappings:
            // Os logins externos de um servidor remoto (CREATE EXTERNLOGIN).
            sql = "SELECT u.user_name, COALESCE(e.remote_login, ''), '', 0"
                  "  FROM SYS.SYSEXTERNLOGIN e"
                  "  JOIN SYS.SYSUSER u ON u.user_id = e.user_id"
                  "  JOIN SYS.SYSSERVER s ON s.srvid = e.srvid"
                  " WHERE s.srvname = " + sqlanywhere_literal(a) +
                  " ORDER BY u.user_name";
            break;

        case CatalogList::web_services:
            sql = "SELECT w.service_name, w.service_type, COALESCE(w.remarks, ''),"
                  "       CASE WHEN w.enabled = 'Y' THEN 1 ELSE 0 END"
                  "  FROM SYS.SYSWEBSERVICE w"
                  "  LEFT JOIN SYS.SYSREMARK r ON r.object_id = w.object_id"
                  " ORDER BY w.service_name";
            break;

        case CatalogList::login_policies:
            sql = "SELECT p.login_policy_name, '', '', 0 FROM SYS.SYSLOGINPOLICY p"
                  " ORDER BY p.login_policy_name";
            break;

        case CatalogList::login_policy_options:
            sql = "SELECT o.login_option_name, o.login_option_value, '', 0"
                  "  FROM SYS.SYSLOGINPOLICYOPTION o"
                  "  JOIN SYS.SYSLOGINPOLICY p ON p.login_policy_id = o.login_policy_id"
                  " WHERE p.login_policy_name = " + sqlanywhere_literal(a) +
                  " ORDER BY o.login_option_name";
            break;

        case CatalogList::publications:
            sql = "SELECT p.publication_name, u.user_name, COALESCE(p.remarks, ''), 0"
                  "  FROM SYS.SYSPUBLICATION p"
                  "  JOIN SYS.SYSUSER u ON u.user_id = p.creator"
                  " ORDER BY p.publication_name";
            break;

        case CatalogList::text_configurations:
            sql = "SELECT c.text_config_name, u.user_name,"
                  "       'terms of ' || c.min_term_length || ' to ' || c.max_term_length ||"
                  "       ' characters', 0"
                  "  FROM SYS.SYSTEXTCONFIG c"
                  "  JOIN SYS.SYSUSER u ON u.user_id = c.creator"
                  " ORDER BY c.text_config_name";
            break;

        case CatalogList::external_environments:
            sql = "SELECT e.name, COALESCE(e.location, ''), '', 0 FROM SYS.SYSEXTERNENV e"
                  " ORDER BY e.name";
            break;

        case CatalogList::spatial_reference_systems:
            sql = "SELECT s.srs_name, CAST(s.srs_id AS varchar(20)),"
                  "       s.srs_type || ', ' || COALESCE(s.organization, ''), 0"
                  "  FROM SYS.ST_SPATIAL_REFERENCE_SYSTEMS s ORDER BY s.srs_id";
            break;

        default:
            // As demais listas sao do `<tree>` do PostgreSQL: nao existem aqui.
            return std::vector<CatalogItem>{};
    }

    OTTER_ASSIGN_OR_RETURN(auto rs, holt_.query_internal(sql));

    std::vector<CatalogItem> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        CatalogItem item;
        item.name    = text_of(rs, row, 0);
        item.detail  = std::string(trimmed(rs.is_null(row, 1) ? std::string_view{}
                                                              : rs.text(row, 1)));
        item.tooltip = text_of(rs, row, 2);
        item.flag    = rs.text(row, 3) == "1";
        out.push_back(std::move(item));
    }
    return out;
}

// --- Definicoes montadas -------------------------------------------------------------

Result<std::string> SqlAnywhereCatalog::load_table_definition(std::string_view schema,
                                                              std::string_view table) {
    const char* const options_sql =
        "SELECT OptionName, Value FROM sa_conn_options(connection_property('Number'))";

    OTTER_ASSIGN_OR_RETURN(auto before, holt_.query_internal(options_sql));

    auto definition = holt_.query_internal(
        "SELECT sa_get_table_definition(" + sqlanywhere_literal(schema) + ", " +
        sqlanywhere_literal(table) + ")");

    // Repoe o que a funcao trocou -- tenha ela dado certo ou nao. Uma opcao
    // que nao se consegue repor nao derruba a leitura: o DDL ja' veio.
    if (auto after = holt_.query_internal(options_sql)) {
        for (std::size_t row = 0; row < after->row_count(); ++row) {
            const std::string_view name = after->text(row, 0);
            for (std::size_t old = 0; old < before.row_count(); ++old) {
                if (before.text(old, 0) != name) continue;
                const std::string was = text_of(before, old, 1);
                if (was != text_of(*after, row, 1)) {
                    (void)holt_.query_internal("SET TEMPORARY OPTION [" + std::string(name) +
                                               "] = " + sqlanywhere_literal(was));
                }
                break;
            }
        }
    }

    if (!definition) return std::unexpected(definition.error());
    if (definition->row_count() == 0 || definition->is_null(0, 0)) {
        return fail(Errc::not_found, "the table definition is not available");
    }
    return std::string(definition->text(0, 0));
}

Result<std::string> SqlAnywhereCatalog::load_event_definition(std::string_view name) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT e.event_id, et.name, e.condition, e.enabled, e.location,"
            "       COALESCE(e.source, e.action)"
            "  FROM SYS.SYSEVENT e"
            "  LEFT JOIN SYS.SYSEVENTTYPE et ON et.event_type_id = e.event_type_id"
            " WHERE e.event_name = " + sqlanywhere_literal(name)));
    if (rs.row_count() == 0) return fail(Errc::not_found, "the event no longer exists");

    std::string out = "CREATE EVENT " + sqlanywhere_quote(name);

    const std::string system_event = text_of(rs, 0, 1);
    if (!system_event.empty()) {
        out += "\nTYPE " + system_event;
        const std::string condition = text_of(rs, 0, 2);
        if (!condition.empty()) out += "\nWHERE " + condition;
    } else {
        // As agendas. interval_units: 'HH', 'NN' (minutos) ou 'SS'.
        OTTER_ASSIGN_OR_RETURN(
            auto schedules,
            holt_.query_internal(
                "SELECT s.sched_name, dateformat(s.start_time, 'HH:NN:SS'),"
                "       dateformat(s.stop_time, 'HH:NN:SS'), s.interval_units,"
                "       s.interval_amt, dateformat(s.start_date, 'YYYY-MM-DD'),"
                "       s.days_of_week, s.days_of_month"
                "  FROM SYS.SYSSCHEDULE s WHERE s.event_id = " + text_of(rs, 0, 0) +
                " ORDER BY s.sched_name"));
        for (std::size_t row = 0; row < schedules.row_count(); ++row) {
            out += row == 0 ? "\nSCHEDULE " : ",\n         ";
            out += sqlanywhere_quote(schedules.text(row, 0));

            const std::string start = text_of(schedules, row, 1);
            const std::string stop  = text_of(schedules, row, 2);
            if (!stop.empty()) {
                out += " BETWEEN '" + start + "' AND '" + stop + "'";
            } else if (!start.empty()) {
                out += " START TIME '" + start + "'";
            }

            const std::string_view units = trimmed(schedules.text(row, 3));
            if (!schedules.is_null(row, 4) && !units.empty()) {
                out += " EVERY " + text_of(schedules, row, 4) +
                       (units == "HH" ? " HOURS" : units == "NN" ? " MINUTES" : " SECONDS");
            }

            // Os dias: mascara de bits. Da semana, o bit 0 e' domingo; do mes,
            // o bit 0 e' o dia 1 (o bit 31, o ultimo dia).
            static constexpr const char* kDays[] = {"Sunday", "Monday", "Tuesday",
                                                    "Wednesday", "Thursday", "Friday",
                                                    "Saturday"};
            const std::int64_t week  = schedules.is_null(row, 6)
                                           ? 0 : to_int64(schedules.text(row, 6));
            const std::int64_t month = schedules.is_null(row, 7)
                                           ? 0 : to_int64(schedules.text(row, 7));
            std::string days;
            for (int bit = 0; bit < 7; ++bit) {
                if ((week & (std::int64_t{1} << bit)) == 0) continue;
                days += (days.empty() ? "'" : ", '") + std::string(kDays[bit]) + "'";
            }
            for (int bit = 0; bit < 32 && week == 0; ++bit) {
                if ((month & (std::int64_t{1} << bit)) == 0) continue;
                days += (days.empty() ? "" : ", ") + std::to_string(bit == 31 ? 0 : bit + 1);
            }
            if (!days.empty()) out += " ON (" + days + ")";

            const std::string date = text_of(schedules, row, 5);
            if (!date.empty()) out += " START DATE '" + date + "'";
        }
    }

    if (trimmed(rs.text(0, 3)) != "Y") out += "\nDISABLE";
    const std::string_view location = trimmed(rs.text(0, 4));
    if (location == "C") out += "\nAT CONSOLIDATED";
    if (location == "R") out += "\nAT REMOTE";

    out += "\nHANDLER\n" + text_of(rs, 0, 5);
    return out;
}

// --- Usuarios ----------------------------------------------------------------------

Result<std::vector<UserMeta>> SqlAnywhereCatalog::load_users() {
    // Quem ENTRA no banco: o bit 1 de user_type marca o papel puro. O
    // bloqueio vem da politica de login (sa_get_user_status), que so' quem tem
    // MANAGE ANY USER le' para os outros -- sem ele, fica desconhecido.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT u.user_name, p.login_policy_name,"
            "       CASE WHEN u.expire_password_on_login = 1 THEN 1 ELSE 0 END"
            "  FROM SYS.SYSUSER u"
            "  LEFT JOIN SYS.SYSLOGINPOLICY p ON p.login_policy_id = u.login_policy_id"
            " WHERE (u.user_type & 1) = 0"
            " ORDER BY u.user_name"));

    std::vector<UserMeta> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        UserMeta user;
        user.name    = text_of(rs, row, 0);
        user.plugin  = text_of(rs, row, 1);   // a politica de login faz esse papel
        user.expired = rs.text(row, 2) == "1";
        out.push_back(std::move(user));
    }

    if (auto status = holt_.query_internal(
            "SELECT user_name, locked FROM sa_get_user_status()")) {
        for (std::size_t row = 0; row < status->row_count(); ++row) {
            const std::string name = text_of(*status, row, 0);
            for (UserMeta& user : out) {
                if (user.name == name) user.locked = status->text(row, 1) == "1";
            }
        }
    }
    return out;
}

Result<std::vector<std::string>> SqlAnywhereCatalog::load_grants(std::string_view user) {
    std::vector<std::string> out;

    // Os papeis (e privilegios de sistema, que sao papeis SYS_..._ROLE).
    // grant_type: bit 1 = com direito de administrar.
    OTTER_ASSIGN_OR_RETURN(
        auto roles,
        holt_.query_internal(
            "SELECT g.role_name, g.grant_type FROM SYS.SYSROLEGRANTS g"
            " WHERE g.grantee_name = " + sqlanywhere_literal(user) +
            " ORDER BY g.role_name"));
    for (std::size_t row = 0; row < roles.row_count(); ++row) {
        std::string line = "GRANT ROLE " + sqlanywhere_quote(roles.text(row, 0)) + " TO " +
                           sqlanywhere_quote(user);
        const std::int64_t type = to_int64(roles.text(row, 1));
        if ((type & 2) != 0) line += " WITH ADMIN OPTION";
        out.push_back(std::move(line));
    }

    // As permissoes de tabela: 'Y' concedida, 'G' com direito de repassar.
    OTTER_ASSIGN_OR_RETURN(
        auto tables,
        holt_.query_internal(
            "SELECT o.user_name, t.table_name, p.selectauth, p.insertauth, p.deleteauth,"
            "       p.updateauth, p.alterauth, p.referenceauth, p.loadauth, p.truncateauth"
            "  FROM SYS.SYSTABLEPERM p"
            "  JOIN SYS.SYSTAB t ON t.table_id = p.stable_id"
            "  JOIN SYS.SYSUSER o ON o.user_id = t.creator"
            "  JOIN SYS.SYSUSER g ON g.user_id = p.grantee"
            " WHERE g.user_name = " + sqlanywhere_literal(user) +
            " ORDER BY o.user_name, t.table_name"));
    static constexpr const char* kPrivileges[] = {"SELECT", "INSERT", "DELETE", "UPDATE",
                                                  "ALTER", "REFERENCES", "LOAD", "TRUNCATE"};
    for (std::size_t row = 0; row < tables.row_count(); ++row) {
        std::string plain, grantable;
        for (std::size_t i = 0; i < 8; ++i) {
            const std::string_view flag = trimmed(tables.text(row, 2 + i));
            std::string& target = flag == "G" ? grantable : plain;
            if (flag != "Y" && flag != "G") continue;
            if (!target.empty()) target += ", ";
            target += kPrivileges[i];
        }
        const std::string object = sqlanywhere_quote(tables.text(row, 0)) + "." +
                                   sqlanywhere_quote(tables.text(row, 1));
        if (!plain.empty()) {
            out.push_back("GRANT " + plain + " ON " + object + " TO " +
                          sqlanywhere_quote(user));
        }
        if (!grantable.empty()) {
            out.push_back("GRANT " + grantable + " ON " + object + " TO " +
                          sqlanywhere_quote(user) + " WITH GRANT OPTION");
        }
    }

    // EXECUTE em rotinas.
    OTTER_ASSIGN_OR_RETURN(
        auto procedures,
        holt_.query_internal(
            "SELECT o.user_name, p.proc_name"
            "  FROM SYS.SYSPROCPERM pp"
            "  JOIN SYS.SYSPROCEDURE p ON p.proc_id = pp.proc_id"
            "  JOIN SYS.SYSUSER o ON o.user_id = p.creator"
            "  JOIN SYS.SYSUSER g ON g.user_id = pp.grantee"
            " WHERE g.user_name = " + sqlanywhere_literal(user) +
            " ORDER BY o.user_name, p.proc_name"));
    for (std::size_t row = 0; row < procedures.row_count(); ++row) {
        out.push_back("GRANT EXECUTE ON " + sqlanywhere_quote(procedures.text(row, 0)) + "." +
                      sqlanywhere_quote(procedures.text(row, 1)) + " TO " +
                      sqlanywhere_quote(user));
    }
    return out;
}

// --- Propriedades e opcoes ----------------------------------------------------------

Result<std::vector<ServerVariable>> SqlAnywhereCatalog::load_properties(
    std::string_view procedure) {
    // sa_conn_properties, sa_eng_properties e sa_db_properties: as tres
    // devolvem (PropName, PropDescription, Value). So' o nome fixo de uma das
    // tres entra na consulta.
    std::string source;
    if (procedure == "connection")    source = "sa_conn_properties(connection_property('Number'))";
    else if (procedure == "server")   source = "sa_eng_properties()";
    else if (procedure == "database") source = "sa_db_properties()";
    else return std::vector<ServerVariable>{};

    OTTER_ASSIGN_OR_RETURN(
        auto rs, holt_.query_internal("SELECT PropName, Value, PropDescription FROM " +
                                      source + " ORDER BY PropName"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable variable;
        variable.name   = text_of(rs, row, 0);
        variable.value  = text_of(rs, row, 1);
        variable.detail = text_of(rs, row, 2);
        out.push_back(std::move(variable));
    }
    return out;
}

Result<std::vector<ServerVariable>> SqlAnywhereCatalog::load_options() {
    // As opcoes EM VIGOR nesta conexao (as de PUBLIC, sobrepostas pelas do
    // usuario e pelas temporarias).
    OTTER_ASSIGN_OR_RETURN(
        auto rs, holt_.query_internal("SELECT OptionName, Value, OptionDescription"
                                      "  FROM sa_conn_options(connection_property('Number'))"
                                      " ORDER BY OptionName"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable variable;
        variable.name   = text_of(rs, row, 0);
        variable.value  = text_of(rs, row, 1);
        variable.detail = text_of(rs, row, 2);
        out.push_back(std::move(variable));
    }
    return out;
}

} // namespace otter::db
