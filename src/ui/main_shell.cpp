#include "ui/main_shell.hpp"
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

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

std::string to_lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
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
    TextEditor::Palette p = editor.GetPalette();

    auto set = [&p](Color c, std::uint32_t value) {
        p[static_cast<std::size_t>(c)] = static_cast<ImU32>(value);
    };

    set(Color::background,      palette::bg_darkest);
    set(Color::text,            palette::text);
    set(Color::keyword,         palette::fur_light);   // SELECT, FROM, JOIN
    set(Color::declaration,     palette::data_light);
    set(Color::number,          0xFFB0A450);
    set(Color::string,          0xFF7CC47C);
    set(Color::punctuation,     palette::text_dim);
    set(Color::preprocessor,    palette::warn);
    set(Color::identifier,      palette::text);
    set(Color::knownIdentifier, palette::data);        // tabelas e colunas
    set(Color::comment,         palette::text_dim);
    set(Color::cursor,          palette::data_light);
    set(Color::selection,       (palette::data & 0x00FFFFFFu) | 0x50000000u);
    set(Color::whitespace,      0xFF3D332C);
    set(Color::lineNumber,      palette::text_dim);
    set(Color::currentLineNumber, palette::fur_light);
    set(Color::currentLineHighlight,
        (palette::fur & 0x00FFFFFFu) | 0x18000000u);
    set(Color::currentLineHighlightBorder,
        (palette::fur & 0x00FFFFFFu) | 0x30000000u);
    set(Color::matchingBracketBackground,
        (palette::data & 0x00FFFFFFu) | 0x40000000u);
    set(Color::matchingBracketActive, palette::data_light);

    editor.SetPalette(p);
}

// Indicador de atividade: um ponto que pulsa enquanto o worker trabalha.
void draw_busy_indicator() {
    const float t = static_cast<float>(ImGui::GetTime());
    const float alpha = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
    ImVec4 color = col4(palette::data_light);
    color.w = alpha;
    ImGui::TextColored(color, "  ●");
}

} // namespace

MainShell::MainShell()
    : editor_(std::make_unique<TextEditor>()),
      autocomplete_config_(std::make_unique<TextEditor::AutoCompleteConfig>()) {
    editor_->SetLanguage(TextEditor::Language::Sql());
    editor_->SetText(std::string(kWelcomeSql));
    editor_->SetShowWhitespacesEnabled(false);
    editor_->SetShowMatchingBrackets(true);
    editor_->SetCompletePairedGlyphs(true);
    editor_->SetTabSize(4);

    apply_editor_palette(*editor_);

    // Completion (ADR 0004). O widget cuida de trigger, popup e insercao com
    // undo; QUAIS sugestoes e em QUE ordem e' inteiramente nosso.
    autocomplete_config_->triggerOnTyping    = true;
    autocomplete_config_->triggerInComments  = false;
    autocomplete_config_->triggerInStrings   = false;
    autocomplete_config_->suggestionWidth    = 52;
    autocomplete_config_->noSuggestionsLabel = "sem sugestões";
    autocomplete_config_->userData           = this;
    autocomplete_config_->callback = [](TextEditor::AutoCompleteState& state) {
        static_cast<MainShell*>(state.userData)->suggest(state);
    };

    editor_->SetAutoCompleteConfig(autocomplete_config_.get());

    // Pre-preenche a partir do ambiente, como psql e outras ferramentas fazem.
    // Evita redigitar a cada execucao durante o desenvolvimento.
    auto from_env = [](const char* name, char* target, std::size_t size) {
        if (const char* value = std::getenv(name)) {
            std::snprintf(target, size, "%s", value);
        }
    };
    from_env("PGHOST",     host_,     sizeof(host_));
    from_env("PGPORT",     port_,     sizeof(port_));
    from_env("PGDATABASE", database_, sizeof(database_));
    from_env("PGUSER",     user_,     sizeof(user_));
    from_env("PGPASSWORD", password_, sizeof(password_));
}

MainShell::~MainShell() = default;

// Gera sugestoes de completion (ADR 0004), agora sobre METADADOS REAIS.
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

    // Tabelas mencionadas na query atual: suas colunas valem mais.
    const std::string sql = to_lower(editor_->GetText());
    auto mentioned = [&sql](std::string_view table) {
        return sql.find(to_lower(std::string(table))) != std::string::npos;
    };

    // Camada 3 -- metadados reais do servidor.
    for (const db::SchemaMeta& schema : session_.schemas()) {
        for (const db::TableMeta& table : schema.tables) {
            const bool in_query = mentioned(table.name);

            for (const db::ColumnMeta& column : table.columns) {
                // Rank 0: coluna de tabela ja' citada na query. Rank 2: demais.
                int rank = in_query ? 0 : 2;
                if (column.primary_key) rank -= 1;   // chaves sobem
                add(column.name + pad_to(column.name, 30) + column.type_name +
                        (column.primary_key ? "  PK" : ""),
                    rank);
            }

            const char* label = table.kind == db::ObjKind::view ? "view" : "tabela";
            add(table.name + pad_to(table.name, 30) + label, in_query ? 1 : 3);
        }
    }

    // Camada 1 -- keywords do dialeto, por ultimo: sao as mais previsiveis.
    static const char* const kKeywords[] = {
        "SELECT", "FROM", "WHERE", "GROUP BY", "ORDER BY", "HAVING",
        "INNER JOIN", "LEFT JOIN", "RIGHT JOIN", "FULL JOIN", "ON",
        "INSERT INTO", "UPDATE", "DELETE FROM", "VALUES", "SET",
        "COUNT", "SUM", "AVG", "MIN", "MAX", "DISTINCT", "AS",
        "LIMIT", "OFFSET", "CASE", "WHEN", "THEN", "ELSE", "END",
        "CREATE TABLE", "ALTER TABLE", "DROP TABLE", "BEGIN", "COMMIT", "ROLLBACK",
    };
    for (const char* keyword : kKeywords) add(keyword, 5);

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

    // Se ha' selecao, executa so' ela -- comportamento esperado de cliente SQL.
    std::string sql = editor_->CurrentCursorHasSelection()
                          ? editor_->GetSectionText(
                                editor_->GetCurrentCursorSelection())
                          : editor_->GetText();
    if (sql.empty()) return;

    session_.execute_async(std::move(sql));
}

void MainShell::draw() {
    // Colhe o resultado assim que o worker termina.
    if (!session_.busy()) {
        if (auto fresh = session_.take_result()) result_ = std::move(fresh);
    }

    // Atalhos globais.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter)) {
        execute_current_sql();
    }

    draw_menu_bar();
    draw_dockspace();

    draw_raft_panel();
    draw_navigator_panel();
    draw_editor_panel();
    draw_grid_panel();
    draw_query_log_panel();
    draw_status_bar();

    if (show_connect_) draw_connect_dialog();
    if (show_about_)   draw_about_window();
    if (show_demo_)    ImGui::ShowDemoWindow(&show_demo_);
}

void MainShell::draw_dockspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Host ocupa a area de trabalho menos a faixa da barra de status.
    const ImVec2 host_size(vp->WorkSize.x, vp->WorkSize.y - kStatusBarHeight);

    ImGui::SetNextWindowPos(vp->WorkPos);
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

        ImGui::DockBuilderDockWindow("Raft",      left_top);
        ImGui::DockBuilderDockWindow("Navigator", left_bottom);
        ImGui::DockBuilderDockWindow("SQL",       center_top);
        ImGui::DockBuilderDockWindow("Resultado", center_bottom);
        ImGui::DockBuilderDockWindow("Queries",   center_bottom);
        ImGui::DockBuilderFinish(dock_id);
    }

    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void MainShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Arquivo")) {
        if (ImGui::MenuItem("Nova conexão...", "Ctrl+Shift+N")) show_connect_ = true;
        if (ImGui::MenuItem("Desconectar", nullptr, false,
                            session_.state() == SessionState::connected)) {
            session_.disconnect();
            result_.reset();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Sair", "Alt+F4")) wants_quit_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Editar")) {
        if (ImGui::MenuItem("Desfazer", "Ctrl+Z", false, editor_->CanUndo())) {
            editor_->Undo();
        }
        if (ImGui::MenuItem("Refazer", "Ctrl+Y", false, editor_->CanRedo())) {
            editor_->Redo();
        }
        ImGui::Separator();
        ImGui::MenuItem("Localizar", "Ctrl+F");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("SQL")) {
        const bool can_run = session_.state() == SessionState::connected &&
                             !session_.busy();
        if (ImGui::MenuItem("Executar", "Ctrl+Enter", false, can_run)) {
            execute_current_sql();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Ajuda")) {
        ImGui::MenuItem("Demo do ImGui", nullptr, &show_demo_);
        ImGui::Separator();
        if (ImGui::MenuItem("Sobre o C-Otter")) show_about_ = true;
        ImGui::EndMenu();
    }

    const ImGuiIO& io = ImGui::GetIO();
    char fps[48];
    std::snprintf(fps, sizeof(fps), "%.1f fps  |  %.2f ms",
                  static_cast<double>(io.Framerate),
                  static_cast<double>(1000.0f / io.Framerate));
    const float width = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextColored(col4(palette::text_dim), "%s", fps);

    ImGui::EndMainMenuBar();
}

void MainShell::draw_raft_panel() {
    if (ImGui::Begin("Raft")) {
        if (ImGui::Button("Nova conexão")) show_connect_ = true;
        ImGui::Separator();

        const SessionState state = session_.state();

        if (state == SessionState::disconnected) {
            ImGui::TextColored(col4(palette::text_dim), "nenhuma conexão");
        } else {
            const std::string database = session_.database_name();

            const std::uint32_t color =
                state == SessionState::connected  ? palette::ok
                : state == SessionState::failed   ? palette::error
                                                  : palette::warn;
            ImGui::TextColored(col4(color), "●");
            ImGui::SameLine();
            ImGui::TextUnformatted(database.c_str());

            if (state == SessionState::connected) {
                ImGui::Indent();
                ImGui::TextColored(col4(palette::text_dim), "PostgreSQL %s",
                                   session_.server_version().c_str());
                ImGui::Unindent();
            }
        }
    }
    ImGui::End();
}

void MainShell::draw_navigator_panel() {
    if (ImGui::Begin("Navigator")) {
        if (session_.state() != SessionState::connected) {
            ImGui::TextColored(col4(palette::text_dim),
                               "conecte-se para navegar o schema");
            ImGui::End();
            return;
        }

        const std::vector<db::SchemaMeta> schemas = session_.schemas();

        for (const db::SchemaMeta& schema : schemas) {
            const std::string label =
                schema.name + " (" + std::to_string(schema.tables.size()) + ")";

            if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                continue;
            }

            for (const db::TableMeta& table : schema.tables) {
                const bool is_view = table.kind == db::ObjKind::view ||
                                     table.kind == db::ObjKind::materialized_view;

                ImGui::PushStyleColor(ImGuiCol_Text,
                                      col(is_view ? palette::data : palette::text));
                const bool open = ImGui::TreeNode(table.name.c_str());
                ImGui::PopStyleColor();

                // Tamanho e estimativa de linhas a direita, em tom apagado.
                if (!table.size_pretty.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(col4(palette::text_dim), "  %s",
                                       table.size_pretty.c_str());
                }

                if (open) {
                    // Lazy: so' consulta as colunas quando o no e' expandido.
                    if (!table.columns_loaded && !session_.busy()) {
                        session_.load_columns_async(schema.name, table.name);
                    }

                    if (table.columns.empty()) {
                        ImGui::TextColored(col4(palette::text_dim), "  carregando...");
                    }

                    for (const db::ColumnMeta& column : table.columns) {
                        ImGui::PushStyleColor(
                            ImGuiCol_Text,
                            col(column.primary_key ? palette::data_light
                                                   : palette::text));
                        ImGui::BulletText("%s", column.name.c_str());
                        ImGui::PopStyleColor();

                        ImGui::SameLine();
                        ImGui::TextColored(col4(palette::text_dim), "%s%s%s",
                                           column.type_name.c_str(),
                                           column.primary_key ? "  PK" : "",
                                           column.nullable ? "" : "  NOT NULL");
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void MainShell::draw_editor_panel() {
    if (ImGui::Begin("SQL")) {
        const bool can_run = session_.state() == SessionState::connected &&
                             !session_.busy();

        ImGui::BeginDisabled(!can_run);
        if (ImGui::Button("Executar  (Ctrl+Enter)")) execute_current_sql();
        ImGui::EndDisabled();

        ImGui::SameLine();
        const TextEditor::DocPos cursor = editor_->GetCurrentCursorPosition();
        ImGui::TextColored(col4(palette::text_dim), "  Ln %zu, Col %zu  |  %zu linhas",
                           cursor.line + 1, cursor.index + 1,
                           editor_->GetLineCount());

        if (session_.busy()) {
            ImGui::SameLine();
            draw_busy_indicator();
        }

        ImGui::Separator();
        editor_->Render("##sql", ImGui::GetContentRegionAvail());
    }
    ImGui::End();
}

void MainShell::draw_grid_panel() {
    if (ImGui::Begin("Resultado")) {
        if (!result_.has_value()) {
            ImGui::TextColored(col4(palette::text_dim),
                               "execute uma query para ver o resultado");
            ImGui::End();
            return;
        }

        const db::ResultSet& rs = *result_;

        ImGui::TextColored(col4(palette::text_dim),
                           "%zu linha(s) × %zu coluna(s)  |  %zu bytes",
                           rs.row_count(), rs.column_count(), rs.bytes_used());
        ImGui::Separator();

        if (rs.column_count() == 0) {
            ImGui::TextColored(col4(palette::ok), "comando executado");
            if (rs.affected_rows() >= 0) {
                ImGui::SameLine();
                ImGui::TextColored(col4(palette::text_dim), " (%lld linha(s) afetada(s))",
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
                            ImGui::TextColored(col4(palette::text_dim), "[null]");
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
    if (ImGui::Begin("Queries")) {
        const std::vector<db::QueryLog> log = session_.query_log();

        if (log.empty()) {
            ImGui::TextColored(col4(palette::text_dim), "nenhuma query ainda");
            ImGui::End();
            return;
        }

        ImGui::TextColored(col4(palette::text_dim),
                           "%zu query(s)  |  inclusive as internas de catálogo",
                           log.size());
        ImGui::Separator();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##querylog", 4, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("tempo", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("linhas", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("estado", ImGuiTableColumnFlags_WidthFixed, 64.0f);
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
                ImGui::TextColored(col4(entry.failed ? palette::error : palette::ok),
                                   entry.failed ? "erro" : "ok");

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

void MainShell::draw_connect_dialog() {
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::Begin("Conexão", &show_connect_,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(col4(palette::data), "PostgreSQL");
        ImGui::Separator();

        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("host", host_, sizeof(host_));
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("porta", port_, sizeof(port_),
                         ImGuiInputTextFlags_CharsDecimal);
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("banco", database_, sizeof(database_));
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("usuário", user_, sizeof(user_));
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("senha", password_, sizeof(password_),
                         ImGuiInputTextFlags_Password);

        ImGui::Separator();

        const bool connecting = session_.state() == SessionState::connecting;
        ImGui::BeginDisabled(connecting);

        if (ImGui::Button("Conectar", ImVec2(120, 0))) {
            db::ConnConfig config;
            config.host     = host_;
            config.port     = static_cast<std::uint16_t>(std::atoi(port_));
            config.database = database_;
            config.user     = user_;
            config.password = password_;
            session_.connect_async(config);
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Fechar", ImVec2(120, 0))) show_connect_ = false;

        if (connecting) {
            ImGui::SameLine();
            draw_busy_indicator();
        }

        const SessionState state = session_.state();
        if (state == SessionState::failed) {
            ImGui::Separator();
            ImGui::PushTextWrapPos(400.0f);
            ImGui::TextColored(col4(palette::error), "%s",
                               session_.status_message().c_str());
            ImGui::PopTextWrapPos();
        } else if (state == SessionState::connected) {
            ImGui::Separator();
            ImGui::TextColored(col4(palette::ok), "%s",
                               session_.status_message().c_str());
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(palette::bg_darkest));

    if (ImGui::Begin("##status", nullptr, flags)) {
        const SessionState state = session_.state();

        const std::uint32_t color =
            state == SessionState::connected ? palette::ok
            : state == SessionState::failed  ? palette::error
            : state == SessionState::connecting ? palette::warn
                                                : palette::text_dim;
        ImGui::TextColored(col4(color), "●");
        ImGui::SameLine();

        if (state == SessionState::connected) {
            ImGui::TextColored(col4(palette::data), "%s",
                               session_.database_name().c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(palette::text_dim), "| PostgreSQL %s |",
                               session_.server_version().c_str());
            ImGui::SameLine();
        }
        ImGui::TextColored(col4(palette::text_dim), "%s",
                           session_.status_message().c_str());
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Sobre o C-Otter", &show_about_,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(palette::fur_light));
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

        ImGui::PushStyleColor(ImGuiCol_Text, col(palette::data));
        ImGui::TextUnformatted("Here, every JOIN is an OTTER JOIN.");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::TextColored(col4(palette::text_dim),
                           "Dear ImGui %s  |  protocolo PostgreSQL v3 nativo",
                           IMGUI_VERSION);
    }
    ImGui::End();
}

} // namespace otter::ui
