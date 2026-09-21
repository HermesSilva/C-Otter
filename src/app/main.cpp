// C-Otter -- ponto de entrada.
#include "base/error.hpp"
#include "ui/app_window.hpp"
#include "ui/main_shell.hpp"

#include <cstdio>

namespace {

int run() {
    otter::ui::WindowConfig config;
    config.title = "C-Otter - every JOIN is an OTTER JOIN";

    auto window = otter::ui::AppWindow::create(config);
    if (!window) {
        std::fprintf(stderr, "falha ao abrir a janela: %s\n",
                     window.error().to_string().c_str());
        return 1;
    }

    otter::ui::MainShell shell;

    (*window)->run([&] {
        shell.draw();
        if (shell.wants_quit()) (*window)->request_close();
    });

    return 0;
}

} // namespace

// Entry point unico nas duas plataformas: com GLFW nao ha' wWinMain. No Windows
// o CMake usa WIN32_EXECUTABLE + /ENTRY:mainCRTStartup para nao abrir console
// atras da janela, mantendo main() padrao (ADR 0006).
int main() {
    return run();
}
