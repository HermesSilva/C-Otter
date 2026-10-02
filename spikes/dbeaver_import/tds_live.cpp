// Spike: o protocolo TDS contra um SQL Server de verdade.
//
//     spike_tds_live [host] [porta] [--encrypt] [--user U --password P]
//     spike_tds_live <host> --instance NOME [--browser-port N]
//
// Sem usuario, entra pela conta do Windows (SSPI). So' consultas de leitura.
// Com --instance, a porta vem do SQL Server Browser (UDP 1434, ou a porta de
// tools/sqlbrowser_test_server.py).
#include "tdswire/browser.hpp"
#include "tdswire/connection.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace otter::tdswire;

int main(int argc, char** argv) {
    ConnectParams params;
    params.integrated = true;
    int positional = 0;
    std::string   instance;
    std::uint16_t browser_port = kBrowserPort;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--encrypt") params.encrypt = true;
        else if (arg == "--user" && i + 1 < argc) { params.user = argv[++i]; params.integrated = false; }
        else if (arg == "--password" && i + 1 < argc) params.password = argv[++i];
        else if (arg == "--db" && i + 1 < argc) params.database = argv[++i];
        else if (arg == "--instance" && i + 1 < argc) instance = argv[++i];
        else if (arg == "--browser-port" && i + 1 < argc) {
            browser_port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        }
        else if (positional == 0) { params.host = arg; ++positional; }
        else if (positional == 1) { params.port = static_cast<std::uint16_t>(std::atoi(arg.c_str())); ++positional; }
        else { params.database.clear(); std::string sql = arg; (void)sql; }
    }

    if (!instance.empty()) {
        const auto port = resolve_instance_port(params.host, instance,
                                                std::chrono::milliseconds(2000), browser_port);
        if (!port) {
            std::fprintf(stderr, "instancia '%s' nao resolvida: %s\n", instance.c_str(),
                         port.error().to_string().c_str());
            return 3;
        }
        std::printf("instancia %s -> porta %u\n", instance.c_str(),
                    static_cast<unsigned>(*port));
        params.port = *port;
    }

    auto connection = Connection::connect(params);
    if (!connection) {
        std::fprintf(stderr, "conexao falhou: %s\n", connection.error().to_string().c_str());
        return 2;
    }
    std::printf("servidor %s %s | banco %s | TLS inteiro: %s | login cifrado: %s\n",
                connection->server_name().c_str(), connection->server_version().c_str(),
                connection->database().c_str(), connection->tls_active() ? "sim" : "nao",
                connection->login_encrypted() ? "sim" : "nao");

    const auto run = [&](const char* sql) {
        std::printf("\n> %s\n", sql);
        std::size_t rows = 0;
        const auto status = connection->query(
            sql,
            [](const std::vector<Column>& columns) {
                for (const Column& column : columns) {
                    std::printf("%s:%s  ", column.name.c_str(), column.type_name.c_str());
                }
                std::printf("\n");
            },
            [&rows](std::span<const Value> values) {
                if (++rows > 12) return;
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

    run("SELECT @@SPID AS spid, SUSER_SNAME() AS login, DB_NAME() AS db, @@VERSION AS v");
    run("SELECT CAST(1 AS tinyint) t, CAST(-2 AS smallint) s, CAST(3 AS int) i, CAST(-4 AS bigint) b, "
        "CAST(1 AS bit) bt, CAST(1.5 AS real) r, CAST(2.25 AS float) f, CAST(12.3456 AS money) m, "
        "CAST(-0.5 AS smallmoney) sm, CAST(123.4500 AS decimal(10,4)) d, CAST(-99999999999999999999.99 AS numeric(38,2)) n");
    run("SELECT CAST('2026-10-01 13:45:30.123' AS datetime) dt, CAST('2026-10-01 13:45' AS smalldatetime) sdt, "
        "CAST('2026-10-01' AS date) d, CAST('13:45:30.1234567' AS time(7)) t, "
        "CAST('2026-10-01 13:45:30.5' AS datetime2(3)) dt2, "
        "CAST('2026-10-01 13:45:30 -03:00' AS datetimeoffset(0)) dto, "
        "CAST('0001-01-01' AS date) dmin, CAST('1753-01-01' AS datetime) dtmin");
    run("SELECT N'a\u00e7\u00e3o \u20ac' nv, CAST('a\u00e7\u00e3o' AS varchar(20)) v, CAST('abc' AS char(5)) c, "
        "CAST(N'x' AS nvarchar(max)) nmax, CAST(0x0102FF AS varbinary(8)) vb, "
        "CAST('6F9619FF-8B86-D011-B42D-00C04FC964FF' AS uniqueidentifier) g, "
        "CAST(NULL AS int) nul, CAST('<a>1</a>' AS xml) x, CAST(42 AS sql_variant) sv, "
        "CAST(N'txt' AS sql_variant) svt");
    run("SELECT TOP 3 name, database_id, create_date FROM sys.databases ORDER BY database_id");
    run("PRINT 'ola do servidor'; SELECT 1 AS um; SELECT 2 AS dois");
    run("SELECT * FROM tabela_que_nao_existe");
    run("BEGIN TRANSACTION");
    run("SELECT @@TRANCOUNT AS trancount");
    run("ROLLBACK");
    run("SELECT TOP 5000 a.object_id, a.name, REPLICATE(N'x', 200) pad FROM sys.all_objects a CROSS JOIN sys.all_objects b");
    return 0;
}
