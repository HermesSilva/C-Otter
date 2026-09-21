#include "ui/main_shell.hpp"

#include "base/i18n.hpp"
#include "db/registry.hpp"
#include "db/aggregate.hpp"
#include "db/ddl.hpp"
#include "db/export.hpp"
#include "sql/format.hpp"
#include "sql/paging.hpp"
#include "ui/file_dialog.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder: layout inicial programatico

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace otter::ui {
namespace {

constexpr float kStatusBarHeight = 26.0f;
constexpr float kToolbarHeight   = 34.0f;

ImU32 col(std::uint32_t c) { return static_cast<ImU32>(c); }

ImVec4 col4(std::uint32_t c) { return ImGui::ColorConvertU32ToFloat4(col(c)); }

std::string to_lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) return false;
    }
    return true;
}

// Casamento por subsequencia: "cliid" casa "cliente_id".
bool fuzzy_subsequence(std::string_view needle, std::string_view haystack) {
    std::size_t i = 0;
    for (char c : haystack) {
        if (i < needle.size() && c == needle[i]) ++i;
    }
    return i == needle.size();
}

// Espacos ate' a coluna `width`. Como a fonte e' monoespacada, isto alinha o
// tipo numa coluna propria dentro do popup, que so' aceita texto puro
// (ver ADR 0004, secao de viabilidade).
std::string pad_to(std::string_view text, std::size_t width) {
    return text.size() >= width ? std::string(1, ' ')
                                : std::string(width - text.size(), ' ');
}

constexpr std::string_view kWelcomeSql =
    "-- C-Otter: every JOIN is an OTTER JOIN\n"
    "--\n"
    "-- Ctrl+Enter executa | Ctrl+Espaço completa\n"
    "\n"
    "SELECT table_name, column_name, data_type\n"
    "  FROM information_schema.columns\n"
    " WHERE table_schema = 'public'\n"
    " ORDER BY table_name, ordinal_position;\n";

// Tema da lontra aplicado ao editor: as cores vem da mesma paleta do logo que
// o resto da UI, para que o painel de SQL nao pareca um corpo estranho.
void apply_editor_palette(TextEditor& editor) {
    using Color = TextEditor::Color;

    // Parte da paleta base do widget adequada ao tema, e NAO da paleta atual
    // do editor: reaproveitar a anterior deixaria cores nao listadas abaixo
    // com o resíduo do tema antigo -- foi assim que o tema claro ficou com
    // texto claro sobre fundo claro.
    TextEditor::Palette p = current_theme().is_dark
                                ? TextEditor::GetDarkPalette()
                                : TextEditor::GetLightPalette();

    auto set = [&p](Color c, std::uint32_t value) {
        p[static_cast<std::size_t>(c)] = static_cast<ImU32>(value);
    };

    const Palette& t = colors();

    set(Color::background,      t.bg_darkest);
    set(Color::text,            t.text);
    set(Color::keyword,         t.syntax_keyword);   // SELECT, FROM, JOIN
    set(Color::declaration,     t.data_light);
    set(Color::number,          t.syntax_number);
    set(Color::string,          t.syntax_string);
    set(Color::punctuation,     t.text_dim);
    set(Color::preprocessor,    t.warn);
    set(Color::identifier,      t.text);
    set(Color::knownIdentifier, t.data);             // tabelas e colunas
    set(Color::comment,         t.syntax_comment);
    set(Color::cursor,          t.data_light);
    set(Color::selection,       with_alpha(t.data, 0.31f));
    set(Color::whitespace,      t.bg_light);
    set(Color::lineNumber,      t.text_dim);
    set(Color::currentLineNumber, t.accent_light);
    set(Color::currentLineHighlight,       with_alpha(t.accent, 0.09f));
    set(Color::currentLineHighlightBorder, with_alpha(t.accent, 0.19f));
    set(Color::matchingBracketBackground,  with_alpha(t.data, 0.25f));
    set(Color::matchingBracketActive, t.data_light);

    editor.SetPalette(p);
}

// Indicador de atividade: um ponto que pulsa enquanto o worker trabalha.
void draw_busy_indicator() {
    const float t = static_cast<float>(ImGui::GetTime());
    const float alpha = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
    ImVec4 color = col4(colors().data_light);
    color.w = alpha;
    ImGui::TextColored(color, "  ●");
}

} // namespace

MainShell::MainShell()
    : autocomplete_config_(std::make_unique<TextEditor::AutoCompleteConfig>()) {
    // Completion (ADR 0004). Uma configuracao compartilhada por todos os
    // documentos: o callback descobre o editor ativo em suggest().
    autocomplete_config_->triggerOnTyping    = true;
    autocomplete_config_->triggerInComments  = false;
    autocomplete_config_->triggerInStrings   = false;
    autocomplete_config_->suggestionWidth    = 52;
    autocomplete_config_->noSuggestionsLabel = TR("no suggestions");
    autocomplete_config_->userData           = this;
    autocomplete_config_->callback = [](TextEditor::AutoCompleteState& state) {
        static_cast<MainShell*>(state.userData)->suggest(state);
    };

    // Primeiro documento, com o texto de boas-vindas.
    new_document().editor().SetText(std::string(kWelcomeSql));

    // O assistente conecta e, ao concluir, tambem guarda o perfil ativo.
    // "Testar" reutiliza a conexao ativa: criar uma permanente a cada clique
    // encheria o Raft de entradas que o usuario nao pediu.
    connection_dialog_.set_on_test([this](const db::ConnectionProfile& profile) {
        session().connect_async(profile.to_conn_config());
    });

    connection_dialog_.set_on_connect([this](const db::ConnectionProfile& profile) {
        active_profile_ = profile;
        remember_profile(profile);
        open_connection(profile);
    });
    connection_dialog_.set_on_save([this](const db::ConnectionProfile& profile) {
        active_profile_ = profile;
        remember_profile(profile);
    });

    // Conexoes salvas na execucao anterior (ADR 0012).
    load_saved_profiles();

    // Abre primeiro: open_new() reinicia o perfil, e so' depois disso faz
    // sentido preencher a partir do ambiente (como psql faz).
    // A ultima conexao usavel salva reabre ja' na aba de configuracao, com os
    // campos preenchidos. Era o defeito mais incomodo do uso diario:
    // redigitar host, banco e usuario a cada execucao (ADR 0012).
    //
    // open_edit em vez de open_new: com um perfil conhecido, parar no
    // catalogo de drivers obrigaria a escolher PostgreSQL de novo para so'
    // entao ver o que ja' estava salvo.
    const db::StoredProfile* last_usable = nullptr;
    for (const db::StoredProfile& stored : saved_profiles_) {
        if (stored.supported) { last_usable = &stored; break; }
    }

    if (last_usable != nullptr) {
        connection_dialog_.open_edit(last_usable->profile);
    } else {
        connection_dialog_.open_new();
    }

    db::ConnectionProfile& profile = connection_dialog_.profile();

    // O ambiente ainda sobrepoe o que foi salvo: e' o que os scripts de
    // captura usam, e e' a convencao do psql.
    auto from_env = [](const char* name, std::string& target) {
        if (const char* value = std::getenv(name)) target = value;
    };
    from_env("PGHOST",     profile.host);
    from_env("PGDATABASE", profile.database);
    from_env("PGUSER",     profile.user);
    from_env("PGPASSWORD", profile.password);
    if (const char* port = std::getenv("PGPORT")) {
        profile.port = static_cast<std::uint16_t>(std::atoi(port));
    }

    // Abre a galeria de icones direto na inicializacao. Existe porque a
    // captura de tela para conferencia visual precisa de um caminho
    // deterministico: automatizar o clique no menu erra o alvo com frequencia.
    if (std::getenv("OTTER_SHOW_ICONS") != nullptr) {
        show_icons_ = true;
        connection_dialog_.close();
    }



    // Tema inicial por ambiente, pelo mesmo motivo: conferir os tres temas
    // exige tres capturas, e trocar pelo menu a cada uma e' fragil.
    if (const char* theme = std::getenv("OTTER_THEME")) {
        set_theme(theme);
    }

    // Conecta direto, usando o perfil ja' montado a partir de PGHOST/PGUSER/...
    // Serve para conferir a arvore de objetos numa captura: automatizar o
    // clique em "Conectar" erra o alvo com frequencia, e uma tela conferida a'
    // mao vale mais que um clique que talvez tenha acontecido.
    if (std::getenv("OTTER_AUTOCONNECT") != nullptr) {
        active_profile_ = profile;
        // Mesmo caminho da conexao normal, incluindo o registro em disco: um
        // atalho que pula etapas deixa de exercitar o que ele deveria testar.
        remember_profile(profile);
        open_connection(profile);
        connection_dialog_.close();
    }
}

MainShell::~MainShell() = default;

SqlDocument& MainShell::new_document() {
    documents_.push_back(std::make_unique<SqlDocument>(next_document_id_++));
    SqlDocument& document = *documents_.back();

    document.editor().SetAutoCompleteConfig(autocomplete_config_.get());
    apply_editor_palette(document.editor());

    active_document_ = documents_.size() - 1;
    return document;
}

void MainShell::close_document(std::size_t index) {
    if (index >= documents_.size()) return;

    // Nunca ficamos sem nenhuma aba: fechar a última abre uma vazia.
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (documents_.empty()) {
        new_document();
        return;
    }
    if (active_document_ >= documents_.size()) {
        active_document_ = documents_.size() - 1;
    }
}

void MainShell::close_others(std::size_t keep_index) {
    if (keep_index >= documents_.size()) return;

    // Guarda o id ANTES de mover: depois do move, documents_[keep_index] e' um
    // unique_ptr vazio e consulta-lo seria desreferenciar nulo.
    const std::size_t keep_id = documents_[keep_index]->id();

    std::vector<std::unique_ptr<SqlDocument>> kept;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        // Abas fixadas sobrevivem a "fechar outras" -- e' o que "fixar" quer
        // dizer.
        if (i == keep_index || documents_[i]->pinned()) {
            kept.push_back(std::move(documents_[i]));
        }
    }
    documents_ = std::move(kept);

    active_document_ = 0;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i]->id() == keep_id) {
            active_document_ = i;
            break;
        }
    }
}

SqlDocument* MainShell::active_document() {
    if (documents_.empty()) return nullptr;
    active_document_ = std::min(active_document_, documents_.size() - 1);
    return documents_[active_document_].get();
}

// Gera sugestoes de completion (ADR 0004), sobre metadados reais e com o
// escopo sintatico do otter_sql.
void MainShell::suggest(TextEditor::AutoCompleteState& state) {
    struct Candidate {
        std::string text;
        int         rank;   // menor = melhor
    };
    std::vector<Candidate> candidates;

    const std::string term = to_lower(state.searchTerm);

    auto matches = [&term](std::string_view name) {
        if (term.empty()) return true;
        const std::string lowered = to_lower(std::string(name));
        return lowered.starts_with(term) || fuzzy_subsequence(term, lowered);
    };

    auto add = [&](std::string text, int rank) {
        if (matches(text)) candidates.push_back({std::move(text), rank});
    };

    // Camada 2 -- escopo sintatico. Descobre o que faz sentido AQUI: tabelas
    // depois de FROM, colunas depois de SELECT/WHERE, colunas de UMA tabela
    // depois de "alias.".
    // O completion age sobre o documento ativo: cada aba tem seu editor.
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    const std::string script = document->editor().GetText();
    const TextEditor::DocPos cursor =
        document->editor().GetCurrentCursorPosition();

    // DocPos e' (linha, indice); o analisador trabalha com offset em bytes.
    std::size_t offset = 0;
    {
        std::size_t line = 0;
        while (line < cursor.line && offset < script.size()) {
            if (script[offset] == '\n') ++line;
            ++offset;
        }
        offset = std::min(offset + cursor.index, script.size());
    }

    const sql::ScopeInfo scope =
        sql::analyze_scope(script, active_dialect(), offset);

    const bool want_tables =
        scope.context == sql::CompletionContext::table_expected ||
        scope.context == sql::CompletionContext::schema_member ||
        scope.context == sql::CompletionContext::unknown;

    const bool want_columns =
        scope.context == sql::CompletionContext::column_expected ||
        scope.context == sql::CompletionContext::alias_member ||
        scope.context == sql::CompletionContext::unknown;

    // Tabelas visiveis na query, por nome e por alias.
    auto in_scope = [&scope](std::string_view table) {
        return std::any_of(scope.tables.begin(), scope.tables.end(),
                           [&](const sql::TableRef& ref) {
                               return iequals(ref.name, table);
                           });
    };

    // Depois de "alias.", so' interessam as colunas daquela tabela.
    std::string qualified_table;
    if (scope.context == sql::CompletionContext::alias_member) {
        for (const sql::TableRef& ref : scope.tables) {
            if (iequals(ref.alias, scope.qualifier) ||
                iequals(ref.name, scope.qualifier)) {
                qualified_table = ref.name;
                break;
            }
        }
    }

    // Camada 3 -- metadados reais do servidor.
    for (const db::SchemaMeta& schema : session().schemas()) {
        for (const db::TableMeta& table : schema.tables) {
            const bool referenced = in_scope(table.name);

            if (want_columns) {
                // Filtra por tabela quando o cursor esta' apos "alias.".
                const bool skip = !qualified_table.empty() &&
                                  !iequals(qualified_table, table.name);
                if (!skip) {
                    for (const db::ColumnMeta& column : table.columns) {
                        // Rank 0: coluna de tabela presente na query.
                        int rank = referenced ? 0 : 2;
                        if (column.primary_key) rank -= 1;   // chaves sobem
                        add(column.name + pad_to(column.name, 30) +
                                column.type_name +
                                (column.primary_key ? "  PK" : ""),
                            rank);
                    }
                }
            }

            if (want_tables && qualified_table.empty()) {
                const char* label =
                    table.kind == db::ObjKind::view ? "view" : "tabela";
                add(table.name + pad_to(table.name, 30) + label,
                    referenced ? 1 : 3);
            }
        }
    }

    // Camada 1 -- keywords, por ultimo: sao as mais previsiveis. Suprimidas
    // quando o contexto pede um nome de objeto, onde so' atrapalhariam.
    const bool want_keywords =
        scope.context == sql::CompletionContext::unknown ||
        scope.context == sql::CompletionContext::column_expected;

    if (want_keywords) {
        static const char* const kKeywords[] = {
            "SELECT", "FROM", "WHERE", "GROUP BY", "ORDER BY", "HAVING",
            "INNER JOIN", "LEFT JOIN", "RIGHT JOIN", "FULL JOIN", "ON",
            "INSERT INTO", "UPDATE", "DELETE FROM", "VALUES", "SET",
            "COUNT", "SUM", "AVG", "MIN", "MAX", "DISTINCT", "AS",
            "LIMIT", "OFFSET", "CASE", "WHEN", "THEN", "ELSE", "END",
            "CREATE TABLE", "ALTER TABLE", "DROP TABLE",
            "BEGIN", "COMMIT", "ROLLBACK",
        };
        for (const char* keyword : kKeywords) add(keyword, 5);
    }

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         if (a.rank != b.rank) return a.rank < b.rank;
                         return a.text < b.text;
                     });

    state.suggestions.clear();
    state.suggestions.reserve(candidates.size());
    for (Candidate& c : candidates) state.suggestions.push_back(std::move(c.text));
}

// Dialeto SQL da conexao ATIVA.
//
// Existe porque `postgres_dialect()` estava cravado em cinco pontos: o lexer
// do realce, o formatador, o divisor de script, a reescrita de paginacao e o
// plano. Num MySQL isso significava nao reconhecer a crase como delimitador,
// tratar # como operador em vez de comentario, e nao entender o DELIMITER --
// o script seria dividido no lugar errado.
const sql::Dialect& MainShell::active_dialect() const {
    const std::string& driver_id =
        active_connection_ < connections_.size()
            ? connections_[active_connection_].profile.driver_id
            : active_profile_.driver_id;
    return sql::dialect_for(driver_id);
}

void MainShell::execute_current_sql() {
    if (session().state() != SessionState::connected || session().busy()) return;

    SqlDocument* document = active_document();
    if (document == nullptr) return;

    std::string sql = document->sql_to_execute();
    if (sql.empty()) return;

    // Nova consulta: volta para a primeira pagina e descarta a ordenacao.
    //
    // Manter a coluna de ordenacao seria errado -- a consulta nova pode nem
    // ter essa coluna, e o servidor rejeitaria o ORDER BY.
    document->set_paged_sql(sql);
    document->set_page(0);
    document->set_sort({});
    document->set_filter({});
    execute_page(*document, 0);
}

namespace {

const std::vector<FileFilter>& sql_filters() {
    // Em ingles: sao chaves de traducao (base/i18n.hpp).
    static const std::vector<FileFilter> kFilters = {
        {"SQL scripts", "*.sql"},
        {"Text files",  "*.txt"},
    };
    return kFilters;
}

} // namespace

void MainShell::open_script_file() {
    const auto path = open_file_dialog(TR("Open script"), sql_filters());
    if (!path) return;   // cancelou

    std::ifstream file(*path, std::ios::binary);
    if (!file) {
        if (SqlDocument* document = active_document()) {
            document->set_status(std::string(TRF("cannot open %s",
                                                 path->c_str())));
        }
        return;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    // Aba nova em vez de sobrescrever a atual: o que esta' aberto pode ter
    // trabalho nao salvo, e descarta-lo sem perguntar seria pior que uma aba
    // a mais.
    SqlDocument& document = new_document();
    document.editor().SetText(buffer.str());
    document.set_file_path(*path);
    document.mark_saved();   // recem-aberto nao esta' modificado
}

void MainShell::save_script_file(bool save_as) {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    std::string path = document->file_path();

    if (path.empty() || save_as) {
        // Sugere o titulo da aba com .sql: "Script 2" vira "Script 2.sql".
        const auto chosen = save_file_dialog(
            TR("Save script"), sql_filters(), document->title() + ".sql");
        if (!chosen) return;
        path = *chosen;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        document->set_status(std::string(TRF("cannot write %s", path.c_str())));
        return;
    }

    const std::string text = document->editor().GetText();
    file.write(text.data(), static_cast<std::streamsize>(text.size()));

    if (!file) {
        document->set_status(std::string(TRF("write failed: %s", path.c_str())));
        return;
    }

    document->set_file_path(path);

    // O ponto de salvamento e' o que faz o indicador de modificado funcionar.
    // Sem "salvar", ele aparecia na primeira edicao e nunca mais saia -- era
    // o defeito 4 de docs/ELEMENTS.md.
    document->mark_saved();
    document->set_status(std::string(TRF("saved to %s", path.c_str())));
}

void MainShell::explain_current_sql(bool analyze) {
    if (session().state() != SessionState::connected || session().busy()) return;

    SqlDocument* document = active_document();
    if (document == nullptr) return;

    std::string sql = document->sql_to_execute();
    if (sql.empty()) return;

    plan_analyze_ = analyze;
    show_plan_    = true;
    plan_.reset();

    session().explain_async(std::move(sql), analyze);
}

void MainShell::draw_plan_node(const db::PlanNode& node, double max_cost,
                               int depth) {
    const Palette& p = colors();

    ImGui::PushID(&node);

    // Barra de custo relativo antes do rotulo: num plano de 40 nos, o que
    // importa e' achar o caro, e a barra responde isso sem ler numero.
    const float share = max_cost > 0.0
                            ? static_cast<float>(node.self_cost() / max_cost)
                            : 0.0f;

    const std::uint32_t bar_color =
        node.is_sequential_scan() ? p.error
        : share > 0.3f            ? p.warn
                                  : p.accent;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float bar_width = ImGui::GetFontSize() * 3.0f;
    const float bar_height = ImGui::GetFontSize() * 0.55f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        ImVec2(origin.x, origin.y + bar_height * 0.4f),
        ImVec2(origin.x + bar_width, origin.y + bar_height * 1.4f),
        with_alpha(p.text_dim, 0.20f), 2.0f);
    dl->AddRectFilled(
        ImVec2(origin.x, origin.y + bar_height * 0.4f),
        ImVec2(origin.x + bar_width * std::max(share, 0.02f),
               origin.y + bar_height * 1.4f),
        bar_color, 2.0f);

    ImGui::Dummy(ImVec2(bar_width + 6.0f, ImGui::GetFontSize()));
    ImGui::SameLine();

    const bool has_children = !node.children.empty();
    bool open = false;

    ImGui::PushStyleColor(ImGuiCol_Text,
                          col(node.is_sequential_scan() ? p.error : p.text));
    if (has_children) {
        ImGui::SetNextItemOpen(depth < 3, ImGuiCond_Once);
        open = ImGui::TreeNode(node.type.c_str());
    } else {
        ImGui::TextUnformatted(node.type.c_str());
    }
    ImGui::PopStyleColor();

    // A relacao acessada ao lado do tipo: "Seq Scan" sozinho nao diz em que.
    if (!node.relation.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.data), "on %s", node.relation.c_str());
    }
    if (!node.index_name.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(col4(p.accent_light), "using %s",
                           node.index_name.c_str());
    }

    ImGui::SameLine();
    if (node.has_actuals) {
        ImGui::TextColored(col4(p.text_dim), "  %.2f ms  %lld row(s)",
                           node.actual_time,
                           static_cast<long long>(node.actual_rows));

        // Estimativa muito errada explica plano ruim: o planejador escolhe
        // Nested Loop porque acha que vem 1 linha, e vem 50 mil.
        const double error = node.estimation_error();
        if (error >= 10.0) {
            ImGui::SameLine();
            icon_inline(Icon::warning, p.warn, 0.85f);
            ImGui::SameLine(0.0f, 2.0f);
            ImGui::TextColored(col4(p.warn), TR("estimate off by %.0fx"), error);
        }
    } else {
        ImGui::TextColored(col4(p.text_dim), "  cost %.2f  ~%lld row(s)",
                           node.total_cost,
                           static_cast<long long>(node.estimated_rows));
    }

    // Condicoes no tooltip: ocupam muito espaco na linha e quase sempre sao
    // o que se quer ler depois de identificar o no' caro.
    if (ImGui::IsItemHovered()) {
        std::string tip = node.type;
        if (!node.relation.empty())        tip += "\non " + node.relation;
        if (!node.index_condition.empty()) tip += "\nIndex Cond: " + node.index_condition;
        if (!node.join_condition.empty())  tip += "\nJoin: " + node.join_condition;
        if (!node.filter.empty())          tip += "\nFilter: " + node.filter;
        if (!node.sort_keys.empty())       tip += "\nSort: " + node.sort_keys;

        char buffer[128];
        std::snprintf(buffer, sizeof buffer,
                      "\n\ncost %.2f..%.2f  width %d",
                      node.startup_cost, node.total_cost, node.row_width);
        tip += buffer;
        if (node.loops > 1) {
            std::snprintf(buffer, sizeof buffer, "\nloops %lld",
                          static_cast<long long>(node.loops));
            tip += buffer;
        }
        ImGui::SetTooltip("%s", tip.c_str());
    }

    if (open) {
        for (const db::PlanNode& child : node.children) {
            draw_plan_node(child, max_cost, depth + 1);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void MainShell::draw_plan_window() {
    const Palette& p = colors();

    ImGui::SetNextWindowSize(ImVec2(920, 560), ImGuiCond_Appearing);
    if (ImGui::Begin(TRW("Execution plan", "###ExecutionPlan"), &show_plan_,
                     ImGuiWindowFlags_NoDocking)) {

        // Caixa do ANALYZE com o aviso ao lado, nao num tooltip escondido:
        // marcar esta caixa faz a consulta RODAR (ADR 0013).
        if (ImGui::Checkbox(TR("Run the query and measure (ANALYZE)"),
                            &plan_analyze_)) {
            explain_current_sql(plan_analyze_);
        }
        if (plan_analyze_) {
            icon_inline(Icon::warning, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(col4(p.warn), TR(
                "ANALYZE executes the query. Writes are rolled back, but the "
                "work is done and the time is real."));
            ImGui::PopTextWrapPos();
        }

        ImGui::SameLine();
        if (icon_button("##replan", Icon::refresh, TR("Explain again"),
                        !session().busy())) {
            explain_current_sql(plan_analyze_);
        }

        ImGui::Separator();

        if (session().busy()) {
            ImGui::TextColored(col4(p.text_dim), TR("  explaining..."));
            ImGui::End();
            return;
        }

        if (!plan_ || plan_->empty()) {
            ImGui::TextColored(col4(p.text_dim), "%s",
                               session().status_message().c_str());
            ImGui::End();
            return;
        }

        if (plan_->analyzed) {
            ImGui::TextColored(col4(p.text_dim),
                               TR("planning %.2f ms  |  execution %.2f ms"),
                               plan_->planning_time, plan_->execution_time);
        } else {
            ImGui::TextColored(col4(p.text_dim),
                               TR("estimated cost %.2f  |  query not executed"),
                               plan_->root.total_cost);
        }

        ImGui::Separator();

        const double max_cost = plan_->max_total_cost();
        if (ImGui::BeginChild("##plantree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
            draw_plan_node(plan_->root, max_cost, 0);
        }
        ImGui::EndChild();

        if (icon_text_button("##copyplan", Icon::copy, TR("Copy JSON"),
                             TR("Copy the raw EXPLAIN output"))) {
            ImGui::SetClipboardText(plan_->raw_json.c_str());
        }
    }
    ImGui::End();
}

void MainShell::format_current_sql() {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    const std::string before = document->editor().GetText();
    if (before.empty()) return;

    const std::string after =
        sql::format_sql(before, active_dialect());

    // Texto igual: nao mexe. SetText move o cursor para o inicio e cria um
    // ponto de desfazer -- fazer isso quando nada mudou seria ruido.
    if (after == before) return;

    document->editor().SetText(after);
}

void MainShell::execute_script() {
    if (session().state() != SessionState::connected || session().busy()) return;

    SqlDocument* document = active_document();
    if (document == nullptr) return;

    const std::string text = document->editor().GetText();
    if (text.empty()) return;

    // O splitter respeita strings, comentarios, $$ ... $$ e blocos BEGIN/END.
    // Um split por ';' quebraria em qualquer funcao armazenada.
    const std::vector<sql::Statement> found =
        sql::split_script(text, active_dialect());

    std::vector<std::string> statements;
    statements.reserve(found.size());
    for (const sql::Statement& statement : found) {
        if (!statement.empty()) statements.emplace_back(statement.text);
    }

    if (statements.empty()) return;

    // Um comando so': usa o caminho normal, que pagina o resultado. Paginar
    // nao faz sentido para script -- o que interessa e' o efeito de cada
    // comando, nao navegar pelas linhas do ultimo.
    if (statements.size() == 1) {
        execute_current_sql();
        return;
    }

    executing_document_id_ = document->id();
    document->set_executing(true);
    document->set_status({});

    // Script nao e' paginado: os botoes de pagina somem, e a contagem exibida
    // passa a ser a do resultado inteiro do ultimo SELECT.
    document->reset_paging();

    session().execute_script_async(std::move(statements));
}

void MainShell::execute_page(SqlDocument& document, std::size_t page) {
    if (session().state() != SessionState::connected || session().busy()) return;
    if (document.paged_sql().empty()) return;

    // A reescrita com LIMIT/OFFSET impede que um SELECT sem limite trave a UI
    // ate' o servidor terminar de enviar tudo (ADR 0011). Quando nao e' seguro
    // reescrever, executa o original: rodar algo diferente do que o usuario
    // escreveu seria pior que a espera.
    const sql::PagedQuery paged = sql::make_paged_query(
        document.paged_sql(), active_dialect(), page,
        sql::kDefaultPageSize, document.sort(), document.filter());

    document.set_page(page);
    document.set_paged(paged.rewritten);
    document.set_has_more(false);

    executing_document_id_ = document.id();
    document.set_executing(true);
    document.set_status({});

    session().execute_async(paged.sql);
}

void MainShell::draw() {
    // Colhe o resultado e entrega ao documento que o pediu -- nao ao que
    // estiver ativo agora, porque o usuario pode ter trocado de aba.
    // Constraints chegaram: recalcula se o resultado da' para editar. Sem
    // isto, a grade ficaria somente leitura ate' o usuario reexecutar.
    if (pending_edit_target_ != 0 && !session().busy()) {
        for (auto& document : documents_) {
            if (document->id() != pending_edit_target_) continue;
            if (document->result().has_value()) {
                document->set_edit_target(
                    db::find_edit_target(*document->result(),
                                         session().schemas()));
            }
            break;
        }
        pending_edit_target_ = 0;
    }

    // Plano pronto: o worker guardou; a UI recolhe no quadro seguinte.
    if (show_plan_ && !session().busy() && !plan_) {
        if (auto fresh = session().take_plan()) plan_ = std::move(*fresh);
    }

    // DDL terminou: descarta o cache do nó para que a próxima expansão releia.
    // Sem isso a coluna recém-criada não apareceria até o usuário mandar
    // atualizar, e ele concluiria que o comando não funcionou.
    if (ddl_pending_reload_ && !session().busy()) {
        ddl_pending_reload_ = false;

        if (!session().last_script_failed() && !ddl_reload_table_.empty()) {
            session().invalidate_table(ddl_reload_schema_, ddl_reload_table_);
        }
        ddl_reload_schema_.clear();
        ddl_reload_table_.clear();
    }

    if (!session().busy() && executing_document_id_ != 0) {
        for (auto& document : documents_) {
            if (document->id() != executing_document_id_) continue;

            // Gravacao de edicoes: o buffer so' e' limpo quando os UPDATE
            // passaram. Limpar antes de saber perderia o trabalho se a
            // transacao falhasse -- e o usuario nao teria como refaze-lo.
            if (document->edits().has_changes() && saving_edits_) {
                saving_edits_ = false;
                if (!session().last_script_failed()) {
                    document->edits().clear();

                    // Relê para mostrar o que o banco REALMENTE gravou:
                    // trigger e DEFAULT podem ter mudado o valor.
                    //
                    // Vale tambem para resultado NAO paginado. Com o `if
                    // (paged())` sozinho, um SELECT curto salvava no banco e
                    // continuava exibindo o valor ANTIGO na grade -- o
                    // usuario concluia que a gravacao nao funcionou. Foi o
                    // que a captura mostrou: 8888.50 no MySQL, 5000.00 na
                    // tela.
                    if (!document->paged_sql().empty()) {
                        execute_page(*document, document->page());

                        // execute_page ja' armou executing_document_id_ para a
                        // releitura em curso. Sair do laco SEM passar pelo
                        // `executing_document_id_ = 0` logo abaixo, que
                        // apagaria a marca e faria a UI nunca colher o
                        // resultado novo -- a grade ficava com o valor ANTIGO
                        // enquanto o banco ja' tinha o novo. O sintoma: salvar
                        // 1234.56 e a grade continuar mostrando 8888.50.
                        document->set_executing(true);
                        rereading_after_save_ = true;
                    }
                }
            }

            if (auto fresh = session().take_result()) {
                // A pagina pediu uma linha a mais do que mostra. Se ela veio,
                // ha' mais resultado adiante -- e ela nao pode aparecer na
                // grade, senao o usuario veria 201 linhas ao pedir 200.
                if (document->paged()) {
                    const bool more = fresh->row_count() > sql::kDefaultPageSize;
                    document->set_has_more(more);
                    if (more) fresh->hide_rows_beyond(sql::kDefaultPageSize);
                }
                document->set_result(std::move(*fresh));

                // De onde o resultado veio decide se da' para editar. E'
                // calculado uma vez, aqui, e nao a cada quadro.
                document->edits().clear();
                recompute_groups(*document);

                // As regras de cor guardam índice de coluna e a faixa de cada
                // coluna em gradiente. Sem recalcular, a regra apontaria para
                // a coluna de índice N do resultado ANTERIOR -- colorindo a
                // coisa errada sem erro nenhum.
                document->color_rules().prepare(*document->result());
                recompute_pivot(*document);
                document->set_edit_target(
                    db::find_edit_target(*document->result(),
                                         session().schemas()));

                // Chave ainda nao lida: pede o carregamento. O alvo e'
                // recalculado no quadro seguinte, quando as constraints
                // chegarem -- sem isto, um SELECT numa tabela nunca expandida
                // ficaria somente leitura sem motivo real.
                if (document->edit_target().refusal ==
                        db::EditRefusal::key_not_loaded &&
                    !document->edit_target().table.empty()) {
                    session().load_constraints_async(
                        document->edit_target().schema,
                        document->edit_target().table);
                    pending_edit_target_ = document->id();
                }
            }

            // Conferir a janela de exportacao exige um resultado na tela e um
            // clique num botao de 24 px, que a automacao erra. A variavel
            // abre assim que o primeiro resultado chega -- ligar antes nao
            // funcionaria, porque a janela se fecha sozinha sem resultado.
            static const bool auto_export =
                std::getenv("OTTER_SHOW_EXPORT") != nullptr;
            if (auto_export) show_export_ = true;

            // A mensagem do worker conta as linhas que CHEGARAM, incluindo a
            // linha-sonda da paginacao. Refaz aqui, onde se sabe que ela
            // existe: a barra de status dizer "201 linhas" depois de mostrar
            // "linhas 1-200" e' contradicao na mesma tela.
            if (document->paged() && document->result().has_value()) {
                const db::ResultSet& rs = *document->result();
                document->set_status(TRF("%zu row(s), %zu column(s)",
                                         rs.row_count(), rs.column_count()));
            } else {
                document->set_status(session().status_message());
            }
            document->set_executing(false);
            break;
        }

        // Nao zera quando uma RELEITURA acabou de ser disparada: ela reusa
        // executing_document_id_, e apaga-lo aqui faria o resultado novo
        // chegar sem ninguem para colher.
        if (rereading_after_save_) rereading_after_save_ = false;
        else                       executing_document_id_ = 0;
    }

    // Atalhos globais. Registrados aqui, e nao so' rotulados no menu: um
    // atalho anunciado que nao funciona e' pior que nenhum.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter)) {
        execute_current_sql();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Alt | ImGuiKey_X)) {
        execute_script();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
        open_script_file();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F)) {
        format_current_sql();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_E)) {
        explain_current_sql(/*analyze=*/false);
    }
    // F5 reexecuta a consulta da aba ativa, como no DBeaver. Sem documento
    // com consulta, nao faz nada -- em vez de um erro sobre nada.
    if (ImGui::IsKeyPressed(ImGuiKey_F5, /*repeat=*/false)) {
        if (SqlDocument* document = active_document();
            document != nullptr && !document->paged_sql().empty()) {
            execute_page(*document, document->page());
        } else {
            execute_current_sql();
        }
    }
    // Shift primeiro: Ctrl+Shift+S tambem satisfaz Ctrl+S, e testar na ordem
    // inversa faria "salvar como" nunca acontecer.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
        save_script_file(/*save_as=*/true);
    } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        save_script_file(/*save_as=*/false);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N)) {
        connection_dialog_.open_new();
    }
    // Ctrl+T abre uma aba; Ctrl+W fecha a atual -- convencao de navegador.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_T)) {
        new_document();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W)) {
        close_document(active_document_);
    }
    // Commit e rollback: mesmos atalhos do DBeaver.
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C)) {
        session().commit_async();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R)) {
        session().rollback_async();
    }

    draw_menu_bar();
    draw_toolbar();
    draw_dockspace();

    draw_raft_panel();
    draw_navigator_panel();
    draw_editor_panel();
    draw_grid_panel();
    draw_query_log_panel();
    draw_status_bar();

    // Traduz o estado da sessao para o que o assistente precisa exibir.
    ConnectionDialog::Feedback feedback;
    feedback.busy      = session().busy();
    feedback.failed    = session().state() == SessionState::failed;
    feedback.succeeded = session().state() == SessionState::connected;
    if (feedback.failed || feedback.succeeded) {
        feedback.message = session().status_message();
    }
    connection_dialog_.draw(feedback);

    // Confirmacao de DDL. `ddl_in_transaction` vem das capabilities do driver:
    // no MySQL cada comando confirma sozinho, e a janela precisa dizer isso.
    {
        const bool connected = session().state() == SessionState::connected;
        bool transactional = true;
        if (connected && session().capabilities()) {
            transactional = session().capabilities()->ddl_in_transaction;
        }
        ddl_dialog_.draw(connected && !session().busy(), transactional);
    }
    draw_ddl_forms();
    draw_value_panel();

    if (show_about_) draw_about_window();
    if (show_plan_) draw_plan_window();
    if (show_export_) draw_export_window();
    if (show_import_) draw_import_window();
    if (show_icons_) draw_icon_gallery();
    if (show_demo_)  ImGui::ShowDemoWindow(&show_demo_);
}

void MainShell::draw_dockspace() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Host ocupa a area de trabalho menos as faixas da barra de ferramentas
    // (no topo) e da barra de status (no rodape).
    const ImVec2 host_size(
        vp->WorkSize.x,
        vp->WorkSize.y - kToolbarHeight - kStatusBarHeight);
    const ImVec2 host_pos(vp->WorkPos.x, vp->WorkPos.y + kToolbarHeight);

    ImGui::SetNextWindowPos(host_pos);
    ImGui::SetNextWindowSize(host_size);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##OtterDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dock_id = ImGui::GetID("OtterDockSpace");

    if (!layout_initialized_) {
        layout_initialized_ = true;

        ImGui::DockBuilderRemoveNode(dock_id);
        ImGui::DockBuilderAddNode(dock_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock_id, host_size);

        ImGuiID left = 0, center = 0;
        ImGui::DockBuilderSplitNode(dock_id, ImGuiDir_Left, 0.24f, &left, &center);

        ImGuiID left_top = 0, left_bottom = 0;
        ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.28f, &left_top, &left_bottom);

        ImGuiID center_top = 0, center_bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, 0.42f,
                                    &center_top, &center_bottom);

        ImGui::DockBuilderDockWindow("###RaftPanel",      left_top);
        ImGui::DockBuilderDockWindow("###NavigatorPanel", left_bottom);
        ImGui::DockBuilderDockWindow("###SqlPanel",       center_top);
        ImGui::DockBuilderDockWindow("###ResultPanel",    center_bottom);
        ImGui::DockBuilderDockWindow("###QueriesPanel",   center_bottom);
        ImGui::DockBuilderFinish(dock_id);
    }

    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

void MainShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu(TR("File"))) {
        if (ImGui::MenuItem(TR("New connection..."), "Ctrl+Shift+N")) {
            connection_dialog_.open_new();
        }
        if (ImGui::MenuItem(TR("Edit connection..."), nullptr, false,
                            session().state() == SessionState::connected)) {
            connection_dialog_.open_edit(active_profile_);
        }
        if (ImGui::MenuItem(TR("Import from DBeaver..."))) {
            show_import_ = true;
            import_scanned_ = false;
            import_status_.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("New SQL tab"), "Ctrl+T")) new_document();
        if (ImGui::MenuItem(TR("Open script..."), "Ctrl+O")) open_script_file();
        if (ImGui::MenuItem(TR("Save script"), "Ctrl+S", false,
                            active_document() != nullptr)) {
            save_script_file(/*save_as=*/false);
        }
        if (ImGui::MenuItem(TR("Save script as..."), "Ctrl+Shift+S", false,
                            active_document() != nullptr)) {
            save_script_file(/*save_as=*/true);
        }
        if (ImGui::MenuItem(TR("Close tab"), "Ctrl+W",
                            false, documents_.size() > 1)) {
            close_document(active_document_);
        }
        if (ImGui::MenuItem(TR("Disconnect"), nullptr, false,
                            session().state() == SessionState::connected)) {
            session().disconnect();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Exit"), "Alt+F4")) wants_quit_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Edit"))) {
        SqlDocument* document = active_document();
        const bool has_document = document != nullptr;

        if (ImGui::MenuItem(TR("Undo"), "Ctrl+Z", false,
                            has_document && document->editor().CanUndo())) {
            document->editor().Undo();
        }
        if (ImGui::MenuItem(TR("Redo"), "Ctrl+Y", false,
                            has_document && document->editor().CanRedo())) {
            document->editor().Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Select all"), "Ctrl+A", false, has_document)) {
            document->editor().SelectAll();
        }
        if (ImGui::MenuItem(TR("Find"), "Ctrl+F", false, has_document)) {
            document->editor().OpenFindReplaceWindow();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("SQL")) {
        const bool can_run = session().state() == SessionState::connected &&
                             !session().busy();
        if (ImGui::MenuItem(TR("Execute"), "Ctrl+Enter", false, can_run)) {
            execute_current_sql();
        }
        if (ImGui::MenuItem(TR("Execute script"), "Alt+X", false, can_run)) {
            execute_script();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Format SQL"), "Ctrl+Shift+F", false,
                            active_document() != nullptr)) {
            format_current_sql();
        }
        if (ImGui::MenuItem(TR("Explain plan"), "Ctrl+Shift+E", false, can_run)) {
            explain_current_sql(/*analyze=*/false);
        }
        ImGui::Separator();

        const bool auto_commit = session().auto_commit();
        const bool in_txn = session().txn_state() != db::TxnState::idle;

        bool toggle = auto_commit;
        if (ImGui::MenuItem(TR("Auto-commit"), nullptr, &toggle, can_run)) {
            session().set_auto_commit_async(toggle);
        }
        if (ImGui::MenuItem(TR("Commit"), "Ctrl+Shift+C", false,
                            can_run && !auto_commit && in_txn)) {
            session().commit_async();
        }
        if (ImGui::MenuItem(TR("Rollback"), "Ctrl+Shift+R", false,
                            can_run && !auto_commit && in_txn)) {
            session().rollback_async();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TR("Help"))) {
        // Seletor de tema: troca em tempo real, sem reiniciar.
        if (ImGui::BeginMenu(TR("Theme"))) {
            const std::string active_theme = current_theme().id;
            for (const Theme& theme : available_themes()) {
                const bool selected = active_theme == theme.id;
                if (ImGui::MenuItem(TR(theme.name.c_str()), nullptr, selected)) {
                    set_theme(theme.id);
                    // Os editores já criados guardam a paleta antiga.
                    for (auto& document : documents_) {
                        apply_editor_palette(document->editor());
                    }
                }
            }
            ImGui::EndMenu();
        }

        // Seletor de idioma: troca em tempo real, sem reiniciar.
        if (ImGui::BeginMenu(TR("Language"))) {
            const std::string_view active = i18n::current_language();
            for (const i18n::Language& language : i18n::available_languages()) {
                const bool selected = active == language.code;
                if (ImGui::MenuItem(language.native_name.c_str(), nullptr,
                                    selected)) {
                    i18n::set_language(language.code);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem(TR("Icon gallery"), nullptr, &show_icons_);
        ImGui::MenuItem(TR("ImGui demo"), nullptr, &show_demo_);
        ImGui::Separator();
        if (ImGui::MenuItem(TR("About C-Otter"))) show_about_ = true;
        ImGui::EndMenu();
    }

    const ImGuiIO& io = ImGui::GetIO();
    char fps[48];
    std::snprintf(fps, sizeof(fps), "%.1f fps  |  %.2f ms",
                  static_cast<double>(io.Framerate),
                  static_cast<double>(1000.0f / io.Framerate));
    const float width = ImGui::CalcTextSize(fps).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
    ImGui::TextColored(col4(colors().text_dim), "%s", fps);

    ImGui::EndMainMenuBar();
}

// Nome do SGBD para exibicao, a partir do driver.
//
// Existe porque "PostgreSQL" estava cravado em dois pontos da tela, e uma
// conexao MySQL exibia "PostgreSQL 8.0.46" na barra de status e no Raft --
// dois campos mentindo sobre o que esta' do outro lado.
std::string MainShell::dbms_name(const std::string& driver_id) {
    const db::Driver* driver = db::find_driver(driver_id);
    return driver != nullptr ? std::string(driver->display_name()) : driver_id;
}

void MainShell::draw_raft_panel() {
    if (ImGui::Begin(TRW("Raft", "###RaftPanel"))) {
        const Palette& p = colors();

        if (ImGui::Button(TR("New connection"))) connection_dialog_.open_new();

        ImGui::SameLine();
        ImGui::BeginDisabled(session().state() != SessionState::connected);
        if (ImGui::Button(TR("Edit"))) connection_dialog_.open_edit(active_profile_);
        ImGui::EndDisabled();

        ImGui::Separator();

        // Conexoes abertas, a ativa em destaque. Uma Session vazia e' o estado
        // inicial, nao uma conexao: nao vale uma linha na lista.
        std::size_t drawn = 0;
        std::size_t close_requested = connections_.size();

        for (std::size_t i = 0; i < connections_.size(); ++i) {
            const Connection& connection = connections_[i];
            const SessionState state = connection.session->state();
            if (state == SessionState::disconnected &&
                connection.profile.host.empty()) {
                continue;
            }
            ++drawn;

            ImGui::PushID(static_cast<int>(i));

            const bool active    = i == active_connection_;
            const bool connected = state == SessionState::connected;

            const std::uint32_t status_color =
                connected                       ? p.ok
                : state == SessionState::failed ? p.error
                : state == SessionState::connecting ? p.warn
                                                    : p.text_dim;

            ImGui::TextColored(col4(status_color), "●");
            ImGui::SameLine(0.0f, 6.0f);

            // Selecionavel de largura total: trocar de conexao e' um clique
            // em qualquer ponto da linha, como no DBeaver.
            ImGui::PushStyleColor(ImGuiCol_Text, col(active ? p.text_bright
                                                            : p.text));
            if (ImGui::Selectable(connection.profile.effective_name().c_str(),
                                  active, ImGuiSelectableFlags_SpanAllColumns)) {
                active_connection_ = i;
                active_profile_    = connection.profile;
            }
            ImGui::PopStyleColor();

            if (ImGui::BeginPopupContextItem("##connmenu")) {
                if (ImGui::MenuItem(TR("Edit connection..."))) {
                    active_connection_ = i;
                    active_profile_    = connection.profile;
                    connection_dialog_.open_edit(connection.profile);
                }
                if (ImGui::MenuItem(TR("Disconnect"), nullptr, false, connected)) {
                    connection.session->disconnect();
                }
                if (ImGui::MenuItem(TR("Close connection"))) {
                    close_requested = i;
                }
                ImGui::Separator();
                if (ImGui::MenuItem(TR("Copy name"))) {
                    ImGui::SetClipboardText(
                        connection.profile.effective_name().c_str());
                }
                ImGui::EndPopup();
            }

            // Detalhe so' da ativa: repetir host, versao e modo de transacao
            // para cada conexao encheria o painel de texto igual.
            if (active) {
                ImGui::Indent();

                const db::ConnectionTypeInfo& type =
                    db::connection_type_info(connection.profile.type);
                ImGui::TextColored(col4(type.color), "%s", type.name);

                if (connected) {
                    ImGui::TextColored(
                        col4(p.text_dim), "%s %s",
                        dbms_name(connection.profile.driver_id).c_str(),
                        connection.session->server_version().c_str());
                    ImGui::TextColored(col4(p.text_dim), "%s:%u",
                                       connection.profile.host.c_str(),
                                       connection.profile.port);
                    ImGui::TextColored(col4(p.text_dim), "%s",
                                       connection.profile.auto_commit
                                           ? TR("auto-commit")
                                           : TR("manual transaction"));
                    if (connection.profile.read_only) {
                        ImGui::TextColored(col4(p.warn), TR("read only"));
                    }
                } else if (state == SessionState::failed) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(col4(p.error), "%s",
                                       connection.session->status_message().c_str());
                    ImGui::PopTextWrapPos();
                }

                if (!connection.profile.description.empty()) {
                    ImGui::TextColored(col4(p.text_dim), "%s",
                                       connection.profile.description.c_str());
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        // Fora do laco: apagar do vector enquanto se itera invalidaria o
        // iterador e a referencia devolvida por session().
        if (close_requested < connections_.size()) {
            close_connection(close_requested);
        }

        if (drawn == 0) {
            ImGui::TextColored(col4(p.text_dim), TR("no connection"));
        }

        draw_saved_profiles();
    }
    ImGui::End();
}

// Perfis salvos que ainda nao foram abertos.
//
// Sem esta secao, importar do DBeaver gravava o perfil e nao mudava nada na
// tela: a lista acima so' mostra conexoes ABERTAS, e o unico caminho ate' o
// que foi importado era reabrir o dialogo de conexao. Foi o que a captura de
// tela mostrou depois de importar -- "1 conexao importada" e nenhuma linha
// nova.
void MainShell::draw_saved_profiles() {
    const Palette& p = colors();

    // Um perfil ja' aberto nao se repete aqui: apareceria duas vezes na mesma
    // lista, uma como conexao e outra como atalho para ela mesma.
    auto already_open = [this](const db::ConnectionProfile& profile) {
        for (const Connection& connection : connections_) {
            if (connection.profile.host == profile.host &&
                connection.profile.port == profile.port &&
                connection.profile.database == profile.database &&
                connection.profile.user == profile.user) {
                return true;
            }
        }
        return false;
    };

    std::size_t pending = 0;
    for (const db::StoredProfile& stored : saved_profiles_) {
        if (!already_open(stored.profile)) ++pending;
    }
    if (pending == 0) return;

    ImGui::Separator();
    ImGui::TextColored(col4(p.text_dim), TR("saved"));

    for (std::size_t i = 0; i < saved_profiles_.size(); ++i) {
        const db::StoredProfile& stored = saved_profiles_[i];
        if (already_open(stored.profile)) continue;

        ImGui::PushID(static_cast<int>(1000 + i));

        ImGui::BeginDisabled(!stored.supported);

        // Duplo clique conecta; clique simples so' seleciona. Conectar no
        // primeiro clique abriria conexao a cada roçada do mouse na lista.
        if (ImGui::Selectable(stored.profile.effective_name().c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                open_connection(stored.profile);
            }
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s@%s:%u\n%s", stored.profile.user.c_str(),
                              stored.profile.host.c_str(), stored.profile.port,
                              stored.supported
                                  ? TR("double-click to connect")
                                  : TR(stored.unsupported_reason.c_str()));
        }

        if (ImGui::BeginPopupContextItem("##savedmenu")) {
            if (ImGui::MenuItem(TR("Connect"), nullptr, false, stored.supported)) {
                open_connection(stored.profile);
            }
            if (ImGui::MenuItem(TR("Edit connection..."))) {
                connection_dialog_.open_edit(stored.profile);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
}

bool MainShell::matches_filter(std::string_view name) const {
    if (navigator_filter_[0] == 0) return true;

    // Sem diferenciar maiusculas: quem digita "cliente" espera achar
    // "TIDxCliente".
    const std::string_view needle(navigator_filter_);
    const auto it = std::search(
        name.begin(), name.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != name.end();
}

void MainShell::draw_navigator_panel() {
    if (ImGui::Begin(TRW("Navigator", "###NavigatorPanel"))) {
        if (session().state() != SessionState::connected) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("connect to browse the schema"));
            ImGui::End();
            return;
        }

        // Filtro por nome. Num banco com 32 tabelas rolar resolve; com 300,
        // nao -- e o ERP_TID do usuario tem varios schemas.
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##navfilter", TR("Filter objects..."),
                                 navigator_filter_, sizeof navigator_filter_);
        ImGui::Separator();

        const std::vector<db::SchemaMeta> schemas = session().schemas();

        for (const db::SchemaMeta& schema : schemas) {
            ImGui::PushID(schema.name.c_str());

            icon_inline(Icon::schema, colors().accent_light);
            ImGui::SameLine(0.0f, 4.0f);

            const bool schema_open =
                ImGui::TreeNodeEx(schema.name.c_str(),
                                  ImGuiTreeNodeFlags_DefaultOpen);

            if (schema_open) {
                // Ordem do DBeaver: tabelas, views, materialized views,
                // sequences, rotinas.
                draw_relations_folder(schema, db::ObjKind::table,
                                      Icon::table, TR("Tables"));
                draw_relations_folder(schema, db::ObjKind::view,
                                      Icon::view, TR("Views"));
                draw_relations_folder(schema, db::ObjKind::materialized_view,
                                      Icon::materialized_view,
                                      TR("Materialized views"));
                // Pastas que o SGBD nao tem ficam FORA, em vez de aparecerem
                // com (0): "Sequences (0)" num MySQL sugere que ele poderia
                // ter uma, e manda o usuario procurar o que nao existe. E' o
                // mesmo criterio que ja' esconde "Constraints" de uma view.
                if (session().has_sequences())  draw_sequences_folder(schema);

                draw_routines_folder(schema);

                if (session().has_user_types()) draw_types_folder(schema);
                if (session().has_events())     draw_events_folder(schema);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }

        // --- System Info ------------------------------------------------------
        //
        // No NÍVEL DA CONEXÃO, depois dos bancos -- como no `<tree>` do
        // DBeaver, onde "System Info" é irmão de "Databases", não filho.
        // Colocá-lo dentro de um banco sugeriria que os números são daquele
        // banco, e são do SERVIDOR.
        if (session().state() == SessionState::connected) {
            if (session().has_users())       draw_users_folder();
            if (session().has_server_info()) draw_server_info_folder();
        }
    }
    ImGui::End();
}

// As contas do servidor, com os GRANTs de cada uma.
//
// Os grants são carregados por usuário, ao expandir: `SHOW GRANTS` é uma
// consulta por conta, e num servidor com 50 contas carregar tudo junto seriam
// 50 idas ao servidor para uma árvore que talvez nem seja aberta.
void MainShell::draw_users_folder() {
    const Palette& p = colors();
    const std::vector<db::UserMeta> users = session().users();

    if (!draw_folder_node(Icon::user, TR("Users"), users.size(),
                          session().users_loaded())) {
        return;
    }

    if (!session().users_loaded() && !session().busy()) {
        session().load_users_async();
    }

    if (session().users_loaded() && users.empty()) {
        // Lista vazia quase sempre é falta de privilégio em mysql.user, não
        // ausência de contas -- todo servidor tem ao menos uma. Dizer isso
        // evita que o usuário conclua o contrário.
        ImGui::TextColored(col4(p.text_dim),
                           TR("no access to the user list"));
    }

    for (const db::UserMeta& user : users) {
        if (!matches_filter(user.name)) continue;

        ImGui::PushID(user.qualified().c_str());

        icon_inline(Icon::user, user.locked ? p.error : p.text_dim);
        ImGui::SameLine(0.0f, 4.0f);

        // Conta bloqueada ou com senha expirada sai marcada: ela EXISTE, mas
        // não conecta -- e é essa a informação que importa ao olhar a lista.
        const bool usable = !user.locked && !user.expired;

        const bool open = ImGui::TreeNodeEx(
            user.qualified().c_str(),
            ImGuiTreeNodeFlags_SpanAvailWidth |
            (usable ? 0 : ImGuiTreeNodeFlags_Selected));

        if (!usable) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.error), "%s",
                               user.locked ? TR("[locked]") : TR("[expired]"));
        }

        if (ImGui::IsItemHovered() && !user.plugin.empty()) {
            ImGui::SetTooltip("%s", user.plugin.c_str());
        }

        if (open) {
            if (!user.grants_loaded && !session().busy()) {
                session().load_grants_async(user.name, user.host);
            }

            if (user.grants_loaded && user.grants.empty()) {
                ImGui::TextColored(col4(p.text_dim), TR("no grants"));
            }

            for (const std::string& grant : user.grants) {
                ImGui::BeginGroup();
                icon_inline(Icon::grant, p.text_dim);
                ImGui::SameLine(0.0f, 4.0f);

                // O texto do GRANT é longo. Truncar na largura do painel e
                // mostrar o inteiro no tooltip é mais legível que quebrar em
                // três linhas cada um.
                ImGui::TextColored(col4(p.text), "%s", grant.c_str());
                ImGui::EndGroup();

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", grant.c_str());
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// As quatro pastas de estatísticas do servidor, mais engines e charsets.
//
// Os valores são carregados sob demanda e NÃO são recarregados sozinhos: um
// status que muda a cada segundo, redesenhado a 60 fps, seria ilegível. O
// usuário pede a atualização quando quiser.
void MainShell::draw_server_info_folder() {
    const Palette& p = colors();

    icon_inline(Icon::info, p.accent_light);
    ImGui::SameLine(0.0f, 4.0f);

    if (!ImGui::TreeNodeEx(TR("System info"), ImGuiTreeNodeFlags_SpanAvailWidth)) {
        return;
    }

    // Uma lista de pares nome/valor, com filtro. Sem filtro, "SHOW GLOBAL
    // STATUS" devolve ~500 linhas e achar uma é rolar a árvore inteira.
    const auto draw_variables = [&](const char* label, Icon icon,
                                    const std::vector<db::ServerVariable>& values,
                                    bool loaded, auto&& request) {
        if (!draw_folder_node(icon, label, values.size(), loaded)) return;

        if (!loaded && !session().busy()) request();

        // O filtro do Navigator vale aqui também: é o mesmo campo, e quem
        // digitou "innodb" quer ver as variáveis de InnoDB.
        std::size_t shown = 0;
        for (const db::ServerVariable& variable : values) {
            if (!matches_filter(variable.name)) continue;
            if (++shown > 200) {
                ImGui::TextColored(col4(p.text_dim),
                                   TR("... and more; use the filter"));
                break;
            }

            ImGui::BeginGroup();
            ImGui::TextColored(col4(p.text), "%s", variable.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "%s", variable.value.c_str());
            ImGui::EndGroup();

            if (!variable.detail.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", variable.detail.c_str());
            }
        }
        ImGui::TreePop();
    };

    draw_variables(TR("Session status"), Icon::info,
                   session().session_status(), session().session_status_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::session_status); });

    draw_variables(TR("Global status"), Icon::info,
                   session().global_status(), session().global_status_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::global_status); });

    draw_variables(TR("Session variables"), Icon::settings,
                   session().session_variables(),
                   session().session_variables_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::session_variables); });

    draw_variables(TR("Global variables"), Icon::settings,
                   session().global_variables(),
                   session().global_variables_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::global_variables); });

    draw_variables(TR("Engines"), Icon::database,
                   session().engines(), session().engines_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::engines); });

    draw_variables(TR("Charsets"), Icon::data_type,
                   session().charsets(), session().charsets_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::charsets); });

    ImGui::TreePop();
}

// Pasta com contagem e um ícone. O número evita expandir só para descobrir que
// está vazio -- é o padrão do DBeaver (docs/NAVIGATOR-TREE.md).
bool MainShell::draw_folder_node(Icon icon, const char* label, std::size_t count,
                                 bool loaded) {
    icon_inline(icon, colors().accent_light);
    ImGui::SameLine(0.0f, 4.0f);

    // OTTER_EXPAND_TREE abre todas as pastas na captura de tela. Conferir os
    // icones de constraint, indice, FK e trigger exige chegar ate' o quarto
    // nivel da arvore, e clicar la' por automacao erra o alvo.
    static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
    if (expand_all) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

    const bool open = ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);

    if (loaded) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "(%zu)", count);
    }
    return open;
}

void MainShell::draw_relations_folder(const db::SchemaMeta& schema,
                                      db::ObjKind kind, Icon icon,
                                      const char* label) {
    // Uma unica consulta traz tabelas, views e materialized views (pg_class
    // com relkind r/v/m/p); as pastas apenas filtram o resultado. Consultar
    // tres vezes o mesmo pg_class seria desperdicio.
    std::size_t count = 0;
    for (const db::TableMeta& relation : schema.tables) {
        // Conta so' o que passa no filtro: "Tabelas (32)" com 3 visiveis
        // seria contradicao na mesma linha.
        if (relation.kind == kind && matches_filter(relation.name)) ++count;
    }

    // Pasta vazia fica escondida, como no DBeaver: um schema sem views nao
    // precisa de um no "Views (0)" ocupando espaco.
    if (count == 0 && schema.tables_loaded) return;

    if (!draw_folder_node(icon, label, count, schema.tables_loaded)) return;

    const Palette& p = colors();
    const std::uint32_t tint = kind == db::ObjKind::table ? p.accent : p.data;

    bool first = true;
    for (const db::TableMeta& relation : schema.tables) {
        if (relation.kind != kind) continue;
        if (!matches_filter(relation.name)) continue;

        ImGui::PushID(relation.name.c_str());

        icon_inline(icon, tint);
        ImGui::SameLine(0.0f, 4.0f);

        ImGui::PushStyleColor(ImGuiCol_Text,
                              col(kind == db::ObjKind::table ? p.text : p.data));
        // So' a primeira de cada pasta: abrir as 32 encheria a arvore de ruido
        // e dispararia 32 consultas de catalogo de uma vez.
        static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
        if (expand_all && first) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        first = false;

        const bool open = ImGui::TreeNode(relation.name.c_str());
        ImGui::PopStyleColor();

        // Duplo clique abre os dados -- e' o gesto que todo cliente de banco
        // tem, e sem ele o usuario precisa do menu de contexto para a acao
        // mais frequente.
        //
        // O TreeNode ja' consome o duplo clique para expandir; IsItemToggled
        // distingue os dois casos.
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsItemToggledOpen()) {

            // Sem as colunas o SELECT sai com '*' e um comentario pedindo
            // para expandir -- o usuario pediu os dados, nao um recado. Se
            // ainda nao chegaram, pede e usa '*' nesta vez: mostrar os dados
            // agora vale mais que uma lista de colunas um quadro depois.
            if (!relation.columns_loaded && !session().busy()) {
                session().load_columns_async(schema.name, relation.name);
            }
            open_sql_tab(db::generate_select(schema.name, relation),
                         /*run=*/true);
        }

        // Logo apos o TreeNode: BeginPopupContextItem usa o ultimo item, e
        // qualquer TextColored entre os dois roubaria o alvo do menu.
        draw_relation_context_menu(schema, relation);

        if (!relation.size_pretty.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "  %s",
                               relation.size_pretty.c_str());
        }

        if (ImGui::IsItemHovered() && !relation.comment.empty()) {
            ImGui::SetTooltip("%s", relation.comment.c_str());
        }

        if (open) {
            draw_table_children(schema, relation);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

// Cada linha da arvore e' envolvida em BeginGroup/EndGroup.
//
// Sem isso, ImGui::IsItemHovered() testa apenas o ULTIMO item desenhado: o
// tooltip da coluna so' aparecia sobre o texto do tipo, o do indice so' sobre
// o tamanho. O grupo faz o retangulo cobrir a linha toda, que e' o alvo que o
// usuario enxerga.
void MainShell::draw_table_children(const db::SchemaMeta& schema,
                                    const db::TableMeta& table) {
    const Palette& p = colors();

    // --- Colunas -------------------------------------------------------------
    if (draw_folder_node(Icon::column, TR("Columns"), table.columns.size(),
                         table.columns_loaded)) {
        if (!table.columns_loaded && !session().busy()) {
            session().load_columns_async(schema.name, table.name);
        }
        if (table.columns.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
        }

        for (const db::ColumnMeta& column : table.columns) {
            ImGui::BeginGroup();
            icon_inline(column.primary_key ? Icon::key : Icon::column,
                        column.primary_key ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, 4.0f);

            ImGui::TextColored(col4(column.primary_key ? p.data_light : p.text),
                               "%s", column.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s%s%s",
                               column.type_name.c_str(),
                               column.primary_key ? "  PK" : "",
                               column.nullable ? "" : "  NOT NULL");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                std::string tip = column.type_name;
                if (!column.default_value.empty()) {
                    tip += "\nDEFAULT " + column.default_value;
                }
                if (!column.comment.empty()) tip += "\n\n" + column.comment;
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Constraints ---------------------------------------------------------
    //
    // Uma view nao tem constraints nem chaves estrangeiras. O DBeaver nem
    // mostra as pastas nesse caso, e mostrar "(0)" sugeriria que a view
    // poderia ter uma.
    if (table.has_constraints() &&
        draw_folder_node(Icon::constraint, TR("Constraints"),
                         table.constraints.size(), table.constraints_loaded)) {
        if (!table.constraints_loaded && !session().busy()) {
            session().load_constraints_async(schema.name, table.name);
        }
        for (const db::ConstraintMeta& constraint : table.constraints) {
            const bool is_pk = constraint.kind == db::ObjKind::primary_key;
            ImGui::BeginGroup();
            icon_inline(is_pk ? Icon::key : Icon::constraint,
                        is_pk ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, 4.0f);

            ImGui::TextColored(col4(is_pk ? p.data_light : p.text), "%s",
                               constraint.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s",
                               std::string(db::to_string(constraint.kind)).c_str());
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", constraint.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Índices -------------------------------------------------------------
    //
    // A view comum nao tem indices, mas a materializada tem -- e' justamente
    // o que permite indexa-la como uma tabela.
    if (table.has_indexes() &&
        draw_folder_node(Icon::index, TR("Indexes"), table.indexes.size(),
                         table.indexes_loaded)) {
        if (!table.indexes_loaded && !session().busy()) {
            session().load_indexes_async(schema.name, table.name);
        }
        for (const db::IndexMeta& index : table.indexes) {
            // Índice inválido (CREATE INDEX CONCURRENTLY que falhou) existe mas
            // não é usado pelo planejador -- precisa ser visível.
            const std::uint32_t color = !index.valid ? p.error
                                        : index.primary ? p.data_light
                                                        : p.text;

            ImGui::BeginGroup();
            icon_inline(Icon::index, color);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(color), "%s", index.name.c_str());

            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s  %s%s%s",
                               index.method.c_str(),
                               index.size_pretty.c_str(),
                               index.unique ? "  UNIQUE" : "",
                               index.valid ? "" : "  INVALID");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", index.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Chaves estrangeiras -------------------------------------------------
    if (table.has_constraints() &&
        draw_folder_node(Icon::foreign_key, TR("Foreign keys"),
                         table.foreign_keys.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session().busy()) {
            session().load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& key : table.foreign_keys) {
            ImGui::BeginGroup();
            icon_inline(Icon::foreign_key, p.accent_light);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.text), "%s", key.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "→ %s.%s", key.target_table.c_str(),
                               key.target_column.c_str());
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n\nON UPDATE %s\nON DELETE %s",
                                  key.definition.c_str(),
                                  key.on_update.c_str(), key.on_delete.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Referências ---------------------------------------------------------
    //
    // Quem aponta para esta tabela. Responder "o que depende disto?" é o que
    // mais falta num cliente SQL.
    if (table.has_constraints() &&
        draw_folder_node(Icon::references, TR("References"),
                         table.references.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session().busy()) {
            session().load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& reference : table.references) {
            icon_inline(Icon::references, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn), "%s.%s",
                               reference.source_table.c_str(),
                               reference.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "→ %s",
                               reference.target_column.c_str());
        }
        ImGui::TreePop();
    }

    // --- Triggers ------------------------------------------------------------
    //
    // A materialized view nao aceita trigger: ela e' atualizada por REFRESH,
    // nao por DML. A view comum aceita INSTEAD OF.
    if (table.has_triggers() &&
        draw_folder_node(Icon::trigger, TR("Triggers"), table.triggers.size(),
                         table.triggers_loaded)) {
        if (!table.triggers_loaded && !session().busy()) {
            session().load_triggers_async(schema.name, table.name);
        }
        for (const db::TriggerMeta& trigger : table.triggers) {
            ImGui::BeginGroup();
            icon_inline(Icon::trigger, trigger.enabled ? p.text_dim : p.error);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(trigger.enabled ? p.text : p.text_dim),
                               "%s", trigger.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s %s%s",
                               trigger.timing.c_str(), trigger.events.c_str(),
                               trigger.enabled ? "" : "  [off]");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", trigger.definition.c_str());
            }
        }
        ImGui::TreePop();
    }

    // --- Partições -----------------------------------------------------------
    //
    // A pasta só aparece quando a tabela É particionada. Mostrar
    // "Partições (0)" em toda tabela comum encheria a árvore de ruído.
    //
    // O carregamento é pedido sempre que a pasta é expandida pela primeira
    // vez: não dá para saber se a tabela é particionada sem perguntar, e
    // perguntar para TODA tabela ao montar a árvore custaria uma consulta por
    // tabela.
    if (!table.is_view() &&
        draw_folder_node(Icon::partition, TR("Partitions"),
                         table.partitions.size(), table.partitions_loaded)) {
        if (!table.partitions_loaded && !session().busy()) {
            session().load_partitions_async(schema.name, table.name);
        }

        if (table.partitions_loaded && table.partitions.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("not partitioned"));
        }

        for (const db::PartitionMeta& partition : table.partitions) {
            ImGui::BeginGroup();
            icon_inline(Icon::partition, p.text_dim);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.text), "%s", partition.name.c_str());

            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s", partition.method.c_str());

            if (!partition.size_pretty.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   partition.size_pretty.c_str());
            }
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s %s\n%s",
                                  partition.method.c_str(),
                                  partition.expression.c_str(),
                                  partition.description.c_str());
            }

            // "Ver dados" só onde a partição É uma tabela consultável: no
            // MySQL ela é divisão interna, e `schema.particao` não existe.
            if (partition.is_table &&
                ImGui::BeginPopupContextItem(partition.name.c_str())) {
                if (ImGui::MenuItem(TR("View data"))) {
                    open_sql_tab("SELECT * FROM " +
                                 db::qualified_name(schema.name, partition.name) +
                                 " LIMIT 200", /*run=*/true);
                }
                ImGui::EndPopup();
            }

            for (const std::string& sub : partition.subpartitions) {
                ImGui::Indent();
                ImGui::TextColored(col4(p.text_dim), "%s", sub.c_str());
                ImGui::Unindent();
            }
        }
        ImGui::TreePop();
    }

    // --- Corpo da view -------------------------------------------------------
    if (table.is_view()) draw_view_definition(schema, table);
}

void MainShell::open_sql_tab(std::string sql, bool run) {
    // new_document() ja' torna a aba nova a ativa, e execute_current_sql()
    // opera sobre a ativa -- a ordem aqui nao e' acidental.
    SqlDocument& document = new_document();
    document.editor().SetText(sql);

    // Executar so' quando pedido: "ver dados" e' uma acao, "gerar SELECT" e'
    // um ponto de partida para editar.
    if (run) execute_current_sql();
}

void MainShell::draw_relation_context_menu(const db::SchemaMeta& schema,
                                           const db::TableMeta& relation) {
    if (!ImGui::BeginPopupContextItem("##relmenu")) return;

    const std::string full = db::qualified_name(schema.name, relation.name);

    // Carrega as colunas enquanto o menu esta' aberto, nao ao clicar num item.
    //
    // Sem isto, "Ver dados" numa tabela nunca expandida gerava `SELECT *` com
    // um comentario pedindo para expandir -- o usuario pediu os dados, nao um
    // recado.
    //
    // `relation` e' uma copia do quadro corrente: o resultado do worker so'
    // aparece no quadro seguinte. Como o menu sobrevive entre quadros, o
    // clique acontece depois -- mas se acontecer antes, o SQL sai com `*` em
    // vez de errado, que e' a degradacao aceitavel.
    if (!relation.columns_loaded && !session().busy()) {
        session().load_columns_async(schema.name, relation.name);
    }

    // Ver dados: a acao mais frequente, no topo e destacada.
    if (ImGui::MenuItem(TR("View data"))) {
        open_sql_tab(db::generate_select(schema.name, relation), /*run=*/true);
    }
    if (ImGui::MenuItem(TR("Count rows"))) {
        open_sql_tab(db::generate_count(schema.name, relation), /*run=*/true);
    }

    ImGui::Separator();

    if (ImGui::BeginMenu(TR("Generate SQL"))) {
        // Sem as colunas carregadas, o SQL gerado seria '*' ou vazio. Em vez
        // de gerar algo pobre, o menu diz o que falta.
        if (!relation.columns_loaded) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("expand the table first"));
        }

        if (ImGui::MenuItem("SELECT")) {
            open_sql_tab(db::generate_select(schema.name, relation, 0), false);
        }

        // INSERT, UPDATE e DELETE nao se aplicam a view: escrever numa view
        // exige trigger INSTEAD OF, e oferecer o comando sugeriria que
        // funciona.
        ImGui::BeginDisabled(relation.is_view());
        if (ImGui::MenuItem("INSERT")) {
            open_sql_tab(db::generate_insert(schema.name, relation), false);
        }
        if (ImGui::MenuItem("UPDATE")) {
            open_sql_tab(db::generate_update(schema.name, relation), false);
        }
        if (ImGui::MenuItem("DELETE")) {
            open_sql_tab(db::generate_delete(schema.name, relation), false);
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItem("DDL")) {
            open_sql_tab(db::generate_ddl(schema.name, relation), false);
        }
        ImGui::EndMenu();
    }

    // --- Alterar a estrutura (docs/DDL-WRITE.md) -----------------------------
    //
    // Toda ação aqui passa pela janela de confirmação: DDL não tem desfazer, e
    // o usuário precisa ver o comando antes de ele rodar.

    ImGui::Separator();

    const bool can_alter = session().state() == SessionState::connected &&
                           !session().busy();

    if (ImGui::BeginMenu(TR("Alter"), can_alter && !relation.is_view())) {
        if (!relation.columns_loaded) {
            // Sem as colunas não dá para montar um MODIFY completo no MySQL.
            // Dizer o que falta é melhor que oferecer um menu que gera erro.
            ImGui::TextColored(col4(colors().text_dim),
                               TR("expand the table first"));
        }

        ImGui::BeginDisabled(!relation.columns_loaded);
        if (ImGui::MenuItem(TR("Add column..."))) {
            open_add_column(schema.name, relation);
        }
        if (ImGui::BeginMenu(TR("Drop column"))) {
            for (const db::ColumnMeta& column : relation.columns) {
                if (ImGui::MenuItem(column.name.c_str())) {
                    db::TableAlteration wanted;
                    wanted.schema = schema.name;
                    wanted.table  = relation.name;
                    wanted.drop_columns.push_back(column.name);

                    confirm_ddl(TRF("Drop column %s from %s",
                                    column.name.c_str(), relation.name.c_str()),
                                db::generate_alter(relation, wanted),
                                schema.name, relation.name);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        // Índices e constraints. As listas vêm do que já foi carregado: sem
        // expandir a pasta não há o que remover, e o item diz isso em vez de
        // aparecer vazio sem explicação.
        ImGui::BeginDisabled(!relation.columns_loaded);
        if (ImGui::MenuItem(TR("Add index..."))) {
            open_add_index(schema.name, relation);
        }
        ImGui::EndDisabled();

        if (ImGui::BeginMenu(TR("Drop index"))) {
            if (!relation.indexes_loaded) {
                ImGui::TextColored(col4(colors().text_dim),
                                   TR("expand Indexes first"));
            }
            for (const db::IndexMeta& index : relation.indexes) {
                // Índice de chave primária ou única não se remove sozinho: a
                // operação correta é remover a CONSTRAINT. Mostrar esmaecido
                // em vez de esconder diz que ele existe e por que não dá.
                const bool from_constraint = index.primary || index.unique;

                ImGui::BeginDisabled(from_constraint);
                if (ImGui::MenuItem(index.name.c_str())) {
                    confirm_ddl(TRF("Drop index %s", index.name.c_str()),
                                db::generate_drop_index(schema.name, relation.name,
                                                        index.name, false),
                                schema.name, relation.name);
                }
                ImGui::EndDisabled();

                if (from_constraint && ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("%s",
                                      TR("belongs to a constraint; drop the "
                                         "constraint instead"));
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Drop constraint"))) {
            if (!relation.constraints_loaded) {
                ImGui::TextColored(col4(colors().text_dim),
                                   TR("expand Constraints first"));
            }
            for (const db::ConstraintMeta& constraint : relation.constraints) {
                if (ImGui::MenuItem(constraint.name.c_str())) {
                    confirm_ddl(
                        TRF("Drop constraint %s", constraint.name.c_str()),
                        db::generate_drop_constraint(schema.name, relation.name,
                                                     constraint.name,
                                                     constraint.kind),
                        schema.name, relation.name);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(TR("Drop foreign key"))) {
            if (!relation.keys_loaded) {
                ImGui::TextColored(col4(colors().text_dim),
                                   TR("expand Foreign keys first"));
            }
            for (const db::ForeignKeyMeta& key : relation.foreign_keys) {
                if (ImGui::MenuItem(key.name.c_str())) {
                    confirm_ddl(TRF("Drop foreign key %s", key.name.c_str()),
                                db::generate_drop_foreign_key(schema.name,
                                                              relation.name,
                                                              key.name),
                                schema.name, relation.name);
                }
            }
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(TR("Rename table..."))) {
            open_rename_table(schema.name, relation);
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(TR("Drop table..."), nullptr, false, can_alter)) {
        confirm_ddl(TRF("Drop %s", relation.name.c_str()),
                    db::generate_drop(schema.name, relation.name, relation.kind),
                    schema.name, relation.name);
    }

    ImGui::Separator();

    if (ImGui::MenuItem(TR("Copy qualified name"))) {
        ImGui::SetClipboardText(full.c_str());
    }
    if (ImGui::MenuItem(TR("Copy name"))) {
        ImGui::SetClipboardText(relation.name.c_str());
    }

    ImGui::Separator();

    // Atualizar: descarta o cache do no' para que a proxima expansao releia
    // o catalogo. Sem isso, um ALTER TABLE feito fora do C-Otter ficaria
    // invisivel ate' reconectar.
    if (ImGui::MenuItem(TR("Refresh"), "F5")) {
        session().invalidate_table(schema.name, relation.name);
    }

    ImGui::EndPopup();
}

void MainShell::draw_view_definition(const db::SchemaMeta& schema,
                                     const db::TableMeta& view) {
    const Palette& p = colors();

    // `false` no lugar de `loaded`: o corpo nao tem contagem para mostrar, e
    // "(1)" ao lado de "Definição" nao diria nada.
    if (!draw_folder_node(Icon::view, TR("Definition"), 0, false)) return;

    if (!view.definition_loaded && !session().busy()) {
        session().load_view_definition_async(schema.name, view.name);
    }

    if (view.definition.empty()) {
        ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
        ImGui::TreePop();
        return;
    }

    draw_sql_body("##viewdef", view.definition);
    ImGui::TreePop();
}

void MainShell::draw_sql_body(const char* id, const std::string& sql) {
    const Palette& p = colors();

    // Altura limitada: uma view de relatorio ou uma funcao plpgsql tem dezenas
    // de linhas e empurraria o resto da arvore para fora da tela. Rolagem
    // horizontal em vez de quebra de linha -- SQL indentado perde a estrutura
    // quando quebrado.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(p.bg_darkest));
    if (ImGui::BeginChild(id, ImVec2(0.0f, ImGui::GetFontSize() * 9.0f),
                          ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(p.syntax_string));
        ImGui::TextUnformatted(sql.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::PushID(id);
    if (icon_text_button("##copy", Icon::copy, TR("Copy"),
                         TR("Copy the definition to the clipboard"))) {
        ImGui::SetClipboardText(sql.c_str());
    }
    ImGui::SameLine();
    if (icon_text_button("##open", Icon::open, TR("Open in editor"),
                         TR("Open the definition in a new SQL tab"))) {
        // Abre como script: olhar a definicao quase sempre precede escrever
        // algo em cima dela.
        new_document().editor().SetText(sql);
    }
    ImGui::PopID();
}

void MainShell::draw_sequences_folder(const db::SchemaMeta& schema) {
    if (!draw_folder_node(Icon::sequence, TR("Sequences"), schema.sequences.size(),
                          schema.sequences_loaded)) {
        return;
    }

    if (!schema.sequences_loaded && !session().busy()) {
        session().load_sequences_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::SequenceMeta& sequence : schema.sequences) {
        ImGui::BeginGroup();
        icon_inline(Icon::sequence, p.text_dim);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.text), "%s", sequence.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "= %lld",
                           static_cast<long long>(sequence.last_value));
        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            std::string tip = "start " + std::to_string(sequence.start_value) +
                              ", increment " + std::to_string(sequence.increment);
            if (!sequence.owned_by.empty()) tip += "\nowned by " + sequence.owned_by;
            if (!sequence.comment.empty())  tip += "\n\n" + sequence.comment;
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    ImGui::TreePop();
}

void MainShell::draw_routines_folder(const db::SchemaMeta& schema) {
    if (!draw_folder_node(Icon::function, TR("Functions"), schema.routines.size(),
                          schema.routines_loaded)) {
        return;
    }

    if (!schema.routines_loaded && !session().busy()) {
        session().load_routines_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::RoutineMeta& routine : schema.routines) {
        const bool is_procedure = routine.kind == db::ObjKind::procedure;

        // O id do ImGui precisa da assinatura, nao so' do nome: sobrecargas
        // compartilham o nome, e duas linhas com o mesmo id fariam a segunda
        // abrir junto com a primeira.
        ImGui::PushID((routine.name + "(" + routine.arguments + ")").c_str());

        ImGui::BeginGroup();
        icon_inline(is_procedure ? Icon::procedure : Icon::function,
                    is_procedure ? p.data : p.text_dim);
        ImGui::SameLine(0.0f, 4.0f);

        ImGui::PushStyleColor(ImGuiCol_Text, col(p.text));
        const bool open = ImGui::TreeNode(routine.name.c_str());
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "(%s)", routine.arguments.c_str());

        // O retorno distingue funcao de procedure de relance, sem tooltip.
        if (!is_procedure && !routine.return_type.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "→ %s", routine.return_type.c_str());
        }
        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            std::string tip = routine.name + "(" + routine.arguments + ")";
            if (!is_procedure) tip += "\n  returns " + routine.return_type;
            tip += "\n  language " + routine.language;
            if (!routine.comment.empty()) tip += "\n\n" + routine.comment;
            ImGui::SetTooltip("%s", tip.c_str());
        }

        if (open) {
            if (!routine.definition_loaded && !session().busy()) {
                session().load_routine_definition_async(
                    schema.name, routine.name, routine.arguments);
            }

            if (routine.definition.empty()) {
                ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
            } else {
                draw_sql_body("##routinedef", routine.definition);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void MainShell::draw_events_folder(const db::SchemaMeta& schema) {
    if (!draw_folder_node(Icon::event, TR("Events"), schema.events.size(),
                          schema.events_loaded)) {
        return;
    }

    if (!schema.events_loaded && !session().busy()) {
        session().load_events_async(schema.name);
    }

    const Palette& p = colors();

    if (schema.events_loaded && schema.events.empty()) {
        // Lista vazia é ambígua no MySQL: pode não haver evento, ou o
        // scheduler pode estar desligado. Dizer as duas possibilidades evita
        // que o usuário conclua a errada.
        ImGui::TextColored(col4(p.text_dim), TR("no events (or the scheduler "
                                                "is off)"));
    }

    for (const db::EventMeta& event : schema.events) {
        ImGui::BeginGroup();

        // Evento desabilitado sai esmaecido: ele EXISTE, mas não vai rodar --
        // e essa é a informação que mais importa ao olhar a lista.
        const bool enabled = event.status == "ENABLED";

        icon_inline(Icon::event, enabled ? p.text_dim : p.error);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(enabled ? p.text : p.text_dim), "%s",
                           event.name.c_str());

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s%s", event.schedule.c_str(),
                           enabled ? "" : "  [off]");
        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\n%s\n\n%s",
                              event.definer.c_str(),
                              event.last_executed.empty()
                                  ? TR("never executed")
                                  : event.last_executed.c_str(),
                              event.definition.c_str());
        }
    }
    ImGui::TreePop();
}

void MainShell::draw_types_folder(const db::SchemaMeta& schema) {
    // Escondida ate' saber que ha' tipos: a maioria dos schemas nao define
    // nenhum, e um no "Tipos (0)" fixo seria ruido em toda arvore.
    if (schema.types_loaded && schema.types.empty()) return;

    if (!draw_folder_node(Icon::data_type, TR("Data types"), schema.types.size(),
                          schema.types_loaded)) {
        return;
    }

    if (!schema.types_loaded && !session().busy()) {
        session().load_types_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::DataTypeMeta& type : schema.types) {
        ImGui::PushID(type.name.c_str());

        // O grupo faz o tooltip valer para a LINHA inteira.
        //
        // IsItemHovered() sozinho testa apenas o ultimo item desenhado -- que
        // aqui e' o tipo base a' direita, nao o nome. Sem o grupo, o tooltip
        // do domain so' aparecia sobre aquele pedaco do texto.
        ImGui::BeginGroup();

        icon_inline(Icon::data_type, p.data_light);
        ImGui::SameLine(0.0f, 4.0f);

        // Enum e composto tem filhos para mostrar; domain e range cabem
        // inteiros no rotulo e no tooltip, entao nao viram no' expansivel.
        bool open = false;
        if (type.has_children()) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(p.text));
            open = ImGui::TreeNode(type.name.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextColored(col4(p.text), "%s", type.name.c_str());
        }

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s",
                           std::string(db::to_string(type.kind)).c_str());

        // O detalhe que cabe numa linha vai ao lado; o resto, no tooltip.
        if (!type.base_type.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.syntax_keyword), "  %s",
                               type.base_type.c_str());
        } else if (!type.subtype.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.syntax_keyword), "  of %s",
                               type.subtype.c_str());
        }

        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            std::string tip = type.name + " (" +
                              std::string(db::to_string(type.kind)) + ")";
            if (!type.base_type.empty()) {
                tip += "\n  " + type.base_type;
                if (type.not_null) tip += " NOT NULL";
                if (!type.default_value.empty()) {
                    tip += " DEFAULT " + type.default_value;
                }
            }
            if (!type.check_constraint.empty()) {
                tip += "\n  " + type.check_constraint;
            }
            if (!type.subtype.empty()) tip += "\n  subtype " + type.subtype;
            if (!type.owner.empty())   tip += "\n  owner " + type.owner;
            if (!type.comment.empty()) tip += "\n\n" + type.comment;
            ImGui::SetTooltip("%s", tip.c_str());
        }

        if (open) {
            // Enum: a ordem e' a de enumsortorder, que define a comparacao
            // entre valores. O indice a esquerda deixa isso explicito.
            for (std::size_t i = 0; i < type.enum_values.size(); ++i) {
                ImGui::TextColored(col4(p.text_dim), "  %zu", i + 1);
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextColored(col4(p.syntax_string), "%s",
                                   type.enum_values[i].c_str());
            }

            for (const db::TypeAttributeMeta& attribute : type.attributes) {
                icon_inline(Icon::column, p.text_dim);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(col4(p.text), "%s", attribute.name.c_str());
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim), "%s%s",
                                   attribute.type_name.c_str(),
                                   attribute.nullable ? "" : "  NOT NULL");
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void MainShell::draw_toolbar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kToolbarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(colors().bg_darkest));

    if (ImGui::Begin("##toolbar", nullptr, flags)) {
        const Palette& p = colors();

        const bool connected = session().state() == SessionState::connected;
        const bool busy      = session().busy();
        const bool can_act   = connected && !busy;

        // Separador vertical fino entre grupos de ações.
        auto group_separator = [&p] {
            ImGui::SameLine(0.0f, 6.0f);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float h = toolbar_button_size();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(pos.x, pos.y + h * 0.22f),
                ImVec2(pos.x, pos.y + h * 0.78f),
                with_alpha(p.bg_light, 0.65f), 1.0f);
            ImGui::SameLine(0.0f, 7.0f);
        };

        // --- Conexão ---------------------------------------------------------
        if (icon_button("##connect", Icon::connect,
                        TR("New connection (Ctrl+Shift+N)"))) {
            connection_dialog_.open_new();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##disconnect", Icon::disconnect, TR("Disconnect"),
                        connected)) {
            session().disconnect();
        }

        group_separator();

        // --- Execução --------------------------------------------------------
        if (icon_button("##execute", Icon::play, TR("Execute (Ctrl+Enter)"),
                        can_act, can_act ? p.ok : 0)) {
            execute_current_sql();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##cancel", Icon::stop, TR("Cancel query"), busy,
                        busy ? p.error : 0)) {
            // Cancelamento entra quando a Session expuser cancel_async().
        }

        group_separator();

        // --- Transações ------------------------------------------------------
        //
        // A razão de a barra e as transações virem juntas: commit e rollback
        // precisam de um lugar visível e permanente.
        const bool auto_commit    = session().auto_commit();
        const db::TxnState txn    = session().txn_state();
        const std::size_t pending = session().uncommitted_changes();
        const bool in_txn         = txn != db::TxnState::idle;

        // Auto-commit como botão de alternância, tingido quando ligado.
        if (icon_button("##autocommit", Icon::refresh,
                        auto_commit ? TR("Auto-commit: on") : TR("Auto-commit: off"),
                        can_act, auto_commit ? p.data_light : p.text_dim)) {
            session().set_auto_commit_async(!auto_commit);
        }

        ImGui::SameLine(0.0f, 2.0f);
        const bool can_txn = can_act && !auto_commit && in_txn;

        if (icon_button("##commit", Icon::commit, TR("Commit (Ctrl+Shift+C)"),
                        can_txn, pending > 0 ? p.ok : 0)) {
            session().commit_async();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##rollback", Icon::rollback, TR("Rollback (Ctrl+Shift+R)"),
                        can_txn, pending > 0 ? p.error : 0)) {
            session().rollback_async();
        }

        // --- Indicador de estado da transação --------------------------------
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::AlignTextToFramePadding();

        if (!connected) {
            ImGui::TextColored(col4(p.text_dim), "—");
        } else if (auto_commit) {
            ImGui::TextColored(col4(p.text_dim), "%s", TR("auto-commit"));
        } else {
            // Cores do monitor de transação do DBeaver: verde parado, âmbar
            // com trabalho pendente, vermelho abortada.
            const std::uint32_t color =
                txn == db::TxnState::failed ? p.error
                : pending > 0               ? p.warn
                                            : p.ok;

            const char* label =
                txn == db::TxnState::failed ? TR("transaction aborted")
                : pending > 0               ? TR("uncommitted changes")
                                            : TR("transaction open");

            // Ponto com glow: o estado da transação merece destaque.
            const ImVec2 dot = ImGui::GetCursorScreenPos();
            const ImVec2 dot_center(dot.x + 5.0f,
                                    dot.y + ImGui::GetFontSize() * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (p.glow_strength > 0.0f) {
                for (int i = 3; i > 0; --i) {
                    const float t = static_cast<float>(i) / 3.0f;
                    dl->AddCircleFilled(
                        dot_center, 4.0f + t * 5.0f,
                        with_alpha(color, p.glow_strength * 0.16f * (1.0f - t)),
                        16);
                }
            }
            dl->AddCircleFilled(dot_center, 4.0f, color, 16);

            ImGui::Dummy(ImVec2(14.0f, ImGui::GetFontSize()));
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(col4(color), "%s", label);

            if (pending > 0) {
                ImGui::SameLine(0.0f, 5.0f);
                ImGui::TextColored(col4(p.text_dim), "(%zu)", pending);
            }
        }

        // --- Contexto, alinhado à direita ------------------------------------
        if (connected) {
            char context[160];
            std::snprintf(context, sizeof(context), "%s  ·  %s",
                          active_profile_.effective_name().c_str(),
                          db::connection_type_info(active_profile_.type).name);

            const float width = ImGui::CalcTextSize(context).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - width - 16.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(
                col4(db::connection_type_info(active_profile_.type).color),
                "%s", context);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_document_tabs() {
    constexpr ImGuiTabBarFlags flags =
        ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
        ImGuiTabBarFlags_FittingPolicyScroll |
        ImGuiTabBarFlags_TabListPopupButton;

    if (!ImGui::BeginTabBar("##doctabs", flags)) return;

    // Botão "+" ao lado das abas, como em navegadores.
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                      ImGuiTabItemFlags_NoTooltip)) {
        new_document();
    }

    std::optional<std::size_t> to_close;
    std::optional<std::size_t> to_close_others;

    for (std::size_t i = 0; i < documents_.size(); ++i) {
        SqlDocument& document = *documents_[i];
        ImGui::PushID(static_cast<int>(document.id()));

        // O sufixo ###id mantém a identidade da aba mesmo quando o título
        // muda (ao salvar com outro nome, por exemplo).
        const std::string label =
            document.title() + (document.modified() ? " *" : "") +
            "###doc" + std::to_string(document.id());

        ImGuiTabItemFlags item_flags = ImGuiTabItemFlags_None;
        if (document.pinned()) item_flags |= ImGuiTabItemFlags_Leading;
        if (document.modified()) item_flags |= ImGuiTabItemFlags_UnsavedDocument;

        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, item_flags)) {
            active_document_ = i;
            draw_document_body(document);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginPopupContextItem("##tabmenu")) {
            if (ImGui::MenuItem(TR("Close"), "Ctrl+W")) to_close = i;
            if (ImGui::MenuItem(TR("Close others"))) to_close_others = i;
            ImGui::Separator();

            bool pinned = document.pinned();
            if (ImGui::MenuItem(TR("Pin tab"), nullptr, &pinned)) {
                document.set_pinned(pinned);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(TR("Copy SQL"))) {
                ImGui::SetClipboardText(document.editor().GetText().c_str());
            }
            ImGui::EndPopup();
        }

        if (!open) to_close = i;
        ImGui::PopID();
    }

    ImGui::EndTabBar();

    // Aplicado fora do laço: remover do vetor durante a iteração invalidaria
    // as referências em uso.
    if (to_close_others) close_others(*to_close_others);
    if (to_close)        close_document(*to_close);
}

void MainShell::draw_document_body(SqlDocument& document) {
    const bool can_run = session().state() == SessionState::connected &&
                         !session().busy();

    ImGui::BeginDisabled(!can_run);
    if (ImGui::Button(TR("Execute  (Ctrl+Enter)"))) execute_current_sql();
    ImGui::EndDisabled();

    ImGui::SameLine();
    const TextEditor::DocPos cursor =
        document.editor().GetCurrentCursorPosition();

    ImGui::TextColored(col4(colors().text_dim),
                       TR("  Ln %zu, Col %zu  |  %zu lines%s"),
                       cursor.line + 1, cursor.index + 1,
                       document.editor().GetLineCount(),
                       document.modified() ? "  ●" : "");

    if (document.executing()) {
        ImGui::SameLine();
        draw_busy_indicator();
    }

    ImGui::Separator();
    document.editor().Render("##sql", ImGui::GetContentRegionAvail());
}

void MainShell::draw_editor_panel() {
    if (ImGui::Begin(TRW("SQL", "###SqlPanel"))) {
        draw_document_tabs();
    }
    ImGui::End();
}

void MainShell::draw_grid_cell(SqlDocument& document, const db::ResultSet& rs,
                               std::size_t row, std::size_t column) {
    const Palette& p = colors();

    const bool editing = editing_active_ &&
                         editing_document_ == document.id() &&
                         editing_row_ == row && editing_column_ == column;

    // --- Celula em edicao ---------------------------------------------------
    if (editing) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetKeyboardFocusHere();

        const bool committed = ImGui::InputText(
            "##celledit", edit_buffer_, sizeof edit_buffer_,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);

        // Enter grava no buffer; Esc descarta. Nenhum dos dois toca o banco --
        // a gravacao e' explicita (ADR 0014).
        if (committed) {
            document.edits().set(row, column, edit_buffer_);
            editing_active_ = false;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            editing_active_ = false;
        } else if (!ImGui::IsItemActive() && ImGui::IsItemDeactivated()) {
            // Clicou fora: guarda o que foi digitado, em vez de perder.
            document.edits().set(row, column, edit_buffer_);
            editing_active_ = false;
        }
        return;
    }

    // --- Valor: o do buffer tem precedencia sobre o do banco ----------------
    const db::CellEdit* edit = document.edits().find(row, column);

    const bool is_null = edit != nullptr ? edit->is_null : rs.is_null(row, column);
    const std::string_view value =
        edit != nullptr ? std::string_view(edit->value) : rs.text(row, column);

    // Fundo distinto para celula alterada: saber o que mudou ANTES de gravar
    // e' o que torna a edicao em buffer util.
    const bool row_deleted = document.edits().is_deleted(row);

    // Cor condicional (ADR 0005). Vem ANTES das marcas de edição, que a
    // sobrepõem: o estado pendente do usuário é mais urgente que uma regra de
    // cor -- esconder "esta célula foi alterada" atrás de um mapa de calor
    // faria o usuário gravar sem saber o que ia gravar.
    db::CellColor conditional;
    if (!document.color_rules().empty()) {
        conditional = document.color_rules().color_for(rs, row, column);
        if (conditional.background != 0) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                   conditional.background);
        }
    }

    if (row_deleted) {
        // Linha inteira em vermelho apagado: marcada para exclusao, ainda
        // nao excluida.
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               with_alpha(p.error, 0.20f));
    } else if (edit != nullptr) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               with_alpha(p.warn, 0.22f));
    }

    // Guarda o inicio da celula: o alvo clicavel volta para ca' e cobre a
    // largura toda, incluindo a area do texto.
    const ImVec2 cell_origin = ImGui::GetCursorPos();
    const float  cell_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

    if (row_deleted) {
        // Sem editor: nao faz sentido alterar o que sera' excluido.
        ImGui::TextColored(col4(p.text_dim), "%s",
                           is_null ? "[null]" : std::string(value).c_str());
    } else if (is_null) {
        ImGui::TextColored(col4(p.text_dim), "[null]");
    } else {
        const db::DataKind kind = rs.column(column).info().kind;
        if (db::is_right_aligned(kind)) {
            const float width = ImGui::CalcTextSize(
                value.data(), value.data() + value.size()).x;
            const float available = ImGui::GetContentRegionAvail().x;
            if (available > width) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
            }
        }

        if (conditional.foreground != 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, conditional.foreground);
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(value.data(), value.data() + value.size());
        }
    }

    // --- Interacao -----------------------------------------------------------
    if (!document.edit_target().editable()) return;

    // Alvo clicavel cobrindo a celula inteira.
    //
    // Dummy NAO serve aqui: ele reserva espaco mas nao e' item interativo,
    // entao IsItemHovered() sempre respondia falso e o duplo clique nunca
    // chegava. InvisibleButton e' item de verdade.
    //
    // Desenhado POR CIMA do texto (cursor recuado), nao ao lado: ao lado, a
    // celula com valor curto teria alvo so' na sobra.
    ImGui::SetCursorPos(cell_origin);
    ImGui::InvisibleButton("##cellhit",
                           ImVec2(cell_width, ImGui::GetTextLineHeight()),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight);

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !row_deleted) {
            editing_active_   = true;
            editing_document_ = document.id();
            editing_row_      = row;
            editing_column_   = column;
            std::snprintf(edit_buffer_, sizeof edit_buffer_, "%s",
                          is_null ? "" : std::string(value).c_str());
        }

        // O valor original no tooltip: poder comparar sem desfazer.
        if (edit != nullptr) {
            const std::string original =
                rs.is_null(row, column) ? "[null]"
                                        : std::string(rs.text(row, column));
            ImGui::SetTooltip(TR("was: %s"), original.c_str());
        }
    }

    ImGui::PushID(static_cast<int>(row * rs.column_count() + column));
    if (ImGui::BeginPopupContextItem("##cellmenu")) {
        if (ImGui::MenuItem(TR("Set NULL"))) {
            // Botao proprio porque digitar nada significa string vazia, nao
            // NULL -- sao valores diferentes no banco.
            document.edits().set_null(row, column);
        }
        if (ImGui::MenuItem(TR("Revert cell"), nullptr, false, edit != nullptr)) {
            document.edits().revert(row, column);
        }
        ImGui::Separator();
        if (ImGui::MenuItem(TR("View value..."))) {
            open_value_panel(document, rs, row, column);
        }
        if (ImGui::MenuItem(TR("Copy value"))) {
            ImGui::SetClipboardText(is_null ? "" : std::string(value).c_str());
        }

        ImGui::Separator();

        const bool deleted = document.edits().is_deleted(row);
        if (ImGui::MenuItem(deleted ? TR("Undo delete") : TR("Delete row"))) {
            if (deleted) document.edits().unmark_deleted(row);
            else         document.edits().mark_deleted(row);
        }
        if (ImGui::MenuItem(TR("New row"))) {
            document.edits().add_row();
        }

        // Duplicar: linha nova com os valores DESTA, exceto a chave primária.
        //
        // Copiar a PK junto produziria um INSERT que viola a unicidade -- e o
        // erro viria do servidor, depois de o usuário já ter preenchido o
        // resto. Deixar a chave em branco é o que torna a duplicação útil numa
        // tabela com id auto-gerado, que é o caso comum.
        const bool duplicated = ImGui::MenuItem(TR("Duplicate row"));

        // A duplicação copia só o que está NA TELA. Uma coluna NOT NULL sem
        // default que ficou fora do SELECT faz o INSERT ser recusado -- com
        // uma mensagem do servidor que nomeia a coluna, mas só depois de
        // clicar em salvar. Dizer antes economiza a viagem.
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s",
                              TR("copies the visible columns, except the key; "
                                 "a NOT NULL column left out of the query will "
                                 "be refused"));
        }

        if (duplicated) {
            const std::size_t index = document.edits().add_row();

            const auto& keys = document.edit_target().key_columns;

            for (std::size_t c = 0; c < rs.column_count(); ++c) {
                // A chave fica vazia. A fonte é `key_columns` do alvo de
                // edição -- índices no ResultSet --, e não o `primary_key` da
                // coluna: o resultado pode vir de um SELECT que não trouxe
                // essa informação.
                if (std::find(keys.begin(), keys.end(), c) != keys.end()) {
                    continue;
                }

                // O valor copiado é o do BUFFER quando há edição pendente:
                // duplicar deve copiar o que está na tela, não o que está no
                // banco.
                const db::CellEdit* pending = document.edits().find(row, c);

                if (pending != nullptr) {
                    if (pending->is_null) document.edits().set_new_null(index, c);
                    else document.edits().set_new_value(index, c, pending->value);
                } else if (rs.is_null(row, c)) {
                    document.edits().set_new_null(index, c);
                } else {
                    document.edits().set_new_value(index, c,
                                                   std::string(rs.text(row, c)));
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// Prepara o painel de valor para uma célula.
//
// O conteúdo é FORMATADO aqui, uma vez, e não a cada quadro: indentar um JSON
// de 4 KB ou montar o hexadecimal de 64 KB sessenta vezes por segundo seria
// desperdício puro.
void MainShell::open_value_panel(SqlDocument& document, const db::ResultSet& rs,
                                 std::size_t row, std::size_t column) {
    value_panel_ = ValuePanel{};
    value_panel_.open   = true;
    value_panel_.column = rs.column(column).info().name;

    // O valor do BUFFER tem precedência: o painel mostra o que está na tela,
    // não o que está no banco.
    const db::CellEdit* pending = document.edits().find(row, column);

    const bool is_null = pending != nullptr ? pending->is_null
                                            : rs.is_null(row, column);
    if (is_null) {
        value_panel_.view = db::ValueView::plain;
        value_panel_.text = "[null]";
        value_panel_.size = 0;
        return;
    }

    const std::string_view value =
        pending != nullptr ? std::string_view(pending->value)
                           : rs.text(row, column);

    value_panel_.size = value.size();
    value_panel_.view = db::choose_view(rs.column(column).info().kind, value);

    switch (value_panel_.view) {
        case db::ValueView::json:
            value_panel_.text = db::format_json(value);
            break;

        case db::ValueView::binary:
            value_panel_.text = db::format_hex(
                {reinterpret_cast<const std::byte*>(value.data()), value.size()});
            break;

        case db::ValueView::boolean:
            value_panel_.text = db::is_true(value) ? "true" : "false";
            break;

        default:
            value_panel_.text = std::string(value);
            break;
    }
}

void MainShell::draw_value_panel() {
    if (!value_panel_.open) return;

    const Palette& p = colors();
    ImGui::SetNextWindowSize(ImVec2(640, 460), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(TRW("Value", "###ValuePanel"), &value_panel_.open,
                     ImGuiWindowFlags_NoDocking)) {

        ImGui::TextColored(col4(p.accent_light), "%s",
                           value_panel_.column.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s  ·  %zu bytes",
                           TR(std::string(db::to_string(value_panel_.view)).c_str()),
                           value_panel_.size);

        ImGui::Separator();

        const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;

        // Booleano ganha um controle próprio em vez de texto: é a diferença
        // entre ver "1" e ver uma caixa marcada.
        if (value_panel_.view == db::ValueView::boolean) {
            bool checked = value_panel_.text == "true";

            // Somente leitura: o painel MOSTRA. Editar continua sendo pelo
            // duplo clique na célula, que é onde o buffer de edição registra.
            ImGui::BeginDisabled();
            ImGui::Checkbox(value_panel_.text.c_str(), &checked);
            ImGui::EndDisabled();
        } else {
            // Fonte monoespaçada já é a do projeto inteiro, e é o que alinha
            // as colunas do hexadecimal.
            ImGui::BeginChild("##valuebody", ImVec2(0, -footer),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(value_panel_.text.c_str());
            ImGui::EndChild();
        }

        if (ImGui::Button(TR("Copy"), ImVec2(120, 0))) {
            ImGui::SetClipboardText(value_panel_.text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Close"), ImVec2(120, 0))) value_panel_.open = false;
    }
    ImGui::End();
}

void MainShell::recompute_groups(SqlDocument& document) {
    if (!document.result().has_value()) {
        document.set_groups({});
        return;
    }

    // `paged()` e' o que diz se o ResultSet cobre o resultado inteiro. Essa
    // informacao vira a marca `partial` e acompanha os numeros ate' a tela.
    document.set_groups(db::group_and_aggregate(
        *document.result(), document.group_spec(), document.paged()));
}

void MainShell::recompute_pivot(SqlDocument& document) {
    if (!document.result().has_value() || !document.pivot_active()) {
        document.pivot_result() = {};
        return;
    }

    // `paged()` diz se o ResultSet cobre o resultado inteiro -- e vira a marca
    // `partial`, que acompanha os números até a tela. Pivotar 200 linhas de 2
    // milhões e apresentar como o resultado seria a mesma mentira da
    // agregação.
    document.pivot_result() = db::pivot(*document.result(),
                                        document.pivot_spec(),
                                        document.paged());
}

// A tabela pivotada, no lugar da grade.
void MainShell::draw_pivot_table(SqlDocument& document, const db::ResultSet& rs) {
    const Palette& p = colors();
    const db::PivotResult& pivot = document.pivot_result();
    const db::PivotSpec& spec = document.pivot_spec();

    // --- Cabeçalho da barra ---------------------------------------------------

    icon_inline(Icon::pivot, p.accent_light);
    ImGui::SameLine(0.0f, 6.0f);

    const auto column_name = [&rs](std::size_t index) {
        return index < rs.column_count() ? rs.column(index).info().name
                                         : std::string{"?"};
    };

    ImGui::TextColored(col4(p.text_dim), TR("Pivot: %s by %s, %s of %s"),
                       spec.rows.empty() ? "?" : column_name(spec.rows[0]).c_str(),
                       column_name(spec.column).c_str(),
                       TR(std::string(db::to_string(spec.function)).c_str()),
                       column_name(spec.value).c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Clear pivot"))) {
        document.set_pivot_active(false);
        document.pivot_result() = {};
        return;
    }

    // O aviso de parcialidade. Sem ele, a célula que soma 200 de 2 milhões
    // seria lida como a soma do resultado -- mentira (ADR 0005).
    if (pivot.partial) {
        ImGui::SameLine();
        icon_inline(Icon::warning, p.warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.warn), TR("over this page only (%zu rows)"),
                           pivot.rows_covered);

        ImGui::SameLine();
        const bool can_run = session().state() == SessionState::connected &&
                             !session().busy();
        if (ImGui::SmallButton(TR("Compute on the server")) && can_run) {
            const std::string sql = db::build_pivot_query(
                document.paged_sql(), rs, spec, pivot.headers);
            if (!sql.empty()) open_sql_tab(sql, /*run=*/true);
        }
    }

    // Truncamento nunca é silencioso: o usuário concluiria que os dados não
    // existem.
    if (pivot.truncated) {
        icon_inline(Icon::warning, p.error);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.error),
                           TR("showing %zu of %zu distinct values; group the "
                              "data before pivoting"),
                           pivot.headers.size(), pivot.distinct_values);
    }

    ImGui::Separator();

    if (pivot.rows.empty()) {
        ImGui::TextColored(col4(p.text_dim), TR("nothing to pivot"));
        return;
    }

    // --- A tabela --------------------------------------------------------------

    // +1 pela coluna de totais, e o teto do ImGui continua sendo 64.
    const auto columns = static_cast<int>(
        std::min<std::size_t>(spec.rows.size() + pivot.headers.size(), 64));

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit;

    if (!ImGui::BeginTable("##pivot", columns, flags)) return;

    for (const std::size_t index : spec.rows) {
        ImGui::TableSetupColumn(column_name(index).c_str());
    }
    for (const std::string& header : pivot.headers) {
        ImGui::TableSetupColumn(header.c_str());
    }
    ImGui::TableSetupScrollFreeze(static_cast<int>(spec.rows.size()), 1);
    ImGui::TableHeadersRow();

    for (const db::PivotRow& row : pivot.rows) {
        ImGui::TableNextRow();

        for (std::size_t i = 0; i < spec.rows.size(); ++i) {
            ImGui::TableSetColumnIndex(static_cast<int>(i));
            ImGui::TextColored(col4(p.accent_light), "%s",
                               i < row.keys.size() ? row.keys[i].c_str() : "");
        }

        for (std::size_t i = 0; i < pivot.headers.size(); ++i) {
            const int index = static_cast<int>(spec.rows.size() + i);
            if (index >= columns) break;

            ImGui::TableSetColumnIndex(index);
            if (i >= row.cells.size()) continue;

            const db::AggregateValue& cell = row.cells[i];

            // Célula ausente sai esmaecida: distinguir "não houve" de um
            // valor é o ponto de não mostrar zero ali.
            if (cell.text == "-") {
                ImGui::TextColored(col4(p.text_dim), "-");
            } else {
                const float width = ImGui::CalcTextSize(cell.text.c_str()).x;
                const float available = ImGui::GetContentRegionAvail().x;
                if (available > width) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                         available - width);
                }
                ImGui::TextUnformatted(cell.text.c_str());
            }
        }
    }

    // Linha de totais, no fim e destacada.
    if (!pivot.totals.empty()) {
        ImGui::TableNextRow();
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                               with_alpha(p.accent, 0.12f));

        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(col4(p.text_bright), TR("Total"));

        for (std::size_t i = 0; i < pivot.totals.size(); ++i) {
            const int index = static_cast<int>(spec.rows.size() + i);
            if (index >= columns) break;

            ImGui::TableSetColumnIndex(index);
            ImGui::TextColored(col4(p.text_bright), "%s",
                               pivot.totals[i].text.c_str());
        }
    }
    ImGui::EndTable();
}

void MainShell::draw_group_bar(SqlDocument& document, const db::ResultSet& rs) {
    const Palette& p = colors();
    const db::GroupSpec& spec = document.group_spec();

    if (spec.group_by.empty() && spec.aggregates.empty()) return;

    // Colunas de agrupamento, cada uma removivel.
    if (!spec.group_by.empty()) {
        ImGui::TextColored(col4(p.text_dim), TR("Grouped by"));

        for (std::size_t i = 0; i < spec.group_by.size(); ++i) {
            const std::size_t column = spec.group_by[i];
            if (column >= rs.column_count()) continue;

            ImGui::SameLine();
            ImGui::PushID(static_cast<int>(i));

            if (ImGui::SmallButton(
                    (rs.column(column).info().name + " x").c_str())) {
                document.group_spec().group_by.erase(
                    document.group_spec().group_by.begin() +
                    static_cast<std::ptrdiff_t>(i));
                recompute_groups(document);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    }

    // O aviso de parcialidade. Sem ele, "soma 650" de uma pagina de 200
    // linhas seria lido como a soma do resultado inteiro -- mentira.
    if (document.groups().partial &&
        (!spec.aggregates.empty() || !spec.group_by.empty())) {
        ImGui::SameLine();
        icon_inline(Icon::warning, p.warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.warn),
                           TR("over this page only (%zu rows)"),
                           document.groups().rows_covered);

        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Compute on the server"))) {
            const std::string sql = db::build_group_query(
                document.paged_sql(), rs, document.group_spec());
            if (!sql.empty()) {
                // Abre numa aba nova: o resultado agrupado tem outras colunas
                // e substituir o atual perderia o que o usuario estava vendo.
                open_sql_tab(sql, /*run=*/true);
            }
        }
    }
}

void MainShell::draw_group_panel(SqlDocument& document,
                                 const db::ResultSet& rs) {
    const Palette& p = colors();
    const db::GroupResult& groups = document.groups();

    if (groups.groups.empty()) return;

    const db::GroupSpec& spec = document.group_spec();
    const int columns = static_cast<int>(spec.group_by.size() +
                                         spec.aggregates.size());
    if (columns == 0) return;

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("##groups", columns, flags,
                          ImVec2(0, ImGui::GetFontSize() * 12.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);

        for (const std::size_t column : spec.group_by) {
            ImGui::TableSetupColumn(column < rs.column_count()
                                        ? rs.column(column).info().name.c_str()
                                        : "?");
        }
        for (const db::AggregateSpec& aggregate : spec.aggregates) {
            const std::string label =
                std::string(db::to_string(aggregate.function)) + "(" +
                (aggregate.column < rs.column_count()
                     ? rs.column(aggregate.column).info().name
                     : "?") + ")";
            ImGui::TableSetupColumn(label.c_str());
        }
        ImGui::TableHeadersRow();

        for (const db::Group& group : groups.groups) {
            ImGui::TableNextRow();

            for (std::size_t i = 0; i < group.key_values.size(); ++i) {
                ImGui::TableSetColumnIndex(static_cast<int>(i));
                ImGui::TextUnformatted(group.key_values[i].c_str());
            }
            for (std::size_t i = 0; i < group.aggregates.size(); ++i) {
                ImGui::TableSetColumnIndex(
                    static_cast<int>(group.key_values.size() + i));

                // Sem alinhamento a' direita aqui.
                //
                // Tentei SetCursorPosX com GetContentRegionAvail() e depois
                // com GetColumnWidth(): nos dois casos os subtotais sumiam da
                // tela. A grade principal usa o mesmo padrao e funciona, mas
                // a diferenca de contexto (tabela aninhada, SizingStretchProp)
                // muda o referencial do cursor.
                //
                // Numero desalinhado e' um defeito estetico; numero invisivel
                // e' um defeito funcional. Fico com o primeiro ate' entender
                // o referencial certo.
                const db::AggregateValue& value = group.aggregates[i];
                ImGui::TextUnformatted(value.text.c_str());
            }
        }

        // Linha de totais, destacada: e' o resumo de tudo, nao mais um grupo.
        if (!groups.totals.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                   with_alpha(p.accent, 0.25f));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(col4(p.text_bright), TR("Total"));

            for (std::size_t i = 0; i < groups.totals.size(); ++i) {
                ImGui::TableSetColumnIndex(
                    static_cast<int>(spec.group_by.size() + i));
                ImGui::TextColored(col4(p.text_bright), "%s",
                                   groups.totals[i].text.c_str());
            }
        }
        ImGui::EndTable();
    }
}

// Prepara o formulário de coluna nova. O DDL só é gerado ao confirmar, para
// que os avisos (NOT NULL sem DEFAULT numa tabela com linhas) reflitam o que
// o usuário acabou de digitar.
void MainShell::open_add_column(const std::string& schema,
                                const db::TableMeta& table) {
    column_form_ = ColumnForm{};
    column_form_.schema = schema;
    column_form_.table  = table.name;
    column_form_.open   = true;

    // Tipo padrão por SGBD: sugerir `serial` num MySQL daria erro de sintaxe.
    const bool mysql = db::sql_dialect() == db::QuoteStyle::backticks;
    std::snprintf(column_form_.type, sizeof column_form_.type, "%s",
                  mysql ? "varchar(100)" : "text");

    // A lista de colunas alimenta o AFTER do MySQL.
    column_form_.existing.clear();
    for (const db::ColumnMeta& column : table.columns) {
        column_form_.existing.push_back(column.name);
    }
    column_form_.current = table;
}

void MainShell::open_add_index(const std::string& schema,
                               const db::TableMeta& table) {
    index_form_ = IndexForm{};
    index_form_.schema = schema;
    index_form_.table  = table.name;
    index_form_.open   = true;

    // Nome sugerido no padrão do projeto: ix_<tabela>_. O usuário completa com
    // as colunas, que é a parte que ele acabou de escolher.
    std::snprintf(index_form_.name, sizeof index_form_.name, "ix_%s_",
                  table.name.c_str());

    for (const db::ColumnMeta& column : table.columns) {
        index_form_.columns.push_back({column.name, false});
    }
}

void MainShell::open_rename_table(const std::string& schema,
                                  const db::TableMeta& table) {
    rename_form_ = RenameForm{};
    rename_form_.schema = schema;
    rename_form_.table  = table.name;
    rename_form_.open   = true;
    std::snprintf(rename_form_.new_name, sizeof rename_form_.new_name, "%s",
                  table.name.c_str());
    rename_form_.current = table;
}

void MainShell::draw_ddl_forms() {
    const Palette& p = colors();

    // --- Coluna nova ---------------------------------------------------------

    if (column_form_.open) {
        ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
        if (ImGui::Begin(TRW("Add column", "###AddColumn"), &column_form_.open,
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextColored(col4(p.text_dim), "%s.%s",
                               column_form_.schema.c_str(),
                               column_form_.table.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("Name"), column_form_.name,
                             sizeof column_form_.name);
            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("Type"), column_form_.type,
                             sizeof column_form_.type);
            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("Default"), column_form_.default_value,
                             sizeof column_form_.default_value);
            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("Comment"), column_form_.comment,
                             sizeof column_form_.comment);

            ImGui::Checkbox(TR("Nullable"), &column_form_.nullable);

            // Posição só existe no MySQL. Esconder no PostgreSQL em vez de
            // desabilitar: um campo que nunca vai funcionar ali é ruído.
            if (db::sql_dialect() == db::QuoteStyle::backticks &&
                !column_form_.existing.empty()) {
                ImGui::Separator();
                ImGui::TextColored(col4(p.text_dim), TR("Position"));

                ImGui::RadioButton(TR("last"), &column_form_.position, 0);
                ImGui::SameLine();
                ImGui::RadioButton(TR("first"), &column_form_.position, 1);
                ImGui::SameLine();
                ImGui::RadioButton(TR("after"), &column_form_.position, 2);

                if (column_form_.position == 2) {
                    ImGui::SetNextItemWidth(260);
                    if (ImGui::BeginCombo(
                            "##after",
                            column_form_.after_index <
                                    static_cast<int>(column_form_.existing.size())
                                ? column_form_.existing[column_form_.after_index].c_str()
                                : "")) {
                        for (int i = 0;
                             i < static_cast<int>(column_form_.existing.size()); ++i) {
                            if (ImGui::Selectable(column_form_.existing[i].c_str(),
                                                  column_form_.after_index == i)) {
                                column_form_.after_index = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }

            ImGui::Separator();

            const bool valid = column_form_.name[0] != 0 &&
                               column_form_.type[0] != 0;

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("Review SQL"), ImVec2(140, 0))) {
                db::NewColumn column;
                column.name          = column_form_.name;
                column.type_name     = column_form_.type;
                column.nullable      = column_form_.nullable;
                column.default_value = column_form_.default_value;
                column.comment       = column_form_.comment;
                column.first         = column_form_.position == 1;
                if (column_form_.position == 2 &&
                    column_form_.after_index <
                        static_cast<int>(column_form_.existing.size())) {
                    column.after = column_form_.existing[column_form_.after_index];
                }

                db::TableAlteration wanted;
                wanted.schema = column_form_.schema;
                wanted.table  = column_form_.table;
                wanted.add_columns.push_back(std::move(column));

                confirm_ddl(TRF("Add column %s to %s", column_form_.name,
                                column_form_.table.c_str()),
                            db::generate_alter(column_form_.current, wanted),
                            column_form_.schema, column_form_.table);
                column_form_.open = false;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                column_form_.open = false;
            }

            if (!valid) {
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim), TR("(name and type)"));
            }
        }
        ImGui::End();
    }

    // --- Índice novo -----------------------------------------------------------

    if (index_form_.open) {
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::Begin(TRW("Add index", "###AddIndex"), &index_form_.open,
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextColored(col4(p.text_dim), "%s.%s",
                               index_form_.schema.c_str(),
                               index_form_.table.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("Name"), index_form_.name,
                             sizeof index_form_.name);

            ImGui::Checkbox(TR("Unique"), &index_form_.unique);

            // CONCURRENTLY só existe no PostgreSQL, e não roda dentro de
            // transação. Esconder no MySQL em vez de desabilitar.
            if (db::sql_dialect() != db::QuoteStyle::backticks) {
                ImGui::Checkbox(TR("Concurrently"), &index_form_.concurrently);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s",
                                      TR("does not block writes, but cannot run "
                                         "inside a transaction"));
                }
            }

            ImGui::Separator();
            ImGui::TextColored(col4(p.text_dim), TR("Columns"));

            // A ORDEM importa num índice composto: ela decide que consultas
            // ele atende. A lista segue a ordem da tabela, e quem quiser outra
            // ordem edita o SQL na janela de conferência.
            std::size_t picked = 0;
            for (auto& [name, selected] : index_form_.columns) {
                ImGui::Checkbox(name.c_str(), &selected);
                if (selected) ++picked;
            }

            ImGui::Separator();

            const bool valid = index_form_.name[0] != 0 && picked > 0;

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("Review SQL"), ImVec2(140, 0))) {
                db::NewIndex index;
                index.name         = index_form_.name;
                index.unique       = index_form_.unique;
                index.concurrently = index_form_.concurrently;

                for (const auto& [name, selected] : index_form_.columns) {
                    if (selected) index.columns.push_back(name);
                }

                confirm_ddl(TRF("Add index %s to %s", index_form_.name,
                                index_form_.table.c_str()),
                            db::generate_create_index(index_form_.schema,
                                                      index_form_.table, index),
                            index_form_.schema, index_form_.table);
                index_form_.open = false;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                index_form_.open = false;
            }

            if (!valid) {
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim),
                                   TR("(name and one column)"));
            }
        }
        ImGui::End();
    }

    // --- Renomear tabela ------------------------------------------------------

    if (rename_form_.open) {
        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
        if (ImGui::Begin(TRW("Rename table", "###RenameTable"),
                         &rename_form_.open,
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextColored(col4(p.text_dim), "%s.%s",
                               rename_form_.schema.c_str(),
                               rename_form_.table.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(260);
            ImGui::InputText(TR("New name"), rename_form_.new_name,
                             sizeof rename_form_.new_name);

            ImGui::Separator();

            const bool valid = rename_form_.new_name[0] != 0 &&
                               rename_form_.new_name != rename_form_.table;

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("Review SQL"), ImVec2(140, 0))) {
                db::TableAlteration wanted;
                wanted.schema   = rename_form_.schema;
                wanted.table    = rename_form_.table;
                wanted.new_name = rename_form_.new_name;

                confirm_ddl(TRF("Rename %s to %s", rename_form_.table.c_str(),
                                rename_form_.new_name),
                            db::generate_alter(rename_form_.current, wanted),
                            rename_form_.schema, rename_form_.table);
                rename_form_.open = false;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                rename_form_.open = false;
            }
        }
        ImGui::End();
    }
}

// Abre a janela de confirmação com um script já gerado.
//
// Ponto único por onde TODA alteração de estrutura passa. Executar direto de
// um item de menu seria mais curto e indefensável: um `DROP TABLE` disparado
// por um clique errado não tem desfazer (docs/DDL-WRITE.md §3).
void MainShell::confirm_ddl(std::string title, db::AlterScript script,
                            std::string schema, std::string table) {
    // Guardado AQUI, e nao no run_ddl: quando a janela confirma, o menu de
    // contexto que originou a acao ja' fechou e a referencia a' tabela nao
    // existe mais.
    ddl_reload_schema_ = std::move(schema);
    ddl_reload_table_  = std::move(table);

    ddl_dialog_.open(std::move(title), std::move(script),
                     [this](const std::vector<std::string>& statements) {
                         run_ddl(statements);
                     });
}

// Executa o DDL confirmado e recarrega a árvore.
//
// Sem o recarregamento, a coluna recém-criada não apareceria até o usuário
// mandar atualizar — e ele concluiria que o comando não funcionou, como a
// grade fazia ao exibir o valor antigo depois de gravar.
void MainShell::run_ddl(const std::vector<std::string>& statements) {
    if (statements.empty()) return;
    if (session().state() != SessionState::connected || session().busy()) return;

    std::vector<std::string> script = statements;

    // Em transação onde o SGBD permite: um script de três ALTERs que falha no
    // terceiro deixaria dois aplicados. No MySQL não adianta -- cada DDL faz
    // commit implícito --, e por isso a janela avisa em vez de prometer.
    const bool transactional =
        session().capabilities() && session().capabilities()->ddl_in_transaction;

    if (transactional && script.size() > 1) {
        script.insert(script.begin(), "BEGIN");
        script.emplace_back("COMMIT");
    }

    ddl_pending_reload_ = true;
    session().execute_script_async(std::move(script));
}

void MainShell::save_pending_edits(SqlDocument& document) {
    if (!document.edits().has_changes()) return;
    if (!document.result().has_value()) return;
    if (session().state() != SessionState::connected || session().busy()) return;

    auto updates = db::generate_changes(*document.result(),
                                        document.edit_target(),
                                        document.edits());
    if (!updates) {
        document.set_status(updates.error().to_string());
        return;
    }

    // Em transacao, mesmo em auto-commit: gravar cinco linhas e falhar na
    // terceira deixaria duas gravadas e tres nao -- estado que o usuario nao
    // pediu e nao consegue reproduzir (ADR 0014).
    std::vector<std::string> statements;
    statements.reserve(updates->size() + 2);
    statements.emplace_back("BEGIN");
    for (std::string& update : *updates) statements.push_back(std::move(update));
    statements.emplace_back("COMMIT");

    executing_document_id_ = document.id();
    document.set_executing(true);
    saving_edits_ = true;

    // O buffer so' e' limpo quando a gravacao termina sem erro -- ver a
    // colheita do resultado em draw().
    session().execute_script_async(std::move(statements));
}

// Menu de cor de uma coluna.
//
// Presets em vez de um formulário genérico: quem abre o menu quer "marcar os
// negativos de vermelho", não escolher operador, dois operandos e duas cores
// em RGB. O formulário completo existe na janela de regras, para quem precisa.
//
// A escolha é a mesma do DBeaver, que oferece "Set color by value" no menu da
// célula e o editor completo em Virtual Model.
void MainShell::draw_color_menu(SqlDocument& document, const db::ResultSet& rs,
                                std::size_t column) {
    const Palette& p = colors();
    const db::ColumnInfo& info = rs.column(column).info();

    auto add = [&](db::ColorOp op, std::string value, std::string value2,
                   std::uint32_t foreground, std::uint32_t background,
                   bool whole_row) {
        db::ColorRule rule;
        rule.column     = info.name;
        rule.op         = op;
        rule.value      = std::move(value);
        rule.value2     = std::move(value2);
        rule.foreground = foreground;
        rule.background = background;
        rule.whole_row  = whole_row;

        document.color_rules().add(std::move(rule));
        if (document.result()) {
            document.color_rules().prepare(*document.result());
        }
    };

    // Gradiente só faz sentido em coluna numérica: num texto o mínimo e o
    // máximo não existem, e a regra ficaria sem efeito -- um item de menu que
    // finge funcionar.
    // is_right_aligned é exatamente "é número": alinhar à direita e ter
    // mínimo/máximo comparáveis são a mesma propriedade.
    const bool numeric = db::is_right_aligned(info.kind);

    ImGui::BeginDisabled(!numeric);
    if (ImGui::MenuItem(TR("Heat map"))) {
        add(db::ColorOp::range, {}, {},
            with_alpha(p.ok, 0.30f), with_alpha(p.error, 0.35f), false);
    }
    if (ImGui::MenuItem(TR("Mark negatives"))) {
        add(db::ColorOp::less, "0", {}, p.error, 0, false);
    }
    if (ImGui::MenuItem(TR("Mark zeros"))) {
        add(db::ColorOp::equals, "0", {}, 0, with_alpha(p.text_dim, 0.20f), false);
    }
    ImGui::EndDisabled();

    if (!numeric) {
        ImGui::TextColored(col4(p.text_dim), TR("(numeric columns only)"));
    }

    ImGui::Separator();

    if (ImGui::MenuItem(TR("Mark nulls"))) {
        add(db::ColorOp::is_null, {}, {}, 0, with_alpha(p.warn, 0.18f), false);
    }

    // Colorir a linha pelo valor DESTA célula: é como se marca "cancelado"
    // sem repetir a regra em cada coluna.
    if (!rs.is_null(0, column)) {
        ImGui::Separator();
        ImGui::TextColored(col4(p.text_dim), TR("Highlight rows where"));

        // Os valores distintos desta coluna, até um limite. Com alta
        // cardinalidade a lista seria inútil e enorme -- e é justamente onde
        // um preset não serve.
        constexpr std::size_t kMaxDistinct = 12;

        std::vector<std::string> distinct;
        for (std::size_t row = 0;
             row < rs.row_count() && distinct.size() < kMaxDistinct + 1; ++row) {
            if (rs.is_null(row, column)) continue;

            std::string value(rs.text(row, column));
            if (std::find(distinct.begin(), distinct.end(), value) ==
                distinct.end()) {
                distinct.push_back(std::move(value));
            }
        }

        if (distinct.size() > kMaxDistinct) {
            ImGui::TextColored(col4(p.text_dim),
                               TR("too many distinct values"));
        } else {
            for (const std::string& value : distinct) {
                ImGui::PushID(value.c_str());
                if (ImGui::MenuItem((info.name + " = " + value).c_str())) {
                    add(db::ColorOp::equals, value, {}, 0,
                        with_alpha(p.accent, 0.20f), /*whole_row=*/true);
                }
                ImGui::PopID();
            }
        }
    }
}

void MainShell::draw_column_header_menu(SqlDocument& document,
                                        const db::ResultSet& rs,
                                        std::size_t column) {
    if (!ImGui::BeginPopupContextItem("##colmenu")) return;

    const Palette& p = colors();
    const db::ColumnInfo& info = rs.column(column).info();
    const bool can_run = session().state() == SessionState::connected &&
                         !session().busy();

    ImGui::TextColored(col4(p.accent_light), "%s", info.name.c_str());
    ImGui::TextColored(col4(p.text_dim), "%s", info.type_name.c_str());
    ImGui::Separator();

    // Cor condicional vem ANTES do retorno por paginação: ela funciona sobre
    // o que está na tela, e não precisa refazer a consulta como o filtro.
    if (ImGui::BeginMenu(TR("Color"))) {
        draw_color_menu(document, rs, column);
        ImGui::EndMenu();
    }

    if (document.color_rules().affects_column(info.name)) {
        if (ImGui::MenuItem(TR("Clear color rules of this column"))) {
            db::ColorRules& rules = document.color_rules();
            for (std::size_t i = rules.rules().size(); i-- > 0;) {
                if (rules.rules()[i].column == info.name) rules.remove(i);
            }
            if (document.result()) rules.prepare(*document.result());
        }
    }

    ImGui::Separator();

    // Filtrar exige refazer a consulta, o que so' vale para resultado
    // paginado -- um resultado completo ja' esta' inteiro na tela.
    if (!document.paged()) {
        ImGui::TextColored(col4(p.text_dim), TR("filtering needs a paged result"));
        ImGui::EndPopup();
        return;
    }

    // O campo guarda o texto por coluna, para nao perder o que foi digitado
    // ao fechar e reabrir o menu.
    static std::string editing_column;
    static char expression[256] = "";

    if (editing_column != info.name) {
        editing_column = info.name;
        const bool same = document.filter().column == info.name;
        std::snprintf(expression, sizeof expression, "%s",
                      same ? document.filter().expression.c_str() : "");
    }

    // "Expressao", nao "valor": o texto vai para a clausula WHERE como
    // digitado, e o rotulo precisa dizer isso.
    ImGui::TextColored(col4(p.text_dim), TR("WHERE %s ..."), info.name.c_str());
    ImGui::SetNextItemWidth(260.0f);

    const bool submitted = ImGui::InputTextWithHint(
        "##filterexpr", TR("> 100   |   LIKE '%lontra%'   |   IS NULL"),
        expression, sizeof expression,
        ImGuiInputTextFlags_EnterReturnsTrue);

    const bool apply = submitted ||
                       ImGui::Button(TR("Apply filter"));

    if (apply && can_run) {
        sql::ColumnFilter filter;
        if (expression[0] != '\0') {
            filter.column     = info.name;
            filter.expression = expression;
        }
        document.set_filter(std::move(filter));
        execute_page(document, 0);   // filtro muda o total: volta ao inicio
        ImGui::CloseCurrentPopup();
    }

    if (!document.filter().empty()) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Clear filter")) && can_run) {
            document.set_filter({});
            expression[0] = '\0';
            execute_page(document, 0);
            ImGui::CloseCurrentPopup();
        }
    }

    ImGui::Separator();

    if (ImGui::MenuItem(TR("Sort ascending"), nullptr, false, can_run)) {
        document.set_sort(sql::SortOrder{info.name, false});
        execute_page(document, 0);
    }
    if (ImGui::MenuItem(TR("Sort descending"), nullptr, false, can_run)) {
        document.set_sort(sql::SortOrder{info.name, true});
        execute_page(document, 0);
    }

    ImGui::Separator();

    // --- Agrupamento e totais (ADR 0005) ------------------------------------
    db::GroupSpec& spec = document.group_spec();

    const bool grouped =
        std::find(spec.group_by.begin(), spec.group_by.end(), column) !=
        spec.group_by.end();

    if (ImGui::MenuItem(grouped ? TR("Ungroup") : TR("Group by this column"))) {
        if (grouped) {
            spec.group_by.erase(std::remove(spec.group_by.begin(),
                                            spec.group_by.end(), column),
                                spec.group_by.end());
        } else {
            spec.group_by.push_back(column);
        }
        recompute_groups(document);
    }

    // --- Pivot (ADR 0005) ----------------------------------------------------
    //
    // Pivotar precisa de TRÊS colunas: a que fica como linha, a que vira
    // colunas, e a agregada. O menu pede a do meio -- esta -- e escolhe as
    // outras: a primeira coluna vira linha, e a primeira numérica vira valor.
    // Um assistente de três passos para a operação mais comum seria atrito.
    if (document.pivot_active()) {
        if (ImGui::MenuItem(TR("Clear pivot"))) {
            document.set_pivot_active(false);
            document.pivot_result() = {};
        }
    } else if (rs.column_count() >= 2) {
        if (ImGui::MenuItem(TR("Pivot by this column"))) {
            db::PivotSpec pivot_spec;

            // A coluna de linha é a primeira que NÃO seja esta.
            for (std::size_t i = 0; i < rs.column_count(); ++i) {
                if (i != column) { pivot_spec.rows = {i}; break; }
            }

            // A de valor é a primeira numérica que não seja nenhuma das duas.
            // Sem numérica, conta as linhas: é o pivot de frequência, que
            // funciona em qualquer resultado.
            pivot_spec.column   = column;
            pivot_spec.function = db::Aggregate::count;
            pivot_spec.value    = column;

            for (std::size_t i = 0; i < rs.column_count(); ++i) {
                if (i == column || i == pivot_spec.rows.front()) continue;
                if (!db::is_right_aligned(rs.column(i).info().kind)) continue;

                pivot_spec.value    = i;
                pivot_spec.function = db::Aggregate::sum;
                break;
            }

            document.pivot_spec() = pivot_spec;
            document.set_pivot_active(true);
            recompute_pivot(document);
        }
    }

    ImGui::Separator();

    if (ImGui::BeginMenu(TR("Aggregate"))) {
        static constexpr db::Aggregate kFunctions[] = {
            db::Aggregate::count, db::Aggregate::count_non_null,
            db::Aggregate::count_distinct, db::Aggregate::sum,
            db::Aggregate::average, db::Aggregate::minimum,
            db::Aggregate::maximum,
        };

        for (const db::Aggregate function : kFunctions) {
            // Agregacao que nao se aplica ao tipo aparece desabilitada, nao
            // escondida: o usuario ve' que existe e por que nao serve aqui.
            const bool applies = db::aggregate_applies(function, info.kind);

            const bool active = std::any_of(
                spec.aggregates.begin(), spec.aggregates.end(),
                [&](const db::AggregateSpec& a) {
                    return a.column == column && a.function == function;
                });

            if (ImGui::MenuItem(TR(std::string(db::to_string(function)).c_str()),
                                nullptr, active, applies)) {
                if (active) {
                    std::erase_if(spec.aggregates,
                                  [&](const db::AggregateSpec& a) {
                                      return a.column == column &&
                                             a.function == function;
                                  });
                } else {
                    spec.aggregates.push_back({column, function});
                }
                recompute_groups(document);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(TR("Clear grouping"), nullptr, false,
                        !spec.group_by.empty() || !spec.aggregates.empty())) {
        spec = {};
        recompute_groups(document);
    }

    ImGui::Separator();
    if (ImGui::MenuItem(TR("Copy column name"))) {
        ImGui::SetClipboardText(info.name.c_str());
    }

    ImGui::EndPopup();
}

void MainShell::draw_export_window() {
    const Palette& p = colors();

    SqlDocument* document = active_document();
    if (document == nullptr || !document->result().has_value()) {
        show_export_ = false;
        return;
    }
    const db::ResultSet& rs = *document->result();

    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_Appearing);
    if (ImGui::Begin(TRW("Export result", "###ExportResult"), &show_export_,
                     ImGuiWindowFlags_NoDocking)) {

        // O que sera' exportado: a PAGINA, nao o resultado inteiro. Dizer
        // isso evita a surpresa de abrir o CSV e achar 200 linhas de dois
        // milhoes (diretiva 6).
        if (document->paged()) {
            icon_inline(Icon::warning, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn),
                               TR("exports the current page only (%zu rows)"),
                               rs.row_count());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TR(
                    "The grid holds one page at a time.\n"
                    "To export everything, run the query with your own LIMIT "
                    "or no LIMIT at all."));
            }
        } else {
            ImGui::TextColored(col4(p.text_dim), TR("%zu row(s), %zu column(s)"),
                               rs.row_count(), rs.column_count());
        }

        ImGui::Separator();

        // --- Formato ---------------------------------------------------------
        static constexpr db::ExportFormat kFormats[] = {
            db::ExportFormat::csv, db::ExportFormat::json,
            db::ExportFormat::markdown, db::ExportFormat::sql_insert,
        };

        for (const db::ExportFormat format : kFormats) {
            if (format != kFormats[0]) ImGui::SameLine();
            if (ImGui::RadioButton(std::string(db::to_string(format)).c_str(),
                                   export_options_.format == format)) {
                const db::ExportFormat previous = export_options_.format;
                export_options_.format = format;

                // Troca a extensao junto com o formato. Deixar ".csv" num
                // arquivo de INSERTs faria o sistema abrir no programa errado
                // -- e o usuario provavelmente nao notaria ate' la'.
                const std::string old_ext(db::file_extension(previous));
                if (export_path_.size() > old_ext.size() &&
                    export_path_.ends_with(old_ext)) {
                    export_path_.replace(export_path_.size() - old_ext.size(),
                                         old_ext.size(),
                                         db::file_extension(format));
                }
            }
        }

        ImGui::Separator();

        // --- Opcoes do formato ativo -----------------------------------------
        if (export_options_.format == db::ExportFormat::csv) {
            ImGui::Checkbox(TR("Header row"), &export_options_.write_header);

            static constexpr struct { char value; const char* label; }
                kDelimiters[] = {
                    {',', ","}, {';', ";"}, {'\t', "Tab"}, {'|', "|"},
                };
            ImGui::TextUnformatted(TR("Delimiter"));
            for (const auto& [value, label] : kDelimiters) {
                ImGui::SameLine();
                if (ImGui::RadioButton(label,
                                       export_options_.delimiter == value)) {
                    export_options_.delimiter = value;
                }
            }

            ImGui::Checkbox(TR("Neutralize spreadsheet formulas"),
                            &export_options_.escape_formulas);
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TR(
                    "A value starting with =, +, - or @ becomes a formula when "
                    "a spreadsheet opens the file, and formulas run.\n\n"
                    "Prefixing it with an apostrophe neutralizes that without "
                    "changing what the cell shows. Turn this off only if you "
                    "are re-importing the data somewhere else."));
            }
        } else if (export_options_.format == db::ExportFormat::sql_insert) {
            char buffer[256];
            std::snprintf(buffer, sizeof buffer, "%s",
                          export_options_.table_name.c_str());
            ImGui::SetNextItemWidth(320);
            if (ImGui::InputText(TR("Target table"), buffer, sizeof buffer)) {
                export_options_.table_name = buffer;
            }
            ImGui::Checkbox(TR("One INSERT per row"),
                            &export_options_.one_statement_per_row);
        } else {
            ImGui::TextColored(col4(p.text_dim), TR("no options"));
        }

        ImGui::Separator();

        // --- Previa ----------------------------------------------------------
        //
        // Limitada a poucas linhas: gerar o arquivo inteiro a cada quadro para
        // mostrar a previa custaria caro num resultado de 200 linhas com
        // colunas largas.
        constexpr std::size_t kPreviewRows = 12;
        db::ResultSet preview = rs;
        preview.hide_rows_beyond(kPreviewRows);

        const std::string text = db::export_to_string(preview, export_options_);

        ImGui::TextColored(col4(p.text_dim), TR("Preview"));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col(p.bg_darkest));
        if (ImGui::BeginChild("##preview", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.4f),
                              ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Text, col(p.syntax_string));
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopStyleColor();

            if (rs.row_count() > kPreviewRows) {
                ImGui::TextColored(col4(p.text_dim), TR("... and %zu more row(s)"),
                                   rs.row_count() - kPreviewRows);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // --- Destino ---------------------------------------------------------
        if (export_path_.empty()) {
            // Sugere um caminho: pedir para digitar do zero seria atrito sem
            // motivo, e a area de trabalho e' onde a maioria procura depois.
            const char* home = std::getenv("USERPROFILE");
            if (home == nullptr) home = std::getenv("HOME");
            export_path_ = std::string(home != nullptr ? home : ".") +
                           "/otter-export" +
                           std::string(db::file_extension(export_options_.format));
        }

        char path_buffer[512];
        std::snprintf(path_buffer, sizeof path_buffer, "%s", export_path_.c_str());
        ImGui::SetNextItemWidth(-160.0f);
        if (ImGui::InputText(TR("File"), path_buffer, sizeof path_buffer)) {
            export_path_ = path_buffer;
        }

        if (icon_text_button("##copyclip", Icon::copy, TR("Copy to clipboard"),
                             TR("Copy the whole result, not just the preview"))) {
            const std::string full = db::export_to_string(rs, export_options_);
            ImGui::SetClipboardText(full.c_str());
            export_status_ = std::string(TRF("%zu row(s) copied",
                                             rs.row_count()));
        }
        ImGui::SameLine();
        if (icon_text_button("##savefile", Icon::save, TR("Save to file"),
                             TR("Write the result to the file above"),
                             !export_path_.empty())) {
            if (auto status = db::export_to_file(rs, export_options_,
                                                 export_path_);
                status) {
                export_status_ = std::string(TRF("%zu row(s) written to %s",
                                                 rs.row_count(),
                                                 export_path_.c_str()));
            } else {
                export_status_ = status.error().to_string();
            }
        }

        if (!export_status_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.ok), "%s", export_status_.c_str());
        }
    }
    ImGui::End();
}

void MainShell::draw_grid_toolbar(SqlDocument& document,
                                  const db::ResultSet& rs) {
    const Palette& p = colors();
    const bool can_run = session().state() == SessionState::connected &&
                         !session().busy();

    if (document.paged()) {
        // Intervalo real de linhas, base 1 -- "linhas 201-400" diz onde o
        // usuario esta'; "200 linhas" sozinho nao diria.
        const std::size_t first = document.page() * sql::kDefaultPageSize + 1;
        const std::size_t last  = first + rs.row_count() - 1;

        if (icon_button("##firstpage", Icon::first_page, TR("First page"),
                        can_run && document.page() > 0)) {
            execute_page(document, 0);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##prevpage", Icon::chevron_left, TR("Previous page"),
                        can_run && document.page() > 0)) {
            execute_page(document, document.page() - 1);
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (icon_button("##nextpage", Icon::chevron_right, TR("Next page"),
                        can_run && document.has_more())) {
            execute_page(document, document.page() + 1);
        }

        ImGui::SameLine();
        if (rs.row_count() == 0) {
            ImGui::TextColored(col4(p.text_dim), TR("no more rows"));
        } else {
            ImGui::TextColored(col4(p.text), TR("rows %zu-%zu"), first, last);
        }

        // "+" em vez de um total: saber o total exigiria um COUNT(*), que
        // varre a tabela outra vez (ADR 0011). Um numero inventado seria pior
        // que a ausencia dele.
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s  |  %zu %s  |  %zu bytes",
                           document.has_more() ? "+" : "",
                           rs.column_count(), TR("column(s)"),
                           rs.bytes_used());

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                TR("The query was rewritten with LIMIT %zu.\n"
                   "See the executed SQL in the Queries tab."),
                sql::kDefaultPageSize + 1);
        }

        ImGui::SameLine();
        if (icon_button("##export", Icon::save, TR("Export result..."))) {
            show_export_ = true;
            export_status_.clear();
        }
        return;
    }

    // Sem paginacao: o resultado e' completo, e a contagem e' exata.
    if (icon_button("##export", Icon::save, TR("Export result..."))) {
        show_export_ = true;
        export_status_.clear();
    }
    ImGui::SameLine();
    ImGui::TextColored(col4(p.text_dim),
                       TR("%zu row(s) x %zu column(s)  |  %zu bytes"),
                       rs.row_count(), rs.column_count(), rs.bytes_used());

    // Quando a consulta nao pode ser paginada, o resultado veio inteiro. Dizer
    // por que evita a pergunta "cade' os botoes de pagina?".
    if (!document.paged_sql().empty()) {
        const sql::PagedQuery probe = sql::make_paged_query(
            document.paged_sql(), active_dialect(), 0);
        if (probe.refusal == sql::PagingRefusal::already_limited) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), TR("  |  your LIMIT"));
        }
    }
}

void MainShell::draw_grid_panel() {
    if (ImGui::Begin(TRW("Result", "###ResultPanel"))) {
        // O resultado pertence ao documento: trocar de aba troca a grade.
        SqlDocument* document = active_document();

        if (document == nullptr || !document->result().has_value()) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("run a query to see the result"));
            // Um erro da última execução aparece mesmo sem resultado.
            if (document != nullptr && !document->status().empty()) {
                ImGui::TextColored(col4(colors().error), "%s",
                                   document->status().c_str());
            }
            ImGui::End();
            return;
        }

        const db::ResultSet& rs = *document->result();
        const Palette& p = colors();

        draw_grid_toolbar(*document, rs);

        // Alteracoes pendentes: contagem e os dois botoes. Fica acima da
        // grade, nao escondido num menu -- e' estado que o usuario precisa
        // ver sem procurar.
        if (document->edits().has_changes()) {
            const std::size_t rows = document->edits().touched_rows();

            icon_inline(Icon::warning, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn),
                               TR("%zu change(s) in %zu row(s), not saved"),
                               document->edits().change_count(), rows);

            ImGui::SameLine();
            if (icon_text_button("##saveedits", Icon::commit, TR("Save changes"),
                                 TR("Run the UPDATEs in a transaction"),
                                 !session().busy())) {
                save_pending_edits(*document);
            }
            ImGui::SameLine();
            if (icon_text_button("##discardedits", Icon::rollback,
                                 TR("Discard"),
                                 TR("Throw the pending changes away"))) {
                document->edits().clear();
            }
        } else if (!document->edit_target().editable() &&
                   rs.row_count() > 0) {
            // Diz POR QUE nao da' para editar, em vez de deixar o usuario
            // tentar e nao conseguir (diretiva 6).
            ImGui::TextColored(col4(p.text_dim), TR("read-only: %s"),
                               TR(std::string(db::to_string(
                                      document->edit_target().refusal)).c_str()));
        }

        // Pivot SUBSTITUI a grade: as duas juntas duplicariam a tela sem
        // ajudar a ler nenhuma.
        if (document->pivot_active()) {
            draw_pivot_table(*document, rs);
            ImGui::End();
            return;
        }

        draw_group_bar(*document, rs);
        draw_group_panel(*document, rs);

        ImGui::Separator();

        if (rs.column_count() == 0) {
            ImGui::TextColored(col4(colors().ok), TR("command executed"));
            if (rs.affected_rows() >= 0) {
                ImGui::SameLine();
                ImGui::TextColored(col4(colors().text_dim),
                                   TR(" (%lld row(s) affected)"),
                                   static_cast<long long>(rs.affected_rows()));
            }
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
            ImGuiTableFlags_SizingFixedFit |
            // Sortable so' marca o cabecalho como clicavel e guarda o pedido.
            // A ordenacao em si e' nossa, no servidor -- SortTristate permite
            // um terceiro clique que volta a' ordem original.
            ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;

        // O ImGui nao desenha mais de 64 colunas numa tabela. Truncar em
        // silencio faria o usuario concluir que a consulta devolveu menos
        // colunas do que devolveu (diretiva 6).
        constexpr std::size_t kMaxColumns = 64;
        const auto columns =
            static_cast<int>(std::min(rs.column_count(), kMaxColumns));

        if (rs.column_count() > kMaxColumns) {
            icon_inline(Icon::warning, p.warn);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(col4(p.warn),
                               TR("showing the first %zu of %zu columns"),
                               kMaxColumns, rs.column_count());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    TR("The grid cannot draw more than %zu columns.\n"
                       "Narrow the SELECT list to see the remaining ones."),
                    kMaxColumns);
            }
        }

        if (ImGui::BeginTable("##results", columns, flags)) {
            ImGui::TableSetupScrollFreeze(1, 1);   // cabecalho e 1a coluna fixos

            for (int c = 0; c < columns; ++c) {
                // Sem DefaultSort: a ordem inicial e' a do servidor. Ordenar
                // sem o usuario pedir esconderia a ordem natural do resultado,
                // que num SELECT com ORDER BY proprio e' justamente o ponto.
                ImGui::TableSetupColumn(
                    rs.column(static_cast<std::size_t>(c)).info().name.c_str());
            }
            // Cabecalhos um a um, em vez de TableHeadersRow(): cada um ganha
            // menu de contexto proprio, com o filtro daquela coluna.
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            for (int c = 0; c < columns; ++c) {
                ImGui::TableSetColumnIndex(c);
                const std::string& name =
                    rs.column(static_cast<std::size_t>(c)).info().name;

                // Coluna filtrada leva um prefixo no rotulo, nao um icone ao
                // lado: TableHeader ocupa a largura toda da celula, e
                // qualquer SameLine depois dele desenha POR CIMA do texto.
                const bool filtered =
                    !document->filter().empty() &&
                    document->filter().column == name;

                ImGui::PushID(c);
                if (filtered) {
                    // '*' e nao um simbolo Unicode: a fonte carregada cobre
                    // Latin-1, e um glifo ausente viraria '?' na tela.
                    const std::string marked = "* " + name;
                    ImGui::PushStyleColor(ImGuiCol_Text, col(p.warn));
                    ImGui::TableHeader(marked.c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::TableHeader(ImGui::TableGetColumnName(c));
                }

                draw_column_header_menu(*document, rs,
                                        static_cast<std::size_t>(c));
                ImGui::PopID();
            }

            // A ordenacao acontece no SERVIDOR, refazendo a consulta: ordenar
            // no cliente reordenaria apenas as 200 linhas da pagina, o que
            // daria uma ordem que nao existe no resultado completo.
            //
            // O ImGui so' avisa que o pedido mudou; nos executamos.
            if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
                specs != nullptr && specs->SpecsDirty) {
                specs->SpecsDirty = false;

                // Ordenar exige refazer a consulta, o que so' faz sentido
                // quando ela e' paginada -- um resultado completo ja' esta'
                // todo na tela, e reexecutar seria custo sem ganho.
                if (document->paged() && !session().busy()) {
                    sql::SortOrder order;

                    // SpecsCount == 0 com SortTristate: o terceiro clique
                    // removeu a ordenacao. SortOrder vazio volta a' ordem
                    // original do servidor.
                    if (specs->SpecsCount > 0) {
                        const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
                        const auto index =
                            static_cast<std::size_t>(spec.ColumnIndex);
                        if (index < rs.column_count()) {
                            order.column     = rs.column(index).info().name;
                            order.descending =
                                spec.SortDirection == ImGuiSortDirection_Descending;
                        }
                    }

                    if (order.column != document->sort().column ||
                        order.descending != document->sort().descending) {
                        document->set_sort(std::move(order));
                        // Volta para a primeira pagina: continuar na pagina 5
                        // de uma ordem diferente nao corresponde a nada.
                        execute_page(*document, 0);
                    }
                }
            }

            // Virtualizacao: so' as linhas visiveis sao desenhadas. E' o que
            // torna 1M linhas viavel (ADR 0005).
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(rs.row_count()));

            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto r = static_cast<std::size_t>(row);
                    ImGui::TableNextRow();

                    for (int c = 0; c < columns; ++c) {
                        const auto ci = static_cast<std::size_t>(c);
                        ImGui::TableSetColumnIndex(c);
                        draw_grid_cell(*document, rs, r, ci);
                    }
                }
            }

            // Linhas novas, depois das do resultado. Fundo verde: sao adicao,
            // nao alteracao -- a distincao importa antes de gravar.
            for (std::size_t i = 0; i < document->edits().insertions().size();
                 ++i) {
                const db::RowInsertion& insertion =
                    document->edits().insertions()[i];

                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(1000000 + i));

                for (int c = 0; c < columns; ++c) {
                    const auto ci = static_cast<std::size_t>(c);
                    ImGui::TableSetColumnIndex(c);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                           with_alpha(p.ok, 0.18f));

                    const auto value_it = insertion.values.find(ci);
                    const auto null_it  = insertion.nulls.find(ci);
                    const bool cell_null =
                        null_it != insertion.nulls.end() && null_it->second;

                    const std::string text =
                        cell_null ? "[null]"
                        : value_it != insertion.values.end() ? value_it->second
                                                             : std::string{};

                    ImGui::PushID(c);
                    // Campo direto, sem duplo clique: a linha nova existe para
                    // ser preenchida, e exigir um clique extra por celula
                    // seria atrito sem motivo.
                    char buffer[512];
                    std::snprintf(buffer, sizeof buffer, "%s",
                                  cell_null ? "" : text.c_str());

                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputTextWithHint(
                            "##newcell",
                            cell_null ? "[null]" : TR("(default)"),
                            buffer, sizeof buffer)) {
                        document->edits().set_new_value(i, ci, buffer);
                    }

                    if (ImGui::BeginPopupContextItem("##newcellmenu")) {
                        if (ImGui::MenuItem(TR("Set NULL"))) {
                            document->edits().set_new_null(i, ci);
                        }
                        if (ImGui::MenuItem(TR("Remove row"))) {
                            document->edits().remove_new_row(i);
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void MainShell::draw_query_log_panel() {
    if (ImGui::Begin(TRW("Queries", "###QueriesPanel"))) {
        const std::vector<db::QueryLog> log = session().query_log();

        if (log.empty()) {
            ImGui::TextColored(col4(colors().text_dim), TR("no queries yet"));
            ImGui::End();
            return;
        }

        ImGui::TextColored(col4(colors().text_dim),
                           "%zu query(s)  |  inclusive as internas de catálogo",
                           log.size());
        ImGui::Separator();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

        if (ImGui::BeginTable("##querylog", 4, flags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(TR("time"), ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn(TR("rows"), ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn(TR("state"), ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("SQL");
            ImGui::TableHeadersRow();

            // Mais recentes primeiro: e' o que se quer ver ao diagnosticar.
            for (std::size_t i = log.size(); i > 0; --i) {
                const db::QueryLog& entry = log[i - 1];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.2f ms",
                            static_cast<double>(entry.duration.count()) / 1000.0);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%zu", entry.rows);

                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(col4(entry.failed ? colors().error : colors().ok),
                                   entry.failed ? TR("error") : "ok");

                ImGui::TableSetColumnIndex(3);
                // Uma linha so': quebras de linha do SQL viram espaco.
                std::string single_line = entry.sql;
                std::replace(single_line.begin(), single_line.end(), '\n', ' ');
                ImGui::TextUnformatted(single_line.c_str());

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", entry.sql.c_str());
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}


void MainShell::draw_status_bar() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - kStatusBarHeight));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, kStatusBarHeight));

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 4));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col4(colors().bg_darkest));

    if (ImGui::Begin("##status", nullptr, flags)) {
        const SessionState state = session().state();

        const std::uint32_t color =
            state == SessionState::connected ? colors().ok
            : state == SessionState::failed  ? colors().error
            : state == SessionState::connecting ? colors().warn
                                                : colors().text_dim;
        ImGui::TextColored(col4(color), "●");
        ImGui::SameLine();

        if (state == SessionState::connected) {
            // session() garante que connections_ nao esta' vazio; o indice e'
            // checado mesmo assim, porque fechar a ultima conexao o deixa
            // apontando para fora por um quadro.
            const std::string driver_id =
                active_connection_ < connections_.size()
                    ? connections_[active_connection_].profile.driver_id
                    : active_profile_.driver_id;

            ImGui::TextColored(col4(colors().data), "%s",
                               session().database_name().c_str());
            ImGui::SameLine();
            // O perfil vem da conexao ATIVA, nao de active_profile_: este
            // ultimo so' e' atualizado pelo dialogo, e conectar por duplo
            // clique num perfil salvo o deixava para tras -- a barra exibia
            // "PostgreSQL 8.0.46" numa conexao MySQL.
            ImGui::TextColored(col4(colors().text_dim), "| %s %s |",
                               dbms_name(driver_id).c_str(),
                               session().server_version().c_str());
            ImGui::SameLine();
        }
        // Progresso do script no lugar da mensagem: num script de 40 comandos,
        // "executando..." parado seria indistinguivel de travado.
        const std::size_t total = session().script_total();
        if (total > 0) {
            const std::size_t done = session().script_progress();
            ImGui::TextColored(col4(colors().warn),
                               TR("running statement %zu of %zu"), done + 1,
                               total);
        } else {
            ImGui::TextColored(col4(colors().text_dim), "%s",
                               session().status_message().c_str());
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void MainShell::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (ImGui::Begin(TR("About C-Otter"), &show_about_,
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, col(colors().accent_light));
        ImGui::TextUnformatted("C-Otter 0.1.0");
        ImGui::PopStyleColor();

        ImGui::TextWrapped(
            "Say it out loud: sea otter. A playful nod to DBeaver, from a lighter, "
            "faster cousin that shares the same river -- and it's written in C.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Sea otters hold hands while they sleep so they never drift apart, keep a "
            "favorite rock in a pocket, and float together in a raft. Basically, they "
            "were born for databases.");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, col(colors().data));
        ImGui::TextUnformatted("Here, every JOIN is an OTTER JOIN.");
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::TextColored(col4(colors().text_dim),
                           "Dear ImGui %s  |  %s",
                           IMGUI_VERSION,
                           TR("native PostgreSQL and MySQL protocols"));
    }
    ImGui::End();
}

Session& MainShell::session() {
    // Sempre ha' uma Session, mesmo antes da primeira conexao: os ~70 pontos
    // que consultam estado (busy(), state(), schemas()) rodam a cada quadro,
    // e devolver ponteiro nulo obrigaria a checar em todos eles.
    //
    // A Session desconectada responde disconnected/false para tudo, que e'
    // exatamente o que a UI precisa desenhar.
    if (connections_.empty()) {
        connections_.push_back({std::make_unique<Session>(),
                                db::ConnectionProfile{}});
        active_connection_ = 0;
    }
    if (active_connection_ >= connections_.size()) {
        active_connection_ = connections_.size() - 1;
    }
    return *connections_[active_connection_].session;
}

const Session& MainShell::session() const {
    return const_cast<MainShell*>(this)->session();
}

Session& MainShell::open_connection(const db::ConnectionProfile& profile) {
    // Reusa a Session vazia criada por session(): abrir a primeira conexao
    // nao deve deixar uma aba morta para tras.
    const bool reuse_empty =
        connections_.size() == 1 &&
        connections_.front().session->state() == SessionState::disconnected;

    if (reuse_empty) {
        connections_.front().profile = profile;
        active_connection_ = 0;
    } else {
        connections_.push_back({std::make_unique<Session>(), profile});
        active_connection_ = connections_.size() - 1;
    }

    // A conexao recem-aberta passa a ser a ativa tambem para o perfil: sem
    // isto, conectar por duplo clique num perfil salvo deixava
    // active_profile_ apontando para a conexao ANTERIOR, e "Editar" abria o
    // perfil errado.
    active_profile_ = profile;

    Session& target = *connections_[active_connection_].session;
    target.connect_async(profile.to_conn_config());
    return target;
}

void MainShell::close_connection(std::size_t index) {
    if (index >= connections_.size()) return;

    // Desconecta antes de destruir: o destrutor da Session junta o worker, e
    // deixar a conexao aberta manteria o socket ate' la'.
    connections_[index].session->disconnect();
    connections_.erase(connections_.begin() +
                       static_cast<std::ptrdiff_t>(index));

    // A ativa passa a ser a anterior, nao a de mesmo indice: fechar a ultima
    // da lista deixaria active_connection_ apontando para fora.
    if (connections_.empty()) {
        active_connection_ = 0;
    } else if (active_connection_ >= connections_.size()) {
        active_connection_ = connections_.size() - 1;
    } else if (index < active_connection_) {
        --active_connection_;
    }
}

void MainShell::load_saved_profiles() {
    auto profiles = db::load_profiles(db::otter_store_location());
    if (!profiles) {
        // Arquivo corrompido nao pode impedir o programa de abrir. A mensagem
        // vai para a barra de status; o usuario decide o que fazer.
        saved_profiles_.clear();
        return;
    }
    saved_profiles_ = std::move(*profiles);
}

void MainShell::persist_profiles() {
    // Falha de gravacao e' relatada, nunca silenciosa: o usuario precisa saber
    // que a conexao que ele acabou de criar nao vai estar la' amanha.
    if (auto status = db::save_profiles(db::otter_store_location(),
                                        saved_profiles_);
        !status) {
        import_status_ = status.error().to_string();
    }
}

void MainShell::remember_profile(const db::ConnectionProfile& profile) {
    // Mesmo host+porta+banco+usuario e' a MESMA conexao, mesmo que o nome
    // tenha mudado: senao, editar o rotulo criaria uma entrada duplicada.
    const auto same_target = [&profile](const db::StoredProfile& stored) {
        return stored.profile.host == profile.host &&
               stored.profile.port == profile.port &&
               stored.profile.database == profile.database &&
               stored.profile.user == profile.user;
    };

    for (db::StoredProfile& stored : saved_profiles_) {
        if (!same_target(stored)) continue;

        const std::string id = stored.id;   // preserva o id e o raw_json
        stored.profile = profile;
        stored.id      = id;
        persist_profiles();
        return;
    }

    db::StoredProfile fresh;
    fresh.profile   = profile;
    fresh.provider  = "postgresql";
    fresh.driver    = "postgres-jdbc";
    fresh.supported = true;

    // O nome fica VAZIO quando o usuario nao deu um. effective_name() deriva
    // "banco@host" na hora de exibir.
    //
    // Gravar o nome derivado congelaria o rotulo: um perfil salvo como
    // "TokenGuard@localhost" e reaberto apontando para outro banco continuaria
    // exibindo TokenGuard, contradizendo a barra de status. Foi o defeito
    // observado ao testar as variaveis de ambiente sobre um perfil salvo.
    saved_profiles_.push_back(std::move(fresh));
    persist_profiles();
}

void MainShell::draw_import_window() {
    const Palette& p = colors();

    // Larga o bastante para o motivo caber inteiro: "o driver MySQL ainda
    // nao foi implementado" cortado no meio nao informa nada.
    ImGui::SetNextWindowSize(ImVec2(1000, 560), ImGuiCond_Appearing);
    if (ImGui::Begin(TRW("Import from DBeaver", "###ImportDBeaver"),
                     &show_import_, ImGuiWindowFlags_NoDocking)) {

        // Varre uma vez ao abrir; reabrir a janela nao deve reler o disco a
        // cada quadro.
        if (!import_scanned_) {
            import_scanned_ = true;
            import_candidates_.clear();
            import_selected_.clear();

            for (const db::StoreLocation& location :
                 db::dbeaver_store_locations()) {
                auto found = db::load_profiles(location);
                if (!found) continue;

                for (db::StoredProfile& stored : *found) {
                    import_candidates_.push_back(std::move(stored));
                }
            }
            // Vem marcado o que da' para usar; o resto fica desmarcado mas
            // visivel, com o motivo.
            for (const db::StoredProfile& stored : import_candidates_) {
                import_selected_.push_back(stored.supported);
            }
        }

        if (import_candidates_.empty()) {
            ImGui::TextColored(col4(p.text_dim),
                               TR("No DBeaver workspace found on this machine."));
            ImGui::TextColored(col4(p.text_dim),
                               TR("Looked under %APPDATA%\\DBeaverData."));
            ImGui::End();
            return;
        }

        ImGui::TextColored(col4(p.text_dim),
                           TR("%zu connection(s) found. Nothing is written back "
                              "to DBeaver."),
                           import_candidates_.size());
        ImGui::Separator();

        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

        const float table_height = ImGui::GetContentRegionAvail().y -
                                   ImGui::GetFrameHeightWithSpacing() * 2.2f;

        if (ImGui::BeginTable("##import", 5, flags,
                              ImVec2(0.0f, table_height))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
            ImGui::TableSetupColumn(TR("Name"),
                                    ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn(TR("Driver"), ImGuiTableColumnFlags_WidthFixed,
                                    80.0f);
            ImGui::TableSetupColumn(TR("Server"),
                                    ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn(TR("Status"),
                                    ImGuiTableColumnFlags_WidthStretch, 1.8f);
            ImGui::TableHeadersRow();

            for (std::size_t i = 0; i < import_candidates_.size(); ++i) {
                const db::StoredProfile& stored = import_candidates_[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::BeginDisabled(!stored.supported);
                bool selected = import_selected_[i];
                if (ImGui::Checkbox("##pick", &selected)) {
                    import_selected_[i] = selected;
                }
                ImGui::EndDisabled();

                const std::uint32_t tint =
                    stored.supported ? p.text : with_alpha(p.text_dim, 0.6f);

                ImGui::TableNextColumn();
                ImGui::TextColored(col4(tint), "%s",
                                   stored.profile.name.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(col4(p.text_dim), "%s",
                                   stored.provider.c_str());

                ImGui::TableNextColumn();
                ImGui::TextColored(col4(tint), "%s:%u/%s",
                                   stored.profile.host.c_str(),
                                   static_cast<unsigned>(stored.profile.port),
                                   stored.profile.database.c_str());

                ImGui::TableNextColumn();
                if (stored.supported) {
                    // Dizer se a senha veio junto evita a surpresa de
                    // importar e descobrir que ainda falta digitar.
                    if (!stored.profile.password.empty()) {
                        ImGui::TextColored(col4(p.ok), TR("with password"));
                    } else {
                        ImGui::TextColored(col4(p.text_dim), TR("no password"));
                    }
                } else {
                    icon_inline(Icon::warning, p.warn);
                    ImGui::SameLine(0.0f, 4.0f);
                    // O motivo vem do store em ingles, que e' a chave de
                    // traducao (diretiva 8). TR() no momento de desenhar, nao
                    // na origem: o store nao conhece o idioma ativo.
                    ImGui::TextColored(col4(p.warn), "%s",
                                       TR(stored.unsupported_reason.c_str()));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        std::size_t picked = 0;
        for (const bool selected : import_selected_) {
            if (selected) ++picked;
        }

        ImGui::Separator();
        if (!import_status_.empty()) {
            ImGui::TextColored(col4(p.ok), "%s", import_status_.c_str());
        }

        if (icon_text_button("##doimport", Icon::save, TR("Import selected"),
                             TR("Copy the selected connections into C-Otter"),
                             picked > 0)) {
            std::size_t imported = 0;
            for (std::size_t i = 0; i < import_candidates_.size(); ++i) {
                if (!import_selected_[i]) continue;

                // Id novo: o do DBeaver pertence ao arquivo dele, e reusa-lo
                // criaria confusao se as duas ferramentas divergirem.
                db::StoredProfile copy = import_candidates_[i];
                copy.id.clear();
                saved_profiles_.push_back(std::move(copy));
                ++imported;
            }
            persist_profiles();
            import_status_ = std::string(TRF("%zu connection(s) imported",
                                             imported));
        }

        ImGui::SameLine();
        if (icon_text_button("##rescan", Icon::refresh, TR("Rescan"),
                             TR("Look for DBeaver workspaces again"))) {
            import_scanned_ = false;
            import_status_.clear();
        }
    }
    ImGui::End();
}

void MainShell::draw_icon_gallery() {
    // Nomes em ingles literal, sem TR(): sao identificadores do enum Icon, nao
    // texto de interface. Traduzi-los tornaria a galeria inutil para conferir
    // qual desenho corresponde a qual constante do codigo.
    struct Entry { Icon icon; const char* name; };
    static const Entry kEntries[] = {
        {Icon::connect, "connect"},       {Icon::disconnect, "disconnect"},
        {Icon::play, "play"},             {Icon::stop, "stop"},
        {Icon::commit, "commit"},         {Icon::rollback, "rollback"},
        {Icon::database, "database"},     {Icon::schema, "schema"},
        {Icon::pivot, "pivot"},
        {Icon::table, "table"},           {Icon::view, "view"},
        {Icon::materialized_view, "materialized_view"},
        {Icon::column, "column"},         {Icon::key, "key"},
        {Icon::constraint, "constraint"}, {Icon::index, "index"},
        {Icon::foreign_key, "foreign_key"},
        {Icon::references, "references"}, {Icon::sequence, "sequence"},
        {Icon::function, "function"},     {Icon::procedure, "procedure"},
        {Icon::trigger, "trigger"},       {Icon::data_type, "data_type"},
        {Icon::extension, "extension"},   {Icon::role, "role"},
        {Icon::tablespace, "tablespace"}, {Icon::folder, "folder"},
        {Icon::refresh, "refresh"},       {Icon::search, "search"},
        {Icon::settings, "settings"},     {Icon::plus, "plus"},
        {Icon::close, "close"},           {Icon::pin, "pin"},
        {Icon::save, "save"},             {Icon::open, "open"},
        {Icon::copy, "copy"},
        {Icon::chevron_left, "chevron_left"},
        {Icon::first_page, "first_page"},
        {Icon::last_page, "last_page"},
        {Icon::chevron_right, "chevron_right"},
        {Icon::chevron_down, "chevron_down"},
        {Icon::warning, "warning"},       {Icon::error, "error"},
        {Icon::info, "info"},             {Icon::clock, "clock"},
        {Icon::filter, "filter"},
    };

    ImGui::SetNextWindowSize(ImVec2(1180, 900), ImGuiCond_Appearing);
    if (ImGui::Begin(TRW("Icon gallery", "###IconGallery"), &show_icons_,
                     ImGuiWindowFlags_NoDocking)) {
        const Palette& p = colors();

        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(p.text_dim), TR(
            "Every object type needs its own drawing. Two icons that look alike "
            "at tree size are a defect."));
        ImGui::PopTextWrapPos();
        ImGui::Separator();

        // Tres tamanhos: o da arvore (o mais critico), o da barra e um grande
        // para inspecionar o traco.
        static float scale = 1.0f;
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat(TR("Scale"), &scale, 0.6f, 4.0f, "%.1fx");
        ImGui::Separator();

        const float cell = 132.0f;
        const int columns = (std::max)(
            1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell));

        if (ImGui::BeginTable("##icons", columns)) {
            for (const Entry& entry : kEntries) {
                ImGui::TableNextColumn();

                const ImVec2 origin = ImGui::GetCursorScreenPos();
                const float box = ImGui::GetFontSize() * 2.2f * scale;
                ImGui::Dummy(ImVec2(box, box));

                draw_icon(entry.icon,
                          ImVec2(origin.x + box * 0.5f, origin.y + box * 0.5f),
                          box * 0.8f, p.accent_light, 1.6f);

                ImGui::TextColored(col4(p.text), "%s", entry.name);

                // Ao lado, o mesmo desenho no tamanho real da arvore: e' ai'
                // que a confusao entre dois icones aparece.
                icon_inline(entry.icon, p.text_dim);
                ImGui::SameLine(0.0f, 4.0f);
                ImGui::TextColored(col4(p.text_dim), TR("tree size"));
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

} // namespace otter::ui


