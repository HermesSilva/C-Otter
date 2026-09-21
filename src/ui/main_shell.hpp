// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade). Nesta fase os paineis sao esqueletos
// com dados sinteticos -- o objetivo e' fixar o layout e o tema.
#pragma once

namespace otter::ui {

class MainShell {
public:
    MainShell();

    // Desenha um frame inteiro da aplicacao.
    void draw();

    [[nodiscard]] bool wants_quit() const noexcept { return wants_quit_; }

private:
    void draw_menu_bar();
    void draw_dockspace();
    void draw_raft_panel();
    void draw_navigator_panel();
    void draw_editor_panel();
    void draw_grid_panel();
    void draw_status_bar();
    void draw_about_window();

    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
};

} // namespace otter::ui
