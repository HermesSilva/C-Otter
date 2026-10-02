// C-Otter -- ui/commands.hpp
//
// Registro unico dos comandos do editor SQL: o id do comando no DBeaver, o
// rotulo, a tecla em cada PERFIL de atalhos, o icone e o estado.
//
// Existe porque o mesmo comando aparece em quatro lugares -- barra lateral,
// menu "SQL Editor", menu de contexto e teclado --, e cada um tinha a sua
// copia do rotulo e da tecla. O menu anunciava Ctrl+Enter, o teclado
// decidia outra coisa, e nada garantia que os dois concordassem. Aqui ha'
// uma linha por comando, e os quatro lugares leem dela.
//
// PERFIS DE ATALHO (pedido do usuario, 2026-09-30):
//
//   "Para teclas de atalhos, crie perfis, DBeaver e C-Otter, desta forma o
//    usuario pode escolher qual perfil vai usar. Este projeto pode e deve
//    evoluir o DBeaver, nao regredir."
//
//   dbeaver -- as teclas do plugin.xml de org.jkiss.dbeaver.ui.editors.sql,
//              mais as do editor de texto do Eclipse que quem vem de la'
//              tem na mao (Ctrl+D apaga a linha, Ctrl+L vai para a linha).
//   otter   -- as do C-Otter: onde nao ha' conflito, as mesmas; onde ha', as
//              de um editor de codigo atual (Ctrl+D seleciona a proxima
//              ocorrencia, Alt+setas move a linha, Ctrl+T abre aba).
//
// Um comando SEM tecla num perfil continua nos menus e na barra.
#pragma once

#include "ui/icons.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace otter::ui {

enum class Command : std::uint16_t {
    // --- Execucao -------------------------------------------------------------
    run_statement,
    run_statement_new,
    run_script,
    run_script_from_position,
    run_script_new,
    run_script_native,
    cancel_query,
    run_count,
    run_all_rows,
    run_expression,
    explain,
    load_plan,
    export_data,

    // --- Navegacao ------------------------------------------------------------
    query_next,
    query_prev,
    goto_bracket,
    select_to_bracket,
    navigate_object,
    goto_line,

    // --- Agrupamentos (folding) -----------------------------------------------
    foldings_enabled,
    expand_all_foldings,
    collapse_all_foldings,

    // --- Paineis e disposicao -------------------------------------------------
    show_output,
    show_log,
    show_variables,
    show_outline,
    show_terminal,
    toggle_extra_panels,
    multiple_results,
    toggle_result_panel,
    maximize_result_panel,
    switch_panel,
    toggle_layout,
    switch_presentation,

    // --- Edicao ---------------------------------------------------------------
    comment_single,
    comment_block,
    word_wrap,
    format,
    morph_delimited,
    trim_spaces,
    trim_leading,
    trim_trailing,
    to_upper,
    to_lower,
    delete_line,
    templates,
    copy_query,
    search_web,
    disable_syntax,

    // --- Script e arquivo -----------------------------------------------------
    new_script,
    open_script,
    recent_script,
    console,
    import_script,
    export_script,
    rename_script,
    delete_script,

    // --- Contexto -------------------------------------------------------------
    sync_connection,
    sync_auto,
    refresh_current_schema,
    refresh_all_schemas,

    // --- Abas de resultado ----------------------------------------------------
    close_result_tab,
    pin_result_tab,
    export_result,
    ddl_by_result,

    ai_assistant,

    // --- Grade de resultados (`core.resultset.*`, 63 comandos no DBeaver) ----
    grid_apply,
    grid_apply_commit,
    grid_reject,
    grid_confirm_save,
    grid_cell_save,
    grid_cell_reset,
    grid_set_null,
    grid_set_default,
    grid_row_edit_inline,
    grid_row_edit,
    grid_row_add,
    grid_row_add_before,
    grid_row_copy,
    grid_row_copy_before,
    grid_copy_above,
    grid_copy_below,
    grid_row_delete,
    grid_row_first,
    grid_row_previous,
    grid_row_next,
    grid_row_last,
    grid_goto_row,
    grid_goto_column,
    grid_navigate_link,
    grid_references,
    grid_select_row,
    grid_select_column,
    grid_select_all,
    grid_copy,
    grid_paste,
    grid_copy_as,
    grid_copy_column_names,
    grid_copy_row_names,
    grid_move_left,
    grid_move_right,
    grid_hide_columns,
    grid_show_columns,
    grid_hide_empty,
    grid_fit_values,
    grid_fit_screen,
    grid_column_menu,
    grid_row_color,
    grid_filter_menu,
    grid_filter_distinct,
    grid_filter_settings,
    grid_focus_filter,
    grid_filter_clear,
    grid_filter_reset,
    grid_filter_save,
    grid_toggle_order,
    grid_fetch_page,
    grid_fetch_all,
    grid_count,
    grid_export,
    grid_open_with,
    grid_generate_script,
    grid_toggle_mode,
    grid_switch_presentation,
    grid_toggle_preview,
    grid_activate_preview,
    grid_panel_value,
    grid_panel_references,
    grid_panel_metadata,
    grid_panel_grouping,
    grid_panel_calc,
    grid_panel_maximize,
    grid_toggle_layout,
    grid_switch_viewer,
    grid_zoom_in,
    grid_zoom_out,

    // --- Aplicacao, banco e navegador (`core.*`, `ui.navigator`) ---------------
    //
    // Os comandos do menu Database, do menu de contexto da arvore e da barra
    // de ferramentas do DBeaver. Acoes em ui/app_commands.cpp.
    app_commit,
    app_rollback,
    app_auto_commit,
    app_txn_pending,
    app_txn_log,
    app_connect,
    app_disconnect,
    app_disconnect_all,
    app_disconnect_others,
    app_reconnect,
    app_read_only,
    app_new_connection,
    app_new_connection_url,
    app_new_folder,
    app_select_connection,
    app_select_schema,
    app_set_default,
    app_goto_object,
    app_object_open,
    app_object_create,
    app_object_delete,
    app_object_rename,
    app_view_data,
    app_read_data_console,
    app_tools_menu,
    app_export_data,
    app_import_data,
    app_filter_toggle,
    app_filter_config,
    app_filter_clear,
    app_filter_include,
    app_filter_exclude,
    app_filter_connected,
    app_filter_focus,
    app_link_editor,
    app_move_up,
    app_move_down,
    app_move_top,
    app_move_bottom,
    app_bookmark_add,
    app_bookmark_navigate,
    app_props_next,
    app_props_previous,
    app_source_tab,
    app_column_index,
    app_column_constraint,
    app_procedure_execute,
    app_copy_special,
    app_copy_special_last,
    app_paste_special,
    app_generate_uuid,
    app_load_resource,
    app_save_resource,
    app_open_spreadsheet,
    app_script_associate,
    app_show_scripts,
    app_show_in_explorer,
    app_change_password,
    app_driver_manager,
    app_preferences,
    app_view_toggle,
    app_log_clear,
    app_log_filter,
    app_process_stop,
    app_clear_history,
    app_reset_settings,
    app_collect_diagnostics,
    app_dashboard_open,
    app_dashboard_add,
    app_dashboard_remove,
    app_dashboard_catalog,
    app_dashboard_configure,
    app_dashboard_refresh,
    app_dashboard_view,
    app_dashboard_reset,
    app_command_palette,
    app_help,
    app_exit,

    // --- Editor de texto do Eclipse que o editor SQL herda ---------------------
    edit_move_lines_up,
    edit_move_lines_down,
    edit_join_lines,
    edit_word_completion,
    edit_open_local_file,

    count
};

inline constexpr std::size_t kCommandCount = static_cast<std::size_t>(Command::count);

// O comando e' da grade de resultado? Inclui os dois de contexto global
// (buscar a proxima pagina, buscar tudo), que agem sobre ela.
[[nodiscard]] constexpr bool is_grid_command(Command command) noexcept {
    return command >= Command::grid_apply && command < Command::app_commit;
}

// O comando e' da aplicacao, do banco ou do navegador (ui/app_commands.cpp)?
[[nodiscard]] constexpr bool is_app_command(Command command) noexcept {
    return command >= Command::app_commit && command < Command::edit_move_lines_up;
}

// Onde a tecla do comando vale.
enum class CommandContext : std::uint8_t {
    editor,      // com o editor de script em foco ("ui.editors.sql.script.focused")
    navigator,   // com a arvore em foco ("ui.context.navigator")
    global,      // em qualquer lugar da janela
    grid,        // com a grade de resultado em foco ("ui.context.resultset")
};

enum class CommandState : std::uint8_t {
    done,           // faz o que o DBeaver faz
    partial,        // faz, com a diferenca dita em `note`
    missing,        // ainda nao; aparece desabilitado, com `note` no tooltip
    out_of_scope,   // decisao registrada de nao fazer; nao aparece
};

enum class Keymap : std::uint8_t { dbeaver, otter };

inline constexpr std::size_t kKeymapCount = 2;

struct CommandInfo {
    Command          id;
    // Id no plugin.xml do DBeaver, sem o prefixo "org.jkiss.dbeaver.". Vazio
    // quando o comando e' do editor de texto do Eclipse ou do C-Otter.
    std::string_view dbeaver_id;
    // Rotulo em ingles -- o `name` do bundle.properties, e a chave de TR().
    const char*      label;
    CommandContext   context;
    // Ate' tres teclas por perfil; 0 = nenhuma.
    std::array<ImGuiKeyChord, 3> dbeaver_keys;
    std::array<ImGuiKeyChord, 3> otter_keys;
    Icon             icon;
    CommandState     state;
    // O que difere do DBeaver, ou por que nao foi feito. Chave de TR().
    const char*      note;
};

// Todos os comandos, na ordem do enum.
[[nodiscard]] std::span<const CommandInfo> all_commands();
[[nodiscard]] const CommandInfo& command_info(Command command);

// As teclas do comando no perfil (so' as que existem).
[[nodiscard]] std::span<const ImGuiKeyChord> command_keys(Command command,
                                                          Keymap keymap);

// "Ctrl+Shift+E". Vazio para 0.
[[nodiscard]] std::string chord_label(ImGuiKeyChord chord);
// A primeira tecla do comando no perfil, para o menu. Vazio se nao tem.
[[nodiscard]] std::string command_shortcut(Command command, Keymap keymap);

[[nodiscard]] std::string_view keymap_id(Keymap keymap) noexcept;       // "dbeaver"
[[nodiscard]] const char*      keymap_label(Keymap keymap) noexcept;    // "DBeaver"
[[nodiscard]] Keymap           keymap_from_id(std::string_view id) noexcept;

// Teclas que o WIDGET do editor trata sozinho, e o que ele faz com elas. No
// perfil C-Otter sao o comportamento; no DBeaver, as que colidem com uma
// tecla de comando sao tomadas pelo comando.
//
// Serve ao teste: nenhuma tecla de comando do perfil C-Otter pode cair em
// cima de uma destas sem querer.
struct WidgetKey {
    ImGuiKeyChord chord;
    const char*   action;
};
[[nodiscard]] std::span<const WidgetKey> widget_keys();

} // namespace otter::ui
