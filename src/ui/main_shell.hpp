// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade, log de queries).
#pragma once

// TextEditor::AutoCompleteState e' tipo aninhado: nao ha' como declara-lo
// adiante, entao o header entra aqui.
#include "TextEditor.h"

#include "db/holt.hpp"
#include "db/connection_store.hpp"
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
    // Tabelas, views e materialized views vao em pastas separadas, como no
    // DBeaver: os filhos de cada uma sao diferentes (uma view nao tem
    // constraints; uma materialized view nao tem triggers).
    void draw_relations_folder(const db::SchemaMeta& schema, db::ObjKind kind,
                               Icon icon, const char* label);
    void draw_table_children(const db::SchemaMeta& schema,
                             const db::TableMeta& table);
    void draw_view_definition(const db::SchemaMeta& schema,
                              const db::TableMeta& view);

    // Menu de contexto de uma relacao: ver dados, gerar SQL, copiar nome.
    // Mapeado do plugin.xml do Navigator (docs/NAVIGATOR-TREE.md).
    void draw_relation_context_menu(const db::SchemaMeta& schema,
                                    const db::TableMeta& relation);

    // Abre uma aba nova com o SQL e, quando `run`, ja' executa.
    void open_sql_tab(std::string sql, bool run);

    // Caixa rolavel com um corpo de SQL, mais os botoes de copiar e abrir no
    // editor. Serve para view e para funcao: o conteudo muda, a apresentacao
    // nao.
    void draw_sql_body(const char* id, const std::string& sql);
    void draw_sequences_folder(const db::SchemaMeta& schema);
    void draw_routines_folder(const db::SchemaMeta& schema);
    void draw_types_folder(const db::SchemaMeta& schema);
    void draw_editor_panel();
    void draw_grid_panel();

    // Linha acima da grade: contagem, navegacao de paginas e avisos.
    void draw_grid_toolbar(SqlDocument& document, const db::ResultSet& rs);
    void draw_query_log_panel();
    void draw_status_bar();
    void draw_about_window();

    // --- Conexoes salvas (ADR 0012) ------------------------------------------
    void load_saved_profiles();
    void persist_profiles();
    void remember_profile(const db::ConnectionProfile& profile);

    // Janela de importacao: lista o que ha' nos workspaces do DBeaver e deixa
    // o usuario escolher. Nunca escreve no diretorio do DBeaver.
    void draw_import_window();

    // Galeria de todos os icones, para conferir de relance se dois tipos de
    // objeto ficaram com desenhos parecidos demais (diretiva 5).
    void draw_icon_gallery();

    void execute_current_sql();

    // Executa uma pagina da consulta guardada no documento (ADR 0011).
    void execute_page(SqlDocument& document, std::size_t page);

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

    // --- Conexoes simultaneas ------------------------------------------------
    //
    // Cada Session ja' encapsula uma conexao inteira: socket, worker, catalogo
    // e estado de transacao. Ter varias e' ter uma lista delas, nao refatorar
    // a classe.
    //
    // unique_ptr porque Session contem std::thread e std::mutex, nao
    // moveis: o vector precisa realocar quando cresce.
    struct Connection {
        std::unique_ptr<Session> session;
        db::ConnectionProfile    profile;
    };

    std::vector<Connection> connections_;
    std::size_t             active_connection_ = 0;

    // Conexao ativa. Os ~70 pontos que usam `session_` continuam valendo: a
    // referencia aponta para a Session da conexao selecionada no Raft.
    [[nodiscard]] Session& session();
    [[nodiscard]] const Session& session() const;

    // Cria uma conexao nova e a torna ativa.
    Session& open_connection(const db::ConnectionProfile& profile);
    void close_connection(std::size_t index);

    // Assistente de conexao completo (abas Principal/PostgreSQL/SSH/SSL/...).
    ConnectionDialog connection_dialog_;

    // Perfil da conexao ativa, para a UI exibir nome, tipo e cor.
    db::ConnectionProfile active_profile_;


    bool layout_initialized_ = false;
    bool wants_quit_         = false;
    bool show_about_         = false;
    bool show_demo_          = false;
    bool show_icons_         = false;
    bool show_import_        = false;

    // Conexoes conhecidas, lidas do disco ao iniciar.
    std::vector<db::StoredProfile> saved_profiles_;

    // Estado da janela de importacao: o que foi encontrado e o que esta'
    // marcado para trazer.
    std::vector<db::StoredProfile> import_candidates_;
    std::vector<bool>              import_selected_;
    std::string                    import_status_;
    bool                           import_scanned_ = false;
};

} // namespace otter::ui
