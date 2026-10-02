// C-Otter -- testes de ui/app_settings: o que vai e volta de settings.json.
//
// O que se protege: uma opcao nova que e' gravada e nao e' lida (ou o
// contrario) nao da' erro nenhum -- so' "esquece" a preferencia a cada
// inicio. Foi o defeito relatado com os grupos da arvore, que nasciam sempre
// abertos.
#include "test_main.hpp"

#include "ui/app_settings.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace otter;
using namespace otter::ui;

namespace {

// Na pasta temporaria, e apagado no fim: teste nao deixa arquivo fora dela
// (diretiva 14).
struct TempFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "otter-test-settings.json";
    TempFile() { std::filesystem::remove(path); }
    ~TempFile() { std::filesystem::remove(path); }
};

} // namespace

OTTER_TEST(settings_remember_which_tree_groups_were_closed) {
    const TempFile file;

    AppSettings settings;
    settings.closed_folders = {"SQL Server Management Studio", "DBeaver/Produção"};
    OTTER_CHECK(save_app_settings(file.path.string(), settings));

    const AppSettings loaded = load_app_settings(file.path.string());
    OTTER_CHECK_EQ(loaded.closed_folders.size(), std::size_t{2});
    OTTER_CHECK_EQ(loaded.closed_folders[0], std::string{"SQL Server Management Studio"});
    OTTER_CHECK_EQ(loaded.closed_folders[1], std::string{"DBeaver/Produção"});
}

OTTER_TEST(settings_file_lists_the_option_even_when_empty) {
    // O arquivo e' tambem a lista do que se pode configurar (diretiva 14): a
    // chave aparece mesmo sem grupo fechado.
    const TempFile file;
    OTTER_CHECK(save_app_settings(file.path.string(), AppSettings{}));

    std::ifstream in(file.path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    OTTER_CHECK(text.str().find("\"closed-folders\"") != std::string::npos);

    // E um arquivo de versao anterior, sem a chave, continua valendo.
    OTTER_CHECK(load_app_settings(file.path.string()).closed_folders.empty());
}

// --- Janela principal ------------------------------------------------------------------

OTTER_TEST(settings_remember_the_main_window) {
    const TempFile file;

    AppSettings settings;
    settings.window = WindowPlacement{-1800, 40, 1500, 900, true};   // monitor a' esquerda
    OTTER_CHECK(save_app_settings(file.path.string(), settings));

    const AppSettings loaded = load_app_settings(file.path.string());
    OTTER_CHECK(loaded.window == settings.window);

    // Nada gravado ainda: largura zero, e o arquivo lista a opcao mesmo assim.
    const TempFile fresh;
    OTTER_CHECK(save_app_settings(fresh.path.string(), AppSettings{}));
    OTTER_CHECK(!load_app_settings(fresh.path.string()).window.saved());
}

OTTER_TEST(window_opens_centered_when_nothing_was_saved) {
    const WorkArea monitors[] = {{0, 0, 1920, 1040}};
    const WindowPlacement placed = fit_window_placement({}, monitors, 1600, 980);
    OTTER_CHECK_EQ(placed.width, 1600);
    OTTER_CHECK_EQ(placed.height, 980);
    OTTER_CHECK_EQ(placed.x, 160);
    OTTER_CHECK_EQ(placed.y, 30);
    OTTER_CHECK(!placed.maximized);

    // Padrao maior que a tela: cabe nela, em vez de esconder a barra de status.
    const WorkArea small[] = {{0, 0, 1366, 728}};
    const WindowPlacement fitted = fit_window_placement({}, small, 1600, 980);
    OTTER_CHECK_EQ(fitted.width, 1366);
    OTTER_CHECK_EQ(fitted.height, 728);
    OTTER_CHECK_EQ(fitted.x, 0);
}

OTTER_TEST(window_reopens_where_it_was_left) {
    // Dois monitores; a janela ficou no da esquerda (x negativo), maximizada.
    const WorkArea monitors[] = {{0, 0, 1920, 1040}, {-1920, 0, 1920, 1040}};
    const WindowPlacement saved{-1800, 60, 1400, 800, true};
    const WindowPlacement placed = fit_window_placement(saved, monitors, 1600, 980);
    OTTER_CHECK(placed == saved);
}

OTTER_TEST(window_comes_back_when_its_monitor_is_gone) {
    // O monitor da esquerda foi desligado: a janela gravada la' ficaria fora
    // da tela, sem jeito de alcanca-la.
    const WorkArea monitors[] = {{0, 0, 1920, 1040}};
    const WindowPlacement saved{-1800, 60, 1400, 800, false};
    const WindowPlacement placed = fit_window_placement(saved, monitors, 1600, 980);
    OTTER_CHECK_EQ(placed.width, 1400);
    OTTER_CHECK_EQ(placed.height, 800);
    OTTER_CHECK_EQ(placed.x, 260);
    OTTER_CHECK_EQ(placed.y, 120);

    // So' uma lasca aparecendo tambem nao serve: nao da' para pegar a barra.
    const WindowPlacement sliver{1900, 60, 1400, 800, false};
    OTTER_CHECK_EQ(fit_window_placement(sliver, monitors, 1600, 980).x, 260);
}

OTTER_TEST(window_is_clamped_to_the_monitor_it_is_on) {
    // A resolucao diminuiu: o tamanho gravado nao cabe mais.
    const WorkArea monitors[] = {{0, 0, 1366, 728}};
    const WindowPlacement saved{100, -20, 1600, 980, false};
    const WindowPlacement placed = fit_window_placement(saved, monitors, 1600, 980);
    OTTER_CHECK_EQ(placed.width, 1366);
    OTTER_CHECK_EQ(placed.height, 728);
    // O topo nunca acima da area util.
    OTTER_CHECK_EQ(placed.y, 0);

    // Tamanho absurdo de pequeno (arquivo editado a' mao): sobe para o minimo.
    const WindowPlacement tiny{100, 100, 10, 10, false};
    const WindowPlacement grown = fit_window_placement(tiny, monitors, 1600, 980);
    OTTER_CHECK_EQ(grown.width, kMinWindowWidth);
    OTTER_CHECK_EQ(grown.height, kMinWindowHeight);
}
