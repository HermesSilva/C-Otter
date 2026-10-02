// C-Otter -- ui/editor_commands.cpp
//
// Os comandos do editor SQL: o que cada um FAZ, e os quatro lugares por onde
// se chega a eles -- teclado, barra lateral, menu "SQL Editor" e menu de
// contexto. A tabela (rotulo, tecla por perfil, estado) esta' em
// ui/commands.cpp; as regras de texto, em sql/editing.cpp.
//
// Mapa extraido de plugins/org.jkiss.dbeaver.ui.editors.sql (plugin.xml):
// 57 comandos, 36 atalhos, as barras `sqlEditor.side.top` e `side.bottom`, o
// menu `SQLEditorMenu` e o menu de contexto `SQLEditor.EditorContext`.
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "db/app_tools.hpp"
#include "db/export.hpp"
#include "db/plan.hpp"
#include "sql/editing.hpp"
#include "sql/paging.hpp"
#include "ui/app_window.hpp"   // mono_font()
#include "ui/commands.hpp"
#include "ui/file_dialog.hpp"
#include "ui/hint.hpp"
#include "ui/icon_images.hpp"
#include "ui/platform_open.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // GetTopMostPopupModal

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

// O espacamento da interface, guardado a cada quadro pela barra lateral do
// editor -- o ultimo ponto antes de o widget assumir o desenho. Quem usa e'
// MenuStyleScope, mais abaixo.
ImVec2 g_ui_item_spacing(8.0f, 4.0f);

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

TextEditor::DocPos doc_pos(std::string_view text, std::size_t offset) {
    const sql::TextPosition position = sql::position_of(text, offset);
    return TextEditor::DocPos(position.line, position.column);
}

const std::vector<FileFilter>& script_filters() {
    static const std::vector<FileFilter> kFilters = {
        {"SQL scripts", "*.sql"},
        {"Text files", "*.txt"},
    };
    return kFilters;
}

const std::vector<FileFilter>& plan_filters() {
    static const std::vector<FileFilter> kFilters = {
        {"Execution plan (JSON)", "*.json"},
    };
    return kFilters;
}

// Nome curto para a aba de resultado: "SELECT cliente".
std::string result_title(std::string_view sql, const sql::Dialect& dialect) {
    const std::vector<sql::OutlineEntry> entries = sql::outline(sql, dialect);
    if (entries.empty()) return {};

    std::string title = entries.front().label;
    constexpr std::size_t kMax = 28;
    if (title.size() > kMax) title = title.substr(0, kMax - 3) + "...";
    return title;
}

} // namespace

// --- Perfil de atalhos e preferencias --------------------------------------------

void MainShell::load_settings() {
    // Na primeira execucao o arquivo nasce aqui, com os padroes.
    settings_ = ensure_app_settings(app_settings_path());
    keymap_   = keymap_from_id(settings_.keymap);
    set_icon_set(icon_set_from_id(settings_.icons));
    side_by_side_ = settings_.side_by_side;

    if (!settings_.theme.empty()) set_theme(settings_.theme);
    // Idioma escolhido no menu vale sobre o do ambiente; sem escolha, fica o
    // que main() detectou.
    if (!settings_.language.empty()) i18n::set_language(settings_.language);
}

void MainShell::save_settings() {
    settings_.keymap       = std::string(keymap_id(keymap_));
    settings_.icons        = std::string(icon_set_id(icon_set()));
    settings_.side_by_side = side_by_side_;
    if (!save_app_settings(app_settings_path(), settings_)) {
        show_toast(TR("could not save the preferences"));
    }
}

void MainShell::note_window_placement(const WindowPlacement& placement) {
    if (!placement.saved()) return;

    if (placement != settings_.window) {
        settings_.window   = placement;
        window_changed_at_ = ImGui::GetTime();
        return;
    }
    // Arrastar a borda muda o tamanho a cada quadro: espera a janela parar,
    // em vez de regravar o arquivo sessenta vezes por segundo.
    if (window_changed_at_ >= 0.0 && ImGui::GetTime() - window_changed_at_ > 0.8) {
        window_changed_at_ = -1.0;
        save_settings();
    }
}

void MainShell::flush_window_placement() {
    if (window_changed_at_ < 0.0) return;
    window_changed_at_ = -1.0;
    save_settings();
}

void MainShell::set_keymap(Keymap keymap) {
    keymap_ = keymap;
    save_settings();
}

void MainShell::draw_keymap_menu() {
    if (!ImGui::BeginMenu(TR("Keymap"))) return;

    for (const Keymap keymap : {Keymap::dbeaver, Keymap::otter}) {
        if (ImGui::MenuItem(keymap_label(keymap), nullptr, keymap_ == keymap)) {
            set_keymap(keymap);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(TR("Show shortcuts..."))) show_shortcuts_ = true;
    ImGui::EndMenu();
}

// Conjunto de icones: os originais do DBeaver ou os vetoriais do C-Otter.
void MainShell::draw_icon_set_menu() {
    if (!ImGui::BeginMenu(TR("Icons"))) return;

    if (ImGui::MenuItem("DBeaver", nullptr, icon_set() == IconSet::dbeaver)) {
        set_icon_set(IconSet::dbeaver);
        save_settings();
    }
    if (ImGui::MenuItem("C-Otter", nullptr, icon_set() == IconSet::otter)) {
        set_icon_set(IconSet::otter);
        save_settings();
    }
    ImGui::EndMenu();
}

// As teclas dos dois perfis lado a lado -- a referencia para quem troca de
// perfil, e a prova na tela de que o menu e o teclado leem a mesma tabela.
void MainShell::draw_shortcuts_window() {
    if (!show_shortcuts_) return;

    // Fundo OPACO: a janela flutua sobre a arvore e o editor, e com a
    // translucidez dos paineis o texto de tras atravessava a tabela.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(colors().bg_darkest));
    ImGui::SetNextWindowSize(ImVec2(760, 620), ImGuiCond_Appearing);
    const bool open =
        ImGui::Begin(TRW("Keyboard shortcuts", "###Shortcuts"), &show_shortcuts_);
    ImGui::PopStyleColor();
    if (open) {
        const Palette& p = colors();
        ImGui::TextColored(col4(p.text_dim), TR("Active keymap: %s"),
                           keymap_label(keymap_));
        ImGui::Separator();

        if (ImGui::BeginTable("##keys", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR("Command"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("DBeaver", ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableSetupColumn("C-Otter", ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableHeadersRow();

            const auto keys_text = [](Command command, Keymap keymap) {
                std::string text;
                for (const ImGuiKeyChord chord : command_keys(command, keymap)) {
                    if (!text.empty()) text += ", ";
                    text += chord_label(chord);
                }
                return text;
            };

            for (const CommandInfo& info : all_commands()) {
                if (info.state == CommandState::out_of_scope) continue;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(
                    col4(info.state == CommandState::missing ? p.text_dim : p.text),
                    "%s", TR(info.label));
                if (info.note[0] != '\0' && ImGui::IsItemHovered()) {
                    hint_fmt("%s", TR(info.note));
                }

                for (const Keymap keymap : {Keymap::dbeaver, Keymap::otter}) {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(
                        col4(keymap == keymap_ ? p.text_bright : p.text_dim), "%s",
                        keys_text(info.id, keymap).c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

// Refaz o layout de paineis (o lado do resultado mudou). As janelas voltam a
// ser ancoradas: os ids dos nos de docking sao outros.
void MainShell::rebuild_layout() {
    layout_initialized_ = false;
    for (Connection& connection : connections_) connection.docked = false;
    output_docked_ = variables_docked_ = outline_docked_ = terminal_docked_ = false;
}

// --- Aviso passageiro --------------------------------------------------------------

void MainShell::show_toast(std::string text) {
    toast_text_  = std::move(text);
    toast_until_ = ImGui::GetTime() + 4.0;
}

void MainShell::draw_toast() {
    if (toast_text_.empty() || ImGui::GetTime() > toast_until_) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x - 16.0f,
               vp->WorkPos.y + vp->WorkSize.y - 44.0f),
        ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoInputs;

    if (ImGui::Begin("##toast", nullptr, flags)) {
        ImGui::TextUnformatted(toast_text_.c_str());
    }
    ImGui::End();
}

// --- Dialeto e sessao do documento -------------------------------------------------

const sql::Dialect& MainShell::document_dialect(const SqlDocument& document) const {
    if (const Connection* owner = connection_by_id(document.connection_id())) {
        return sql::dialect_for(owner->profile.driver_id);
    }
    return active_dialect();
}

// --- Estado dos comandos -----------------------------------------------------------

bool MainShell::command_enabled(Command command) {
    if (is_grid_command(command)) return grid_command_enabled(command);
    if (is_app_command(command) || command >= Command::edit_move_lines_up) {
        return app_command_enabled(command);
    }

    const CommandInfo& info = command_info(command);
    if (info.state == CommandState::missing ||
        info.state == CommandState::out_of_scope) {
        return false;
    }

    SqlDocument* document = active_document();

    // A aba de um OBJETO (ui/object_editor.cpp) nao e' um script: o texto
    // dela e' o DDL lido do servidor. Executar, formatar ou comentar ali
    // agiria sobre um texto que o usuario nao escreveu -- para os comandos do
    // editor ela conta como "sem documento".
    const bool is_object = document != nullptr && document->is_object();
    const bool has_doc   = document != nullptr && !is_object;

    Session* target = document != nullptr ? &session_for(*document) : nullptr;
    const bool connected =
        target != nullptr && target->state() == SessionState::connected;
    const bool can_run = connected && !target->busy() && !is_object;

    switch (command) {
        case Command::run_statement:
        case Command::run_statement_new:
        case Command::run_script:
        case Command::run_script_from_position:
        case Command::run_script_new:
        case Command::run_count:
        case Command::run_all_rows:
        case Command::run_expression:
        case Command::export_data:
        case Command::refresh_current_schema:
        case Command::refresh_all_schemas:
            return can_run;

        case Command::explain:
            // So' onde o driver sabe ler o plano: no SQL Server (SHOWPLAN_XML
            // ainda nao lido) o item fica apagado, em vez de mandar um
            // EXPLAIN que o servidor devolve como erro de sintaxe.
            return can_run && target->capabilities().has_value() &&
                   target->capabilities()->explain_plan;

        case Command::cancel_query:
            return connected && target->busy();

        case Command::navigate_object:
            return connected;

        case Command::export_result:
            return has_doc && document->result().has_value();

        case Command::close_result_tab:
        case Command::pin_result_tab:
            return has_doc;

        case Command::load_plan:
        case Command::show_output:
        case Command::show_log:
        case Command::show_variables:
        case Command::show_outline:
        case Command::show_terminal:
        case Command::toggle_result_panel:
        case Command::maximize_result_panel:
        case Command::switch_panel:
        case Command::toggle_layout:
        case Command::new_script:
        case Command::open_script:
        case Command::recent_script:
        case Command::console:
        case Command::sync_auto:
            return true;

        default:
            return has_doc;
    }
}

bool MainShell::command_checked(Command command) {
    if (is_grid_command(command)) return grid_command_checked(command);
    if (is_app_command(command)) return app_command_checked(command);

    SqlDocument* document = active_document();
    const Connection* owner =
        document != nullptr ? connection_by_id(document->connection_id()) : nullptr;

    switch (command) {
        case Command::foldings_enabled:
            return owner != nullptr && owner->profile.editor.code_folding;
        case Command::word_wrap:
            return owner != nullptr && owner->profile.editor.word_wrap;
        case Command::disable_syntax:
            return document != nullptr && document->editor().GetLanguage() == nullptr;
        case Command::show_output:    return show_output_;
        case Command::show_log:       return show_log_;
        case Command::show_variables: return show_variables_;
        case Command::show_outline:   return show_outline_;
        case Command::show_terminal:  return show_terminal_;
        case Command::toggle_result_panel:   return !results_hidden_;
        case Command::maximize_result_panel: return results_maximized_;
        case Command::toggle_layout:         return side_by_side_;
        case Command::sync_auto:             return sync_auto_;
        case Command::pin_result_tab:
            return document != nullptr &&
                   document->result_tab(document->active_result_tab()).pinned;
        default:
            return false;
    }
}

// --- Teclado -----------------------------------------------------------------------

// Registra as teclas do contexto e executa o comando da que foi apertada.
//
// RouteGlobal | RouteOverFocused, e nao IsKeyChordPressed: o widget do editor
// tambem le' teclas, com rota "focada", e as duas leituras disparavam juntas
// -- Ctrl+Enter executava a consulta E inseria uma linha em branco embaixo
// dela, Ctrl+Shift+F formatava E abria a busca. A rota global-sobre-focada
// ganha da do widget, que entao nao ve' a tecla.
//
// E' por isso que o contexto `editor` so' e' despachado com o editor em
// foco: com a rota tomada o tempo todo, Ctrl+D da grade ("copiar da linha de
// cima") deixaria de chegar a ela.
void MainShell::dispatch_shortcuts(CommandContext context) {
    // Com um dialogo modal aberto, a tecla e' dele.
    if (ImGui::GetTopMostPopupModal() != nullptr) return;

    constexpr ImGuiInputFlags kRoute =
        ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused;

    for (const CommandInfo& info : all_commands()) {
        if (info.context != context) continue;
        if (info.state == CommandState::missing ||
            info.state == CommandState::out_of_scope) {
            continue;
        }

        // Navegar entre consultas repete com a tecla segurada; o resto nao --
        // executar um script trinta vezes por segundo nao e' o que se quer.
        const bool repeat = info.id == Command::query_next ||
                            info.id == Command::query_prev ||
                            info.id == Command::delete_line ||
                            info.id == Command::grid_row_next ||
                            info.id == Command::grid_row_previous ||
                            info.id == Command::grid_move_left ||
                            info.id == Command::grid_move_right ||
                            info.id == Command::grid_zoom_in ||
                            info.id == Command::grid_zoom_out;

        for (const ImGuiKeyChord chord : command_keys(info.id, keymap_)) {
            // Tecla sem Ctrl nem Alt (Delete, F2, Enter) com um campo de
            // texto em foco e' do campo: o filtro da arvore usa Delete para
            // apagar letra.
            if ((chord & (ImGuiMod_Ctrl | ImGuiMod_Alt)) == 0 &&
                ImGui::GetIO().WantTextInput &&
                (context == CommandContext::navigator ||
                 context == CommandContext::global)) {
                continue;
            }
            if (!ImGui::Shortcut(chord,
                                 kRoute | (repeat ? ImGuiInputFlags_Repeat : 0))) {
                continue;
            }
            if (command_enabled(info.id)) run_command(info.id);
        }
    }
}

// Um item de menu a partir da tabela: rotulo, tecla do perfil ativo, marca e
// habilitacao. O comando e' ENFILEIRADO: o menu de contexto e' desenhado de
// dentro do Render() do editor, e mexer no texto ali e' mexer no documento
// no meio do desenho dele.
bool MainShell::command_menu_item(Command command) {
    const CommandInfo& info = command_info(command);
    if (info.state == CommandState::out_of_scope) return false;

    const std::string shortcut = command_shortcut(command, keymap_);
    const bool enabled = command_enabled(command);

    const bool clicked =
        ImGui::MenuItem(TR(info.label), shortcut.empty() ? nullptr : shortcut.c_str(),
                        command_checked(command), enabled);

    // O que difere do DBeaver, ou por que esta' desabilitado, na propria
    // tela (diretiva 6).
    if (info.note[0] != '\0' &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        hint_fmt("%s", TR(info.note));
    }

    if (clicked) queued_commands_.push_back(command);
    return clicked;
}

// OTTER_COMMAND_FILE=<caminho>: a cada poucos quadros o arquivo e' lido,
// apagado, e cada linha executada como se o item de menu tivesse sido
// escolhido. Uma linha e' o ROTULO do comando em ingles ("Select All"), ou:
//
//     select <linha> <coluna>     seleciona uma celula da grade (base zero)
//     extend <linha> <coluna>     estende o bloco ate' ela (como Shift+clique)
//     sql <texto>                 poe o texto no editor e executa
//     text <texto>                so' poe o texto (\n = quebra); para scripts
//     type <texto>                insere no cursor, como digitar (\n = quebra)
//     filter <condicao>           aplica a condicao da barra de filtro
//     value <texto>               edita a celula corrente (como digitar nela)
//     document <n>                ativa o n-esimo script (base zero)
//     object ..., form ..., ddl ...   o editor de objeto: ver object_command
//
// Com estes a conferencia nao precisa de clique nem de tecla: o programa nem
// precisa estar na frente (tools/screenshot.ps1 fotografa a janela coberta).
//
// Existe porque a automacao de teclado nao e' confiavel aqui: SendKeys nao
// entrega setas, e combinacoes com Ctrl+Alt chegam trocadas ou atrasadas. O
// arquivo exercita o comando de verdade -- habilitacao, fila e execucao --, e
// o que se confere e' o efeito: a area de transferencia, a captura.
//
// NAO prova que a TECLA chega ao comando; isso e' da tabela (ui/commands.cpp)
// e do teste dela.
void MainShell::poll_command_file() {
    static const char* path = std::getenv("OTTER_COMMAND_FILE");
    if (path == nullptr || path[0] == '\0') return;
    if (ImGui::GetFrameCount() % 6 != 0) return;

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        if (!in) return;
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty()) lines.push_back(std::move(line));
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);

    for (const std::string& line : lines) {
        const bool select = line.starts_with("select ");
        const bool extend = line.starts_with("extend ");
        if (select || extend) {
            unsigned long long row = 0;
            unsigned long long column = 0;
            if (std::sscanf(line.c_str() + 7, "%llu %llu", &row, &column) == 2) {
                if (SqlDocument* document = active_document()) {
                    select_grid_cell(*document, static_cast<std::size_t>(row),
                                     static_cast<std::size_t>(column), extend);
                }
            }
            continue;
        }

        if (line.starts_with("sql ")) {
            if (SqlDocument* document = active_document()) {
                const std::string sql = line.substr(4);
                document->editor().SetText(sql);
                run_sql(*document, sql, RunMode::same_tab);
            }
            continue;
        }
        // text <conteudo>: so' poe o texto no editor (\n = quebra de linha).
        // Quem executa e' a linha seguinte ("Execute SQL script"), pelo
        // caminho do comando -- e' assim que se confere um script de varias
        // linhas, que nao cabe em "sql".
        if (line.starts_with("text ")) {
            if (SqlDocument* document = active_document();
                document != nullptr && !document->is_object()) {
                std::string text = line.substr(5);
                for (std::size_t at = 0; (at = text.find("\\n", at)) != std::string::npos;) {
                    text.replace(at, 2, "\n");
                }
                document->editor().SetText(text);
            }
            continue;
        }
        // type <conteudo>: insere no cursor como se fosse digitado (\n =
        // quebra) -- uma edicao de verdade, que anda o indice de desfazer.
        // "text" troca o texto inteiro sem passar pelo desfazer, e por isso
        // nao aciona a gravacao automatica; este aciona.
        if (line.starts_with("type ")) {
            if (SqlDocument* document = active_document();
                document != nullptr && !document->is_object()) {
                std::string text = line.substr(5);
                for (std::size_t at = 0; (at = text.find("\\n", at)) != std::string::npos;) {
                    text.replace(at, 2, "\n");
                }
                document->editor().ReplaceTextInCurrentCursor(text);
            }
            continue;
        }
        if (line.starts_with("filter ")) {
            if (SqlDocument* document = active_document()) {
                const std::string condition = line.substr(7);
                GridView& view = document->grid_view();
                std::snprintf(view.filter_text, sizeof view.filter_text, "%s",
                              condition.c_str());
                sql::ColumnFilter filter = document->filter();
                filter.condition = condition;
                apply_grid_filter(*document, std::move(filter));
            }
            continue;
        }
        if (line.starts_with("value ")) {
            if (SqlDocument* document = active_document();
                document != nullptr && has_selection_ &&
                document->edit_target().editable()) {
                document->edits().set(selected_row_, selected_column_, line.substr(6));
            }
            continue;
        }
        if (line.starts_with("document ")) {
            const auto index = static_cast<std::size_t>(
                std::strtoull(line.c_str() + 9, nullptr, 10));
            if (index < documents_.size()) active_document_ = index;
            continue;
        }

        // Editor de objeto, dialogos e a confirmacao de DDL
        // (ui/object_editor.cpp).
        if (object_command(line)) continue;
        // Navegador e janelas da aplicacao (ui/app_commands.cpp).
        if (app_command_line(line)) continue;

        bool found = false;
        for (const CommandInfo& info : all_commands()) {
            if (line == info.label) {
                // Executado JA', na ordem do arquivo: enfileirar deixaria
                // todos os "select" rodarem antes do primeiro comando, e os
                // comandos agiriam todos sobre a ultima celula.
                if (command_enabled(info.id)) run_command(info.id);
                else show_toast("OTTER_COMMAND_FILE: disabled: " + line);
                found = true;
                break;
            }
        }
        if (!found) show_toast("OTTER_COMMAND_FILE: unknown command: " + line);
    }
}

void MainShell::run_queued_commands() {
    if (queued_commands_.empty()) return;

    const std::vector<Command> commands = std::move(queued_commands_);
    queued_commands_.clear();
    for (const Command command : commands) {
        if (command_enabled(command)) run_command(command);
    }
}

// --- Execucao ----------------------------------------------------------------------

// `@set x = 1`, `@unset x`, `@echo texto`: comandos do CLIENTE, que nao vao
// ao servidor. Verdadeiro se a instrucao era um deles.
bool MainShell::handle_control_command(SqlDocument& document,
                                       std::string_view statement) {
    const sql::ControlCommand command = sql::parse_control_command(statement);

    switch (command.kind) {
        case sql::ControlCommand::Kind::none:
            return false;

        case sql::ControlCommand::Kind::set:
            // O valor passa pela expansao: `@set b = ${a}0` usa o `a` atual.
            document.variables()[command.name] =
                sql::expand_variables(command.value, document.variables());
            break;

        case sql::ControlCommand::Kind::unset:
            document.variables().erase(command.name);
            break;

        case sql::ControlCommand::Kind::echo:
            session_for(document).append_output(
                sql::expand_variables(command.value, document.variables()));
            show_output_ = true;
            break;
    }
    return true;
}

// Executa UMA instrucao no documento, paginada (ADR 0011).
void MainShell::run_sql(SqlDocument& document, std::string sql, RunMode mode) {
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;

    if (sql.empty()) {
        show_toast(TR("no statement at the cursor"));
        return;
    }

    if (handle_control_command(document, sql)) {
        show_toast(TR("client command applied"));
        return;
    }
    sql = sql::expand_variables(sql, document.variables());

    // Aba nova quando pedida, e tambem quando a atual esta' FIXADA: fixar e'
    // dizer "nao sobrescreva este resultado".
    const bool pinned = document.result_tab(document.active_result_tab()).pinned;
    if (mode == RunMode::new_tab || pinned) {
        // A unica aba, ainda vazia, e' reaproveitada: abrir "Result 2" ao
        // lado de uma "Result 1" em branco seria ruido.
        const bool reuse = !pinned && document.result_tab_count() == 1 &&
                           !document.result().has_value();
        if (!reuse) document.add_result_tab();
    }
    document.set_result_tab_title(document.active_result_tab(),
                                  result_title(sql, document_dialect(document)));
    force_result_tab_ = document.active_result_tab_id();

    // Nova consulta: volta para a primeira pagina e descarta a ordenacao --
    // a consulta nova pode nem ter aquela coluna.
    document.set_paged_sql(sql);
    document.set_page(0);
    document.set_sort({});
    document.set_filter({});
    // Colunas escondidas e cores de linha eram das colunas da consulta
    // anterior.
    document.grid_view().reset_for_new_query();

    if (mode == RunMode::all_rows) fetch_all_rows(document);
    else                           execute_page(document, 0);
}

// As instrucoes de [from, fim) em ordem, com os comandos de cliente ja'
// aplicados e as variaveis expandidas.
std::vector<std::string> MainShell::prepare_script(SqlDocument& document,
                                                   std::size_t from_offset) {
    const std::string text = document.editor().GetText();
    const sql::Dialect& dialect = document_dialect(document);

    std::vector<std::string> statements;
    for (const sql::StatementRange& range : sql::statement_ranges(text, dialect)) {
        if (range.end < from_offset) continue;

        const std::string_view statement =
            std::string_view(text).substr(range.begin, range.end - range.begin);

        // Na ordem do script: um `@set` vale para as instrucoes DEPOIS dele.
        if (handle_control_command(document, statement)) continue;
        statements.push_back(
            sql::expand_variables(statement, document.variables()));
    }
    return statements;
}

void MainShell::run_script(SqlDocument& document, std::size_t from_offset,
                           bool separate_tabs) {
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;

    std::vector<std::string> statements = prepare_script(document, from_offset);
    if (statements.empty()) return;

    if (separate_tabs) {
        // Uma aba de resultado por consulta, uma depois da outra: a sessao
        // executa um comando por vez.
        for (std::string& statement : statements) {
            run_queue_.push_back({document.id(), std::move(statement)});
        }
        return;
    }

    // Um comando so': o caminho normal, que pagina o resultado.
    if (statements.size() == 1) {
        run_sql(document, std::move(statements.front()), RunMode::same_tab);
        return;
    }

    if (document.result_tab(document.active_result_tab()).pinned) {
        document.add_result_tab();
    }
    force_result_tab_ = document.active_result_tab_id();

    executing_document_id_ = document.id();
    document.set_executing(true);
    document.set_status({});

    // Script nao e' paginado: os botoes de pagina somem, e a contagem
    // exibida passa a ser a do resultado inteiro do ultimo SELECT.
    document.reset_paging();

    const Connection* owner = connection_by_id(document.connection_id());
    const bool stop_on_error =
        owner == nullptr || owner->profile.editor.stop_script_on_error;

    target.execute_script_async(std::move(statements), stop_on_error);
}

// A fila de "Execute queries in separate tabs": dispara a proxima quando a
// anterior terminou.
void MainShell::process_run_queue() {
    if (run_queue_.empty()) return;
    if (executing_document_id_ != 0 || counting_document_id_ != 0) return;

    SqlDocument* document = nullptr;
    for (std::unique_ptr<SqlDocument>& candidate : documents_) {
        if (candidate->id() == run_queue_.front().document_id) {
            document = candidate.get();
            break;
        }
    }

    // A aba foi fechada, ou a conexao caiu: o que restava da fila nao tem
    // onde rodar.
    if (document == nullptr ||
        session_for(*document).state() != SessionState::connected) {
        run_queue_.clear();
        return;
    }
    if (session_for(*document).busy()) return;

    std::string sql = std::move(run_queue_.front().sql);
    run_queue_.pop_front();
    run_sql(*document, std::move(sql), RunMode::new_tab);
}

// --- Os comandos -------------------------------------------------------------------

void MainShell::run_command(Command command) {
    if (is_grid_command(command)) {
        run_grid_command(command);
        return;
    }
    if (is_app_command(command) || command >= Command::edit_move_lines_up) {
        run_app_command(command);
        return;
    }
    if (command == Command::run_script_native) {
        run_script_native();
        return;
    }
    if (command == Command::ddl_by_result) {
        // CREATE TABLE com as colunas do resultado, numa aba nova.
        if (SqlDocument* document = active_document();
            document != nullptr && document->result()) {
            const bool mysql = &document_dialect(*document) == &sql::mysql_dialect();
            open_sql_tab(db::create_table_from_result(*document->result(),
                                                      "new_table", mysql),
                         /*run=*/false);
        }
        return;
    }

    SqlDocument* document = active_document();

    // Os que nao dependem de documento.
    switch (command) {
        case Command::new_script:
        case Command::console:
            // Quem abre um script pelo teclado vai digitar em seguida. Sem
            // isto a aba aparecia e as teclas nao iam a lugar nenhum.
            focus_document_id_ = new_document().id();
            focus_editor_      = true;
            return;
        case Command::open_script:
            open_script_file();
            return;
        case Command::recent_script: {
            // O ultimo script da conexao ativa; sem nenhum, um novo.
            const std::size_t connection_id =
                active_connection_ < connections_.size()
                    ? connections_[active_connection_].id
                    : 0;
            for (std::size_t i = documents_.size(); i-- > 0;) {
                if (documents_[i]->connection_id() == connection_id) {
                    active_document_ = i;
                    focus_editor_    = true;
                    return;
                }
            }
            new_document();
            return;
        }
        case Command::show_output:    show_output_ = !show_output_;       return;
        case Command::show_log:       show_log_ = !show_log_;             return;
        case Command::show_variables: show_variables_ = !show_variables_; return;
        case Command::show_outline:   show_outline_ = !show_outline_;     return;
        case Command::show_terminal:  show_terminal_ = !show_terminal_;   return;
        case Command::toggle_result_panel:
            results_hidden_    = !results_hidden_;
            results_maximized_ = false;
            // Dois quadros: no primeiro as janelas voltam a existir, no
            // segundo ja' podem receber o foco.
            if (!results_hidden_) reselect_result_ = 2;
            return;
        case Command::maximize_result_panel:
            results_maximized_ = !results_maximized_;
            results_hidden_    = false;
            return;
        case Command::switch_panel:
            if (editor_has_focus_) ImGui::SetWindowFocus("###ResultPanel");
            else                   focus_editor_ = true;
            return;
        case Command::toggle_layout:
            side_by_side_ = !side_by_side_;
            rebuild_layout();
            save_settings();
            return;
        case Command::sync_auto:
            sync_auto_ = !sync_auto_;
            return;
        case Command::load_plan: {
            const auto path = open_file_dialog(TR("Load Execution Plan"), plan_filters());
            if (!path) return;

            std::ifstream file(*path, std::ios::binary);
            std::ostringstream buffer;
            buffer << file.rdbuf();

            auto parsed = db::parse_plan_json(buffer.str());
            if (!parsed) {
                show_toast(std::string(TR("not an execution plan: ")) +
                           parsed.error().to_string());
                return;
            }
            plan_         = std::move(*parsed);
            plan_analyze_ = false;
            show_plan_    = true;
            return;
        }
        default:
            break;
    }

    if (document == nullptr) return;

    TextEditor&         editor  = document->editor();
    const sql::Dialect& dialect = document_dialect(*document);
    Connection*         owner   = connection_by_id(document->connection_id());

    switch (command) {
        // --- Execucao ---------------------------------------------------------
        case Command::run_statement:
            run_sql(*document, document->sql_to_execute(dialect), RunMode::same_tab);
            break;

        case Command::run_statement_new:
            run_sql(*document, document->sql_to_execute(dialect), RunMode::new_tab);
            break;

        case Command::run_script:
            run_script(*document, 0, /*separate_tabs=*/false);
            break;

        case Command::run_script_from_position: {
            // A partir da instrucao SOB o cursor, inclusive.
            const std::string text = editor.GetText();
            const auto range =
                sql::statement_range_at(text, dialect, document->cursor_offset());
            run_script(*document, range ? range->begin : 0, false);
            break;
        }

        case Command::run_script_new:
            run_script(*document, 0, /*separate_tabs=*/true);
            break;

        case Command::cancel_query:
            if (auto status = session_for(*document).cancel_query(); !status) {
                show_toast(status.error().to_string());
            }
            break;

        case Command::run_count: {
            const std::string sql = sql::expand_variables(
                document->sql_to_execute(dialect), document->variables());
            if (sql.empty()) break;

            const sql::PagedQuery counted = sql::make_count_query(sql, dialect, {});
            if (!counted.rewritten) {
                show_toast(TR("this statement cannot be counted"));
                break;
            }
            // O resultado chega pelo canal da grade; a marca impede que a
            // celula com o numero substitua as linhas exibidas.
            counting_document_id_ = document->id();
            count_to_toast_       = true;
            document->set_executing(true);
            session_for(*document).execute_async(counted.sql);
            break;
        }

        case Command::run_all_rows:
            run_sql(*document, document->sql_to_execute(dialect), RunMode::all_rows);
            break;

        case Command::run_expression: {
            // A expressao selecionada (ou a instrucao) dentro de um SELECT.
            std::string expression = document->sql_to_execute(dialect);
            while (!expression.empty() &&
                   (expression.back() == ';' ||
                    std::isspace(static_cast<unsigned char>(expression.back())))) {
                expression.pop_back();
            }
            if (expression.empty()) break;
            run_sql(*document, "SELECT " + expression, RunMode::same_tab);
            break;
        }

        case Command::explain:
            explain_current_sql(/*analyze=*/false);
            break;

        case Command::export_data:
            // Sem limite de linhas, e a janela de exportacao abre quando o
            // resultado chegar.
            export_after_run_ = document->id();
            run_sql(*document, document->sql_to_execute(dialect), RunMode::all_rows);
            break;

        // --- Navegacao --------------------------------------------------------
        case Command::query_next:
        case Command::query_prev: {
            const std::string text = editor.GetText();
            const std::size_t at = document->cursor_offset();
            const auto target =
                command == Command::query_next
                    ? sql::next_statement_start(text, dialect, at)
                    : sql::previous_statement_start(text, dialect, at);
            if (!target) break;

            const TextEditor::DocPos position = doc_pos(text, *target);
            editor.SetCursor(position);
            editor.ScrollToLine(position.line);
            break;
        }

        case Command::goto_bracket:
        case Command::select_to_bracket: {
            const std::string text = editor.GetText();
            const std::size_t at = document->cursor_offset();
            const auto match = sql::matching_bracket(text, dialect, at);
            if (!match) break;

            if (command == Command::goto_bracket) {
                editor.SetCursor(doc_pos(text, *match));
            } else {
                // Do cursor ate' o par, com o colchete de destino dentro.
                const std::size_t from = (std::min)(at, *match);
                const std::size_t to   = (std::max)(at, *match + 1);
                editor.SelectRegion(doc_pos(text, from), doc_pos(text, to));
            }
            break;
        }

        case Command::navigate_object: {
            // O identificador sob o cursor.
            const TextEditor::DocPos cursor = editor.GetCurrentCursorPosition();
            const std::string word = editor.GetSectionText(
                editor.FindWordStart(cursor), editor.FindWordEnd(cursor));
            if (word.empty()) break;

            const auto iequals = [](std::string_view a, std::string_view b) {
                return a.size() == b.size() &&
                       std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                           return std::tolower(static_cast<unsigned char>(x)) ==
                                  std::tolower(static_cast<unsigned char>(y));
                       });
            };

            Session& target = session_for(*document);
            for (const db::SchemaMeta& schema : target.schemas()) {
                for (const db::TableMeta& relation : schema.tables) {
                    if (!iequals(relation.name, word)) continue;

                    tree_reveal_.connection_id = document->connection_id();
                    tree_reveal_.schema        = schema.name;
                    tree_reveal_.relation      = relation.name;
                    tree_reveal_.frames        = kRevealFrames;
                    ImGui::SetWindowFocus("###NavigatorPanel");
                    return;
                }
            }
            show_toast(std::string(TRF("\"%s\" is not a table or view of this "
                                       "connection", word.c_str())));
            break;
        }

        case Command::goto_line:
            goto_line_open_ = true;
            std::snprintf(goto_line_buffer_, sizeof goto_line_buffer_, "%zu",
                          editor.GetCurrentCursorPosition().line + 1);
            break;

        // --- Agrupamentos -----------------------------------------------------
        case Command::foldings_enabled:
            if (owner != nullptr) {
                owner->profile.editor.code_folding =
                    !owner->profile.editor.code_folding;
            }
            break;

        case Command::expand_all_foldings:
            editor.UnfoldAll();
            break;

        case Command::collapse_all_foldings:
            // De baixo para cima: recolher uma linha esconde as de dentro, e
            // subindo cada bloco ainda esta' visivel quando chega a vez dele.
            for (std::size_t line = editor.GetLineCount(); line-- > 0;) {
                if (editor.IsLineFoldable(line) && !editor.IsLineFolded(line)) {
                    editor.ToggleAtLine(line);
                }
            }
            break;

        // --- Edicao -----------------------------------------------------------
        case Command::comment_single:
            editor.ToggleComments();
            break;

        case Command::comment_block:
            // Sem selecao, vale para a linha do cursor.
            if (!editor.AnyCursorHasSelection()) {
                editor.SelectLine(editor.GetCurrentCursorPosition().line);
            }
            editor.FilterSelections([](std::string_view text) {
                return sql::toggle_block_comment(text);
            });
            break;

        case Command::word_wrap:
            if (owner != nullptr) {
                owner->profile.editor.word_wrap = !owner->profile.editor.word_wrap;
            }
            break;

        case Command::format:
            format_current_sql();
            break;

        case Command::morph_delimited:
            if (!editor.AnyCursorHasSelection()) {
                show_toast(TR("select the text to morph first"));
                break;
            }
            morph_open_ = true;
            break;

        case Command::trim_spaces:
        case Command::trim_leading:
        case Command::trim_trailing: {
            const bool leading  = command != Command::trim_trailing;
            const bool trailing = command != Command::trim_leading;
            const auto trim = [leading, trailing](std::string_view text) {
                return sql::trim_lines(text, leading, trailing);
            };
            // Com selecao, so' ela; sem, o documento inteiro.
            if (editor.AnyCursorHasSelection()) editor.FilterSelections(trim);
            else                                editor.FilterLines(trim);
            break;
        }

        case Command::to_upper:
            editor.SelectionToUpperCase();
            break;

        case Command::to_lower:
            editor.SelectionToLowerCase();
            break;

        case Command::delete_line: {
            // Comprimento da linha em CARACTERES, que e' como o editor
            // enderea a coluna.
            const auto length = [&editor](std::size_t line) {
                const std::string text = editor.GetLineText(line);
                return sql::position_of(text, text.size()).column;
            };

            const std::size_t line  = editor.GetCurrentCursorPosition().line;
            const std::size_t count = editor.GetLineCount();
            if (line + 1 < count) {
                editor.ReplaceSectionText(TextEditor::DocPos(line, 0),
                                          TextEditor::DocPos(line + 1, 0), "");
            } else if (line > 0) {
                // A ultima linha leva a quebra de ANTES dela.
                editor.ReplaceSectionText(
                    TextEditor::DocPos(line - 1, length(line - 1)),
                    TextEditor::DocPos(line, length(line)), "");
            } else {
                editor.ReplaceSectionText(TextEditor::DocPos(0, 0),
                                          TextEditor::DocPos(0, length(0)), "");
            }
            break;
        }

        case Command::templates:
            templates_open_ = true;
            break;

        case Command::copy_query: {
            const std::string sql = document->sql_to_execute(dialect);
            if (sql.empty()) break;
            ImGui::SetClipboardText(sql.c_str());
            show_toast(TR("query copied"));
            break;
        }

        case Command::search_web: {
            std::string text = editor.CurrentCursorHasSelection()
                                   ? editor.GetSectionText(editor.GetCurrentCursorSelection())
                                   : std::string{};
            if (text.empty()) {
                const TextEditor::DocPos cursor = editor.GetCurrentCursorPosition();
                text = editor.GetSectionText(editor.FindWordStart(cursor),
                                             editor.FindWordEnd(cursor));
            }
            if (text.empty()) break;

            // Limite: uma selecao de script inteiro nao e' uma busca.
            if (text.size() > 200) text.resize(200);
            if (!open_url("https://www.google.com/search?q=" + url_encode(text))) {
                show_toast(TR("could not open the browser"));
            }
            break;
        }

        case Command::disable_syntax:
            editor.SetLanguage(editor.GetLanguage() == nullptr
                                   ? TextEditor::Language::Sql()
                                   : nullptr);
            break;

        // --- Script e arquivo -------------------------------------------------
        case Command::import_script: {
            const auto path = open_file_dialog(TR("Import SQL script"), script_filters());
            if (!path) break;

            std::ifstream file(*path, std::ios::binary);
            if (!file) {
                show_toast(std::string(TRF("cannot open %s", path->c_str())));
                break;
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();

            // Substitui o conteudo pelo caminho que respeita o desfazer:
            // importar por cima do script errado precisa de um Ctrl+Z.
            editor.SelectAll();
            editor.ReplaceTextInAllCursors(buffer.str());
            break;
        }

        case Command::export_script: {
            const auto path = save_file_dialog(TR("Export SQL script"),
                                               script_filters(),
                                               document->title() + ".sql");
            if (!path) break;

            // Exportar NAO amarra o documento ao arquivo -- e' uma copia. Para
            // isso existe "Save as".
            std::ofstream file(*path, std::ios::binary | std::ios::trunc);
            const std::string text = editor.GetText();
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            show_toast(file ? std::string(TRF("exported to %s", path->c_str()))
                            : std::string(TRF("cannot write %s", path->c_str())));
            break;
        }

        case Command::rename_script:
            renaming_document_ = document->id();
            std::snprintf(rename_buffer_, sizeof rename_buffer_, "%s",
                          document->title().c_str());
            break;

        case Command::delete_script:
            delete_script_id_ = document->id();
            break;

        // --- Contexto ---------------------------------------------------------
        case Command::sync_connection:
            if (tree_selected_connection_id_ != 0 &&
                connection_by_id(tree_selected_connection_id_) != nullptr) {
                document->set_connection_id(tree_selected_connection_id_);
            } else {
                show_toast(TR("select a connection in the navigator first"));
            }
            break;

        case Command::refresh_current_schema:
        case Command::refresh_all_schemas:
            session_for(*document).reload_catalog_async();
            break;

        // --- Abas de resultado ------------------------------------------------
        case Command::close_result_tab:
            document->close_result_tab(document->active_result_tab());
            force_result_tab_ = document->active_result_tab_id();
            break;

        case Command::pin_result_tab: {
            const std::size_t index = document->active_result_tab();
            document->set_result_tab_pinned(index,
                                            !document->result_tab(index).pinned);
            break;
        }

        case Command::export_result:
            show_export_ = true;
            break;

        default:
            break;
    }
}

// --- Barra lateral -----------------------------------------------------------------

// Os botoes do `sqlEditor.side.top` e `side.bottom` do DBeaver, na ordem e
// com a visibilidade padrao da extensao `toolBarConfiguration`: em cima
// executar, executar em nova aba, executar script, plano, IA e terminal;
// embaixo os quatro paineis.
void MainShell::draw_editor_side_toolbar(float height) {
    g_ui_item_spacing = ImGui::GetStyle().ItemSpacing;
    const Palette& p = colors();

    constexpr Command kTop[] = {
        Command::run_statement, Command::run_statement_new, Command::run_script,
        Command::explain,       Command::ai_assistant,      Command::show_terminal,
    };
    constexpr Command kBottom[] = {
        Command::show_output, Command::show_log, Command::show_variables,
        Command::show_outline,
    };

    // Menor que o botao da barra principal: sao dez em coluna, e no tamanho
    // cheio os quatro de baixo ficavam fora da area do editor.
    const float button  = ImGui::GetFontSize() + 10.0f;
    const float spacing = 2.0f;

    // Sem barra de rolagem, mas a roda do mouse rola: numa janela muito baixa
    // os botoes de baixo continuam alcancaveis.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, spacing));
    ImGui::BeginChild("##sidebar", ImVec2(button + 4.0f, height),
                      ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    const auto side_button = [&](Command command) {
        const CommandInfo& info = command_info(command);

        std::string tooltip = TR(info.label);
        const std::string shortcut = command_shortcut(command, keymap_);
        if (!shortcut.empty()) tooltip += "  (" + shortcut + ")";
        // O que difere do DBeaver -- ou, no botao desabilitado, o motivo --
        // aparece no proprio botao (diretiva 6).
        if (info.note[0] != '\0') tooltip += std::string("\n") + TR(info.note);

        const bool enabled = command_enabled(command);
        const std::string id = "##side" + std::to_string(static_cast<int>(command));
        if (icon_button(id.c_str(), info.icon, tooltip.c_str(), enabled,
                        command_checked(command) ? p.accent : 0, button)) {
            run_command(command);
        }
    };

    for (const Command command : kTop) side_button(command);

    // Os de baixo encostam no fim da barra, como no DBeaver.
    const float bottom_height =
        static_cast<float>(std::size(kBottom)) * (button + spacing);
    const float bottom_y = height - bottom_height;
    if (bottom_y > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(bottom_y);

    for (const Command command : kBottom) side_button(command);

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// --- Menus -------------------------------------------------------------------------

namespace {

// Os menus de contexto do editor sao preenchidos de DENTRO do desenho do
// widget, que a essa altura empilhou a fonte mono e ItemSpacing zero. Sem
// desfazer os dois, o menu sai em fonte de codigo, com as linhas coladas e o
// atalho encostado no rotulo ("Select to the matching bracketCtrl+Shift+]")
// -- foi assim que apareceu na primeira captura.
struct MenuStyleScope {
    MenuStyleScope() {
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0], 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, g_ui_item_spacing);

        // A distancia entre rotulo, atalho e marca e' calculada no Begin()
        // do popup -- que o widget ja' chamou, com espacamento zero. Trocar
        // o estilo agora nao a alcanca; as colunas sao corrigidas direto:
        // `Spacing` entra na largura pedida, e os deslocamentos (ja'
        // calculados para este quadro) ganham o espaco que faltou.
        ImGuiMenuColumns& columns = ImGui::GetCurrentWindow()->DC.MenuColumns;
        const ImU16 spacing = static_cast<ImU16>(g_ui_item_spacing.x);
        if (columns.Spacing == 0) {
            columns.Spacing = spacing;
            columns.OffsetShortcut =
                static_cast<ImU16>(columns.OffsetShortcut + spacing);
            columns.OffsetMark =
                static_cast<ImU16>(columns.OffsetMark + 2 * spacing);
            // Sem isto a janela fica dois espacos mais estreita que as
            // colunas, e a seta dos submenus sai cortada na borda.
            columns.TotalWidth += static_cast<ImU32>(2 * spacing);
        }
    }
    ~MenuStyleScope() {
        ImGui::PopStyleVar();
        ImGui::PopFont();
    }
    MenuStyleScope(const MenuStyleScope&)            = delete;
    MenuStyleScope& operator=(const MenuStyleScope&) = delete;
};

} // namespace

// O menu de contexto do editor (popup `SQLEditor.EditorContext`).
void MainShell::draw_editor_context_menu() {
    SqlDocument* document = active_document();
    if (document == nullptr) return;
    TextEditor& editor = document->editor();
    const MenuStyleScope menu_style;

    // Recortar / copiar / colar agem na hora: sao do widget, e ele sabe
    // lidar com isso de dentro do proprio desenho.
    if (ImGui::MenuItem(TR("Cut"), "Ctrl+X", false, editor.AnyCursorHasSelection())) {
        editor.Cut();
    }
    if (ImGui::MenuItem(TR("Copy"), "Ctrl+C", false, editor.AnyCursorHasSelection())) {
        editor.Copy();
    }
    if (ImGui::MenuItem(TR("Paste"), "Ctrl+V")) editor.Paste();
    command_menu_item(Command::copy_query);
    command_menu_item(Command::search_web);

    ImGui::Separator();
    if (ImGui::BeginMenu(TR("Execute"))) {
        command_menu_item(Command::run_statement);
        command_menu_item(Command::run_statement_new);
        command_menu_item(Command::run_script);
        command_menu_item(Command::run_script_from_position);
        command_menu_item(Command::run_script_new);
        command_menu_item(Command::cancel_query);
        ImGui::Separator();
        command_menu_item(Command::run_count);
        command_menu_item(Command::run_all_rows);
        command_menu_item(Command::run_expression);
        command_menu_item(Command::export_data);
        ImGui::Separator();
        command_menu_item(Command::explain);
        command_menu_item(Command::load_plan);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Format"))) {
        command_menu_item(Command::format);
        command_menu_item(Command::morph_delimited);
        ImGui::Separator();
        command_menu_item(Command::to_upper);
        command_menu_item(Command::to_lower);
        ImGui::Separator();
        command_menu_item(Command::trim_spaces);
        command_menu_item(Command::trim_leading);
        command_menu_item(Command::trim_trailing);
        ImGui::Separator();
        command_menu_item(Command::comment_single);
        command_menu_item(Command::comment_block);
        command_menu_item(Command::word_wrap);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("File"))) {
        command_menu_item(Command::rename_script);
        command_menu_item(Command::import_script);
        command_menu_item(Command::export_script);
        command_menu_item(Command::delete_script);
        ImGui::Separator();
        command_menu_item(Command::disable_syntax);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    command_menu_item(Command::navigate_object);
    command_menu_item(Command::goto_bracket);
    command_menu_item(Command::select_to_bracket);
    command_menu_item(Command::goto_line);
    command_menu_item(Command::templates);

    ImGui::Separator();
    draw_layout_menu();
    draw_panels_menu();
}

void MainShell::draw_layout_menu() {
    if (!ImGui::BeginMenu(TR("Layout"))) return;
    command_menu_item(Command::toggle_layout);
    ImGui::Separator();
    command_menu_item(Command::multiple_results);
    ImGui::Separator();
    command_menu_item(Command::toggle_result_panel);
    command_menu_item(Command::maximize_result_panel);
    command_menu_item(Command::switch_panel);
    ImGui::EndMenu();
}

void MainShell::draw_panels_menu() {
    if (!ImGui::BeginMenu(TR("Panels"))) return;
    command_menu_item(Command::show_output);
    command_menu_item(Command::show_log);
    command_menu_item(Command::show_variables);
    command_menu_item(Command::show_outline);
    command_menu_item(Command::show_terminal);
    ImGui::EndMenu();
}

// O menu da regua (numeros de linha): `#SQLRulerContext`.
void MainShell::draw_ruler_context_menu() {
    const MenuStyleScope menu_style;
    command_menu_item(Command::foldings_enabled);
    command_menu_item(Command::expand_all_foldings);
    command_menu_item(Command::collapse_all_foldings);
    ImGui::Separator();
    command_menu_item(Command::goto_line);
}

// O menu "SQL Editor" da barra principal (`SQLEditorMenu`), na ordem do
// plugin.xml.
void MainShell::draw_sql_editor_menu() {
    command_menu_item(Command::open_script);
    command_menu_item(Command::recent_script);
    command_menu_item(Command::new_script);
    command_menu_item(Command::console);

    ImGui::Separator();
    command_menu_item(Command::run_statement);
    command_menu_item(Command::run_statement_new);
    command_menu_item(Command::run_script);
    command_menu_item(Command::run_script_from_position);
    command_menu_item(Command::run_script_new);
    command_menu_item(Command::cancel_query);
    command_menu_item(Command::run_count);
    command_menu_item(Command::run_all_rows);
    command_menu_item(Command::run_expression);

    ImGui::Separator();
    command_menu_item(Command::explain);
    command_menu_item(Command::load_plan);

    ImGui::Separator();
    command_menu_item(Command::import_script);
    command_menu_item(Command::export_script);

    ImGui::Separator();
    draw_panels_menu();
    draw_layout_menu();

    ImGui::Separator();
    if (ImGui::BeginMenu(TR("Context"))) {
        command_menu_item(Command::sync_connection);
        command_menu_item(Command::refresh_current_schema);
        command_menu_item(Command::refresh_all_schemas);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    command_menu_item(Command::sync_auto);
}

// --- Abas de resultado -------------------------------------------------------------

void MainShell::draw_result_tabs(SqlDocument& document) {
    // Uma aba so', sem nome proprio e sem estar fixada: a barra nao diria
    // nada que o titulo do painel ja' nao diga.
    if (document.result_tab_count() == 1 && !document.result_tab(0).pinned &&
        document.result_tab(0).title.empty()) {
        return;
    }

    // Trocar de script troca a barra inteira. O ImGui lembra a aba
    // selecionada POR barra, e na primeira vez escolheria a primeira -- a
    // ativa do documento e' que vale.
    if (last_result_document_id_ != document.id()) {
        last_result_document_id_ = document.id();
        force_result_tab_        = document.active_result_tab_id();
    }

    ImGui::PushID(static_cast<int>(document.id()));
    if (ImGui::BeginTabBar("##resulttabs", ImGuiTabBarFlags_Reorderable |
                                               ImGuiTabBarFlags_FittingPolicyScroll)) {
        std::size_t to_close  = document.result_tab_count();
        std::size_t to_select = document.result_tab_count();

        for (std::size_t i = 0; i < document.result_tab_count(); ++i) {
            const SqlDocument::ResultTab& tab = document.result_tab(i);

            // "###" mantem a identidade da aba quando o titulo muda a cada
            // execucao.
            const std::string label =
                (tab.title.empty() ? std::string(TR("Result")) + " " +
                                         std::to_string(i + 1)
                                   : tab.title) +
                (tab.pinned ? "  \xE2\x97\x8F" : "") + "###rt" +
                std::to_string(tab.id);

            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
            if (tab.id == force_result_tab_) flags |= ImGuiTabItemFlags_SetSelected;

            bool open = true;
            // A fixada nao tem o "x": fechar por engano e' o que fixar evita.
            const bool visible =
                ImGui::BeginTabItem(label.c_str(), tab.pinned ? nullptr : &open, flags);

            if (ImGui::BeginPopupContextItem("##rtmenu")) {
                if (ImGui::MenuItem(TR("Pin/unpin"), nullptr, tab.pinned)) {
                    document.set_result_tab_pinned(i, !tab.pinned);
                }
                if (ImGui::MenuItem(TR("Close"), nullptr, false, !tab.pinned)) {
                    to_close = i;
                }
                ImGui::EndPopup();
            }

            if (visible) {
                to_select = i;
                ImGui::EndTabItem();
            }
            if (!open) to_close = i;
        }
        ImGui::EndTabBar();

        force_result_tab_ = 0;

        // Com uma execucao em curso a aba ativa nao muda por clique: o
        // resultado chega nela, e a colheita a restauraria de qualquer modo.
        if (to_select < document.result_tab_count() && !document.executing()) {
            document.select_result_tab(to_select);
        }
        if (to_close < document.result_tab_count()) {
            document.close_result_tab(to_close);
            force_result_tab_ = document.active_result_tab_id();
        }
    }
    ImGui::PopID();
}

// --- Paineis -----------------------------------------------------------------------

// Ancora o painel junto do resultado na primeira vez que aparece nesta
// execucao. Mesma regra da janela do editor: o layout.ini guarda ids de no'
// que mudam quando a arvore de paineis muda.
void MainShell::dock_panel_once(bool& docked) {
    if (docked || result_dock_id_ == 0) return;
    ImGui::SetNextWindowDockID(result_dock_id_, ImGuiCond_Always);
    docked = true;
}

void MainShell::draw_output_panel() {
    if (!show_output_) return;

    dock_panel_once(output_docked_);
    if (ImGui::Begin(TRW("Output", "###OutputPanel"), &show_output_)) {
        const Palette& p = colors();
        SqlDocument* document = active_document();
        Session& target = document != nullptr ? session_for(*document) : session();

        if (ImGui::SmallButton(TR("Clear"))) target.clear_server_output();

        // Sem o aviso, um painel vazio num MySQL pareceria "o servidor nao
        // disse nada", quando o que falta e' o cliente recolher.
        if (target.state() == SessionState::connected &&
            !target.reports_server_output()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim),
                               TR("this driver does not report server output; "
                                  "only @echo lines appear"));
        }
        ImGui::Separator();

        const std::vector<std::string> lines = target.server_output();
        if (lines.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("no server output yet"));
        }

        ImFont* mono = mono_font();
        if (mono != nullptr) ImGui::PushFont(mono);
        if (ImGui::BeginChild("##outputlines")) {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(lines.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    ImGui::TextUnformatted(lines[static_cast<std::size_t>(i)].c_str());
                }
            }
            // Acompanha o fim, como um terminal -- a menos que o usuario
            // tenha rolado para cima para ler.
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
        if (mono != nullptr) ImGui::PopFont();
    }
    ImGui::End();
}

void MainShell::draw_variables_panel() {
    if (!show_variables_) return;

    dock_panel_once(variables_docked_);
    if (ImGui::Begin(TRW("Variables", "###VariablesPanel"), &show_variables_)) {
        const Palette& p = colors();
        SqlDocument* document = active_document();

        ImGui::TextColored(col4(p.text_dim),
                           TR("@set name = value defines; ${name} uses it in a query"));

        if (document == nullptr || document->variables().empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("no variables in this script"));
        } else if (ImGui::BeginTable("##vars", 3,
                                     ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn(TR("Name"), ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn(TR("Value"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableHeadersRow();

            std::string to_erase;
            for (const auto& [name, value] : document->variables()) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(col4(p.data), "%s", value.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(name.c_str());
                if (ImGui::SmallButton("x")) to_erase = name;
                ImGui::PopID();
            }
            ImGui::EndTable();

            if (!to_erase.empty()) document->variables().erase(to_erase);
        }
    }
    ImGui::End();
}

void MainShell::draw_outline_panel() {
    if (!show_outline_) return;

    dock_panel_once(outline_docked_);
    if (ImGui::Begin(TRW("Outline", "###OutlinePanel"), &show_outline_)) {
        const Palette& p = colors();
        SqlDocument* document = active_document();

        if (document == nullptr) {
            ImGui::TextColored(col4(p.text_dim), TR("no script open"));
            ImGui::End();
            return;
        }

        // Recalcular so' quando o texto muda: tokenizar um script de mil
        // linhas a cada quadro apareceria no contador de FPS.
        const std::size_t stamp = document->editor().GetUndoIndex();
        if (outline_document_id_ != document->id() || outline_stamp_ != stamp ||
            outline_dirty_) {
            outline_ = sql::outline(document->editor().GetText(),
                                    document_dialect(*document));
            outline_document_id_ = document->id();
            outline_stamp_       = stamp;
            outline_dirty_       = false;
        }

        if (outline_.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("the script has no statements"));
        }

        const std::size_t cursor_line =
            document->editor().GetCurrentCursorPosition().line;

        for (std::size_t i = 0; i < outline_.size(); ++i) {
            const sql::OutlineEntry& entry = outline_[i];

            // A instrucao onde o cursor esta': a ultima que comeca ate' ele.
            const bool current =
                entry.line <= cursor_line &&
                (i + 1 == outline_.size() || outline_[i + 1].line > cursor_line);

            ImGui::PushID(static_cast<int>(i));
            ImGui::TextColored(col4(p.text_dim), "%4zu", entry.line + 1);
            ImGui::SameLine();
            if (ImGui::Selectable(entry.label.c_str(), current)) {
                const TextEditor::DocPos position(entry.line, 0);
                document->editor().SetCursor(position);
                document->editor().ScrollToLine(entry.line);
                focus_editor_ = true;
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

// Terminal SQL: uma linha de comando e o que voltou, em texto. Usa a sessao
// do script ativo -- e' o mesmo banco, a mesma transacao.
void MainShell::draw_terminal_panel() {
    // O que o comando anterior devolveu.
    if (terminal_pending_id_ != 0) {
        Connection* owner = connection_by_id(terminal_pending_id_);
        if (owner == nullptr) {
            terminal_pending_id_ = 0;
        } else if (!owner->session->busy()) {
            if (auto result = owner->session->take_result()) {
                if (result->column_count() > 0) {
                    db::ExportOptions options;
                    options.format    = db::ExportFormat::markdown;
                    options.null_text = "[null]";

                    std::istringstream text(db::export_to_string(*result, options));
                    for (std::string line; std::getline(text, line);) {
                        terminal_lines_.push_back(std::move(line));
                    }
                    terminal_lines_.push_back(
                        std::string(TRF("(%zu row(s))", result->row_count())));
                } else {
                    terminal_lines_.push_back(owner->session->status_message());
                }
            } else {
                terminal_lines_.push_back(owner->session->status_message());
            }
            terminal_lines_.emplace_back();
            terminal_pending_id_ = 0;
            terminal_scroll_     = true;
        }
    }

    // Escondido junto com a parte de baixo ("Toggle results panel"); a
    // resposta pendente, acima, e' colhida de qualquer modo.
    if (!show_terminal_ || results_hidden_) return;

    dock_panel_once(terminal_docked_);
    if (ImGui::Begin(TRW("SQL Terminal", "###TerminalPanel"), &show_terminal_)) {
        const Palette& p = colors();
        SqlDocument* document = active_document();
        Connection* owner =
            document != nullptr ? connection_by_id(document->connection_id())
                                : nullptr;
        const bool ready = owner != nullptr &&
                           owner->session->state() == SessionState::connected &&
                           !owner->session->busy() && terminal_pending_id_ == 0 &&
                           executing_document_id_ == 0;

        ImFont* mono = mono_font();
        if (mono != nullptr) ImGui::PushFont(mono);

        const float input_height = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        if (ImGui::BeginChild("##termlines", ImVec2(0.0f, -input_height))) {
            if (terminal_lines_.empty()) {
                ImGui::TextColored(col4(p.text_dim),
                                   TR("type a statement and press Enter"));
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(terminal_lines_.size()));
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                    const std::string& line =
                        terminal_lines_[static_cast<std::size_t>(i)];
                    // O que o usuario digitou sai destacado do que voltou.
                    const bool echoed = line.rfind("> ", 0) == 0;
                    ImGui::TextColored(col4(echoed ? p.accent_light : p.text), "%s",
                                       line.c_str());
                }
            }
            if (terminal_scroll_) {
                ImGui::SetScrollHereY(1.0f);
                terminal_scroll_ = false;
            }
        }
        ImGui::EndChild();

        ImGui::TextColored(col4(ready ? p.ok : p.text_dim), ">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::BeginDisabled(!ready);
        const bool entered = ImGui::InputText(
            "##terminput", terminal_input_, sizeof terminal_input_,
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::EndDisabled();

        if (mono != nullptr) ImGui::PopFont();

        if (entered && terminal_input_[0] != '\0' && ready) {
            std::string sql = terminal_input_;
            terminal_lines_.push_back("> " + sql);
            terminal_input_[0] = '\0';
            terminal_scroll_   = true;
            ImGui::SetKeyboardFocusHere(-1);   // o foco volta para a linha

            if (handle_control_command(*document, sql)) {
                terminal_lines_.emplace_back(TR("client command applied"));
            } else {
                sql = sql::expand_variables(sql, document->variables());

                // Limitado a uma pagina: um SELECT sem LIMIT numa tabela de
                // milhoes viraria milhoes de linhas de texto.
                const sql::PagedQuery paged = sql::make_paged_query(
                    sql, document_dialect(*document), 0, kTerminalRows);
                if (paged.rewritten) {
                    terminal_lines_.push_back(
                        std::string(TRF("(showing at most %zu rows)", kTerminalRows)));
                }
                terminal_pending_id_ = owner->id;
                owner->session->execute_async(paged.sql);
            }
        }

        // Um terminal comprido demais pesa na memoria sem servir a ninguem.
        constexpr std::size_t kMaxLines = 5000;
        if (terminal_lines_.size() > kMaxLines) {
            terminal_lines_.erase(
                terminal_lines_.begin(),
                terminal_lines_.begin() +
                    static_cast<std::ptrdiff_t>(terminal_lines_.size() - kMaxLines));
        }
    }
    ImGui::End();
}

// --- Dialogos ----------------------------------------------------------------------

void MainShell::draw_goto_line_dialog() {
    if (goto_line_open_) {
        ImGui::OpenPopup("###GotoLine");
        goto_line_open_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(TRW("Go to Line", "###GotoLine"), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    SqlDocument* document = active_document();
    const std::size_t lines = document != nullptr ? document->editor().GetLineCount() : 0;

    ImGui::TextUnformatted(TRF("Enter line number (1..%zu):", lines));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(220);
    const bool entered = ImGui::InputText(
        "##line", goto_line_buffer_, sizeof goto_line_buffer_,
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsDecimal |
            ImGuiInputTextFlags_AutoSelectAll);

    const bool confirmed = entered || ImGui::Button(TR("OK"), ImVec2(100, 0));
    if (confirmed && document != nullptr && lines > 0) {
        const long wanted = std::strtol(goto_line_buffer_, nullptr, 10);
        // Fora do intervalo, vai para a ponta mais proxima em vez de recusar.
        const std::size_t line =
            wanted < 1 ? 0
            : (std::min)(static_cast<std::size_t>(wanted) - 1, lines - 1);

        document->editor().SetCursor(TextEditor::DocPos(line, 0));
        document->editor().ScrollToLine(line);
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// "Morph to delimited list": os seis campos do dialogo do DBeaver
// (MorphDelimitedListHandler.ConfigDialog), com a previa embaixo.
void MainShell::draw_morph_dialog() {
    if (morph_open_) {
        ImGui::OpenPopup("###Morph");
        morph_open_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(TRW("Delimited text options", "###Morph"), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    SqlDocument* document = active_document();

    ImGui::TextColored(col4(colors().data), TR("Source"));
    ImGui::SetNextItemWidth(260);
    ImGui::InputText(TR("Column delimiter"), morph_source_, sizeof morph_source_);
    ImGui::TextColored(col4(colors().text_dim),
                       TR("\\t and \\n stand for tab and line break"));

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Target"));
    ImGui::SetNextItemWidth(260);
    ImGui::InputText(TR("Result delimiter"), morph_target_, sizeof morph_target_);
    ImGui::SetNextItemWidth(260);
    ImGui::InputText(TR("String quote character"), morph_quote_, sizeof morph_quote_);
    ImGui::SetNextItemWidth(260);
    ImGui::InputInt(TR("Wrap line on column"), &morph_wrap_);
    ImGui::SetNextItemWidth(260);
    ImGui::InputText(TR("Leading text"), morph_leading_, sizeof morph_leading_);
    ImGui::SetNextItemWidth(260);
    ImGui::InputText(TR("Trailing text"), morph_trailing_, sizeof morph_trailing_);

    // O campo aceita "\t" e "\n" escritos por extenso: nao ha' como digitar
    // um tab numa caixa de texto.
    const auto unescape = [](std::string_view text) {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                const char next = text[i + 1];
                if (next == 't') { out.push_back('\t'); ++i; continue; }
                if (next == 'n') { out.push_back('\n'); ++i; continue; }
                if (next == '\\') { out.push_back('\\'); ++i; continue; }
            }
            out.push_back(text[i]);
        }
        return out;
    };

    sql::MorphOptions options;
    options.source_delimiters = unescape(morph_source_);
    options.target_delimiter  = unescape(morph_target_);
    options.quote             = morph_quote_;
    options.wrap_line         = morph_wrap_ > 0 ? static_cast<std::size_t>(morph_wrap_) : 0;
    options.leading_text      = unescape(morph_leading_);
    options.trailing_text     = unescape(morph_trailing_);

    // A previa, com a selecao real: ver o resultado antes de trocar o texto
    // e' o que o dialogo do DBeaver nao da'.
    if (document != nullptr && document->editor().CurrentCursorHasSelection()) {
        const std::string selected = document->editor().GetSectionText(
            document->editor().GetCurrentCursorSelection());
        std::string preview = sql::morph_delimited_list(selected, options);
        if (preview.size() > 400) preview = preview.substr(0, 400) + "...";

        ImGui::Spacing();
        ImGui::TextColored(col4(colors().data), TR("Preview"));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 480.0f);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    if (ImGui::Button(TR("OK"), ImVec2(100, 0)) && document != nullptr) {
        document->editor().FilterSelections([options](std::string_view text) {
            return sql::morph_delimited_list(text, options);
        });
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// "Complete template name": os templates cujo nome comeca pela palavra antes
// do cursor; escolher troca a palavra pelo texto do template.
void MainShell::draw_templates_popup() {
    if (templates_open_) {
        ImGui::OpenPopup("###Templates");
        templates_open_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(TRW("Templates", "###Templates"), nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    SqlDocument* document = active_document();
    if (document == nullptr) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    TextEditor& editor = document->editor();
    const TextEditor::DocPos cursor = editor.GetCurrentCursorPosition();
    const TextEditor::DocPos word_start = editor.FindWordStart(cursor);
    const std::string prefix = editor.GetSectionText(word_start, cursor);

    const sql::Template* chosen = nullptr;
    std::size_t shown = 0;
    for (const sql::Template& entry : sql::default_templates()) {
        // Sem palavra antes do cursor, todos; com ela, os que comecam assim.
        if (!prefix.empty() && !entry.name.starts_with(prefix)) continue;
        ++shown;

        const std::string label =
            std::string(entry.name) + "  -  " + std::string(entry.description);
        if (ImGui::Selectable(label.c_str())) chosen = &entry;
        if (ImGui::IsItemHovered()) {
            hint_fmt("%s", std::string(entry.pattern).c_str());
        }
    }
    if (shown == 0) {
        ImGui::TextColored(col4(colors().text_dim),
                           TR("no template starts with \"%s\""), prefix.c_str());
    }

    if (chosen != nullptr) {
        const sql::ExpandedTemplate expanded = sql::expand_template(*chosen);

        // Troca a palavra digitada pelo texto, num passo de desfazer.
        editor.ReplaceSectionText(word_start, cursor, expanded.text);

        // Seleciona a primeira variavel: e' o que o usuario preenche agora.
        const auto place = [&](std::size_t offset) {
            const sql::TextPosition at = sql::position_of(expanded.text, offset);
            return TextEditor::DocPos(
                word_start.line + at.line,
                at.line == 0 ? word_start.index + at.column : at.column);
        };
        if (expanded.select > 0) {
            editor.SelectRegion(place(expanded.cursor),
                                place(expanded.cursor + expanded.select));
        } else {
            editor.SetCursor(place(expanded.cursor));
        }
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::Spacing();
    if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        focus_editor_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// "Delete this script": apaga o ARQUIVO e fecha a aba. Sem desfazer, por isso
// a confirmacao diz o caminho.
void MainShell::draw_delete_script_confirm() {
    if (delete_script_id_ == 0) return;

    SqlDocument* document = nullptr;
    std::size_t  index    = 0;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i]->id() == delete_script_id_) {
            document = documents_[i].get();
            index    = i;
            break;
        }
    }
    if (document == nullptr) {
        delete_script_id_ = 0;
        return;
    }

    ImGui::OpenPopup("###DeleteScript");
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(TRW("Delete this script", "###DeleteScript"),
                                nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const bool has_file = !document->file_path().empty();
    ImGui::TextUnformatted(TRF("Delete the script \"%s\"?", document->title().c_str()));
    if (has_file) {
        ImGui::TextColored(col4(colors().warn), "%s", document->file_path().c_str());
        ImGui::TextColored(col4(colors().text_dim),
                           TR("The file is removed from disk. This cannot be undone."));
    } else {
        ImGui::TextColored(col4(colors().text_dim),
                           TR("The script was never saved; the tab is closed."));
    }
    ImGui::Spacing();

    if (ImGui::Button(TR("Delete"), ImVec2(100, 0))) {
        if (has_file) {
            std::error_code ec;
            std::filesystem::remove(document->file_path(), ec);
            if (ec) show_toast(ec.message());
        }
        // Sem isto a gravacao automatica recriaria o arquivo ao fechar a aba
        // (retire_script grava o que estiver pendente).
        document->autosave().off = true;
        close_document(index);
        delete_script_id_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        delete_script_id_ = 0;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Tudo o que os comandos do editor desenham fora do proprio editor.
void MainShell::draw_editor_extras() {
    draw_goto_line_dialog();
    draw_morph_dialog();
    draw_templates_popup();
    draw_delete_script_confirm();
    draw_shortcuts_window();
    draw_toast();
}

} // namespace otter::ui
