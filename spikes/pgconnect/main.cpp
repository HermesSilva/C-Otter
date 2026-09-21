// Spike: conexao real a um PostgreSQL, leitura de catalogo e ResultSet colunar.
//
// Valida de ponta a ponta o que a Fase 1 precisa: conectar, ler metadados de um
// schema de verdade, converter para o formato colunar e medir o custo.
//
//   spike_pgconnect <host> <port> <database> <user> <password>
#include "db/drivers/postgres.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void print_table(const otter::db::ResultSet& rs, std::size_t max_rows) {
    // Larguras a partir do conteudo, para a saida ficar legivel.
    std::vector<std::size_t> widths(rs.column_count());
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        widths[c] = rs.column(c).info().name.size();
        for (std::size_t r = 0; r < std::min(rs.row_count(), max_rows); ++r) {
            widths[c] = std::max(widths[c], rs.text(r, c).size());
        }
        widths[c] = std::min(widths[c], std::size_t{38});
    }

    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        std::printf("%-*s  ", static_cast<int>(widths[c]),
                    rs.column(c).info().name.c_str());
    }
    std::printf("\n");

    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        std::printf("%s  ", std::string(widths[c], '-').c_str());
    }
    std::printf("\n");

    for (std::size_t r = 0; r < std::min(rs.row_count(), max_rows); ++r) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            // Nulo precisa ser visualmente distinto de string vazia.
            const std::string cell =
                rs.is_null(r, c) ? "[null]" : std::string(rs.text(r, c));
            std::printf("%-*.*s  ", static_cast<int>(widths[c]),
                        static_cast<int>(widths[c]), cell.c_str());
        }
        std::printf("\n");
    }

    if (rs.row_count() > max_rows) {
        std::printf("... mais %zu linha(s)\n", rs.row_count() - max_rows);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "uso: spike_pgconnect <host> <port> <database> <user> <password>\n");
        return 2;
    }

    otter::db::ConnConfig config;
    config.host     = argv[1];
    config.port     = static_cast<std::uint16_t>(std::atoi(argv[2]));
    config.database = argv[3];
    config.user     = argv[4];
    config.password = argv[5];

    std::printf("C-Otter -- spike de conexao PostgreSQL\n");
    std::printf("conectando a %s:%u/%s ...\n\n",
                config.host.c_str(), config.port, config.database.c_str());

    const auto t_connect = Clock::now();
    auto holt = otter::db::postgres_driver().connect(config);
    const double connect_ms = ms_since(t_connect);

    if (!holt) {
        std::fprintf(stderr, "FALHOU: %s\n", holt.error().to_string().c_str());
        return 1;
    }

    std::printf("conectado em %.1f ms | servidor %s\n\n",
                connect_ms, (*holt)->server_version().c_str());

    // --- Metadados: as tabelas do schema -------------------------------------
    // Esta consulta e' o tipo de ativo portavel dos plugins ext.* do DBeaver.
    constexpr const char* kTables = R"(
        SELECT c.relname                        AS tabela,
               obj_description(c.oid, 'pg_class') AS comentario,
               c.reltuples::bigint              AS linhas_estimadas,
               pg_size_pretty(pg_total_relation_size(c.oid)) AS tamanho
          FROM pg_class c
          JOIN pg_namespace n ON n.oid = c.relnamespace
         WHERE c.relkind = 'r'
           AND n.nspname = 'public'
         ORDER BY c.relname
    )";

    const auto t_meta = Clock::now();
    auto tables = (*holt)->query(kTables);
    const double meta_ms = ms_since(t_meta);

    if (!tables) {
        std::fprintf(stderr, "metadados falharam: %s\n",
                     tables.error().to_string().c_str());
        return 1;
    }

    std::printf("=== Tabelas do schema public (%.1f ms) ===\n", meta_ms);
    print_table(*tables, 12);

    // --- Foreign keys: alimentam a inferencia de JOIN (ADR 0004, camada 4) ---
    constexpr const char* kForeignKeys = R"(
        SELECT src.relname  AS tabela_origem,
               att.attname  AS coluna_origem,
               tgt.relname  AS tabela_destino
          FROM pg_constraint con
          JOIN pg_class src ON src.oid = con.conrelid
          JOIN pg_class tgt ON tgt.oid = con.confrelid
          JOIN pg_attribute att ON att.attrelid = con.conrelid
                               AND att.attnum = con.conkey[1]
         WHERE con.contype = 'f'
         ORDER BY 1, 2
    )";

    auto fks = (*holt)->query(kForeignKeys);
    if (fks) {
        std::printf("\n=== Foreign keys (%zu) -- inferencia de JOIN ===\n",
                    fks->row_count());
        print_table(*fks, 8);
    }

    // --- Verificacao do formato colunar --------------------------------------
    std::printf("\n=== ResultSet colunar ===\n");
    std::printf("%-22s %-14s %-10s %s\n", "coluna", "tipo", "kind", "bytes");
    for (std::size_t c = 0; c < tables->column_count(); ++c) {
        const auto& column = tables->column(c);
        std::printf("%-22s %-14s %-10s %zu\n",
                    column.info().name.c_str(),
                    column.info().type_name.c_str(),
                    std::string(otter::db::to_string(column.info().kind)).c_str(),
                    column.bytes_used());
    }
    std::printf("\ntotal em memoria: %zu bytes para %zu linhas x %zu colunas\n",
                tables->bytes_used(), tables->row_count(), tables->column_count());

    // --- Inspetor de queries (ADR 0008) --------------------------------------
    std::printf("\n=== Log de queries ===\n");
    for (const otter::db::QueryLog& entry : (*holt)->query_log()) {
        std::printf("  %8.2f ms  %6zu linhas  %s\n",
                    static_cast<double>(entry.duration.count()) / 1000.0,
                    entry.rows,
                    entry.failed ? "FALHOU" : "ok");
    }

    (*holt)->close();
    std::printf("\nOK\n");
    return 0;
}
