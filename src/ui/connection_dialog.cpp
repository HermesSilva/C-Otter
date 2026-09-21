#include "ui/connection_dialog.hpp"
#include "net/tls.hpp"
#include "base/i18n.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace otter::ui {
namespace {

ImVec4 col4(std::uint32_t c) {
    return ImGui::ColorConvertU32ToFloat4(static_cast<ImU32>(c));
}

// Campo de texto ligado a um std::string. O ImGui trabalha com buffer cru;
// isto evita espalhar arrays de char pela estrutura de configuracao.
bool input_string(const char* label, std::string& value, std::size_t capacity = 256,
                  ImGuiInputTextFlags flags = 0) {
    std::vector<char> buffer(std::max(capacity, value.size() + 1), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());

    if (ImGui::InputText(label, buffer.data(), buffer.size(), flags)) {
        value = buffer.data();
        return true;
    }
    return false;
}

// Campo com texto-fantasma: o que aparece em cinza enquanto esta' vazio.
//
// Para o nome da conexao, o fantasma e' o nome DERIVADO ("banco@host"). Sem
// ele, um campo vazio nao diria com que rotulo a conexao vai aparecer na
// lista -- e o usuario descobriria depois de salvar.
bool input_string_hint(const char* label, const char* hint, std::string& value,
                       std::size_t capacity = 256) {
    std::vector<char> buffer(std::max(capacity, value.size() + 1), char{0});
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());

    if (ImGui::InputTextWithHint(label, hint, buffer.data(), buffer.size())) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool input_uint16(const char* label, std::uint16_t& value) {
    int temporary = value;
    if (ImGui::InputInt(label, &temporary, 0, 0)) {
        value = static_cast<std::uint16_t>(std::clamp(temporary, 0, 65535));
        return true;
    }
    return false;
}

bool input_seconds(const char* label, std::chrono::seconds& value) {
    int temporary = static_cast<int>(value.count());
    if (ImGui::InputInt(label, &temporary, 0, 0)) {
        value = std::chrono::seconds(std::max(0, temporary));
        return true;
    }
    return false;
}

// Rotulo com tooltip de ajuda, como o "(?)" do DBeaver.
void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextColored(col4(colors().text_dim), "(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

// Em ingles: a chave de traducao e' o proprio texto (base/i18n.hpp), e a
// comparacao com DriverEntry::category tambem usa estes valores.
constexpr std::array<const char*, 8> kCategories = {
    "All", "Popular", "SQL", "NoSQL", "Analytical",
    "Files", "Embedded", "Timeseries",
};

} // namespace

// Catalogo espelhando o do DBeaver. Os indisponiveis aparecem esmaecidos com o
// motivo -- esconder a lista inteira daria a impressao de que o produto so'
// fala com PostgreSQL por design.
std::vector<DriverEntry> driver_catalog() {
    // Categorias e notas em ingles: sao chaves de traducao, resolvidas com
    // TR() no momento de desenhar.
    return {
        {"postgresql", "PostgreSQL",  "Popular",    5432, true,  nullptr},
        {"mysql",      "MySQL",       "Popular",    3306, true,  ""},
        {"mariadb",    "MariaDB",     "Popular",    3306, true,  ""},
        {"sqlite",     "SQLite",      "Embedded",      0, false, "planned for phase 3"},
        {"mssql",      "SQL Server",  "Popular",    1433, false, "TDS planned for phase 3"},
        {"oracle",     "Oracle",      "Popular",    1521, false, "planned for phase 3"},
        {"db2",        "Db2 for LUW", "SQL",       50000, false, "out of scope for v1"},
        {"clickhouse", "ClickHouse",  "Analytical", 8123, false, "out of scope for v1"},
        {"duckdb",     "DuckDB",      "Analytical",    0, false, "out of scope for v1"},
        {"snowflake",  "Snowflake",   "Analytical",  443, false, "out of scope for v1"},
        {"h2",         "H2",          "Embedded",   8082, false, "out of scope for v1"},
        {"firebird",   "Firebird",    "SQL",        3050, false, "out of scope for v1"},
        {"csv",        "CSV",         "Files",         0, false, "out of scope for v1"},
        {"mongodb",    "MongoDB",     "NoSQL",     27017, false, "out of scope for v1"},
        {"redis",      "Redis",       "NoSQL",      6379, false, "out of scope for v1"},
        {"cassandra",  "Cassandra",   "NoSQL",      9042, false, "out of scope for v1"},
        {"influxdb",   "InfluxDB",    "Timeseries", 8086, false, "out of scope for v1"},
        {"timescale",  "TimescaleDB", "Timeseries", 5432, false, "uses the PostgreSQL driver"},
    };
}

ConnectionDialog::ConnectionDialog() = default;

void ConnectionDialog::open_new() {
    profile_ = db::ConnectionProfile{};
    step_    = Step::select_driver;
    editing_ = false;
    visible_ = true;
}

void ConnectionDialog::open_edit(const db::ConnectionProfile& profile) {
    profile_ = profile;
    step_    = Step::configure;
    editing_ = true;
    visible_ = true;
}

void ConnectionDialog::draw(const Feedback& feedback) {
    if (!visible_) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Maior que as 760x560 das abas: a arvore come' 230px de largura, e as
    // paginas passaram a ter conteudo proprio em vez de dividir uma aba.
    ImGui::SetNextWindowSize(ImVec2(940, 620), ImGuiCond_Appearing);

    const char* title = editing_ ? TR("Edit connection###ConnDialog")
                                 : TR("New connection###ConnDialog");

    if (ImGui::Begin(title, &visible_, ImGuiWindowFlags_NoDocking)) {
        if (step_ == Step::select_driver) {
            draw_driver_catalog();
        } else {
            draw_configuration(feedback);
        }
    }
    ImGui::End();
}

void ConnectionDialog::draw_driver_catalog() {
    ImGui::TextColored(col4(colors().accent_light), TR("Select the database"));
    ImGui::TextColored(col4(colors().text_dim),
                       TR("Choose the driver for the new connection."));
    ImGui::Separator();

    // Coluna de categorias, como no DBeaver.
    ImGui::BeginChild("##categories", ImVec2(150, -46), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(kCategories.size()); ++i) {
        if (ImGui::Selectable(TR(kCategories[static_cast<std::size_t>(i)]),
                              category_index_ == i)) {
            category_index_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##drivers", ImVec2(0, -46));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", TR("Filter drivers..."),
                             driver_filter_, sizeof(driver_filter_));
    ImGui::Separator();

    const std::string_view category =
        kCategories[static_cast<std::size_t>(category_index_)];

    if (ImGui::BeginTable("##driverlist", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(TR("Driver"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Category"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(TR("Status"), ImGuiTableColumnFlags_WidthFixed, 210.0f);

        for (const DriverEntry& driver : driver_catalog()) {
            if (category != "All" && category != driver.category) continue;
            if (!contains_ci(driver.name, driver_filter_)) continue;

            ImGui::TableNextRow();
            ImGui::PushID(driver.id);

            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(!driver.available);

            const bool selected = profile_.driver_id == driver.id;
            if (ImGui::Selectable(driver.name, selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                profile_.driver_id = driver.id;
                profile_.port      = driver.default_port;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    step_ = Step::configure;
                }
            }
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(col4(colors().text_dim), "%s", TR(driver.category));

            ImGui::TableSetColumnIndex(2);
            if (driver.available) {
                ImGui::TextColored(col4(colors().ok), TR("available"));
            } else {
                ImGui::TextColored(col4(colors().text_dim), "%s", TR(driver.note));
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Separator();

    // Habilita pelo que o catalogo diz estar disponivel, e nao por um nome
    // fixo: com o driver fixo aqui, acrescentar o MySQL deixaria a linha
    // selecionavel e o botao "Proximo" morto, sem explicacao na tela.
    bool can_advance = false;
    for (const DriverEntry& driver : driver_catalog()) {
        if (profile_.driver_id == driver.id) { can_advance = driver.available; break; }
    }
    ImGui::BeginDisabled(!can_advance);
    if (ImGui::Button(TR("Next >"), ImVec2(110, 0))) step_ = Step::configure;
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(110, 0))) visible_ = false;

    if (!can_advance) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim),
                           TR("  select an available driver"));
    }
}

void ConnectionDialog::draw_configuration(const Feedback& feedback) {
    // Faixa colorida do tipo de conexão -- o mesmo recurso que o DBeaver usa
    // para diferenciar produção de desenvolvimento num olhar.
    // O nome da conexao fica AQUI, no topo, e nao escondido na ultima aba.
    //
    // Ele estava em "Geral", a oitava aba, e a faixa do topo so' o EXIBIA --
    // quem queria renomear via o nome na tela, clicava nele, nada acontecia,
    // e concluia que a aplicacao nao deixava renomear. O campo funcionava; o
    // problema era onde ele estava.
    //
    // No DBeaver o nome tambem fica sempre visivel, fora das abas. Quem vem
    // de la' procura no topo.
    const db::ConnectionTypeInfo& type = db::connection_type_info(profile_.type);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col4(type.color & 0x40FFFFFFu));
    ImGui::BeginChild("##typeband", ImVec2(0, 30), ImGuiChildFlags_None);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(col4(type.color), "  %s", type.name);
    ImGui::SameLine();
    ImGui::TextColored(col4(colors().text_dim), "|");
    ImGui::SameLine();

    // O placeholder mostra o nome DERIVADO ("banco@host") quando o campo esta'
    // vazio: e' o que a lista lateral vai exibir, e ve-lo aqui evita a
    // surpresa de salvar sem nome e achar a conexao com outro rotulo.
    ImGui::SetNextItemWidth(-8.0f);
    input_string_hint("##connname", profile_.effective_name().c_str(),
                      profile_.name, 128);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Connection name. Empty uses \"database@host\"."));
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Arvore a' esquerda, pagina a' direita -- a estrutura do DBeaver.
    //
    // A altura reserva o rodape; o painel da esquerda tem largura fixa, como
    // la', para o conteudo nao dancar ao trocar de pagina.
    const float footer = 72.0f;
    const float body = ImGui::GetContentRegionAvail().y - footer;

    // 230px: "Configurações de conexão" cabe inteiro. Com 190 o rotulo
    // truncava para "Configurações de conexã" -- e um rotulo cortado e'
    // justamente o que impede reconhecer a pagina que se procura.
    ImGui::BeginChild("##pagetree", ImVec2(230.0f, body), ImGuiChildFlags_Borders);
    draw_page_tree();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##pagebody", ImVec2(0, body), ImGuiChildFlags_Borders);
    draw_page_body();
    ImGui::EndChild();

    // Rodapé fixo com as ações.
    ImGui::Separator();

    if (!editing_) {
        if (ImGui::Button(TR("< Back"), ImVec2(100, 0))) step_ = Step::select_driver;
        ImGui::SameLine();
    }

    ImGui::BeginDisabled(feedback.busy);

    // "Testar" conecta e DEIXA o dialogo aberto: o ponto e' ver o resultado
    // no rodape e continuar ajustando os campos.
    //
    // Fica a' ESQUERDA, e OK/Close a' direita, como no DBeaver: quem procura
    // o botao de confirmar olha para o canto inferior direito.
    if (ImGui::Button(TR("Test Connection ..."), ImVec2(150, 0)) && on_test_) {
        on_test_(profile_);
    }

    // Empurra OK/Close para a direita. Dois botoes de 100 + o espacamento.
    const float pair = 100.0f * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - pair);

    if (ImGui::Button(editing_ ? TR("OK") : TR("Finish"), ImVec2(100, 0))) {
        // Apenas UM dos dois: on_save_ e on_connect_ gravam o perfil em
        // disco, e chamar os dois abriria a conexao duas vezes alem de
        // gravar duas.
        //
        // Editando, "Salvar" so' guarda -- reconectar a cada ajuste de
        // descricao seria intrusivo. Criando, "Concluir" conecta, que e' o
        // que o usuario acabou de pedir.
        if (editing_) {
            if (on_save_) on_save_(profile_);
        } else {
            if (on_connect_) on_connect_(profile_);
        }
        // Fecha: o dialogo cumpriu seu papel. Antes era preciso clicar em
        // Cancelar depois de concluir, o que sugere que algo deu errado.
        visible_ = false;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(TR("Close"), ImVec2(100, 0))) visible_ = false;

    if (feedback.busy) {
        ImGui::SameLine();
        const float t = static_cast<float>(ImGui::GetTime());
        ImVec4 pulse = col4(colors().data_light);
        pulse.w = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
        ImGui::TextColored(pulse, TR("  * connecting..."));
    } else if (feedback.failed) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(colors().error), "%s", feedback.message.c_str());
        ImGui::PopTextWrapPos();
    } else if (feedback.succeeded) {
        ImGui::TextColored(col4(colors().ok), "%s", feedback.message.c_str());
    }
}

// A arvore de paginas, na ordem e no aninhamento de
// EditConnectionWizard.addPages() -- ver docs/DIALOG-PARITY.md.
//
// Declarar a hierarquia numa tabela, em vez de espalha-la em chamadas de
// TreeNode, deixa a comparacao com o DBeaver ser uma leitura linha a linha.
// Os rotulos sao os oficiais (UIConnectionMessages.properties e os
// bundle.properties), nao traducoes livres: quem vem de la' procura por
// estas palavras.
void ConnectionDialog::draw_page_tree() {
    using Page = ConnectionDialog::Page;

    // `selectable == false` marca a categoria que apenas agrupa. No DBeaver
    // "Data Editor" e "SQL Editor" ABREM pagina, entao sao selecionaveis; e'
    // o comportamento herdado aqui.
    static const PageNode kNodes[] = {
        {Page::connection_settings, "Connection settings",  0, true},
        {Page::initialization,      "Initialization",       1, true},
        {Page::transactions,        "Transactions",         1, true},
        {Page::driver_properties,   "Internal parameters",  1, true},
        {Page::general,             "General",              0, true},
        {Page::metadata,            "Metadata",             0, true},
        {Page::errors_timeouts,     "Errors and timeouts",  0, true},
        {Page::data_transfer,       "Data Transfer",        0, true},
        {Page::data_editor,         "Data Editor",          0, true},
        {Page::binary_editor,       "Binary Editor",        1, true},
        {Page::data_formats,        "Data Formats",         1, true},
        {Page::data_editor_grid,    "Grid",                 1, true},
        {Page::sql_editor,          "SQL Editor",           0, true},
        {Page::sql_code_editor,     "Code Editor",          1, true},
        {Page::sql_completion,      "Code Completion",      1, true},
        {Page::sql_formatting,      "Formatting",           1, true},
        {Page::sql_processing,      "SQL Processing",       1, true},
    };

    for (const PageNode& node : kNodes) {
        // O recuo faz o papel do aninhamento. Um TreeNode de verdade
        // permitiria colapsar, mas o DBeaver abre as categorias por padrao e
        // colapsa-las esconderia justamente o que se quer comparar.
        if (node.depth > 0) ImGui::Indent(16.0f * node.depth);

        const bool selected = node.selectable && page_ == node.page;
        if (ImGui::Selectable(TR(node.label), selected) && node.selectable) {
            page_ = node.page;
        }

        if (node.depth > 0) ImGui::Unindent(16.0f * node.depth);
    }
}

void ConnectionDialog::draw_page_body() {
    using Page = ConnectionDialog::Page;

    switch (page_) {
    case Page::connection_settings: draw_page_connection_settings(); break;
    case Page::initialization:      draw_page_initialization();      break;
    case Page::transactions:        draw_page_transactions();        break;
    case Page::driver_properties:   draw_page_driver_properties();   break;
    case Page::general:             draw_page_general();             break;
    case Page::metadata:            draw_page_metadata();            break;

    // Abaixo, o que o DBeaver oferece e o C-Otter ainda nao. Cada uma diz o
    // que falta, em vez de mostrar uma pagina vazia que parece defeito.
    case Page::errors_timeouts:   draw_page_errors_timeouts();  break;
    case Page::data_transfer:     draw_page_data_transfer();    break;
    case Page::data_editor:       draw_page_data_editor();      break;
    case Page::data_editor_grid:  draw_page_data_editor();      break;
    case Page::binary_editor:     draw_page_binary_editor();    break;
    case Page::data_formats:      draw_page_data_formats();     break;
    case Page::sql_editor:
        draw_page_placeholder("SQL editor defaults for this connection");
        break;
    case Page::sql_completion:    draw_page_sql_completion();   break;
    case Page::sql_code_editor:   draw_page_sql_code_editor();  break;
    case Page::sql_formatting:    draw_page_sql_formatting();   break;
    case Page::sql_processing:    draw_page_sql_processing();   break;
    }
}

// Formatacao do SQL, por conexao (main.sql.format).
//
// As opcoes existiam em sql::FormatOptions, com os valores fixos no codigo:
// Ctrl+Shift+F sempre formatava em MAIUSCULAS, estilo rio, indentacao 4.
// Agora sao do PERFIL -- e' o que permite maiusculas no banco legado e
// minusculas no novo, que e' a razao de o DBeaver as por por conexao.
void ConnectionDialog::draw_page_sql_formatting() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Keywords"));
    ImGui::Separator();

    const char* kCases[] = {TR("As typed"), TR("UPPERCASE"), TR("lowercase")};
    ImGui::SetNextItemWidth(220);
    ImGui::Combo(TR("Case"), &editor.keyword_case, kCases, IM_ARRAYSIZE(kCases));
    help_marker(TR("Applies when formatting (Ctrl+Shift+F). It does not "
                   "change what you type."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Layout"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(TR("Indent width"), &editor.indent_width, 1, 2);
    editor.indent_width = std::clamp(editor.indent_width, 1, 16);

    ImGui::Checkbox(TR("River style"), &editor.river_style);
    help_marker(TR("Aligns the main clauses to the right, like psql:\n"
                   "  SELECT a, b\n"
                   "    FROM t\n"
                   "   WHERE x\n\n"
                   "Off produces the style more common in source code, with "
                   "every clause at the left margin."));

    int wrap = static_cast<int>(editor.wrap_select_after);
    ImGui::SetNextItemWidth(120);
    if (ImGui::InputInt(TR("Wrap the SELECT list after"), &wrap, 1, 2)) {
        editor.wrap_select_after =
            static_cast<std::size_t>(std::clamp(wrap, 1, 64));
    }
    help_marker(TR("One column per line when the list has more items than "
                   "this. Short lists fit on one line and read better that "
                   "way."));
}

// Processamento SQL, por conexao (main.sqlexecute).
void ConnectionDialog::draw_page_sql_processing() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Result set"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(140);
    ImGui::InputInt(TR("Rows per page"), &editor.page_size, 50, 100);
    editor.page_size = std::clamp(editor.page_size, 10, 10000);
    help_marker(TR("A query without LIMIT is rewritten to bring one page at "
                   "a time (ADR 0011). Bigger fills the screen in one trip; "
                   "smaller comes back faster on a distant server."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Scripts"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Stop on the first error"), &editor.stop_script_on_error);
    help_marker(TR("Off, the script keeps going after a failed statement. "
                   "In a migration that runs the remaining statements against "
                   "a state the author did not expect."));

    // O delimitador nao e' configuravel: o divisor de script ja' entende o
    // ';' padrao, o $$...$$ do PostgreSQL e o DELIMITER do MySQL -- que e'
    // o comando SQL com que se troca o delimitador, e funciona dentro do
    // proprio script. Uma caixa aqui seria uma segunda forma de dizer a
    // mesma coisa, e as duas poderiam discordar.
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("The statement delimiter follows the dialect: ';', "
                          "$$...$$ on PostgreSQL, and the DELIMITER command "
                          "on MySQL."));
}

// Editor de codigo, por conexao (main.sql.codeeditor).
//
// Espelha TextEditor::config, que ja' tinha estas opcoes com os valores
// fixos em MainShell.
void ConnectionDialog::draw_page_sql_code_editor() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Indentation"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(TR("Tab size"), &editor.tab_size, 1, 2);
    editor.tab_size = std::clamp(editor.tab_size, 1, 16);
    help_marker(TR("How many columns a tab takes ON SCREEN. It does not "
                   "change what is written to the file."));

    ImGui::Checkbox(TR("Auto-indent"), &editor.auto_indent);
    help_marker(TR("A new line starts at the same indentation as the "
                   "previous one."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Display"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Line numbers"), &editor.show_line_numbers);

    ImGui::Checkbox(TR("Word wrap"), &editor.word_wrap);
    help_marker(TR("Off by default, as in any code editor: with it on, one "
                   "long line takes several rows and the line number stops "
                   "matching what the server reports in an error."));

    ImGui::Checkbox(TR("Code folding"), &editor.code_folding);
    help_marker(TR("Collapse blocks. Needs matching brackets, which it turns "
                   "on by itself."));

    ImGui::Checkbox(TR("Matching brackets"), &editor.show_matching_brackets);
    help_marker(TR("Highlights the pair of the bracket under the cursor. "
                   "Turning it off also turns off block folding, which "
                   "depends on it."));

    // Desabilitada: a opcao chega ao TextEditor (confirmado com trace --
    // SetShowWhitespacesEnabled(true) e' chamada a cada quadro), mas os
    // pontos nao sao desenhados. O defeito esta' no widget de terceiro, e a
    // caixa ligada seria um campo que finge funcionar (diretriz 6).
    ImGui::BeginDisabled(true);
    ImGui::Checkbox(TR("Show whitespace"), &editor.show_whitespace);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(col4(colors().warn), "%s", TR("(not working yet)"));
    help_marker(TR("Draws spaces and tabs. The option reaches the editor but "
                   "nothing is drawn -- a defect in the text widget, not in "
                   "the setting."));
}

// Completar codigo, por conexao (main.sql.completion).
//
// Espelha TextEditor::AutoCompleteConfig, que ja' tinha estas opcoes com os
// valores fixos em MainShell.
void ConnectionDialog::draw_page_sql_completion() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("When to suggest"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Suggest while typing"), &editor.complete_on_typing);
    help_marker(TR("Off, completion only opens with Ctrl+Space."));

    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(TR("Delay (ms)"), &editor.complete_delay_ms, 50, 100);
    editor.complete_delay_ms = std::clamp(editor.complete_delay_ms, 0, 2000);
    help_marker(TR("How long to wait after a keystroke before opening the "
                   "list. Zero opens immediately, which gets in the way when "
                   "typing fast."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Where to suggest"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Inside comments"), &editor.complete_in_comments);
    ImGui::Checkbox(TR("Inside strings"), &editor.complete_in_strings);
    help_marker(TR("Off by default: a table name suggested inside a string "
                   "literal would be inserted as text, not as an "
                   "identifier."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Behaviour"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Insert a single match automatically"),
                    &editor.auto_insert_single);
    help_marker(TR("With one candidate only, insert it without showing the "
                   "list. Saves a keystroke, but surprises when the single "
                   "match is not what you meant."));
}

// A pagina existe na arvore porque o DBeaver a tem, mas o C-Otter ainda nao
// implementou o que vai dentro. Dizer isso e' a diretriz 6: um campo que
// parece funcionar e nao funciona e' pior que um campo ausente -- e uma
// pagina em branco parece defeito.
void ConnectionDialog::draw_page_placeholder(const char* what) {
    ImGui::TextColored(col4(colors().warn), "%s", TR("Not implemented yet."));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(col4(colors().text_dim), "%s", TR(what));
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("The DBeaver dialog has this page. It is listed here so "
                          "the structure matches; the options are not available "
                          "in this version."));
    ImGui::PopTextWrapPos();
}

void ConnectionDialog::draw_page_connection_settings() {
    ImGui::TextColored(col4(colors().data), TR("Server"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(320);
    input_string(TR("Host"), profile_.host, 128);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port"), profile_.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Database"), profile_.database, 128);

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Authentication"));
    ImGui::Separator();

    // Passam por TR() na montagem, nao no literal: um array `constexpr` de
    // literais em portugues nao traduz, e era o que acontecia aqui -- o
    // combo dizia "Banco de dados nativo" mesmo com a UI em ingles.
    const char* kAuthModels[] = {
        TR("Database Native"), TR("No Authentication"), TR("Ident / Peer"),
        TR("Kerberos"), TR("AWS IAM"),
    };
    int auth = static_cast<int>(profile_.auth_model);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Method"), &auth, kAuthModels,
                     IM_ARRAYSIZE(kAuthModels))) {
        profile_.auth_model = static_cast<db::AuthModel>(auth);
    }

    const bool needs_credentials =
        profile_.auth_model == db::AuthModel::database_native;

    ImGui::BeginDisabled(!needs_credentials);
    ImGui::SetNextItemWidth(320);
    input_string(TR("User"), profile_.user, 128);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Password"), profile_.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::Checkbox(TR("Save password"), &profile_.save_password);

    // O aviso e' literal de proposito: a criptografia do arquivo usa a chave
    // fixa do DBeaver, que e' publica no codigo-fonte dele. Deixar o usuario
    // supor que ha' um cofre por tras seria o tipo de campo que finge
    // funcionar (diretiva 6; ADR 0012).
    help_marker(TR(
        "The password is stored in credentials-config.json, encrypted the "
        "same way DBeaver does it.\n\n"
        "That encryption uses a fixed key published in DBeaver's source "
        "code: it protects against a casual look at the file, and against "
        "nothing more. Leave it off for credentials that matter."));

    if (profile_.save_password) {
        icon_inline(Icon::warning, colors().warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(colors().warn),
                           TR("weak encryption, for DBeaver compatibility"));
    }
    ImGui::EndDisabled();

    // Rede, nas abas de baixo -- e' onde o DBeaver as poe.
    //
    // Eram abas de primeiro nivel, irmas de "Principal". No DBeaver SSH, SSL
    // e Proxy pertencem a' pagina do driver (ConnectionPageSettings), nao a'
    // raiz: quem procura SSL procura DENTRO das configuracoes de conexao.
    ImGui::Spacing();
    if (ImGui::BeginTabBar("##network", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(TR("SSH"))) {
            draw_tab_ssh();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("SSL"))) {
            draw_tab_ssl();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("Proxy"))) {
            draw_tab_proxy();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// "Metadata" no DBeaver: as opcoes de leitura do catalogo. Aqui elas sao as
// do SGBD conectado -- so' PostgreSQL tem pagina propria por enquanto.
void ConnectionDialog::draw_page_metadata() {
    if (profile_.driver_id != "postgresql") {
        draw_page_placeholder("Metadata reading options for this driver");
        return;
    }

    ImGui::TextColored(col4(colors().data), TR("Navigator settings"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Show all databases"),
                    &profile_.postgres.show_non_default_databases);
    help_marker(TR("Lists every database on the server, not only the connected one."));

    ImGui::Checkbox(TR("Show template databases"),
                    &profile_.postgres.show_template_databases);
    help_marker(TR("Includes template0 and template1."));

    ImGui::Checkbox(TR("Show inaccessible databases"),
                    &profile_.postgres.show_unavailable_databases);
    help_marker(TR("Includes databases the user has no permission to connect to."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Performance"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Read size statistics"),
                    &profile_.postgres.show_database_statistics);
    help_marker(TR("Computes the on-disk size of tables and indexes. On very large "
                   "databases it makes expanding the tree slower."));

    ImGui::Checkbox(TR("Read all data types"),
                    &profile_.postgres.read_all_data_types);
    help_marker("Inclui tipos raros e de sistema. Deixa o carregamento de "
                "metadados mais lento.");

    ImGui::Checkbox(TR("Read key columns"),
                    &profile_.postgres.read_keys_with_columns);
    help_marker(TR("Loads the columns of each key along with the key. Useful for "
                   "JOIN inference; costs one extra query."));

    ImGui::Checkbox(TR("Use prepared statements"),
                    &profile_.postgres.use_prepared_statements);

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), "SQL");
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string(TR("Session role"), profile_.postgres.session_role, 64);
    help_marker(TR("Runs SET ROLE when opening the connection."));

    ImGui::Checkbox(TR("Replace legacy timezone"),
                    &profile_.postgres.replace_legacy_timezone);
    help_marker("Converte timestamptz do formato antigo para o atual.");

}

void ConnectionDialog::draw_page_driver_properties() {
    ImGui::TextColored(col4(colors().data), TR("Driver properties"));
    ImGui::TextColored(col4(colors().text_dim),
                       TR("Parameters passed directly to the driver on connect."));

    // Diz ONDE eles entram, que e' o que decide se um nome vai funcionar.
    // O texto anterior prometia "passados ao driver" e nada acontecia: os
    // valores iam para disco e a conexao os ignorava.
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       profile_.driver_id == "postgresql"
                           ? TR("PostgreSQL: runtime parameters of the startup "
                                "message (search_path, statement_timeout, "
                                "TimeZone...).")
                           : TR("MySQL: SET @@name = value, right after "
                                "connecting."));
    ImGui::TextColored(col4(colors().warn), "%s",
                       TR("A name the server does not accept fails the "
                          "connection, naming the parameter."));
    ImGui::Separator();

    if (ImGui::BeginTable("##props", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(TR("Property"));
        ImGui::TableSetupColumn(TR("Value"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableHeadersRow();

        std::string to_remove;
        for (auto& [key, value] : profile_.driver_properties) {
            ImGui::TableNextRow();
            ImGui::PushID(key.c_str());

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            input_string("##value", value, 256);

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("×")) to_remove = key;

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (!to_remove.empty()) profile_.driver_properties.erase(to_remove);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##newkey", TR("property"),
                             property_key_, sizeof(property_key_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##newvalue", TR("value"),
                             property_value_, sizeof(property_value_));
    ImGui::SameLine();
    if (ImGui::Button(TR("Add")) && property_key_[0] != '\0') {
        profile_.driver_properties[property_key_] = property_value_;
        property_key_[0] = '\0';
        property_value_[0] = '\0';
    }

}

void ConnectionDialog::draw_tab_ssh() {
    ImGui::BeginChild("##ssh", ImVec2(0, 0));

    ImGui::Checkbox(TR("Use SSH tunnel"), &profile_.ssh.enabled);

    // Com quebra: o aviso e' mais largo que o painel e sem isto some' a
    // metade dele, justamente a que diz que o tunel nao e' estabelecido.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(col4(colors().warn),
                       TR("Not implemented - the settings are saved, but the tunnel is "
                          "not established."));
    ImGui::PopTextWrapPos();
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssh.enabled);

    ImGui::SetNextItemWidth(320);
    input_string(TR("SSH host"), profile_.ssh.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port##ssh"), profile_.ssh.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("User##ssh"), profile_.ssh.user, 64);

    // Sem static nem constexpr: TR() resolve em runtime e o rotulo precisa
    // mudar quando o usuario troca de idioma.
    const char* const kAuthTypes[] = {
        TR("Password"), TR("Public key"), TR("SSH agent"),
    };
    int auth = static_cast<int>(profile_.ssh.auth);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Authentication##ssh"), &auth, kAuthTypes,
                     IM_ARRAYSIZE(kAuthTypes))) {
        profile_.ssh.auth = static_cast<db::SshAuthType>(auth);
    }

    if (profile_.ssh.auth == db::SshAuthType::password) {
        ImGui::SetNextItemWidth(320);
        input_string(TR("Password##ssh"), profile_.ssh.password, 128,
                     ImGuiInputTextFlags_Password);
    } else if (profile_.ssh.auth == db::SshAuthType::public_key) {
        ImGui::SetNextItemWidth(320);
        input_string(TR("Private key"), profile_.ssh.private_key_path, 260);
        ImGui::SetNextItemWidth(320);
        input_string("Passphrase", profile_.ssh.passphrase, 128,
                     ImGuiInputTextFlags_Password);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Timeout (s)##ssh"), profile_.ssh.connect_timeout);
    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Keep-alive (s)##ssh"), profile_.ssh.keep_alive);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_ssl() {
    ImGui::BeginChild("##ssl", ImVec2(0, 0));

    // O binario do Linux ainda nao tem TLS (tls_openssl.cpp e' um esboco).
    // Deixar a caixa clicavel la' seria oferecer algo que falha so' na hora
    // de conectar -- a diretiva 6 manda dizer na tela.
    const bool available = net::tls_available();
    if (!available) {
        ImGui::TextColored(
            col4(colors().warn),
            TR("TLS is not available in this build of C-Otter."));
        ImGui::Separator();
    }

    ImGui::BeginDisabled(!available);
    ImGui::Checkbox(TR("Use SSL"), &profile_.ssl.enabled);
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssl.enabled || !available);

    static constexpr const char* kModes[] = {
        "disable", "allow", "prefer", "require", "verify-ca", "verify-full",
    };
    int mode = static_cast<int>(profile_.ssl.mode);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Mode"), &mode, kModes, IM_ARRAYSIZE(kModes))) {
        profile_.ssl.mode = static_cast<db::SslMode>(mode);
    }
    help_marker("require exige criptografia; verify-ca valida o certificado do "
                "server; verify-full also validates the host name.");

    // allow e prefer significam "tenta cifrar, aceita em claro": protegem
    // contra um escuta passivo e contra mais ninguem. Existem para nao perder
    // o valor de um perfil importado do DBeaver, mas o C-Otter conecta em
    // claro neles -- e quem escolhe precisa saber disso ANTES de conectar.
    if (profile_.ssl.mode == db::SslMode::allow ||
        profile_.ssl.mode == db::SslMode::prefer) {
        ImGui::TextColored(
            col4(colors().warn),
            TR("This mode accepts an unencrypted connection. Use 'require' "
               "or stronger to actually require TLS."));
    }

    ImGui::SetNextItemWidth(400);
    input_string(TR("CA certificate"), profile_.ssl.root_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string(TR("Client certificate"), profile_.ssl.client_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string(TR("Client key"), profile_.ssl.client_key_path, 260);

    // Estes tres campos sao gravados e lidos -- um perfil importado do
    // DBeaver nao os perde -- mas o aperto de mao ainda nao os usa: a
    // validacao vai pela cadeia de certificados do sistema.
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(col4(colors().warn),
                       TR("Certificate files are saved but not used yet; "
                          "validation uses the system certificate store."));
    ImGui::PopTextWrapPos();

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_proxy() {
    ImGui::BeginChild("##proxy", ImVec2(0, 0));

    ImGui::Checkbox(TR("Use SOCKS proxy"), &profile_.proxy.enabled);
    ImGui::TextColored(col4(colors().warn), TR("Not implemented."));
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.proxy.enabled);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Host##proxy"), profile_.proxy.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port##proxy"), profile_.proxy.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("User##proxy"), profile_.proxy.user, 64);
    ImGui::SetNextItemWidth(320);
    input_string(TR("Password##proxy"), profile_.proxy.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

// No DBeaver, Transactions e' pagina propria (PrefPageTransactions), irma de
// Initialization dentro de "Connection settings" -- nao uma secao dela.
void ConnectionDialog::draw_page_transactions() {
    ImGui::TextColored(col4(colors().data), TR("Transactions"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Auto-commit"), &profile_.auto_commit);
    help_marker(TR("When off, every change needs an explicit commit. Production "
                   "connections start with auto-commit off."));

    ImGui::Checkbox(TR("Read-only connection"), &profile_.read_only);
    help_marker(TR("Blocks INSERT, UPDATE, DELETE and DDL on the client."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Isolation"));
    ImGui::Separator();

    // O DBeaver so' lista os niveis com a conexao VIVA, porque le' do
    // servidor quais sao suportados. Aqui os quatro do padrao SQL aparecem
    // sempre, com "(padrão do servidor)" na frente -- uma lista vazia antes
    // de conectar seria pior, e um nivel recusado falha ao conectar
    // nomeando-se.
    const char* kLevels[] = {
        TR("(server default)"),
        TR("Read uncommitted"), TR("Read committed"),
        TR("Repeatable read"),  TR("Serializable"),
    };

    // -1 vira indice 0; os demais deslocam de 1.
    int level = profile_.isolation_level + 1;
    ImGui::SetNextItemWidth(240);
    if (ImGui::Combo(TR("Level"), &level, kLevels, IM_ARRAYSIZE(kLevels))) {
        profile_.isolation_level = level - 1;
    }
    help_marker(TR("Applied when connecting. A level the server does not "
                   "support fails the connection, naming it -- better than "
                   "a session that silently ignored the setting."));
}

void ConnectionDialog::draw_page_initialization() {
    ImGui::TextColored(col4(colors().data), TR("Session"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string(TR("Default schema"), profile_.default_schema, 64);
    help_marker(TR("Sets search_path when connecting."));

    ImGui::Text(TR("Initialization queries"));
    help_marker(TR("Run in order, right after the connection is established."));

    std::vector<char> buffer(
        std::max<std::size_t>(2048, profile_.bootstrap_queries.size() + 1), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s",
                  profile_.bootstrap_queries.c_str());
    if (ImGui::InputTextMultiline("##bootstrap", buffer.data(), buffer.size(),
                                  ImVec2(-1, 90))) {
        profile_.bootstrap_queries = buffer.data();
    }

}

// Formatos de dados (main.dataformat).
//
// O C-Otter exibe numeros, datas e horas COMO O SERVIDOR OS ENVIA, sem
// reformatar. E' uma divergencia deliberada do DBeaver, que tem mascaras por
// tipo -- e a razao esta' na tela, nao so' aqui.
void ConnectionDialog::draw_page_data_formats() {
    ImGui::TextColored(col4(colors().data), TR("Values as the server sends them"));
    ImGui::Separator();

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("Numbers, dates and times appear exactly as the "
                          "server formatted them. Nothing is reformatted on "
                          "the way."));
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("This is deliberate: a client mask hides what is "
                          "really stored. A timestamp shown as 31/12/2025 "
                          "does not say whether the column has a time zone, "
                          "and a number rounded for display hides the scale "
                          "that will be used in a comparison."));
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("To change the format, change it at the source: "
                          "the DateStyle and TimeZone parameters on the "
                          "\"Internal parameters\" page, or a cast in the "
                          "query."));
    ImGui::PopTextWrapPos();
}

// Editor binario (main.resultset.editors).
void ConnectionDialog::draw_page_binary_editor() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Hex dump"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(140);
    ImGui::InputInt(TR("Limit (KB)"), &editor.hex_limit_kb, 16, 64);
    editor.hex_limit_kb = std::clamp(editor.hex_limit_kb, 1, 16384);
    help_marker(TR("How much of a BLOB the value panel formats. Beyond this "
                   "it stops and says how much was left out -- formatting "
                   "200 MB would spend memory nobody reads."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("The panel picks the view from the TYPE: indented "
                          "JSON, hex for binary, a checkbox for boolean."));
}

// Transferencia de dados (main.datatransfer).
//
// Padroes da janela de exportacao. Ela sempre pergunta antes de gravar;
// isto so' decide com que valores abre -- por isso nao ha' risco de
// exportar sem querer no formato errado.
void ConnectionDialog::draw_page_data_transfer() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Export defaults"));
    ImGui::Separator();

    const char* kFormats[] = {"CSV", "JSON", "Markdown", "SQL INSERT"};
    ImGui::SetNextItemWidth(200);
    ImGui::Combo(TR("Format"), &editor.export_format, kFormats,
                 IM_ARRAYSIZE(kFormats));

    ImGui::Checkbox(TR("Write the header row"), &editor.export_write_header);
    help_marker(TR("Column names in the first line. Only applies to CSV -- "
                   "JSON and SQL carry the names in every record."));

    ImGui::SetNextItemWidth(200);
    input_string_hint(TR("Null as"), TR("(empty)"), editor.export_null_text, 32);
    help_marker(TR("Empty is the right choice for re-importing: a reader "
                   "treats an empty field as NULL, while \"[null]\" would "
                   "come back as the literal text."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim), "%s",
                       TR("Import is not available in this version. The "
                          "export window still asks for the path and the "
                          "options before writing."));
}

// Editor de dados / Grade (main.resultset.grid).
void ConnectionDialog::draw_page_data_editor() {
    db::EditorOptions& editor = profile_.editor;

    ImGui::TextColored(col4(colors().data), TR("Null value"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(200);
    input_string(TR("Shown as"), editor.null_text, 32);
    help_marker(TR("How NULL appears in the grid. Leaving it empty is not "
                   "allowed: an empty string and NULL are different values "
                   "in the database, and showing them alike is the classic "
                   "mistake of a SQL client."));

    // Vazio volta ao padrao em vez de aceitar: um NULL indistinguivel de
    // string vazia e' justamente o que o campo existe para evitar.
    if (editor.null_text.empty()) editor.null_text = "[null]";

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Alignment"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Numbers to the right"), &editor.align_numbers_right);
    help_marker(TR("As in a spreadsheet: the decimal point lines up and "
                   "orders of magnitude can be compared at a glance."));
}

// Erros e tempos limite (main.errorHandle).
//
// Timeout, keep-alive e fechar ociosas MORAVAM em "Inicialização". No
// DBeaver eles estao aqui -- e' a pagina que o nome anuncia, e quem procura
// "por que a conexao caiu" procura em "Erros e tempos limite", nao em
// "Inicialização" (diretriz 12).
void ConnectionDialog::draw_page_errors_timeouts() {
    ImGui::TextColored(col4(colors().data), TR("Timeouts"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Connect timeout (s)"), profile_.connect_timeout);
    help_marker(TR("How long to wait for the server to accept the "
                   "connection. Does not limit how long a query may run."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Keep the connection alive"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Keep-alive"), &profile_.keep_alive);
    help_marker(TR("Sends a ping while idle, so a firewall or a proxy does "
                   "not drop the connection for being quiet."));

    if (profile_.keep_alive) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        input_seconds(TR("Interval (s)"), profile_.keep_alive_interval);
    }

    ImGui::Checkbox(TR("Close idle connections"),
                    &profile_.close_idle_connections);
    help_marker(TR("The opposite of keep-alive: releases the connection "
                   "after a while without use. Useful against a server with "
                   "few slots."));

    // Keep-alive e fechar ociosas se contradizem: um mantem viva, o outro
    // fecha. Dizer na tela e' melhor que deixar o usuario descobrir que a
    // conexao cai mesmo com keep-alive ligado.
    if (profile_.keep_alive && profile_.close_idle_connections) {
        ImGui::Spacing();
        icon_inline(Icon::warning, colors().warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(colors().warn), "%s",
                           TR("The two options contradict each other: one "
                              "keeps the connection open, the other closes "
                              "it. Closing wins."));
        ImGui::PopTextWrapPos();
    }
}

void ConnectionDialog::draw_page_general() {
    ImGui::TextColored(col4(colors().data), TR("Identification"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(360);
    input_string(TR("Connection name"), profile_.name, 128);
    help_marker("Vazio usa \"banco@host\".");

    ImGui::SetNextItemWidth(360);
    input_string(TR("Description"), profile_.description, 256);

    ImGui::SetNextItemWidth(260);
    input_string(TR("Folder"), profile_.folder, 128);
    help_marker(TR("Groups the connection in the tree. Use / for subfolders."));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Connection type"));
    ImGui::Separator();

    for (int i = 0; i < 3; ++i) {
        const auto type = static_cast<db::ConnectionType>(i);
        const db::ConnectionTypeInfo& info = db::connection_type_info(type);

        ImGui::PushStyleColor(ImGuiCol_Text, info.color);
        if (ImGui::RadioButton(info.name, profile_.type == type)) {
            profile_.type = type;
            // Produção começa sem auto-commit: alteração acidental exige
            // commit explícito para virar permanente.
            profile_.auto_commit = info.auto_commit;
        }
        ImGui::PopStyleColor();
        if (i < 2) ImGui::SameLine();
    }

    const db::ConnectionTypeInfo& current = db::connection_type_info(profile_.type);
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim),
                       TR("auto-commit: %s | confirm execute: %s | "
                          "confirm data change: %s"),
                       current.auto_commit ? TR("on") : TR("off"),
                       current.confirm_execute ? TR("yes") : TR("no"),
                       current.confirm_data_change ? TR("yes") : TR("no"));

}

} // namespace otter::ui


