#include "db/catalog_mysql.hpp"

#include <charconv>
#include <string>

namespace otter::db {
namespace {

std::int64_t to_int64(std::string_view text) {
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

// Categoria logica a partir do DATA_TYPE do information_schema, que vem sem
// tamanho ("varchar", nao "varchar(50)").
DataKind kind_from_type_name(std::string_view name) noexcept {
    if (name == "tinyint" || name == "smallint" || name == "mediumint" ||
        name == "int" || name == "integer" || name == "bigint" ||
        name == "year" || name == "bit") {
        // BOOLEAN no MySQL e' apelido de TINYINT(1): nao ha' tipo booleano
        // de verdade, e forcar DataKind::boolean aqui faria 2 e 3 virarem
        // "true" na grade, escondendo o valor real.
        return DataKind::integer;
    }
    if (name == "float" || name == "double" || name == "real") {
        return DataKind::floating;
    }
    if (name == "decimal" || name == "numeric") return DataKind::numeric;
    if (name == "date")     return DataKind::date;
    if (name == "time")     return DataKind::time;
    if (name == "datetime" || name == "timestamp") return DataKind::timestamp;
    if (name == "json")     return DataKind::json;
    if (name.ends_with("blob") || name == "binary" || name == "varbinary") {
        return DataKind::binary;
    }
    if (name == "geometry" || name == "point" || name == "linestring" ||
        name == "polygon" || name.starts_with("multi") ||
        name == "geometrycollection") {
        return DataKind::geometry;
    }
    if (name.ends_with("text") || name == "char" || name == "varchar" ||
        name == "enum" || name == "set") {
        return DataKind::string;
    }
    return DataKind::unknown;
}

// Uma linha por par (indice, coluna) chega do information_schema. Junta as
// colunas de um mesmo indice numa lista, preservando SEQ_IN_INDEX -- a ordem
// das colunas de um indice composto determina que consultas ele atende, e
// embaralha-la daria um DDL que nao reproduz o indice.
void append_column(std::string& list, std::string_view column) {
    if (!list.empty()) list += ", ";
    list += column;
}

} // namespace

std::string mysql_quote(std::string_view identifier) {
    std::string out;
    out.reserve(identifier.size() + 2);
    out.push_back('`');
    for (char c : identifier) {
        if (c == '`') out.push_back('`');
        out.push_back(c);
    }
    out.push_back('`');
    return out;
}

std::string mysql_literal(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('\'');
    for (char c : text) {
        // A barra invertida e' caractere de escape no MySQL por padrao (ao
        // contrario do padrao SQL), entao precisa ser dobrada tambem. Escapar
        // so' a aspa deixaria passar `\'`, que fecha a string.
        if (c == '\'' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

MysqlCatalog::MysqlCatalog(Holt& holt) : holt_(holt) {
    const std::string text = holt_.server_version();
    mariadb_ = text.find("MariaDB") != std::string::npos ||
               text.find("mariadb") != std::string::npos;

    // Mesma armadilha do aperto de mao: o "5.5.5-" na frente e' falso.
    std::string_view view = text;
    if (mariadb_ && view.starts_with("5.5.5-")) view.remove_prefix(6);
    version_ = ServerVersion::parse(view);
}

Result<std::vector<SchemaMeta>> MysqlCatalog::load_schemas() {
    // Os bancos internos ficam de fora: sao centenas de tabelas de
    // instrumentacao que ninguem navega, e mistura-las com as do usuario
    // esconderia o que importa. O DBeaver faz o mesmo (opcao "Show system
    // objects", desligada por padrao).
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT SCHEMA_NAME, DEFAULT_CHARACTER_SET_NAME, "
            "       DEFAULT_COLLATION_NAME "
            "  FROM information_schema.SCHEMATA "
            " WHERE SCHEMA_NAME NOT IN ('information_schema', 'performance_schema',"
            "                           'mysql', 'sys') "
            " ORDER BY SCHEMA_NAME"));

    std::vector<SchemaMeta> schemas;
    schemas.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SchemaMeta schema;
        schema.name = std::string(rs.text(row, 0));

        // O MySQL nao tem comentario de banco nem dono. O charset ocupa o
        // lugar do comentario porque e' a informacao que muda o comportamento
        // (comparacao, ordenacao) e o usuario precisa ver.
        schema.comment = std::string(rs.text(row, 1)) + " / " +
                         std::string(rs.text(row, 2));
        schemas.push_back(std::move(schema));
    }
    return schemas;
}

Result<std::vector<TableMeta>> MysqlCatalog::load_tables(std::string_view schema) {
    // TABLE_ROWS e' ESTIMATIVA no InnoDB -- vem das estatisticas, nao de uma
    // contagem, e erra com folga. Usamos assim mesmo, como o DBeaver: um
    // COUNT(*) por tabela ao abrir a arvore travaria a interface.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT TABLE_NAME, TABLE_TYPE, TABLE_COMMENT, TABLE_ROWS, "
            "       DATA_LENGTH + INDEX_LENGTH, ENGINE "
            "  FROM information_schema.TABLES "
            " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
            " ORDER BY TABLE_NAME"));

    std::vector<TableMeta> tables;
    tables.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TableMeta table;
        table.name = std::string(rs.text(row, 0));

        const std::string_view type = rs.text(row, 1);
        if (type == "VIEW")            table.kind = ObjKind::view;
        else if (type == "SEQUENCE")   table.kind = ObjKind::sequence;
        else                           table.kind = ObjKind::table;

        // Sequences do MariaDB aparecem em TABLES; load_sequences() ja' as
        // lista, e deixa-las aqui tambem as mostraria duas vezes na arvore.
        if (table.kind == ObjKind::sequence) continue;

        table.comment        = std::string(rs.text(row, 2));
        table.estimated_rows = to_int64(rs.text(row, 3));

        const std::int64_t bytes = to_int64(rs.text(row, 4));
        if (bytes > 0) {
            if (bytes >= 1024 * 1024 * 1024) {
                table.size_pretty = std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
            } else if (bytes >= 1024 * 1024) {
                table.size_pretty = std::to_string(bytes / (1024 * 1024)) + " MB";
            } else {
                table.size_pretty = std::to_string(bytes / 1024) + " kB";
            }
        }
        tables.push_back(std::move(table));
    }
    return tables;
}

Result<std::vector<ColumnMeta>> MysqlCatalog::load_columns(std::string_view schema,
                                                            std::string_view table) {
    // COLUMN_TYPE traz o tipo COMPLETO ("varchar(50)", "enum('a','b')"),
    // enquanto DATA_TYPE traz so' a familia. Precisamos dos dois: o completo
    // para mostrar e gerar DDL, a familia para classificar.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT COLUMN_NAME, COLUMN_TYPE, DATA_TYPE, IS_NULLABLE, "
            "       COLUMN_KEY, COLUMN_DEFAULT, COLUMN_COMMENT, "
            "       ORDINAL_POSITION, EXTRA "
            "  FROM information_schema.COLUMNS "
            " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
            "   AND TABLE_NAME = "   + mysql_literal(table) +
            " ORDER BY ORDINAL_POSITION"));

    std::vector<ColumnMeta> columns;
    columns.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ColumnMeta column;
        column.name        = std::string(rs.text(row, 0));
        column.type_name   = std::string(rs.text(row, 1));
        column.kind        = kind_from_type_name(rs.text(row, 2));
        column.nullable    = rs.text(row, 3) == "YES";
        column.primary_key = rs.text(row, 4) == "PRI";
        column.comment     = std::string(rs.text(row, 6));
        column.position    = static_cast<std::int32_t>(to_int64(rs.text(row, 7)));

        // COLUMN_DEFAULT nulo e' "sem default"; a string "NULL" seria um
        // default de valor nulo. Sao coisas diferentes no DDL.
        if (!rs.is_null(row, 5)) column.default_value = std::string(rs.text(row, 5));

        // EXTRA guarda auto_increment e as colunas geradas. Sem isso o DDL
        // gerado perderia o auto_increment e a tabela recriada nao teria id.
        const std::string_view extra = rs.text(row, 8);
        if (!extra.empty()) {
            if (!column.default_value.empty()) column.default_value += " ";
            column.default_value += std::string(extra);
        }
        columns.push_back(std::move(column));
    }
    return columns;
}

Result<std::vector<ConstraintMeta>> MysqlCatalog::load_constraints(
    std::string_view schema, std::string_view table) {

    // PRIMARY KEY e UNIQUE. As foreign keys tem estrutura propria e saem por
    // load_table_foreign_keys(); inclui-las aqui as mostraria duas vezes.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT tc.CONSTRAINT_NAME, tc.CONSTRAINT_TYPE, kc.COLUMN_NAME "
            "  FROM information_schema.TABLE_CONSTRAINTS tc "
            "  JOIN information_schema.KEY_COLUMN_USAGE kc "
            "    ON kc.CONSTRAINT_SCHEMA = tc.CONSTRAINT_SCHEMA "
            "   AND kc.CONSTRAINT_NAME   = tc.CONSTRAINT_NAME "
            "   AND kc.TABLE_NAME        = tc.TABLE_NAME "
            " WHERE tc.TABLE_SCHEMA = " + mysql_literal(schema) +
            "   AND tc.TABLE_NAME = "   + mysql_literal(table) +
            "   AND tc.CONSTRAINT_TYPE IN ('PRIMARY KEY', 'UNIQUE') "
            " ORDER BY tc.CONSTRAINT_NAME, kc.ORDINAL_POSITION"));

    std::vector<ConstraintMeta> constraints;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        const std::string name(rs.text(row, 0));

        // Uma linha por COLUNA: agrupamos pelo nome, que vem ordenado.
        if (constraints.empty() || constraints.back().name != name) {
            ConstraintMeta constraint;
            constraint.name = name;
            constraint.kind = rs.text(row, 1) == "PRIMARY KEY"
                                  ? ObjKind::primary_key
                                  : ObjKind::unique_key;
            constraints.push_back(std::move(constraint));
        }
        append_column(constraints.back().columns, rs.text(row, 2));
    }

    for (ConstraintMeta& constraint : constraints) {
        constraint.definition =
            (constraint.kind == ObjKind::primary_key ? "PRIMARY KEY (" : "UNIQUE (") +
            constraint.columns + ")";
    }
    return constraints;
}

Result<std::vector<IndexMeta>> MysqlCatalog::load_indexes(std::string_view schema,
                                                           std::string_view table) {
    // STATISTICS e' onde o MySQL guarda os indices -- nome pouco intuitivo,
    // herdado do padrao SQL. INDEX_TYPE distingue BTREE de FULLTEXT e SPATIAL.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT INDEX_NAME, NON_UNIQUE, COLUMN_NAME, INDEX_TYPE, "
            "       SEQ_IN_INDEX, INDEX_COMMENT "
            "  FROM information_schema.STATISTICS "
            " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
            "   AND TABLE_NAME = "   + mysql_literal(table) +
            " ORDER BY INDEX_NAME, SEQ_IN_INDEX"));

    std::vector<IndexMeta> indexes;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        const std::string name(rs.text(row, 0));

        if (indexes.empty() || indexes.back().name != name) {
            IndexMeta index;
            index.name    = name;
            index.unique  = rs.text(row, 1) == "0";
            // O indice da chave primaria se chama literalmente "PRIMARY" --
            // nao ha' flag separada.
            index.primary = name == "PRIMARY";
            index.method  = std::string(rs.text(row, 3));
            indexes.push_back(std::move(index));
        }
        append_column(indexes.back().columns, rs.text(row, 2));
    }

    for (IndexMeta& index : indexes) {
        index.definition = (index.primary ? "PRIMARY KEY ("
                            : index.unique ? "UNIQUE INDEX " + mysql_quote(index.name) + " ("
                                           : "INDEX " + mysql_quote(index.name) + " (") +
                           index.columns + ")";
    }
    return indexes;
}

namespace {

// As FKs saem da mesma consulta, filtrada de dois jeitos: pela tabela de
// ORIGEM (as chaves desta tabela) ou pela tabela de DESTINO (quem aponta para
// ca'). A segunda e' a que mais falta num cliente SQL.
Result<std::vector<ForeignKeyMeta>> load_keys(Holt& holt, const std::string& where) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt.query(
            "SELECT kc.CONSTRAINT_NAME, kc.TABLE_NAME, kc.COLUMN_NAME, "
            "       kc.REFERENCED_TABLE_NAME, kc.REFERENCED_COLUMN_NAME, "
            "       rc.UPDATE_RULE, rc.DELETE_RULE "
            "  FROM information_schema.KEY_COLUMN_USAGE kc "
            "  JOIN information_schema.REFERENTIAL_CONSTRAINTS rc "
            "    ON rc.CONSTRAINT_SCHEMA = kc.CONSTRAINT_SCHEMA "
            "   AND rc.CONSTRAINT_NAME   = kc.CONSTRAINT_NAME " +
            where +
            " ORDER BY kc.CONSTRAINT_NAME, kc.ORDINAL_POSITION"));

    std::vector<ForeignKeyMeta> keys;
    keys.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ForeignKeyMeta key;
        key.name          = std::string(rs.text(row, 0));
        key.source_table  = std::string(rs.text(row, 1));
        key.source_column = std::string(rs.text(row, 2));
        key.target_table  = std::string(rs.text(row, 3));
        key.target_column = std::string(rs.text(row, 4));
        key.on_update     = std::string(rs.text(row, 5));
        key.on_delete     = std::string(rs.text(row, 6));

        key.definition = "FOREIGN KEY (" + key.source_column + ") REFERENCES " +
                         key.target_table + "(" + key.target_column + ")" +
                         (key.on_delete.empty() ? "" : " ON DELETE " + key.on_delete) +
                         (key.on_update.empty() ? "" : " ON UPDATE " + key.on_update);
        keys.push_back(std::move(key));
    }
    return keys;
}

} // namespace

Result<std::vector<ForeignKeyMeta>> MysqlCatalog::load_foreign_keys(
    std::string_view schema) {
    return load_keys(holt_, " WHERE kc.TABLE_SCHEMA = " + mysql_literal(schema) +
                            "   AND kc.REFERENCED_TABLE_NAME IS NOT NULL");
}

Result<std::vector<ForeignKeyMeta>> MysqlCatalog::load_table_foreign_keys(
    std::string_view schema, std::string_view table) {
    return load_keys(holt_, " WHERE kc.TABLE_SCHEMA = " + mysql_literal(schema) +
                            "   AND kc.TABLE_NAME = "   + mysql_literal(table) +
                            "   AND kc.REFERENCED_TABLE_NAME IS NOT NULL");
}

Result<std::vector<ForeignKeyMeta>> MysqlCatalog::load_references(
    std::string_view schema, std::string_view table) {
    // Filtra pelo DESTINO: quem depende desta tabela.
    return load_keys(holt_,
                     " WHERE kc.REFERENCED_TABLE_SCHEMA = " + mysql_literal(schema) +
                     "   AND kc.REFERENCED_TABLE_NAME = "   + mysql_literal(table));
}

Result<std::vector<TriggerMeta>> MysqlCatalog::load_triggers(std::string_view schema,
                                                              std::string_view table) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT TRIGGER_NAME, EVENT_OBJECT_TABLE, ACTION_TIMING, "
            "       EVENT_MANIPULATION, ACTION_STATEMENT "
            "  FROM information_schema.TRIGGERS "
            " WHERE TRIGGER_SCHEMA = " + mysql_literal(schema) +
            "   AND EVENT_OBJECT_TABLE = " + mysql_literal(table) +
            " ORDER BY TRIGGER_NAME"));

    std::vector<TriggerMeta> triggers;
    triggers.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        TriggerMeta trigger;
        trigger.name       = std::string(rs.text(row, 0));
        trigger.table      = std::string(rs.text(row, 1));
        trigger.timing     = std::string(rs.text(row, 2));
        trigger.events     = std::string(rs.text(row, 3));
        trigger.definition = std::string(rs.text(row, 4));

        // Nao existe trigger desabilitada no MySQL, ao contrario do
        // PostgreSQL. Deixar `enabled` sempre verdadeiro e' o correto aqui.
        triggers.push_back(std::move(trigger));
    }
    return triggers;
}

Result<std::vector<SequenceMeta>> MysqlCatalog::load_sequences(std::string_view schema) {
    // Sequences so' existem no MariaDB 10.3+. Perguntar num MySQL daria erro,
    // e um erro na arvore parece falha de conexao -- devolver vazio faz a
    // pasta simplesmente nao aparecer, que e' o comportamento do DBeaver
    // (visibleIf="object.dataSource.supportsSequences()").
    if (!mariadb_ || !version_.at_least(10, 3)) return std::vector<SequenceMeta>{};

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SELECT TABLE_NAME FROM information_schema.TABLES "
                    " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
                    "   AND TABLE_TYPE = 'SEQUENCE' ORDER BY TABLE_NAME"));

    std::vector<SequenceMeta> sequences;
    sequences.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        SequenceMeta sequence;
        sequence.name = std::string(rs.text(row, 0));

        // Os valores correntes exigem uma consulta POR sequence. Fazemos, mas
        // a falha nao derruba a lista: uma sequence sem permissao de leitura
        // ainda deve aparecer na arvore, so' que sem os numeros.
        auto detail = holt_.query(
            "SELECT start_value, minimum_value, maximum_value, increment, cycle_option "
            "  FROM " + mysql_quote(schema) + "." + mysql_quote(sequence.name));

        if (detail && detail->row_count() > 0) {
            sequence.start_value = to_int64(detail->text(0, 0));
            sequence.min_value   = to_int64(detail->text(0, 1));
            sequence.max_value   = to_int64(detail->text(0, 2));
            sequence.increment   = to_int64(detail->text(0, 3));
            sequence.cycles      = detail->text(0, 4) != "0";
        }
        sequences.push_back(std::move(sequence));
    }
    return sequences;
}

Result<std::vector<RoutineMeta>> MysqlCatalog::load_routines(std::string_view schema) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT ROUTINE_NAME, ROUTINE_TYPE, DTD_IDENTIFIER, "
            "       ROUTINE_COMMENT, ROUTINE_BODY "
            "  FROM information_schema.ROUTINES "
            " WHERE ROUTINE_SCHEMA = " + mysql_literal(schema) +
            " ORDER BY ROUTINE_NAME"));

    std::vector<RoutineMeta> routines;
    routines.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        RoutineMeta routine;
        routine.name        = std::string(rs.text(row, 0));
        routine.kind        = rs.text(row, 1) == "PROCEDURE" ? ObjKind::procedure
                                                             : ObjKind::function;
        routine.return_type = std::string(rs.text(row, 2));
        routine.comment     = std::string(rs.text(row, 3));
        routine.language    = std::string(rs.text(row, 4));   // SQL ou EXTERNAL
        routines.push_back(std::move(routine));
    }

    // Os parametros vem de outra tabela. Uma consulta so' para todas as
    // rotinas do banco, em vez de uma por rotina: um banco com 200 procedures
    // daria 200 idas ao servidor.
    OTTER_ASSIGN_OR_RETURN(
        auto parameters,
        holt_.query(
            "SELECT SPECIFIC_NAME, PARAMETER_NAME, DTD_IDENTIFIER, PARAMETER_MODE "
            "  FROM information_schema.PARAMETERS "
            " WHERE SPECIFIC_SCHEMA = " + mysql_literal(schema) +
            "   AND ORDINAL_POSITION > 0 "   // 0 e' o retorno da funcao
            " ORDER BY SPECIFIC_NAME, ORDINAL_POSITION"));

    for (std::size_t row = 0; row < parameters.row_count(); ++row) {
        const std::string_view owner = parameters.text(row, 0);

        for (RoutineMeta& routine : routines) {
            if (routine.name != owner) continue;

            if (!routine.arguments.empty()) routine.arguments += ", ";

            // O modo (IN/OUT/INOUT) so' existe em procedures; em funcoes vem
            // vazio. Omiti-lo numa procedure esconderia que o parametro
            // devolve valor.
            const std::string_view mode = parameters.text(row, 3);
            if (!mode.empty() && mode != "IN") {
                routine.arguments += std::string(mode) + " ";
            }
            routine.arguments += std::string(parameters.text(row, 1)) + " " +
                                 std::string(parameters.text(row, 2));
            break;
        }
    }
    return routines;
}

Result<std::vector<DataTypeMeta>> MysqlCatalog::load_types(std::string_view schema) {
    // O MySQL nao tem CREATE TYPE. ENUM e SET sao atributos de COLUNA, e ja'
    // aparecem no COLUMN_TYPE dela. Devolver vazio e' o correto -- inventar
    // uma pasta "Tipos" sempre vazia seria ruido.
    (void)schema;
    return std::vector<DataTypeMeta>{};
}

Result<std::string> MysqlCatalog::load_routine_definition(std::string_view schema,
                                                          std::string_view name,
                                                          std::string_view arguments) {
    (void)arguments;   // o MySQL identifica a rotina so' pelo nome: nao ha'
                       // sobrecarga, ao contrario do PostgreSQL.

    // ROUTINE_DEFINITION do information_schema vem VAZIA para quem nao e' o
    // dono nem tem SELECT em mysql.proc -- um caso comum e silencioso. O
    // SHOW CREATE respeita as permissoes de forma previsivel e traz o corpo
    // completo, com o DELIMITER e as caracteristicas.
    const std::string qualified = mysql_quote(schema) + "." + mysql_quote(name);

    // Nao ha' como saber de antemao se o objeto e' procedure ou funcao a
    // partir do nome, entao tentamos os dois. O primeiro SHOW FALHA quando o
    // objeto e' do outro tipo (erro 1305, "PROCEDURE does not exist"), e
    // propagar essa falha esconderia a funcao -- foi o que o teste contra o
    // servidor pegou.
    for (const char* kind : {"PROCEDURE", "FUNCTION"}) {
        auto rs = holt_.query(std::string("SHOW CREATE ") + kind + " " + qualified);
        if (!rs) continue;

        // Coluna 2 e' "Create Procedure" / "Create Function". Vem VAZIA para
        // quem nao tem permissao de ver o corpo -- e nesse caso seguimos para
        // a proxima tentativa em vez de devolver uma definicao em branco, que
        // pareceria uma rotina sem codigo.
        if (rs->row_count() > 0 && rs->column_count() > 2 &&
            !rs->is_null(0, 2) && !rs->text(0, 2).empty()) {
            return std::string(rs->text(0, 2));
        }
    }
    return fail(Errc::not_found, "rotina sem definição acessível");
}

Result<std::string> MysqlCatalog::load_view_definition(std::string_view schema,
                                                        std::string_view name) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SELECT VIEW_DEFINITION FROM information_schema.VIEWS "
                    " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
                    "   AND TABLE_NAME = "   + mysql_literal(name)));

    if (rs.row_count() == 0) return fail(Errc::not_found, "view não encontrada");
    return std::string(rs.text(0, 0));
}
// --- Particoes, eventos e informacao do servidor ---------------------------------

Result<std::vector<PartitionMeta>> MysqlCatalog::load_partitions(
    std::string_view schema, std::string_view table) {

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT PARTITION_NAME, PARTITION_METHOD, PARTITION_EXPRESSION, "
            "       PARTITION_DESCRIPTION, TABLE_ROWS, "
            "       DATA_LENGTH + INDEX_LENGTH, SUBPARTITION_NAME "
            "  FROM information_schema.PARTITIONS "
            " WHERE TABLE_SCHEMA = " + mysql_literal(schema) +
            "   AND TABLE_NAME = "   + mysql_literal(table) +
            " ORDER BY PARTITION_ORDINAL_POSITION, "
            "          SUBPARTITION_ORDINAL_POSITION"));

    std::vector<PartitionMeta> partitions;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        // Tabela NAO particionada devolve uma linha com PARTITION_NAME nulo.
        // Trata-la como particao criaria um no' fantasma chamado "[null]" na
        // arvore de toda tabela comum.
        if (rs.is_null(row, 0)) continue;

        const std::string name(rs.text(row, 0));

        // Subparticoes vem como linhas EXTRAS da mesma particao. Agrupamos,
        // em vez de repetir a particao -- o DBeaver as mostra aninhadas.
        if (!partitions.empty() && partitions.back().name == name) {
            if (!rs.is_null(row, 6)) {
                partitions.back().subpartitions.emplace_back(rs.text(row, 6));
            }
            continue;
        }

        PartitionMeta partition;
        partition.name        = name;
        partition.method      = std::string(rs.text(row, 1));
        partition.expression  = std::string(rs.text(row, 2));
        partition.description = std::string(rs.text(row, 3));
        partition.estimated_rows = to_int64(rs.text(row, 4));

        // No MySQL a particao e' divisao INTERNA da tabela, nao uma tabela
        // propria: nao da' para consultar `schema.particao`. Marcar isso
        // impede a UI de oferecer "ver dados" onde nao funciona.
        partition.is_table = false;

        const std::int64_t bytes = to_int64(rs.text(row, 5));
        if (bytes > 0) {
            partition.size_pretty = bytes >= 1024 * 1024
                ? std::to_string(bytes / (1024 * 1024)) + " MB"
                : std::to_string(bytes / 1024) + " kB";
        }

        if (!rs.is_null(row, 6)) {
            partition.subpartitions.emplace_back(rs.text(row, 6));
        }
        partitions.push_back(std::move(partition));
    }
    return partitions;
}

Result<std::vector<EventMeta>> MysqlCatalog::load_events(std::string_view schema) {
    // Eventos so' existem a partir do MySQL 5.1. Num servidor anterior a
    // tabela nao existe e a consulta daria erro -- que na arvore parece falha
    // de conexao.
    if (!mariadb_ && !version_.at_least(5, 1)) {
        return std::vector<EventMeta>{};
    }

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(
            "SELECT EVENT_NAME, DEFINER, EVENT_TYPE, "
            "       INTERVAL_VALUE, INTERVAL_FIELD, EXECUTE_AT, "
            "       STARTS, ENDS, STATUS, ON_COMPLETION, LAST_EXECUTED, "
            "       EVENT_DEFINITION, EVENT_COMMENT "
            "  FROM information_schema.EVENTS "
            " WHERE EVENT_SCHEMA = " + mysql_literal(schema) +
            " ORDER BY EVENT_NAME"));

    std::vector<EventMeta> events;
    events.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        EventMeta event;
        event.name    = std::string(rs.text(row, 0));
        event.definer = std::string(rs.text(row, 1));
        event.type    = std::string(rs.text(row, 2));

        // A agenda vem em DUAS formas excludentes: ONE TIME usa EXECUTE_AT,
        // RECURRING usa INTERVAL_VALUE + INTERVAL_FIELD. Mostrar o campo
        // vazio do outro tipo nao diria nada.
        if (event.type == "ONE TIME") {
            event.schedule = "AT " + std::string(rs.text(row, 5));
        } else if (!rs.is_null(row, 3)) {
            event.schedule = "EVERY " + std::string(rs.text(row, 3)) + " " +
                             std::string(rs.text(row, 4));
        }

        event.starts        = std::string(rs.text(row, 6));
        event.ends          = std::string(rs.text(row, 7));
        event.status        = std::string(rs.text(row, 8));
        event.on_completion = std::string(rs.text(row, 9));
        event.last_executed = rs.is_null(row, 10) ? std::string{}
                                                  : std::string(rs.text(row, 10));
        event.definition    = std::string(rs.text(row, 11));
        event.comment       = std::string(rs.text(row, 12));

        events.push_back(std::move(event));
    }
    return events;
}

Result<std::vector<ServerVariable>> MysqlCatalog::load_status(bool global) {
    // SHOW em vez de information_schema: no MySQL 5.7+ as tabelas
    // GLOBAL_STATUS e SESSION_STATUS estao DEPRECIADAS, e no 8.0 foram
    // REMOVIDAS de la' (mudaram para performance_schema). O SHOW funciona em
    // todas as versoes, que e' o que o ADR 0010 pede.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(global ? "SHOW GLOBAL STATUS" : "SHOW SESSION STATUS"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable variable;
        variable.name  = std::string(rs.text(row, 0));
        variable.value = std::string(rs.text(row, 1));
        out.push_back(std::move(variable));
    }
    return out;
}

Result<std::vector<ServerVariable>> MysqlCatalog::load_variables(bool global) {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query(global ? "SHOW GLOBAL VARIABLES" : "SHOW SESSION VARIABLES"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable variable;
        variable.name  = std::string(rs.text(row, 0));
        variable.value = std::string(rs.text(row, 1));
        out.push_back(std::move(variable));
    }
    return out;
}

Result<std::vector<ServerVariable>> MysqlCatalog::load_engines() {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SELECT ENGINE, SUPPORT, COMMENT "
                    "  FROM information_schema.ENGINES ORDER BY ENGINE"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable engine;
        engine.name = std::string(rs.text(row, 0));

        // SUPPORT tem quatro valores, e "DEFAULT" e' o que interessa saber:
        // e' o engine que CREATE TABLE usa quando nao se diz qual.
        engine.value  = std::string(rs.text(row, 1));
        engine.detail = std::string(rs.text(row, 2));

        out.push_back(std::move(engine));
    }
    return out;
}

Result<std::vector<ServerVariable>> MysqlCatalog::load_charsets() {
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SELECT CHARACTER_SET_NAME, DEFAULT_COLLATE_NAME, "
                    "       DESCRIPTION, MAXLEN "
                    "  FROM information_schema.CHARACTER_SETS "
                    " ORDER BY CHARACTER_SET_NAME"));

    std::vector<ServerVariable> out;
    out.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        ServerVariable charset;
        charset.name  = std::string(rs.text(row, 0));
        charset.value = std::string(rs.text(row, 1));

        // MAXLEN junto da descricao: e' o que distingue utf8 (3 bytes, NAO
        // cobre emoji) de utf8mb4 (4 bytes, cobre) -- a diferenca que mais
        // causa surpresa no MySQL.
        charset.detail = std::string(rs.text(row, 2)) + " (max " +
                         std::string(rs.text(row, 3)) + " bytes)";

        out.push_back(std::move(charset));
    }
    return out;
}
// --- Usuarios e privilegios -------------------------------------------------------

Result<std::vector<UserMeta>> MysqlCatalog::load_users() {
    // A coluna de senha NAO e' selecionada -- nem `authentication_string`,
    // nem `password`. O hash nao serve para nada na interface (nao da' para
    // mostrar, nao da' para reusar) e te-lo em memoria so' aumenta a
    // superficie de um vazamento.
    //
    // As colunas de estado mudaram de nome entre versoes: `account_locked` so'
    // existe do 5.7.6 em diante, e `password_expired` do 5.6.6. Pedi-las num
    // servidor anterior daria erro de coluna inexistente, que na arvore
    // pareceria falha de conexao.
    const bool has_lock = !mariadb_ && version_.at_least(5, 7);
    const bool has_expiry = mariadb_ || version_.at_least(5, 6);

    std::string columns = "user, host, plugin";
    if (has_expiry) columns += ", password_expired";
    if (has_lock)   columns += ", account_locked";

    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SELECT " + columns +
                    "  FROM mysql.user ORDER BY user, host"));

    std::vector<UserMeta> users;
    users.reserve(rs.row_count());

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        UserMeta user;
        user.name   = std::string(rs.text(row, 0));
        user.host   = std::string(rs.text(row, 1));
        user.plugin = std::string(rs.text(row, 2));

        std::size_t next = 3;
        if (has_expiry && next < rs.column_count()) {
            user.expired = rs.text(row, next++) == "Y";
        }
        if (has_lock && next < rs.column_count()) {
            user.locked = rs.text(row, next) == "Y";
        }
        users.push_back(std::move(user));
    }
    return users;
}

Result<std::vector<std::string>> MysqlCatalog::load_grants(std::string_view user,
                                                           std::string_view host) {
    // SHOW GRANTS FOR 'user'@'host' -- o par e' a identidade da conta no
    // MySQL, e pedir so' o nome pegaria a conta errada quando ha' 'root'@'%'
    // e 'root'@'localhost' com privilegios diferentes.
    OTTER_ASSIGN_OR_RETURN(
        auto rs,
        holt_.query("SHOW GRANTS FOR " + mysql_literal(user) + "@" +
                    mysql_literal(host)));

    std::vector<std::string> grants;
    grants.reserve(rs.row_count());

    // A resposta tem UMA coluna, cujo nome varia com a conta ("Grants for
    // root@localhost"). Por isso lemos pelo indice, nao pelo nome.
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        grants.emplace_back(rs.text(row, 0));
    }
    return grants;
}

} // namespace otter::db
