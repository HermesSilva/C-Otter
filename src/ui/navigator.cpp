// C-Otter -- ui/navigator.cpp
//
// A arvore unica de conexoes e objetos (ADR 0018): a conexao e' a raiz, e o
// que ela contem fica dentro dela, na forma do `<tree>` do plugin.xml do
// driver no DBeaver.
//
//   PostgreSQL            MySQL
//   conexao               conexao
//   ├ Databases           ├ Databases
//   │ └ banco             │ └ banco
//   │   ├ Schemas         │   ├ Tables, Views, ...
//   │   │ └ schema        ├ Users
//   │   ├ Event Triggers  └ System Info
//   │   ├ Extensions
//   │   ├ Storage
//   │   ├ System Info
//   │   └ Roles
//   ├ Administer
//   └ System Info
//
//   SQL Server
//   conexao
//   ├ Databases
//   │ └ banco
//   │   ├ Schemas
//   │   │ └ schema  (Tables, Views, Indexes, Procedures, Sequences,
//   │   │            Synonyms, Data Types)
//   │   └ Database triggers
//   ├ Security
//   │ └ Logins
//   └ Administer
//
// As pastas de objeto (tabelas, colunas, indices...) continuam em
// main_shell.cpp; aqui fica o que e' da CONEXAO e do SERVIDOR.
#include "ui/main_shell.hpp"

#include "db/app_tools.hpp"
#include "db/ddl.hpp"
#include "db/mssql_object.hpp"
#include "db/sqlanywhere_object.hpp"
#include "db/mysql_object.hpp"

#include "base/i18n.hpp"
#include "db/registry.hpp"
#include "ui/icon_images.hpp"
#include "ui/hint.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// Driver + host + porta + banco + usuario: o que faz duas entradas serem a
// MESMA conexao. E' tambem o id do no' no ImGui -- estavel entre "perfil
// salvo" e "conexao aberta", para que o no' expandido continue expandido
// quando a conexao nasce.
std::string profile_key(const db::ConnectionProfile& profile) {
    return profile.driver_id + "|" + profile.host + "|" +
           std::to_string(profile.port) + "|" + profile.database + "|" +
           profile.user;
}

bool same_target(const db::ConnectionProfile& a, const db::ConnectionProfile& b) {
    return a.driver_id == b.driver_id && a.host == b.host && a.port == b.port &&
           a.database == b.database && a.user == b.user;
}

// As consultas do Session Manager e do Lock Manager do DBeaver
// (PostgreSessionManager / PostgreLockManager). La' sao editores proprios;
// aqui abrem numa aba de resultado -- ver ADR 0018, divergencias.
constexpr const char* kSessionsSql =
    "SELECT sa.pid, sa.datname, sa.usename, sa.application_name,\n"
    "       sa.client_addr, sa.backend_start, sa.state, sa.wait_event_type,\n"
    "       sa.wait_event, sa.query_start, sa.query\n"
    "  FROM pg_catalog.pg_stat_activity sa\n"
    " ORDER BY sa.backend_start";

constexpr const char* kLocksSql =
    "SELECT blocked.pid      AS blocked_pid,\n"
    "       blocked.usename  AS blocked_user,\n"
    "       blocking.pid     AS blocking_pid,\n"
    "       blocking.usename AS blocking_user,\n"
    "       blocked.query    AS blocked_statement,\n"
    "       blocking.query   AS blocking_statement\n"
    "  FROM pg_catalog.pg_stat_activity blocked\n"
    "  JOIN LATERAL unnest(pg_catalog.pg_blocking_pids(blocked.pid)) AS b(pid)\n"
    "    ON true\n"
    "  JOIN pg_catalog.pg_stat_activity blocking ON blocking.pid = b.pid";

} // namespace

// --- Painel ---------------------------------------------------------------------

bool MainShell::revealing_here() const {
    return tree_reveal_.frames > 0 && active_connection_ < connections_.size() &&
           connections_[active_connection_].id == tree_reveal_.connection_id;
}

void MainShell::draw_navigator_panel() {
    tree_claim_ = 0;

    if (ImGui::Begin(TRW("Database Navigator", "###NavigatorPanel"))) {
        const Palette& p = colors();

        // Atalhos de contexto `navigator` (Ctrl+Enter abre o ultimo script
        // da conexao, Ctrl+Alt+Enter um console).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            dispatch_shortcuts(CommandContext::navigator);
        }

        // Filtro por nome de OBJETO. O campo do DBeaver filtra conexoes; o
        // daqui desce ate' tabelas, roles e variaveis, que e' onde a lista
        // cresce -- num banco com 300 tabelas, rolar nao resolve.
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (app_.focus_nav_filter) {
            ImGui::SetKeyboardFocusHere();
            app_.focus_nav_filter = false;
        }
        ImGui::InputTextWithHint("##navfilter", TR("Filter objects..."),
                                 navigator_filter_, sizeof navigator_filter_);
        ImGui::Separator();

        // As pastas de conexao, na ordem em que aparecem nos perfis. Ordem
        // estavel, nao alfabetica: reordenar a cada conexao nova moveria as
        // de baixo.
        const std::vector<std::string> folders = connection_folders();

        std::size_t total = 0;

        // Os itens raiz encostam mais na borda: metade da distancia entre
        // ela e a seta (margem da janela + a margem que o TreeNode poe antes
        // do triangulo). Pedido do usuario, com captura -- sobrava uma faixa
        // vazia a' esquerda da arvore inteira.
        const float root_pull = std::floor(
            (ImGui::GetCursorPosX() + ImGui::GetStyle().FramePadding.x) * 0.5f);
        ImGui::Unindent(root_pull);

        // So' as do primeiro nivel: cada uma desenha as que tem dentro.
        for (const std::string& folder : folders) {
            if (db::folder_parent(folder).empty()) {
                draw_connection_folder(folder, folders, total);
            }
        }

        // As conexoes SEM pasta ficam na raiz, depois -- como no DBeaver.
        const std::vector<TreeEntry> root_entries = tree_entries(std::string{});
        total += root_entries.size();
        for (const TreeEntry& entry : root_entries) {
            draw_connection_node(entry.connection, entry.saved);
        }

        ImGui::Indent(root_pull);

        if (total == 0) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("no connection"));
        }

        // Botao direito na area vazia: "Create > Connection" e "Create > New
        // Folder", como no DBeaver (NavigatorHandlerObjectCreateNew, para o
        // no' raiz das conexoes).
        //
        // So' havia "New connection...": pasta so' se criava pelo menu
        // Database, e quem procurava aqui -- onde o DBeaver a poe -- concluia
        // que nao existia (relato do usuario, 2026-10-01; diretiva 12).
        if (nav_menu_request_) {
            // Pedido pelo canal de comandos: o menu nasce aqui, e nao onde o
            // mouse de quem usa a maquina estiver (fora da janela, a captura
            // nao o mostraria).
            ImGui::OpenPopup("##navmenu");
            ImGui::SetNextWindowPos(ImGui::GetCursorScreenPos());
            nav_menu_request_ = false;
        }
        if (ImGui::BeginPopupContextWindow(
                "##navmenu", ImGuiPopupFlags_MouseButtonRight |
                             ImGuiPopupFlags_NoOpenOverItems)) {
            draw_folder_create_menu(std::string{}, nullptr);
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    // O clique dentro da subarvore de uma conexao a torna a ativa -- e' o que
    // o DBeaver faz ao selecionar um no'. Aplicado so' aqui, depois de tudo
    // desenhado: trocar a ativa no meio faria o resto da arvore falar com a
    // sessao errada.
    if (tree_claim_ != 0) {
        for (std::size_t i = 0; i < connections_.size(); ++i) {
            if (connections_[i].id != tree_claim_) continue;
            active_connection_ = i;
            active_profile_    = connections_[i].profile;
            break;
        }

        // A selecao da arvore, para "Set connection from navigator". Com o
        // auto-sync ligado, o script ativo passa a ser desta conexao na hora.
        tree_selected_connection_id_ = tree_claim_;
        if (sync_auto_) {
            if (SqlDocument* document = active_document()) {
                document->set_connection_id(tree_claim_);
            }
        }
        tree_claim_ = 0;
    }

    // O realce de "Open Declaration" dura alguns quadros e some.
    if (tree_reveal_.frames > 0 && --tree_reveal_.frames == 0) tree_reveal_ = {};

    apply_tree_requests();
}

// --- Pastas de conexao -----------------------------------------------------------

std::vector<std::string> MainShell::connection_folders() const {
    std::vector<std::string> folders;
    // Os ancestrais entram antes: "Clientes/Producao" faz "Clientes" existir.
    const auto note = [&folders](const std::string& path) {
        if (path.empty()) return;
        for (std::size_t slash = path.find('/');; slash = path.find('/', slash + 1)) {
            const std::string part = path.substr(0, slash);
            if (!part.empty() &&
                std::find(folders.begin(), folders.end(), part) == folders.end()) {
                folders.push_back(part);
            }
            if (slash == std::string::npos) break;
        }
    };
    for (const db::StoredProfile& stored : saved_profiles_) note(stored.profile.folder);
    for (const Connection& connection : connections_) {
        if (connection.parent_id == 0) note(connection.profile.folder);
    }
    for (const std::string& folder : settings_.folders) note(folder);
    return folders;
}

void MainShell::draw_connection_folder(const std::string& path,
                                       const std::vector<std::string>& all,
                                       std::size_t& total) {
    const Palette& p = colors();
    const std::vector<TreeEntry> entries = tree_entries(path);
    total += entries.size();

    ImGui::PushID(path.c_str());

    // A pasta abre como foi deixada na ultima vez (settings.json). O ImGui so'
    // lembra o estado dos nos dentro da execucao; sem isto as pastas nasciam
    // todas abertas a cada inicio, e quem tem vinte conexoes importadas numa
    // a fechava de novo todo dia.
    const auto closed_at = std::find(settings_.closed_folders.begin(),
                                     settings_.closed_folders.end(), path);
    const bool was_closed = closed_at != settings_.closed_folders.end();
    ImGui::SetNextItemOpen(!was_closed, ImGuiCond_Once);
    if (tree_folder_request_ == path) {
        ImGui::SetNextItemOpen(tree_folder_request_open_, ImGuiCond_Always);
        tree_folder_request_.clear();
    }

    const bool open = ImGui::TreeNodeEx("##folder", ImGuiTreeNodeFlags_SpanAvailWidth);

    // O menu da pasta. O estado do no' e' lido AQUI: o icone e o rotulo,
    // desenhados depois, passam a ser o "ultimo item". "nav foldermenu
    // <caminho>" do canal de comandos abre o mesmo popup.
    const bool requested = !folder_menu_request_.empty() && folder_menu_request_ == path;
    if (requested) folder_menu_request_.clear();
    if (requested ||
        (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right))) {
        ImGui::OpenPopup("##foldermenu");
    }
    const ImVec2 menu_anchor(ImGui::GetItemRectMin().x + 60.0f, ImGui::GetItemRectMax().y);

    // Mudou (clique na seta, duplo clique, teclado): grava na hora -- o
    // programa pode ser fechado de qualquer jeito.
    if (open == was_closed) {
        if (open) settings_.closed_folders.erase(closed_at);
        else      settings_.closed_folders.push_back(path);
        save_settings();
    }
    same_line_after_arrow();
    icon_inline(Icon::folder, p.accent_light);
    ImGui::SameLine(0.0f, tree_label_gap());
    // So' o ultimo nome: o resto do caminho sao as pastas de cima.
    ImGui::TextUnformatted(db::folder_leaf(path).c_str());

    // Create > Connection / New Folder, Rename, Delete: o menu do DBNLocalFolder.
    if (requested) ImGui::SetNextWindowPos(menu_anchor);   // canal: ao lado do no'
    if (ImGui::BeginPopup("##foldermenu")) {
        draw_folder_create_menu(path, nullptr);
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Rename"))) open_rename_folder(path);
        if (ImGui::MenuItem(TR("Delete"))) {
            app_.delete_folder = true;
            app_.folder_delete = path;
        }
        ImGui::EndPopup();
    }

    if (open) {
        // Subpastas primeiro, depois as conexoes -- "Folders first", o padrao
        // do DBeaver.
        for (const std::string& child : all) {
            if (child != path && db::folder_parent(child) == path) {
                draw_connection_folder(child, all, total);
            }
        }
        for (const TreeEntry& entry : entries) {
            draw_connection_node(entry.connection, entry.saved);
        }
        ImGui::TreePop();
    } else {
        // Fechada, o que ha' dentro continua contando: sem isto a arvore dizia
        // "no connection" com todas as conexoes dentro de pastas fechadas.
        for (const std::string& child : all) {
            if (child != path && db::folder_contains(path, child)) {
                total += tree_entries(child).size();
            }
        }
    }
    ImGui::PopID();
}

void MainShell::draw_folder_create_menu(const std::string& parent,
                                        const db::ConnectionProfile* move) {
    if (!ImGui::BeginMenu(TR("Create"))) return;

    // Numa conexao o DBeaver so' oferece a pasta; "Connection" e' da raiz e
    // das pastas.
    if (move == nullptr && ImGui::MenuItem(TR("Connection"))) {
        connection_dialog_.open_new();
        // A conexao criada a partir de uma pasta nasce dentro dela.
        connection_dialog_.profile().folder = parent;
    }
    if (ImGui::MenuItem(TR("New Folder"))) {
        open_new_folder(parent, move != nullptr ? std::optional(*move) : std::nullopt);
    }
    ImGui::EndMenu();
}

void MainShell::open_new_folder(std::string parent,
                                std::optional<db::ConnectionProfile> move) {
    app_.new_folder    = true;
    app_.folder[0]     = '\0';
    app_.folder_parent = std::move(parent);
    app_.folder_rename.clear();
    app_.folder_move   = std::move(move);
    app_.focus_search  = true;
}

void MainShell::open_rename_folder(const std::string& path) {
    app_.new_folder    = true;
    std::snprintf(app_.folder, sizeof app_.folder, "%s", db::folder_leaf(path).c_str());
    app_.folder_parent = db::folder_parent(path);
    app_.folder_rename = path;
    app_.folder_move.reset();
    app_.focus_search  = true;
}

void MainShell::rebase_folder(const std::string& from, const std::string& to) {
    if (from.empty() || from == to) return;

    for (db::StoredProfile& stored : saved_profiles_) {
        stored.profile.folder = db::folder_rebase(stored.profile.folder, from, to);
    }
    for (Connection& connection : connections_) {
        connection.profile.folder = db::folder_rebase(connection.profile.folder, from, to);
    }
    active_profile_.folder = db::folder_rebase(active_profile_.folder, from, to);

    // As pastas vazias e o estado aberto/fechado acompanham. Depois da troca
    // podem sobrar repetidas (a pasta de destino ja' existia) e a raiz ("").
    const auto rebase_list = [&from, &to](std::vector<std::string>& list) {
        std::vector<std::string> out;
        for (const std::string& entry : list) {
            std::string moved = db::folder_rebase(entry, from, to);
            if (!moved.empty() && std::find(out.begin(), out.end(), moved) == out.end()) {
                out.push_back(std::move(moved));
            }
        }
        list = std::move(out);
    };
    rebase_list(settings_.folders);
    rebase_list(settings_.closed_folders);

    // A pasta de destino continua existindo mesmo que fique sem conexao: ao
    // apagar a unica subpasta de uma pasta sem conexoes, a de cima sumiria
    // junto.
    if (!to.empty() &&
        std::find(settings_.folders.begin(), settings_.folders.end(), to) ==
            settings_.folders.end()) {
        settings_.folders.push_back(to);
    }

    persist_profiles();
    save_settings();
}

void MainShell::open_rename_connection(const db::ConnectionProfile& profile) {
    app_.rename_connection = true;
    app_.rename_target     = profile;
    std::snprintf(app_.connection_name, sizeof app_.connection_name, "%s",
                  profile.effective_name().c_str());
    app_.focus_search = true;
}

void MainShell::rename_profile(const db::ConnectionProfile& profile,
                               const std::string& name) {
    // O perfil salvo e a conexao aberta: e' do perfil DELA que a aba do
    // editor e a barra de estado tiram o nome.
    for (db::StoredProfile& stored : saved_profiles_) {
        if (same_target(stored.profile, profile)) stored.profile.name = name;
    }
    for (Connection& connection : connections_) {
        if (same_target(connection.profile, profile)) connection.profile.name = name;
    }
    if (same_target(active_profile_, profile)) active_profile_.name = name;
    persist_profiles();
}

void MainShell::move_profile_to_folder(const db::ConnectionProfile& profile,
                                       const std::string& folder) {
    // A pasta de ORIGEM continua existindo, vazia: tirar a ultima conexao nao
    // a apaga (no DBeaver tambem nao).
    //
    // So' quando ela fica SEM conexao: enquanto houver outra dentro, a pasta
    // existe por ela, e a lista de pastas vazias nao precisa conhece-la.
    const bool still_used =
        std::any_of(saved_profiles_.begin(), saved_profiles_.end(),
                    [&profile](const db::StoredProfile& stored) {
                        return stored.profile.folder == profile.folder &&
                               !same_target(stored.profile, profile);
                    });
    if (!profile.folder.empty() && !still_used &&
        std::find(settings_.folders.begin(), settings_.folders.end(), profile.folder) ==
            settings_.folders.end()) {
        settings_.folders.push_back(profile.folder);
        save_settings();
    }

    for (db::StoredProfile& stored : saved_profiles_) {
        if (same_target(stored.profile, profile)) stored.profile.folder = folder;
    }
    for (Connection& connection : connections_) {
        if (same_target(connection.profile, profile)) connection.profile.folder = folder;
    }
    if (same_target(active_profile_, profile)) active_profile_.folder = folder;
    persist_profiles();
}

// As entradas de uma pasta ("" = raiz), na ordem dos perfis salvos.
//
// Antes as conexoes ABERTAS vinham primeiro e as salvas depois: conectar
// fazia a linha pular para o topo da pasta. Na ordem dos perfis, a linha fica
// onde estava e so' o estado muda.
std::vector<MainShell::TreeEntry> MainShell::tree_entries(
    const std::string& folder) const {
    std::vector<TreeEntry> entries;
    std::vector<bool> used(connections_.size(), false);

    for (std::size_t s = 0; s < saved_profiles_.size(); ++s) {
        const db::ConnectionProfile& profile = saved_profiles_[s].profile;
        if (profile.folder != folder) continue;

        TreeEntry entry;
        entry.saved      = s;
        entry.connection = find_root_connection(profile);
        if (entry.connection != kNone) used[entry.connection] = true;

        // "Show all connections" desmarcado: so' as que estao abertas.
        if (settings_.connected_only &&
            (entry.connection == kNone ||
             connections_[entry.connection].session->state() ==
                 SessionState::disconnected)) {
            continue;
        }
        entries.push_back(entry);
    }

    // Conexao sem perfil salvo correspondente. Nao deveria existir -- toda
    // conexao passa por remember_profile --, mas escondê-la deixaria uma
    // sessao aberta sem nenhum no' por onde fecha-la.
    for (std::size_t c = 0; c < connections_.size(); ++c) {
        const Connection& connection = connections_[c];
        if (used[c] || connection.parent_id != 0) continue;
        if (connection.profile.folder != folder) continue;
        // A Session vazia de antes da primeira conexao. O teste era "host
        // vazio", mas o perfil padrao nasce com "localhost": ela aparecia
        // como um no' solto na raiz, igual a um perfil salvo e sem nada
        // dentro. So' ficou visivel quando o dialogo de conexao deixou de
        // cobrir a arvore no inicio. Desconectada e sem perfil salvo, nao ha'
        // sessao a fechar nem o que mostrar.
        if (connection.session->state() == SessionState::disconnected) continue;

        bool has_saved = false;
        for (const db::StoredProfile& stored : saved_profiles_) {
            has_saved |= same_target(stored.profile, connection.profile);
        }
        if (has_saved) continue;   // esta' noutra pasta

        TreeEntry entry;
        entry.connection = c;
        entries.push_back(entry);
    }
    return entries;
}

std::size_t MainShell::find_root_connection(
    const db::ConnectionProfile& profile) const {
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        // So' RAIZ: a sessao de um banco expandido tem o banco dele no
        // perfil, e casaria com um perfil salvo que aponte para o mesmo
        // banco -- escondendo esse perfil da arvore.
        if (connections_[i].parent_id != 0) continue;
        if (same_target(connections_[i].profile, profile)) return i;
    }
    return kNone;
}

std::size_t MainShell::find_database_connection(std::size_t parent_id,
                                                std::string_view database) const {
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i].parent_id == parent_id &&
            connections_[i].profile.database == database) {
            return i;
        }
    }
    return kNone;
}

// --- Escopo de conexao -----------------------------------------------------------
//
// As funcoes da arvore usam session(), a conexao ativa. Com varias conexoes
// na mesma arvore, cada subarvore precisa falar com a sessao DELA: enquanto
// e' desenhada, ela e' a corrente; ao fim, a anterior volta (ADR 0018).

std::size_t MainShell::push_tree_connection(std::size_t index) {
    const std::size_t previous = active_connection_;
    active_connection_ = index;
    // O SQL que os menus desta subarvore geram e' no dialeto DELA.
    if (index < connections_.size()) {
        db::set_sql_dialect_for(connections_[index].profile.driver_id);
    }
    return previous;
}

void MainShell::pop_tree_connection(std::size_t previous, std::size_t index,
                                    float top) {
    const float  bottom = ImGui::GetCursorScreenPos().y;
    const ImVec2 mouse  = ImGui::GetMousePos();

    // A mais INTERNA ganha: o banco expandido fica dentro da faixa da conexao
    // raiz, e fecha o escopo antes dela.
    const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                         ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    // AllowWhenBlockedByPopup: o botao direito abre o menu de contexto, e
    // quando descer e subir caem no MESMO quadro o menu ja' cobre a janela --
    // sem a flag, o clique que abriu o menu nao ativava a conexao, e o menu
    // agia sobre uma enquanto a barra mostrava outra.
    if (tree_claim_ == 0 && clicked &&
        ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
        mouse.y >= top && mouse.y < bottom && index < connections_.size()) {
        tree_claim_ = connections_[index].id;
    }
    active_connection_ = previous;
    if (previous < connections_.size()) {
        db::set_sql_dialect_for(connections_[previous].profile.driver_id);
    }
}

// --- No' da conexao --------------------------------------------------------------

void MainShell::draw_connection_node(std::size_t conn_index,
                                     std::size_t saved_index) {
    const Palette& p = colors();
    const bool     is_open = conn_index != kNone;

    // Copia, nao referencia: o no' vive um quadro, e os pedidos (conectar,
    // fechar) mexem nos vetores so' depois da arvore inteira.
    const db::ConnectionProfile profile =
        is_open ? connections_[conn_index].profile
                : saved_profiles_[saved_index].profile;

    const bool supported =
        is_open || saved_profiles_[saved_index].supported;
    const SessionState state =
        is_open ? connections_[conn_index].session->state()
                : SessionState::disconnected;
    const bool        connected = state == SessionState::connected;
    const std::size_t conn_id   = is_open ? connections_[conn_index].id : 0;
    const bool        is_active = is_open && conn_index == active_connection_;

    const std::string key = profile_key(profile);
    ImGui::PushID(key.c_str());

    const float       top      = ImGui::GetCursorScreenPos().y;
    const std::size_t previous = is_open ? push_tree_connection(conn_index) : 0;

    // Desconectar recolhe o no': aberto e desconectado, ele mostraria uma
    // subarvore vazia, e expandir de novo -- o gesto que conecta -- exigiria
    // recolher antes.
    // "Open Declaration": a raiz abre se o objeto e' desta conexao, ou de um
    // banco dela.
    bool reveal_root = false;
    if (is_open && tree_reveal_.frames > 0 && reveal_forcing()) {
        if (const Connection* target = connection_by_id(tree_reveal_.connection_id)) {
            reveal_root = target->id == conn_id || target->parent_id == conn_id;
        }
    }

    if (tree_collapse_.erase(key) > 0) {
        ImGui::SetNextItemOpen(false);
    } else if (reveal_root) {
        ImGui::SetNextItemOpen(true);
    } else if (is_open && connections_[conn_index].expand_once) {
        ImGui::SetNextItemOpen(true);
        connections_[conn_index].expand_once = false;
    }

    const std::uint32_t status_color =
        connected                           ? p.ok
        : state == SessionState::failed     ? p.error
        : state == SessionState::connecting ? p.warn
                                            : p.text_dim;

    ImGui::BeginDisabled(!supported);
    ImGui::BeginGroup();

    // Seta ou duplo clique expandem; clique simples so' seleciona. Expandir
    // CONECTA, como no DBeaver -- e conectar a cada rocada do mouse na lista
    // abriria conexao sem querer.
    const bool open = ImGui::TreeNodeEx(
        "##conn", ImGuiTreeNodeFlags_SpanAvailWidth |
                      ImGuiTreeNodeFlags_OpenOnArrow |
                      ImGuiTreeNodeFlags_OpenOnDoubleClick |
                      (is_active ? ImGuiTreeNodeFlags_Selected : 0));
    const bool toggled = ImGui::IsItemToggledOpen();

    // Icone do SGBD na cor do estado: identifica o banco sem ler o nome, e
    // diz se esta' conectado sem um segundo simbolo.
    same_line_after_arrow();
    // Sem driver (SQL Server importado do SSMS) o perfil guarda o driver
    // padrao; mostrar o elefante do PostgreSQL diria que e' um PostgreSQL.
    icon_inline(supported ? driver_icon(profile.driver_id) : Icon::database,
                status_color);

    // O icone do DBeaver tem cor propria e nao aceita a do estado. O estado
    // vai num ponto no canto dele -- o DBeaver poe um selo verde ou vermelho
    // no mesmo lugar.
    if (icon_set() == IconSet::dbeaver && is_open &&
        state != SessionState::disconnected) {
        const ImVec2 corner = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 at(corner.x - 3.0f, corner.y - 3.0f);
        dl->AddCircleFilled(at, 4.5f, ImGui::GetColorU32(ImGuiCol_WindowBg));
        dl->AddCircleFilled(at, 3.2f, col(status_color));
    }

    ImGui::SameLine(0.0f, tree_label_gap());
    ImGui::TextColored(col4(connected ? p.text_bright : p.text), "%s",
                       profile.effective_name().c_str());

    // "localhost:5432" ao lado do nome, esmaecido -- a mesma informacao, no
    // mesmo lugar, da arvore do DBeaver.
    ImGui::SameLine();
    ImGui::TextColored(col4(p.text_dim), "%s:%u", profile.host.c_str(),
                       static_cast<unsigned>(profile.port));

    ImGui::EndGroup();
    // EndGroup restaura o recuo salvo em BeginGroup e engole o do TreePush;
    // ver draw_relations_folder.
    if (open) ImGui::Indent();
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        const db::ConnectionTypeInfo& type = db::connection_type_info(profile.type);

        const std::string address =
            profile.host + ":" + std::to_string(static_cast<unsigned>(profile.port));

        Hint card(profile.effective_name());
        card.accent(TR("Type"), type.name);
        if (!supported) {
            card.text(TR(saved_profiles_[saved_index].unsupported_reason.c_str()));
        } else if (connected) {
            card.row(TR("Server"), dbms_name(profile.driver_id) + " " +
                                       session().server_version())
                .row(TR("User"), profile.user)
                .row(TR("Host"), address)
                .row(TR("Transactions"), profile.auto_commit ? TR("auto-commit")
                                                             : TR("manual transaction"))
                .row(TR("Access"), profile.read_only ? TR("read only") : "");
        } else if (state == SessionState::failed) {
            card.row(TR("User"), profile.user)
                .row(TR("Host"), address)
                .text(session().status_message());
        } else {
            card.row(TR("User"), profile.user)
                .row(TR("Host"), address)
                .text(TR("double-click to connect"));
        }
        card.text(profile.description);
        card.show();
    }

    const bool can_connect = supported && !connected &&
                             state != SessionState::connecting;

    if (!conn_menu_request_.empty() && conn_menu_request_ == profile.effective_name()) {
        ImGui::OpenPopup("##connmenu");
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetItemRectMin().x + 60.0f, ImGui::GetItemRectMax().y));
        conn_menu_request_.clear();
    }
    if (ImGui::BeginPopupContextItem("##connmenu")) {
        if (ImGui::MenuItem(TR("Connect"), nullptr, false, can_connect)) {
            if (is_open) tree_requests_.reconnect = conn_id;
            else         tree_requests_.open = profile;
        }
        if (ImGui::MenuItem(TR("Invalidate/Reconnect"), nullptr, false,
                            connected)) {
            tree_requests_.reconnect = conn_id;
        }
        if (ImGui::MenuItem(TR("Disconnect"), nullptr, false,
                            is_open && state != SessionState::disconnected)) {
            tree_requests_.disconnect = conn_id;
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TR("New SQL script"), nullptr, false, connected)) {
            tree_claim_ = conn_id;
            open_sql_tab(std::string{}, /*run=*/false);
        }
        if (ImGui::MenuItem(TR("Edit connection..."), nullptr, false, supported)) {
            if (is_open) tree_claim_ = conn_id;
            active_profile_ = profile;
            connection_dialog_.open_edit(profile);
        }
        // "Rename" fica no menu do no', como no DBeaver. So' se trocava o nome
        // abrindo o dialogo de edicao inteiro (pedido do usuario, 2026-10-01).
        if (ImGui::MenuItem(TR("Rename"), nullptr, false, saved_index != kNone)) {
            open_rename_connection(profile);
        }
        if (ImGui::MenuItem(TR("Delete"), nullptr, false, saved_index != kNone)) {
            tree_requests_.erase_saved = profile;
        }

        // Pastas. "Create > New Folder" numa conexao cria a pasta ao lado dela
        // e a poe dentro -- e' o que o DBeaver faz. Mover para uma pasta que ja'
        // existe, la', e' arrastar; aqui e' um submenu, que nao depende do
        // mouse acertar o alvo (e a raiz, que nao tem linha onde soltar, e' um
        // item como os outros).
        if (saved_index != kNone) {
            ImGui::Separator();
            draw_folder_create_menu(db::folder_parent(profile.folder), &profile);
            if (ImGui::BeginMenu(TR("Move to folder"))) {
                if (ImGui::MenuItem(TR("(root)"), nullptr, profile.folder.empty(),
                                    !profile.folder.empty())) {
                    tree_requests_.move = std::make_pair(profile, std::string{});
                }
                for (const std::string& folder : connection_folders()) {
                    const bool here = folder == profile.folder;
                    if (ImGui::MenuItem(folder.c_str(), nullptr, here, !here)) {
                        tree_requests_.move = std::make_pair(profile, folder);
                    }
                }
                ImGui::EndMenu();
            }
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TR("Copy name"))) {
            ImGui::SetClipboardText(profile.effective_name().c_str());
        }
        if (ImGui::MenuItem(TR("Refresh"), "F5", false,
                            connected && !session().busy())) {
            session().reload_catalog_async();
        }
        ImGui::EndPopup();
    }

    // Expandir conecta. `toggled && open`: recolher nao conta.
    if (toggled && open && can_connect) {
        if (is_open) tree_requests_.reconnect = conn_id;
        else         tree_requests_.open = profile;
    }

    if (open) {
        if (connected) {
            draw_connection_tree(conn_index);
        } else if (state == SessionState::connecting) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("  connecting..."));
        } else if (state == SessionState::failed) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
            ImGui::TextWrapped("%s", session().status_message().c_str());
            ImGui::PopStyleColor();
        }
        ImGui::TreePop();
    }

    if (is_open) pop_tree_connection(previous, conn_index, top);
    ImGui::PopID();
}

// O que uma conexao CONECTADA contem. A forma depende do SGBD.
void MainShell::draw_connection_tree(std::size_t conn_index) {
    if (session().is_mssql()) {
        draw_databases_folder(conn_index);
        draw_mssql_server_folders();
        return;
    }
    if (session().is_sqlanywhere()) {
        draw_sqlanywhere_tree();
        return;
    }
    if (session().has_database_level()) {
        draw_databases_folder(conn_index);
        draw_administer_folder();
        draw_server_lists_folder();
        return;
    }

    // MySQL: "database" e "schema" sao a mesma coisa, e a conexao enxerga
    // todos. schemas() JA' sao os bancos -- um nivel "Schemas" abaixo deles
    // seria o no' inventado que a diretiva 6 proibe.
    const std::vector<db::SchemaMeta> databases = session().schemas();
    const std::string current = connections_[conn_index].profile.database;

    ImGui::SetNextItemOpen(true, revealing_here() && reveal_forcing()
                                     ? ImGuiCond_Always
                                     : ImGuiCond_Once);
    folder_creates(db::ObjectType::database);
    if (draw_folder_node(Icon::database, TR("Databases"), databases.size(), true)) {
        for (const db::SchemaMeta& database : databases) {
            draw_schema_node(database, Icon::database, database.name == current);
        }
        ImGui::TreePop();
    }

    if (session().has_users())       draw_users_folder();

    // "Administer" do `<tree>` do MySQL: o Session Manager (MySQLSessionEditor).
    // A lista abre numa aba de resultado, com Kill Query / Kill Connection
    // acima da grade -- como a do PostgreSQL (ADR 0018).
    if (draw_folder_node(Icon::administer, TR("Administer"), 0, false)) {
        if (draw_action_leaf(Icon::sessions, TR("Session Manager"),
                             TR("Double-click to list the server sessions"))) {
            open_sql_tab(std::string(db::mysql_sessions_query()), /*run=*/true);
        }
        ImGui::TreePop();
    }

    if (session().has_server_info()) draw_server_info_folder();
}

// --- Bancos (PostgreSQL) ---------------------------------------------------------

void MainShell::draw_databases_folder(std::size_t root_index) {
    const Palette& p = colors();

    // Copias: a subarvore de um banco troca a conexao corrente, e nada aqui
    // pode depender de uma referencia para dentro de connections_.
    const std::size_t           root_id = connections_[root_index].id;
    const db::ConnectionProfile root    = connections_[root_index].profile;
    const std::string           current = session().database_name();

    std::vector<db::DatabaseMeta> databases = session().databases();

    // A lista pode ter falhado (sem privilegio em pg_database, por exemplo).
    // O banco conectado existe de qualquer modo -- a conexao esta' nele.
    const bool has_current =
        std::any_of(databases.begin(), databases.end(),
                    [&current](const db::DatabaseMeta& db) {
                        return db.name == current;
                    });
    if (!has_current) {
        db::DatabaseMeta self;
        self.name = current;
        databases.insert(databases.begin(), std::move(self));
    }

    // "Show all databases" desligado: so' o banco da conexao, como no
    // DBeaver (PostgreDataSource.isReadDatabaseList).
    //
    // No SQL Server a lista vem sempre: "Show All Databases" nasce ligado no
    // dialogo dele, e a conexao costuma apontar para `master`, onde nao ha'
    // nada do usuario para ver.
    const bool mssql    = session().is_mssql();
    const bool show_all = mssql || root.postgres.show_non_default_databases;
    if (!show_all) {
        std::erase_if(databases, [&current](const db::DatabaseMeta& db) {
            return db.name != current;
        });
    }

    std::int64_t largest = 0;
    for (const db::DatabaseMeta& db : databases) {
        largest = (std::max)(largest, db.size_bytes);
    }

    const Connection* reveal_target =
        tree_reveal_.frames > 0 && reveal_forcing()
            ? connection_by_id(tree_reveal_.connection_id)
            : nullptr;
    const bool reveal_under_root =
        reveal_target != nullptr &&
        (reveal_target->id == root_id || reveal_target->parent_id == root_id);

    ImGui::SetNextItemOpen(true, reveal_under_root ? ImGuiCond_Always
                                                   : ImGuiCond_Once);
    // "Create New Database" no menu da pasta. Faltava aqui -- so' a pasta do
    // MySQL o tinha --, e o botao direito em "Databases" de um PostgreSQL ou
    // de um SQL Server nao abria nada (relato do usuario, 2026-10-01).
    folder_creates(db::ObjectType::database);
    if (!draw_folder_node(Icon::database, TR("Databases"), databases.size(),
                          true)) {
        return;
    }

    for (const db::DatabaseMeta& db : databases) {
        const bool        is_default  = db.name == current;
        const std::size_t child_index =
            is_default ? root_index : find_database_connection(root_id, db.name);
        const bool is_active =
            !is_default && child_index != kNone && child_index == tree_previous_active_;

        ImGui::PushID(db.name.c_str());

        const SessionState child_state =
            child_index == kNone ? SessionState::disconnected
                                 : connections_[child_index].session->state();
        const bool child_connected = child_state == SessionState::connected;

        // Com a sessao do banco aberta, a LINHA dele ja' pertence a ela:
        // clicar no nome do banco o torna o ativo, como selecionar o no' no
        // DBeaver -- e nao so' clicar em algo dentro dele.
        const bool        scoped   = !is_default && child_connected;
        const float       node_top = ImGui::GetCursorScreenPos().y;
        const std::size_t previous = scoped ? push_tree_connection(child_index) : 0;

        // O banco da conexao nasce aberto: e' para onde o usuario vai em
        // nove de dez vezes, e os outros custam uma conexao cada.
        const bool reveal_db =
            reveal_under_root && child_index != kNone &&
            connections_[child_index].id == reveal_target->id;
        if (reveal_db)       ImGui::SetNextItemOpen(true);
        else if (is_default) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

        ImGui::BeginGroup();
        const bool open = ImGui::TreeNodeEx(
            "##db", ImGuiTreeNodeFlags_SpanAvailWidth |
                        (is_active ? ImGuiTreeNodeFlags_Selected : 0));

        same_line_after_arrow();
        icon_inline(Icon::database,
                    !db.allow_connect ? p.text_dim
                    : is_default      ? p.accent
                                      : p.accent_light);
        ImGui::SameLine(0.0f, tree_label_gap());

        // O DBeaver poe o banco da conexao em negrito; aqui e' a cor clara.
        ImGui::TextColored(col4(!db.allow_connect ? p.text_dim
                                : is_default      ? p.text_bright
                                                  : p.text),
                           "%s", db.name.c_str());

        if (db.size_bytes >= 0) {
            draw_size_bar(db.size_pretty,
                          largest > 0 ? static_cast<float>(db.size_bytes) /
                                            static_cast<float>(largest)
                                      : 0.0f);
        }
        ImGui::EndGroup();
        if (open) ImGui::Indent();   // ver draw_relations_folder

        if (ImGui::IsItemHovered()) {
            Hint(db.name)
                .row(TR("Owner"), db.owner)
                .row(mssql ? TR("Collation") : TR("Encoding"), db.encoding)
                .row(TR("Size"), db.size_bytes >= 0 ? db.size_pretty : std::string{})
                .row(TR("Template"), db.is_template ? TR("yes") : "")
                .text(db.allow_connect ? "" : TR("does not accept connections"))
                .text(db.comment)
                .show();
        }

        {
            db::ObjectRef tracked;
            tracked.type = db::ObjectType::database;
            tracked.name = db.name;
            nav_track(tracked);
        }
        if (ImGui::BeginPopupContextItem("##dbmenu")) {
            if (ImGui::MenuItem(TR("New SQL script"), nullptr, false,
                                child_connected)) {
                // A aba nasce na sessao DESTE banco, nao na da raiz.
                const std::size_t before = push_tree_connection(child_index);
                tree_claim_ = connections_[child_index].id;
                open_sql_tab(std::string{}, /*run=*/false);
                active_connection_ = before;
            }
            if (ImGui::MenuItem(TR("Invalidate/Reconnect"), nullptr, false,
                                !is_default && child_index != kNone &&
                                    child_state != SessionState::connecting)) {
                tree_requests_.reconnect = connections_[child_index].id;
            }
            ImGui::Separator();
            {
                // View Database, Create New Database, Rename, Delete, Tools
                // (Analyze, Vacuum, Reindex), Copy, Refresh.
                db::ObjectRef ref;
                ref.type = db::ObjectType::database;
                ref.name = db.name;
                draw_object_menu(ref);
            }
            ImGui::EndPopup();
        }

        if (open) {
            if (is_default) {
                draw_database_contents();
            } else if (!db.allow_connect) {
                ImGui::TextColored(col4(p.text_dim),
                                   "%s", TR("this database does not accept connections"));
            } else if (child_index == kNone) {
                // Cada banco do PostgreSQL e' um catalogo isolado: navegar
                // nele exige uma conexao propria, aberta so' agora que o
                // usuario pediu (ADR 0018).
                tree_requests_.open_database = db.name;
                tree_requests_.open_database_root = root_id;
                ImGui::TextColored(col4(p.text_dim), "%s", TR("  connecting..."));
            } else if (child_connected) {
                draw_database_contents();
            } else if (child_state == SessionState::connecting) {
                ImGui::TextColored(col4(p.text_dim), "%s", TR("  connecting..."));
            } else if (child_state == SessionState::disconnected) {
                // A sessao existe e nunca conectou: e' a de um script reaberto
                // ao iniciar (ui/script_session.cpp). Um botao, e nao conectar
                // sozinho a cada quadro -- se a conexao pede senha e o usuario
                // cancela, o pedido voltaria sem parar.
                ImGui::TextColored(col4(p.text_dim), "  %s", TR("not connected"));
                ImGui::SameLine();
                if (ImGui::SmallButton(TR("Connect"))) {
                    tree_requests_.reconnect = connections_[child_index].id;
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
                ImGui::TextWrapped(
                    "%s",
                    connections_[child_index].session->status_message().c_str());
                ImGui::PopStyleColor();
            }
            ImGui::TreePop();
        }

        if (scoped) pop_tree_connection(previous, child_index, node_top);
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// A coluna de tamanho da arvore do DBeaver: o numero sobre uma barra
// proporcional ao MAIOR banco, encostada na borda direita.
void MainShell::draw_size_bar(const std::string& text, float fraction) {
    const Palette& p = colors();

    ImGui::SameLine();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float  width  = ImGui::GetFontSize() * 3.4f;
    const float  height = ImGui::GetTextLineHeight();
    const float  right  = cursor.x + ImGui::GetContentRegionAvail().x;

    // SEMPRE encostada a' direita, mesmo por cima do fim de um nome comprido
    // -- como a coluna do DBeaver. A primeira versao a empurrava para depois
    // do nome quando nao cabia: a coluna deixava de ser coluna, cada linha
    // com o numero num lugar, e os nomes longos o jogavam para fora do
    // painel. O fundo opaco cobre o trecho do nome que ficaria por baixo; o
    // nome inteiro continua no tooltip e ao alargar o painel.
    const float  x0 = right - width;
    const ImVec2 min(x0, cursor.y);
    const ImVec2 max(x0 + width, cursor.y + height);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Apaga o que o nome escreveu daqui ate' a borda: um nome mais largo
    // que o painel continuava DEPOIS da barra, e sobravam duas letras soltas
    // ao lado do numero.
    dl->AddRectFilled(ImVec2(min.x - 4.0f, min.y),
                      ImVec2(dl->GetClipRectMax().x, max.y),
                      ImGui::GetColorU32(ImGuiCol_WindowBg));
    dl->AddRectFilled(min, max, col(p.bg_darkest), 2.0f);

    const float fill = width * (std::clamp)(fraction, 0.0f, 1.0f);
    if (fill > 1.0f) {
        dl->AddRectFilled(min, ImVec2(min.x + fill, max.y),
                          ImGui::GetColorU32(col(p.accent), 0.45f), 2.0f);
    }

    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    dl->AddText(ImVec2(max.x - size.x - 3.0f, min.y), col(p.text), text.c_str());

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, height));
}

// O que um banco contem: schemas e os objetos do nivel do banco. Roda com a
// sessao DO BANCO como corrente.
void MainShell::draw_database_contents() {
    if (session().is_mssql()) {
        draw_mssql_database_contents();
        return;
    }
    const std::vector<db::SchemaMeta> schemas = session().schemas();

    ImGui::SetNextItemOpen(true, revealing_here() && reveal_forcing()
                                     ? ImGuiCond_Always
                                     : ImGuiCond_Once);
    folder_creates(db::ObjectType::schema);
    if (draw_folder_node(Icon::schema, TR("Schemas"), schemas.size(), true)) {
        for (const db::SchemaMeta& schema : schemas) {
            // So' o `public` nasce aberto. Com todos abertos, um banco de
            // doze schemas ocupava a arvore inteira ao conectar.
            draw_schema_node(schema, Icon::schema, schema.name == "public");
        }
        ImGui::TreePop();
    }

    // Cada lista cujo item e' um objeto com editor (ui/object_editor.cpp).
    const auto objects_of = [](db::ObjectType type) {
        ListOptions options;
        options.object_type = type;
        return options;
    };

    ListOptions event_triggers = objects_of(db::ObjectType::event_trigger);
    event_triggers.off_suffix = "  [off]";
    draw_list_folder(Icon::event_trigger, TR("Event Triggers"),
                     db::CatalogList::event_triggers, Icon::event_trigger, {}, {},
                     {}, event_triggers);

    draw_list_folder(Icon::extension, TR("Extensions"),
                     db::CatalogList::extensions, Icon::extension, {}, {}, {},
                     objects_of(db::ObjectType::extension));

    if (draw_folder_node(Icon::storage, TR("Storage"), 0, false)) {
        draw_list_folder(Icon::tablespace, TR("Tablespaces"),
                         db::CatalogList::tablespaces, Icon::tablespace, {}, {}, {},
                         objects_of(db::ObjectType::tablespace));
        ImGui::TreePop();
    }

    if (draw_folder_node(Icon::system_info, TR("System Info"), 0, false)) {
        draw_list_folder(Icon::foreign_wrapper, TR("Foreign data wrappers"),
                         db::CatalogList::foreign_data_wrappers,
                         Icon::foreign_wrapper, {}, {}, {},
                         objects_of(db::ObjectType::foreign_data_wrapper));

        ListOptions servers = objects_of(db::ObjectType::foreign_server);
        servers.children = [this](const db::CatalogItem& server) {
            ListOptions mappings;
            mappings.object_type   = db::ObjectType::user_mapping;
            mappings.object_parent = server.name;
            draw_list_folder(Icon::user_mapping, TR("User Mappings"),
                             db::CatalogList::user_mappings, Icon::user_mapping,
                             server.name, {}, {}, mappings);
        };
        draw_list_folder(Icon::foreign_server, TR("Foreign servers"),
                         db::CatalogList::foreign_servers, Icon::foreign_server,
                         {}, {}, {}, servers);

        // Valor alterado em relacao ao padrao sai marcado: num servidor com
        // 350 parametros, e' o que se procura ao abrir esta pasta.
        ListOptions settings;
        settings.on_suffix = "  *";
        draw_list_folder(Icon::setting, TR("Settings"), db::CatalogList::settings,
                         Icon::setting, {}, {}, {}, settings);
        ImGui::TreePop();
    }

    // Role que pode fazer login e' usuario; a que nao pode e' grupo, e so' o
    // grupo tem a pasta Members -- `visibleIf="!object.canLogin"` no DBeaver.
    ListOptions members;
    members.off_icon    = Icon::role_group;
    members.object_type = db::ObjectType::role;

    ListOptions roles;
    roles.off_icon    = Icon::role_group;
    roles.object_type = db::ObjectType::role;
    roles.children = [this, members](const db::CatalogItem& role) {
        if (!role.flag) {
            draw_list_folder(Icon::role_group, TR("Members"),
                             db::CatalogList::role_members, Icon::role,
                             role.name, {}, {}, members);
        }
        draw_list_folder(Icon::role, TR("Roles"), db::CatalogList::role_belongs,
                         Icon::role, role.name, {}, {}, members);
    };
    draw_list_folder(Icon::role, TR("Roles"), db::CatalogList::roles, Icon::role,
                     {}, {}, {}, roles);
}

// --- Schema ----------------------------------------------------------------------

void MainShell::draw_schema_node(const db::SchemaMeta& schema, Icon icon,
                                 bool open_by_default) {
    ImGui::PushID(schema.name.c_str());

    if (revealing_here() && reveal_forcing() &&
        schema.name == tree_reveal_.schema) {
        ImGui::SetNextItemOpen(true);
    } else if (open_by_default) {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    }
    // "nav folder open|close <schema>" do canal de comandos.
    if (!tree_folder_request_.empty() && tree_folder_request_ == schema.name) {
        ImGui::SetNextItemOpen(tree_folder_request_open_, ImGuiCond_Always);
        tree_folder_request_.clear();
    }

    // Seta, icone, nome -- a ordem do DBeaver. Ver draw_folder_node.
    ImGui::BeginGroup();
    const bool open =
        ImGui::TreeNodeEx("##schema", ImGuiTreeNodeFlags_SpanAvailWidth);
    same_line_after_arrow();
    icon_inline(icon, colors().accent_light);
    ImGui::SameLine(0.0f, tree_label_gap());
    ImGui::TextUnformatted(schema.name.c_str());
    ImGui::EndGroup();
    if (open) ImGui::Indent();   // ver draw_relations_folder

    // Criar objeto pertence ao SCHEMA: e' ele que os contem. No no' da
    // tabela ficaria ambiguo -- "nova tabela" a partir de uma tabela sugere
    // duplica-la.
    {
        db::ObjectRef tracked;
        // No MySQL este no' E' o banco: "schema" e "database" sao o mesmo.
        // No SQL Anywhere e' o dono dos objetos -- um schema, sem nivel de
        // banco acima dele.
        tracked.type = session().has_database_level() || session().is_sqlanywhere()
                           ? db::ObjectType::schema
                           : db::ObjectType::database;
        tracked.name = schema.name;
        nav_track(tracked);
    }
    if (ImGui::BeginPopupContextItem("##schemamenu")) {
        const bool can_create =
            session().state() == SessionState::connected && !session().busy();

        if (ImGui::MenuItem(TR("New table..."), nullptr, false, can_create)) {
            open_create_table(schema.name);
        }
        if (ImGui::MenuItem(TR("New view..."), nullptr, false, can_create)) {
            open_create_view(schema.name);
        }

        if (session().has_database_level() || session().is_sqlanywhere()) {
            // O submenu "Create" do DBeaver: o que mora num schema. Os que o
            // SGBD nao cria por formulario somem (can_create_object).
            if (ImGui::BeginMenu(TR("Create"), can_create)) {
                for (const db::ObjectType type :
                     {db::ObjectType::table, db::ObjectType::view,
                      db::ObjectType::materialized_view, db::ObjectType::sequence,
                      db::ObjectType::function, db::ObjectType::procedure,
                      db::ObjectType::data_type, db::ObjectType::event}) {
                    create_menu_item(type, schema.name, {});
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            db::ObjectRef ref;
            ref.type = db::ObjectType::schema;
            ref.name = schema.name;
            // View Schema, Create New Schema, Rename, Delete, Tools, Copy,
            // Refresh.
            draw_object_menu(ref);
        } else {
            // MySQL: o que mora num banco, e o menu do proprio banco (View,
            // Create New Database, Delete, Tools: Dump / Restore / Execute
            // script).
            if (ImGui::BeginMenu(TR("Create"), can_create)) {
                for (const db::ObjectType type :
                     {db::ObjectType::table, db::ObjectType::view,
                      db::ObjectType::function, db::ObjectType::procedure,
                      db::ObjectType::event, db::ObjectType::sequence}) {
                    create_menu_item(type, schema.name, {});
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            db::ObjectRef ref;
            ref.type = db::ObjectType::database;
            ref.name = schema.name;
            draw_object_menu(ref);
        }
        ImGui::EndPopup();
    }

    if (open) {
        draw_schema_contents(schema);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// As pastas de um schema, na ordem do `<tree>` do DBeaver para o SGBD.
void MainShell::draw_schema_contents(const db::SchemaMeta& schema) {
    const bool pg = session().is_postgres();
    const bool ms = session().is_mssql();
    const bool sa = session().is_sqlanywhere();

    draw_relations_folder(schema, db::ObjKind::table, Icon::table, TR("Tables"));
    if (pg) {
        draw_relations_folder(schema, db::ObjKind::foreign_table,
                              Icon::foreign_table, TR("Foreign Tables"));
    }
    draw_relations_folder(schema, db::ObjKind::view, Icon::view, TR("Views"));
    if (pg || sa) {
        draw_relations_folder(schema, db::ObjKind::materialized_view,
                              Icon::materialized_view, TR("Materialized Views"));
    }
    if (pg || ms || sa) {

        // Todos os indices do schema numa lista so' -- a pasta "virtual" do
        // DBeaver. Dentro de cada tabela continuam os dela.
        ListOptions indexes;
        indexes.object_type   = db::ObjectType::index;
        indexes.object_schema = schema.name;
        draw_list_folder(Icon::index, TR("Indexes"),
                         db::CatalogList::schema_indexes, Icon::index,
                         schema.name, {}, {}, indexes);
    }

    draw_routines_folder(schema);

    // Pastas que o SGBD nao tem ficam FORA, em vez de aparecerem com (0):
    // "Sequences (0)" num MySQL sugere que ele poderia ter uma, e manda o
    // usuario procurar o que nao existe.
    if (session().has_sequences())  draw_sequences_folder(schema);
    if (ms) {
        // Um sinonimo e' outro nome para um objeto, daqui ou de outro banco;
        // o alvo vai ao lado.
        ListOptions synonyms;
        synonyms.empty_text = TR("no synonyms");
        draw_list_folder(Icon::synonym, TR("Synonyms"), db::CatalogList::synonyms,
                         Icon::synonym, schema.name, {}, {}, synonyms);
    }
    if (session().has_user_types()) draw_types_folder(schema);

    if (pg) {
        ListOptions aggregates;
        aggregates.object_type   = db::ObjectType::aggregate;
        aggregates.object_schema = schema.name;
        draw_list_folder(Icon::aggregate, TR("Aggregate functions"),
                         db::CatalogList::aggregates, Icon::aggregate,
                         schema.name, {}, {}, aggregates);
    }
    if (session().has_events()) draw_events_folder(schema);
}

// --- SQL Server -------------------------------------------------------------------

void MainShell::draw_mssql_database_contents() {
    const std::vector<db::SchemaMeta> schemas = session().schemas();

    ImGui::SetNextItemOpen(true, revealing_here() && reveal_forcing()
                                     ? ImGuiCond_Always
                                     : ImGuiCond_Once);
    folder_creates(db::ObjectType::schema);
    if (draw_folder_node(Icon::schema, TR("Schemas"), schemas.size(), true)) {
        for (const db::SchemaMeta& schema : schemas) {
            // So' o `dbo` nasce aberto -- o `public` do SQL Server.
            draw_schema_node(schema, Icon::schema, schema.name == "dbo");
        }
        ImGui::TreePop();
    }

    // Os gatilhos de DDL do banco (CREATE TRIGGER ... ON DATABASE).
    ListOptions triggers;
    triggers.off_suffix = "  [off]";
    draw_list_folder(Icon::trigger, TR("Database triggers"),
                     db::CatalogList::database_triggers, Icon::trigger, {}, {}, {},
                     triggers);
}

// --- SQL Anywhere -----------------------------------------------------------------
//
// O DBeaver nao tem plugin do SQL Anywhere: so' a arvore generica do driver
// "Sybase jConnect". As pastas daqui sao as do Sybase Central, a ferramenta do
// proprio SGBD (docs/SQLANYWHERE-MAP.md). Uma conexao fala com UM banco: nao
// ha' nivel "Databases", e o "schema" e' o dono dos objetos.
void MainShell::draw_sqlanywhere_tree() {
    const std::vector<db::SchemaMeta> owners = session().schemas();
    const std::string current = session().current_schema();

    ImGui::SetNextItemOpen(true, revealing_here() && reveal_forcing()
                                     ? ImGuiCond_Always
                                     : ImGuiCond_Once);
    if (draw_folder_node(Icon::schema, TR("Schemas"), owners.size(), true)) {
        for (const db::SchemaMeta& owner : owners) {
            // So' o do usuario conectado nasce aberto.
            draw_schema_node(owner, Icon::schema, owner.name == current);
        }
        ImGui::TreePop();
    }

    // Quem entra no banco, com o que recebeu; e os papeis, com os membros.
    draw_users_folder();

    ListOptions members;
    members.object_type = db::ObjectType::role;

    ListOptions roles;
    roles.object_type   = db::ObjectType::role;
    roles.object_parent = "role";   // e' o que faz o DROP ser DROP ROLE
    roles.children = [this, members](const db::CatalogItem& role) {
        draw_list_folder(Icon::role_group, TR("Members"),
                         db::CatalogList::role_members, Icon::user, role.name, {},
                         {}, members);
        draw_list_folder(Icon::role, TR("Roles"), db::CatalogList::role_belongs,
                         Icon::role, role.name, {}, {}, members);
    };
    draw_list_folder(Icon::folder_user, TR("Roles"), db::CatalogList::pure_roles,
                     Icon::role_group, {}, {}, {}, roles);

    ListOptions policies;
    policies.children = [this](const db::CatalogItem& policy) {
        draw_list_folder(Icon::setting, TR("Options"),
                         db::CatalogList::login_policy_options, Icon::setting,
                         policy.name);
    };
    draw_list_folder(Icon::policy, TR("Login Policies"),
                     db::CatalogList::login_policies, Icon::policy, {}, {}, {},
                     policies);

    if (draw_folder_node(Icon::storage, TR("Storage"), 0, false)) {
        // Cada dbspace e' um arquivo do banco; o caminho vai ao lado.
        draw_list_folder(Icon::tablespace, TR("Dbspaces"), db::CatalogList::tablespaces,
                         Icon::tablespace);
        ImGui::TreePop();
    }

    // O que liga o banco ao que esta' fora dele.
    ListOptions servers;
    servers.on_suffix  = "  read-only";
    servers.empty_text = TR("no remote servers");
    servers.children = [this](const db::CatalogItem& server) {
        draw_list_folder(Icon::user_mapping, TR("External Logins"),
                         db::CatalogList::user_mappings, Icon::user_mapping,
                         server.name);
    };
    draw_list_folder(Icon::foreign_server, TR("Remote Servers"),
                     db::CatalogList::foreign_servers, Icon::foreign_server, {}, {}, {},
                     servers);

    ListOptions services;
    services.off_suffix = "  [off]";
    services.empty_text = TR("no web services");
    draw_list_folder(Icon::foreign_wrapper, TR("Web Services"),
                     db::CatalogList::web_services, Icon::foreign_wrapper, {}, {}, {},
                     services);

    ListOptions publications;
    publications.empty_text = TR("no publications");
    draw_list_folder(Icon::dependency, TR("Publications"),
                     db::CatalogList::publications, Icon::dependency, {}, {}, {},
                     publications);

    draw_list_folder(Icon::search, TR("Text Configuration Objects"),
                     db::CatalogList::text_configurations, Icon::search);
    draw_list_folder(Icon::language, TR("External Environments"),
                     db::CatalogList::external_environments, Icon::language);
    draw_list_folder(Icon::access_method, TR("Spatial Reference Systems"),
                     db::CatalogList::spatial_reference_systems, Icon::access_method);

    if (draw_folder_node(Icon::administer, TR("Administer"), 0, false)) {
        if (draw_action_leaf(Icon::sessions, TR("Session Manager"),
                             TR("Double-click to list the server sessions"))) {
            open_sql_tab(std::string(db::sqlanywhere_sessions_query()), /*run=*/true);
        }
        if (draw_action_leaf(Icon::locks, TR("Lock Manager"),
                             TR("Double-click to list the locks in effect"))) {
            open_sql_tab(std::string(db::sqlanywhere_locks_query()), /*run=*/true);
        }
        // O que o Sybase Central poe no menu do banco. Aqui o banco nao tem
        // no' proprio (e' a conexao), e os tres ficam junto das sessoes.
        if (draw_action_leaf(Icon::save, TR("Checkpoint"),
                             TR("Double-click to write the changed pages to disk"))) {
            confirm_object_ddl(TR("Checkpoint"), db::sqlanywhere_checkpoint());
        }
        if (draw_action_leaf(Icon::constraint, TR("Validate database"),
                             TR("Double-click to check every page of the database"))) {
            confirm_object_ddl(TR("Validate database"),
                               db::sqlanywhere_validate_database());
        }
        if (draw_action_leaf(Icon::storage, TR("Backup database"),
                             TR("Double-click to copy the database files to a "
                                "directory of the server"))) {
            db::ObjectRef database;
            database.type = db::ObjectType::database;
            database.name = session().database_name();
            open_object_form(ObjectForm::Kind::sa_backup, std::move(database));
        }
        ImGui::TreePop();
    }

    draw_server_info_folder();
}

void MainShell::draw_mssql_server_folders() {
    // "Security" > "Logins": as contas do SERVIDOR. Os usuarios de cada banco
    // sao outra coisa, e aparecem na aba Permissions dos objetos.
    if (draw_folder_node(Icon::folder_user, TR("Security"), 0, false)) {
        ListOptions logins;
        logins.off_suffix  = "  [off]";
        logins.object_type = db::ObjectType::role;
        draw_list_folder(Icon::folder_user, TR("Logins"), db::CatalogList::logins,
                         Icon::user, {}, {}, {}, logins);
        ImGui::TreePop();
    }

    if (draw_folder_node(Icon::administer, TR("Administer"), 0, false)) {
        if (draw_action_leaf(Icon::sessions, TR("Session Manager"),
                             TR("Double-click to list the server sessions"))) {
            open_sql_tab(std::string(db::mssql_sessions_query()), /*run=*/true);
        }
        // O DBeaver nao tem Lock Manager para o SQL Server; a consulta e' a
        // mesma pergunta do PostgreSQL -- quem espera por quem.
        if (draw_action_leaf(Icon::locks, TR("Lock Manager"),
                             TR("Double-click to list who is blocking whom"))) {
            open_sql_tab(std::string(db::mssql_locks_query()), /*run=*/true);
        }
        ImGui::TreePop();
    }
}

// --- Nivel do servidor (PostgreSQL) ----------------------------------------------

// No' que so' executa uma acao -- Session Manager, Lock Manager.
bool MainShell::draw_action_leaf(Icon icon, const char* label,
                                 const char* tooltip) {
    const Palette& p = colors();

    ImGui::PushID(label);

    // O no' nao tem seta, mas e' irmao de pastas que tem: o recuo do espaco
    // da seta poe o icone dele na coluna dos icones das pastas. Sem isso
    // "Session Manager" ficava um passo a' esquerda de "Jobs", parecendo de
    // outro nivel.
    const float arrow = ImGui::GetTreeNodeToLabelSpacing() + tree_arrow_gap();
    ImGui::Indent(arrow);
    icon_inline(icon, p.accent_light);
    ImGui::SameLine(0.0f, tree_label_gap());

    bool activated = false;
    if (ImGui::Selectable(label, false,
                          ImGuiSelectableFlags_AllowDoubleClick)) {
        activated = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    }
    if (ImGui::IsItemHovered()) hint_fmt("%s", tooltip);

    // O mesmo pelo menu de contexto: duplo clique nao e' descobrivel, e o
    // DBeaver tambem oferece "Open" no menu do no'.
    if (ImGui::BeginPopupContextItem("##leafmenu")) {
        if (ImGui::MenuItem(TR("Open"))) activated = true;
        ImGui::EndPopup();
    }
    ImGui::Unindent(arrow);
    ImGui::PopID();
    return activated;
}

void MainShell::draw_administer_folder() {
    if (!draw_folder_node(Icon::administer, TR("Administer"), 0, false)) return;

    if (draw_action_leaf(Icon::sessions, TR("Session Manager"),
                         TR("Double-click to list the server sessions"))) {
        open_sql_tab(kSessionsSql, /*run=*/true);
    }

    // pg_blocking_pids existe desde o 9.6; antes disso a consulta falharia,
    // e o no' fica de fora -- como o `visibleIf supportsLocks()` do DBeaver.
    if (db::ServerVersion::parse(session().server_version()).at_least(9, 6) &&
        draw_action_leaf(Icon::locks, TR("Lock Manager"),
                         TR("Double-click to list who is blocking whom"))) {
        open_sql_tab(kLocksSql, /*run=*/true);
    }

    // pgAgent e' uma extensao. Lista vazia pode ser "nao instalado" ou "sem
    // jobs" -- o texto diz as duas, para o usuario nao concluir a errada.
    ListOptions jobs;
    jobs.off_suffix = "  [off]";
    jobs.empty_text = TR("no jobs (or pgAgent is not installed)");
    jobs.children = [this](const db::CatalogItem& job) {
        ListOptions steps;
        steps.off_suffix = "  [off]";
        // `detail` do job e' o id, que e' por onde passos e agendas sao
        // pedidos: dois jobs podem ter o mesmo nome.
        draw_list_folder(Icon::job_step, TR("Steps"), db::CatalogList::job_steps,
                         Icon::job_step, job.detail, {}, {}, steps);
        draw_list_folder(Icon::job_schedule, TR("Schedules"),
                         db::CatalogList::job_schedules, Icon::job_schedule,
                         job.detail, {}, {}, steps);
    };
    draw_list_folder(Icon::job, TR("Jobs"), db::CatalogList::jobs, Icon::job, {},
                     {}, {}, jobs);

    ImGui::TreePop();
}

// "System Info" no nivel da CONEXAO: o que e' do servidor, igual em todo
// banco -- por isso irmao de "Databases", nao filho.
void MainShell::draw_server_lists_folder() {
    if (!draw_folder_node(Icon::system_info, TR("System Info"), 0, false)) return;

    ListOptions methods;
    methods.children = [this](const db::CatalogItem& method) {
        ListOptions classes;
        classes.on_suffix = "  default";
        draw_list_folder(Icon::operator_class, TR("Operator classes"),
                         db::CatalogList::operator_classes, Icon::operator_class,
                         method.name, {}, {}, classes);
        draw_list_folder(Icon::operator_family, TR("Operator families"),
                         db::CatalogList::operator_families,
                         Icon::operator_family, method.name);
    };
    draw_list_folder(Icon::access_method, TR("Access Methods"),
                     db::CatalogList::access_methods, Icon::access_method, {}, {},
                     {}, methods);

    ListOptions encodings;
    encodings.on_suffix = "  *";
    draw_list_folder(Icon::encoding, TR("Encodings"), db::CatalogList::encodings,
                     Icon::encoding, {}, {}, {}, encodings);

    draw_list_folder(Icon::collation, TR("Collations"),
                     db::CatalogList::collations, Icon::collation);
    ListOptions languages;
    languages.object_type = db::ObjectType::language;
    draw_list_folder(Icon::language, TR("Languages"), db::CatalogList::languages,
                     Icon::language, {}, {}, {}, languages);

    // A instalada sai com o icone de extensao; a que so' esta' disponivel,
    // com o da caixa por abrir.
    ListOptions available;
    available.off_icon = Icon::extension_available;
    draw_list_folder(Icon::extension_available, TR("Available Extensions"),
                     db::CatalogList::available_extensions, Icon::extension, {},
                     {}, {}, available);

    ImGui::TreePop();
}

// --- Pasta generica --------------------------------------------------------------

// Uma pasta que LISTA: carrega ao expandir, mostra nome + detalhe, descricao
// no tooltip. Serve a todas as listas de CatalogList (ADR 0018).
void MainShell::draw_list_folder(Icon folder_icon, const char* label,
                                 db::CatalogList list, Icon item_icon,
                                 const std::string& a, const std::string& b,
                                 const std::string& c,
                                 const ListOptions& options) {
    const Palette& p = colors();

    // O id leva a lista alem do rotulo: "Roles" e' tanto a pasta do banco
    // quanto a de "a quais roles esta pertence", dentro de cada role.
    ImGui::PushID(static_cast<int>(list));

    const Session::ListState state = session().list(list, a, b, c);

    if (options.object_type) {
        folder_creates(*options.object_type, options.object_schema,
                       options.object_parent);
    }
    if (!draw_folder_node(folder_icon, label, state.items.size(), state.loaded)) {
        ImGui::PopID();
        return;
    }

    if (!state.loaded) {
        if (!session().busy()) session().load_list_async(list, a, b, c);
        ImGui::TextColored(col4(p.text_dim), "%s", TR("  loading..."));
    }

    // A recusa do servidor vai para a TELA: uma pasta vazia sem explicacao
    // seria lida como "nao ha' nenhum", quando o que falta e' privilegio.
    if (!state.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
        ImGui::TextWrapped("%s", state.error.c_str());
        ImGui::PopStyleColor();
    } else if (state.loaded && state.items.empty() &&
               options.empty_text != nullptr) {
        ImGui::TextColored(col4(p.text_dim), "%s", options.empty_text);
    }

    std::size_t shown = 0;
    for (std::size_t i = 0; i < state.items.size(); ++i) {
        const db::CatalogItem& item = state.items[i];
        if (!matches_filter(item.name)) continue;

        // pg_settings tem ~350 linhas, pg_collation mais de mil. Desenhar
        // tudo a cada quadro pesa, e ninguem acha nada rolando.
        if (++shown > 300) {
            ImGui::TextColored(col4(p.text_dim),
                               "%s", TR("... and more; use the filter"));
            break;
        }

        ImGui::PushID(static_cast<int>(i));

        const Icon icon = (!item.flag && options.off_icon) ? *options.off_icon
                                                           : item_icon;
        const char* suffix = item.flag ? options.on_suffix : options.off_suffix;
        const bool  off    = !item.flag && options.off_suffix != nullptr;

        bool open    = false;
        bool toggled = false;
        ImGui::BeginGroup();
        if (options.children) {
            open    = ImGui::TreeNodeEx("##item", ImGuiTreeNodeFlags_SpanAvailWidth);
            toggled = ImGui::IsItemToggledOpen();
            same_line_after_arrow();
            icon_inline(icon, p.accent_light);
            ImGui::SameLine(0.0f, tree_label_gap());
        } else {
            icon_inline(icon, off ? p.error : p.text_dim);
            ImGui::SameLine(0.0f, tree_label_gap());
        }

        ImGui::TextColored(col4(off ? p.text_dim : p.text), "%s",
                           item.name.c_str());
        if (!item.detail.empty() || suffix != nullptr) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s%s", item.detail.c_str(),
                               suffix != nullptr ? suffix : "");
        }
        ImGui::EndGroup();
        if (open) ImGui::Indent();   // ver draw_relations_folder

        if (!item.tooltip.empty() && ImGui::IsItemHovered()) {
            hint_fmt("%s", item.tooltip.c_str());
        }

        if (options.object_type) {
            db::ObjectRef ref;
            ref.type   = *options.object_type;
            ref.schema = options.object_schema;
            ref.parent = options.object_parent;
            ref.name   = item.name;
            // "soma_total(numeric)": a lista de agregados traz a assinatura
            // no nome, e o editor a quer separada.
            if (ref.type == db::ObjectType::aggregate) {
                const std::size_t paren = item.name.find('(');
                if (paren != std::string::npos && item.name.back() == ')') {
                    ref.name      = item.name.substr(0, paren);
                    ref.signature = item.name.substr(
                        paren + 1, item.name.size() - paren - 2);
                }
            }
            object_node(ref, toggled);
        }

        if (open) {
            options.children(item);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::TreePop();
    ImGui::PopID();
}

// --- Pedidos adiados -------------------------------------------------------------
//
// Conectar, fechar e abrir a sessao de um banco mexem em connections_. Feitos
// no meio do desenho, invalidariam os indices e as referencias que a arvore
// esta' usando -- por isso viram pedidos, atendidos depois do End().

void MainShell::apply_tree_requests() {
    TreeRequests requests = std::move(tree_requests_);
    tree_requests_ = {};

    const auto index_of = [this](std::size_t id) {
        for (std::size_t i = 0; i < connections_.size(); ++i) {
            if (connections_[i].id == id) return i;
        }
        return kNone;
    };

    if (requests.open) open_connection(*requests.open);

    if (requests.reconnect != 0) {
        const std::size_t index = index_of(requests.reconnect);
        if (index != kNone) {
            // As sessoes dos bancos pertencem a' conexao que caiu: reconectar
            // a raiz as descarta, e cada uma renasce ao expandir o banco.
            //
            // So' se a raiz chegou a conectar. Desconectada desde o inicio,
            // os "filhos" dela sao as sessoes dos scripts reabertos
            // (ui/script_session.cpp), que nunca conectaram: descarta-las
            // passaria os scripts para a raiz, e um script do banco ERP
            // rodaria no banco padrao sem ninguem ter pedido.
            if (connections_[index].parent_id == 0 &&
                connections_[index].session->state() != SessionState::disconnected) {
                close_database_connections(requests.reconnect);
            }
            const std::size_t again = index_of(requests.reconnect);
            Connection& connection = connections_[again];
            // A conexao FALHOU com uma senha digitada (nao salva): reconectar
            // pergunta de novo. Sem isto uma senha errada ficava valendo ate'
            // fechar o programa, e reconectar repetia o mesmo erro.
            if (connection.session->state() == SessionState::failed &&
                connection.credentials_asked && !connection.profile.save_password) {
                connection.credentials_asked = false;
                connection.profile.password.clear();
            }
            // A sessao de um banco usa as credenciais da RAIZ como estao
            // agora: a de um script reaberto foi criada antes de a raiz
            // conectar, e pediria de novo a senha que o usuario ja' digitou.
            if (connection.parent_id != 0) {
                if (const Connection* root = connection_by_id(connection.parent_id);
                    root != nullptr && root->credentials_asked) {
                    connection.profile.user      = root->profile.user;
                    connection.profile.password  = root->profile.password;
                    connection.credentials_asked = true;
                }
            }
            connection.session->disconnect();
            connect_session(connection);
            connection.expand_once = true;
        }
    }

    if (requests.disconnect != 0) {
        close_database_connections(requests.disconnect);
        const std::size_t index = index_of(requests.disconnect);
        if (index != kNone) {
            connections_[index].session->disconnect();
            tree_collapse_.insert(profile_key(connections_[index].profile));
        }
    }

    if (!requests.open_database.empty()) {
        const std::size_t root = index_of(requests.open_database_root);
        if (root != kNone &&
            find_database_connection(requests.open_database_root,
                                     requests.open_database) == kNone) {
            open_database_connection(root, requests.open_database);
        }
    }

    if (requests.erase_saved) {
        erase_candidate_   = *requests.erase_saved;
        show_erase_confirm_ = true;
    }

    if (requests.move) move_profile_to_folder(requests.move->first, requests.move->second);
}

void MainShell::connect_session(Connection& connection) {
    // Senha nao salva: pergunta antes de conectar, como o DBeaver. "Nao
    // salva" e' a caixa desmarcada -- uma senha vazia SALVA (um PostgreSQL
    // local em `trust`) conecta direto.
    const db::ConnectionProfile& wanted = connection.profile;
    if (!connection.credentials_asked &&
        wanted.auth_model == db::AuthModel::database_native &&
        !wanted.save_password && wanted.password.empty()) {
        close_auth_prompt();
        auth_prompt_.open          = true;
        auth_prompt_.connection_id = connection.id;
        std::snprintf(auth_prompt_.user, sizeof auth_prompt_.user, "%s",
                      wanted.user.c_str());
        return;
    }

    // Templates e bancos sem acesso so' fazem sentido com "Show all
    // databases" ligado -- e' o que o tooltip da caixa diz no DBeaver.
    const db::PostgresOptions& pg = connection.profile.postgres;
    connection.session->set_database_listing(
        pg.show_non_default_databases && pg.show_template_databases,
        pg.show_non_default_databases && pg.show_unavailable_databases);
    connection.session->connect_async(connection.profile.to_conn_config());
}

// Separado de open_database_connection: um script reaberto ao iniciar
// (ui/script_session.cpp) precisa da entrada da sessao do banco dele, mas
// nao deve conectar sozinho.
std::size_t MainShell::add_database_connection(std::size_t root_index,
                                               const std::string& database) {
    // O perfil da raiz, apontando para o outro banco. Mesmas credenciais,
    // mesmo TLS, mesmas preferencias de editor.
    db::ConnectionProfile profile = connections_[root_index].profile;
    const std::string     label   = profile.effective_name();
    profile.database = database;
    profile.name     = label + " / " + database;

    Connection child;
    child.session   = std::make_unique<Session>();
    child.profile   = std::move(profile);
    child.id        = next_connection_id_++;
    child.parent_id = connections_[root_index].id;
    // A senha digitada para a raiz vale para os bancos dela.
    child.credentials_asked = connections_[root_index].credentials_asked;

    // Nao troca a conexao ativa nem cria aba: expandir um banco e' olhar, e
    // a janela de editor dele so' aparece quando ganhar o primeiro script.
    connections_.push_back(std::move(child));
    return connections_.size() - 1;
}

void MainShell::open_database_connection(std::size_t root_index,
                                         const std::string& database) {
    const std::size_t child = add_database_connection(root_index, database);
    connect_session(connections_[child]);
}

void MainShell::close_database_connections(std::size_t root_id) {
    for (std::size_t i = connections_.size(); i-- > 0;) {
        if (connections_[i].parent_id != root_id) continue;

        // Os scripts da sessao que fecha ficam com a conexao RAIZ dela -- o
        // mesmo servidor. Sem isto caiam na conexao que estivesse ativa, e um
        // SELECT do banco ERP aparecia na janela de um MySQL.
        const std::size_t child_id = connections_[i].id;
        for (std::unique_ptr<SqlDocument>& document : documents_) {
            if (document->connection_id() == child_id) {
                document->set_connection_id(root_id);
            }
        }
        close_connection(i);
    }
}

// Confirmacao de exclusao. Apagar o perfil perde a senha gravada, e nao ha'
// desfazer.
void MainShell::draw_erase_connection_confirm() {
    if (show_erase_confirm_) {
        ImGui::OpenPopup(TR("Delete connection?###EraseConnection"));
        show_erase_confirm_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal(TR("Delete connection?###EraseConnection"),
                                nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    // TextUnformatted: o nome vem do usuario, e um '%' nele seria lido como
    // formato.
    ImGui::TextUnformatted(TRF("Delete the connection \"%s\"?",
                               erase_candidate_.effective_name().c_str()));
    ImGui::TextColored(col4(colors().text_dim),
                       "%s", TR("The saved password is removed too. Open scripts are kept."));
    ImGui::Spacing();

    if (ImGui::Button(TR("Delete"))) {
        // A conexao aberta fecha junto, com as sessoes dos bancos dela.
        const std::size_t open = find_root_connection(erase_candidate_);
        if (open != kNone) {
            close_database_connections(connections_[open].id);
            close_connection(find_root_connection(erase_candidate_));
        }

        std::erase_if(saved_profiles_, [this](const db::StoredProfile& stored) {
            return same_target(stored.profile, erase_candidate_);
        });
        persist_profiles();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// --- Autenticacao ----------------------------------------------------------------
//
// O BaseAuthDialog do DBeaver: titulo "'<conexao>' Authentication", o grupo
// "User Credentials:" com Username e Password, e a caixa "Save
// Password/Passphrase".

void MainShell::close_auth_prompt() {
    // A senha digitada nao fica na memoria do dialogo depois de fechado.
    std::memset(auth_prompt_.password, 0, sizeof auth_prompt_.password);
    auth_prompt_ = AuthPrompt{};
}

void MainShell::draw_auth_prompt() {
    AuthPrompt& prompt = auth_prompt_;
    if (!prompt.open) {
        auth_prompt_submit_ = false;
        return;
    }

    Connection* connection = connection_by_id(prompt.connection_id);
    if (connection == nullptr) {   // a conexao foi fechada com o dialogo aberto
        close_auth_prompt();
        return;
    }

    const Palette& p = colors();
    const std::string title = "'" + connection->profile.effective_name() + "' " +
                              TR("Authentication") + "###AuthPrompt";

    bool open = true;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    // Opaca (diretiva 13): flutua sobre a arvore e o editor.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible =
        ImGui::Begin(title.c_str(), &open,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();

    bool accepted = false;
    bool cancelled = !open;

    if (visible) {
        ImGui::TextColored(col4(p.text_dim), "%s:%u", connection->profile.host.c_str(),
                           static_cast<unsigned>(connection->profile.port));
        ImGui::SeparatorText(TR("User Credentials:"));

        if (ImGui::BeginTable("##credentials", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("##field", ImGuiTableColumnFlags_WidthFixed, 300.0f);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Username:"));
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            // O teclado vai para o campo que falta: a senha, quando o usuario
            // ja' veio do perfil.
            if (!prompt.focused && prompt.user[0] == '\0') {
                ImGui::SetKeyboardFocusHere();
                prompt.focused = true;
            }
            ImGui::InputText("##user", prompt.user, sizeof prompt.user);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(TR("Password:"));
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (!prompt.focused) {
                ImGui::SetKeyboardFocusHere();
                prompt.focused = true;
            }
            if (ImGui::InputText("##password", prompt.password, sizeof prompt.password,
                                 ImGuiInputTextFlags_Password |
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
                accepted = true;
            }
            ImGui::EndTable();
        }

        ImGui::Checkbox(TR("Save Password/Passphrase"), &prompt.save);
        if (prompt.save) {
            // O mesmo aviso do dialogo de conexao: a cifra do arquivo e' a do
            // DBeaver, com chave publica (ADR 0012).
            icon_inline(Icon::warning, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn), "%s",
                               TR("weak encryption, for DBeaver compatibility"));
        }

        ImGui::Separator();
        if (ImGui::Button(TR("OK"), ImVec2(120, 0))) accepted = true;
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) cancelled = true;
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancelled = true;
        }
    }
    ImGui::End();

    if (auth_prompt_submit_) accepted = true;
    auth_prompt_submit_ = false;

    if (accepted) {
        const db::ConnectionProfile original = connection->profile;
        connection->profile.user     = prompt.user;
        connection->profile.password = prompt.password;
        connection->credentials_asked = true;

        if (prompt.save) {
            connection->profile.save_password = true;
            // No perfil SALVO de mesmo alvo -- o alvo de ANTES de o usuario
            // ser trocado aqui, que e' como ele esta' gravado.
            bool changed = false;
            for (db::StoredProfile& stored : saved_profiles_) {
                if (!same_target(stored.profile, original)) continue;
                stored.profile.user          = connection->profile.user;
                stored.profile.password      = connection->profile.password;
                stored.profile.save_password = true;
                changed = true;
            }
            if (changed) persist_profiles();
        }

        const std::size_t id = prompt.connection_id;
        close_auth_prompt();
        if (Connection* target = connection_by_id(id)) {
            target->expand_once = true;
            connect_session(*target);
        }
        return;
    }
    if (cancelled) close_auth_prompt();
}

} // namespace otter::ui
