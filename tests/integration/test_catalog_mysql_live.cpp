// Executa as consultas de catalogo do MySQL contra um servidor de verdade.
//
// Mesmo motivo do test_catalog_live.cpp: uma consulta sintaticamente valida
// em C++ pode ser rejeitada pelo servidor, e nenhum teste unitario pega isso.
// No PostgreSQL foi o load_routine_definition, que ficou "pronto" por semanas
// montando uma assinatura que o servidor recusava.
//
// Pre-requisito -- criar o banco otter_test:
//
//     mysql -h localhost -u root -p < tests/integration/fixtures_mysql.sql
//
//     otter_tests_mysql_live <host> <port> <db> <user> <pass>
//
// Sem argumentos, usa MYSQL_HOST/MYSQL_PORT/MYSQL_USER/MYSQL_PASSWORD.
#include "db/catalog_mysql.hpp"
#include "db/drivers/mysql.hpp"

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

template <typename T, typename Fn>
const T* find_by(const std::vector<T>& items, Fn&& name_of, std::string_view wanted) {
    for (const T& item : items) {
        if (name_of(item) == wanted) return &item;
    }
    return nullptr;
}

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
        config.host     = env_or("MYSQL_HOST", "localhost");
        config.port     = static_cast<std::uint16_t>(
                              std::atoi(env_or("MYSQL_PORT", "3306").c_str()));
        config.database = env_or("MYSQL_DATABASE", kSchema);
        config.user     = env_or("MYSQL_USER", "root");
        config.password = env_or("MYSQL_PASSWORD", "");
    }

    auto holt = otter::db::mysql_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexao falhou: %s\n",
                     holt.error().to_string().c_str());
        return 2;
    }

    otter::db::MysqlCatalog catalog(**holt);
    std::printf("Servidor: %s%s\n\n", (*holt)->server_version().c_str(),
                catalog.is_mariadb() ? " (MariaDB)" : "");

    // --- Bancos ---------------------------------------------------------------

    std::printf("load_schemas\n");
    {
        auto schemas = catalog.load_schemas();
        check(schemas.has_value(), "consulta aceita pelo servidor");
        if (schemas) {
            check(find_by(*schemas, [](const auto& s) { return s.name; },
                          kSchema) != nullptr,
                  "otter_test aparece");

            // Os bancos internos ficam de fora, senao a arvore fica cheia de
            // instrumentacao que ninguem navega.
            check(find_by(*schemas, [](const auto& s) { return s.name; },
                          "information_schema") == nullptr,
                  "information_schema nao aparece");
            check(find_by(*schemas, [](const auto& s) { return s.name; },
                          "performance_schema") == nullptr,
                  "performance_schema nao aparece");
        }
    }

    // --- Tabelas e views ------------------------------------------------------

    std::printf("\nload_tables\n");
    {
        auto tables = catalog.load_tables(kSchema);
        check(tables.has_value(), "consulta aceita");
        if (tables) {
            const auto* cliente = find_by(*tables, [](const auto& t) { return t.name; },
                                          "cliente");
            check(cliente != nullptr, "tabela cliente encontrada");
            if (cliente) {
                check(cliente->kind == otter::db::ObjKind::table, "cliente e' tabela");
                check(cliente->comment == "Clientes do sistema",
                      "comentario da tabela veio");
                check(!cliente->size_pretty.empty(), "tamanho calculado");
            }

            const auto* view = find_by(*tables, [](const auto& t) { return t.name; },
                                       "cliente_ativo");
            check(view != nullptr && view->kind == otter::db::ObjKind::view,
                  "cliente_ativo e' view");

            // Uma view nao tem constraints -- as pastas nao devem aparecer.
            if (view) check(!view->has_constraints(), "view nao oferece constraints");
        }
    }

    // --- Colunas --------------------------------------------------------------

    std::printf("\nload_columns\n");
    {
        auto columns = catalog.load_columns(kSchema, "cliente");
        check(columns.has_value(), "consulta aceita");
        if (columns) {
            check(columns->size() == 8, "8 colunas");

            const auto* id = find_by(*columns, [](const auto& c) { return c.name; }, "id");
            check(id != nullptr && id->primary_key, "id e' chave primaria");

            // EXTRA carrega o auto_increment. Sem ele o DDL gerado criaria uma
            // tabela sem geracao de id.
            check(id != nullptr &&
                      id->default_value.find("auto_increment") != std::string::npos,
                  "auto_increment preservado");

            const auto* nome = find_by(*columns, [](const auto& c) { return c.name; },
                                       "nome");
            check(nome != nullptr && !nome->nullable, "nome e' NOT NULL");
            check(nome != nullptr && nome->comment == "Razao social",
                  "comentario da coluna veio");
            check(nome != nullptr && nome->type_name == "varchar(120)",
                  "tipo COMPLETO, com tamanho");

            // ENUM precisa trazer os valores no tipo -- e' onde eles vivem no
            // MySQL, que nao tem tipo nomeado.
            const auto* situacao = find_by(*columns, [](const auto& c) { return c.name; },
                                           "situacao");
            check(situacao != nullptr &&
                      situacao->type_name.find("'suspenso'") != std::string::npos,
                  "valores do ENUM no tipo");

            // BLOB e TEXT tem o mesmo tipo no protocolo; aqui vem do
            // information_schema e precisam sair diferentes.
            const auto* anexo = find_by(*columns, [](const auto& c) { return c.name; },
                                        "anexo");
            check(anexo != nullptr && anexo->kind == otter::db::DataKind::binary,
                  "BLOB classificado como binario");

            const auto* observacao = find_by(*columns,
                                             [](const auto& c) { return c.name; },
                                             "observacao");
            check(observacao != nullptr &&
                      observacao->kind == otter::db::DataKind::string,
                  "TEXT classificado como texto");

            const auto* limite = find_by(*columns, [](const auto& c) { return c.name; },
                                         "limite");
            check(limite != nullptr && limite->kind == otter::db::DataKind::numeric,
                  "DECIMAL classificado como numerico");
        }
    }

    // --- Constraints ----------------------------------------------------------

    std::printf("\nload_constraints\n");
    {
        auto constraints = catalog.load_constraints(kSchema, "cliente");
        check(constraints.has_value(), "consulta aceita");
        if (constraints) {
            const auto* pk = find_by(*constraints, [](const auto& c) { return c.name; },
                                     "PRIMARY");
            check(pk != nullptr, "chave primaria encontrada");
            check(pk != nullptr && pk->kind == otter::db::ObjKind::primary_key,
                  "classificada como primary key");
            check(pk != nullptr && pk->columns == "id", "coluna da PK");

            const auto* uq = find_by(*constraints, [](const auto& c) { return c.name; },
                                     "uq_cliente_documento");
            check(uq != nullptr && uq->kind == otter::db::ObjKind::unique_key,
                  "constraint unica encontrada");
        }

        // Chave primaria COMPOSTA: a ordem das colunas define que consultas o
        // indice atende, e embaralha-la daria um DDL que nao reproduz a tabela.
        auto composite = catalog.load_constraints(kSchema, "pedido_item");
        if (composite) {
            const auto* pk = find_by(*composite, [](const auto& c) { return c.name; },
                                     "PRIMARY");
            check(pk != nullptr && pk->columns == "pedido_id, sequencia",
                  "PK composta na ordem declarada");
        }

        // Tabela sem PK: a lista vem vazia, e e' isso que faz a grade recusar
        // a edicao com uma explicacao em vez de fingir que salvou.
        auto none = catalog.load_constraints(kSchema, "registro_sem_pk");
        check(none.has_value() && none->empty(), "tabela sem PK devolve lista vazia");
    }

    // --- Indices --------------------------------------------------------------

    std::printf("\nload_indexes\n");
    {
        auto indexes = catalog.load_indexes(kSchema, "cliente");
        check(indexes.has_value(), "consulta aceita");
        if (indexes) {
            const auto* primary = find_by(*indexes, [](const auto& i) { return i.name; },
                                          "PRIMARY");
            check(primary != nullptr && primary->primary, "indice da PK marcado");
            check(primary != nullptr && primary->unique, "indice da PK e' unico");

            const auto* composite = find_by(*indexes,
                                            [](const auto& i) { return i.name; },
                                            "ix_cliente_nome_situacao");
            check(composite != nullptr, "indice composto encontrado");
            check(composite != nullptr && composite->columns == "nome, situacao",
                  "colunas do indice na ordem de SEQ_IN_INDEX");
            check(composite != nullptr && !composite->unique,
                  "indice nao unico marcado corretamente");
            check(composite != nullptr && composite->method == "BTREE",
                  "metodo do indice");
        }
    }

    // --- Chaves estrangeiras --------------------------------------------------

    std::printf("\nload_table_foreign_keys\n");
    {
        auto keys = catalog.load_table_foreign_keys(kSchema, "pedido");
        check(keys.has_value(), "consulta aceita");
        if (keys) {
            const auto* fk = find_by(*keys, [](const auto& k) { return k.name; },
                                     "fk_pedido_cliente");
            check(fk != nullptr, "FK encontrada");
            check(fk != nullptr && fk->target_table == "cliente", "tabela destino");
            check(fk != nullptr && fk->target_column == "id", "coluna destino");
            check(fk != nullptr && fk->on_delete == "CASCADE", "regra ON DELETE");
            check(fk != nullptr && fk->on_update == "RESTRICT", "regra ON UPDATE");
        }
    }

    std::printf("\nload_references\n");
    {
        // "Quem depende desta tabela?" -- o que mais falta num cliente SQL.
        auto refs = catalog.load_references(kSchema, "cliente");
        check(refs.has_value(), "consulta aceita");
        check(refs.has_value() && !refs->empty(), "pedido aponta para cliente");
        if (refs && !refs->empty()) {
            check(find_by(*refs, [](const auto& k) { return k.source_table; },
                          "pedido") != nullptr,
                  "origem da referencia identificada");
        }
    }

    // --- Triggers -------------------------------------------------------------

    std::printf("\nload_triggers\n");
    {
        auto triggers = catalog.load_triggers(kSchema, "pedido");
        check(triggers.has_value(), "consulta aceita");
        if (triggers) {
            const auto* trigger = find_by(*triggers,
                                          [](const auto& t) { return t.name; },
                                          "trg_pedido_antes_inserir");
            check(trigger != nullptr, "trigger encontrada");
            check(trigger != nullptr && trigger->timing == "BEFORE", "momento");
            check(trigger != nullptr && trigger->events == "INSERT", "evento");
            check(trigger != nullptr && !trigger->definition.empty(), "corpo veio");
        }
    }

    // --- Rotinas --------------------------------------------------------------

    std::printf("\nload_routines\n");
    {
        auto routines = catalog.load_routines(kSchema);
        check(routines.has_value(), "consulta aceita");
        if (routines) {
            const auto* procedure = find_by(*routines,
                                            [](const auto& r) { return r.name; },
                                            "sp_total_do_cliente");
            check(procedure != nullptr, "procedure encontrada");
            check(procedure != nullptr &&
                      procedure->kind == otter::db::ObjKind::procedure,
                  "classificada como procedure");
            check(procedure != nullptr && !procedure->arguments.empty(),
                  "parametros carregados");

            // O modo OUT precisa aparecer: sem ele o usuario nao sabe que o
            // parametro devolve valor.
            check(procedure != nullptr &&
                      procedure->arguments.find("OUT") != std::string::npos,
                  "modo OUT do parametro visivel");

            const auto* function = find_by(*routines,
                                           [](const auto& r) { return r.name; },
                                           "fn_limite_disponivel");
            check(function != nullptr &&
                      function->kind == otter::db::ObjKind::function,
                  "funcao classificada como funcao");
            check(function != nullptr && !function->return_type.empty(),
                  "tipo de retorno da funcao");
        }
    }

    std::printf("\nload_routine_definition\n");
    {
        // Este e' o caminho que ficou quebrado por semanas no PostgreSQL.
        auto body = catalog.load_routine_definition(kSchema, "sp_total_do_cliente", "");
        check(body.has_value(), "definicao da procedure obtida");
        if (body) {
            check(body->find("COALESCE") != std::string::npos,
                  "corpo contem o codigo real");
        }

        auto function = catalog.load_routine_definition(kSchema,
                                                        "fn_limite_disponivel", "");
        check(function.has_value(), "definicao da funcao obtida");
        if (function) {
            check(function->find("RETURN") != std::string::npos,
                  "corpo da funcao contem RETURN");
        }
    }

    std::printf("\nload_view_definition\n");
    {
        auto definition = catalog.load_view_definition(kSchema, "cliente_ativo");
        check(definition.has_value(), "definicao da view obtida");
        if (definition) {
            check(definition->find("select") != std::string::npos ||
                      definition->find("SELECT") != std::string::npos,
                  "corpo da view e' um SELECT");
        }
    }

    // --- Sequences e tipos ----------------------------------------------------

    std::printf("\nload_sequences / load_types\n");
    {
        // Em MySQL nao ha' sequences. Devolver vazio (e nao erro) e' o que faz
        // a pasta simplesmente nao aparecer na arvore.
        auto sequences = catalog.load_sequences(kSchema);
        check(sequences.has_value(), "sequences nao dao erro em MySQL");
        if (!catalog.is_mariadb()) {
            check(sequences.has_value() && sequences->empty(),
                  "MySQL nao tem sequences");
        }

        auto types = catalog.load_types(kSchema);
        check(types.has_value() && types->empty(),
              "MySQL nao tem tipos definidos pelo usuario");
    }

    // --- Origem da coluna, para a grade editavel -------------------------------

    std::printf("\norigem das colunas (grade editavel)\n");
    {
        auto rs = (*holt)->query("SELECT c.nome AS razao, c.id FROM otter_test.cliente c");
        check(rs.has_value(), "consulta com apelido aceita");
        if (rs) {
            const otter::db::ColumnInfo& first = rs->column(0).info();

            // O apelido e' o que aparece na grade; a tabela e a coluna REAIS
            // sao o que o UPDATE precisa. Um UPDATE contra o apelido "c" nao
            // existe.
            check(first.name == "razao", "apelido na grade");
            check(first.source_table == "cliente", "tabela REAL, nao o apelido");
            check(first.source_column_name == "nome", "coluna REAL, nao o apelido");
            check(first.source_schema == "otter_test", "banco de origem");
            check(first.has_source(), "coluna tem origem identificada");
        }

        // Expressao nao tem origem -- e' o que faz a grade recusar a edicao.
        auto expression = (*holt)->query("SELECT COUNT(*) FROM otter_test.cliente");
        if (expression) {
            check(!expression->column(0).info().has_source(),
                  "agregado nao tem tabela de origem");
        }
    }

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
