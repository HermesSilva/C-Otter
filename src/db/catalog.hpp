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

// Tipo definido pelo usuario. O `typtype` do pg_type decide o que preencher:
// enum tem valores, composto tem atributos, domain tem tipo base e restricoes.
enum class TypeKind : std::uint8_t {
    base,        // 'b' -- escalar do proprio PostgreSQL
    composite,   // 'c' -- CREATE TYPE ... AS (campo tipo, ...)
    domain,      // 'd' -- CREATE DOMAIN
    enumeration, // 'e' -- CREATE TYPE ... AS ENUM
    range,       // 'r' -- CREATE TYPE ... AS RANGE
    pseudo,      // 'p' -- trigger, record, void
};

[[nodiscard]] std::string_view to_string(TypeKind kind) noexcept;

struct TypeAttributeMeta {
    std::string name;
    std::string type_name;
    bool        nullable = true;
};

struct DataTypeMeta {
    std::string name;
    TypeKind    kind = TypeKind::base;
    std::string comment;
    std::string owner;

    // Enum: os rotulos na ordem de enumsortorder -- a ordem importa, porque e'
    // ela que define a comparacao entre valores do tipo.
    std::vector<std::string> enum_values;

    // Composto: os campos declarados.
    std::vector<TypeAttributeMeta> attributes;

    // Domain: o tipo sobre o qual foi criado, mais as restricoes.
    std::string base_type;
    std::string default_value;
    std::string check_constraint;
    bool        not_null = false;

    // Range: o subtipo sobre o qual o intervalo e' definido.
    std::string subtype;

    [[nodiscard]] bool has_children() const noexcept {
        return !enum_values.empty() || !attributes.empty();
    }
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

// Partição de uma tabela. Só o MySQL e o PostgreSQL 10+ têm.
//
// O PostgreSQL implementa partição como TABELA de verdade, com nome próprio e
// entrada no catálogo; o MySQL, como divisão INTERNA de uma tabela só. Por
// isso `is_table`: no PostgreSQL dá para consultar a partição diretamente, no
// MySQL não -- e oferecer "ver dados desta partição" onde não dá seria um
// campo que finge funcionar.
struct PartitionMeta {
    std::string  name;
    std::string  method;        // RANGE, LIST, HASH, KEY
    std::string  expression;    // a coluna ou expressão que particiona
    std::string  description;   // o limite: "1000", "MAXVALUE", "'sul','norte'"
    std::int64_t estimated_rows = 0;
    std::string  size_pretty;
    bool         is_table = false;

    // Subpartições, quando há. O MySQL permite um nível.
    std::vector<std::string> subpartitions;
};

// Evento agendado. Só o MySQL tem -- o PostgreSQL usa pgAgent ou cron externo.
struct EventMeta {
    std::string name;
    std::string definer;
    std::string type;          // ONE TIME ou RECURRING
    std::string schedule;      // "EVERY 1 DAY", ou o instante de EXECUTE AT
    std::string starts;
    std::string ends;
    std::string status;        // ENABLED, DISABLED, SLAVESIDE_DISABLED
    std::string on_completion;
    std::string last_executed;
    std::string definition;
    std::string comment;
};

// Uma variável ou estatística do servidor: nome e valor.
//
// Serve para as quatro pastas de System Info (status e variáveis, de sessão e
// globais), para engines e para charsets -- todas são pares nome/valor com um
// detalhe a mais.
struct ServerVariable {
    std::string name;
    std::string value;
    std::string detail;        // descrição, ou o valor secundário
};

struct TableMeta {
    std::string  name;
    ObjKind      kind = ObjKind::table;

    // OID no pg_class. Casa com o `source_table_oid` das colunas do resultado
    // e e' o que permite saber de que tabela um SELECT veio (ADR 0014).
    std::uint32_t oid = 0;

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

    // Particoes. Vazio quando a tabela nao e' particionada -- e a pasta nao
    // aparece, como no `visibleIf` do DBeaver.
    std::vector<PartitionMeta>  partitions;

    // Corpo da view (`pg_get_viewdef`). Vazio para tabelas; carregado sob
    // demanda, porque uma view de relatorio pode ter varios KB de SQL.
    std::string definition;

    bool columns_loaded     = false;
    bool constraints_loaded = false;
    bool indexes_loaded     = false;
    bool keys_loaded        = false;
    bool triggers_loaded    = false;
    bool partitions_loaded  = false;
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
    std::vector<DataTypeMeta> types;

    // Eventos agendados. So' o MySQL tem.
    std::vector<EventMeta>    events;

    bool tables_loaded    = false;
    bool sequences_loaded = false;
    bool routines_loaded  = false;
    bool types_loaded     = false;
    bool events_loaded    = false;
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

    // Particoes. No PostgreSQL 10+ cada uma e' uma TABELA de verdade, com
    // nome proprio e entrada no pg_class -- da' para consultar diretamente,
    // ao contrario do MySQL.
    [[nodiscard]] Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table);

    // O PostgreSQL nao tem evento agendado: usa pgAgent ou cron do sistema.
    // Devolve vazio sempre; existe para a interface bater com o MysqlCatalog.
    [[nodiscard]] Result<std::vector<EventMeta>> load_events(
        std::string_view schema);

    // --- Por schema ----------------------------------------------------------

    [[nodiscard]] Result<std::vector<SequenceMeta>> load_sequences(
        std::string_view schema);

    [[nodiscard]] Result<std::vector<RoutineMeta>> load_routines(
        std::string_view schema);

    // Tipos definidos pelo usuario: enum, composto, domain e range.
    //
    // Os valores do enum e os atributos do composto vem na mesma passagem:
    // sao poucos por tipo, e uma consulta por tipo expandido multiplicaria o
    // custo sem ganho perceptivel.
    [[nodiscard]] Result<std::vector<DataTypeMeta>> load_types(
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
