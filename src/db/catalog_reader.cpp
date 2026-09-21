#include "db/catalog_reader.hpp"

#include "db/catalog_mysql.hpp"

namespace otter::db {
namespace {

// Adaptador: as duas classes de catalogo ja' tem os metodos com a assinatura
// certa, so' nao sao virtuais. Um template evita escrever o mesmo repasse
// duas vezes -- e evita que uma delas fique para tras quando a interface
// crescer.
template <typename Catalog>
class ReaderFor final : public CatalogReader {
public:
    explicit ReaderFor(Holt& holt) : catalog_(holt) {}

    Result<std::vector<SchemaMeta>> load_schemas() override {
        return catalog_.load_schemas();
    }
    Result<std::vector<TableMeta>> load_tables(std::string_view schema) override {
        return catalog_.load_tables(schema);
    }
    Result<std::vector<ColumnMeta>> load_columns(std::string_view schema,
                                                 std::string_view table) override {
        return catalog_.load_columns(schema, table);
    }
    Result<std::vector<ForeignKeyMeta>> load_foreign_keys(
        std::string_view schema) override {
        return catalog_.load_foreign_keys(schema);
    }
    Result<std::vector<ConstraintMeta>> load_constraints(
        std::string_view schema, std::string_view table) override {
        return catalog_.load_constraints(schema, table);
    }
    Result<std::vector<IndexMeta>> load_indexes(std::string_view schema,
                                                std::string_view table) override {
        return catalog_.load_indexes(schema, table);
    }
    Result<std::vector<ForeignKeyMeta>> load_table_foreign_keys(
        std::string_view schema, std::string_view table) override {
        return catalog_.load_table_foreign_keys(schema, table);
    }
    Result<std::vector<ForeignKeyMeta>> load_references(
        std::string_view schema, std::string_view table) override {
        return catalog_.load_references(schema, table);
    }
    Result<std::vector<TriggerMeta>> load_triggers(std::string_view schema,
                                                   std::string_view table) override {
        return catalog_.load_triggers(schema, table);
    }
    Result<std::vector<PartitionMeta>> load_partitions(
        std::string_view schema, std::string_view table) override {
        return catalog_.load_partitions(schema, table);
    }
    Result<std::vector<EventMeta>> load_events(std::string_view schema) override {
        return catalog_.load_events(schema);
    }
    Result<std::vector<SequenceMeta>> load_sequences(std::string_view schema) override {
        return catalog_.load_sequences(schema);
    }
    Result<std::vector<RoutineMeta>> load_routines(std::string_view schema) override {
        return catalog_.load_routines(schema);
    }
    Result<std::vector<DataTypeMeta>> load_types(std::string_view schema) override {
        return catalog_.load_types(schema);
    }
    Result<std::string> load_routine_definition(std::string_view schema,
                                                std::string_view name,
                                                std::string_view arguments) override {
        return catalog_.load_routine_definition(schema, name, arguments);
    }
    Result<std::string> load_view_definition(std::string_view schema,
                                             std::string_view name) override {
        return catalog_.load_view_definition(schema, name);
    }

    [[nodiscard]] std::string default_schema() const override;
    [[nodiscard]] std::string_view schema_label() const noexcept override;
    [[nodiscard]] bool has_sequences() const noexcept override;
    [[nodiscard]] bool has_user_types() const noexcept override;
    [[nodiscard]] bool has_events() const noexcept override;

private:
    Catalog catalog_;
};

template <>
std::string ReaderFor<PostgresCatalog>::default_schema() const {
    return "public";
}

template <>
std::string_view ReaderFor<PostgresCatalog>::schema_label() const noexcept {
    return "Schema";
}

template <>
bool ReaderFor<PostgresCatalog>::has_sequences() const noexcept { return true; }

template <>
bool ReaderFor<PostgresCatalog>::has_user_types() const noexcept { return true; }

template <>
bool ReaderFor<PostgresCatalog>::has_events() const noexcept {
    // O PostgreSQL nao tem evento agendado: usa pgAgent (uma extensao) ou cron
    // do sistema. Uma pasta "Eventos" sempre vazia so' faria procurar o que
    // nao existe.
    return false;
}

template <>
std::string ReaderFor<MysqlCatalog>::default_schema() const {
    // Nao existe nivel de schema no MySQL: o banco corrente faz esse papel.
    // Quem chama usa o banco da conexao quando isto vem vazio.
    return {};
}

template <>
std::string_view ReaderFor<MysqlCatalog>::schema_label() const noexcept {
    return "Database";
}

template <>
bool ReaderFor<MysqlCatalog>::has_sequences() const noexcept {
    // Sequences so' existem no MariaDB 10.3+. Perguntar ao catalogo, e nao
    // cravar false: num MariaDB a pasta precisa aparecer.
    return catalog_.is_mariadb() && catalog_.version().at_least(10, 3);
}

template <>
bool ReaderFor<MysqlCatalog>::has_events() const noexcept {
    // Eventos existem desde o MySQL 5.1 e no MariaDB.
    return catalog_.is_mariadb() || catalog_.version().at_least(5, 1);
}

template <>
bool ReaderFor<MysqlCatalog>::has_user_types() const noexcept {
    // O MySQL nao tem CREATE TYPE. ENUM e SET sao atributos de COLUNA, e ja'
    // aparecem no tipo dela -- uma pasta "Tipos de dados" sempre vazia so'
    // faria o usuario procurar o que nao existe.
    return false;
}

} // namespace

std::unique_ptr<CatalogReader> make_catalog_reader(std::string_view driver_id,
                                                   Holt& holt) {
    if (driver_id == "postgresql") {
        return std::make_unique<ReaderFor<PostgresCatalog>>(holt);
    }
    if (driver_id == "mysql" || driver_id == "mariadb") {
        return std::make_unique<ReaderFor<MysqlCatalog>>(holt);
    }
    // Driver desconhecido nao ganha um leitor padrao: consultar o pg_catalog
    // num servidor que nao e' PostgreSQL daria um erro incompreensivel.
    return nullptr;
}

} // namespace otter::db
