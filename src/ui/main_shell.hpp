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
#include "sql/script.hpp"
#include "ui/connection_dialog.hpp"
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

    void execute_current_sql();

    std::unique_ptr<TextEditor> editor_;
    std::unique_ptr<TextEditor::AutoCompleteConfig> autocomplete_config_;
    Session session_;

    // Assistente de conexao completo (abas Principal/PostgreSQL/SSH/SSL/...).
    ConnectionDialog connection_dialog_;

    // Perfil da conexao ativa, para a UI exibir nome, tipo e cor.
    db::ConnectionProfile active_profile_;

    // Resultado ativo na grade. Copiado da sessao quando o worker termina.
    std::optional<db::ResultSet> result_;

    // Indice de undo correspondente ao ultimo estado salvo. Comparar com
    // GetUndoIndex() diz se ha' alteracoes pendentes -- e cobre o caso de
    // desfazer de volta ao ponto salvo, em que o texto deixa de estar sujo.
    std::size_t save_point_ = 0;

    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
};

} // namespace otter::ui
