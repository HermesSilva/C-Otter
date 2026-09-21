#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder: layout inicial programatico

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace otter::ui {
namespace {

constexpr float kStatusBarHeight = 26.0f;
constexpr float kToolbarHeight   = 34.0f;

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

std::string to_lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return false;
    }
    return true;
}

// Casamento por subsequencia: "cliid" casa "cliente_id".
bool fuzzy_subsequence(std::string_view needle, std::string_view haystack) {
    std::size_t i = 0;
    for (char c : haystack) {
        if (i < needle.size() && c == needle[i]) ++i;
    }
    return i == needle.size();
}

// Espacos ate' a coluna `width`. Como a fonte e' monoespacada, isto alinha o
// tipo numa coluna propria dentro do popup, que so' aceita texto puro
// (ver ADR 0004, secao de viabilidade).
std::string pad_to(std::string_view text, std::size_t width) {
    return text.size() >= width ? std::string(1, ' ')
                                : std::string(width - text.size(), ' ');
}

constexpr std::string_view kWelcomeSql =
    "-- C-Otter: every JOIN is an OTTER JOIN\n"
    "--\n"
    "-- Ctrl+Enter executa | Ctrl+Espaço completa\n"
    "\n"
    "SELECT table_name, column_name, data_type\n"
    "  FROM information_schema.columns\n"
    " WHERE table_schema = 'public'\n"
    " ORDER BY table_name, ordinal_position;\n";

// Tema da lontra aplicado ao editor: as cores vem da mesma paleta do logo que
// o resto da UI, para que o painel de SQL nao pareca um corpo estranho.
void apply_editor_palette(TextEditor& editor) {
    using Color = TextEditor::Color;

    // Parte da paleta base do widget adequada ao tema, e NAO da paleta atual
    // do editor: reaproveitar a anterior deixaria cores nao listadas abaixo
    // com o resíduo do tema antigo -- foi assim que o tema claro ficou com
    // texto claro sobre fundo claro.
    TextEditor::Palette p = current_theme().is_dark
                                ? TextEditor::GetDarkPalette()
                                : TextEditor::GetLightPalette();

    auto set = [&p](Color c, std::uint32_t value) {
        p[static_cast<std::size_t>(c)] = static_cast<ImU32>(value);
    };

    const Palette& t = colors();

    set(Color::background,      t.bg_darkest);
    set(Color::text,            t.text);
    set(Color::keyword,         t.syntax_keyword);   // SELECT, FROM, JOIN
    set(Color::declaration,     t.data_light);
    set(Color::number,          t.syntax_number);
    set(Color::string,          t.syntax_string);
    set(Color::punctuation,     t.text_dim);
    set(Color::preprocessor,    t.warn);
    set(Color::identifier,      t.text);
    set(Color::knownIdentifier, t.data);             // tabelas e colunas
    set(Color::comment,         t.syntax_comment);
    set(Color::cursor,          t.data_light);
    set(Color::selection,       with_alpha(t.data, 0.31f));
    set(Color::whitespace,      t.bg_light);
    set(Color::lineNumber,      t.text_dim);
    set(Color::currentLineNumber, t.accent_light);
    set(Color::currentLineHighlight,       with_alpha(t.accent, 0.09f));
    set(Color::currentLineHighlightBorder, with_alpha(t.accent, 0.19f));
    set(Color::matchingBracketBackground,  with_alpha(t.data, 0.25f));
    set(Color::matchingBracketActive, t.data_light);

    editor.SetPalette(p);
}

// Indicador de atividade: um ponto que pulsa enquanto o worker trabalha.
void draw_busy_indicator() {
    const float t = static_cast<float>(ImGui::GetTime());
    const float alpha = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
    ImVec4 color = col4(colors().data_light);
    color.w = alpha;
    ImGui::TextColored(color, "  ●");
}

} // namespace

MainShell::MainShell()
    : autocomplete_config_(std::make_unique<TextEditor::AutoCompleteConfig>()) {
    // Completion (ADR 0004). Uma configuracao compartilhada por todos os
    // documentos: o callback descobre o editor ativo em suggest().
    autocomplete_config_->triggerOnTyping    = true;
    autocomplete_config_->triggerInComments  = false;
    autocomplete_config_->triggerInStrings   = false;
    autocomplete_config_->suggestionWidth    = 52;
    autocomplete_config_->noSuggestionsLabel = TR("no suggestions");
    autocomplete_config_->userData           = this;
    autocomplete_config_->callback = [](TextEditor::AutoCompleteState& state) {
        static_cast<MainShell*>(state.userData)->suggest(state);
    };

    // Primeiro documento, com o texto de boas-vindas.
    new_document().editor().SetText(std::string(kWelcomeSql));

    // O assistente conecta e, ao concluir, tambem guarda o perfil ativo.
    connection_dialog_.set_on_connect([this](const db::ConnectionProfile& profile) {
        active_profile_ = profile;
        session_.connect_async(profile.to_conn_config());
    });
    connection_dialog_.set_on_save([this](const db::ConnectionProfile& profile) {
        active_profile_ = profile;
    });

    // Abre primeiro: open_new() reinicia o perfil, e so' depois disso faz
    // sentido preencher a partir do ambiente (como psql faz).
    connection_dialog_.open_new();

    db::ConnectionProfile& profile = connection_dialog_.profile();
    auto from_env = [](const char* name, std::string& target) {
        if (const char* value = std::getenv(name)) target = value;
    };
    from_env("PGHOST",     profile.host);
    from_env("PGDATABASE", profile.database);
    from_env("PGUSER",     profile.user);
    from_env("PGPASSWORD", profile.password);
    if (const char* port = std::getenv("PGPORT")) {
        profile.port = static_cast<std::uint16_t>(std::atoi(port));
    }

    // Abre a galeria de icones direto na inicializacao. Existe porque a
    // captura de tela para conferencia visual precisa de um caminho
    // deterministico: automatizar o clique no menu erra o alvo com frequencia.
    if (std::getenv("OTTER_SHOW_ICONS") != nullptr) {
        show_icons_ = true;
        connection_dialog_.close();
    }

    // Tema inicial por ambiente, pelo mesmo motivo: conferir os tres temas
    // exige tres capturas, e trocar pelo menu a cada uma e' fragil.
    if (const char* theme = std::getenv("OTTER_THEME")) {
        set_theme(theme);
    }

    // Conecta direto, usando o perfil ja' montado a partir de PGHOST/PGUSER/...
    // Serve para conferir a arvore de objetos numa captura: automatizar o
    // clique em "Conectar" erra o alvo com frequencia, e uma tela conferida a'
    // mao vale mais que um clique que talvez tenha acontecido.
    if (std::getenv("OTTER_AUTOCONNECT") != nullptr) {
        active_profile_ = profile;
        session_.connect_async(profile.to_conn_config());
        connection_dialog_.close();
    }
}

MainShell::~MainShell() = default;

SqlDocument& MainShell::new_document() {
    documents_.push_back(std::make_unique<SqlDocument>(next_document_id_++));
    SqlDocument& document = *documents_.back();

    document.editor().SetAutoCompleteConfig(autocomplete_config_.get());
    apply_editor_palette(document.editor());

    active_document_ = documents_.size() - 1;
    return document;
}

void MainShell::close_document(std::size_t index) {
    if (index >= documents_.size()) return;

    // Nunca ficamos sem nenhuma aba: fechar a última abre uma vazia.
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (documents_.empty()) {
        new_document();
        return;
    }
    if (active_document_ >= documents_.size()) {
        active_document_ = documents_.size() - 1;
    }
}

void MainShell::close_others(std::size_t keep_index) {
    if (keep_index >= documents_.size()) return;

    // Guarda o id ANTES de mover: depois do move, documents_[keep_index] e' um
    // unique_ptr vazio e consulta-lo seria desreferenciar nulo.
    const std::size_t keep_id = documents_[keep_index]->id();

    std::vector<std::unique_ptr<SqlDocument>> kept;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        // Abas fixadas sobrevivem a "fechar outras" -- e' o que "fixar" quer
        // dizer.
        if (i == keep_index || documents_[i]->pinned()) {
            kept.push_back(std::move(documents_[i]));
        }
    }
    documents_ = std::move(kept);

    active_document_ = 0;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i]->id() == keep_id) {
            active_document_ = i;
            break;
        }
    }
}

SqlDocument* MainShell::active_document() {
    if (documents_.empty()) return nullptr;
    active_document_ = std::min(active_document_, documents_.size() - 1);
    return documents_[active_document_].get();
}

// Gera sugestoes de completion (ADR 0004), sobre metadados reais e com o
// escopo sintatico do otter_sql.
void MainShell::suggest(TextEditor::AutoCompleteState& state) {
    struct Candidate {
        std::string text;
        int         rank;   // menor = melhor
    };
    std::vector<Candidate> candidates;

    const std::string term = to_lower(state.searchTerm);

    auto matches = [&term](std::string_view name) {
        if (term.empty()) return true;
        const std::string lowered = to_lower(std::string(name));
        return lowered.starts_with(term) || fuzzy_subsequence(term, lowered);
    };

    auto add = [&](std::string text, int rank) {
        if (matches(text)) candidates.push_back({std::move(text), rank});
    };

    // Camada 2 -- escopo sintatico. Descobre o que faz sentido AQUI: tabelas
    // depois de FROM, colunas depois de SELECT/WHERE, colunas de UMA tabela
    // depois de "alias.".
    // O completion age sobre o documento ativo: cada aba tem seu editor.
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    const std::string script = document->editor().GetText();
    const TextEditor::DocPos cursor =
        document->editor().GetCurrentCursorPosition();

    // DocPos e' (linha, indice); o analisador trabalha com offset em bytes.
    std::size_t offset = 0;
    {
        std::size_t line = 0;
        while (line < cursor.line && offset < script.size()) {
            if (script[offset] == '\n') ++line;
            ++offset;
        }
        offset = std::min(offset + cursor.index, script.size());
    }

    const sql::ScopeInfo scope =
        sql::analyze_scope(script, sql::postgres_dialect(), offset);

    const bool want_tables =
        scope.context == sql::CompletionContext::table_expected ||
        scope.context == sql::CompletionContext::schema_member ||
        scope.context == sql::CompletionContext::unknown;

    const bool want_columns =
        scope.context == sql::CompletionContext::column_expected ||
        scope.context == sql::CompletionContext::alias_member ||
        scope.context == sql::CompletionContext::unknown;

    // Tabelas visiveis na query, por nome e por alias.
    auto in_scope = [&scope](std::string_view table) {
        return std::any_of(scope.tables.begin(), scope.tables.end(),
                           [&](const sql::TableRef& ref) {
                               return iequals(ref.name, table);
                           });
    };

    // Depois de "alias.", so' interessam as colunas daquela tabela.
    std::string qualified_table;
    if (scope.context == sql::CompletionContext::alias_member) {
        for (const sql::TableRef& ref : scope.tables) {
            if (iequals(ref.alias, scope.qualifier) ||
                iequals(ref.name, scope.qualifier)) {
                qualified_table = ref.name;
                break;
            }
        }
    }

    // Camada 3 -- metadados reais do servidor.
    for (const db::SchemaMeta& schema : session_.schemas()) {
        for (const db::TableMeta& table : schema.tables) {
            const bool referenced = in_scope(table.name);

            if (want_columns) {
                // Filtra por tabela quando o cursor esta' apos "alias.".
                const bool skip = !qualified_table.empty() &&
                                  !iequals(qualified_table, table.name);
                if (!skip) {
                    for (const db::ColumnMeta& column : table.columns) {
                        // Rank 0: coluna de tabela presente na query.
                        int rank = referenced ? 0 : 2;
                        if (column.primary_key) rank -= 1;   // chaves sobem
                        add(column.name + pad_to(column.name, 30) +
                                column.type_name +
                                (column.primary_key ? "  PK" : ""),
                            rank);
                    }
                }
            }

            if (want_tables && qualified_table.empty()) {
                const char* label =
                    table.kind == db::ObjKind::view ? "view" : "tabela";
                add(table.name + pad_to(table.name, 30) + label,
                    referenced ? 1 : 3);
            }
        }
    }

    // Camada 1 -- keywords, por ultimo: sao as mais previsiveis. Suprimidas
    // quando o contexto pede um nome de objeto, onde so' atrapalhariam.
    const bool want_keywords =
        scope.context == sql::CompletionContext::unknown ||
        scope.context == sql::CompletionContext::column_expected;

    if (want_keywords) {
        static const char* const kKeywords[] = {
            "SELECT", "FROM", "WHERE", "GROUP BY", "ORDER BY", "HAVING",
            "INNER JOIN", "LEFT JOIN", "RIGHT JOIN", "FULL JOIN", "ON",
            "INSERT INTO", "UPDATE", "DELETE FROM", "VALUES", "SET",
            "COUNT", "SUM", "AVG", "MIN", "MAX", "DISTINCT", "AS",
            "LIMIT", "OFFSET", "CASE", "WHEN", "THEN", "ELSE", "END",
            "CREATE TABLE", "ALTER TABLE", "DROP TABLE",
            "BEGIN", "COMMIT", "ROLLBACK",
        };
        for (const char* keyword : kKeywords) add(keyword, 5);
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.rank != b.rank) return a.rank < b.rank;
                         return a.text < b.text;
                     });

    state.suggestions.clear();
    state.suggestions.reserve(candidates.size());
    for (Candidate& c : candidates) state.suggestions.push_back(std::move(c.text));
}

void MainShell::execute_current_sql() {
    if (session_.state() != SessionState::connected || session_.busy()) return;

    SqlDocument* document = active_document();
    if (document == nullptr) return;

    std::string sql = document->sql_to_execute();
    if (sql.empty()) return;

    // Guarda QUAL documento pediu: o resultado deve voltar para ele, mesmo que
    // o usuario troque de aba enquanto a query roda.
    executing_document_id_ = document->id();
    document->set_executing(true);
    document->set_status({});

    session_.execute_async(std::move(sql));
}

void MainShell::draw() {
    // Colhe o resultado e entrega ao documento que o pediu -- nao ao que
    // estiver ativo agora, porque o usuario pode ter trocado de aba.
    if (!session_.busy() && executing_document_id_ != 0) {
        for (auto& document : documents_) {
            if (document->id() != executing_document_id_) continue;

            if (auto fresh = session_.take_result()) {
                document->set_result(std::move(*fresh));
            }
            document->set_status(session_.status_message());
            document->set_executing(false);
            break;
        }
        executing_document_id_ = 0;
    }

    // Atalhos globais. Registrados aqui, e nao so' rotulados no menu: um
    // atalho anunciado que nao funciona e' pior que nenhum.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter)) {
        execute_current_sql();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N)) {
        connection_dialog_.open_new();
    }
    // Ctrl+T abre uma aba; Ctrl+W fecha a atual -- convencao de navegador.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_T)) {
        new_document();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W)) {
        close_document(active_document_);
    }
    // Commit e rollback: mesmos atalhos do DBeaver.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C)) {
        session_.commit_async();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R)) {
        session_.rollback_async();
    }

    draw_menu_bar();
    draw_toolbar();
    draw_dockspace();

    draw_raft_panel();
    draw_navigator_panel();
    draw_editor_panel();
    draw_grid_panel();
    draw_query_log_panel();
    draw_status_bar();

    // Traduz o estado da sessao para o que o assistente precisa exibir.
    ConnectionDialog::Feedback feedback;
    feedback.busy      = session_.busy();
    feedback.failed    = session_.state() == SessionState::failed;
    feedback.succeeded = session_.state() == SessionState::connected;
    if (feedback.failed || feedback.succeeded) {
        feedback.message = session_.status_message();
    }
    connection_dialog_.draw(feedback);

    if (show_about_) draw_about_window();
    if (show_icons_) draw_icon_gallery();
    if (show_demo_)  ImGui::ShowDemoWindow(&show_demo_);
}

void MainShell::draw_dockspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Host ocupa a area de trabalho menos as faixas da barra de ferramentas
    // (no topo) e da barra de status (no rodape).
    const ImVec2 host_size(
        vp->WorkSize.x,
        vp->WorkSize.y - kToolbarHeight - kStatusBarHeight);
    const ImVec2 host_pos(vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight);

    ImGui::SetNextWindowPos(host_pos);
    ImGui::SetNextWindowSize(host_size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##OtterDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dock_id = ImGui::GetID("OtterDockSpace");

    if (!layout_initialized_) {
        layout_initialized_ = true;

        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock_id, host_size);

        ImGuiID left = 0, center = 0;
        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Left, 0.24f, &left, &center);

        ImGuiID left_top = 0, left_bottom = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.28f, &left_top, &left_bottom);

        ImGuiID center_top = 0, center_bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.42f,
                                    &center_top, &center_bottom);

        ImGui::DockBuilderDockWindow("###RaftPanel",      left_top);
        ImGui::DockBuilderDockWindow("###NavigatorPanel", left_bottom);
        ImGui::DockBuilderDockWindow("###SqlPanel",       center_top);
        ImGui::DockBuilderDockWindow("###ResultPanel",    center_bottom);
        ImGui::DockBuilderDockWindow("###QueriesPanel",   center_bottom);
        ImGui::DockBuilderFinish(dock_id);
    }

    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void MainShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu(TR("File"))) {
        if (ImGui::MenuItem(TR("New connection..."), "Ctrl+Shift+N")) {
            connection_dialog_.open_new();
        }
        if (ImGui::MenuItem(TR("Edit connection..."), nullptr, false,
                            session_.state() == SessionState::connected)) {
            connection_dialog_.open_edit(active_profile_);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("New SQL tab"), "Ctrl+T")) new_document();
        if (ImGui::MenuItem(TR("Close tab"), "Ctrl+W",
                            false, documents_.size() > 1)) {
            close_document(active_document_);
        }
        if (ImGui::MenuItem(TR("Disconnect"), nullptr, false,
                            session_.state() == SessionState::connected)) {
            session_.disconnect();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Exit"), "Alt+F4")) wants_quit_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Edit"))) {
        SqlDocument* document = active_document();
        const bool has_document = document != nullptr;

        if (ImGui::MenuItem(TR("Undo"), "Ctrl+Z", false,
                            has_document && document->editor().CanUndo())) {
            document->editor().Undo();
        }
        if (ImGui::MenuItem(TR("Redo"), "Ctrl+Y", false,
                            has_document && document->editor().CanRedo())) {
            document->editor().Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Select all"), "Ctrl+A", false, has_document)) {
            document->editor().SelectAll();
        }
        if (ImGui::MenuItem(TR("Find"), "Ctrl+F", false, has_document)) {
            document->editor().OpenFindReplaceWindow();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("SQL")) {
        const bool can_run = session_.state() == SessionState::connected &&
                             !session_.busy();
        if (ImGui::MenuItem(TR("Execute"), "Ctrl+Enter", false, can_run)) {
            execute_current_sql();
        }
        ImGui::Separator();

        const bool auto_commit = session_.auto_commit();
        const bool in_txn = session_.txn_state() != db::TxnState::idle;

        bool toggle = auto_commit;
        if (ImGui::MenuItem(TR("Auto-commit"), nullptr, &toggle, can_run)) {
            session_.set_auto_commit_async(toggle);
        }
        if (ImGui::MenuItem(TR("Commit"), "Ctrl+Shift+C", false,
                            can_run && !auto_commit && in_txn)) {
            session_.commit_async();
        }
        if (ImGui::MenuItem(TR("Rollback"), "Ctrl+Shift+R", false,
                            can_run && !auto_commit && in_txn)) {
            session_.rollback_async();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Help"))) {
        // Seletor de tema: troca em tempo real, sem reiniciar.
        if (ImGui::BeginMenu(TR("Theme"))) {
            const std::string active_theme = current_theme().id;
            for (const Theme& theme : available_themes()) {
                const bool selected = active_theme == theme.id;
                if (ImGui::MenuItem(TR(theme.name.c_str()), nullptr, selected)) {
                    set_theme(theme.id);
                    // Os editores já criados guardam a paleta antiga.
                    for (auto& document : documents_) {
                        apply_editor_palette(document->editor());
                    }
                }
            }
            ImGui::EndMenu();
        }

        // Seletor de idioma: troca em tempo real, sem reiniciar.
        if (ImGui::BeginMenu(TR("Language"))) {
            const std::string_view active = i18n::current_language();
            for (const i18n::Language& language : i18n::available_languages()) {
                const bool selected = active == language.code;
                if (ImGui::MenuItem(language.native_name.c_str(), nullptr,
                                    selected)) {
                    i18n::set_language(language.code);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem(TR("Icon gallery"), nullptr, &show_icons_);
        ImGui::MenuItem(TR("ImGui demo"), nullptr, &show_demo_);
        ImGui::Separator();
        if (ImGui::MenuItem(TR("About C-Otter"))) show_about_ = true;
        ImGui::EndMenu();
    }

    const ImGuiIO& io = ImGui::GetIO();
    char fps[48];
    std::snprintf(fps, sizeof(fps), "%.1f fps  |  %.2f ms",
                  static_cast<double>(io.Framerate),
                  static_cast<double>(1000.0f / io.Framerate));
    const float width = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextColored(col4(colors().text_dim), "%s", fps);

    ImGui::EndMainMenuBar();
}

void MainShell::draw_raft_panel() {
    if (ImGui::Begin(TRW("Raft", "###RaftPanel"))) {
        if (ImGui::Button(TR("New connection"))) connection_dialog_.open_new();

        const SessionState state = session_.state();
        const bool connected = state == SessionState::connected;

        ImGui::SameLine();
        ImGui::BeginDisabled(!connected);
        if (ImGui::Button(TR("Edit"))) connection_dialog_.open_edit(active_profile_);
        ImGui::EndDisabled();

        ImGui::Separator();

        if (state == SessionState::disconnected) {
            ImGui::TextColored(col4(colors().text_dim), TR("no connection"));
            ImGui::End();
            return;
        }

        const std::uint32_t status_color =
            connected                        ? colors().ok
            : state == SessionState::failed  ? colors().error
                                             : colors().warn;

        ImGui::TextColored(col4(status_color), "●");
        ImGui::SameLine();
        ImGui::TextUnformatted(active_profile_.effective_name().c_str());

        // Menu de contexto sobre a conexão, como no DBeaver.
        if (ImGui::BeginPopupContextItem("##connmenu")) {
            if (ImGui::MenuItem(TR("Edit connection..."))) {
                connection_dialog_.open_edit(active_profile_);
            }
            if (ImGui::MenuItem(TR("Disconnect"), nullptr, false, connected)) {
                session_.disconnect();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Copy name"))) {
                ImGui::SetClipboardText(active_profile_.effective_name().c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::Indent();

        // Faixa do tipo de conexão: produção precisa ser reconhecível de longe.
        const db::ConnectionTypeInfo& type =
            db::connection_type_info(active_profile_.type);
        ImGui::TextColored(col4(type.color), "%s", type.name);

        if (connected) {
            ImGui::TextColored(col4(colors().text_dim), "PostgreSQL %s",
                               session_.server_version().c_str());
            ImGui::TextColored(col4(colors().text_dim), "%s:%u",
                               active_profile_.host.c_str(),
                               active_profile_.port);
            ImGui::TextColored(col4(colors().text_dim), "%s",
                               active_profile_.auto_commit ? TR("auto-commit")
                                                           : TR("manual transaction"));
            if (active_profile_.read_only) {
                ImGui::TextColored(col4(colors().warn), TR("read only"));
            }
        }

        if (!active_profile_.description.empty()) {
            ImGui::TextColored(col4(colors().text_dim), "%s",
                               active_profile_.description.c_str());
        }

        ImGui::Unindent();
    }
    ImGui::End();
}

void MainShell::draw_navigator_panel() {
    if (ImGui::Begin(TRW("Navigator", "###NavigatorPanel"))) {
        if (session_.state() != SessionState::connected) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("connect to browse the schema"));
            ImGui::End();
            return;
        }

        const std::vector<db::SchemaMeta> schemas = session_.schemas();

        for (const db::SchemaMeta& schema : schemas) {
            ImGui::PushID(schema.name.c_str());

            icon_inline(Icon::schema, colors().accent_light);
            ImGui::SameLine(0.0f, 4.0f);

            const bool schema_open =
                ImGui::TreeNodeEx(schema.name.c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen);

            if (schema_open) {
                // Ordem do DBeaver: tabelas, views, materialized views,
                // sequences, rotinas.
                draw_relations_folder(schema, db::ObjKind::table,
                                      Icon::table, TR("Tables"));
                draw_relations_folder(schema, db::ObjKind::view,
                                      Icon::view, TR("Views"));
                draw_relations_folder(schema, db::ObjKind::materialized_view,
                                      Icon::materialized_view,
                                      TR("Materialized views"));
                draw_sequences_folder(schema);
                draw_routines_folder(schema);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

// Pasta com contagem e um ícone. O número evita expandir só para descobrir que
// está vazio -- é o padrão do DBeaver (docs/NAVIGATOR-TREE.md).
bool MainShell::draw_folder_node(Icon icon, const char* label, std::size_t count,
                                 bool loaded) {
    icon_inline(icon, colors().accent_light);
    ImGui::SameLine(0.0f, 4.0f);

    // OTTER_EXPAND_TREE abre todas as pastas na captura de tela. Conferir os
    // icones de constraint, indice, FK e trigger exige chegar ate' o quarto
    // nivel da arvore, e clicar la' por automacao erra o alvo.
    static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
    if (expand_all) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);

    if (loaded) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "(%zu)", count);
    }
    return open;
}

void MainShell::draw_relations_folder(const db::SchemaMeta& schema,
                                      db::ObjKind kind, Icon icon,
                                      const char* label) {
    // Uma unica consulta traz tabelas, views e materialized views (pg_class
    // com relkind r/v/m/p); as pastas apenas filtram o resultado. Consultar
    // tres vezes o mesmo pg_class seria desperdicio.
    std::size_t count = 0;
    for (const db::TableMeta& relation : schema.tables) {
        if (relation.kind == kind) ++count;
    }

    // Pasta vazia fica escondida, como no DBeaver: um schema sem views nao
    // precisa de um no "Views (0)" ocupando espaco.
    if (count == 0 && schema.tables_loaded) return;

    if (!draw_folder_node(icon, label, count, schema.tables_loaded)) return;

    const Palette& p = colors();
    const std::uint32_t tint = kind == db::ObjKind::table ? p.accent : p.data;

    bool first = true;
    for (const db::TableMeta& relation : schema.tables) {
        if (relation.kind != kind) continue;

        ImGui::PushID(relation.name.c_str());

        icon_inline(icon, tint);
        ImGui::SameLine(0.0f, 4.0f);

        ImGui::PushStyleColor(ImGuiCol_Text,
                              col(kind == db::ObjKind::table ? p.text : p.data));
        // So' a primeira de cada pasta: abrir as 32 encheria a arvore de ruido
        // e dispararia 32 consultas de catalogo de uma vez.
        static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
        if (expand_all && first) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        first = false;

        const bool open = ImGui::TreeNode(relation.name.c_str());
        ImGui::PopStyleColor();

        if (!relation.size_pretty.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "  %s",
                               relation.size_pretty.c_str());
        }

        if (ImGui::IsItemHovered() && !relation.comment.empty()) {
            ImGui::SetTooltip("%s", relation.comment.c_str());
        }

        if (open) {
            draw_table_children(schema, relation);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void MainShell::draw_table_children(const db::SchemaMeta& schema,
                                    const db::TableMeta& table) {
    const Palette& p = colors();

    // --- Colunas -------------------------------------------------------------
    if (draw_folder_node(Icon::column, TR("Columns"), table.columns.size(),
                         table.columns_loaded)) {
        if (!table.columns_loaded && !session_.busy()) {
            session_.load_columns_async(schema.name, table.name);
        }
        if (table.columns.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
        }

        for (const db::ColumnMeta& column : table.columns) {
            icon_inline(column.primary_key ? Icon::key : Icon::column,
                        column.primary_key ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, 4.0f);

            ImGui::TextColored(col4(column.primary_key ? p.data_light : p.text),
                               "%s", column.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s%s%s",
                               column.type_name.c_str(),
                               column.primary_key ? "  PK" : "",
                               column.nullable ? "" : "  NOT NULL");

            if (ImGui::IsItemHovered()) {
                std::string tip = column.type_name;
                if (!column.default_value.empty()) {
                    tip += "\nDEFAULT " + column.default_value;
                }
                if (!column.comment.empty()) tip += "\n\n" + column.comment;
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Constraints ---------------------------------------------------------
    //
    // Uma view nao tem constraints nem chaves estrangeiras. O DBeaver nem
    // mostra as pastas nesse caso, e mostrar "(0)" sugeriria que a view
    // poderia ter uma.
    if (table.has_constraints() &&
        draw_folder_node(Icon::constraint, TR("Constraints"),
                         table.constraints.size(), table.constraints_loaded)) {
        if (!table.constraints_loaded && !session_.busy()) {
            session_.load_constraints_async(schema.name, table.name);
        }
        for (const db::ConstraintMeta& constraint : table.constraints) {
            const bool is_pk = constraint.kind == db::ObjKind::primary_key;
            icon_inline(is_pk ? Icon::key : Icon::constraint,
                        is_pk ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, 4.0f);

            ImGui::TextColored(col4(is_pk ? p.data_light : p.text), "%s",
                               constraint.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s",
                               std::string(db::to_string(constraint.kind)).c_str());

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", constraint.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Índices -------------------------------------------------------------
    //
    // A view comum nao tem indices, mas a materializada tem -- e' justamente
    // o que permite indexa-la como uma tabela.
    if (table.has_indexes() &&
        draw_folder_node(Icon::index, TR("Indexes"), table.indexes.size(),
                         table.indexes_loaded)) {
        if (!table.indexes_loaded && !session_.busy()) {
            session_.load_indexes_async(schema.name, table.name);
        }
        for (const db::IndexMeta& index : table.indexes) {
            // Índice inválido (CREATE INDEX CONCURRENTLY que falhou) existe mas
            // não é usado pelo planejador -- precisa ser visível.
            const std::uint32_t color = !index.valid ? p.error
                                        : index.primary ? p.data_light
                                                        : p.text;

            icon_inline(Icon::index, color);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(color), "%s", index.name.c_str());

            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s  %s%s%s",
                               index.method.c_str(),
                               index.size_pretty.c_str(),
                               index.unique ? "  UNIQUE" : "",
                               index.valid ? "" : "  INVALID");

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", index.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Chaves estrangeiras -------------------------------------------------
    if (table.has_constraints() &&
        draw_folder_node(Icon::foreign_key, TR("Foreign keys"),
                         table.foreign_keys.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session_.busy()) {
            session_.load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& key : table.foreign_keys) {
            icon_inline(Icon::foreign_key, p.accent_light);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.text), "%s", key.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "→ %s.%s", key.target_table.c_str(),
                               key.target_column.c_str());

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n\nON UPDATE %s\nON DELETE %s",
                                  key.definition.c_str(),
                                  key.on_update.c_str(), key.on_delete.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Referências ---------------------------------------------------------
    //
    // Quem aponta para esta tabela. Responder "o que depende disto?" é o que
    // mais falta num cliente SQL.
    if (table.has_constraints() &&
        draw_folder_node(Icon::references, TR("References"),
                         table.references.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session_.busy()) {
            session_.load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& reference : table.references) {
            icon_inline(Icon::references, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn), "%s.%s",
                               reference.source_table.c_str(),
                               reference.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "→ %s",
                               reference.target_column.c_str());
        }
        ImGui::TreePop();
    }

    // --- Triggers ------------------------------------------------------------
    //
    // A materialized view nao aceita trigger: ela e' atualizada por REFRESH,
    // nao por DML. A view comum aceita INSTEAD OF.
    if (table.has_triggers() &&
        draw_folder_node(Icon::trigger, TR("Triggers"), table.triggers.size(),
                         table.triggers_loaded)) {
        if (!table.triggers_loaded && !session_.busy()) {
            session_.load_triggers_async(schema.name, table.name);
        }
        for (const db::TriggerMeta& trigger : table.triggers) {
            icon_inline(Icon::trigger, trigger.enabled ? p.text_dim : p.error);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(trigger.enabled ? p.text : p.text_dim),
                               "%s", trigger.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s %s%s",
                               trigger.timing.c_str(), trigger.events.c_str(),
                               trigger.enabled ? "" : "  [off]");

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trigger.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Corpo da view -------------------------------------------------------
    if (table.is_view()) draw_view_definition(schema, table);
}

void MainShell::draw_view_definition(const db::SchemaMeta& schema,
                                     const db::TableMeta& view) {
    const Palette& p = colors();

    // `false` no lugar de `loaded`: o corpo nao tem contagem para mostrar, e
    // "(1)" ao lado de "Definição" nao diria nada.
    if (!draw_folder_node(Icon::view, TR("Definition"), 0, false)) return;

    if (!view.definition_loaded && !session_.busy()) {
        session_.load_view_definition_async(schema.name, view.name);
    }

    if (view.definition.empty()) {
        ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
        ImGui::TreePop();
        return;
    }

    // Caixa rolavel com o SQL. Altura limitada: uma view de relatorio tem
    // dezenas de linhas e empurraria o resto da arvore para fora da tela.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(p.bg_darkest));
    if (ImGui::BeginChild("##viewdef",
                          ImVec2(0.0f, ImGui::GetFontSize() * 9.0f),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(p.syntax_string));
        ImGui::TextUnformatted(view.definition.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (icon_text_button("##copydef", Icon::copy, TR("Copy"),
                         TR("Copy the definition to the clipboard"))) {
        ImGui::SetClipboardText(view.definition.c_str());
    }
    ImGui::SameLine();
    if (icon_text_button("##opendef", Icon::open, TR("Open in editor"),
                         TR("Open the definition in a new SQL tab"))) {
        // Abre como script: a view vira ponto de partida para uma consulta,
        // que e' o uso mais comum de olhar a definicao.
        new_document().editor().SetText(view.definition);
    }

    ImGui::TreePop();
}

void MainShell::draw_sequences_folder(const db::SchemaMeta& schema) {
    if (!draw_folder_node(Icon::sequence, TR("Sequences"), schema.sequences.size(),
                          schema.sequences_loaded)) {
        return;
    }

    if (!schema.sequences_loaded && !session_.busy()) {
        session_.load_sequences_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::SequenceMeta& sequence : schema.sequences) {
        icon_inline(Icon::sequence, p.text_dim);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.text), "%s", sequence.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "= %lld",
                           static_cast<long long>(sequence.last_value));

        if (ImGui::IsItemHovered()) {
            std::string tip = "start " + std::to_string(sequence.start_value) +
                              ", increment " + std::to_string(sequence.increment);
            if (!sequence.owned_by.empty()) tip += "\nowned by " + sequence.owned_by;
            if (!sequence.comment.empty())  tip += "\n\n" + sequence.comment;
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    ImGui::TreePop();
}

void MainShell::draw_routines_folder(const db::SchemaMeta& schema) {
    if (!draw_folder_node(Icon::function, TR("Functions"), schema.routines.size(),
                          schema.routines_loaded)) {
        return;
    }

    if (!schema.routines_loaded && !session_.busy()) {
        session_.load_routines_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::RoutineMeta& routine : schema.routines) {
        const bool is_procedure = routine.kind == db::ObjKind::procedure;

        icon_inline(is_procedure ? Icon::procedure : Icon::function,
                    is_procedure ? p.data : p.text_dim);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.text), "%s", routine.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "(%s)", routine.arguments.c_str());

        if (ImGui::IsItemHovered()) {
            std::string tip = routine.name + "(" + routine.arguments + ")";
            if (!is_procedure) tip += "\n  returns " + routine.return_type;
            tip += "\n  language " + routine.language;
            if (!routine.comment.empty()) tip += "\n\n" + routine.comment;
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    ImGui::TreePop();
}

void MainShell::draw_toolbar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kToolbarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(colors().bg_darkest));

    if (ImGui::Begin("##toolbar", nullptr, flags)) {
        const Palette& p = colors();

        const bool connected = session_.state() == SessionState::connected;
        const bool busy      = session_.busy();
        const bool can_act   = connected && !busy;

        // Separador vertical fino entre grupos de ações.
        auto group_separator = [&p] {
            ImGui::SameLine(0.0f, 6.0f);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float h = toolbar_button_size();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(pos.x, pos.y + h * 0.22f),
                ImVec2(pos.x, pos.y + h * 0.78f),
                with_alpha(p.bg_light, 0.65f), 1.0f);
            ImGui::SameLine(0.0f, 7.0f);
        };

        // --- Conexão ---------------------------------------------------------
        if (icon_button("##connect", Icon::connect,
                        TR("New connection (Ctrl+Shift+N)"))) {
            connection_dialog_.open_new();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##disconnect", Icon::disconnect, TR("Disconnect"),
                        connected)) {
            session_.disconnect();
        }

        group_separator();

        // --- Execução --------------------------------------------------------
        if (icon_button("##execute", Icon::play, TR("Execute (Ctrl+Enter)"),
                        can_act, can_act ? p.ok : 0)) {
            execute_current_sql();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##cancel", Icon::stop, TR("Cancel query"), busy,
                        busy ? p.error : 0)) {
            // Cancelamento entra quando a Session expuser cancel_async().
        }

        group_separator();

        // --- Transações ------------------------------------------------------
        //
        // A razão de a barra e as transações virem juntas: commit e rollback
        // precisam de um lugar visível e permanente.
        const bool auto_commit    = session_.auto_commit();
        const db::TxnState txn    = session_.txn_state();
        const std::size_t pending = session_.uncommitted_changes();
        const bool in_txn         = txn != db::TxnState::idle;

        // Auto-commit como botão de alternância, tingido quando ligado.
        if (icon_button("##autocommit", Icon::refresh,
                        auto_commit ? TR("Auto-commit: on") : TR("Auto-commit: off"),
                        can_act, auto_commit ? p.data_light : p.text_dim)) {
            session_.set_auto_commit_async(!auto_commit);
        }

        ImGui::SameLine(0.0f, 2.0f);
        const bool can_txn = can_act && !auto_commit && in_txn;

        if (icon_button("##commit", Icon::commit, TR("Commit (Ctrl+Shift+C)"),
                        can_txn, pending > 0 ? p.ok : 0)) {
            session_.commit_async();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##rollback", Icon::rollback, TR("Rollback (Ctrl+Shift+R)"),
                        can_txn, pending > 0 ? p.error : 0)) {
            session_.rollback_async();
        }

        // --- Indicador de estado da transação --------------------------------
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::AlignTextToFramePadding();

        if (!connected) {
            ImGui::TextColored(col4(p.text_dim), "—");
        } else if (auto_commit) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("auto-commit"));
        } else {
            // Cores do monitor de transação do DBeaver: verde parado, âmbar
            // com trabalho pendente, vermelho abortada.
            const std::uint32_t color =
                txn == db::TxnState::failed ? p.error
                : pending > 0               ? p.warn
                                            : p.ok;

            const char* label =
                txn == db::TxnState::failed ? TR("transaction aborted")
                : pending > 0               ? TR("uncommitted changes")
                                            : TR("transaction open");

            // Ponto com glow: o estado da transação merece destaque.
            const ImVec2 dot = ImGui::GetCursorScreenPos();
            const ImVec2 dot_center(dot.x + 5.0f,
                                    dot.y + ImGui::GetFontSize() * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (p.glow_strength > 0.0f) {
                for (int i = 3; i > 0; --i) {
                    const float t = static_cast<float>(i) / 3.0f;
                    dl->AddCircleFilled(
                        dot_center, 4.0f + t * 5.0f,
                        with_alpha(color, p.glow_strength * 0.16f * (1.0f - t)),
                        16);
                }
            }
            dl->AddCircleFilled(dot_center, 4.0f, color, 16);

            ImGui::Dummy(ImVec2(14.0f, ImGui::GetFontSize()));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(col4(color), "%s", label);

            if (pending > 0) {
                ImGui::SameLine(0.0f, 5.0f);
                ImGui::TextColored(col4(p.text_dim), "(%zu)", pending);
            }
        }

        // --- Contexto, alinhado à direita ------------------------------------
        if (connected) {
            char context[160];
            std::snprintf(context, sizeof(context), "%s  ·  %s",
                          active_profile_.effective_name().c_str(),
                          db::connection_type_info(active_profile_.type).name);

            const float width = ImGui::CalcTextSize(context).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(
                col4(db::connection_type_info(active_profile_.type).color),
                "%s", context);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_document_tabs() {
    constexpr ImGuiTabBarFlags flags =
        ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
        ImGuiTabBarFlags_FittingPolicyScroll |
        ImGuiTabBarFlags_TabListPopupButton;

    if (!ImGui::BeginTabBar("##doctabs", flags)) return;

    // Botão "+" ao lado das abas, como em navegadores.
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                      ImGuiTabItemFlags_NoTooltip)) {
        new_document();
    }

    std::optional<std::size_t> to_close;
    std::optional<std::size_t> to_close_others;

    for (std::size_t i = 0; i < documents_.size(); ++i) {
        SqlDocument& document = *documents_[i];
        ImGui::PushID(static_cast<int>(document.id()));

        // O sufixo ###id mantém a identidade da aba mesmo quando o título
        // muda (ao salvar com outro nome, por exemplo).
        const std::string label =
            document.title() + (document.modified() ? " *" : "") +
            "###doc" + std::to_string(document.id());

        ImGuiTabItemFlags item_flags = ImGuiTabItemFlags_None;
        if (document.pinned()) item_flags |= ImGuiTabItemFlags_Leading;
        if (document.modified()) item_flags |= ImGuiTabItemFlags_UnsavedDocument;

        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, item_flags)) {
            active_document_ = i;
            draw_document_body(document);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginPopupContextItem("##tabmenu")) {
            if (ImGui::MenuItem(TR("Close"), "Ctrl+W")) to_close = i;
            if (ImGui::MenuItem(TR("Close others"))) to_close_others = i;
            ImGui::Separator();

            bool pinned = document.pinned();
            if (ImGui::MenuItem(TR("Pin tab"), nullptr, &pinned)) {
                document.set_pinned(pinned);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Copy SQL"))) {
                ImGui::SetClipboardText(document.editor().GetText().c_str());
            }
            ImGui::EndPopup();
        }

        if (!open) to_close = i;
        ImGui::PopID();
    }

    ImGui::EndTabBar();

    // Aplicado fora do laço: remover do vetor durante a iteração invalidaria
    // as referências em uso.
    if (to_close_others) close_others(*to_close_others);
    if (to_close)        close_document(*to_close);
}

void MainShell::draw_document_body(SqlDocument& document) {
    const bool can_run = session_.state() == SessionState::connected &&
                         !session_.busy();

    ImGui::BeginDisabled(!can_run);
    if (ImGui::Button(TR("Execute  (Ctrl+Enter)"))) execute_current_sql();
    ImGui::EndDisabled();

    ImGui::SameLine();
    const TextEditor::DocPos cursor =
        document.editor().GetCurrentCursorPosition();

    ImGui::TextColored(col4(colors().text_dim),
                       TR("  Ln %zu, Col %zu  |  %zu lines%s"),
                       cursor.line + 1, cursor.index + 1,
                       document.editor().GetLineCount(),
                       document.modified() ? "  ●" : "");

    if (document.executing()) {
        ImGui::SameLine();
        draw_busy_indicator();
    }

    ImGui::Separator();
    document.editor().Render("##sql", ImGui::GetContentRegionAvail());
}

void MainShell::draw_editor_panel() {
    if (ImGui::Begin(TRW("SQL", "###SqlPanel"))) {
        draw_document_tabs();
    }
    ImGui::End();
}

void MainShell::draw_grid_panel() {
    if (ImGui::Begin(TRW("Result", "###ResultPanel"))) {
        // O resultado pertence ao documento: trocar de aba troca a grade.
        SqlDocument* document = active_document();

        if (document == nullptr || !document->result().has_value()) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("run a query to see the result"));
            // Um erro da última execução aparece mesmo sem resultado.
            if (document != nullptr && !document->status().empty()) {
                ImGui::TextColored(col4(colors().error), "%s",
                                   document->status().c_str());
            }
            ImGui::End();
            return;
        }

        const db::ResultSet& rs = *document->result();

        ImGui::TextColored(col4(colors().text_dim),
                           TR("%zu row(s) x %zu column(s)  |  %zu bytes"),
                           rs.row_count(), rs.column_count(), rs.bytes_used());
        ImGui::Separator();

        if (rs.column_count() == 0) {
            ImGui::TextColored(col4(colors().ok), TR("command executed"));
            if (rs.affected_rows() >= 0) {
                ImGui::SameLine();
                ImGui::TextColored(col4(colors().text_dim),
                                   TR(" (%lld row(s) affected)"),
                                   static_cast<long long>(rs.affected_rows()));
            }
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_SizingFixedFit;

        const auto columns = static_cast<int>(
            std::min(rs.column_count(), std::size_t{64}));   // limite do ImGui

        if (ImGui::BeginTable("##results", columns, flags)) {
            ImGui::TableSetupScrollFreeze(1, 1);   // cabecalho e 1a coluna fixos

            for (int c = 0; c < columns; ++c) {
                ImGui::TableSetupColumn(
                    rs.column(static_cast<std::size_t>(c)).info().name.c_str());
            }
            ImGui::TableHeadersRow();

            // Virtualizacao: so' as linhas visiveis sao desenhadas. E' o que
            // torna 1M linhas viavel (ADR 0005).
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rs.row_count()));

            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto r = static_cast<std::size_t>(row);
                    ImGui::TableNextRow();

                    for (int c = 0; c < columns; ++c) {
                        const auto ci = static_cast<std::size_t>(c);
                        ImGui::TableSetColumnIndex(c);

                        if (rs.is_null(r, ci)) {
                            // Nulo visualmente distinto de string vazia.
                            ImGui::TextColored(col4(colors().text_dim), "[null]");
                            continue;
                        }

                        const std::string_view value = rs.text(r, ci);
                        const db::DataKind kind = rs.column(ci).info().kind;

                        if (db::is_right_aligned(kind)) {
                            const float width = ImGui::CalcTextSize(
                                value.data(), value.data() + value.size()).x;
                            const float available = ImGui::GetContentRegionAvail().x;
                            if (available > width) {
                                ImGui::SetCursorPosX(
                                    ImGui::GetCursorPosX() + available - width);
                            }
                        }
                        ImGui::TextUnformatted(value.data(),
                                               value.data() + value.size());
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void MainShell::draw_query_log_panel() {
    if (ImGui::Begin(TRW("Queries", "###QueriesPanel"))) {
        const std::vector<db::QueryLog> log = session_.query_log();

        if (log.empty()) {
            ImGui::TextColored(col4(colors().text_dim), TR("no queries yet"));
            ImGui::End();
            return;
        }

        ImGui::TextColored(col4(colors().text_dim),
                           "%zu query(s)  |  inclusive as internas de catálogo",
                           log.size());
        ImGui::Separator();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##querylog", 4, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR("time"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(TR("rows"), ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn(TR("state"), ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("SQL");
            ImGui::TableHeadersRow();

            // Mais recentes primeiro: e' o que se quer ver ao diagnosticar.
            for (std::size_t i = log.size(); i > 0; --i) {
                const db::QueryLog& entry = log[i - 1];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.2f ms",
                            static_cast<double>(entry.duration.count()) / 1000.0);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", entry.rows);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(col4(entry.failed ? colors().error : colors().ok),
                                   entry.failed ? TR("error") : "ok");

                ImGui::TableSetColumnIndex(3);
                // Uma linha so': quebras de linha do SQL viram espaco.
                std::string single_line = entry.sql;
                std::replace(single_line.begin(), single_line.end(), '\n', ' ');
                ImGui::TextUnformatted(single_line.c_str());

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.sql.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}


void MainShell::draw_status_bar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kStatusBarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(colors().bg_darkest));

    if (ImGui::Begin("##status", nullptr, flags)) {
        const SessionState state = session_.state();

        const std::uint32_t color =
            state == SessionState::connected ? colors().ok
            : state == SessionState::failed  ? colors().error
            : state == SessionState::connecting ? colors().warn
                                                : colors().text_dim;
        ImGui::TextColored(col4(color), "●");
        ImGui::SameLine();

        if (state == SessionState::connected) {
            ImGui::TextColored(col4(colors().data), "%s",
                               session_.database_name().c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(colors().text_dim), "| PostgreSQL %s |",
                               session_.server_version().c_str());
            ImGui::SameLine();
        }
        ImGui::TextColored(col4(colors().text_dim), "%s",
                           session_.status_message().c_str());
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin(TR("About C-Otter"), &show_about_,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(colors().accent_light));
        ImGui::TextUnformatted("C-Otter 0.1.0");
        ImGui::PopStyleColor();

        ImGui::TextWrapped(
            "Say it out loud: sea otter. A playful nod to DBeaver, from a lighter, "
            "faster cousin that shares the same river -- and it's written in C.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Sea otters hold hands while they sleep so they never drift apart, keep a "
            "favorite rock in a pocket, and float together in a raft. Basically, they "
            "were born for databases.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, col(colors().data));
        ImGui::TextUnformatted("Here, every JOIN is an OTTER JOIN.");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::TextColored(col4(colors().text_dim),
                           "Dear ImGui %s  |  protocolo PostgreSQL v3 nativo",
                           IMGUI_VERSION);
    }
    ImGui::End();
}

void MainShell::draw_icon_gallery() {
    // Nomes em ingles literal, sem TR(): sao identificadores do enum Icon, nao
    // texto de interface. Traduzi-los tornaria a galeria inutil para conferir
    // qual desenho corresponde a qual constante do codigo.
    struct Entry { Icon icon; const char* name; };
    static const Entry kEntries[] = {
        {Icon::connect, "connect"},       {Icon::disconnect, "disconnect"},
        {Icon::play, "play"},             {Icon::stop, "stop"},
        {Icon::commit, "commit"},         {Icon::rollback, "rollback"},
        {Icon::database, "database"},     {Icon::schema, "schema"},
        {Icon::table, "table"},           {Icon::view, "view"},
        {Icon::materialized_view, "materialized_view"},
        {Icon::column, "column"},         {Icon::key, "key"},
        {Icon::constraint, "constraint"}, {Icon::index, "index"},
        {Icon::foreign_key, "foreign_key"},
        {Icon::references, "references"}, {Icon::sequence, "sequence"},
        {Icon::function, "function"},     {Icon::procedure, "procedure"},
        {Icon::trigger, "trigger"},       {Icon::data_type, "data_type"},
        {Icon::extension, "extension"},   {Icon::role, "role"},
        {Icon::tablespace, "tablespace"}, {Icon::folder, "folder"},
        {Icon::refresh, "refresh"},       {Icon::search, "search"},
        {Icon::settings, "settings"},     {Icon::plus, "plus"},
        {Icon::close, "close"},           {Icon::pin, "pin"},
        {Icon::save, "save"},             {Icon::open, "open"},
        {Icon::copy, "copy"},             {Icon::chevron_right, "chevron_right"},
        {Icon::chevron_down, "chevron_down"},
        {Icon::warning, "warning"},       {Icon::error, "error"},
        {Icon::info, "info"},             {Icon::clock, "clock"},
        {Icon::filter, "filter"},
    };

    ImGui::SetNextWindowSize(ImVec2(1180, 900), ImGuiCond_Appearing);
    if (ImGui::Begin(TRW("Icon gallery", "###IconGallery"), &show_icons_,
                     ImGuiWindowFlags_NoDocking)) {
        const Palette& p = colors();

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(p.text_dim), TR(
            "Every object type needs its own drawing. Two icons that look alike "
            "at tree size are a defect."));
        ImGui::PopTextWrapPos();
        ImGui::Separator();

        // Tres tamanhos: o da arvore (o mais critico), o da barra e um grande
        // para inspecionar o traco.
        static float scale = 1.0f;
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat(TR("Scale"), &scale, 0.6f, 4.0f, "%.1fx");
        ImGui::Separator();

        const float cell = 132.0f;
        const int columns = (std::max)(
            1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));

        if (ImGui::BeginTable("##icons", columns)) {
            for (const Entry& entry : kEntries) {
                ImGui::TableNextColumn();

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float box = ImGui::GetFontSize() * 2.2f * scale;
                ImGui::Dummy(ImVec2(box, box));

                draw_icon(entry.icon,
                          ImVec2(origin.x + box * 0.5f, origin.y + box * 0.5f),
                          box * 0.8f, p.accent_light, 1.6f);

                ImGui::TextColored(col4(p.text), "%s", entry.name);

                // Ao lado, o mesmo desenho no tamanho real da arvore: e' ai'
                // que a confusao entre dois icones aparece.
                icon_inline(entry.icon, p.text_dim);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(col4(p.text_dim), TR("tree size"));
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace otter::ui


