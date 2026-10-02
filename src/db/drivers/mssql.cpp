// Driver do SQL Server sobre lib/tdswire (ADR 0024). Adapta o protocolo ao
// contrato Holt/Driver, como postgres.cpp e mysql.cpp fazem para os deles.
//
// O que e' diferente dos outros dois, e por isso nao da' para copiar:
//
//   - os valores chegam em BINARIO; quem os converte para texto e' o tdswire
//   - o modo manual e' `SET IMPLICIT_TRANSACTIONS ON`: o servidor abre a
//     transacao sozinho no primeiro comando, e avisa por ENVCHANGE
//   - DDL e' transacional (como no PostgreSQL, ao contrario do MySQL)
//   - um erro dentro da transacao NAO a aborta (salvo XACT_ABORT): nao existe
//     o estado "failed" do PostgreSQL
//   - cancelar e' um pacote ATTENTION na MESMA conexao, nao uma conexao nova
//   - o resultado nao diz de que tabela cada coluna veio: para a grade
//     editavel, pergunta-se ao servidor (sp_describe_first_result_set)
#include "db/drivers/mssql.hpp"

#include "tdswire/browser.hpp"
#include "tdswire/connection.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <map>
#include <string>

namespace otter::db {
namespace {

DataKind kind_from_type(const tdswire::TypeInfo& type) noexcept {
    using tdswire::TypeId;
    switch (type.id) {
        case TypeId::int1:
        case TypeId::int2:
        case TypeId::int4:
        case TypeId::int8:
        case TypeId::intn:
            return DataKind::integer;
        case TypeId::bit:
        case TypeId::bitn:
            return DataKind::boolean;
        case TypeId::float4:
        case TypeId::float8:
        case TypeId::floatn:
            return DataKind::floating;
        case TypeId::money:
        case TypeId::money4:
        case TypeId::moneyn:
        case TypeId::decimal:
        case TypeId::numeric:
        case TypeId::decimaln:
        case TypeId::numericn:
            return DataKind::numeric;
        case TypeId::daten:
            return DataKind::date;
        case TypeId::timen:
            return DataKind::time;
        case TypeId::datetime:
        case TypeId::datetime4:
        case TypeId::datetimen:
        case TypeId::datetime2n:
        case TypeId::datetimeoffsetn:
            return DataKind::timestamp;
        case TypeId::guid:
            return DataKind::uuid;
        case TypeId::bigvarbinary:
        case TypeId::bigbinary:
        case TypeId::image:
        case TypeId::udt:
            return DataKind::binary;
        case TypeId::bigvarchar:
        case TypeId::bigchar:
        case TypeId::nvarchar:
        case TypeId::nchar:
        case TypeId::text:
        case TypeId::ntext:
        case TypeId::xml:
        case TypeId::variant:
            return DataKind::string;
        case TypeId::null_:
            return DataKind::unknown;
    }
    return DataKind::unknown;
}

// N'texto', com a aspa dobrada.
std::string n_literal(std::string_view text) {
    std::string out = "N'";
    for (const char c : text) {
        out += c;
        if (c == '\'') out += c;
    }
    out += '\'';
    return out;
}

// [nome], com o colchete de fechamento dobrado.
std::string bracket(std::string_view name) {
    std::string out = "[";
    for (const char c : name) {
        out += c;
        if (c == ']') out += c;
    }
    out += ']';
    return out;
}

// O comando e' um SELECT (ou um CTE)? So' esses tem "tabela de origem" a
// perguntar; para o resto a consulta extra seria um custo sem retorno.
bool looks_like_select(std::string_view sql) {
    std::size_t i = 0;
    for (;;) {
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])) != 0) ++i;
        if (sql.substr(i, 2) == "--") {
            const std::size_t end = sql.find('\n', i);
            if (end == std::string_view::npos) return false;
            i = end + 1;
        } else if (sql.substr(i, 2) == "/*") {
            const std::size_t end = sql.find("*/", i + 2);
            if (end == std::string_view::npos) return false;
            i = end + 2;
        } else {
            break;
        }
    }
    const auto starts = [&sql, i](std::string_view word) {
        if (sql.size() - i < word.size()) return false;
        for (std::size_t k = 0; k < word.size(); ++k) {
            if (std::toupper(static_cast<unsigned char>(sql[i + k])) != word[k]) return false;
        }
        const std::size_t end = i + word.size();
        return end == sql.size() || std::isalnum(static_cast<unsigned char>(sql[end])) == 0;
    };
    return starts("SELECT") || starts("WITH");
}

class MssqlHolt final : public Holt {
public:
    explicit MssqlHolt(tdswire::Connection conn) : conn_(std::move(conn)) {}

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

    // ATTENTION na mesma conexao: e' o cancelamento do proprio protocolo.
    Status cancel() override { return conn_.cancel(); }

    // --- Transacoes ---------------------------------------------------------

    [[nodiscard]] bool auto_commit() const noexcept override { return auto_commit_; }

    Status set_auto_commit(bool enabled) override {
        if (enabled == auto_commit_) return {};

        if (enabled) {
            // Saindo do modo manual: confirma o que estava pendente. Commit, e
            // nao rollback -- descartar trabalho do usuario sem pedir seria pior.
            OTTER_RETURN_IF_ERROR(run_silent("IF @@TRANCOUNT > 0 COMMIT TRANSACTION"));
            OTTER_RETURN_IF_ERROR(run_silent("SET IMPLICIT_TRANSACTIONS OFF"));
        } else {
            // O servidor abre a transacao sozinho no primeiro comando que
            // precisa de uma, e avisa por ENVCHANGE -- e' o que o JDBC faz em
            // setAutoCommit(false).
            OTTER_RETURN_IF_ERROR(run_silent("SET IMPLICIT_TRANSACTIONS ON"));
        }
        auto_commit_ = enabled;
        clear_changes();
        return {};
    }

    [[nodiscard]] TxnState txn_state() const noexcept override {
        return conn_.in_transaction() ? TxnState::active : TxnState::idle;
    }

    Status commit() override {
        OTTER_RETURN_IF_ERROR(run_silent("IF @@TRANCOUNT > 0 COMMIT TRANSACTION"));
        clear_changes();
        return {};
    }

    Status rollback() override {
        OTTER_RETURN_IF_ERROR(run_silent("IF @@TRANCOUNT > 0 ROLLBACK TRANSACTION"));
        clear_changes();
        return {};
    }

    Status savepoint(std::string_view name) override {
        return run_silent("SAVE TRANSACTION " + bracket(name));
    }

    Status rollback_to(std::string_view name) override {
        return run_silent("ROLLBACK TRANSACTION " + bracket(name));
    }

    // O SQL Server nao tem RELEASE SAVEPOINT: o ponto some com a transacao.
    Status release_savepoint(std::string_view) override { return {}; }

    [[nodiscard]] Result<IsolationLevel> isolation_level() override {
        OTTER_ASSIGN_OR_RETURN(
            auto rs, run("SELECT transaction_isolation_level FROM sys.dm_exec_sessions "
                         "WHERE session_id = @@SPID", true));
        if (rs.row_count() == 0) return IsolationLevel::read_committed;

        const std::string_view level = rs.text(0, 0);
        if (level == "1") return IsolationLevel::read_uncommitted;
        if (level == "3") return IsolationLevel::repeatable_read;
        if (level == "4") return IsolationLevel::serializable;
        // 2 = READ COMMITTED, o padrao; 5 = SNAPSHOT, que nao esta' nos quatro
        // do padrao SQL e e' mostrado como o mais proximo.
        return IsolationLevel::read_committed;
    }

    Status set_isolation_level(IsolationLevel level) override {
        return run_silent("SET TRANSACTION ISOLATION LEVEL " + std::string(to_string(level)));
    }

    [[nodiscard]] Capabilities capabilities() const noexcept override {
        Capabilities caps;
        caps.transactions       = true;
        caps.savepoints         = true;
        caps.ddl_in_transaction = true;    // CREATE TABLE desfaz com ROLLBACK
        caps.server_cursors     = false;
        caps.binary_transfer    = true;
        caps.multiple_results   = true;
        caps.arrays             = false;
        caps.cancel_query       = true;
        caps.explain_plan       = false;   // SHOWPLAN_XML: ainda nao lido
        return caps;
    }

    [[nodiscard]] std::string server_version() const override {
        return conn_.server_version();
    }

    [[nodiscard]] std::string secure_channel() const override {
        // So' o login cifrado NAO conta: os dados trafegam em claro, e o
        // cadeado na barra de estado diria o contrario.
        if (!conn_.tls_active()) return {};
        const auto& info = conn_.tls_info();
        std::string text = info.protocol;
        if (!info.cipher.empty()) text += ", " + info.cipher;
        return text;
    }

    [[nodiscard]] std::string current_schema() const override { return schema_; }
    [[nodiscard]] std::string current_database() const override {
        return conn_.database();
    }

    [[nodiscard]] std::vector<std::string> take_server_output() override {
        std::vector<std::string> out = std::move(output_);
        output_.clear();
        return out;
    }
    [[nodiscard]] bool reports_server_output() const noexcept override { return true; }

    Status load_session_defaults() {
        if (auto rs = run("SELECT SCHEMA_NAME()", true); rs && rs->row_count() > 0) {
            schema_ = std::string(rs->text(0, 0));
        }
        return {};
    }

private:
    Status run_silent(std::string_view sql) {
        auto result = run(sql, /*internal=*/true);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    struct Origin {
        std::string schema;
        std::string table;
        std::string column;
    };

    // De que tabela cada coluna do SELECT vem, pela ordem. Vazio quando o
    // servidor nao sabe dizer (SQL dinamico, tabela temporaria) -- e ai' a
    // grade fica somente leitura, que e' a resposta honesta.
    std::vector<Origin> describe_origins(std::string_view sql) {
        std::vector<Origin> origins;
        bool wanted = false;
        (void)conn_.query(
            "EXEC sp_describe_first_result_set @tsql = " + n_literal(sql) +
                ", @params = NULL, @browse_information_mode = 1",
            [&wanted](const std::vector<tdswire::Column>& columns) {
                // is_hidden(1) ... source_schema(24) source_table(25)
                // source_column(26): confere pelo NOME, a ordem pode mudar
                // entre versoes.
                wanted = columns.size() > 26 && columns[25].name == "source_table";
            },
            [&](std::span<const tdswire::Value> row) {
                if (!wanted) return;
                // As colunas de chave que o modo browse acrescenta vem
                // marcadas como ocultas: nao estao no resultado de verdade.
                if (!row[0].null && row[0].text == "1") return;
                Origin origin;
                if (!row[24].null) origin.schema = std::string(row[24].text);
                if (!row[25].null) origin.table = std::string(row[25].text);
                if (!row[26].null) origin.column = std::string(row[26].text);
                origins.push_back(std::move(origin));
            });
        (void)conn_.take_messages();
        return origins;
    }

    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "connection is closed");

        const auto started = std::chrono::steady_clock::now();

        std::vector<Origin> origins;
        if (!internal && looks_like_select(sql)) origins = describe_origins(sql);

        ResultSetBuilder builder;
        std::size_t rows = 0;
        int result_sets = 0;

        const Status status = conn_.query(
            sql,
            [&](const std::vector<tdswire::Column>& columns) {
                // Um lote pode devolver varios conjuntos; a grade mostra o
                // PRIMEIRO. Os outros sao lidos e descartados -- o fluxo
                // precisa chegar ao fim de qualquer modo.
                if (++result_sets != 1) return;
                for (std::size_t c = 0; c < columns.size(); ++c) {
                    const tdswire::Column& column = columns[c];
                    ColumnInfo info;
                    info.name      = column.name;
                    info.type_name = column.type_name;
                    info.kind      = kind_from_type(column.type);
                    info.type_oid  = static_cast<std::uint32_t>(column.type.id);
                    info.size      = column.type.plp
                                         ? -1
                                         : static_cast<std::int32_t>(column.type.max_length);
                    info.precision = column.type.precision;
                    info.scale     = column.type.scale;
                    info.nullable  = column.nullable;
                    if (c < origins.size() && origins.size() == columns.size()) {
                        info.source_schema      = origins[c].schema;
                        info.source_table       = origins[c].table;
                        info.source_column_name = origins[c].column;
                    }
                    builder.add_column(std::move(info));
                }
            },
            [&](std::span<const tdswire::Value> row) {
                if (result_sets != 1) return;
                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].null) {
                        builder.append_null(c);
                    } else {
                        builder.append_text(c, row[c].text);
                    }
                }
                ++rows;
            });

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);

        // PRINT e avisos do servidor: o painel de saida os mostra.
        for (tdswire::Message& message : conn_.take_messages()) {
            if (!internal) output_.push_back(std::move(message.text));
        }

        if (!status) {
            record(QueryLog{std::string(sql), elapsed, 0, internal, true,
                            status.error().message()});
            return std::unexpected(status.error());
        }

        builder.set_row_count(rows);
        builder.set_affected_rows(conn_.last_affected_rows());

        if (!internal && !auto_commit_ && conn_.last_affected_rows() > 0 && rows == 0) {
            note_change();
        }

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});
        return builder.take();
    }

    tdswire::Connection      conn_;
    std::string              schema_ = "dbo";
    bool                     auto_commit_ = true;
    std::vector<std::string> output_;
};

class MssqlDriver final : public Driver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "sqlserver"; }
    [[nodiscard]] std::string_view display_name() const noexcept override {
        return "SQL Server";
    }
    [[nodiscard]] std::uint16_t default_port() const noexcept override { return 1433; }

    [[nodiscard]] Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) override {
        // "host\instancia": a porta de uma instancia nomeada e' dinamica, e
        // quem a informa e' o SQL Server Browser do servidor (UDP 1434). Uma
        // porta diferente da padrao no perfil vale mais que o nome -- e' o
        // que os clientes da Microsoft fazem, e a saida quando o Browser
        // esta' desligado ou atras de um firewall.
        std::string   host = config.host;
        std::uint16_t port = config.port != 0 ? config.port : 1433;

        if (const std::size_t slash = host.find('\\'); slash != std::string::npos) {
            const std::string instance = host.substr(slash + 1);
            host.erase(slash);

            if (port == 1433 && !instance.empty()) {
                // Curto: sem Browser nao vem resposta nenhuma (UDP), e o
                // tempo inteiro da conexao aqui pareceria travamento.
                const auto wait = (std::min)(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        config.connect_timeout),
                    std::chrono::milliseconds(3000));
                // OTTER_SQLBROWSER_PORT: so' para a conferencia, contra
                // tools/sqlbrowser_test_server.py -- o Browser de verdade
                // costuma estar desligado em maquina de desenvolvimento.
                std::uint16_t browser_port = tdswire::kBrowserPort;
                if (const char* override_port = std::getenv("OTTER_SQLBROWSER_PORT")) {
                    const int value = std::atoi(override_port);
                    if (value > 0 && value <= 65535) {
                        browser_port = static_cast<std::uint16_t>(value);
                    }
                }
                auto resolved =
                    tdswire::resolve_instance_port(host, instance, wait, browser_port);
                if (!resolved) {
                    // Tentar a 1433 conectaria na instancia PADRAO -- outro
                    // banco de dados. Falhar dizendo o que fazer e' melhor.
                    return fail(Errc::connection_failed,
                                "could not find the instance '" + instance + "' on " + host +
                                    ": " + resolved.error().message() +
                                    ". The SQL Server Browser service (UDP 1434) must be "
                                    "running and reachable, or set the TCP port of the "
                                    "instance in the connection");
                }
                port = *resolved;
            }
        }

        tdswire::ConnectParams params;
        params.host       = host;
        params.port       = port;
        params.database   = config.database;
        params.user       = config.user;
        params.password   = config.password;
        params.integrated = config.integrated_auth;
        params.timeout    = std::chrono::duration_cast<std::chrono::milliseconds>(
            config.connect_timeout);

        params.proxy.host     = config.proxy_host;
        params.proxy.port     = config.proxy_port;
        params.proxy.user     = config.proxy_user;
        params.proxy.password = config.proxy_password;

        // Aba SSL marcada = a conexao INTEIRA cifrada. Desmarcada, so' o
        // pacote de login e' cifrado -- a senha nunca vai em claro.
        params.encrypt            = config.ssl_enabled();
        params.verify_certificate = config.ssl_verifies_certificate();

        OTTER_ASSIGN_OR_RETURN(auto conn, tdswire::Connection::connect(params));
        auto holt = std::unique_ptr<MssqlHolt>(new MssqlHolt(std::move(conn)));
        OTTER_RETURN_IF_ERROR(holt->load_session_defaults());

        if (config.isolation_level.has_value()) {
            OTTER_RETURN_IF_ERROR(holt->set_isolation_level(*config.isolation_level));
        }
        return std::unique_ptr<Holt>(std::move(holt));
    }
};

} // namespace

Driver& mssql_driver() {
    static MssqlDriver driver;
    return driver;
}

} // namespace otter::db
