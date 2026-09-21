// C-Otter -- ponto de entrada.
#include "base/error.hpp"
#include "ui/app_window.hpp"
#include "ui/main_shell.hpp"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

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

#ifdef _WIN32
// Subsystem WINDOWS: sem console atras da janela. O CMake aponta o entry point
// para wWinMain; definir tambem main() daria conflito de simbolo.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return run();
}
#else
int main() {
    return run();
}
#endif
