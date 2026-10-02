// Driver do Oracle sobre lib/orawire (ADR 0027). Adapta o protocolo ao
// contrato Holt/Driver, como os outros fazem para os deles.
//
// O que e' diferente, e por isso nao da' para copiar:
//
//   - nao existe "modo" de auto-commit no servidor: cada instrucao leva (ou
//     nao) o pedido de commit junto. Sem ele a transacao fica aberta, e quem
//     diz isso e' o servidor, no fim de cada chamada
//   - DDL faz commit implicito, antes e depois (como no MySQL)
//   - uma instrucao por vez, SEM o ponto e virgula (oracle_statement_text)
//   - so' dois niveis de isolamento: READ COMMITTED e SERIALIZABLE
//   - nao ha' banco acima do schema, e schema e' USUARIO: toda conta tem o seu
//   - os valores chegam no formato interno do servidor; quem os vira em texto
//     e' o orawire
//   - o resultado nao diz de que tabela cada coluna veio: a grade de uma
//     consulta livre fica somente leitura (a da tabela aberta pela arvore
//     sabe a origem por construcao)
#include "db/drivers/oracle.hpp"

#include "orawire/connection.hpp"

#include <chrono>
#include <mutex>
#include <string>

namespace otter::db {
namespace {

using orawire::OraType;

DataKind kind_from_column(const orawire::Column& column) noexcept {
    switch (column.type) {
        case OraType::number:
            // NUMBER(p,0) e' inteiro; NUMBER sem precisao pode ser qualquer
            // coisa, e fica como decimal exato.
            return column.precision > 0 && column.scale == 0 ? DataKind::integer
                                                             : DataKind::numeric;
        case OraType::binary_integer:
            return DataKind::integer;
        case OraType::binary_float:
        case OraType::binary_double:
            return DataKind::floating;
        case OraType::date:               // o DATE do Oracle tem hora
        case OraType::timestamp:
        case OraType::timestamp_tz:
        case OraType::timestamp_ltz:
            return DataKind::timestamp;
        case OraType::interval_ym:
        case OraType::interval_ds:
            return DataKind::interval;
        case OraType::raw:
        case OraType::long_raw:
        case OraType::blob:
        case OraType::bfile:
            return DataKind::binary;
        case OraType::boolean:
            return DataKind::boolean;
        case OraType::json:
            return DataKind::json;
        case OraType::varchar:
        case OraType::char_:
        case OraType::long_:
        case OraType::clob:
        case OraType::rowid:
        case OraType::urowid:
            return DataKind::string;
        case OraType::cursor:
        case OraType::object:
        case OraType::vector:
            return DataKind::unknown;
    }
    return DataKind::unknown;
}

// A primeira palavra, em maiusculas, e onde ela termina.
std::string first_word(std::string_view sql, std::size_t& end) {
    std::size_t i = 0;
    while (i < sql.size()) {
        const char c = sql[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++i;
        } else if (sql.substr(i, 2) == "--") {
            while (i < sql.size() && sql[i] != '\n') ++i;
        } else if (sql.substr(i, 2) == "/*") {
            const std::size_t close = sql.find("*/", i + 2);
            i = close == std::string_view::npos ? sql.size() : close + 2;
        } else {
            break;
        }
    }
    std::string word;
    while (i < sql.size() && ((sql[i] >= 'a' && sql[i] <= 'z') || (sql[i] >= 'A' && sql[i] <= 'Z'))) {
        word.push_back(static_cast<char>(sql[i] >= 'a' ? sql[i] - 32 : sql[i]));
        ++i;
    }
    end = i;
    return word;
}

// CREATE [OR REPLACE] [[NON]EDITIONABLE] {PROCEDURE|FUNCTION|PACKAGE|TRIGGER|
// TYPE|LIBRARY}: o corpo e' PL/SQL, e o ';' final e' dele.
bool creates_stored_code(std::string_view sql) {
    std::size_t at = 0;
    if (first_word(sql, at) != "CREATE") return false;

    for (int words = 0; words < 6; ++words) {
        std::size_t end = 0;
        const std::string word = first_word(sql.substr(at), end);
        if (word.empty()) return false;
        at += end;
        if (word == "PROCEDURE" || word == "FUNCTION" || word == "PACKAGE" ||
            word == "TRIGGER" || word == "TYPE" || word == "LIBRARY") {
            return true;
        }
        if (word != "OR" && word != "REPLACE" && word != "EDITIONABLE" &&
            word != "NONEDITIONABLE" && word != "AND" && word != "RESOLVE" &&
            word != "FORCE") {
            return false;
        }
    }
    return false;
}

class OracleHolt final : public Holt {
public:
    explicit OracleHolt(orawire::Connection conn) : conn_(std::move(conn)) {}

    [[nodiscard]] bool is_open() const noexcept override { return conn_.is_open(); }
    void close() override { conn_.close(); }

    [[nodiscard]] Result<ResultSet> query(std::string_view sql) override {
        return run(sql, /*internal=*/false);
    }

    [[nodiscard]] Status execute(std::string_view sql) override {
        auto result = run(sql, /*internal=*/false);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    [[nodiscard]] Result<ResultSet> query_internal(std::string_view sql) override {
        return run(sql, /*internal=*/true);
    }

    // Marcador de interrupcao na mesma conexao: a instrucao volta ORA-01013.
    Status cancel() override { return conn_.cancel(); }

    // --- Transacoes ---------------------------------------------------------

    [[nodiscard]] bool auto_commit() const noexcept override { return conn_.auto_commit(); }

    Status set_auto_commit(bool enabled) override {
        if (enabled == conn_.auto_commit()) return {};
        // Saindo do modo manual: confirma o que estava pendente. Commit, e nao
        // rollback -- descartar trabalho do usuario sem pedir seria pior.
        if (enabled && conn_.in_transaction()) OTTER_RETURN_IF_ERROR(conn_.commit());
        conn_.set_auto_commit(enabled);
        clear_changes();
        return {};
    }

    [[nodiscard]] TxnState txn_state() const noexcept override {
        // Um erro aborta a INSTRUCAO, nao a transacao: nao existe o estado
        // "failed" do PostgreSQL.
        return conn_.in_transaction() ? TxnState::active : TxnState::idle;
    }

    Status commit() override {
        OTTER_RETURN_IF_ERROR(conn_.commit());
        clear_changes();
        return {};
    }

    Status rollback() override {
        OTTER_RETURN_IF_ERROR(conn_.rollback());
        clear_changes();
        return {};
    }

    Status savepoint(std::string_view name) override {
        return run_silent("SAVEPOINT " + oracle_quote(name));
    }

    Status rollback_to(std::string_view name) override {
        return run_silent("ROLLBACK TO SAVEPOINT " + oracle_quote(name));
    }

    // O Oracle nao tem RELEASE SAVEPOINT: o ponto some com a transacao.
    Status release_savepoint(std::string_view) override { return {}; }

    [[nodiscard]] Result<IsolationLevel> isolation_level() override {
        // O servidor nao tem uma consulta simples para o nivel da SESSAO; o
        // que vale e' o ultimo que esta conexao pediu.
        return isolation_;
    }

    Status set_isolation_level(IsolationLevel level) override {
        if (level != IsolationLevel::read_committed && level != IsolationLevel::serializable) {
            return fail(Errc::not_supported,
                        "Oracle only has READ COMMITTED and SERIALIZABLE");
        }
        OTTER_RETURN_IF_ERROR(run_silent(
            level == IsolationLevel::serializable
                ? "ALTER SESSION SET ISOLATION_LEVEL = SERIALIZABLE"
                : "ALTER SESSION SET ISOLATION_LEVEL = READ COMMITTED"));
        isolation_ = level;
        return {};
    }

    [[nodiscard]] Capabilities capabilities() const noexcept override {
        Capabilities caps;
        caps.transactions = true;
        caps.savepoints   = true;
        // CREATE TABLE faz commit implicito e leva junto o que estava
        // pendente: um rollback oferecido depois nao desfaria nada.
        caps.ddl_in_transaction = false;
        caps.server_cursors     = false;
        caps.binary_transfer    = true;
        caps.multiple_results   = false;
        caps.arrays             = false;
        caps.cancel_query       = true;
        caps.explain_plan       = false;   // EXPLAIN PLAN + DBMS_XPLAN: ainda nao lido
        return caps;
    }

    // So' o numero: a barra de status ja' poe o nome do SGBD na frente.
    [[nodiscard]] std::string server_version() const override {
        return conn_.server_version();
    }

    [[nodiscard]] std::string current_schema() const override {
        const std::lock_guard<std::mutex> lock(names_mutex_);
        return schema_;
    }

    // O Oracle nao tem banco acima do schema; o que identifica "onde estou" e'
    // o container (a PDB), dito pelo servidor.
    [[nodiscard]] std::string current_database() const override {
        const std::lock_guard<std::mutex> lock(names_mutex_);
        return container_;
    }

    void load_session_defaults() {
        auto rs = run("SELECT SYS_CONTEXT('USERENV','CURRENT_SCHEMA'), "
                      "SYS_CONTEXT('USERENV','CON_NAME') FROM DUAL",
                      true);
        if (!rs || rs->row_count() == 0) return;
        const std::lock_guard<std::mutex> lock(names_mutex_);
        schema_ = std::string(rs->text(0, 0));
        container_ = std::string(rs->text(0, 1));
    }

private:
    Status run_silent(std::string_view sql) {
        auto result = run(sql, /*internal=*/true);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "connection is closed");

        const auto started = std::chrono::steady_clock::now();
        const std::string text = oracle_statement_text(sql);

        ResultSetBuilder builder;
        std::vector<orawire::Column> columns;
        std::size_t rows = 0;

        const Status status = conn_.query(
            text,
            [&](const std::vector<orawire::Column>& described) {
                columns = described;
                for (const orawire::Column& column : columns) {
                    ColumnInfo info;
                    info.name      = column.name;
                    info.type_name = std::string(orawire::type_name(column.type,
                                                                    column.charset_form));
                    info.kind      = kind_from_column(column);
                    info.type_oid  = static_cast<std::uint32_t>(column.type);
                    info.size      = column.max_size > 0
                                         ? static_cast<std::int32_t>(column.max_size)
                                         : -1;
                    info.precision = column.precision;
                    info.scale     = column.scale;
                    info.nullable  = column.nullable;
                    builder.add_column(std::move(info));
                }
            },
            [&](std::span<const orawire::Value> row) {
                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].null) builder.append_null(c);
                    else             builder.append_text(c, row[c].text);
                }
                ++rows;
            });

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);

        if (!status) {
            record(QueryLog{std::string(sql), elapsed, 0, internal, true,
                            status.error().message()});
            return std::unexpected(status.error());
        }

        builder.set_row_count(rows);
        builder.set_affected_rows(conn_.last_affected_rows());

        if (!internal && !conn_.auto_commit() && conn_.last_affected_rows() > 0) {
            note_change();
        }
        // O servidor avisa quando o schema corrente muda (ALTER SESSION SET
        // CURRENT_SCHEMA). A barra de status le' de outra thread: pelo mutex.
        if (!conn_.current_schema().empty()) {
            const std::lock_guard<std::mutex> lock(names_mutex_);
            schema_ = conn_.current_schema();
        }

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});
        return builder.take();
    }

    orawire::Connection conn_;
    std::string         schema_;
    std::string         container_;
    mutable std::mutex  names_mutex_;
    IsolationLevel      isolation_ = IsolationLevel::read_committed;
};

class OracleDriver final : public Driver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "oracle"; }
    [[nodiscard]] std::string_view display_name() const noexcept override { return "Oracle"; }
    [[nodiscard]] std::uint16_t default_port() const noexcept override { return 1521; }

    [[nodiscard]] Result<std::unique_ptr<Holt>> connect(const ConnConfig& config) override {
        if (config.ssl_enabled()) {
            // Dizer, em vez de conectar em claro com o perfil pedindo TLS.
            return fail(Errc::not_supported,
                        "TLS (TCPS) is not implemented for Oracle yet; set SSL mode to "
                        "\"disable\" to connect without it");
        }

        orawire::ConnectParams params;
        params.host     = config.host;
        params.port     = config.port != 0 ? config.port : 1521;
        params.user     = config.user;
        params.password = config.password;
        params.timeout  = std::chrono::duration_cast<std::chrono::milliseconds>(
            config.connect_timeout);

        params.proxy.host     = config.proxy_host;
        params.proxy.port     = config.proxy_port;
        params.proxy.user     = config.proxy_user;
        params.proxy.password = config.proxy_password;

        // O campo "Database" e' o NOME DE SERVICO. Um banco antigo, que so' se
        // nomeia por SID, vai na propriedade "sid" do driver -- e' a escolha
        // "Service name / SID" do dialogo do DBeaver.
        if (const auto sid = config.driver_properties.find("sid");
            sid != config.driver_properties.end() && !sid->second.empty()) {
            params.sid = sid->second;
        } else {
            params.service_name = config.database;
        }

        OTTER_ASSIGN_OR_RETURN(auto conn, orawire::Connection::connect(params));

        auto holt = std::unique_ptr<OracleHolt>(new OracleHolt(std::move(conn)));
        holt->load_session_defaults();

        if (config.isolation_level.has_value()) {
            OTTER_RETURN_IF_ERROR(holt->set_isolation_level(*config.isolation_level));
        }
        return std::unique_ptr<Holt>(std::move(holt));
    }
};

} // namespace

Driver& oracle_driver() {
    static OracleDriver driver;
    return driver;
}

std::string oracle_quote(std::string_view identifier) {
    std::string out = "\"";
    for (const char c : identifier) {
        out += c;
        if (c == '"') out += c;
    }
    out += '"';
    return out;
}

std::string oracle_statement_text(std::string_view sql) {
    const auto trim_right = [&sql] {
        while (!sql.empty() && (sql.back() == ' ' || sql.back() == '\t' ||
                                sql.back() == '\r' || sql.back() == '\n')) {
            sql.remove_suffix(1);
        }
    };

    trim_right();
    // A barra sozinha na ultima linha e' do SQL*Plus, nao do SQL.
    if (!sql.empty() && sql.back() == '/') {
        std::string_view before = sql.substr(0, sql.size() - 1);
        while (!before.empty() && (before.back() == ' ' || before.back() == '\t')) {
            before.remove_suffix(1);
        }
        if (before.empty() || before.back() == '\n') {
            sql = before;
            trim_right();
        }
    }

    std::size_t end = 0;
    const std::string word = first_word(sql, end);
    const bool keeps_semicolon =
        word == "BEGIN" || word == "DECLARE" || creates_stored_code(sql);
    if (!keeps_semicolon) {
        while (!sql.empty() && sql.back() == ';') {
            sql.remove_suffix(1);
            trim_right();
        }
        return std::string(sql);
    }

    // Ao contrario: o divisor de scripts entrega o bloco SEM o ';' final (para
    // ele e' separador), e sem o ';' o servidor recusa o END (PLS-00103).
    std::string text(sql);
    if (!text.empty() && text.back() != ';') text.push_back(';');
    return text;
}

} // namespace otter::db
