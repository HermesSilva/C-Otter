#include "db/catalog_reader.hpp"

#include "db/catalog_mssql.hpp"
#include "db/catalog_mysql.hpp"
#include "db/catalog_oracle.hpp"
#include "db/catalog_sqlanywhere.hpp"
#include "db/object_info_load.hpp"

namespace otter::db {
namespace {

// Adaptador: as duas classes de catalogo ja' tem os metodos com a assinatura
// certa, so' nao sao virtuais. Um template evita escrever o mesmo repasse
// duas vezes -- e evita que uma delas fique para tras quando a interface
// crescer.
template <typename Catalog>
class ReaderFor final : public CatalogReader {
public:
    explicit ReaderFor(Holt& holt) : catalog_(holt), holt_(holt) {}

    ObjectInfo load_object_info(const ObjectRef& ref) override {
        // A sobrecarga escolhe o SGBD pelo tipo do catalogo.
        return db::load_object_info(catalog_, holt_, ref);
    }

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
    [[nodiscard]] bool has_server_info() const noexcept override;
    [[nodiscard]] Result<std::vector<ServerVariable>> load_server_info(
        ServerInfoKind kind) override;
    [[nodiscard]] bool has_users() const noexcept override;
    [[nodiscard]] Result<std::vector<UserMeta>> load_users() override;
    [[nodiscard]] Result<std::vector<std::string>> load_grants(
        std::string_view user, std::string_view host) override;
    [[nodiscard]] bool has_database_level() const noexcept override;
    [[nodiscard]] Result<std::vector<DatabaseMeta>> load_databases(
        bool templates, bool unavailable) override;
    [[nodiscard]] Result<std::vector<CatalogItem>> load_list(
        CatalogList list, std::string_view a, std::string_view b,
        std::string_view c) override;

private:
    Catalog catalog_;
    Holt&   holt_;
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
bool ReaderFor<PostgresCatalog>::has_server_info() const noexcept {
    // O PostgreSQL tem pg_stat_* e pg_settings, mas com forma e significado
    // diferentes -- nao sao pares nome/valor equivalentes. Oferecer a pasta
    // com os dados errados seria pior que nao oferecer.
    return false;
}

template <>
Result<std::vector<ServerVariable>>
ReaderFor<PostgresCatalog>::load_server_info(ServerInfoKind) {
    return std::vector<ServerVariable>{};
}

template <>
bool ReaderFor<PostgresCatalog>::has_users() const noexcept {
    // No PostgreSQL o conceito e' ROLE, que e' usuario e grupo ao mesmo
    // tempo, e ja' tem no' proprio na arvore. Duplicar como "Usuarios" so'
    // confundiria.
    return false;
}

template <>
Result<std::vector<UserMeta>> ReaderFor<PostgresCatalog>::load_users() {
    return std::vector<UserMeta>{};
}

template <>
Result<std::vector<std::string>> ReaderFor<PostgresCatalog>::load_grants(
    std::string_view, std::string_view) {
    return std::vector<std::string>{};
}

template <>
bool ReaderFor<PostgresCatalog>::has_events() const noexcept {
    // O PostgreSQL nao tem evento agendado: usa pgAgent (uma extensao) ou cron
    // do sistema. Uma pasta "Eventos" sempre vazia so' faria procurar o que
    // nao existe.
    return false;
}

template <>
bool ReaderFor<PostgresCatalog>::has_database_level() const noexcept {
    return true;
}

template <>
Result<std::vector<DatabaseMeta>> ReaderFor<PostgresCatalog>::load_databases(
    bool templates, bool unavailable) {
    return catalog_.load_databases(templates, unavailable);
}

template <>
Result<std::vector<CatalogItem>> ReaderFor<PostgresCatalog>::load_list(
    CatalogList list, std::string_view a, std::string_view b,
    std::string_view c) {
    return catalog_.load_list(list, a, b, c);
}

template <>
bool ReaderFor<MysqlCatalog>::has_database_level() const noexcept {
    // No MySQL "database" e "schema" sao sinonimos, e a conexao enxerga
    // todos: load_schemas() ja' devolve os bancos.
    return false;
}

template <>
Result<std::vector<DatabaseMeta>> ReaderFor<MysqlCatalog>::load_databases(
    bool, bool) {
    return std::vector<DatabaseMeta>{};
}

template <>
Result<std::vector<CatalogItem>> ReaderFor<MysqlCatalog>::load_list(
    CatalogList, std::string_view, std::string_view, std::string_view) {
    // As listas de CatalogList sao as do `<tree>` do PostgreSQL. As do MySQL
    // (status, variaveis, engines, charsets, usuarios) tem caminho proprio.
    return std::vector<CatalogItem>{};
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
bool ReaderFor<MysqlCatalog>::has_server_info() const noexcept { return true; }

template <>
bool ReaderFor<MysqlCatalog>::has_users() const noexcept { return true; }

template <>
Result<std::vector<UserMeta>> ReaderFor<MysqlCatalog>::load_users() {
    return catalog_.load_users();
}

template <>
Result<std::vector<std::string>> ReaderFor<MysqlCatalog>::load_grants(
    std::string_view user, std::string_view host) {
    return catalog_.load_grants(user, host);
}

template <>
Result<std::vector<ServerVariable>>
ReaderFor<MysqlCatalog>::load_server_info(ServerInfoKind kind) {
    switch (kind) {
        case ServerInfoKind::session_status:    return catalog_.load_status(false);
        case ServerInfoKind::global_status:     return catalog_.load_status(true);
        case ServerInfoKind::session_variables: return catalog_.load_variables(false);
        case ServerInfoKind::global_variables:  return catalog_.load_variables(true);
        case ServerInfoKind::engines:           return catalog_.load_engines();
        case ServerInfoKind::charsets:          return catalog_.load_charsets();
        case ServerInfoKind::privileges:        return catalog_.load_privileges();
        case ServerInfoKind::plugins:           return catalog_.load_plugins();
    }
    return std::vector<ServerVariable>{};
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

// --- SQL Server -------------------------------------------------------------------

template <>
bool ReaderFor<MssqlCatalog>::has_database_level() const noexcept {
    // Servidor -> banco -> schema, como no PostgreSQL: cada banco e' navegado
    // por uma sessao propria.
    return true;
}

template <>
Result<std::vector<DatabaseMeta>> ReaderFor<MssqlCatalog>::load_databases(
    bool templates, bool unavailable) {
    return catalog_.load_databases(templates, unavailable);
}

template <>
Result<std::vector<CatalogItem>> ReaderFor<MssqlCatalog>::load_list(
    CatalogList list, std::string_view a, std::string_view b, std::string_view c) {
    return catalog_.load_list(list, a, b, c);
}

template <>
std::string ReaderFor<MssqlCatalog>::default_schema() const { return "dbo"; }

template <>
std::string_view ReaderFor<MssqlCatalog>::schema_label() const noexcept {
    return "Schema";
}

template <>
bool ReaderFor<MssqlCatalog>::has_sequences() const noexcept {
    return catalog_.version().at_least(11);   // SQL Server 2012
}

template <>
bool ReaderFor<MssqlCatalog>::has_user_types() const noexcept { return true; }

template <>
bool ReaderFor<MssqlCatalog>::has_events() const noexcept { return false; }

template <>
bool ReaderFor<MssqlCatalog>::has_server_info() const noexcept { return false; }

template <>
Result<std::vector<ServerVariable>>
ReaderFor<MssqlCatalog>::load_server_info(ServerInfoKind) {
    return std::vector<ServerVariable>{};
}

template <>
bool ReaderFor<MssqlCatalog>::has_users() const noexcept {
    // Os logins do servidor tem pasta propria (Security > Logins), pela lista
    // generica; a pasta "Users" e' a do MySQL.
    return false;
}

template <>
Result<std::vector<UserMeta>> ReaderFor<MssqlCatalog>::load_users() {
    return std::vector<UserMeta>{};
}

template <>
Result<std::vector<std::string>> ReaderFor<MssqlCatalog>::load_grants(std::string_view,
                                                                     std::string_view) {
    return std::vector<std::string>{};
}

// --- SQL Anywhere -----------------------------------------------------------------

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_database_level() const noexcept {
    // Uma conexao = um banco (um arquivo). O servidor pode ter outros abertos,
    // mas cada um tem os proprios usuarios e exige outro login: nao ha' nivel
    // de banco na arvore.
    return false;
}

template <>
Result<std::vector<DatabaseMeta>> ReaderFor<SqlAnywhereCatalog>::load_databases(bool,
                                                                                bool) {
    return std::vector<DatabaseMeta>{};
}

template <>
Result<std::vector<CatalogItem>> ReaderFor<SqlAnywhereCatalog>::load_list(
    CatalogList list, std::string_view a, std::string_view b, std::string_view c) {
    return catalog_.load_list(list, a, b, c);
}

template <>
std::string ReaderFor<SqlAnywhereCatalog>::default_schema() const {
    // O dono dos objetos faz o papel de schema: a arvore comeca no usuario
    // da conexao.
    return catalog_.user();
}

template <>
std::string_view ReaderFor<SqlAnywhereCatalog>::schema_label() const noexcept {
    return "Schema";
}

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_sequences() const noexcept {
    return catalog_.version().at_least(12);
}

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_user_types() const noexcept { return true; }

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_events() const noexcept { return true; }

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_server_info() const noexcept { return true; }

template <>
Result<std::vector<ServerVariable>>
ReaderFor<SqlAnywhereCatalog>::load_server_info(ServerInfoKind kind) {
    // As quatro pastas de pares nome/valor, com o que o SQL Anywhere tem em
    // cada lugar: propriedades da conexao e do servidor, opcoes em vigor e
    // propriedades do banco. As demais (engines, charsets...) sao do MySQL.
    switch (kind) {
        case ServerInfoKind::session_status:    return catalog_.load_properties("connection");
        case ServerInfoKind::global_status:     return catalog_.load_properties("server");
        case ServerInfoKind::session_variables: return catalog_.load_options();
        case ServerInfoKind::global_variables:  return catalog_.load_properties("database");
        default:                                break;
    }
    return std::vector<ServerVariable>{};
}

template <>
bool ReaderFor<SqlAnywhereCatalog>::has_users() const noexcept { return true; }

template <>
Result<std::vector<UserMeta>> ReaderFor<SqlAnywhereCatalog>::load_users() {
    return catalog_.load_users();
}

template <>
Result<std::vector<std::string>> ReaderFor<SqlAnywhereCatalog>::load_grants(
    std::string_view user, std::string_view) {
    return catalog_.load_grants(user);
}

// --- Oracle -----------------------------------------------------------------------

template <>
bool ReaderFor<OracleCatalog>::has_database_level() const noexcept {
    // Nao ha' banco acima do schema: load_schemas() ja' devolve os usuarios
    // que a conexao enxerga, como os bancos do MySQL.
    return false;
}

template <>
Result<std::vector<DatabaseMeta>> ReaderFor<OracleCatalog>::load_databases(bool, bool) {
    return std::vector<DatabaseMeta>{};
}

template <>
Result<std::vector<CatalogItem>> ReaderFor<OracleCatalog>::load_list(
    CatalogList list, std::string_view a, std::string_view b, std::string_view c) {
    return catalog_.load_list(list, a, b, c);
}

template <>
std::string ReaderFor<OracleCatalog>::default_schema() const {
    // O schema E' o usuario: a arvore comeca no da conexao.
    return catalog_.user();
}

template <>
std::string_view ReaderFor<OracleCatalog>::schema_label() const noexcept {
    return "Schema";
}

template <>
bool ReaderFor<OracleCatalog>::has_sequences() const noexcept { return true; }

template <>
bool ReaderFor<OracleCatalog>::has_user_types() const noexcept { return true; }

template <>
bool ReaderFor<OracleCatalog>::has_events() const noexcept {
    // O DBMS_SCHEDULER existe, mas ainda nao e' lido: uma pasta sempre vazia
    // faria procurar o que o programa nao mostra.
    return false;
}

template <>
bool ReaderFor<OracleCatalog>::has_server_info() const noexcept {
    // V$PARAMETER e V$SESSION exigem privilegio que a conta comum nao tem.
    return false;
}

template <>
Result<std::vector<ServerVariable>>
ReaderFor<OracleCatalog>::load_server_info(ServerInfoKind) {
    return std::vector<ServerVariable>{};
}

template <>
bool ReaderFor<OracleCatalog>::has_users() const noexcept { return false; }

template <>
Result<std::vector<UserMeta>> ReaderFor<OracleCatalog>::load_users() {
    return std::vector<UserMeta>{};
}

template <>
Result<std::vector<std::string>> ReaderFor<OracleCatalog>::load_grants(std::string_view,
                                                                      std::string_view) {
    return std::vector<std::string>{};
}

} // namespace

std::unique_ptr<CatalogReader> make_catalog_reader(std::string_view driver_id,
                                                   Holt& holt) {
    if (driver_id == "oracle") {
        return std::make_unique<ReaderFor<OracleCatalog>>(holt);
    }
    if (driver_id == "postgresql") {
        return std::make_unique<ReaderFor<PostgresCatalog>>(holt);
    }
    if (driver_id == "mysql" || driver_id == "mariadb") {
        return std::make_unique<ReaderFor<MysqlCatalog>>(holt);
    }
    if (driver_id == "sqlserver" || driver_id == "mssql") {
        return std::make_unique<ReaderFor<MssqlCatalog>>(holt);
    }
    if (driver_id == "sqlanywhere") {
        return std::make_unique<ReaderFor<SqlAnywhereCatalog>>(holt);
    }
    // Driver desconhecido nao ganha um leitor padrao: consultar o pg_catalog
    // num servidor que nao e' PostgreSQL daria um erro incompreensivel.
    return nullptr;
}

} // namespace otter::db
