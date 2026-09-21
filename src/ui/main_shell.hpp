// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Raft, Navigator, editor SQL, grade, log de queries).
#pragma once

// TextEditor::AutoCompleteState e' tipo aninhado: nao ha' como declara-lo
// adiante, entao o header entra aqui.
#include "TextEditor.h"

#include "db/alter.hpp"
#include "db/value_view.hpp"
#include "db/holt.hpp"
#include "db/connection_store.hpp"
#include "db/export.hpp"
#include "db/plan.hpp"
#include "db/result_set.hpp"
#include "sql/dialect.hpp"
#include "sql/script.hpp"
#include "ui/connection_dialog.hpp"
#include "ui/ddl_dialog.hpp"
#include "ui/icons.hpp"
#include "ui/session.hpp"
#include "ui/sql_document.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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

    // Pedido de saida: pelo menu, pelo "X" da janela ou por Alt+F4.
    //
    // Nao encerra direto -- se ha' script nao salvo ou alteracao pendente na
    // grade, abre a confirmacao. Sair e perder o trabalho sem perguntar e' o
    // tipo de coisa que nao tem desfazer.
    void request_quit();

private:
    void draw_menu_bar();
    void draw_toolbar();
    void draw_dockspace();
    void draw_raft_panel();
    // Devolve quantas linhas desenhou: o painel precisa do total para saber
    // se mostra "nenhuma conexao".
    std::size_t draw_saved_profiles();

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
    void draw_events_folder(const db::SchemaMeta& schema);

    // As pastas de System Info, no nivel da CONEXAO -- como no DBeaver, onde
    // "System Info" e' irmao de "Databases", nao filho.
    void draw_server_info_folder();

    // Contas do servidor, com os GRANTs de cada uma.
    void draw_users_folder();
    void draw_editor_panel();
    void handle_grid_keys(SqlDocument& document, const db::ResultSet& rs);
    void draw_record_mode_button(SqlDocument& document);
    void draw_record_view(SqlDocument& document, const db::ResultSet& rs);
    void draw_grid_panel();

    // Linha acima da grade: contagem, navegacao de paginas e avisos.
    void draw_grid_toolbar(SqlDocument& document, const db::ResultSet& rs);

    // Janela de exportacao do resultado: formato, opcoes e previa.
    void draw_export_window();

    // Menu de contexto de um cabecalho de coluna: filtro e ordenacao.
    void draw_column_header_menu(SqlDocument& document, const db::ResultSet& rs,
                                 std::size_t column);

    // Menu de cor condicional de uma coluna: presets, nao formulario cru.
    void draw_bar_menu(SqlDocument& document, const db::ResultSet& rs,
                       std::size_t column);
    void draw_color_menu(SqlDocument& document, const db::ResultSet& rs,
                         std::size_t column);

    // Uma celula da grade: valor, marca de alteracao pendente e o editor
    // embutido quando o usuario da' duplo clique (ADR 0014).
    void draw_grid_cell(SqlDocument& document, const db::ResultSet& rs,
                        std::size_t row, std::size_t column);

    // Painel de valor: mostra uma celula por TIPO -- JSON indentado, BLOB em
    // hexadecimal, booleano como caixa. A celula da grade mostra tudo como
    // uma linha de texto, que e' o certo para caber na tabela e errado para
    // ler um JSON de 4 KB.
    void open_value_panel(SqlDocument& document, const db::ResultSet& rs,
                          std::size_t row, std::size_t column);
    void draw_value_panel();

    // Grava as alteracoes pendentes, em transacao.
    void save_pending_edits(SqlDocument& document);

    // --- DDL de escrita (docs/DDL-WRITE.md) ----------------------------------
    //
    // Ponto UNICO por onde toda alteracao de estrutura passa: mostra o SQL e
    // pede confirmacao antes de executar. DDL nao tem desfazer.
    void confirm_ddl(std::string title, db::AlterScript script,
                     std::string schema, std::string table);
    void run_ddl(const std::vector<std::string>& statements);

    // Formularios de coluna nova e de renomear. Desenhados fora do menu de
    // contexto: um menu se fecha ao primeiro clique fora dele, e um
    // formulario precisa sobreviver a varios.
    void open_add_column(const std::string& schema, const db::TableMeta& table);
    void open_add_index(const std::string& schema, const db::TableMeta& table);

    // Criar objeto. Pertence ao SCHEMA -- e' ele que os contem.
    void open_create_table(const std::string& schema);
    void open_create_view(const std::string& schema);
    void open_rename_table(const std::string& schema, const db::TableMeta& table);
    void draw_ddl_forms();

    // --- Agrupamento e totais (ADR 0005) -------------------------------------
    //
    // Recalcula sobre o que esta' em memoria. Marca o resultado como parcial
    // quando o ResultSet e' uma pagina.
    void recompute_groups(SqlDocument& document);

    // Tabela dinamica (ADR 0005). Quando ativa, substitui a grade.
    void recompute_pivot(SqlDocument& document);
    void draw_pivot_table(SqlDocument& document, const db::ResultSet& rs);

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
    // Abas de script de UMA conexao: so' os documentos dela.
    void draw_document_tabs(std::size_t connection_id);
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

        // Identidade estavel da conexao, para o documento saber a qual
        // pertence. NAO da' para usar o indice: fechar uma conexao desloca
        // todas as seguintes, e as abas passariam a apontar para a vizinha.
        std::size_t id = 0;
    };

    std::vector<Connection> connections_;
    std::size_t             active_connection_ = 0;
    std::size_t             next_connection_id_ = 1;

    // Conexao a que uma aba pertence, ou nullptr se ela ja' foi fechada.
    [[nodiscard]] Connection* connection_by_id(std::size_t id);
    [[nodiscard]] const Connection* connection_by_id(std::size_t id) const;

    // Sessao de um documento -- a dele, nao a ativa. Executar sempre pela
    // ativa e' o que fazia um script rodar contra a base errada.
    [[nodiscard]] Session& session_for(const SqlDocument& document);

    // Desenha a janela de UMA conexao, com as abas de script dela.
    void draw_connection_editor(Connection& connection);

    // Conexao ativa. Os ~70 pontos que usam `session_` continuam valendo: a
    // referencia aponta para a Session da conexao selecionada no Raft.
    [[nodiscard]] Session& session();
    [[nodiscard]] const Session& session() const;

    // Cria uma conexao nova e a torna ativa.
    Session& open_connection(const db::ConnectionProfile& profile);
    void close_connection(std::size_t index);

    // Assistente de conexao completo (abas Principal/PostgreSQL/SSH/SSL/...).
    ConnectionDialog connection_dialog_;

    // Confirmacao de DDL. Uma so' para o programa inteiro: duas janelas de
    // confirmacao abertas ao mesmo tempo seriam um convite a confirmar a
    // errada.
    DdlDialog        ddl_dialog_;

    struct ValuePanel {
        bool             open = false;
        std::string      column;
        std::string      text;       // ja' formatado; nao refaz a cada quadro
        db::ValueView    view = db::ValueView::plain;
        std::size_t      size = 0;
    };
    ValuePanel       value_panel_;

    // A arvore precisa ser relida depois de um DDL: sem isso a coluna nova
    // nao apareceria ate' o usuario mandar atualizar, e ele concluiria que o
    // comando nao funcionou.
    bool             ddl_pending_reload_ = false;
    std::string      ddl_reload_schema_;
    std::string      ddl_reload_table_;

    // Estado dos formularios. `current` guarda a tabela COMO ESTA': no MySQL,
    // MODIFY COLUMN exige a definicao inteira, e gerar o ALTER sem ela
    // apagaria atributos.
    struct ColumnForm {
        bool        open = false;
        std::string schema;
        std::string table;
        db::TableMeta current;
        std::vector<std::string> existing;

        char name[128]          = {};
        char type[128]          = {};
        char default_value[256] = {};
        char comment[256]       = {};
        bool nullable = true;

        int  position = 0;      // 0 = fim, 1 = FIRST, 2 = AFTER
        int  after_index = 0;
    };
    ColumnForm column_form_;

    struct CreateTableForm {
        bool        open = false;
        std::string schema;

        char name[128]    = {};
        char comment[256] = {};

        struct Column {
            char name[128] = {};
            char type[128] = {};
            bool nullable = true;
            bool key = false;
        };
        std::vector<Column> columns;
    };
    CreateTableForm create_table_;

    struct CreateViewForm {
        bool        open = false;
        std::string schema;

        char name[128]        = {};
        char definition[4096] = {};
        bool or_replace = true;
    };
    CreateViewForm create_view_;

    struct IndexForm {
        bool        open = false;
        std::string schema;
        std::string table;

        char name[128] = {};
        bool unique = false;
        bool concurrently = false;

        // (nome, marcada). A ORDEM segue a da tabela: num indice composto ela
        // decide que consultas ele atende, e embaralha-la daria outro indice.
        std::vector<std::pair<std::string, bool>> columns;
    };
    IndexForm index_form_;

    struct RenameForm {
        bool        open = false;
        std::string schema;
        std::string table;
        db::TableMeta current;
        char        new_name[128] = {};
    };
    RenameForm rename_form_;

    // Perfil da conexao ativa, para a UI exibir nome, tipo e cor.
    db::ConnectionProfile active_profile_;


    bool layout_initialized_ = false;

    // No' do dock onde as janelas de conexao nascem -- o lugar da antiga
    // janela unica "SQL". Zero antes do layout ser montado.
    unsigned int editor_dock_id_ = 0;
    bool wants_quit_         = false;

    // Confirmacao de saida aberta. A saida so' acontece se o usuario
    // escolher sair; qualquer outra coisa apenas fecha o dialogo.
    bool confirm_quit_       = false;

    // Desenha a confirmacao de saida, quando ha' trabalho a perder.
    void draw_quit_confirm();

    // Quantos scripts tem texto nao salvo, e quantas celulas foram editadas
    // sem gravar. Sao as duas coisas que sair descartaria.
    [[nodiscard]] std::size_t unsaved_documents() const;
    [[nodiscard]] std::size_t pending_cell_edits() const;
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

    // Celula SELECIONADA: (documento, linha, coluna). Diferente da que esta'
    // em edicao -- selecionar e' so' apontar, e e' o que da' aos atalhos de
    // teclado uma "linha atual" sobre a qual agir.
    //
    // Sem isso nenhum dos 50 atalhos de grade do DBeaver tem sentido: nao ha'
    // o que excluir com Alt+Delete nem de onde copiar com Ctrl+D. Mapa em
    // docs/GRID-KEYS.md.
    std::size_t        selected_document_ = 0;
    std::size_t        selected_row_ = 0;
    std::size_t        selected_column_ = 0;
    bool               has_selection_ = false;

    // A selecao mudou por TECLADO neste quadro, e a grade precisa rolar para
    // mostra-la. Navegar para uma linha fora da area visivel sem rolar deixa
    // a selecao invisivel -- o usuario ve' a tecla nao fazer nada.
    bool               scroll_to_selection_ = false;

    // A grade fica com as setas no proximo quadro? Ver draw(): a navegacao do
    // ImGui consome a tecla dentro do NewFrame, antes de o nosso codigo rodar.
    bool               grid_owns_arrows_ = false;
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
