// C-Otter -- ui/grid_commands.cpp
//
// Os comandos da grade de resultado (`core.resultset.*` no DBeaver): teclado,
// menu de contexto da celula, barra de filtro, paineis e os dialogos que eles
// abrem. A tabela com rotulo e tecla de cada um esta' em ui/commands.cpp; o
// mapa contra o DBeaver, em docs/EDITOR-COMMANDS.md.
//
// O que e' REGRA -- recortar a selecao, gerar o script das linhas, somar,
// montar a consulta de uma chave estrangeira -- esta' em db/grid_ops.cpp, com
// teste. Aqui fica o que so' existe com janela.
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "base/json.hpp"
#include "base/paths.hpp"
#include "db/ddl.hpp"
#include "db/export.hpp"
#include "db/grid_ops.hpp"
#include "db/value_view.hpp"
#include "sql/paging.hpp"
#include "ui/app_window.hpp"   // mono_font()
#include "ui/commands.hpp"
#include "ui/hint.hpp"
#include "ui/platform_open.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // tabela: ordem e largura das colunas

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }
ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

constexpr float kZoomMin  = 0.6f;
constexpr float kZoomMax  = 2.5f;
constexpr float kZoomStep = 1.1f;

// As cores de "Set row color". Fundos, aplicados com transparencia: tem de
// dar para ler o texto por cima nos tres temas.
constexpr std::uint32_t kRowPalette[] = {
    0xFF4D4DE6u, 0xFF3D9BF0u, 0xFF37C8E8u, 0xFF58C06Au,
    0xFFC9A64Eu, 0xFFE0823Du, 0xFFC76AD6u, 0xFF8C8C8Cu,
};

std::string filters_path() {
    namespace fs = std::filesystem;
    return (fs::path(data_directory()) / "grid-filters.json").string();
}

// Corta um texto longo para caber num item de menu.
std::string shorten(std::string_view text, std::size_t limit = 40) {
    std::string out;
    for (const char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') out += ' ';
        else                                     out += c;
        if (out.size() >= limit) {
            out += "...";
            break;
        }
    }
    return out;
}

} // namespace

// --- Selecao -----------------------------------------------------------------------

// O bloco selecionado: do canto fixo (ancora) ate' a celula corrente. As
// colunas saem na ordem da TELA e sem as escondidas -- e' o que o usuario ve',
// e o que ele espera ao colar numa planilha.
db::GridSelection MainShell::grid_selection(const SqlDocument& document,
                                            const db::ResultSet& rs) const {
    db::GridSelection selection;
    if (!has_selection_ || selected_document_ != document.id() ||
        rs.row_count() == 0 || rs.column_count() == 0) {
        return selection;
    }

    const std::size_t row    = std::min(selected_row_, rs.row_count() - 1);
    const std::size_t anchor = std::min(anchor_row_, rs.row_count() - 1);
    selection.row_first = std::min(row, anchor);
    selection.row_last  = std::max(row, anchor);

    const GridView& view = document.grid_view();

    // Posicao de cada coluna na tela. Sem a ordem da tabela (primeiro
    // quadro, modo registro), vale a do resultado.
    std::vector<std::size_t> order = view.order;
    if (order.empty()) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (!view.is_hidden(c)) order.push_back(c);
        }
    }

    const auto position = [&order](std::size_t column) {
        const auto it = std::find(order.begin(), order.end(), column);
        return it != order.end()
                   ? static_cast<std::size_t>(it - order.begin())
                   : order.size();
    };

    const std::size_t a = position(std::min(selected_column_, rs.column_count() - 1));
    const std::size_t b = position(std::min(anchor_column_, rs.column_count() - 1));
    if (a >= order.size() || b >= order.size()) {
        selection.columns = {std::min(selected_column_, rs.column_count() - 1)};
        return selection;
    }
    for (std::size_t i = std::min(a, b); i <= std::max(a, b); ++i) {
        selection.columns.push_back(order[i]);
    }
    return selection;
}

bool MainShell::grid_cell_in_selection(const SqlDocument& document,
                                       std::size_t row, std::size_t column) const {
    if (!has_selection_ || selected_document_ != document.id()) return false;

    const std::size_t r0 = std::min(selected_row_, anchor_row_);
    const std::size_t r1 = std::max(selected_row_, anchor_row_);
    if (row < r0 || row > r1) return false;

    if (selected_column_ == anchor_column_) return column == selected_column_;

    // Pela posicao na tela, como grid_selection().
    const std::vector<std::size_t>& order = document.grid_view().order;
    if (order.empty()) {
        return column >= std::min(selected_column_, anchor_column_) &&
               column <= std::max(selected_column_, anchor_column_);
    }
    const auto position = [&order](std::size_t c) {
        return std::find(order.begin(), order.end(), c) - order.begin();
    };
    const auto p  = position(column);
    const auto pa = position(selected_column_);
    const auto pb = position(anchor_column_);
    return p >= std::min(pa, pb) && p <= std::max(pa, pb);
}

void MainShell::select_grid_cell(SqlDocument& document, std::size_t row,
                                 std::size_t column, bool extend) {
    const bool fresh = !has_selection_ || selected_document_ != document.id();

    selected_document_ = document.id();
    selected_row_      = row;
    selected_column_   = column;
    has_selection_     = true;

    // Sem Shift (ou sem selecao anterior), o bloco volta a ser uma celula.
    if (!extend || fresh) {
        anchor_row_    = row;
        anchor_column_ = column;
    }
}

void MainShell::start_inline_edit(SqlDocument& document, const db::ResultSet& rs,
                                  std::size_t row, std::size_t column) {
    if (!document.edit_target().editable()) return;
    if (document.edits().is_deleted(row)) return;

    editing_active_   = true;
    editing_document_ = document.id();
    editing_row_      = row;
    editing_column_   = column;

    const db::CellValue value = db::cell_value(rs, document.edits(), row, column);
    std::snprintf(edit_buffer_, sizeof edit_buffer_, "%s",
                  value.is_null ? "" : value.text.c_str());
}

// A tabela de origem do resultado, com as chaves estrangeiras. Copia: o
// catalogo vive atras do mutex da sessao.
std::optional<db::TableMeta> MainShell::grid_source_table(const SqlDocument& document) {
    const db::EditTarget& target = document.edit_target();
    if (target.table.empty()) return std::nullopt;

    for (const db::SchemaMeta& schema : session_for(document).schemas()) {
        if (schema.name != target.schema) continue;
        for (const db::TableMeta& table : schema.tables) {
            if (table.name == target.table) return table;
        }
    }
    return std::nullopt;
}

// --- Filtro ------------------------------------------------------------------------

void MainShell::apply_grid_filter(SqlDocument& document, sql::ColumnFilter filter) {
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;
    if (document.paged_sql().empty()) {
        show_toast(TR("filtering needs a query that can be run again"));
        return;
    }

    document.set_filter(std::move(filter));
    // Filtro muda o total: a contagem anterior nao vale, e a pagina 5 do
    // conjunto novo nao corresponde a nada.
    execute_page(document, 0);
}

// A barra acima da grade: o campo "Enter a SQL expression to filter results"
// do DBeaver, com os botoes de aplicar, limpar e personalizar.
void MainShell::draw_grid_filter_bar(SqlDocument& document, const db::ResultSet& rs) {
    if (rs.column_count() == 0) return;

    const Palette& p = colors();
    GridView& view = document.grid_view();

    const bool can_filter = !document.paged_sql().empty();
    const float button = ImGui::GetFrameHeight();

    icon_inline(Icon::filter, document.filter().empty() ? p.text_dim : p.warn);
    ImGui::SameLine(0.0f, 6.0f);

    // Tres botoes a' direita do campo.
    const float reserve = (button + 2.0f) * 3.0f + 6.0f;
    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - reserve));

    if (view.focus_filter) {
        ImGui::SetKeyboardFocusHere();
        view.focus_filter = false;
    }

    ImGui::BeginDisabled(!can_filter);
    const bool entered = ImGui::InputTextWithHint(
        "##gridfilter", TR("Enter a SQL expression to filter results (e.g. total > 100)"),
        view.filter_text, sizeof view.filter_text,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();

    if (!can_filter && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        hint(TR("filtering needs a query that can be run again"));
    }

    const bool dirty = document.filter().condition != view.filter_text;

    ImGui::SameLine(0.0f, 4.0f);
    const bool apply = icon_button("##filterapply", Icon::filter_apply,
                                   TR("Apply filter criteria (Enter)"),
                                   can_filter && dirty, dirty ? p.ok : 0, button);
    if ((entered || apply) && can_filter) {
        sql::ColumnFilter filter = document.filter();
        filter.condition = view.filter_text;
        apply_grid_filter(document, std::move(filter));
    }

    ImGui::SameLine(0.0f, 2.0f);
    const bool has_any = !document.filter().empty() || !document.sort().empty();
    if (icon_button("##filterclear", Icon::filter_reset,
                    TR("Remove all filters/orderings"), can_filter && has_any, 0,
                    button)) {
        queued_commands_.push_back(Command::grid_filter_clear);
    }

    ImGui::SameLine(0.0f, 2.0f);
    if (icon_button("##filtercustom", Icon::filter_config, TR("Customize filters ..."),
                    true, 0, button)) {
        queued_commands_.push_back(Command::grid_filter_settings);
    }
}

// --- Habilitacao e marca -----------------------------------------------------------

bool MainShell::grid_command_enabled(Command command) {
    SqlDocument* document = active_document();
    if (document == nullptr) return false;

    const bool has_result = document->result().has_value() &&
                            document->result()->column_count() > 0;
    Session& target = session_for(*document);
    const bool can_run = target.state() == SessionState::connected && !target.busy();

    // Os que valem sem celula selecionada.
    switch (command) {
        case Command::grid_confirm_save:
            return true;
        case Command::grid_apply:
        case Command::grid_apply_commit:
        case Command::grid_cell_save:
            return has_result && can_run && document->edits().has_changes();
        case Command::grid_reject:
            return has_result && document->edits().has_changes();
        case Command::grid_fetch_page:
            return has_result && can_run && document->paged() && document->has_more();
        case Command::grid_fetch_all:
            return has_result && can_run && document->paged() &&
                   !document->paged_sql().empty();
        case Command::grid_count:
            return has_result && can_run && !document->paged_sql().empty();
        case Command::grid_filter_clear:
            return has_result && can_run &&
                   (!document->filter().empty() || !document->sort().empty());
        case Command::grid_filter_reset:
        case Command::grid_filter_save:
            return has_result && !document->edit_target().table.empty();
        case Command::grid_export:
        case Command::grid_open_with:
        case Command::grid_filter_settings:
        case Command::grid_focus_filter:
        case Command::grid_toggle_mode:
        case Command::grid_switch_presentation:
        case Command::grid_show_columns:
        case Command::grid_hide_empty:
        case Command::grid_fit_values:
        case Command::grid_fit_screen:
        case Command::grid_zoom_in:
        case Command::grid_zoom_out:
        case Command::grid_panel_metadata:
        case Command::grid_panel_calc:
        case Command::grid_panel_references:
        case Command::grid_panel_grouping:
        case Command::grid_select_all:
        case Command::grid_row_add:
        case Command::grid_row_add_before:
            if (command == Command::grid_row_add ||
                command == Command::grid_row_add_before) {
                return has_result && document->edit_target().editable();
            }
            return has_result;
        default:
            break;
    }

    // Daqui para baixo, tudo age sobre a celula corrente.
    if (!has_result) return false;
    const db::ResultSet& rs = *document->result();
    const bool has_cell = has_selection_ && selected_document_ == document->id() &&
                          selected_row_ < rs.row_count() &&
                          selected_column_ < rs.column_count();
    if (!has_cell) return false;

    const bool editable = document->edit_target().editable();
    const std::size_t row = selected_row_;

    switch (command) {
        case Command::grid_cell_reset:
        case Command::grid_set_null:
        case Command::grid_set_default:
        case Command::grid_row_edit_inline:
        case Command::grid_row_edit:
        case Command::grid_row_copy:
        case Command::grid_row_copy_before:
        case Command::grid_row_delete:
        case Command::grid_paste:
            return editable;
        case Command::grid_copy_above:
            return editable && row > 0;
        case Command::grid_copy_below:
            return editable && row + 1 < rs.row_count();
        case Command::grid_navigate_link:
        case Command::grid_references:
        case Command::grid_generate_script:
            return !document->edit_target().table.empty();
        case Command::grid_filter_menu:
        case Command::grid_filter_distinct:
        case Command::grid_toggle_order:
            return can_run && !document->paged_sql().empty();
        case Command::grid_hide_columns:
            // A ultima coluna visivel nao some: uma grade sem coluna nenhuma
            // nao tem como ser desfeita pelo mouse.
            return document->grid_view().hidden_count() + 1 < rs.column_count();
        default:
            return true;
    }
}

bool MainShell::grid_command_checked(Command command) {
    SqlDocument* document = active_document();
    switch (command) {
        case Command::grid_confirm_save:      return settings_.confirm_data_save;
        case Command::grid_panel_value:       return value_panel_.open;
        case Command::grid_toggle_preview:    return value_panel_.open;
        case Command::grid_panel_metadata:    return show_grid_metadata_;
        case Command::grid_panel_references:  return show_grid_references_;
        case Command::grid_panel_calc:        return show_grid_calc_;
        case Command::grid_panel_grouping:
            return document != nullptr && !document->group_spec().group_by.empty();
        case Command::grid_toggle_mode:
            return document != nullptr && document->record_mode();
        case Command::grid_switch_presentation:
            return document != nullptr &&
                   document->grid_view().presentation == GridPresentation::text;
        default:
            return false;
    }
}

// --- Os comandos -------------------------------------------------------------------

void MainShell::run_grid_command(Command command) {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    if (command == Command::grid_confirm_save) {
        settings_.confirm_data_save = !settings_.confirm_data_save;
        save_settings();
        return;
    }
    if (!document->result().has_value()) return;

    const db::ResultSet& rs = *document->result();
    GridView& view = document->grid_view();
    db::EditBuffer& edits = document->edits();

    const bool has_cell = has_selection_ && selected_document_ == document->id() &&
                          selected_row_ < rs.row_count() &&
                          selected_column_ < rs.column_count();
    const std::size_t row = has_cell ? selected_row_ : 0;
    const std::size_t column = has_cell ? selected_column_ : 0;
    const db::GridSelection selection = grid_selection(*document, rs);
    const std::size_t last_row = rs.row_count() > 0 ? rs.row_count() - 1 : 0;

    switch (command) {
        // --- Gravar ---------------------------------------------------------------
        case Command::grid_apply:
        case Command::grid_cell_save:
            request_save_edits(*document, /*commit_after=*/false);
            return;
        case Command::grid_apply_commit:
            request_save_edits(*document, /*commit_after=*/true);
            return;
        case Command::grid_reject:
            edits.clear();
            return;

        // --- Celula ---------------------------------------------------------------
        case Command::grid_cell_reset:
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                // Numa linha marcada para exclusao, reverter a celula nao
                // diria nada: e' a marca que esta' pendente.
                if (edits.is_deleted(r)) {
                    edits.unmark_deleted(r);
                    continue;
                }
                for (const std::size_t c : selection.columns) edits.revert(r, c);
            }
            return;
        case Command::grid_set_null:
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                for (const std::size_t c : selection.columns) edits.set_null(r, c);
            }
            return;
        case Command::grid_set_default:
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                for (const std::size_t c : selection.columns) edits.set_default(r, c);
            }
            return;
        case Command::grid_row_edit_inline:
            start_inline_edit(*document, rs, row, column);
            return;
        case Command::grid_row_edit:
            open_value_panel(*document, rs, row, column);
            value_panel_.editing = document->edit_target().editable() &&
                                   !edits.is_deleted(row);
            return;

        // --- Linha ----------------------------------------------------------------
        case Command::grid_row_add:
            edits.add_row(has_cell ? row + 1 : db::RowInsertion::npos);
            return;
        case Command::grid_row_add_before:
            edits.add_row(has_cell ? row : db::RowInsertion::npos);
            return;
        case Command::grid_row_copy:
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                duplicate_row(*document, rs, r, selection.row_last + 1);
            }
            return;
        case Command::grid_row_copy_before:
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                duplicate_row(*document, rs, r, selection.row_first);
            }
            return;
        case Command::grid_copy_above:
            for (const std::size_t c : selection.columns) {
                edits.copy_cell_from(rs, row - 1, row, c);
            }
            return;
        case Command::grid_copy_below:
            for (const std::size_t c : selection.columns) {
                edits.copy_cell_from(rs, row + 1, row, c);
            }
            return;
        case Command::grid_row_delete: {
            // Alterna: com a linha corrente ja' marcada, desmarca o bloco.
            const bool unmark = edits.is_deleted(row);
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                if (unmark) edits.unmark_deleted(r);
                else        edits.mark_deleted(r);
            }
            return;
        }

        // --- Navegacao ------------------------------------------------------------
        case Command::grid_row_first:
            select_grid_cell(*document, 0, column, false);
            scroll_to_selection_ = true;
            return;
        case Command::grid_row_previous:
            select_grid_cell(*document, row > 0 ? row - 1 : 0, column, false);
            scroll_to_selection_ = true;
            return;
        case Command::grid_row_next:
            select_grid_cell(*document, std::min(row + 1, last_row), column, false);
            scroll_to_selection_ = true;
            return;
        case Command::grid_row_last:
            select_grid_cell(*document, last_row, column, false);
            scroll_to_selection_ = true;
            return;
        case Command::grid_goto_row:
            goto_kind_      = GotoKind::row;
            goto_buffer_[0] = '\0';
            goto_open_      = true;
            return;
        case Command::grid_goto_column:
            goto_kind_      = GotoKind::column;
            goto_buffer_[0] = '\0';
            goto_open_      = true;
            return;

        case Command::grid_navigate_link:
        case Command::grid_references: {
            const std::optional<db::TableMeta> table = grid_source_table(*document);
            if (!table) {
                show_toast(TR("the result has no source table"));
                return;
            }
            // As chaves sao lidas sob demanda. Pede, e repete o comando
            // quando chegarem -- em vez de dizer "sem chave" de uma tabela
            // que tem.
            if (!table->keys_loaded) {
                if (grid_pending_command_ == command) {
                    grid_pending_command_ = Command::count;
                    show_toast(TR("could not read the table keys"));
                    return;
                }
                session_for(*document).load_keys_async(document->edit_target().schema,
                                                      document->edit_target().table);
                grid_pending_command_ = command;
                return;
            }
            grid_pending_command_ = Command::count;

            if (command == Command::grid_navigate_link) {
                const db::LinkQuery link = db::navigate_link_query(
                    rs, edits, *table, document->edit_target().schema, row, column);
                if (link.sql.empty()) {
                    show_toast(TR("this cell is not a foreign key with a value"));
                    return;
                }
                open_sql_tab(link.sql, /*run=*/true);
            } else {
                grid_links_ = db::reference_queries(
                    rs, edits, *table, document->edit_target().schema, row);
                if (grid_links_.empty()) {
                    show_toast(TR("no table references this row"));
                    return;
                }
                grid_popup_request_ = GridPopup::references;
            }
            return;
        }

        // --- Selecao e copia ------------------------------------------------------
        case Command::grid_select_row:
            // A linha inteira: da primeira a' ultima coluna DA TELA.
            if (!view.order.empty()) {
                anchor_column_   = view.order.front();
                selected_column_ = view.order.back();
            } else {
                anchor_column_   = 0;
                selected_column_ = rs.column_count() - 1;
            }
            return;
        case Command::grid_select_column:
            anchor_row_   = 0;
            selected_row_ = last_row;
            return;
        case Command::grid_select_all:
            select_grid_cell(*document, 0,
                             view.order.empty() ? 0 : view.order.front(), false);
            selected_row_    = last_row;
            selected_column_ = view.order.empty() ? rs.column_count() - 1
                                                  : view.order.back();
            return;
        case Command::grid_copy:
            ImGui::SetClipboardText(
                db::selection_to_text(rs, edits, selection).c_str());
            return;
        case Command::grid_paste: {
            const char* clipboard = ImGui::GetClipboardText();
            if (clipboard == nullptr || clipboard[0] == '\0') return;

            const auto rows = db::parse_clipboard(clipboard);
            if (rows.empty()) return;

            // Um valor so' preenche o BLOCO selecionado inteiro -- e' como se
            // repete um valor numa coluna.
            if (rows.size() == 1 && rows.front().size() == 1) {
                for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                    for (const std::size_t c : selection.columns) {
                        edits.set(r, c, rows.front().front());
                    }
                }
                return;
            }

            // Um bloco e' colado a partir do canto de cima da selecao, nas
            // colunas da tela. O que nao cabe nas linhas carregadas fica de
            // fora: colar nao cria linha.
            std::vector<std::size_t> order = view.order;
            if (order.empty()) {
                for (std::size_t c = 0; c < rs.column_count(); ++c) order.push_back(c);
            }
            const auto start = std::find(order.begin(), order.end(),
                                         selection.columns.empty()
                                             ? column : selection.columns.front());
            std::size_t skipped = 0;
            for (std::size_t i = 0; i < rows.size(); ++i) {
                const std::size_t r = selection.row_first + i;
                if (r >= rs.row_count()) { skipped += rows.size() - i; break; }
                auto target = start;
                for (const std::string& value : rows[i]) {
                    if (target == order.end()) break;
                    edits.set(r, *target, value);
                    ++target;
                }
            }
            if (skipped > 0) {
                show_toast(TRF("%zu pasted row(s) did not fit and were left out",
                               skipped));
            }
            return;
        }
        case Command::grid_copy_as:
            grid_popup_request_ = GridPopup::copy_as;
            return;
        case Command::grid_copy_column_names: {
            std::string names;
            for (const std::size_t c : selection.columns) {
                if (!names.empty()) names += '\t';
                names += rs.column(c).info().name;
            }
            ImGui::SetClipboardText(names.c_str());
            return;
        }
        case Command::grid_copy_row_names: {
            // O numero que a barra mostra: contado desde o inicio do
            // resultado, nao desde o inicio da pagina.
            const std::size_t base =
                document->paged() ? document->page() * document->page_size() : 0;
            std::string numbers;
            for (std::size_t r = selection.row_first; r <= selection.row_last; ++r) {
                if (!numbers.empty()) numbers += '\n';
                numbers += std::to_string(base + r + 1);
            }
            ImGui::SetClipboardText(numbers.c_str());
            return;
        }

        // --- Colunas --------------------------------------------------------------
        case Command::grid_move_left:
            view.move_column = column;
            view.move_delta  = -1;
            return;
        case Command::grid_move_right:
            view.move_column = column;
            view.move_delta  = 1;
            return;
        case Command::grid_hide_columns: {
            const std::size_t visible = rs.column_count() - view.hidden_count();
            std::size_t hidden_now = 0;
            for (const std::size_t c : selection.columns) {
                if (hidden_now + 1 >= visible) break;   // sobra pelo menos uma
                view.set_hidden(c, true);
                ++hidden_now;
            }
            // A selecao nao pode ficar numa coluna que sumiu.
            for (std::size_t c = 0; c < rs.column_count(); ++c) {
                if (!view.is_hidden(c)) {
                    select_grid_cell(*document, row, c, false);
                    break;
                }
            }
            return;
        }
        case Command::grid_show_columns:
            view.hidden.clear();
            return;
        case Command::grid_hide_empty: {
            const std::size_t visible = rs.column_count() - view.hidden_count();
            std::size_t left = visible;
            for (std::size_t c = 0; c < rs.column_count() && left > 1; ++c) {
                if (view.is_hidden(c)) continue;
                bool any = false;
                for (std::size_t r = 0; r < rs.row_count() && !any; ++r) {
                    any = !rs.is_null(r, c) && !rs.text(r, c).empty();
                }
                if (!any) {
                    view.set_hidden(c, true);
                    --left;
                }
            }
            return;
        }
        case Command::grid_fit_values:
            view.fit_values = true;
            return;
        case Command::grid_fit_screen:
            view.fit_screen = true;
            return;
        case Command::grid_column_menu:
        case Command::grid_filter_menu:
            grid_menu_request_ = column + 1;
            return;
        case Command::grid_row_color:
            grid_popup_request_ = GridPopup::row_color;
            return;

        // --- Filtro e ordem -------------------------------------------------------
        case Command::grid_filter_distinct: {
            grid_distinct_column_  = column;
            grid_distinct_search_[0] = '\0';
            grid_distinct_.clear();
            grid_popup_request_ = GridPopup::distinct;

            // No servidor, sobre o resultado INTEIRO e com os outros filtros
            // em vigor: os valores oferecidos sao os que ainda existem.
            sql::ColumnFilter others = document->filter();
            others.set_column(rs.column(column).info().name, "");

            const Connection* owner = connection_by_id(document->connection_id());
            const sql::Dialect& dialect =
                owner != nullptr ? sql::dialect_for(owner->profile.driver_id)
                                 : active_dialect();
            const sql::PagedQuery inner = sql::make_unpaged_query(
                document->paged_sql(), dialect, {}, others);

            Session& target = session_for(*document);
            if (inner.rewritten && target.state() == SessionState::connected &&
                !target.busy()) {
                grid_distinct_loading_ = true;
                grid_distinct_partial_ = false;
                distinct_document_id_  = document->id();
                document->set_executing(true);
                target.execute_async(
                    db::distinct_query(inner.sql, rs.column(column).info().name));
            } else {
                grid_distinct_         = db::distinct_values(rs, column);
                grid_distinct_loading_ = false;
                grid_distinct_partial_ = true;
            }
            return;
        }
        case Command::grid_filter_settings:
            open_filter_settings(*document, rs);
            return;
        case Command::grid_focus_filter:
            view.focus_filter = true;
            return;
        case Command::grid_filter_clear:
            document->set_sort({});
            view.filter_text[0] = '\0';
            apply_grid_filter(*document, {});
            return;
        case Command::grid_filter_save:
            save_default_filter(*document, rs);
            return;
        case Command::grid_filter_reset:
            load_default_filter(*document, rs);
            return;
        case Command::grid_toggle_order: {
            // Tres estados, os do clique no cabecalho: ascendente,
            // descendente, sem ordem. Sem o terceiro nao haveria como voltar
            // a' ordem do servidor pelo teclado.
            const std::string& name = rs.column(column).info().name;
            sql::SortOrder order;
            if (document->sort().column != name) {
                order = sql::SortOrder{name, false};
            } else if (!document->sort().descending) {
                order = sql::SortOrder{name, true};
            }
            document->set_sort(std::move(order));
            execute_page(*document, 0);
            return;
        }

        // --- Dados ----------------------------------------------------------------
        case Command::grid_fetch_page:
            execute_page(*document, document->page() + 1);
            return;
        case Command::grid_fetch_all: {
            // Confirma quando se SABE que e' grande. Total desconhecido nao
            // dispara o aviso: perguntar sem poder dizer "quantas" seria um
            // alarme sem informacao (ADR 0011).
            constexpr std::size_t kConfirmAbove = 100000;
            const bool big = document->total_rows().has_value() &&
                             *document->total_rows() > kConfirmAbove;
            if (big) confirm_fetch_all_ = document->id();
            else     fetch_all_rows(*document);
            return;
        }
        case Command::grid_count:
            count_total_rows(*document);
            return;
        case Command::grid_export:
            show_export_ = true;
            export_status_.clear();
            return;
        case Command::grid_open_with: {
            namespace fs = std::filesystem;
            db::ExportOptions options;
            options.format = db::ExportFormat::csv;

            std::error_code ec;
            const fs::path path = fs::temp_directory_path(ec) /
                                  ("c-otter-result-" +
                                   std::to_string(document->id()) + ".csv");
            if (ec || !db::export_to_file(rs, options, path.string())) {
                show_toast(TR("could not write the temporary file"));
                return;
            }
            if (!open_path(path.string())) {
                show_toast(TR("no application is associated with CSV files"));
            }
            return;
        }
        case Command::grid_generate_script:
            grid_popup_request_ = GridPopup::generate;
            return;

        // --- Apresentacao e paineis -----------------------------------------------
        case Command::grid_toggle_mode:
            document->set_record_mode(!document->record_mode());
            return;
        case Command::grid_switch_presentation:
            view.presentation = view.presentation == GridPresentation::grid
                                    ? GridPresentation::text
                                    : GridPresentation::grid;
            return;
        case Command::grid_toggle_preview:
        case Command::grid_panel_value:
            if (value_panel_.open) value_panel_.open = false;
            else if (has_cell)     open_value_panel(*document, rs, row, column);
            return;
        case Command::grid_activate_preview:
            if (!value_panel_.open && has_cell) {
                open_value_panel(*document, rs, row, column);
            }
            ImGui::SetWindowFocus("###ValuePanel");
            return;
        case Command::grid_panel_references:
            show_grid_references_ = !show_grid_references_;
            return;
        case Command::grid_panel_metadata:
            show_grid_metadata_ = !show_grid_metadata_;
            return;
        case Command::grid_panel_calc:
            show_grid_calc_ = !show_grid_calc_;
            return;
        case Command::grid_panel_grouping: {
            db::GroupSpec& spec = document->group_spec();
            if (!spec.group_by.empty()) spec = {};
            else                        spec.group_by.push_back(column);
            recompute_groups(*document);
            return;
        }
        case Command::grid_switch_viewer:
            cycle_value_viewer();
            return;
        case Command::grid_zoom_in:
            view.zoom = std::min(kZoomMax, view.zoom * kZoomStep);
            return;
        case Command::grid_zoom_out:
            view.zoom = std::max(kZoomMin, view.zoom / kZoomStep);
            return;

        default:
            return;
    }
}

// --- Teclado -----------------------------------------------------------------------

// As setas, Home/End e PageUp/PageDown sao tratadas aqui; todo o resto vem da
// tabela de comandos, com a tecla do perfil ativo.
//
// So' age quando a JANELA do resultado tem foco. Sem isso `Alt+Delete`
// marcaria uma linha para exclusao enquanto o usuario digita no editor SQL --
// e' a razao de o DBeaver prender estes atalhos ao contexto
// `resultset.focused`.
void MainShell::handle_grid_keys(SqlDocument& document, const db::ResultSet& rs) {
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // Um campo de texto da propria janela (barra de filtro, linha nova) esta'
    // recebendo teclado: Enter, Esc, Ctrl+C e Ctrl+A sao dele.
    const bool typing = ImGui::GetIO().WantTextInput;

    // Anota para o PROXIMO quadro quem fica com as setas. Ver draw(): a
    // navegacao do ImGui consome a tecla dentro do NewFrame.
    grid_owns_arrows_ = focused && !editing_active_ && !typing;
    grid_focused_     = focused && !typing;

    if (!focused || typing || editing_active_) return;
    if (ImGui::GetTopMostPopupModal() != nullptr) return;

    // Os comandos. Antes da navegacao: Ctrl+Alt+seta ("proxima linha") nao
    // pode ser lido tambem como seta simples.
    dispatch_shortcuts(CommandContext::grid);

    if (rs.row_count() == 0 || rs.column_count() == 0) return;

    const ImGuiIO& io = ImGui::GetIO();
    const auto pressed = [](ImGuiKey key) {
        // Repeticao LIGADA: segurar a seta para descer varias linhas e' o
        // comportamento de qualquer grade.
        return ImGui::IsKeyPressed(key, /*repeat=*/true);
    };

    // Setas com Ctrl ou Alt sao comandos (linha anterior, mover coluna).
    const bool plain = !io.KeyCtrl && !io.KeyAlt;
    const bool extend = io.KeyShift;

    const bool any_arrow = plain && (pressed(ImGuiKey_DownArrow) ||
                                     pressed(ImGuiKey_UpArrow) ||
                                     pressed(ImGuiKey_LeftArrow) ||
                                     pressed(ImGuiKey_RightArrow));

    // Primeira tecla sem selecao comeca no canto, em vez de nao fazer nada.
    if (!has_selection_ || selected_document_ != document.id()) {
        if (any_arrow) {
            const GridView& view = document.grid_view();
            select_grid_cell(document, 0, view.order.empty() ? 0 : view.order.front(),
                             false);
            scroll_to_selection_ = true;
        }
        return;
    }

    const std::size_t last_row = rs.row_count() - 1;

    // A ordem da TELA: o usuario pode ter arrastado e escondido colunas, e
    // "direita" e' a coluna que esta' a' direita.
    std::vector<std::size_t> order = document.grid_view().order;
    if (order.empty()) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) order.push_back(c);
    }
    std::size_t position = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (order[i] == selected_column_) { position = i; break; }
    }
    const std::size_t last_position = order.size() - 1;

    std::size_t row = std::min(selected_row_, last_row);

    // Saturar nas bordas, nao dar a volta: uma seta para baixo na ultima
    // linha que pula para a primeira faz perder o lugar sem aviso.
    if (plain) {
        if (pressed(ImGuiKey_DownArrow))  row = std::min(row + 1, last_row);
        if (pressed(ImGuiKey_UpArrow))    row = row > 0 ? row - 1 : 0;
        if (pressed(ImGuiKey_RightArrow)) position = std::min(position + 1, last_position);
        if (pressed(ImGuiKey_LeftArrow))  position = position > 0 ? position - 1 : 0;
    }

    // Home/End andam na LINHA; com Ctrl, no resultado inteiro -- a convencao
    // de toda planilha.
    if (!io.KeyAlt) {
        if (pressed(ImGuiKey_Home)) { position = 0; if (io.KeyCtrl) row = 0; }
        if (pressed(ImGuiKey_End))  { position = last_position; if (io.KeyCtrl) row = last_row; }
    }

    // Uma "pagina" e' o que cabe na tela, nao a pagina do resultado.
    const std::size_t screen_rows = std::max<std::size_t>(
        1, static_cast<std::size_t>(ImGui::GetContentRegionAvail().y /
                                    std::max(ImGui::GetTextLineHeightWithSpacing(), 1.0f)));
    if (plain) {
        if (pressed(ImGuiKey_PageDown)) row = std::min(row + screen_rows, last_row);
        if (pressed(ImGuiKey_PageUp))   row = row > screen_rows ? row - screen_rows : 0;
    }

    const std::size_t column = order[position];
    if (row != selected_row_ || column != selected_column_) {
        select_grid_cell(document, row, column, extend);
        scroll_to_selection_ = true;
    }
}

// --- Menu de contexto da celula ----------------------------------------------------

// O conteudo do menu, na ordem do `ResultSetViewer.fillContextMenu` do
// DBeaver: valor, edicao, copia, linha, filtros, navegacao, disposicao,
// dados. Todos os itens vem da tabela -- o menu mostra a tecla do perfil
// ativo e desabilita o que nao se aplica, dizendo por que.
void MainShell::draw_grid_cell_menu(SqlDocument& document, const db::ResultSet& rs) {
    const std::size_t row    = std::min(selected_row_, rs.row_count() - 1);
    const std::size_t column = std::min(selected_column_, rs.column_count() - 1);
    const db::ColumnInfo& info = rs.column(column).info();

    command_menu_item(Command::grid_panel_value);
    command_menu_item(Command::grid_row_edit);
    command_menu_item(Command::grid_row_edit_inline);
    command_menu_item(Command::grid_set_null);
    command_menu_item(Command::grid_set_default);
    command_menu_item(Command::grid_cell_reset);

    ImGui::Separator();
    command_menu_item(Command::grid_copy);
    if (ImGui::BeginMenu(TR("Advanced copy"))) {
        command_menu_item(Command::grid_copy_as);
        command_menu_item(Command::grid_copy_column_names);
        command_menu_item(Command::grid_copy_row_names);
        ImGui::Separator();
        command_menu_item(Command::grid_select_row);
        command_menu_item(Command::grid_select_column);
        command_menu_item(Command::grid_select_all);
        ImGui::EndMenu();
    }
    command_menu_item(Command::grid_paste);

    ImGui::Separator();
    if (ImGui::BeginMenu(TR("Edit"))) {
        command_menu_item(Command::grid_row_add);
        command_menu_item(Command::grid_row_add_before);
        command_menu_item(Command::grid_row_copy);
        command_menu_item(Command::grid_row_copy_before);
        command_menu_item(Command::grid_row_delete);
        ImGui::Separator();
        command_menu_item(Command::grid_copy_above);
        command_menu_item(Command::grid_copy_below);
        ImGui::Separator();
        command_menu_item(Command::grid_apply);
        command_menu_item(Command::grid_apply_commit);
        command_menu_item(Command::grid_reject);
        command_menu_item(Command::grid_confirm_save);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Filters"))) {
        // Os filtros "pelo valor desta celula" do DBeaver. O valor aparece
        // no item: escolher "= 7" sem ver o 7 seria escolher no escuro.
        const db::CellValue value =
            db::cell_value(rs, document.edits(), row, column);
        const bool can = command_enabled(Command::grid_filter_menu);

        const auto by_value = [&](const std::string& expression) {
            const std::string label = info.name + " " + shorten(expression);
            if (ImGui::MenuItem(label.c_str(), nullptr, false, can)) {
                sql::ColumnFilter filter = document.filter();
                filter.set_column(info.name, expression);
                // Enfileirado como o resto do menu: executar aqui trocaria o
                // resultado no meio do desenho da grade.
                pending_filter_          = std::move(filter);
                pending_filter_document_ = document.id();
            }
        };

        if (value.is_null) {
            by_value("IS NULL");
            by_value("IS NOT NULL");
        } else {
            const std::string literal = db::sql_literal(info, value.text, false);
            by_value("= " + literal);
            by_value("<> " + literal);
            by_value("> " + literal);
            by_value("< " + literal);
            if (!db::is_right_aligned(info.kind) &&
                info.kind != db::DataKind::boolean) {
                by_value("LIKE " + db::sql_literal(info, "%" + value.text + "%", false));
            }
            by_value("IS NULL");
            by_value("IS NOT NULL");
        }

        ImGui::Separator();
        command_menu_item(Command::grid_filter_distinct);
        command_menu_item(Command::grid_filter_menu);
        command_menu_item(Command::grid_filter_settings);
        command_menu_item(Command::grid_focus_filter);
        ImGui::Separator();
        command_menu_item(Command::grid_filter_clear);
        command_menu_item(Command::grid_filter_save);
        command_menu_item(Command::grid_filter_reset);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Navigate"))) {
        command_menu_item(Command::grid_navigate_link);
        command_menu_item(Command::grid_references);
        ImGui::Separator();
        command_menu_item(Command::grid_goto_row);
        command_menu_item(Command::grid_goto_column);
        ImGui::Separator();
        command_menu_item(Command::grid_row_first);
        command_menu_item(Command::grid_row_previous);
        command_menu_item(Command::grid_row_next);
        command_menu_item(Command::grid_row_last);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Layout"))) {
        command_menu_item(Command::grid_toggle_mode);
        command_menu_item(Command::grid_switch_presentation);
        ImGui::Separator();
        command_menu_item(Command::grid_toggle_order);
        command_menu_item(Command::grid_column_menu);
        command_menu_item(Command::grid_move_left);
        command_menu_item(Command::grid_move_right);
        command_menu_item(Command::grid_hide_columns);
        command_menu_item(Command::grid_show_columns);
        command_menu_item(Command::grid_hide_empty);
        command_menu_item(Command::grid_fit_values);
        command_menu_item(Command::grid_fit_screen);
        ImGui::Separator();
        command_menu_item(Command::grid_row_color);
        command_menu_item(Command::grid_zoom_in);
        command_menu_item(Command::grid_zoom_out);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Panels"))) {
        command_menu_item(Command::grid_panel_value);
        command_menu_item(Command::grid_panel_references);
        command_menu_item(Command::grid_panel_metadata);
        command_menu_item(Command::grid_panel_grouping);
        command_menu_item(Command::grid_panel_calc);
        ImGui::Separator();
        command_menu_item(Command::grid_activate_preview);
        command_menu_item(Command::grid_switch_viewer);
        ImGui::EndMenu();
    }

    ImGui::Separator();
    command_menu_item(Command::grid_export);
    command_menu_item(Command::grid_open_with);
    command_menu_item(Command::grid_generate_script);
    ImGui::Separator();
    command_menu_item(Command::grid_count);
    command_menu_item(Command::grid_fetch_page);
    command_menu_item(Command::grid_fetch_all);
}

// --- Dentro da tabela --------------------------------------------------------------

// Atende, com a tabela do ImGui aberta, o que os comandos pediram: esconder,
// mover, ajustar largura. E copia de volta a ordem das colunas na tela.
void MainShell::apply_grid_table_requests(SqlDocument& document,
                                          const db::ResultSet& rs, int columns) {
    GridView& view = document.grid_view();
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (table == nullptr) return;

    for (int c = 0; c < columns; ++c) {
        ImGui::TableSetColumnEnabled(c, !view.is_hidden(static_cast<std::size_t>(c)));
    }

    if (view.move_column < static_cast<std::size_t>(columns) && view.move_delta != 0) {
        const int index = static_cast<int>(view.move_column);
        const int from  = table->Columns[index].DisplayOrder;

        // O vizinho VISIVEL naquela direcao: pular por cima de uma coluna
        // escondida nao muda nada na tela, e a tecla pareceria nao funcionar.
        int to = from + view.move_delta;
        while (to >= 0 && to < columns &&
               view.is_hidden(static_cast<std::size_t>(table->DisplayOrderToIndex[to]))) {
            to += view.move_delta;
        }
        if (to >= 0 && to < columns) {
            ImGui::TableQueueSetColumnDisplayOrder(table, index, to);
        }
    }
    view.move_column = static_cast<std::size_t>(-1);
    view.move_delta  = 0;

    if (view.fit_values) {
        ImGui::TableSetColumnWidthAutoAll(table);
        view.fit_values = false;
    }
    if (view.fit_screen) {
        const std::size_t visible = rs.column_count() > view.hidden_count()
                                        ? static_cast<std::size_t>(columns) -
                                              std::min(view.hidden_count(),
                                                       static_cast<std::size_t>(columns) - 1)
                                        : 1;
        const float width = std::max(
            40.0f, (ImGui::GetContentRegionAvail().x -
                    ImGui::GetStyle().ScrollbarSize) /
                       static_cast<float>(std::max<std::size_t>(visible, 1)) -
                       ImGui::GetStyle().CellPadding.x * 2.0f);
        for (int c = 0; c < columns; ++c) {
            if (!view.is_hidden(static_cast<std::size_t>(c))) {
                ImGui::TableSetColumnWidth(c, width);
            }
        }
        view.fit_screen = false;
    }

    view.order.clear();
    for (int position = 0; position < columns; ++position) {
        const auto index = static_cast<std::size_t>(table->DisplayOrderToIndex[position]);
        if (!view.is_hidden(index)) view.order.push_back(index);
    }
}

// A cor de fundo da linha, por "Set row color". Zero = nenhuma.
std::uint32_t MainShell::grid_row_color(const SqlDocument& document,
                                        const db::ResultSet& rs, std::size_t row) const {
    for (const GridRowColor& rule : document.grid_view().row_colors) {
        const auto index = rs.find_column(rule.column);
        if (!index) continue;
        if (rule.is_null) {
            if (rs.is_null(row, *index)) return rule.color;
        } else if (!rs.is_null(row, *index) && rs.text(row, *index) == rule.value) {
            return rule.color;
        }
    }
    return 0;
}

// --- Apresentacao em texto ---------------------------------------------------------

// O resultado como texto puro, colunas alinhadas com espacos -- a
// apresentacao "Text" do DBeaver. Serve para selecionar com o mouse e colar
// num e-mail ou num chamado sem perder o alinhamento.
void MainShell::draw_grid_text(SqlDocument& document, const db::ResultSet& rs) {
    const GridView& view = document.grid_view();

    // Largura de cada coluna, em caracteres, com teto: um JSON de 4 KB numa
    // celula nao pode empurrar as outras colunas para fora da tela.
    constexpr std::size_t kMaxWidth = 48;
    // Medir todas as linhas a cada quadro custaria caro num resultado
    // grande; as primeiras bastam para escolher a largura.
    constexpr std::size_t kSampleRows = 500;

    std::vector<std::size_t> columns = view.order;
    if (columns.empty()) {
        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (!view.is_hidden(c)) columns.push_back(c);
        }
    }

    const std::string null_text = editor_options_for(document).null_text;

    const auto cell_text = [&](std::size_t row, std::size_t column) {
        const db::CellValue value = db::cell_value(rs, document.edits(), row, column);
        std::string text = value.is_null ? null_text : value.text;
        for (char& c : text) {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }
        if (text.size() > kMaxWidth) text = text.substr(0, kMaxWidth - 3) + "...";
        return text;
    };

    std::vector<std::size_t> widths;
    widths.reserve(columns.size());
    for (const std::size_t c : columns) {
        std::size_t width = std::min(rs.column(c).info().name.size(), kMaxWidth);
        for (std::size_t r = 0; r < std::min(rs.row_count(), kSampleRows); ++r) {
            width = std::max(width, cell_text(r, c).size());
        }
        widths.push_back(width);
    }

    const auto pad = [](std::string text, std::size_t width, bool right) {
        if (text.size() >= width) return text;
        const std::string spaces(width - text.size(), ' ');
        return right ? spaces + text : text + spaces;
    };

    ImFont* mono = mono_font();
    if (mono != nullptr) ImGui::PushFont(mono, ImGui::GetStyle().FontSizeBase * view.zoom);

    ImGui::BeginChild("##gridtext", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    std::string header;
    std::string rule;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) { header += " | "; rule += "-+-"; }
        header += pad(rs.column(columns[i]).info().name.substr(0, kMaxWidth),
                      widths[i], false);
        rule += std::string(widths[i], '-');
    }
    ImGui::TextColored(col4(colors().accent_light), "%s", header.c_str());
    ImGui::TextColored(col4(colors().text_dim), "%s", rule.c_str());

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rs.row_count()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            std::string line;
            for (std::size_t i = 0; i < columns.size(); ++i) {
                if (i > 0) line += " | ";
                const bool right =
                    db::is_right_aligned(rs.column(columns[i]).info().kind);
                line += pad(cell_text(static_cast<std::size_t>(r), columns[i]),
                            widths[i], right);
            }
            ImGui::TextUnformatted(line.c_str());
        }
    }

    // O texto inteiro para a area de transferencia: selecionar com o mouse
    // num texto desenhado linha a linha nao existe no ImGui.
    if (ImGui::BeginPopupContextWindow("##gridtextmenu")) {
        if (ImGui::MenuItem(TR("Copy all as text"))) {
            std::string all = header + "\n" + rule + "\n";
            for (std::size_t r = 0; r < rs.row_count(); ++r) {
                for (std::size_t i = 0; i < columns.size(); ++i) {
                    if (i > 0) all += " | ";
                    all += pad(cell_text(r, columns[i]), widths[i],
                               db::is_right_aligned(rs.column(columns[i]).info().kind));
                }
                all += '\n';
            }
            ImGui::SetClipboardText(all.c_str());
        }
        command_menu_item(Command::grid_switch_presentation);
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    if (mono != nullptr) ImGui::PopFont();
}

// --- Popups ------------------------------------------------------------------------

// Os menus que um comando abre: referencias, "copy as", gerar script, cor da
// linha e filtro por valor. Abertos AQUI, dentro da janela do resultado: um
// popup do ImGui pertence a' janela em que foi aberto, e o comando pode ter
// vindo do teclado, de fora de qualquer desenho.
void MainShell::draw_grid_popups(SqlDocument& document, const db::ResultSet& rs) {
    // Filtro escolhido no menu de contexto no quadro anterior.
    if (pending_filter_document_ == document.id()) {
        pending_filter_document_ = 0;
        apply_grid_filter(document, std::move(pending_filter_));
        pending_filter_ = {};
    }

    // Comando que esperava as chaves da tabela.
    if (grid_pending_command_ != Command::count &&
        !session_for(document).busy()) {
        const Command pending = grid_pending_command_;
        run_grid_command(pending);
    }

    switch (grid_popup_request_) {
        case GridPopup::references: ImGui::OpenPopup("##gridrefs");     break;
        case GridPopup::copy_as:    ImGui::OpenPopup("##gridcopyas");   break;
        case GridPopup::generate:   ImGui::OpenPopup("##gridgenerate"); break;
        case GridPopup::row_color:  ImGui::OpenPopup("##gridrowcolor"); break;
        case GridPopup::distinct:   ImGui::OpenPopup("##griddistinct"); break;
        case GridPopup::none:       break;
    }
    if (grid_popup_request_ != GridPopup::none) grid_popup_last_ = grid_popup_request_;
    grid_popup_request_ = GridPopup::none;

    // Posiciona o menu sob a celula corrente. So' antes do BeginPopup DELE:
    // um BeginPopup que nao abre descarta a posicao pedida.
    const auto place = [this](GridPopup kind) {
        if (grid_popup_last_ != kind) return;
        if (selected_cell_x_ <= 0.0f && selected_cell_y_ <= 0.0f) return;
        ImGui::SetNextWindowPos(ImVec2(selected_cell_x_, selected_cell_y_ + 2.0f),
                                ImGuiCond_Appearing);
    };

    const Palette& p = colors();
    const db::GridSelection selection = grid_selection(document, rs);

    // --- Referencias --------------------------------------------------------------
    place(GridPopup::references);
    if (ImGui::BeginPopup("##gridrefs")) {
        ImGui::TextColored(col4(p.text_dim), "%s", TR("Rows that reference this one"));
        ImGui::Separator();
        for (const db::LinkQuery& link : grid_links_) {
            if (ImGui::MenuItem(link.title.c_str())) {
                open_sql_tab(link.sql, /*run=*/true);
            }
            if (ImGui::IsItemHovered()) Hint(link.title).code(link.sql).show();
        }
        ImGui::EndPopup();
    }

    // --- Copy as ------------------------------------------------------------------
    place(GridPopup::copy_as);
    if (ImGui::BeginPopup("##gridcopyas")) {
        ImGui::TextColored(col4(p.text_dim), TR("%zu row(s) x %zu column(s)"),
                           selection.row_count(), selection.columns.size());
        ImGui::Separator();

        const auto copy_with = [&](db::ExportFormat format) {
            db::ExportOptions options;
            options.format = format;
            // Para a area de transferencia nao ha' planilha abrindo o
            // arquivo: o apostrofo de protecao so' sujaria o valor colado.
            options.escape_formulas = false;
            if (!document.edit_target().table.empty()) {
                options.table_name = db::qualified_name(
                    document.edit_target().schema, document.edit_target().table);
            }
            const db::ResultSet part = db::slice(rs, document.edits(), selection);
            ImGui::SetClipboardText(db::export_to_string(part, options).c_str());
        };

        if (ImGui::MenuItem(TR("Tab-separated, with header"))) {
            ImGui::SetClipboardText(
                db::selection_to_text(rs, document.edits(), selection, true).c_str());
        }
        if (ImGui::MenuItem("CSV"))          copy_with(db::ExportFormat::csv);
        if (ImGui::MenuItem("Markdown"))     copy_with(db::ExportFormat::markdown);
        if (ImGui::MenuItem("JSON"))         copy_with(db::ExportFormat::json);
        if (ImGui::MenuItem("SQL INSERT"))   copy_with(db::ExportFormat::sql_insert);
        ImGui::EndPopup();
    }

    // --- Gerar script -------------------------------------------------------------
    place(GridPopup::generate);
    if (ImGui::BeginPopup("##gridgenerate")) {
        ImGui::TextColored(col4(p.text_dim), TR("%zu row(s)"), selection.row_count());
        ImGui::Separator();

        const auto generate = [&](const char* label, db::RowScript kind) {
            if (!ImGui::MenuItem(label)) return;
            const auto script = db::row_script(rs, document.edits(),
                                               document.edit_target(), selection, kind);
            if (script) open_sql_tab(*script, /*run=*/false);
            // A recusa diz o motivo (sem chave, chave nula): gerar um DELETE
            // sem WHERE completo seria pior que nao gerar.
            else        show_toast(TR(script.error().message().c_str()));
        };
        generate("SELECT", db::RowScript::select_by_key);
        generate("INSERT", db::RowScript::insert);
        generate("UPDATE", db::RowScript::update);
        generate("DELETE", db::RowScript::delete_by_key);
        ImGui::EndPopup();
    }

    // --- Cor da linha -------------------------------------------------------------
    place(GridPopup::row_color);
    if (ImGui::BeginPopup("##gridrowcolor")) {
        GridView& view = document.grid_view();
        const std::size_t row    = std::min(selected_row_, rs.row_count() - 1);
        const std::size_t column = std::min(selected_column_, rs.column_count() - 1);
        const std::string& name  = rs.column(column).info().name;
        const bool is_null = rs.is_null(row, column);
        const std::string value = is_null ? std::string{}
                                          : std::string(rs.text(row, column));

        ImGui::TextColored(col4(p.text_dim), "%s = %s", name.c_str(),
                           is_null ? "NULL" : shorten(value, 28).c_str());
        ImGui::Separator();

        const auto same = [&](const GridRowColor& rule) {
            return rule.column == name && rule.is_null == is_null && rule.value == value;
        };

        for (std::size_t i = 0; i < std::size(kRowPalette); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (i > 0) ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::ColorButton("##rowcolor",
                                   ImGui::ColorConvertU32ToFloat4(kRowPalette[i]),
                                   ImGuiColorEditFlags_NoTooltip, ImVec2(22, 22))) {
                std::erase_if(view.row_colors, same);
                view.row_colors.push_back({name, value, is_null,
                                           with_alpha(kRowPalette[i], 0.30f)});
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TR("Reset the color of this value"), nullptr, false,
                            std::any_of(view.row_colors.begin(),
                                        view.row_colors.end(), same))) {
            std::erase_if(view.row_colors, same);
        }
        if (ImGui::MenuItem(TR("Reset all row colors"), nullptr, false,
                            !view.row_colors.empty())) {
            view.row_colors.clear();
        }
        ImGui::EndPopup();
    }

    // --- Filtrar por valor --------------------------------------------------------
    place(GridPopup::distinct);
    if (ImGui::BeginPopup("##griddistinct")) {
        const std::size_t column = std::min(grid_distinct_column_, rs.column_count() - 1);
        const db::ColumnInfo& info = rs.column(column).info();

        ImGui::TextColored(col4(p.accent_light), "%s", info.name.c_str());
        if (grid_distinct_loading_) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("  loading..."));
        } else {
            if (grid_distinct_partial_) {
                ImGui::TextColored(col4(p.warn), "%s", TR("values of the loaded rows only"));
            }
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(280.0f);
            ImGui::InputTextWithHint("##distinctsearch", TR("type to narrow the list"),
                                     grid_distinct_search_, sizeof grid_distinct_search_);

            ImGui::BeginChild("##distinctlist", ImVec2(280.0f, 260.0f));
            const std::string_view needle = grid_distinct_search_;
            for (std::size_t i = 0; i < grid_distinct_.size(); ++i) {
                const db::DistinctValue& value = grid_distinct_[i];
                if (!needle.empty() &&
                    value.text.find(needle) == std::string::npos) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(i));
                const std::string label =
                    (value.is_null ? std::string("[NULL]") : shorten(value.text, 30)) +
                    "  (" + std::to_string(value.count) + ")";
                if (ImGui::Selectable(label.c_str())) {
                    sql::ColumnFilter filter = document.filter();
                    filter.set_column(info.name,
                                      db::equals_expression(info, value.text,
                                                            value.is_null));
                    pending_filter_          = std::move(filter);
                    pending_filter_document_ = document.id();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
            if (grid_distinct_.empty()) {
                ImGui::TextColored(col4(p.text_dim), "%s", TR("no values"));
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
}

// --- Filtros: dialogo e padrao salvo -----------------------------------------------

void MainShell::open_filter_settings(SqlDocument& document, const db::ResultSet& rs) {
    filter_form_ = FilterForm{};
    filter_form_.open        = true;
    filter_form_.document_id = document.id();

    const GridView& view = document.grid_view();
    const sql::ColumnFilter& filter = document.filter();

    filter_form_.rows.resize(rs.column_count());
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        FilterForm::Row& row = filter_form_.rows[c];
        row.name    = rs.column(c).info().name;
        row.visible = !view.is_hidden(c);
        std::snprintf(row.criteria, sizeof row.criteria, "%s",
                      std::string(filter.expression_for(row.name)).c_str());
    }
    std::snprintf(filter_form_.where, sizeof filter_form_.where, "%s",
                  filter.condition.c_str());

    filter_form_.sort_column = 0;   // 0 = sem ordem
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (rs.column(c).info().name == document.sort().column) {
            filter_form_.sort_column = static_cast<int>(c) + 1;
        }
    }
    filter_form_.sort_descending = document.sort().descending;
}

// "Customize filters": visibilidade, criterio por coluna, condicao livre e
// ordem, num lugar so' -- o `FilterSettingsDialog` do DBeaver.
void MainShell::draw_filter_settings() {
    if (!filter_form_.open) return;

    SqlDocument* document = nullptr;
    for (std::unique_ptr<SqlDocument>& d : documents_) {
        if (d->id() == filter_form_.document_id) { document = d.get(); break; }
    }
    if (document == nullptr || !document->result().has_value() ||
        document->result()->column_count() != filter_form_.rows.size()) {
        filter_form_.open = false;
        return;
    }

    const Palette& p = colors();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    ImGui::SetNextWindowSize(ImVec2(620, 520), ImGuiCond_Appearing);
    const bool open = ImGui::Begin(TRW("Result filter settings", "###FilterSettings"),
                                   &filter_form_.open, ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();

    if (open) {
        const float footer = ImGui::GetFrameHeightWithSpacing() * 5.6f;

        if (ImGui::BeginTable("##filtercols", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY,
                              ImVec2(0, -footer))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR("Column"), ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableSetupColumn(TR("Visible"), ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn(TR("Criteria"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (std::size_t i = 0; i < filter_form_.rows.size(); ++i) {
                FilterForm::Row& row = filter_form_.rows[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(row.name.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Checkbox("##visible", &row.visible);

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##criteria", "> 100   |   IS NULL",
                                         row.criteria, sizeof row.criteria);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        ImGui::TextColored(col4(p.text_dim), "%s", TR("Custom WHERE"));
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##filterwhere", filter_form_.where, sizeof filter_form_.where);

        ImGui::TextColored(col4(p.text_dim), "%s", TR("Order by"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260.0f);
        const char* current =
            filter_form_.sort_column == 0
                ? TR("(server order)")
                : filter_form_.rows[static_cast<std::size_t>(
                                        filter_form_.sort_column - 1)].name.c_str();
        if (ImGui::BeginCombo("##filterorder", current)) {
            if (ImGui::Selectable(TR("(server order)"), filter_form_.sort_column == 0)) {
                filter_form_.sort_column = 0;
            }
            for (std::size_t i = 0; i < filter_form_.rows.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(filter_form_.rows[i].name.c_str(),
                                      filter_form_.sort_column ==
                                          static_cast<int>(i) + 1)) {
                    filter_form_.sort_column = static_cast<int>(i) + 1;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Checkbox(TR("Descending"), &filter_form_.sort_descending);

        ImGui::Spacing();
        const bool can_run = session_for(*document).state() == SessionState::connected &&
                             !session_for(*document).busy();

        // Uma coluna visivel, no minimo: sem nenhuma a grade some, e o menu
        // que a traria de volta mora no cabecalho dela.
        bool any_visible = false;
        for (const FilterForm::Row& row : filter_form_.rows) any_visible |= row.visible;

        ImGui::BeginDisabled(!can_run || !any_visible);
        if (ImGui::Button(TR("OK"), ImVec2(110, 0))) {
            GridView& view = document->grid_view();
            sql::ColumnFilter filter;
            for (std::size_t i = 0; i < filter_form_.rows.size(); ++i) {
                const FilterForm::Row& row = filter_form_.rows[i];
                view.set_hidden(i, !row.visible);
                filter.set_column(row.name, row.criteria);
            }
            filter.condition = filter_form_.where;
            std::snprintf(view.filter_text, sizeof view.filter_text, "%s",
                          filter_form_.where);

            sql::SortOrder order;
            if (filter_form_.sort_column > 0) {
                order.column = filter_form_.rows[static_cast<std::size_t>(
                                                     filter_form_.sort_column - 1)].name;
                order.descending = filter_form_.sort_descending;
            }
            document->set_sort(std::move(order));

            // Sem consulta refazivel, vale so' a visibilidade.
            if (!document->paged_sql().empty()) {
                apply_grid_filter(*document, std::move(filter));
            }
            filter_form_.open = false;
        }
        ImGui::EndDisabled();
        if (!any_visible) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.warn), "%s", TR("keep at least one column visible"));
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Reset"), ImVec2(110, 0))) {
            for (FilterForm::Row& row : filter_form_.rows) {
                row.visible     = true;
                row.criteria[0] = '\0';
            }
            filter_form_.where[0]        = '\0';
            filter_form_.sort_column     = 0;
            filter_form_.sort_descending = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(110, 0))) filter_form_.open = false;
    }
    ImGui::End();
}

// A chave do filtro padrao: a tabela, dentro do servidor e do banco. Sem o
// servidor, `public.cliente` de producao e de teste dividiriam o filtro.
std::string MainShell::default_filter_key(const SqlDocument& document) const {
    const Connection* owner = connection_by_id(document.connection_id());
    const db::EditTarget& target = document.edit_target();
    if (owner == nullptr || target.table.empty()) return {};

    return owner->profile.driver_id + "|" + owner->profile.host + ":" +
           std::to_string(owner->profile.port) + "/" + owner->profile.database +
           "|" + target.schema + "." + target.table;
}

// "Save as default filter": grava criterios, ordem e colunas escondidas da
// tabela em `.C-Otter/grid-filters.json`.
void MainShell::save_default_filter(SqlDocument& document, const db::ResultSet& rs) {
    const std::string key = default_filter_key(document);
    if (key.empty()) return;

    json::Object root;
    {
        std::ifstream in(filters_path(), std::ios::binary);
        std::ostringstream text;
        if (in) text << in.rdbuf();
        if (const auto parsed = json::parse(text.str());
            parsed && parsed->is_object()) {
            root = parsed->as_object();
        }
    }

    const sql::ColumnFilter& filter = document.filter();
    const GridView& view = document.grid_view();

    json::Object columns;
    if (!filter.column.empty() && !filter.expression.empty()) {
        columns[filter.column] = json::Value(filter.expression);
    }
    for (const sql::ColumnFilter::Criterion& criterion : filter.more) {
        if (!criterion.column.empty() && !criterion.expression.empty()) {
            columns[criterion.column] = json::Value(criterion.expression);
        }
    }

    json::Array hidden;
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (view.is_hidden(c)) hidden.emplace_back(rs.column(c).info().name);
    }

    json::Object entry;
    entry["where"]      = json::Value(filter.condition);
    entry["columns"]    = json::Value(std::move(columns));
    entry["sort"]       = json::Value(document.sort().column);
    entry["descending"] = json::Value(document.sort().descending);
    entry["hidden"]     = json::Value(std::move(hidden));
    root[key] = json::Value(std::move(entry));

    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(filters_path()).parent_path(), ec);
    std::ofstream out(filters_path(), std::ios::binary | std::ios::trunc);
    if (out) out << json::serialize(json::Value(std::move(root)));

    show_toast(out ? TR("default filter saved for this table")
                   : TR("could not save the preferences"));
}

// "Reset default filter": volta ao filtro salvo da tabela -- ou a nenhum, se
// nada foi salvo.
void MainShell::load_default_filter(SqlDocument& document, const db::ResultSet& rs) {
    const std::string key = default_filter_key(document);
    if (key.empty()) return;

    sql::ColumnFilter filter;
    sql::SortOrder    order;
    GridView& view = document.grid_view();
    view.hidden.clear();

    std::ifstream in(filters_path(), std::ios::binary);
    std::ostringstream text;
    if (in) text << in.rdbuf();

    bool found = false;
    if (const auto parsed = json::parse(text.str()); parsed && parsed->is_object()) {
        const json::Value& entry = (*parsed)[key];
        if (entry.is_object()) {
            found = true;
            filter.condition = std::string(entry["where"].as_string());
            for (const auto& [name, expression] : entry["columns"].as_object()) {
                filter.set_column(name, expression.as_string());
            }
            order.column     = std::string(entry["sort"].as_string());
            order.descending = entry["descending"].as_bool(false);

            for (const json::Value& name : entry["hidden"].as_array()) {
                if (const auto index = rs.find_column(name.as_string())) {
                    view.set_hidden(*index, true);
                }
            }
        }
    }

    std::snprintf(view.filter_text, sizeof view.filter_text, "%s",
                  filter.condition.c_str());
    document.set_sort(std::move(order));
    apply_grid_filter(document, std::move(filter));

    show_toast(found ? TR("default filter of this table applied")
                     : TR("no default filter saved for this table; filters removed"));
}

// --- Gravar com confirmacao --------------------------------------------------------

// Ponto unico por onde o "aplicar" passa. Com "Show confirmation before
// save" ligado, mostra o SQL antes de executar.
void MainShell::request_save_edits(SqlDocument& document, bool commit_after) {
    if (!document.edits().has_changes() || !document.result().has_value()) return;

    if (!settings_.confirm_data_save) {
        save_pending_edits(document, commit_after);
        return;
    }

    auto statements = db::generate_changes(*document.result(), document.edit_target(),
                                           document.edits());
    if (!statements) {
        document.set_status(statements.error().to_string());
        return;
    }

    save_confirm_.open         = true;
    save_confirm_.document_id  = document.id();
    save_confirm_.commit_after = commit_after;
    save_confirm_.sql.clear();
    for (const std::string& statement : *statements) {
        save_confirm_.sql += statement + "\n\n";
    }
}

void MainShell::draw_save_confirm() {
    if (!save_confirm_.open) return;

    SqlDocument* document = nullptr;
    for (std::unique_ptr<SqlDocument>& d : documents_) {
        if (d->id() == save_confirm_.document_id) { document = d.get(); break; }
    }
    if (document == nullptr) { save_confirm_.open = false; return; }

    constexpr const char* kPopup = "###SaveConfirm";
    ImGui::OpenPopup(kPopup);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal(TRW("Save changes", "###SaveConfirm"), nullptr)) {
        ImGui::TextColored(col4(colors().text_dim), TR("%zu change(s) in %zu row(s)"),
                           document->edits().change_count(),
                           document->edits().touched_rows());

        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.5f;
        ImFont* mono = mono_font();
        if (mono != nullptr) ImGui::PushFont(mono, 0.0f);
        ImGui::BeginChild("##savesql", ImVec2(0, -footer), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(save_confirm_.sql.c_str());
        ImGui::EndChild();
        if (mono != nullptr) ImGui::PopFont();

        const bool can_run = session_for(*document).state() == SessionState::connected &&
                             !session_for(*document).busy();
        ImGui::BeginDisabled(!can_run);
        if (ImGui::Button(TR("Save"), ImVec2(110, 0))) {
            save_pending_edits(*document, save_confirm_.commit_after);
            save_confirm_.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TR("Copy"), ImVec2(110, 0))) {
            ImGui::SetClipboardText(save_confirm_.sql.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(110, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            save_confirm_.open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// --- Paineis -----------------------------------------------------------------------

// Os paineis do resultado alem do de valor: metadados, referencias e
// calculo. Janelas ancoraveis, que nascem junto do resultado.
void MainShell::draw_grid_panels() {
    SqlDocument* document = active_document();
    const bool has_result = document != nullptr && document->result().has_value() &&
                            document->result()->column_count() > 0;
    const Palette& p = colors();

    // --- Metadados ----------------------------------------------------------------
    if (show_grid_metadata_) {
        dock_panel_once(grid_metadata_docked_);
        if (ImGui::Begin(TRW("Metadata", "###GridMetadata"), &show_grid_metadata_)) {
            if (!has_result) {
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   TR("run a query to see the result"));
            } else if (ImGui::BeginTable("##meta", 6,
                                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_ScrollY |
                                             ImGuiTableFlags_Resizable |
                                             ImGuiTableFlags_SizingFixedFit)) {
                const db::ResultSet& rs = *document->result();
                const auto& keys = document->edit_target().key_columns;

                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("#");
                ImGui::TableSetupColumn(TR("Column"));
                ImGui::TableSetupColumn(TR("Type"));
                ImGui::TableSetupColumn(TR("Kind"));
                ImGui::TableSetupColumn(TR("Source"));
                ImGui::TableSetupColumn(TR("Key"));
                ImGui::TableHeadersRow();

                for (std::size_t c = 0; c < rs.column_count(); ++c) {
                    const db::ColumnInfo& info = rs.column(c).info();
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%zu", c + 1);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info.name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(info.type_name.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextColored(col4(p.text_dim), "%s",
                                       std::string(db::to_string(info.kind)).c_str());
                    ImGui::TableSetColumnIndex(4);
                    if (info.has_source()) {
                        // A tabela de origem, quando ha': uma expressao ou um
                        // agregado nao vem de coluna nenhuma.
                        const std::string source =
                            !info.source_table.empty()
                                ? info.source_table + "." + info.source_column_name
                                : document->edit_target().table.empty()
                                      ? std::string("oid ") +
                                            std::to_string(info.source_table_oid)
                                      : document->edit_target().table;
                        ImGui::TextUnformatted(source.c_str());
                    } else {
                        ImGui::TextColored(col4(p.text_dim), "%s", TR("expression"));
                    }
                    ImGui::TableSetColumnIndex(5);
                    if (std::find(keys.begin(), keys.end(), c) != keys.end()) {
                        icon_inline(Icon::key, p.accent);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    // --- Calculo ------------------------------------------------------------------
    if (show_grid_calc_) {
        dock_panel_once(grid_calc_docked_);
        if (ImGui::Begin(TRW("Calc", "###GridCalc"), &show_grid_calc_)) {
            if (!has_result || !has_selection_ ||
                selected_document_ != document->id()) {
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   TR("select cells in the grid"));
            } else {
                const db::ResultSet& rs = *document->result();
                const db::GridSelection selection = grid_selection(*document, rs);
                const db::SelectionStats stats =
                    db::selection_stats(rs, document->edits(), selection);

                if (ImGui::BeginTable("##calc", 2, ImGuiTableFlags_SizingFixedFit)) {
                    const auto line = [&](const char* label, const std::string& value) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextColored(col4(p.text_dim), "%s", label);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(value.c_str());
                    };
                    char number[64];

                    line(TR("Cells"), std::to_string(stats.cells));
                    line(TR("NULL"), std::to_string(stats.nulls));
                    line(TR("Distinct"), std::to_string(stats.distinct));
                    if (stats.numeric) {
                        std::snprintf(number, sizeof number, "%.6g", stats.sum);
                        line(TR("Sum"), number);
                        std::snprintf(number, sizeof number, "%.6g", stats.average);
                        line(TR("Average"), number);
                    }
                    line(TR("Minimum"), stats.minimum);
                    line(TR("Maximum"), stats.maximum);
                    ImGui::EndTable();
                }
                if (!stats.numeric && stats.cells > stats.nulls) {
                    ImGui::TextColored(col4(p.text_dim), "%s",
                                       TR("sum and average need a numeric selection"));
                }
            }
        }
        ImGui::End();
    }

    // --- Referencias --------------------------------------------------------------
    if (show_grid_references_) {
        dock_panel_once(grid_references_docked_);
        if (ImGui::Begin(TRW("References", "###GridReferences"),
                         &show_grid_references_)) {
            if (!has_result || document->edit_target().table.empty()) {
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   TR("the result has no source table"));
            } else {
                const std::optional<db::TableMeta> table = grid_source_table(*document);
                Session& target = session_for(*document);

                if (!table) {
                    ImGui::TextColored(col4(p.text_dim), "%s",
                                       TR("the result has no source table"));
                } else if (!table->keys_loaded) {
                    // Pede uma vez por tabela: sem privilegio o servidor
                    // recusaria a cada quadro.
                    const std::string wanted = document->edit_target().schema + "." +
                                               document->edit_target().table;
                    if (!target.busy() && grid_references_requested_ != wanted) {
                        grid_references_requested_ = wanted;
                        target.load_keys_async(document->edit_target().schema,
                                               document->edit_target().table);
                    }
                    ImGui::TextColored(col4(p.text_dim), "%s", TR("  loading..."));
                } else {
                    const db::ResultSet& rs = *document->result();
                    const bool has_cell = has_selection_ &&
                                          selected_document_ == document->id() &&
                                          selected_row_ < rs.row_count();

                    ImGui::TextColored(col4(p.accent_light), "%s",
                                       document->edit_target().table.c_str());

                    // Para onde esta linha aponta...
                    ImGui::TextColored(col4(p.text_dim), "%s", TR("Foreign keys"));
                    if (table->foreign_keys.empty()) {
                        ImGui::TextColored(col4(p.text_dim), "  %s", TR("none"));
                    }
                    for (const db::ForeignKeyMeta& key : table->foreign_keys) {
                        ImGui::PushID(key.name.c_str());
                        const std::string label = key.source_column + "  \xE2\x86\x92  " +
                                                  key.target_table + "." +
                                                  key.target_column;
                        std::size_t column = 0;
                        bool        usable = false;
                        if (has_cell) {
                            // A primeira coluna da chave decide: a consulta
                            // usa todas.
                            const std::size_t comma = key.source_column.find(',');
                            if (const auto index = rs.find_column(
                                    key.source_column.substr(0, comma))) {
                                column = *index;
                                usable = true;
                            }
                        }
                        if (ImGui::Selectable(label.c_str(), false,
                                              usable ? 0 : ImGuiSelectableFlags_Disabled)) {
                            const db::LinkQuery link = db::navigate_link_query(
                                rs, document->edits(), *table,
                                document->edit_target().schema, selected_row_, column);
                            if (!link.sql.empty()) open_sql_tab(link.sql, true);
                            else show_toast(TR("this cell is not a foreign key with a value"));
                        }
                        ImGui::PopID();
                    }

                    // ...e quem aponta para ela.
                    ImGui::Spacing();
                    ImGui::TextColored(col4(p.text_dim), "%s", TR("Referenced by"));
                    if (table->references.empty()) {
                        ImGui::TextColored(col4(p.text_dim), "  %s", TR("none"));
                    }
                    if (has_cell) {
                        const std::vector<db::LinkQuery> links = db::reference_queries(
                            rs, document->edits(), *table,
                            document->edit_target().schema, selected_row_);
                        for (std::size_t i = 0; i < links.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            if (ImGui::Selectable(links[i].title.c_str())) {
                                open_sql_tab(links[i].sql, true);
                            }
                            if (ImGui::IsItemHovered()) {
                                Hint(links[i].title).code(links[i].sql).show();
                            }
                            ImGui::PopID();
                        }
                    } else if (!table->references.empty()) {
                        ImGui::TextColored(col4(p.text_dim), "  %s",
                                           TR("select a row to follow its references"));
                    }
                }
            }
        }
        ImGui::End();
    }
}

// --- Barra de baixo ----------------------------------------------------------------

// Um botao da barra: icone, dica com a tecla do perfil e o motivo quando
// desabilitado -- tudo da tabela de comandos.
void MainShell::grid_tool_button(Command command, Icon icon) {
    const CommandInfo& info = command_info(command);

    std::string tooltip = TR(info.label);
    const std::string shortcut = command_shortcut(command, keymap_);
    if (!shortcut.empty()) tooltip += "  (" + shortcut + ")";
    if (info.note[0] != '\0') tooltip += std::string("\n") + TR(info.note);

    const std::string id = "##gt" + std::to_string(static_cast<int>(command));
    if (icon_button(id.c_str(), icon, tooltip.c_str(), command_enabled(command),
                    command_checked(command) ? colors().accent : 0,
                    ImGui::GetFrameHeight())) {
        // Enfileirado: o comando pode trocar o resultado, e a grade ainda
        // esta' sendo desenhada com o atual.
        queued_commands_.push_back(command);
    }
}

// A barra sob a grade, na ordem da do DBeaver: gravar e descartar; editar,
// acrescentar, duplicar e excluir linha; navegar; buscar mais; e, a' direita,
// visao de registro e paineis.
void MainShell::draw_grid_bottom_bar(SqlDocument& document, const db::ResultSet& rs) {
    (void)document;
    (void)rs;

    const auto group = [this](std::initializer_list<std::pair<Command, Icon>> buttons) {
        for (const auto& [command, icon] : buttons) {
            grid_tool_button(command, icon);
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::SameLine(0.0f, 10.0f);
    };

    group({{Command::grid_apply, Icon::accept}, {Command::grid_reject, Icon::reject}});
    group({{Command::grid_row_edit, Icon::row_edit},
           {Command::grid_row_add, Icon::row_add},
           {Command::grid_row_copy, Icon::row_copy},
           {Command::grid_row_delete, Icon::row_delete}});
    group({{Command::grid_row_first, Icon::first_page},
           {Command::grid_row_previous, Icon::chevron_left},
           {Command::grid_row_next, Icon::chevron_right},
           {Command::grid_row_last, Icon::last_page}});
    group({{Command::grid_filter_distinct, Icon::filter_value},
           {Command::grid_filter_settings, Icon::filter_config},
           {Command::grid_filter_clear, Icon::filter_reset}});

    // A' direita: as visoes e os paineis.
    constexpr int kRight = 7;
    const float width = static_cast<float>(kRight) * (ImGui::GetFrameHeight() + 2.0f);
    const float start = ImGui::GetWindowContentRegionMax().x - width;
    if (start > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(start);

    grid_tool_button(Command::grid_toggle_mode, Icon::record);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_switch_presentation, Icon::grid_mode);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_panel_value, Icon::panels);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_panel_references, Icon::panel_references);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_panel_metadata, Icon::panel_metadata);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_panel_grouping, Icon::panel_grouping);
    ImGui::SameLine(0.0f, 2.0f);
    grid_tool_button(Command::grid_panel_calc, Icon::panel_calc);
}

// --- Painel de valor ---------------------------------------------------------------

void MainShell::ValuePanel::format() {
    if (is_null) {
        text = "[null]";
        return;
    }
    switch (view) {
        case db::ValueView::json:
            text = db::format_json(raw);
            break;
        case db::ValueView::binary:
            text = db::format_hex(
                {reinterpret_cast<const std::byte*>(raw.data()), raw.size()},
                hex_limit);
            break;
        case db::ValueView::boolean:
            text = db::is_true(raw) ? "true" : "false";
            break;
        default:
            text = raw;
            break;
    }
}

// Prepara o painel de valor para uma celula.
//
// O conteudo e' FORMATADO aqui, uma vez, e nao a cada quadro: indentar um
// JSON de 4 KB ou montar o hexadecimal de 64 KB sessenta vezes por segundo
// seria desperdicio puro.
void MainShell::open_value_panel(SqlDocument& document, const db::ResultSet& rs,
                                 std::size_t row, std::size_t column) {
    value_panel_ = ValuePanel{};
    value_panel_.open        = true;
    value_panel_.column      = rs.column(column).info().name;
    value_panel_.document_id = document.id();
    value_panel_.row         = row;
    value_panel_.cell_column = column;
    value_panel_.kind        = rs.column(column).info().kind;

    // O limite vem do perfil (pagina "Editor binario").
    value_panel_.hex_limit =
        static_cast<std::size_t>(editor_options_for(document).hex_limit_kb) * 1024;

    // O valor do BUFFER tem precedencia: o painel mostra o que esta' na tela,
    // nao o que esta' no banco.
    const db::CellValue value = db::cell_value(rs, document.edits(), row, column);
    value_panel_.is_null = value.is_null;
    value_panel_.raw     = value.text;
    value_panel_.size    = value.text.size();
    value_panel_.view    = value.is_null
                               ? db::ValueView::plain
                               : db::choose_view(value_panel_.kind, value.text);
    value_panel_.format();
}

// "Switch content viewer": texto -> JSON -> hexadecimal -> texto.
void MainShell::cycle_value_viewer() {
    if (!value_panel_.open || value_panel_.is_null) return;

    switch (value_panel_.view) {
        case db::ValueView::plain:  value_panel_.view = db::ValueView::json;   break;
        case db::ValueView::json:   value_panel_.view = db::ValueView::binary; break;
        default:                    value_panel_.view = db::ValueView::plain;  break;
    }
    value_panel_.format();
}

void MainShell::draw_value_panel() {
    if (!value_panel_.open) return;

    const Palette& p = colors();
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_FirstUseEver);

    // Fundo opaco: a janela flutua sobre a grade (diretiva 13).
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool visible = ImGui::Begin(TRW("Value", "###ValuePanel"), &value_panel_.open,
                                      ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();

    if (visible) {
        ImGui::TextColored(col4(p.accent_light), "%s", value_panel_.column.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s  \xC2\xB7  %zu bytes",
                           TR(std::string(db::to_string(value_panel_.view)).c_str()),
                           value_panel_.size);

        // O visualizador (`switchContentViewer`). Fora da edicao: trocar a
        // visao no meio de uma edicao jogaria fora o que foi digitado.
        if (!value_panel_.editing && !value_panel_.is_null &&
            value_panel_.view != db::ValueView::boolean) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Switch viewer"))) cycle_value_viewer();
        }

        ImGui::Separator();
        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;

        SqlDocument* document = nullptr;
        for (std::unique_ptr<SqlDocument>& d : documents_) {
            if (d->id() == value_panel_.document_id) { document = d.get(); break; }
        }
        // O resultado pode ter mudado por baixo do painel: sem a celula de
        // origem, "Apply" gravaria na linha errada.
        const bool cell_valid =
            document != nullptr && document->result().has_value() &&
            value_panel_.row < document->result()->row_count() &&
            value_panel_.cell_column < document->result()->column_count() &&
            document->result()->column(value_panel_.cell_column).info().name ==
                value_panel_.column;
        const bool can_edit = cell_valid && document->edit_target().editable() &&
                              !document->edits().is_deleted(value_panel_.row);

        if (value_panel_.editing && can_edit) {
            if (value_panel_.buffer.empty()) {
                // Folga para crescer: o campo do ImGui tem tamanho fixo.
                value_panel_.buffer.assign(value_panel_.raw.size() + 64 * 1024, '\0');
                std::memcpy(value_panel_.buffer.data(), value_panel_.raw.data(),
                            value_panel_.raw.size());
            }

            ImFont* mono = mono_font();
            if (mono != nullptr) ImGui::PushFont(mono, 0.0f);
            ImGui::InputTextMultiline("##valueedit", value_panel_.buffer.data(),
                                      value_panel_.buffer.size(),
                                      ImVec2(-FLT_MIN, -footer));
            if (mono != nullptr) ImGui::PopFont();

            if (ImGui::Button(TR("Apply"), ImVec2(110, 0))) {
                document->edits().set(value_panel_.row, value_panel_.cell_column,
                                      value_panel_.buffer.data());
                value_panel_.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Set to NULL"), ImVec2(110, 0))) {
                document->edits().set_null(value_panel_.row, value_panel_.cell_column);
                value_panel_.open = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(110, 0))) value_panel_.open = false;
        } else {
            value_panel_.editing = false;

            // Booleano ganha um controle proprio em vez de texto: e' a
            // diferenca entre ver "1" e ver uma caixa marcada.
            if (value_panel_.view == db::ValueView::boolean) {
                bool checked = value_panel_.text == "true";
                ImGui::BeginDisabled();
                ImGui::Checkbox(value_panel_.text.c_str(), &checked);
                ImGui::EndDisabled();
            } else {
                ImFont* mono = mono_font();
                if (mono != nullptr) ImGui::PushFont(mono, 0.0f);
                ImGui::BeginChild("##valuebody", ImVec2(0, -footer),
                                  ImGuiChildFlags_Borders,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(value_panel_.text.c_str());
                ImGui::EndChild();
                if (mono != nullptr) ImGui::PopFont();
            }

            if (ImGui::Button(TR("Copy"), ImVec2(110, 0))) {
                ImGui::SetClipboardText(value_panel_.text.c_str());
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!can_edit);
            if (ImGui::Button(TR("Edit"), ImVec2(110, 0))) {
                value_panel_.editing = true;
                value_panel_.buffer.clear();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(TR("Close"), ImVec2(110, 0))) value_panel_.open = false;
        }
    }
    ImGui::End();
}

} // namespace otter::ui
