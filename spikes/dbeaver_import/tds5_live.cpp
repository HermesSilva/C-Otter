// Spike: o protocolo TDS 5.0 contra um SQL Anywhere de verdade.
//
//     spike_tds5_live [host] [porta] [--db BANCO] [--user U] ["<sql>" ...]
//
// A senha vem de OTTER_SA_PASSWORD (nao da linha de comando). Sem SQL, roda um
// roteiro de leitura que exercita cada tipo.
#include "tdswire/tds5.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace otter::tdswire;

int main(int argc, char** argv) {
    Tds5ConnectParams params;
    params.user = "DBA";
    if (const char* password = std::getenv("OTTER_SA_PASSWORD")) params.password = password;

    std::vector<std::string> statements;
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) params.database = argv[++i];
        else if (arg == "--user" && i + 1 < argc) params.user = argv[++i];
        else if (positional == 0) { params.host = arg; ++positional; }
        else if (positional == 1) {
            params.port = static_cast<std::uint16_t>(std::atoi(arg.c_str()));
            ++positional;
        }
        else statements.push_back(arg);
    }

    auto connection = Tds5Connection::connect(params);
    if (!connection) {
        std::fprintf(stderr, "conexao falhou: %s\n", connection.error().to_string().c_str());
        return 2;
    }
    std::printf("servidor %s %s | banco %s\n", connection->server_name().c_str(),
                connection->server_version().c_str(), connection->database().c_str());

    const auto run = [&](const char* sql) {
        std::printf("\n> %s\n", sql);
        std::size_t rows = 0;
        const auto status = connection->query(
            sql,
            [](const std::vector<Tds5Column>& columns) {
                for (const Tds5Column& column : columns) {
                    std::printf("%s:%s[0x%02X u%u len%u]", column.name.c_str(),
                                column.type_name.c_str(), static_cast<unsigned>(column.type),
                                column.user_type, column.max_length);
                    if (!column.table.empty()) {
                        std::printf("<%s.%s.%s>", column.schema.c_str(), column.table.c_str(),
                                    column.column.c_str());
                    }
                    std::printf("%s%s  ", column.key ? "*" : "", column.nullable ? "?" : "");
                }
                std::printf("\n");
            },
            [&rows, &statements](std::span<const Value> values) {
                // O roteiro corta em 12 linhas; uma consulta pedida mostra tudo.
                if (++rows > 12 && statements.empty()) return;
                for (const Value& value : values) {
                    std::printf("%.*s | ", value.null ? 6 : static_cast<int>(value.text.size()),
                                value.null ? "[null]" : value.text.data());
                }
                std::printf("\n");
            });
        if (!status) std::printf("ERRO: %s\n", status.error().to_string().c_str());
        else std::printf("(%zu linhas, afetadas %lld, em transacao: %s)\n", rows,
                         static_cast<long long>(connection->last_affected_rows()),
                         connection->in_transaction() ? "sim" : "nao");
        for (const Message& message : connection->take_messages()) {
            std::printf("INFO: %s\n", message.text.c_str());
        }
    };

    if (!statements.empty()) {
        for (const std::string& sql : statements) run(sql.c_str());
        return 0;
    }

    run("SELECT @@version AS v, db_name() AS db, user_name() AS usr, @@spid AS spid");
    run("SELECT CAST(200 AS tinyint) t, CAST(-2 AS smallint) s, CAST(3 AS int) i, "
        "CAST(-4 AS bigint) b, CAST(1 AS bit) bt, CAST(1.5 AS real) r, CAST(2.25 AS double) f, "
        "CAST(12.3456 AS money) m, CAST(-0.5 AS smallmoney) sm, "
        "CAST(123.4500 AS decimal(10,4)) d, CAST(-99999999999999999999.99 AS numeric(38,2)) n");
    run("SELECT CAST(60000 AS unsigned smallint) us, CAST(4000000000 AS unsigned int) ui, "
        "CAST(18000000000000000000 AS unsigned bigint) ub");
    run("SELECT CAST('2026-10-01 13:45:30.123456' AS timestamp) ts, "
        "CAST('2026-10-01' AS date) d, CAST('13:45:30.123456' AS time) t, "
        "CAST('2026-10-01 13:45' AS smalldatetime) sdt, CAST('0001-01-01' AS date) dmin, "
        "CAST('2026-10-01 13:45:30.5' AS datetime) dt");
    run("SELECT 'a\xC3\xA7\xC3\xA3o \xE2\x82\xAC' v, CAST('abc' AS char(5)) c, "
        "CAST('nv \xC3\xA7' AS nvarchar(20)) nv, CAST('x' AS long varchar) lv, "
        "CAST(0x0102FF AS varbinary(8)) vb, CAST(0xCAFE AS long binary) lb, "
        "CAST(NULL AS int) nul, newid() g, CAST('<a>1</a>' AS xml) x, '' vazio");
    run("SELECT FIRST * FROM GROUPO.Customers ORDER BY ID");
    run("SELECT TOP 3 START AT 2 ID, Surname AS sobrenome, GivenName FROM GROUPO.Customers ORDER BY ID");
    run("MESSAGE 'ola do servidor' TO CLIENT; SELECT 1 AS um; SELECT 2 AS dois");
    run("SELECT * FROM tabela_que_nao_existe");
    run("SELECT connection_property('chained') chained, connection_property('quoted_identifier') qi, "
        "connection_property('isolation_level') iso, connection_property('CharSet') cs, "
        "connection_property('CommProtocol') proto");
    run("BEGIN TRANSACTION");
    run("SELECT @@trancount AS trancount");
    run("ROLLBACK");
    run("SELECT TOP 5000 a.table_id, a.table_name, repeat('x', 200) pad "
        "FROM SYS.SYSTAB a CROSS JOIN SYS.SYSTAB b");
    return 0;
}
