// C-Otter -- ui/script_session.cpp
//
// Os scripts do MainShell em disco: gravacao automatica, o indice da sessao e
// a reabertura ao iniciar. O formato e os nomes de arquivo estao em
// ui/script_store.hpp, que nao depende do ImGui e e' o que os testes cobrem.
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "ui/script_store.hpp"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace otter::ui {
namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// "Alguns ms apos parar a digitacao" (pedido do usuario). Curto o bastante
// para que encerrar o programa a' forca perca no maximo a ultima palavra;
// longo o bastante para nao regravar o arquivo a cada tecla.
constexpr double kAutosaveDelay = 0.4;

// O indice e' remontado e comparado neste intervalo; so' e' gravado se mudou.
constexpr double kIndexInterval = 0.5;

// Depois de uma gravacao que falhou (pasta so' de leitura, disco cheio):
// tentar de novo a cada quadro encheria a barra de status.
constexpr double kRetryDelay = 5.0;

bool same_target(const db::ConnectionProfile& a, const db::ConnectionProfile& b) {
    return a.driver_id == b.driver_id && a.host == b.host && a.port == b.port &&
           a.database == b.database && a.user == b.user;
}

} // namespace

// Os nomes que as abas abertas ja' usam -- o de arquivo, ou o reservado para
// a que ainda nao foi gravada.
std::vector<std::string> MainShell::open_script_names(const SqlDocument* except) const {
    std::vector<std::string> names;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document.get() == except || document->is_object()) continue;
        if (document->file_path().empty()) {
            if (!document->default_title().empty()) {
                names.push_back(document->default_title());
            }
        } else {
            names.push_back(
                std::filesystem::path(document->file_path()).stem().string());
        }
    }
    return names;
}

ScriptEntry MainShell::script_entry(const SqlDocument& document) const {
    ScriptEntry entry;
    entry.file   = script_file_field(scripts_dir_, document.file_path());
    entry.title  = document.custom_title();
    entry.pinned = document.pinned();
    entry.open   = true;

    const Connection* owner = connection_by_id(document.connection_id());
    if (owner == nullptr) return entry;

    // A sessao de outro banco do servidor (ADR 0018) e' gravada como a
    // conexao RAIZ mais o nome do banco: o perfil dela e' derivado e nao
    // existe entre os salvos.
    const Connection* root = owner;
    if (owner->parent_id != 0) {
        if (const Connection* parent = connection_by_id(owner->parent_id)) {
            root           = parent;
            entry.database = owner->profile.database;
        }
    }

    // So' conexao SALVA: uma aberta por URL ou pelo ambiente nao existira'
    // na proxima execucao, e o nome dela casaria com outra por acaso.
    for (const db::StoredProfile& stored : saved_profiles_) {
        if (!same_target(stored.profile, root->profile)) continue;
        entry.connection_id = stored.id;
        entry.connection    = stored.profile.effective_name();
        break;
    }
    if (entry.connection.empty()) entry.database.clear();
    return entry;
}

// A conexao de um script reaberto, SEM conectar: a aba volta com o nome da
// conexao no titulo e a sessao desconectada. Devolve 0 se o perfil nao
// existe mais.
//
// Nao conecta de proposito. O DBeaver conecta ao ativar o editor
// (`SQLEditor.connectOnActivation`); aqui isso faria o programa abrir
// conexao -- talvez de producao, talvez pedindo senha -- so' por ter sido
// iniciado. Quem conecta e' o usuario: pela arvore ou pelo "Connect" da aba.
std::size_t MainShell::ensure_script_connection(const ScriptEntry& entry) {
    const db::StoredProfile* found = nullptr;
    if (!entry.connection_id.empty()) {
        for (const db::StoredProfile& stored : saved_profiles_) {
            if (stored.id == entry.connection_id) {
                found = &stored;
                break;
            }
        }
    }
    if (found == nullptr && !entry.connection.empty()) {
        for (const db::StoredProfile& stored : saved_profiles_) {
            if (stored.profile.effective_name() == entry.connection) {
                found = &stored;
                break;
            }
        }
    }
    if (found == nullptr || !found->supported) return 0;

    std::size_t root = find_root_connection(found->profile);
    if (root == kNone) {
        Connection connection;
        connection.session = std::make_unique<Session>();
        connection.profile = found->profile;
        connection.id      = next_connection_id_++;
        connections_.push_back(std::move(connection));
        root = connections_.size() - 1;
    }

    const std::size_t root_id = connections_[root].id;
    if (entry.database.empty() ||
        entry.database == connections_[root].profile.database) {
        return root_id;
    }

    std::size_t child = find_database_connection(root_id, entry.database);
    if (child == kNone) child = add_database_connection(root, entry.database);
    return connections_[child].id;
}

SqlDocument* MainShell::open_stored_script(const ScriptEntry& entry) {
    const std::string path = script_path(scripts_dir_, entry.file);

    // Fora da lista de fechados de qualquer jeito: ou vira aba, ou o arquivo
    // sumiu e nao ha' o que listar.
    std::erase_if(closed_scripts_,
                  [&entry](const ScriptEntry& closed) { return closed.file == entry.file; });

    const auto text = read_script(path);
    if (!text) return nullptr;

    // Antes de criar a aba: new_document() amarra a' conexao ATIVA, e a
    // conexao do script pode ter de nascer agora.
    const std::size_t connection_id = ensure_script_connection(entry);

    SqlDocument& document = new_document();
    document.editor().SetText(*text);
    document.set_storage_path(path);
    document.set_title(entry.title);
    document.set_pinned(entry.pinned);
    if (connection_id != 0) document.set_connection_id(connection_id);
    document.mark_saved();
    document.autosave().on_disk = true;
    return &document;
}

// Ao iniciar: as abas que estavam abertas voltam, cada uma na conexao dela.
// Sem script nenhum o programa abre sem aba (pedido do usuario, 2026-10-01).
void MainShell::restore_scripts() {
    const ScriptSession session = load_script_session(scripts_dir_);
    std::size_t active_id = 0;

    for (const ScriptEntry& entry : session.scripts) {
        const std::string path = script_path(scripts_dir_, entry.file);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) continue;   // apagado por fora

        if (!entry.open) {
            closed_scripts_.push_back(entry);
            continue;
        }
        if (const SqlDocument* document = open_stored_script(entry)) {
            if (entry.file == session.active) active_id = document->id();
        }
    }

    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i]->id() != active_id) continue;
        active_document_        = i;
        restore_front_document_ = active_id;
        restore_front_frames_   = 3;
    }

    // O que esta' em disco agora: o indice so' e' regravado quando diferir.
    script_index_written_ = serialize_script_session(session);
}

bool MainShell::save_script_now(SqlDocument& document) {
    SqlDocument::Autosave& state = document.autosave();
    const std::string      text  = document.editor().GetText();

    std::string path = document.file_path();
    if (path.empty()) {
        // Aba nova e vazia nao vira arquivo: o DBeaver apaga o script novo
        // que ficou vazio (`script.delete.empty`), e aqui ele nem nasce.
        if (text.empty()) {
            state.pending = false;
            document.mark_saved();
            return true;
        }

        // O nome reservado na criacao da aba -- a menos que outro arquivo
        // tenha aparecido com ele nesse meio tempo.
        std::string name = document.default_title();
        std::error_code ec;
        if (name.empty() ||
            std::filesystem::exists(script_path(scripts_dir_, script_file_name(name)), ec)) {
            name = unique_script_name(scripts_dir_, open_script_names(&document));
            document.set_default_title(name);
        }
        path = script_path(scripts_dir_, script_file_name(name));
        document.set_storage_path(path);
    }

    if (text.empty() && is_stored_script(scripts_dir_, path)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        state.on_disk = false;
        state.pending = false;
        document.mark_saved();
        return true;
    }

    if (!write_script(path, text)) {
        document.set_status(std::string(TRF("cannot write %s", path.c_str())));
        // Pelo relogio guardado, e nao por ImGui::GetTime(): isto tambem roda
        // ao sair, depois do ultimo quadro.
        state.changed_at = script_clock_ + kRetryDelay;
        return false;
    }
    state.on_disk = true;
    state.pending = false;
    // O ponto de salvamento: o indicador de "modificado" da aba apaga.
    document.mark_saved();
    return true;
}

void MainShell::write_script_index() {
    ScriptSession session;

    const SqlDocument* front = active_document();
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->is_object()) continue;
        const SqlDocument::Autosave& state = document->autosave();
        if (state.off || !state.on_disk || document->file_path().empty()) continue;

        session.scripts.push_back(script_entry(*document));
        if (document.get() == front) session.active = session.scripts.back().file;
    }
    for (const ScriptEntry& closed : closed_scripts_) session.scripts.push_back(closed);

    const std::string text = serialize_script_session(session);
    if (text == script_index_written_) return;
    if (save_script_session(scripts_dir_, session)) script_index_written_ = text;
}

// A cada quadro. Barato quando nada mudou: um indice de desfazer por aba.
void MainShell::autosave_scripts() {
    const double now = ImGui::GetTime();
    script_clock_ = now;

    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        // A aba de um objeto guarda o DDL lido do servidor: nao e' um script.
        if (document->is_object()) continue;
        SqlDocument::Autosave& state = document->autosave();
        if (state.off) continue;

        // O indice de desfazer anda a cada edicao (digitar, colar, formatar,
        // desfazer). Comparar o TEXTO a cada quadro custaria uma copia do
        // script inteiro 144 vezes por segundo.
        const std::size_t index = document->editor().GetUndoIndex();
        if (index != state.seen_index) {
            state.seen_index = index;
            state.changed_at = now;
            state.pending    = true;
        }
        if (state.pending && now - state.changed_at >= kAutosaveDelay) {
            save_script_now(*document);
        }
    }

    if (now - script_index_checked_at_ >= kIndexInterval) {
        script_index_checked_at_ = now;
        write_script_index();
    }
}

// Sem esperar o atraso: ao sair, e antes de fechar uma aba.
void MainShell::flush_scripts() {
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->is_object()) continue;
        SqlDocument::Autosave& state = document->autosave();
        if (state.off) continue;
        if (document->editor().GetUndoIndex() != state.seen_index) {
            state.seen_index = document->editor().GetUndoIndex();
            state.pending    = true;
        }
        if (state.pending) save_script_now(*document);
    }
    write_script_index();
}

// A aba vai fechar: o que foi digitado e' gravado, e o script passa a' lista
// dos fechados -- o arquivo continua em `.script`, com a conexao dele, e
// volta por "Show scripts". E' o que o DBeaver faz: fechar o editor nao apaga
// o script.
void MainShell::retire_script(SqlDocument& document) {
    if (document.is_object()) return;
    SqlDocument::Autosave& state = document.autosave();
    if (state.off) return;

    if (document.editor().GetUndoIndex() != state.seen_index) state.pending = true;
    if (state.pending) save_script_now(document);
    if (document.file_path().empty() || !state.on_disk) return;

    ScriptEntry entry = script_entry(document);
    entry.open = false;
    std::erase_if(closed_scripts_,
                  [&entry](const ScriptEntry& closed) { return closed.file == entry.file; });
    closed_scripts_.push_back(std::move(entry));
}

bool MainShell::can_connect_from_tab(std::size_t connection_id) const {
    const Connection* connection = connection_by_id(connection_id);
    if (connection == nullptr) return false;
    if (connection->parent_id != 0) return true;
    return std::any_of(saved_profiles_.begin(), saved_profiles_.end(),
                       [connection](const db::StoredProfile& stored) {
                           return stored.supported &&
                                  same_target(stored.profile, connection->profile);
                       });
}

// O nome de uma aba nova: o primeiro da serie do DBeaver que nao e' de um
// arquivo em disco nem de outra aba aberta.
std::string MainShell::next_script_name() const {
    return unique_script_name(scripts_dir_, open_script_names(nullptr));
}

} // namespace otter::ui
