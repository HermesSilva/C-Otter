// Diz QUAL driver cada perfil salvo usaria, sem conectar.
//
// Por que existe: o defeito "conecta no PostgreSQL falando MySQL" nao aparece
// no arquivo -- o JSON estava certo -- e so' se manifesta como um timeout em
// "reading packet header". Perguntar ao registro qual driver seria escolhido
// mostra o problema em um comando, e sem senha nenhuma.
#include "db/connection_store.hpp"
#include "db/registry.hpp"

#include <cstdio>

int main() {
    auto profiles = otter::db::load_profiles(otter::db::otter_store_location());
    if (!profiles) { std::printf("sem perfis\n"); return 2; }

    for (const auto& stored : *profiles) {
        const auto& p = stored.profile;
        auto* driver = otter::db::find_driver(p.driver_id);

        std::printf("%-22s %s:%u  driver_id=%-12s -> %s\n",
                    p.effective_name().c_str(), p.host.c_str(),
                    static_cast<unsigned>(p.port), p.driver_id.c_str(),
                    driver ? std::string(driver->display_name()).c_str()
                           : "NENHUM");
    }
    return 0;
}
