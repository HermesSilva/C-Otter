// Consulta rapida usando o perfil salvo do C-Otter. Existe para conferir no
// BANCO o que a tela mostrou -- sem passar senha em linha de comando.
//
//   qq <perfil> [--db <banco>] <sql>
//   qq <perfil> [--db <banco>] -f <arquivo.sql>
//
// `--db` troca o banco do perfil: o perfil PostgreSQL local aponta para
// `postgres`, e as fixtures ficam no ERP_TID. `-f` manda o arquivo inteiro
// numa Query simples -- e' o que deixa aplicar fixtures.sql sem o psql, que
// pediria a senha.
#include "db/connection_store.hpp"
#include "db/registry.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("uso: qq <perfil> [--db <banco>] <sql> | -f <arquivo>\n");
        return 2;
    }

    std::string database;
    std::string sql;
    bool        from_file = false;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            database = argv[++i];
        } else if (std::strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            std::ifstream in(argv[++i], std::ios::binary);
            if (!in) { std::printf("arquivo ilegivel: %s\n", argv[i]); return 2; }
            std::ostringstream text;
            text << in.rdbuf();
            sql       = text.str();
            from_file = true;
        } else {
            sql = argv[i];
        }
    }

    auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
    if (!profiles) { std::printf("sem perfis\n"); return 2; }

    for (const auto& stored : *profiles) {
        if (stored.profile.effective_name() != argv[1]) continue;

        auto* driver = otter::db::find_driver(stored.profile.driver_id);
        if (!driver) { std::printf("sem driver\n"); return 2; }

        auto config = stored.profile.to_conn_config();
        if (!database.empty()) config.database = database;

        auto holt = driver->connect(config);
        if (!holt) { std::printf("FALHOU: %s\n", holt.error().to_string().c_str()); return 2; }

        if (from_file) {
            auto status = (*holt)->execute(sql);
            if (!status) { std::printf("FALHOU: %s\n", status.error().to_string().c_str()); return 2; }
            std::printf("ok\n");
            return 0;
        }

        auto rs = (*holt)->query(sql);
        if (!rs) { std::printf("FALHOU: %s\n", rs.error().to_string().c_str()); return 2; }

        for (std::size_t c = 0; c < rs->column_count(); ++c) {
            std::printf("%-22s", std::string(rs->column(c).info().name).c_str());
        }
        std::printf("\n");
        for (std::size_t r = 0; r < rs->row_count(); ++r) {
            for (std::size_t c = 0; c < rs->column_count(); ++c) {
                std::printf("%-22s", rs->is_null(r, c) ? "[null]"
                                                       : std::string(rs->text(r, c)).c_str());
            }
            std::printf("\n");
        }
        return 0;
    }
    std::printf("perfil nao encontrado\n");
    return 2;
}
