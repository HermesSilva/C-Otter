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

    // --- Particoes, eventos e informacao do servidor -------------------------

    std::printf("\nload_partitions\n");
    {
        // Tabela NAO particionada: o information_schema devolve uma linha com
        // PARTITION_NAME nulo, e trata-la como particao criaria um no'
        // fantasma "[null]" na arvore de TODA tabela comum.
        auto none = catalog.load_partitions(kSchema, "cliente");
        check(none.has_value(), "consulta aceita");
        check(none.has_value() && none->empty(),
              "tabela sem particao devolve lista VAZIA");

        // Agora uma tabela particionada de verdade.
        (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.part_probe");
        auto created = (*holt)->execute(
            "CREATE TABLE otter_test.part_probe ("
            "  id INT NOT NULL, ano INT NOT NULL, PRIMARY KEY (id, ano)"
            ") ENGINE=InnoDB "
            "PARTITION BY RANGE (ano) ("
            "  PARTITION p2024 VALUES LESS THAN (2025),"
            "  PARTITION p2025 VALUES LESS THAN (2026),"
            "  PARTITION pfuturo VALUES LESS THAN MAXVALUE)");

        if (created) {
            auto parts = catalog.load_partitions(kSchema, "part_probe");
            check(parts.has_value(), "particoes carregadas");
            check(parts.has_value() && parts->size() == 3, "3 particoes");

            if (parts && parts->size() == 3) {
                check((*parts)[0].name == "p2024", "nome da particao");
                check((*parts)[0].method == "RANGE", "metodo");
                // O MySQL devolve a expressao COM crases: `ano`. Guardamos
                // como vem, porque e' o que o servidor diz -- tirar as crases
                // daria um texto que nao casa com o SHOW CREATE TABLE.
                check((*parts)[0].expression.find("ano") != std::string::npos,
                      "expressao contem a coluna");
                check((*parts)[0].description == "2025", "limite superior");
                check((*parts)[2].description == "MAXVALUE",
                      "particao final e' MAXVALUE");

                // No MySQL a particao e' divisao INTERNA: nao da' para
                // consultar `schema.particao`, e a UI nao pode oferecer.
                check(!(*parts)[0].is_table,
                      "particao do MySQL NAO e' tabela consultavel");
            }
            (void)(*holt)->execute("DROP TABLE otter_test.part_probe");
        }
    }

    std::printf("\nload_events\n");
    {
        auto events = catalog.load_events(kSchema);
        check(events.has_value(), "consulta aceita (pode vir vazia)");

        // Cria um evento para exercitar a leitura. O scheduler pode estar
        // desligado -- o evento e' DEFINIDO de todo jeito, que e' o que o
        // catalogo le'.
        (void)(*holt)->execute("DROP EVENT IF EXISTS otter_test.ev_probe");
        auto created = (*holt)->execute(
            "CREATE EVENT otter_test.ev_probe "
            "ON SCHEDULE EVERY 1 DAY "
            "DO SELECT 1");

        if (created) {
            auto after = catalog.load_events(kSchema);
            check(after.has_value() && !after->empty(), "evento aparece");

            if (after && !after->empty()) {
                const auto* probe = find_by(*after,
                                            [](const auto& e) { return e.name; },
                                            "ev_probe");
                check(probe != nullptr, "evento encontrado pelo nome");
                check(probe != nullptr && probe->type == "RECURRING",
                      "tipo RECURRING");

                // A agenda vem em duas formas excludentes: RECURRING usa
                // INTERVAL, ONE TIME usa EXECUTE_AT. Mostrar o campo vazio do
                // outro tipo nao diria nada.
                check(probe != nullptr &&
                          probe->schedule.find("EVERY 1") != std::string::npos,
                      "agenda montada do intervalo");
                check(probe != nullptr && !probe->definition.empty(),
                      "corpo do evento");
            }
            (void)(*holt)->execute("DROP EVENT otter_test.ev_probe");
        }
    }

    std::printf("\nSystem Info\n");
    {
        // Sessao e servidor sao numeros DIFERENTES: uma sessao recem-aberta
        // tem poucas queries, o servidor tem muitas. Confundi-los levaria a
        // diagnostico errado.
        auto session_status = catalog.load_status(/*global=*/false);
        auto global_status  = catalog.load_status(/*global=*/true);

        check(session_status.has_value() && !session_status->empty(),
              "status da sessao");
        check(global_status.has_value() && !global_status->empty(),
              "status global");

        auto variables = catalog.load_variables(/*global=*/true);
        check(variables.has_value() && !variables->empty(), "variaveis globais");

        if (variables) {
            check(find_by(*variables, [](const auto& v) { return v.name; },
                          "version") != nullptr,
                  "a variavel 'version' esta' la'");
        }

        auto engines = catalog.load_engines();
        check(engines.has_value() && !engines->empty(), "engines");
        if (engines) {
            const auto* innodb = find_by(*engines,
                                         [](const auto& e) { return e.name; },
                                         "InnoDB");
            check(innodb != nullptr, "InnoDB listado");
            check(innodb != nullptr && !innodb->value.empty(),
                  "suporte do engine (DEFAULT/YES/NO)");
        }

        auto charsets = catalog.load_charsets();
        check(charsets.has_value() && !charsets->empty(), "charsets");
        if (charsets) {
            const auto* utf8mb4 = find_by(*charsets,
                                          [](const auto& c) { return c.name; },
                                          "utf8mb4");
            check(utf8mb4 != nullptr, "utf8mb4 listado");

            // MAXLEN e' o que distingue utf8 (3 bytes, NAO cobre emoji) de
            // utf8mb4 (4 bytes) -- a diferenca que mais causa surpresa.
            check(utf8mb4 != nullptr &&
                      utf8mb4->detail.find("4 bytes") != std::string::npos,
                  "utf8mb4 tem 4 bytes por caractere");
        }
    }

    std::printf("\nload_users / load_grants\n");
    {
        auto users = catalog.load_users();
        check(users.has_value(), "consulta aceita");
        check(users.has_value() && !users->empty(),
              "ha' contas (todo servidor tem ao menos uma)");

        if (users && !users->empty()) {
            const auto* root = find_by(*users,
                                       [](const auto& u) { return u.name; },
                                       "root");
            check(root != nullptr, "conta root encontrada");

            // A identidade da conta no MySQL e' o PAR (user, host): existem
            // 'root'@'%' e 'root'@'localhost' com privilegios diferentes.
            check(root != nullptr && !root->host.empty(), "host da conta");
            check(root != nullptr && !root->plugin.empty(),
                  "plugin de autenticacao");

            // O MySQL 8 traz contas de sistema BLOQUEADAS. Se nenhuma
            // aparecesse assim, seria sinal de que account_locked nao foi
            // lida -- e a arvore mostraria como utilizavel o que nao e'.
            bool any_locked = false;
            for (const auto& user : *users) {
                if (user.locked) any_locked = true;
            }
            check(any_locked, "contas de sistema aparecem bloqueadas");

            if (root != nullptr) {
                auto grants = catalog.load_grants(root->name, root->host);
                check(grants.has_value(), "SHOW GRANTS aceito");
                check(grants.has_value() && !grants->empty(),
                      "root tem privilegios");

                if (grants && !grants->empty()) {
                    check((*grants)[0].find("GRANT") != std::string::npos,
                          "o texto do GRANT vem inteiro");
                }
            }
        }
    }

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
