// Spike: o que a primeira execucao importaria NESTA maquina, do DBeaver, do
// pgAdmin e do SSMS. So' leitura; nenhuma senha e' impressa.
#include "db/connection_import.hpp"

#include <cstdio>

int main() {
    const otter::db::ExternalProfiles found = otter::db::external_profiles();
    std::printf("DBeaver %zu, pgAdmin %zu, SSMS %zu -> %zu conexoes\n", found.from_dbeaver,
                found.from_pgadmin, found.from_ssms, found.profiles.size());
    for (const otter::db::StoredProfile& stored : found.profiles) {
        const otter::db::ConnectionProfile& p = stored.profile;
        std::printf("  [%s] %-34s %s:%u/%s user=%s%s%s\n", stored.provider.c_str(),
                    p.effective_name().c_str(), p.host.c_str(),
                    static_cast<unsigned>(p.port), p.database.c_str(), p.user.c_str(),
                    p.password.empty() ? "" : " (senha)",
                    stored.supported ? "" : " (nao suportado)");
    }
    return 0;
}
