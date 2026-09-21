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
#include "ui/icons.hpp"
#include "ui/session.hpp"
#include "ui/sql_document.hpp"

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
    void draw_toolbar();
    void draw_dockspace();
    void draw_raft_panel();
    void draw_navigator_panel();

    // Arvore de objetos. Cada pasta consulta o catalogo apenas quando expandida
    // (ver docs/NAVIGATOR-TREE.md).
    bool draw_folder_node(Icon icon, const char* label,
                          std::size_t count, bool loaded);
    void draw_tables_folder(const db::SchemaMeta& schema);
    void draw_table_children(const db::SchemaMeta& schema,
                             const db::TableMeta& table);
    void draw_sequences_folder(const db::SchemaMeta& schema);
    void draw_routines_folder(const db::SchemaMeta& schema);
    void draw_editor_panel();
    void draw_grid_panel();
    void draw_query_log_panel();
    void draw_status_bar();
    void draw_about_window();

    // Galeria de todos os icones, para conferir de relance se dois tipos de
    // objeto ficaram com desenhos parecidos demais (diretiva 5).
    void draw_icon_gallery();

    void execute_current_sql();

    // --- Documentos (abas) ---------------------------------------------------
    SqlDocument& new_document();
    void close_document(std::size_t index);
    void close_others(std::size_t keep_index);
    [[nodiscard]] SqlDocument* active_document();
    void draw_document_tabs();
    void draw_document_body(SqlDocument& document);

    std::vector<std::unique_ptr<SqlDocument>> documents_;
    std::size_t active_document_ = 0;
    std::size_t next_document_id_ = 1;

    // O documento cujo worker esta' rodando; o resultado volta para ele, nao
    // para o que estiver ativo quando terminar.
    std::size_t executing_document_id_ = 0;

    std::unique_ptr<TextEditor::AutoCompleteConfig> autocomplete_config_;
    Session session_;

    // Assistente de conexao completo (abas Principal/PostgreSQL/SSH/SSL/...).
    ConnectionDialog connection_dialog_;

    // Perfil da conexao ativa, para a UI exibir nome, tipo e cor.
    db::ConnectionProfile active_profile_;


    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
    bool show_icons_         = false;
};

} // namespace otter::ui
