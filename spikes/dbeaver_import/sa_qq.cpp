// Consulta rapida a um SQL Anywhere pelo DRIVER (nao pelo protocolo cru): e' o
// que a grade recebe -- nomes, tipos descritos, tabela de origem, estado da
// transacao.
//
//     spike_sa_qq [--host H] [--port N] [--db BANCO] [--user U] [--manual] "<sql>" ...
//
// A senha vem de OTTER_SA_PASSWORD. So' localhost, salvo --host explicito.
// --manual desliga o auto-commit antes de rodar.
#include "db/drivers/sqlanywhere.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace otter::db;

int main(int argc, char** argv) {
    ConnConfig config;
    config.driver_id = "sqlanywhere";
    config.host      = "localhost";
    config.port      = 2638;
    config.user      = "DBA";
    if (const char* password = std::getenv("OTTER_SA_PASSWORD")) config.password = password;

    bool manual = false;
    std::vector<std::string> statements;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) config.host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
        }
        else if (arg == "--db" && i + 1 < argc) config.database = argv[++i];
        else if (arg == "--user" && i + 1 < argc) config.user = argv[++i];
        else if (arg == "--manual") manual = true;
        else statements.push_back(arg);
    }

    auto holt = sqlanywhere_driver().connect(config);
    if (!holt) {
        std::fprintf(stderr, "conexao falhou: %s\n", holt.error().to_string().c_str());
        return 2;
    }
    std::printf("SQL Anywhere %s | banco %s | usuario %s\n", (*holt)->server_version().c_str(),
                (*holt)->current_database().c_str(), (*holt)->current_schema().c_str());
    if (manual) (void)(*holt)->set_auto_commit(false);

    for (const std::string& sql : statements) {
        std::printf("\n> %s\n", sql.c_str());
        auto rs = (*holt)->query(sql);
        if (!rs) {
            std::printf("ERRO: %s\n", rs.error().to_string().c_str());
        } else {
            for (std::size_t c = 0; c < rs->column_count(); ++c) {
                const ColumnInfo& info = rs->column(c).info();
                std::printf("%s:%s", info.name.c_str(), info.type_name.c_str());
                if (info.has_source()) {
                    std::printf("<%s.%s.%s>", info.source_schema.c_str(),
                                info.source_table.c_str(), info.source_column_name.c_str());
                }
                std::printf("  ");
            }
            std::printf("\n");
            for (std::size_t r = 0; r < rs->row_count(); ++r) {
                for (std::size_t c = 0; c < rs->column_count(); ++c) {
                    if (rs->is_null(r, c)) {
                        std::printf("[null] | ");
                    } else {
                        const std::string_view text = rs->text(r, c);
                        std::printf("%.*s | ", static_cast<int>(text.size()), text.data());
                    }
                }
                std::printf("\n");
            }
            std::printf("(%zu linhas, afetadas %lld, transacao: %s)\n", rs->row_count(),
                        static_cast<long long>(rs->affected_rows()),
                        (*holt)->txn_state() == TxnState::active ? "aberta" : "nao");
        }
        for (const std::string& line : (*holt)->take_server_output()) {
            std::printf("SAIDA: %s\n", line.c_str());
        }
    }
    return 0;
}
