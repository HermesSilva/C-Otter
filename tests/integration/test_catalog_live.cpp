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
#include "live_connect.hpp"

#include "db/catalog.hpp"
#include "db/connection_store.hpp"
#include "db/drivers/postgres.hpp"
#include "sql/paging.hpp"

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

        // Sem PGPASSWORD, a senha vem do perfil PostgreSQL salvo no C-Otter.
        // Sem isso a suite so' rodava com a senha na linha de comando ou no
        // ambiente -- e ficou dias sem rodar porque ninguem a tinha a mao.
        // Host, porta e usuario do perfil prevalecem; o banco continua sendo
        // PGDATABASE (ou ERP_TID), porque e' la' que as fixtures estao.
        if (config.password.empty()) {
            auto profiles =
                otter::db::load_profiles(otter::db::otter_store_location());
            if (profiles) {
                for (const auto& stored : *profiles) {
                    if (stored.profile.driver_id != "postgresql") continue;
                    if (stored.profile.password.empty()) continue;
                    // So' perfil LOCAL. Sem este filtro a suite pegou o
                    // primeiro perfil com senha -- que era o de producao.
                    if (!live::is_local_host(stored.profile.host)) continue;
                    const std::string database = config.database;
                    config          = stored.profile.to_conn_config();
                    config.database = database;
                    std::printf("senha do perfil salvo \"%s\"\n",
                                stored.profile.effective_name().c_str());
                    break;
                }
            }
        }
    }

    // O host FINAL, venha de onde vier (argumentos, ambiente, perfil).
    live::require_local(config);

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

    std::size_t tables = 0, views = 0, mviews = 0, partitioned = 0, foreign = 0;
    bool partition_listed = false;
    for (const auto& r : *relations) {
        if (r.kind == otter::db::ObjKind::table)                  ++tables;
        else if (r.kind == otter::db::ObjKind::view)              ++views;
        else if (r.kind == otter::db::ObjKind::materialized_view) ++mviews;
        else if (r.kind == otter::db::ObjKind::partitioned_table) ++partitioned;
        else if (r.kind == otter::db::ObjKind::foreign_table)     ++foreign;
        partition_listed |= r.name.rfind("lancamento_", 0) == 0;
    }
    check(tables == 5,
          "5 tabelas (cliente, pedido, evento_volume, documento, documento_fiscal)");
    check(foreign == 1, "1 foreign table (importacao)");
    for (const auto& r : *relations) {
        if (r.name == "lancamento") {
            // A tabela-mae nao guarda nada; o tamanho e' a soma das particoes.
            check(r.size_bytes > 0 && !r.size_pretty.empty(),
                  "particionada mostra o tamanho das particoes, nao zero");
        }
        if (r.kind == otter::db::ObjKind::view || r.is_foreign()) {
            check(r.size_bytes < 0 && r.size_pretty.empty(),
                  ("sem tamanho ao lado de " + r.name).c_str());
        }
        if (r.name == "evento_volume") {
            // O formato e' o da coluna de tamanho do DBeaver ("209M"), nao o
            // de pg_size_pretty ("209 MB").
            check(r.size_bytes > 100 * 1024 * 1024 && r.size_pretty.back() == 'M' &&
                      r.size_pretty.find(' ') == std::string::npos,
                  "tamanho da tabela em bytes e no formato curto");
        }
    }
    check(views  == 2, "2 views");
    check(mviews == 1, "1 materialized view");
    check(partitioned == 1, "1 tabela particionada (lancamento)");
    // O DBeaver esconde as particoes da pasta Tables -- elas aparecem so'
    // dentro da tabela-mae. Lista-las nos dois lugares duplicava a arvore.
    check(!partition_listed, "particoes FORA da lista de tabelas");

    std::printf("\nload_partitions\n");
    {
        auto partitions = catalog.load_partitions(kSchema, "lancamento");
        check(partitions.has_value(), "consulta aceita pelo servidor");
        check(partitions && partitions->size() == 2, "2 particoes");
        check(partitions && !partitions->empty() &&
                  (*partitions)[0].name == "lancamento_2025" &&
                  (*partitions)[0].method == "RANGE" &&
                  (*partitions)[0].description.find("FOR VALUES FROM (2025)") == 0,
              "nome, metodo e limites da primeira particao");
        auto plain = catalog.load_partitions(kSchema, "cliente");
        check(plain && plain->empty(), "tabela comum nao tem particoes");
    }

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
        // 3 originais + fn_dividir + fn_evento_ddl + soma_total (agregada) +
        // as duas do file_fdw, que nasce dentro do schema.
        check(routines->size() == 8, "8 rotinas");

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

    // --- Paginacao (ADR 0011) ------------------------------------------------
    //
    // Contra a tabela de 2 milhoes de linhas. Os testes unitarios provam que a
    // reescrita produz o texto certo; so' aqui se prova que o servidor aceita
    // esse texto e devolve a pagina pedida.
    std::printf("\npaginacao sobre evento_volume\n");
    {
        const std::string base =
            "SELECT evento_id FROM otter_test.evento_volume ORDER BY evento_id";

        const otter::sql::PagedQuery first = otter::sql::make_paged_query(
            base, otter::sql::postgres_dialect(), 0);
        check(first.rewritten, "consulta reescrita");

        auto page0 = (*holt)->query(first.sql);
        check(page0.has_value(), "servidor aceita a consulta reescrita");
        if (page0) {
            // 201, nao 200: a linha-sonda diz que ha' proxima pagina.
            check(page0->row_count() == otter::sql::kDefaultPageSize + 1,
                  "primeira pagina traz a linha-sonda");
            check(!page0->text(0, 0).empty() && page0->text(0, 0) == "1",
                  "primeira pagina comeca na linha 1");
        }

        const otter::sql::PagedQuery third = otter::sql::make_paged_query(
            base, otter::sql::postgres_dialect(), 2);
        auto page2 = (*holt)->query(third.sql);
        check(page2.has_value(), "terceira pagina aceita");
        if (page2) {
            // OFFSET 400 -> primeira linha e' a de id 401.
            check(page2->text(0, 0) == "401", "OFFSET posiciona a pagina certa");
        }

        // A ultima pagina nao tem linha-sonda: e' assim que a UI sabe que
        // chegou ao fim e desabilita "proxima".
        const std::size_t last_page =
            (2000000 / otter::sql::kDefaultPageSize) - 1;
        const otter::sql::PagedQuery last = otter::sql::make_paged_query(
            base, otter::sql::postgres_dialect(), last_page);
        auto tail = (*holt)->query(last.sql);
        check(tail.has_value(), "ultima pagina aceita");
        if (tail) {
            check(tail->row_count() == otter::sql::kDefaultPageSize,
                  "ultima pagina sem linha-sonda");
        }
    }

    // --- Arvore unica (ADR 0018) --------------------------------------------
    using otter::db::CatalogItem;
    using otter::db::CatalogList;

    // Carrega a lista e exige que o servidor ACEITE a consulta. `expected`
    // vazio = so' aceitar; senao, o item precisa estar la'.
    auto list_has = [&](const char* label, CatalogList list,
                        std::string_view expected, std::string_view a = {},
                        std::string_view b = {}, std::string_view c = {})
        -> std::vector<CatalogItem> {
        auto items = catalog.load_list(list, a, b, c);
        if (!items) {
            std::printf("    %s\n", items.error().to_string().c_str());
            check(false, (std::string(label) + ": consulta aceita").c_str());
            return {};
        }
        check(true, (std::string(label) + ": consulta aceita (" +
                     std::to_string(items->size()) + ")").c_str());
        if (!expected.empty()) {
            bool found = false;
            for (const CatalogItem& item : *items) {
                found |= item.name == expected;
            }
            check(found, (std::string(label) + ": tem " +
                          std::string(expected)).c_str());
        }
        return *items;
    };

    std::printf("\nload_databases\n");
    {
        auto plain = catalog.load_databases(false, false);
        check(plain.has_value(), "consulta aceita pelo servidor");
        bool current = false, has_template = false, sized = false;
        if (plain) {
            for (const auto& db : *plain) {
                current      |= db.name == config.database;
                has_template |= db.is_template;
                if (db.name == config.database) {
                    sized = db.size_bytes > 0 && !db.size_pretty.empty();
                }
            }
        }
        check(current, "o banco conectado esta' na lista");
        check(!has_template, "templates FORA por padrao");
        check(sized, "tamanho do banco conectado medido");

        auto with_templates = catalog.load_databases(true, true);
        bool template0 = false;
        if (with_templates) {
            for (const auto& db : *with_templates) {
                template0 |= db.name == "template0" && db.is_template &&
                             !db.allow_connect;
            }
        }
        check(template0, "template0 aparece quando pedido, sem aceitar conexao");
    }

    std::printf("\nformat_size\n");
    check(otter::db::format_size(-1).empty(), "desconhecido fica vazio");
    check(otter::db::format_size(512) == "512", "bytes sem unidade");
    check(otter::db::format_size(8073216) == "7.7M", "7.7M como no DBeaver");
    check(otter::db::format_size(298844160) == "285M", "285M sem casa decimal");

    std::printf("\nlistas por schema\n");
    list_has("Indexes (schema)", CatalogList::schema_indexes, "cliente_pkey", kSchema);
    list_has("Aggregate functions", CatalogList::aggregates,
             "soma_total(numeric)", kSchema);

    std::printf("\nlistas por relacao\n");
    list_has("Dependencies de cliente", CatalogList::dependencies, "",
             kSchema, "cliente");
    list_has("Dependencies de view", CatalogList::dependencies, "",
             kSchema, "vw_cliente_ativo");
    {
        // A politica de RLS depende da tabela, e precisa aparecer com NOME:
        // o pg_depend aponta para o pg_policy, que a consulta do DBeaver nao
        // junta -- a linha saia em branco.
        const auto deps = list_has("Dependencies de documento",
                                   CatalogList::dependencies,
                                   "pl_documento_leitura", kSchema, "documento");
        bool unnamed = false;
        for (const CatalogItem& item : deps) unnamed |= item.name.empty();
        check(!unnamed, "nenhuma dependencia sem nome");
    }
    list_has("Rules", CatalogList::rules, "rl_documento_noop", kSchema, "documento");
    list_has("Policies", CatalogList::policies, "pl_documento_leitura",
             kSchema, "documento");
    list_has("Child tables", CatalogList::child_tables, "documento_fiscal",
             kSchema, "documento");
    {
        // A particionada NAO lista as particoes como filhas por heranca:
        // elas tem pasta propria, e apareceriam em dois lugares.
        auto children = catalog.load_list(CatalogList::child_tables, kSchema,
                                          "lancamento");
        check(children && children->empty(),
              "particoes nao aparecem em Child tables");
    }

    std::printf("\nlistas por rotina\n");
    {
        const auto params = list_has(
            "Function parameters", CatalogList::routine_parameters, "p_a",
            kSchema, "fn_dividir",
            "p_a integer, p_b integer, OUT quociente integer, OUT resto integer");
        check(params.size() == 4, "4 parametros");
        check(params.size() == 4 && params[0].tooltip == "IN" &&
                  params[2].name == "quociente" && params[2].tooltip == "OUT" &&
                  params[2].flag,
              "modo IN/OUT de cada parametro");
        check(params.size() == 4 && params[0].detail == "integer",
              "tipo do parametro");

        const auto simple = list_has(
            "parametros so' de entrada", CatalogList::routine_parameters,
            "p_cliente", kSchema, "fn_credito_disponivel", "p_cliente integer");
        check(simple.size() == 1, "1 parametro");

        list_has("Dependencies de funcao", CatalogList::routine_dependencies, "",
                 kSchema, "fn_pedido_auditoria", "");
    }
    {
        // O corpo da agregada: pg_get_functiondef a recusa.
        auto body = catalog.load_routine_definition(kSchema, "soma_total",
                                                    "numeric");
        check(body && body->rfind("CREATE AGGREGATE", 0) == 0 &&
                  body->find("SFUNC = numeric_add") != std::string::npos,
              "definicao da funcao agregada");
    }

    std::printf("\nlistas por banco\n");
    {
        const auto triggers = list_has("Event Triggers",
                                       CatalogList::event_triggers,
                                       "otter_evento_ddl");
        for (const CatalogItem& item : triggers) {
            if (item.name == "otter_evento_ddl") {
                check(!item.flag, "event trigger desabilitado marcado como tal");
                check(item.detail == "ddl_command_start", "evento do trigger");
            }
        }
    }
    list_has("Extensions", CatalogList::extensions, "file_fdw");
    list_has("Tablespaces", CatalogList::tablespaces, "pg_default");
    list_has("Foreign data wrappers", CatalogList::foreign_data_wrappers,
             "file_fdw");
    list_has("Foreign servers", CatalogList::foreign_servers, "otter_arquivos");
    list_has("User Mappings", CatalogList::user_mappings, "public",
             "otter_arquivos");
    list_has("Settings", CatalogList::settings, "max_connections");
    {
        const auto roles = list_has("Roles", CatalogList::roles, config.user);
        for (const CatalogItem& role : roles) {
            if (role.name == config.user) {
                check(role.flag, "o usuario conectado pode fazer login");
            }
            if (role.name == "pg_monitor") {
                check(!role.flag, "pg_monitor e' grupo, sem login");
            }
        }
    }
    // pg_monitor e' membro de pg_read_all_settings desde o PostgreSQL 10.
    list_has("Members", CatalogList::role_members, "pg_monitor",
             "pg_read_all_settings");
    list_has("Roles (belongs)", CatalogList::role_belongs,
             "pg_read_all_settings", "pg_monitor");

    std::printf("\nlistas por servidor\n");
    list_has("Access Methods", CatalogList::access_methods, "btree");
    list_has("Operator classes", CatalogList::operator_classes, "int4_ops", "btree");
    list_has("Operator families", CatalogList::operator_families, "integer_ops",
             "btree");
    list_has("Encodings", CatalogList::encodings, "UTF8");
    list_has("Collations", CatalogList::collations, "C");
    list_has("Languages", CatalogList::languages, "plpgsql");
    list_has("Available Extensions", CatalogList::available_extensions,
             "file_fdw");
    {
        // Sem pgAgent a lista e' VAZIA, nao erro.
        auto jobs = catalog.load_list(CatalogList::jobs);
        check(jobs.has_value(), "Jobs: sem pgAgent devolve vazio, nao erro");
        auto steps = catalog.load_list(CatalogList::job_steps, "1; DROP TABLE x");
        check(steps && steps->empty(), "Job steps: id nao numerico e' recusado");
    }

    std::printf("\nsaida do servidor (NOTICE)\n");
    {
        // O painel "Show server output" depende disto: os NoticeResponse eram
        // lidos pelo protocolo e descartados.
        (void)(*holt)->take_server_output();
        auto status = (*holt)->execute(
            "DO $$ BEGIN RAISE NOTICE 'otter %', 42; END $$");
        check(status.has_value(), "bloco com RAISE NOTICE aceito");

        const std::vector<std::string> output = (*holt)->take_server_output();
        // A severidade vem no idioma do servidor (lc_messages): "NOTICE" num,
        // "NOTA" noutro. O que se confere e' a forma "<severidade>: <texto>".
        check(output.size() == 1 && output.front().ends_with(": otter 42") &&
                  output.front().size() > std::string(": otter 42").size(),
              "o NOTICE chega com a severidade e a mensagem");
        check((*holt)->take_server_output().empty(),
              "a leitura esvazia a lista");
        check((*holt)->reports_server_output(), "o driver declara que reporta");

        // Um aviso de comando comum tambem: DROP IF EXISTS do que nao existe.
        (void)(*holt)->execute("DROP TABLE IF EXISTS otter_test.nao_existe_xyz");
        check(!(*holt)->take_server_output().empty(),
              "DROP IF EXISTS de tabela ausente gera aviso");
    }

    std::printf("\n%d verificacoes, %d falha(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
