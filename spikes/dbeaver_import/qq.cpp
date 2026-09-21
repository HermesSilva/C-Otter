// Consulta rapida usando o perfil salvo do C-Otter. Existe para conferir no
// BANCO o que a tela mostrou -- sem passar senha em linha de comando.
#include "db/connection_store.hpp"
#include "db/registry.hpp"

#include <cstdio>

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("uso: qq <nome-do-perfil> <sql>\n"); return 2; }

    auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
    if (!profiles) { std::printf("sem perfis\n"); return 2; }

    for (const auto& stored : *profiles) {
        if (stored.profile.effective_name() != argv[1]) continue;

        auto* driver = otter::db::find_driver(stored.profile.driver_id);
        if (!driver) { std::printf("sem driver\n"); return 2; }

        auto holt = driver->connect(stored.profile.to_conn_config());
        if (!holt) { std::printf("FALHOU: %s\n", holt.error().to_string().c_str()); return 2; }

        auto rs = (*holt)->query(argv[2]);
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
