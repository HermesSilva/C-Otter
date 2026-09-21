// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade, log de queries).
#pragma once

// TextEditor::AutoCompleteState e' tipo aninhado: nao ha' como declara-lo
// adiante, entao o header entra aqui.
#include "TextEditor.h"

#include "db/holt.hpp"
#include "db/result_set.hpp"
#include "ui/session.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace otter::ui {

class MainShell {
public:
    MainShell();
    ~MainShell();

    // Publico porque o callback de autocomplete e' uma lambda sem captura que
    // recupera este objeto via state.userData.
    void suggest(TextEditor::AutoCompleteState& state);

    void draw();

    [[nodiscard]] bool wants_quit() const noexcept { return wants_quit_; }

private:
    void draw_menu_bar();
    void draw_dockspace();
    void draw_raft_panel();
    void draw_navigator_panel();
    void draw_editor_panel();
    void draw_grid_panel();
    void draw_query_log_panel();
    void draw_status_bar();
    void draw_about_window();
    void draw_connect_dialog();

    void execute_current_sql();

    std::unique_ptr<TextEditor> editor_;
    std::unique_ptr<TextEditor::AutoCompleteConfig> autocomplete_config_;
    Session session_;

    // Resultado ativo na grade. Copiado da sessao quando o worker termina.
    std::optional<db::ResultSet> result_;

    // Campos do dialogo de conexao.
    char host_[128]     = "localhost";
    char port_[8]       = "5432";
    char database_[128] = "ERP_TID";
    char user_[64]      = "postgres";
    char password_[128] = "";

    std::size_t save_point_ = 0;   // indice de undo do ultimo save

    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
    bool show_connect_       = true;   // abre ao iniciar
};

} // namespace otter::ui
