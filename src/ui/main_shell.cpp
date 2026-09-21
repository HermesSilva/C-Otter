#include "ui/main_shell.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder: layout inicial programatico

#include <cstdio>
#include <string_view>
#include <vector>

namespace otter::ui {
namespace {

// -- Dados sinteticos -------------------------------------------------------
// Existem para dar forma ao layout. Serao substituidos por otter_meta e
// otter_db nas fases 1 e 2; nenhuma logica de produto depende deles.

struct FakeColumn {
    const char* name;
    const char* type;
    bool        pk;
};

struct FakeTable {
    const char*              name;
    std::vector<FakeColumn>  columns;
};

const std::vector<FakeTable>& sample_tables() {
    static const std::vector<FakeTable> tables = {
        {"otters",  {{"id", "int4", true}, {"name", "varchar(80)", false},
                     {"raft_id", "int4", false}, {"favorite_rock", "text", false}}},
        {"rafts",   {{"id", "int4", true}, {"name", "varchar(80)", false},
                     {"river", "varchar(120)", false}}},
        {"holts",   {{"id", "int4", true}, {"otter_id", "int4", false},
                     {"depth_cm", "numeric(6,2)", false}}},
        {"catches", {{"id", "int8", true}, {"otter_id", "int4", false},
                     {"species", "varchar(60)", false}, {"weight_g", "int4", false}}},
    };
    return tables;
}

struct FakeRow {
    int         id;
    const char* name;
    const char* raft;
    const char* rock;
    int         catches;
};

const std::vector<FakeRow>& sample_rows() {
    static const std::vector<FakeRow> rows = {
        {1, "Nina",    "Pebble Bay",  "quartzo listrado", 42},
        {2, "Oslo",    "Pebble Bay",  "basalto liso",     37},
        {3, "Kelp",    "Kelp Forest", "granito rosa",     58},
        {4, "Marina",  "Kelp Forest", "obsidiana",        61},
        {5, "Pistache","Cold Creek",  "seixo do rio",     29},
        {6, "Tulipa",  "Cold Creek",  "calcedonia",       33},
        {7, "Brisa",   "Pebble Bay",  "agata",            47},
    };
    return rows;
}

constexpr std::string_view kSampleSql =
    "-- C-Otter: every JOIN is an OTTER JOIN\n"
    "SELECT r.name          AS raft,\n"
    "       o.name          AS otter,\n"
    "       COUNT(c.id)     AS catches,\n"
    "       SUM(c.weight_g) AS total_g\n"
    "  FROM otters o\n"
    "  JOIN rafts  r ON r.id = o.raft_id\n"
    "  LEFT JOIN catches c ON c.otter_id = o.id\n"
    " WHERE r.river = 'Pebble Bay'\n"
    " GROUP BY r.name, o.name\n"
    " ORDER BY catches DESC;\n";

constexpr float kStatusBarHeight = 26.0f;

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

// Realce ilustrativo: as cores reais virao do lexer do otter_sql via ILexer5
// (ADR 0003/0004). Aqui so' mostramos a paleta aplicada a texto SQL.
void draw_fake_sql(std::string_view sql) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float line_height = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    std::size_t start = 0;
    int line_no = 1;
    while (start <= sql.size()) {
        const std::size_t end = sql.find('\n', start);
        const std::string_view line =
            sql.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                            : end - start);

        // Numero da linha, em calha propria.
        char gutter[8];
        std::snprintf(gutter, sizeof(gutter), "%3d", line_no);
        dl->AddText(pos, col(palette::text_dim), gutter);

        const ImVec2 text_pos(pos.x + 38.0f, pos.y);
        const bool is_comment = line.starts_with("--");
        dl->AddText(text_pos,
                    is_comment ? col(palette::text_dim) : col(palette::text),
                    line.data(), line.data() + line.size());

        pos.y += line_height;
        ++line_no;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }

    ImGui::Dummy(ImVec2(0.0f, static_cast<float>(line_no) * line_height));
}

} // namespace

MainShell::MainShell() = default;

void MainShell::draw() {
    // A barra de menu primeiro: ela reduz o WorkSize do viewport, e o dockspace
    // precisa desse valor ja' ajustado para nao invadir a faixa do menu.
    draw_menu_bar();
    draw_dockspace();

    draw_raft_panel();
    draw_navigator_panel();
    draw_editor_panel();
    draw_grid_panel();
    draw_status_bar();

    if (show_about_) draw_about_window();
    if (show_demo_)  ImGui::ShowDemoWindow(&show_demo_);
}

void MainShell::draw_dockspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Host ocupa a area de trabalho menos a faixa da barra de status, senao os
    // paineis ficam por baixo dela.
    const ImVec2 host_size(vp->WorkSize.x, vp->WorkSize.y - kStatusBarHeight);

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(host_size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##OtterDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dock_id = ImGui::GetID("OtterDockSpace");

    if (!layout_initialized_) {
        layout_initialized_ = true;

        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock_id, host_size);

        // Layout: Raft e Navigator a esquerda, editor em cima, grade embaixo.
        ImGuiID left = 0, center = 0;
        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Left, 0.22f, &left, &center);

        ImGuiID left_top = 0, left_bottom = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.35f, &left_top, &left_bottom);

        ImGuiID center_top = 0, center_bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.42f,
                                    &center_top, &center_bottom);

        ImGui::DockBuilderDockWindow("Raft",      left_top);
        ImGui::DockBuilderDockWindow("Navigator", left_bottom);
        ImGui::DockBuilderDockWindow("SQL",       center_top);
        ImGui::DockBuilderDockWindow("Resultado", center_bottom);
        ImGui::DockBuilderFinish(dock_id);
    }

    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void MainShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Arquivo")) {
        ImGui::MenuItem("Nova conexão...", "Ctrl+Shift+N");
        ImGui::MenuItem("Abrir script...", "Ctrl+O");
        ImGui::MenuItem("Salvar script",   "Ctrl+S");
        ImGui::Separator();
        if (ImGui::MenuItem("Sair", "Alt+F4")) wants_quit_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Editar")) {
        ImGui::MenuItem("Desfazer", "Ctrl+Z");
        ImGui::MenuItem("Refazer",  "Ctrl+Y");
        ImGui::Separator();
        ImGui::MenuItem("Localizar", "Ctrl+F");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("SQL")) {
        ImGui::MenuItem("Executar",           "Ctrl+Enter");
        ImGui::MenuItem("Executar script",    "Alt+X");
        ImGui::MenuItem("Explicar plano",     "Ctrl+Shift+E");
        ImGui::Separator();
        ImGui::MenuItem("Formatar",           "Ctrl+Shift+F");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Ajuda")) {
        ImGui::MenuItem("Demo do ImGui", nullptr, &show_demo_);
        ImGui::Separator();
        if (ImGui::MenuItem("Sobre o C-Otter")) show_about_ = true;
        ImGui::EndMenu();
    }

    // Indicador de FPS a direita: a meta de 144 fps precisa estar visivel.
    const ImGuiIO& io = ImGui::GetIO();
    char fps[48];
    std::snprintf(fps, sizeof(fps), "%.1f fps  |  %.2f ms",
                  static_cast<double>(io.Framerate),
                  static_cast<double>(1000.0f / io.Framerate));
    const float width = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)), "%s", fps);

    ImGui::EndMainMenuBar();
}

void MainShell::draw_raft_panel() {
    if (ImGui::Begin("Raft")) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::data)),
                           "Conexões");
        ImGui::Separator();

        // Um Holt e' uma conexao; o Raft e' o conjunto que flutua junto.
        if (ImGui::TreeNodeEx("Pebble Bay (PostgreSQL)",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BulletText("localhost:5432");
            ImGui::BulletText("database: otters");
            ImGui::TreePop();
        }
        ImGui::TreeNodeEx("Cold Creek (SQLite)", ImGuiTreeNodeFlags_Leaf |
                                                  ImGuiTreeNodeFlags_NoTreePushOnOpen);
        ImGui::TreeNodeEx("Kelp Forest (MySQL)", ImGuiTreeNodeFlags_Leaf |
                                                  ImGuiTreeNodeFlags_NoTreePushOnOpen);
    }
    ImGui::End();
}

void MainShell::draw_navigator_panel() {
    if (ImGui::Begin("Navigator")) {
        if (ImGui::TreeNodeEx("otters", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::TreeNodeEx("public", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (const FakeTable& table : sample_tables()) {
                    if (ImGui::TreeNode(table.name)) {
                        for (const FakeColumn& c : table.columns) {
                            ImGui::PushStyleColor(
                                ImGuiCol_Text,
                                col(c.pk ? palette::data : palette::text));
                            ImGui::BulletText("%s", c.name);
                            ImGui::PopStyleColor();
                            ImGui::SameLine();
                            ImGui::TextColored(
                                ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)),
                                "%s%s", c.type, c.pk ? "  PK" : "");
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void MainShell::draw_editor_panel() {
    if (ImGui::Begin("SQL")) {
        if (ImGui::Button("Executar")) { /* Fase 1 */ }
        ImGui::SameLine();
        ImGui::Button("Formatar");
        ImGui::SameLine();
        ImGui::Button("Explicar");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)),
                           "  |  Scintilla entra aqui (ADR 0003)");
        ImGui::Separator();

        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImGui::ColorConvertU32ToFloat4(col(palette::bg_darkest)));
        ImGui::BeginChild("##sqltext", ImVec2(0, 0), ImGuiChildFlags_Borders);
        draw_fake_sql(kSampleSql);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void MainShell::draw_grid_panel() {
    if (ImGui::Begin("Resultado")) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)),
                           "7 linhas  |  agregação local (ADR 0005)");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::warn)),
                           "  parcial");
        ImGui::Separator();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_SizingStretchProp;

        if (ImGui::BeginTable("##results", 5, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);     // cabecalho fixo
            ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthFixed, 48.0f);
            ImGui::TableSetupColumn("otter");
            ImGui::TableSetupColumn("raft");
            ImGui::TableSetupColumn("favorite_rock");
            ImGui::TableSetupColumn("catches", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            int total_catches = 0;
            for (const FakeRow& row : sample_rows()) {
                total_catches += row.catches;

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(
                    ImGui::ColorConvertU32ToFloat4(col(palette::data)), "%d", row.id);

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.name);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.raft);

                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.rock);

                // Barra na celula, proporcional ao valor (ADR 0005).
                ImGui::TableSetColumnIndex(4);
                const float ratio = static_cast<float>(row.catches) / 70.0f;
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                const float h = ImGui::GetTextLineHeight();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    p, ImVec2(p.x + w * ratio, p.y + h),
                    (col(palette::fur) & 0x00FFFFFFu) | 0x60000000u, 2.0f);
                ImGui::Text("%d", row.catches);
            }

            // Linha de totais (ADR 0005). Fundo proprio para nao ser confundida
            // com um registro do resultado.
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   (col(palette::fur) & 0x00FFFFFFu) | 0x50000000u);

            ImGui::PushStyleColor(ImGuiCol_Text, col(palette::data_light));
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("\xE2\x88\x91");          // U+2211, sigma
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%zu linhas", sample_rows().size());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", total_catches);
            ImGui::PopStyleColor();

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void MainShell::draw_status_bar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // WorkPos/WorkSize, nao Pos/Size: a area de trabalho exclui a barra de menu
    // e qualquer barra do sistema. Usar Size desenha a barra de status fora da
    // janela visivel.
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kStatusBarHeight));

    // Sem NoBringToFrontOnFocus: a barra precisa ficar sobre o dockspace, que
    // ocupa o viewport inteiro.
    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImGui::ColorConvertU32ToFloat4(col(palette::bg_darkest)));

    if (ImGui::Begin("##status", nullptr, flags)) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::data)),
                           "Pebble Bay");
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)),
                           "| PostgreSQL 16 | public | autocommit");
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Sobre o C-Otter", &show_about_,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize)) {
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
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col(palette::text_dim)),
                           "Dear ImGui %s  |  Scintilla 5.6.6  |  Lexilla 5.5.3",
                           IMGUI_VERSION);
    }
    ImGui::End();
}

} // namespace otter::ui
