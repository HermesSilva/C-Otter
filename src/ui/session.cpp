#include "ui/session.hpp"

#include "base/i18n.hpp"
#include "db/catalog_reader.hpp"
#include "db/ddl.hpp"
#include "db/registry.hpp"
#include "sql/dialect.hpp"
#include "sql/paging.hpp"

#include <utility>

namespace otter::ui {
namespace {

// Chave de uma lista no mapa. O separador e' um caractere de controle, que
// nao aparece em nome de objeto -- com '.', a lista (a="x.y", b="") colidiria
// com (a="x", b="y").
std::string list_key(db::CatalogList list, std::string_view a,
                     std::string_view b, std::string_view c) {
    std::string key = std::to_string(static_cast<int>(list));
    for (std::string_view part : {a, b, c}) {
        key.push_back('\x1f');
        key.append(part);
    }
    return key;
}

// Schemas com as tabelas de cada um, e as FKs do schema padrao. Fator comum
// da conexao e do "Refresh".
struct CatalogSnapshot {
    std::vector<db::SchemaMeta>     schemas;
    std::vector<db::ForeignKeyMeta> keys;
    std::vector<db::DatabaseMeta>   databases;
    std::string                     error;
    std::size_t                     table_count = 0;
};

CatalogSnapshot read_catalog(db::CatalogReader& catalog,
                             const std::string& database, bool templates,
                             bool unavailable) {
    CatalogSnapshot snapshot;

    auto loaded = catalog.load_schemas();
    if (!loaded) {
        snapshot.error = loaded.error().to_string();
        return snapshot;
    }
    snapshot.schemas = std::move(*loaded);

    for (db::SchemaMeta& schema : snapshot.schemas) {
        auto tables = catalog.load_tables(schema.name);
        if (tables) {
            schema.tables = std::move(*tables);
            schema.tables_loaded = true;
            snapshot.table_count += schema.tables.size();
        }
    }

    // As foreign keys alimentam a inferencia de JOIN do completion
    // (ADR 0004, camada 4). O schema de onde le-las depende do SGBD:
    // "public" no PostgreSQL, o banco da conexao no MySQL -- pedir
    // "public" a um MySQL simplesmente nao acharia nada.
    std::string fk_schema = catalog.default_schema();
    if (fk_schema.empty()) fk_schema = database;

    auto fks = catalog.load_foreign_keys(fk_schema);
    if (fks) snapshot.keys = std::move(*fks);

    // A lista de bancos vem junto: e' o segundo nivel da arvore, e ela
    // vazia por meio segundo depois de conectar pareceria falha. Uma recusa
    // aqui nao derruba a conexao -- a arvore mostra so' o banco conectado.
    if (catalog.has_database_level()) {
        auto databases = catalog.load_databases(templates, unavailable);
        if (databases) snapshot.databases = std::move(*databases);
    }
    return snapshot;
}

} // namespace

Session::Session() = default;

Session::~Session() {
    join_worker();
    // holt_ e' destruido depois do join: o worker pode estar usando a conexao.
}

void Session::join_worker() {
    if (worker_.joinable()) worker_.join();
}

void Session::connect_async(const db::ConnConfig& config) {
    if (busy_.load(std::memory_order_acquire)) return;

    join_worker();   // garante que nao ha' worker anterior vivo

    busy_.store(true, std::memory_order_release);
    state_.store(SessionState::connecting, std::memory_order_release);

    // Copiadas sob o lock para o worker: ele nao pode ler os membros depois,
    // com a UI podendo troca-los.
    bool list_templates   = false;
    bool list_unavailable = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = TRF("connecting to %s:%u...", config.host.c_str(),
                              static_cast<unsigned>(config.port));
        database_name_  = config.database;
        driver_id_      = config.driver_id;
        engine_.store(config.driver_id == "mysql" || config.driver_id == "mariadb"
                          ? Engine::mysql
                      : config.driver_id == "sqlserver" || config.driver_id == "mssql"
                          ? Engine::mssql
                      : config.driver_id == "sqlanywhere"
                          ? Engine::sqlanywhere
                      : config.driver_id == "oracle"
                          ? Engine::oracle
                          : Engine::postgres,
                      std::memory_order_release);
        schemas_.clear();
        foreign_keys_.clear();
        databases_.clear();
        lists_.clear();
        list_templates   = list_templates_;
        list_unavailable = list_unavailable_;
    }

    worker_ = std::thread([this, profile_config = config, list_templates,
                           list_unavailable] {
        db::ConnConfig config = profile_config;
        db::Driver* driver = db::find_driver(config.driver_id);
        if (driver == nullptr) {
            // Driver desconhecido nao cai no padrao: conectar a um MySQL
            // falando o protocolo do PostgreSQL daria um erro de protocolo
            // que nao ajuda ninguem.
            const std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = TRF("no driver for '%s'", config.driver_id.c_str());
            state_.store(SessionState::failed, std::memory_order_release);
            busy_.store(false, std::memory_order_release);
            return;
        }

        // O dialeto do SQL gerado e' POR THREAD (db/ddl.cpp): este worker
        // gera para o SGBD dele, e o thread de UI decide o seu a cada quadro,
        // pela conexao em uso. Era um global so', que ficava com o da ultima
        // conexao aberta.
        db::set_sql_dialect_for(config.driver_id);

        // Tunel SSH: aberto ANTES da conexao, que entao vai para a ponta
        // local dele. O nome do banco e o resto do perfil nao mudam.
        std::unique_ptr<db::SshTunnel> tunnel;
        if (config.ssh.enabled()) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                status_message_ = TRF("opening the SSH tunnel to %s...",
                                      config.ssh.host.c_str());
            }
            auto opened = db::SshTunnel::open(config.ssh, config.host, config.port);
            if (!opened) {
                const std::lock_guard<std::mutex> lock(mutex_);
                status_message_ = opened.error().to_string();
                state_.store(SessionState::failed, std::memory_order_release);
                busy_.store(false, std::memory_order_release);
                return;
            }
            tunnel = std::make_unique<db::SshTunnel>(std::move(*opened));
            config.host = "127.0.0.1";
            config.port = tunnel->local_port();
        }

        auto connection = driver->connect(config);

        if (!connection) {
            const std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = connection.error().to_string();
            state_.store(SessionState::failed, std::memory_order_release);
            busy_.store(false, std::memory_order_release);
            return;
        }

        std::unique_ptr<db::Holt> holt = std::move(*connection);

        // Role, schema padrao, consultas de inicializacao, somente leitura e
        // auto-commit do perfil. Uma que falha derruba a conexao nomeando-se:
        // uma sessao que ignorou "somente leitura" em silencio e' o pior
        // resultado possivel.
        if (Status setup = db::apply_session_setup(*holt, config); !setup) {
            const std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = setup.error().to_string();
            state_.store(SessionState::failed, std::memory_order_release);
            busy_.store(false, std::memory_order_release);
            return;
        }

        // Ja' que estamos no worker, carrega o catalogo antes de liberar a UI:
        // uma arvore vazia por meio segundo pareceria falha de conexao.
        std::unique_ptr<db::CatalogReader> catalog =
            db::make_catalog_reader(config.driver_id, *holt);

        if (catalog == nullptr) {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt_           = std::move(holt);
            tunnel_         = std::move(tunnel);
            status_message_ = TRF("connected, but there is no catalog reader for '%s'",
                                  config.driver_id.c_str());
            state_.store(SessionState::connected, std::memory_order_release);
            busy_.store(false, std::memory_order_release);
            return;
        }

        const bool sequences  = catalog->has_sequences();
        const bool user_types = catalog->has_user_types();
        const bool events      = catalog->has_events();
        const bool server_info = catalog->has_server_info();
        const bool users       = catalog->has_users();
        const bool database_level = catalog->has_database_level();
        const db::Capabilities caps = holt->capabilities();

        CatalogSnapshot snapshot = read_catalog(*catalog, config.database,
                                                list_templates, list_unavailable);

        const std::string message =
            snapshot.error.empty()
                ? std::string(TRF("connected | %zu schema(s), %zu table(s), %zu FK(s)",
                                  snapshot.schemas.size(), snapshot.table_count,
                                  snapshot.keys.size()))
                : std::string(TR("connected, but the catalog failed: ")) +
                      snapshot.error;

        const std::lock_guard<std::mutex> lock(mutex_);
        holt_            = std::move(holt);
        // Perfil sem banco: vale o que o servidor escolheu.
        if (database_name_.empty()) database_name_ = holt_->current_database();
        tunnel_          = std::move(tunnel);
        schemas_         = std::move(snapshot.schemas);
        foreign_keys_    = std::move(snapshot.keys);
        databases_       = std::move(snapshot.databases);
        has_database_level_ = database_level;
        reports_server_output_ = holt_->reports_server_output();
        status_message_  = message;
        has_sequences_   = sequences;
        has_user_types_  = user_types;
        has_events_      = events;
        has_server_info_ = server_info;
        has_users_       = users;
        capabilities_    = caps;
        state_.store(SessionState::connected, std::memory_order_release);
        busy_.store(false, std::memory_order_release);
    });
}

void Session::execute_async(std::string sql) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, sql = std::move(sql)] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }

        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        auto result = holt->query(sql);

        const std::lock_guard<std::mutex> lock(mutex_);
        collect_output_locked(*holt);
        if (result) {
            const std::size_t rows = result->row_count();
            const std::size_t cols = result->column_count();
            result_ = std::move(*result);
            status_message_ = TRF("%zu row(s), %zu column(s)", rows, cols);
        } else {
            result_.reset();
            status_message_ = result.error().to_string();
        }
        busy_.store(false, std::memory_order_release);
    });
}

void Session::execute_script_async(std::vector<std::string> statements,
                                   bool stop_on_error) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;
    if (statements.empty()) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    script_done_.store(0, std::memory_order_release);
    script_total_.store(statements.size(), std::memory_order_release);
    script_failed_.store(false, std::memory_order_release);

    worker_ = std::thread([this, statements = std::move(statements),
                           stop_on_error] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        std::optional<db::ResultSet> last_result;
        std::size_t executed = 0;
        std::size_t failed   = 0;
        std::size_t affected = 0;
        std::string first_error;

        for (const std::string& sql : statements) {
            auto result = holt->query(sql);
            ++executed;
            script_done_.store(executed, std::memory_order_release);

            if (!result) {
                ++failed;
                if (first_error.empty()) {
                    // A mensagem guardada e' a do PRIMEIRO erro, com o numero
                    // do comando: num script de 40 linhas, "syntax error"
                    // sozinho nao diz onde procurar.
                    first_error = TRF("statement %zu failed: %s", executed,
                                      result.error().to_string().c_str());
                }
                if (stop_on_error) break;
                continue;
            }

            // Guarda o ultimo que produziu LINHAS. Um script que termina em
            // COMMIT deixaria a grade vazia se guardassemos o ultimo de todos.
            if (result->column_count() > 0) {
                last_result = std::move(*result);
            } else if (result->affected_rows() > 0) {
                affected += static_cast<std::size_t>(result->affected_rows());
            }
        }

        // O script abriu a propria transacao e parou no meio: ela e' desfeita
        // aqui. Sem isto a sessao ficava com a transacao aberta -- no SQL
        // Server, com as alteracoes anteriores ao erro aplicadas e as travas
        // presas ate' alguem notar; no PostgreSQL, no estado "abortada".
        if (failed > 0 && stop_on_error && holt->auto_commit() &&
            (statements.front() == "BEGIN" || statements.front() == "BEGIN TRANSACTION")) {
            (void)holt->rollback();
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        collect_output_locked(*holt);
        if (last_result) result_ = std::move(*last_result);

        if (failed > 0) {
            status_message_ = first_error;
        } else if (affected > 0) {
            status_message_ = TRF("%zu statement(s), %zu row(s) affected",
                                  executed, affected);
        } else {
            status_message_ = TRF("%zu statement(s) executed", executed);
        }

        script_failed_.store(failed > 0, std::memory_order_release);
        script_total_.store(0, std::memory_order_release);
        busy_.store(false, std::memory_order_release);
    });
}

void Session::explain_async(std::string sql, bool analyze) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;
    if (sql.empty()) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, sql = std::move(sql), analyze] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        const std::vector<std::string> statements =
            db::explain_statements(sql, analyze);

        std::optional<db::QueryPlan> plan;
        std::string message;

        // O ROLLBACK e' o ultimo comando e precisa rodar MESMO se o EXPLAIN
        // falhar -- e' o que garante que a analise nao deixa rastro
        // (ADR 0013). Por isso o laco nao interrompe no erro.
        for (std::size_t i = 0; i < statements.size(); ++i) {
            const std::string& statement = statements[i];
            auto result = holt->query(statement);

            if (!result) {
                // Guarda o primeiro erro, mas segue para o ROLLBACK.
                if (message.empty()) message = result.error().to_string();
                continue;
            }

            // O EXPLAIN devolve o JSON numa unica celula.
            if (result->row_count() > 0 && result->column_count() > 0) {
                auto parsed = db::parse_plan_json(result->text(0, 0));
                if (parsed) plan = std::move(*parsed);
                else if (message.empty()) message = parsed.error().to_string();
            }
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        plan_ = std::move(plan);

        if (!message.empty()) {
            status_message_ = message;
        } else if (plan_) {
            status_message_ = analyze
                ? TRF("plan analyzed in %.2f ms (rolled back)",
                      plan_->execution_time)
                : std::string(TR("plan estimated (query not executed)"));
        }
        busy_.store(false, std::memory_order_release);
    });
}

std::optional<db::QueryPlan> Session::take_plan() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!plan_) return std::nullopt;

    std::optional<db::QueryPlan> out = std::move(plan_);
    plan_.reset();
    return out;
}

std::optional<db::TableMeta> Session::table(std::string_view schema,
                                            std::string_view name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const db::SchemaMeta& s : schemas_) {
        if (s.name != schema) continue;
        for (const db::TableMeta& t : s.tables) {
            if (t.name == name) return t;
        }
    }
    return std::nullopt;
}

db::SchemaMeta* Session::find_schema(std::string_view schema) {
    for (db::SchemaMeta& s : schemas_) {
        if (s.name == schema) return &s;
    }
    return nullptr;
}

db::TableMeta* Session::find_table(std::string_view schema,
                                   std::string_view table) {
    db::SchemaMeta* s = find_schema(schema);
    if (s == nullptr) return nullptr;

    for (db::TableMeta& t : s->tables) {
        if (t.name == table) return &t;
    }
    return nullptr;
}

void Session::run_catalog_async(
    std::function<void(db::CatalogReader&)> loader) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, loader = std::move(loader)] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        // Thread novo, dialeto no padrao: o DDL de tabela e' montado aqui.
        db::set_sql_dialect_for(driver_id_);

        // O leitor e' criado por chamada, e nao guardado: ele segura uma
        // referencia ao Holt, e o Holt pode ser trocado por uma reconexao
        // entre uma expansao da arvore e a seguinte.
        std::unique_ptr<db::CatalogReader> catalog =
            db::make_catalog_reader(driver_id_, *holt);
        if (catalog == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }
        loader(*catalog);
        busy_.store(false, std::memory_order_release);
    });
}

void Session::load_columns_async(std::string schema, std::string table) {
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto columns = catalog.load_columns(schema, table);
        if (!columns) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, table)) {
            t->columns = std::move(*columns);
            t->columns_loaded = true;
        }
    });
}

void Session::load_constraints_async(std::string schema, std::string table) {
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto constraints = catalog.load_constraints(schema, table);
        if (!constraints) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, table)) {
            t->constraints = std::move(*constraints);
            t->constraints_loaded = true;
        }
    });
}

void Session::load_indexes_async(std::string schema, std::string table) {
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto indexes = catalog.load_indexes(schema, table);
        if (!indexes) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, table)) {
            t->indexes = std::move(*indexes);
            t->indexes_loaded = true;
        }
    });
}

void Session::load_keys_async(std::string schema, std::string table) {
    // Chaves e referências vêm juntas: quem abre uma quase sempre quer a outra,
    // e são duas consultas baratas sobre o mesmo catálogo.
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto keys       = catalog.load_table_foreign_keys(schema, table);
        auto references = catalog.load_references(schema, table);

        const std::lock_guard<std::mutex> lock(mutex_);
        db::TableMeta* t = find_table(schema, table);
        if (t == nullptr) return;

        if (keys)       t->foreign_keys = std::move(*keys);
        if (references) t->references   = std::move(*references);
        t->keys_loaded = true;
    });
}

void Session::load_view_definition_async(std::string schema, std::string view) {
    run_catalog_async([this, schema, view](db::CatalogReader& catalog) {
        auto definition = catalog.load_view_definition(schema, view);
        if (!definition) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, view)) {
            t->definition = std::move(*definition);
            t->definition_loaded = true;
        }
    });
}

void Session::load_triggers_async(std::string schema, std::string table) {
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto triggers = catalog.load_triggers(schema, table);
        if (!triggers) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, table)) {
            t->triggers = std::move(*triggers);
            t->triggers_loaded = true;
        }
    });
}

void Session::load_partitions_async(std::string schema, std::string table) {
    run_catalog_async([this, schema, table](db::CatalogReader& catalog) {
        auto partitions = catalog.load_partitions(schema, table);
        if (!partitions) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::TableMeta* t = find_table(schema, table)) {
            t->partitions = std::move(*partitions);
            t->partitions_loaded = true;
        }
    });
}

void Session::load_events_async(std::string schema) {
    run_catalog_async([this, schema](db::CatalogReader& catalog) {
        auto events = catalog.load_events(schema);
        if (!events) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        for (db::SchemaMeta& s : schemas_) {
            if (s.name != schema) continue;
            s.events = std::move(*events);
            s.events_loaded = true;
            break;
        }
    });
}

void Session::load_sequences_async(std::string schema) {
    run_catalog_async([this, schema](db::CatalogReader& catalog) {
        auto sequences = catalog.load_sequences(schema);
        if (!sequences) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::SchemaMeta* s = find_schema(schema)) {
            s->sequences = std::move(*sequences);
            s->sequences_loaded = true;
        }
    });
}

void Session::load_routines_async(std::string schema) {
    run_catalog_async([this, schema](db::CatalogReader& catalog) {
        auto routines = catalog.load_routines(schema);
        if (!routines) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::SchemaMeta* s = find_schema(schema)) {
            s->routines = std::move(*routines);
            s->routines_loaded = true;
        }
    });
}

void Session::load_types_async(std::string schema) {
    run_catalog_async([this, schema](db::CatalogReader& catalog) {
        auto types = catalog.load_types(schema);
        if (!types) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        if (db::SchemaMeta* s = find_schema(schema)) {
            s->types = std::move(*types);
            s->types_loaded = true;
        }
    });
}

void Session::load_routine_definition_async(std::string schema, std::string name,
                                            std::string arguments) {
    run_catalog_async([this, schema, name, arguments](db::CatalogReader& catalog) {
        auto definition = catalog.load_routine_definition(schema, name, arguments);
        if (!definition) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        db::SchemaMeta* s = find_schema(schema);
        if (s == nullptr) return;

        for (db::RoutineMeta& routine : s->routines) {
            // Nome E assinatura: duas sobrecargas de mesmo nome receberiam o
            // corpo da primeira se a comparacao fosse so' pelo nome.
            if (routine.name != name || routine.arguments != arguments) continue;

            routine.definition = std::move(*definition);
            routine.definition_loaded = true;
            break;
        }
    });
}

void Session::invalidate_table(std::string_view schema,
                               std::string_view table) {
    const std::lock_guard<std::mutex> lock(mutex_);
    db::TableMeta* t = find_table(schema, table);
    if (t == nullptr) return;

    // Limpa os dados E as flags: manter `columns_loaded` com o vector vazio
    // faria a arvore mostrar "Colunas (0)" para sempre, sem reconsultar.
    t->columns.clear();
    t->constraints.clear();
    t->indexes.clear();
    t->foreign_keys.clear();
    t->references.clear();
    t->triggers.clear();
    t->definition.clear();

    t->partitions.clear();

    t->columns_loaded     = false;
    t->constraints_loaded = false;
    t->indexes_loaded     = false;
    t->keys_loaded        = false;
    t->triggers_loaded    = false;
    t->partitions_loaded  = false;
    t->definition_loaded  = false;

    // As listas da relacao (dependencias, regras, politicas, filhas) saem
    // junto. A chave e' lista + schema + relacao + extra; casar o trecho do
    // meio dispensa enumerar as listas, e uma nova entra sozinha.
    std::string scope;
    scope.push_back('\x1f');
    scope.append(schema);
    scope.push_back('\x1f');
    scope.append(table);
    scope.push_back('\x1f');
    std::erase_if(lists_, [&scope](const auto& entry) {
        return entry.first.find(scope) != std::string::npos;
    });
}

// --- Arvore unica (ADR 0018) --------------------------------------------------------

// --- Saida do servidor ------------------------------------------------------------

void Session::collect_output_locked(db::Holt& holt) {
    for (std::string& line : holt.take_server_output()) {
        server_output_.push_back(std::move(line));
    }

    // Uma sessao longa nao cresce sem fim: fica o fim, que e' o que se le'.
    constexpr std::size_t kMaxLines = 10000;
    if (server_output_.size() > kMaxLines) {
        server_output_.erase(server_output_.begin(),
                             server_output_.begin() +
                                 static_cast<std::ptrdiff_t>(server_output_.size() -
                                                             kMaxLines));
    }
}

std::vector<std::string> Session::server_output() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return server_output_;
}

void Session::append_output(std::string line) {
    const std::lock_guard<std::mutex> lock(mutex_);
    server_output_.push_back(std::move(line));
}

void Session::clear_server_output() {
    const std::lock_guard<std::mutex> lock(mutex_);
    server_output_.clear();
}

void Session::set_database_listing(bool templates, bool unavailable) {
    const std::lock_guard<std::mutex> lock(mutex_);
    list_templates_   = templates;
    list_unavailable_ = unavailable;
}

std::vector<db::DatabaseMeta> Session::databases() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return databases_;
}

Session::ListState Session::list(db::CatalogList list, std::string_view a,
                                 std::string_view b, std::string_view c) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = lists_.find(list_key(list, a, b, c));
    return it != lists_.end() ? it->second : ListState{};
}

void Session::load_list_async(db::CatalogList list, std::string a,
                              std::string b, std::string c) {
    run_catalog_async([this, list, a, b, c](db::CatalogReader& catalog) {
        auto items = catalog.load_list(list, a, b, c);

        const std::lock_guard<std::mutex> lock(mutex_);
        ListState& state = lists_[list_key(list, a, b, c)];
        state.loaded = true;
        if (items) {
            state.items = std::move(*items);
            state.error.clear();
        } else {
            state.items.clear();
            state.error = items.error().to_string();
        }
    });
}

std::uint16_t Session::tunnel_port() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return tunnel_ ? tunnel_->local_port() : std::uint16_t{0};
}

// --- Exportar a consulta inteira ---------------------------------------------------

Session::TransferState Session::transfer_state() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    TransferState state = transfer_;
    state.rows = transfer_rows_.load(std::memory_order_acquire);
    return state;
}

void Session::clear_transfer() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!transfer_.running) transfer_ = {};
}

void Session::export_query_async(std::string sql, db::ExportOptions options,
                                 std::string path) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);
    transfer_rows_.store(0, std::memory_order_release);
    transfer_cancel_.store(false, std::memory_order_release);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        transfer_         = {};
        transfer_.running = true;
        transfer_.path    = path;
    }

    worker_ = std::thread([this, sql = std::move(sql), options = std::move(options),
                           path = std::move(path)] {
        db::Holt* holt = nullptr;
        std::string driver;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt   = holt_.get();
            driver = driver_id_;
        }
        // Thread novo: o formato SQL exporta INSERTs com a citacao do SGBD.
        db::set_sql_dialect_for(driver);

        std::string error;
        const auto run = [&]() -> Status {
            if (holt == nullptr) return fail(Errc::io_error, "not connected");

            db::ExportStream stream(options);
            OTTER_RETURN_IF_ERROR(stream.open(path));

            constexpr std::size_t kChunk = 5000;
            const std::string body(db::strip_trailing_semicolon(sql));
            const auto cancelled = [this] {
                return transfer_cancel_.load(std::memory_order_acquire);
            };

            bool exported = false;

            // --- Cursor de servidor (PostgreSQL) ---------------------------------
            //
            // Uma leitura so', consistente, com memoria limitada ao pedaco. O
            // cursor so' vive dentro de transacao: em auto-commit abre-se uma
            // para ele; em modo manual ele entra na do usuario, que fica como
            // estava.
            if (holt->capabilities().server_cursors) {
                const bool own_transaction =
                    holt->auto_commit() && holt->txn_state() == db::TxnState::idle;
                if (own_transaction) OTTER_RETURN_IF_ERROR(holt->execute("BEGIN"));

                // SAVEPOINT em modo manual: um DECLARE recusado (a consulta nao
                // e' um SELECT) abortaria a transacao do usuario inteira.
                if (!own_transaction) (void)holt->execute("SAVEPOINT otter_export");

                auto declared = holt->execute(
                    "DECLARE otter_export NO SCROLL CURSOR FOR " + body);
                if (declared) {
                    Status status;
                    while (!cancelled()) {
                        auto chunk = holt->query("FETCH FORWARD " +
                                                 std::to_string(kChunk) +
                                                 " FROM otter_export");
                        if (!chunk) { status = std::unexpected(chunk.error()); break; }
                        // O primeiro pedaco vai mesmo vazio: e' dele que sai
                        // o cabecalho de um resultado sem linhas.
                        if (chunk->row_count() == 0 && stream.rows() > 0) break;
                        status = stream.write(*chunk);
                        if (!status) break;
                        transfer_rows_.store(stream.rows(), std::memory_order_release);
                        if (chunk->row_count() < kChunk) break;
                    }
                    (void)holt->execute("CLOSE otter_export");
                    if (own_transaction) {
                        (void)holt->execute(status ? "COMMIT" : "ROLLBACK");
                    } else {
                        (void)holt->execute("RELEASE SAVEPOINT otter_export");
                    }
                    OTTER_RETURN_IF_ERROR(status);
                    exported = true;
                } else if (own_transaction) {
                    (void)holt->execute("ROLLBACK");
                } else {
                    (void)holt->execute("ROLLBACK TO SAVEPOINT otter_export");
                }
            }

            // --- LIMIT/OFFSET, ou a consulta de uma vez ---------------------------
            if (!exported) {
                const sql::Dialect& dialect = sql::dialect_for(driver);
                for (std::size_t page = 0; !cancelled(); ++page) {
                    const sql::PagedQuery paged =
                        sql::make_paged_query(sql, dialect, page, kChunk);

                    OTTER_ASSIGN_OR_RETURN(auto chunk, holt->query(paged.sql));
                    // A linha a mais e' a sonda de "ha' proxima pagina".
                    const bool more = paged.rewritten && chunk.row_count() > kChunk;
                    if (more) chunk.hide_rows_beyond(kChunk);

                    if (chunk.row_count() > 0 || page == 0) {
                        OTTER_RETURN_IF_ERROR(stream.write(chunk));
                    }
                    transfer_rows_.store(stream.rows(), std::memory_order_release);
                    if (!more) break;
                }
            }

            return stream.finish();
        };

        const Status status = run();
        if (!status) error = status.error().to_string();

        const std::lock_guard<std::mutex> lock(mutex_);
        if (holt != nullptr) collect_output_locked(*holt);
        transfer_.running   = false;
        transfer_.finished  = true;
        transfer_.cancelled = transfer_cancel_.load(std::memory_order_acquire);
        transfer_.error     = std::move(error);
        busy_.store(false, std::memory_order_release);
    });
}

Session::ObjectState Session::object_info(const db::ObjectRef& ref) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = objects_.find(ref.key());
    return it != objects_.end() ? it->second : ObjectState{};
}

void Session::load_object_info_async(db::ObjectRef ref) {
    run_catalog_async([this, ref = std::move(ref)](db::CatalogReader& catalog) {
        db::ObjectInfo info = catalog.load_object_info(ref);

        const std::lock_guard<std::mutex> lock(mutex_);
        ObjectState& state = objects_[ref.key()];
        // Carregado mesmo com erro: sem privilegio o servidor recusa sempre,
        // e repetir a cada quadro so' repetiria a recusa.
        state.loaded = true;
        state.info   = std::move(info);
    });
}

void Session::invalidate_object_info(const db::ObjectRef& ref) {
    const std::lock_guard<std::mutex> lock(mutex_);
    objects_.erase(ref.key());
}

void Session::reload_catalog_async() {
    bool templates = false, unavailable = false;
    std::string database;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        templates   = list_templates_;
        unavailable = list_unavailable_;
        database    = database_name_;
    }

    run_catalog_async([this, templates, unavailable,
                       database](db::CatalogReader& catalog) {
        CatalogSnapshot snapshot =
            read_catalog(catalog, database, templates, unavailable);

        const std::lock_guard<std::mutex> lock(mutex_);
        if (!snapshot.error.empty()) {
            // Mantem a arvore que havia: trocar tudo por vazio porque UMA
            // releitura falhou esconderia o banco inteiro.
            status_message_ = snapshot.error;
            return;
        }
        schemas_      = std::move(snapshot.schemas);
        foreign_keys_ = std::move(snapshot.keys);
        if (!snapshot.databases.empty()) {
            databases_ = std::move(snapshot.databases);
        }
        lists_.clear();
        // Um DDL pode ter mudado qualquer objeto aberto: os editores releem.
        objects_.clear();
        users_loaded_ = false;
    });
}

bool Session::auto_commit() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->auto_commit() : true;
}

db::TxnState Session::txn_state() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->txn_state() : db::TxnState::idle;
}

std::size_t Session::uncommitted_changes() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->uncommitted_changes() : 0;
}

// Schema corrente, do estado que a conexao ja' conhece -- sem ida ao
// servidor, porque isto e' lido a cada quadro pela barra de status.
std::string Session::current_schema() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->current_schema() : std::string{};
}

// Fator comum das tres operacoes de transacao: rodar no worker e refletir o
// resultado na mensagem de estado.
void Session::run_txn_async(std::function<Status(db::Holt&)> operation,
                            std::string success_message) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, operation = std::move(operation),
                           success_message = std::move(success_message)] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        const Status status = operation(*holt);

        const std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = status ? success_message : status.error().to_string();
        busy_.store(false, std::memory_order_release);
    });
}

void Session::set_auto_commit_async(bool enabled) {
    run_txn_async(
        [enabled](db::Holt& holt) { return holt.set_auto_commit(enabled); },
        enabled ? TR("auto-commit on") : TR("auto-commit off"));
}

void Session::commit_async() {
    run_txn_async([](db::Holt& holt) { return holt.commit(); },
                  TR("transaction committed"));
}

void Session::rollback_async() {
    run_txn_async([](db::Holt& holt) { return holt.rollback(); },
                  TR("transaction rolled back"));
}

void Session::run_statement_async(std::string sql, std::string success_message) {
    run_txn_async(
        [sql = std::move(sql)](db::Holt& holt) { return holt.execute(sql); },
        std::move(success_message));
}

void Session::ping_async() {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        const auto result = holt->query_internal("SELECT 1");

        const std::lock_guard<std::mutex> lock(mutex_);
        // Numa transacao abortada o servidor recusa qualquer consulta; a
        // conexao esta' viva, e dizer "perdida" seria mentira.
        if (!result && holt->txn_state() != db::TxnState::failed) {
            status_message_ = std::string(TR("connection lost: ")) +
                              result.error().to_string();
            state_.store(SessionState::failed, std::memory_order_release);
        }
        busy_.store(false, std::memory_order_release);
    });
}

void Session::sample_async(std::vector<std::pair<std::string, std::string>> queries) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;
    if (queries.empty()) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, queries = std::move(queries)] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        std::map<std::string, Sample> fresh;
        for (const auto& [id, sql] : queries) {
            Sample sample;
            const auto result = holt->query_internal(sql);
            if (!result) {
                sample.error = result.error().to_string();
            } else if (result->row_count() > 0) {
                for (std::size_t c = 0; c < result->column_count(); ++c) {
                    const std::string text(result->text(0, c));
                    sample.values.emplace_back(result->column(c).info().name,
                                               std::strtod(text.c_str(), nullptr));
                }
            }
            fresh[id] = std::move(sample);
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, sample] : fresh) samples_[id] = std::move(sample);
        sample_serial_.fetch_add(1, std::memory_order_acq_rel);
        busy_.store(false, std::memory_order_release);
    });
}

std::map<std::string, Session::Sample> Session::samples() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return samples_;
}

void Session::disconnect() {
    join_worker();

    const std::lock_guard<std::mutex> lock(mutex_);
    if (holt_) holt_->close();
    holt_.reset();
    tunnel_.reset();   // encerra o processo do ssh junto com a conexao
    schemas_.clear();
    foreign_keys_.clear();
    result_.reset();
    status_message_ = TR("disconnected");
    state_.store(SessionState::disconnected, std::memory_order_release);
}

std::string Session::status_message() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_message_;
}

std::string Session::server_version() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->server_version() : std::string();
}

std::string Session::secure_channel() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->secure_channel() : std::string();
}

std::string Session::database_name() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return database_name_;
}

std::vector<db::SchemaMeta> Session::schemas() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return schemas_;
}

std::vector<db::ForeignKeyMeta> Session::foreign_keys() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return foreign_keys_;
}

std::optional<db::ResultSet> Session::take_result() {
    const std::lock_guard<std::mutex> lock(mutex_);
    // Move e limpa: um optional movido continua "engaged", e devolver o mesmo
    // resultado duas vezes esconderia a chegada de um novo.
    std::optional<db::ResultSet> taken = std::move(result_);
    result_.reset();
    return taken;
}

std::vector<db::QueryLog> Session::query_log() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return holt_ ? holt_->query_log() : std::vector<db::QueryLog>{};
}

void Session::clear_query_log() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (holt_) holt_->clear_query_log();
}

Status Session::cancel_query() {
    // Mesmo padrao do worker: pega o ponteiro SOB o lock e usa FORA dele.
    //
    // Segurar o mutex durante o cancel() seria inofensivo aqui (ele abre
    // conexao nova e nao toca no estado da original), mas travaria a UI se
    // algum driver futuro decidisse esperar por algo.
    db::Holt* holt = nullptr;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        holt = holt_.get();
    }

    if (holt == nullptr) {
        return fail(Errc::closed, "sem conexão para cancelar");
    }
    return holt->cancel();
}

// --- Informacao do servidor (System Info) ----------------------------------------

void Session::load_server_info_async(ServerInfo what) {
    const auto index = static_cast<std::size_t>(what);
    if (index >= kServerInfoCount) return;

    run_catalog_async([this, what, index](db::CatalogReader& catalog) {
        auto values = catalog.load_server_info(
            static_cast<db::ServerInfoKind>(what));
        if (!values) return;

        const std::lock_guard<std::mutex> lock(mutex_);
        server_info_[index] = std::move(*values);
        server_info_loaded_[index] = true;
    });
}

namespace {

// O corpo dos doze acessores e' o mesmo: pegar o mutex e copiar. Escrever
// doze vezes convidaria a esquecer o lock num deles -- e uma corrida de
// leitura num vector sendo substituido pelo worker e' o tipo de defeito que
// aparece uma vez por semana e nunca no depurador.
template <typename T>
T locked_copy(std::mutex& mutex, const T& value) {
    const std::lock_guard<std::mutex> lock(mutex);
    return value;
}

} // namespace

std::vector<db::ServerVariable> Session::session_status() const {
    return locked_copy(mutex_, server_info_[0]);
}
std::vector<db::ServerVariable> Session::global_status() const {
    return locked_copy(mutex_, server_info_[1]);
}
std::vector<db::ServerVariable> Session::session_variables() const {
    return locked_copy(mutex_, server_info_[2]);
}
std::vector<db::ServerVariable> Session::global_variables() const {
    return locked_copy(mutex_, server_info_[3]);
}
std::vector<db::ServerVariable> Session::server_info(ServerInfo what) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto index = static_cast<std::size_t>(what);
    return index < kServerInfoCount ? server_info_[index]
                                    : std::vector<db::ServerVariable>{};
}

bool Session::server_info_loaded(ServerInfo what) const noexcept {
    const auto index = static_cast<std::size_t>(what);
    return index < kServerInfoCount && server_info_loaded_[index];
}

std::vector<db::ServerVariable> Session::engines() const {
    return locked_copy(mutex_, server_info_[4]);
}
std::vector<db::ServerVariable> Session::charsets() const {
    return locked_copy(mutex_, server_info_[5]);
}

// As marcas de "carregado" sao bool: ler um bool que o worker escreve nao
// precisa de lock (nao ha' estado intermediario), e o pior caso e' desenhar
// um quadro a mais com a pasta vazia.
bool Session::session_status_loaded() const noexcept    { return server_info_loaded_[0]; }
bool Session::global_status_loaded() const noexcept     { return server_info_loaded_[1]; }
bool Session::session_variables_loaded() const noexcept { return server_info_loaded_[2]; }
bool Session::global_variables_loaded() const noexcept  { return server_info_loaded_[3]; }
bool Session::engines_loaded() const noexcept           { return server_info_loaded_[4]; }
bool Session::charsets_loaded() const noexcept          { return server_info_loaded_[5]; }
// --- Usuarios ---------------------------------------------------------------------

std::vector<db::UserMeta> Session::users() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return users_;
}

void Session::load_users_async() {
    run_catalog_async([this](db::CatalogReader& catalog) {
        auto users = catalog.load_users();

        const std::lock_guard<std::mutex> lock(mutex_);

        // Marca como carregado mesmo na FALHA: sem privilegio em mysql.user o
        // servidor recusa, e repetir a consulta a cada quadro martelaria o
        // servidor com um erro que ja' se sabe que vai acontecer.
        users_loaded_ = true;
        if (users) users_ = std::move(*users);
    });
}

void Session::load_grants_async(std::string user, std::string host) {
    run_catalog_async([this, user, host](db::CatalogReader& catalog) {
        auto grants = catalog.load_grants(user, host);

        const std::lock_guard<std::mutex> lock(mutex_);
        for (db::UserMeta& u : users_) {
            if (u.name != user || u.host != host) continue;

            u.grants_loaded = true;
            if (grants) u.grants = std::move(*grants);
            break;
        }
    });
}

} // namespace otter::ui
