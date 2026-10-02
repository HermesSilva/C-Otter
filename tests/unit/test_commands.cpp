// O registro de comandos do editor SQL (ui/commands.hpp) e os dois perfis de
// atalhos.
//
// O que estes testes impedem de voltar:
//   - uma tecla do DBeaver sumir ou mudar sem ninguem perceber;
//   - dois comandos com a mesma tecla no mesmo perfil (so' um dispararia);
//   - o perfil C-Otter tomar, sem querer, uma tecla que o editor ja' usa;
//   - um comando "parcial" sem dizer na tela o que falta (diretiva 6).
#include "test_main.hpp"

#include "base/paths.hpp"
#include "ui/app_settings.hpp"
#include "ui/commands.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace otter::ui;

namespace {

constexpr ImGuiKeyChord C   = ImGuiMod_Ctrl;
constexpr ImGuiKeyChord A   = ImGuiMod_Alt;
constexpr ImGuiKeyChord CS  = ImGuiMod_Ctrl | ImGuiMod_Shift;
constexpr ImGuiKeyChord CA  = ImGuiMod_Ctrl | ImGuiMod_Alt;
constexpr ImGuiKeyChord CAS = ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Shift;

// Os atalhos de plugins/org.jkiss.dbeaver.ui.editors.sql/plugin.xml
// (elementos <key>, plataforma win32), um por linha. E' a lista contra a
// qual o perfil DBeaver e' conferido -- o denominador (diretiva 4).
struct Expected {
    const char*   dbeaver_id;
    ImGuiKeyChord chord;
};
const Expected kDbeaverKeys[] = {
    {"core.sql.editor.console", CA | ImGuiKey_Enter},
    {"core.sql.editor.create", C | ImGuiKey_F3},
    {"core.sql.editor.create", C | ImGuiKey_RightBracket},
    {"core.sql.editor.open", C | ImGuiKey_LeftBracket},
    {"core.sql.editor.open", ImGuiKey_F3},
    {"core.sql.editor.recent", C | ImGuiKey_Enter},
    {"core.sql.script.run.scriptNative", A | ImGuiKey_N},
    {"ui.editors.sql.close.tab", CS | ImGuiKey_Backslash},
    {"ui.editors.sql.comment.multi", CS | ImGuiKey_Slash},
    {"ui.editors.sql.comment.single", C | ImGuiKey_Slash},
    {"ui.editors.sql.gotoMatchingBracket", CS | ImGuiKey_LeftBracket},
    {"ui.editors.sql.maximize.result.panel", CS | ImGuiKey_T},
    {"ui.editors.sql.navigate.object", ImGuiKey_F4},
    {"ui.editors.sql.open.file", CAS | ImGuiKey_O},
    {"ui.editors.sql.query.next", A | ImGuiKey_DownArrow},
    {"ui.editors.sql.query.prev", A | ImGuiKey_UpArrow},
    {"ui.editors.sql.rename", C | ImGuiKey_F2},
    {"ui.editors.sql.run.all.rows", CAS | ImGuiKey_A},
    {"ui.editors.sql.run.explain", CS | ImGuiKey_E},
    {"ui.editors.sql.run.expression", CA | ImGuiKey_Apostrophe},
    {"ui.editors.sql.run.script", A | ImGuiKey_X},
    {"ui.editors.sql.run.scriptFromPosition", A | ImGuiKey_P},
    {"ui.editors.sql.run.scriptNew", CAS | ImGuiKey_X},
    {"ui.editors.sql.run.statement", C | ImGuiKey_Enter},
    {"ui.editors.sql.run.statementNew", C | ImGuiKey_Backslash},
    {"ui.editors.sql.selectToMatchingBracket", CS | ImGuiKey_RightBracket},
    {"ui.editors.sql.show.output", CS | ImGuiKey_O},
    {"ui.editors.sql.switch.panel", A | ImGuiKey_T},
    {"ui.editors.sql.sync.connection", CS | ImGuiKey_Period},
    {"ui.editors.sql.toggle.pinned.tab", CS | ImGuiKey_P},
    {"ui.editors.sql.toggle.result.panel", C | ImGuiKey_T},
    {"ui.editors.sql.word.wrap", CAS | ImGuiKey_W},
    {"ui.editors.text.content.format", CS | ImGuiKey_F},
};

bool has_key(const CommandInfo& info, Keymap keymap, ImGuiKeyChord chord) {
    const auto keys = command_keys(info.id, keymap);
    return std::find(keys.begin(), keys.end(), chord) != keys.end();
}

} // namespace

OTTER_TEST(commands_table_follows_the_enum) {
    // command_info() indexa pelo enum: uma linha fora de ordem faria um
    // comando devolver o rotulo e a tecla de OUTRO.
    const auto commands = all_commands();
    OTTER_CHECK_EQ(commands.size(), kCommandCount);
    for (std::size_t i = 0; i < commands.size(); ++i) {
        OTTER_CHECK_EQ(static_cast<std::size_t>(commands[i].id), i);
    }
}

OTTER_TEST(commands_dbeaver_keymap_has_every_dbeaver_shortcut) {
    for (const Expected& expected : kDbeaverKeys) {
        const CommandInfo* found = nullptr;
        for (const CommandInfo& info : all_commands()) {
            if (info.dbeaver_id == expected.dbeaver_id) found = &info;
        }
        if (found == nullptr) {
            std::printf("    sem comando para %s\n", expected.dbeaver_id);
            OTTER_CHECK(found != nullptr);
            continue;
        }
        if (!has_key(*found, Keymap::dbeaver, expected.chord)) {
            std::printf("    %s sem a tecla %s\n", expected.dbeaver_id,
                        chord_label(expected.chord).c_str());
        }
        OTTER_CHECK(has_key(*found, Keymap::dbeaver, expected.chord));
    }
}

OTTER_TEST(commands_no_two_commands_share_a_key_in_a_keymap) {
    // Editor e global valem ao mesmo tempo (o editor em foco tambem recebe
    // as globais), entao disputam entre si. O mesmo vale para a grade e as
    // globais. Editor, grade e navegador sao focos DIFERENTES: Ctrl+D apaga a
    // linha num e copia da linha de cima no outro, sem conflito.
    for (const Keymap keymap : {Keymap::dbeaver, Keymap::otter}) {
        std::map<ImGuiKeyChord, const char*> editor_and_global;
        std::map<ImGuiKeyChord, const char*> grid_and_global;
        std::map<ImGuiKeyChord, const char*> navigator;

        const auto claim = [&](std::map<ImGuiKeyChord, const char*>& seen,
                               ImGuiKeyChord chord, const CommandInfo& info) {
            const auto [it, inserted] = seen.emplace(chord, info.label);
            if (!inserted) {
                std::printf("    [%s] %s: \"%s\" e \"%s\"\n",
                            keymap_label(keymap), chord_label(chord).c_str(),
                            it->second, info.label);
            }
            OTTER_CHECK(inserted);
        };

        for (const CommandInfo& info : all_commands()) {
            if (info.state == CommandState::out_of_scope ||
                info.state == CommandState::missing) {
                continue;
            }
            for (const ImGuiKeyChord chord : command_keys(info.id, keymap)) {
                switch (info.context) {
                    case CommandContext::navigator:
                        claim(navigator, chord, info);
                        break;
                    case CommandContext::editor:
                        claim(editor_and_global, chord, info);
                        break;
                    case CommandContext::grid:
                        claim(grid_and_global, chord, info);
                        break;
                    case CommandContext::global:
                        claim(editor_and_global, chord, info);
                        claim(grid_and_global, chord, info);
                        break;
                }
            }
        }
    }
}

OTTER_TEST(commands_otter_keymap_only_takes_widget_keys_on_purpose) {
    // O perfil C-Otter mantem as teclas do editor: Ctrl+D seleciona a proxima
    // ocorrencia, Alt+setas move a linha, Ctrl+[ e Ctrl+] recuam. Uma tecla
    // de comando em cima de uma delas TOMA a tecla do editor -- e so' pode
    // acontecer nas quatro abaixo, por decisao.
    const ImGuiKeyChord kOnPurpose[] = {
        C | ImGuiKey_Enter,    // executar, e nao "inserir linha abaixo"
        CS | ImGuiKey_F,       // formatar, e nao "localizar todos"
        C | ImGuiKey_L,        // ir para a linha (Ctrl+/ ja' comenta)
        C | ImGuiKey_Slash,    // o mesmo comando do editor
        CS | ImGuiKey_K,       // o mesmo comando do editor (apagar a linha)
    };

    for (const CommandInfo& info : all_commands()) {
        // Com a grade em foco o editor nao le teclado: Ctrl+C la' copia
        // celulas, e nao toma nada dele.
        if (info.context == CommandContext::grid) continue;

        for (const ImGuiKeyChord chord : command_keys(info.id, Keymap::otter)) {
            for (const WidgetKey& widget : widget_keys()) {
                if (widget.chord != chord) continue;

                const bool allowed =
                    std::find(std::begin(kOnPurpose), std::end(kOnPurpose), chord) !=
                    std::end(kOnPurpose);
                if (!allowed) {
                    std::printf("    \"%s\" toma %s do editor (%s)\n", info.label,
                                chord_label(chord).c_str(), widget.action);
                }
                OTTER_CHECK(allowed);
            }
        }
    }
}

OTTER_TEST(commands_keymaps_differ_where_the_editors_differ) {
    // O ponto dos perfis: a mesma tecla faz coisas diferentes em cada um.
    const CommandInfo& delete_line = command_info(Command::delete_line);
    OTTER_CHECK(has_key(delete_line, Keymap::dbeaver, C | ImGuiKey_D));
    OTTER_CHECK(!has_key(delete_line, Keymap::otter, C | ImGuiKey_D));

    const CommandInfo& new_script = command_info(Command::new_script);
    OTTER_CHECK(has_key(new_script, Keymap::otter, C | ImGuiKey_T));
    OTTER_CHECK(!has_key(new_script, Keymap::dbeaver, C | ImGuiKey_T));

    // No DBeaver, Alt+setas navega entre consultas; no C-Otter move a linha
    // (tecla do editor), e a navegacao fica em Ctrl+Alt+setas.
    const CommandInfo& next = command_info(Command::query_next);
    OTTER_CHECK(has_key(next, Keymap::dbeaver, A | ImGuiKey_DownArrow));
    OTTER_CHECK(has_key(next, Keymap::otter, CA | ImGuiKey_DownArrow));
}

OTTER_TEST(commands_partial_and_missing_ones_say_why) {
    // O texto vai para o tooltip do botao e do item de menu. Sem ele, um
    // comando parcial pareceria completo, e um desabilitado, defeito.
    for (const CommandInfo& info : all_commands()) {
        const bool needs_note = info.state != CommandState::done;
        if (needs_note && info.note[0] == '\0') {
            std::printf("    \"%s\" sem explicacao\n", info.label);
        }
        OTTER_CHECK(!needs_note || info.note[0] != '\0');
        OTTER_CHECK(info.label[0] != '\0');
    }
}

OTTER_TEST(commands_chord_label_is_readable) {
    OTTER_CHECK_EQ(chord_label(CS | ImGuiKey_E), std::string{"Ctrl+Shift+E"});
    OTTER_CHECK_EQ(chord_label(C | ImGuiKey_Backslash), std::string{"Ctrl+\\"});
    OTTER_CHECK_EQ(chord_label(A | ImGuiKey_DownArrow), std::string{"Alt+Down"});
    OTTER_CHECK_EQ(chord_label(CAS | ImGuiKey_X), std::string{"Ctrl+Alt+Shift+X"});
    OTTER_CHECK(chord_label(0).empty());

    OTTER_CHECK_EQ(command_shortcut(Command::run_statement, Keymap::dbeaver),
                   std::string{"Ctrl+Enter"});
    OTTER_CHECK(command_shortcut(Command::run_count, Keymap::dbeaver).empty());
}

OTTER_TEST(commands_keymap_ids_round_trip) {
    OTTER_CHECK(keymap_from_id(keymap_id(Keymap::dbeaver)) == Keymap::dbeaver);
    OTTER_CHECK(keymap_from_id(keymap_id(Keymap::otter)) == Keymap::otter);
    // Id desconhecido (arquivo de uma versao futura) cai no padrao.
    OTTER_CHECK(keymap_from_id("vim") == Keymap::dbeaver);
}

OTTER_TEST(app_settings_round_trip_and_survive_a_missing_file) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "otter-test-settings";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const std::string path = (dir / "settings.json").string();

    // Arquivo ausente: os padroes, sem erro.
    const AppSettings defaults = load_app_settings(path);
    OTTER_CHECK_EQ(defaults.keymap, std::string{"dbeaver"});
    OTTER_CHECK_EQ(defaults.icons, std::string{"dbeaver"});
    OTTER_CHECK_EQ(defaults.theme, std::string{"amber"});
    // Ler nao cria nada.
    OTTER_CHECK(!fs::exists(path));

    AppSettings wanted;
    wanted.keymap       = "otter";
    wanted.theme        = "light";
    wanted.language     = "pt-BR";
    wanted.side_by_side = true;
    OTTER_CHECK(save_app_settings(path, wanted));

    const AppSettings back = load_app_settings(path);
    OTTER_CHECK_EQ(back.keymap, std::string{"otter"});
    OTTER_CHECK_EQ(back.theme, std::string{"light"});
    OTTER_CHECK_EQ(back.language, std::string{"pt-BR"});
    OTTER_CHECK(back.side_by_side);

    fs::remove_all(dir, ec);
}

OTTER_TEST(app_settings_missing_file_is_created_with_the_defaults) {
    // Pedido do usuario: sem config, cria a pasta e grava os padroes, com o
    // tema ambar.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "otter-test-settings-ensure";
    std::error_code ec;
    fs::remove_all(dir, ec);

    const std::string path = (dir / ".C-Otter" / "settings.json").string();
    const AppSettings created = ensure_app_settings(path);
    OTTER_CHECK_EQ(created.theme, std::string{"amber"});
    OTTER_CHECK(fs::exists(path));

    // O arquivo traz TODAS as opcoes, nao so' as que fogem do padrao.
    const AppSettings back = load_app_settings(path);
    OTTER_CHECK_EQ(back.keymap, std::string{"dbeaver"});
    OTTER_CHECK_EQ(back.icons, std::string{"dbeaver"});
    OTTER_CHECK_EQ(back.theme, std::string{"amber"});
    OTTER_CHECK(back.language.empty());
    OTTER_CHECK(!back.side_by_side);

    // Um arquivo que ja' existe nao e' regravado com os padroes.
    AppSettings chosen = back;
    chosen.theme = "light";
    OTTER_CHECK(save_app_settings(path, chosen));
    OTTER_CHECK_EQ(ensure_app_settings(path).theme, std::string{"light"});

    fs::remove_all(dir, ec);
}

OTTER_TEST(app_settings_live_next_to_the_executable) {
    namespace fs = std::filesystem;
    const std::string directory = otter::executable_directory();
    OTTER_CHECK(!directory.empty());

    const fs::path path = app_settings_path();
    OTTER_CHECK_EQ(path.filename().string(), std::string{"settings.json"});
    OTTER_CHECK_EQ(path.parent_path().filename().string(), std::string{".C-Otter"});
    OTTER_CHECK(fs::equivalent(path.parent_path().parent_path(), directory));
}

OTTER_TEST(commands_grid_has_every_dbeaver_resultset_binding) {
    // Os <key> de plugins/org.jkiss.dbeaver.ui.editors.data/plugin.xml
    // (sem plataforma, ou win32). E' o denominador da grade (diretiva 4).
    constexpr ImGuiKeyChord AS = ImGuiMod_Alt | ImGuiMod_Shift;
    constexpr ImGuiKeyChord S  = ImGuiMod_Shift;
    const Expected kGridKeys[] = {
        {"core.resultset.applyChanges", C | ImGuiKey_S},
        {"core.resultset.cell.reset", ImGuiKey_Escape},
        {"core.resultset.cell.save", CAS | ImGuiKey_Enter},
        {"core.resultset.cell.setDefault", C | ImGuiKey_Backspace},
        {"core.resultset.fetch.all", CS | ImGuiKey_Equal},
        {"core.resultset.fetch.page", CA | ImGuiKey_N},
        {"core.resultset.filterMenu", ImGuiKey_F11},
        {"core.resultset.filterMenu.distinct", C | ImGuiKey_F11},
        {"core.resultset.filterSettings", AS | ImGuiKey_F},
        {"core.resultset.focus.filter", CAS | ImGuiKey_T},
        {"core.resultset.grid.activatePreview", CS | ImGuiKey_7},
        {"core.resultset.grid.copyColumnNames", AS | ImGuiKey_C},
        {"core.resultset.grid.gotoColumn", CS | ImGuiKey_G},
        {"core.resultset.grid.gotoRow", C | ImGuiKey_G},
        {"core.resultset.grid.hideColumns", AS | ImGuiKey_H},
        {"core.resultset.grid.moveColumnLeft", AS | ImGuiKey_LeftArrow},
        {"core.resultset.grid.moveColumnRight", AS | ImGuiKey_RightArrow},
        {"core.resultset.grid.selectColumn", CA | ImGuiKey_C},
        {"core.resultset.grid.selectRow", CA | ImGuiKey_R},
        {"core.resultset.grid.showColumnContextMenu", S | ImGuiKey_F11},
        {"core.resultset.grid.showColumns", AS | ImGuiKey_T},
        {"core.resultset.grid.togglePanel", CA | ImGuiKey_F2},
        {"core.resultset.grid.togglePanel", CA | ImGuiKey_F3},
        {"core.resultset.grid.togglePanel", CA | ImGuiKey_F4},
        {"core.resultset.grid.togglePanel", CA | ImGuiKey_F5},
        {"core.resultset.grid.togglePanel", CA | ImGuiKey_F6},
        {"core.resultset.grid.togglePreview", C | ImGuiKey_7},
        {"core.resultset.grid.togglePreview", ImGuiKey_F7},
        {"core.resultset.navigateLink", A | ImGuiKey_Space},
        {"core.resultset.referencesMenu", CS | ImGuiKey_1},
        {"core.resultset.rejectChanges", C | ImGuiKey_R},
        {"core.resultset.row.add", A | ImGuiKey_Insert},
        {"core.resultset.row.add.before", AS | ImGuiKey_Insert},
        {"core.resultset.row.copy", CA | ImGuiKey_Insert},
        {"core.resultset.row.copy.before", CAS | ImGuiKey_Insert},
        {"core.resultset.row.copy.from.above", C | ImGuiKey_D},
        {"core.resultset.row.copy.from.below", CA | ImGuiKey_D},
        {"core.resultset.row.delete", A | ImGuiKey_Delete},
        {"core.resultset.row.edit", S | ImGuiKey_Enter},
        {"core.resultset.row.edit.inline", ImGuiKey_Enter},
        {"core.resultset.row.first", CAS | ImGuiKey_LeftArrow},
        {"core.resultset.row.last", CAS | ImGuiKey_RightArrow},
        {"core.resultset.row.next", CA | ImGuiKey_RightArrow},
        {"core.resultset.row.previous", CA | ImGuiKey_LeftArrow},
        {"core.resultset.switchPresentation", C | ImGuiKey_GraveAccent},
        {"core.resultset.toggleMode", ImGuiKey_Tab},
        {"core.resultset.toggleOrder", C | ImGuiKey_2},
        {"core.resultset.zoomIn", A | ImGuiKey_0},
        {"core.resultset.zoomOut", A | ImGuiKey_9},
    };

    for (const Expected& expected : kGridKeys) {
        // `togglePanel` e' um comando com parametro: a tecla pode estar em
        // qualquer das linhas com esse id.
        bool found = false;
        for (const CommandInfo& info : all_commands()) {
            if (info.dbeaver_id != expected.dbeaver_id) continue;
            found |= has_key(info, Keymap::dbeaver, expected.chord);
        }
        if (!found) {
            std::printf("    %s sem a tecla %s\n", expected.dbeaver_id,
                        chord_label(expected.chord).c_str());
        }
        OTTER_CHECK(found);
    }
}

OTTER_TEST(commands_labels_are_unique) {
    // O rotulo identifica o comando para quem le o menu -- e para o arquivo
    // de comandos da conferencia automatizada (OTTER_COMMAND_FILE).
    std::map<std::string, const char*> seen;
    for (const CommandInfo& info : all_commands()) {
        const auto [it, inserted] = seen.emplace(info.label, info.label);
        if (!inserted) std::printf("    rotulo repetido: \"%s\"\n", info.label);
        OTTER_CHECK(inserted);
    }
}
