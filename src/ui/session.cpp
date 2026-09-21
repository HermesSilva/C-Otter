#include "ui/session.hpp"

#include "base/i18n.hpp"
#include "db/drivers/postgres.hpp"

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
        schemas_.clear();
        foreign_keys_.clear();
    }

    worker_ = std::thread([this, config] {
        auto connection = db::postgres_driver().connect(config);

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
        db::PostgresCatalog catalog(*holt);

        std::vector<db::SchemaMeta>     schemas;
        std::vector<db::ForeignKeyMeta> keys;
        std::string message;

        auto loaded = catalog.load_schemas();
        if (loaded) {
            schemas = std::move(*loaded);

            for (db::SchemaMeta& schema : schemas) {
                auto tables = catalog.load_tables(schema.name);
                if (tables) {
                    schema.tables = std::move(*tables);
                    schema.tables_loaded = true;
                }
            }

            // Foreign keys de 'public' alimentam a inferencia de JOIN do
            // completion (ADR 0004, camada 4).
            auto fks = catalog.load_foreign_keys("public");
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
        holt_           = std::move(holt);
        schemas_        = std::move(schemas);
        foreign_keys_   = std::move(keys);
        status_message_ = std::move(message);
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

void Session::load_columns_async(std::string schema, std::string table) {
    if (busy_.load(std::memory_order_acquire)) return;
    if (state_.load(std::memory_order_acquire) != SessionState::connected) return;

    join_worker();
    busy_.store(true, std::memory_order_release);

    worker_ = std::thread([this, schema = std::move(schema),
                           table = std::move(table)] {
        db::Holt* holt = nullptr;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            holt = holt_.get();
        }
        if (holt == nullptr) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        db::PostgresCatalog catalog(*holt);
        auto columns = catalog.load_columns(schema, table);

        const std::lock_guard<std::mutex> lock(mutex_);
        if (columns) {
            for (db::SchemaMeta& s : schemas_) {
                if (s.name != schema) continue;
                for (db::TableMeta& t : s.tables) {
                    if (t.name != table) continue;
                    t.columns = std::move(*columns);
                    t.columns_loaded = true;
                    break;
                }
                break;
            }
        }
        busy_.store(false, std::memory_order_release);
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

} // namespace otter::ui

