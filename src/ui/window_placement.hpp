// C-Otter -- ui/window_placement.hpp
//
// Tamanho e posicao da janela principal, gravados e restaurados.
//
// Pedido do usuario (2026-10-01): "O tamanho e posicao da janela principal
// deve ser gravado, e restaurando."
//
// A parte que decide ONDE a janela abre fica aqui, sem GLFW, para ter teste:
// restaurar cegamente o que foi gravado abre a janela fora da tela quando o
// segundo monitor foi desligado, ou maior que a tela quando a resolucao
// mudou -- e uma janela que nao se alcanca nao tem conserto pela interface.
#pragma once

#include <span>

namespace otter::ui {

// A janela NORMAL (nao maximizada): a posicao e o tamanho da area de conteudo,
// em coordenadas de tela. `maximized` vai a' parte -- ao sair do maximizado a
// janela volta para este retangulo.
struct WindowPlacement {
    int  x = 0;
    int  y = 0;
    int  width = 0;      // 0 = nada gravado ainda
    int  height = 0;
    bool maximized = false;

    [[nodiscard]] bool saved() const noexcept { return width > 0 && height > 0; }

    friend bool operator==(const WindowPlacement&, const WindowPlacement&) = default;
};

// A area util de um monitor (sem a barra de tarefas).
struct WorkArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

inline constexpr int kMinWindowWidth  = 640;
inline constexpr int kMinWindowHeight = 400;

// Onde a janela deve abrir. `monitors[0]` e' o monitor principal.
//
//   - nada gravado: tamanho padrao, centrada no principal
//   - gravado e visivel: como estava, com o tamanho limitado ao monitor
//   - gravado mas fora de todos os monitores (monitor desligado): o tamanho
//     gravado, centrada no principal
[[nodiscard]] WindowPlacement fit_window_placement(const WindowPlacement& saved,
                                                   std::span<const WorkArea> monitors,
                                                   int default_width, int default_height);

} // namespace otter::ui
