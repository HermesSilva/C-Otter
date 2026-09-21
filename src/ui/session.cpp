#include "ui/session.hpp"

#include "base/i18n.hpp"
#include "db/catalog_reader.hpp"
#include "db/ddl.hpp"
#include "db/registry.hpp"

#include <utility>

namespace otter::ui {

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

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        status_message_ = TRF("connecting to %s:%u...", config.host.c_str(),
                              static_cast<unsigned>(config.port));
        database_name_  = config.database;
        driver_id_      = config.driver_id;
        schemas_.clear();
        foreign_keys_.clear();
    }

    worker_ = std::thread([this, config] {
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

        // O SQL gerado (UPDATE da grade, DDL, agregacao no servidor) precisa
        // do delimitador do SGBD certo: com aspas duplas num MySQL, o comando
        // compara a coluna com uma STRING em vez de referencia-la.
        db::set_sql_dialect_for(config.driver_id);

        auto connection = driver->connect(config);

        if (!connection) {
            const std::lock_guard<std::mutex> lock(mutex_);
            status_message_ = connection.error().to_string();
            state_.store(SessionState::failed, std::memory_order_release);
            busy_.store(false, std::memory_order_release);
            return;
        }

        std::unique_ptr<db::Holt> holt = std::move(*connection);

        // Ja' que estamos no worker, carrega o catalogo antes de liberar a UI:
        // uma arvore vazia por meio segundo pareceria falha de conexao.
        std::unique_ptr<db::CatalogReader> catalog =
            db::make_catalog_reader(config.driver_id, *holt);

        if (catalog == nullptr) {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt_           = std::move(holt);
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
        const db::Capabilities caps = holt->capabilities();

        std::vector<db::SchemaMeta>     schemas;
        std::vector<db::ForeignKeyMeta> keys;
        std::string message;

        auto loaded = catalog->load_schemas();
        if (loaded) {
            schemas = std::move(*loaded);

            for (db::SchemaMeta& schema : schemas) {
                auto tables = catalog->load_tables(schema.name);
                if (tables) {
                    schema.tables = std::move(*tables);
                    schema.tables_loaded = true;
                }
            }

            // As foreign keys alimentam a inferencia de JOIN do completion
            // (ADR 0004, camada 4). O schema de onde le-las depende do SGBD:
            // "public" no PostgreSQL, o banco da conexao no MySQL -- pedir
            // "public" a um MySQL simplesmente nao acharia nada.
            std::string fk_schema = catalog->default_schema();
            if (fk_schema.empty()) fk_schema = config.database;

            auto fks = catalog->load_foreign_keys(fk_schema);
            if (fks) keys = std::move(*fks);

            std::size_t table_count = 0;
            for (const db::SchemaMeta& schema : schemas) {
                table_count += schema.tables.size();
            }
            message = TRF("connected | %zu schema(s), %zu table(s), %zu FK(s)",
                          schemas.size(), table_count, keys.size());
        } else {
            message = std::string(TR("connected, but the catalog failed: ")) +
                      loaded.error().to_string();
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        holt_            = std::move(holt);
        schemas_         = std::move(schemas);
        foreign_keys_    = std::move(keys);
        status_message_  = std::move(message);
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

        const std::lock_guard<std::mutex> lock(mutex_);
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

    t->columns_loaded     = false;
    t->constraints_loaded = false;
    t->indexes_loaded     = false;
    t->keys_loaded        = false;
    t->triggers_loaded    = false;
    t->definition_loaded  = false;
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

void Session::disconnect() {
    join_worker();

    const std::lock_guard<std::mutex> lock(mutex_);
    if (holt_) holt_->close();
    holt_.reset();
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
