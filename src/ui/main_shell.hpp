// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade). Nesta fase os paineis sao esqueletos
// com dados sinteticos -- o objetivo e' fixar o layout e o tema.
#pragma once

#include <cstddef>
#include <memory>

class TextEditor;

namespace otter::ui {

class MainShell {
public:
    MainShell();
    ~MainShell();

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

    std::unique_ptr<TextEditor> editor_;
    std::size_t save_point_ = 0;   // indice de undo do ultimo save

    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
};

} // namespace otter::ui
