// C-Otter -- ui/session.hpp
//
// Estado da conexao ativa, visto pela UI. Isola a camada de dados dos paineis:
// main_shell nao fala com otter_db diretamente.
//
// Toda operacao de banco roda num worker; o thread de render nunca bloqueia.
#pragma once

#include "db/catalog.hpp"
#include "db/holt.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace otter::ui {

// Estado da conexao, consultado pela UI a cada frame.
enum class SessionState {
    disconnected,
    connecting,
    connected,
    failed,
};

class Session {
public:
    Session();
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // Dispara a conexao em background e retorna imediatamente.
    void connect_async(const db::ConnConfig& config);

    // Executa a query em background; o resultado aparece em last_result().
    void execute_async(std::string sql);

    void disconnect();

    [[nodiscard]] SessionState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool busy() const noexcept {
        return busy_.load(std::memory_order_acquire);
    }

    // Os acessos abaixo copiam sob lock: sao chamados pelo thread de UI
    // enquanto o worker pode estar escrevendo.
    [[nodiscard]] std::string status_message() const;
    [[nodiscard]] std::string server_version() const;
    [[nodiscard]] std::string database_name() const;
    [[nodiscard]] std::vector<db::SchemaMeta> schemas() const;
    [[nodiscard]] std::vector<db::ForeignKeyMeta> foreign_keys() const;
    [[nodiscard]] std::optional<db::ResultSet> take_result();
    [[nodiscard]] std::vector<db::QueryLog> query_log() const;

    // Carrega as colunas de uma tabela sob demanda (lazy).
    void load_columns_async(std::string schema, std::string table);

private:
    void join_worker();

    mutable std::mutex mutex_;
    std::unique_ptr<db::Holt> holt_;

    std::atomic<SessionState> state_{SessionState::disconnected};
    std::atomic<bool>         busy_{false};

    std::string status_message_;
    std::string database_name_;

    std::vector<db::SchemaMeta>     schemas_;
    std::vector<db::ForeignKeyMeta> foreign_keys_;
    std::optional<db::ResultSet>    result_;

    std::thread worker_;
};

} // namespace otter::ui
