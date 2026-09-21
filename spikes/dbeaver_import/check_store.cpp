// Le o armazenamento do PROPRIO C-Otter e diz se a senha volta.
//
// Escrito para separar duas hipoteses de um defeito visto na tela: a conexao
// importada tentava autenticar com "(using password: NO)". Ou a senha nao foi
// GRAVADA, ou nao esta' sendo LIDA de volta -- e sem este teste a diferenca
// nao aparece.
#include "db/connection_store.hpp"

#include <cstdio>

int main() {
    const auto location = otter::db::otter_store_location();
    std::printf("armazenamento: %s\n", location.directory.c_str());

    auto profiles = otter::db::load_profiles(location);
    if (!profiles) {
        std::printf("FALHOU: %s\n", profiles.error().to_string().c_str());
        return 2;
    }

    std::printf("%zu perfil(is)\n\n", profiles->size());

    for (const auto& stored : *profiles) {
        std::printf("  %-28s driver=%-12s %s:%u\n",
                    stored.profile.effective_name().c_str(),
                    stored.profile.driver_id.c_str(),
                    stored.profile.host.c_str(), stored.profile.port);
        std::printf("      id=%s\n", stored.id.c_str());
        std::printf("      usuario=%s  save-password=%s  senha=%s\n",
                    stored.profile.user.c_str(),
                    stored.profile.save_password ? "sim" : "nao",
                    stored.profile.password.empty()
                        ? "VAZIA"
                        : ("presente, " +
                           std::to_string(stored.profile.password.size()) +
                           " caractere(s)").c_str());
    }
    return 0;
}
