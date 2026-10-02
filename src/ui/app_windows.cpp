// C-Otter -- ui/app_windows.cpp
//
// As janelas que os comandos de aplicacao abrem (ui/app_commands.cpp): os
// menus Database e Navigate, as listas de busca (Ctrl+9, Ctrl+0, Ctrl+Shift+D,
// Ctrl+3), filtro de objetos, favoritos, conexao por URL, copia e colagem
// avancadas, preferencias, drivers, diagnostico, transacoes pendentes e o
// dashboard.
//
// Toda janela daqui e' opaca e flutuante (diretiva 13), e todo rotulo passa
// por TR() (diretiva 8).
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "base/paths.hpp"
#include "db/app_tools.hpp"
#include "db/registry.hpp"
#include "ui/hint.hpp"
#include "ui/icon_images.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }
ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(),
                                needle.end(), [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

// Janela flutuante, opaca, centrada ao aparecer.
bool begin_floating(const char* title, bool* open, float width = 0.0f,
                    ImGuiWindowFlags extra = 0) {
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    if (width > 0.0f) {
        ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Appearing);
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(colors().bg_darkest, 1.0f)));
    const bool visible = ImGui::Begin(title, open,
                                      ImGuiWindowFlags_NoDocking |
                                          ImGuiWindowFlags_NoCollapse | extra);
    ImGui::PopStyleColor();
    return visible;
}

// Uma linha das listas de busca.
struct PickItem {
    std::string           label;
    std::string           detail;
    Icon                  icon = Icon::info;
    bool                  enabled = true;
    std::function<void()> action;
};

} // namespace

// --- Menus -------------------------------------------------------------------------------

// "Database", como no DBeaver: conexao, transacao e ferramentas do servidor.
void MainShell::draw_database_menu() {
    if (!ImGui::BeginMenu(TR("Database"))) return;

    command_menu_item(Command::app_new_connection);
    command_menu_item(Command::app_new_connection_url);
    command_menu_item(Command::app_new_folder);
    ImGui::Separator();
    command_menu_item(Command::app_connect);
    command_menu_item(Command::app_reconnect);
    command_menu_item(Command::app_disconnect);
    command_menu_item(Command::app_disconnect_others);
    command_menu_item(Command::app_disconnect_all);
    ImGui::Separator();
    if (ImGui::BeginMenu(TR("Transaction Mode"))) {
        command_menu_item(Command::app_auto_commit);
        command_menu_item(Command::app_read_only);
        ImGui::EndMenu();
    }
    command_menu_item(Command::app_commit);
    command_menu_item(Command::app_rollback);
    command_menu_item(Command::app_txn_log);
    command_menu_item(Command::app_txn_pending);
    ImGui::Separator();
    command_menu_item(Command::app_select_connection);
    command_menu_item(Command::app_select_schema);
    command_menu_item(Command::app_change_password);
    ImGui::Separator();
    command_menu_item(Command::app_driver_manager);
    command_menu_item(Command::app_dashboard_open);
    ImGui::EndMenu();
}

// "Navigate": achar objetos e andar entre eles.
void MainShell::draw_navigate_menu() {
    if (!ImGui::BeginMenu(TR("Navigate"))) return;

    command_menu_item(Command::app_goto_object);
    command_menu_item(Command::app_command_palette);
    command_menu_item(Command::app_show_scripts);
    ImGui::Separator();
    command_menu_item(Command::app_link_editor);
    command_menu_item(Command::app_filter_focus);
    ImGui::Separator();
    command_menu_item(Command::app_bookmark_add);
    command_menu_item(Command::app_bookmark_navigate);
    ImGui::Separator();
    command_menu_item(Command::app_props_next);
    command_menu_item(Command::app_props_previous);
    command_menu_item(Command::app_source_tab);
    ImGui::EndMenu();
}

// "Window": paineis, preferencias e o que diz respeito ao programa inteiro.
void MainShell::draw_window_menu() {
    if (!ImGui::BeginMenu(TR("Window"))) return;

    command_menu_item(Command::app_view_toggle);
    command_menu_item(Command::app_dashboard_open);
    ImGui::Separator();
    if (ImGui::BeginMenu(TR("Database Navigator"))) {
        command_menu_item(Command::app_filter_connected);
        command_menu_item(Command::app_filter_config);
        command_menu_item(Command::app_filter_toggle);
        command_menu_item(Command::app_filter_clear);
        ImGui::Separator();
        command_menu_item(Command::app_move_up);
        command_menu_item(Command::app_move_down);
        command_menu_item(Command::app_move_top);
        command_menu_item(Command::app_move_bottom);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu(TR("Query Manager"))) {
        command_menu_item(Command::app_log_filter);
        command_menu_item(Command::app_log_clear);
        ImGui::EndMenu();
    }
    command_menu_item(Command::app_process_stop);
    ImGui::Separator();
    command_menu_item(Command::app_preferences);
    ImGui::EndMenu();
}

// --- As janelas ----------------------------------------------------------------------------

void MainShell::draw_app_windows() {
    const Palette& p = colors();
    Session& active = session();

    // O canal de comandos fecha a janela aberta com "app cancel".
    const bool cancel = app_.cancel;
    app_.cancel = false;

    // --- Listas de busca ----------------------------------------------------------------
    //
    // Cinco janelas com a mesma forma: um campo, a lista filtrada, setas e
    // Enter. So' uma aberta por vez.
    const auto pick_window = [&](bool& open, const char* title,
                                 const char* hint_text,
                                 const std::vector<PickItem>& items) {
        if (!open) return;
        if (cancel) { open = false; return; }

        ImGui::SetNextWindowSizeConstraints(ImVec2(520, 120), ImVec2(900, 560));
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(ImVec2(center.x, center.y * 0.55f), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
        const bool visible =
            ImGui::Begin(title, &open,
                         ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::PopStyleColor();
        if (!visible) { ImGui::End(); return; }

        if (app_.focus_search) {
            ImGui::SetKeyboardFocusHere();
            app_.focus_search = false;
        }
        ImGui::SetNextItemWidth(500.0f);
        const bool entered = ImGui::InputTextWithHint(
            "##search", hint_text, app_.search, sizeof app_.search,
            ImGuiInputTextFlags_EnterReturnsTrue);

        const int count = static_cast<int>(items.size());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++app_.search_cursor;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   --app_.search_cursor;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape))    open = false;
        app_.search_cursor = count == 0 ? 0 : std::clamp(app_.search_cursor, 0, count - 1);

        int chosen = -1;
        if (entered || app_.submit) chosen = app_.search_cursor;
        app_.submit = false;

        ImGui::Separator();
        if (ImGui::BeginChild("##results", ImVec2(500.0f, static_cast<float>(std::min(16, std::max(count, 1))) *
                                                              ImGui::GetFrameHeight()))) {
            for (int i = 0; i < count; ++i) {
                const PickItem& item = items[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                ImGui::BeginDisabled(!item.enabled);
                const bool selected = i == app_.search_cursor;
                if (ImGui::Selectable("##item", selected,
                                      ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0.0f, ImGui::GetFrameHeight() - 4.0f))) {
                    chosen = i;
                }
                if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                ImGui::SameLine(4.0f);
                icon_inline(item.icon, selected ? p.accent : p.text_dim);
                ImGui::SameLine(0.0f, 6.0f);
                ImGui::TextUnformatted(item.label.c_str());
                if (!item.detail.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(col4(p.text_dim), "%s", item.detail.c_str());
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            if (count == 0) ImGui::TextColored(col4(p.text_dim), "%s", TR("nothing found"));
        }
        ImGui::EndChild();
        ImGui::End();

        if (chosen >= 0 && chosen < count) {
            const PickItem& item = items[static_cast<std::size_t>(chosen)];
            if (item.enabled && item.action) {
                open = false;
                item.action();
            }
        }
    };

    if (app_.select_connection) {
        std::vector<PickItem> items;
        for (std::size_t s = 0; s < saved_profiles_.size(); ++s) {
            const db::ConnectionProfile& profile = saved_profiles_[s].profile;
            if (!contains_ci(profile.effective_name(), app_.search)) continue;
            const std::size_t open_index = find_root_connection(profile);
            const bool connected =
                open_index != static_cast<std::size_t>(-1) &&
                connections_[open_index].session->state() == SessionState::connected;
            items.push_back({profile.effective_name(),
                             profile.host + ":" + std::to_string(profile.port) +
                                 (connected ? std::string("  ") + TR("connected") : ""),
                             Icon::connect, saved_profiles_[s].supported,
                             [this, profile, open_index] {
                                 if (open_index != static_cast<std::size_t>(-1)) {
                                     active_connection_ = open_index;
                                     active_profile_    = profile;
                                     // O script ativo passa a ser desta conexao,
                                     // como no "Select active connection".
                                     if (SqlDocument* document = active_document();
                                         document != nullptr && !document->is_object()) {
                                         document->set_connection_id(
                                             connections_[open_index].id);
                                     }
                                     if (connections_[open_index].session->state() !=
                                         SessionState::connected) {
                                         tree_requests_.reconnect = connections_[open_index].id;
                                     }
                                 } else {
                                     open_connection(profile);
                                 }
                             }});
        }
        pick_window(app_.select_connection,
                    TRW("Select active connection", "###PickConnection"),
                    TR("Connection name"), items);
    }

    if (app_.select_schema) {
        std::vector<PickItem> items;
        const std::string current = active.current_schema();
        for (const db::SchemaMeta& schema : active.schemas()) {
            if (!contains_ci(schema.name, app_.search)) continue;
            items.push_back({schema.name,
                             schema.name == current ? TR("current") : std::string{},
                             Icon::schema, true,
                             [this, name = schema.name] { set_default_schema(name); }});
        }
        pick_window(app_.select_schema, TRW("Select active schema", "###PickSchema"),
                    TR("Schema name"), items);
    }

    // Aberta pelo icone de banco da aba da conexao (draw_tab_database_icon).
    if (app_.select_database) {
        std::vector<PickItem> items;
        const Connection* here = connection_by_id(app_.database_connection);
        if (here == nullptr) {
            app_.select_database = false;
        } else {
            const Connection* root = here->parent_id != 0
                                         ? connection_by_id(here->parent_id) : here;
            if (root == nullptr) root = here;
            const Session& server = *root->session;

            // MySQL: os bancos sao os schemas, e o corrente e' o do ultimo USE.
            // Os outros: a lista que a arvore mostra em "Databases" -- toda
            // ela, mesmo com "Show all databases" desligado: quem clicou no
            // icone quer trocar de banco, e uma lista so' com o corrente nao
            // trocaria nada.
            std::vector<std::string> names;
            std::string current;
            if (server.is_mysql()) {
                current = here->session->current_schema();
                for (const db::SchemaMeta& schema : server.schemas()) names.push_back(schema.name);
            } else {
                current = here->session->database_name();
                for (const db::DatabaseMeta& database : server.databases()) {
                    names.push_back(database.name);
                }
            }
            if (!current.empty() &&
                std::find(names.begin(), names.end(), current) == names.end()) {
                names.insert(names.begin(), current);
            }

            const std::size_t id = here->id;
            for (const std::string& name : names) {
                if (!contains_ci(name, app_.search)) continue;
                items.push_back({name, name == current ? TR("current") : std::string{},
                                 Icon::database, true,
                                 [this, id, name] { switch_tab_database(id, name); }});
            }
            // Ao abrir, a selecao comeca no banco em que a aba esta'.
            if (app_.search_cursor < 0) {
                app_.search_cursor = 0;
                for (std::size_t i = 0; i < items.size(); ++i) {
                    if (items[i].label == current) app_.search_cursor = static_cast<int>(i);
                }
            }
        }
        pick_window(app_.select_database,
                    TRW("Select active database", "###PickDatabase"),
                    TR("Database name"), items);
    }

    if (app_.goto_object) {
        // Tabelas, views, rotinas, sequencias e tipos ja' lidos do catalogo.
        // Num banco com o schema ainda fechado na arvore so' as relacoes
        // aparecem: as outras pastas carregam ao serem abertas.
        std::vector<PickItem> items;
        const std::size_t connection_id =
            active_connection_ < connections_.size() ? connections_[active_connection_].id : 0;
        const auto add = [&](db::ObjectRef ref, Icon icon) {
            if (items.size() >= 300) return;
            if (!contains_ci(ref.name, app_.search)) return;
            items.push_back({ref.name, ref.schema + "  " + TR(std::string(db::to_string(ref.type)).c_str()),
                             icon, true, [this, ref, connection_id] {
                                 nav_selected_ = NavTarget{true, connection_id, ref};
                                 open_object_editor(ref);
                             }});
        };
        if (app_.search[0] != '\0') {
            for (const db::SchemaMeta& schema : active.schemas()) {
                for (const db::TableMeta& table : schema.tables) {
                    db::ObjectRef ref;
                    ref.type = table.kind == db::ObjKind::view ? db::ObjectType::view
                             : table.kind == db::ObjKind::materialized_view
                                 ? db::ObjectType::materialized_view
                                 : db::ObjectType::table;
                    ref.schema = schema.name;
                    ref.name   = table.name;
                    add(ref, ref.type == db::ObjectType::table ? Icon::table : Icon::view);
                }
                for (const db::RoutineMeta& routine : schema.routines) {
                    db::ObjectRef ref;
                    ref.type = routine.kind == db::ObjKind::procedure
                                   ? db::ObjectType::procedure : db::ObjectType::function;
                    ref.schema    = schema.name;
                    ref.name      = routine.name;
                    ref.signature = routine.signature;
                    add(ref, Icon::procedure);
                }
                for (const db::SequenceMeta& sequence : schema.sequences) {
                    db::ObjectRef ref;
                    ref.type   = db::ObjectType::sequence;
                    ref.schema = schema.name;
                    ref.name   = sequence.name;
                    add(ref, Icon::sequence);
                }
            }
        }
        pick_window(app_.goto_object, TRW("Open database object", "###PickObject"),
                    TR("Object name (type to search)"), items);
    }

    if (app_.palette) {
        // Do C-Otter: todos os comandos numa busca, com a tecla do perfil ativo.
        std::vector<PickItem> items;
        for (const CommandInfo& info : all_commands()) {
            if (info.state == CommandState::out_of_scope ||
                info.state == CommandState::missing) {
                continue;
            }
            const std::string label = TR(info.label);
            if (!contains_ci(label, app_.search) && !contains_ci(info.label, app_.search)) {
                continue;
            }
            items.push_back({label, command_shortcut(info.id, keymap_), info.icon,
                             command_enabled(info.id),
                             [this, id = info.id] { queued_commands_.push_back(id); }});
        }
        pick_window(app_.palette, TRW("Command palette", "###PickCommand"),
                    TR("Type a command"), items);
    }

    if (app_.scripts) {
        std::vector<PickItem> items;
        for (std::size_t i = 0; i < documents_.size(); ++i) {
            const SqlDocument& document = *documents_[i];
            if (document.is_object()) continue;
            if (!contains_ci(document.title(), app_.search)) continue;
            const Connection* owner = connection_by_id(document.connection_id());
            items.push_back({document.title(),
                             (owner != nullptr ? owner->profile.effective_name()
                                               : std::string{}) +
                                 (document.file_path().empty() ? std::string{}
                                                               : "  " + document.file_path()),
                             Icon::open, true, [this, i, id = document.id()] {
                                 active_document_    = i;
                                 select_document_id_ = id;
                                 focus_editor_       = true;
                                 focus_document_id_  = id;
                             }});
        }
        // Os fechados que continuam em `.script`: escolher reabre a aba, na
        // conexao que o script tinha. No DBeaver esta lista e' a pasta
        // Scripts do projeto.
        for (const ScriptEntry& closed : closed_scripts_) {
            const std::string name =
                closed.title.empty()
                    ? std::filesystem::path(closed.file).stem().string()
                    : closed.title;
            if (!contains_ci(name, app_.search)) continue;
            items.push_back({name,
                             closed.connection +
                                 (closed.database.empty() ? std::string{}
                                                          : " (" + closed.database + ")") +
                                 "  " + script_path(scripts_dir_, closed.file),
                             Icon::open, true, [this, entry = closed] {
                                 if (const SqlDocument* document = open_stored_script(entry)) {
                                     select_document_id_ = document->id();
                                     focus_document_id_  = document->id();
                                     focus_editor_       = true;
                                 } else {
                                     show_toast(TRF("cannot open %s", entry.file.c_str()));
                                 }
                             }});
        }
        pick_window(app_.scripts, TRW("Scripts", "###PickScript"), TR("Script name"), items);
    }

    if (app_.associate) {
        std::vector<PickItem> items;
        for (const Connection& connection : connections_) {
            if (connection.session->state() == SessionState::disconnected &&
                connection.profile.host.empty()) {
                continue;
            }
            if (!contains_ci(connection.profile.effective_name(), app_.search)) continue;
            items.push_back({connection.profile.effective_name(),
                             connection.profile.database, Icon::connect, true,
                             [this, id = connection.id] {
                                 if (SqlDocument* document = active_document()) {
                                     document->set_connection_id(id);
                                 }
                             }});
        }
        pick_window(app_.associate,
                    TRW("Associate with data source", "###PickAssociate"),
                    TR("Connection name"), items);
    }

    if (app_.bookmarks) {
        std::vector<PickItem> items;
        for (std::size_t i = 0; i < settings_.bookmarks.size(); ++i) {
            const AppSettings::Bookmark& bookmark = settings_.bookmarks[i];
            if (!contains_ci(bookmark.title, app_.search)) continue;
            items.push_back({bookmark.title,
                             bookmark.connection +
                                 (bookmark.database.empty() ? "" : " / " + bookmark.database) +
                                 "  " + bookmark.schema,
                             Icon::pin, true,
                             [this, bookmark] { open_bookmark(bookmark); }});
        }
        pick_window(app_.bookmarks, TRW("Bookmarks", "###PickBookmark"),
                    TR("Bookmark name"), items);
    }

    // --- Ferramentas do no' (Alt+`) ---------------------------------------------------
    if (app_.tools_popup) {
        ImGui::OpenPopup("##contexttools");
        app_.tools_popup = false;
    }
    if (ImGui::BeginPopup("##contexttools")) {
        if (const NavTarget* target = nav_target()) {
            draw_object_menu(target->ref);
        }
        ImGui::EndPopup();
    }

    // --- Conexao por URL ----------------------------------------------------------------
    if (app_.url_dialog) {
        if (cancel) app_.url_dialog = false;
        if (begin_floating(TRW("New connection from JDBC URL", "###UrlDialog"),
                           &app_.url_dialog, 560.0f)) {
            ImGui::TextWrapped("%s", TR("Paste the connection URL. The connection dialog "
                                        "opens with the fields filled in."));
            if (app_.focus_search) {
                ImGui::SetKeyboardFocusHere();
                app_.focus_search = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool entered = ImGui::InputTextWithHint(
                "##url", "jdbc:postgresql://host:5432/database", app_.url, sizeof app_.url,
                ImGuiInputTextFlags_EnterReturnsTrue);

            const auto parsed = db::profile_from_url(app_.url);
            if (app_.url[0] != '\0') {
                if (parsed) {
                    ImGui::TextColored(col4(p.text_dim), "%s  %s:%u/%s  %s",
                                       dbms_name(parsed->driver_id).c_str(),
                                       parsed->host.c_str(),
                                       static_cast<unsigned>(parsed->port),
                                       parsed->database.c_str(), parsed->user.c_str());
                } else {
                    ImGui::TextColored(col4(p.error), "%s",
                                       TR(parsed.error().message().c_str()));
                }
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(!parsed);
            if (ImGui::Button(TR("Create")) || ((entered || app_.submit) && parsed)) {
                connection_dialog_.open_configure(*parsed);
                app_.url_dialog = false;
            }
            ImGui::EndDisabled();
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.url_dialog = false;
        }
        ImGui::End();
    }

    // --- Pasta nova ---------------------------------------------------------------------
    //
    // A mesma janela cria e renomeia: nos dois casos e' um nome, dentro de uma
    // pasta-pai. O caminho inteiro e' pai + "/" + nome.
    if (app_.new_folder) {
        if (cancel) app_.new_folder = false;
        const bool renaming = !app_.folder_rename.empty();
        if (begin_floating(renaming ? TRW("Rename folder", "###NewFolder")
                                    : TRW("New Folder", "###NewFolder"),
                           &app_.new_folder, 380.0f)) {
            // Onde a pasta fica -- e a conexao que ela vai receber.
            if (!app_.folder_parent.empty()) {
                ImGui::TextColored(col4(p.text_dim), "%s  %s", TR("In folder"),
                                   app_.folder_parent.c_str());
            }
            if (app_.folder_move) {
                ImGui::TextColored(col4(p.text_dim), "%s  %s", TR("Receives the connection"),
                                   app_.folder_move->effective_name().c_str());
            }
            if (app_.focus_search) {
                ImGui::SetKeyboardFocusHere();
                app_.focus_search = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool entered = ImGui::InputTextWithHint(
                "##folder", TR("Folder name"), app_.folder, sizeof app_.folder,
                ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = db::folder_normalize(app_.folder);
            const std::string path = db::folder_join(app_.folder_parent, name);

            const std::vector<std::string> existing = connection_folders();
            const bool unchanged = renaming && path == app_.folder_rename;
            const bool exists =
                !unchanged &&
                std::find(existing.begin(), existing.end(), path) != existing.end();
            // Renomear uma pasta para dentro dela mesma ("a" -> "a/b") a faria
            // sumir: o caminho novo comeca pelo antigo.
            const bool into_itself =
                renaming && !unchanged && db::folder_contains(app_.folder_rename, path);
            if (exists) {
                ImGui::TextColored(col4(p.warn), "%s", TR("this folder already exists"));
            } else if (into_itself) {
                ImGui::TextColored(col4(p.warn), "%s",
                                   TR("a folder cannot be moved into itself"));
            }
            const bool valid = !name.empty() && !exists && !into_itself && !unchanged;
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("OK")) || ((entered || app_.submit) && valid)) {
                if (renaming) {
                    rebase_folder(app_.folder_rename, path);
                } else {
                    settings_.folders.push_back(path);
                    save_settings();
                    if (app_.folder_move) move_profile_to_folder(*app_.folder_move, path);
                }
                app_.new_folder = false;
            }
            ImGui::EndDisabled();
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.new_folder = false;
        }
        ImGui::End();
    }

    // --- Renomear conexao ---------------------------------------------------------------
    if (app_.rename_connection && !app_.rename_target) app_.rename_connection = false;
    if (app_.rename_connection) {
        if (cancel) app_.rename_connection = false;
        if (begin_floating(TRW("Rename Connection", "###RenameConnection"),
                           &app_.rename_connection, 380.0f)) {
            const db::ConnectionProfile& target = *app_.rename_target;
            if (app_.focus_search) {
                ImGui::SetKeyboardFocusHere();
                app_.focus_search = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool entered = ImGui::InputTextWithHint(
                "##connname", TR("Connection name"), app_.connection_name,
                sizeof app_.connection_name, ImGuiInputTextFlags_EnterReturnsTrue);

            std::string name = app_.connection_name;
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();

            // Dois rotulos iguais na arvore confundem: o nome de OUTRA conexao
            // e' recusado aqui, em vez de virar "nome_1" sem aviso.
            const bool taken = std::any_of(
                saved_profiles_.begin(), saved_profiles_.end(),
                [&](const db::StoredProfile& stored) {
                    const db::ConnectionProfile& other = stored.profile;
                    const bool same = other.driver_id == target.driver_id &&
                                      other.host == target.host && other.port == target.port &&
                                      other.database == target.database &&
                                      other.user == target.user;
                    return !same && other.effective_name() == name;
                });
            if (taken) {
                ImGui::TextColored(col4(p.warn), "%s",
                                   TR("another connection already has this name"));
            }
            const bool valid = !name.empty() && !taken && name != target.effective_name();
            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("OK")) || ((entered || app_.submit) && valid)) {
                rename_profile(target, name);
                app_.rename_connection = false;
            }
            ImGui::EndDisabled();
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.rename_connection = false;
        }
        ImGui::End();
    }

    // --- Apagar pasta -------------------------------------------------------------------
    //
    // O texto e' o do DBeaver (confirm_local_folder_delete_message): o que
    // importa dizer e' que as CONEXOES ficam -- sobem para a pasta de cima,
    // com as subpastas.
    if (app_.delete_folder) {
        if (cancel) app_.delete_folder = false;
        if (begin_floating(TRW("Delete folder", "###DeleteFolder"), &app_.delete_folder,
                           420.0f, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(400.0f);
            ImGui::TextUnformatted(
                TRF("Are you sure you want to delete folder \"%s\"?",
                    app_.folder_delete.c_str()));
            ImGui::Spacing();
            ImGui::TextUnformatted(TR("Connections in this folder will NOT be deleted."));
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button(TR("Delete")) || app_.submit) {
                app_.delete_folder = false;
                rebase_folder(app_.folder_delete, db::folder_parent(app_.folder_delete));
                // A propria pasta apagada nao pode voltar como "vazia".
                std::erase(settings_.folders, app_.folder_delete);
                save_settings();
            }
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.delete_folder = false;
        }
        ImGui::End();
    }

    // --- Filtro de objetos --------------------------------------------------------------
    if (app_.filter_config) {
        if (cancel) app_.filter_config = false;
        if (begin_floating(TRW("Configure filter", "###FilterConfig"), &app_.filter_config,
                           480.0f)) {
            AppSettings::Filter& filter = settings_.filters[filter_key(active_connection_)];
            ImGui::TextColored(col4(p.text_dim), "%s", filter_key(active_connection_).c_str());
            ImGui::TextWrapped("%s", TR("Masks separated by comma; * and % match any "
                                        "text. Applies to every object of the tree."));
            ImGui::Checkbox(TR("Enable"), &filter.enabled);
            if (app_.focus_search) {
                ImGui::SetKeyboardFocusHere();
                app_.focus_search = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##include", TR("Include (e.g. cli*, ped*)"),
                                     app_.filter_include, sizeof app_.filter_include);
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##exclude", TR("Exclude (e.g. tmp_*)"),
                                     app_.filter_exclude, sizeof app_.filter_exclude);
            if (ImGui::Button(TR("OK")) || app_.submit) {
                filter.include = db::join_masks(db::split_masks(app_.filter_include));
                filter.exclude = db::join_masks(db::split_masks(app_.filter_exclude));
                save_settings();
                refresh_object_filters();
                app_.filter_config = false;
            }
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.filter_config = false;
        }
        ImGui::End();
    }

    // --- Copia avancada -----------------------------------------------------------------
    if (app_.copy_special) {
        if (cancel) app_.copy_special = false;
        if (begin_floating(TRW("Options", "###CopySpecial"), &app_.copy_special, 380.0f)) {
            ImGui::Checkbox(TR("Copy header"), &settings_.copy_header);
            ImGui::Checkbox(TR("Copy row numbers"), &settings_.copy_row_numbers);
            ImGui::Checkbox(TR("Quote always"), &settings_.copy_quote_always);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText(TR("Column delimiter"), app_.copy_column, sizeof app_.copy_column);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText(TR("Row delimiter"), app_.copy_row, sizeof app_.copy_row);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText(TR("Quote character"), app_.copy_quote, sizeof app_.copy_quote);
            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputText(TR("NULL value text"), app_.copy_null, sizeof app_.copy_null);
            if (ImGui::Button(TR("Copy")) || app_.submit) {
                settings_.copy_column_delimiter = app_.copy_column;
                settings_.copy_row_delimiter    = app_.copy_row;
                settings_.copy_quote            = app_.copy_quote;
                settings_.copy_null_text        = app_.copy_null;
                save_settings();
                copy_special_now();
                app_.copy_special = false;
            }
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.copy_special = false;
        }
        ImGui::End();
    }

    // --- Colagem avancada ---------------------------------------------------------------
    if (app_.paste_special) {
        if (cancel) app_.paste_special = false;
        if (begin_floating(TRW("Advanced paste", "###PasteSpecial"), &app_.paste_special,
                           380.0f)) {
            ImGui::Checkbox(TR("Empty values as NULL"), &app_.paste_empty_null);
            ImGui::TextWrapped("%s", TR("Values separated by tab, one row per line, "
                                        "pasted from the selected cell."));
            SqlDocument* document = active_document();
            if (ImGui::Button(TR("Paste")) || app_.submit) {
                app_.paste_special = false;
                const char* clipboard = ImGui::GetClipboardText();
                if (document != nullptr && document->result() && has_selection_ &&
                    clipboard != nullptr) {
                    const db::ResultSet& rs = *document->result();
                    const auto rows = db::parse_clipboard(clipboard);
                    const GridView& view = document->grid_view();
                    // As colunas na ordem da tela, a partir da selecionada.
                    std::vector<std::size_t> order = view.order;
                    if (order.empty()) {
                        for (std::size_t c = 0; c < rs.column_count(); ++c) order.push_back(c);
                    }
                    const auto start =
                        std::find(order.begin(), order.end(), selected_column_);
                    std::size_t pasted = 0;
                    for (std::size_t r = 0; r < rows.size(); ++r) {
                        const std::size_t row = selected_row_ + r;
                        if (row >= rs.row_count()) break;
                        auto column = start;
                        for (const std::string& value : rows[r]) {
                            if (column == order.end()) break;
                            if (value.empty() && app_.paste_empty_null) {
                                document->edits().set_null(row, *column);
                            } else {
                                document->edits().set(row, *column, value);
                            }
                            ++pasted;
                            ++column;
                        }
                    }
                    show_toast(TRF("%zu value(s) pasted; save to apply", pasted));
                }
            }
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) app_.paste_special = false;
        }
        ImGui::End();
    }

    // --- Drivers ------------------------------------------------------------------------
    if (app_.drivers) {
        if (cancel) app_.drivers = false;
        if (begin_floating(TRW("Driver Manager", "###Drivers"), &app_.drivers, 520.0f)) {
            ImGui::TextWrapped("%s", TR("C-Otter speaks the database wire protocols itself: "
                                        "there are no JDBC drivers to download or configure."));
            if (ImGui::BeginTable("##drivers", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn(TR("Driver"));
                ImGui::TableSetupColumn("Id");
                ImGui::TableSetupColumn(TR("Default port"));
                ImGui::TableHeadersRow();
                for (const db::Driver* driver : db::all_drivers()) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    icon_inline(driver_icon(std::string(driver->id())), p.text);
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextUnformatted(std::string(driver->display_name()).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(std::string(driver->id()).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", static_cast<unsigned>(driver->default_port()));
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    // --- Preferencias -------------------------------------------------------------------
    if (app_.preferences) {
        if (cancel) app_.preferences = false;
        if (begin_floating(TRW("Preferences", "###Preferences"), &app_.preferences, 480.0f)) {
            ImGui::TextColored(col4(p.data), "%s", TR("User Interface"));
            ImGui::Separator();
            if (ImGui::BeginCombo(TR("Theme"), TR(current_theme().name.c_str()))) {
                for (const Theme& theme : available_themes()) {
                    if (ImGui::Selectable(TR(theme.name.c_str()),
                                          current_theme().id == theme.id)) {
                        set_theme(theme.id);
                        settings_.theme = std::string(theme.id);
                        reapply_palettes();
                        save_settings();
                    }
                }
                ImGui::EndCombo();
            }
            std::string language_label(i18n::current_language());
            for (const i18n::Language& language : i18n::available_languages()) {
                if (i18n::current_language() == language.code) {
                    language_label = language.native_name;
                }
            }
            if (ImGui::BeginCombo(TR("Language"), language_label.c_str())) {
                for (const i18n::Language& language : i18n::available_languages()) {
                    if (ImGui::Selectable(language.native_name.c_str(),
                                          i18n::current_language() == language.code)) {
                        i18n::set_language(language.code);
                        settings_.language = language.code;
                        save_settings();
                    }
                }
                ImGui::EndCombo();
            }
            int keymap = static_cast<int>(keymap_);
            const char* keymaps[] = {"DBeaver", "C-Otter"};
            if (ImGui::Combo(TR("Keyboard shortcuts"), &keymap, keymaps, 2)) {
                set_keymap(static_cast<Keymap>(keymap));
            }
            int icons = icon_set() == IconSet::dbeaver ? 0 : 1;
            const char* sets[] = {"DBeaver", "C-Otter"};
            if (ImGui::Combo(TR("Icons"), &icons, sets, 2)) {
                set_icon_set(icons == 0 ? IconSet::dbeaver : IconSet::otter);
                save_settings();
            }

            ImGui::Spacing();
            ImGui::TextColored(col4(p.data), "%s", TR("Database Navigator"));
            ImGui::Separator();
            if (ImGui::Checkbox(TR("Link with editor"), &settings_.link_with_editor)) {
                save_settings();
            }
            bool all = !settings_.connected_only;
            if (ImGui::Checkbox(TR("Show all connections"), &all)) {
                settings_.connected_only = !all;
                save_settings();
            }

            ImGui::Spacing();
            ImGui::TextColored(col4(p.data), "%s", TR("Data Editor"));
            ImGui::Separator();
            if (ImGui::Checkbox(TR("Show confirmation before save"),
                                &settings_.confirm_data_save)) {
                save_settings();
            }

            ImGui::Spacing();
            ImGui::TextColored(col4(p.data), "%s", TR("Dashboard"));
            ImGui::Separator();
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt(TR("Update period (s)"), &settings_.dashboard_interval)) {
                settings_.dashboard_interval = std::clamp(settings_.dashboard_interval, 1, 3600);
                save_settings();
            }

            ImGui::Spacing();
            ImGui::TextWrapped("%s", TR("Editor, completion, result and transfer settings "
                                        "are per connection: Edit connection, the pages "
                                        "under \"Connection settings\"."));
            if (ImGui::Button(TR("Edit connection..."))) {
                connection_dialog_.open_edit(active_profile_);
                app_.preferences = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Reset Settings..."))) app_.confirm_reset = true;
        }
        ImGui::End();
    }

    // --- Diagnostico --------------------------------------------------------------------
    if (app_.diagnostics_open) {
        if (cancel) app_.diagnostics_open = false;
        ImGui::SetNextWindowSize(ImVec2(760, 460), ImGuiCond_Appearing);
        if (begin_floating(TRW("Collect diagnostic info", "###Diagnostics"),
                           &app_.diagnostics_open)) {
            if (!app_.diagnostics_path.empty()) {
                ImGui::TextColored(col4(p.text_dim), "%s %s", TR("Saved to"),
                                   app_.diagnostics_path.c_str());
            }
            if (ImGui::Button(TR("Copy"))) ImGui::SetClipboardText(app_.diagnostics.c_str());
            ImGui::InputTextMultiline("##diagnostics", app_.diagnostics.data(),
                                      app_.diagnostics.size() + 1, ImVec2(-1, -1),
                                      ImGuiInputTextFlags_ReadOnly);
        }
        ImGui::End();
    }

    // --- Confirmacoes -------------------------------------------------------------------
    const auto confirm = [&](bool& open, const char* title, const char* text,
                             const std::function<void()>& action) {
        if (!open) return;
        if (cancel) { open = false; return; }
        if (begin_floating(title, &open, 420.0f, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(400.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            if (ImGui::Button(TR("OK")) || app_.submit) {
                open = false;
                action();
            }
            app_.submit = false;
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"))) open = false;
        }
        ImGui::End();
    };
    confirm(app_.confirm_reset, TRW("Reset Settings", "###ConfirmReset"),
            TR("Theme, language, keyboard shortcuts, icons and layout go back to the "
               "defaults. Connections, bookmarks and filters are kept."),
            [this] { apply_reset_settings(); });
    confirm(app_.confirm_history, TRW("Clear History", "###ConfirmHistory"),
            TR("Clears the query log and the server output of every open connection."),
            [this] {
                for (Connection& connection : connections_) {
                    connection.session->clear_query_log();
                    connection.session->clear_server_output();
                }
                show_toast(TR("history cleared"));
            });

    // --- Filtro do log ------------------------------------------------------------------
    if (app_.log_filter) {
        if (cancel) app_.log_filter = false;
        if (begin_floating(TRW("Log filters", "###LogFilter"), &app_.log_filter, 320.0f,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox(TR("Failed statements only"), &query_log_failed_only_);
            ImGui::Checkbox(TR("Show internal (metadata) queries"), &query_log_show_internal_);
        }
        ImGui::End();
    }

    // --- Paineis ------------------------------------------------------------------------
    if (app_.views) {
        if (cancel) app_.views = false;
        if (begin_floating(TRW("Show/Hide view", "###Views"), &app_.views, 300.0f,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Checkbox(TR("Execution log"), &show_log_);
            ImGui::Checkbox(TR("Server output"), &show_output_);
            ImGui::Checkbox(TR("SQL variables"), &show_variables_);
            ImGui::Checkbox(TR("Outline"), &show_outline_);
            ImGui::Checkbox(TR("SQL Terminal"), &show_terminal_);
            bool results = !results_hidden_;
            if (ImGui::Checkbox(TR("Results"), &results)) results_hidden_ = !results;
            ImGui::Checkbox(TR("Dashboard"), &app_.dashboard);
        }
        ImGui::End();
    }

    // --- Transacoes ---------------------------------------------------------------------
    if (app_.txn_pending) {
        if (cancel) app_.txn_pending = false;
        if (begin_floating(TRW("Pending transactions", "###TxnPending"), &app_.txn_pending,
                           560.0f)) {
            std::size_t shown = 0;
            if (ImGui::BeginTable("##pending", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn(TR("Connection"));
                ImGui::TableSetupColumn(TR("State"));
                ImGui::TableSetupColumn(TR("Changes"));
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (Connection& connection : connections_) {
                    Session& target = *connection.session;
                    if (target.state() != SessionState::connected) continue;
                    const db::TxnState txn = target.txn_state();
                    if (target.auto_commit() || txn == db::TxnState::idle) continue;
                    ++shown;
                    ImGui::PushID(static_cast<int>(connection.id));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(connection.profile.effective_name().c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(col4(txn == db::TxnState::failed ? p.error : p.warn),
                                       "%s", txn == db::TxnState::failed ? TR("Failed")
                                                                         : TR("Active"));
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", target.uncommitted_changes());
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(target.busy());
                    if (ImGui::SmallButton(TR("Commit"))) target.commit_async();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(TR("Rollback"))) target.rollback_async();
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (shown == 0) {
                ImGui::TextColored(col4(p.text_dim), "%s", TR("no open transaction"));
            }
        }
        ImGui::End();
    }

    if (app_.txn_log) {
        if (cancel) app_.txn_log = false;
        ImGui::SetNextWindowSize(ImVec2(640, 360), ImGuiCond_Appearing);
        if (begin_floating(TRW("Transaction log", "###TxnLog"), &app_.txn_log)) {
            // As instrucoes do usuario desde o ultimo COMMIT/ROLLBACK: e' o
            // que a transacao aberta contem.
            const std::vector<db::QueryLog> log = active.query_log();
            std::size_t first = 0;
            for (std::size_t i = 0; i < log.size(); ++i) {
                std::string head = log[i].sql.substr(0, 10);
                std::transform(head.begin(), head.end(), head.begin(), [](char c) {
                    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                });
                if (head.starts_with("COMMIT") || head.starts_with("ROLLBACK") ||
                    head.starts_with("END")) {
                    first = i + 1;
                }
            }
            ImGui::TextColored(col4(p.text_dim), "%s",
                               active.auto_commit() ? TR("auto-commit: every statement "
                                                         "commits on its own")
                                                    : TR("statements of the open transaction"));
            if (ImGui::BeginTable("##txnlog", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_ScrollY,
                                  ImVec2(0, -1))) {
                ImGui::TableSetupColumn(TR("Time"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(TR("Rows"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("SQL");
                ImGui::TableHeadersRow();
                for (std::size_t i = first; i < log.size(); ++i) {
                    if (log[i].internal) continue;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%.1f ms",
                                static_cast<double>(log[i].duration.count()) / 1000.0);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", log[i].rows);
                    ImGui::TableNextColumn();
                    ImGui::TextColored(col4(log[i].failed ? p.error : p.text), "%s",
                                       log[i].sql.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    draw_dashboard_window();
}

// --- Dashboard -------------------------------------------------------------------------

std::vector<std::string> MainShell::dashboard_chart_ids() const {
    std::vector<std::string> ids;
    for (const std::string& id : settings_.dashboard_charts) {
        if (id != "-") ids.push_back(id);
    }
    if (settings_.dashboard_charts.empty()) {
        const Connection* owner = connection_by_id(dashboard_.connection_id);
        const std::string driver = owner != nullptr ? owner->profile.driver_id
                                                    : std::string("postgresql");
        for (const db::DashboardChart& chart : db::dashboard_catalog(driver)) {
            ids.emplace_back(chart.id);
        }
    }
    return ids;
}

void MainShell::dashboard_reset_charts() {
    dashboard_.charts.clear();
    for (const std::string& id : dashboard_chart_ids()) {
        dashboard_.charts.push_back(DashboardChartState{id, {}, {}});
    }
    dashboard_.last_request = -1.0e9;
}

void MainShell::draw_dashboard_window() {
    if (!app_.dashboard) return;
    const Palette& p = colors();

    Connection* owner = connection_by_id(dashboard_.connection_id);
    if (owner == nullptr) {
        app_.dashboard = false;
        return;
    }
    Session& target = *owner->session;
    const std::span<const db::DashboardChart> catalog =
        db::dashboard_catalog(owner->profile.driver_id);
    const auto find_chart = [&catalog](const std::string& id) -> const db::DashboardChart* {
        for (const db::DashboardChart& chart : catalog) {
            if (id == chart.id) return &chart;
        }
        return nullptr;
    };

    // Uma leitura a cada `dashboard_interval` segundos, so' com a sessao
    // livre: o dashboard nunca disputa a conexao com uma consulta do usuario.
    const double now = ImGui::GetTime();
    if (target.state() == SessionState::connected && !target.busy() &&
        now - dashboard_.last_request >= settings_.dashboard_interval) {
        std::vector<std::pair<std::string, std::string>> queries;
        for (const DashboardChartState& chart : dashboard_.charts) {
            if (const db::DashboardChart* known = find_chart(chart.id)) {
                queries.emplace_back(chart.id, known->sql);
            }
        }
        dashboard_.last_request = now;
        target.sample_async(std::move(queries));
    }

    // Leitura nova chegou: um ponto por serie.
    if (target.sample_serial() != dashboard_.seen_serial) {
        dashboard_.seen_serial = target.sample_serial();
        const double seconds = dashboard_.last_sample > 0.0 ? now - dashboard_.last_sample : 0.0;
        dashboard_.last_sample = now;
        const auto samples = target.samples();
        for (DashboardChartState& chart : dashboard_.charts) {
            const auto found = samples.find(chart.id);
            if (found == samples.end()) continue;
            chart.error = found->second.error;
            const db::DashboardChart* known = find_chart(chart.id);
            const bool delta = known != nullptr && known->delta;
            for (const auto& [name, value] : found->second.values) {
                auto series = std::find_if(chart.series.begin(), chart.series.end(),
                                           [&name](const DashboardSeries& s) {
                                               return s.name == name;
                                           });
                if (series == chart.series.end()) {
                    chart.series.push_back(DashboardSeries{name, {}, 0.0, false});
                    series = chart.series.end() - 1;
                }
                // A primeira leitura de um contador so' serve de base.
                if (!delta || series->has_previous) {
                    series->points.push_back(static_cast<float>(
                        db::dashboard_value(delta, series->previous, value, seconds)));
                    const auto limit = static_cast<std::size_t>(settings_.dashboard_points);
                    if (series->points.size() > limit) {
                        series->points.erase(series->points.begin(),
                                             series->points.end() -
                                                 static_cast<std::ptrdiff_t>(limit));
                    }
                }
                series->previous     = value;
                series->has_previous = true;
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(900, 560), ImGuiCond_FirstUseEver);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible = ImGui::Begin(TRW("Dashboard", "###Dashboard"), &app_.dashboard,
                                      ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    if (!visible) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(col4(p.text_dim), "%s  |  %s %d s", owner->profile.effective_name().c_str(),
                       TR("update every"), settings_.dashboard_interval);
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Add chart"))) app_.dashboard_catalog = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(dashboard_.selected.empty());
    if (ImGui::SmallButton(TR("Remove chart"))) queued_commands_.push_back(Command::app_dashboard_remove);
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("View chart"))) queued_commands_.push_back(Command::app_dashboard_view);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Refresh chart"))) dashboard_.last_request = -1.0e9;
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Reset dashboard"))) queued_commands_.push_back(Command::app_dashboard_reset);

    static const std::uint32_t kSeriesColors[] = {0xFF5CB8F0, 0xFF6CC56C, 0xFF4C8CF0,
                                                  0xFFD08060, 0xFFB070D0};

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const bool zoomed = !dashboard_.zoomed.empty();
    const int per_row = zoomed ? 1 : std::max(1, static_cast<int>(available / 330.0f));
    const float width = (available - spacing * static_cast<float>(per_row - 1)) /
                        static_cast<float>(per_row);
    const float height = zoomed ? ImGui::GetContentRegionAvail().y - 8.0f : 190.0f;

    int index = 0;
    for (const DashboardChartState& chart : dashboard_.charts) {
        if (zoomed && chart.id != dashboard_.zoomed) continue;
        const db::DashboardChart* known = find_chart(chart.id);
        if (known == nullptr) continue;
        if (index % per_row != 0) ImGui::SameLine();
        ++index;

        ImGui::PushID(chart.id.c_str());
        const bool selected = dashboard_.selected == chart.id;
        ImGui::PushStyleColor(ImGuiCol_Border,
                              col(selected ? p.accent : with_alpha(p.text_dim, 0.4f)));
        ImGui::BeginChild("##chart", ImVec2(width, height), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dashboard_.selected = chart.id;
        }
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            dashboard_.zoomed = zoomed ? std::string{} : chart.id;
        }

        ImGui::TextColored(col4(p.text_bright), "%s", TR(known->title));
        if (!chart.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
            ImGui::TextWrapped("%s", chart.error.c_str());
            ImGui::PopStyleColor();
        }

        // Legenda com o ultimo valor de cada serie.
        for (std::size_t s = 0; s < chart.series.size(); ++s) {
            const DashboardSeries& series = chart.series[s];
            if (s > 0) ImGui::SameLine();
            // "/s" cola no numero; uma unidade por extenso ("bytes") leva espaco.
            ImGui::TextColored(col4(kSeriesColors[s % 5]), "%s %.1f%s%s", series.name.c_str(),
                               series.points.empty() ? 0.0 : static_cast<double>(series.points.back()),
                               known->unit[0] != '\0' && known->unit[0] != '/' ? " " : "",
                               known->unit);
        }

        // O grafico: linhas sobre uma escala comum, de zero ao maior valor.
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x,
                          std::max(20.0f, ImGui::GetContentRegionAvail().y - 4.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float max_value = 1.0f;
        for (const DashboardSeries& series : chart.series) {
            for (const float v : series.points) max_value = std::max(max_value, v);
        }
        for (int g = 0; g <= 4; ++g) {
            const float y = origin.y + size.y * static_cast<float>(g) / 4.0f;
            draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
                          col(with_alpha(p.text_dim, 0.15f)));
        }
        const auto limit = static_cast<float>(std::max(2, settings_.dashboard_points - 1));
        for (std::size_t s = 0; s < chart.series.size(); ++s) {
            const std::vector<float>& points = chart.series[s].points;
            if (points.size() < 2) continue;
            std::vector<ImVec2> line;
            line.reserve(points.size());
            const float offset = limit - static_cast<float>(points.size() - 1);
            for (std::size_t i = 0; i < points.size(); ++i) {
                line.emplace_back(origin.x + size.x * (offset + static_cast<float>(i)) / limit,
                                  origin.y + size.y * (1.0f - points[i] / max_value));
            }
            draw->AddPolyline(line.data(), static_cast<int>(line.size()),
                              col(kSeriesColors[s % 5]), ImDrawFlags_None, 1.6f);
        }
        const std::string scale = TRF("max %.1f", static_cast<double>(max_value));
        draw->AddText(ImVec2(origin.x + 2.0f, origin.y), col(p.text_dim), scale.c_str());
        ImGui::Dummy(size);
        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::End();

    // Catalogo: marcar e desmarcar graficos.
    if (app_.dashboard_catalog) {
        if (begin_floating(TRW("Chart catalog", "###DashboardCatalog"),
                           &app_.dashboard_catalog, 360.0f,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
            std::vector<std::string> ids = dashboard_chart_ids();
            bool changed = false;
            for (const db::DashboardChart& chart : catalog) {
                bool on = std::find(ids.begin(), ids.end(), chart.id) != ids.end();
                if (ImGui::Checkbox(TR(chart.title), &on)) {
                    if (on) ids.emplace_back(chart.id);
                    else    std::erase(ids, std::string(chart.id));
                    changed = true;
                }
            }
            if (changed) {
                settings_.dashboard_charts = ids.empty() ? std::vector<std::string>{"-"} : ids;
                save_settings();
                dashboard_reset_charts();
            }
        }
        ImGui::End();
    }
    if (app_.dashboard_settings) {
        if (begin_floating(TRW("Dashboard settings", "###DashboardSettings"),
                           &app_.dashboard_settings, 320.0f,
                           ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt(TR("Update period (s)"), &settings_.dashboard_interval)) {
                settings_.dashboard_interval = std::clamp(settings_.dashboard_interval, 1, 3600);
                save_settings();
            }
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputInt(TR("Points per chart"), &settings_.dashboard_points)) {
                settings_.dashboard_points = std::clamp(settings_.dashboard_points, 10, 2000);
                save_settings();
            }
        }
        ImGui::End();
    }
}

} // namespace otter::ui
