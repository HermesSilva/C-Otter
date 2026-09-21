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
#include <functional>
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

    // Carregamento tardio por pasta da arvore. Cada uma consulta o catalogo
    // apenas quando o no e' expandido -- expandir "Colunas" nao deve custar
    // uma leitura de indices.
    void load_columns_async(std::string schema, std::string table);
    void load_constraints_async(std::string schema, std::string table);
    void load_indexes_async(std::string schema, std::string table);
    void load_keys_async(std::string schema, std::string table);
    void load_triggers_async(std::string schema, std::string table);

    // Corpo da view (`pg_get_viewdef`). Carregado so' quando o no "Definicao"
    // e' expandido: uma view de relatorio pode ter varios KB de SQL.
    void load_view_definition_async(std::string schema, std::string view);
    void load_sequences_async(std::string schema);
    void load_routines_async(std::string schema);

    // --- Transacoes ---------------------------------------------------------
    //
    // Consultas baratas e sincronas: leem estado ja' conhecido pela conexao,
    // sem ida ao servidor. Chamadas a cada frame pela barra de ferramentas.
    [[nodiscard]] bool auto_commit() const;
    [[nodiscard]] db::TxnState txn_state() const;
    [[nodiscard]] std::size_t uncommitted_changes() const;

    // Executadas no worker: emitem SQL de verdade.
    void set_auto_commit_async(bool enabled);
    void commit_async();
    void rollback_async();

private:
    void join_worker();

    // Fator comum de commit/rollback/auto-commit: roda no worker e reflete o
    // resultado na mensagem de estado.
    void run_txn_async(std::function<Status(db::Holt&)> operation,
                       std::string success_message);

    // Fator comum dos carregadores de catalogo: abre um worker que recebe o
    // catalogo pronto e escreve no modelo sob lock.
    void run_catalog_async(std::function<void(db::PostgresCatalog&)> loader);

    // Localiza uma tabela no modelo. O chamador deve ja' segurar o mutex.
    [[nodiscard]] db::TableMeta* find_table(std::string_view schema,
                                            std::string_view table);
    [[nodiscard]] db::SchemaMeta* find_schema(std::string_view schema);

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

