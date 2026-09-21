// Executa as consultas de catalogo contra um PostgreSQL de verdade.
//
// Por que existe separado dos testes unitarios: uma consulta sintaticamente
// valida em C++ pode ser rejeitada pelo servidor, e nenhum teste unitario
// pega isso. Foi exatamente o que aconteceu com load_routine_definition, que
// ficou "pronto" por semanas montando uma assinatura que o PostgreSQL
// recusava:
//
//     ERRO: o nome do tipo de dados "p_cliente integer" nao e' valido
//
// Pre-requisito -- criar o schema otter_test:
//
//     psql -h localhost -U postgres -d ERP_TID -f tests/integration/fixtures.sql
//
// Nao roda no ctest por padrao: depende de um banco externo. Rodar assim:
//
//     otter_tests_live <host> <port> <db> <user> <pass>
//
// Sem argumentos, tenta as variaveis PGHOST/PGPORT/PGDATABASE/PGUSER/PGPASSWORD.
#include "db/catalog.hpp"
#include "db/drivers/postgres.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int failures = 0;
int checks   = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (condition) {
        std::printf("  [ OK ] %s\n", what);
    } else {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string(fallback);
}

constexpr const char* kSchema = "otter_test";

} // namespace

int main(int argc, char** argv) {
    otter::db::ConnConfig config;
    if (argc >= 6) {
        config.host     = argv[1];
        config.port     = static_cast<std::uint16_t>(std::atoi(argv[2]));
        config.database = argv[3];
        config.user     = argv[4];
        config.password = argv[5];
    } else {
        config.host     = env_or("PGHOST", "localhost");
        config.port     = static_cast<std::uint16_t>(
                              std::atoi(env_or("PGPORT", "5432").c_str()));
        config.database = env_or("PGDATABASE", "ERP_TID");
        config.user     = env_or("PGUSER", "postgres");
        config.password = env_or("PGPASSWORD", "");
    }

    auto holt = otter::db::postgres_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexao falhou: %s\n",
                     holt.error().to_string().c_str());
        return 2;
    }

    otter::db::PostgresCatalog catalog(**holt);
    std::printf("PostgreSQL %d.%d, schema %s\n\n",
                catalog.version().major, catalog.version().minor, kSchema);

    // --- Relacoes ------------------------------------------------------------
    std::printf("load_tables\n");
    auto relations = catalog.load_tables(kSchema);
    check(relations.has_value(), "consulta aceita pelo servidor");
    if (!relations) {
        std::fprintf(stderr, "%s\n", relations.error().to_string().c_str());
        return 1;
    }

    std::size_t tables = 0, views = 0, mviews = 0;
    for (const auto& r : *relations) {
        if (r.kind == otter::db::ObjKind::table)                  ++tables;
        else if (r.kind == otter::db::ObjKind::view)              ++views;
        else if (r.kind == otter::db::ObjKind::materialized_view) ++mviews;
    }
    check(tables == 2, "2 tabelas");
    check(views  == 2, "2 views");
    check(mviews == 1, "1 materialized view");

    // --- Corpo das views -----------------------------------------------------
    std::printf("\nload_view_definition\n");
    for (const auto& r : *relations) {
        if (!r.is_view()) continue;

        auto body = catalog.load_view_definition(kSchema, r.name);
        check(body.has_value() && !body->empty(),
              ("corpo de " + r.name + " nao vazio").c_str());

        // O segundo argumento de pg_get_viewdef pede a versao formatada. Sem
        // ele, tudo volta numa linha so'.
        if (body && !body->empty()) {
            check(body->find('\n') != std::string::npos,
                  ("corpo de " + r.name + " vem formatado").c_str());
        }
    }

    // --- Corpo das rotinas ---------------------------------------------------
    //
    // O caso que motivou este arquivo. Cobre funcao com argumento, funcao sem
    // argumento e procedure: as tres formas que a montagem manual da
    // assinatura quebrava de jeitos diferentes.
    std::printf("\nload_routine_definition\n");
    auto routines = catalog.load_routines(kSchema);
    check(routines.has_value(), "load_routines aceito pelo servidor");
    if (routines) {
        check(routines->size() == 3, "3 rotinas");

        for (const auto& r : *routines) {
            auto body = catalog.load_routine_definition(kSchema, r.name,
                                                        r.arguments);
            check(body.has_value(),
                  ("consulta de " + r.name + " aceita").c_str());
            check(body && !body->empty(),
                  ("corpo de " + r.name + " nao vazio").c_str());

            // Uma rotina sempre comeca por CREATE; string vazia ou lixo
            // indicaria que a linha voltou de outra funcao.
            if (body && !body->empty()) {
                check(body->rfind("CREATE", 0) == 0,
                      ("corpo de " + r.name + " comeca por CREATE").c_str());
            }
        }
    }

    // --- Tipos ---------------------------------------------------------------
    std::printf("\nload_types\n");
    auto types = catalog.load_types(kSchema);
    check(types.has_value(), "consulta aceita pelo servidor");
    if (types) {
        // O filtro e' o que importa aqui: pg_type tem 16 linhas no schema para
        // estes 3 tipos. Sem os filtros, as tabelas e os arrays entrariam.
        check(types->size() == 3, "3 tipos (tabelas e arrays filtrados)");

        for (const auto& t : *types) {
            if (t.kind == otter::db::TypeKind::enumeration) {
                check(t.enum_values.size() == 3, "enum com 3 valores");
                // enumsortorder, nao ordem alfabetica: 'cancelado' viria
                // primeiro se estivesse ordenado por rotulo.
                check(!t.enum_values.empty() && t.enum_values[0] == "aberto",
                      "enum na ordem de enumsortorder");
            } else if (t.kind == otter::db::TypeKind::composite) {
                check(t.attributes.size() == 3, "composto com 3 campos");
                check(!t.attributes.empty() &&
                          t.attributes[0].name == "logradouro",
                      "campos do composto na ordem de attnum");
            } else if (t.kind == otter::db::TypeKind::domain) {
                check(!t.base_type.empty(), "domain com tipo base");
                check(!t.check_constraint.empty(), "domain com CHECK");
            }
        }
    }

    // --- Filhos de uma tabela ------------------------------------------------
    std::printf("\nfilhos de 'cliente'\n");
    check(catalog.load_columns(kSchema, "cliente").has_value(),  "load_columns");
    check(catalog.load_constraints(kSchema, "cliente").has_value(),
          "load_constraints");
    check(catalog.load_indexes(kSchema, "cliente").has_value(),  "load_indexes");
    check(catalog.load_references(kSchema, "cliente").has_value(),
          "load_references");
    check(catalog.load_triggers(kSchema, "pedido").has_value(),  "load_triggers");
    check(catalog.load_sequences(kSchema).has_value(),           "load_sequences");

    std::printf("\n%d verificacoes, %d falha(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
