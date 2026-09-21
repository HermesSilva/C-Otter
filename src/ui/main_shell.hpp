// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade). Nesta fase os paineis sao esqueletos
// com dados sinteticos -- o objetivo e' fixar o layout e o tema.
#pragma once

// TextEditor::AutoCompleteState e' tipo aninhado: nao ha' como declara-lo
// adiante, entao o header entra aqui.
#include "TextEditor.h"

#include <cstddef>
#include <memory>

namespace otter::ui {

class MainShell {
public:
    MainShell();
    ~MainShell();

    // Publico porque o callback de autocomplete e' uma lambda sem captura que
    // recupera este objeto via state.userData.
    void suggest(TextEditor::AutoCompleteState& state);

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
    std::unique_ptr<TextEditor::AutoCompleteConfig> autocomplete_config_;
    std::size_t save_point_ = 0;   // indice de undo do ultimo save

    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
};

} // namespace otter::ui
