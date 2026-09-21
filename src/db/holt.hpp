// C-Otter -- db/holt.hpp
//
// Holt e' uma conexao com um banco (a toca da lontra). Substitui o papel do
// java.sql.Connection do JDBC, com contrato definido pelo que o nucleo precisa.
#pragma once

#include "base/error.hpp"
#include "db/result_set.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace otter::db {

struct ConnConfig {
    // Qual driver falar: "postgresql", "mysql". Viaja junto com o host e a
    // senha porque e' o que decide o PROTOCOLO -- separa-lo do resto faria
    // cada ponto de conexao ter de reencontra-lo, e um ponto esquecido
    // conectaria ao MySQL falando o protocolo do PostgreSQL.
    std::string driver_id = "postgresql";

    std::string host     = "localhost";
    std::uint16_t port   = 5432;
    std::string database;
    std::string user;
    std::string password;
    std::string options;                              // parametros extras do driver
    std::chrono::seconds connect_timeout{10};
};

// O que o driver suporta. A UI consulta isto para habilitar ou esconder acoes,
// em vez de tentar e falhar.
struct Capabilities {
    bool transactions      = false;
    bool savepoints        = false;
    bool ddl_in_transaction = false;
    bool server_cursors    = false;
    bool binary_transfer   = false;
    bool multiple_results  = false;
    bool arrays            = false;
    bool cancel_query      = false;
    bool explain_plan      = false;
};

// Estado da transacao na conexao. Vem do servidor, nao de um palpite do
// cliente: o PostgreSQL informa em cada ReadyForQuery.
enum class TxnState : std::uint8_t {
    idle,        // fora de transacao
    active,      // transacao aberta, com alteracoes pendentes
    failed,      // transacao abortada; so' ROLLBACK e' aceito
};

[[nodiscard]] std::string_view to_string(TxnState state) noexcept;

// Nivel de isolamento, na ordem do padrao SQL.
enum class IsolationLevel : std::uint8_t {
    read_uncommitted,
    read_committed,
    repeatable_read,
    serializable,
};

[[nodiscard]] std::string_view to_string(IsolationLevel level) noexcept;

// Registro de uma query executada -- alimenta o inspetor de queries (ADR 0008).
// Toda query e' registrada, inclusive as internas de metadados: ferramenta que
// esconde o que faz e' dificil de confiar.
struct QueryLog {
    std::string               sql;
    std::chrono::microseconds duration{0};
    std::size_t               rows = 0;
    bool                      internal = false;   // consulta de catalogo
    bool                      failed = false;
    std::string               error;
};

class Holt {
public:
    virtual ~Holt() = default;

    Holt(const Holt&)            = delete;
    Holt& operator=(const Holt&) = delete;

    [[nodiscard]] virtual bool is_open() const noexcept = 0;
    virtual void close() = 0;

    // Executa e devolve o resultado completo.
    [[nodiscard]] virtual Result<ResultSet> query(std::string_view sql) = 0;

    // Executa sem produzir resultado (DDL, DML).
    [[nodiscard]] virtual Status execute(std::string_view sql) = 0;

    // Cancela a query em andamento, de outra thread.
    virtual Status cancel() = 0;

    // --- Transacoes ---------------------------------------------------------

    [[nodiscard]] virtual bool auto_commit() const noexcept = 0;
    virtual Status set_auto_commit(bool enabled) = 0;

    // Estado reportado pelo servidor apos a ultima query.
    [[nodiscard]] virtual TxnState txn_state() const noexcept = 0;

    virtual Status commit() = 0;
    virtual Status rollback() = 0;

    virtual Status savepoint(std::string_view name) = 0;
    virtual Status rollback_to(std::string_view name) = 0;
    virtual Status release_savepoint(std::string_view name) = 0;

    [[nodiscard]] virtual Result<IsolationLevel> isolation_level() = 0;
    virtual Status set_isolation_level(IsolationLevel level) = 0;

    // Quantas instrucoes de alteracao rodaram desde o ultimo commit. A UI usa
    // para avisar antes de fechar com trabalho pendente.
    [[nodiscard]] std::size_t uncommitted_changes() const noexcept {
        return uncommitted_changes_;
    }

    [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::string server_version() const = 0;
    [[nodiscard]] virtual std::string current_schema() const = 0;

    // Historico desta conexao, para o inspetor de queries.
    [[nodiscard]] const std::vector<QueryLog>& query_log() const noexcept {
        return query_log_;
    }
    void clear_query_log() { query_log_.clear(); }

protected:
    Holt() = default;

    void record(QueryLog entry) {
        // Limite defensivo: uma sessao longa nao pode crescer sem fim.
        constexpr std::size_t kMaxEntries = 2000;
        if (query_log_.size() >= kMaxEntries) {
            query_log_.erase(query_log_.begin(),
                             query_log_.begin() + kMaxEntries / 4);
        }
        query_log_.push_back(std::move(entry));
    }

    // Contabiliza alteracoes pendentes. Chamado pelo driver ao ver um comando
    // que modifica dados fora de auto-commit.
    void note_change() noexcept { ++uncommitted_changes_; }
    void clear_changes() noexcept { uncommitted_changes_ = 0; }

private:
    std::vector<QueryLog> query_log_;
    std::size_t           uncommitted_changes_ = 0;
};

// Driver de um SGBD. Interface virtual pura, registrada estaticamente
// (sem OSGi, ver ADR 0001 #5).
class Driver {
public:
    virtual ~Driver() = default;

    [[nodiscard]] virtual std::string_view id() const noexcept = 0;
    [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
    [[nodiscard]] virtual std::uint16_t default_port() const noexcept = 0;

    [[nodiscard]] virtual Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) = 0;
};

} // namespace otter::db
