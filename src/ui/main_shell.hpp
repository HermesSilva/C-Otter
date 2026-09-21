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
#include "db/export.hpp"
#include "db/plan.hpp"
#include "db/result_set.hpp"
#include "sql/dialect.hpp"
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
    void draw_saved_profiles();

    // Nome do SGBD para exibicao, a partir do driver_id do perfil.
    static std::string dbms_name(const std::string& driver_id);

    // Dialeto SQL da conexao ativa, para lexer, formatador e reescrita.
    [[nodiscard]] const sql::Dialect& active_dialect() const;
    void draw_navigator_panel();

    // O nome passa no filtro do Navigator? Filtro vazio aceita tudo.
    [[nodiscard]] bool matches_filter(std::string_view name) const;

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

    // Janela de exportacao do resultado: formato, opcoes e previa.
    void draw_export_window();

    // Menu de contexto de um cabecalho de coluna: filtro e ordenacao.
    void draw_column_header_menu(SqlDocument& document, const db::ResultSet& rs,
                                 std::size_t column);

    // Uma celula da grade: valor, marca de alteracao pendente e o editor
    // embutido quando o usuario da' duplo clique (ADR 0014).
    void draw_grid_cell(SqlDocument& document, const db::ResultSet& rs,
                        std::size_t row, std::size_t column);

    // Grava as alteracoes pendentes, em transacao.
    void save_pending_edits(SqlDocument& document);

    // --- Agrupamento e totais (ADR 0005) -------------------------------------
    //
    // Recalcula sobre o que esta' em memoria. Marca o resultado como parcial
    // quando o ResultSet e' uma pagina.
    void recompute_groups(SqlDocument& document);

    // Painel de agrupamento acima da grade, e a linha de totais no rodape.
    void draw_group_bar(SqlDocument& document, const db::ResultSet& rs);
    void draw_group_panel(SqlDocument& document, const db::ResultSet& rs);
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

    // Executa o texto inteiro como script: varios comandos separados por ';'
    // (ou DELIMITER, ou $$...$$), um apos o outro.
    void execute_script();

    // Reindenta o SQL da aba ativa (Ctrl+Shift+F).
    void format_current_sql();

    // --- Plano de execucao (ADR 0013) ----------------------------------------
    void explain_current_sql(bool analyze);
    void draw_plan_window();
    void draw_plan_node(const db::PlanNode& node, double max_cost, int depth);

    // --- Arquivo -------------------------------------------------------------
    void open_script_file();
    // `save_as` forca o dialogo mesmo quando o documento ja' tem caminho.
    void save_script_file(bool save_as);

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

    // --- Exportacao ----------------------------------------------------------
    bool               show_export_ = false;

    // Plano da ultima explicacao e o modo pedido.
    std::optional<db::QueryPlan> plan_;
    bool               show_plan_ = false;
    bool               plan_analyze_ = false;

    // Celula em edicao no momento: (documento, linha, coluna). Fora dela, a
    // grade so' desenha texto.
    std::size_t        editing_document_ = 0;
    std::size_t        editing_row_ = 0;
    std::size_t        editing_column_ = 0;
    bool               editing_active_ = false;
    bool               saving_edits_ = false;

    // Uma releitura foi disparada logo apos gravar edicoes. Ela REUSA
    // executing_document_id_, que por isso nao pode ser zerado no mesmo
    // quadro -- se fosse, o resultado novo chegaria sem ninguem para colher e
    // a grade ficaria exibindo o valor anterior a' gravacao.
    bool               rereading_after_save_ = false;
    char               navigator_filter_[128] = "";

    // Documento esperando as constraints para saber se da' para editar.
    std::size_t        pending_edit_target_ = 0;
    char               edit_buffer_[1024] = "";
    db::ExportOptions  export_options_;
    std::string        export_path_;
    std::string        export_status_;
};

} // namespace otter::ui
