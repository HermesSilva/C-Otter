#include "ui/commands.hpp"

#include <string>

namespace otter::ui {
namespace {

// Atalhos para a tabela ficar legivel.
constexpr ImGuiKeyChord C   = ImGuiMod_Ctrl;
constexpr ImGuiKeyChord S   = ImGuiMod_Shift;
constexpr ImGuiKeyChord A   = ImGuiMod_Alt;
constexpr ImGuiKeyChord CS  = ImGuiMod_Ctrl | ImGuiMod_Shift;
constexpr ImGuiKeyChord CA  = ImGuiMod_Ctrl | ImGuiMod_Alt;
constexpr ImGuiKeyChord CAS = ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiMod_Shift;
constexpr ImGuiKeyChord AS  = ImGuiMod_Alt | ImGuiMod_Shift;

using Keys = std::array<ImGuiKeyChord, 3>;

constexpr Keys none{0, 0, 0};
constexpr Keys one(ImGuiKeyChord a) { return {a, 0, 0}; }
constexpr Keys two(ImGuiKeyChord a, ImGuiKeyChord b) { return {a, b, 0}; }
constexpr Keys three(ImGuiKeyChord a, ImGuiKeyChord b, ImGuiKeyChord c) {
    return {a, b, c};
}

constexpr CommandContext ed  = CommandContext::editor;
constexpr CommandContext nav = CommandContext::navigator;
constexpr CommandContext gl  = CommandContext::global;
constexpr CommandContext gr  = CommandContext::grid;

constexpr CommandState ok   = CommandState::done;
constexpr CommandState part = CommandState::partial;
constexpr CommandState todo = CommandState::missing;
constexpr CommandState out  = CommandState::out_of_scope;

// Uma linha por comando. Rotulos e teclas DBeaver extraidos de
// plugins/org.jkiss.dbeaver.ui.editors.sql (plugin.xml e bundle.properties);
// os tres do editor de texto do Eclipse estao marcados.
//
// A ORDEM e' a do enum -- command_info() indexa por ele, e o teste confere.
constexpr CommandInfo kCommands[] = {
    // --- Execucao -------------------------------------------------------------
    {Command::run_statement, "ui.editors.sql.run.statement", "Execute SQL query", ed,
     one(C | ImGuiKey_Enter), one(C | ImGuiKey_Enter), Icon::play, ok, ""},
    {Command::run_statement_new, "ui.editors.sql.run.statementNew",
     "Execute SQL in new tab", ed,
     one(C | ImGuiKey_Backslash), two(C | ImGuiKey_Backslash, CS | ImGuiKey_Enter),
     Icon::play_new, ok, ""},
    {Command::run_script, "ui.editors.sql.run.script", "Execute SQL script", ed,
     one(A | ImGuiKey_X), one(A | ImGuiKey_X), Icon::play_script, part,
     "Shows the result of the last query; DBeaver opens one result tab per query"},
    {Command::run_script_from_position, "ui.editors.sql.run.scriptFromPosition",
     "Execute SQL script from the position", ed,
     one(A | ImGuiKey_P), one(A | ImGuiKey_P), Icon::play_script, ok, ""},
    {Command::run_script_new, "ui.editors.sql.run.scriptNew",
     "Execute queries in separate tabs", ed,
     one(CAS | ImGuiKey_X), one(CAS | ImGuiKey_X), Icon::play_script, part,
     "Runs the queries one after another; DBeaver opens a connection per query"},
    {Command::run_script_native, "core.sql.script.run.scriptNative",
     "Execute SQL script natively", ed, one(A | ImGuiKey_N), none, Icon::play_script,
     part, "Runs the script through the psql or mysql client of the system"},
    {Command::cancel_query, "ui.editors.sql.cancel.query", "Cancel active query", ed,
     none, none, Icon::stop, ok, ""},
    {Command::run_count, "ui.editors.sql.run.count", "Select row count", ed,
     none, none, Icon::info, ok, ""},
    {Command::run_all_rows, "ui.editors.sql.run.all.rows", "Select all rows", ed,
     one(CAS | ImGuiKey_A), one(CAS | ImGuiKey_A), Icon::info, ok, ""},
    {Command::run_expression, "ui.editors.sql.run.expression",
     "Evaluate SQL expression", ed,
     one(CA | ImGuiKey_Apostrophe), one(CA | ImGuiKey_Apostrophe), Icon::info, ok, ""},
    {Command::explain, "ui.editors.sql.run.explain", "Explain Execution Plan", ed,
     one(CS | ImGuiKey_E), one(CS | ImGuiKey_E), Icon::plan, ok, ""},
    {Command::load_plan, "ui.editors.sql.load.plan", "Load Execution Plan", ed,
     none, none, Icon::plan, part,
     "Loads a plan saved as EXPLAIN (FORMAT JSON); the DBeaver .dbplan format is not read"},
    {Command::export_data, "ui.editors.sql.export.data", "Export from Query", ed,
     none, none, Icon::save, part,
     "Runs the query without a row limit and opens the export window; DBeaver streams to the file"},

    // --- Navegacao ------------------------------------------------------------
    {Command::query_next, "ui.editors.sql.query.next", "Next query", ed,
     one(A | ImGuiKey_DownArrow), one(CA | ImGuiKey_DownArrow), Icon::chevron_down,
     ok, ""},
    {Command::query_prev, "ui.editors.sql.query.prev", "Previous query", ed,
     one(A | ImGuiKey_UpArrow), one(CA | ImGuiKey_UpArrow), Icon::chevron_down,
     ok, ""},
    {Command::goto_bracket, "ui.editors.sql.gotoMatchingBracket",
     "Go to matching bracket", ed,
     one(CS | ImGuiKey_LeftBracket), one(CS | ImGuiKey_LeftBracket), Icon::info, ok, ""},
    {Command::select_to_bracket, "ui.editors.sql.selectToMatchingBracket",
     "Select to the matching bracket", ed,
     one(CS | ImGuiKey_RightBracket), one(CS | ImGuiKey_RightBracket), Icon::info,
     ok, ""},
    {Command::navigate_object, "ui.editors.sql.navigate.object", "Open Declaration", ed,
     one(ImGuiKey_F4), two(ImGuiKey_F4, ImGuiKey_F12), Icon::search, part,
     "Reveals the object in the navigator tree; DBeaver opens its property editor"},
    // Editor de texto do Eclipse (ITextEditorActionDefinitionIds.LINE_GOTO).
    {Command::goto_line, "", "Go to Line...", ed,
     one(C | ImGuiKey_L), one(C | ImGuiKey_L), Icon::info, ok, ""},

    // --- Agrupamentos ---------------------------------------------------------
    {Command::foldings_enabled, "ui.editors.sql.FoldingsEnabled", "Foldings Enabled",
     ed, none, none, Icon::info, ok, ""},
    {Command::expand_all_foldings, "ui.editors.sql.ExpandAllFoldings",
     "Expand All Foldings", ed, none, none, Icon::info, ok, ""},
    {Command::collapse_all_foldings, "ui.editors.sql.CollapseAllFoldings",
     "Collapse All Foldings", ed, none, none, Icon::info, ok, ""},

    // --- Paineis e disposicao -------------------------------------------------
    {Command::show_output, "ui.editors.sql.show.output", "Show server output", gl,
     one(CS | ImGuiKey_O), one(CS | ImGuiKey_O), Icon::server_output, part,
     "PostgreSQL notices; MySQL warnings are not collected yet"},
    {Command::show_log, "ui.editors.sql.show.log", "Show execution log", gl,
     none, none, Icon::exec_log, ok, ""},
    {Command::show_variables, "ui.editors.sql.show.variables", "Show SQL variables", gl,
     none, none, Icon::variables, ok, ""},
    {Command::show_outline, "ui.editors.sql.show.outline", "Toggle outline", gl,
     none, none, Icon::outline, ok, ""},
    {Command::show_terminal, "ui.editors.sql.show.terminalView", "SQL Terminal", gl,
     none, none, Icon::terminal, ok, ""},
    {Command::toggle_extra_panels, "ui.editors.sql.toggle.extraPanels",
     "Show panels in result tabs", gl, none, none, Icon::info, out,
     "The panels are dockable windows: dragging one onto the results does this"},
    {Command::multiple_results, "ui.editors.sql.multipleResultsPerTab",
     "Show multiple results in a single tab", gl, none, none, Icon::info, todo,
     "The grid shows one result per tab"},
    {Command::toggle_result_panel, "ui.editors.sql.toggle.result.panel",
     "Toggle results panel", gl,
     one(C | ImGuiKey_T), one(C | ImGuiKey_J), Icon::info, ok, ""},
    {Command::maximize_result_panel, "ui.editors.sql.maximize.result.panel",
     "Maximize results panel", gl,
     one(CS | ImGuiKey_T), one(CS | ImGuiKey_T), Icon::info, ok, ""},
    {Command::switch_panel, "ui.editors.sql.switch.panel", "Switch active panel", gl,
     one(A | ImGuiKey_T), one(A | ImGuiKey_T), Icon::info, ok, ""},
    {Command::toggle_layout, "ui.editors.sql.toggleLayout", "Toggle editor layout", gl,
     none, none, Icon::info, ok, ""},
    {Command::switch_presentation, "ui.editors.sql.switch.presentation",
     "Switch presentation to", gl, none, none, Icon::info, out,
     "Presentations are plugin extensions (visual query builder); none exists here"},

    // --- Edicao ---------------------------------------------------------------
    {Command::comment_single, "ui.editors.sql.comment.single", "Toggle Line Comment", ed,
     one(C | ImGuiKey_Slash), one(C | ImGuiKey_Slash), Icon::info, ok, ""},
    {Command::comment_block, "ui.editors.sql.comment.multi", "Toggle Block Comment", ed,
     one(CS | ImGuiKey_Slash), one(CS | ImGuiKey_Slash), Icon::info, ok, ""},
    {Command::word_wrap, "ui.editors.sql.word.wrap", "Toggle Word Wrap", ed,
     one(CAS | ImGuiKey_W), two(CAS | ImGuiKey_W, A | ImGuiKey_Z), Icon::info, ok, ""},
    {Command::format, "ui.editors.text.content.format", "Content Format", gl,
     one(CS | ImGuiKey_F), one(CS | ImGuiKey_F), Icon::info, ok, ""},
    {Command::morph_delimited, "ui.editors.sql.morph.delimited.list",
     "Morph to delimited list", ed, none, none, Icon::info, ok, ""},
    {Command::trim_spaces, "ui.editors.sql.trim.spaces", "Trim spaces", ed,
     none, none, Icon::info, ok, ""},
    {Command::trim_leading, "ui.editors.sql.trim.leading.spaces",
     "Trim leading spaces", ed, none, none, Icon::info, ok, ""},
    {Command::trim_trailing, "ui.editors.sql.trim.trailing.spaces",
     "Trim trailing spaces", ed, none, none, Icon::info, ok, ""},
    // Editor de texto do Eclipse: Ctrl+Shift+X / Ctrl+Shift+Y.
    {Command::to_upper, "", "To Upper Case", ed,
     one(CS | ImGuiKey_X), one(CS | ImGuiKey_U), Icon::info, ok, ""},
    {Command::to_lower, "", "To Lower Case", ed,
     one(CS | ImGuiKey_Y), one(CS | ImGuiKey_L), Icon::info, ok, ""},
    // Eclipse: Ctrl+D apaga a linha. No perfil C-Otter Ctrl+D e' do widget
    // (seleciona a proxima ocorrencia), e apagar a linha e' Ctrl+Shift+K.
    {Command::delete_line, "", "Delete Line", ed,
     one(C | ImGuiKey_D), one(CS | ImGuiKey_K), Icon::info, ok, ""},
    {Command::templates, "ui.editors.sql.assist.templates", "Complete template name",
     ed, none, one(CA | ImGuiKey_Space), Icon::info, part,
     "The five default templates; template variables are plain placeholders"},
    {Command::copy_query, "ui.editors.sql.copy.query", "Copy selected query", ed,
     none, none, Icon::copy, ok, ""},
    {Command::search_web, "ui.editors.sql.search.web", "Search in web", ed,
     none, none, Icon::search, ok, ""},
    {Command::disable_syntax, "ui.editors.sql.disableSQLSyntaxParser",
     "Disable SQL syntax parser", ed, none, none, Icon::info, ok, ""},

    // --- Script e arquivo -----------------------------------------------------
    {Command::new_script, "core.sql.editor.create", "New SQL script", gl,
     two(C | ImGuiKey_RightBracket, C | ImGuiKey_F3), one(C | ImGuiKey_T),
     Icon::plus, ok, ""},
    {Command::open_script, "core.sql.editor.open", "Open SQL script", gl,
     three(ImGuiKey_F3, C | ImGuiKey_LeftBracket, C | ImGuiKey_O), one(C | ImGuiKey_O),
     Icon::open, part,
     "Opens a file; DBeaver lists the scripts saved in the project"},
    {Command::recent_script, "core.sql.editor.recent", "Last edited SQL script", nav,
     one(C | ImGuiKey_Enter), one(C | ImGuiKey_Enter), Icon::open, ok, ""},
    {Command::console, "core.sql.editor.console", "Open SQL console", nav,
     one(CA | ImGuiKey_Enter), one(CA | ImGuiKey_Enter), Icon::terminal, ok, ""},
    {Command::import_script, "ui.editors.sql.open.file", "Import SQL script", ed,
     one(CAS | ImGuiKey_O), one(CAS | ImGuiKey_O), Icon::open, ok, ""},
    {Command::export_script, "ui.editors.sql.save.file", "Export SQL script", ed,
     none, none, Icon::save, ok, ""},
    {Command::rename_script, "ui.editors.sql.rename", "Rename SQL Script", ed,
     one(C | ImGuiKey_F2), two(C | ImGuiKey_F2, ImGuiKey_F2), Icon::info, ok, ""},
    {Command::delete_script, "ui.editors.sql.deleteThisScript", "Delete this script",
     ed, none, none, Icon::close, ok, ""},

    // --- Contexto -------------------------------------------------------------
    {Command::sync_connection, "ui.editors.sql.sync.connection",
     "Set connection from navigator", gl,
     one(CS | ImGuiKey_Period), one(CS | ImGuiKey_Period), Icon::connect, ok, ""},
    {Command::sync_auto, "ui.editors.sql.sync.auto",
     "Auto-sync connection with navigator", gl, none, none, Icon::connect, ok, ""},
    {Command::refresh_current_schema, "ui.editors.sql.refresh.current.schema",
     "Refresh current schema", ed, none, none, Icon::refresh, part,
     "Reloads the whole catalog of the connection, not only one schema"},
    {Command::refresh_all_schemas, "ui.editors.sql.refresh.all.schemas",
     "Refresh all schemas", ed, none, none, Icon::refresh, ok, ""},

    // --- Abas de resultado ----------------------------------------------------
    {Command::close_result_tab, "ui.editors.sql.close.tab", "Close", gl,
     one(CS | ImGuiKey_Backslash), one(CS | ImGuiKey_Backslash), Icon::close, ok, ""},
    {Command::pin_result_tab, "ui.editors.sql.toggle.pinned.tab", "Pin/unpin", gl,
     one(CS | ImGuiKey_P), one(CS | ImGuiKey_P), Icon::pin, ok, ""},
    // Do C-Otter: o DBeaver exporta por um assistente, sem tecla.
    {Command::export_result, "", "Export result...", gl,
     none, one(CS | ImGuiKey_X), Icon::save, ok, ""},
    {Command::ddl_by_result, "ui.editors.sql.generate.ddl.by.resultSet", "DDL", gl,
     none, none, Icon::info, ok, ""},

    {Command::ai_assistant, "ai.chat", "AI Assistant", ed, none, none, Icon::ai, out,
     "Sending the schema to an external service is out of scope (ADR 0004)"},

    // --- Grade de resultados --------------------------------------------------
    //
    // Rotulos e teclas de plugins/org.jkiss.dbeaver.ui.editors.data
    // (plugin.xml e bundle.properties). `togglePanel` e' UM comando com
    // parametro no DBeaver; aqui sao cinco linhas, uma por painel, porque
    // cada uma tem a sua tecla. Copiar, colar e selecionar tudo sao do
    // Eclipse, e por isso nao tem id.
    {Command::grid_apply, "core.resultset.applyChanges",
     "Apply changes", gr,
     one(C | ImGuiKey_S), one(C | ImGuiKey_S), Icon::commit, ok,
     ""},
    {Command::grid_apply_commit, "core.resultset.applyAndCommitChanges",
     "Apply and commit changes", gr,
     none, none, Icon::commit, ok,
     ""},
    {Command::grid_reject, "core.resultset.rejectChanges",
     "Reject changes", gr,
     one(C | ImGuiKey_R), one(C | ImGuiKey_R), Icon::rollback, ok,
     ""},
    {Command::grid_confirm_save, "core.resultset.toggleConfirmSave",
     "Show confirmation before save", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_cell_save, "core.resultset.cell.save",
     "Apply cell changes", gr,
     one(CAS | ImGuiKey_Enter), one(CAS | ImGuiKey_Enter), Icon::commit, part,
     "Saves every pending change of the result, not only the current cell"},
    {Command::grid_cell_reset, "core.resultset.cell.reset",
     "Reset cell changes", gr,
     one(ImGuiKey_Escape), one(ImGuiKey_Escape), Icon::rollback, ok,
     ""},
    {Command::grid_set_null, "core.resultset.cell.setNull",
     "Set to NULL", gr,
     none, one(C | ImGuiKey_0), Icon::info, ok,
     ""},
    {Command::grid_set_default, "core.resultset.cell.setDefault",
     "Set to default", gr,
     one(C | ImGuiKey_Backspace), one(C | ImGuiKey_Backspace), Icon::info, ok,
     ""},
    {Command::grid_row_edit_inline, "core.resultset.row.edit.inline",
     "Inline edit", gr,
     one(ImGuiKey_Enter), two(ImGuiKey_Enter, ImGuiKey_F2), Icon::info, ok,
     ""},
    {Command::grid_row_edit, "core.resultset.row.edit",
     "Edit cell", gr,
     one(S | ImGuiKey_Enter), one(S | ImGuiKey_Enter), Icon::info, ok,
     ""},
    {Command::grid_row_add, "core.resultset.row.add",
     "Add row", gr,
     one(A | ImGuiKey_Insert), one(A | ImGuiKey_Insert), Icon::plus, ok,
     ""},
    {Command::grid_row_add_before, "core.resultset.row.add.before",
     "Add row (insert before)", gr,
     one(AS | ImGuiKey_Insert), one(AS | ImGuiKey_Insert), Icon::plus, ok,
     ""},
    {Command::grid_row_copy, "core.resultset.row.copy",
     "Duplicate row", gr,
     one(CA | ImGuiKey_Insert), one(CA | ImGuiKey_Insert), Icon::copy, ok,
     ""},
    {Command::grid_row_copy_before, "core.resultset.row.copy.before",
     "Duplicate row (insert before)", gr,
     one(CAS | ImGuiKey_Insert), one(CAS | ImGuiKey_Insert), Icon::copy, ok,
     ""},
    {Command::grid_copy_above, "core.resultset.row.copy.from.above",
     "Copy from row above", gr,
     one(C | ImGuiKey_D), one(C | ImGuiKey_D), Icon::copy, ok,
     ""},
    {Command::grid_copy_below, "core.resultset.row.copy.from.below",
     "Copy from row below", gr,
     one(CA | ImGuiKey_D), one(CA | ImGuiKey_D), Icon::copy, ok,
     ""},
    {Command::grid_row_delete, "core.resultset.row.delete",
     "Delete current row", gr,
     one(A | ImGuiKey_Delete), one(A | ImGuiKey_Delete), Icon::close, ok,
     ""},
    {Command::grid_row_first, "core.resultset.row.first",
     "First row", gr,
     one(CAS | ImGuiKey_LeftArrow), one(CAS | ImGuiKey_LeftArrow), Icon::first_page, ok,
     ""},
    {Command::grid_row_previous, "core.resultset.row.previous",
     "Previous row", gr,
     one(CA | ImGuiKey_LeftArrow), one(CA | ImGuiKey_LeftArrow), Icon::chevron_left, ok,
     ""},
    {Command::grid_row_next, "core.resultset.row.next",
     "Next row", gr,
     one(CA | ImGuiKey_RightArrow), one(CA | ImGuiKey_RightArrow), Icon::chevron_right, ok,
     ""},
    {Command::grid_row_last, "core.resultset.row.last",
     "Last row", gr,
     one(CAS | ImGuiKey_RightArrow), one(CAS | ImGuiKey_RightArrow), Icon::last_page, ok,
     ""},
    {Command::grid_goto_row, "core.resultset.grid.gotoRow",
     "Go To Row", gr,
     one(C | ImGuiKey_G), one(C | ImGuiKey_G), Icon::info, ok,
     ""},
    {Command::grid_goto_column, "core.resultset.grid.gotoColumn",
     "Go To Column", gr,
     one(CS | ImGuiKey_G), one(CS | ImGuiKey_G), Icon::info, ok,
     ""},
    {Command::grid_navigate_link, "core.resultset.navigateLink",
     "Navigate link", gr,
     one(A | ImGuiKey_Space), one(A | ImGuiKey_Space), Icon::foreign_key, ok,
     ""},
    {Command::grid_references, "core.resultset.referencesMenu",
     "References", gr,
     one(CS | ImGuiKey_1), one(CS | ImGuiKey_1), Icon::references, ok,
     ""},
    {Command::grid_select_row, "core.resultset.grid.selectRow",
     "Select row(s)", gr,
     one(CA | ImGuiKey_R), one(CA | ImGuiKey_R), Icon::info, ok,
     ""},
    {Command::grid_select_column, "core.resultset.grid.selectColumn",
     "Select column(s)", gr,
     one(CA | ImGuiKey_C), one(CA | ImGuiKey_C), Icon::info, ok,
     ""},
    {Command::grid_select_all, "",
     "Select All", gr,
     one(C | ImGuiKey_A), one(C | ImGuiKey_A), Icon::info, ok,
     ""},
    {Command::grid_copy, "",
     "Copy", gr,
     one(C | ImGuiKey_C), one(C | ImGuiKey_C), Icon::copy, ok,
     ""},
    {Command::grid_paste, "",
     "Paste", gr,
     one(C | ImGuiKey_V), one(C | ImGuiKey_V), Icon::copy, ok,
     ""},
    {Command::grid_copy_as, "core.resultset.copyAs",
     "Copy as", gr,
     none, none, Icon::copy, ok,
     ""},
    {Command::grid_copy_column_names, "core.resultset.grid.copyColumnNames",
     "Copy column name(s)", gr,
     one(AS | ImGuiKey_C), one(AS | ImGuiKey_C), Icon::copy, ok,
     ""},
    {Command::grid_copy_row_names, "core.resultset.grid.copyRowNames",
     "Copy row number(s)", gr,
     none, none, Icon::copy, ok,
     ""},
    {Command::grid_move_left, "core.resultset.grid.moveColumnLeft",
     "Move column left", gr,
     one(AS | ImGuiKey_LeftArrow), one(AS | ImGuiKey_LeftArrow), Icon::chevron_left, ok,
     ""},
    {Command::grid_move_right, "core.resultset.grid.moveColumnRight",
     "Move column right", gr,
     one(AS | ImGuiKey_RightArrow), one(AS | ImGuiKey_RightArrow), Icon::chevron_right, ok,
     ""},
    {Command::grid_hide_columns, "core.resultset.grid.hideColumns",
     "Hide columns", gr,
     one(AS | ImGuiKey_H), one(AS | ImGuiKey_H), Icon::info, ok,
     ""},
    {Command::grid_show_columns, "core.resultset.grid.showColumns",
     "Show columns", gr,
     one(AS | ImGuiKey_T), one(AS | ImGuiKey_T), Icon::info, ok,
     ""},
    {Command::grid_hide_empty, "core.resultset.grid.columnsHideEmpty",
     "Hide columns with no data", gr,
     none, none, Icon::info, part,
     "Looks at the rows that are loaded, not at the whole result"},
    {Command::grid_fit_values, "core.resultset.grid.columnsFitValue",
     "Columns width: fit values", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_fit_screen, "core.resultset.grid.columnsFitScreen",
     "Columns width: fit screen", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_column_menu, "core.resultset.grid.showColumnContextMenu",
     "Show context menu for column", gr,
     one(S | ImGuiKey_F11), one(S | ImGuiKey_F11), Icon::info, ok,
     ""},
    {Command::grid_row_color, "core.resultset.grid.selectRowColor",
     "Set row color", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_filter_menu, "core.resultset.filterMenu",
     "Filter menu", gr,
     one(ImGuiKey_F11), one(ImGuiKey_F11), Icon::filter, ok,
     ""},
    {Command::grid_filter_distinct, "core.resultset.filterMenu.distinct",
     "Filter by value", gr,
     one(C | ImGuiKey_F11), one(C | ImGuiKey_F11), Icon::filter, ok,
     ""},
    {Command::grid_filter_settings, "core.resultset.filterSettings",
     "Customize filters ...", gr,
     one(AS | ImGuiKey_F), one(AS | ImGuiKey_F), Icon::filter, ok,
     ""},
    {Command::grid_focus_filter, "core.resultset.focus.filter",
     "Activate filter/data editor", gr,
     one(CAS | ImGuiKey_T), one(CAS | ImGuiKey_T), Icon::filter, ok,
     ""},
    {Command::grid_filter_clear, "core.resultset.filterClear",
     "Remove all filters/orderings", gr,
     none, none, Icon::filter, ok,
     ""},
    {Command::grid_filter_reset, "core.resultset.filterReset",
     "Reset default filter", gr,
     none, none, Icon::filter, ok,
     ""},
    {Command::grid_filter_save, "core.resultset.filterSave",
     "Save as default filter", gr,
     none, none, Icon::filter, ok,
     ""},
    {Command::grid_toggle_order, "core.resultset.toggleOrder",
     "Toggle results sort order", gr,
     one(C | ImGuiKey_2), one(C | ImGuiKey_2), Icon::info, ok,
     ""},
    {Command::grid_fetch_page, "core.resultset.fetch.page",
     "Fetch Next Page", gl,
     one(CA | ImGuiKey_N), one(CA | ImGuiKey_N), Icon::chevron_right, ok,
     ""},
    {Command::grid_fetch_all, "core.resultset.fetch.all",
     "Fetch All Data", gl,
     one(CS | ImGuiKey_Equal), one(CS | ImGuiKey_Equal), Icon::last_page, ok,
     ""},
    {Command::grid_count, "core.resultset.count",
     "Row Count", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_export, "core.resultset.export",
     "Export data", gr,
     none, none, Icon::save, part,
     "Exports the rows that are loaded; DBeaver opens the data transfer wizard"},
    {Command::grid_open_with, "core.resultset.openWith",
     "Open with", gr,
     none, none, Icon::save, part,
     "Writes a CSV to the temporary folder and opens it with the system default application"},
    {Command::grid_generate_script, "core.resultset.generateScript",
     "Generate script", gr,
     none, none, Icon::play_script, ok,
     ""},
    {Command::grid_toggle_mode, "core.resultset.toggleMode",
     "Toggle Grid/Record view", gr,
     one(ImGuiKey_Tab), one(ImGuiKey_Tab), Icon::record, ok,
     ""},
    {Command::grid_switch_presentation, "core.resultset.switchPresentation",
     "Switch presentation", gr,
     one(C | ImGuiKey_GraveAccent), one(C | ImGuiKey_GraveAccent), Icon::info, part,
     "Grid and plain text; DBeaver also has chart and spatial presentations"},
    {Command::grid_toggle_preview, "core.resultset.grid.togglePreview",
     "Toggle result panels", gr,
     two(C | ImGuiKey_7, ImGuiKey_F7), two(C | ImGuiKey_7, ImGuiKey_F7), Icon::info, ok,
     ""},
    {Command::grid_activate_preview, "core.resultset.grid.activatePreview",
     "Activate results/panel", gr,
     one(CS | ImGuiKey_7), one(CS | ImGuiKey_7), Icon::info, ok,
     ""},
    {Command::grid_panel_value, "core.resultset.grid.togglePanel",
     "Value panel", gr,
     one(CA | ImGuiKey_F2), one(CA | ImGuiKey_F2), Icon::info, ok,
     ""},
    {Command::grid_panel_references, "core.resultset.grid.togglePanel",
     "References panel", gr,
     one(CA | ImGuiKey_F3), one(CA | ImGuiKey_F3), Icon::references, ok,
     ""},
    {Command::grid_panel_metadata, "core.resultset.grid.togglePanel",
     "Metadata panel", gr,
     one(CA | ImGuiKey_F4), one(CA | ImGuiKey_F4), Icon::info, ok,
     ""},
    {Command::grid_panel_grouping, "core.resultset.grid.togglePanel",
     "Grouping panel", gr,
     one(CA | ImGuiKey_F5), one(CA | ImGuiKey_F5), Icon::info, ok,
     ""},
    {Command::grid_panel_calc, "core.resultset.grid.togglePanel",
     "Calc panel", gr,
     one(CA | ImGuiKey_F6), one(CA | ImGuiKey_F6), Icon::info, ok,
     ""},
    {Command::grid_panel_maximize, "core.resultset.grid.togglePanelMaximize",
     "Maximize/restore panels", gr,
     none, none, Icon::info, out,
     "The panels are dockable windows: drag one out or double-click its tab"},
    {Command::grid_toggle_layout, "core.resultset.grid.toggleLayout",
     "Toggle panels layout", gr,
     none, none, Icon::info, out,
     "The panels are dockable windows: drag one to the side or to the bottom"},
    {Command::grid_switch_viewer, "core.resultset.grid.switchContentViewer",
     "Switch content viewer", gr,
     none, none, Icon::info, ok,
     ""},
    {Command::grid_zoom_in, "core.resultset.zoomIn",
     "Zoom in data grid", gr,
     one(A | ImGuiKey_0), two(A | ImGuiKey_0, C | ImGuiKey_Equal), Icon::plus, ok,
     ""},
    {Command::grid_zoom_out, "core.resultset.zoomOut",
     "Zoom out data grid", gr,
     one(A | ImGuiKey_9), two(A | ImGuiKey_9, C | ImGuiKey_Minus), Icon::info, ok,
     ""},

    // --- Aplicacao, banco e navegador ------------------------------------------
    //
    // Rotulos e teclas de plugins/org.jkiss.dbeaver.core, ui.navigator,
    // ui.editors.connection e ui.app.standalone (plugin.xml, bundle.properties).
    //
    // Commit e rollback: no DBeaver as teclas sao Ctrl+Alt+Shift+K / R, e
    // Ctrl+Shift+C e' "Advanced copy" na grade. O perfil C-Otter guarda as
    // teclas que o programa sempre teve (Ctrl+Shift+C / Ctrl+Shift+R).
    {Command::app_commit, "core.commit", "Commit", gl,
     two(CAS | ImGuiKey_K, C | ImGuiKey_4), one(CS | ImGuiKey_C), Icon::commit, ok, ""},
    {Command::app_rollback, "core.rollback", "Rollback", gl,
     two(CAS | ImGuiKey_R, C | ImGuiKey_8), one(CS | ImGuiKey_R), Icon::rollback, ok, ""},
    // Ctrl+Shift+A: no DBeaver e' "Set as default" na arvore; no perfil
    // C-Otter continua sendo o auto-commit que o programa sempre teve.
    {Command::app_auto_commit, "core.txn.autocommit", "Auto-commit", gl,
     none, one(CS | ImGuiKey_A), Icon::refresh, ok, ""},
    {Command::app_txn_pending, "core.txn.pending", "Pending transactions", gl,
     none, none, Icon::commit, ok, ""},
    {Command::app_txn_log, "core.txn.log", "Transaction log", gl,
     none, none, Icon::exec_log, part,
     "Lists the statements of the open transaction from the query log of this session"},
    {Command::app_connect, "core.connect", "Connect", nav,
     none, none, Icon::connect, ok, ""},
    {Command::app_disconnect, "core.disconnect", "Disconnect", gl,
     none, none, Icon::disconnect, ok, ""},
    {Command::app_disconnect_all, "core.disconnectAll", "Disconnect All", gl,
     none, none, Icon::disconnect, ok, ""},
    {Command::app_disconnect_others, "core.disconnectOther", "Disconnect Other", gl,
     none, none, Icon::disconnect, ok, ""},
    {Command::app_reconnect, "core.invalidate", "Invalidate/Reconnect", gl,
     none, none, Icon::refresh, ok, ""},
    {Command::app_read_only, "core.connection.readonly", "Read-only", gl,
     none, none, Icon::lock, ok, ""},
    {Command::app_new_connection, "core.new.connection", "New Connection", gl,
     one(CS | ImGuiKey_N), one(CS | ImGuiKey_N), Icon::connect, ok, ""},
    {Command::app_new_connection_url, "core.new.connection.from.url",
     "New connection from JDBC URL", gl, none, none, Icon::connect, ok, ""},
    {Command::app_new_folder, "core.new.folder", "New Folder", nav,
     none, none, Icon::folder, ok, ""},
    {Command::app_select_connection, "ui.tools.select.connection",
     "Select active connection", gl,
     one(C | ImGuiKey_9), one(C | ImGuiKey_9), Icon::connect, ok, ""},
    {Command::app_select_schema, "ui.tools.select.schema", "Select active schema", gl,
     one(C | ImGuiKey_0), none, Icon::schema, ok, ""},
    {Command::app_set_default, "core.navigator.set.default", "Set as default", nav,
     one(CS | ImGuiKey_A), none, Icon::schema, ok, ""},
    // Ctrl+Shift+D no DBeaver; no perfil C-Otter essa tecla e' do editor
    // (selecionar todas as ocorrencias), e abrir objeto e' Ctrl+P.
    {Command::app_goto_object, "core.object.goto", "Open database object ...", gl,
     one(CS | ImGuiKey_D), one(C | ImGuiKey_P), Icon::search, ok, ""},
    {Command::app_object_open, "core.object.open", "Edit object", nav,
     one(ImGuiKey_F4), two(ImGuiKey_F4, ImGuiKey_Enter), Icon::open, ok, ""},
    {Command::app_object_create, "core.object.create", "Create object", nav,
     two(A | ImGuiKey_Insert, C | ImGuiKey_N), two(A | ImGuiKey_Insert, C | ImGuiKey_N),
     Icon::plus, ok, ""},
    {Command::app_object_delete, "core.object.delete", "Delete object", nav,
     one(ImGuiKey_Delete), one(ImGuiKey_Delete), Icon::close, ok, ""},
    // Eclipse: org.eclipse.ui.edit.rename.
    {Command::app_object_rename, "", "Rename object", nav,
     one(ImGuiKey_F2), one(ImGuiKey_F2), Icon::info, ok, ""},
    {Command::app_view_data, "ui.editors.data.forSelection", "View data", nav,
     none, none, Icon::table, ok, ""},
    {Command::app_read_data_console, "core.sql.editor.forSelection",
     "Read data in SQL console", nav, none, none, Icon::terminal, ok, ""},
    {Command::app_tools_menu, "ui.tools.menu", "Context tools", gl,
     one(A | ImGuiKey_GraveAccent), one(A | ImGuiKey_GraveAccent), Icon::administer,
     ok, ""},
    {Command::app_export_data, "core.export.data", "Export Data", nav,
     none, none, Icon::save, ok, ""},
    {Command::app_import_data, "core.import.data", "Import Data", nav,
     none, none, Icon::open, part,
     "Imports a CSV file; DBeaver also imports XLSX, XML and from another table"},
    {Command::app_filter_toggle, "core.object.filter.toggle", "Toggle filter", nav,
     none, none, Icon::filter, ok, ""},
    {Command::app_filter_config, "core.object.filter.config", "Configure filter", nav,
     none, none, Icon::filter_config, ok, ""},
    {Command::app_filter_clear, "core.object.filter.clear", "Clear filter", nav,
     none, none, Icon::filter_reset, ok, ""},
    {Command::app_filter_include, "core.object.filter.add.include",
     "Show only selected object(s)", nav, none, none, Icon::filter_apply, ok, ""},
    {Command::app_filter_exclude, "core.object.filter.add.exclude",
     "Hide selected object(s)", nav, none, none, Icon::filter, ok, ""},
    {Command::app_filter_connected, "navigator.filter.connected",
     "Show all connections", nav, none, none, Icon::connect, ok, ""},
    {Command::app_filter_focus, "core.navigator.filter.focus",
     "Focus Database Navigator Filter", gl, none, one(CA | ImGuiKey_F), Icon::search,
     ok, ""},
    {Command::app_link_editor, "core.navigator.linkeditor", "Link with editor", gl,
     one(CS | ImGuiKey_Comma), one(CS | ImGuiKey_Comma), Icon::references, ok, ""},
    {Command::app_move_up, "core.object.move.up", "Move up", nav,
     none, none, Icon::chevron_down, part,
     "Reorders saved connections; DBeaver also reorders the columns of a new table"},
    {Command::app_move_down, "core.object.move.down", "Move down", nav,
     none, none, Icon::chevron_down, part,
     "Reorders saved connections; DBeaver also reorders the columns of a new table"},
    {Command::app_move_top, "core.object.move.top", "Move to top", nav,
     none, none, Icon::chevron_down, part,
     "Reorders saved connections; DBeaver also reorders the columns of a new table"},
    {Command::app_move_bottom, "core.object.move.bottom", "Move to bottom", nav,
     none, none, Icon::chevron_down, part,
     "Reorders saved connections; DBeaver also reorders the columns of a new table"},
    {Command::app_bookmark_add, "core.navigator.bookmark.add", "Add bookmark", nav,
     one(CAS | ImGuiKey_D), one(CA | ImGuiKey_B), Icon::pin, ok, ""},
    {Command::app_bookmark_navigate, "core.navigator.bookmark.navigate", "Navigate to",
     gl, none, none, Icon::pin, ok, ""},
    {Command::app_props_next, "entity.propsTab.nextPage", "Next tab", gl,
     one(AS | ImGuiKey_DownArrow), one(AS | ImGuiKey_DownArrow), Icon::chevron_down,
     ok, ""},
    {Command::app_props_previous, "entity.propsTab.prevPage", "Previous tab", gl,
     one(AS | ImGuiKey_UpArrow), one(AS | ImGuiKey_UpArrow), Icon::chevron_down, ok, ""},
    {Command::app_source_tab, "ui.object.property.source.activate",
     "Open source tab", gl, none, none, Icon::info, ok, ""},
    {Command::app_column_index, "navigator.create.column.index",
     "New index from selection", nav, none, none, Icon::index, ok, ""},
    {Command::app_column_constraint, "navigator.create.column.constraint",
     "New constraint from selection", nav, none, none, Icon::constraint, ok, ""},
    {Command::app_procedure_execute, "core.procedure.execute",
     "Execute stored procedure", nav, none, none, Icon::procedure, ok, ""},
    {Command::app_copy_special, "core.edit.copy.special", "Advanced copy", gr,
     one(CS | ImGuiKey_C), none, Icon::copy, ok, ""},
    {Command::app_copy_special_last, "core.edit.copy.special.with.last.settings",
     "Advanced copy with the most recent settings", gr,
     one(CAS | ImGuiKey_C), one(CAS | ImGuiKey_C), Icon::copy, ok, ""},
    {Command::app_paste_special, "core.edit.paste.special", "Advanced paste ...", gr,
     one(CS | ImGuiKey_V), one(CS | ImGuiKey_V), Icon::copy, ok, ""},
    {Command::app_generate_uuid, "core.generate.uuid", "Generate UUID", gl,
     one(CAS | ImGuiKey_U), one(CAS | ImGuiKey_U), Icon::info, ok, ""},
    {Command::app_load_resource, "core.edit.load.resource",
     "Load resource(s) from local disk", gr, none, none, Icon::open, ok, ""},
    {Command::app_save_resource, "core.edit.save.resource",
     "Save resource(s) to local disk", gr, none, none, Icon::save, ok, ""},
    {Command::app_open_spreadsheet, "ext.data.office.results.openSpreadsheet",
     "Open results in Excel", gr, none, none, Icon::save, part,
     "Writes a CSV and opens it with the spreadsheet application; DBeaver writes XLSX"},
    {Command::app_script_associate, "core.sql.script.associate",
     "Associate with data source", gl, none, none, Icon::connect, ok, ""},
    {Command::app_show_scripts, "core.sql.editor.showScripts", "Show scripts", gl,
     none, none, Icon::open, ok, ""},
    {Command::app_show_in_explorer, "core.show.in.explorer",
     "Show resource in explorer", gl, none, none, Icon::folder, ok, ""},
    {Command::app_change_password, "connection.changeCurrentPassword",
     "Change user password", gl, none, none, Icon::key, ok, ""},
    {Command::app_driver_manager, "core.driver.manager", "Driver manager", gl,
     none, none, Icon::settings, part,
     "Lists the built-in drivers; C-Otter speaks the wire protocols itself and has no "
     "JDBC drivers to download or configure"},
    {Command::app_preferences, "core.navigator.preferences", "Preferences", gl,
     none, one(C | ImGuiKey_Comma), Icon::settings, ok, ""},
    {Command::app_view_toggle, "core.view.toggle", "Show/Hide view", gl,
     none, none, Icon::panels, ok, ""},
    {Command::app_log_clear, "core.qm.clear", "Clear log", gl,
     none, none, Icon::close, ok, ""},
    {Command::app_log_filter, "core.qm.filter", "Log filters", gl,
     none, none, Icon::filter, ok, ""},
    {Command::app_process_stop, "core.process.stop", "Stop processes", gl,
     none, none, Icon::stop, ok, ""},
    {Command::app_clear_history, "core.util.clearHistory", "Clear History...", gl,
     none, none, Icon::close, ok, ""},
    {Command::app_reset_settings, "core.util.resetSettings", "Reset Settings...", gl,
     none, none, Icon::settings, ok, ""},
    {Command::app_collect_diagnostics, "core.util.collectDiagnosticInfo",
     "Collect diagnostic info", gl, none, none, Icon::info, ok, ""},
    {Command::app_dashboard_open, "ui.dashboard.open", "Dashboard", gl,
     one(CAS | ImGuiKey_B), one(CAS | ImGuiKey_B), Icon::system_info, part,
     "Line charts of the server statistics; DBeaver also has bar and pie views and "
     "user-defined charts"},
    {Command::app_dashboard_add, "ui.dashboard.add", "Add chart", gl,
     none, none, Icon::plus, ok, ""},
    {Command::app_dashboard_remove, "ui.dashboard.remove", "Remove chart", gl,
     none, none, Icon::close, ok, ""},
    {Command::app_dashboard_catalog, "ui.dashboard.catalog.show",
     "Show chart catalog", gl, none, none, Icon::system_info, ok, ""},
    {Command::app_dashboard_configure, "ui.dashboard.configure",
     "Dashboard settings", gl, none, none, Icon::settings, ok, ""},
    {Command::app_dashboard_refresh, "ui.chart.refresh", "Refresh chart", gl,
     none, none, Icon::refresh, ok, ""},
    {Command::app_dashboard_view, "ui.dashboard.view", "View chart", gl,
     none, none, Icon::system_info, ok, ""},
    {Command::app_dashboard_reset, "", "Reset dashboard", gl,
     none, none, Icon::refresh, ok, ""},
    // Do C-Otter: todos os comandos numa busca -- o "Quick Access" (Ctrl+3) do
    // Eclipse, que o DBeaver herda.
    {Command::app_command_palette, "", "Command palette", gl,
     one(C | ImGuiKey_3), two(C | ImGuiKey_3, CAS | ImGuiKey_P), Icon::search, ok, ""},
    {Command::app_help, "", "Keyboard shortcuts", gl,
     one(ImGuiKey_F1), one(ImGuiKey_F1), Icon::info, ok, ""},
    {Command::app_exit, "core.exit", "Exit", gl,
     none, none, Icon::close, ok, ""},

    // --- Editor de texto do Eclipse ---------------------------------------------
    //
    // No perfil C-Otter mover linhas e' Alt+setas, tecla do proprio widget.
    {Command::edit_move_lines_up, "", "Move lines up", ed,
     one(CS | ImGuiKey_UpArrow), none, Icon::info, ok, ""},
    {Command::edit_move_lines_down, "", "Move lines down", ed,
     one(CS | ImGuiKey_DownArrow), none, Icon::info, ok, ""},
    {Command::edit_join_lines, "", "Join lines", ed,
     one(CS | ImGuiKey_J), one(CS | ImGuiKey_J), Icon::info, ok, ""},
    {Command::edit_word_completion, "", "Word completion", ed,
     one(CS | ImGuiKey_Space), one(CS | ImGuiKey_Space), Icon::info, ok, ""},
    {Command::edit_open_local_file, "", "Open file...", gl,
     none, none, Icon::open, ok, ""},
};

static_assert(std::size(kCommands) == kCommandCount,
              "kCommands ficou fora de sincronia com o enum Command");

// O que o TextEditor trata sozinho (TextEditor::handleKeyboardInputs).
constexpr WidgetKey kWidgetKeys[] = {
    {C | ImGuiKey_Enter,        "insert line below"},
    {S | ImGuiKey_Enter,        "insert line above"},
    {C | ImGuiKey_A,            "select all"},
    {C | ImGuiKey_D,            "add next occurrence to the selection"},
    {CA | ImGuiKey_D,           "add next whole-word occurrence"},
    {CA | ImGuiKey_L,           "add next whole-word occurrence"},
    {CS | ImGuiKey_D,           "select all occurrences"},
    {CAS | ImGuiKey_D,          "select all whole-word occurrences"},
    {C | ImGuiKey_X,            "cut"},
    {C | ImGuiKey_C,            "copy"},
    {C | ImGuiKey_V,            "paste"},
    {C | ImGuiKey_Z,            "undo"},
    {CS | ImGuiKey_Z,           "redo"},
    {C | ImGuiKey_Y,            "redo"},
    {CS | ImGuiKey_K,           "delete the selected lines"},
    {C | ImGuiKey_LeftBracket,  "deindent"},
    {C | ImGuiKey_RightBracket, "indent"},
    {A | ImGuiKey_UpArrow,      "move lines up"},
    {A | ImGuiKey_DownArrow,    "move lines down"},
    {C | ImGuiKey_Slash,        "toggle line comment"},
    {C | ImGuiKey_L,            "toggle line comment"},
    {C | ImGuiKey_F,            "find and replace"},
    {CS | ImGuiKey_F,           "find all"},
    {C | ImGuiKey_G,            "find next"},
    {C | ImGuiKey_Space,        "autocomplete"},
};

const char* key_name(ImGuiKey key) {
    switch (key) {
        case ImGuiKey_Enter:        return "Enter";
        case ImGuiKey_Backslash:    return "\\";
        case ImGuiKey_Slash:        return "/";
        case ImGuiKey_LeftBracket:  return "[";
        case ImGuiKey_RightBracket: return "]";
        case ImGuiKey_Apostrophe:   return "'";
        case ImGuiKey_Period:       return ".";
        case ImGuiKey_Comma:        return ",";
        case ImGuiKey_Equal:        return "=";
        case ImGuiKey_Minus:        return "-";
        case ImGuiKey_UpArrow:      return "Up";
        case ImGuiKey_DownArrow:    return "Down";
        case ImGuiKey_LeftArrow:    return "Left";
        case ImGuiKey_RightArrow:   return "Right";
        case ImGuiKey_Space:        return "Space";
        case ImGuiKey_GraveAccent:  return "`";
        case ImGuiKey_Backspace:    return "Backspace";
        case ImGuiKey_Delete:       return "Delete";
        case ImGuiKey_Insert:       return "Insert";
        case ImGuiKey_Escape:       return "Esc";
        default:                    return ImGui::GetKeyName(key);
    }
}

} // namespace

std::span<const CommandInfo> all_commands() { return kCommands; }

const CommandInfo& command_info(Command command) {
    return kCommands[static_cast<std::size_t>(command)];
}

std::span<const ImGuiKeyChord> command_keys(Command command, Keymap keymap) {
    const CommandInfo& info = command_info(command);
    const auto& keys = keymap == Keymap::dbeaver ? info.dbeaver_keys
                                                 : info.otter_keys;
    std::size_t count = 0;
    while (count < keys.size() && keys[count] != 0) ++count;
    return {keys.data(), count};
}

std::string chord_label(ImGuiKeyChord chord) {
    if (chord == 0) return {};

    std::string label;
    if ((chord & ImGuiMod_Ctrl) != 0)  label += "Ctrl+";
    if ((chord & ImGuiMod_Alt) != 0)   label += "Alt+";
    if ((chord & ImGuiMod_Shift) != 0) label += "Shift+";
    label += key_name(static_cast<ImGuiKey>(chord & ~ImGuiMod_Mask_));
    return label;
}

std::string command_shortcut(Command command, Keymap keymap) {
    const std::span<const ImGuiKeyChord> keys = command_keys(command, keymap);
    return keys.empty() ? std::string{} : chord_label(keys.front());
}

std::string_view keymap_id(Keymap keymap) noexcept {
    return keymap == Keymap::dbeaver ? "dbeaver" : "otter";
}

const char* keymap_label(Keymap keymap) noexcept {
    return keymap == Keymap::dbeaver ? "DBeaver" : "C-Otter";
}

Keymap keymap_from_id(std::string_view id) noexcept {
    return id == "otter" ? Keymap::otter : Keymap::dbeaver;
}

std::span<const WidgetKey> widget_keys() { return kWidgetKeys; }

} // namespace otter::ui
