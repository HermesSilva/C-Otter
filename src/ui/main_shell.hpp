// C-Otter -- ui/main_shell.hpp
//
// Estrutura da janela principal: menu, dock space e os paineis
// (Database Navigator, editor SQL, grade, log de queries).
#pragma once

// TextEditor::AutoCompleteState e' tipo aninhado: nao ha' como declara-lo
// adiante, entao o header entra aqui.
#include "TextEditor.h"

#include "db/alter.hpp"
#include "db/app_tools.hpp"
#include "db/mysql_object.hpp"
#include "db/object_info.hpp"
#include "db/value_view.hpp"
#include "db/holt.hpp"
#include "db/connection_store.hpp"
#include "db/export.hpp"
#include "db/grid_ops.hpp"
#include "base/process.hpp"
#include "db/import.hpp"
#include "db/native_tools.hpp"
#include "db/plan.hpp"
#include "db/result_set.hpp"
#include "sql/dialect.hpp"
#include "sql/script.hpp"
#include "sql/editing.hpp"
#include "ui/app_settings.hpp"
#include "ui/commands.hpp"
#include "ui/connection_dialog.hpp"
#include "ui/ddl_dialog.hpp"
#include "ui/icons.hpp"
#include "ui/script_store.hpp"
#include "ui/session.hpp"
#include "ui/sql_document.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <set>
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
    // Os scripts sao gravados na hora e NAO entram na pergunta (pedido do
    // usuario, 2026-10-01: o salvamento e' automatico). A confirmacao so'
    // abre para o que nao da' para gravar sozinho: celula editada e ainda nao
    // enviada ao banco, e alteracao pendente num editor de objeto.
    void request_quit();

    // Grava agora os scripts com mudanca pendente e o indice da sessao, sem
    // esperar o atraso da digitacao. Ao sair.
    void flush_scripts();

    // A janela principal, como esta' agora (chamado a cada quadro por main).
    // Mudou e ficou parada por um instante: vai para o settings.json. Gravar
    // so' ao sair perderia a posicao quando o programa e' encerrado pelo
    // gerenciador de tarefas ou cai.
    void note_window_placement(const WindowPlacement& placement);
    // Ao sair: grava o que ainda estiver pendente.
    void flush_window_placement();

private:
    void draw_menu_bar();
    void draw_toolbar();
    void draw_dockspace();
    // Nome do SGBD para exibicao, a partir do driver_id do perfil.
    static std::string dbms_name(const std::string& driver_id);

    // Aplica ao editor as preferencias da conexao do documento -- editor de
    // codigo e completar. A AutoCompleteConfig e' compartilhada por todos os
    // editores, entao vale a cada quadro.
    void apply_completion_options(SqlDocument& document);

    // Preferencias da conexao do documento, ou os padroes quando ela nao
    // existe mais. Referencia, nao copia: e' lida por celula da grade.
    [[nodiscard]] const db::EditorOptions& editor_options_for(
        const SqlDocument& document) const;


    // Dialeto SQL da conexao ativa, para lexer, formatador e reescrita.
    [[nodiscard]] const sql::Dialect& active_dialect() const;

    // --- Comandos do editor SQL (ui/commands.hpp, ui/editor_commands.cpp) -----
    //
    // Um registro, quatro entradas: teclado, barra lateral, menu "SQL Editor"
    // e menu de contexto. O perfil de atalhos (DBeaver ou C-Otter) decide a
    // tecla; o comando e' o mesmo.
    AppSettings settings_;
    Keymap      keymap_ = Keymap::dbeaver;
    void load_settings();
    void save_settings();
    void set_keymap(Keymap keymap);
    void draw_keymap_menu();
    void draw_icon_set_menu();
    void draw_shortcuts_window();
    bool show_shortcuts_ = false;

    // Aviso passageiro no canto: o retorno de um comando que nao muda nada
    // visivel ("query copied", a contagem de linhas).
    void show_toast(std::string text);
    void draw_toast();
    std::string toast_text_;
    double      toast_until_ = 0.0;

    // Dialeto do DOCUMENTO, nao da conexao ativa.
    [[nodiscard]] const sql::Dialect& document_dialect(
        const SqlDocument& document) const;

    [[nodiscard]] bool command_enabled(Command command);
    [[nodiscard]] bool command_checked(Command command);
    void run_command(Command command);
    void dispatch_shortcuts(CommandContext context);
    bool command_menu_item(Command command);

    // Comandos escolhidos num menu, executados no inicio do quadro seguinte:
    // o menu de contexto e' desenhado de dentro do Render() do editor.
    std::vector<Command> queued_commands_;
    void run_queued_commands();

    // Conferencia automatizada: le comandos de um arquivo e os executa pelo
    // MESMO caminho do menu (variavel OTTER_COMMAND_FILE; ver
    // docs/ELEMENTS.md, "Variaveis de inspecao").
    void poll_command_file();

    // Como uma instrucao e' executada.
    enum class RunMode {
        same_tab,   // reaproveita a aba de resultado (a menos que fixada)
        new_tab,    // abre outra aba de resultado
        all_rows,   // sem limite de linhas
    };
    bool handle_control_command(SqlDocument& document, std::string_view statement);
    void run_sql(SqlDocument& document, std::string sql, RunMode mode);
    [[nodiscard]] std::vector<std::string> prepare_script(SqlDocument& document,
                                                          std::size_t from_offset);
    void run_script(SqlDocument& document, std::size_t from_offset,
                    bool separate_tabs);

    // "Execute queries in separate tabs": uma instrucao por vez.
    struct QueuedRun {
        std::size_t document_id = 0;
        std::string sql;
    };
    std::deque<QueuedRun> run_queue_;
    void process_run_queue();

    void draw_editor_side_toolbar(float height);
    void draw_editor_context_menu();
    void draw_ruler_context_menu();
    void draw_layout_menu();
    void draw_panels_menu();
    void draw_sql_editor_menu();

    // Abas de resultado do documento, no topo do painel de resultado.
    void draw_result_tabs(SqlDocument& document);
    std::size_t force_result_tab_ = 0;          // id da aba a selecionar
    std::size_t last_result_document_id_ = 0;

    // Paineis do editor (`sqlEditor.side.bottom` + terminal).
    void dock_panel_once(bool& docked);
    void draw_output_panel();
    void draw_variables_panel();
    void draw_outline_panel();
    void draw_terminal_panel();
    unsigned int result_dock_id_ = 0;
    bool output_docked_    = false;
    bool variables_docked_ = false;
    bool outline_docked_   = false;
    bool terminal_docked_  = false;
    bool show_output_    = false;
    bool show_log_       = true;
    bool show_variables_ = false;
    bool show_outline_   = false;
    bool show_terminal_  = false;

    // Disposicao: resultado escondido, maximizado, ou ao lado do editor.
    bool results_hidden_    = false;
    bool results_maximized_ = false;
    bool side_by_side_      = false;
    // Quadros ate' reselecionar a aba "Result". Ao reaparecerem juntas, as
    // janelas de baixo deixam selecionada a ULTIMA desenhada (o log), e quem
    // reexibe o painel quer ver o resultado.
    int  reselect_result_   = 0;
    void rebuild_layout();

    bool sync_auto_        = false;
    bool editor_has_focus_ = false;   // do quadro anterior
    bool focus_editor_     = false;   // pedido: devolver o foco ao editor
    // A QUAL documento o pedido se dirige; 0 = ao que estiver visivel. Um
    // script recem-criado so' aparece no quadro seguinte, e sem o alvo quem
    // consumia o pedido era a aba antiga, ainda na tela.
    std::size_t focus_document_id_ = 0;

    std::size_t export_after_run_ = 0;   // documento: abrir a exportacao ao chegar
    bool        count_to_toast_   = false;

    // A conexao selecionada na arvore -- "Set connection from navigator".
    std::size_t tree_selected_connection_id_ = 0;

    // "Open Declaration": abre o caminho ate' o objeto na arvore e o realca.
    struct TreeReveal {
        std::size_t connection_id = 0;
        std::string schema;
        std::string relation;
        int         frames = 0;
    };
    TreeReveal tree_reveal_;
    static constexpr int kRevealFrames = 120;
    // A arvore em desenho e' a do objeto procurado?
    [[nodiscard]] bool revealing_here() const;
    // Nos primeiros quadros os nos do caminho sao abertos a' forca; depois o
    // usuario pode recolhe-los, e so' o realce continua.
    [[nodiscard]] bool reveal_forcing() const noexcept {
        return tree_reveal_.frames > kRevealFrames - 6;
    }

    bool goto_line_open_ = false;
    char goto_line_buffer_[16] = "";
    void draw_goto_line_dialog();

    bool morph_open_ = false;
    char morph_source_[32]   = "\\t\\n,";
    char morph_target_[16]   = ",";
    char morph_quote_[8]     = "'";
    int  morph_wrap_         = 80;
    char morph_leading_[64]  = "";
    char morph_trailing_[64] = "";
    void draw_morph_dialog();

    bool templates_open_ = false;
    void draw_templates_popup();

    std::size_t delete_script_id_ = 0;
    void draw_delete_script_confirm();

    std::vector<sql::OutlineEntry> outline_;
    std::size_t outline_document_id_ = 0;
    std::size_t outline_stamp_ = 0;
    bool        outline_dirty_ = true;

    std::vector<std::string> terminal_lines_;
    char        terminal_input_[1024] = "";
    std::size_t terminal_pending_id_ = 0;   // conexao que esta' respondendo
    bool        terminal_scroll_ = false;
    static constexpr std::size_t kTerminalRows = 200;

    void draw_editor_extras();

    // --- Arvore unica (ADR 0018, ui/navigator.cpp) ----------------------------
    //
    // Um painel: cada conexao -- aberta ou so' salva -- e' um no' raiz, e o
    // que ela contem fica dentro dela, como no Database Navigator do DBeaver.
    void draw_navigator_panel();

    // Uma linha da arvore de conexoes: o perfil salvo, a conexao aberta que
    // lhe corresponde, ou os dois. Indice invalido = nao ha'.
    struct TreeEntry {
        std::size_t connection = static_cast<std::size_t>(-1);
        std::size_t saved      = static_cast<std::size_t>(-1);
    };
    [[nodiscard]] std::vector<TreeEntry> tree_entries(
        const std::string& folder) const;

    // A conexao RAIZ aberta para este perfil, ou indice invalido.
    [[nodiscard]] std::size_t find_root_connection(
        const db::ConnectionProfile& profile) const;
    // A sessao do banco `database` aberta sob a conexao `parent_id`.
    [[nodiscard]] std::size_t find_database_connection(
        std::size_t parent_id, std::string_view database) const;

    void draw_connection_node(std::size_t conn_index, std::size_t saved_index);
    void draw_connection_tree(std::size_t conn_index);
    void draw_databases_folder(std::size_t root_index);
    void draw_database_contents();
    void draw_schema_node(const db::SchemaMeta& schema, Icon icon,
                          bool open_by_default);
    void draw_schema_contents(const db::SchemaMeta& schema);
    void draw_administer_folder();
    void draw_server_lists_folder();
    // SQL Server: o que um banco contem, e as pastas do servidor (Security,
    // Administer) -- o `<tree>` de org.jkiss.dbeaver.ext.mssql.
    void draw_mssql_database_contents();
    void draw_mssql_server_folders();
    // SQL Anywhere: um banco por conexao, e as pastas do Sybase Central.
    void draw_sqlanywhere_tree();
    void draw_size_bar(const std::string& text, float fraction);
    bool draw_action_leaf(Icon icon, const char* label, const char* tooltip);

    // Como uma pasta de lista se apresenta. Tudo opcional.
    struct ListOptions {
        const char* off_suffix = nullptr;   // ao lado, quando flag e' falso
        const char* on_suffix  = nullptr;   // ao lado, quando flag e' verdadeiro
        const char* empty_text = nullptr;   // no lugar da lista vazia
        std::optional<Icon> off_icon;       // icone quando flag e' falso
        // Presente = cada item e' um no' que se expande.
        std::function<void(const db::CatalogItem&)> children;

        // Presente = cada item e' um OBJETO: duplo clique e F4 abrem o editor
        // dele, e o botao direito, o menu (View, Rename, Delete, Tools). A
        // pasta ganha "Create New", quando o tipo tem dialogo de criacao.
        std::optional<db::ObjectType> object_type;
        std::string object_schema;
        std::string object_parent;
    };
    void draw_list_folder(Icon folder_icon, const char* label,
                          db::CatalogList list, Icon item_icon,
                          const std::string& a, const std::string& b,
                          const std::string& c, const ListOptions& options);
    // Sobrecarga em vez de `const ListOptions& options = {}`: os
    // inicializadores de membro de uma struct aninhada so' valem com a classe
    // de fora completa, e um argumento padrao na declaracao vem antes disso.
    // O MSVC aceita; GCC e Clang recusam. O corpo de funcao membro ja' ve^ a
    // classe completa.
    void draw_list_folder(Icon folder_icon, const char* label,
                          db::CatalogList list, Icon item_icon,
                          const std::string& a = {}, const std::string& b = {},
                          const std::string& c = {}) {
        draw_list_folder(folder_icon, label, list, item_icon, a, b, c,
                         ListOptions{});
    }

    // Enquanto a subarvore de uma conexao e' desenhada, ELA e' a corrente:
    // session() devolve a sessao dela. Devolve a anterior, para restaurar.
    [[nodiscard]] std::size_t push_tree_connection(std::size_t index);
    void pop_tree_connection(std::size_t previous, std::size_t index, float top);

    // Id da conexao em cuja subarvore o usuario clicou neste quadro (0 =
    // nenhuma). Vira a ativa depois que a arvore inteira foi desenhada.
    std::size_t tree_claim_ = 0;
    // A ativa de verdade, guardada antes de os escopos a trocarem.
    std::size_t tree_previous_active_ = 0;

    // Nos a recolher no proximo quadro (pela chave do perfil).
    std::set<std::string> tree_collapse_;
    // "nav folder open|close <grupo>" do canal de comandos: abre ou fecha o
    // grupo no proximo quadro, pelo mesmo no' que o clique na seta alcanca.
    std::string tree_folder_request_;
    bool        tree_folder_request_open_ = false;

    // O que a arvore pediu e so' pode ser feito depois de desenhada.
    struct TreeRequests {
        std::optional<db::ConnectionProfile> open;         // conectar perfil salvo
        std::optional<db::ConnectionProfile> erase_saved;  // excluir (pede confirmacao)
        // "Move to folder": o perfil e a pasta de destino ("" = raiz).
        std::optional<std::pair<db::ConnectionProfile, std::string>> move;
        std::size_t reconnect  = 0;                        // id da conexao
        std::size_t disconnect = 0;
        std::string open_database;                         // banco a abrir...
        std::size_t open_database_root = 0;                // ...sob esta conexao
    };
    TreeRequests tree_requests_;
    void apply_tree_requests();

    // --- Pastas de conexao ("New Folder" do DBeaver) ---------------------------
    //
    // Uma pasta e' um caminho ("Clientes/Producao") no campo `folder` do
    // perfil; a que ainda nao tem conexao fica em settings_.folders.
    //
    // Todas as pastas que existem, ancestrais incluidos, na ordem em que
    // aparecem (perfis, conexoes abertas, pastas vazias).
    [[nodiscard]] std::vector<std::string> connection_folders() const;
    void draw_connection_folder(const std::string& path,
                                const std::vector<std::string>& all, std::size_t& total);
    // "Create >" do menu de contexto: Connection (so' em pasta e na raiz) e
    // New Folder. Com `move`, a pasta nova recebe essa conexao.
    void draw_folder_create_menu(const std::string& parent,
                                 const db::ConnectionProfile* move);
    void open_new_folder(std::string parent,
                         std::optional<db::ConnectionProfile> move = std::nullopt);
    void open_rename_folder(const std::string& path);
    // Troca o prefixo `from` por `to` em perfis, conexoes abertas e pastas
    // vazias, e grava. Renomear, e apagar (to = pai de from).
    void rebase_folder(const std::string& from, const std::string& to);
    void move_profile_to_folder(const db::ConnectionProfile& profile,
                                const std::string& folder);
    // "Rename" do menu da conexao (core.object.rename do DBeaver, F2): so' o
    // nome exibido -- o alvo (driver, host, banco, usuario) nao muda.
    void open_rename_connection(const db::ConnectionProfile& profile);
    void rename_profile(const db::ConnectionProfile& profile, const std::string& name);
    // Canal de comandos: "nav menu" (o menu da area vazia) e "nav connmenu
    // <conexao>" abrem o MESMO popup do botao direito.
    bool        nav_menu_request_ = false;
    std::string conn_menu_request_;

    void open_database_connection(std::size_t root_index,
                                  const std::string& database);
    // So' cria a entrada da sessao do banco, sem conectar. Devolve o indice.
    std::size_t add_database_connection(std::size_t root_index,
                                        const std::string& database);
    void close_database_connections(std::size_t root_id);

    db::ConnectionProfile erase_candidate_;
    bool                  show_erase_confirm_ = false;
    void draw_erase_connection_confirm();

    // "'<conexao>' Authentication" -- o BaseAuthDialog do DBeaver: pede
    // usuario e senha de uma conexao que nao tem a senha salva, ANTES de
    // conectar. Sem ele a conexao so' falhava com o erro do servidor, e a
    // unica saida era editar o perfil.
    struct AuthPrompt {
        bool        open = false;
        std::size_t connection_id = 0;
        char        user[128] = "";
        char        password[256] = "";
        bool        save = false;
        bool        focused = false;
    };
    AuthPrompt auth_prompt_;
    // "auth ok" do canal de comandos: o botao OK do dialogo.
    bool auth_prompt_submit_ = false;
    void draw_auth_prompt();
    void close_auth_prompt();

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

    // --- Comandos da grade (ui/grid_commands.cpp) ------------------------------
    //
    // Os `core.resultset.*` do DBeaver. A tabela (rotulo, tecla de cada
    // perfil) esta' em ui/commands.cpp; aqui, o que cada um faz.
    void run_grid_command(Command command);
    [[nodiscard]] bool grid_command_enabled(Command command);
    [[nodiscard]] bool grid_command_checked(Command command);

    // O bloco selecionado: do canto fixo (ancora) a' celula corrente.
    [[nodiscard]] db::GridSelection grid_selection(const SqlDocument& document,
                                                   const db::ResultSet& rs) const;
    [[nodiscard]] bool grid_cell_in_selection(const SqlDocument& document,
                                              std::size_t row,
                                              std::size_t column) const;
    // `extend` = Shift: a ancora fica, e o bloco cresce.
    void select_grid_cell(SqlDocument& document, std::size_t row,
                          std::size_t column, bool extend);
    void start_inline_edit(SqlDocument& document, const db::ResultSet& rs,
                           std::size_t row, std::size_t column);
    [[nodiscard]] std::optional<db::TableMeta> grid_source_table(
        const SqlDocument& document);

    void draw_grid_cell_menu(SqlDocument& document, const db::ResultSet& rs);
    void draw_grid_filter_bar(SqlDocument& document, const db::ResultSet& rs);
    void draw_grid_bottom_bar(SqlDocument& document, const db::ResultSet& rs);
    void grid_tool_button(Command command, Icon icon);
    void draw_grid_popups(SqlDocument& document, const db::ResultSet& rs);
    void draw_grid_text(SqlDocument& document, const db::ResultSet& rs);
    void draw_grid_panels();
    void apply_grid_table_requests(SqlDocument& document, const db::ResultSet& rs,
                                   int columns);
    [[nodiscard]] std::uint32_t grid_row_color(const SqlDocument& document,
                                               const db::ResultSet& rs,
                                               std::size_t row) const;

    void apply_grid_filter(SqlDocument& document, sql::ColumnFilter filter);
    void open_filter_settings(SqlDocument& document, const db::ResultSet& rs);
    void draw_filter_settings();
    [[nodiscard]] std::string default_filter_key(const SqlDocument& document) const;
    void save_default_filter(SqlDocument& document, const db::ResultSet& rs);
    void load_default_filter(SqlDocument& document, const db::ResultSet& rs);

    // Gravar, passando pela confirmacao quando ela esta' ligada.
    void request_save_edits(SqlDocument& document, bool commit_after);
    void draw_save_confirm();
    void cycle_value_viewer();

    // O outro canto do bloco selecionado.
    std::size_t anchor_row_    = 0;
    std::size_t anchor_column_ = 0;

    // A janela do resultado tinha o teclado no quadro anterior? Decide se
    // Ctrl+S grava a grade ou o script.
    bool grid_focused_ = false;

    // Arrastar com o botao esquerdo estende a selecao; 0 = nao esta' arrastando.
    std::size_t grid_drag_document_ = 0;

    enum class GridPopup { none, references, copy_as, generate, row_color, distinct };
    GridPopup grid_popup_request_ = GridPopup::none;

    // Onde o menu aberto por comando aparece: sob a celula corrente. O padrao
    // do ImGui e' a posicao do MOUSE, que num comando vindo do teclado pode
    // estar em qualquer canto da tela -- o menu abria longe do que ele trata.
    GridPopup grid_popup_last_ = GridPopup::none;
    float     selected_cell_x_ = 0.0f;
    float     selected_cell_y_ = 0.0f;

    std::vector<db::LinkQuery> grid_links_;   // o menu "References"

    // "Filter by value": os distintos da coluna, pedidos ao servidor.
    std::vector<db::DistinctValue> grid_distinct_;
    std::size_t grid_distinct_column_  = 0;
    bool        grid_distinct_loading_ = false;
    bool        grid_distinct_partial_ = false;   // so' das linhas carregadas
    std::size_t distinct_document_id_  = 0;       // consulta em curso
    char        grid_distinct_search_[64] = "";

    // Filtro escolhido num menu, aplicado no quadro seguinte.
    sql::ColumnFilter pending_filter_;
    std::size_t       pending_filter_document_ = 0;

    // Comando esperando as chaves da tabela chegarem do catalogo.
    Command grid_pending_command_ = Command::count;

    bool show_grid_metadata_   = false;
    bool show_grid_references_ = false;
    bool show_grid_calc_       = false;
    bool grid_metadata_docked_   = false;
    bool grid_references_docked_ = false;
    bool grid_calc_docked_       = false;
    std::string grid_references_requested_;   // "schema.tabela" ja' pedida

    struct FilterForm {
        struct Row {
            std::string name;
            bool        visible = true;
            char        criteria[256] = "";
        };
        bool             open = false;
        std::size_t      document_id = 0;
        std::vector<Row> rows;
        char             where[512] = "";
        int              sort_column = 0;        // 0 = ordem do servidor
        bool             sort_descending = false;
    };
    FilterForm filter_form_;

    struct SaveConfirm {
        bool        open = false;
        std::size_t document_id = 0;
        bool        commit_after = false;
        std::string sql;
    };
    SaveConfirm save_confirm_;

    // "Apply and commit": o COMMIT vai depois que as alteracoes passarem, e
    // a releitura depois do COMMIT.
    bool        commit_after_save_ = false;
    std::size_t reread_after_commit_ = 0;   // documento a reler
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
    //
    // Em auto-commit as alteracoes vao numa transacao propria (ADR 0014). Em
    // modo manual entram na transacao que o usuario ja' tem aberta, como no
    // DBeaver -- e `commit_after` a encerra ("Apply and commit changes").
    void save_pending_edits(SqlDocument& document, bool commit_after = false);

    // Linha nova com os valores de `row`, menos a chave. Usada pelo menu de
    // contexto e pela tecla -- uma regra so'.
    void duplicate_row(SqlDocument& document, const db::ResultSet& rs,
                       std::size_t row,
                       std::size_t anchor = db::RowInsertion::npos);

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

    // --- Editor de objeto (ui/object_editor.cpp, docs/OBJECT-EDITOR.md) -------
    //
    // A aba que o DBeaver abre com duplo clique ou F4 num no' da arvore:
    // Properties (com as secoes e o DDL) e Data.
    void open_object_editor(db::ObjectRef ref, bool data = false);
    void draw_object_editor(SqlDocument& document);
    void draw_object_properties(SqlDocument& document, const db::ObjectInfo& info,
                                bool loaded);
    void draw_object_form(SqlDocument& document, const db::ObjectInfo& info);
    void draw_object_section(SqlDocument& document, const db::ObjectInfo& info);
    void draw_object_source(SqlDocument& document, const db::ObjectInfo& info);
    void draw_object_permissions(SqlDocument& document, const db::ObjectInfo& info);
    void draw_object_data(SqlDocument& document);
    void save_object_edits(SqlDocument& document, const db::ObjectInfo& info);
    [[nodiscard]] std::string object_arguments(Session& target,
                                               const db::ObjectRef& ref);

    // A grade de um documento, dentro da janela corrente.
    void draw_grid_view(SqlDocument& document);

    // "Cancel active query" e "Terminate session", acima da grade quando o
    // resultado lista sessoes (Session Manager, Lock Manager).
    void draw_session_actions(SqlDocument& document, const db::ResultSet& rs);
    std::string session_pid_;   // o pid da linha selecionada, no ultimo quadro

    // Na arvore e nas listas do editor: duplo clique e F4 abrem o editor, o
    // botao direito abre o menu (View, Create New, Rename, Delete, Tools).
    void object_node(const db::ObjectRef& ref, bool toggled = false);
    void draw_object_menu(const db::ObjectRef& ref);
    void draw_object_tools_menu(const db::ObjectRef& ref);
    // O mesmo para o MySQL: Analyze, Check, Optimize, Repair, Truncate, Dump,
    // Execute script, Change password (db/mysql_object.hpp).
    void draw_mysql_tools_menu(const db::ObjectRef& ref);
    void run_mysql_table_tool(db::MysqlTableTool tool, const db::ObjectRef& ref,
                              std::string_view option);
    void draw_mysql_session_actions(SqlDocument& document, const db::ResultSet& rs);
    void draw_mssql_tools_menu(const db::ObjectRef& ref);
    void draw_mssql_session_actions(SqlDocument& document, const db::ResultSet& rs);
    void draw_sqlanywhere_tools_menu(const db::ObjectRef& ref);
    void draw_sqlanywhere_session_actions(SqlDocument& document,
                                          const db::ResultSet& rs);
    [[nodiscard]] bool can_create_object(db::ObjectType type) const;
    void create_menu_item(db::ObjectType type, const std::string& schema,
                          const std::string& parent);

    // Os dialogos de criar, renomear, apagar e das ferramentas. Um por vez:
    // campos genericos, com o significado dado pelo tipo.
    struct ObjectForm {
        enum class Kind : std::uint8_t {
            none, create, rename, drop, vacuum, truncate, refresh_mview, password,
            ms_backup, ms_restore,   // SQL Server: BACKUP / RESTORE DATABASE
            sa_backup,               // SQL Anywhere: BACKUP DATABASE DIRECTORY
        };
        Kind          kind = Kind::none;
        db::ObjectRef target;   // ao criar: so' o tipo, o schema e o pai

        char name[128]   = "";
        char text_a[256] = "";
        char text_b[256] = "";
        char text_c[256] = "";
        char text_d[256] = "";
        char body[8192]  = "";
        int  choice_a = 0;
        int  choice_b = 0;
        bool flag_a = false, flag_b = false, flag_c = false, flag_d = false;
        bool flag_e = false, flag_f = false, flag_g = false;

        // Verdadeiro uma vez: o primeiro campo recebe o teclado ao abrir.
        bool focused = false;
        [[nodiscard]] bool appearing_focus() noexcept {
            const bool first = !focused;
            focused = true;
            return first;
        }
    };
    ObjectForm object_form_;
    void open_object_form(ObjectForm::Kind kind, db::ObjectRef target);
    void draw_object_forms();
    bool list_combo(const char* id, db::CatalogList list, char* buffer,
                    std::size_t size, bool allow_empty, bool only_unflagged = false);
    void routine_combo(const char* id, char* buffer, std::size_t size,
                       std::string_view returns);

    // confirm_ddl, mais o que o editor faz depois que o servidor aceita: a
    // aba segue o objeto renomeado e as edicoes pendentes somem.
    void confirm_object_ddl(std::string title, db::AlterScript script,
                            std::size_t document_id = 0,
                            std::optional<db::ObjectRef> becomes = {});
    void finish_object_ddl(bool failed);
    struct ObjectApply {
        bool                         pending = false;
        std::size_t                  document_id = 0;
        std::optional<db::ObjectRef> becomes;
    };
    ObjectApply object_apply_;

    // --- Import Data (ui/data_transfer.cpp) ------------------------------------
    //
    // Carregar um CSV numa tabela: arquivo e formato, a previa com o destino
    // de cada coluna, e as opcoes de carga.
    struct ImportForm {
        bool        open = false;
        std::size_t connection_id = 0;
        std::string schema;
        std::string table;

        char           path[512] = "";
        db::CsvOptions options;
        std::string    text;       // o arquivo inteiro
        db::CsvTable   preview;    // as primeiras linhas

        std::vector<std::string> table_columns;
        std::vector<std::string> mapping;   // por coluna do arquivo; vazio = ignora

        bool truncate = false;
        int  batch_rows = 500;

        bool        running = false;
        bool        failed = false;
        std::size_t rows = 0;
        std::string status;
    };
    ImportForm import_form_;
    bool       import_submit_ = false;   // canal de comandos: "import run"
    void open_import(const std::string& schema, const std::string& table);
    void reload_import_file(bool detect);
    void draw_import_data_window();

    // --- Backup e Restore (ui/data_transfer.cpp, db/native_tools.hpp) ---------
    //
    // O "Tools > Backup / Restore" do DBeaver: um formulario com as opcoes do
    // pg_dump / pg_restore, e a janela com a saida do programa enquanto roda.
    struct ToolForm {
        enum class Kind : std::uint8_t { none, backup, restore };
        Kind        kind = Kind::none;
        std::size_t connection_id = 0;
        std::string database;
        std::string schema;   // backup de um schema
        std::string table;    // backup de uma tabela ("schema.tabela")

        int  format = 0;          // db::DumpFormat
        int  compression = -1;    // -1 = padrao
        char encoding[32] = "";
        bool use_inserts = false;
        bool no_privileges = false;
        bool no_owner = false;
        bool clean = false;
        bool create = false;
        char file[512] = "";
        std::string error;

        // MySQL (mysqldump): as opcoes de MySQLExportSettings do DBeaver.
        int  my_method       = 0;      // db::MysqlDumpOptions::Method
        bool my_no_create    = false;
        bool my_add_drop     = true;
        bool my_disable_keys = true;
        bool my_extended     = true;
        bool my_events       = false;
        bool my_routines     = false;
        bool my_comments     = true;
        bool my_hex_blob     = false;
        bool my_no_data      = false;
    };
    ToolForm tool_form_;
    void draw_mysql_tool_form();
    bool     tool_submit_ = false;   // canal de comandos: "tool run"

    struct ToolRun {
        bool        open = false;
        std::string title;
        std::string command_line;   // sem a senha: ela vai no ambiente
        Process     process;
        std::string output;
        bool        running = false;
        int         exit_code = 0;
        bool        reload_after = false;   // restore: a arvore mudou
        std::size_t connection_id = 0;
    };
    ToolRun tool_run_;

    void open_backup(const db::ObjectRef& ref);
    void open_restore(const std::string& database);
    void draw_tool_windows();
    void start_tool(std::string title, Result<ProcessOptions> command, bool reload_after);

    // Linhas do OTTER_COMMAND_FILE que falam com o editor de objeto, os
    // dialogos e a confirmacao de DDL. Verdadeiro se a linha era uma delas.
    bool object_command(const std::string& line);
    bool        object_form_submit_ = false;   // "form ok": aperta o botao principal
    db::ObjectRef object_menu_debug_;          // "objectmenu": menu a mostrar
    bool          object_menu_debug_open_ = false;

    // A parte de baixo (resultado, log) estava escondida por haver um editor
    // de objeto na frente.
    bool bottom_hidden_for_object_ = false;

    // Aba de documento a trazer para a frente no proximo quadro.
    std::size_t select_document_id_ = 0;

    // O que a PROXIMA pasta desenhada cria pelo menu de contexto ("Create New
    // Table"). Dito por quem chama draw_folder_node, consumido la' dentro.
    struct FolderCreate {
        db::ObjectType type = db::ObjectType::table;
        std::string    schema;
        std::string    parent;
    };
    std::optional<FolderCreate> folder_create_;
    // "nav foldermenu <rotulo>": a proxima pasta com esse rotulo abre o menu
    // dela. O rotulo chega em ingles (a chave); a comparacao e' com o texto
    // ja' traduzido que a pasta recebe.
    std::string folder_menu_request_;
    void folder_creates(db::ObjectType type, std::string schema = {},
                        std::string parent = {}) {
        folder_create_ = FolderCreate{type, std::move(schema), std::move(parent)};
    }

    // A aba em que o editor de uma relacao abre: a ultima que o usuario
    // deixou, como no DBeaver.
    bool relation_opens_data_ = false;

    // --- Comandos de aplicacao, banco e navegador (ui/app_commands.cpp) --------
    //
    // Os `core.*` e `ui.navigator.*` do DBeaver: menu Database, menu de
    // contexto da arvore, barra de ferramentas. A tabela (rotulo, tecla de
    // cada perfil) esta' em ui/commands.cpp; aqui, o que cada um faz.
    [[nodiscard]] bool app_command_enabled(Command command);
    [[nodiscard]] bool app_command_checked(Command command);
    void run_app_command(Command command);
    // Linhas do OTTER_COMMAND_FILE destes comandos ("nav select ...").
    bool app_command_line(const std::string& line);

    void draw_database_menu();
    // As abas abertas recebem a paleta do tema novo.
    void reapply_palettes();
    void draw_navigate_menu();
    void draw_window_menu();
    void draw_app_windows();

    // Keep-alive e "fechar conexoes ociosas" do perfil, uma vez por quadro.
    void tick_connections();

    // O no' da arvore sobre o qual os comandos de contexto `navigator` agem:
    // o que esta' sob o mouse, ou o ultimo clicado.
    struct NavTarget {
        bool          valid = false;
        std::size_t   connection_id = 0;
        db::ObjectRef ref;
    };
    NavTarget nav_selected_;
    NavTarget nav_hovered_;
    int       nav_hover_frame_ = -10;
    void nav_track(const db::ObjectRef& ref);
    [[nodiscard]] const NavTarget* nav_target() const;
    // A sessao do alvo, tornada ativa para o comando agir sobre ela.
    bool activate_nav_target(const NavTarget& target);

    [[nodiscard]] const db::ObjectFilter& object_filter_for(std::size_t connection_index) const;
    [[nodiscard]] std::string filter_key(std::size_t connection_index) const;
    void refresh_object_filters();
    void move_saved_profile(int direction, bool to_edge);

    void open_bookmark(const AppSettings::Bookmark& bookmark);
    std::optional<AppSettings::Bookmark> pending_bookmark_;
    void apply_reset_settings();
    [[nodiscard]] std::string diagnostics_text();
    void run_script_native();
    void set_default_schema(const std::string& schema);
    void link_tree_to_editor();
    std::size_t linked_document_id_ = 0;   // a aba que a arvore ja' acompanhou
    void copy_special_now();
    void load_cell_from_file(SqlDocument& document, const std::string& path);
    void save_cell_to_file(SqlDocument& document, const std::string& path);

    struct AppWindows {
        bool txn_pending   = false;
        bool txn_log       = false;
        bool select_connection = false;
        bool select_schema = false;
        bool select_database = false;
        std::size_t database_connection = 0;   // id da janela cujo icone abriu a lista
        bool goto_object   = false;
        bool palette       = false;
        bool filter_config = false;
        bool bookmarks     = false;
        bool preferences   = false;
        bool drivers       = false;
        bool scripts       = false;
        bool url_dialog    = false;
        bool new_folder    = false;
        bool delete_folder = false;
        bool rename_connection = false;
        bool copy_special  = false;
        bool paste_special = false;
        bool associate     = false;
        bool confirm_reset = false;
        bool confirm_history = false;
        bool log_filter    = false;
        bool views         = false;
        bool tools_popup   = false;
        bool dashboard     = false;
        bool dashboard_catalog = false;
        bool dashboard_settings = false;

        // Foco no campo de busca ao abrir (uma vez).
        bool focus_search = false;
        bool focus_nav_filter = false;
        bool diagnostics_open = false;
        // Canal de comandos: "app ok" / "app pick" e "app cancel".
        bool submit = false;
        bool cancel = false;

        char search[128]       = "";
        int  search_cursor     = 0;
        char url[512]          = "";
        std::string url_error;
        char folder[128]       = "";
        // A janela "New Folder": onde a pasta nasce, a conexao que ela recebe
        // e -- quando e' "Rename" -- a pasta que esta' sendo renomeada.
        std::string folder_parent;
        std::string folder_rename;
        std::optional<db::ConnectionProfile> folder_move;
        std::string folder_delete;   // a da confirmacao "Delete folder"
        // A janela "Rename Connection": o nome digitado e a conexao.
        char connection_name[128] = "";
        std::optional<db::ConnectionProfile> rename_target;
        char filter_include[512] = "";
        char filter_exclude[512] = "";
        char copy_column[16]   = "";
        char copy_row[16]      = "";
        char copy_quote[8]     = "";
        char copy_null[32]     = "";
        bool paste_as_rows     = false;
        bool paste_empty_null  = true;
        std::string diagnostics;
        std::string diagnostics_path;

        // Palavra completada por ultimo, para "Word completion" rodar em ciclo.
        std::string word_prefix;
        std::string word_last;
    };
    AppWindows app_;

    // --- Dashboard (ui/dashboard.cpp) -------------------------------------------
    struct DashboardSeries {
        std::string        name;
        std::vector<float> points;
        double             previous = 0.0;
        bool               has_previous = false;
    };
    struct DashboardChartState {
        std::string                  id;
        std::vector<DashboardSeries> series;
        std::string                  error;
    };
    struct DashboardState {
        std::size_t connection_id = 0;
        std::vector<DashboardChartState> charts;
        std::size_t seen_serial = 0;
        double      last_request = -1.0e9;
        double      last_sample  = 0.0;
        std::string zoomed;      // "View chart": o grafico em tela cheia
        std::string selected;    // alvo de "Remove chart"
        bool        docked = false;
    };
    DashboardState dashboard_;
    void draw_dashboard_window();
    void dashboard_reset_charts();
    [[nodiscard]] std::vector<std::string> dashboard_chart_ids() const;

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
    // Primeira execucao: as conexoes do DBeaver, do pgAdmin e do SSMS.
    void import_external_on_first_run(bool fresh);
    void persist_profiles();
    // Devolve o perfil como foi GRAVADO: o nome pode ter ganho sufixo "_N"
    // para nao repetir o de outra conexao, e quem abre a conexao precisa do
    // mesmo rotulo que o Raft mostra.
    [[nodiscard]] db::ConnectionProfile remember_profile(
        const db::ConnectionProfile& profile);

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

    // Conta o resultado inteiro (o `resultset.count` do DBeaver). Sob
    // demanda: e' outra varredura completa.
    void count_total_rows(SqlDocument& document);

    // Documento que espera uma contagem. O resultado chega pelo mesmo canal
    // da grade, e sem a marca substituiria as linhas por uma celula.
    std::size_t counting_document_id_ = 0;

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

    // --- Scripts em disco (ui/script_session.cpp, ui/script_store.hpp) --------
    //
    // Todo script e' gravado sozinho em `.script`, ao lado do executavel,
    // pouco depois de a digitacao parar; as abas abertas voltam ao iniciar.
    void restore_scripts();
    void autosave_scripts();
    bool save_script_now(SqlDocument& document);
    void write_script_index();
    // Antes de fechar a aba: grava o pendente e guarda o script entre os
    // fechados.
    void retire_script(SqlDocument& document);
    SqlDocument* open_stored_script(const ScriptEntry& entry);
    [[nodiscard]] ScriptEntry script_entry(const SqlDocument& document) const;
    [[nodiscard]] std::size_t ensure_script_connection(const ScriptEntry& entry);
    [[nodiscard]] std::vector<std::string> open_script_names(
        const SqlDocument* except) const;
    [[nodiscard]] std::string next_script_name() const;
    // A conexao tem como ser (re)conectada pela aba: e' de um perfil salvo,
    // ou a sessao de um banco dele. A Session vazia de antes da primeira
    // conexao nao tem.
    [[nodiscard]] bool can_connect_from_tab(std::size_t connection_id) const;

    std::string              scripts_dir_;
    // Scripts que existem em disco e nao estao abertos, com a conexao de cada
    // um: e' o que "Show scripts" oferece para reabrir.
    std::vector<ScriptEntry> closed_scripts_;
    // O texto do indice como esta' em disco, para so' regravar quando mudar.
    std::string              script_index_written_;
    double                   script_index_checked_at_ = 0.0;
    double                   script_clock_ = 0.0;   // ImGui::GetTime() do ultimo quadro
    // O script que estava na frente ao sair, e por quantos quadros ainda
    // pedir que a aba dele venha para a frente (ver draw_editor_panel).
    std::size_t              restore_front_document_ = 0;
    int                      restore_front_frames_   = 0;

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

        // Sessao de OUTRO banco do mesmo servidor, aberta ao expandir o
        // banco na arvore (ADR 0018): o id da conexao raiz. 0 = e' raiz.
        std::size_t parent_id = 0;

        // Abrir o no' na arvore no proximo quadro -- quem conecta pelo
        // dialogo espera ver o que ha' dentro.
        bool expand_once = false;

        // O usuario ja' respondeu ao dialogo de autenticacao desta conexao
        // nesta execucao: a senha digitada (mesmo vazia) vale ate' fechar, e
        // reconectar ou abrir outro banco do servidor nao pergunta de novo.
        bool credentials_asked = false;

        // A janela de editor desta conexao ja' foi ancorada nesta execucao.
        bool docked = false;

        // Keep-alive / fechar ociosa: quando a sessao trabalhou pela ultima
        // vez e quando levou o ultimo ping (ImGui::GetTime()).
        double last_active = 0.0;
        double last_ping   = 0.0;

        // Filtro de objetos desta conexao (core.object.filter.*), refeito das
        // preferencias por refresh_object_filters().
        db::ObjectFilter filter;
    };

    // Conecta a sessao com o perfil dela, levando as opcoes de listagem de
    // bancos. Ponto unico: abrir, reconectar e abrir banco passam por aqui.
    void connect_session(Connection& connection);
    // O titulo de uma conexao na tela: "PostgreSQL 18 (ERP_TID)" -- o nome e,
    // entre parenteses, o banco em que a sessao esta'. Pedido do usuario
    // (2026-10-01): com varias sessoes do mesmo servidor, so' o nome nao diz
    // em qual banco o script vai rodar.
    [[nodiscard]] std::string connection_title(const Connection& connection) const;

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
    // O icone de banco na aba da janela: abre "Select active database".
    void draw_tab_database_icon(std::size_t connection_id);
    void open_database_picker(std::size_t connection_id);
    // Leva o script da janela `connection_id` para o banco `database` do
    // mesmo servidor (MySQL: USE; os outros: a sessao daquele banco).
    void switch_tab_database(std::size_t connection_id, const std::string& database);

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

        // De onde o valor veio, para o "Edit cell" gravar de volta e para
        // "Switch content viewer" reformatar.
        std::size_t      document_id = 0;
        std::size_t      row = 0;
        std::size_t      cell_column = 0;
        std::string      raw;            // o valor sem formatar
        bool             is_null = false;
        db::DataKind     kind = db::DataKind::unknown;
        std::size_t      hex_limit = 64 * 1024;

        // Modo de edicao: o texto vai para o buffer da grade com "Apply".
        bool              editing = false;
        std::vector<char> buffer;

        // Refaz `text` a partir de `raw`, na visao escolhida.
        void format();
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

    // Quando a janela mudou pela ultima vez (ImGui::GetTime); negativo = nada
    // por gravar.
    double window_changed_at_ = -1.0;

    // Filtro do inspetor de queries. As internas de catalogo aparecem por
    // padrao -- e' a diferenca deliberada para o DBeaver, que as esconde:
    // ferramenta que nao mostra o que faz e' dificil de confiar (ADR 0008).
    bool query_log_failed_only_   = false;
    bool query_log_show_internal_ = true;

    // Confirmacao de saida aberta. A saida so' acontece se o usuario
    // escolher sair; qualquer outra coisa apenas fecha o dialogo.
    bool confirm_quit_       = false;

    // Desenha a confirmacao de saida, quando ha' trabalho a perder.
    void draw_quit_confirm();

    // --- Fechar a aba de uma conexao -----------------------------------------
    // O "x" da janela de editor: fecha os documentos DELA; a sessao continua.
    // O pedido e' anotado e aplicado no inicio de draw_editor_panel, fora do
    // laco que desenha as janelas.
    // A conexao cuja janela de editor recebe o foco neste quadro (0 = nenhuma);
    // calculada em draw_editor_panel, antes de desenhar as janelas.
    std::size_t editor_focus_connection_ = 0;
    std::size_t close_tab_request_ = 0;   // id da conexao; 0 = nenhum
    std::size_t confirm_close_tab_ = 0;   // confirmacao aberta para esta
    void apply_close_tab_request();
    void close_connection_documents(std::size_t connection_id);
    void draw_close_tab_confirm();
    [[nodiscard]] std::size_t connection_unsaved_documents(std::size_t connection_id) const;
    [[nodiscard]] std::size_t connection_pending_cell_edits(std::size_t connection_id) const;

    // Renomear a aba de script. Fora do menu de contexto: um menu se fecha ao
    // primeiro clique fora, e um campo de texto precisa sobreviver a varios.
    void draw_rename_tab();
    std::size_t renaming_document_ = 0;   // 0 = nenhum
    char        rename_buffer_[128] = "";

    // Ir para a linha / coluna (resultset.grid.gotoRow, gotoColumn). Um
    // dialogo, nao um campo na barra: e' acao de uma vez, e um campo
    // permanente gastaria espaco da grade em troca de um uso esporadico.
    void draw_goto_dialog();

    // Buscar o resultado inteiro, sem paginacao (resultset.fetch.all).
    // Contraria o ADR 0011 de proposito, a pedido explicito do usuario --
    // ver o comentario no atalho.
    void fetch_all_rows(SqlDocument& document);
    void draw_fetch_all_confirm();
    std::size_t confirm_fetch_all_ = 0;   // documento aguardando confirmacao
    enum class GotoKind { row, column };
    bool     goto_open_ = false;
    GotoKind goto_kind_ = GotoKind::row;
    char     goto_buffer_[32] = "";

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
    // O que cada candidata faria ao ser importada (ver ImportAction).
    std::vector<int>               import_actions_;
    // "dialog import run" do canal de comandos: o botao "Import selected".
    bool                           import_connections_submit_ = false;
    // "dialog import existing": marca SO' as linhas que completam o que ja'
    // esta' aqui (senha, grupo), sem acrescentar conexao.
    bool                           import_passwords_only_ = false;

    // --- Exportacao ----------------------------------------------------------
    bool               show_export_ = false;
    // Exportar a consulta inteira (lida do servidor em pedacos), e nao so' as
    // linhas carregadas na grade.
    bool               export_whole_query_ = false;
    bool               export_submit_ = false;   // canal de comandos: "export save"
    // "Export Data" da arvore: abrir a janela quando os dados do objeto
    // chegarem.
    bool               export_object_pending_ = false;

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

    // Pedido de abrir o menu da coluna por TECLADO (F11, Shift+F11).
    // +1 para que 0 signifique "nenhum pedido": a coluna 0 e' valida.
    //
    // O menu nao pode ser aberto de dentro de handle_grid_keys: o popup do
    // ImGui se prende ao ULTIMO item desenhado, e ali o ultimo item e' a
    // celula, nao o cabecalho. O pedido atravessa o quadro e e' consumido
    // onde o cabecalho acabou de ser desenhado.
    std::size_t        grid_menu_request_ = 0;
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

    // Os padroes da conexao ja' foram aplicados a esta abertura da janela?
    //
    // Reaplica-los a cada quadro desfaria o que o usuario escolhesse dentro
    // dela. Reposto quando a janela fecha.
    bool               export_defaults_applied_ = false;
    std::string        export_path_;
    std::string        export_status_;
};

} // namespace otter::ui
