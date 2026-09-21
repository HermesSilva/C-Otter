// Spike: conexao real ao MySQL falando lib/mywire.
//
// Existe porque teste unitario de protocolo prova a ARITMETICA, nao o
// PROTOCOLO. Os testes de test_mywire.cpp comparam hashes com hashes e
// pacotes com bytes; nenhum deles descobriria que o campo de capacidades foi
// lido pela metade, ou que a sequencia nao zerou entre comandos. So' o
// servidor responde isso (diretiva 2).
//
//   set MYSQL_USER=root
//   set MYSQL_PASSWORD=...
//   build\win-release\bin\spike_myconnect.exe
#include "mywire/connection.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string env_or(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

std::string as_text(std::span<const std::byte> data) {
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

int run_query(otter::mywire::Connection& connection, std::string_view sql,
              int limit = 10) {
    std::vector<otter::mywire::FieldDescription> fields;
    int rows = 0;

    const otter::Status status = connection.query(
        sql,
        [&](std::span<const otter::mywire::RawValue> row) {
            if (rows < limit) {
                std::printf("   ");
                for (const otter::mywire::RawValue& value : row) {
                    std::printf("%-24s", value.null ? "[null]"
                                                    : as_text(value.data).c_str());
                }
                std::printf("\n");
            }
            ++rows;
        },
        &fields);

    if (!status) {
        std::printf("   FALHOU: %s\n", status.error().to_string().c_str());
        return -1;
    }

    std::printf("   -> %d linha(s), %zu coluna(s)\n", rows, fields.size());
    return rows;
}

} // namespace

int main() {
    otter::mywire::ConnectParams params;
    params.host     = env_or("MYSQL_HOST", "localhost");
    params.port     = static_cast<std::uint16_t>(
                          std::atoi(env_or("MYSQL_PORT", "3306").c_str()));
    params.user     = env_or("MYSQL_USER", "root");
    params.password = env_or("MYSQL_PASSWORD", "");
    params.database = env_or("MYSQL_DATABASE", "");

    std::printf("Conectando a %s:%u como %s...\n", params.host.c_str(),
                params.port, params.user.c_str());

    otter::Result<otter::mywire::Connection> connection =
        otter::mywire::Connection::connect(params);
    if (!connection) {
        std::fprintf(stderr, "FALHOU: %s\n", connection.error().to_string().c_str());
        return 2;
    }

    std::printf("OK. Servidor: %s\n", connection->server_version().c_str());
    std::printf("    versao normalizada: %u   MariaDB: %s   conexao #%u\n\n",
                connection->version_number(),
                connection->is_mariadb() ? "sim" : "nao",
                connection->connection_id());

    std::printf("1. SELECT simples\n");
    if (run_query(*connection, "SELECT 1 AS um, 'texto' AS t, NULL AS n") < 0) return 3;

    std::printf("2. Bancos\n");
    if (run_query(*connection, "SHOW DATABASES") < 0) return 3;

    std::printf("3. Tipos e nulos\n");
    if (run_query(*connection,
                  "SELECT CAST(1 AS SIGNED), CAST(1.5 AS DECIMAL(10,2)), "
                  "NOW(), NULL, 'x'") < 0) return 3;

    std::printf("4. Resultado grande (16k linhas) -- exercita varios pacotes\n");
    if (run_query(*connection,
                  "SELECT seq.n FROM ("
                  "  SELECT a.n + b.n * 10 + c.n * 100 + d.n * 1000 AS n"
                  "  FROM (SELECT 0 n UNION SELECT 1 UNION SELECT 2 UNION SELECT 3"
                  "        UNION SELECT 4 UNION SELECT 5 UNION SELECT 6"
                  "        UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) a,"
                  "       (SELECT 0 n UNION SELECT 1 UNION SELECT 2 UNION SELECT 3"
                  "        UNION SELECT 4 UNION SELECT 5 UNION SELECT 6"
                  "        UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) b,"
                  "       (SELECT 0 n UNION SELECT 1 UNION SELECT 2 UNION SELECT 3"
                  "        UNION SELECT 4 UNION SELECT 5 UNION SELECT 6"
                  "        UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) c,"
                  "       (SELECT 0 n UNION SELECT 1 UNION SELECT 2 UNION SELECT 3"
                  "        UNION SELECT 4 UNION SELECT 5 UNION SELECT 6"
                  "        UNION SELECT 7 UNION SELECT 8 UNION SELECT 9) d"
                  ") seq", 3) < 0) return 3;

    std::printf("5. Erro do servidor -- a mensagem precisa dizer o que houve\n");
    {
        const otter::Status status =
            connection->query("SELECT * FROM tabela_que_nao_existe", nullptr);
        if (status) {
            std::printf("   PROBLEMA: consulta invalida NAO deu erro\n");
            return 3;
        }
        std::printf("   -> %s\n", status.error().to_string().c_str());
    }

    std::printf("6. Colunas: tabela real vs apelido\n");
    {
        std::vector<otter::mywire::FieldDescription> fields;
        const otter::Status status = connection->query(
            "SELECT t.SCHEMA_NAME AS nome FROM information_schema.SCHEMATA t LIMIT 1",
            nullptr, &fields);
        if (!status) {
            std::printf("   FALHOU: %s\n", status.error().to_string().c_str());
            return 3;
        }
        for (const otter::mywire::FieldDescription& field : fields) {
            std::printf("   apelido=%s coluna_real=%s tabela=%s apelido_tabela=%s "
                        "tipo=%u charset=%u binario=%s\n",
                        field.name.c_str(), field.original_name.c_str(),
                        field.table.c_str(), field.table_alias.c_str(),
                        static_cast<unsigned>(field.type), field.charset,
                        field.is_binary() ? "sim" : "nao");
        }
    }

    std::printf("7. PING\n");
    std::printf("   -> %s\n", connection->ping() ? "OK" : "FALHOU");

    std::printf("\nTudo passou.\n");
    return 0;
}
