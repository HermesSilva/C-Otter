// Spike: le' o workspace real do DBeaver e mostra o que encontrou.
//
// Existe para provar, contra o arquivo de verdade, que:
//
//   - achamos os workspaces sem caminho fixo
//   - o JSON do DBeaver e' lido sem perder campo
//   - o credentials-config.json decifra com a chave dele
//
// Um teste unitario com JSON inventado provaria menos: o formato real tem
// campos que nao imaginamos e acentos escapados.
//
//   spike_dbeaver_import [--show-passwords]
#include "db/connection_store.hpp"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    // Senha so' aparece quando pedida de proposito: este spike roda numa
    // maquina de trabalho, com credenciais reais na tela.
    bool show_passwords = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--show-passwords") == 0) show_passwords = true;
    }

    const auto locations = otter::db::dbeaver_store_locations();
    if (locations.empty()) {
        std::printf("nenhum workspace do DBeaver encontrado\n");
        return 0;
    }

    std::printf("%zu workspace(s)\n", locations.size());

    for (const auto& location : locations) {
        std::printf("\n=== %s ===\n", location.directory.c_str());

        auto profiles = otter::db::load_profiles(location);
        if (!profiles) {
            std::printf("FALHOU: %s\n", profiles.error().to_string().c_str());
            continue;
        }

        std::size_t supported = 0;
        for (const auto& stored : *profiles) {
            if (stored.supported) ++supported;
        }
        std::printf("%zu conexao(oes), %zu utilizavel(eis)\n\n",
                    profiles->size(), supported);

        for (const auto& stored : *profiles) {
            const auto& p = stored.profile;

            std::printf("  %-28s %-12s %s:%u/%s\n",
                        p.name.c_str(), stored.provider.c_str(),
                        p.host.c_str(), static_cast<unsigned>(p.port),
                        p.database.c_str());

            if (!stored.supported) {
                std::printf("      indisponivel: %s\n",
                            stored.unsupported_reason.c_str());
            }
            if (!p.user.empty()) {
                std::printf("      usuario: %s\n", p.user.c_str());
            }
            if (!p.password.empty()) {
                // A senha decifrada prova que a chave e o AES estao certos.
                // Sem --show-passwords, so' o tamanho.
                if (show_passwords) {
                    std::printf("      senha: %s\n", p.password.c_str());
                } else {
                    std::printf("      senha: decifrada, %zu caractere(s)\n",
                                p.password.size());
                }
            }
            if (!p.driver_properties.empty()) {
                std::printf("      %zu propriedade(s) de driver\n",
                            p.driver_properties.size());
            }
        }
    }

    std::printf("\ndestino do C-Otter: %s\n",
                otter::db::otter_store_location().directory.c_str());
    return 0;
}
