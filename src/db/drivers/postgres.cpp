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

    [[nodiscard]] std::string current_schema() const override { return schema_; }

private:
    Result<ResultSet> run(std::string_view sql, bool internal) {
        if (!conn_.is_open()) return fail(Errc::closed, "conexão fechada");

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
                builder.add_column(std::move(info));
            }
        }

        builder.set_row_count(rows);
        builder.set_affected_rows(conn_.last_affected_rows());

        record(QueryLog{std::string(sql), elapsed, rows, internal, false, {}});
        return builder.take();
    }

    pgwire::Connection conn_;
    std::string        schema_ = "public";
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

        OTTER_ASSIGN_OR_RETURN(auto conn, pgwire::Connection::connect(params));
        return std::unique_ptr<Holt>(new PostgresHolt(std::move(conn)));
    }
};

} // namespace

Driver& postgres_driver() {
    static PostgresDriver driver;
    return driver;
}

} // namespace otter::db
