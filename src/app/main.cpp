// C-Otter -- ponto de entrada.
#include "base/error.hpp"
#include "base/i18n.hpp"
#include "ui/app_settings.hpp"
#include "ui/app_window.hpp"
#include "ui/main_shell.hpp"

#include <cstdio>

namespace {

int run() {
    // Idioma: catalogos embutidos, depois .lang ao lado do executavel (que
    // podem sobrescreve-los), e enfim o idioma do sistema. Ingles e' o padrao
    // e dispensa catalogo -- as chaves ja' sao o texto em ingles.
    otter::i18n::load_builtin_catalogs();
    otter::i18n::load_catalogs("lang");
    otter::i18n::set_language(otter::i18n::detect_system_language());

    otter::ui::WindowConfig config;
    config.title = "C-Otter - every JOIN is an OTTER JOIN";
    // A janela abre onde foi deixada. So' LE o arquivo: quem o cria, na
    // primeira execucao, e' o MainShell -- e criar aqui faria a pasta de
    // dados deixar de parecer nova antes de as conexoes das outras
    // ferramentas serem importadas (ADR 0023).
    config.placement =
        otter::ui::load_app_settings(otter::ui::app_settings_path()).window;

    auto window = otter::ui::AppWindow::create(config);
    if (!window) {
        std::fprintf(stderr, "falha ao abrir a janela: %s\n",
                     window.error().to_string().c_str());
        return 1;
    }

    otter::ui::MainShell shell;

    (*window)->run([&] {
        // O "X" da janela entra pelo mesmo caminho do menu Sair: vira um
        // pedido que o shell pode recusar para confirmar o trabalho nao
        // salvo. Antes ele fechava direto, sem passar pelo shell.
        if ((*window)->close_requested()) {
            (*window)->clear_close_request();
            shell.request_quit();
        }

        shell.note_window_placement((*window)->placement());
        shell.draw();
        if (shell.wants_quit()) (*window)->request_close();
    });
    shell.flush_window_placement();
    // O que foi digitado no ultimo instante, e as abas que estavam abertas.
    shell.flush_scripts();

    return 0;
}

} // namespace

// Entry point unico nas duas plataformas: com GLFW nao ha' wWinMain. No Windows
// o CMake usa WIN32_EXECUTABLE + /ENTRY:mainCRTStartup para nao abrir console
// atras da janela, mantendo main() padrao (ADR 0006).
int main() {
    return run();
}
