// Driver PostgreSQL sobre lib/pgwire -- protocolo v3 nativo, sem libpq
// (ADR 0009). Este arquivo adapta o protocolo ao contrato Holt/Driver.
#include "db/drivers/postgres.hpp"

#include "pgwire/connection.hpp"

#include <chrono>
#include <string>
#include <unordered_map>

namespace otter::db {
namespace {

// OIDs de pg_type. Estaveis desde sempre -- sao parte do contrato do
// PostgreSQL e nao mudam entre versoes (compatibilidade retroativa).
enum : std::uint32_t {
    kOidBool = 16,       kOidBytea = 17,      kOidChar = 18,
    kOidName = 19,       kOidInt8 = 20,       kOidInt2 = 21,
    kOidInt4 = 23,       kOidText = 25,       kOidOid = 26,
    kOidJson = 114,      kOidXml = 142,       kOidPoint = 600,
    kOidFloat4 = 700,    kOidFloat8 = 701,    kOidMoney = 790,
    kOidInet = 869,      kOidBpchar = 1042,   kOidVarchar = 1043,
    kOidDate = 1082,     kOidTime = 1083,     kOidTimestamp = 1114,
    kOidTimestampTz = 1184, kOidInterval = 1186, kOidTimeTz = 1266,
    kOidNumeric = 1700,  kOidUuid = 2950,     kOidJsonb = 3802,
};

DataKind kind_from_oid(std::uint32_t oid) noexcept {
    switch (oid) {
        case kOidBool:                            return DataKind::boolean;
        case kOidInt2: case kOidInt4: case kOidInt8:
        case kOidOid:                             return DataKind::integer;
        case kOidFloat4: case kOidFloat8:         return DataKind::floating;
        case kOidNumeric: case kOidMoney:         return DataKind::numeric;
        case kOidBytea:                           return DataKind::binary;
        case kOidDate:                            return DataKind::date;
        case kOidTime: case kOidTimeTz:           return DataKind::time;
        case kOidTimestamp: case kOidTimestampTz: return DataKind::timestamp;
        case kOidInterval:                        return DataKind::interval;
        case kOidUuid:                            return DataKind::uuid;
        case kOidJson: case kOidJsonb:            return DataKind::json;
        case kOidPoint:                           return DataKind::geometry;
        case kOidChar: case kOidName: case kOidText:
        case kOidBpchar: case kOidVarchar:
        case kOidXml: case kOidInet:              return DataKind::string;
        default:                                  return DataKind::unknown;
    }
}

std::string type_name_for(std::uint32_t oid) {
    static const std::unordered_map<std::uint32_t, const char*> names = {
        {kOidBool, "bool"},      {kOidBytea, "bytea"},   {kOidChar, "char"},
        {kOidName, "name"},      {kOidInt8, "int8"},     {kOidInt2, "int2"},
        {kOidInt4, "int4"},      {kOidText, "text"},     {kOidOid, "oid"},
        {kOidJson, "json"},      {kOidXml, "xml"},       {kOidPoint, "point"},
        {kOidFloat4, "float4"},  {kOidFloat8, "float8"}, {kOidMoney, "money"},
        {kOidInet, "inet"},      {kOidBpchar, "bpchar"}, {kOidVarchar, "varchar"},
        {kOidDate, "date"},      {kOidTime, "time"},     {kOidTimestamp, "timestamp"},
        {kOidTimestampTz, "timestamptz"}, {kOidInterval, "interval"},
        {kOidTimeTz, "timetz"},  {kOidNumeric, "numeric"}, {kOidUuid, "uuid"},
        {kOidJsonb, "jsonb"},
    };
    const auto it = names.find(oid);
    return it != names.end() ? it->second : ("oid:" + std::to_string(oid));
}

class PostgresHolt final : public Holt {
public:
    explicit PostgresHolt(pgwire::Connection conn) : conn_(std::move(conn)) {}

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

    Status cancel() override { return conn_.cancel_current_query(); }

    [[nodiscard]] std::vector<std::string> take_server_output() override {
        return conn_.take_notices();
    }
    [[nodiscard]] bool reports_server_output() const noexcept override {
        return true;
    }

    // --- Transacoes ---------------------------------------------------------
    //
    // O PostgreSQL nao tem um "modo autocommit" no protocolo: ele esta' sempre
    // em autocommit, a menos que um BEGIN explicito abra uma transacao. Entao
    // desligar autocommit significa abrir uma transacao e reabri-la apos cada
    // commit ou rollback.

    [[nodiscard]] bool auto_commit() const noexcept override {
        return auto_commit_;
    }

    Status set_auto_commit(bool enabled) override {
        if (enabled == auto_commit_) return {};

        if (enabled) {
            // Saindo do modo manual: encerra a transacao aberta. Commit, e nao
            // rollback -- descartar trabalho do usuario sem pedir seria pior.
            if (conn_.transaction_status() != pgwire::TransactionStatus::idle) {
                OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
            }
            auto_commit_ = true;
            clear_changes();
            return {};
        }

        auto_commit_ = false;
        return begin_if_needed();
    }

    [[nodiscard]] TxnState txn_state() const noexcept override {
        switch (conn_.transaction_status()) {
            case pgwire::TransactionStatus::in_block: return TxnState::active;
            case pgwire::TransactionStatus::failed:   return TxnState::failed;
            case pgwire::TransactionStatus::idle:     break;
        }
        return TxnState::idle;
    }

    Status commit() override {
        if (conn_.transaction_status() == pgwire::TransactionStatus::idle) {
            return {};   // nada a confirmar
        }
        OTTER_RETURN_IF_ERROR(run_silent("COMMIT"));
        clear_changes();
        return begin_if_needed();
    }

    Status rollback() override {
        if (conn_.transaction_status() == pgwire::TransactionStatus::idle) {
            return {};
        }
        OTTER_RETURN_IF_ERROR(run_silent("ROLLBACK"));
        clear_changes();
        return begin_if_needed();
    }

    Status savepoint(std::string_view name) override {
        OTTER_RETURN_IF_ERROR(begin_if_needed());
        return run_silent("SAVEPOINT " + quote_identifier(name));
    }

    Status rollback_to(std::string_view name) override {
        return run_silent("ROLLBACK TO SAVEPOINT " + quote_identifier(name));
    }

    Status release_savepoint(std::string_view name) override {
        return run_silent("RELEASE SAVEPOINT " + quote_identifier(name));
    }

    [[nodiscard]] Result<IsolationLevel> isolation_level() override {
        OTTER_ASSIGN_OR_RETURN(auto rs,
                               run("SHOW transaction_isolation", true));
        if (rs.row_count() == 0) return IsolationLevel::read_committed;

        const std::string_view value = rs.text(0, 0);
        if (value == "read uncommitted") return IsolationLevel::read_uncommitted;
        if (value == "repeatable read")  return IsolationLevel::repeatable_read;
        if (value == "serializable")     return IsolationLevel::serializable;
        return IsolationLevel::read_committed;
    }

    Status set_isolation_level(IsolationLevel level) override {
        // SESSION CHARACTERISTICS vale para as proximas transacoes; aplicar
        // dentro de uma transacao aberta afetaria so' ela.
        return run_silent(
            "SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL " +
            std::string(to_string(level)));
    }

    [[nodiscard]] Capabilities capabilities() const noexcept override {
        Capabilities caps;
        caps.transactions       = true;
        caps.savepoints         = true;
        caps.ddl_in_transaction = true;   // vantagem real do PostgreSQL
        caps.server_cursors     = true;
        caps.binary_transfer    = true;
        caps.arrays             = true;
        caps.cancel_query       = true;
        caps.explain_plan       = true;
        return caps;
    }

    [[nodiscard]] std::string server_version() const override {
        return conn_.server_version();
    }

    [[nodiscard]] std::string secure_channel() const override {
        if (!conn_.tls_active()) return {};
        const auto& info = conn_.tls_info();
        std::string text = info.protocol;
        if (!info.cipher.empty()) text += ", " + info.cipher;
        return text;
    }

    [[nodiscard]] std::string current_schema() const override { return schema_; }

    [[nodiscard]] Result<ResultSet> query_internal(std::string_view sql) override {
        return run(sql, /*internal=*/true);
    }

private:
    // Abre transacao quando estamos em modo manual e nao ha' uma aberta.
    Status begin_if_needed() {
        if (auto_commit_) return {};
        if (conn_.transaction_status() != pgwire::TransactionStatus::idle) {
            return {};
        }
        return run_silent("BEGIN");
    }

    // Comando de controle: registrado no log como interno, sem resultado.
    Status run_silent(std::string_view sql) {
        auto result = run(sql, /*internal=*/true);
        if (!result) return std::unexpected(result.error());
        return {};
    }

    // Delimita um identificador para SAVEPOINT. O nome vem da UI, entao nao
    // pode ser interpolado cru.
    static std::string quote_identifier(std::string_view name) {
        std::string out;
        out.reserve(name.size() + 2);
        out.push_back('"');
        for (char c : name) {
            if (c == '"') out.push_back('"');
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "conexão fechada");

        // Em modo manual, a primeira query do usuario precisa de um BEGIN.
        // Comandos internos (incluindo o proprio BEGIN) nao recursam aqui.
        if (!internal && !auto_commit_) {
            OTTER_RETURN_IF_ERROR(begin_if_needed());
        }

        const auto started = std::chrono::steady_clock::now();

        ResultSetBuilder builder;
        std::vector<pgwire::FieldDescription> fields;
        bool columns_ready = false;
        std::size_t rows = 0;

        const Status status = conn_.query(
            sql,
            [&](std::span<const pgwire::RawValue> row) {
                // As colunas so' existem apos RowDescription; criamos na
                // primeira linha, quando `fields` ja' esta' preenchido.
                if (!columns_ready) {
                    for (const pgwire::FieldDescription& field : fields) {
                        ColumnInfo info;
                        info.name      = field.name;
                        info.type_oid  = field.type_oid;
                        info.kind      = kind_from_oid(field.type_oid);
                        info.type_name = type_name_for(field.type_oid);
                        info.size      = field.type_size;

                        // Origem da coluna, para a grade editavel (ADR 0014).
                        info.source_table_oid = field.table_oid;
                        info.source_column    = field.column_id;

                        // typmod carrega precisao e escala de numeric.
                        if (info.kind == DataKind::numeric && field.type_modifier > 4) {
                            const std::int32_t mod = field.type_modifier - 4;
                            info.precision = static_cast<std::int16_t>((mod >> 16) & 0xFFFF);
                            info.scale     = static_cast<std::int16_t>(mod & 0xFFFF);
                        }
                        builder.add_column(std::move(info));
                    }
                    columns_ready = true;
                }

                for (std::size_t c = 0; c < row.size(); ++c) {
                    if (row[c].null) {
                        builder.append_null(c);
                    } else {
                        builder.append(c, row[c].data);
                    }
                }
                ++rows;
            },
            &fields);

        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);

        if (!status) {
            record(QueryLog{std::string(sql), elapsed, 0, internal, true,
                            status.error().message()});
            return std::unexpected(status.error());
        }

        // Query sem linhas mas com colunas: registramos as colunas mesmo assim,
        // para a grade saber o formato do resultado vazio.
        if (!columns_ready) {
            for (const pgwire::FieldDescription& field : fields) {
                ColumnInfo info;
                info.name      = field.name;
                info.type_oid  = field.type_oid;
                info.kind      = kind_from_oid(field.type_oid);
                info.type_name = type_name_for(field.type_oid);
                info.source_table_oid = field.table_oid;
                info.source_column    = field.column_id;
                builder.add_column(std::move(info));
            }
        }

        builder.set_row_count(rows);
        builder.set_affected_rows(conn_.last_affected_rows());

        // Comando do usuario que alterou linhas fora de auto-commit: conta como
        // trabalho pendente, para a UI avisar antes de descartar.
        if (!internal && !auto_commit_ && conn_.last_affected_rows() > 0 &&
            rows == 0) {
            note_change();
        }

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});
        track_search_path(sql);
        return builder.take();
    }

    // `SET search_path TO x, ...` muda o schema corrente, e a barra de status
    // seguia dizendo "public": o campo era fixo. O primeiro nome da lista e'
    // o schema em que um CREATE sem qualificar cai -- e' ele que se mostra.
    void track_search_path(std::string_view sql) {
        constexpr std::string_view kPrefix = "set search_path";
        std::size_t i = 0;
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])) != 0) ++i;
        if (sql.size() - i < kPrefix.size()) return;
        for (std::size_t k = 0; k < kPrefix.size(); ++k) {
            if (std::tolower(static_cast<unsigned char>(sql[i + k])) != kPrefix[k]) return;
        }
        i += kPrefix.size();

        // Pula " TO " ou " = ".
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])) != 0) ++i;
        if (i < sql.size() && sql[i] == '=') {
            ++i;
        } else if (i + 1 < sql.size() &&
                   std::tolower(static_cast<unsigned char>(sql[i])) == 't' &&
                   std::tolower(static_cast<unsigned char>(sql[i + 1])) == 'o') {
            i += 2;
        } else {
            return;
        }
        while (i < sql.size() && std::isspace(static_cast<unsigned char>(sql[i])) != 0) ++i;
        if (i >= sql.size()) return;

        std::string name;
        if (sql[i] == '"') {
            for (++i; i < sql.size(); ++i) {
                if (sql[i] == '"') {
                    if (i + 1 < sql.size() && sql[i + 1] == '"') { name += '"'; ++i; continue; }
                    break;
                }
                name += sql[i];
            }
        } else {
            while (i < sql.size() && sql[i] != ',' && sql[i] != ';' &&
                   std::isspace(static_cast<unsigned char>(sql[i])) == 0) {
                name += static_cast<char>(std::tolower(static_cast<unsigned char>(sql[i])));
                ++i;
            }
        }
        if (!name.empty() && name != "default") schema_ = std::move(name);
    }

    pgwire::Connection conn_;
    std::string        schema_ = "public";
    bool               auto_commit_ = true;
};

class PostgresDriver final : public Driver {
public:
    [[nodiscard]] std::string_view id() const noexcept override { return "postgresql"; }
    [[nodiscard]] std::string_view display_name() const noexcept override {
        return "PostgreSQL";
    }
    [[nodiscard]] std::uint16_t default_port() const noexcept override { return 5432; }

    [[nodiscard]] Result<std::unique_ptr<Holt>> connect(
        const ConnConfig& config) override {
        pgwire::ConnectParams params;
        params.host     = config.host;
        params.port     = config.port;
        params.database = config.database;
        params.user     = config.user;
        params.password = config.password;
        params.timeout  = std::chrono::duration_cast<std::chrono::milliseconds>(
            config.connect_timeout);

        params.proxy.host     = config.proxy_host;
        params.proxy.port     = config.proxy_port;
        params.proxy.user     = config.proxy_user;
        params.proxy.password = config.proxy_password;

        // `require` implica exigir: se o servidor nao oferecer TLS, a conexao
        // falha em vez de cair em claro sem avisar.
        // As propriedades do perfil viram parametros de runtime da
        // StartupMessage -- search_path, statement_timeout, TimeZone e
        // qualquer outro GUC alteravel na conexao.
        params.runtime_params.reserve(config.driver_properties.size());
        for (const auto& [name, value] : config.driver_properties) {
            params.runtime_params.emplace_back(name, value);
        }

        params.use_tls     = config.ssl_enabled();
        params.require_tls = config.ssl_enabled();
        params.allow_invalid_certificate = !config.ssl_verifies_certificate();

        OTTER_ASSIGN_OR_RETURN(auto conn, pgwire::Connection::connect(params));
        auto holt = std::unique_ptr<PostgresHolt>(
            new PostgresHolt(std::move(conn)));

        // Nivel de isolamento do perfil. Um nivel que o servidor nao aceita
        // FALHA a conexao, nomeando-o -- melhor que uma sessao que ignorou o
        // pedido em silencio.
        if (config.isolation_level.has_value()) {
            OTTER_RETURN_IF_ERROR(
                holt->set_isolation_level(*config.isolation_level));
        }

        return std::unique_ptr<Holt>(std::move(holt));
    }
};

} // namespace

Driver& postgres_driver() {
    static PostgresDriver driver;
    return driver;
}

} // namespace otter::db
