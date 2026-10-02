// Executa contra um SQL Anywhere de verdade o que o perfil SQL Anywhere GERA e
// LE': o protocolo (lib/tdswire/tds5.cpp), o catalogo
// (db/catalog_sqlanywhere.cpp), o editor de objeto (db/sqlanywhere_object.cpp),
// o ALTER TABLE (db/alter.cpp), a paginacao por TOP / START AT
// (sql/paging.cpp), a gravacao da grade (db/edit.cpp) e a importacao.
//
// Um comando sintaticamente plausivel pode ser recusado pelo servidor -- ou,
// pior, aceito fazendo outra coisa. Aqui cada gerador roda e o EFEITO e' lido
// de volta do catalogo.
//
// Duas partes:
//
//   1. LEITURA do banco em que a conexao cai. Se for o `demo` que acompanha o
//      SQL Anywhere, confere o catalogo contra o que se sabe dele (o dono
//      GROUPO, a tabela Employees...). Nada e' escrito ali.
//
//   2. ESCRITA num banco de rascunho (`otter_scratch_sa`): um arquivo criado
//      na pasta temporaria por CREATE DATABASE, aberto no mesmo servidor e
//      apagado no fim por DROP DATABASE.
//
//     otter_tests_sqlanywhere_live [<host> [<port>]]
//
// Usuario DBA; a senha vem de OTTER_SA_PASSWORD (sem ela, "sql" -- a senha
// documentada do banco de demonstracao). So' roda contra localhost: o teste
// cria e apaga um banco.
#include "db/alter.hpp"
#include "db/app_tools.hpp"
#include "db/catalog_sqlanywhere.hpp"
#include "db/ddl.hpp"
#include "db/drivers/sqlanywhere.hpp"
#include "db/edit.hpp"
#include "db/grid_ops.hpp"
#include "db/import.hpp"
#include "db/object_info_load.hpp"
#include "db/sqlanywhere_object.hpp"
#include "sql/dialect.hpp"
#include "sql/paging.hpp"
#include "sql/script.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

using namespace otter::db;

namespace {

int failures = 0;
int checks   = 0;
Holt* g_holt = nullptr;

void check(bool condition, const std::string& what) {
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what.c_str());
    if (!condition) ++failures;
}

void equal(const std::string& got, const std::string& wanted, const std::string& what) {
    check(got == wanted, what + (got == wanted ? "" : "  -- veio '" + got + "'"));
}

std::string scalar(const std::string& sql) {
    auto rs = g_holt->query(sql);
    if (!rs) return "(erro: " + rs.error().to_string() + ")";
    if (rs->row_count() == 0 || rs->is_null(0, 0)) return {};
    return std::string(rs->text(0, 0));
}

// Roda o script inteiro; devolve o primeiro erro, ou vazio.
std::string run(const AlterScript& script) {
    if (!script.ok()) return "(nao gerado: " + script.error + ")";
    for (const std::string& statement : script.statements) {
        if (auto status = g_holt->execute(statement); !status) {
            return status.error().to_string() + "  <<  " + statement;
        }
    }
    return {};
}

void runs(const AlterScript& script, const std::string& what) {
    const std::string error = run(script);
    check(error.empty(), what + (error.empty() ? "" : "  -- " + error));
}

void sql(const std::string& statement, const std::string& what) {
    auto status = g_holt->execute(statement);
    check(static_cast<bool>(status),
          what + (status ? "" : "  -- " + status.error().to_string()));
}

ObjectRef ref_of(ObjectType type, const char* schema, const char* name,
                 const char* parent = "") {
    ObjectRef ref;
    ref.type   = type;
    ref.schema = schema;
    ref.name   = name;
    ref.parent = parent;
    return ref;
}

const std::string* property(const ObjectInfo& info, std::string_view name) {
    for (const ObjectProperty& item : info.properties) {
        if (item.name == name) return &item.value;
    }
    return nullptr;
}

bool contains(const std::string& text, std::string_view piece) {
    return text.find(piece) != std::string::npos;
}

TableMeta loaded_table(SqlAnywhereCatalog& catalog, const char* schema, const char* name) {
    TableMeta table;
    table.name = name;
    table.kind = ObjKind::table;
    if (auto columns = catalog.load_columns(schema, name)) {
        table.columns        = std::move(*columns);
        table.columns_loaded = true;
    }
    if (auto constraints = catalog.load_constraints(schema, name)) {
        table.constraints        = std::move(*constraints);
        table.constraints_loaded = true;
    }
    return table;
}

const ColumnMeta* column_of(const TableMeta& table, std::string_view name) {
    for (const ColumnMeta& column : table.columns) {
        if (column.name == name) return &column;
    }
    return nullptr;
}

template <typename Item>
const Item* named(const std::vector<Item>& items, std::string_view name) {
    for (const Item& item : items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

constexpr const char* kDb       = "otter_scratch_sa";
constexpr const char* kUser     = "otter_user";
constexpr const char* kPassword = "Otter#Scratch'9x";   // aspa na senha: o literal a dobra

// --- Parte 1: o banco `demo`, so' leitura -------------------------------------------

void read_demo(SqlAnywhereCatalog& catalog) {
    std::printf("\nBanco demo (so' leitura)\n");

    auto schemas = catalog.load_schemas();
    check(schemas && named(*schemas, "GROUPO") != nullptr && !schemas->empty() &&
              schemas->front().name == catalog.user(),
          "load_schemas: GROUPO esta' la', e o usuario da conexao vem primeiro");

    auto tables = catalog.load_tables("GROUPO");
    check(tables && named(*tables, "Customers") != nullptr &&
              named(*tables, "3731doc1") == nullptr,
          "load_tables: Customers; as tabelas internas de indice de texto nao");
    if (tables) {
        const TableMeta* view = named(*tables, "ViewSalesOrders");
        const TableMeta* materialized = named(*tables, "EmployeeConfidential");
        const TableMeta* customers = named(*tables, "Customers");
        check(view != nullptr && view->kind == ObjKind::view, "ViewSalesOrders e' view");
        check(materialized != nullptr && materialized->kind == ObjKind::materialized_view,
              "EmployeeConfidential e' view materializada");
        check(customers != nullptr && customers->estimated_rows == 126 &&
                  customers->size_bytes > 0 && !customers->comment.empty(),
              "Customers: 126 linhas, tamanho e comentario");
    }

    const TableMeta employees = loaded_table(catalog, "GROUPO", "Employees");
    check(employees.columns.size() == 21, "Employees: 21 colunas");
    const ColumnMeta* id = column_of(employees, "EmployeeID");
    check(id != nullptr && id->primary_key && !id->nullable && id->kind == DataKind::integer,
          "EmployeeID: chave, NOT NULL, inteiro");
    const ColumnMeta* salary = column_of(employees, "Salary");
    check(salary != nullptr && salary->type_name == "numeric(20,3)" &&
              salary->kind == DataKind::numeric,
          "Salary: numeric(20,3)");
    const ColumnMeta* start = column_of(employees, "StartDate");
    check(start != nullptr && start->kind == DataKind::date, "StartDate: date");
    const ColumnMeta* benefit = column_of(employees, "BenefitDayCare");
    check(benefit != nullptr && benefit->kind == DataKind::boolean && benefit->nullable,
          "BenefitDayCare: bit, aceita NULL");

    bool pk = false, unique = false, column_check = false;
    for (const ConstraintMeta& constraint : employees.constraints) {
        pk     = pk || (constraint.kind == ObjKind::primary_key &&
                        constraint.columns == "EmployeeID");
        unique = unique || (constraint.kind == ObjKind::unique_key && constraint.name == "SSN");
        column_check = column_check || (constraint.kind == ObjKind::check_constraint &&
                                        contains(constraint.definition, "CHECK"));
    }
    check(pk && unique && column_check, "constraints: PK, UNIQUE (SSN) e CHECK (Sexes)");

    const TableMeta orders = loaded_table(catalog, "GROUPO", "SalesOrders");
    const ColumnMeta* order_id = column_of(orders, "ID");
    check(order_id != nullptr && order_id->default_value == "autoincrement",
          "SalesOrders.ID: default autoincrement");

    auto indexes = catalog.load_indexes("GROUPO", "Customers");
    const IndexMeta* by_name = indexes ? named(*indexes, "IX_customer_name") : nullptr;
    check(by_name != nullptr && by_name->columns == "Surname,GivenName" && !by_name->unique &&
              contains(by_name->definition, "CREATE INDEX"),
          "load_indexes: IX_customer_name (Surname, GivenName)");
    const IndexMeta* key_index = indexes ? named(*indexes, "CustomersKey") : nullptr;
    check(key_index != nullptr && key_index->primary && key_index->definition.empty(),
          "o indice da chave primaria nao vira CREATE INDEX");

    auto keys = catalog.load_table_foreign_keys("GROUPO", "SalesOrderItems");
    const ForeignKeyMeta* to_orders = keys ? named(*keys, "FK_ID_ID") : nullptr;
    check(to_orders != nullptr && to_orders->target_table == "SalesOrders" &&
              to_orders->source_column == "ID" && to_orders->target_column == "ID" &&
              to_orders->on_delete == "CASCADE" && to_orders->on_update == "RESTRICT",
          "chave estrangeira: alvo, colunas e ON DELETE CASCADE");
    auto references = catalog.load_references("GROUPO", "Customers");
    check(references && references->size() == 2, "load_references: quem aponta para Customers");
    auto all_keys = catalog.load_foreign_keys("GROUPO");
    check(all_keys && all_keys->size() == 10, "load_foreign_keys: as 10 do dono");

    auto triggers = catalog.load_triggers("GROUPO", "Departments");
    const TriggerMeta* trigger = triggers ? named(*triggers, "TR_change_managers") : nullptr;
    check(triggers && triggers->size() == 2 && trigger != nullptr &&
              trigger->timing == "BEFORE" && trigger->events == "UPDATE" &&
              contains(trigger->definition, "TR_change_managers"),
          "load_triggers: so' os do usuario (nao as acoes referenciais)");

    auto routines = catalog.load_routines("GROUPO");
    const RoutineMeta* show = routines ? named(*routines, "ShowCustomerProducts") : nullptr;
    check(routines && routines->size() == 9 && show != nullptr &&
              show->kind == ObjKind::procedure &&
              show->arguments == "IN customer_ID integer" && show->return_type == "TABLE",
          "load_routines: 9 rotinas; os parametros sem as colunas do resultado");
    if (show != nullptr) {
        std::string call = routine_call_sql("GROUPO", *show, false);
        const std::size_t at = call.find(":customer_ID");
        if (at != std::string::npos) call.replace(at, 12, "101");
        auto rs = g_holt->query(call);
        check(rs && rs->row_count() == 10 && rs->column_count() == 3,
              "a chamada gerada da procedure devolve o conjunto de resultado" +
                  (rs ? std::string{} : "  -- " + rs.error().to_string() + " << " + call));
    }
    auto parameters =
        catalog.load_list(CatalogList::routine_parameters, "GROUPO", "ShowCustomerProducts", {});
    check(parameters && parameters->size() == 4 && parameters->front().detail == "integer" &&
              parameters->back().tooltip == "RESULT",
          "parametros da rotina: a entrada e as tres colunas do resultado");

    auto definition = catalog.load_view_definition("GROUPO", "ViewSalesOrders");
    check(definition && contains(*definition, "SalesOrderItems"), "definicao da view");
    auto source = catalog.load_routine_definition("GROUPO", "ShowCustomers", {});
    check(source && contains(*source, "ShowCustomers"), "fonte da procedure");

    auto types = catalog.load_types("DBA");
    const DataTypeMeta* domain = types ? named(*types, "person_name_t") : nullptr;
    check(domain != nullptr && domain->base_type == "char(20)" &&
              domain->kind == TypeKind::domain,
          "load_types: os dominios do DBA");

    auto dependencies =
        catalog.load_list(CatalogList::dependencies, "GROUPO", "ViewSalesOrders", {});
    check(dependencies && dependencies->size() == 3, "dependencias da view: tres tabelas");
    auto schema_indexes = catalog.load_list(CatalogList::schema_indexes, "GROUPO", {}, {});
    check(schema_indexes && schema_indexes->size() >= 29, "indices do dono");

    auto roles = catalog.load_list(CatalogList::roles, {}, {}, {});
    const CatalogItem* dba = roles ? named(*roles, "DBA") : nullptr;
    const CatalogItem* read_role = roles ? named(*roles, "READ_ROLE") : nullptr;
    check(dba != nullptr && dba->flag && read_role != nullptr && !read_role->flag,
          "usuarios e papeis: DBA entra no banco, READ_ROLE e' papel");
    auto members = catalog.load_list(CatalogList::role_members, "READ_ROLE", {}, {});
    check(members && named(*members, "BROWSER") != nullptr, "membros de READ_ROLE");

    // As pastas do Sybase Central (ui/navigator.cpp, draw_sqlanywhere_tree).
    auto pure_roles = catalog.load_list(CatalogList::pure_roles, {}, {}, {});
    check(pure_roles && named(*pure_roles, "READ_ROLE") != nullptr &&
              named(*pure_roles, "PUBLIC") != nullptr && named(*pure_roles, "DBA") == nullptr &&
              named(*pure_roles, "SYS_AUTH_DBA_ROLE") == nullptr,
          "papeis puros: sem os usuarios e sem os privilegios de sistema" +
              (pure_roles ? std::string{} : "  -- " + pure_roles.error().to_string()));
    auto policies = catalog.load_list(CatalogList::login_policies, {}, {}, {});
    check(policies && named(*policies, "root") != nullptr, "politicas de login");
    auto policy_options =
        catalog.load_list(CatalogList::login_policy_options, "root", {}, {});
    const CatalogItem* locked =
        policy_options ? named(*policy_options, "locked") : nullptr;
    check(locked != nullptr && locked->detail == "Off", "opcoes da politica root");
    auto text_configs = catalog.load_list(CatalogList::text_configurations, {}, {}, {});
    check(text_configs && named(*text_configs, "default_char") != nullptr,
          "objetos de configuracao de texto" +
              (text_configs ? std::string{} : "  -- " + text_configs.error().to_string()));
    auto environments = catalog.load_list(CatalogList::external_environments, {}, {}, {});
    check(environments && named(*environments, "java") != nullptr, "ambientes externos");
    auto srs = catalog.load_list(CatalogList::spatial_reference_systems, {}, {}, {});
    const CatalogItem* wgs84 = srs ? named(*srs, "WGS 84") : nullptr;
    check(wgs84 != nullptr && wgs84->detail == "4326",
          "sistemas de referencia espacial" +
              (srs ? std::string{} : "  -- " + srs.error().to_string()));
    for (const CatalogList list :
         {CatalogList::foreign_servers, CatalogList::web_services, CatalogList::publications}) {
        auto items = catalog.load_list(list, {}, {}, {});
        check(static_cast<bool>(items),
              "lista (pode estar vazia no demo) responde sem erro" +
                  (items ? std::string{} : "  -- " + items.error().to_string()));
    }

    auto users = catalog.load_users();
    check(users && named(*users, "DBA") != nullptr && named(*users, "READ_ROLE") == nullptr,
          "load_users: so' quem entra no banco");
    auto grants = catalog.load_grants("READ_ROLE");
    bool select_customers = false;
    if (grants) {
        for (const std::string& grant : *grants) {
            select_customers = select_customers ||
                               grant == "GRANT SELECT ON \"GROUPO\".\"Customers\" TO \"READ_ROLE\"";
        }
    }
    check(select_customers, "load_grants: as permissoes de tabela como GRANT");

    auto options = catalog.load_options();
    const ServerVariable* quoted = options ? named(*options, "quoted_identifier") : nullptr;
    check(quoted != nullptr && quoted->value == "On",
          "opcoes da conexao: quoted_identifier ligado pelo driver");
    auto server = catalog.load_properties("server");
    check(server && named(*server, "ProductVersion") != nullptr, "propriedades do servidor");
    auto database = catalog.load_properties("database");
    check(database && named(*database, "PageSize") != nullptr, "propriedades do banco");

    // --- Editor de objeto ----------------------------------------------------------
    ObjectInfo info =
        load_object_info(catalog, *g_holt, ref_of(ObjectType::table, "GROUPO", "Departments"));
    check(info.error.empty() && contains(info.ddl, "CREATE TABLE \"GROUPO\".\"Departments\"") &&
              contains(info.ddl, "TR_change_managers") && !contains(info.ddl, "\r"),
          "tabela: o DDL do servidor, com os triggers, sem CR" +
              (info.error.empty() ? "" : " -- " + info.error));
    const std::string* rows = property(info, "Row count");
    check(rows != nullptr && *rows == "5", "tabela: 5 linhas nas propriedades");
    bool read_select = false, modify_insert = false;
    for (const ObjectPermission& permission : info.permissions) {
        read_select   = read_select || (permission.grantee == "READ_ROLE" &&
                                        permission.privilege == "SELECT");
        modify_insert = modify_insert || (permission.grantee == "MODIFY_ROLE" &&
                                          permission.privilege == "INSERT");
    }
    check(info.has_permissions && read_select && modify_insert,
          "tabela: as permissoes, uma linha por privilegio");
    check(g_holt->txn_state() == TxnState::idle, "ler o DDL nao deixa transacao aberta");
    {
        // As datas das propriedades saem como na grade: sem a fracao de zeros.
        const ObjectInfo customers =
            load_object_info(catalog, *g_holt, ref_of(ObjectType::table, "GROUPO", "Customers"));
        bool found = false;
        bool trimmed_fraction = true;
        for (const ObjectProperty& property : customers.properties) {
            if (property.name != "Created") continue;
            found = true;
            trimmed_fraction = property.value.size() == 19;
        }
        check(found && trimmed_fraction,
              "propriedade Created: data e hora sem a fracao de zeros");
    }
    equal(scalar("SELECT connection_property('chained') || '/' || "
                 "connection_property('date_format')"),
          "Off/YYYY-MM-DD",
          "as opcoes que sa_get_table_definition troca foram repostas");

    info = load_object_info(catalog, *g_holt,
                            ref_of(ObjectType::view, "GROUPO", "ViewSalesOrders"));
    check(info.error.empty() && contains(info.ddl, "ViewSalesOrders") &&
              property(info, "Status") != nullptr && *property(info, "Status") == "valid",
          "view: fonte e estado");
    info = load_object_info(catalog, *g_holt,
                            ref_of(ObjectType::materialized_view, "GROUPO", "EmployeeConfidential"));
    check(info.error.empty() && property(info, "Refresh type") != nullptr,
          "view materializada: propriedades" + (info.error.empty() ? "" : " -- " + info.error));
    info = load_object_info(catalog, *g_holt,
                            ref_of(ObjectType::procedure, "GROUPO", "ShowContacts"));
    check(info.error.empty() && contains(info.ddl, "ShowContacts") &&
              property(info, "Kind") != nullptr && *property(info, "Kind") == "procedure",
          "procedure: fonte e tipo");
    info = load_object_info(
        catalog, *g_holt,
        ref_of(ObjectType::trigger, "GROUPO", "TR_change_departments", "Departments"));
    check(info.error.empty() && contains(info.ddl, "TR_change_departments") &&
              property(info, "Timing") != nullptr && *property(info, "Timing") == "AFTER",
          "trigger: fonte e momento");
    info = load_object_info(catalog, *g_holt, ref_of(ObjectType::schema, "", "GROUPO"));
    check(info.error.empty() && property(info, "Kind") != nullptr &&
              *property(info, "Kind") == "user, role",
          "dono (GROUPO): usuario que tambem e' papel");
    info = load_object_info(catalog, *g_holt, ref_of(ObjectType::database, "", "demo"));
    check(info.error.empty() && property(info, "Page size") != nullptr &&
              *property(info, "Page size") == "4096",
          "banco: tamanho de pagina");
    info = load_object_info(catalog, *g_holt,
                            ref_of(ObjectType::data_type, "DBA", "person_name_t"));
    check(info.error.empty() && contains(info.ddl, "CREATE DOMAIN \"person_name_t\" char(20)"),
          "dominio: o CREATE DOMAIN montado");

    // --- O que o driver informa sobre uma consulta ------------------------------------
    auto rs = g_holt->query(
        "SELECT c.ID, c.Surname AS sobrenome, o.OrderDate, o.ID + 1 AS soma "
        "FROM GROUPO.Customers c JOIN GROUPO.SalesOrders o ON o.CustomerID = c.ID "
        "WHERE o.ID = 2001");
    check(rs && rs->row_count() == 1 && rs->column_count() == 4, "consulta com apelido e JOIN");
    if (rs && rs->column_count() == 4) {
        const ColumnInfo& alias = rs->column(1).info();
        check(alias.name == "sobrenome" && alias.source_table == "Customers" &&
                  alias.source_schema == "GROUPO" && alias.source_column_name == "Surname" &&
                  alias.type_name == "char(20)",
              "coluna sob apelido: tabela e nome REAIS, e o tipo com o tamanho certo");
        const ColumnInfo& date = rs->column(2).info();
        check(date.kind == DataKind::date && date.type_name == "date" &&
                  rs->text(0, 2) == "2000-03-16",
              "date: e' date (nao texto), no formato ISO");
        check(!rs->column(3).info().has_source(), "expressao nao tem tabela de origem");
    }
}

} // namespace

int main(int argc, char** argv) {
    ConnConfig config;
    config.host      = argc >= 2 ? argv[1] : "localhost";
    config.port      = argc >= 3 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 2638;
    config.user      = "DBA";
    config.driver_id = "sqlanywhere";
    if (const char* password = std::getenv("OTTER_SA_PASSWORD")) {
        config.password = password;
    } else {
        config.password = "sql";
    }

    // Este teste cria e apaga um banco: so' num servidor local.
    if (config.host != "localhost" && config.host != "127.0.0.1") {
        std::fprintf(stderr, "recusado: so' roda contra localhost (host = %s)\n",
                     config.host.c_str());
        return 2;
    }

    auto master = sqlanywhere_driver().connect(config);
    if (!master) {
        std::fprintf(stderr, "conexao falhou: %s\n", master.error().to_string().c_str());
        return 2;
    }
    g_holt = master->get();
    set_sql_dialect(QuoteStyle::anywhere);
    const std::string home_database = g_holt->current_database();
    std::printf("Servidor: SQL Anywhere %s | banco %s | usuario %s\n",
                g_holt->server_version().c_str(), home_database.c_str(),
                g_holt->current_schema().c_str());

    // --- Conexao --------------------------------------------------------------------
    std::printf("\nConexao\n");
    equal(scalar("SELECT connection_property('CommProtocol')"), "TDS", "o protocolo e' TDS");
    equal(scalar("SELECT connection_property('quoted_identifier')"), "On",
          "aspas duplas delimitam nome");
    equal(scalar("SELECT \"user_name\" FROM SYS.SYSUSER WHERE user_name = 'SYS'"), "SYS",
          "\"coluna\" entre aspas e' coluna, nao texto");
    {
        ConnConfig wrong = config;
        wrong.password = "senha errada";
        auto refused = sqlanywhere_driver().connect(wrong);
        check(!refused && refused.error().code() == otter::Errc::auth_failed,
              "senha errada e' recusada como falha de autenticacao");

        ConnConfig missing = config;
        missing.database = "banco_que_nao_existe";
        auto other = sqlanywhere_driver().connect(missing);
        check(!other && contains(other.error().message(), "banco_que_nao_existe"),
              "banco inexistente e' recusado (o servidor cairia no padrao)");

        ConnConfig named_database = config;
        named_database.database = home_database;
        check(static_cast<bool>(sqlanywhere_driver().connect(named_database)),
              "o banco pelo nome conecta");

        ConnConfig encrypted = config;
        encrypted.ssl_mode = SslMode::require;
        auto no_tls = sqlanywhere_driver().connect(encrypted);
        check(!no_tls && no_tls.error().code() == otter::Errc::not_supported,
              "SSL pedido e' recusado com o motivo (o TDS do servidor nao cifra)");

        // As propriedades do driver viram opcoes TEMPORARIAS da conexao.
        ConnConfig with_option = config;
        with_option.driver_properties["blocking"] = "Off";
        auto tuned = sqlanywhere_driver().connect(with_option);
        if (tuned) {
            auto value = (*tuned)->query("SELECT connection_property('blocking')");
            check(value && value->row_count() == 1 && value->text(0, 0) == "Off",
                  "propriedade do driver aplicada como SET TEMPORARY OPTION");
        } else {
            check(false, "conectar com propriedade do driver -- " + tuned.error().to_string());
        }
        equal(scalar("SELECT connection_property('blocking')"), "On",
              "a opcao e' so' daquela conexao: a desta continua no padrao");

        ConnConfig bad_option = config;
        bad_option.driver_properties["opcao_que_nao_existe"] = "1";
        check(!sqlanywhere_driver().connect(bad_option),
              "propriedade que o servidor nao conhece falha a conexao");
    }

    // --- Valores -------------------------------------------------------------------------
    std::printf("\nValores\n");
    {
        auto rs = g_holt->query(
            "SELECT CAST(200 AS tinyint), CAST(-2 AS smallint), CAST(-4 AS bigint), "
            "CAST(18000000000000000000 AS unsigned bigint), CAST(1 AS bit), "
            "CAST(1.5 AS real), CAST(2.25 AS double), CAST(12.3456 AS money), "
            "CAST(-99999999999999999999.99 AS numeric(38,2)), "
            "CAST('0001-01-01' AS date), CAST('13:45:30.123456' AS time), "
            "CAST('1500-02-03 04:05:06.5' AS timestamp), "
            "CAST('2026-10-01 13:45:00' AS timestamp), "
            "CAST(0x0102FF AS varbinary(8)), CAST(NULL AS integer), "
            "CAST('6f9619ff-8b86-d011-b42d-00c04fc964ff' AS uniqueidentifier), "
            "UNISTR('a\\u00e7\\u00e3o \\u65e5\\u672c')");
        check(static_cast<bool>(rs), "a consulta de tipos" +
                                         (rs ? std::string{} : "  -- " + rs.error().to_string()));
        if (rs && rs->row_count() == 1) {
            const char* wanted[] = {"200", "-2", "-4", "18000000000000000000", "1", "1.5",
                                    "2.25", "12.3456", "-99999999999999999999.99",
                                    "0001-01-01", "13:45:30.123456", "1500-02-03 04:05:06.5",
                                    "2026-10-01 13:45:00", "0x0102FF", "",
                                    "6f9619ff-8b86-d011-b42d-00c04fc964ff", "ação 日本"};
            const char* label[] = {"tinyint sem sinal", "smallint", "bigint", "unsigned bigint",
                                   "bit", "real", "double", "money", "numeric(38,2)",
                                   "date antes de 1753 (o TDS a cortaria)", "time com microssegundos",
                                   "timestamp antigo, fracao sem zeros a' direita",
                                   "timestamp sem fracao", "varbinary", "NULL",
                                   "uniqueidentifier", "nvarchar fora do cp1252"};
            for (std::size_t c = 0; c < 17 && c < rs->column_count(); ++c) {
                if (c == 14) {
                    check(rs->is_null(0, c), label[c]);
                } else {
                    equal(std::string(rs->text(0, c)), wanted[c], label[c]);
                }
            }
            check(rs->column(9).info().kind == DataKind::date &&
                      rs->column(10).info().kind == DataKind::time &&
                      rs->column(11).info().kind == DataKind::timestamp &&
                      rs->column(15).info().kind == DataKind::uuid &&
                      rs->column(8).info().type_name == "numeric(38,2)",
                  "o tipo de cada coluna vem da descricao do servidor");
        }

        // O literal gerado para texto fora do ASCII: UNISTR.
        equal(scalar("SELECT " + quote_literal("ação 日本 😀 a\\b 'x'")),
              "ação 日本 😀 a\\b 'x'", "quote_literal: UNISTR ida e volta, com barra e aspa");
        equal(scalar("SELECT " + quote_literal("c:\\new 'x'")), "c:\\new 'x'",
              "literal ASCII: a barra invertida nao e' escape");

        auto many = g_holt->query("SELECT 1 AS um; SELECT 2 AS dois, 3 AS tres");
        check(many && many->column_count() == 1 && many->text(0, 0) == "1",
              "lote com dois SELECT: a grade recebe o primeiro");
        sql("MESSAGE 'ola do servidor' TO CLIENT", "MESSAGE ... TO CLIENT");
        const std::vector<std::string> output = g_holt->take_server_output();
        check(!output.empty() && output.back() == "ola do servidor",
              "a mensagem do servidor vai para o painel de saida");

        auto failed = g_holt->query("SELECT * FROM tabela_que_nao_existe");
        check(!failed && contains(failed.error().message(), "tabela_que_nao_existe"),
              "erro do servidor traz a mensagem dele");
        check(static_cast<bool>(g_holt->query("SELECT 1")), "a conexao segue viva depois do erro");
    }

    SqlAnywhereCatalog home(*g_holt);
    if (home_database == "demo") {
        read_demo(home);
    } else {
        std::printf("\n(o banco da conexao nao e' o demo: a parte de leitura foi pulada)\n");
    }

    // --- Parte 2: o banco de rascunho ---------------------------------------------------
    std::printf("\nBanco de rascunho\n");
    std::string file = std::getenv("TEMP") != nullptr ? std::getenv("TEMP") : ".";
    for (char& c : file) {
        if (c == '\\') c = '/';
    }
    file += "/otter_scratch_sa.db";
    const std::string file_literal = sqlanywhere_literal(file);

    // Restos de uma execucao interrompida.
    (void)g_holt->execute(std::string("STOP DATABASE ") + kDb + " UNCONDITIONALLY");
    (void)g_holt->execute("DROP DATABASE " + file_literal);

    sql("CREATE DATABASE " + file_literal + " DBA USER 'DBA' DBA PASSWORD 'otter_scratch'",
        "CREATE DATABASE na pasta temporaria");
    (void)g_holt->take_server_output();
    sql("START DATABASE " + file_literal + " AS " + kDb + " AUTOSTOP OFF", "START DATABASE");

    ConnConfig scratch_config   = config;
    scratch_config.database     = kDb;
    scratch_config.password     = "otter_scratch";
    auto scratch = sqlanywhere_driver().connect(scratch_config);
    if (!scratch) {
        std::fprintf(stderr, "conexao ao banco de rascunho falhou: %s\n",
                     scratch.error().to_string().c_str());
        (void)g_holt->execute(std::string("STOP DATABASE ") + kDb + " UNCONDITIONALLY");
        (void)g_holt->execute("DROP DATABASE " + file_literal);
        return 2;
    }
    Holt* const master_holt = g_holt;
    g_holt = scratch->get();
    SqlAnywhereCatalog catalog(*g_holt);
    equal(g_holt->current_database(), kDb, "a segunda sessao nasce no banco pedido");

    // --- Usuarios, papeis, tabela ---------------------------------------------------------
    std::printf("\nUsuario (schema) e tabela\n");
    {
        SqlAnywhereNewUser owner;
        owner.name = "vendas";
        const AlterScript create_owner = sqlanywhere_create_user(owner);
        check(!create_owner.warnings.empty(), "usuario sem senha: o aviso diz que ele nao entra");
        runs(create_owner, "CREATE USER (o dono dos objetos)");

        std::vector<NewColumn> columns;
        NewColumn id;
        id.name = "id"; id.type_name = "integer"; id.nullable = false;
        id.default_value = "AUTOINCREMENT";
        NewColumn nome;
        nome.name = "Nome Completo"; nome.type_name = "nvarchar(100)"; nome.nullable = false;
        nome.comment = "nome do cliente";
        NewColumn ativo;
        ativo.name = "ativo"; ativo.type_name = "bit"; ativo.nullable = false;
        ativo.default_value = "1";
        NewColumn credito;
        credito.name = "credito"; credito.type_name = "numeric(12,2)";
        NewColumn foto;
        foto.name = "foto"; foto.type_name = "long binary";
        NewColumn nascimento;
        nascimento.name = "nascimento"; nascimento.type_name = "date";
        columns = {id, nome, ativo, credito, foto, nascimento};
        runs(generate_create_table("vendas", "cliente", columns, {"id"}, "cadastro de clientes"),
             "CREATE TABLE com PK, autoincrement, default e comentarios");
    }
    {
        auto schemas = catalog.load_schemas();
        check(schemas && named(*schemas, "vendas") != nullptr,
              "load_schemas: o dono novo aparece (tem tabela)");
        auto tables = catalog.load_tables("vendas");
        check(tables && tables->size() == 1 && tables->front().name == "cliente" &&
                  tables->front().comment == "cadastro de clientes",
              "load_tables: a tabela e o comentario");

        const TableMeta table = loaded_table(catalog, "vendas", "cliente");
        check(table.columns.size() == 6, "load_columns: seis colunas");
        const ColumnMeta* nome = column_of(table, "Nome Completo");
        check(nome != nullptr && nome->type_name == "nvarchar(100)" && !nome->nullable &&
                  nome->comment == "nome do cliente",
              "coluna com espaco no nome: tipo, nulidade e comentario");
        const ColumnMeta* id = column_of(table, "id");
        check(id != nullptr && id->primary_key && id->default_value == "autoincrement",
              "a coluna autoincrement e' a chave");
        const ColumnMeta* credito = column_of(table, "credito");
        check(credito != nullptr && credito->nullable,
              "coluna sem NOT NULL aceita NULL (NULL explicito no CREATE)");
        check(!table.constraints.empty() &&
                  table.constraints.front().kind == ObjKind::primary_key,
              "load_constraints: a chave primaria");
    }

    // --- ALTER TABLE ------------------------------------------------------------------------
    std::printf("\nALTER TABLE\n");
    {
        TableMeta table = loaded_table(catalog, "vendas", "cliente");
        table.estimated_rows = 0;

        TableAlteration add;
        add.schema = "vendas";
        add.table  = "cliente";
        NewColumn email;
        email.name = "email"; email.type_name = "varchar(120)"; email.comment = "contato";
        NewColumn pontos;
        pontos.name = "pontos"; pontos.type_name = "integer"; pontos.nullable = false;
        pontos.default_value = "0";
        add.add_columns = {email, pontos};
        runs(generate_alter(table, add), "ADD de duas colunas (sem a palavra COLUMN)");

        table = loaded_table(catalog, "vendas", "cliente");
        const ColumnMeta* pontos_now = column_of(table, "pontos");
        check(table.columns.size() == 8 && pontos_now != nullptr &&
                  pontos_now->default_value == "0" && !pontos_now->nullable,
              "as duas colunas existem; o default da nova e' lido");

        TableAlteration change;
        change.schema = "vendas";
        change.table  = "cliente";
        ColumnChange email_change;
        email_change.name      = "email";
        email_change.new_name  = "e-mail";
        email_change.type_name = "varchar(200)";
        email_change.nullable  = false;
        email_change.comment   = "endereco de contato";
        ColumnChange pontos_change;
        pontos_change.name          = "pontos";
        pontos_change.default_value = "10";
        change.alter_columns = {email_change, pontos_change};
        runs(generate_alter(table, change),
             "renomeia, troca tipo e nulidade, troca o default, comenta");

        table = loaded_table(catalog, "vendas", "cliente");
        const ColumnMeta* renamed = column_of(table, "e-mail");
        check(renamed != nullptr && renamed->type_name == "varchar(200)" &&
                  !renamed->nullable && renamed->comment == "endereco de contato",
              "a coluna renomeada tem o tipo, a nulidade e o comentario novos");
        const ColumnMeta* pontos_after = column_of(table, "pontos");
        check(pontos_after != nullptr && pontos_after->default_value == "10",
              "o default foi trocado");

        TableAlteration drop_default;
        drop_default.schema = "vendas";
        drop_default.table  = "cliente";
        ColumnChange none;
        none.name          = "pontos";
        none.default_value = std::string{};
        drop_default.alter_columns = {none};
        runs(generate_alter(table, drop_default), "DROP DEFAULT");
        table = loaded_table(catalog, "vendas", "cliente");
        check(column_of(table, "pontos") != nullptr &&
                  column_of(table, "pontos")->default_value.empty(),
              "a coluna ficou sem default");

        TableAlteration drop;
        drop.schema       = "vendas";
        drop.table        = "cliente";
        drop.drop_columns = {"ativo"};
        const AlterScript drop_script = generate_alter(table, drop);
        check(drop_script.has_destructive(), "DROP de coluna e' marcado como destrutivo");
        runs(drop_script, "DROP de coluna (sem a palavra COLUMN)");
        table = loaded_table(catalog, "vendas", "cliente");
        check(column_of(table, "ativo") == nullptr, "a coluna saiu");
        sql("ALTER TABLE vendas.cliente ADD ativo bit NOT NULL DEFAULT 1", "(repoe a coluna)");

        TableAlteration comment;
        comment.schema  = "vendas";
        comment.table   = "cliente";
        comment.comment = "clientes da loja";
        runs(generate_alter(table, comment), "COMMENT ON TABLE");
        equal(scalar("SELECT r.remarks FROM SYS.SYSTAB t JOIN SYS.SYSREMARK r "
                     "ON r.object_id = t.object_id WHERE t.table_name = 'cliente'"),
              "clientes da loja", "o comentario novo esta' no catalogo");
    }

    // --- Indices, constraints, chave estrangeira -----------------------------------------
    std::printf("\nIndices e constraints\n");
    {
        NewIndex index;
        index.name    = "ix_cliente_nome";
        index.columns = {"Nome Completo"};
        index.unique  = true;
        runs(generate_create_index("vendas", "cliente", index), "CREATE UNIQUE INDEX");

        auto indexes = catalog.load_indexes("vendas", "cliente");
        const IndexMeta* found = indexes ? named(*indexes, "ix_cliente_nome") : nullptr;
        check(found != nullptr && found->unique && !found->definition.empty(),
              "load_indexes: o indice unico, com o CREATE dele");

        runs(sqlanywhere_object_rename(
                 ref_of(ObjectType::index, "vendas", "ix_cliente_nome", "cliente"),
                 "ix_cliente_nome2"),
             "ALTER INDEX ... RENAME TO");
        runs(sqlanywhere_object_comment(
                 ref_of(ObjectType::index, "vendas", "ix_cliente_nome2", "cliente"), "por nome"),
             "COMMENT ON INDEX");
        runs(generate_drop_index("vendas", "cliente", "ix_cliente_nome2", false),
             "DROP INDEX dono.tabela.indice");

        NewConstraint check_constraint;
        check_constraint.name       = "ck_cliente_credito";
        check_constraint.kind       = ConstraintKind::check;
        check_constraint.expression = "credito >= 0";
        runs(generate_add_constraint("vendas", "cliente", check_constraint), "ADD CHECK");
        runs(sqlanywhere_object_rename(
                 ref_of(ObjectType::constraint, "vendas", "ck_cliente_credito", "cliente"),
                 "ck_credito"),
             "RENAME CONSTRAINT");

        NewConstraint unique;
        unique.name    = "uq_cliente_email";
        unique.kind    = ConstraintKind::unique;
        unique.columns = {"e-mail"};
        runs(generate_add_constraint("vendas", "cliente", unique), "ADD UNIQUE");

        sql("CREATE TABLE vendas.pedido (id integer NOT NULL DEFAULT AUTOINCREMENT PRIMARY KEY, "
            "cliente_id integer NOT NULL, total numeric(12,2) NOT NULL)",
            "(tabela pedido)");
        NewForeignKey key;
        key.name           = "fk_pedido_cliente";
        key.columns        = {"cliente_id"};
        key.target_table   = "cliente";
        key.target_columns = {"id"};
        key.on_delete      = "RESTRICT";
        key.on_update      = "CASCADE";
        runs(generate_add_foreign_key("vendas", "pedido", key),
             "ADD FOREIGN KEY com o nome como papel");

        auto keys = catalog.load_table_foreign_keys("vendas", "pedido");
        check(keys && keys->size() == 1 && keys->front().name == "fk_pedido_cliente" &&
                  keys->front().target_table == "cliente" &&
                  keys->front().source_column == "cliente_id" &&
                  keys->front().on_update == "CASCADE" && keys->front().on_delete == "RESTRICT",
              "load_table_foreign_keys: nome, colunas e acoes");
        auto references = catalog.load_references("vendas", "cliente");
        check(references && references->size() == 1 &&
                  references->front().source_table == "pedido",
              "load_references: quem aponta para cliente");

        const TableMeta table = loaded_table(catalog, "vendas", "cliente");
        bool has_check = false, has_unique = false;
        for (const ConstraintMeta& constraint : table.constraints) {
            has_check  = has_check || constraint.name == "ck_credito";
            has_unique = has_unique || (constraint.name == "uq_cliente_email" &&
                                        constraint.kind == ObjKind::unique_key);
        }
        check(has_check && has_unique, "load_constraints: CHECK renomeada e UNIQUE");

        // A definicao lida precisa servir para recriar a chave.
        const std::string definition = keys && !keys->empty() ? keys->front().definition : "";
        runs(generate_drop_foreign_key("vendas", "pedido", "fk_pedido_cliente"),
             "DROP FOREIGN KEY");
        sql("ALTER TABLE vendas.pedido ADD CONSTRAINT fk_volta " + definition,
            "a definicao lida do catalogo recria a chave");
        runs(sqlanywhere_object_drop(
                 ref_of(ObjectType::constraint, "vendas", "fk_volta", "pedido")),
             "(DROP CONSTRAINT da chave recriada)");
        runs(generate_drop_constraint("vendas", "cliente", "ck_credito",
                                      ObjKind::check_constraint),
             "DROP CONSTRAINT");
        runs(generate_add_foreign_key("vendas", "pedido", key), "(FK de volta)");
    }

    // --- View, rotina, trigger, sequence, dominio, evento ----------------------------------
    std::printf("\nView, rotina, trigger, sequence, dominio, evento\n");
    {
        runs(generate_create_view("vendas", "v_cliente",
                                  "SELECT id, \"Nome Completo\" FROM vendas.cliente", true),
             "CREATE OR REPLACE VIEW");
        const ObjectRef view = ref_of(ObjectType::view, "vendas", "v_cliente");
        ObjectInfo info = load_object_info(catalog, *g_holt, view);
        check(info.error.empty() && contains(info.ddl, "v_cliente"),
              "editor da view: o fonte" + (info.error.empty() ? "" : " -- " + info.error));

        // Gravar o fonte editado: CREATE vira ALTER, e a view muda no lugar.
        std::string edited = info.ddl;
        const std::size_t at = edited.find("\"Nome Completo\"");
        if (at != std::string::npos) edited.insert(at + 15, ", credito");
        runs(sqlanywhere_source_script(view, edited), "fonte editado da view (ALTER VIEW)");
        equal(scalar("SELECT count(*) FROM SYS.SYSTABCOL c JOIN SYS.SYSTAB t "
                     "ON t.table_id = c.table_id WHERE t.table_name = 'v_cliente'"),
              "3", "a view passou a ter tres colunas");
        runs(sqlanywhere_view_enable(view, false), "ALTER VIEW ... DISABLE");
        info = load_object_info(catalog, *g_holt, view);
        check(property(info, "Status") != nullptr && *property(info, "Status") == "disabled",
              "a view aparece desabilitada");
        runs(sqlanywhere_view_enable(view, true), "ALTER VIEW ... ENABLE");

        sql("CREATE PROCEDURE vendas.p_total(IN cliente integer, OUT total numeric(12,2)) "
            "BEGIN SELECT COALESCE(SUM(p.total), 0) INTO total FROM vendas.pedido p "
            "WHERE p.cliente_id = cliente; END",
            "(procedure com parametro OUT)");
        sql("CREATE FUNCTION vendas.f_dobro(IN n integer) RETURNS integer "
            "BEGIN RETURN n * 2; END",
            "(funcao)");
        sql("CREATE PROCEDURE vendas.p_pedidos(IN cliente integer) "
            "RESULT (id integer, total numeric(12,2)) "
            "BEGIN SELECT p.id, p.total FROM vendas.pedido p WHERE p.cliente_id = cliente; END",
            "(procedure com conjunto de resultado)");

        auto routines = catalog.load_routines("vendas");
        check(routines && routines->size() == 3, "load_routines: tres rotinas");
        if (routines) {
            for (const RoutineMeta& routine : *routines) {
                std::string call = routine_call_sql("vendas", routine, false);
                for (const char* marker : {":cliente", ":n"}) {
                    for (std::size_t p = call.find(marker); p != std::string::npos;
                         p = call.find(marker)) {
                        call.replace(p, std::string(marker).size(), "1");
                    }
                }
                auto rs = g_holt->query(call);
                check(static_cast<bool>(rs),
                      "chamada gerada de " + routine.name +
                          (rs ? "" : "  -- " + rs.error().to_string() + "  <<  " + call));
                if (rs && routine.name == "f_dobro") {
                    check(routine.kind == ObjKind::function && routine.return_type == "integer",
                          "f_dobro e' funcao, devolve integer");
                    equal(std::string(rs->text(0, 0)), "2", "a funcao devolveu 2");
                }
                if (rs && routine.name == "p_total") {
                    check(rs->row_count() == 1 && rs->column_count() == 1,
                          "o parametro OUT volta como coluna");
                }
            }
        }

        const ObjectRef procedure = ref_of(ObjectType::procedure, "vendas", "p_total");
        info = load_object_info(catalog, *g_holt, procedure);
        check(info.error.empty() && contains(info.ddl, "p_total"), "editor da procedure: o fonte");
        runs(sqlanywhere_source_script(procedure, info.ddl),
             "regravar a procedure (ALTER PROCEDURE)");
        runs(sqlanywhere_object_comment(procedure, "soma os pedidos"), "comentario da procedure");
        info = load_object_info(catalog, *g_holt, procedure);
        const std::string* comment = property(info, "Comment");
        check(comment != nullptr && *comment == "soma os pedidos", "o comentario e' lido de volta");
        runs(sqlanywhere_object_comment(procedure, ""), "remocao do comentario (IS NULL)");
        runs(sqlanywhere_object_comment(ref_of(ObjectType::function, "vendas", "f_dobro"),
                                        "o dobro"),
             "comentario de funcao (COMMENT ON PROCEDURE)");

        NewTrigger trigger;
        trigger.name   = "trg_pedido";
        trigger.table  = "pedido";
        trigger.timing = "BEFORE";
        trigger.event  = "INSERT";
        trigger.body   = "SET new_row.total = ABS(new_row.total)";
        runs(generate_create_trigger("vendas", trigger),
             "CREATE TRIGGER: o corpo de uma linha ganha BEGIN/END e REFERENCING");
        auto triggers = catalog.load_triggers("vendas", "pedido");
        check(triggers && triggers->size() == 1 && triggers->front().timing == "BEFORE" &&
                  triggers->front().events == "INSERT",
              "load_triggers");
        const ObjectRef trigger_ref = ref_of(ObjectType::trigger, "vendas", "trg_pedido", "pedido");
        info = load_object_info(catalog, *g_holt, trigger_ref);
        check(info.error.empty() && contains(info.ddl, "trg_pedido"), "editor do trigger");
        runs(sqlanywhere_object_comment(trigger_ref, "total sempre positivo"),
             "COMMENT ON TRIGGER");

        NewSequence sequence;
        sequence.name      = "seq_nota";
        sequence.start     = 100;
        sequence.increment = 5;
        runs(generate_create_sequence("vendas", sequence), "CREATE SEQUENCE");
        auto sequences = catalog.load_sequences("vendas");
        check(sequences && sequences->size() == 1 && sequences->front().name == "seq_nota" &&
                  sequences->front().start_value == 100 && sequences->front().increment == 5,
              "load_sequences");
        const ObjectRef sequence_ref = ref_of(ObjectType::sequence, "vendas", "seq_nota");
        info = load_object_info(catalog, *g_holt, sequence_ref);
        check(info.error.empty() && contains(info.ddl, "START WITH 100"), "editor da sequence");
        SqlAnywhereNewSequence second;
        second.schema = "vendas"; second.name = "seq_b"; second.start = "1"; second.cycle = true;
        second.maximum = "9";
        runs(sqlanywhere_create_sequence(second), "CREATE SEQUENCE pelo formulario proprio");
        second.maximum = "9; DROP";
        check(!sqlanywhere_create_sequence(second).ok(), "limite que nao e' numero e' recusado");

        runs(sqlanywhere_create_domain("cep", "varchar(9)", true, "", "@col LIKE '_____-___'"),
             "CREATE DOMAIN");
        auto types = catalog.load_types("DBA");
        const DataTypeMeta* cep = types ? named(*types, "cep") : nullptr;
        check(cep != nullptr && cep->base_type == "varchar(9)" && cep->not_null &&
                  !cep->check_constraint.empty(),
              "load_types: o dominio, o tipo base e a condicao");

        sql("CREATE EVENT otter_evento HANDLER BEGIN MESSAGE 'evento' TO CONSOLE; END",
            "(evento manual)");
        auto events = catalog.load_events("DBA");
        const EventMeta* event = events ? named(*events, "otter_evento") : nullptr;
        check(event != nullptr && event->status == "ENABLED" && event->type == "MANUAL",
              "load_events" +
                  (events ? std::string{} : "  -- " + events.error().to_string()));
        runs(sqlanywhere_event_enable("otter_evento", false), "ALTER EVENT ... DISABLE");
        events = catalog.load_events("DBA");
        event  = events ? named(*events, "otter_evento") : nullptr;
        check(event != nullptr && event->status == "DISABLED", "o evento aparece desligado");
        runs(sqlanywhere_event_enable("otter_evento", true), "ALTER EVENT ... ENABLE");
        runs(sqlanywhere_event_trigger("otter_evento"), "TRIGGER EVENT");
        const ObjectRef event_ref = ref_of(ObjectType::event, "DBA", "otter_evento");
        info = load_object_info(catalog, *g_holt, event_ref);
        check(info.error.empty() && contains(info.ddl, "CREATE EVENT \"otter_evento\"") &&
                  contains(info.ddl, "HANDLER"),
              "editor do evento: o CREATE EVENT montado" +
                  (info.error.empty() ? "" : " -- " + info.error));
        runs(sqlanywhere_source_script(event_ref, info.ddl),
             "regravar o evento (ALTER EVENT ... HANDLER)");

        // Um evento com agenda: o comando montado tem de recria-lo.
        sql("CREATE EVENT otter_agenda SCHEDULE diario BETWEEN '08:00' AND '18:00' "
            "EVERY 2 HOURS ON ('Monday', 'Friday') START DATE '2030-01-01' DISABLE "
            "HANDLER BEGIN MESSAGE 'agenda' TO CONSOLE; END",
            "(evento com agenda)");
        events = catalog.load_events("DBA");
        const EventMeta* scheduled = events ? named(*events, "otter_agenda") : nullptr;
        check(scheduled != nullptr && scheduled->type == "RECURRING" &&
                  scheduled->status == "DISABLED" && contains(scheduled->schedule, "diario"),
              "load_events: a agenda, recorrente e desligada");
        auto event_ddl = catalog.load_event_definition("otter_agenda");
        check(event_ddl && contains(*event_ddl, "BETWEEN '08:00:00' AND '18:00:00'") &&
                  contains(*event_ddl, "EVERY 2 HOURS") &&
                  contains(*event_ddl, "ON ('Monday', 'Friday')") &&
                  contains(*event_ddl, "START DATE '2030-01-01'") &&
                  contains(*event_ddl, "DISABLE"),
              "o CREATE EVENT montado traz a agenda" +
                  (event_ddl ? "\n" + *event_ddl : "  -- " + event_ddl.error().to_string()));
        sql("DROP EVENT otter_agenda", "(apaga)");
        if (event_ddl) sql(*event_ddl, "o comando montado recria o evento");
        sql("DROP EVENT otter_agenda", "(apaga de novo)");

        auto dependencies = catalog.load_list(CatalogList::dependencies, "vendas", "v_cliente", {});
        check(dependencies && dependencies->size() == 1 &&
                  dependencies->front().name == "vendas.cliente",
              "dependencias da view");
        auto schema_indexes = catalog.load_list(CatalogList::schema_indexes, "vendas", {}, {});
        check(schema_indexes && schema_indexes->size() >= 3, "indices do dono");
        auto dbspaces = catalog.load_list(CatalogList::tablespaces, {}, {}, {});
        check(dbspaces && named(*dbspaces, "system") != nullptr, "dbspaces");
    }

    // --- Editor de objeto e permissoes --------------------------------------------------------
    std::printf("\nEditor de objeto e permissoes\n");
    {
        const ObjectRef table = ref_of(ObjectType::table, "vendas", "cliente");
        ObjectInfo info = load_object_info(catalog, *g_holt, table);
        check(info.error.empty() && contains(info.ddl, "CREATE TABLE \"vendas\".\"cliente\"") &&
                  contains(info.ddl, "\"Nome Completo\""),
              "DDL da tabela, do servidor" + (info.error.empty() ? "" : " -- " + info.error));

        SqlAnywhereNewUser user;
        user.name     = kUser;
        user.password = kPassword;
        runs(sqlanywhere_create_user(user), "CREATE USER com senha");
        runs(sqlanywhere_user_password(kUser, "Outra#Senha'7y"), "ALTER USER ... IDENTIFIED BY");
        runs(sqlanywhere_create_role("otter_papel"), "CREATE ROLE");
        sql("GRANT ROLE otter_papel TO otter_user", "(concede o papel)");

        auto roles = catalog.load_list(CatalogList::roles, {}, {}, {});
        const CatalogItem* user_item = roles ? named(*roles, kUser) : nullptr;
        const CatalogItem* role_item = roles ? named(*roles, "otter_papel") : nullptr;
        check(user_item != nullptr && user_item->flag && role_item != nullptr &&
                  !role_item->flag,
              "usuarios e papeis: o usuario entra, o papel nao");
        auto belongs = catalog.load_list(CatalogList::role_belongs, kUser, {}, {});
        check(belongs && named(*belongs, "otter_papel") != nullptr, "papeis do usuario");

        {
            ConnConfig as_user = scratch_config;
            as_user.user       = kUser;
            as_user.password   = "Outra#Senha'7y";
            auto session = sqlanywhere_driver().connect(as_user);
            check(static_cast<bool>(session),
                  "login do usuario novo, com a senha trocada (aspa dentro)" +
                      (session ? std::string{} : "  -- " + session.error().to_string()));
            if (session) {
                equal((*session)->current_schema(), kUser, "a sessao e' do usuario novo");
                auto denied = (*session)->query("SELECT count(*) FROM vendas.cliente");
                check(!denied, "sem GRANT, o usuario nao le' a tabela");
            }
        }

        runs(sqlanywhere_grant(table, "SELECT", kUser, true), "GRANT ... WITH GRANT OPTION");
        runs(sqlanywhere_grant(table, "UPDATE", "otter_papel", false), "GRANT UPDATE ao papel");
        info = load_object_info(catalog, *g_holt, table);
        bool select_grantable = false, update = false;
        for (const ObjectPermission& permission : info.permissions) {
            if (permission.grantee == kUser && permission.privilege == "SELECT") {
                select_grantable = permission.grantable;
            }
            if (permission.grantee == "otter_papel" && permission.privilege == "UPDATE") {
                update = true;
            }
        }
        check(select_grantable && update, "as permissoes sao lidas, com a opcao de repasse");

        auto grants = catalog.load_grants(kUser);
        bool role_grant = false, table_grant = false;
        if (grants) {
            for (const std::string& grant : *grants) {
                role_grant  = role_grant || contains(grant, "GRANT ROLE \"otter_papel\"");
                table_grant = table_grant || (contains(grant, "GRANT SELECT ON \"vendas\".\"cliente\"") &&
                                              contains(grant, "WITH GRANT OPTION"));
            }
        }
        check(role_grant && table_grant, "load_grants: papel e permissao de tabela");

        runs(sqlanywhere_revoke(table, "UPDATE", "otter_papel"), "REVOKE");
        info = load_object_info(catalog, *g_holt, table);
        bool still_update = false;
        for (const ObjectPermission& permission : info.permissions) {
            still_update = still_update || permission.privilege == "UPDATE";
        }
        check(!still_update, "UPDATE saiu da lista");

        const ObjectRef routine = ref_of(ObjectType::procedure, "vendas", "p_pedidos");
        runs(sqlanywhere_grant(routine, "EXECUTE", kUser, false), "GRANT EXECUTE");
        check(!sqlanywhere_grant(routine, "EXECUTE", kUser, true).ok(),
              "WITH GRANT OPTION em rotina e' recusado com o motivo");
        info = load_object_info(catalog, *g_holt, routine);
        check(info.permissions.size() == 1 && info.permissions.front().grantee == kUser,
              "a permissao da rotina e' lida");
        const ObjectRef sequence = ref_of(ObjectType::sequence, "vendas", "seq_nota");
        runs(sqlanywhere_grant(sequence, "USAGE", kUser, false), "GRANT USAGE ON SEQUENCE");
        check(!sqlanywhere_grant(sequence, "SELECT", kUser, false).ok(),
              "privilegio que a sequence nao tem e' recusado");

        // Numa tabela sem acao referencial: o servidor recusa renomear a que
        // tem uma chave estrangeira com ON UPDATE / ON DELETE.
        sql("CREATE TABLE vendas.rascunho (a integer NULL)", "(tabela para renomear)");
        runs(sqlanywhere_object_rename(ref_of(ObjectType::table, "vendas", "rascunho"),
                                       "rascunho2"),
             "ALTER TABLE ... RENAME");
        runs(sqlanywhere_object_rename(
                 ref_of(ObjectType::column, "vendas", "a", "rascunho2"), "b"),
             "ALTER TABLE ... RENAME coluna TO");
        equal(scalar("SELECT c.column_name FROM SYS.SYSTABCOL c JOIN SYS.SYSTAB t "
                     "ON t.table_id = c.table_id WHERE t.table_name = 'rascunho2'"),
              "b", "a tabela e a coluna tem os nomes novos");
        runs(generate_drop("vendas", "rascunho2", ObjKind::table, false), "(DROP da tabela)");
        check(!sqlanywhere_object_rename(ref_of(ObjectType::view, "vendas", "v_cliente"), "x").ok(),
              "renomear view e' recusado com o motivo");
    }

    // --- Grade: gravar edicoes --------------------------------------------------------------
    std::printf("\nGrade\n");
    {
        sql("INSERT INTO vendas.cliente (\"Nome Completo\", \"e-mail\", credito, pontos) VALUES "
            "('Ana', 'ana@x', 10.50, 1), ('Bia', 'bia@x', 20, 2), ('Caio', 'caio@x', NULL, 3)",
            "(tres clientes)");

        auto rs = g_holt->query("SELECT * FROM vendas.cliente ORDER BY id");
        check(rs && rs->row_count() == 3, "a consulta da grade");
        if (rs) {
            check(rs->column(1).info().source_table == "cliente" &&
                      rs->column(1).info().source_schema == "vendas",
                  "o resultado diz de que tabela cada coluna veio");

            SchemaMeta schema;
            schema.name = "vendas";
            schema.tables.push_back(loaded_table(catalog, "vendas", "cliente"));
            const EditTarget target = find_edit_target(*rs, {schema});
            check(target.editable(),
                  "a grade e' editavel" +
                      (target.editable() ? std::string{}
                                         : "  -- " + std::string(to_string(target.refusal))));

            std::size_t c_nome = 0, c_credito = 0, c_ativo = 0, c_foto = 0, c_email = 0,
                        c_pontos = 0, c_nascimento = 0;
            for (std::size_t c = 0; c < rs->column_count(); ++c) {
                const std::string& name = rs->column(c).info().name;
                if (name == "Nome Completo") c_nome = c;
                if (name == "credito") c_credito = c;
                if (name == "ativo") c_ativo = c;
                if (name == "foto") c_foto = c;
                if (name == "e-mail") c_email = c;
                if (name == "pontos") c_pontos = c;
                if (name == "nascimento") c_nascimento = c;
            }

            EditBuffer buffer;
            buffer.set(0, c_nome, "Ana Júlia D'Ávila 日本");   // fora do cp1252: UNISTR
            buffer.set(0, c_ativo, "false");                 // bit: 0, nao FALSE
            buffer.set(0, c_foto, "0xCAFE01");               // binario: literal 0x cru
            buffer.set(0, c_nascimento, "0990-05-17");       // antes de 1753
            buffer.set_null(1, c_credito);
            buffer.mark_deleted(2);
            const std::size_t fresh = buffer.add_row();
            buffer.set_new_value(fresh, c_nome, "Davi");
            buffer.set_new_value(fresh, c_email, "davi@x");
            buffer.set_new_value(fresh, c_credito, "7.25");
            buffer.set_new_value(fresh, c_pontos, "4");
            buffer.set_new_value(fresh, c_ativo, "true");

            auto changes = generate_changes(*rs, target, buffer);
            check(static_cast<bool>(changes),
                  "os comandos da grade sao gerados" +
                      (changes ? std::string{} : "  -- " + changes.error().to_string()));
            if (changes) {
                AlterScript script;
                script.statements.emplace_back(transaction_begin_sql());
                for (std::string& statement : *changes) {
                    script.statements.push_back(std::move(statement));
                }
                script.statements.emplace_back(transaction_commit_sql());
                runs(script, "INSERT, UPDATE e DELETE dentro de BEGIN TRANSACTION / COMMIT");
            }
            equal(scalar("SELECT \"Nome Completo\" FROM vendas.cliente WHERE id = 1"),
                  "Ana Júlia D'Ávila 日本", "o texto Unicode chegou inteiro (nvarchar)");
            equal(scalar("SELECT CAST(ativo AS integer) FROM vendas.cliente WHERE id = 1"), "0",
                  "o bit virou 0");
            equal(scalar("SELECT foto FROM vendas.cliente WHERE id = 1"), "0xCAFE01",
                  "o binario foi gravado");
            equal(scalar("SELECT nascimento FROM vendas.cliente WHERE id = 1"), "0990-05-17",
                  "a data antes de 1753 foi gravada e lida inteira");
            equal(scalar("SELECT count(*) FROM vendas.cliente WHERE id = 2 AND credito IS NULL"),
                  "1", "o NULL foi gravado");
            equal(scalar("SELECT count(*) FROM vendas.cliente WHERE id = 3"), "0",
                  "a linha excluida saiu");
            equal(scalar("SELECT credito FROM vendas.cliente WHERE \"Nome Completo\" = 'Davi'"),
                  "7.25", "a linha nova entrou");
        }
    }

    // --- Paginacao ---------------------------------------------------------------------------
    std::printf("\nPaginacao (TOP / START AT)\n");
    {
        sql("CREATE TABLE DBA.numeros (n integer NOT NULL PRIMARY KEY, grupo integer NOT NULL, "
            "texto long varchar NULL)",
            "(tabela de numeros)");
        sql("INSERT INTO DBA.numeros (n, grupo) SELECT row_num, MOD(row_num, 5) "
            "FROM sa_rowgenerator(1, 50)",
            "(50 linhas)");

        const otter::sql::Dialect& dialect = otter::sql::dialect_for("sqlanywhere");
        const auto page = [&](const char* text, std::size_t number, std::size_t size,
                              const otter::sql::SortOrder& sort = {},
                              const otter::sql::ColumnFilter& filter = {}) {
            return otter::sql::make_paged_query(text, dialect, number, size, sort, filter);
        };
        const auto rows = [&](const otter::sql::PagedQuery& paged, const char* what) {
            auto rs = g_holt->query(paged.sql);
            check(static_cast<bool>(rs),
                  std::string(what) +
                      (rs ? "" : "  -- " + rs.error().to_string() + "\n" + paged.sql));
            return rs ? std::optional<ResultSet>(std::move(*rs)) : std::nullopt;
        };

        if (auto rs = rows(page("SELECT * FROM numeros", 0, 10), "sem ORDER BY: pagina 1")) {
            check(rs->row_count() == 11, "veio a pagina e a linha-sonda (11)");
            check(rs->column(0).info().source_table == "numeros",
                  "a consulta paginada ainda diz a tabela de origem");
        }
        if (auto rs = rows(page("SELECT n FROM numeros ORDER BY n -- fim", 2, 10),
                           "com ORDER BY e comentario no fim: pagina 3")) {
            check(rs->row_count() == 11 && rs->text(0, 0) == "21", "comeca na linha 21");
        }
        if (auto rs = rows(page("SELECT n FROM numeros ORDER BY n;", 4, 10), "ultima pagina")) {
            check(rs->row_count() == 10, "a ultima pagina nao tem a sonda");
        }
        if (auto rs = rows(page("SELECT DISTINCT grupo FROM numeros ORDER BY grupo", 0, 3),
                           "DISTINCT")) {
            check(rs->row_count() == 4 && rs->text(0, 0) == "0",
                  "TOP entra depois de DISTINCT");
        }
        if (auto rs = rows(page("SELECT n FROM numeros WHERE n <= 5 UNION ALL "
                                "SELECT n FROM numeros WHERE n > 45 ORDER BY 1 DESC", 0, 4),
                           "UNION com ORDER BY")) {
            check(rs->row_count() == 5 && rs->text(0, 0) == "50",
                  "UNION: envolvido, e o ORDER BY sai para o SELECT de fora");
        }
        if (auto rs = rows(page("SELECT grupo, COUNT(*) AS total FROM numeros GROUP BY grupo",
                                0, 10),
                           "GROUP BY")) {
            check(rs->row_count() == 5, "GROUP BY pagina");
        }
        if (auto rs = rows(page("WITH x AS (SELECT n FROM numeros) SELECT n FROM x ORDER BY n",
                                1, 20),
                           "CTE")) {
            check(rs->row_count() == 21 && rs->text(0, 0) == "21",
                  "CTE: o TOP entra no SELECT principal");
        }
        if (auto rs = rows(page("// comentario\nSELECT n FROM numeros ORDER BY n", 0, 5),
                           "comentario com //")) {
            check(rs->row_count() == 6, "o lexer conhece o comentario //");
        }

        otter::sql::SortOrder sort;
        sort.column     = "n";
        sort.descending = true;
        if (auto rs = rows(page("SELECT n, grupo FROM numeros", 0, 5, sort),
                           "ordenacao da grade")) {
            check(rs->text(0, 0) == "50", "ordena no servidor: comeca em 50");
        }

        otter::sql::ColumnFilter filter;
        filter.set_column("grupo", "= 3");
        if (auto rs = rows(page("SELECT n, grupo FROM numeros ORDER BY n", 0, 20, sort, filter),
                           "filtro sobre consulta com ORDER BY")) {
            check(rs->row_count() == 10 && rs->text(0, 0) == "48",
                  "filtra (10 linhas do grupo 3) e reordena");
        }

        check(page("SELECT TOP 5 * FROM numeros", 0, 10).refusal ==
                  otter::sql::PagingRefusal::already_limited,
              "TOP ja' e' um limite: nao reescreve");
        check(page("SELECT FIRST * FROM numeros ORDER BY n", 0, 10).refusal ==
                  otter::sql::PagingRefusal::already_limited,
              "FIRST tambem");
        check(page("SELECT * INTO #copia FROM numeros", 0, 10).refusal ==
                  otter::sql::PagingRefusal::not_a_query,
              "SELECT INTO nao e' consulta de grade");
        check(page("CALL sa_conn_info()", 0, 10).refusal ==
                  otter::sql::PagingRefusal::not_a_query,
              "CALL nao e' paginado");

        const otter::sql::PagedQuery count =
            otter::sql::make_count_query("SELECT * FROM numeros ORDER BY n", dialect, filter);
        equal(scalar(count.sql), "10", "contagem com ORDER BY interno e filtro");
        const otter::sql::PagedQuery count_top =
            otter::sql::make_count_query("SELECT TOP 7 * FROM numeros ORDER BY n", dialect, {});
        equal(scalar(count_top.sql), "7", "contagem de consulta com TOP");
        const otter::sql::PagedQuery count_cte = otter::sql::make_count_query(
            "WITH x AS (SELECT n FROM numeros) SELECT n FROM x", dialect, {});
        equal(scalar(count_cte.sql), "50", "contagem de consulta com WITH");

        const otter::sql::PagedQuery whole =
            otter::sql::make_unpaged_query("SELECT n, grupo FROM numeros", dialect, {}, {});
        auto distinct = g_holt->query(distinct_query(whole.sql, "grupo", 3));
        check(distinct && distinct->row_count() == 3,
              "valores distintos (TOP e GROUP BY pelo nome)" +
                  (distinct ? std::string{} : "  -- " + distinct.error().to_string()));

        TableMeta numbers = loaded_table(catalog, "DBA", "numeros");
        auto top = g_holt->query(generate_select("DBA", numbers, 7));
        check(top && top->row_count() == 7, "SELECT gerado pela arvore usa TOP");
        equal(scalar(generate_count("DBA", numbers)), "50", "contagem gerada pela arvore");

        // O divisor de scripts: `go` separa lotes; o corpo BEGIN ... END de
        // uma procedure e' um comando so'.
        const std::vector<otter::sql::Statement> pieces = otter::sql::split_script(
            "CREATE PROCEDURE p_x() BEGIN SELECT 1; SELECT 2; END\ngo\nSELECT 3; SELECT 4",
            dialect);
        check(pieces.size() == 3 && pieces.front().text.find("SELECT 2; END") !=
                                        std::string_view::npos,
              "split_script: a procedure inteira, depois dois comandos");

        // Sem `go`: o comando acaba no END do bloco, e END IF / END LOOP nao
        // sao esse END. Cada pedaco tem de RODAR -- mandado inteiro, o servidor
        // recusava o que vinha depois do END.
        const std::vector<otter::sql::Statement> script = otter::sql::split_script(
            "CREATE PROCEDURE DBA.p_bloco(IN v integer, OUT r integer)\nBEGIN\n"
            "  DECLARE i integer;\n  SET i = 0;\n  SET r = 0;\n"
            "  IF v > 0 THEN\n    SET r = 1;\n  END IF;\n"
            "  WHILE i < v LOOP\n    SET i = i + 1;\n    SET r = r + i;\n  END LOOP;\n"
            "END;\n"
            "CREATE TABLE DBA.t_bloco (a integer NULL);\n"
            "INSERT INTO DBA.t_bloco VALUES (7);",
            dialect);
        check(script.size() == 3, "script com procedure Watcom: tres comandos, sem `go`");
        bool all_ran = script.size() == 3;
        for (const otter::sql::Statement& statement : script) {
            const otter::Status status = g_holt->execute(std::string(statement.text));
            if (!status) {
                all_ran = false;
                std::printf("      -- %s\n", status.error().to_string().c_str());
            }
        }
        check(all_ran, "cada pedaco do script roda");
        auto called = g_holt->query("CALL DBA.p_bloco(3)");
        check(called && called->row_count() == 1 && called->text(0, 0) == "7",
              "a procedure criada pelo script: IF e LOOP dentro do corpo (1 + 1 + 2 + 3)");
        equal(scalar("SELECT a FROM DBA.t_bloco"), "7", "o comando depois da procedure rodou");
        sql("DROP PROCEDURE DBA.p_bloco", "(apaga a procedure)");
        sql("DROP TABLE DBA.t_bloco", "(apaga a tabela)");
    }

    // --- Importacao e CREATE TABLE pelo resultado -------------------------------------------
    std::printf("\nImportacao\n");
    {
        std::string csv = "n,grupo,texto\n";
        for (int i = 0; i < 2500; ++i) {
            csv += std::to_string(1000 + i) + "," + std::to_string(i % 7) + ",linha " +
                   std::to_string(i) + (i == 3 ? " ção" : "") + "\n";
        }
        const CsvTable data = parse_csv(csv, CsvOptions{});
        ImportPlan plan;
        plan.schema         = "DBA";
        plan.table          = "numeros";
        plan.columns        = {"n", "grupo", "texto"};
        plan.truncate_first = true;
        plan.batch_rows     = 1000;
        const ImportScript script = generate_import(data, plan);
        check(script.error.empty() && script.statements.size() == 4,
              "TRUNCATE + 3 lotes de 1000 linhas");

        AlterScript load;
        for (const std::string& statement : script.statements) load.statements.push_back(statement);
        runs(load, "a carga roda");
        equal(scalar("SELECT count(*) FROM numeros"), "2500", "2500 linhas importadas");
        equal(scalar("SELECT texto FROM numeros WHERE n = 1003"), "linha 3 ção",
              "acento preservado na carga");

        auto rs = g_holt->query(
            "SELECT id, \"Nome Completo\", credito, foto, ativo, nascimento FROM vendas.cliente");
        if (rs) {
            const std::string ddl = create_table_from_result(*rs, "do_resultado", false);
            auto status = g_holt->execute(ddl);
            check(static_cast<bool>(status),
                  "CREATE TABLE gerado do resultado" +
                      (status ? std::string{} : "  -- " + status.error().to_string() + "\n" + ddl));
            const TableMeta copy = loaded_table(catalog, "DBA", "do_resultado");
            const ColumnMeta* nome = column_of(copy, "Nome Completo");
            const ColumnMeta* credito = column_of(copy, "credito");
            check(nome != nullptr && nome->type_name == "nvarchar(100)" && !nome->nullable &&
                      credito != nullptr && credito->type_name == "numeric(12,2)" &&
                      credito->nullable,
                  "os tipos e a nulidade do resultado viram os da tabela");
        }
    }

    // --- Transacoes do driver ------------------------------------------------------------------
    std::printf("\nTransacoes\n");
    {
        check(g_holt->auto_commit(), "nasce em auto-commit");
        check(g_holt->txn_state() == TxnState::idle, "sem transacao aberta");
        check(static_cast<bool>(g_holt->set_auto_commit(false)), "modo manual (chained)");
        equal(scalar("SELECT count(*) FROM numeros WHERE n > 9000"), "0", "(uma leitura)");
        check(g_holt->txn_state() == TxnState::idle,
              "so' ler nao abre transacao com alteracao pendente");
        sql("INSERT INTO numeros (n, grupo) VALUES (9001, 1)", "(comando que abre a transacao)");
        check(g_holt->txn_state() == TxnState::active, "a transacao esta' aberta");
        check(g_holt->uncommitted_changes() == 1, "uma alteracao pendente contada");
        check(static_cast<bool>(g_holt->savepoint("sp1")), "SAVEPOINT");
        sql("INSERT INTO numeros (n, grupo) VALUES (9002, 1)", "(insere depois do ponto)");
        check(static_cast<bool>(g_holt->rollback_to("sp1")), "ROLLBACK TO SAVEPOINT");
        check(static_cast<bool>(g_holt->release_savepoint("sp1")),
              "RELEASE depois do ROLLBACK TO: o ponto ja' foi solto, e isso nao e' erro");
        check(static_cast<bool>(g_holt->savepoint("sp2")) &&
                  static_cast<bool>(g_holt->release_savepoint("sp2")),
              "SAVEPOINT e RELEASE SAVEPOINT");
        check(g_holt->txn_state() == TxnState::active,
              "depois do ROLLBACK TO, a primeira insercao continua pendente");
        check(static_cast<bool>(g_holt->commit()), "COMMIT");
        check(g_holt->txn_state() == TxnState::idle, "o COMMIT fecha a transacao");
        equal(scalar("SELECT count(*) FROM numeros WHERE n IN (9001, 9002)"), "1",
              "ficou so' o que veio antes do ponto");
        (void)g_holt->commit();
        sql("DELETE FROM numeros WHERE n = 9001", "(apaga)");
        check(static_cast<bool>(g_holt->rollback()), "ROLLBACK");
        equal(scalar("SELECT count(*) FROM numeros WHERE n = 9001"), "1",
              "o ROLLBACK desfez a exclusao");
        (void)g_holt->rollback();
        check(static_cast<bool>(g_holt->set_auto_commit(true)), "volta ao auto-commit");
        check(g_holt->txn_state() == TxnState::idle, "sem transacao aberta");

        auto level = g_holt->isolation_level();
        check(level && *level == IsolationLevel::read_committed,
              "nivel de isolamento da conexao TDS: read committed");
        check(static_cast<bool>(g_holt->set_isolation_level(IsolationLevel::serializable)),
              "SET isolation_level = 3");
        level = g_holt->isolation_level();
        check(level && *level == IsolationLevel::serializable, "o nivel novo e' lido de volta");
        (void)g_holt->set_isolation_level(IsolationLevel::read_committed);
    }

    // --- Ferramentas, sessoes, DROP ---------------------------------------------------------
    std::printf("\nFerramentas e sessoes\n");
    {
        runs(sqlanywhere_table_tool(SqlAnywhereTableTool::validate, "DBA", "numeros"),
             "VALIDATE TABLE");
        runs(sqlanywhere_table_tool(SqlAnywhereTableTool::reorganize, "DBA", "numeros"),
             "REORGANIZE TABLE");
        runs(sqlanywhere_table_tool(SqlAnywhereTableTool::create_statistics, "DBA", "numeros"),
             "CREATE STATISTICS");
        runs(sqlanywhere_checkpoint(), "CHECKPOINT");
        runs(sqlanywhere_truncate("DBA", "numeros"), "TRUNCATE TABLE");
        equal(scalar("SELECT count(*) FROM numeros"), "0", "a tabela ficou vazia");

        auto sessions = g_holt->query(std::string(sqlanywhere_sessions_query()));
        check(sessions && sessions->row_count() >= 1 &&
                  sessions->column(0).info().name == "Number",
              "sessoes: a consulta" +
                  (sessions ? std::string{} : "  -- " + sessions.error().to_string()));
        auto locks = g_holt->query(std::string(sqlanywhere_locks_query()));
        check(static_cast<bool>(locks),
              "travas: a consulta" + (locks ? std::string{} : "  -- " + locks.error().to_string()));
        check(!sqlanywhere_session_kill("12; DROP").ok(), "DROP CONNECTION so' aceita digitos");

        // DROP CONNECTION de verdade, numa sessao vitima aberta so' para isso.
        {
            auto victim = sqlanywhere_driver().connect(scratch_config);
            check(static_cast<bool>(victim), "(sessao vitima)");
            if (victim) {
                auto number = (*victim)->query("SELECT connection_property('Number')");
                const std::string id = number ? std::string(number->text(0, 0)) : std::string{};
                runs(sqlanywhere_session_kill(id), "DROP CONNECTION da sessao vitima");
                auto after = (*victim)->query("SELECT 1");
                check(!after, "a sessao vitima foi encerrada");
            }
        }

        // Cancelar: o pedido de atencao com a conexao parada nao a derruba.
        check(static_cast<bool>(g_holt->cancel()), "cancelar com a conexao parada");
        equal(scalar("SELECT 41 + 1"), "42", "a conexao segue util depois do cancelamento");

        for (const DashboardChart& chart : dashboard_catalog("sqlanywhere")) {
            auto rs = g_holt->query(chart.sql);
            check(rs && rs->row_count() == 1,
                  std::string("dashboard: ") + chart.id +
                      (rs ? std::string{} : "  -- " + rs.error().to_string()));
        }
        check(std::string(dashboard_catalog("sqlanywhere").front().id) == "sa.sessions",
              "o dashboard e' o do SQL Anywhere");

        runs(sqlanywhere_object_drop(ref_of(ObjectType::trigger, "vendas", "trg_pedido", "pedido")),
             "DROP TRIGGER");
        runs(sqlanywhere_object_drop(
                 ref_of(ObjectType::foreign_key, "vendas", "fk_pedido_cliente", "pedido")),
             "DROP FOREIGN KEY pelo editor");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::column, "vendas", "foto", "cliente")),
             "DROP de coluna pelo editor");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::view, "vendas", "v_cliente")), "DROP VIEW");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::procedure, "vendas", "p_total")),
             "DROP PROCEDURE");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::function, "vendas", "f_dobro")),
             "DROP FUNCTION");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::sequence, "vendas", "seq_nota")),
             "DROP SEQUENCE");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::event, "DBA", "otter_evento")),
             "DROP EVENT");
        runs(generate_drop("DBA", "cep", ObjKind::data_type, false), "DROP DOMAIN");
        runs(generate_drop("vendas", "pedido", ObjKind::table, /*cascade=*/true),
             "DROP TABLE (CASCADE nao vai para o comando)");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::role, "", "otter_papel", "role")),
             "DROP ROLE");
        runs(sqlanywhere_object_drop(ref_of(ObjectType::role, "", kUser)), "DROP USER");
    }

    // --- Limpeza -------------------------------------------------------------------------------
    std::printf("\nLimpeza\n");
    scratch->reset();
    g_holt = master_holt;
    (void)g_holt->execute(std::string("STOP DATABASE ") + kDb + " UNCONDITIONALLY");
    sql("DROP DATABASE " + file_literal, "DROP DATABASE (apaga o arquivo)");
    equal(scalar(std::string("SELECT count(*) FROM sa_db_info() WHERE Alias = '") + kDb + "'"),
          "0", "o banco de rascunho nao esta' mais no servidor");
    check(g_holt->txn_state() == TxnState::idle, "a conexao principal termina sem transacao");

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
