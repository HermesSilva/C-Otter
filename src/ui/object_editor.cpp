// C-Otter -- ui/object_editor.cpp
//
// O editor de objeto: a aba que o DBeaver abre ao dar duplo clique num no' da
// arvore (ou F4). Mapa em docs/OBJECT-EDITOR.md.
//
//   Properties   o formulario de propriedades em cima; embaixo, a lista de
//                secoes a' esquerda (Columns, Constraints, ..., Statistics,
//                Permissions, DDL) e o conteudo da secao a' direita
//   Data         a grade, para o que tem linhas
//
// Aqui fica so' a TELA. O que e' regra -- as consultas, o ALTER de cada
// propriedade, GRANT/REVOKE, o DROP -- esta' em db/object_info.cpp e
// db/object_ddl.cpp, com teste unitario e conferencia no servidor.
#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "db/ddl.hpp"
#include "db/mssql_object.hpp"
#include "db/sqlanywhere_object.hpp"
#include "db/mysql_object.hpp"
#include "db/object_ddl.hpp"
#include "db/object_info.hpp"
#include "ui/app_window.hpp"   // mono_font()
#include "ui/hint.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // ClearActiveID (canal de comandos)

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace otter::ui {
namespace {

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }
ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

using db::ObjectType;

Icon icon_for(ObjectType type) {
    switch (type) {
        case ObjectType::database:             return Icon::database;
        case ObjectType::schema:               return Icon::schema;
        case ObjectType::table:                return Icon::table;
        case ObjectType::view:                 return Icon::view;
        case ObjectType::materialized_view:    return Icon::materialized_view;
        case ObjectType::foreign_table:        return Icon::foreign_table;
        case ObjectType::column:               return Icon::column;
        case ObjectType::index:                return Icon::index;
        case ObjectType::constraint:           return Icon::constraint;
        case ObjectType::foreign_key:          return Icon::foreign_key;
        case ObjectType::trigger:              return Icon::trigger;
        case ObjectType::rule:                 return Icon::rule;
        case ObjectType::policy:               return Icon::policy;
        case ObjectType::sequence:             return Icon::sequence;
        case ObjectType::function:             return Icon::function;
        case ObjectType::procedure:            return Icon::procedure;
        case ObjectType::aggregate:            return Icon::aggregate;
        case ObjectType::data_type:            return Icon::data_type;
        case ObjectType::role:                 return Icon::role;
        case ObjectType::extension:            return Icon::extension;
        case ObjectType::tablespace:           return Icon::tablespace;
        case ObjectType::event_trigger:        return Icon::event_trigger;
        case ObjectType::foreign_server:       return Icon::foreign_server;
        case ObjectType::foreign_data_wrapper: return Icon::foreign_wrapper;
        case ObjectType::user_mapping:         return Icon::user_mapping;
        case ObjectType::language:             return Icon::language;
        case ObjectType::event:                return Icon::event;
    }
    return Icon::object_page;
}

// O nome do tipo como o DBeaver o escreve nos menus ("View Table", "Create
// New Materialized View"). Em ingles: e' chave de traducao.
const char* type_label(ObjectType type) {
    switch (type) {
        case ObjectType::database:             return "Database";
        case ObjectType::schema:               return "Schema";
        case ObjectType::table:                return "Table";
        case ObjectType::view:                 return "View";
        case ObjectType::materialized_view:    return "Materialized View";
        case ObjectType::foreign_table:        return "Foreign Table";
        case ObjectType::column:               return "Column";
        case ObjectType::index:                return "Index";
        case ObjectType::constraint:           return "Constraint";
        case ObjectType::foreign_key:          return "Foreign Key";
        case ObjectType::trigger:              return "Trigger";
        case ObjectType::rule:                 return "Rule";
        case ObjectType::policy:               return "Policy";
        case ObjectType::sequence:             return "Sequence";
        case ObjectType::function:             return "Function";
        case ObjectType::procedure:            return "Procedure";
        case ObjectType::aggregate:            return "Aggregate function";
        case ObjectType::data_type:            return "Data type";
        case ObjectType::role:                 return "Role";
        case ObjectType::extension:            return "Extension";
        case ObjectType::tablespace:           return "Tablespace";
        case ObjectType::event_trigger:        return "Event Trigger";
        case ObjectType::foreign_server:       return "Foreign server";
        case ObjectType::foreign_data_wrapper: return "Foreign data wrapper";
        case ObjectType::user_mapping:         return "User mapping";
        case ObjectType::language:             return "Language";
        case ObjectType::event:                return "Event";
    }
    return "Object";
}

// O "papel" tem outro nome em cada SGBD: Role no PostgreSQL, User no MySQL
// (a conta), Login no SQL Server (a conta do servidor).
//
// No SQL Anywhere ha' os dois: o usuario (entra no banco) e o papel puro (nao
// entra). Quem chama diz qual e' pelo `parent` do objeto -- "role" para o
// papel, como db/sqlanywhere_object.cpp espera no DROP. O tipo definido pelo
// usuario la' e' o dominio.
const char* type_label(ObjectType type, Session::Engine engine,
                       std::string_view parent = {}) {
    if (type == ObjectType::role) {
        if (engine == Session::Engine::mysql) return "User";
        if (engine == Session::Engine::mssql) return "Login";
        if (engine == Session::Engine::sqlanywhere) return parent == "role" ? "Role" : "User";
    }
    if (type == ObjectType::data_type && engine == Session::Engine::sqlanywhere) {
        return "Domain";
    }
    return type_label(type);
}

bool is_relation(ObjectType type) {
    return type == ObjectType::table || type == ObjectType::view ||
           type == ObjectType::materialized_view || type == ObjectType::foreign_table;
}

// O fonte se edita aqui e se grava com CREATE OR REPLACE. So' onde o comando
// existe: materialized view, trigger e regra nao tem OR REPLACE util, e
// "gravar" exigiria apagar e recriar -- perdendo dados, permissoes e
// dependentes sem avisar.
//
// No MySQL so' a view tem OR REPLACE; rotina, trigger e evento se gravam por
// DROP + CREATE (db/mysql_object.cpp), como o DBeaver faz -- e a janela de
// revisao avisa o que se perde.
//
// No SQL Server view, rotina e trigger se gravam por ALTER, que altera no
// lugar e preserva as permissoes.
bool source_editable(ObjectType type, Session::Engine engine) {
    if (engine == Session::Engine::mysql) return db::mysql_source_editable(type);
    if (engine == Session::Engine::mssql) return db::mssql_source_editable(type);
    if (engine == Session::Engine::sqlanywhere) return db::sqlanywhere_source_editable(type);
    return type == ObjectType::view || type == ObjectType::function ||
           type == ObjectType::procedure;
}

// "Source" onde o DBeaver chama assim (o objeto E' o codigo); "DDL" no resto.
const char* source_section(ObjectType type) {
    switch (type) {
        case ObjectType::view:
        case ObjectType::materialized_view:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::trigger:
        case ObjectType::rule:
        case ObjectType::event_trigger:
        case ObjectType::event:
            return "Source";
        default:
            return "DDL";
    }
}

// As secoes da lista da esquerda, na ordem do DBeaver. Rotulos em ingles.
std::vector<const char*> sections_for(const db::ObjectRef& ref, Session::Engine engine,
                                      const db::ObjectInfo& info) {
    std::vector<const char*> out;
    const char* source = source_section(ref.type);
    const bool pg = engine == Session::Engine::postgres;
    const bool ms = engine == Session::Engine::mssql;
    const bool sa = engine == Session::Engine::sqlanywhere;

    switch (ref.type) {
        case ObjectType::table:
            out = {"Columns", "Constraints", "Foreign Keys", "Indexes"};
            if (pg || ms) out.push_back("Dependencies");
            out.push_back("References");
            // SQL Server: o catalogo ainda nao le' as particoes. O SQL
            // Anywhere nao particiona tabelas.
            if (!ms && !sa) out.push_back("Partitions");
            out.push_back("Triggers");
            if (pg) { out.push_back("Rules"); out.push_back("Policies"); }
            break;
        case ObjectType::view:
            out = {source, "Columns"};
            if (pg || ms || sa) out.push_back("Dependencies");
            if (pg) out.push_back("Rules");
            out.push_back("Triggers");
            break;
        case ObjectType::materialized_view:
            out = {source, "Columns", "Indexes", "Dependencies"};
            break;
        case ObjectType::foreign_table:
            out = {"Columns", "Constraints", "Dependencies"};
            break;
        case ObjectType::function:
        case ObjectType::procedure:
            out = {source};
            if (pg || ms) { out.push_back("Parameters"); out.push_back("Dependencies"); }
            // O SQL Anywhere so' registra dependencia de view.
            if (sa) out.push_back("Parameters");
            break;
        case ObjectType::role:
            // No MySQL a conta nao tem membros: tem os GRANTs dela. O login do
            // SQL Server e' do servidor: os papeis sao de cada banco. No SQL
            // Anywhere o usuario tambem pode ser papel: membros, papeis
            // recebidos e -- para quem entra no banco -- o que lhe foi dado.
            if (pg)       out = {"Members", "Member of"};
            else if (sa) {
                out = {"Members", "Member of"};
                if (ref.parent != "role") out.push_back("Grants");
            }
            else if (!ms) out = {"Grants"};
            break;
        case ObjectType::foreign_server:
            out = {"User Mappings"};
            break;
        default:
            break;
    }

    if (!info.statistics.empty()) out.push_back("Statistics");
    if (info.has_permissions)     out.push_back("Permissions");
    if (std::find_if(out.begin(), out.end(), [source](const char* s) {
            return std::strcmp(s, source) == 0;
        }) == out.end()) {
        out.push_back(source);
    }
    return out;
}

Icon section_icon(std::string_view section) {
    if (section == "Columns")      return Icon::column;
    if (section == "Constraints")  return Icon::constraint;
    if (section == "Foreign Keys") return Icon::foreign_key;
    if (section == "Indexes")      return Icon::index;
    if (section == "Dependencies") return Icon::dependency;
    if (section == "References")   return Icon::references;
    if (section == "Partitions")   return Icon::partition;
    if (section == "Triggers")     return Icon::trigger;
    if (section == "Rules")        return Icon::rule;
    if (section == "Policies")     return Icon::policy;
    if (section == "Statistics")   return Icon::system_info;
    if (section == "Permissions")  return Icon::grant;
    if (section == "Parameters")   return Icon::parameter;
    if (section == "Members")      return Icon::role_group;
    if (section == "Member of")    return Icon::role;
    if (section == "Grants")       return Icon::grant;
    if (section == "User Mappings") return Icon::user_mapping;
    return Icon::object_page;
}

// Cabecalho + linhas de uma lista de secao. `on_row` recebe o indice quando a
// linha e' desenhada: e' la' que entram o duplo clique e o menu.
template <typename RowFn>
void draw_rows(const char* id, std::initializer_list<const char*> headers,
               const std::vector<std::vector<std::string>>& rows, RowFn&& on_row) {
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    const int columns = static_cast<int>(headers.size());
    if (!ImGui::BeginTable(id, columns, flags, ImVec2(0.0f, 0.0f))) return;

    ImGui::TableSetupScrollFreeze(1, 1);
    for (const char* header : headers) ImGui::TableSetupColumn(TR(header));
    ImGui::TableHeadersRow();

    for (std::size_t r = 0; r < rows.size(); ++r) {
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(r));
        for (int c = 0; c < columns; ++c) {
            ImGui::TableSetColumnIndex(c);
            const std::string& cell =
                static_cast<std::size_t>(c) < rows[r].size() ? rows[r][static_cast<std::size_t>(c)]
                                                             : std::string{};
            if (c == 0) {
                // A linha inteira e' o alvo: quem procura a coluna pelo tipo
                // clica no tipo.
                ImGui::Selectable(cell.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick);
                on_row(r);
            } else {
                ImGui::TextUnformatted(cell.c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// Linha de formulario: rotulo a' esquerda, campo a' direita ocupando o resto.
// A ordem do DBeaver -- o ImGui, sozinho, poe o rotulo depois do campo.
void form_label(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool begin_form(const char* id, float label_width = 150.0f, float field_width = 300.0f) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, label_width);
    ImGui::TableSetupColumn("##field", ImGuiTableColumnFlags_WidthFixed, field_width);
    return true;
}

// "a, b , c" -> {"a", "b", "c"}.
std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        std::string_view part = text.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        while (!part.empty() && part.front() == ' ') part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ') part.remove_suffix(1);
        if (!part.empty()) out.emplace_back(part);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

// Junta dois scripts, mantendo os indices dos destrutivos corretos.
void append_script(db::AlterScript& into, db::AlterScript from) {
    if (!from.error.empty()) {
        if (into.error.empty()) into.error = std::move(from.error);
        return;
    }
    const std::size_t base = into.statements.size();
    for (std::string& statement : from.statements) {
        into.statements.push_back(std::move(statement));
    }
    for (const std::size_t index : from.destructive) into.destructive.push_back(base + index);
    for (std::string& warning : from.warnings) into.warnings.push_back(std::move(warning));
}

} // namespace

// --- Abrir -------------------------------------------------------------------------

void MainShell::open_object_editor(db::ObjectRef ref, bool data) {
    if (active_connection_ >= connections_.size()) return;
    const std::size_t connection_id = connections_[active_connection_].id;

    // No MySQL coluna, indice, constraint e chave estrangeira nao tem editor
    // proprio: o que se abre e' a tabela, ja' na secao deles.
    std::string wanted_section;
    if (!session().is_postgres()) {
        switch (ref.type) {
            case ObjectType::column:      wanted_section = "Columns";      break;
            case ObjectType::index:       wanted_section = "Indexes";      break;
            case ObjectType::constraint:  wanted_section = "Constraints";  break;
            case ObjectType::foreign_key: wanted_section = "Foreign Keys"; break;
            default: break;
        }
        if (!wanted_section.empty()) {
            db::ObjectRef table;
            table.type   = ObjectType::table;
            table.schema = ref.schema;
            table.name   = ref.parent;
            ref = std::move(table);
        }
    }

    // O mesmo objeto nao abre duas vezes: a aba que ja' existe vem para a
    // frente. Duas abas do mesmo objeto, com edicoes diferentes pendentes,
    // gravariam uma por cima da outra.
    const std::string key = ref.key();
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        SqlDocument& document = *documents_[i];
        ObjectView* view = document.object();
        if (view == nullptr || document.connection_id() != connection_id) continue;
        if (view->ref.key() != key) continue;

        if (data) {
            view->page        = ObjectView::Page::data;
            view->select_page = true;
        }
        if (!wanted_section.empty()) view->section = wanted_section;
        select_document_id_ = document.id();
        active_document_    = i;
        tree_claim_         = connection_id;
        return;
    }

    SqlDocument& document = new_document();
    document.set_title(ref.title());

    ObjectView view;
    view.ref         = std::move(ref);
    view.page        = data ? ObjectView::Page::data : ObjectView::Page::properties;
    view.select_page = data;
    view.section     = wanted_section;
    document.set_object(std::move(view));

    // O DDL so' se edita onde ha' CREATE OR REPLACE; ate' la', somente leitura.
    document.editor().SetReadOnlyEnabled(true);

    select_document_id_ = document.id();
    // A janela da conexao vem para a frente: o objeto foi pedido na arvore, e
    // a aba dele pode estar atras da de outra conexao.
    focus_editor_      = true;
    focus_document_id_ = document.id();
    tree_claim_        = connection_id;
}

// --- A aba -------------------------------------------------------------------------

void MainShell::draw_object_editor(SqlDocument& document) {
    ObjectView& view = *document.object();
    Session& target  = session_for(document);
    const Palette& p = colors();

    // O pedido de foco foi para a JANELA (draw_connection_editor); aqui nao
    // ha' editor de texto para recebe-lo.
    if (focus_editor_ &&
        (focus_document_id_ == 0 || focus_document_id_ == document.id())) {
        focus_editor_      = false;
        focus_document_id_ = 0;
    }

    const bool connected = target.state() == SessionState::connected;
    const Session::ObjectState state = target.object_info(view.ref);
    if (!state.loaded && connected && !target.busy()) {
        target.load_object_info_async(view.ref);
    }

    // Cabecalho: icone, tipo, nome qualificado.
    icon_inline(icon_for(view.ref.type), p.accent_light);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextColored(col4(p.text_dim), "%s",
                       TR(type_label(view.ref.type, target.engine())));
    ImGui::SameLine();
    {
        std::string full;
        if (!view.ref.schema.empty()) full += view.ref.schema + ".";
        if (!view.ref.parent.empty()) full += view.ref.parent + ".";
        full += view.ref.title();
        ImGui::TextColored(col4(p.text_bright), "%s", full.c_str());
    }
    if (!connected) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.error), "%s", TR("(not connected)"));
    } else if (!state.loaded) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
    }

    const ObjectView::Page wanted = view.page;
    const bool             select = view.select_page;
    view.select_page = false;

    if (!ImGui::BeginTabBar("##objectpages")) return;

    if (ImGui::BeginTabItem(TR("Properties"), nullptr,
                            select && wanted == ObjectView::Page::properties
                                ? ImGuiTabItemFlags_SetSelected
                                : ImGuiTabItemFlags_None)) {
        if (!select) view.page = ObjectView::Page::properties;
        draw_object_properties(document, state.info, state.loaded);
        ImGui::EndTabItem();
    }

    if (is_relation(view.ref.type) &&
        ImGui::BeginTabItem(TR("Data"), nullptr,
                            select && wanted == ObjectView::Page::data
                                ? ImGuiTabItemFlags_SetSelected
                                : ImGuiTabItemFlags_None)) {
        view.page = ObjectView::Page::data;
        draw_object_data(document);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    // A proxima relacao aberta pela arvore cai na aba em que esta ficou.
    if (is_relation(view.ref.type) && !select) {
        relation_opens_data_ = view.page == ObjectView::Page::data;
    }
}

// --- Data --------------------------------------------------------------------------

void MainShell::draw_object_data(SqlDocument& document) {
    ObjectView& view = *document.object();
    Session& target  = session_for(document);

    // A consulta parte ao abrir a aba pela primeira vez, e nao ao abrir o
    // editor: quem veio ver as colunas de uma tabela de 2 milhoes de linhas
    // nao pediu os dados.
    if (!view.data_started && target.state() == SessionState::connected &&
        !target.busy()) {
        view.data_started = true;
        run_sql(document,
                "SELECT * FROM " + db::qualified_name(view.ref.schema, view.ref.name),
                RunMode::same_tab);
    }

    if (document.executing()) {
        ImGui::TextColored(col4(colors().text_dim), "%s", TR("loading..."));
    } else if (export_object_pending_ && document.result().has_value()) {
        // "Export Data" pedido na arvore: os dados chegaram, a janela abre.
        export_object_pending_ = false;
        show_export_           = true;
    }
    draw_grid_view(document);
}

// --- Properties --------------------------------------------------------------------

void MainShell::draw_object_properties(SqlDocument& document, const db::ObjectInfo& info,
                                       bool loaded) {
    ObjectView& view = *document.object();
    const Palette& p = colors();

    if (!info.error.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
        ImGui::TextWrapped("%s", info.error.c_str());
        ImGui::PopStyleColor();
    }
    if (!loaded || info.properties.empty()) return;

    draw_object_form(document, info);

    // A barra de baixo (Save / Revert / Refresh) fica sempre a' vista, como
    // no rodape' do editor do DBeaver.
    const float bar = ImGui::GetFrameHeightWithSpacing() + 4.0f;

    Session& target = session_for(document);
    const std::vector<const char*> sections =
        sections_for(view.ref, target.engine(), info);

    // A secao escolhida pode nao existir mais (o objeto mudou de tipo de
    // conteudo apos uma releitura): cai na primeira.
    if (std::find_if(sections.begin(), sections.end(), [&view](const char* s) {
            return view.section == s;
        }) == sections.end()) {
        view.section = sections.empty() ? std::string{} : sections.front();
    }

    // "Next tab" / "Previous tab" (Alt+Shift+setas) e "Open source tab".
    if (view.step_section != 0 && !sections.empty()) {
        const auto current = std::find_if(
            sections.begin(), sections.end(),
            [&view](const char* s) { return view.section == s; });
        const auto count = static_cast<std::ptrdiff_t>(sections.size());
        const std::ptrdiff_t index =
            ((current - sections.begin()) + view.step_section + count) % count;
        view.section = sections[static_cast<std::size_t>(index)];
    }
    view.step_section = 0;
    if (view.goto_source) {
        view.section     = source_section(view.ref.type);
        view.goto_source = false;
    }

    ImGui::Separator();

    const float list_width = ImGui::GetFontSize() * 9.5f;
    if (ImGui::BeginChild("##sections", ImVec2(list_width, -bar),
                          ImGuiChildFlags_Borders)) {
        for (const char* section : sections) {
            ImGui::PushID(section);
            const bool selected = view.section == section;

            // Icone e rotulo na mesma linha clicavel, como as abas verticais
            // do DBeaver.
            if (ImGui::Selectable("##section", selected)) view.section = section;
            ImGui::SameLine(4.0f);
            icon_inline(section_icon(section), selected ? p.accent : p.text_dim);
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(col4(selected ? p.text_bright : p.text), "%s",
                               TR(section));
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    if (ImGui::BeginChild("##sectionbody", ImVec2(0.0f, -bar))) {
        draw_object_section(document, info);
    }
    ImGui::EndChild();

    // --- Rodape' -----------------------------------------------------------------
    const bool source_dirty =
        view.source_loaded && document.editor().GetText() != view.source_original;
    const bool dirty   = !view.edits.empty() || source_dirty;
    const bool can_run = target.state() == SessionState::connected && !target.busy();

    if (icon_text_button("##saveobject", Icon::accept, TR("Save ..."),
                         TR("Review the SQL of the pending changes and run it"),
                         dirty && can_run)) {
        save_object_edits(document, info);
    }
    ImGui::SameLine();
    if (icon_text_button("##revertobject", Icon::reject, TR("Revert"),
                         TR("Throw the pending changes away"), dirty)) {
        view.edits.clear();
        view.source_loaded = false;   // o texto volta ao que o servidor tem
    }
    ImGui::SameLine();
    if (icon_text_button("##refreshobject", Icon::refresh, TR("Refresh"),
                         TR("Read the object from the server again"), can_run)) {
        target.invalidate_object_info(view.ref);
        if (is_relation(view.ref.type)) {
            target.invalidate_table(view.ref.schema, view.ref.name);
        }
        view.source_loaded = false;
    }
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.warn), "%s", TR("changes not saved"));
    }
}

// O formulario de cima: rotulo e valor, em duas colunas de pares.
void MainShell::draw_object_form(SqlDocument& document, const db::ObjectInfo& info) {
    ObjectView& view = *document.object();
    Session& target  = session_for(document);
    const Palette& p = colors();

    // O comentario vai por ultimo, na largura toda: e' texto corrido.
    std::vector<const db::ObjectProperty*> fields;
    const db::ObjectProperty* comment = nullptr;
    for (const db::ObjectProperty& property : info.properties) {
        if (property.name == "Comment") comment = &property;
        else                            fields.push_back(&property);
    }

    const auto current = [&view](const db::ObjectProperty& property) -> std::string {
        const auto it = view.edits.find(property.name);
        return it != view.edits.end() ? it->second : property.value;
    };
    const auto set = [&view](const db::ObjectProperty& property, std::string value) {
        if (value == property.value) view.edits.erase(property.name);
        else                         view.edits[property.name] = std::move(value);
    };

    // Lista de nomes para os campos que escolhem outro objeto (dono, schema,
    // tablespace): o DBeaver usa combo, e digitar o nome de um papel de cor
    // e' pedir erro de digitacao num ALTER.
    const auto names_for = [&](db::ObjectEdit edit) -> std::vector<std::string> {
        std::vector<std::string> names;
        if (edit == db::ObjectEdit::schema) {
            for (const db::SchemaMeta& schema : target.schemas()) {
                names.push_back(schema.name);
            }
            return names;
        }
        const db::CatalogList list = edit == db::ObjectEdit::owner
                                         ? db::CatalogList::roles
                                         : db::CatalogList::tablespaces;
        const Session::ListState state = target.list(list);
        if (!state.loaded && !target.busy()) target.load_list_async(list);
        for (const db::CatalogItem& item : state.items) names.push_back(item.name);
        return names;
    };

    const auto draw_field = [&](const db::ObjectProperty& property) {
        ImGui::PushID(property.name.c_str());
        const std::string value  = current(property);
        const bool        edited = view.edits.contains(property.name);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col4(edited ? p.warn : p.text_dim), "%s",
                           TR(property.name.c_str()));
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);

        const bool role_flag =
            view.ref.type == ObjectType::role &&
            !db::role_option_keyword(property.name, true).empty();

        if (role_flag) {
            bool on = value == "yes";
            if (ImGui::Checkbox("##flag", &on)) set(property, on ? "yes" : "no");
        } else if (property.edit == db::ObjectEdit::owner ||
                   property.edit == db::ObjectEdit::schema ||
                   property.edit == db::ObjectEdit::tablespace) {
            if (ImGui::BeginCombo("##choice", value.c_str())) {
                for (const std::string& name : names_for(property.edit)) {
                    if (ImGui::Selectable(name.c_str(), name == value)) {
                        set(property, name);
                    }
                }
                ImGui::EndCombo();
            }
        } else if (property.edit != db::ObjectEdit::none) {
            char buffer[512];
            std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
            if (ImGui::InputText("##value", buffer, sizeof buffer)) {
                set(property, buffer);
            }
        } else {
            // Somente leitura, mas selecionavel: copiar o OID ou o tamanho e'
            // uso comum.
            char buffer[512];
            std::snprintf(buffer, sizeof buffer, "%s", value.c_str());
            ImGui::PushStyleColor(ImGuiCol_FrameBg, col(p.bg_darkest));
            ImGui::InputText("##value", buffer, sizeof buffer,
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::PopStyleColor();
            if (value.size() >= sizeof buffer - 1 && ImGui::IsItemHovered()) {
                hint_fmt("%s", value.c_str());
            }
        }
        ImGui::PopID();
    };

    if (ImGui::BeginTable("##objectform", 4, ImGuiTableFlags_SizingStretchProp)) {
        const float label = ImGui::GetFontSize() * 9.0f;
        ImGui::TableSetupColumn("##l1", ImGuiTableColumnFlags_WidthFixed, label);
        ImGui::TableSetupColumn("##v1", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##l2", ImGuiTableColumnFlags_WidthFixed, label);
        ImGui::TableSetupColumn("##v2", ImGuiTableColumnFlags_WidthStretch);

        // Metade a' esquerda, metade a' direita, na ordem da consulta: o Name
        // fica no topo da primeira coluna, como no DBeaver.
        const std::size_t half = (fields.size() + 1) / 2;
        for (std::size_t r = 0; r < half; ++r) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            draw_field(*fields[r]);
            ImGui::TableNextColumn();
            if (half + r < fields.size()) draw_field(*fields[half + r]);
        }
        ImGui::EndTable();
    }

    if (comment != nullptr) {
        ImGui::PushID("##comment");
        const bool edited = view.edits.contains(comment->name);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col4(edited ? p.warn : p.text_dim), "%s", TR("Comment"));
        ImGui::SameLine(ImGui::GetFontSize() * 9.0f + ImGui::GetStyle().CellPadding.x * 2);

        char buffer[2048];
        std::snprintf(buffer, sizeof buffer, "%s", current(*comment).c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (comment->edit == db::ObjectEdit::none) {
            ImGui::InputText("##value", buffer, sizeof buffer,
                             ImGuiInputTextFlags_ReadOnly);
        } else if (ImGui::InputText("##value", buffer, sizeof buffer)) {
            set(*comment, buffer);
        }
        ImGui::PopID();
    }
}

// Monta o script das propriedades alteradas e abre a confirmacao.
void MainShell::save_object_edits(SqlDocument& document, const db::ObjectInfo& info) {
    ObjectView& view = *document.object();

    db::AlterScript script;
    db::ObjectRef   ref = view.ref;   // segue o objeto pelas mudancas de nome

    const auto edit_of = [&view](const char* label) -> const std::string* {
        const auto it = view.edits.find(label);
        return it != view.edits.end() ? &it->second : nullptr;
    };

    // Atributos do papel.
    if (ref.type == ObjectType::role) {
        for (const auto& [label, value] : view.edits) {
            if (db::role_option_keyword(label, true).empty()) continue;
            append_script(script, db::generate_role_option(ref.name, label, value == "yes"));
        }
    }

    // A ORDEM importa: tudo o que cita o objeto pelo nome atual vem antes de
    // muda-lo de schema, e a troca de nome e' a ultima.
    if (const std::string* value = edit_of("Comment")) {
        append_script(script, db::generate_object_comment(ref, *value));
    }
    if (const std::string* value = edit_of("Owner")) {
        append_script(script, db::generate_object_owner(ref, *value));
    }
    if (const std::string* value = edit_of("Tablespace")) {
        append_script(script, db::generate_object_tablespace(ref, *value));
    }
    if (const std::string* value = edit_of("Schema")) {
        append_script(script, db::generate_object_schema(ref, *value));
        ref.schema = *value;
    }
    if (const std::string* value = edit_of("Name")) {
        append_script(script, db::generate_object_rename(ref, *value));
        ref.name = *value;
    }

    // O fonte, por ultimo: o CREATE OR REPLACE cita o nome que esta' no texto.
    const Session::Engine engine = session_for(document).engine();
    if (view.source_loaded && source_editable(view.ref.type, engine)) {
        const std::string text = document.editor().GetText();
        if (text != view.source_original) {
            if (engine == Session::Engine::postgres) {
                db::AlterScript source;
                source.statements.push_back(text);
                append_script(script, std::move(source));
            } else if (engine == Session::Engine::mssql) {
                append_script(script, db::mssql_source_script(ref, text));
            } else if (engine == Session::Engine::sqlanywhere) {
                append_script(script, db::sqlanywhere_source_script(ref, text));
            } else {
                append_script(script, db::mysql_source_script(ref, text));
            }
        }
    }
    (void)info;

    // Nada mudou (o valor voltou ao original, ou a releitura chegou antes):
    // uma janela "Review SQL" vazia so' confundiria.
    if (script.statements.empty() && script.error.empty()) {
        show_toast(TR("nothing to save"));
        return;
    }

    confirm_object_ddl(TRF("Alter %s", view.ref.title().c_str()), std::move(script),
                       document.id(), ref);
}

// --- Secoes ------------------------------------------------------------------------

void MainShell::draw_object_section(SqlDocument& document, const db::ObjectInfo& info) {
    ObjectView& view = *document.object();
    Session& target  = session_for(document);
    const Palette& p = colors();
    const db::ObjectRef& ref = view.ref;
    const std::string&   section = view.section;

    if (section == "DDL" || section == "Source") {
        draw_object_source(document, info);
        return;
    }
    if (section == "Permissions") {
        draw_object_permissions(document, info);
        return;
    }
    if (section == "Statistics") {
        std::vector<std::vector<std::string>> rows;
        for (const db::ObjectProperty& statistic : info.statistics) {
            rows.push_back({TR(statistic.name.c_str()), statistic.value});
        }
        draw_rows("##statistics", {"Name", "Value"}, rows, [](std::size_t) {});
        return;
    }

    // As listas genericas do catalogo (as mesmas da arvore).
    const auto list_section = [&](db::CatalogList list, const std::string& a,
                                  const std::string& b, const std::string& c,
                                  std::optional<ObjectType> child) {
        const Session::ListState state = target.list(list, a, b, c);
        if (!state.loaded) {
            if (!target.busy()) target.load_list_async(list, a, b, c);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        if (!state.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(p.error));
            ImGui::TextWrapped("%s", state.error.c_str());
            ImGui::PopStyleColor();
            return;
        }
        std::vector<std::vector<std::string>> rows;
        for (const db::CatalogItem& item : state.items) {
            rows.push_back({item.name, item.detail, item.tooltip});
        }
        draw_rows("##list", {"Name", "Details", "Description"}, rows,
                  [&](std::size_t r) {
                      if (!child) return;
                      db::ObjectRef sub;
                      sub.type   = *child;
                      sub.schema = *child == ObjectType::role ? std::string{} : ref.schema;
                      sub.name   = state.items[r].name;
                      sub.parent = (*child == ObjectType::rule ||
                                    *child == ObjectType::policy ||
                                    *child == ObjectType::user_mapping)
                                       ? ref.name
                                       : std::string{};
                      object_node(sub);
                  });
    };

    if (section == "Dependencies") {
        if (ref.type == ObjectType::function || ref.type == ObjectType::procedure) {
            list_section(db::CatalogList::routine_dependencies, ref.schema, ref.name,
                         object_arguments(target, ref), std::nullopt);
        } else {
            list_section(db::CatalogList::dependencies, ref.schema, ref.name, {},
                         std::nullopt);
        }
        return;
    }
    if (section == "Rules") {
        list_section(db::CatalogList::rules, ref.schema, ref.name, {}, ObjectType::rule);
        return;
    }
    if (section == "Policies") {
        list_section(db::CatalogList::policies, ref.schema, ref.name, {},
                     ObjectType::policy);
        return;
    }
    if (section == "Parameters") {
        list_section(db::CatalogList::routine_parameters, ref.schema, ref.name,
                     object_arguments(target, ref), std::nullopt);
        return;
    }
    if (section == "Members") {
        list_section(db::CatalogList::role_members, ref.name, {}, {}, ObjectType::role);
        return;
    }
    if (section == "Member of") {
        list_section(db::CatalogList::role_belongs, ref.name, {}, {}, ObjectType::role);
        return;
    }
    if (section == "Grants") {
        // SHOW GRANTS da conta, como a pasta Users da arvore.
        const std::string host = ref.parent.empty() ? "%" : ref.parent;
        if (!target.users_loaded() && !target.busy()) target.load_users_async();
        for (const db::UserMeta& user : target.users()) {
            // No SQL Anywhere a conta e' so' o nome: nao ha' host.
            if (user.name != ref.name ||
                (!target.is_sqlanywhere() && user.host != host)) {
                continue;
            }
            if (!user.grants_loaded && !target.busy()) {
                target.load_grants_async(user.name, user.host);
            }
            for (const std::string& grant : user.grants) {
                ImGui::TextWrapped("%s", grant.c_str());
            }
            if (user.grants_loaded && user.grants.empty()) {
                ImGui::TextColored(col4(colors().text_dim), "%s", TR("no grants"));
            }
        }
        return;
    }
    if (section == "User Mappings") {
        list_section(db::CatalogList::user_mappings, ref.name, {}, {},
                     ObjectType::user_mapping);
        return;
    }

    // --- Filhos de uma relacao: vem do modelo da arvore ------------------------------
    const std::optional<db::TableMeta> found = target.table(ref.schema, ref.name);
    if (!found) {
        ImGui::TextColored(col4(p.text_dim), "%s",
                           TR("refresh the navigator to load this object"));
        return;
    }
    const db::TableMeta& table = *found;
    const bool idle = !target.busy();

    const auto child = [&ref](ObjectType type, const std::string& name) {
        db::ObjectRef sub;
        sub.type   = type;
        sub.schema = ref.schema;
        sub.name   = name;
        // O indice e' objeto do SCHEMA; os demais pertencem a' tabela.
        sub.parent = type == ObjectType::index ? std::string{} : ref.name;
        return sub;
    };

    std::vector<std::vector<std::string>> rows;

    if (section == "Columns") {
        if (!table.columns_loaded) {
            if (idle) target.load_columns_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        for (const db::ColumnMeta& column : table.columns) {
            rows.push_back({column.name, std::to_string(column.position),
                            column.type_name, column.nullable ? "" : "[v]",
                            column.primary_key ? "[v]" : "", column.default_value,
                            column.comment});
        }
        draw_rows("##columns",
                  {"Column Name", "#", "Data type", "Not Null", "Key", "Default",
                   "Comment"},
                  rows, [&](std::size_t r) {
                      object_node(child(ObjectType::column, table.columns[r].name));
                  });
        return;
    }

    if (section == "Constraints") {
        if (!table.constraints_loaded) {
            if (idle) target.load_constraints_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        for (const db::ConstraintMeta& constraint : table.constraints) {
            rows.push_back({constraint.name, std::string(db::to_string(constraint.kind)),
                            constraint.columns, constraint.definition});
        }
        draw_rows("##constraints", {"Name", "Type", "Columns", "Definition"}, rows,
                  [&](std::size_t r) {
                      object_node(child(ObjectType::constraint, table.constraints[r].name));
                  });
        return;
    }

    if (section == "Foreign Keys" || section == "References") {
        if (!table.keys_loaded) {
            if (idle) target.load_keys_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        const bool outgoing = section == "Foreign Keys";
        const std::vector<db::ForeignKeyMeta>& keys =
            outgoing ? table.foreign_keys : table.references;
        for (const db::ForeignKeyMeta& key : keys) {
            rows.push_back({key.name, outgoing ? key.source_column : key.source_table,
                            outgoing ? key.target_table : key.source_column,
                            key.target_column, key.on_update, key.on_delete});
        }
        if (outgoing) {
            draw_rows("##fks",
                      {"Name", "Columns", "Ref Table", "Ref Columns", "On Update",
                       "On Delete"},
                      rows, [&](std::size_t r) {
                          object_node(child(ObjectType::foreign_key, keys[r].name));
                      });
        } else {
            draw_rows("##refs",
                      {"Name", "Table", "Columns", "Ref Columns", "On Update",
                       "On Delete"},
                      rows, [&](std::size_t r) {
                          // A chave pertence a' OUTRA tabela.
                          db::ObjectRef sub;
                          sub.type   = ObjectType::foreign_key;
                          sub.schema = keys[r].source_schema.empty() ? ref.schema
                                                                    : keys[r].source_schema;
                          sub.name   = keys[r].name;
                          sub.parent = keys[r].source_table;
                          object_node(sub);
                      });
        }
        return;
    }

    if (section == "Indexes") {
        if (!table.indexes_loaded) {
            if (idle) target.load_indexes_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        for (const db::IndexMeta& index : table.indexes) {
            rows.push_back({index.name, index.columns, index.method,
                            index.unique ? "[v]" : "", index.primary ? "[v]" : "",
                            index.valid ? "" : "INVALID", index.size_pretty,
                            index.definition});
        }
        draw_rows("##indexes",
                  {"Name", "Columns", "Access method", "Unique", "Primary", "State",
                   "Size", "Definition"},
                  rows, [&](std::size_t r) {
                      object_node(child(ObjectType::index, table.indexes[r].name));
                  });
        return;
    }

    if (section == "Partitions") {
        if (!table.partitions_loaded) {
            if (idle) target.load_partitions_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        if (table.partitions.empty()) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("not partitioned"));
            return;
        }
        for (const db::PartitionMeta& partition : table.partitions) {
            rows.push_back({partition.name, partition.method, partition.expression,
                            partition.description, partition.size_pretty});
        }
        draw_rows("##partitions", {"Name", "Method", "Key", "Bounds", "Size"}, rows,
                  [&](std::size_t r) {
                      // No PostgreSQL a particao e' uma tabela: abre como tal.
                      if (!table.partitions[r].is_table) return;
                      db::ObjectRef sub;
                      sub.type   = ObjectType::table;
                      sub.schema = ref.schema;
                      sub.name   = table.partitions[r].name;
                      object_node(sub);
                  });
        return;
    }

    if (section == "Triggers") {
        if (!table.triggers_loaded) {
            if (idle) target.load_triggers_async(ref.schema, ref.name);
            ImGui::TextColored(col4(p.text_dim), "%s", TR("loading..."));
            return;
        }
        for (const db::TriggerMeta& trigger : table.triggers) {
            rows.push_back({trigger.name, trigger.timing, trigger.events,
                            trigger.enabled ? "[v]" : "", trigger.definition});
        }
        draw_rows("##triggers", {"Name", "Timing", "Events", "Enabled", "Definition"},
                  rows, [&](std::size_t r) {
                      object_node(child(ObjectType::trigger, table.triggers[r].name));
                  });
        return;
    }
}

// A assinatura COM nomes (`pg_get_function_arguments`), que e' como as listas
// de parametros e dependencias localizam a rotina. O editor guarda so' os
// tipos; o texto completo vem do modelo da arvore.
std::string MainShell::object_arguments(Session& target, const db::ObjectRef& ref) {
    for (const db::SchemaMeta& schema : target.schemas()) {
        if (schema.name != ref.schema) continue;
        for (const db::RoutineMeta& routine : schema.routines) {
            if (routine.name == ref.name && routine.signature == ref.signature) {
                return routine.arguments;
            }
        }
    }
    return ref.signature;
}

// --- DDL / Source ------------------------------------------------------------------

void MainShell::draw_object_source(SqlDocument& document, const db::ObjectInfo& info) {
    ObjectView& view = *document.object();
    const Palette& p = colors();
    TextEditor& editor = document.editor();

    const Session::Engine engine = session_for(document).engine();
    const bool pg       = engine == Session::Engine::postgres;
    const bool editable = source_editable(view.ref.type, engine) && !info.ddl.empty();

    // O texto entra no editor UMA vez por leitura: redesenhar nao pode apagar
    // o que o usuario esta' digitando.
    //
    // "Por leitura", e nao "uma vez": depois de gravar, o catalogo e' relido
    // em segundo plano, e por alguns quadros a leitura ANTERIOR ainda esta'
    // no cache. Carregar so' uma vez fixava no editor o texto velho -- visto
    // na conferencia: a funcao gravada continuava mostrando o corpo antigo, e
    // o proximo "Save" nao achava diferenca nenhuma.
    if (!view.source_loaded || info.ddl != view.source_server) {
        editor.SetReadOnlyEnabled(false);
        editor.SetText(info.ddl);
        editor.SetReadOnlyEnabled(!editable);
        view.source_server   = info.ddl;
        view.source_original = editor.GetText();
        view.source_loaded   = true;
    }

    if (info.ddl.empty()) {
        ImGui::TextColored(col4(p.text_dim), "%s", TR("no DDL for this object"));
        return;
    }

    if (icon_text_button("##copyddl", Icon::copy, TR("Copy"),
                         TR("Copy the definition to the clipboard"))) {
        ImGui::SetClipboardText(editor.GetText().c_str());
    }
    ImGui::SameLine();
    if (icon_text_button("##openddl", Icon::open, TR("Open in editor"),
                         TR("Open the definition in a new SQL tab"))) {
        const std::size_t connection = document.connection_id();
        SqlDocument& script = new_document();
        script.set_connection_id(connection);
        script.editor().SetText(editor.GetText());
        select_document_id_ = script.id();
        return;   // `document` pode ter sido realocado junto com o vetor
    }
    ImGui::SameLine();
    ImGui::TextColored(
        col4(p.text_dim), "%s",
        !editable ? TR("read-only: this object type has no CREATE OR REPLACE")
        : engine == Session::Engine::mssql || engine == Session::Engine::sqlanywhere
            ? TR("edit and use Save: the object is changed in place with ALTER")
        : pg || view.ref.type == ObjectType::view
            ? TR("edit and use Save to run CREATE OR REPLACE")
            : TR("edit and use Save: the object is dropped and created again"));

    ImFont* mono = mono_font();
    if (mono != nullptr) ImGui::PushFont(mono);
    editor.Render("##objectsource", ImGui::GetContentRegionAvail());
    if (mono != nullptr) ImGui::PopFont();
}

// --- Permissions -------------------------------------------------------------------
//
// Como a aba do DBeaver: os papeis a' esquerda, os privilegios do papel
// escolhido a' direita, com "Grant All" e "Revoke All".

void MainShell::draw_object_permissions(SqlDocument& document, const db::ObjectInfo& info) {
    ObjectView& view = *document.object();
    Session& target  = session_for(document);
    const Palette& p = colors();

    std::vector<std::string_view> privileges = db::privileges_for(view.ref.type);
    // O que o servidor concedeu e a lista fixa nao conhece (MAINTAIN, do
    // PostgreSQL 17) entra tambem: a contagem ao lado do papel diria 8 com
    // 7 caixas na tela.
    for (const db::ObjectPermission& permission : info.permissions) {
        if (std::find(privileges.begin(), privileges.end(), permission.privilege) ==
            privileges.end()) {
            privileges.push_back(permission.privilege);
        }
    }
    if (privileges.empty()) {
        ImGui::TextColored(col4(p.text_dim), "%s",
                           TR("this object type has no privileges"));
        return;
    }

    // PUBLIC, depois quem ja' tem concessao, depois os demais papeis. No
    // MySQL nao ha' PUBLIC: a lista sao as contas, como 'usuario'@'host'.
    //
    // No SQL Server sao os usuarios e papeis do BANCO; `public` e' um papel
    // como os outros e ja' vem na lista.
    const bool pg = target.is_postgres();
    const bool my = target.is_mysql();
    const Session::ListState roles =
        !my ? target.list(db::CatalogList::roles) : Session::ListState{};
    if (!my && !roles.loaded && !target.busy()) {
        target.load_list_async(db::CatalogList::roles);
    }
    if (my && !target.users_loaded() && !target.busy()) target.load_users_async();

    std::vector<std::string> grantees;
    if (pg) grantees.emplace_back("PUBLIC");
    const auto add = [&grantees](const std::string& name) {
        if (std::find(grantees.begin(), grantees.end(), name) == grantees.end()) {
            grantees.push_back(name);
        }
    };
    for (const db::ObjectPermission& permission : info.permissions) add(permission.grantee);
    for (const db::CatalogItem& role : roles.items) add(role.name);
    if (my) {
        for (const db::UserMeta& user : target.users()) {
            add("'" + user.name + "'@'" + user.host + "'");
        }
    }
    if (!pg) {
        // A conta escolhida precisa existir na lista: o padrao "PUBLIC" do
        // PostgreSQL nao vale aqui.
        if (std::find(grantees.begin(), grantees.end(), view.grantee) == grantees.end()) {
            view.grantee = grantees.empty() ? std::string{} : grantees.front();
        }
    }

    const auto granted = [&info](const std::string& grantee, std::string_view privilege)
        -> const db::ObjectPermission* {
        for (const db::ObjectPermission& permission : info.permissions) {
            if (permission.grantee == grantee && permission.privilege == privilege) {
                return &permission;
            }
        }
        return nullptr;
    };
    const auto count_for = [&info](const std::string& grantee) {
        return std::count_if(info.permissions.begin(), info.permissions.end(),
                             [&grantee](const db::ObjectPermission& permission) {
                                 return permission.grantee == grantee;
                             });
    };

    const bool can_run = target.state() == SessionState::connected && !target.busy();

    const float list_width = ImGui::GetFontSize() * 14.0f;
    if (ImGui::BeginChild("##grantees", ImVec2(list_width, 0.0f),
                          ImGuiChildFlags_Borders)) {
        for (const std::string& grantee : grantees) {
            const auto count = count_for(grantee);
            const bool selected = view.grantee == grantee;

            // Quem nao tem nada sai esmaecido: a lista traz TODOS os papeis, e
            // o que interessa de relance e' quem ja' tem acesso.
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  col(count > 0 ? p.text : p.text_dim));
            const std::string label =
                grantee + (count > 0 ? "  (" + std::to_string(count) + ")" : "");
            // O nome vai como TEXTO, ao lado de um Selectable sem rotulo: o
            // ImGui corta o rotulo em "##", e os usuarios de certificado do
            // SQL Server se chamam "##MS_PolicyEventProcessingLogin##" --
            // saiam como duas linhas em branco na lista.
            ImGui::PushID(grantee.c_str());
            if (ImGui::Selectable("##grantee", selected)) view.grantee = grantee;
            ImGui::SameLine(4.0f);
            ImGui::TextUnformatted(label.c_str());
            ImGui::PopID();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();

    const std::string title = db::object_sql_name(view.ref);
    if (ImGui::Button(TR("Grant All")) && can_run) {
        confirm_object_ddl(TRF("Grant on %s", title.c_str()),
                           db::generate_grant(view.ref, "ALL", view.grantee),
                           document.id(), view.ref);
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Revoke All")) && can_run) {
        confirm_object_ddl(TRF("Revoke on %s", title.c_str()),
                           db::generate_revoke(view.ref, "ALL", view.grantee),
                           document.id(), view.ref);
    }
    ImGui::SameLine();
    ImGui::TextColored(col4(p.text_dim), "%s", view.grantee.c_str());

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_NoSavedSettings;
    // Contorno nas caixas: no tema claro a caixa VAZIA tem a cor do fundo, e
    // uma grade de privilegios nao concedidos parecia uma lista sem caixas.
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    const bool privileges_table = ImGui::BeginTable("##privileges", 3, flags);
    if (!privileges_table) ImGui::PopStyleVar();
    if (privileges_table) {
        ImGui::TableSetupColumn(TR("Permission"), ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 11.0f);
        ImGui::TableSetupColumn(TR("With GRANT"), ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn(TR("Granted by"), ImGuiTableColumnFlags_WidthFixed,
                                ImGui::GetFontSize() * 10.0f);
        ImGui::TableHeadersRow();

        for (const std::string_view privilege : privileges) {
            const std::string name(privilege);
            const db::ObjectPermission* permission = granted(view.grantee, privilege);

            ImGui::TableNextRow();
            ImGui::PushID(name.c_str());

            ImGui::TableSetColumnIndex(0);
            bool has = permission != nullptr;
            ImGui::BeginDisabled(!can_run);
            if (ImGui::Checkbox(name.c_str(), &has)) {
                // A caixa NAO muda sozinha: o que muda o estado e' o comando,
                // depois de confirmado -- e a releitura a redesenha.
                confirm_object_ddl(
                    has ? TRF("Grant on %s", title.c_str())
                        : TRF("Revoke on %s", title.c_str()),
                    has ? db::generate_grant(view.ref, privilege, view.grantee)
                        : db::generate_revoke(view.ref, privilege, view.grantee),
                    document.id(), view.ref);
            }

            ImGui::TableSetColumnIndex(1);
            bool with_grant = permission != nullptr && permission->grantable;
            // PUBLIC nao pode repassar; e so' se repassa o que se tem.
            ImGui::BeginDisabled(permission == nullptr || view.grantee == "PUBLIC");
            if (ImGui::Checkbox("##withgrant", &with_grant)) {
                db::AlterScript script;
                if (with_grant) {
                    script = db::generate_grant(view.ref, privilege, view.grantee, true);
                } else {
                    // Tirar so' a opcao de repassar: REVOKE GRANT OPTION FOR.
                    script = db::generate_revoke(view.ref, privilege, view.grantee);
                    if (script.ok()) {
                        script.statements.front().replace(0, 6,
                                                          "REVOKE GRANT OPTION FOR");
                    }
                }
                confirm_object_ddl(TRF("Grant on %s", title.c_str()), std::move(script),
                                   document.id(), view.ref);
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(2);
            if (permission != nullptr) {
                ImGui::TextColored(col4(p.text_dim), "%s", permission->grantor.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }
    ImGui::EndGroup();
}

// --- Confirmar e aplicar -----------------------------------------------------------

// Como confirm_ddl, mais o que o editor precisa depois: a aba segue o objeto
// quando ele muda de nome ou de schema, e as edicoes pendentes so' somem se o
// servidor aceitou.
void MainShell::confirm_object_ddl(std::string title, db::AlterScript script,
                                   std::size_t document_id,
                                   std::optional<db::ObjectRef> becomes) {
    ddl_reload_schema_.clear();
    ddl_reload_table_.clear();

    ddl_dialog_.open(std::move(title), std::move(script),
                     [this, document_id,
                      becomes = std::move(becomes)](const std::vector<std::string>& statements) {
                         run_ddl(statements);
                         object_apply_.pending     = true;
                         object_apply_.document_id = document_id;
                         object_apply_.becomes     = becomes;
                     });
}

// Chamado quando o script de DDL terminou (draw()).
void MainShell::finish_object_ddl(bool failed) {
    if (!object_apply_.pending) return;
    const ObjectApply apply = std::move(object_apply_);
    object_apply_ = {};

    if (failed) {
        // O erro do servidor na tela: sem isto a janela fechava e nada dizia
        // que o comando nao rodou.
        show_toast(session().status_message());
        return;
    }

    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->id() != apply.document_id) continue;
        ObjectView* view = document->object();
        if (view == nullptr) break;

        if (apply.becomes) {
            view->ref = *apply.becomes;
            document->set_title(view->ref.title());
        }
        view->edits.clear();
        view->source_loaded = false;   // o fonte e' relido do servidor
        break;
    }
}

// --- Arvore: duplo clique, F4 e menu de contexto -------------------------------------

// Chamado logo depois do item (a linha da arvore, ou a linha de uma lista do
// editor). Duplo clique e F4 abrem o editor; o botao direito, o menu.
void MainShell::object_node(const db::ObjectRef& ref, bool toggled) {
    // No MySQL o editor cobre tabela, view, rotina e trigger (SHOW CREATE);
    // para o resto nao ha' o que abrir, e oferecer daria uma aba com erro.
    // No MySQL os tipos sao os do plugin dele (db/mysql_object.hpp); para o
    // resto nao ha' o que abrir, e oferecer daria uma aba com erro.
    //
    // No SQL Server: o que db/mssql_object.cpp sabe descrever.
    if (session().is_mssql() && ref.type != ObjectType::database &&
        ref.type != ObjectType::schema && ref.type != ObjectType::table &&
        ref.type != ObjectType::view && ref.type != ObjectType::function &&
        ref.type != ObjectType::procedure && ref.type != ObjectType::trigger &&
        ref.type != ObjectType::sequence && ref.type != ObjectType::role &&
        ref.type != ObjectType::column && ref.type != ObjectType::index &&
        ref.type != ObjectType::constraint && ref.type != ObjectType::foreign_key) {
        return;
    }
    // No SQL Anywhere: o que db/sqlanywhere_object.cpp sabe descrever.
    if (session().is_sqlanywhere() && ref.type != ObjectType::schema &&
        ref.type != ObjectType::role && ref.type != ObjectType::table &&
        ref.type != ObjectType::view && ref.type != ObjectType::materialized_view &&
        ref.type != ObjectType::function && ref.type != ObjectType::procedure &&
        ref.type != ObjectType::trigger && ref.type != ObjectType::sequence &&
        ref.type != ObjectType::data_type && ref.type != ObjectType::event &&
        ref.type != ObjectType::column && ref.type != ObjectType::index &&
        ref.type != ObjectType::constraint && ref.type != ObjectType::foreign_key) {
        return;
    }
    if (session().is_mysql() && ref.type != ObjectType::table &&
        ref.type != ObjectType::view && ref.type != ObjectType::function &&
        ref.type != ObjectType::procedure && ref.type != ObjectType::trigger &&
        ref.type != ObjectType::event && ref.type != ObjectType::database &&
        ref.type != ObjectType::role && ref.type != ObjectType::column &&
        ref.type != ObjectType::index && ref.type != ObjectType::constraint &&
        ref.type != ObjectType::foreign_key && ref.type != ObjectType::sequence) {
        return;
    }

    // F4, F2, Delete e Alt+Insert sairam daqui: sao comandos da tabela
    // (app_object_open, _rename, _delete, _create), com a tecla do perfil
    // ativo, e agem sobre o no' que nav_track() anota -- o que esta' sob o
    // mouse, ou o ultimo clicado.
    nav_track(ref);

    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        !toggled) {
        open_object_editor(ref);
    }
    if (ImGui::BeginPopupContextItem("##objectmenu")) {
        draw_object_menu(ref);
        ImGui::EndPopup();
    }
}

bool MainShell::can_create_object(db::ObjectType type) const {
    // Oracle: nenhum formulario de criacao ainda -- os que existem geram o
    // SQL dos outros dialetos (docs/ORACLE-MAP.md).
    if (session().is_oracle()) return false;

    // O que so' existe num dos dois: schema, extensao, tablespace, politica,
    // materialized view e event trigger sao do PostgreSQL; evento, do MySQL.
    if (session().is_mssql()) {
        switch (type) {
            case ObjectType::database:
            case ObjectType::schema:
            case ObjectType::table:
            case ObjectType::view:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::constraint:
            case ObjectType::foreign_key:
            case ObjectType::trigger:
            case ObjectType::function:
            case ObjectType::procedure:
            case ObjectType::role:
                return true;
            case ObjectType::sequence:
                return session().has_sequences();   // SQL Server 2012+
            default:
                return false;
        }
    }
    if (session().is_sqlanywhere()) {
        // Banco e schema ficam de fora: CREATE DATABASE cria um ARQUIVO no
        // servidor, e o "schema" e' um usuario (criado na pasta Users).
        switch (type) {
            case ObjectType::table:
            case ObjectType::view:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::constraint:
            case ObjectType::foreign_key:
            case ObjectType::trigger:
            case ObjectType::function:
            case ObjectType::procedure:
            case ObjectType::role:
            case ObjectType::sequence:
            case ObjectType::data_type:
            case ObjectType::event:
                return true;
            default:
                return false;
        }
    }
    if (session().is_mysql()) {
        switch (type) {
            case ObjectType::database:
            case ObjectType::table:
            case ObjectType::view:
            case ObjectType::column:
            case ObjectType::index:
            case ObjectType::constraint:
            case ObjectType::foreign_key:
            case ObjectType::trigger:
            case ObjectType::function:
            case ObjectType::procedure:
            case ObjectType::role:
            case ObjectType::event:
                return true;
            case ObjectType::sequence:
                return session().has_sequences();   // MariaDB 10.3+
            default:
                return false;
        }
    }
    switch (type) {
        case ObjectType::database:
        case ObjectType::schema:
        case ObjectType::table:
        case ObjectType::view:
        case ObjectType::materialized_view:
        case ObjectType::column:
        case ObjectType::index:
        case ObjectType::constraint:
        case ObjectType::foreign_key:
        case ObjectType::trigger:
        case ObjectType::policy:
        case ObjectType::sequence:
        case ObjectType::function:
        case ObjectType::procedure:
        case ObjectType::role:
        case ObjectType::extension:
        case ObjectType::tablespace:
        case ObjectType::event_trigger:
            return true;
        default:
            return false;
    }
}

// "Create New <tipo>" -- o item que o DBeaver poe no menu do no' e da pasta.
void MainShell::create_menu_item(db::ObjectType type, const std::string& schema,
                                 const std::string& parent) {
    if (!can_create_object(type)) return;

    const bool can_run =
        session().state() == SessionState::connected && !session().busy();
    // No MySQL o "papel" e' a conta: o item diz "User", como no DBeaver; no
    // SQL Server, "Login".
    const std::string label =
        TRF("Create New %s", TR(type_label(type, session().engine(), parent)));
    if (!ImGui::MenuItem(label.c_str(), nullptr, false, can_run)) return;

    // Os tres que ja' tinham formulario proprio continuam nele.
    if (type == ObjectType::table) { open_create_table(schema); return; }
    if (type == ObjectType::view)  { open_create_view(schema);  return; }
    if (type == ObjectType::column || type == ObjectType::index) {
        if (const std::optional<db::TableMeta> table = session().table(schema, parent)) {
            if (type == ObjectType::column) open_add_column(schema, *table);
            else                            open_add_index(schema, *table);
        }
        return;
    }

    db::ObjectRef target;
    target.type   = type;
    target.schema = schema;
    target.parent = parent;
    open_object_form(ObjectForm::Kind::create, std::move(target));
}

void MainShell::draw_object_menu(const db::ObjectRef& ref) {
    const bool can_run = session().state() == SessionState::connected && !session().busy();
    const char* type   = TR(type_label(ref.type, session().engine(), ref.parent));

    if (ImGui::MenuItem(TRF("View %s", type), "F4")) open_object_editor(ref);
    if (is_relation(ref.type) && ImGui::MenuItem(TR("View Data"))) {
        open_object_editor(ref, /*data=*/true);
    }

    ImGui::Separator();

    if (session().is_oracle()) {
        // Somente leitura por enquanto: renomear, apagar e as ferramentas
        // gerariam o SQL de outro dialeto. O menu diz isso (diretiva 6).
        if (ImGui::MenuItem(TR("Copy name"))) ImGui::SetClipboardText(ref.name.c_str());
        if (ImGui::MenuItem(TR("Refresh"), "F5", false, can_run)) {
            session().invalidate_object_info(ref);
            session().reload_catalog_async();
        }
        ImGui::Separator();
        ImGui::MenuItem(TR("Creating and altering objects is not implemented for Oracle yet"),
                        nullptr, false, false);
        return;
    }

    // Criar outro do mesmo tipo, no mesmo lugar.
    create_menu_item(ref.type, ref.schema, ref.parent);

    if (db::editable_property(ref.type, "Name") != db::ObjectEdit::none &&
        ImGui::MenuItem(TR("Rename"), "F2", false, can_run)) {
        open_object_form(ObjectForm::Kind::rename, ref);
    }
    if (ImGui::MenuItem(TR("Delete"), "Delete", false, can_run)) {
        open_object_form(ObjectForm::Kind::drop, ref);
    }

    draw_object_tools_menu(ref);

    ImGui::Separator();
    if (ImGui::MenuItem(TR("Copy name"))) ImGui::SetClipboardText(ref.name.c_str());
    if (ImGui::MenuItem(TR("Copy qualified name"))) {
        ImGui::SetClipboardText(db::object_sql_name(ref).c_str());
    }
    if (ImGui::MenuItem(TR("Refresh"), "F5", false, can_run)) {
        session().invalidate_object_info(ref);
        session().reload_catalog_async();
    }
}

// O submenu "Tools" do DBeaver, por tipo de objeto.
void MainShell::draw_object_tools_menu(const db::ObjectRef& ref) {
    if (session().is_mysql()) {
        draw_mysql_tools_menu(ref);
        return;
    }
    if (session().is_mssql()) {
        draw_mssql_tools_menu(ref);
        return;
    }
    if (session().is_sqlanywhere()) {
        draw_sqlanywhere_tools_menu(ref);
        return;
    }
    const bool has_tools =
        ref.type == ObjectType::table || ref.type == ObjectType::materialized_view ||
        ref.type == ObjectType::database || ref.type == ObjectType::index ||
        ref.type == ObjectType::trigger || ref.type == ObjectType::event_trigger ||
        ref.type == ObjectType::schema || ref.type == ObjectType::role;
    if (!has_tools) return;

    const bool can_run =
        session().state() == SessionState::connected && !session().busy();

    if (!ImGui::BeginMenu(TR("Tools"), can_run)) return;

    const bool whole_database = ref.type == ObjectType::database;
    const std::string schema = whole_database ? std::string{} : ref.schema;
    const std::string table  = whole_database ? std::string{} : ref.name;

    if (ref.type == ObjectType::table || ref.type == ObjectType::materialized_view ||
        whole_database) {
        if (ImGui::MenuItem(TR("Analyze"))) {
            confirm_object_ddl(TRF("Analyze %s", ref.name.c_str()),
                               db::generate_analyze(schema, table));
        }
        if (ImGui::MenuItem(TR("Vacuum"))) {
            open_object_form(ObjectForm::Kind::vacuum, ref);
        }
    }
    if (ref.type == ObjectType::table && ImGui::MenuItem(TR("Truncate"))) {
        open_object_form(ObjectForm::Kind::truncate, ref);
    }
    if (ref.type == ObjectType::materialized_view &&
        ImGui::MenuItem(TR("Refresh Materialized View"))) {
        open_object_form(ObjectForm::Kind::refresh_mview, ref);
    }
    if (ref.type == ObjectType::trigger || ref.type == ObjectType::event_trigger) {
        if (ImGui::MenuItem(TR("Enable trigger"))) {
            confirm_object_ddl(TRF("Enable trigger %s", ref.name.c_str()),
                               db::generate_trigger_enable(ref, true));
        }
        if (ImGui::MenuItem(TR("Disable trigger"))) {
            confirm_object_ddl(TRF("Disable trigger %s", ref.name.c_str()),
                               db::generate_trigger_enable(ref, false));
        }
    }
    if (ref.type == ObjectType::table || ref.type == ObjectType::materialized_view ||
        ref.type == ObjectType::index || ref.type == ObjectType::schema ||
        whole_database) {
        if (ImGui::MenuItem(TR("Reindex"))) {
            confirm_object_ddl(TRF("Reindex %s", ref.name.c_str()),
                               db::generate_reindex(ref));
        }
    }
    if (ref.type == ObjectType::role && ImGui::MenuItem(TR("Change password..."))) {
        open_object_form(ObjectForm::Kind::password, ref);
    }

    // Backup e Restore: pelo cliente nativo (pg_dump / pg_restore), como no
    // DBeaver.
    if (whole_database || ref.type == ObjectType::schema || ref.type == ObjectType::table) {
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Backup"))) open_backup(ref);
        if (whole_database && ImGui::MenuItem(TR("Restore"))) open_restore(ref.name);
    }
    ImGui::EndMenu();
}

// --- Formularios -------------------------------------------------------------------

void MainShell::open_object_form(ObjectForm::Kind kind, db::ObjectRef target) {
    object_form_        = {};
    object_form_.kind   = kind;
    object_form_.target = std::move(target);

    if (kind == ObjectForm::Kind::rename) {
        std::snprintf(object_form_.name, sizeof object_form_.name, "%s",
                      object_form_.target.name.c_str());
    }
    if (kind == ObjectForm::Kind::create) {
        // Os padroes do DBeaver.
        switch (object_form_.target.type) {
            case ObjectType::function:
                std::snprintf(object_form_.text_a, sizeof object_form_.text_a, "sql");
                std::snprintf(object_form_.text_b, sizeof object_form_.text_b, "int4");
                break;
            case ObjectType::procedure:
                std::snprintf(object_form_.text_a, sizeof object_form_.text_a, "sql");
                object_form_.flag_a = true;   // e' procedure
                break;
            case ObjectType::role:
                object_form_.flag_a = true;   // "Is user"
                break;
            case ObjectType::sequence:
                std::snprintf(object_form_.text_a, sizeof object_form_.text_a, "1");
                std::snprintf(object_form_.text_b, sizeof object_form_.text_b, "1");
                break;
            case ObjectType::policy:
                object_form_.flag_a = true;   // permissiva
                std::snprintf(object_form_.text_a, sizeof object_form_.text_a, "PUBLIC");
                break;
            case ObjectType::extension:
                std::snprintf(object_form_.text_a, sizeof object_form_.text_a, "public");
                break;
            case ObjectType::materialized_view:
                object_form_.flag_a = true;   // WITH DATA
                break;
            default:
                break;
        }
    }
    if (kind == ObjectForm::Kind::refresh_mview) object_form_.flag_a = true;
}

// Um combo alimentado por uma lista do catalogo. Vazio e' escolha valida
// ("o padrao do servidor") quando `allow_empty`.
bool MainShell::list_combo(const char* id, db::CatalogList list, char* buffer,
                           std::size_t size, bool allow_empty, bool only_unflagged) {
    const Session::ListState state = session().list(list);
    if (!state.loaded && !session().busy()) session().load_list_async(list);

    bool changed = false;
    if (ImGui::BeginCombo(id, buffer[0] != '\0' ? buffer : TR("(default)"))) {
        if (allow_empty && ImGui::Selectable(TR("(default)"), buffer[0] == '\0')) {
            buffer[0] = '\0';
            changed   = true;
        }
        for (const db::CatalogItem& item : state.items) {
            if (only_unflagged && item.flag) continue;
            const std::string label =
                item.detail.empty() ? item.name : item.name + "   " + item.detail;
            ImGui::PushID(item.name.c_str());
            if (ImGui::Selectable(label.c_str(), item.name == buffer)) {
                std::snprintf(buffer, size, "%s", item.name.c_str());
                changed = true;
            }
            if (!item.tooltip.empty() && ImGui::IsItemHovered()) {
                hint_fmt("%s", item.tooltip.c_str());
            }
            ImGui::PopID();
        }
        if (!state.loaded) ImGui::TextDisabled("%s", TR("loading..."));
        ImGui::EndCombo();
    }
    return changed;
}

void MainShell::draw_object_forms() {
    // "objectmenu" do canal de comandos: o menu de contexto de um no', numa
    // janela propria, para a captura de tela (um popup de verdade exige o
    // clique direito que a automacao nao da').
    if (object_menu_debug_open_) {
        bool open = true;
        ImGui::SetNextWindowPos(ImVec2(420.0f, 120.0f), ImGuiCond_Appearing);
        // Opaca (diretiva 13): com a translucidez dos paineis, a grade de tras
        // atravessava os itens e a captura nao deixava ler o menu.
        ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(colors().bg_darkest, 1.0f)));
        if (ImGui::Begin("Object menu###ObjectMenuDebug", &open,
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoFocusOnAppearing)) {
            draw_object_menu(object_menu_debug_);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        if (!open) object_menu_debug_open_ = false;
    }

    ObjectForm& form = object_form_;
    if (form.kind == ObjectForm::Kind::none) {
        object_form_submit_ = false;
        return;
    }

    const Palette& p = colors();
    const db::ObjectRef& target = form.target;
    const ObjectType type = target.type;

    // O titulo de cada dialogo e' o do DBeaver.
    std::string title;
    switch (form.kind) {
        case ObjectForm::Kind::create:
            switch (type) {
                case ObjectType::database:      title = TR("Create database"); break;
                case ObjectType::schema:        title = TR("Create schema"); break;
                case ObjectType::extension:     title = TR("Install extensions"); break;
                case ObjectType::role:
                    title = session().is_mssql()   ? TR("Create login")
                            : session().is_mysql() ? TR("Create user")
                            : session().is_sqlanywhere() && target.parent != "role"
                                ? TR("Create user")
                                : TR("Create role");
                    break;
                case ObjectType::tablespace:    title = TR("Create tablespace"); break;
                case ObjectType::event_trigger: title = TR("Create new Event Trigger"); break;
                default:
                    title = TRF("Create New %s",
                                TR(type_label(type, session().engine(), target.parent)));
                    break;
            }
            break;
        case ObjectForm::Kind::rename:
            title = TRF("Rename %s",
                        TR(type_label(type, session().engine(), target.parent)));
            break;
        case ObjectForm::Kind::drop:
            title = TRF("Delete %s",
                        TR(type_label(type, session().engine(), target.parent)));
            break;
        case ObjectForm::Kind::sa_backup:
        case ObjectForm::Kind::ms_backup:     title = TR("Backup database"); break;
        case ObjectForm::Kind::ms_restore:    title = TR("Restore database"); break;
        case ObjectForm::Kind::vacuum:        title = TR("Vacuum"); break;
        case ObjectForm::Kind::truncate:      title = TR("Truncate"); break;
        case ObjectForm::Kind::refresh_mview: title = TR("Refresh Materialized View"); break;
        case ObjectForm::Kind::password:      title = TR("Change password..."); break;
        case ObjectForm::Kind::none:          break;
    }
    title += "###ObjectForm";

    bool open = true;
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    // Fundo OPACO (diretiva 13): o dialogo flutua sobre o editor, e com a
    // translucidez dos paineis a lista de tras atravessava os campos.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool form_visible =
        ImGui::Begin(title.c_str(), &open,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    if (!form_visible) {
        ImGui::End();
        if (!open) form.kind = ObjectForm::Kind::none;
        return;
    }

    // Onde o objeto vai nascer, ou sobre qual objeto e' a acao.
    {
        std::string where;
        if (!target.schema.empty()) where += target.schema;
        if (!target.parent.empty()) where += (where.empty() ? "" : ".") + target.parent;
        if (form.kind != ObjectForm::Kind::create && !target.name.empty()) {
            where += (where.empty() ? "" : ".") + target.title();
        }
        if (!where.empty()) {
            ImGui::TextColored(col4(p.text_dim), "%s", where.c_str());
            ImGui::Separator();
        }
    }

    db::AlterScript script;        // o que o botao principal vai confirmar
    std::string     action_title;
    const char*     button = TR("Review SQL");
    bool            direct_source = false;   // abre no editor em vez de executar
    std::string     source_text;

    const auto text_field = [](const char* label, char* buffer, std::size_t size,
                               ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) {
        form_label(label);
        ImGui::PushID(label);
        ImGui::InputText("##field", buffer, size, flags);
        ImGui::PopID();
    };
    const auto check_field = [](const char* label, bool* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::Checkbox(label, value);
    };
    const auto choice_field = [](const char* label, int* value,
                                 std::initializer_list<const char*> options) {
        form_label(label);
        ImGui::PushID(label);
        const char* current = *value >= 0 && *value < static_cast<int>(options.size())
                                  ? *(options.begin() + *value)
                                  : "";
        if (ImGui::BeginCombo("##choice", current)) {
            int index = 0;
            for (const char* option : options) {
                if (ImGui::Selectable(option, index == *value)) *value = index;
                ++index;
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return current;
    };

    if (form.kind == ObjectForm::Kind::create) {
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();

        switch (type) {
            case ObjectType::database: {
                if (session().is_mssql()) {
                    // SQLServerCreateDatabaseDialog: General > Database name. O
                    // collation e' o que o CREATE DATABASE aceita a mais.
                    ImGui::SeparatorText(TR("General"));
                    if (begin_form("##msdb")) {
                        text_field(TR("Database name"), form.name, sizeof form.name);
                        text_field(TR("Collation"), form.text_a, sizeof form.text_a);
                        ImGui::EndTable();
                    }
                    script       = db::mssql_create_database(form.name, form.text_a);
                    action_title = TRF("Create database %s", form.name);
                    break;
                }
                if (session().is_mysql()) {
                    // MySQLCreateDatabaseDialog: Database name, Charset, Collation.
                    if (!session().charsets_loaded() && !session().busy()) {
                        session().load_server_info_async(Session::ServerInfo::charsets);
                    }
                    if (begin_form("##mydb")) {
                        text_field(TR("Database name"), form.name, sizeof form.name);
                        form_label(TR("Charset"));
                        if (ImGui::BeginCombo("##charset", form.text_a[0] != '\0'
                                                               ? form.text_a
                                                               : TR("(default)"))) {
                            if (ImGui::Selectable(TR("(default)"), form.text_a[0] == '\0')) {
                                form.text_a[0] = '\0';
                                form.text_b[0] = '\0';
                            }
                            for (const db::ServerVariable& charset : session().charsets()) {
                                if (ImGui::Selectable(charset.name.c_str(),
                                                      charset.name == form.text_a)) {
                                    std::snprintf(form.text_a, sizeof form.text_a, "%s",
                                                  charset.name.c_str());
                                    // O collation padrao do charset escolhido,
                                    // como o dialogo do DBeaver preenche.
                                    std::snprintf(form.text_b, sizeof form.text_b, "%s",
                                                  charset.value.c_str());
                                }
                            }
                            ImGui::EndCombo();
                        }
                        text_field(TR("Collation"), form.text_b, sizeof form.text_b);
                        ImGui::EndTable();
                    }
                    script = db::mysql_create_database(form.name, form.text_a, form.text_b);
                    action_title = TRF("Create database %s", form.name);
                    break;
                }
                // General: Database name, Owner. Definition: Template database,
                // Encoding, Tablespace.
                ImGui::SeparatorText(TR("General"));
                if (begin_form("##db1")) {
                    text_field(TR("Database name"), form.name, sizeof form.name);
                    form_label(TR("Owner"));
                    list_combo("##owner", db::CatalogList::roles, form.text_a,
                               sizeof form.text_a, true);
                    ImGui::EndTable();
                }
                ImGui::SeparatorText(TR("Definition"));
                if (begin_form("##db2")) {
                    form_label(TR("Template database"));
                    if (ImGui::BeginCombo("##template", form.text_b[0] != '\0'
                                                            ? form.text_b
                                                            : TR("(default)"))) {
                        if (ImGui::Selectable(TR("(default)"), form.text_b[0] == '\0')) {
                            form.text_b[0] = '\0';
                        }
                        for (const char* name : {"template0", "template1"}) {
                            if (ImGui::Selectable(name, std::strcmp(form.text_b, name) == 0)) {
                                std::snprintf(form.text_b, sizeof form.text_b, "%s", name);
                            }
                        }
                        for (const db::DatabaseMeta& database : session().databases()) {
                            if (ImGui::Selectable(database.name.c_str(),
                                                  database.name == form.text_b)) {
                                std::snprintf(form.text_b, sizeof form.text_b, "%s",
                                              database.name.c_str());
                            }
                        }
                        ImGui::EndCombo();
                    }
                    form_label(TR("Encoding"));
                    list_combo("##encoding", db::CatalogList::encodings, form.text_c,
                               sizeof form.text_c, true);
                    form_label(TR("Tablespace"));
                    list_combo("##tablespace", db::CatalogList::tablespaces, form.text_d,
                               sizeof form.text_d, true);
                    ImGui::EndTable();
                }
                db::NewDatabase database;
                database.name        = form.name;
                database.owner       = form.text_a;
                database.template_db = form.text_b;
                database.encoding    = form.text_c;
                database.tablespace  = form.text_d;
                script       = db::generate_create_database(database);
                action_title = TRF("Create database %s", form.name);
                break;
            }

            case ObjectType::schema:
                if (begin_form("##schema")) {
                    text_field(TR("Schema name"), form.name, sizeof form.name);
                    {
                        char database[256];
                        std::snprintf(database, sizeof database, "%s",
                                      session().database_name().c_str());
                        text_field(TR("Database"), database, sizeof database,
                                   ImGuiInputTextFlags_ReadOnly);
                    }
                    form_label(TR("Owner"));
                    list_combo("##owner", db::CatalogList::roles, form.text_a,
                               sizeof form.text_a, true);
                    ImGui::EndTable();
                }
                script       = session().is_mssql()
                                   ? db::mssql_create_schema(form.name, form.text_a)
                                   : db::generate_create_schema(form.name, form.text_a);
                action_title = TRF("Create schema %s", form.name);
                break;

            case ObjectType::extension: {
                if (begin_form("##extension", 150.0f, 420.0f)) {
                    {
                        char database[256];
                        std::snprintf(database, sizeof database, "%s",
                                      session().database_name().c_str());
                        text_field(TR("Database"), database, sizeof database,
                                   ImGuiInputTextFlags_ReadOnly);
                    }
                    form_label(TR("Schema"));
                    if (ImGui::BeginCombo("##schema", form.text_a)) {
                        for (const db::SchemaMeta& schema : session().schemas()) {
                            if (ImGui::Selectable(schema.name.c_str(),
                                                  schema.name == form.text_a)) {
                                std::snprintf(form.text_a, sizeof form.text_a, "%s",
                                              schema.name.c_str());
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::EndTable();
                }

                // A lista das disponiveis, como a tabela do DBeaver: Name,
                // Version, Description. As ja' instaladas ficam de fora.
                ImGui::TextColored(col4(p.text_dim), "%s", TR("Extension"));
                const Session::ListState available =
                    session().list(db::CatalogList::available_extensions);
                if (!available.loaded && !session().busy()) {
                    session().load_list_async(db::CatalogList::available_extensions);
                }
                if (ImGui::BeginTable("##available", 3,
                                      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_ScrollY |
                                          ImGuiTableFlags_SizingFixedFit,
                                      ImVec2(600.0f, 260.0f))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn(TR("Name"), ImGuiTableColumnFlags_WidthFixed, 160);
                    ImGui::TableSetupColumn(TR("Version"), ImGuiTableColumnFlags_WidthFixed, 70);
                    ImGui::TableSetupColumn(TR("Description"),
                                            ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (const db::CatalogItem& item : available.items) {
                        if (item.flag) continue;   // ja' instalada
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (ImGui::Selectable(item.name.c_str(), item.name == form.name,
                                              ImGuiSelectableFlags_SpanAllColumns)) {
                            std::snprintf(form.name, sizeof form.name, "%s",
                                          item.name.c_str());
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(item.detail.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(item.tooltip.c_str());
                    }
                    ImGui::EndTable();
                }
                script       = db::generate_create_extension(form.name, form.text_a);
                action_title = TRF("Install extension %s", form.name);
                break;
            }

            case ObjectType::role: {
                if (session().is_sqlanywhere() && target.parent == "role") {
                    // O papel puro so' tem nome: quem o recebe vem depois, por GRANT.
                    if (begin_form("##sarole")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        ImGui::EndTable();
                    }
                    script       = db::sqlanywhere_create_role(form.name);
                    action_title = TRF("Create role %s", form.name);
                    break;
                }
                if (session().is_sqlanywhere()) {
                    // O "Create User" do Sybase Central: nome, senha e a
                    // politica de login. Sem senha o usuario nao entra -- so'
                    // serve de dono de objetos, e a revisao avisa.
                    if (begin_form("##sauser")) {
                        text_field(TR("User Name"), form.name, sizeof form.name);
                        text_field(TR("Password"), form.text_a, sizeof form.text_a,
                                   ImGuiInputTextFlags_Password);
                        text_field(TR("Confirm password"), form.text_b, sizeof form.text_b,
                                   ImGuiInputTextFlags_Password);
                        form_label(TR("Login policy"));
                        list_combo("##policy", db::CatalogList::login_policies,
                                   form.text_c, sizeof form.text_c, true);
                        ImGui::EndTable();
                    }
                    if (std::strcmp(form.text_a, form.text_b) != 0) {
                        script.error = "the two passwords are different";
                    } else {
                        db::SqlAnywhereNewUser user;
                        user.name         = form.name;
                        user.password     = form.text_a;
                        user.login_policy = form.text_c;
                        script = db::sqlanywhere_create_user(user);
                    }
                    action_title = TRF("Create user %s", form.name);
                    break;
                }
                if (session().is_mssql()) {
                    // SQLServerLoginConfigurator ("Create login"): nome e senha.
                    // O banco padrao e' o que o CREATE LOGIN aceita a mais.
                    if (begin_form("##mslogin")) {
                        text_field(TR("Login Name"), form.name, sizeof form.name);
                        text_field(TR("Password"), form.text_a, sizeof form.text_a,
                                   ImGuiInputTextFlags_Password);
                        text_field(TR("Confirm password"), form.text_b, sizeof form.text_b,
                                   ImGuiInputTextFlags_Password);
                        form_label(TR("Default database"));
                        if (ImGui::BeginCombo("##defaultdb", form.text_c[0] != '\0'
                                                                 ? form.text_c
                                                                 : TR("(default)"))) {
                            if (ImGui::Selectable(TR("(default)"), form.text_c[0] == '\0')) {
                                form.text_c[0] = '\0';
                            }
                            for (const db::DatabaseMeta& database : session().databases()) {
                                if (ImGui::Selectable(database.name.c_str(),
                                                      database.name == form.text_c)) {
                                    std::snprintf(form.text_c, sizeof form.text_c, "%s",
                                                  database.name.c_str());
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::EndTable();
                    }
                    if (std::strcmp(form.text_a, form.text_b) != 0) {
                        script.error = "the two passwords are different";
                    } else {
                        db::MssqlNewLogin login;
                        login.name             = form.name;
                        login.password         = form.text_a;
                        login.default_database = form.text_c;
                        script = db::mssql_create_login(login);
                    }
                    action_title = TRF("Create login %s", form.name);
                    break;
                }
                if (session().is_mysql()) {
                    // MySQLUserEditorGeneral: User Name, Host, Password, Confirm.
                    if (begin_form("##myuser")) {
                        text_field(TR("User Name"), form.name, sizeof form.name);
                        text_field(TR("Host"), form.text_c, sizeof form.text_c);
                        text_field(TR("Password"), form.text_a, sizeof form.text_a,
                                   ImGuiInputTextFlags_Password);
                        text_field(TR("Confirm password"), form.text_b, sizeof form.text_b,
                                   ImGuiInputTextFlags_Password);
                        ImGui::EndTable();
                    }
                    if (std::strcmp(form.text_a, form.text_b) != 0) {
                        script.error = "the two passwords are different";
                    } else {
                        db::MysqlNewUser user;
                        user.name     = form.name;
                        user.host     = form.text_c;
                        user.password = form.text_a;
                        script = db::mysql_create_user(user);
                    }
                    action_title = TRF("Create user %s", form.name);
                    break;
                }
                ImGui::SeparatorText(TR("Settings"));
                if (begin_form("##role")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    ImGui::BeginDisabled(!form.flag_a);
                    text_field(TR("Password"), form.text_a, sizeof form.text_a,
                               ImGuiInputTextFlags_Password);
                    ImGui::EndDisabled();
                    check_field(TR("Is user"), &form.flag_a);
                    ImGui::EndTable();
                }
                db::NewRole role;
                role.name     = form.name;
                role.password = form.text_a;
                role.is_user  = form.flag_a;
                script       = db::generate_create_role(role);
                action_title = TRF("Create role %s", form.name);
                break;
            }

            case ObjectType::tablespace: {
                if (begin_form("##tablespace")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    form_label(TR("Owner"));
                    list_combo("##owner", db::CatalogList::roles, form.text_a,
                               sizeof form.text_a, true);
                    text_field(TR("Location"), form.text_b, sizeof form.text_b);
                    text_field(TR("Options"), form.text_c, sizeof form.text_c);
                    ImGui::EndTable();
                }
                db::NewTablespace tablespace;
                tablespace.name     = form.name;
                tablespace.owner    = form.text_a;
                tablespace.location = form.text_b;
                tablespace.options  = form.text_c;
                script       = db::generate_create_tablespace(tablespace);
                action_title = TRF("Create tablespace %s", form.name);
                break;
            }

            case ObjectType::function:
            case ObjectType::procedure: {
                if (session().is_mssql()) {
                    // SQLServerProcedureConfigurator ("New Procedure"): Name, Type.
                    if (begin_form("##msroutine")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        form_label(TR("Type"));
                        if (ImGui::BeginCombo("##kind", form.flag_a ? "PROCEDURE"
                                                                   : "FUNCTION")) {
                            if (ImGui::Selectable("FUNCTION", !form.flag_a)) form.flag_a = false;
                            if (ImGui::Selectable("PROCEDURE", form.flag_a)) form.flag_a = true;
                            ImGui::EndCombo();
                        }
                        ImGui::EndTable();
                    }
                    source_text   = db::mssql_routine_template(target.schema, form.name,
                                                               form.flag_a);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                if (session().is_sqlanywhere()) {
                    // Nome e tipo; o corpo (Watcom SQL) nasce no editor.
                    if (begin_form("##saroutine")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        form_label(TR("Type"));
                        if (ImGui::BeginCombo("##kind", form.flag_a ? "PROCEDURE"
                                                                   : "FUNCTION")) {
                            if (ImGui::Selectable("FUNCTION", !form.flag_a)) form.flag_a = false;
                            if (ImGui::Selectable("PROCEDURE", form.flag_a)) form.flag_a = true;
                            ImGui::EndCombo();
                        }
                        ImGui::EndTable();
                    }
                    source_text   = db::sqlanywhere_routine_template(target.schema, form.name,
                                                                     form.flag_a);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                if (session().is_mysql()) {
                    // MySQLProcedureConfigurator: Name, Type.
                    if (begin_form("##myroutine")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        form_label(TR("Type"));
                        if (ImGui::BeginCombo("##kind", form.flag_a ? "PROCEDURE"
                                                                   : "FUNCTION")) {
                            if (ImGui::Selectable("FUNCTION", !form.flag_a)) form.flag_a = false;
                            if (ImGui::Selectable("PROCEDURE", form.flag_a)) form.flag_a = true;
                            ImGui::EndCombo();
                        }
                        ImGui::EndTable();
                    }
                    source_text   = db::mysql_routine_template(target.schema, form.name,
                                                               form.flag_a);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                if (begin_form("##routine")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    form_label(TR("Type"));
                    int kind = form.flag_a ? 1 : 0;
                    if (ImGui::BeginCombo("##kind", kind == 1 ? "PROCEDURE" : "FUNCTION")) {
                        if (ImGui::Selectable("FUNCTION", kind == 0)) form.flag_a = false;
                        if (ImGui::Selectable("PROCEDURE", kind == 1)) form.flag_a = true;
                        ImGui::EndCombo();
                    }
                    form_label(TR("Language"));
                    list_combo("##language", db::CatalogList::languages, form.text_a,
                               sizeof form.text_a, false);
                    ImGui::BeginDisabled(form.flag_a);
                    text_field(TR("Return type"), form.text_b, sizeof form.text_b);
                    ImGui::EndDisabled();
                    ImGui::EndTable();
                }
                db::NewRoutine routine;
                routine.schema      = target.schema;
                routine.name        = form.name;
                routine.procedure   = form.flag_a;
                routine.language    = form.text_a;
                routine.return_type = form.text_b;
                source_text   = db::routine_template(routine);
                direct_source = true;
                button        = TR("Open in editor");
                // So' para validar o nome: o texto vai para o editor.
                if (form.name[0] != '\0') script.statements.push_back(source_text);
                else                      script.error = "the name is required";
                break;
            }

            case ObjectType::event_trigger: {
                const auto& events = db::event_trigger_events();
                if (begin_form("##eventtrigger")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    form_label(TR("Event Type"));
                    const std::string current(events[static_cast<std::size_t>(
                        std::clamp(form.choice_a, 0, static_cast<int>(events.size()) - 1))]);
                    if (ImGui::BeginCombo("##event", current.c_str())) {
                        for (int i = 0; i < static_cast<int>(events.size()); ++i) {
                            const std::string name(events[static_cast<std::size_t>(i)]);
                            if (ImGui::Selectable(name.c_str(), i == form.choice_a)) {
                                form.choice_a = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    form_label(TR("Trigger function"));
                    routine_combo("##function", form.text_a, sizeof form.text_a,
                                  "event_trigger");
                    ImGui::EndTable();
                }
                script = db::generate_create_event_trigger(
                    form.name,
                    events[static_cast<std::size_t>(
                        std::clamp(form.choice_a, 0, static_cast<int>(events.size()) - 1))],
                    form.text_a);
                action_title = TRF("Create event trigger %s", form.name);
                break;
            }

            case ObjectType::event: {
                // MySQLEventConfigurator: so' o nome; o resto e' o fonte.
                if (begin_form("##myevent")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    ImGui::EndTable();
                }
                // No SQL Anywhere o evento nao tem dono no nome: e' do banco.
                source_text   = session().is_sqlanywhere()
                                    ? db::sqlanywhere_event_template(form.name)
                                    : db::mysql_event_template(target.schema, form.name);
                direct_source = true;
                button        = TR("Open in editor");
                if (form.name[0] != '\0') script.statements.push_back(source_text);
                else                      script.error = "the name is required";
                break;
            }

            case ObjectType::trigger: {
                if (session().is_mssql()) {
                    // No T-SQL o corpo do trigger e' o codigo, e so' existem
                    // AFTER e INSTEAD OF -- nasce no editor.
                    const char* ms_timing = "";
                    const char* ms_event  = "";
                    if (begin_form("##mstrigger")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        ms_timing = choice_field(TR("Timing"), &form.choice_a,
                                                 {"AFTER", "INSTEAD OF"});
                        ms_event = choice_field(TR("Event"), &form.choice_b,
                                                {"INSERT", "UPDATE", "DELETE",
                                                 "INSERT, UPDATE",
                                                 "INSERT, UPDATE, DELETE"});
                        ImGui::EndTable();
                    }
                    source_text = db::mssql_trigger_template(target.schema, target.parent,
                                                             form.name, ms_timing, ms_event);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                if (session().is_sqlanywhere()) {
                    // O corpo e' Watcom SQL, com REFERENCING para as linhas
                    // nova e antiga -- nasce no editor.
                    const char* sa_timing = "";
                    const char* sa_event  = "";
                    if (begin_form("##satrigger")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        sa_timing = choice_field(TR("Timing"), &form.choice_a,
                                                 {"BEFORE", "AFTER", "INSTEAD OF"});
                        sa_event = choice_field(TR("Event"), &form.choice_b,
                                                {"INSERT", "UPDATE", "DELETE",
                                                 "INSERT, UPDATE",
                                                 "INSERT, UPDATE, DELETE"});
                        ImGui::EndTable();
                    }
                    source_text = db::sqlanywhere_trigger_template(
                        target.schema, target.parent, form.name, sa_timing, sa_event);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                if (session().is_mysql()) {
                    // MySQLTriggerConfigurator: o corpo do trigger E' o codigo,
                    // nao a chamada de uma funcao -- nasce no editor.
                    const char* my_timing = "";
                    const char* my_event  = "";
                    if (begin_form("##mytrigger")) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        my_timing = choice_field(TR("Timing"), &form.choice_a,
                                                 {"BEFORE", "AFTER"});
                        my_event = choice_field(TR("Event"), &form.choice_b,
                                                {"INSERT", "UPDATE", "DELETE"});
                        ImGui::EndTable();
                    }
                    source_text = db::mysql_trigger_template(target.schema, target.parent,
                                                             form.name, my_timing, my_event);
                    direct_source = true;
                    button        = TR("Open in editor");
                    if (form.name[0] != '\0') script.statements.push_back(source_text);
                    else                      script.error = "the name is required";
                    break;
                }
                const char* timing = "";
                const char* event  = "";
                if (begin_form("##trigger")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    timing = choice_field(TR("Timing"), &form.choice_a,
                                          {"BEFORE", "AFTER", "INSTEAD OF"});
                    event = choice_field(TR("Event"), &form.choice_b,
                                         {"INSERT", "UPDATE", "DELETE",
                                          "INSERT OR UPDATE",
                                          "INSERT OR UPDATE OR DELETE"});
                    form_label(TR("Trigger function"));
                    routine_combo("##function", form.text_a, sizeof form.text_a, "trigger");
                    ImGui::EndTable();
                }
                db::NewTrigger trigger;
                trigger.name   = form.name;
                trigger.table  = target.parent;
                trigger.timing = timing;
                trigger.event  = event;
                trigger.body   = form.text_a;
                // "schema.fn" -> "schema.fn()": o comando chama a funcao.
                if (!trigger.body.empty() && trigger.body.find('(') == std::string::npos) {
                    trigger.body += "()";
                }
                script       = db::generate_create_trigger(target.schema, trigger);
                action_title = TRF("Create trigger %s", form.name);
                break;
            }

            case ObjectType::policy: {
                const char* command = "";
                if (begin_form("##policy", 150.0f, 360.0f)) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    command = choice_field(TR("Command"), &form.choice_a,
                                           {"ALL", "SELECT", "INSERT", "UPDATE", "DELETE"});
                    check_field(TR("Permissive"), &form.flag_a);
                    text_field(TR("Roles"), form.text_a, sizeof form.text_a);
                    text_field(TR("Using"), form.text_b, sizeof form.text_b);
                    text_field(TR("With check"), form.text_c, sizeof form.text_c);
                    ImGui::EndTable();
                }
                db::NewPolicy policy;
                policy.schema           = target.schema;
                policy.table            = target.parent;
                policy.name             = form.name;
                policy.command          = command;
                policy.permissive       = form.flag_a;
                policy.roles            = form.text_a;
                policy.using_expression = form.text_b;
                policy.check_expression = form.text_c;
                script       = db::generate_create_policy(policy);
                action_title = TRF("Create policy %s", form.name);
                break;
            }

            case ObjectType::sequence: {
                if (begin_form("##sequence")) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    text_field(TR("Start"), form.text_a, sizeof form.text_a,
                               ImGuiInputTextFlags_CharsDecimal);
                    text_field(TR("Increment"), form.text_b, sizeof form.text_b,
                               ImGuiInputTextFlags_CharsDecimal);
                    text_field(TR("Minimum"), form.text_c, sizeof form.text_c,
                               ImGuiInputTextFlags_CharsDecimal);
                    text_field(TR("Maximum"), form.text_d, sizeof form.text_d,
                               ImGuiInputTextFlags_CharsDecimal);
                    check_field(TR("Cycle"), &form.flag_a);
                    ImGui::EndTable();
                }
                if (session().is_sqlanywhere()) {
                    // Os numeros vao como texto: vazio e' "o padrao do servidor",
                    // que zero nao saberia dizer.
                    db::SqlAnywhereNewSequence anywhere;
                    anywhere.schema    = target.schema;
                    anywhere.name      = form.name;
                    anywhere.start     = form.text_a;
                    anywhere.increment = form.text_b;
                    anywhere.minimum   = form.text_c;
                    anywhere.maximum   = form.text_d;
                    anywhere.cycle     = form.flag_a;
                    script       = db::sqlanywhere_create_sequence(anywhere);
                    action_title = TRF("Create sequence %s", form.name);
                    break;
                }
                db::NewSequence sequence;
                sequence.name      = form.name;
                sequence.start     = std::strtoll(form.text_a, nullptr, 10);
                sequence.increment = std::strtoll(form.text_b, nullptr, 10);
                sequence.minimum   = std::strtoll(form.text_c, nullptr, 10);
                sequence.maximum   = std::strtoll(form.text_d, nullptr, 10);
                sequence.cycle     = form.flag_a;
                script       = db::generate_create_sequence(target.schema, sequence);
                action_title = TRF("Create sequence %s", form.name);
                break;
            }

            case ObjectType::constraint: {
                const char* kind = "";
                if (begin_form("##constraint", 150.0f, 360.0f)) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    kind = choice_field(TR("Type"), &form.choice_a,
                                        {"PRIMARY KEY", "UNIQUE", "CHECK"});
                    if (form.choice_a == 2) {
                        text_field(TR("Expression"), form.text_b, sizeof form.text_b);
                    } else {
                        text_field(TR("Columns"), form.text_a, sizeof form.text_a);
                    }
                    ImGui::EndTable();
                }
                (void)kind;
                db::NewConstraint constraint;
                constraint.name = form.name;
                constraint.kind = form.choice_a == 0   ? db::ConstraintKind::primary_key
                                  : form.choice_a == 1 ? db::ConstraintKind::unique
                                                       : db::ConstraintKind::check;
                constraint.columns    = split_list(form.text_a);
                constraint.expression = form.text_b;
                script = db::generate_add_constraint(target.schema, target.parent, constraint);
                action_title = TRF("Create constraint %s", form.name);
                break;
            }

            case ObjectType::foreign_key: {
                const char* on_delete = "";
                const char* on_update = "";
                if (begin_form("##fk", 150.0f, 360.0f)) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    text_field(TR("Columns"), form.text_a, sizeof form.text_a);
                    text_field(TR("Ref Table"), form.text_b, sizeof form.text_b);
                    text_field(TR("Ref Columns"), form.text_c, sizeof form.text_c);
                    on_delete = choice_field(TR("On Delete"), &form.choice_a,
                                             {"NO ACTION", "RESTRICT", "CASCADE",
                                              "SET NULL", "SET DEFAULT"});
                    on_update = choice_field(TR("On Update"), &form.choice_b,
                                             {"NO ACTION", "RESTRICT", "CASCADE",
                                              "SET NULL", "SET DEFAULT"});
                    ImGui::EndTable();
                }
                db::NewForeignKey key;
                key.name    = form.name;
                key.columns = split_list(form.text_a);
                // "schema.tabela" ou so' "tabela" (no mesmo schema).
                const std::string reference = form.text_b;
                const std::size_t dot = reference.find('.');
                key.target_schema  = dot == std::string::npos ? target.schema
                                                              : reference.substr(0, dot);
                key.target_table   = dot == std::string::npos ? reference
                                                              : reference.substr(dot + 1);
                key.target_columns = split_list(form.text_c);
                key.on_delete      = on_delete;
                key.on_update      = on_update;
                script = db::generate_add_foreign_key(target.schema, target.parent, key);
                action_title = TRF("Create foreign key %s", form.name);
                break;
            }

            case ObjectType::materialized_view: {
                if (begin_form("##mview", 150.0f, 520.0f)) {
                    text_field(TR("Name"), form.name, sizeof form.name);
                    form_label(TR("Query"));
                    ImGui::InputTextMultiline("##query", form.body, sizeof form.body,
                                              ImVec2(-FLT_MIN, 180.0f));
                    check_field(TR("With data"), &form.flag_a);
                    ImGui::EndTable();
                }
                script = db::generate_create_materialized_view(target.schema, form.name,
                                                               form.body, form.flag_a);
                action_title = TRF("Create materialized view %s", form.name);
                break;
            }

            case ObjectType::data_type:
                if (session().is_sqlanywhere()) {
                    // CREATE DOMAIN: o tipo base, e o que toda coluna do
                    // dominio herda -- nulo, padrao e condicao.
                    if (begin_form("##sadomain", 150.0f, 360.0f)) {
                        text_field(TR("Name"), form.name, sizeof form.name);
                        text_field(TR("Base type"), form.text_a, sizeof form.text_a);
                        check_field(TR("Not null"), &form.flag_a);
                        text_field(TR("Default"), form.text_b, sizeof form.text_b);
                        text_field(TR("Check"), form.text_c, sizeof form.text_c);
                        ImGui::EndTable();
                    }
                    script = db::sqlanywhere_create_domain(form.name, form.text_a,
                                                           form.flag_a, form.text_b,
                                                           form.text_c);
                    action_title = TRF("Create domain %s", form.name);
                    break;
                }
                [[fallthrough]];

            default:
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   TR("this object type is created with SQL"));
                script.error = "no form";
                break;
        }
    } else if (form.kind == ObjectForm::Kind::rename) {
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();
        if (begin_form("##rename")) {
            text_field(TR("New name"), form.name, sizeof form.name);
            ImGui::EndTable();
        }
        script       = db::generate_object_rename(target, form.name);
        action_title = TRF("Rename %s", target.title().c_str());
    } else if (form.kind == ObjectForm::Kind::drop) {
        ImGui::Text(TR("Are you sure you want to delete %s '%s'?"),
                    TR(type_label(type, session().engine(), target.parent)),
                    target.title().c_str());
        // CASCADE so' existe no PostgreSQL.
        if (session().is_postgres()) ImGui::Checkbox(TR("Cascade"), &form.flag_a);
        script       = db::generate_object_drop(target, form.flag_a);
        action_title = TRF("Delete %s", target.title().c_str());
    } else if (form.kind == ObjectForm::Kind::vacuum) {
        const db::ServerVersion version =
            db::ServerVersion::parse(session().server_version());
        ImGui::Checkbox(TR("Full"), &form.flag_a);
        ImGui::Checkbox(TR("Freeze"), &form.flag_b);
        ImGui::Checkbox(TR("Analyzed"), &form.flag_c);
        // As opcoes aparecem conforme a versao, como o `visibleIf` do DBeaver.
        if (version.at_least(9, 6)) ImGui::Checkbox(TR("Disable page skipping"), &form.flag_d);
        if (version.at_least(12)) {
            ImGui::Checkbox(TR("Skip locked"), &form.flag_e);
            ImGui::Checkbox(TR("Index cleanup"), &form.flag_f);
            ImGui::Checkbox(TR("Truncate"), &form.flag_g);
        }
        db::VacuumOptions options;
        options.full                  = form.flag_a;
        options.freeze                = form.flag_b;
        options.analyze               = form.flag_c;
        options.disable_page_skipping = form.flag_d;
        options.skip_locked           = form.flag_e;
        options.index_cleanup         = form.flag_f;
        options.truncate              = form.flag_g;
        const bool whole = type == ObjectType::database;
        script = db::generate_vacuum(whole ? std::string{} : target.schema,
                                     whole ? std::string{} : target.name, options);
        action_title = TRF("Vacuum %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::truncate && !session().is_postgres()) {
        // TRUNCATE do MySQL, do SQL Server e do SQL Anywhere nao tem opcoes.
        ImGui::Text(TR("Are you sure you want to delete %s '%s'?"), TR("all rows of"),
                    target.title().c_str());
        script       = session().is_mssql()
                           ? db::mssql_truncate(target.schema, target.name)
                       : session().is_sqlanywhere()
                           ? db::sqlanywhere_truncate(target.schema, target.name)
                           : db::mysql_truncate(target.schema, target.name);
        action_title = TRF("Truncate %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::truncate) {
        ImGui::Checkbox(TR("Only"), &form.flag_a);
        ImGui::Checkbox(TR("Restart identity"), &form.flag_b);
        ImGui::Checkbox(TR("Cascade"), &form.flag_c);
        db::TruncateOptions options;
        options.only             = form.flag_a;
        options.restart_identity = form.flag_b;
        options.cascade          = form.flag_c;
        script       = db::generate_truncate(target.schema, target.name, options);
        action_title = TRF("Truncate %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::refresh_mview &&
               session().is_sqlanywhere()) {
        // Sem opcoes: REFRESH recalcula a view inteira.
        ImGui::Text(TR("Recompute the materialized view '%s'?"), target.title().c_str());
        script       = db::sqlanywhere_refresh_view(target.schema, target.name);
        action_title = TRF("Refresh %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::refresh_mview) {
        ImGui::Checkbox(TR("With data"), &form.flag_a);
        script = db::generate_refresh_materialized_view(target.schema, target.name,
                                                        form.flag_a);
        action_title = TRF("Refresh %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::password) {
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();
        if (begin_form("##password")) {
            text_field(TR("Password"), form.text_a, sizeof form.text_a,
                       ImGuiInputTextFlags_Password);
            text_field(TR("Confirm password"), form.text_b, sizeof form.text_b,
                       ImGuiInputTextFlags_Password);
            ImGui::EndTable();
        }
        if (std::strcmp(form.text_a, form.text_b) != 0) {
            script.error = "the two passwords are different";
        } else if (session().is_mssql()) {
            script = db::mssql_login_password(target.name, form.text_a);
        } else if (session().is_sqlanywhere()) {
            script = db::sqlanywhere_user_password(target.name, form.text_a);
        } else if (session().is_mysql()) {
            // Nome vazio = a conta da propria sessao (ALTER USER USER()).
            script = db::mysql_user_password(target.name, target.parent, form.text_a);
        } else {
            script = db::generate_role_password(target.name, form.text_a);
        }
        action_title = TRF("Change password of %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::ms_backup) {
        // BACKUP DATABASE roda NO SERVIDOR: o caminho e' de la', e quem grava
        // e' a conta do servico. Dito na tela -- um seletor de arquivo local
        // apontaria para um disco que o servidor nao enxerga.
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();
        if (begin_form("##msbackup", 150.0f, 460.0f)) {
            text_field(TR("File (on the server)"), form.body, sizeof form.body);
            check_field(TR("Copy only"), &form.flag_a);
            check_field(TR("Compression"), &form.flag_b);
            check_field(TR("Overwrite the file"), &form.flag_c);
            ImGui::EndTable();
        }
        db::MssqlBackupOptions options;
        options.file        = form.body;
        options.copy_only   = form.flag_a;
        options.compression = form.flag_b;
        options.overwrite   = form.flag_c;
        script       = db::mssql_backup_database(target.name, options);
        action_title = TRF("Backup %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::sa_backup) {
        // BACKUP DATABASE roda NO SERVIDOR, como o do SQL Server: a pasta e' de
        // la', e quem grava e' o processo do servidor de banco.
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();
        if (begin_form("##sabackup", 170.0f, 460.0f)) {
            text_field(TR("Directory (on the server)"), form.body, sizeof form.body);
            ImGui::EndTable();
        }
        script       = db::sqlanywhere_backup_database(form.body);
        action_title = TRF("Backup %s", target.name.c_str());
    } else if (form.kind == ObjectForm::Kind::ms_restore) {
        if (form.appearing_focus()) ImGui::SetKeyboardFocusHere();
        if (begin_form("##msrestore", 150.0f, 460.0f)) {
            text_field(TR("Database name"), form.name, sizeof form.name);
            text_field(TR("File (on the server)"), form.body, sizeof form.body);
            check_field(TR("Replace the existing database"), &form.flag_a);
            ImGui::EndTable();
        }
        script       = db::mssql_restore_database(form.name, form.body, form.flag_a);
        action_title = TRF("Restore %s", form.name);
    }

    // --- O SQL e os botoes ------------------------------------------------------
    ImGui::Separator();

    // O motivo da recusa na tela, em vez de um botao apagado sem explicacao.
    if (!script.error.empty() && script.error != "no form") {
        ImGui::TextColored(col4(p.text_dim), "%s", TR(script.error.c_str()));
    }

    const bool valid = script.ok();
    const bool submitted = object_form_submit_ && valid;
    object_form_submit_ = false;

    ImGui::BeginDisabled(!valid);
    if (ImGui::Button(button, ImVec2(160, 0)) || submitted) {
        if (direct_source) {
            // A rotina nova nasce no editor, como no DBeaver: o corpo ainda
            // precisa ser escrito, e executar o esqueleto criaria uma funcao
            // vazia.
            SqlDocument& document = new_document();
            document.editor().SetText(source_text);
            select_document_id_ = document.id();
            focus_editor_       = true;
            focus_document_id_  = document.id();
        } else {
            confirm_object_ddl(std::move(action_title), std::move(script));
        }
        form.kind = ObjectForm::Kind::none;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(120, 0)) ||
        (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         ImGui::IsKeyPressed(ImGuiKey_Escape))) {
        form.kind = ObjectForm::Kind::none;
    }

    ImGui::End();
    if (!open) form.kind = ObjectForm::Kind::none;
}

// --- Session Manager / Lock Manager --------------------------------------------------
//
// O DBeaver tem editores proprios para as sessoes e as travas, com os botoes
// "Cancel active query" e "Terminate session". Aqui a lista e' uma consulta
// numa aba de resultado (ADR 0018); os dois botoes aparecem acima da grade
// sempre que o resultado tem uma coluna de pid, agindo sobre a linha
// selecionada.
void MainShell::draw_session_actions(SqlDocument& document, const db::ResultSet& rs) {
    Session& target = session_for(document);
    if (target.is_mysql()) {
        draw_mysql_session_actions(document, rs);
        return;
    }
    if (target.is_mssql()) {
        draw_mssql_session_actions(document, rs);
        return;
    }
    if (target.is_sqlanywhere()) {
        draw_sqlanywhere_session_actions(document, rs);
        return;
    }

    // Qual coluna tem o pid: a da celula selecionada, se for uma delas (no
    // Lock Manager ha' duas: quem espera e quem bloqueia); senao a primeira.
    const auto is_pid = [&rs](std::size_t column) {
        const std::string& name = rs.column(column).info().name;
        return name == "pid" || name == "blocked_pid" || name == "blocking_pid";
    };
    std::size_t pid_column = rs.column_count();
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (is_pid(c)) { pid_column = c; break; }
    }
    if (pid_column == rs.column_count() || rs.row_count() == 0) return;

    const bool selected = has_selection_ && selected_document_ == document.id() &&
                          selected_row_ < rs.row_count();
    if (selected && selected_column_ < rs.column_count() && is_pid(selected_column_)) {
        pid_column = selected_column_;
    }

    const std::string pid =
        selected && !rs.is_null(selected_row_, pid_column)
            ? std::string(rs.text(selected_row_, pid_column))
            : std::string{};
    const bool can_run = !pid.empty() && target.state() == SessionState::connected &&
                         !target.busy();
    session_pid_ = pid;   // para o canal de comandos ("session terminate")

    const char* why = pid.empty() ? TR("select a row of the session to act on") : "";

    if (icon_text_button("##cancelbackend", Icon::stop, TR("Cancel active query"),
                         pid.empty() ? why
                                     : TR("Ask the server to interrupt the query of "
                                          "this session (pg_cancel_backend)"),
                         can_run)) {
        confirm_object_ddl(TRF("Cancel the query of session %s", pid.c_str()),
                           db::generate_session_kill(pid, /*terminate=*/false));
    }
    ImGui::SameLine();
    if (icon_text_button("##terminatebackend", Icon::disconnect, TR("Terminate session"),
                         pid.empty() ? why
                                     : TR("Close this session; its open transaction "
                                          "is rolled back (pg_terminate_backend)"),
                         can_run)) {
        confirm_object_ddl(TRF("Terminate session %s", pid.c_str()),
                           db::generate_session_kill(pid, /*terminate=*/true));
    }
    ImGui::SameLine();
    if (icon_text_button("##refreshsessions", Icon::refresh, TR("Refresh"),
                         TR("Run the query again"),
                         target.state() == SessionState::connected && !target.busy())) {
        execute_page(document, document.page());
    }
    if (!pid.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "pid %s", pid.c_str());
    }
}

// --- Conferencia automatizada (OTTER_COMMAND_FILE) -----------------------------------
//
// As linhas que o canal de comandos aceita para esta parte da tela. Existem
// pelo mesmo motivo do resto do canal (ver poll_command_file): conferir a
// tela sem tomar o mouse nem o teclado de quem esta' usando a maquina.
//
//     object <tipo>|<schema>|<nome>|<pai>|<assinatura>     abre o editor
//     objectdata <...>                                     abre na aba Data
//     objectmenu <...>                                     mostra o menu do no'
//     page properties|data
//     section <rotulo>                                     "Columns", "DDL"...
//     property <rotulo>=<valor>                            altera no formulario
//     source <texto>                                       troca o fonte (\n = quebra)
//     grantee <papel>
//     grant <privilegio> | revoke <privilegio>
//     save object                                          o botao "Save ..."
//     form <acao> <tipo>|<schema>|<nome>|<pai>|<assinatura>
//          acao: create rename drop vacuum truncate refresh password
//     field <campo> <valor>      campo: name a b c d body choice_a choice_b flag_a..flag_g
//     form ok | form cancel
//     ddl execute | ddl cancel | ddl dump                  a janela "Review SQL"
//
// `ddl dump` grava o script em revisao em <OTTER_COMMAND_FILE>.ddl: o SQL e'
// conferido como texto, nao lendo pixels.
bool MainShell::object_command(const std::string& line) {
    const auto parse_ref = [](std::string_view text) -> std::optional<db::ObjectRef> {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t bar = text.find('|', start);
            parts.emplace_back(text.substr(
                start, bar == std::string_view::npos ? std::string_view::npos : bar - start));
            if (bar == std::string_view::npos) break;
            start = bar + 1;
        }
        parts.resize(5);

        db::ObjectRef ref;
        bool known = false;
        for (int t = 0; t < db::kObjectTypeCount; ++t) {
            if (db::to_string(static_cast<ObjectType>(t)) == parts[0]) {
                ref.type = static_cast<ObjectType>(t);
                known    = true;
                break;
            }
        }
        if (!known) return std::nullopt;
        ref.schema    = parts[1];
        ref.name      = parts[2];
        ref.parent    = parts[3];
        ref.signature = parts[4];
        return ref;
    };

    const auto active_object = [this]() -> SqlDocument* {
        SqlDocument* document = active_document();
        return document != nullptr && document->is_object() ? document : nullptr;
    };

    if (line.starts_with("object ") || line.starts_with("objectdata ")) {
        const bool data = line.starts_with("objectdata ");
        if (auto ref = parse_ref(line.substr(data ? 11 : 7))) {
            open_object_editor(std::move(*ref), data);
        } else {
            show_toast("OTTER_COMMAND_FILE: unknown object type: " + line);
        }
        return true;
    }
    if (line.starts_with("objectmenu ")) {
        if (auto ref = parse_ref(line.substr(11))) {
            object_menu_debug_      = std::move(*ref);
            object_menu_debug_open_ = true;
        }
        return true;
    }
    if (line.starts_with("page ")) {
        if (SqlDocument* document = active_object()) {
            document->object()->page = line.substr(5) == "data"
                                           ? ObjectView::Page::data
                                           : ObjectView::Page::properties;
            document->object()->select_page = true;
        }
        return true;
    }
    if (line.starts_with("section ")) {
        if (SqlDocument* document = active_object()) {
            document->object()->section = line.substr(8);
        }
        return true;
    }
    if (line.starts_with("property ")) {
        if (SqlDocument* document = active_object()) {
            const std::string rest = line.substr(9);
            const std::size_t eq   = rest.find('=');
            if (eq != std::string::npos) {
                document->object()->edits[rest.substr(0, eq)] = rest.substr(eq + 1);
            }
        }
        return true;
    }
    if (line.starts_with("source ")) {
        if (SqlDocument* document = active_object()) {
            std::string text = line.substr(7);
            for (std::size_t at = 0; (at = text.find("\\n", at)) != std::string::npos;) {
                text.replace(at, 2, "\n");
            }
            document->editor().SetText(text);
        }
        return true;
    }
    if (line.starts_with("grantee ")) {
        if (SqlDocument* document = active_object()) {
            document->object()->grantee = line.substr(8);
        }
        return true;
    }
    if (line.starts_with("grant ") || line.starts_with("revoke ")) {
        if (SqlDocument* document = active_object()) {
            const bool grant = line.starts_with("grant ");
            const ObjectView& view = *document->object();
            const std::string privilege = line.substr(grant ? 6 : 7);
            confirm_object_ddl(
                grant ? "Grant" : "Revoke",
                grant ? db::generate_grant(view.ref, privilege, view.grantee)
                      : db::generate_revoke(view.ref, privilege, view.grantee),
                document->id(), view.ref);
        }
        return true;
    }
    if (line == "save object") {
        if (SqlDocument* document = active_object()) {
            save_object_edits(*document, session_for(*document)
                                             .object_info(document->object()->ref)
                                             .info);
        }
        return true;
    }

    if (line == "form ok")     { object_form_submit_ = true; return true; }
    if (line == "form cancel") { object_form_.kind = ObjectForm::Kind::none; return true; }
    if (line.starts_with("form ")) {
        const std::string rest  = line.substr(5);
        const std::size_t space = rest.find(' ');
        if (space == std::string::npos) return true;

        const std::string action = rest.substr(0, space);
        const ObjectForm::Kind kind =
            action == "create"     ? ObjectForm::Kind::create
            : action == "rename"   ? ObjectForm::Kind::rename
            : action == "drop"     ? ObjectForm::Kind::drop
            : action == "vacuum"   ? ObjectForm::Kind::vacuum
            : action == "truncate" ? ObjectForm::Kind::truncate
            : action == "refresh"  ? ObjectForm::Kind::refresh_mview
            : action == "password" ? ObjectForm::Kind::password
            : action == "msbackup" ? ObjectForm::Kind::ms_backup
            : action == "msrestore" ? ObjectForm::Kind::ms_restore
            : action == "sabackup" ? ObjectForm::Kind::sa_backup
                                   : ObjectForm::Kind::none;
        if (auto ref = parse_ref(rest.substr(space + 1));
            ref && kind != ObjectForm::Kind::none) {
            open_object_form(kind, std::move(*ref));
        }
        return true;
    }
    if (line.starts_with("field ")) {
        const std::string rest  = line.substr(6);
        const std::size_t space = rest.find(' ');
        const std::string field = rest.substr(0, space);
        const std::string value = space == std::string::npos ? std::string{}
                                                             : rest.substr(space + 1);
        ObjectForm& form = object_form_;
        // O campo com o teclado guarda o texto num buffer do ImGui e ignora o
        // que for escrito por fora: sem soltar o foco, o valor do canal nao
        // aparecia no campo que acabou de abrir focado.
        ImGui::ClearActiveID();
        const auto put = [&value](char* buffer, std::size_t size) {
            std::snprintf(buffer, size, "%s", value.c_str());
        };
        const bool on = value == "1" || value == "true" || value == "yes";

        if (field == "name")          put(form.name, sizeof form.name);
        else if (field == "a")        put(form.text_a, sizeof form.text_a);
        else if (field == "b")        put(form.text_b, sizeof form.text_b);
        else if (field == "c")        put(form.text_c, sizeof form.text_c);
        else if (field == "d")        put(form.text_d, sizeof form.text_d);
        else if (field == "body")     put(form.body, sizeof form.body);
        else if (field == "choice_a") form.choice_a = std::atoi(value.c_str());
        else if (field == "choice_b") form.choice_b = std::atoi(value.c_str());
        else if (field == "flag_a")   form.flag_a = on;
        else if (field == "flag_b")   form.flag_b = on;
        else if (field == "flag_c")   form.flag_c = on;
        else if (field == "flag_d")   form.flag_d = on;
        else if (field == "flag_e")   form.flag_e = on;
        else if (field == "flag_f")   form.flag_f = on;
        else if (field == "flag_g")   form.flag_g = on;
        return true;
    }

    // export open | export format <NOME> | export path <arquivo> |
    // export whole 0|1 | export save | export cancel
    if (line.starts_with("export ")) {
        const std::string rest = line.substr(7);
        if (rest == "open") {
            show_export_ = true;
        } else if (rest == "save") {
            export_submit_ = true;
        } else if (rest == "cancel") {
            if (SqlDocument* document = active_document()) {
                session_for(*document).cancel_transfer();
            }
        } else if (rest.starts_with("format ")) {
            for (int f = 0; f <= static_cast<int>(db::ExportFormat::txt); ++f) {
                if (db::to_string(static_cast<db::ExportFormat>(f)) == rest.substr(7)) {
                    export_options_.format = static_cast<db::ExportFormat>(f);
                }
            }
        } else if (rest.starts_with("path ")) {
            export_path_ = rest.substr(5);
        } else if (rest.starts_with("whole ")) {
            export_whole_query_ = rest.substr(6) == "1";
        }
        return true;
    }

    // tool backup <tipo>|<schema>|<nome>|| | tool restore <banco> |
    // tool file <caminho> | tool format <0..3> | tool flag <nome> 0|1 |
    // tool run | tool status
    // mytool analyze|check|optimize|repair <banco>|<tabela> [opcao]
    // As ferramentas de tabela do MySQL, que na tela ficam no submenu Tools.
    // mstool stats|rebuild|reorganize|check <schema>|<tabela>
    // mstool enable|disable <schema>|<trigger>|<tabela>
    // As ferramentas do SQL Server, que na tela ficam no submenu Tools.
    if (line.starts_with("mstool ")) {
        const std::string rest = line.substr(7);
        const std::size_t space = rest.find(' ');
        const std::string verb = rest.substr(0, space);
        const std::string spec = space == std::string::npos ? std::string{}
                                                            : rest.substr(space + 1);
        std::vector<std::string> parts;
        for (std::size_t start = 0; start <= spec.size();) {
            const std::size_t bar = spec.find('|', start);
            parts.push_back(spec.substr(
                start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        parts.resize(3);

        if (verb == "enable" || verb == "disable") {
            db::ObjectRef trigger;
            trigger.type   = ObjectType::trigger;
            trigger.schema = parts[0];
            trigger.name   = parts[1];
            trigger.parent = parts[2];
            confirm_object_ddl(verb, db::mssql_trigger_enable(trigger, verb == "enable"));
            return true;
        }
        const db::MssqlTableTool tool =
            verb == "rebuild"      ? db::MssqlTableTool::rebuild_indexes
            : verb == "reorganize" ? db::MssqlTableTool::reorganize_indexes
            : verb == "check"      ? db::MssqlTableTool::check
                                   : db::MssqlTableTool::update_statistics;
        confirm_object_ddl(verb, db::mssql_table_tool(tool, parts[0], parts[1]));
        return true;
    }

    // satool validate|reorganize|stats <dono>|<tabela>
    // satool viewon|viewoff|refresh <dono>|<view>[|m]   (m = materializada)
    // satool eventon|eventoff|eventrun <evento>
    // satool checkpoint | satool validatedb
    // As ferramentas do SQL Anywhere, que na tela ficam no submenu Tools e na
    // pasta Administer.
    if (line.starts_with("satool ")) {
        const std::string rest = line.substr(7);
        const std::size_t space = rest.find(' ');
        const std::string verb = rest.substr(0, space);
        const std::string spec = space == std::string::npos ? std::string{}
                                                            : rest.substr(space + 1);
        std::vector<std::string> parts;
        for (std::size_t start = 0; start <= spec.size();) {
            const std::size_t bar = spec.find('|', start);
            parts.push_back(spec.substr(
                start, bar == std::string::npos ? std::string::npos : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        parts.resize(3);

        if (verb == "checkpoint") {
            confirm_object_ddl(verb, db::sqlanywhere_checkpoint());
        } else if (verb == "validatedb") {
            confirm_object_ddl(verb, db::sqlanywhere_validate_database());
        } else if (verb == "eventon" || verb == "eventoff") {
            confirm_object_ddl(verb, db::sqlanywhere_event_enable(parts[0], verb == "eventon"));
        } else if (verb == "eventrun") {
            confirm_object_ddl(verb, db::sqlanywhere_event_trigger(parts[0]));
        } else if (verb == "viewon" || verb == "viewoff" || verb == "refresh") {
            db::ObjectRef view;
            view.type   = parts[2] == "m" || verb == "refresh" ? ObjectType::materialized_view
                                                               : ObjectType::view;
            view.schema = parts[0];
            view.name   = parts[1];
            confirm_object_ddl(verb, verb == "refresh"
                                         ? db::sqlanywhere_refresh_view(parts[0], parts[1])
                                         : db::sqlanywhere_view_enable(view, verb == "viewon"));
        } else {
            const db::SqlAnywhereTableTool tool =
                verb == "reorganize" ? db::SqlAnywhereTableTool::reorganize
                : verb == "stats"    ? db::SqlAnywhereTableTool::create_statistics
                                     : db::SqlAnywhereTableTool::validate;
            confirm_object_ddl(verb, db::sqlanywhere_table_tool(tool, parts[0], parts[1]));
        }
        return true;
    }

    if (line.starts_with("mytool ")) {
        const std::string rest = line.substr(7);
        const std::size_t space = rest.find(' ');
        const std::string verb = rest.substr(0, space);
        const std::string spec = space == std::string::npos ? std::string{}
                                                            : rest.substr(space + 1);
        const std::size_t bar = spec.find('|');
        const std::size_t option_at = spec.find(' ', bar == std::string::npos ? 0 : bar);
        db::ObjectRef ref;
        ref.type   = ObjectType::table;
        ref.schema = spec.substr(0, bar);
        ref.name   = bar == std::string::npos
                         ? std::string{}
                         : spec.substr(bar + 1, option_at == std::string::npos
                                                    ? std::string::npos
                                                    : option_at - bar - 1);
        const std::string option =
            option_at == std::string::npos ? std::string{} : spec.substr(option_at + 1);
        const db::MysqlTableTool tool = verb == "check"      ? db::MysqlTableTool::check
                                        : verb == "optimize" ? db::MysqlTableTool::optimize
                                        : verb == "repair"   ? db::MysqlTableTool::repair
                                                             : db::MysqlTableTool::analyze;
        run_mysql_table_tool(tool, ref, option);
        return true;
    }

    if (line.starts_with("tool ")) {
        const std::string rest = line.substr(5);
        if (rest.starts_with("backup ")) {
            if (auto ref = parse_ref(rest.substr(7))) open_backup(*ref);
        } else if (rest.starts_with("restore ")) {
            open_restore(rest.substr(8));
        } else if (rest.starts_with("file ")) {
            std::snprintf(tool_form_.file, sizeof tool_form_.file, "%s",
                          rest.substr(5).c_str());
        } else if (rest.starts_with("format ")) {
            tool_form_.format = std::atoi(rest.c_str() + 7);
        } else if (rest.starts_with("flag ")) {
            const std::string spec = rest.substr(5);
            const std::size_t space = spec.find(' ');
            const std::string name = spec.substr(0, space);
            const bool on = space != std::string::npos && spec.substr(space + 1) == "1";
            if (name == "inserts")         tool_form_.use_inserts = on;
            else if (name == "privileges") tool_form_.no_privileges = on;
            else if (name == "owner")      tool_form_.no_owner = on;
            else if (name == "clean")      tool_form_.clean = on;
            else if (name == "create")     tool_form_.create = on;
            // mysqldump
            else if (name == "nodata")     tool_form_.my_no_data = on;
            else if (name == "nocreate")   tool_form_.my_no_create = on;
            else if (name == "routines")   tool_form_.my_routines = on;
            else if (name == "events")     tool_form_.my_events = on;
            else if (name == "adddrop")    tool_form_.my_add_drop = on;
        } else if (rest == "run") {
            tool_submit_ = true;
        } else if (rest == "status") {
            if (const char* path = std::getenv("OTTER_COMMAND_FILE")) {
                if (std::FILE* out = std::fopen((std::string(path) + ".ddl").c_str(), "wb")) {
                    std::fprintf(out, "%s\n%s\nexit=%d\n%s\n%s\n",
                                 tool_run_.running ? "RUNNING" : "FINISHED",
                                 tool_run_.command_line.c_str(), tool_run_.exit_code,
                                 tool_form_.error.c_str(), tool_run_.output.c_str());
                    std::fclose(out);
                }
            }
        }
        return true;
    }

    // import open <schema>|<tabela> | import path <arquivo> |
    // import map <n> <coluna> | import truncate 0|1 | import run
    if (line.starts_with("import ")) {
        const std::string rest = line.substr(7);
        if (rest.starts_with("open ")) {
            const std::string target = rest.substr(5);
            const std::size_t bar = target.find('|');
            if (bar != std::string::npos) {
                open_import(target.substr(0, bar), target.substr(bar + 1));
            }
        } else if (rest.starts_with("path ")) {
            std::snprintf(import_form_.path, sizeof import_form_.path, "%s",
                          rest.substr(5).c_str());
            reload_import_file(/*detect=*/true);
        } else if (rest.starts_with("map ")) {
            const std::string spec = rest.substr(4);
            const std::size_t space = spec.find(' ');
            const auto index = static_cast<std::size_t>(std::atoi(spec.c_str()));
            if (index < import_form_.mapping.size()) {
                import_form_.mapping[index] =
                    space == std::string::npos ? std::string{} : spec.substr(space + 1);
            }
        } else if (rest.starts_with("truncate ")) {
            import_form_.truncate = rest.substr(9) == "1";
        } else if (rest == "run") {
            import_submit_ = true;
        } else if (rest == "status") {
            if (const char* path = std::getenv("OTTER_COMMAND_FILE")) {
                if (std::FILE* out = std::fopen((std::string(path) + ".ddl").c_str(), "wb")) {
                    std::fprintf(out, "%s%s\n", import_form_.failed ? "FAILED: " : "",
                                 import_form_.status.c_str());
                    for (std::size_t c = 0; c < import_form_.mapping.size(); ++c) {
                        std::fprintf(out, "%s -> %s\n",
                                     c < import_form_.preview.header.size()
                                         ? import_form_.preview.header[c].c_str() : "?",
                                     import_form_.mapping[c].c_str());
                    }
                    std::fclose(out);
                }
            }
        }
        return true;
    }

    if (line == "session cancel" || line == "session terminate") {
        const bool terminate = line == "session terminate";
        // O SQL Server so' tem KILL: encerra a sessao nos dois casos.
        confirm_object_ddl(terminate ? "Terminate session" : "Cancel query",
                           session().is_mssql()
                               ? db::mssql_session_kill(session_pid_)
                           : session().is_sqlanywhere()
                               ? db::sqlanywhere_session_kill(session_pid_)
                               : db::generate_session_kill(session_pid_, terminate));
        return true;
    }

    if (line == "ddl execute") { (void)ddl_dialog_.execute_now(); return true; }
    if (line == "ddl cancel")  { ddl_dialog_.close(); return true; }
    if (line == "ddl dump") {
        if (const char* path = std::getenv("OTTER_COMMAND_FILE")) {
            const db::AlterScript& script = ddl_dialog_.script();
            if (std::FILE* out = std::fopen((std::string(path) + ".ddl").c_str(), "wb")) {
                if (!ddl_dialog_.visible()) std::fputs("(closed)\n", out);
                if (!script.error.empty()) {
                    std::fprintf(out, "ERROR: %s\n", script.error.c_str());
                }
                for (const std::string& statement : script.statements) {
                    std::fprintf(out, "%s\n", statement.c_str());
                }
                for (const std::string& warning : script.warnings) {
                    std::fprintf(out, "WARNING: %s\n", warning.c_str());
                }
                std::fclose(out);
            }
        }
        return true;
    }
    return false;
}

// Um campo de funcao: digitavel, com a lista das funcoes do banco que
// devolvem `returns` ("trigger", "event_trigger") -- as unicas que servem.
void MainShell::routine_combo(const char* id, char* buffer, std::size_t size,
                              std::string_view returns) {
    ImGui::PushID(id);
    const float button = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - button);
    ImGui::InputText("##name", buffer, size);
    ImGui::SameLine(0.0f, 0.0f);

    if (ImGui::BeginCombo("##pick", "", ImGuiComboFlags_NoPreview |
                                            ImGuiComboFlags_PopupAlignLeft)) {
        bool any = false;
        for (const db::SchemaMeta& schema : session().schemas()) {
            // As rotinas so' estao no modelo depois de a pasta ser aberta na
            // arvore: pede as que faltam.
            if (!schema.routines_loaded) {
                if (!session().busy()) session().load_routines_async(schema.name);
                continue;
            }
            for (const db::RoutineMeta& routine : schema.routines) {
                if (routine.return_type != returns) continue;
                any = true;
                const std::string name = db::qualified_name(schema.name, routine.name);
                if (ImGui::Selectable(name.c_str(), name == buffer)) {
                    std::snprintf(buffer, size, "%s", name.c_str());
                }
            }
        }
        if (!any) {
            ImGui::TextDisabled("%s", TR("no function returns this type yet"));
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
}

// --- MySQL: Tools e Session Manager ---------------------------------------------------
//
// O submenu "Tools" do plugin MySQL do DBeaver: Analyze, Check, Optimize,
// Repair e Truncate para tabela; Dump database e Execute script para banco;
// e a troca de senha da conta.
void MainShell::draw_mysql_tools_menu(const db::ObjectRef& ref) {
    const bool is_table    = ref.type == ObjectType::table;
    const bool is_database = ref.type == ObjectType::database ||
                             ref.type == ObjectType::schema;
    const bool is_user     = ref.type == ObjectType::role;
    if (!is_table && !is_database && !is_user) return;

    const bool can_run =
        session().state() == SessionState::connected && !session().busy();
    if (!ImGui::BeginMenu(TR("Tools"), can_run)) return;

    if (is_table) {
        // Os quatro devolvem uma tabela de estado (Table, Op, Msg_type,
        // Msg_text): rodam como consulta, numa aba, para o resultado aparecer
        // -- e' o que a janela de estado do DBeaver mostra.
        struct Entry {
            const char*        label;
            db::MysqlTableTool tool;
        };
        static constexpr Entry kTools[] = {
            {"Analyze", db::MysqlTableTool::analyze},
            {"Check", db::MysqlTableTool::check},
            {"Optimize", db::MysqlTableTool::optimize},
            {"Repair", db::MysqlTableTool::repair},
        };
        for (const Entry& entry : kTools) {
            const std::vector<std::string_view> options =
                db::mysql_table_tool_options(entry.tool);
            if (options.size() <= 1) {
                if (ImGui::MenuItem(TR(entry.label))) {
                    run_mysql_table_tool(entry.tool, ref, {});
                }
                continue;
            }
            if (ImGui::BeginMenu(TR(entry.label))) {
                for (const std::string_view option : options) {
                    const std::string label =
                        option.empty() ? std::string(TR("(default)")) : std::string(option);
                    if (ImGui::MenuItem(label.c_str())) {
                        run_mysql_table_tool(entry.tool, ref, option);
                    }
                }
                ImGui::EndMenu();
            }
        }
        if (ImGui::MenuItem(TR("Truncate"))) {
            open_object_form(ObjectForm::Kind::truncate, ref);
        }
    }

    if (is_user && ImGui::MenuItem(TR("Change password..."))) {
        open_object_form(ObjectForm::Kind::password, ref);
    }

    if (is_table || is_database) {
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Dump database"))) open_backup(ref);
        if (is_database && ImGui::MenuItem(TR("Restore database"))) open_restore(ref.name);
        if (is_database && ImGui::MenuItem(TR("Execute script"))) open_restore(ref.name);
    }
    ImGui::EndMenu();
}

void MainShell::run_mysql_table_tool(db::MysqlTableTool tool, const db::ObjectRef& ref,
                                     std::string_view option) {
    open_sql_tab(db::mysql_table_tool_sql(tool, ref.schema, ref.name, option),
                 /*run=*/true);
}

// MySQLSessionEditor: "Kill Query" e "Kill Connection" sobre a linha escolhida
// de SHOW FULL PROCESSLIST (a coluna `Id`).
void MainShell::draw_mysql_session_actions(SqlDocument& document,
                                           const db::ResultSet& rs) {
    Session& target = session_for(document);

    // So' no resultado do PROCESSLIST: `Id`, `User`, `Host`, `db`, `Command`...
    if (rs.column_count() < 5 || rs.row_count() == 0) return;
    if (rs.column(0).info().name != "Id" || rs.column(4).info().name != "Command") return;

    const bool selected = has_selection_ && selected_document_ == document.id() &&
                          selected_row_ < rs.row_count();
    const std::string id = selected && !rs.is_null(selected_row_, 0)
                               ? std::string(rs.text(selected_row_, 0))
                               : std::string{};
    const bool can_run = !id.empty() && target.state() == SessionState::connected &&
                         !target.busy();
    session_pid_ = id;   // para o canal de comandos ("session cancel|terminate")

    const char* why = id.empty() ? TR("select a row of the session to act on") : "";

    if (icon_text_button("##killquery", Icon::stop, TR("Kill Query"),
                         id.empty() ? why
                                    : TR("Interrupt the statement this session is running "
                                         "(KILL QUERY)"),
                         can_run)) {
        confirm_object_ddl(TRF("Cancel the query of session %s", id.c_str()),
                           db::mysql_session_kill(id, /*connection=*/false));
    }
    ImGui::SameLine();
    if (icon_text_button("##killconnection", Icon::disconnect, TR("Kill Connection"),
                         id.empty() ? why
                                    : TR("Close this session; its open transaction is "
                                         "rolled back (KILL CONNECTION)"),
                         can_run)) {
        confirm_object_ddl(TRF("Terminate session %s", id.c_str()),
                           db::mysql_session_kill(id, /*connection=*/true));
    }
    ImGui::SameLine();
    if (icon_text_button("##refreshsessions", Icon::refresh, TR("Refresh"),
                         TR("Run the query again"),
                         target.state() == SessionState::connected && !target.busy())) {
        execute_page(document, document.page());
    }
    if (!id.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "id %s", id.c_str());
    }
}

// --- SQL Server: Tools e Session Manager ----------------------------------------------
//
// O plugin do SQL Server do DBeaver (Community) nao tem submenu "Tools". Estes
// sao os equivalentes do que o do PostgreSQL e o do MySQL oferecem ali --
// estatisticas, indices, verificacao, truncar, backup -- nos comandos do T-SQL.
void MainShell::draw_mssql_tools_menu(const db::ObjectRef& ref) {
    const bool is_table    = ref.type == ObjectType::table;
    const bool is_trigger  = ref.type == ObjectType::trigger;
    const bool is_database = ref.type == ObjectType::database;
    const bool is_login    = ref.type == ObjectType::role;
    if (!is_table && !is_trigger && !is_database && !is_login) return;

    const bool can_run =
        session().state() == SessionState::connected && !session().busy();
    if (!ImGui::BeginMenu(TR("Tools"), can_run)) return;

    if (is_table) {
        struct Entry {
            const char*        label;
            db::MssqlTableTool tool;
        };
        static constexpr Entry kTools[] = {
            {"Update statistics", db::MssqlTableTool::update_statistics},
            {"Rebuild indexes", db::MssqlTableTool::rebuild_indexes},
            {"Reorganize indexes", db::MssqlTableTool::reorganize_indexes},
            {"Check table", db::MssqlTableTool::check},
        };
        for (const Entry& entry : kTools) {
            if (ImGui::MenuItem(TR(entry.label))) {
                confirm_object_ddl(TRF("%s: %s", TR(entry.label), ref.name.c_str()),
                                   db::mssql_table_tool(entry.tool, ref.schema, ref.name));
            }
        }
        if (ImGui::MenuItem(TR("Truncate"))) {
            open_object_form(ObjectForm::Kind::truncate, ref);
        }
    }

    if (is_trigger) {
        if (ImGui::MenuItem(TR("Enable trigger"))) {
            confirm_object_ddl(TRF("Enable trigger %s", ref.name.c_str()),
                               db::mssql_trigger_enable(ref, true));
        }
        if (ImGui::MenuItem(TR("Disable trigger"))) {
            confirm_object_ddl(TRF("Disable trigger %s", ref.name.c_str()),
                               db::mssql_trigger_enable(ref, false));
        }
    }

    if (is_login && ImGui::MenuItem(TR("Change password..."))) {
        open_object_form(ObjectForm::Kind::password, ref);
    }

    if (is_database) {
        if (ImGui::MenuItem(TR("Backup database"))) {
            open_object_form(ObjectForm::Kind::ms_backup, ref);
            object_form_.flag_a = true;   // COPY_ONLY: nao quebra a cadeia de backups
        }
        if (ImGui::MenuItem(TR("Restore database"))) {
            open_object_form(ObjectForm::Kind::ms_restore, ref);
            std::snprintf(object_form_.name, sizeof object_form_.name, "%s",
                          ref.name.c_str());
        }
    }
    ImGui::EndMenu();
}

// SQLServerSessionEditor: "Kill session" sobre a linha escolhida. O SQL Server
// nao tem "cancelar so' a consulta" de fora da sessao -- KILL a encerra.
void MainShell::draw_mssql_session_actions(SqlDocument& document,
                                           const db::ResultSet& rs) {
    Session& target = session_for(document);

    // A coluna do id: a da celula selecionada, se for uma delas (no Lock
    // Manager ha' duas: quem espera e quem bloqueia); senao a primeira.
    const auto is_id = [&rs](std::size_t column) {
        const std::string& name = rs.column(column).info().name;
        return name == "session_id" || name == "blocked_session_id" ||
               name == "blocking_session_id";
    };
    std::size_t id_column = rs.column_count();
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (is_id(c)) { id_column = c; break; }
    }
    if (id_column == rs.column_count() || rs.row_count() == 0) return;

    const bool selected = has_selection_ && selected_document_ == document.id() &&
                          selected_row_ < rs.row_count();
    if (selected && selected_column_ < rs.column_count() && is_id(selected_column_)) {
        id_column = selected_column_;
    }

    const std::string id = selected && !rs.is_null(selected_row_, id_column)
                               ? std::string(rs.text(selected_row_, id_column))
                               : std::string{};
    const bool can_run = !id.empty() && id != "0" &&
                         target.state() == SessionState::connected && !target.busy();
    session_pid_ = id;   // para o canal de comandos ("session terminate")

    if (icon_text_button("##killsession", Icon::disconnect, TR("Terminate session"),
                         id.empty() ? TR("select a row of the session to act on")
                                    : TR("Close this session; its open transaction is "
                                         "rolled back (KILL)"),
                         can_run)) {
        confirm_object_ddl(TRF("Terminate session %s", id.c_str()),
                           db::mssql_session_kill(id));
    }
    ImGui::SameLine();
    if (icon_text_button("##refreshsessions", Icon::refresh, TR("Refresh"),
                         TR("Run the query again"),
                         target.state() == SessionState::connected && !target.busy())) {
        execute_page(document, document.page());
    }
    if (!id.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "session %s", id.c_str());
    }
}

// --- SQL Anywhere: Tools e Session Manager ---------------------------------------------
//
// O DBeaver nao tem plugin do SQL Anywhere. Estes sao os itens que o Sybase
// Central poe no menu de cada objeto -- validar, reorganizar, estatisticas,
// ligar e desligar view e evento --, nos comandos do proprio SGBD.
void MainShell::draw_sqlanywhere_tools_menu(const db::ObjectRef& ref) {
    const bool is_table = ref.type == ObjectType::table;
    const bool is_view  = ref.type == ObjectType::view ||
                          ref.type == ObjectType::materialized_view;
    const bool is_event = ref.type == ObjectType::event;
    // O papel puro nao tem senha; o dono (schema) e' um usuario.
    const bool is_user  = (ref.type == ObjectType::role && ref.parent != "role") ||
                          ref.type == ObjectType::schema;
    if (!is_table && !is_view && !is_event && !is_user) return;

    const bool can_run =
        session().state() == SessionState::connected && !session().busy();
    if (!ImGui::BeginMenu(TR("Tools"), can_run)) return;

    if (is_table) {
        struct Entry {
            const char*              label;
            db::SqlAnywhereTableTool tool;
        };
        static constexpr Entry kTools[] = {
            {"Validate table", db::SqlAnywhereTableTool::validate},
            {"Reorganize table", db::SqlAnywhereTableTool::reorganize},
            {"Create statistics", db::SqlAnywhereTableTool::create_statistics},
        };
        for (const Entry& entry : kTools) {
            if (ImGui::MenuItem(TR(entry.label))) {
                confirm_object_ddl(TRF("%s: %s", TR(entry.label), ref.name.c_str()),
                                   db::sqlanywhere_table_tool(entry.tool, ref.schema,
                                                              ref.name));
            }
        }
        if (ImGui::MenuItem(TR("Truncate"))) {
            open_object_form(ObjectForm::Kind::truncate, ref);
        }
    }

    if (is_view) {
        if (ref.type == ObjectType::materialized_view &&
            ImGui::MenuItem(TR("Refresh Materialized View"))) {
            open_object_form(ObjectForm::Kind::refresh_mview, ref);
        }
        if (ImGui::MenuItem(TR("Enable view"))) {
            confirm_object_ddl(TRF("Enable view %s", ref.name.c_str()),
                               db::sqlanywhere_view_enable(ref, true));
        }
        if (ImGui::MenuItem(TR("Disable view"))) {
            confirm_object_ddl(TRF("Disable view %s", ref.name.c_str()),
                               db::sqlanywhere_view_enable(ref, false));
        }
    }

    if (is_event) {
        if (ImGui::MenuItem(TR("Enable event"))) {
            confirm_object_ddl(TRF("Enable event %s", ref.name.c_str()),
                               db::sqlanywhere_event_enable(ref.name, true));
        }
        if (ImGui::MenuItem(TR("Disable event"))) {
            confirm_object_ddl(TRF("Disable event %s", ref.name.c_str()),
                               db::sqlanywhere_event_enable(ref.name, false));
        }
        if (ImGui::MenuItem(TR("Trigger event now"))) {
            confirm_object_ddl(TRF("Trigger event %s", ref.name.c_str()),
                               db::sqlanywhere_event_trigger(ref.name));
        }
    }

    if (is_user && ImGui::MenuItem(TR("Change password..."))) {
        db::ObjectRef user = ref;
        user.type = ObjectType::role;
        open_object_form(ObjectForm::Kind::password, std::move(user));
    }
    ImGui::EndMenu();
}

// "Disconnect" do Sybase Central sobre a linha escolhida: DROP CONNECTION. O
// numero da conexao e' `Number` na lista de sessoes e `conn_id` na de travas;
// `BlockedOn` e' quem a segura.
void MainShell::draw_sqlanywhere_session_actions(SqlDocument& document,
                                                 const db::ResultSet& rs) {
    Session& target = session_for(document);

    const auto is_id = [&rs](std::size_t column) {
        const std::string& name = rs.column(column).info().name;
        return name == "Number" || name == "conn_id" || name == "BlockedOn";
    };
    std::size_t id_column = rs.column_count();
    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        if (is_id(c)) { id_column = c; break; }
    }
    if (id_column == rs.column_count() || rs.row_count() == 0) return;
    // So' nas duas listas daqui: "Number" sozinho pode ser coluna de qualquer
    // consulta do usuario.
    const std::string& first = rs.column(id_column).info().name;
    if (first == "Number" && (rs.column_count() < 2 || rs.column(1).info().name != "Userid")) {
        return;
    }

    const bool selected = has_selection_ && selected_document_ == document.id() &&
                          selected_row_ < rs.row_count();
    if (selected && selected_column_ < rs.column_count() && is_id(selected_column_)) {
        id_column = selected_column_;
    }

    const std::string id = selected && !rs.is_null(selected_row_, id_column)
                               ? std::string(rs.text(selected_row_, id_column))
                               : std::string{};
    const bool can_run = !id.empty() && id != "0" &&
                         target.state() == SessionState::connected && !target.busy();
    session_pid_ = id;   // para o canal de comandos ("session terminate")

    if (icon_text_button("##dropconnection", Icon::disconnect, TR("Terminate session"),
                         id.empty() ? TR("select a row of the session to act on")
                                    : TR("Close this connection; its open transaction is "
                                         "rolled back (DROP CONNECTION)"),
                         can_run)) {
        confirm_object_ddl(TRF("Terminate session %s", id.c_str()),
                           db::sqlanywhere_session_kill(id));
    }
    ImGui::SameLine();
    if (icon_text_button("##refreshsessions", Icon::refresh, TR("Refresh"),
                         TR("Run the query again"),
                         target.state() == SessionState::connected && !target.busy())) {
        execute_page(document, document.page());
    }
    if (!id.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "connection %s", id.c_str());
    }
}

} // namespace otter::ui
