// C-Otter -- ui/app_commands.cpp
//
// Os comandos de aplicacao, de banco e do navegador: o menu Database, o menu
// de contexto da arvore e a barra de ferramentas do DBeaver (`core.*`,
// `ui.navigator.*`, `ui.editors.connection.*`, `ui.app.standalone.*`).
//
// A tabela -- rotulo, id no DBeaver, tecla de cada perfil -- esta' em
// ui/commands.cpp. Aqui fica o que cada comando FAZ; as janelas que eles
// abrem estao em ui/app_windows.cpp, e as regras puras (URL, copia avancada,
// filtro, UUID) em db/app_tools.cpp, com teste.
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "base/paths.hpp"
#include "db/app_tools.hpp"
#include "db/ddl.hpp"
#include "db/mysql_object.hpp"
#include "db/native_tools.hpp"
#include "db/object_info.hpp"
#include "db/registry.hpp"
#include "ui/file_dialog.hpp"
#include "ui/hint.hpp"
#include "ui/icon_images.hpp"
#include "ui/platform_open.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID (canal de comandos)

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

using db::ObjectType;

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

bool is_relation_type(ObjectType type) noexcept {
    return type == ObjectType::table || type == ObjectType::view ||
           type == ObjectType::materialized_view || type == ObjectType::foreign_table;
}

bool is_routine_type(ObjectType type) noexcept {
    return type == ObjectType::function || type == ObjectType::procedure;
}

bool same_ref(const db::ObjectRef& a, const db::ObjectRef& b) noexcept {
    return a.type == b.type && a.name == b.name && a.schema == b.schema &&
           a.parent == b.parent && a.signature == b.signature;
}

bool same_target(const db::ConnectionProfile& a, const db::ConnectionProfile& b) {
    return a.driver_id == b.driver_id && a.host == b.host && a.port == b.port &&
           a.database == b.database && a.user == b.user;
}

// Le o arquivo inteiro, em bytes. Vazio se nao abriu.
std::string read_whole_file(const std::string& path) {
    std::ifstream in(std::filesystem::path(std::u8string_view(
                         reinterpret_cast<const char8_t*>(path.data()), path.size())),
                     std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool write_whole_file(const std::string& path, std::string_view bytes) {
    std::ofstream out(std::filesystem::path(std::u8string_view(
                          reinterpret_cast<const char8_t*>(path.data()), path.size())),
                      std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

} // namespace

// --- O alvo dos comandos da arvore ---------------------------------------------------

// Chamado logo depois de cada no' de objeto. Anota o que esta' sob o mouse e
// o que foi clicado, e pinta o selecionado -- a arvore do DBeaver tem selecao,
// e os comandos do menu de contexto agem sobre ela.
void MainShell::nav_track(const db::ObjectRef& ref) {
    if (active_connection_ >= connections_.size()) return;
    const std::size_t id = connections_[active_connection_].id;

    if (ImGui::IsItemHovered()) {
        nav_hovered_     = NavTarget{true, id, ref};
        nav_hover_frame_ = ImGui::GetFrameCount();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            nav_selected_ = NavTarget{true, id, ref};
        }
    }

    if (nav_selected_.valid && nav_selected_.connection_id == id &&
        same_ref(nav_selected_.ref, ref)) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float right = ImGui::GetWindowPos().x + ImGui::GetWindowWidth();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(min.x - 2.0f, min.y), ImVec2(right, max.y),
            static_cast<ImU32>(with_alpha(colors().accent, 0.18f)), 2.0f);
    }
}

const MainShell::NavTarget* MainShell::nav_target() const {
    // A selecao manda, como no DBeaver; sem nada clicado, vale o que esta'
    // sob o mouse (era so' assim que F4 e F2 funcionavam).
    if (nav_selected_.valid && connection_by_id(nav_selected_.connection_id) != nullptr) {
        return &nav_selected_;
    }
    if (nav_hovered_.valid && nav_hover_frame_ >= ImGui::GetFrameCount() - 2 &&
        connection_by_id(nav_hovered_.connection_id) != nullptr) {
        return &nav_hovered_;
    }
    return nullptr;
}

bool MainShell::activate_nav_target(const NavTarget& target) {
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i].id != target.connection_id) continue;
        active_connection_ = i;
        active_profile_    = connections_[i].profile;
        return true;
    }
    return false;
}

// --- Filtro de objetos ----------------------------------------------------------------

const db::ObjectFilter& MainShell::object_filter_for(std::size_t connection_index) const {
    static const db::ObjectFilter kNoFilter{};
    if (connection_index >= connections_.size()) return kNoFilter;
    return connections_[connection_index].filter;
}

// A chave do filtro e' o nome da conexao RAIZ: a sessao de um banco expandido
// na arvore usa o filtro da conexao de que faz parte.
std::string MainShell::filter_key(std::size_t connection_index) const {
    if (connection_index >= connections_.size()) return {};
    const Connection& connection = connections_[connection_index];
    if (connection.parent_id != 0) {
        if (const Connection* root = connection_by_id(connection.parent_id)) {
            return root->profile.effective_name();
        }
    }
    return connection.profile.effective_name();
}

void MainShell::refresh_object_filters() {
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        db::ObjectFilter filter;
        const auto found = settings_.filters.find(filter_key(i));
        if (found != settings_.filters.end()) {
            filter.enabled = found->second.enabled;
            filter.include = db::split_masks(found->second.include);
            filter.exclude = db::split_masks(found->second.exclude);
        }
        connections_[i].filter = std::move(filter);
    }
}

// --- Keep-alive e conexoes ociosas -----------------------------------------------------

void MainShell::tick_connections() {
    const double now = ImGui::GetTime();

    // Os filtros acompanham conexoes que abrem e fecham; refazer a cada meio
    // segundo custa menos que rastrear cada ponto que mexe em connections_.
    if (ImGui::GetFrameCount() % 30 == 0) refresh_object_filters();

    for (Connection& connection : connections_) {
        Session& target = *connection.session;
        if (target.state() != SessionState::connected || target.busy() ||
            connection.last_active == 0.0) {
            connection.last_active = now;
            connection.last_ping   = now;
            continue;
        }

        const db::ConnectionProfile& profile = connection.profile;

        // "Close idle connections": so' sem transacao aberta -- fechar com
        // trabalho pendente seria um rollback que ninguem pediu.
        if (profile.close_idle_connections &&
            target.txn_state() == db::TxnState::idle &&
            target.uncommitted_changes() == 0 &&
            now - connection.last_active >=
                static_cast<double>(profile.close_idle_interval.count())) {
            tree_requests_.disconnect = connection.id;
            show_toast(TRF("'%s' was idle for %d s and was disconnected",
                           profile.effective_name().c_str(),
                           static_cast<int>(profile.close_idle_interval.count())));
            connection.last_active = now;
            continue;
        }

        // Keep-alive. Com as duas opcoes ligadas, fechar ganha (e' o que o
        // dialogo de conexao diz): o ping contaria como uso e a conexao
        // nunca ficaria ociosa.
        if (profile.keep_alive && !profile.close_idle_connections &&
            profile.keep_alive_interval.count() > 0 &&
            now - connection.last_ping >=
                static_cast<double>(profile.keep_alive_interval.count())) {
            connection.last_ping = now;
            const double idle_since = connection.last_active;
            target.ping_async();
            // O ping nao e' uso: nao zera o relogio de ociosidade.
            connection.last_active = idle_since;
        }
    }

    // "Link with editor": a arvore acompanha a aba que vem para a frente.
    if (settings_.link_with_editor) {
        const SqlDocument* front = active_document();
        const std::size_t front_id = front != nullptr ? front->id() : 0;
        if (front_id != linked_document_id_) {
            linked_document_id_ = front_id;
            link_tree_to_editor();
        }
    }

    // Favorito esperando a conexao abrir.
    if (pending_bookmark_) {
        const AppSettings::Bookmark bookmark = *pending_bookmark_;
        pending_bookmark_.reset();
        open_bookmark(bookmark);
    }
}

// --- Habilitacao -----------------------------------------------------------------------

bool MainShell::app_command_enabled(Command command) {
    const CommandInfo& info = command_info(command);
    if (info.state == CommandState::missing ||
        info.state == CommandState::out_of_scope) {
        return false;
    }

    Session& active = session();
    const SessionState state = active.state();
    const bool connected = state == SessionState::connected;
    const bool can_run   = connected && !active.busy();

    SqlDocument* document = active_document();
    const bool is_object = document != nullptr && document->is_object();
    const bool has_script = document != nullptr && !is_object;
    const bool has_result = document != nullptr && document->result().has_value();

    const NavTarget* target = nav_target();
    const bool has_target = target != nullptr;
    const ObjectType type = has_target ? target->ref.type : ObjectType::table;

    switch (command) {
        case Command::app_commit:
        case Command::app_rollback:
            return can_run && !active.auto_commit() &&
                   active.txn_state() != db::TxnState::idle;
        case Command::app_auto_commit:
        case Command::app_read_only:
        case Command::app_select_schema:
            return can_run;
        case Command::app_change_password:
            return can_run;
        case Command::app_txn_log:
        case Command::app_dashboard_open:
            return connected;

        case Command::app_connect:
            return state == SessionState::disconnected || state == SessionState::failed;
        case Command::app_disconnect:
        case Command::app_reconnect:
            return state == SessionState::connected || state == SessionState::failed;
        case Command::app_disconnect_all:
            return std::any_of(connections_.begin(), connections_.end(),
                               [](const Connection& c) {
                                   return c.session->state() != SessionState::disconnected;
                               });
        case Command::app_disconnect_others: {
            const std::size_t active_id =
                active_connection_ < connections_.size()
                    ? connections_[active_connection_].id : 0;
            return std::any_of(connections_.begin(), connections_.end(),
                               [active_id](const Connection& c) {
                                   return c.id != active_id && c.parent_id != active_id &&
                                          c.session->state() != SessionState::disconnected;
                               });
        }

        case Command::app_set_default:
            return has_target && can_run &&
                   (type == ObjectType::schema || type == ObjectType::database);
        case Command::app_object_open:
        case Command::app_bookmark_add:
        case Command::app_filter_include:
        case Command::app_filter_exclude:
            return has_target;
        case Command::app_object_create:
            return has_target && can_run && can_create_object(type);
        case Command::app_object_delete:
            return has_target && can_run;
        case Command::app_object_rename:
            return has_target && can_run &&
                   db::editable_property(type, "Name") != db::ObjectEdit::none;
        case Command::app_view_data:
        case Command::app_read_data_console:
        case Command::app_export_data:
            return has_target && connected && is_relation_type(type);
        case Command::app_import_data:
            return has_target && can_run && type == ObjectType::table;
        case Command::app_tools_menu:
            return has_target && can_run;
        case Command::app_column_index:
        case Command::app_column_constraint:
            return has_target && can_run && type == ObjectType::column;
        case Command::app_procedure_execute:
            return has_target && connected && is_routine_type(type);

        case Command::app_filter_toggle:
        case Command::app_filter_clear: {
            const auto found = settings_.filters.find(filter_key(active_connection_));
            return found != settings_.filters.end() &&
                   (!found->second.include.empty() || !found->second.exclude.empty());
        }
        case Command::app_filter_config:
            return active_connection_ < connections_.size();

        case Command::app_move_up:
        case Command::app_move_down:
        case Command::app_move_top:
        case Command::app_move_bottom:
            return std::any_of(saved_profiles_.begin(), saved_profiles_.end(),
                               [this](const db::StoredProfile& stored) {
                                   return same_target(stored.profile, active_profile_);
                               });
        case Command::app_bookmark_navigate:
            return !settings_.bookmarks.empty();

        case Command::app_props_next:
        case Command::app_props_previous:
        case Command::app_source_tab:
            return is_object;

        case Command::app_copy_special:
        case Command::app_copy_special_last:
        case Command::app_open_spreadsheet:
            return has_result;
        case Command::app_save_resource:
            return has_result && has_selection_ &&
                   selected_document_ == document->id();
        case Command::app_paste_special:
        case Command::app_load_resource:
            return has_result && has_selection_ &&
                   selected_document_ == document->id() &&
                   document->edit_target().editable();

        case Command::app_generate_uuid:
            return document != nullptr;
        case Command::app_script_associate:
            return has_script;
        case Command::app_show_in_explorer:
            return has_script && !document->file_path().empty();

        case Command::app_process_stop:
            return tool_run_.running || active.transfer_state().running;

        case Command::app_dashboard_add:
        case Command::app_dashboard_catalog:
        case Command::app_dashboard_configure:
        case Command::app_dashboard_refresh:
        case Command::app_dashboard_reset:
            return app_.dashboard && connected;
        case Command::app_dashboard_remove:
        case Command::app_dashboard_view:
            return app_.dashboard && !dashboard_.selected.empty();

        case Command::edit_move_lines_up:
        case Command::edit_move_lines_down:
        case Command::edit_join_lines:
        case Command::edit_word_completion:
            return has_script;

        default:
            return true;
    }
}

bool MainShell::app_command_checked(Command command) {
    switch (command) {
        case Command::app_auto_commit:  return session().auto_commit();
        case Command::app_read_only:
            return active_connection_ < connections_.size() &&
                   connections_[active_connection_].profile.read_only;
        case Command::app_link_editor:  return settings_.link_with_editor;
        case Command::app_filter_connected: return !settings_.connected_only;
        case Command::app_filter_toggle: {
            const auto found = settings_.filters.find(filter_key(active_connection_));
            return found != settings_.filters.end() && found->second.enabled;
        }
        case Command::app_dashboard_open: return app_.dashboard;
        case Command::app_txn_pending:    return app_.txn_pending;
        case Command::app_txn_log:        return app_.txn_log;
        default:                          return false;
    }
}

// --- Execucao --------------------------------------------------------------------------

void MainShell::run_app_command(Command command) {
    Session& active = session();
    const bool pg = active.is_postgres();
    SqlDocument* document = active_document();
    const NavTarget* target = nav_target();

    const std::size_t active_id =
        active_connection_ < connections_.size() ? connections_[active_connection_].id : 0;

    // Os comandos que agem sobre um no' da arvore falam com a sessao DELE.
    const auto on_target = [&]() -> const db::ObjectRef* {
        if (target == nullptr || !activate_nav_target(*target)) return nullptr;
        return &target->ref;
    };

    switch (command) {
        // --- Transacoes -------------------------------------------------------------
        case Command::app_commit:       active.commit_async();   return;
        case Command::app_rollback:     active.rollback_async(); return;
        case Command::app_auto_commit:
            active.set_auto_commit_async(!active.auto_commit());
            return;
        case Command::app_txn_pending:  app_.txn_pending = !app_.txn_pending; return;
        case Command::app_txn_log:      app_.txn_log = !app_.txn_log;         return;

        // --- Conexao ----------------------------------------------------------------
        case Command::app_connect:
        case Command::app_reconnect:
            if (active_id != 0) tree_requests_.reconnect = active_id;
            return;
        case Command::app_disconnect:
            if (active_id != 0) tree_requests_.disconnect = active_id;
            return;
        case Command::app_disconnect_all:
        case Command::app_disconnect_others: {
            const bool others = command == Command::app_disconnect_others;
            // Os ids primeiro: fechar as sessoes de banco de uma raiz remove
            // entradas de connections_, e iterar sobre ele ao mesmo tempo
            // pularia vizinhas.
            std::vector<std::size_t> roots;
            for (const Connection& connection : connections_) {
                if (connection.parent_id != 0) continue;
                if (others && connection.id == active_id) continue;
                if (others && active_connection_ < connections_.size() &&
                    connections_[active_connection_].parent_id == connection.id) {
                    continue;   // a raiz da sessao ativa fica
                }
                if (connection.session->state() == SessionState::disconnected) continue;
                roots.push_back(connection.id);
            }
            for (const std::size_t id : roots) {
                close_database_connections(id);
                if (Connection* connection = connection_by_id(id)) {
                    connection->session->disconnect();
                }
            }
            show_toast(TRF("%zu connection(s) closed", roots.size()));
            return;
        }
        case Command::app_read_only: {
            if (active_connection_ >= connections_.size()) return;
            // O SQL Server nao tem sessao somente-leitura (so' o banco inteiro,
            // por ALTER DATABASE): dizer isso e' melhor que marcar uma caixa
            // que nao protege nada.
            if (active.is_mssql()) {
                show_toast(TR("SQL Server has no read-only session mode"));
                return;
            }
            // O SQL Anywhere tambem nao: so' o banco inteiro, ao abri-lo (-r).
            if (active.is_sqlanywhere()) {
                show_toast(TR("SQL Anywhere has no read-only session mode"));
                return;
            }
            db::ConnectionProfile& profile = connections_[active_connection_].profile;
            profile.read_only = !profile.read_only;
            const char* mode = profile.read_only ? "ONLY" : "WRITE";
            active.run_statement_async(
                pg ? std::string("SET SESSION CHARACTERISTICS AS TRANSACTION READ ") + mode
                   : std::string("SET SESSION TRANSACTION READ ") + mode,
                profile.read_only ? TR("connection is now read-only")
                                  : TR("connection is now read-write"));
            // Gravado no perfil, como o DBeaver: a proxima conexao nasce assim.
            if (connections_[active_connection_].parent_id == 0) {
                active_profile_ = remember_profile(profile);
            }
            return;
        }
        case Command::app_new_connection:
            connection_dialog_.open_new();
            return;
        case Command::app_new_connection_url:
            app_.url_dialog = true;
            app_.url[0]     = '\0';
            app_.url_error.clear();
            app_.focus_search = true;
            return;
        case Command::app_new_folder:
            open_new_folder(std::string{});
            return;
        case Command::app_select_connection:
            app_.select_connection = true;
            app_.search[0]    = '\0';
            app_.search_cursor = 0;
            app_.focus_search = true;
            return;
        case Command::app_select_schema:
            app_.select_schema = true;
            app_.search[0]    = '\0';
            app_.search_cursor = 0;
            app_.focus_search = true;
            return;
        case Command::app_set_default: {
            const db::ObjectRef* ref = on_target();
            if (ref == nullptr) return;
            if (ref->type == ObjectType::database) {
                // No PostgreSQL o banco e' da conexao: "tornar padrao" e' abrir
                // a sessao dele, que a arvore faz ao expandir.
                if (active_connection_ < connections_.size()) {
                    const Connection& here = connections_[active_connection_];
                    tree_requests_.open_database      = ref->name;
                    tree_requests_.open_database_root =
                        here.parent_id != 0 ? here.parent_id : here.id;
                }
                return;
            }
            set_default_schema(ref->name);
            return;
        }
        case Command::app_goto_object:
            app_.goto_object  = true;
            app_.search[0]    = '\0';
            app_.search_cursor = 0;
            app_.focus_search = true;
            return;

        // --- Objetos ----------------------------------------------------------------
        case Command::app_object_open:
            if (const db::ObjectRef* ref = on_target()) open_object_editor(*ref);
            return;
        case Command::app_object_create:
            if (const db::ObjectRef* ref = on_target()) {
                // O mesmo que "Create New <tipo>" do menu do no'.
                const db::ObjectRef copy = *ref;
                if (copy.type == ObjectType::table) {
                    open_create_table(copy.schema);
                } else if (copy.type == ObjectType::view) {
                    open_create_view(copy.schema);
                } else if (copy.type == ObjectType::column || copy.type == ObjectType::index) {
                    if (const auto table = session().table(copy.schema, copy.parent)) {
                        if (copy.type == ObjectType::column) open_add_column(copy.schema, *table);
                        else                                 open_add_index(copy.schema, *table);
                    }
                } else {
                    db::ObjectRef fresh;
                    fresh.type   = copy.type;
                    fresh.schema = copy.schema;
                    fresh.parent = copy.parent;
                    open_object_form(ObjectForm::Kind::create, std::move(fresh));
                }
            }
            return;
        case Command::app_object_delete:
            if (const db::ObjectRef* ref = on_target()) {
                if (object_form_.kind == ObjectForm::Kind::none) {
                    open_object_form(ObjectForm::Kind::drop, *ref);
                }
            }
            return;
        case Command::app_object_rename:
            if (const db::ObjectRef* ref = on_target()) {
                if (object_form_.kind == ObjectForm::Kind::none) {
                    open_object_form(ObjectForm::Kind::rename, *ref);
                }
            }
            return;
        case Command::app_view_data:
            if (const db::ObjectRef* ref = on_target()) {
                open_object_editor(*ref, /*data=*/true);
            }
            return;
        case Command::app_read_data_console:
            if (const db::ObjectRef* ref = on_target()) {
                const db::ObjectRef copy = *ref;
                if (const auto table = session().table(copy.schema, copy.name)) {
                    open_sql_tab(db::generate_select(copy.schema, *table), /*run=*/true);
                } else {
                    open_sql_tab("SELECT * FROM " +
                                     db::qualified_name(copy.schema, copy.name),
                                 /*run=*/true);
                }
            }
            return;
        case Command::app_export_data:
            if (const db::ObjectRef* ref = on_target()) {
                open_object_editor(*ref, /*data=*/true);
                export_object_pending_ = true;
            }
            return;
        case Command::app_import_data:
            if (const db::ObjectRef* ref = on_target()) open_import(ref->schema, ref->name);
            return;
        case Command::app_tools_menu:
            if (on_target() != nullptr) app_.tools_popup = true;
            return;
        case Command::app_column_index:
            if (const db::ObjectRef* ref = on_target()) {
                const db::ObjectRef copy = *ref;
                if (const auto table = session().table(copy.schema, copy.parent)) {
                    open_add_index(copy.schema, *table);
                    // A coluna escolhida ja' vem marcada.
                    for (auto& [name, checked] : index_form_.columns) {
                        checked = name == copy.name;
                    }
                    std::snprintf(index_form_.name, sizeof index_form_.name, "%s_%s_idx",
                                  copy.parent.c_str(), copy.name.c_str());
                }
            }
            return;
        case Command::app_column_constraint:
            if (const db::ObjectRef* ref = on_target()) {
                const db::ObjectRef copy = *ref;
                db::NewConstraint constraint;
                constraint.kind    = db::ConstraintKind::unique;
                constraint.name    = copy.parent + "_" + copy.name + "_key";
                constraint.columns = {copy.name};
                confirm_ddl(TRF("New constraint on %s", copy.parent.c_str()),
                            db::generate_add_constraint(copy.schema, copy.parent, constraint),
                            copy.schema, copy.parent);
            }
            return;
        case Command::app_procedure_execute:
            if (const db::ObjectRef* ref = on_target()) {
                const db::ObjectRef copy = *ref;
                db::RoutineMeta routine;
                routine.name = copy.name;
                routine.kind = copy.type == ObjectType::procedure ? db::ObjKind::procedure
                                                                  : db::ObjKind::function;
                // A assinatura completa (com os nomes) vem do catalogo ja'
                // carregado; sem ela, vale a do no' -- so' os tipos.
                routine.arguments = copy.signature;
                for (const db::SchemaMeta& schema : session().schemas()) {
                    if (schema.name != copy.schema) continue;
                    for (const db::RoutineMeta& known : schema.routines) {
                        if (known.name == copy.name &&
                            (copy.signature.empty() || known.signature == copy.signature)) {
                            routine = known;
                        }
                    }
                }
                open_sql_tab(db::routine_call_sql(copy.schema, routine, active.is_mysql()),
                             /*run=*/false);
                focus_editor_ = true;
            }
            return;

        // --- Filtro ------------------------------------------------------------------
        case Command::app_filter_toggle: {
            AppSettings::Filter& filter = settings_.filters[filter_key(active_connection_)];
            filter.enabled = !filter.enabled;
            save_settings();
            refresh_object_filters();
            return;
        }
        case Command::app_filter_config: {
            const AppSettings::Filter& filter =
                settings_.filters[filter_key(active_connection_)];
            std::snprintf(app_.filter_include, sizeof app_.filter_include, "%s",
                          filter.include.c_str());
            std::snprintf(app_.filter_exclude, sizeof app_.filter_exclude, "%s",
                          filter.exclude.c_str());
            app_.filter_config = true;
            app_.focus_search  = true;
            return;
        }
        case Command::app_filter_clear:
            settings_.filters.erase(filter_key(active_connection_));
            save_settings();
            refresh_object_filters();
            return;
        case Command::app_filter_include:
        case Command::app_filter_exclude:
            if (const db::ObjectRef* ref = on_target()) {
                AppSettings::Filter& filter =
                    settings_.filters[filter_key(active_connection_)];
                std::vector<std::string> masks = db::split_masks(
                    command == Command::app_filter_include ? filter.include
                                                           : filter.exclude);
                if (std::find(masks.begin(), masks.end(), ref->name) == masks.end()) {
                    masks.push_back(ref->name);
                }
                (command == Command::app_filter_include ? filter.include
                                                        : filter.exclude) =
                    db::join_masks(masks);
                filter.enabled = true;
                save_settings();
                refresh_object_filters();
            }
            return;
        case Command::app_filter_connected:
            settings_.connected_only = !settings_.connected_only;
            save_settings();
            return;
        case Command::app_filter_focus:
            ImGui::SetWindowFocus("###NavigatorPanel");
            app_.focus_nav_filter = true;
            return;
        case Command::app_link_editor:
            settings_.link_with_editor = !settings_.link_with_editor;
            save_settings();
            if (settings_.link_with_editor) link_tree_to_editor();
            return;

        case Command::app_move_up:     move_saved_profile(-1, false); return;
        case Command::app_move_down:   move_saved_profile(+1, false); return;
        case Command::app_move_top:    move_saved_profile(-1, true);  return;
        case Command::app_move_bottom: move_saved_profile(+1, true);  return;

        case Command::app_bookmark_add:
            if (target != nullptr) {
                const Connection* owner = connection_by_id(target->connection_id);
                if (owner == nullptr) return;

                AppSettings::Bookmark bookmark;
                bookmark.title     = target->ref.title();
                bookmark.type      = std::string(db::to_string(target->ref.type));
                bookmark.schema    = target->ref.schema;
                bookmark.name      = target->ref.name;
                bookmark.parent    = target->ref.parent;
                bookmark.signature = target->ref.signature;
                if (owner->parent_id != 0) {
                    const Connection* root = connection_by_id(owner->parent_id);
                    bookmark.connection =
                        root != nullptr ? root->profile.effective_name() : std::string{};
                    bookmark.database = owner->profile.database;
                } else {
                    bookmark.connection = owner->profile.effective_name();
                }
                settings_.bookmarks.push_back(std::move(bookmark));
                save_settings();
                show_toast(TRF("bookmark added: %s", target->ref.title().c_str()));
            }
            return;
        case Command::app_bookmark_navigate:
            app_.bookmarks = true;
            return;

        // --- Editor de objeto --------------------------------------------------------
        case Command::app_props_next:
        case Command::app_props_previous:
            if (document != nullptr && document->is_object()) {
                ObjectView& view = *document->object();
                view.page         = ObjectView::Page::properties;
                view.select_page  = true;
                view.step_section = command == Command::app_props_next ? 1 : -1;
            }
            return;
        case Command::app_source_tab:
            if (document != nullptr && document->is_object()) {
                ObjectView& view = *document->object();
                view.page        = ObjectView::Page::properties;
                view.select_page = true;
                view.goto_source = true;
            }
            return;

        // --- Grade --------------------------------------------------------------------
        case Command::app_copy_special:
            std::snprintf(app_.copy_column, sizeof app_.copy_column, "%s",
                          settings_.copy_column_delimiter.c_str());
            std::snprintf(app_.copy_row, sizeof app_.copy_row, "%s",
                          settings_.copy_row_delimiter.c_str());
            std::snprintf(app_.copy_quote, sizeof app_.copy_quote, "%s",
                          settings_.copy_quote.c_str());
            std::snprintf(app_.copy_null, sizeof app_.copy_null, "%s",
                          settings_.copy_null_text.c_str());
            app_.copy_special = true;
            return;
        case Command::app_copy_special_last:
            copy_special_now();
            return;
        case Command::app_paste_special:
            app_.paste_special = true;
            return;
        case Command::app_generate_uuid: {
            const std::string uuid = db::generate_uuid();
            if (document == nullptr) return;
            // Com a grade em foco e a celula editavel, o UUID vai para ela;
            // senao, para o cursor do editor.
            if (grid_focused_ && has_selection_ && selected_document_ == document->id() &&
                document->edit_target().editable()) {
                document->edits().set(selected_row_, selected_column_, uuid);
            } else if (!document->is_object()) {
                document->editor().ReplaceTextInAllCursors(uuid);
            } else {
                ImGui::SetClipboardText(uuid.c_str());
                show_toast(TRF("UUID copied: %s", uuid.c_str()));
            }
            return;
        }
        case Command::app_load_resource: {
            if (document == nullptr) return;
            const auto path = open_file_dialog(TR("Load resource(s) from local disk"),
                                               {{"*", "*.*"}});
            if (!path) return;
            load_cell_from_file(*document, *path);
            return;
        }
        case Command::app_save_resource: {
            if (document == nullptr || !document->result()) return;
            const db::ResultSet& rs = *document->result();
            const std::string name = rs.column(selected_column_).info().name;
            const auto path = save_file_dialog(TR("Save resource(s) to local disk"),
                                               {{"*", "*.*"}}, name + ".txt");
            if (!path) return;
            save_cell_to_file(*document, *path);
            return;
        }
        case Command::app_open_spreadsheet: {
            if (document == nullptr || !document->result()) return;
            namespace fs = std::filesystem;
            db::ExportOptions options;
            options.format = db::ExportFormat::csv;

            // Na pasta de dados do programa (portatil, ADR 0020), nao no
            // temporario do usuario.
            std::error_code ec;
            const fs::path folder = fs::path(data_directory()) / "exports";
            fs::create_directories(folder, ec);
            const fs::path path =
                folder / ("result-" + std::to_string(document->id()) + ".csv");
            if (ec || !db::export_to_file(*document->result(), options, path.string())) {
                show_toast(TR("could not write the temporary file"));
                return;
            }
            if (!open_path(path.string())) {
                show_toast(TR("no application is associated with CSV files"));
            }
            return;
        }

        // --- Scripts --------------------------------------------------------------------
        case Command::app_script_associate:
            app_.associate = true;
            return;
        case Command::app_show_scripts:
            app_.scripts      = true;
            app_.search[0]    = '\0';
            app_.search_cursor = 0;
            app_.focus_search = true;
            return;
        case Command::app_show_in_explorer:
            if (document != nullptr && !document->file_path().empty()) {
                if (!reveal_in_file_manager(document->file_path())) {
                    show_toast(TR("could not open the file manager"));
                }
            }
            return;
        case Command::edit_open_local_file:
            open_script_file();
            return;

        // --- Aplicacao --------------------------------------------------------------------
        case Command::app_change_password: {
            if (active_connection_ >= connections_.size()) return;
            db::ObjectRef role;
            role.type = ObjectType::role;
            // No MySQL a conta e' o par usuario@host, e o host com que a
            // sessao foi aceita so' o servidor sabe: nome vazio gera
            // ALTER USER USER(), que e' "a minha conta".
            if (!active.is_mysql()) {
                role.name = connections_[active_connection_].profile.user;
            }
            open_object_form(ObjectForm::Kind::password, std::move(role));
            return;
        }
        case Command::app_driver_manager: app_.drivers = true;     return;
        case Command::app_preferences:    app_.preferences = true; return;
        case Command::app_view_toggle:    app_.views = true;       return;
        case Command::app_log_clear:
            active.clear_query_log();
            return;
        case Command::app_log_filter:
            app_.log_filter = true;
            return;
        case Command::app_process_stop:
            if (tool_run_.running) tool_run_.process.kill();
            if (active.transfer_state().running) active.cancel_transfer();
            return;
        case Command::app_clear_history:   app_.confirm_history = true; return;
        case Command::app_reset_settings:  app_.confirm_reset = true;   return;
        case Command::app_collect_diagnostics: {
            app_.diagnostics = diagnostics_text();
            namespace fs = std::filesystem;
            const fs::path path = fs::path(data_directory()) / "diagnostics.txt";
            app_.diagnostics_path =
                write_whole_file(path.string(), app_.diagnostics) ? path.string()
                                                                  : std::string{};
            app_.diagnostics_open = true;
            return;
        }

        // --- Dashboard ----------------------------------------------------------------------
        case Command::app_dashboard_open:
            app_.dashboard = !app_.dashboard;
            if (app_.dashboard) {
                dashboard_ = {};
                dashboard_.connection_id = active_id;
                dashboard_reset_charts();
            }
            return;
        case Command::app_dashboard_add:
        case Command::app_dashboard_catalog:
            app_.dashboard_catalog = true;
            return;
        case Command::app_dashboard_remove: {
            std::vector<std::string> ids = dashboard_chart_ids();
            std::erase(ids, dashboard_.selected);
            settings_.dashboard_charts = std::move(ids);
            // Lista vazia significaria "os padroes": guarda um marcador.
            if (settings_.dashboard_charts.empty()) {
                settings_.dashboard_charts.emplace_back("-");
            }
            save_settings();
            dashboard_.selected.clear();
            dashboard_.zoomed.clear();
            dashboard_reset_charts();
            return;
        }
        case Command::app_dashboard_configure:
            app_.dashboard_settings = true;
            return;
        case Command::app_dashboard_refresh:
            dashboard_.last_request = -1.0e9;   // a proxima leitura sai ja'
            return;
        case Command::app_dashboard_view:
            dashboard_.zoomed =
                dashboard_.zoomed == dashboard_.selected ? std::string{}
                                                         : dashboard_.selected;
            return;
        case Command::app_dashboard_reset:
            settings_.dashboard_charts.clear();
            save_settings();
            dashboard_.selected.clear();
            dashboard_.zoomed.clear();
            dashboard_reset_charts();
            return;

        case Command::app_command_palette:
            app_.palette      = true;
            app_.search[0]    = '\0';
            app_.search_cursor = 0;
            app_.focus_search = true;
            return;
        case Command::app_help:
            show_shortcuts_ = true;
            return;
        case Command::app_exit:
            request_quit();
            return;

        // --- Editor de texto -------------------------------------------------------------
        case Command::edit_move_lines_up:
            if (document != nullptr) document->editor().MoveLinesUp();
            return;
        case Command::edit_move_lines_down:
            if (document != nullptr) document->editor().MoveLinesDown();
            return;
        case Command::edit_join_lines: {
            if (document == nullptr) return;
            TextEditor& editor = document->editor();
            const std::size_t line = editor.GetMainCursorPosition().line;
            if (line + 1 >= editor.GetLineCount()) return;

            const std::string first  = editor.GetLineText(line);
            const std::string second = editor.GetLineText(line + 1);
            const std::string joined = db::join_lines(first, second);
            editor.ReplaceSectionText(
                TextEditor::DocPos(line, 0),
                TextEditor::DocPos(line + 1, second.size()), joined);
            return;
        }
        case Command::edit_word_completion: {
            if (document == nullptr) return;
            TextEditor& editor = document->editor();
            const TextEditor::DocPos cursor = editor.GetMainCursorPosition();
            const std::string line = editor.GetLineText(cursor.line);

            // A palavra a' esquerda do cursor. `index` do widget e' em pontos
            // de codigo, e a linha esta' em UTF-8: anda pelos bytes contando.
            std::size_t byte = 0;
            for (std::size_t point = 0; point < cursor.index && byte < line.size(); ++point) {
                ++byte;
                while (byte < line.size() &&
                       (static_cast<unsigned char>(line[byte]) & 0xC0) == 0x80) {
                    ++byte;
                }
            }
            std::size_t start = byte;
            const auto word_char = [](char c) {
                return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ||
                       (static_cast<unsigned char>(c) & 0x80) != 0;
            };
            while (start > 0 && word_char(line[start - 1])) --start;
            const std::string typed = line.substr(start, byte - start);
            if (typed.empty()) return;

            // Apertar de novo troca a palavra completada pela proxima: o
            // prefixo e' o que foi DIGITADO, guardado da primeira vez.
            if (app_.word_last != typed) app_.word_prefix = typed;
            const std::string next =
                db::complete_word(editor.GetText(), app_.word_prefix, typed);
            if (next.empty()) return;

            std::size_t start_point = 0;
            for (std::size_t i = 0; i < start; ++i) {
                if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) ++start_point;
            }
            editor.ReplaceSectionText(TextEditor::DocPos(cursor.line, start_point),
                                      cursor, next);
            app_.word_last = next;
            return;
        }

        default:
            return;
    }
}

// --- O que os comandos usam ---------------------------------------------------------------

// Torna `schema` o schema padrao da sessao ativa -- "Set as default" na
// arvore e "Select active schema" (Ctrl+0).
void MainShell::set_default_schema(const std::string& schema) {
    Session& active = session();
    if (active.state() != SessionState::connected || active.busy()) return;

    // O SQL Server nao troca o schema padrao da SESSAO: ele e' do usuario do
    // banco (ALTER USER ... WITH DEFAULT_SCHEMA), e muda-lo aqui valeria para
    // todo mundo que usa essa conta.
    if (active.is_mssql()) {
        show_toast(TR("SQL Server has no session default schema: qualify the names"));
        return;
    }
    // No SQL Anywhere o nome sem dono procura nos objetos do usuario conectado
    // e nos dos papeis dele; nao ha' comando que troque isso na sessao.
    if (active.is_sqlanywhere()) {
        show_toast(TR("SQL Anywhere has no session default schema: qualify the names"));
        return;
    }
    db::SessionSetup setup;
    setup.default_schema = schema;
    const std::vector<std::string> statements = db::session_setup_statements(
        active.is_postgres() ? "postgresql" : "mysql", setup);
    if (statements.empty()) return;

    active.run_statement_async(statements.front(),
                               TRF("default schema: %s", schema.c_str()));
}

// "Link with editor": a arvore mostra o objeto (ou a conexao) da aba ativa.
void MainShell::link_tree_to_editor() {
    const SqlDocument* document = active_document();
    if (document == nullptr) return;
    const Connection* owner = connection_by_id(document->connection_id());
    if (owner == nullptr) return;

    for (std::size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i].id == owner->id) {
            active_connection_ = i;
            active_profile_    = connections_[i].profile;
        }
    }

    if (const ObjectView* view = document->object()) {
        nav_selected_ = NavTarget{true, owner->id, view->ref};
        if (is_relation_type(view->ref.type)) {
            tree_reveal_ = TreeReveal{owner->id, view->ref.schema, view->ref.name,
                                      kRevealFrames};
        }
    }
}

void MainShell::move_saved_profile(int direction, bool to_edge) {
    const auto current = std::find_if(
        saved_profiles_.begin(), saved_profiles_.end(),
        [this](const db::StoredProfile& stored) {
            return same_target(stored.profile, active_profile_);
        });
    if (current == saved_profiles_.end()) return;

    // So' entre as conexoes da MESMA pasta: e' a ordem que a arvore mostra.
    std::vector<std::size_t> siblings;
    for (std::size_t i = 0; i < saved_profiles_.size(); ++i) {
        if (saved_profiles_[i].profile.folder == current->profile.folder) {
            siblings.push_back(i);
        }
    }
    const std::size_t index =
        static_cast<std::size_t>(current - saved_profiles_.begin());
    const auto at = std::find(siblings.begin(), siblings.end(), index);
    auto position = static_cast<std::ptrdiff_t>(at - siblings.begin());
    const auto last = static_cast<std::ptrdiff_t>(siblings.size()) - 1;

    while (true) {
        const std::ptrdiff_t next = position + direction;
        if (next < 0 || next > last) break;
        std::swap(saved_profiles_[siblings[static_cast<std::size_t>(position)]],
                  saved_profiles_[siblings[static_cast<std::size_t>(next)]]);
        position = next;
        if (!to_edge) break;
    }
    persist_profiles();
}

void MainShell::open_bookmark(const AppSettings::Bookmark& bookmark) {
    // A conexao do favorito, pelo nome.
    std::size_t root = kNone;
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i].parent_id == 0 &&
            connections_[i].profile.effective_name() == bookmark.connection) {
            root = i;
        }
    }

    if (root == kNone ||
        connections_[root].session->state() == SessionState::disconnected ||
        connections_[root].session->state() == SessionState::failed) {
        // Fechada: conecta, e o favorito abre quando ela estiver pronta.
        for (const db::StoredProfile& stored : saved_profiles_) {
            if (stored.profile.effective_name() != bookmark.connection) continue;
            if (root == kNone) open_connection(stored.profile);
            else               tree_requests_.reconnect = connections_[root].id;
            pending_bookmark_ = bookmark;
            return;
        }
        show_toast(TRF("connection '%s' no longer exists", bookmark.connection.c_str()));
        return;
    }
    if (connections_[root].session->state() != SessionState::connected) {
        pending_bookmark_ = bookmark;   // ainda conectando
        return;
    }

    std::size_t owner = root;
    if (!bookmark.database.empty() &&
        bookmark.database != connections_[root].profile.database) {
        const std::size_t child =
            find_database_connection(connections_[root].id, bookmark.database);
        if (child == kNone) {
            open_database_connection(root, bookmark.database);
            pending_bookmark_ = bookmark;
            return;
        }
        if (connections_[child].session->state() != SessionState::connected) {
            if (connections_[child].session->state() == SessionState::connecting) {
                pending_bookmark_ = bookmark;
            } else {
                show_toast(TRF("database '%s' is not available",
                               bookmark.database.c_str()));
            }
            return;
        }
        owner = child;
    }

    db::ObjectRef ref;
    bool known = false;
    for (int t = 0; t < db::kObjectTypeCount; ++t) {
        if (db::to_string(static_cast<ObjectType>(t)) == bookmark.type) {
            ref.type = static_cast<ObjectType>(t);
            known    = true;
        }
    }
    if (!known) return;
    ref.schema    = bookmark.schema;
    ref.name      = bookmark.name;
    ref.parent    = bookmark.parent;
    ref.signature = bookmark.signature;

    active_connection_ = owner;
    active_profile_    = connections_[owner].profile;
    nav_selected_      = NavTarget{true, connections_[owner].id, ref};
    open_object_editor(std::move(ref));
}

// Copia a selecao com as opcoes guardadas ("Advanced copy").
void MainShell::copy_special_now() {
    SqlDocument* document = active_document();
    if (document == nullptr || !document->result()) return;
    const db::ResultSet& rs = *document->result();

    db::CopyOptions options;
    options.column_delimiter = db::unescape_delimiter(settings_.copy_column_delimiter);
    options.row_delimiter    = db::unescape_delimiter(settings_.copy_row_delimiter);
    options.quote            = settings_.copy_quote;
    options.quote_always     = settings_.copy_quote_always;
    options.copy_header      = settings_.copy_header;
    options.copy_row_numbers = settings_.copy_row_numbers;
    options.null_text        = settings_.copy_null_text;

    db::GridSelection selection = grid_selection(*document, rs);
    options.first_row_number =
        (document->paged() ? document->page() * document->page_size() : 0) +
        selection.row_first + 1;

    const std::string text = db::advanced_copy(rs, document->edits(), selection, options);
    ImGui::SetClipboardText(text.c_str());
    show_toast(TRF("%zu row(s) copied",
                   selection.row_last >= selection.row_first
                       ? selection.row_last - selection.row_first + 1
                       : static_cast<std::size_t>(0)));
}

void MainShell::load_cell_from_file(SqlDocument& document, const std::string& path) {
    if (!has_selection_ || !document.result()) return;
    const db::ResultSet& rs = *document.result();
    if (selected_column_ >= rs.column_count()) return;

    const std::string bytes = read_whole_file(path);
    if (bytes.empty()) {
        show_toast(TR("the file is empty or could not be read"));
        return;
    }

    // Coluna binaria: o conteudo vai como literal hexadecimal do bytea
    // ("\x..."), que e' o que o servidor aceita num UPDATE em texto. O resto
    // vai como texto.
    std::string value;
    if (rs.column(selected_column_).info().kind == db::DataKind::binary) {
        static constexpr char kHex[] = "0123456789abcdef";
        value.reserve(bytes.size() * 2 + 2);
        value += "\\x";
        for (const char c : bytes) {
            const auto byte = static_cast<unsigned char>(c);
            value += kHex[byte >> 4];
            value += kHex[byte & 0x0F];
        }
    } else {
        value = bytes;
    }
    document.edits().set(selected_row_, selected_column_, std::move(value));
    show_toast(TRF("%zu byte(s) loaded into the cell; save to apply", bytes.size()));
}

void MainShell::save_cell_to_file(SqlDocument& document, const std::string& path) {
    if (!has_selection_ || !document.result()) return;
    const db::ResultSet& rs = *document.result();
    if (selected_row_ >= rs.row_count() || selected_column_ >= rs.column_count()) return;

    const db::CellValue value =
        db::cell_value(rs, document.edits(), selected_row_, selected_column_);

    std::string bytes = value.text;
    // bytea chega como "\x4f74..."; no arquivo vao os BYTES, nao o texto.
    if (rs.column(selected_column_).info().kind == db::DataKind::binary &&
        bytes.starts_with("\\x")) {
        std::string raw;
        raw.reserve(bytes.size() / 2);
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
            const int high = nibble(bytes[i]);
            const int low  = nibble(bytes[i + 1]);
            if (high < 0 || low < 0) break;
            raw += static_cast<char>((high << 4) | low);
        }
        bytes = std::move(raw);
    }

    if (write_whole_file(path, bytes)) {
        show_toast(TRF("%zu byte(s) saved", bytes.size()));
    } else {
        show_toast(TR("could not write the file"));
    }
}

void MainShell::apply_reset_settings() {
    // As conexoes, os favoritos e os filtros ficam: "Reset Settings" do
    // DBeaver repoe as preferencias de interface, nao apaga dados.
    AppSettings fresh;
    fresh.folders   = settings_.folders;
    fresh.filters   = settings_.filters;
    fresh.bookmarks = settings_.bookmarks;
    settings_ = std::move(fresh);

    keymap_       = keymap_from_id(settings_.keymap);
    side_by_side_ = settings_.side_by_side;
    set_theme(settings_.theme);
    reapply_palettes();
    i18n::set_language(i18n::detect_system_language());
    set_icon_set(icon_set_from_id(settings_.icons));
    save_settings();
    rebuild_layout();
}

// O texto de "Collect diagnostic info". Sem senha nenhuma: so' o que ajuda a
// entender um problema, e nada que nao possa ir num relato de defeito.
std::string MainShell::diagnostics_text() {
    std::string out;
    const auto line = [&out](const std::string& text) { out += text + "\n"; };

    line("C-Otter diagnostic info");
    line("=======================");
    line(std::string("Dear ImGui: ") + IMGUI_VERSION);
#if defined(_WIN32)
    line("Platform: Windows");
#elif defined(__APPLE__)
    line("Platform: macOS");
#else
    line("Platform: Linux");
#endif
    line("Data directory: " + data_directory());
    line("Theme: " + settings_.theme + "   Keymap: " + settings_.keymap +
         "   Icons: " + settings_.icons + "   Language: " +
         (settings_.language.empty() ? std::string("(environment)") : settings_.language));
    line("");

    line("Drivers");
    for (const db::Driver* driver : db::all_drivers()) {
        line("  " + std::string(driver->id()) + "  (" +
             std::string(driver->display_name()) + ", default port " +
             std::to_string(driver->default_port()) + ")");
    }
    line("");

    line("Saved connections: " + std::to_string(saved_profiles_.size()));
    for (const db::StoredProfile& stored : saved_profiles_) {
        const db::ConnectionProfile& profile = stored.profile;
        line("  " + profile.effective_name() + "  [" + profile.driver_id + "] " +
             profile.host + ":" + std::to_string(profile.port) + "/" + profile.database +
             "  user=" + profile.user +
             (profile.ssl.enabled ? "  ssl=" + std::string(db::to_string(profile.ssl.mode))
                                  : std::string{}) +
             (profile.ssh.enabled ? "  ssh=" + profile.ssh.host : std::string{}) +
             (profile.proxy.enabled ? "  proxy=" + profile.proxy.host : std::string{}) +
             (stored.supported ? "" : "  (unsupported)"));
    }
    line("");

    line("Open sessions: " + std::to_string(connections_.size()));
    for (const Connection& connection : connections_) {
        const Session& target = *connection.session;
        const SessionState state = target.state();
        line("  " + connection.profile.effective_name() + "  " +
             (state == SessionState::connected    ? "connected"
              : state == SessionState::connecting ? "connecting"
              : state == SessionState::failed     ? "failed"
                                                  : "disconnected") +
             (state == SessionState::connected
                  ? "  server " + target.server_version() +
                        (target.secure_channel().empty()
                             ? std::string("  (not encrypted)")
                             : "  " + target.secure_channel()) +
                        (target.auto_commit() ? "  auto-commit" : "  manual commit")
                  : "  " + target.status_message()));
    }
    line("");

    line("Last statements of the active session");
    const std::vector<db::QueryLog> log = session().query_log();
    const std::size_t first = log.size() > 40 ? log.size() - 40 : 0;
    for (std::size_t i = first; i < log.size(); ++i) {
        std::string sql = log[i].sql.substr(0, 160);
        std::replace(sql.begin(), sql.end(), '\n', ' ');
        char prefix[64];
        std::snprintf(prefix, sizeof prefix, "  %8.2f ms  %s",
                      static_cast<double>(log[i].duration.count()) / 1000.0,
                      log[i].failed ? "FAILED  " : "");
        line(prefix + sql + (log[i].failed ? "  -- " + log[i].error : std::string{}));
    }
    return out;
}

// "Execute SQL script natively": o script inteiro pelo psql, como o DBeaver
// faz. O que o psql entende e o protocolo nao -- \copy, \i, \set -- so' roda
// assim.
void MainShell::run_script_native() {
    SqlDocument* document = active_document();
    if (document == nullptr || document->is_object()) return;
    const Connection* owner = connection_by_id(document->connection_id());
    if (owner == nullptr) return;

    if (owner->session->is_mssql()) {
        // O cliente nativo seria o sqlcmd, que ainda nao e' chamado daqui.
        show_toast(TR("native script execution is not available for SQL Server yet"));
        return;
    }
    if (owner->session->is_sqlanywhere()) {
        // O cliente nativo seria o dbisql, que ainda nao e' chamado daqui.
        show_toast(TR("native script execution is not available for SQL Anywhere yet"));
        return;
    }
    const bool mysql = owner->profile.driver_id == "mysql";
    const std::string program =
        mysql ? db::find_mysql_tool("mysql") : db::find_postgres_tool("psql");
    if (program.empty()) {
        show_toast(mysql ? TR("mysql was not found: install the MySQL client tools")
                         : TR("psql was not found: install the PostgreSQL client tools"));
        return;
    }

    // O texto vai para um arquivo na pasta de dados (portatil): o psql le'
    // um script de arquivo, e o documento pode nem ter sido salvo.
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = fs::path(data_directory()) / "scripts";
    fs::create_directories(folder, ec);
    const fs::path script = folder / ("native-" + std::to_string(document->id()) + ".sql");
    if (ec || !write_whole_file(script.string(), document->editor().GetText())) {
        show_toast(TR("could not write the temporary file"));
        return;
    }

    db::ConnConfig config = owner->profile.to_conn_config();
    if (const std::uint16_t port = owner->session->tunnel_port(); port != 0) {
        config.host = "127.0.0.1";
        config.port = port;
    }

    tool_form_               = {};
    tool_form_.connection_id = owner->id;
    if (mysql) {
        // O banco do script e' o que a sessao tem selecionado agora.
        if (const std::string current = owner->session->current_schema(); !current.empty()) {
            config.database = current;
        }
        start_tool(TRF("mysql: %s", document->title().c_str()),
                   db::mysql_script_command(config, script.string(), program),
                   /*reload_after=*/true);
    } else {
        db::RestoreOptions options;
        options.format = db::DumpFormat::plain;   // psql -f <arquivo>
        options.file   = script.string();
        start_tool(TRF("psql: %s", document->title().c_str()),
                   db::restore_command(config, options, program), /*reload_after=*/true);
    }
    if (!tool_form_.error.empty()) show_toast(tool_form_.error);
}

// --- Canal de comandos -------------------------------------------------------------------

// Linhas do OTTER_COMMAND_FILE:
//
//     nav select <tipo>|<schema>|<nome>|<pai>|<assinatura>   seleciona o no'
//     nav clear                                              tira a selecao
//     nav database <banco>                                   ativa a sessao do banco
//     nav folder open|close <grupo>                          abre ou fecha um grupo,
//                                 uma pasta da arvore (pelo rotulo) ou um schema
//     nav foldermenu <rotulo>                                menu de contexto de uma pasta
//     nav menu | nav connmenu <conexao>      menu da area vazia / de uma conexao
//     conn rename <conexao>                  "Rename Connection" (nome: "app name", "app ok")
//     folder new <pai>[|<conexao>] | folder rename <caminho> | folder delete <caminho>
//     folder move <conexao>|<pasta>          pastas de conexao (nome: "app folder", "app ok")
//     dialog edit <conexao> | dialog close    o dialogo de conexao de um perfil salvo
//     dialog new [filtro] | dialog url <jdbc:...> | dialog tab SSH|SSL|Proxy
//                                             o catalogo de drivers; a pagina do
//                                             driver de uma URL (sem gravar); a aba de rede
//     auth user|password|save <valor> | auth ok | auth cancel
//                                             o dialogo de autenticacao
//     dialog import | dialog import existing | dialog import run
//                                             a janela "Import connections"
//     tab database [titulo]      o icone de banco da aba ("Select active database")
//     app search <texto>         digita no campo de busca da janela aberta
//     app pick [n]                escolhe o n-esimo resultado (padrao 0)
//     app url <texto> | app folder <nome> | app filter <incluir>|<excluir>
//     app ok | app cancel         o botao principal / fechar a janela aberta
//     app dump                    grava o estado em <arquivo de comandos>.app
//     app chart <id>              seleciona um grafico do dashboard
bool MainShell::app_command_line(const std::string& line) {
    if (line == "nav clear") {
        nav_selected_ = {};
        return true;
    }
    if (line.starts_with("nav select ")) {
        std::vector<std::string> parts;
        std::string_view rest = std::string_view(line).substr(11);
        while (true) {
            const std::size_t bar = rest.find('|');
            parts.emplace_back(rest.substr(0, bar));
            if (bar == std::string_view::npos) break;
            rest.remove_prefix(bar + 1);
        }
        parts.resize(5);

        db::ObjectRef ref;
        bool known = false;
        for (int t = 0; t < db::kObjectTypeCount; ++t) {
            if (db::to_string(static_cast<ObjectType>(t)) == parts[0]) {
                ref.type = static_cast<ObjectType>(t);
                known    = true;
            }
        }
        if (!known) {
            show_toast("OTTER_COMMAND_FILE: unknown object type: " + parts[0]);
            return true;
        }
        ref.schema    = parts[1];
        ref.name      = parts[2];
        ref.parent    = parts[3];
        ref.signature = parts[4];
        if (active_connection_ < connections_.size()) {
            nav_selected_ = NavTarget{true, connections_[active_connection_].id,
                                      std::move(ref)};
        }
        return true;
    }

    // nav foldermenu <rotulo>: o menu de contexto da primeira pasta da arvore
    // com esse rotulo ("Databases", "Schemas", "Tables"...) -- o que na tela
    // e' o botao direito sobre ela.
    if (line.starts_with("nav foldermenu ")) {
        folder_menu_request_ = TR(line.substr(15).c_str());
        return true;
    }

    // nav menu: o menu da area vazia da arvore. nav connmenu <conexao>: o do
    // no' de uma conexao. Os mesmos popups do botao direito.
    if (line == "nav menu") {
        nav_menu_request_ = true;
        return true;
    }
    if (line.starts_with("nav connmenu ")) {
        conn_menu_request_ = line.substr(13);
        return true;
    }
    // conn rename <conexao>: abre "Rename Connection" (o nome novo vai por
    // "app name <texto>" no MESMO lote, o botao por "app ok").
    if (line.starts_with("conn rename ")) {
        const std::string name = line.substr(12);
        for (const db::StoredProfile& stored : saved_profiles_) {
            if (stored.profile.effective_name() != name) continue;
            open_rename_connection(stored.profile);
            break;
        }
        return true;
    }
    // folder new <pai>[|<conexao>] | folder rename <caminho> | folder delete
    // <caminho>: abrem a janela (o nome vai por "app folder", o botao por
    // "app ok"). folder move <conexao>|<pasta>: "Move to folder".
    if (line.starts_with("folder ")) {
        const std::string rest = line.substr(7);
        const auto profile_named = [this](const std::string& name)
            -> std::optional<db::ConnectionProfile> {
            for (const db::StoredProfile& stored : saved_profiles_) {
                if (stored.profile.effective_name() == name) return stored.profile;
            }
            return std::nullopt;
        };
        if (rest == "new" || rest.starts_with("new ")) {
            const std::string spec = rest.size() > 4 ? rest.substr(4) : std::string{};
            const std::size_t bar  = spec.find('|');
            open_new_folder(spec.substr(0, bar),
                            bar == std::string::npos ? std::nullopt
                                                     : profile_named(spec.substr(bar + 1)));
        } else if (rest.starts_with("rename ")) {
            open_rename_folder(rest.substr(7));
        } else if (rest.starts_with("delete ")) {
            app_.delete_folder = true;
            app_.folder_delete = rest.substr(7);
        } else if (rest.starts_with("move ")) {
            const std::string spec = rest.substr(5);
            const std::size_t bar  = spec.find('|');
            if (bar != std::string::npos) {
                if (const auto profile = profile_named(spec.substr(0, bar))) {
                    tree_requests_.move = std::make_pair(*profile, spec.substr(bar + 1));
                }
            }
        }
        return true;
    }

    // nav folder open|close <grupo>: o que na tela e' clicar na seta do grupo.
    if (line.starts_with("nav folder open ") || line.starts_with("nav folder close ")) {
        tree_folder_request_open_ = line.starts_with("nav folder open ");
        tree_folder_request_      = line.substr(tree_folder_request_open_ ? 16 : 17);
        return true;
    }

    // nav database <banco>: a sessao daquele banco, sob a raiz da conexao
    // ativa, passa a ser a ativa -- o que na tela e' expandir o banco na
    // arvore e clicar nele (PostgreSQL e SQL Server: uma sessao por banco).
    if (line.starts_with("nav database ")) {
        const std::string database = line.substr(13);
        if (active_connection_ >= connections_.size()) return true;

        std::size_t root = active_connection_;
        if (const std::size_t parent = connections_[root].parent_id; parent != 0) {
            for (std::size_t i = 0; i < connections_.size(); ++i) {
                if (connections_[i].id == parent) root = i;
            }
        }
        std::size_t owner = root;
        if (connections_[root].session->database_name() != database) {
            owner = find_database_connection(connections_[root].id, database);
            if (owner == kNone) {
                open_database_connection(root, database);
                owner = connections_.size() - 1;
            }
        }
        // Um script na janela DESSA sessao, como "New SQL script" no menu do
        // banco: a conexao ativa segue a aba ativa, e sem uma aba propria a
        // sessao do banco perderia a vez no quadro seguinte.
        active_connection_ = owner;
        tree_claim_        = connections_[owner].id;
        open_sql_tab(std::string{}, /*run=*/false);
        return true;
    }

    // dialog edit <nome>: "Edit connection" do menu do no', para conferir a
    // pagina do driver daquele perfil numa captura.
    if (line.starts_with("dialog edit ")) {
        const std::string name = line.substr(12);
        for (const db::StoredProfile& stored : saved_profiles_) {
            if (stored.profile.effective_name() != name) continue;
            connection_dialog_.open_edit(stored.profile);
            break;
        }
        return true;
    }
    if (line == "dialog close") {
        connection_dialog_.close();
        return true;
    }
    // dialog new [filtro]: "New connection", no catalogo de drivers.
    if (line == "dialog new" || line.starts_with("dialog new ")) {
        connection_dialog_.open_new();
        connection_dialog_.set_driver_filter(line.size() > 11 ? line.substr(11)
                                                              : std::string{});
        return true;
    }
    // dialog url <jdbc:...>: a pagina do driver com o perfil daquela URL. Nada
    // e' gravado enquanto ninguem confirmar o dialogo.
    if (line.starts_with("dialog url ")) {
        if (auto profile = db::profile_from_url(line.substr(11))) {
            connection_dialog_.open_configure(*profile);
        } else {
            show_toast("OTTER_COMMAND_FILE: " + profile.error().to_string());
        }
        return true;
    }
    if (line.starts_with("dialog tab ")) {
        connection_dialog_.request_network_tab(line.substr(11));
        return true;
    }
    // connect url <jdbc:...>: abre uma conexao a partir da URL SEM grava-la
    // nos perfis -- para os roteiros de conferencia nao deixarem conexao no
    // arquivo de quem usa a maquina.
    if (line.starts_with("connect url ")) {
        if (auto profile = db::profile_from_url(line.substr(12))) {
            open_connection(*profile);
        } else {
            show_toast("OTTER_COMMAND_FILE: " + profile.error().to_string());
        }
        return true;
    }

    // connect save <nome>|<jdbc:...>: o mesmo, GRAVANDO o perfil com esse nome
    // -- o que na tela e' concluir o dialogo "New connection". So' para
    // quando o usuario pediu a conexao salva.
    if (line.starts_with("connect save ")) {
        const std::string rest = line.substr(13);
        const std::size_t bar  = rest.find('|');
        if (bar == std::string::npos) return true;
        if (auto profile = db::profile_from_url(rest.substr(bar + 1))) {
            profile->name = rest.substr(0, bar);
            const db::ConnectionProfile saved = remember_profile(*profile);
            active_profile_ = saved;
            open_connection(saved);
        } else {
            show_toast("OTTER_COMMAND_FILE: " + profile.error().to_string());
        }
        return true;
    }

    // auth user <nome> | auth password <senha> | auth save 0|1 | auth ok |
    // auth cancel: o dialogo "'<conexao>' Authentication". A senha passa pelo
    // arquivo de comandos: so' para contas de RASCUNHO, criadas pelo roteiro.
    if (line.starts_with("auth ")) {
        const std::string rest = line.substr(5);
        ImGui::ClearActiveID();   // o campo com o teclado ignora o que vem de fora
        if (rest.starts_with("user ")) {
            std::snprintf(auth_prompt_.user, sizeof auth_prompt_.user, "%s",
                          rest.c_str() + 5);
        } else if (rest.starts_with("password ")) {
            std::snprintf(auth_prompt_.password, sizeof auth_prompt_.password, "%s",
                          rest.c_str() + 9);
        } else if (rest.starts_with("save ")) {
            auth_prompt_.save = rest.substr(5) == "1";
        } else if (rest == "ok") {
            auth_prompt_submit_ = auth_prompt_.open;
        } else if (rest == "cancel") {
            close_auth_prompt();
        }
        return true;
    }

    // tab close <titulo> | tab confirm | tab cancel: o "x" da aba de uma
    // conexao e os dois botoes da confirmacao. O titulo e' o que a aba mostra
    // ("PostgreSQL 18 (ERP_TID)"); "tab close" sozinho fecha a da conexao
    // ativa. Mesmo caminho do clique: o pedido cai em apply_close_tab_request.
    if (line == "tab close" || line.starts_with("tab close ")) {
        const std::string wanted = line.size() > 10 ? line.substr(10) : std::string();
        for (std::size_t i = 0; i < connections_.size(); ++i) {
            const Connection& connection = connections_[i];
            if (wanted.empty() ? i == active_connection_
                               : connection_title(connection) == wanted) {
                close_tab_request_ = connection.id;
                break;
            }
        }
        if (close_tab_request_ == 0) {
            show_toast("OTTER_COMMAND_FILE: no such tab: " + wanted);
        }
        return true;
    }
    if (line == "tab confirm") {
        if (confirm_close_tab_ != 0) {
            close_connection_documents(std::exchange(confirm_close_tab_, 0));
        }
        return true;
    }
    if (line == "tab cancel") {
        confirm_close_tab_ = 0;
        return true;
    }
    // tab database [titulo]: o icone de banco na aba da conexao (a ativa, sem
    // titulo). Depois, "app search <banco>" e "app pick 0" escolhem.
    if (line == "tab database" || line.starts_with("tab database ")) {
        const std::string wanted = line.size() > 13 ? line.substr(13) : std::string();
        for (std::size_t i = 0; i < connections_.size(); ++i) {
            if (wanted.empty() ? i == active_connection_
                               : connection_title(connections_[i]) == wanted) {
                open_database_picker(connections_[i].id);
                return true;
            }
        }
        show_toast("OTTER_COMMAND_FILE: no such tab: " + wanted);
        return true;
    }
    // tab connect: o botao "Connect" da aba de um script cuja conexao esta'
    // fechada (um script reaberto ao iniciar).
    if (line == "tab connect") {
        if (const SqlDocument* document = active_document();
            document != nullptr && can_connect_from_tab(document->connection_id())) {
            tree_requests_.reconnect = document->connection_id();
        }
        return true;
    }

    // dialog import: "File > Import connections"; dialog import run: o botao
    // "Import selected" com o que a janela trouxe marcado.
    if (line == "dialog import") {
        show_import_    = true;
        import_scanned_ = false;
        import_status_.clear();
        return true;
    }
    if (line == "dialog import run") {
        import_connections_submit_ = true;
        return true;
    }
    // dialog import existing: deixa marcadas so' as linhas que completam o
    // que ja' esta' aqui (senha, grupo) -- nao acrescenta conexao nenhuma.
    if (line == "dialog import existing") {
        import_passwords_only_ = true;
        return true;
    }

    if (!line.starts_with("app ")) return false;
    const std::string rest = line.substr(4);

    if (rest.starts_with("search ")) {
        std::snprintf(app_.search, sizeof app_.search, "%s", rest.c_str() + 7);
        app_.search_cursor = 0;
        return true;
    }
    if (rest == "pick" || rest.starts_with("pick ")) {
        app_.search_cursor = rest.size() > 5 ? std::atoi(rest.c_str() + 5) : 0;
        app_.submit = true;
        return true;
    }
    if (rest.starts_with("url ")) {
        std::snprintf(app_.url, sizeof app_.url, "%s", rest.c_str() + 4);
        return true;
    }
    if (rest.starts_with("folder ")) {
        std::snprintf(app_.folder, sizeof app_.folder, "%s", rest.c_str() + 7);
        return true;
    }
    if (rest.starts_with("name ")) {
        std::snprintf(app_.connection_name, sizeof app_.connection_name, "%s",
                      rest.c_str() + 5);
        return true;
    }
    if (rest.starts_with("filter ")) {
        const std::string spec = rest.substr(7);
        const std::size_t bar = spec.find('|');
        std::snprintf(app_.filter_include, sizeof app_.filter_include, "%s",
                      spec.substr(0, bar).c_str());
        std::snprintf(app_.filter_exclude, sizeof app_.filter_exclude, "%s",
                      bar == std::string::npos ? "" : spec.substr(bar + 1).c_str());
        return true;
    }
    if (rest.starts_with("chart ")) {
        dashboard_.selected = rest.substr(6);
        return true;
    }
    if (rest == "ok") {
        app_.submit = true;
        return true;
    }
    if (rest == "cancel") {
        app_.cancel = true;
        return true;
    }
    if (rest == "dump") {
        const char* path = std::getenv("OTTER_COMMAND_FILE");
        if (path == nullptr) return true;

        std::string out;
        const NavTarget* target = nav_target();
        out += "nav: " + (target != nullptr
                              ? std::string(db::to_string(target->ref.type)) + " " +
                                    target->ref.schema + "." + target->ref.name
                              : std::string("(none)")) + "\n";
        out += "schema: " + session().current_schema() + "\n";
        out += std::string("auto-commit: ") + (session().auto_commit() ? "on" : "off") + "\n";
        out += "status: " + session().status_message() + "\n";
        out += "folders:";
        for (const std::string& folder : settings_.folders) out += " " + folder;
        out += "\nbookmarks:";
        for (const AppSettings::Bookmark& bookmark : settings_.bookmarks) {
            out += " " + bookmark.title;
        }
        out += "\nsaved:";
        for (const db::StoredProfile& stored : saved_profiles_) {
            out += " [" + stored.profile.effective_name() + "]";
        }
        out += "\ndashboard:";
        for (const DashboardChartState& chart : dashboard_.charts) {
            out += " " + chart.id + "(";
            for (const DashboardSeries& series : chart.series) {
                out += series.name + "=" +
                       (series.points.empty() ? std::string("-")
                                              : std::to_string(series.points.back())) + ";";
            }
            out += chart.error.empty() ? ")" : " ERROR " + chart.error + ")";
        }
        // As abas de conexao que estao na tela, com quantos documentos cada
        // uma tem: e' o que se confere depois de "tab close".
        out += "\ntabs:";
        for (const Connection& connection : connections_) {
            const auto count = std::count_if(
                documents_.begin(), documents_.end(),
                [&connection](const std::unique_ptr<SqlDocument>& document) {
                    return document->connection_id() == connection.id;
                });
            if (count > 0) {
                out += " [" + connection_title(connection) + ": " +
                       std::to_string(count) + "]";
            }
        }
        out += "\nclose-confirm: ";
        out += confirm_close_tab_ != 0 ? "open" : "closed";
        out += "\nclipboard: ";
        if (const char* clipboard = ImGui::GetClipboardText()) out += clipboard;
        out += "\n";
        // As conexoes e o estado de cada sessao.
        for (const Connection& connection : connections_) {
            const SessionState state = connection.session->state();
            out += "connection: " + connection_title(connection) + " | " +
                   (state == SessionState::connected    ? "connected"
                    : state == SessionState::connecting ? "connecting"
                    : state == SessionState::failed
                          ? "failed: " + connection.session->status_message()
                          : std::string("disconnected")) +
                   (connection.parent_id != 0 ? " | database session" : "") + "\n";
        }
        out += std::string("auth-prompt: ") + (auth_prompt_.open ? "open" : "closed") + "\n";
        // Os scripts: a pasta, cada aba com o arquivo dela, e os fechados.
        out += "scripts-dir: " + scripts_dir_ + "\n";
        for (const std::unique_ptr<SqlDocument>& document : documents_) {
            if (document->is_object()) continue;
            const Connection* owner = connection_by_id(document->connection_id());
            out += "script: " + document->title() + " | " +
                   (owner != nullptr ? connection_title(*owner) : std::string{}) + " | " +
                   (document->autosave().on_disk ? document->file_path()
                                                 : std::string("(not on disk)")) +
                   (document->modified() ? " | modified" : "") + "\n";
        }
        for (const ScriptEntry& closed : closed_scripts_) {
            out += "closed-script: " + closed.file + " | " + closed.connection +
                   (closed.database.empty() ? std::string{} : " (" + closed.database + ")") +
                   "\n";
        }
        out += std::string("quit-confirm: ") + (confirm_quit_ ? "open" : "closed") + "\n";
        if (SqlDocument* document = active_document()) {
            out += "document: " + document->title() + "\n";
            if (!document->is_object()) out += "text: " + document->editor().GetText() + "\n";
        }
        write_whole_file(std::string(path) + ".app", out);
        return true;
    }

    show_toast("OTTER_COMMAND_FILE: unknown app command: " + rest);
    return true;
}

} // namespace otter::ui
