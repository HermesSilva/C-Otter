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

private:
    std::vector<QueryLog> query_log_;
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
