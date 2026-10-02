// Spike: conexao real ao Oracle falando lib/orawire.
//
// Existe pela mesma razao do spike_myconnect: teste unitario prova a
// aritmetica, nao o protocolo. Aqui o protocolo nem tem especificacao publica
// (ADR 0027) -- so' o servidor diz se a mensagem esta' certa (diretiva 2).
//
//   $env:ORA_PASSWORD = "..."                      # ORA_HOST, ORA_PORT,
//   build\win-release\bin\spike_oraconnect.exe     # ORA_SERVICE, ORA_USER
//   build\win-release\bin\spike_oraconnect.exe "select * from dual" "..."
//
// Sem argumentos roda uma bateria que passa por cada tipo de valor. SO' contra
// o banco de teste local (memoria: suites ao vivo so' em localhost).
#include "orawire/connection.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using otter::orawire::Column;
using otter::orawire::Connection;
using otter::orawire::Value;

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

// Bytes que nao sao texto (RAW) viram hexadecimal, para caber no terminal.
std::string printable(const Column& column, const Value& value) {
    if (value.null) return "[null]";
    using otter::orawire::OraType;
    if (column.type != OraType::raw && column.type != OraType::blob &&
        column.type != OraType::long_raw) {
        std::string text(value.text);
        if (text.size() > 60) text = text.substr(0, 57) + "...";
        return text;
    }
    std::string hex = "0x";
    for (std::size_t i = 0; i < value.text.size() && i < 24; ++i) {
        char pair[4];
        std::snprintf(pair, sizeof(pair), "%02X", static_cast<unsigned char>(value.text[i]));
        hex += pair;
    }
    if (value.text.size() > 24) hex += "...";
    return hex;
}

bool run(Connection& connection, const std::string& sql, int limit = 12) {
    std::printf("\n> %s\n", sql.c_str());

    std::vector<Column> columns;
    long long rows = 0;
    const otter::Status status = connection.query(
        sql,
        [&](const std::vector<Column>& described) {
            columns = described;
            std::printf("  ");
            for (const Column& column : columns) {
                std::printf("%s:%.*s  ", column.name.c_str(),
                            static_cast<int>(type_name(column.type, column.charset_form).size()),
                            type_name(column.type, column.charset_form).data());
            }
            std::printf("\n");
        },
        [&](std::span<const Value> row) {
            if (rows < limit) {
                std::printf("  ");
                for (std::size_t i = 0; i < row.size(); ++i) {
                    std::printf("%s | ", printable(columns[i], row[i]).c_str());
                }
                std::printf("\n");
            }
            ++rows;
        });

    if (!status) {
        std::printf("  FALHOU: %s\n", status.error().to_string().c_str());
        return false;
    }
    std::printf("  -> %lld linha(s), afetadas %lld, transacao %s\n", rows,
                static_cast<long long>(connection.last_affected_rows()),
                connection.in_transaction() ? "aberta" : "nao");
    return true;
}

} // namespace

int main(int argc, char** argv) {
    otter::orawire::ConnectParams params;
    params.host         = env_or("ORA_HOST", "localhost");
    params.port         = static_cast<std::uint16_t>(std::atoi(env_or("ORA_PORT", "1521").c_str()));
    params.service_name = env_or("ORA_SERVICE", "FREEPDB1");
    params.user         = env_or("ORA_USER", "system");
    params.password     = env_or("ORA_PASSWORD", "");

    if (params.host != "localhost" && params.host != "127.0.0.1") {
        std::printf("spike_oraconnect so' roda contra o banco de teste local.\n");
        return 2;
    }

    std::printf("conectando a %s:%u/%s como %s ...\n", params.host.c_str(), params.port,
                params.service_name.c_str(), params.user.c_str());

    auto connected = Connection::connect(params);
    if (!connected) {
        std::printf("FALHOU: %s\n", connected.error().to_string().c_str());
        return 1;
    }
    Connection connection = std::move(*connected);
    std::printf("conectado: Oracle %s\n  %s\n", connection.server_version().c_str(),
                connection.server_banner().c_str());

    int failures = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) failures += run(connection, argv[i], 50) ? 0 : 1;
        return failures == 0 ? 0 : 1;
    }

    const std::vector<std::string> battery = {
        "select 1 as um, 'texto' as texto, sysdate as agora from dual",
        "select user, sys_context('userenv','con_name') as pdb, "
        "sys_context('userenv','current_schema') as esquema from dual",
        // NUMBER: inteiro, decimal, negativo, pequeno, grande, zero, nulo.
        "select 0 a, 1 b, -1 c, 12345.678 d, -0.001 e, 1e20 f, 100 g, 0.5 h, "
        "cast(null as number) n from dual",
        // Datas, com fracao e com fuso.
        "select date '2024-02-29' d, timestamp '2024-02-29 13:45:10.123456' ts, "
        "timestamp '2024-02-29 13:45:10.5 -03:00' tz, "
        "interval '1 02:03:04.5' day to second ds, interval '1-6' year to month ym from dual",
        // Texto nacional (UTF-16 no fio), acento, RAW e ponto flutuante binario.
        "select n'ação ñ' nc, 'coração' c, hextoraw('DEADBEEF') r, "
        "cast(1.5 as binary_double) bd, cast(-2.25 as binary_float) bf, rowid rid from dual",
        // CLOB e BLOB: obrigam a segunda ida, de definicao.
        "select to_clob('um clob de teste') c, to_blob(hextoraw('0102030405')) b from dual",
        // Mais linhas que a pre-busca: obriga o FETCH.
        "select level n, 'linha ' || level t from dual connect by level <= 1200",
        // Colunas repetidas entre linhas: o vetor de bits.
        "select mod(level, 3) m, 'fixo' f, level n from dual connect by level <= 9",
        // Catalogo, com coluna LONG.
        "select table_name, num_rows from all_tables where owner = 'SYS' and rownum <= 5",
        "select view_name, text from all_views where owner = 'SYS' and rownum <= 2",
        // Erro do servidor.
        "select * from tabela_que_nao_existe",
        // E a conexao continua boa depois do erro.
        "select 'ainda viva' ok from dual",
    };
    for (const std::string& sql : battery) failures += run(connection, sql) ? 0 : 1;

    std::printf("\n%d instrucao(oes) falharam (uma e' esperada: a tabela inexistente)\n",
                failures);
    return failures == 1 ? 0 : 1;
}
