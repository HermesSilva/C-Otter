#include "db/catalog_oracle.hpp"

#include <charconv>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>

// Uma regra vale para o arquivo inteiro: cada consulta le UMA view do
// dicionario, e o que seria um JOIN e' juntado aqui, no cliente.
//
// Nao e' estilo. As views ALL_* sao elas mesmas consultas de varias tabelas
// internas, e o otimizador se perde ao junta-las: a primeira versao de
// load_tables fazia ALL_OBJECTS + ALL_TABLES + ALL_TAB_COMMENTS + ALL_MVIEWS
// numa consulta so' e levou 10 segundos para 41 linhas (medido no 23.26).
// Cada view sozinha responde em 0,1 s.

namespace otter::db {
namespace {

int to_int(std::string_view text, int fallback) {
    int value = fallback;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

// MAXVALUE de uma sequence tem 28 digitos: satura em vez de transbordar.
std::int64_t to_int64(std::string_view text) {
    return std::strtoll(std::string(text).c_str(), nullptr, 10);
}

// (dono, nome) de uma constraint ou de um indice.
using OwnedName = std::pair<std::string, std::string>;

// "'A', 'B'" para um IN (...).
std::string in_list(const std::set<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        if (!out.empty()) out += ", ";
        out += oracle_literal(value);
    }
    return out;
}

// As colunas de cada constraint, na ordem da chave: (dono, nome) -> "a,b".
Result<std::map<OwnedName, std::string>> constraint_columns(Holt& holt,
                                                            const std::string& condition) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs, holt.query_internal(
                     "SELECT c.owner, c.constraint_name, c.column_name "
                     "  FROM all_cons_columns c WHERE " + condition +
                     " ORDER BY c.owner, c.constraint_name, c.position"));

    std::map<OwnedName, std::string> columns;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        std::string& list =
            columns[{std::string(rs.text(r, 0)), std::string(rs.text(r, 1))}];
        if (!list.empty()) list += ',';
        list += rs.text(r, 2);
    }
    return columns;
}

} // namespace

std::string oracle_literal(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        out += c;
        if (c == '\'') out += c;
    }
    out += '\'';
    return out;
}

std::string oracle_type_text(std::string_view type, int length, int precision, int scale,
                             int char_length, bool char_semantics) {
    std::string out(type);
    if (type == "VARCHAR2" || type == "VARCHAR" || type == "CHAR") {
        out += "(" + std::to_string(char_semantics ? char_length : length) +
               (char_semantics ? " CHAR)" : ")");
    } else if (type == "NVARCHAR2" || type == "NCHAR") {
        out += "(" + std::to_string(char_length) + ")";
    } else if (type == "RAW") {
        out += "(" + std::to_string(length) + ")";
    } else if (type == "NUMBER" && precision >= 0) {
        out += "(" + std::to_string(precision);
        if (scale > 0) out += "," + std::to_string(scale);
        out += ")";
    } else if (type == "FLOAT" && precision >= 0) {
        out += "(" + std::to_string(precision) + ")";
    }
    return out;
}

DataKind oracle_kind(std::string_view type, int precision, int scale) noexcept {
    if (type == "NUMBER") {
        return precision > 0 && scale == 0 ? DataKind::integer : DataKind::numeric;
    }
    if (type == "FLOAT" || type == "BINARY_FLOAT" || type == "BINARY_DOUBLE") {
        return DataKind::floating;
    }
    if (type == "DATE" || type.starts_with("TIMESTAMP")) return DataKind::timestamp;
    if (type.starts_with("INTERVAL")) return DataKind::interval;
    if (type == "RAW" || type == "LONG RAW" || type == "BLOB" || type == "BFILE") {
        return DataKind::binary;
    }
    if (type == "BOOLEAN") return DataKind::boolean;
    if (type == "JSON") return DataKind::json;
    return DataKind::string;
}

OracleCatalog::OracleCatalog(Holt& holt) : holt_(holt) {}

Result<std::vector<SchemaMeta>> OracleCatalog::load_schemas() {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT u.username FROM all_users u "
            "WHERE u.oracle_maintained = 'N' "
            "   OR u.username = SYS_CONTEXT('USERENV','CURRENT_SCHEMA') "
            "ORDER BY u.username"));

    std::vector<SchemaMeta> schemas;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        SchemaMeta schema;
        schema.name  = std::string(rs.text(r, 0));
        schema.owner = schema.name;      // o schema E' o usuario
        schemas.push_back(std::move(schema));
    }
    return schemas;
}

Result<std::vector<TableMeta>> OracleCatalog::load_tables(std::string_view schema) {
    const std::string owner = oracle_literal(schema);

    // Uma view materializada tambem esta' em ALL_TABLES (o segmento que guarda
    // as linhas): sai de la' pelo nome, depois. BIN$...: o que esta' na
    // lixeira (DROP TABLE sem PURGE). NESTED: tabela aninhada, que nao se
    // consulta sozinha.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.table_name, 'TABLE', t.num_rows, t.partitioned "
            "  FROM all_tables t "
            " WHERE t.owner = " + owner +
            "   AND t.nested = 'NO' AND t.table_name NOT LIKE 'BIN$%' "
            "UNION ALL "
            "SELECT v.view_name, 'VIEW', NULL, 'NO' FROM all_views v "
            " WHERE v.owner = " + owner +
            " UNION ALL "
            "SELECT m.mview_name, 'MATERIALIZED VIEW', NULL, 'NO' FROM all_mviews m "
            " WHERE m.owner = " + owner +
            " ORDER BY 1"));

    std::set<std::string> materialized;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        if (rs.text(r, 1) == "MATERIALIZED VIEW") materialized.emplace(rs.text(r, 0));
    }

    std::vector<TableMeta> tables;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        const std::string_view type = rs.text(r, 1);
        if (type == "TABLE" && materialized.contains(std::string(rs.text(r, 0)))) continue;

        TableMeta table;
        table.name = std::string(rs.text(r, 0));
        table.kind = type == "VIEW"                ? ObjKind::view
                     : type == "MATERIALIZED VIEW" ? ObjKind::materialized_view
                     : rs.text(r, 3) == "YES"      ? ObjKind::partitioned_table
                                                   : ObjKind::table;
        // NUM_ROWS e' a estatistica do ultimo ANALYZE; nulo = nunca coletada.
        table.estimated_rows = rs.is_null(r, 2) ? 0 : to_int64(rs.text(r, 2));
        tables.push_back(std::move(table));
    }

    // Os comentarios, 'a parte. Falhar aqui nao tira as tabelas da arvore.
    if (auto comments = holt_.query_internal(
            "SELECT c.table_name, c.comments FROM all_tab_comments c "
            " WHERE c.owner = " + owner + " AND c.comments IS NOT NULL")) {
        std::map<std::string_view, std::string_view> by_name;
        for (std::size_t r = 0; r < comments->row_count(); ++r) {
            by_name.emplace(comments->text(r, 0), comments->text(r, 1));
        }
        for (TableMeta& table : tables) {
            if (const auto found = by_name.find(table.name); found != by_name.end()) {
                table.comment = std::string(found->second);
            }
        }
    }
    return tables;
}

Result<std::vector<ColumnMeta>> OracleCatalog::load_columns(std::string_view schema,
                                                            std::string_view table) {
    const std::string where_table =
        "owner = " + oracle_literal(schema) + " AND table_name = " + oracle_literal(table);

    // DATA_DEFAULT e' LONG: nao entra em WHERE nem em ORDER BY, so' na lista.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT c.column_name, c.data_type, c.data_length, c.data_precision, "
            "       c.data_scale, c.nullable, c.column_id, c.char_length, c.char_used, "
            "       c.data_default "
            "  FROM all_tab_cols c "
            " WHERE c." + where_table + " AND c.hidden_column = 'NO' "
            " ORDER BY c.column_id"));

    std::vector<ColumnMeta> columns;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        const std::string_view type = rs.text(r, 1);
        const int precision = rs.is_null(r, 3) ? -1 : to_int(rs.text(r, 3), -1);
        const int scale     = rs.is_null(r, 4) ? -1 : to_int(rs.text(r, 4), -1);

        ColumnMeta column;
        column.name      = std::string(rs.text(r, 0));
        column.type_name = oracle_type_text(type, to_int(rs.text(r, 2), 0), precision, scale,
                                            to_int(rs.text(r, 7), 0), rs.text(r, 8) == "C");
        column.kind     = oracle_kind(type, precision, scale);
        column.nullable = rs.text(r, 5) == "Y";
        column.position = to_int(rs.text(r, 6), 0);

        // O dicionario guarda o DEFAULT como foi digitado, com o espaco ou a
        // quebra de linha do fim.
        std::string_view value = rs.text(r, 9);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\n')) {
            value.remove_suffix(1);
        }
        column.default_value = std::string(value);
        columns.push_back(std::move(column));
    }

    const auto column_named = [&columns](std::string_view name) -> ColumnMeta* {
        for (ColumnMeta& column : columns) {
            if (column.name == name) return &column;
        }
        return nullptr;
    };

    if (auto comments = holt_.query_internal(
            "SELECT m.column_name, m.comments FROM all_col_comments m "
            " WHERE m." + where_table + " AND m.comments IS NOT NULL")) {
        for (std::size_t r = 0; r < comments->row_count(); ++r) {
            if (ColumnMeta* column = column_named(comments->text(r, 0))) {
                column->comment = std::string(comments->text(r, 1));
            }
        }
    }

    // A chave primaria: o nome da constraint, depois as colunas dela.
    if (auto key = holt_.query_internal(
            "SELECT k.constraint_name FROM all_constraints k "
            " WHERE k." + where_table + " AND k.constraint_type = 'P'");
        key && key->row_count() > 0) {
        if (auto parts = holt_.query_internal(
                "SELECT c.column_name FROM all_cons_columns c "
                " WHERE c.owner = " + oracle_literal(schema) +
                "   AND c.constraint_name = " + oracle_literal(key->text(0, 0)))) {
            for (std::size_t r = 0; r < parts->row_count(); ++r) {
                if (ColumnMeta* column = column_named(parts->text(r, 0))) {
                    column->primary_key = true;
                }
            }
        }
    }
    return columns;
}

Result<std::vector<ConstraintMeta>> OracleCatalog::load_constraints(std::string_view schema,
                                                                    std::string_view table) {
    const std::string where_table =
        "owner = " + oracle_literal(schema) + " AND table_name = " + oracle_literal(table);

    // Um NOT NULL e' guardado como CHECK de nome gerado ("COL" IS NOT NULL):
    // ja' aparece na coluna, e como constraint so' faria volume.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT k.constraint_name, k.constraint_type, k.search_condition_vc, "
            "       k.deferrable "
            "  FROM all_constraints k "
            " WHERE k." + where_table +
            "   AND k.constraint_type IN ('P', 'U', 'C') "
            "   AND NOT (k.constraint_type = 'C' AND k.generated = 'GENERATED NAME' "
            "            AND k.search_condition_vc LIKE '%IS NOT NULL') "
            " ORDER BY DECODE(k.constraint_type, 'P', 0, 'U', 1, 2), k.constraint_name"));

    OTTER_ASSIGN_OR_RETURN(const auto columns, constraint_columns(holt_, "c." + where_table));

    std::vector<ConstraintMeta> constraints;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        ConstraintMeta constraint;
        constraint.name       = std::string(rs.text(r, 0));
        constraint.deferrable = rs.text(r, 3) == "DEFERRABLE";
        if (const auto found = columns.find({std::string(schema), constraint.name});
            found != columns.end()) {
            constraint.columns = found->second;
        }

        const std::string_view type = rs.text(r, 1);
        if (type == "P") {
            constraint.kind = ObjKind::primary_key;
            constraint.definition = "PRIMARY KEY (" + constraint.columns + ")";
        } else if (type == "U") {
            constraint.kind = ObjKind::unique_key;
            constraint.definition = "UNIQUE (" + constraint.columns + ")";
        } else {
            constraint.kind = ObjKind::check_constraint;
            constraint.definition = "CHECK (" + std::string(rs.text(r, 2)) + ")";
        }
        constraints.push_back(std::move(constraint));
    }
    return constraints;
}

Result<std::vector<IndexMeta>> OracleCatalog::load_indexes(std::string_view schema,
                                                           std::string_view table) {
    const std::string owner = oracle_literal(schema);
    const std::string name  = oracle_literal(table);

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT i.index_name, i.index_type, i.uniqueness, i.status, i.owner "
            "  FROM all_indexes i "
            " WHERE i.table_owner = " + owner + " AND i.table_name = " + name +
            " ORDER BY i.index_name"));

    std::map<OwnedName, std::string> columns;
    if (auto parts = holt_.query_internal(
            "SELECT c.index_owner, c.index_name, c.column_name "
            "  FROM all_ind_columns c "
            " WHERE c.table_owner = " + owner + " AND c.table_name = " + name +
            " ORDER BY c.index_owner, c.index_name, c.column_position")) {
        for (std::size_t r = 0; r < parts->row_count(); ++r) {
            std::string& list =
                columns[{std::string(parts->text(r, 0)), std::string(parts->text(r, 1))}];
            if (!list.empty()) list += ',';
            list += parts->text(r, 2);
        }
    }

    // O indice que sustenta a chave primaria.
    std::string primary_index;
    if (auto key = holt_.query_internal(
            "SELECT k.index_name FROM all_constraints k "
            " WHERE k.owner = " + owner + " AND k.table_name = " + name +
            "   AND k.constraint_type = 'P'");
        key && key->row_count() > 0) {
        primary_index = std::string(key->text(0, 0));
    }

    std::vector<IndexMeta> indexes;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        IndexMeta index;
        index.name   = std::string(rs.text(r, 0));
        index.method = std::string(rs.text(r, 1));     // NORMAL, BITMAP, FUNCTION-BASED...
        index.unique = rs.text(r, 2) == "UNIQUE";
        // UNUSABLE: um indice que o servidor nao usa ate' ser reconstruido.
        // N/A e' o de tabela particionada, cujo estado fica em cada particao.
        index.valid   = rs.text(r, 3) != "UNUSABLE";
        index.primary = index.name == primary_index;
        if (const auto found = columns.find({std::string(rs.text(r, 4)), index.name});
            found != columns.end()) {
            index.columns = found->second;
        }
        index.definition = std::string(index.unique ? "CREATE UNIQUE INDEX " : "CREATE INDEX ") +
                           index.name + " ON " + std::string(table) + " (" + index.columns + ")";
        indexes.push_back(std::move(index));
    }
    return indexes;
}

Result<std::vector<ForeignKeyMeta>> OracleCatalog::foreign_keys_where(
    const std::string& condition) {
    // Uma chave estrangeira aponta para outra CONSTRAINT (a PK ou UNIQUE do
    // outro lado), nao para uma tabela: dai' o auto-relacionamento.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT k.constraint_name, k.table_name, r.constraint_name, r.table_name, "
            "       k.delete_rule, k.owner, r.owner "
            "  FROM all_constraints k "
            "  JOIN all_constraints r "
            "    ON r.owner = k.r_owner AND r.constraint_name = k.r_constraint_name "
            " WHERE k.constraint_type = 'R' AND " + condition +
            " ORDER BY k.table_name, k.constraint_name"));

    std::vector<ForeignKeyMeta> keys;
    if (rs.row_count() == 0) return keys;

    // As colunas dos dois lados, so' das tabelas envolvidas.
    std::set<std::string> owners;
    std::set<std::string> tables;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        owners.emplace(rs.text(r, 5));
        owners.emplace(rs.text(r, 6));
        tables.emplace(rs.text(r, 1));
        tables.emplace(rs.text(r, 3));
    }
    std::string columns_where = "c.owner IN (" + in_list(owners) + ")";
    // O limite do Oracle para um IN e' 1000 itens; acima de umas centenas de
    // tabelas vale mais ler as colunas do dono inteiro.
    if (tables.size() <= 200) columns_where += " AND c.table_name IN (" + in_list(tables) + ")";
    OTTER_ASSIGN_OR_RETURN(const auto columns, constraint_columns(holt_, columns_where));

    const auto columns_of = [&columns](std::string_view owner, std::string_view name) {
        const auto found = columns.find({std::string(owner), std::string(name)});
        return found == columns.end() ? std::string{} : found->second;
    };

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        ForeignKeyMeta key;
        key.name          = std::string(rs.text(r, 0));
        key.source_table  = std::string(rs.text(r, 1));
        key.source_column = columns_of(rs.text(r, 5), rs.text(r, 0));
        key.target_table  = std::string(rs.text(r, 3));
        key.target_column = columns_of(rs.text(r, 6), rs.text(r, 2));
        // O Oracle nao tem ON UPDATE: a chave referenciada nao muda em cascata.
        key.on_update     = "NO ACTION";
        key.on_delete     = std::string(rs.text(r, 4));
        key.source_schema = std::string(rs.text(r, 5));
        key.target_schema = std::string(rs.text(r, 6));
        key.definition = "FOREIGN KEY (" + key.source_column + ") REFERENCES " +
                         key.target_table + " (" + key.target_column + ")";
        if (key.on_delete != "NO ACTION") key.definition += " ON DELETE " + key.on_delete;
        keys.push_back(std::move(key));
    }
    return keys;
}

Result<std::vector<ForeignKeyMeta>> OracleCatalog::load_foreign_keys(std::string_view schema) {
    return foreign_keys_where("k.owner = " + oracle_literal(schema));
}

Result<std::vector<ForeignKeyMeta>> OracleCatalog::load_table_foreign_keys(
    std::string_view schema, std::string_view table) {
    return foreign_keys_where("k.owner = " + oracle_literal(schema) +
                              " AND k.table_name = " + oracle_literal(table));
}

Result<std::vector<ForeignKeyMeta>> OracleCatalog::load_references(std::string_view schema,
                                                                   std::string_view table) {
    return foreign_keys_where("r.owner = " + oracle_literal(schema) +
                              " AND r.table_name = " + oracle_literal(table));
}

Result<std::vector<TriggerMeta>> OracleCatalog::load_triggers(std::string_view schema,
                                                              std::string_view table) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.trigger_name, t.table_name, t.trigger_type, t.triggering_event, "
            "       t.status, t.description "
            "  FROM all_triggers t "
            " WHERE t.table_owner = " + oracle_literal(schema) +
            "   AND t.table_name = " + oracle_literal(table) +
            " ORDER BY t.trigger_name"));

    std::vector<TriggerMeta> triggers;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        TriggerMeta trigger;
        trigger.name   = std::string(rs.text(r, 0));
        trigger.table  = std::string(rs.text(r, 1));
        // "BEFORE EACH ROW", "AFTER STATEMENT", "INSTEAD OF": o momento e' o
        // que vem antes de EACH ROW / STATEMENT.
        const std::string_view type = rs.text(r, 2);
        trigger.timing = type.starts_with("BEFORE")  ? "BEFORE"
                         : type.starts_with("AFTER") ? "AFTER"
                                                     : std::string(type);
        trigger.events  = std::string(rs.text(r, 3));   // "INSERT OR UPDATE"
        trigger.enabled = rs.text(r, 4) == "ENABLED";
        trigger.definition = std::string(rs.text(r, 5));
        triggers.push_back(std::move(trigger));
    }
    return triggers;
}

Result<std::vector<PartitionMeta>> OracleCatalog::load_partitions(std::string_view schema,
                                                                  std::string_view table) {
    const std::string owner = oracle_literal(schema);
    const std::string name  = oracle_literal(table);

    // HIGH_VALUE e' LONG. O metodo e a chave ficam na tabela, nao na particao.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT p.partition_name, p.num_rows, p.high_value "
            "  FROM all_tab_partitions p "
            " WHERE p.table_owner = " + owner + " AND p.table_name = " + name +
            " ORDER BY p.partition_position"));

    std::vector<PartitionMeta> partitions;
    if (rs.row_count() == 0) return partitions;

    std::string method;
    if (auto type = holt_.query_internal(
            "SELECT t.partitioning_type FROM all_part_tables t "
            " WHERE t.owner = " + owner + " AND t.table_name = " + name);
        type && type->row_count() > 0) {
        method = std::string(type->text(0, 0));
    }
    std::string expression;
    if (auto key = holt_.query_internal(
            "SELECT c.column_name FROM all_part_key_columns c "
            " WHERE c.owner = " + owner + " AND c.name = " + name +
            "   AND c.object_type = 'TABLE' ORDER BY c.column_position")) {
        for (std::size_t r = 0; r < key->row_count(); ++r) {
            if (!expression.empty()) expression += ',';
            expression += key->text(r, 0);
        }
    }

    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        PartitionMeta partition;
        partition.name           = std::string(rs.text(r, 0));
        partition.method         = method;
        partition.expression     = expression;
        partition.estimated_rows = rs.is_null(r, 1) ? 0 : to_int64(rs.text(r, 1));
        partition.description    = std::string(rs.text(r, 2));
        // "SELECT ... FROM t PARTITION (p)" existe, mas a particao nao e' uma
        // tabela com nome proprio.
        partition.is_table = false;
        partitions.push_back(std::move(partition));
    }
    return partitions;
}

Result<std::vector<EventMeta>> OracleCatalog::load_events(std::string_view) {
    return std::vector<EventMeta>{};
}

Result<std::vector<SequenceMeta>> OracleCatalog::load_sequences(std::string_view schema) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT s.sequence_name, s.last_number, s.min_value, s.max_value, "
            "       s.increment_by, s.cycle_flag "
            "  FROM all_sequences s "
            " WHERE s.sequence_owner = " + oracle_literal(schema) +
            " ORDER BY s.sequence_name"));

    std::vector<SequenceMeta> sequences;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        SequenceMeta sequence;
        sequence.name        = std::string(rs.text(r, 0));
        // LAST_NUMBER e' o proximo valor a ir para o CACHE, nao o ultimo
        // entregue: o servidor nao guarda este.
        sequence.last_value  = to_int64(rs.text(r, 1));
        sequence.min_value   = to_int64(rs.text(r, 2));
        sequence.start_value = sequence.min_value;
        sequence.max_value   = to_int64(rs.text(r, 3));
        sequence.increment   = to_int64(rs.text(r, 4));
        sequence.cycles      = rs.text(r, 5) == "Y";
        sequences.push_back(std::move(sequence));
    }
    return sequences;
}

Result<std::vector<RoutineMeta>> OracleCatalog::load_routines(std::string_view schema) {
    const std::string owner = oracle_literal(schema);

    // So' as rotinas SOLTAS. As de pacote moram dentro do pacote (ainda nao
    // lido).
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT o.object_name, o.object_type, o.status "
            "  FROM all_objects o "
            " WHERE o.owner = " + owner +
            "   AND o.object_type IN ('PROCEDURE', 'FUNCTION') "
            " ORDER BY o.object_name"));

    std::vector<RoutineMeta> routines;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        RoutineMeta routine;
        routine.name     = std::string(rs.text(r, 0));
        routine.kind     = rs.text(r, 1) == "FUNCTION" ? ObjKind::function : ObjKind::procedure;
        routine.language = "PL/SQL";
        // Nao ha' sobrecarga fora de pacote: o nome basta para ALTER e DROP.
        if (rs.text(r, 2) != "VALID") routine.comment = "INVALID";
        routines.push_back(std::move(routine));
    }
    if (routines.empty()) return routines;

    // Os argumentos. POSITION 0 e' o retorno da funcao; DATA_LEVEL > 0 sao os
    // campos de um argumento composto.
    if (auto arguments = holt_.query_internal(
            "SELECT a.object_name, a.position, a.argument_name, a.in_out, a.data_type "
            "  FROM all_arguments a "
            " WHERE a.owner = " + owner +
            "   AND a.package_name IS NULL AND a.data_level = 0 "
            " ORDER BY a.object_name, a.position")) {
        for (std::size_t r = 0; r < arguments->row_count(); ++r) {
            for (RoutineMeta& routine : routines) {
                if (routine.name != arguments->text(r, 0)) continue;
                if (arguments->text(r, 1) == "0") {
                    routine.return_type = std::string(arguments->text(r, 4));
                } else {
                    if (!routine.arguments.empty()) routine.arguments += ", ";
                    routine.arguments += std::string(arguments->text(r, 2)) + " " +
                                         std::string(arguments->text(r, 3)) + " " +
                                         std::string(arguments->text(r, 4));
                }
                break;
            }
        }
    }
    return routines;
}

Result<std::vector<DataTypeMeta>> OracleCatalog::load_types(std::string_view schema) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT t.type_name, t.typecode, t.owner "
            "  FROM all_types t "
            " WHERE t.owner = " + oracle_literal(schema) +
            " ORDER BY t.type_name"));

    std::vector<DataTypeMeta> types;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        DataTypeMeta type;
        type.name  = std::string(rs.text(r, 0));
        // OBJECT tem atributos (composto); COLLECTION e' VARRAY ou tabela
        // aninhada, sem equivalente exato -- fica como tipo base.
        type.kind  = rs.text(r, 1) == "OBJECT" ? TypeKind::composite : TypeKind::base;
        type.owner = std::string(rs.text(r, 2));
        types.push_back(std::move(type));
    }
    if (types.empty()) return types;

    // Os atributos dos tipos objeto, numa consulta so'.
    if (auto attributes = holt_.query_internal(
            "SELECT a.type_name, a.attr_name, a.attr_type_name "
            "  FROM all_type_attrs a "
            " WHERE a.owner = " + oracle_literal(schema) +
            " ORDER BY a.type_name, a.attr_no")) {
        for (std::size_t r = 0; r < attributes->row_count(); ++r) {
            for (DataTypeMeta& type : types) {
                if (type.name != attributes->text(r, 0)) continue;
                type.attributes.push_back({std::string(attributes->text(r, 1)),
                                           std::string(attributes->text(r, 2)), true});
                break;
            }
        }
    }
    return types;
}

Result<std::string> OracleCatalog::load_routine_definition(std::string_view schema,
                                                           std::string_view name,
                                                           std::string_view) {
    // ALL_SOURCE guarda o fonte linha a linha, a partir de "PROCEDURE nome".
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT s.text FROM all_source s "
            " WHERE s.owner = " + oracle_literal(schema) +
            "   AND s.name = " + oracle_literal(name) +
            "   AND s.type IN ('PROCEDURE', 'FUNCTION') "
            " ORDER BY s.type, s.line"));

    if (rs.row_count() == 0) {
        return fail(Errc::not_found, "the source of this routine is not visible to this user");
    }
    std::string source = "CREATE OR REPLACE ";
    for (std::size_t r = 0; r < rs.row_count(); ++r) source += rs.text(r, 0);
    return source;
}

Result<std::string> OracleCatalog::load_view_definition(std::string_view schema,
                                                        std::string_view name) {
    // TEXT e' LONG. Nas views do proprio dicionario (SYS) o servidor a devolve
    // nula a quem nao e' DBA; TEXT_VC traz os primeiros 4000 caracteres.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT v.text_vc, v.text FROM all_views v "
            " WHERE v.owner = " + oracle_literal(schema) +
            "   AND v.view_name = " + oracle_literal(name)));
    if (rs.row_count() > 0) {
        return std::string(rs.is_null(0, 1) ? rs.text(0, 0) : rs.text(0, 1));
    }

    OTTER_ASSIGN_OR_RETURN(
        auto mview,
        holt_.query_internal(
            "SELECT m.query FROM all_mviews m "
            " WHERE m.owner = " + oracle_literal(schema) +
            "   AND m.mview_name = " + oracle_literal(name)));
    if (mview.row_count() > 0) return std::string(mview.text(0, 0));
    return std::string{};
}

Result<std::vector<CatalogItem>> OracleCatalog::load_list(CatalogList list, std::string_view a,
                                                          std::string_view, std::string_view) {
    if (list != CatalogList::schema_indexes) return std::vector<CatalogItem>{};

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query_internal(
            "SELECT i.index_name, i.table_name, i.uniqueness, i.index_type "
            "  FROM all_indexes i "
            " WHERE i.owner = " + oracle_literal(a) +
            " ORDER BY i.index_name"));

    std::vector<CatalogItem> items;
    for (std::size_t r = 0; r < rs.row_count(); ++r) {
        CatalogItem item;
        item.name    = std::string(rs.text(r, 0));
        item.detail  = std::string(rs.text(r, 1));
        item.tooltip = std::string(rs.text(r, 2)) + " " + std::string(rs.text(r, 3));
        item.flag    = rs.text(r, 2) == "UNIQUE";
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace otter::db
