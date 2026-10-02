// Executa contra um SQL Server de verdade o que o perfil SQL Server GERA e LE':
// o catalogo (db/catalog_mssql.cpp), o editor de objeto (db/mssql_object.cpp),
// o ALTER TABLE em T-SQL (db/alter.cpp), a paginacao por OFFSET/FETCH
// (sql/paging.cpp), a gravacao da grade (db/edit.cpp) e a importacao.
//
// Um comando sintaticamente plausivel pode ser recusado pelo servidor -- ou,
// pior, aceito fazendo outra coisa. Aqui cada gerador roda e o EFEITO e' lido
// de volta das views do catalogo.
//
// Tudo acontece num banco de rascunho (`otter_scratch_ms`) e num login de
// rascunho, criados e apagados pelo proprio teste. Nenhum outro banco do
// servidor e' tocado.
//
//     otter_tests_mssql_live [<host> [<port>]]
//
// Conecta com a conta do Windows (precisa de CREATE DATABASE e de ALTER ANY
// LOGIN). So' roda contra localhost: o teste cria e apaga banco e login.
#include "db/alter.hpp"
#include "db/app_tools.hpp"
#include "db/catalog_mssql.hpp"
#include "db/ddl.hpp"
#include "db/drivers/mssql.hpp"
#include "db/edit.hpp"
#include "db/grid_ops.hpp"
#include "db/import.hpp"
#include "db/mssql_object.hpp"
#include "db/object_info_load.hpp"
#include "sql/dialect.hpp"
#include "sql/paging.hpp"

#include <cstdio>
#include <cstdlib>
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

TableMeta loaded_table(MssqlCatalog& catalog, const char* schema, const char* name) {
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

constexpr const char* kDb    = "otter_scratch_ms";
constexpr const char* kLogin = "otter_scratch_login";

} // namespace

int main(int argc, char** argv) {
    ConnConfig config;
    config.host            = argc >= 2 ? argv[1] : "localhost";
    config.port            = argc >= 3 ? static_cast<std::uint16_t>(std::atoi(argv[2])) : 1433;
    config.database        = "master";
    config.integrated_auth = true;
    config.driver_id       = "sqlserver";

    // Este teste cria e apaga banco e login: so' num servidor local.
    if (config.host != "localhost" && config.host != "127.0.0.1") {
        std::fprintf(stderr, "recusado: so' roda contra localhost (host = %s)\n",
                     config.host.c_str());
        return 2;
    }

    auto master = mssql_driver().connect(config);
    if (!master) {
        std::fprintf(stderr, "conexao falhou: %s\n", master.error().to_string().c_str());
        return 2;
    }
    g_holt = master->get();
    set_sql_dialect(QuoteStyle::brackets);
    std::printf("Servidor: %s\n\n", g_holt->server_version().c_str());

    // Restos de uma execucao interrompida.
    (void)g_holt->execute(
        "IF DB_ID(N'otter_scratch_ms') IS NOT NULL BEGIN "
        "ALTER DATABASE [otter_scratch_ms] SET SINGLE_USER WITH ROLLBACK IMMEDIATE; "
        "DROP DATABASE [otter_scratch_ms]; END");
    (void)g_holt->execute(
        "IF SUSER_ID(N'otter_scratch_login') IS NOT NULL DROP LOGIN [otter_scratch_login]");

    // --- Servidor: banco e login ----------------------------------------------------
    std::printf("Servidor\n");
    runs(mssql_create_database(kDb, ""), "CREATE DATABASE");
    equal(scalar("SELECT COUNT(*) FROM sys.databases WHERE name = N'otter_scratch_ms'"), "1",
          "o banco existe");

    {
        MssqlCatalog catalog(*g_holt);
        auto databases = catalog.load_databases(false, false);
        bool found = false;
        if (databases) {
            for (const DatabaseMeta& database : *databases) {
                if (database.name == kDb) found = database.allow_connect;
            }
        }
        check(found, "load_databases lista o banco novo, com acesso");

        MssqlNewLogin login;
        login.name             = kLogin;
        login.password         = "Otter#Scratch'9x";   // aspa na senha: o literal a dobra
        login.default_database = kDb;
        runs(mssql_create_login(login), "CREATE LOGIN");
        runs(mssql_login_password(kLogin, "Outra#Senha'7y"), "ALTER LOGIN ... PASSWORD");

        auto logins = catalog.load_list(CatalogList::logins, {}, {}, {});
        bool listed = false;
        if (logins) {
            for (const CatalogItem& item : *logins) listed = listed || item.name == kLogin;
        }
        check(listed, "a lista de logins traz o novo");

        const ObjectInfo info =
            load_object_info(catalog, *g_holt, ref_of(ObjectType::role, "", kLogin));
        check(info.error.empty() && property(info, "Name") != nullptr,
              "editor do login: propriedades" + (info.error.empty() ? "" : " -- " + info.error));
    }

    // --- O banco de rascunho ----------------------------------------------------------
    config.database = kDb;
    auto scratch = mssql_driver().connect(config);
    if (!scratch) {
        std::fprintf(stderr, "conexao ao banco de rascunho falhou: %s\n",
                     scratch.error().to_string().c_str());
        g_holt = master->get();
        (void)g_holt->execute("DROP DATABASE [otter_scratch_ms]");
        (void)g_holt->execute("DROP LOGIN [otter_scratch_login]");
        return 2;
    }
    g_holt = scratch->get();
    MssqlCatalog catalog(*g_holt);
    equal(scalar("SELECT DB_NAME()"), kDb, "a segunda sessao nasce no banco pedido");

    std::printf("\nSchema e tabela\n");
    runs(mssql_create_schema("vendas", ""), "CREATE SCHEMA");

    {
        std::vector<NewColumn> columns;
        NewColumn id;
        id.name = "id"; id.type_name = "int IDENTITY(1,1)"; id.nullable = false;
        NewColumn nome;
        nome.name = "Nome Completo"; nome.type_name = "nvarchar(100)"; nome.nullable = false;
        nome.comment = "nome do cliente";
        NewColumn ativo;
        ativo.name = "ativo"; ativo.type_name = "bit"; ativo.nullable = false;
        ativo.default_value = "1";
        NewColumn credito;
        credito.name = "credito"; credito.type_name = "decimal(12,2)";
        NewColumn foto;
        foto.name = "foto"; foto.type_name = "varbinary(max)";
        columns = {id, nome, ativo, credito, foto};
        runs(generate_create_table("vendas", "cliente", columns, {"id"}, "cadastro de clientes"),
             "CREATE TABLE com PK, default e comentarios");
    }
    equal(scalar("SELECT CAST(value AS nvarchar(200)) FROM sys.extended_properties "
                 "WHERE major_id = OBJECT_ID(N'vendas.cliente') AND minor_id = 0 "
                 "AND name = N'MS_Description'"),
          "cadastro de clientes", "comentario da tabela (MS_Description)");

    {
        auto tables = catalog.load_tables("vendas");
        check(tables && tables->size() == 1 && tables->front().name == "cliente" &&
                  tables->front().comment == "cadastro de clientes",
              "load_tables: a tabela e o comentario");

        const TableMeta table = loaded_table(catalog, "vendas", "cliente");
        check(table.columns.size() == 5, "load_columns: cinco colunas");
        const ColumnMeta* nome = column_of(table, "Nome Completo");
        check(nome != nullptr && nome->type_name == "nvarchar(100)" && !nome->nullable &&
                  nome->comment == "nome do cliente",
              "coluna com espaco no nome: tipo, nulidade e comentario");
        const ColumnMeta* id = column_of(table, "id");
        check(id != nullptr && id->primary_key && id->default_value == "IDENTITY",
              "a coluna identity e' a chave");
        const ColumnMeta* foto = column_of(table, "foto");
        check(foto != nullptr && foto->type_name == "varbinary(max)", "varbinary(max)");
        check(!table.constraints.empty() &&
                  table.constraints.front().kind == ObjKind::primary_key,
              "load_constraints: a chave primaria");
    }

    // --- ALTER TABLE em T-SQL ---------------------------------------------------------
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
        pontos.name = "pontos"; pontos.type_name = "int"; pontos.nullable = false;
        pontos.default_value = "0";
        add.add_columns = {email, pontos};
        runs(generate_alter(table, add), "ADD de duas colunas (sem a palavra COLUMN)");
        equal(scalar("SELECT COUNT(*) FROM sys.columns WHERE object_id = "
                     "OBJECT_ID(N'vendas.cliente') AND name IN (N'email', N'pontos')"),
              "2", "as duas colunas existem");

        table = loaded_table(catalog, "vendas", "cliente");
        const ColumnMeta* pontos_now = column_of(table, "pontos");
        check(pontos_now != nullptr && contains(pontos_now->default_value, "0"),
              "o default da coluna nova e' lido");

        TableAlteration change;
        change.schema = "vendas";
        change.table  = "cliente";
        ColumnChange email_change;
        email_change.name      = "email";
        email_change.new_name  = "e-mail";
        email_change.type_name = "nvarchar(200)";
        email_change.nullable  = false;
        email_change.comment   = "endereco de contato";
        ColumnChange pontos_change;
        pontos_change.name          = "pontos";
        pontos_change.default_value = "10";
        change.alter_columns = {email_change, pontos_change};
        // NOT NULL so' com a tabela vazia -- e' o caso.
        runs(generate_alter(table, change),
             "renomeia, troca tipo e nulidade, troca o default, comenta");

        table = loaded_table(catalog, "vendas", "cliente");
        const ColumnMeta* renamed = column_of(table, "e-mail");
        check(renamed != nullptr && renamed->type_name == "nvarchar(200)" &&
                  !renamed->nullable && renamed->comment == "endereco de contato",
              "a coluna renomeada tem o tipo, a nulidade e o comentario novos");
        const ColumnMeta* pontos_after = column_of(table, "pontos");
        check(pontos_after != nullptr && contains(pontos_after->default_value, "10"),
              "o default foi trocado (constraint antiga removida, nova criada)");

        TableAlteration drop_default;
        drop_default.schema = "vendas";
        drop_default.table  = "cliente";
        ColumnChange none;
        none.name          = "pontos";
        none.default_value = std::string{};
        drop_default.alter_columns = {none};
        runs(generate_alter(table, drop_default), "DROP do default pelo nome da constraint");
        equal(scalar("SELECT COUNT(*) FROM sys.default_constraints dc JOIN sys.columns c "
                     "ON c.object_id = dc.parent_object_id AND c.column_id = dc.parent_column_id "
                     "WHERE dc.parent_object_id = OBJECT_ID(N'vendas.cliente') "
                     "AND c.name = N'pontos'"),
              "0", "a coluna ficou sem default");

        // Coluna COM default: o DROP COLUMN so' passa tirando a constraint antes.
        table = loaded_table(catalog, "vendas", "cliente");
        TableAlteration drop;
        drop.schema       = "vendas";
        drop.table        = "cliente";
        drop.drop_columns = {"ativo"};
        const AlterScript drop_script = generate_alter(table, drop);
        check(drop_script.has_destructive(), "DROP COLUMN e' marcado como destrutivo");
        runs(drop_script, "DROP COLUMN de coluna com default");
        equal(scalar("SELECT COUNT(*) FROM sys.columns WHERE object_id = "
                     "OBJECT_ID(N'vendas.cliente') AND name = N'ativo'"),
              "0", "a coluna saiu");
        sql("ALTER TABLE vendas.cliente ADD ativo bit NOT NULL DEFAULT 1", "(repoe a coluna)");
    }

    // --- Indices, constraints, chave estrangeira -------------------------------------
    std::printf("\nIndices e constraints\n");
    {
        NewIndex index;
        index.name    = "ix_cliente_nome";
        index.columns = {"Nome Completo"};
        index.unique  = true;
        runs(generate_create_index("vendas", "cliente", index), "CREATE UNIQUE INDEX");

        auto indexes = catalog.load_indexes("vendas", "cliente");
        bool found = false;
        if (indexes) {
            for (const IndexMeta& item : *indexes) {
                found = found || (item.name == "ix_cliente_nome" && item.unique);
            }
        }
        check(found, "load_indexes: o indice unico");

        runs(mssql_object_rename(ref_of(ObjectType::index, "vendas", "ix_cliente_nome", "cliente"),
                                 "ix_cliente_nome2"),
             "sp_rename de indice");
        runs(generate_drop_index("vendas", "cliente", "ix_cliente_nome2", false),
             "DROP INDEX ... ON tabela");

        NewConstraint check_constraint;
        check_constraint.name       = "ck_cliente_credito";
        check_constraint.kind       = ConstraintKind::check;
        check_constraint.expression = "credito >= 0";
        runs(generate_add_constraint("vendas", "cliente", check_constraint), "ADD CHECK");

        sql("CREATE TABLE vendas.pedido (id int IDENTITY(1,1) PRIMARY KEY, "
            "cliente_id int NOT NULL, total decimal(12,2) NOT NULL)",
            "(tabela pedido)");
        NewForeignKey key;
        key.name           = "fk_pedido_cliente";
        key.columns        = {"cliente_id"};
        key.target_table   = "cliente";
        key.target_columns = {"id"};
        key.on_delete      = "RESTRICT";   // vira NO ACTION no T-SQL
        key.on_update      = "CASCADE";
        runs(generate_add_foreign_key("vendas", "pedido", key),
             "ADD FOREIGN KEY (RESTRICT vira NO ACTION)");

        auto keys = catalog.load_table_foreign_keys("vendas", "pedido");
        check(keys && keys->size() == 1 && keys->front().target_table == "cliente" &&
                  keys->front().source_column == "cliente_id",
              "load_table_foreign_keys");
        auto references = catalog.load_references("vendas", "cliente");
        check(references && references->size() == 1 &&
                  references->front().source_table == "pedido",
              "load_references: quem aponta para cliente");

        runs(generate_drop_foreign_key("vendas", "pedido", "fk_pedido_cliente"), "DROP da FK");
        runs(generate_drop_constraint("vendas", "cliente", "ck_cliente_credito",
                                      ObjKind::check_constraint),
             "DROP CONSTRAINT");
        runs(generate_add_foreign_key("vendas", "pedido", key), "(FK de volta)");
    }

    // --- View, rotina, trigger, sequence, sinonimo -------------------------------------
    std::printf("\nView, rotina, trigger, sequence, sinonimo\n");
    {
        runs(generate_create_view("vendas", "v_cliente",
                                  "SELECT id, [Nome Completo] FROM vendas.cliente", true),
             "CREATE OR ALTER VIEW");
        const ObjectRef view = ref_of(ObjectType::view, "vendas", "v_cliente");
        ObjectInfo info = load_object_info(catalog, *g_holt, view);
        check(info.error.empty() && contains(info.ddl, "v_cliente"),
              "editor da view: o fonte" + (info.error.empty() ? "" : " -- " + info.error));

        // Gravar o fonte editado: CREATE vira ALTER, e a view muda no lugar.
        std::string edited = info.ddl;
        const std::size_t at = edited.find("[Nome Completo]");
        if (at != std::string::npos) edited.insert(at + 15, ", credito");
        runs(mssql_source_script(view, edited), "fonte editado da view (ALTER VIEW)");
        equal(scalar("SELECT COUNT(*) FROM sys.columns WHERE object_id = "
                     "OBJECT_ID(N'vendas.v_cliente')"),
              "3", "a view passou a ter tres colunas");

        sql("CREATE PROCEDURE vendas.p_total @cliente int, @total decimal(12,2) OUTPUT AS "
            "BEGIN SET NOCOUNT ON; SELECT @total = COALESCE(SUM(total), 0) FROM vendas.pedido "
            "WHERE cliente_id = @cliente; END",
            "(procedure com parametro OUTPUT)");
        sql("CREATE FUNCTION vendas.f_dobro(@n int) RETURNS int AS BEGIN RETURN @n * 2; END",
            "(funcao escalar)");
        sql("CREATE FUNCTION vendas.f_pedidos(@cliente int) RETURNS TABLE AS RETURN "
            "(SELECT id, total FROM vendas.pedido WHERE cliente_id = @cliente)",
            "(funcao de tabela)");

        auto routines = catalog.load_routines("vendas");
        check(routines && routines->size() == 3, "load_routines: tres rotinas");
        if (routines) {
            for (const RoutineMeta& routine : *routines) {
                std::string call = routine_call_sql("vendas", routine, false);
                // Os marcadores que o editor perguntaria ao executar.
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
                    equal(std::string(rs->text(0, 0)), "2", "a funcao escalar devolveu 2");
                }
                if (rs && routine.name == "p_total") {
                    check(rs->column_count() == 1 && rs->column(0).info().name == "total",
                          "o parametro OUTPUT volta como coluna");
                }
            }
        }

        auto parameters = catalog.load_list(CatalogList::routine_parameters, "vendas", "p_total", {});
        check(parameters && parameters->size() == 2 && parameters->back().flag,
              "parametros da procedure, o segundo OUTPUT");

        const ObjectRef procedure = ref_of(ObjectType::procedure, "vendas", "p_total");
        info = load_object_info(catalog, *g_holt, procedure);
        check(info.error.empty() && contains(info.ddl, "CREATE PROCEDURE"),
              "editor da procedure: o fonte");
        runs(mssql_source_script(procedure, info.ddl), "regravar a procedure (ALTER PROCEDURE)");

        runs(mssql_object_comment(procedure, "soma os pedidos"), "comentario da procedure");
        info = load_object_info(catalog, *g_holt, procedure);
        const std::string* comment = property(info, "Comment");
        check(comment != nullptr && *comment == "soma os pedidos", "o comentario e' lido de volta");
        runs(mssql_object_comment(procedure, "soma tudo"), "troca do comentario (update)");
        runs(mssql_object_comment(procedure, ""), "remocao do comentario");

        NewTrigger trigger;
        trigger.name   = "trg_pedido";
        trigger.table  = "pedido";
        trigger.timing = "AFTER";
        trigger.event  = "INSERT";
        trigger.body   = "BEGIN SET NOCOUNT ON; END";
        runs(generate_create_trigger("vendas", trigger), "CREATE TRIGGER (corpo e' codigo)");
        trigger.timing = "BEFORE";
        check(!generate_create_trigger("vendas", trigger).ok(), "BEFORE e' recusado com o motivo");

        auto triggers = catalog.load_triggers("vendas", "pedido");
        check(triggers && triggers->size() == 1 && triggers->front().enabled &&
                  triggers->front().timing == "AFTER",
              "load_triggers");
        const ObjectRef trigger_ref = ref_of(ObjectType::trigger, "vendas", "trg_pedido", "pedido");
        runs(mssql_trigger_enable(trigger_ref, false), "DISABLE TRIGGER");
        triggers = catalog.load_triggers("vendas", "pedido");
        check(triggers && triggers->size() == 1 && !triggers->front().enabled,
              "o trigger aparece desligado");
        runs(mssql_trigger_enable(trigger_ref, true), "ENABLE TRIGGER");
        info = load_object_info(catalog, *g_holt, trigger_ref);
        check(info.error.empty() && contains(info.ddl, "trg_pedido"), "editor do trigger");

        NewSequence sequence;
        sequence.name      = "seq_nota";
        sequence.start     = 100;
        sequence.increment = 5;
        runs(generate_create_sequence("vendas", sequence), "CREATE SEQUENCE");
        auto sequences = catalog.load_sequences("vendas");
        check(sequences && sequences->size() == 1 && sequences->front().start_value == 100 &&
                  sequences->front().increment == 5,
              "load_sequences");
        info = load_object_info(catalog, *g_holt, ref_of(ObjectType::sequence, "vendas", "seq_nota"));
        check(info.error.empty() && !info.properties.empty(), "editor da sequence");

        runs(mssql_create_synonym("vendas", "clientes", "vendas.cliente"), "CREATE SYNONYM");
        auto synonyms = catalog.load_list(CatalogList::synonyms, "vendas", {}, {});
        check(synonyms && synonyms->size() == 1 && synonyms->front().name == "clientes",
              "lista de sinonimos");

        sql("CREATE TYPE vendas.cep FROM varchar(9) NOT NULL", "(tipo alias)");
        auto types = catalog.load_types("vendas");
        check(types && types->size() == 1 && types->front().base_type == "varchar(9)",
              "load_types: o alias e o tipo base");

        auto dependencies = catalog.load_list(CatalogList::dependencies, "vendas", "v_cliente", {});
        check(dependencies && !dependencies->empty(), "dependencias da view");

        auto schema_indexes = catalog.load_list(CatalogList::schema_indexes, "vendas", {}, {});
        check(schema_indexes && schema_indexes->size() >= 2, "indices do schema");
    }

    // --- Editor de objeto: tabela, banco, schema; permissoes -----------------------------
    std::printf("\nEditor de objeto e permissoes\n");
    {
        const ObjectRef table = ref_of(ObjectType::table, "vendas", "cliente");
        ObjectInfo info = load_object_info(catalog, *g_holt, table);
        check(info.error.empty() && contains(info.ddl, "CREATE TABLE") &&
                  contains(info.ddl, "IDENTITY(1,1)") && contains(info.ddl, "[Nome Completo]"),
              "DDL da tabela montado do catalogo" +
                  (info.error.empty() ? "" : " -- " + info.error));

        // O DDL mostrado precisa RODAR: recria a tabela com outro nome.
        std::string copy = info.ddl;
        for (std::size_t p = copy.find("[cliente]"); p != std::string::npos;
             p = copy.find("[cliente]", p + 1)) {
            copy.replace(p, 9, "[cliente_copia]");
        }
        // Os comentarios citam a tabela por LITERAL (sp_addextendedproperty).
        for (std::size_t p = copy.find("N'cliente'"); p != std::string::npos;
             p = copy.find("N'cliente'", p + 1)) {
            copy.replace(p, 10, "N'cliente_copia'");
        }
        for (std::size_t p = copy.find("PK_"); p != std::string::npos;
             p = copy.find("PK_", p + 4)) {
            copy.replace(p, 3, "PKC_");
        }
        // O script traz varios comandos separados por linha "GO" ou por ';'.
        {
            auto status = g_holt->execute(copy);
            check(static_cast<bool>(status),
                  "o DDL da tabela e' executavel" +
                      (status ? "" : "  -- " + status.error().to_string() + "\n" + copy));
        }

        info = load_object_info(catalog, *g_holt, ref_of(ObjectType::database, "", kDb));
        check(info.error.empty() && property(info, "Name") != nullptr, "editor do banco");
        info = load_object_info(catalog, *g_holt, ref_of(ObjectType::schema, "", "vendas"));
        check(info.error.empty() && property(info, "Name") != nullptr, "editor do schema");

        sql("CREATE USER otter_user FOR LOGIN [otter_scratch_login]", "(usuario do banco)");
        auto roles = catalog.load_list(CatalogList::roles, {}, {}, {});
        bool has_user = false, has_public = false;
        if (roles) {
            for (const CatalogItem& item : *roles) {
                has_user   = has_user || item.name == "otter_user";
                has_public = has_public || item.name == "public";
            }
        }
        check(has_user && has_public, "principais do banco: o usuario novo e public");

        // SQL Server Authentication: o login criado acima entra com usuario e
        // senha (a senha trocada pelo ALTER LOGIN, com a aspa dentro).
        {
            ConnConfig as_login       = config;
            as_login.integrated_auth  = false;
            as_login.user             = kLogin;
            as_login.password         = "Outra#Senha'7y";
            auto session = mssql_driver().connect(as_login);
            check(static_cast<bool>(session),
                  "login por usuario e senha" +
                      (session ? std::string{} : "  -- " + session.error().to_string()));
            if (session) {
                auto who = (*session)->query("SELECT SUSER_SNAME(), DB_NAME()");
                check(who && who->text(0, 0) == kLogin && who->text(0, 1) == kDb,
                      "a sessao e' do login, no banco padrao dele");
            }

            as_login.password = "senha errada";
            auto refused = mssql_driver().connect(as_login);
            check(!refused, "senha errada e' recusada");
        }

        runs(mssql_grant(table, "SELECT", "otter_user", true), "GRANT ... WITH GRANT OPTION");
        runs(mssql_grant(table, "UPDATE", "otter_user", false), "GRANT UPDATE");
        info = load_object_info(catalog, *g_holt, table);
        bool select_grantable = false, update = false;
        for (const ObjectPermission& permission : info.permissions) {
            if (permission.grantee != "otter_user") continue;
            if (permission.privilege == "SELECT") select_grantable = permission.grantable;
            if (permission.privilege == "UPDATE") update = true;
        }
        check(select_grantable && update, "as permissoes sao lidas, com a opcao de repasse");

        // "Tirar so' a opcao de repassar": a tela troca REVOKE por REVOKE GRANT OPTION FOR.
        AlterScript option = mssql_revoke(table, "SELECT", "otter_user");
        if (option.ok()) option.statements.front().replace(0, 6, "REVOKE GRANT OPTION FOR");
        runs(option, "REVOKE GRANT OPTION FOR");
        runs(mssql_revoke(table, "UPDATE", "otter_user"), "REVOKE");
        info = load_object_info(catalog, *g_holt, table);
        bool still_select = false, still_update = false;
        for (const ObjectPermission& permission : info.permissions) {
            if (permission.grantee != "otter_user") continue;
            if (permission.privilege == "SELECT") still_select = !permission.grantable;
            if (permission.privilege == "UPDATE") still_update = true;
        }
        check(still_select && !still_update, "SELECT ficou sem repasse; UPDATE saiu");

        runs(mssql_object_schema(ref_of(ObjectType::table, "vendas", "cliente_copia"), "dbo"),
             "ALTER SCHEMA TRANSFER");
        runs(mssql_object_rename(ref_of(ObjectType::table, "dbo", "cliente_copia"), "copia"),
             "sp_rename de tabela");
        equal(scalar("SELECT COUNT(*) FROM sys.tables WHERE name = N'copia' "
                     "AND schema_id = SCHEMA_ID(N'dbo')"),
              "1", "a tabela esta' em dbo com o nome novo");
    }

    // --- Grade: gravar edicoes ----------------------------------------------------------
    std::printf("\nGrade\n");
    {
        sql("INSERT INTO vendas.cliente ([Nome Completo], [e-mail], credito, pontos) VALUES "
            "(N'Ana', N'ana@x', 10.50, 1), (N'Bia', N'bia@x', 20, 2), (N'Caio', N'caio@x', NULL, 3)",
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

            std::size_t c_nome = 0, c_credito = 0, c_ativo = 0, c_foto = 0, c_email = 0, c_pontos = 0;
            for (std::size_t c = 0; c < rs->column_count(); ++c) {
                const std::string& name = rs->column(c).info().name;
                if (name == "Nome Completo") c_nome = c;
                if (name == "credito") c_credito = c;
                if (name == "ativo") c_ativo = c;
                if (name == "foto") c_foto = c;
                if (name == "e-mail") c_email = c;
                if (name == "pontos") c_pontos = c;
            }

            EditBuffer buffer;
            buffer.set(0, c_nome, "Ana Júlia D'Ávila 日本");   // fora do ASCII: literal N'...'
            buffer.set(0, c_ativo, "false");                 // bit: 0, nao FALSE
            buffer.set(0, c_foto, "0xCAFE01");               // varbinary: literal 0x cru
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
                for (std::string& statement : *changes) script.statements.push_back(std::move(statement));
                script.statements.emplace_back(transaction_commit_sql());
                runs(script, "INSERT, UPDATE e DELETE dentro de BEGIN/COMMIT TRANSACTION");
            }
            equal(scalar("SELECT [Nome Completo] FROM vendas.cliente WHERE id = 1"),
                  "Ana Júlia D'Ávila 日本", "o texto Unicode chegou inteiro");
            equal(scalar("SELECT CAST(ativo AS int) FROM vendas.cliente WHERE id = 1"), "0",
                  "o bit virou 0");
            equal(scalar("SELECT CONVERT(varchar(20), foto, 1) FROM vendas.cliente WHERE id = 1"),
                  "0xCAFE01", "o binario foi gravado");
            equal(scalar("SELECT COUNT(*) FROM vendas.cliente WHERE id = 2 AND credito IS NULL"),
                  "1", "o NULL foi gravado");
            equal(scalar("SELECT COUNT(*) FROM vendas.cliente WHERE id = 3"), "0",
                  "a linha excluida saiu");
            equal(scalar("SELECT CAST(credito AS varchar(20)) FROM vendas.cliente "
                         "WHERE [Nome Completo] = N'Davi'"),
                  "7.25", "a linha nova entrou");
        }
    }

    // --- Paginacao -----------------------------------------------------------------------
    std::printf("\nPaginacao (OFFSET / FETCH)\n");
    {
        sql("CREATE TABLE dbo.numeros (n int PRIMARY KEY, grupo int NOT NULL, texto text NULL)",
            "(tabela de numeros)");
        sql("INSERT INTO dbo.numeros (n, grupo) SELECT TOP (50) "
            "ROW_NUMBER() OVER (ORDER BY (SELECT NULL)), "
            "ROW_NUMBER() OVER (ORDER BY (SELECT NULL)) % 5 FROM sys.all_objects",
            "(50 linhas)");

        const otter::sql::Dialect& dialect = otter::sql::dialect_for("sqlserver");
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

        if (auto rs = rows(page("SELECT * FROM dbo.numeros", 0, 10), "sem ORDER BY: pagina 1")) {
            check(rs->row_count() == 11, "veio a pagina e a linha-sonda (11)");
        }
        if (auto rs = rows(page("SELECT n FROM dbo.numeros ORDER BY n -- fim", 2, 10),
                           "com ORDER BY e comentario no fim: pagina 3")) {
            check(rs->row_count() == 11 && rs->text(0, 0) == "21", "comeca na linha 21");
        }
        if (auto rs = rows(page("SELECT n FROM dbo.numeros ORDER BY n;", 4, 10), "ultima pagina")) {
            check(rs->row_count() == 10, "a ultima pagina nao tem a sonda");
        }
        if (auto rs = rows(page("SELECT DISTINCT grupo FROM dbo.numeros", 0, 3), "DISTINCT")) {
            check(rs->row_count() == 4, "DISTINCT pagina pela posicao da coluna");
        }
        if (auto rs = rows(page("SELECT n FROM dbo.numeros WHERE n <= 5 UNION ALL "
                                "SELECT n FROM dbo.numeros WHERE n > 45", 0, 4),
                           "UNION")) {
            check(rs->row_count() == 5, "UNION pagina");
        }
        if (auto rs = rows(page("SELECT grupo, COUNT(*) AS total FROM dbo.numeros GROUP BY grupo",
                                0, 10),
                           "GROUP BY")) {
            check(rs->row_count() == 5, "GROUP BY pagina");
        }
        if (auto rs = rows(page("WITH x AS (SELECT n FROM dbo.numeros) SELECT n FROM x", 1, 20),
                           "CTE")) {
            check(rs->row_count() == 21, "CTE pagina");
        }

        otter::sql::SortOrder sort;
        sort.column     = "n";
        sort.descending = true;
        if (auto rs = rows(page("SELECT n, grupo FROM dbo.numeros", 0, 5, sort),
                           "ordenacao da grade")) {
            check(rs->text(0, 0) == "50", "ordena no servidor: comeca em 50");
        }

        otter::sql::ColumnFilter filter;
        filter.set_column("grupo", "= 3");
        if (auto rs = rows(page("SELECT n, grupo FROM dbo.numeros ORDER BY n", 0, 20, sort, filter),
                           "filtro sobre consulta com ORDER BY")) {
            check(rs->row_count() == 10 && rs->text(0, 0) == "48",
                  "filtra (10 linhas do grupo 3) e reordena");
        }

        check(page("SELECT TOP 5 * FROM dbo.numeros", 0, 10).refusal ==
                  otter::sql::PagingRefusal::already_limited,
              "TOP ja' e' um limite: nao reescreve");
        check(page("SELECT * INTO dbo.copia2 FROM dbo.numeros", 0, 10).refusal ==
                  otter::sql::PagingRefusal::not_a_query,
              "SELECT INTO nao e' consulta de grade");
        check(page("SELECT n FROM dbo.numeros FOR XML AUTO", 0, 10).refusal ==
                  otter::sql::PagingRefusal::unsupported_form,
              "FOR XML nao e' paginado");
        check(page("EXEC sp_who", 0, 10).refusal == otter::sql::PagingRefusal::not_a_query,
              "EXEC nao e' paginado");

        const otter::sql::PagedQuery count =
            otter::sql::make_count_query("SELECT * FROM dbo.numeros ORDER BY n", dialect, filter);
        equal(scalar(count.sql), "10", "contagem com ORDER BY interno e filtro");
        const otter::sql::PagedQuery count_top =
            otter::sql::make_count_query("SELECT TOP 7 * FROM dbo.numeros ORDER BY n", dialect, {});
        equal(scalar(count_top.sql), "7", "contagem de consulta com TOP");

        const otter::sql::PagedQuery whole =
            otter::sql::make_unpaged_query("SELECT n, grupo FROM dbo.numeros", dialect, {}, {});
        auto distinct = g_holt->query(distinct_query(whole.sql, "grupo", 3));
        check(distinct && distinct->row_count() == 3,
              "valores distintos (TOP e GROUP BY pelo nome)" +
                  (distinct ? std::string{} : "  -- " + distinct.error().to_string()));

        TableMeta numbers = loaded_table(catalog, "dbo", "numeros");
        auto top = g_holt->query(generate_select("dbo", numbers, 7));
        check(top && top->row_count() == 7, "SELECT gerado pela arvore usa TOP");
        equal(scalar(generate_count("dbo", numbers)), "50", "contagem gerada pela arvore");
    }

    // --- Importacao e CREATE TABLE pelo resultado -----------------------------------------
    std::printf("\nImportacao\n");
    {
        std::string csv = "n,grupo,texto\n";
        for (int i = 0; i < 2500; ++i) {
            csv += std::to_string(1000 + i) + "," + std::to_string(i % 7) + ",linha " +
                   std::to_string(i) + (i == 3 ? " ção" : "") + "\n";
        }
        const CsvTable data = parse_csv(csv, CsvOptions{});
        ImportPlan plan;
        plan.schema         = "dbo";
        plan.table          = "numeros";
        plan.columns        = {"n", "grupo", "texto"};
        plan.truncate_first = true;
        plan.batch_rows     = 5000;   // acima do limite do servidor: o gerador corta em 1000
        const ImportScript script = generate_import(data, plan);
        check(script.error.empty() && script.statements.size() == 4,
              "TRUNCATE + 3 lotes de ate' 1000 linhas");

        AlterScript load;
        load.statements.emplace_back(transaction_begin_sql());
        for (const std::string& statement : script.statements) load.statements.push_back(statement);
        load.statements.emplace_back(transaction_commit_sql());
        runs(load, "a carga roda numa transacao");
        equal(scalar("SELECT COUNT(*) FROM dbo.numeros"), "2500", "2500 linhas importadas");
        equal(scalar("SELECT CAST(texto AS varchar(40)) FROM dbo.numeros WHERE n = 1003"),
              "linha 3 ção", "acento preservado na carga");

        auto rs = g_holt->query("SELECT id, [Nome Completo], credito, foto, ativo FROM vendas.cliente");
        if (rs) {
            const std::string ddl = create_table_from_result(*rs, "do_resultado", false);
            auto status = g_holt->execute(ddl);
            check(static_cast<bool>(status),
                  "CREATE TABLE gerado do resultado" +
                      (status ? std::string{} : "  -- " + status.error().to_string() + "\n" + ddl));
            equal(scalar("SELECT TYPE_NAME(user_type_id) + '/' + CAST(max_length AS varchar(9)) "
                         "FROM sys.columns WHERE object_id = OBJECT_ID(N'dbo.do_resultado') "
                         "AND name = N'Nome Completo'"),
                  "nvarchar/200", "nvarchar(100) continua nvarchar(100)");
        }
    }

    // --- Transacoes do driver ---------------------------------------------------------------
    std::printf("\nTransacoes\n");
    {
        check(g_holt->auto_commit(), "nasce em auto-commit");
        check(static_cast<bool>(g_holt->set_auto_commit(false)), "modo manual");
        sql("DELETE FROM dbo.numeros WHERE n >= 3000", "(comando que abre a transacao)");
        sql("INSERT INTO dbo.numeros (n, grupo) VALUES (9001, 1)", "(insere)");
        check(g_holt->txn_state() == TxnState::active, "a transacao esta' aberta");
        check(static_cast<bool>(g_holt->savepoint("sp1")), "SAVE TRANSACTION");
        sql("INSERT INTO dbo.numeros (n, grupo) VALUES (9002, 1)", "(insere depois do ponto)");
        check(static_cast<bool>(g_holt->rollback_to("sp1")), "ROLLBACK ao ponto");
        check(static_cast<bool>(g_holt->commit()), "COMMIT");
        equal(scalar("SELECT COUNT(*) FROM dbo.numeros WHERE n IN (9001, 9002)"), "1",
              "ficou so' o que veio antes do ponto");
        sql("DELETE FROM dbo.numeros WHERE n = 9001", "(apaga)");
        check(static_cast<bool>(g_holt->rollback()), "ROLLBACK");
        equal(scalar("SELECT COUNT(*) FROM dbo.numeros WHERE n = 9001"), "1",
              "o ROLLBACK desfez a exclusao");
        (void)g_holt->rollback();
        check(static_cast<bool>(g_holt->set_auto_commit(true)), "volta ao auto-commit");
        check(g_holt->txn_state() == TxnState::idle, "sem transacao aberta");
    }

    // --- Ferramentas, sessoes, DROP ------------------------------------------------------
    std::printf("\nFerramentas e sessoes\n");
    {
        runs(mssql_table_tool(MssqlTableTool::update_statistics, "dbo", "numeros"),
             "UPDATE STATISTICS");
        runs(mssql_table_tool(MssqlTableTool::rebuild_indexes, "dbo", "numeros"),
             "ALTER INDEX ALL REBUILD");
        runs(mssql_table_tool(MssqlTableTool::reorganize_indexes, "vendas", "cliente"),
             "ALTER INDEX ALL REORGANIZE");
        runs(mssql_table_tool(MssqlTableTool::check, "dbo", "numeros"), "DBCC CHECKTABLE");
        runs(mssql_truncate("dbo", "numeros"), "TRUNCATE TABLE");
        equal(scalar("SELECT COUNT(*) FROM dbo.numeros"), "0", "a tabela ficou vazia");

        auto sessions = g_holt->query(std::string(mssql_sessions_query()));
        check(sessions && sessions->row_count() >= 2 &&
                  sessions->column(0).info().name == "session_id",
              "Session Manager: a consulta" +
                  (sessions ? std::string{} : "  -- " + sessions.error().to_string()));
        auto locks = g_holt->query(std::string(mssql_locks_query()));
        check(static_cast<bool>(locks),
              "Lock Manager: a consulta" +
                  (locks ? std::string{} : "  -- " + locks.error().to_string()));
        check(!mssql_session_kill("12; DROP").ok(), "KILL so' aceita digitos");

        // KILL de verdade, numa sessao vitima aberta so' para isso.
        {
            auto victim = mssql_driver().connect(config);
            check(static_cast<bool>(victim), "(sessao vitima)");
            if (victim) {
                auto spid = (*victim)->query("SELECT @@SPID");
                const std::string id = spid ? std::string(spid->text(0, 0)) : std::string{};
                runs(mssql_session_kill(id), "KILL da sessao vitima");
                auto after = (*victim)->query("SELECT 1");
                check(!after, "a sessao vitima foi encerrada");
            }
        }

        for (const DashboardChart& chart : dashboard_catalog("sqlserver")) {
            auto rs = g_holt->query(chart.sql);
            check(rs && rs->row_count() == 1,
                  std::string("dashboard: ") + chart.id +
                      (rs ? std::string{} : "  -- " + rs.error().to_string()));
        }

        runs(mssql_object_drop(ref_of(ObjectType::trigger, "vendas", "trg_pedido", "pedido")),
             "DROP TRIGGER");
        runs(mssql_object_drop(ref_of(ObjectType::foreign_key, "vendas", "fk_pedido_cliente", "pedido")),
             "DROP da chave estrangeira pelo editor");
        runs(mssql_object_drop(ref_of(ObjectType::column, "vendas", "foto", "cliente")),
             "DROP COLUMN pelo editor");
        runs(mssql_object_drop(ref_of(ObjectType::view, "vendas", "v_cliente")), "DROP VIEW");
        runs(mssql_object_drop(ref_of(ObjectType::procedure, "vendas", "p_total")), "DROP PROCEDURE");
        runs(mssql_object_drop(ref_of(ObjectType::function, "vendas", "f_dobro")), "DROP FUNCTION");
        runs(mssql_object_drop(ref_of(ObjectType::sequence, "vendas", "seq_nota")), "DROP SEQUENCE");
        runs(generate_drop("vendas", "pedido", ObjKind::table, /*cascade=*/true),
             "DROP TABLE (CASCADE nao vai para o T-SQL)");
    }

    // --- Limpeza ---------------------------------------------------------------------------
    std::printf("\nLimpeza\n");
    scratch->reset();
    g_holt = master->get();
    runs(mssql_object_rename(ref_of(ObjectType::database, "", kDb), "otter_scratch_ms2"),
         "ALTER DATABASE MODIFY NAME");
    runs(mssql_object_drop(ref_of(ObjectType::database, "", "otter_scratch_ms2")), "DROP DATABASE");
    runs(mssql_object_drop(ref_of(ObjectType::role, "", kLogin)), "DROP LOGIN");
    equal(scalar("SELECT COUNT(*) FROM sys.databases WHERE name LIKE N'otter[_]scratch[_]ms%'"),
          "0", "nenhum banco de rascunho ficou");
    equal(scalar("SELECT COUNT(*) FROM sys.server_principals WHERE name = N'otter_scratch_login'"),
          "0", "o login de rascunho saiu");

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
