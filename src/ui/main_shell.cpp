#include "ui/main_shell.hpp"

#include "db/connection_import.hpp"

#include <filesystem>

#include "base/i18n.hpp"
#include "db/registry.hpp"
#include "db/aggregate.hpp"
#include "db/ddl.hpp"
#include "db/export.hpp"
#include "sql/format.hpp"
#include "sql/paging.hpp"
#include "ui/app_window.hpp"   // mono_font(), para o editor SQL
#include "ui/file_dialog.hpp"
#include "ui/icon_images.hpp"
#include "ui/hint.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder: layout inicial programatico

#include <algorithm>
#include <cctype>
#include <cmath>
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
    // Espaco e tabulacao: discretos, mas VISIVEIS.
    //
    // Era `bg_light`, que no tema claro e' branco puro -- os pontos eram
    // desenhados sobre fundo branco e sumiam. "Mostrar espaços" parecia nao
    // funcionar; funcionava, e a cor e' que era invisivel.
    //
    // `text_dim` com alfa da' a discricao pretendida sem depender do fundo:
    // ele ja' e' escolhido por tema para contrastar com ele.
    set(Color::whitespace,      with_alpha(t.text_dim, 0.55f));
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

    // Antes de qualquer documento: o nome de uma aba nova e' escolhido contra
    // os arquivos desta pasta.
    scripts_dir_ = scripts_directory();

    // Cria a Session vazia ANTES de qualquer documento: new_document() amarra
    // a aba a' conexao ativa, e sem nenhuma conexao existindo ela nasceria
    // com id 0 -- que nao pertence a janela nenhuma, deixando a aba invisivel.
    (void)session();

    // NENHUM documento nasce aqui. Havia um script de boas-vindas, e com ele
    // o programa abria sempre com uma aba de conexao sem nome e sem conexao
    // -- o usuario pediu a area vazia quando nao ha' script (2026-10-01), que
    // e' tambem como o DBeaver abre. As abas vem dos scripts gravados
    // (restore_scripts, mais abaixo), de conectar (open_connection) ou de
    // "New script".

    // O assistente conecta e, ao concluir, tambem guarda o perfil ativo.
    // "Testar" reutiliza a conexao ativa: criar uma permanente a cada clique
    // encheria o Raft de entradas que o usuario nao pediu.
    connection_dialog_.set_on_test([this](const db::ConnectionProfile& profile) {
        session().connect_async(profile.to_conn_config());
    });

    connection_dialog_.set_on_connect([this](const db::ConnectionProfile& wanted) {
        const db::ConnectionProfile profile = remember_profile(wanted);
        active_profile_ = profile;
        open_connection(profile);
    });
    connection_dialog_.set_on_save([this](const db::ConnectionProfile& wanted) {
        const db::ConnectionProfile profile = remember_profile(wanted);
        active_profile_ = profile;

        // A conexao ABERTA tambem recebe o perfil novo.
        //
        // Sem isto, editar uma conexao viva gravava em disco e nao mudava
        // nada na tela: as preferencias do editor sao lidas de
        // Connection::profile, que continuava com os valores antigos ate' a
        // proxima execucao. O usuario ligava "mostrar espacos", clicava OK,
        // e o editor seguia igual.
        //
        // Mesmo criterio de remember_profile: driver + host + porta + banco
        // + usuario. O `id` nao serve -- uma conexao aberta pelo dialogo de
        // "nova conexao" ainda nao tem id atribuido.
        bool matched = false;
        for (Connection& connection : connections_) {
            // So' a conexao RAIZ: a sessao de um banco expandido na arvore
            // tem perfil derivado, e nao e' ela que o usuario editou.
            if (connection.parent_id != 0) continue;

            if (connection.profile.driver_id == profile.driver_id &&
                connection.profile.host == profile.host &&
                connection.profile.port == profile.port &&
                connection.profile.database == profile.database &&
                connection.profile.user == profile.user) {
                connection.profile = profile;
                matched = true;

                // As opcoes de listagem de bancos valem na hora: marcar
                // "Show template databases" e so' ver o efeito ao reconectar
                // pareceria que a caixa nao funciona.
                const db::PostgresOptions& pg = profile.postgres;
                connection.session->set_database_listing(
                    pg.show_non_default_databases && pg.show_template_databases,
                    pg.show_non_default_databases &&
                        pg.show_unavailable_databases);
                connection.session->reload_catalog_async();
            }
        }

        // Nenhuma conexao aberta corresponde -- e' o caso de editar um
        // perfil salvo, ou de ajustar as preferencias ANTES de conectar.
        //
        // A Session vazia (a que existe antes da primeira conexao) recebe o
        // perfil mesmo assim: e' dela que o editor visivel le' as opcoes, e
        // sem isto ligar "mostrar espacos" nao mudava nada ate' conectar.
        if (!matched && connections_.size() == 1 &&
            connections_.front().session->state() == SessionState::disconnected) {
            connections_.front().profile = profile;
        }
    });

    // A copia automatica da pasta antiga (%APPDATA%\C-Otter, de antes do ADR
    // 0020) SAIU, a pedido do usuario (2026-10-01): ela trazia de volta, a
    // cada pasta nova, conexoes de teste que ele nao criou ali ("MySQL de
    // teste", "_1", "_2"). Nenhuma conexao nasce sozinha a partir de dados
    // do proprio C-Otter; so' as das OUTRAS ferramentas, na primeira execucao.
    //
    // Antes de qualquer gravacao: a pasta de dados ainda esta' vazia?
    const bool fresh_store = db::store_is_fresh(db::otter_store_location());
    import_external_on_first_run(fresh_store);

    // Conexoes salvas na execucao anterior (ADR 0012).
    load_saved_profiles();

    // Os scripts que estavam abertos, cada um na conexao dele -- depois dos
    // perfis, que e' por onde a conexao de cada script e' achada.
    restore_scripts();

    // Abre primeiro: open_new() reinicia o perfil, e so' depois disso faz
    // sentido preencher a partir do ambiente (como psql faz).
    // A ultima conexao usavel salva reabre ja' na aba de configuracao, com os
    // campos preenchidos. Era o defeito mais incomodo do uso diario:
    // redigitar host, banco e usuario a cada execucao (ADR 0012).
    //
    // open_edit em vez de open_new: com um perfil conhecido, parar no
    // catalogo de drivers obrigaria a escolher PostgreSQL de novo para so'
    // entao ver o que ja' estava salvo.
    //
    // Com alguma variavel PG* definida, so' um perfil PostgreSQL serve de
    // base. As variaveis sao a convencao do psql: aplicadas sobre um perfil
    // MySQL, mudavam host/porta/banco e deixavam o DRIVER MySQL -- que falava
    // o protocolo errado com a porta 5432 e morria em "reading packet header:
    // timed out". Pior: remember_profile gravava o hibrido no disco, e o
    // perfil "MySQL de teste" passou a apontar para o PostgreSQL.
    const bool pg_env = std::getenv("PGHOST") != nullptr ||
                        std::getenv("PGPORT") != nullptr ||
                        std::getenv("PGDATABASE") != nullptr ||
                        std::getenv("PGUSER") != nullptr ||
                        std::getenv("PGPASSWORD") != nullptr;

    // Com PGHOST definido, o perfil-base tem de ser DAQUELE host. Antes valia
    // "o primeiro PostgreSQL salvo": com as conexoes importadas na primeira
    // execucao (ADR 0023), o primeiro podia ser um servidor de producao, e o
    // ambiente trocava so' o host -- sobrava um hibrido com a porta e o
    // usuario de outro servidor ("VULTR_1  localhost:54045").
    const char* env_host = std::getenv("PGHOST");

    const db::StoredProfile* last_usable = nullptr;
    for (const db::StoredProfile& stored : saved_profiles_) {
        if (!stored.supported) continue;
        if (pg_env && stored.profile.driver_id != "postgresql") continue;
        if (env_host != nullptr && stored.profile.host != env_host) continue;
        // Entre os do mesmo host, o que tem senha salva: e' o que conecta.
        if (last_usable == nullptr ||
            (last_usable->profile.password.empty() && !stored.profile.password.empty())) {
            last_usable = &stored;
        }
        if (!pg_env) break;
    }

    if (last_usable != nullptr) {
        connection_dialog_.open_edit(last_usable->profile);
    } else if (pg_env) {
        // Perfil novo ja' em PostgreSQL, na aba de configuracao: o ambiente
        // escolheu o driver, e parar no catalogo pediria a escolha de novo.
        connection_dialog_.open_configure(db::ConnectionProfile{});
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



    // Perfil de atalhos, tema e idioma da execucao anterior. Antes do tema
    // por ambiente, que continua valendo por cima -- e' o que as capturas
    // usam.
    load_settings();
    for (auto& document : documents_) {
        apply_editor_palette(document->editor());
    }

    // Tema inicial por ambiente, pelo mesmo motivo: conferir os tres temas
    // exige tres capturas, e trocar pelo menu a cada uma e' fragil.
    if (const char* theme = std::getenv("OTTER_THEME")) {
        set_theme(theme);

        // Os documentos JA' criados guardam a paleta do tema anterior -- o
        // primeiro nasce antes desta linha. Sem reaplicar, OTTER_THEME=light
        // deixava a interface clara e o EDITOR escuro, e a captura do tema
        // claro nao mostrava o tema claro.
        //
        // Mesma razao do laco no menu Tema, que ja' fazia isto.
        for (auto& document : documents_) {
            apply_editor_palette(document->editor());
        }
    }

    // Conecta direto, usando o perfil ja' montado a partir de PGHOST/PGUSER/...
    // Serve para conferir a arvore de objetos numa captura: automatizar o
    // clique em "Conectar" erra o alvo com frequencia, e uma tela conferida a'
    // mao vale mais que um clique que talvez tenha acontecido.
    if (std::getenv("OTTER_AUTOCONNECT") != nullptr) {
        // SEM gravar o perfil. Gravava, "para exercitar o caminho normal" -- e
        // cada captura de conferencia deixava uma conexao que o usuario nao
        // criou: "localhost_1", "MySQL de teste_1", "MySQL de teste_2". Ele
        // pediu o fim da criacao automatica (2026-10-01); uma conexao montada
        // a partir de variaveis de ambiente e' de quem rodou o script, nao do
        // arquivo de conexoes.
        active_profile_ = profile;
        open_connection(active_profile_);
        connection_dialog_.close();
    }

    // O dialogo so' se abre sozinho quando NAO ha' conexao salva -- e' a
    // primeira execucao, e nao ha' mais nada a fazer no programa. Com
    // conexoes na arvore, abri-lo a cada inicio punha uma janela modal entre
    // o usuario e o duplo clique que ele ia dar (pedido dele, 2026-09-30); o
    // DBeaver tambem abre direto no navegador.
    //
    // Fechado aqui, e nao deixando de abrir la' em cima: o perfil do dialogo
    // e' onde as variaveis PG* sao aplicadas, e o autoconnect le dele.
    if (!saved_profiles_.empty()) connection_dialog_.close();

    // Abre o dialogo de conexao ja' na etapa de configuracao, para conferir a
    // arvore de paginas numa captura (docs/DIALOG-PARITY.md).
    //
    // Automatizar "Editar" a partir da lista erra o alvo: a posicao do item
    // depende de quantas conexoes ha' gravadas. Uma flag acerta sempre.
    if (std::getenv("OTTER_SHOW_CONN_DIALOG") != nullptr) {
        connection_dialog_.open_edit(profile);
    }
}

MainShell::~MainShell() = default;

SqlDocument& MainShell::new_document() {
    documents_.push_back(std::make_unique<SqlDocument>(next_document_id_++));
    SqlDocument& document = *documents_.back();

    document.editor().SetAutoCompleteConfig(autocomplete_config_.get());
    apply_editor_palette(document.editor());

    // Os dois menus de contexto do editor: sobre o texto e sobre a regua. O
    // widget abre o popup e chama de volta para preenche-lo.
    document.editor().SetTextContextMenuCallback(
        [this](TextEditor::PopupData&) { draw_editor_context_menu(); });
    document.editor().SetLineNumberContextMenuCallback(
        [this](TextEditor::PopupData&) { draw_ruler_context_menu(); });

    // Nasce na conexao ATIVA. E' o padrao certo para quem abre um script pelo
    // menu ou pelo Navigator: o alvo e' a base que se esta' olhando. Quem
    // cria pelo "+" de uma janela de conexao sobrescreve isto logo depois,
    // com a conexao daquela janela.
    if (active_connection_ < connections_.size()) {
        document.set_connection_id(connections_[active_connection_].id);
    }

    // O nome da aba e' o do arquivo em que ela sera' gravada ("Script",
    // "Script-1", ... como no DBeaver). Escolhido ja', e nao na primeira
    // gravacao: o rotulo nao pode mudar no meio da digitacao.
    document.set_default_title(next_script_name());

    active_document_ = documents_.size() - 1;
    return document;
}

void MainShell::close_document(std::size_t index) {
    if (index >= documents_.size()) return;

    // Fechar a ultima aba deixa a lista VAZIA -- e a janela da conexao some
    // com ela (draw_editor_panel). Antes uma aba vazia renascia no lugar, e
    // nao havia como fechar a janela de uma conexao. active_document() ja'
    // devolve nulo para a lista vazia; quem o chama trata.
    retire_script(*documents_[index]);
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!documents_.empty() && active_document_ >= documents_.size()) {
        active_document_ = documents_.size() - 1;
    }
}

// So' editores de OBJETO: um script e' gravado sozinho (ui/script_session.cpp)
// e fechar a aba dele nao perde nada.
std::size_t MainShell::connection_unsaved_documents(std::size_t connection_id) const {
    std::size_t count = 0;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->connection_id() == connection_id && document->is_object() &&
            document->modified()) {
            ++count;
        }
    }
    return count;
}

std::size_t MainShell::connection_pending_cell_edits(std::size_t connection_id) const {
    std::size_t count = 0;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->connection_id() == connection_id) {
            count += document->edits().change_count();
        }
    }
    return count;
}

// Fecha a ABA DA CONEXAO: todos os scripts e editores de objeto dela. A
// sessao continua como estava -- fechar um editor nao desconecta, no DBeaver
// tambem nao; quem desconecta e' a arvore.
void MainShell::close_connection_documents(std::size_t connection_id) {
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->connection_id() == connection_id) retire_script(*document);
    }
    std::erase_if(documents_,
                  [connection_id](const std::unique_ptr<SqlDocument>& document) {
                      return document->connection_id() == connection_id;
                  });
    if (!documents_.empty() && active_document_ >= documents_.size()) {
        active_document_ = documents_.size() - 1;
    }

    // A janela volta ao no' dos editores quando ganhar outro script: o no'
    // em que ela estava pode ter deixado de existir ao ficar vazio, e sem
    // isto ela renasceria flutuando sobre a grade.
    if (Connection* connection = connection_by_id(connection_id)) {
        connection->docked = false;
    }
}

// O "x" da aba da conexao (e "tab close" no canal de comandos). Com trabalho
// nao gravado pergunta antes, pela mesma razao de request_quit: o que se
// perde aqui nao tem desfazer.
void MainShell::apply_close_tab_request() {
    if (close_tab_request_ == 0) return;
    const std::size_t connection_id = std::exchange(close_tab_request_, 0);

    if (connection_unsaved_documents(connection_id) == 0 &&
        connection_pending_cell_edits(connection_id) == 0) {
        close_connection_documents(connection_id);
        return;
    }
    confirm_close_tab_ = connection_id;
}

void MainShell::draw_close_tab_confirm() {
    if (confirm_close_tab_ == 0) return;

    const Connection* connection = connection_by_id(confirm_close_tab_);
    if (connection == nullptr) {
        confirm_close_tab_ = 0;   // a conexao foi removida com o dialogo aberto
        return;
    }

    ImGui::OpenPopup("###CloseTabConfirm");

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(TRW("Close tab", "###CloseTabConfirm"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // QUAL aba: o pedido pode ter vindo do "x" de uma aba que nao esta'
        // na frente.
        ImGui::TextUnformatted(connection_title(*connection).c_str());
        ImGui::TextColored(col4(colors().warn), "%s",
                           TR("There is work that was not saved."));
        ImGui::Spacing();

        if (const std::size_t scripts =
                connection_unsaved_documents(confirm_close_tab_);
            scripts > 0) {
            ImGui::BulletText(TR("%zu object editor(s) with unsaved changes"), scripts);
        }
        if (const std::size_t cells =
                connection_pending_cell_edits(confirm_close_tab_);
            cells > 0) {
            ImGui::BulletText(TR("%zu cell edit(s) not written to the database"),
                              cells);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // "Cancelar" e' o padrao, como na confirmacao de saida.
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            confirm_close_tab_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, col(colors().error));
        if (ImGui::Button(TR("Close and discard"), ImVec2(160, 0))) {
            close_connection_documents(std::exchange(confirm_close_tab_, 0));
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

void MainShell::close_others(std::size_t keep_index) {
    if (keep_index >= documents_.size()) return;

    // Guarda o id ANTES de mover: depois do move, documents_[keep_index] e' um
    // unique_ptr vazio e consulta-lo seria desreferenciar nulo.
    const std::size_t keep_id = documents_[keep_index]->id();

    // "Outras" sao as da MESMA conexao: o menu foi aberto na barra de abas de
    // uma janela de conexao, e fechar junto os scripts das demais apagaria
    // trabalho que nem esta' visivel dali.
    const std::size_t keep_conn = documents_[keep_index]->connection_id();

    std::vector<std::unique_ptr<SqlDocument>> kept;
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        // Abas fixadas sobrevivem a "fechar outras" -- e' o que "fixar" quer
        // dizer.
        if (i == keep_index || documents_[i]->pinned() ||
            documents_[i]->connection_id() != keep_conn) {
            kept.push_back(std::move(documents_[i]));
        } else {
            retire_script(*documents_[i]);
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

// Editores de OBJETO com alteracao por gravar. Os scripts nao contam: sao
// gravados sozinhos, e perguntar por eles ao sair era o que o usuario pediu
// para acabar (2026-10-01).
std::size_t MainShell::unsaved_documents() const {
    std::size_t count = 0;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->is_object() && document->modified()) ++count;
    }
    return count;
}

std::size_t MainShell::pending_cell_edits() const {
    std::size_t count = 0;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        count += document->edits().change_count();
    }
    return count;
}

// Sair sem perguntar descarta o que o usuario digitou e nao gravou. Nada
// disso tem desfazer depois que o processo morre, entao a confirmacao so' e'
// pulada quando NAO ha' o que perder -- perguntar sempre treina a clicar em
// "sair" sem ler.
void MainShell::request_quit() {
    // Os scripts primeiro, sem esperar o atraso da digitacao: o que foi
    // digitado no ultimo instante tambem fica.
    flush_scripts();

    if (unsaved_documents() == 0 && pending_cell_edits() == 0) {
        wants_quit_ = true;
        return;
    }
    confirm_quit_ = true;
}

void MainShell::draw_quit_confirm() {
    if (!confirm_quit_) return;

    constexpr const char* kPopup = "###QuitConfirm";
    ImGui::OpenPopup(kPopup);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    // O titulo passa por TRW: o ImGui identifica a janela pelo nome, e
    // traduzi-lo sem id estavel a faria perder a posicao ao trocar de idioma.
    if (ImGui::BeginPopupModal(TRW("Exit C-Otter", "###QuitConfirm"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(col4(colors().warn), "%s",
                           TR("There is work that was not saved."));
        ImGui::Spacing();

        // Diz O QUE se perde, com numeros. "Alteracoes nao salvas" nao ajuda
        // a decidir; "3 scripts e 37 celulas" ajuda.
        if (const std::size_t editors = unsaved_documents(); editors > 0) {
            ImGui::BulletText(TR("%zu object editor(s) with unsaved changes"), editors);
        }
        if (const std::size_t cells = pending_cell_edits(); cells > 0) {
            ImGui::BulletText(TR("%zu cell edit(s) not written to the database"),
                              cells);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // "Cancelar" e' o padrao: Enter e Esc ficam na saida SEGURA. Sair e'
        // o botao que exige mira.
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            confirm_quit_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, col(colors().error));
        if (ImGui::Button(TR("Exit and discard"), ImVec2(160, 0))) {
            confirm_quit_ = false;
            wants_quit_   = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

SqlDocument* MainShell::active_document() {
    if (documents_.empty()) return nullptr;
    active_document_ = std::min(active_document_, documents_.size() - 1);
    return documents_[active_document_].get();
}

// Gera sugestoes de completion (ADR 0004), sobre metadados reais e com o
// escopo sintatico do otter_sql.
// Aplica ao editor as preferencias de completar codigo da conexao do
// documento (pagina "Completar código" do dialogo).
//
// A cada quadro, e nao uma vez: a AutoCompleteConfig e' compartilhada por
// todos os editores, e trocar de aba precisa trocar as opcoes junto -- senao
// a aba do banco legado herdaria as do novo.
// Preferencias do editor da conexao do documento.
//
// Devolve uma referencia a um PADRAO estatico quando a conexao nao existe --
// devolver por valor faria uma copia por celula da grade, e por ponteiro
// obrigaria cada ponto de uso a testar nulo.
const db::EditorOptions& MainShell::editor_options_for(
    const SqlDocument& document) const {
    static const db::EditorOptions kDefaults;

    const Connection* owner = connection_by_id(document.connection_id());
    return owner != nullptr ? owner->profile.editor : kDefaults;
}

void MainShell::apply_completion_options(SqlDocument& document) {
    const Connection* owner = connection_by_id(document.connection_id());
    if (owner == nullptr) return;

    const db::EditorOptions& editor = owner->profile.editor;

    // Opcoes do editor de texto, aplicadas ao TextEditor DESTE documento --
    // cada aba tem o seu, ao contrario da AutoCompleteConfig, que e'
    // compartilhada.
    TextEditor& text = document.editor();
    text.SetTabSize(static_cast<std::size_t>(editor.tab_size));
    text.SetAutoIndentEnabled(editor.auto_indent);
    text.SetShowLineNumbersEnabled(editor.show_line_numbers);
    text.SetShowMatchingBrackets(editor.show_matching_brackets);
    text.SetShowWhitespacesEnabled(editor.show_whitespace);
    text.SetWordWrapEnabled(editor.word_wrap);

    // A dobra LIGA os parenteses correspondentes sozinha (e' o que ela usa
    // para achar o bloco). Aplicada por ultimo para nao ser desfeita pelo
    // SetShowMatchingBrackets acima, que faz o inverso: desligar os
    // parenteses desliga a dobra.
    text.SetLineFoldingEnabled(editor.code_folding &&
                               editor.show_matching_brackets);

    autocomplete_config_->triggerOnTyping   = editor.complete_on_typing;
    autocomplete_config_->triggerInComments = editor.complete_in_comments;
    autocomplete_config_->triggerInStrings  = editor.complete_in_strings;
    autocomplete_config_->autoInsertSingleSuggestions = editor.auto_insert_single;
    autocomplete_config_->triggerDelay =
        std::chrono::milliseconds(editor.complete_delay_ms);
}

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

    // Inferencia de JOIN pelas chaves estrangeiras.
    //
    // Com duas tabelas na query ligadas por FK, sugere a condicao INTEIRA
    // ("pedido.cliente_id = cliente.id") em primeiro lugar, em vez de exigir
    // que o usuario lembre qual coluna referencia qual.
    //
    // As FKs ja' estavam carregadas -- o Navigator as usa para desenhar o no'
    // "Chaves estrangeiras" --, mas nada as lia aqui.
    //
    // So' no contexto de coluna, que e' onde o ON cai: sugerir uma igualdade
    // depois de SELECT ou FROM seria ruido.
    if (want_columns && qualified_table.empty() && scope.tables.size() >= 2) {
        // O lado esquerdo usa o ALIAS quando ha' um: quem escreveu
        // "FROM pedido p" espera "p.cliente_id", e o nome cheio nao compila
        // em alguns dialetos depois de declarado o alias.
        auto label_for = [&scope](std::string_view table) {
            for (const sql::TableRef& ref : scope.tables) {
                if (iequals(ref.name, table)) {
                    return ref.alias.empty() ? ref.name : ref.alias;
                }
            }
            return std::string(table);
        };

        for (const db::ForeignKeyMeta& fk : session().foreign_keys()) {
            // As DUAS pontas precisam estar na query: sugerir um JOIN com
            // uma tabela que nao esta' no FROM produziria SQL invalido.
            if (!in_scope(fk.source_table) || !in_scope(fk.target_table)) {
                continue;
            }

            const std::string condition =
                label_for(fk.source_table) + "." + fk.source_column + " = " +
                label_for(fk.target_table) + "." + fk.target_column;

            // Filtra pelo termo digitado como qualquer outro candidato: um
            // push_back direto faria a sugestao ficar na lista mesmo depois
            // de o usuario digitar algo que nao casa com ela.
            if (!matches(condition)) continue;

            // Rank -1: acima de tudo, inclusive das colunas de tabela em
            // escopo (rank 0). Quando ha' uma FK ligando as duas tabelas, e'
            // quase sempre o que se quer escrever.
            candidates.push_back({condition + pad_to(condition, 46) +
                                      std::string(TR("foreign key")),
                                  -1});
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
                // Passa por TR(): era o literal "tabela", que aparecia em
                // portugues com a interface em ingles (diretriz 8).
                const char* label =
                    table.kind == db::ObjKind::view ? TR("view") : TR("table");
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
    // Do documento aberto, nao da conexao selecionada no Raft. Eram coisas
    // diferentes desde que uma segunda conexao pode existir: com uma aba de
    // MySQL em foco e o PostgreSQL selecionado na lista, o realce tratava a
    // crase como texto e `#` como operador, e o divisor de script quebrava no
    // lugar errado.
    const MainShell* self = this;
    if (const SqlDocument* document =
            const_cast<MainShell*>(self)->active_document()) {
        if (const Connection* connection =
                connection_by_id(document->connection_id())) {
            return sql::dialect_for(connection->profile.driver_id);
        }
    }

    const std::string& driver_id =
        active_connection_ < connections_.size()
            ? connections_[active_connection_].profile.driver_id
            : active_profile_.driver_id;
    return sql::dialect_for(driver_id);
}

void MainShell::execute_current_sql() {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    // Pela sessao e pelo dialeto do DOCUMENTO, e so' a instrucao sob o
    // cursor (ou a selecao) -- ver run_sql e SqlDocument::sql_to_execute.
    run_sql(*document, document->sql_to_execute(document_dialect(*document)),
            RunMode::same_tab);
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

    // Ja' aberto: traz a aba para a frente. Duas abas do mesmo arquivo
    // gravariam uma por cima da outra, cada uma com o seu texto.
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i]->is_object()) continue;
        std::error_code ec;
        if (documents_[i]->file_path().empty() ||
            !std::filesystem::equivalent(documents_[i]->file_path(), *path, ec)) {
            continue;
        }
        active_document_    = i;
        select_document_id_ = documents_[i]->id();
        focus_document_id_  = documents_[i]->id();
        focus_editor_       = true;
        return;
    }

    // Um script fechado da pasta `.script`: volta com a conexao que tinha.
    for (const ScriptEntry& closed : closed_scripts_) {
        std::error_code ec;
        if (!std::filesystem::equivalent(script_path(scripts_dir_, closed.file), *path, ec)) {
            continue;
        }
        const ScriptEntry entry = closed;   // open_stored_script mexe na lista
        if (const SqlDocument* document = open_stored_script(entry)) {
            focus_document_id_ = document->id();
            focus_editor_      = true;
        }
        return;
    }

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
    // O arquivo e' de fora da pasta de scripts: continua onde esta', e a
    // gravacao automatica escreve NELE -- copia-lo para `.script` criaria
    // duas versoes do mesmo script.
    document.autosave().on_disk = true;
}

void MainShell::save_script_file(bool save_as) {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    if (document->is_object()) return;

    // "Save" e' a gravacao automatica adiantada: o script ja' tem o lugar
    // dele em `.script` (ou e' um arquivo aberto de fora), e nao ha' o que
    // perguntar. So' "Save as" abre o dialogo.
    if (!save_as) {
        if (save_script_now(*document) && !document->file_path().empty() &&
            document->autosave().on_disk) {
            document->set_status(
                std::string(TRF("saved to %s", document->file_path().c_str())));
        }
        return;
    }

    // Sugere o titulo da aba com .sql: "Script-2" vira "Script-2.sql".
    const auto chosen = save_file_dialog(
        TR("Save script"), sql_filters(), document->title() + ".sql");
    if (!chosen) return;
    const std::string path = *chosen;

    if (!write_script(path, document->editor().GetText())) {
        document->set_status(std::string(TRF("cannot write %s", path.c_str())));
        return;
    }

    // "Salvar como" MUDA o script de lugar: a copia automatica em `.script`
    // sai, senao ficariam dois arquivos e so' um deles acompanharia a
    // digitacao.
    const std::string previous = document->file_path();
    std::error_code ec;
    if (!previous.empty() && is_stored_script(scripts_dir_, previous) &&
        !std::filesystem::equivalent(previous, path, ec)) {
        std::filesystem::remove(previous, ec);
    }

    document->set_file_path(path);
    document->autosave().on_disk = true;
    document->autosave().pending = false;

    // O ponto de salvamento e' o que faz o indicador de modificado funcionar.
    // Sem "salvar", ele aparecia na primeira edicao e nunca mais saia -- era
    // o defeito 4 de docs/ELEMENTS.md.
    document->mark_saved();
    document->set_status(std::string(TRF("saved to %s", path.c_str())));
}

void MainShell::explain_current_sql(bool analyze) {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    // Pela sessao do documento, como a execucao: explicar o plano na base
    // errada daria uma arvore que nao corresponde a nada.
    Session& target = session_for(*document);
    if (target.state() != SessionState::connected || target.busy()) return;

    std::string sql = document->sql_to_execute(document_dialect(*document));
    if (sql.empty()) return;

    plan_analyze_ = analyze;
    show_plan_    = true;
    plan_.reset();

    target.explain_async(std::move(sql), analyze);
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
        char cost[64];
        std::snprintf(cost, sizeof cost, "%.2f..%.2f", node.startup_cost,
                      node.total_cost);

        // Os rotulos do plano sao os do EXPLAIN: quem le um plano procura por
        // "Index Cond" e "Filter", nao por uma traducao deles.
        Hint(node.type)
            .accent(TR("Relation"), node.relation)
            .row("Index Cond", node.index_condition)
            .row("Join", node.join_condition)
            .row("Filter", node.filter)
            .row("Sort", node.sort_keys)
            .row(TR("Cost"), cost)
            .row(TR("Width"), std::to_string(node.row_width))
            .row(TR("Loops"), node.loops > 1 ? std::to_string(node.loops)
                                             : std::string{})
            .show();
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

    // As opcoes vem do PERFIL da conexao do documento (pagina "Formatação"
    // do dialogo). Antes eram os valores padrao de FormatOptions, fixos no
    // codigo -- Ctrl+Shift+F sempre formatava em MAIUSCULAS, estilo rio.
    sql::FormatOptions options;
    if (const Connection* owner = connection_by_id(document->connection_id())) {
        const db::EditorOptions& editor = owner->profile.editor;
        options.keyword_case =
            editor.keyword_case == 0 ? sql::KeywordCase::preserve
            : editor.keyword_case == 2 ? sql::KeywordCase::lower
                                       : sql::KeywordCase::upper;
        options.indent_width      = editor.indent_width;
        options.river_style       = editor.river_style;
        options.wrap_select_after = editor.wrap_select_after;
    }

    const std::string after =
        sql::format_sql(before, active_dialect(), options);

    // Texto igual: nao mexe. SetText move o cursor para o inicio e cria um
    // ponto de desfazer -- fazer isso quando nada mudou seria ruido.
    if (after == before) return;

    document->editor().SetText(after);
}

void MainShell::execute_script() {
    SqlDocument* document = active_document();
    if (document == nullptr) return;

    // O divisor, os comandos de cliente (@set, @echo) e a expansao de
    // variaveis estao em run_script.
    run_script(*document, 0, /*separate_tabs=*/false);
}

// Conta o resultado inteiro -- o `resultset.count` do DBeaver.
//
// Sob demanda, disparada pelo "+" ao lado do intervalo de linhas. Automatica
// seria o oposto do que a paginacao existe para fazer: outra varredura
// completa a cada consulta (ADR 0011).
void MainShell::count_total_rows(SqlDocument& document) {
    if (document.paged_sql().empty()) return;

    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;

    const Connection* owner = connection_by_id(document.connection_id());
    const sql::Dialect& dialect =
        owner != nullptr ? sql::dialect_for(owner->profile.driver_id)
                         : active_dialect();

    const sql::PagedQuery counted =
        sql::make_count_query(document.paged_sql(), dialect, document.filter());
    if (!counted.rewritten) return;

    // Marca ESTE documento como o que espera uma contagem. O resultado chega
    // pelo mesmo canal da grade, e sem a marca ele substituiria as linhas
    // exibidas por uma celula com o numero.
    counting_document_id_ = document.id();
    document.set_executing(true);

    target.execute_async(counted.sql);
}

void MainShell::execute_page(SqlDocument& document, std::size_t page) {
    // Caminho por onde TODA execucao passa -- e' aqui que a conexao errada
    // fazia mais estrago.
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;
    if (document.paged_sql().empty()) return;

    // A reescrita com LIMIT/OFFSET impede que um SELECT sem limite trave a UI
    // ate' o servidor terminar de enviar tudo (ADR 0011). Quando nao e' seguro
    // reescrever, executa o original: rodar algo diferente do que o usuario
    // escreveu seria pior que a espera.
    // Dialeto do documento: a reescrita com LIMIT/OFFSET cita identificadores,
    // e citar com aspas o que o MySQL espera entre crases faria o servidor
    // rejeitar a consulta.
    const Connection* owner = connection_by_id(document.connection_id());
    const sql::Dialect& dialect =
        owner != nullptr ? sql::dialect_for(owner->profile.driver_id)
                         : active_dialect();

    // O tamanho da pagina vem do perfil (pagina "Processamento SQL"). Era o
    // kDefaultPageSize fixo -- 200 e' bom num banco local e caro num
    // servidor distante, e a resposta certa depende da latencia.
    const std::size_t page_size =
        owner != nullptr ? static_cast<std::size_t>(owner->profile.editor.page_size)
                         : sql::kDefaultPageSize;

    const sql::PagedQuery paged = sql::make_paged_query(
        document.paged_sql(), dialect, page,
        page_size, document.sort(), document.filter());

    // Guardado no documento: a colheita do resultado precisa do MESMO valor
    // para saber onde cortar a linha-sonda, e reler do perfil la' daria o
    // numero errado se o usuario mudasse a opcao durante a consulta.
    document.set_page_size(page_size);

    document.set_page(page);
    document.set_paged(paged.rewritten);
    document.set_has_more(false);

    executing_document_id_ = document.id();
    document.set_executing(true);
    document.set_status({});

    target.execute_async(paged.sql);
}

void MainShell::draw() {
    // O dialeto do SQL gerado (aspas, e qual conjunto de geradores -- ver
    // db/object_info.cpp) e' o da conexao EM USO, decidido a cada quadro.
    //
    // Era definido uma vez, ao conectar, e ficava com o da ULTIMA conexao
    // aberta: com um PostgreSQL e um MySQL lado a lado, o menu de uma tabela
    // do MySQL gerava `ALTER TABLE "t"`, que o servidor recusa.
    if (active_connection_ < connections_.size()) {
        db::set_sql_dialect_for(connections_[active_connection_].profile.driver_id);
    }
    // A navegacao por teclado do ImGui consome as setas dentro do NewFrame --
    // ANTES de qualquer codigo nosso rodar. Desliga-la no meio do quadro nao
    // adianta: a tecla ja' foi consumida.
    //
    // Por isso a decisao e' aplicada AQUI, no inicio do quadro, com base no
    // que a grade observou no quadro ANTERIOR. Um quadro de atraso e' de
    // 16 ms e nao se percebe; sem ele, as setas da grade nunca funcionam.
    ImGui::GetIO().ConfigFlags =
        grid_owns_arrows_
            ? (ImGui::GetIO().ConfigFlags & ~ImGuiConfigFlags_NavEnableKeyboard)
            : (ImGui::GetIO().ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard);

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

        const bool failed = session().last_script_failed();

        // O editor de objeto segue o objeto renomeado e limpa as edicoes
        // pendentes -- ou mostra o erro do servidor, se o comando falhou.
        finish_object_ddl(failed);

        if (!failed) {
            // A LISTA de objetos tambem: so' invalidar a tabela nao fazia uma
            // tabela recem-criada aparecer, nem a removida sumir -- era
            // preciso reconectar para ver o efeito do proprio comando.
            session().reload_catalog_async();
        }
        ddl_reload_schema_.clear();
        ddl_reload_table_.clear();
    }

    // A sessao do documento que executou, nao a ativa: o usuario pode ter
    // clicado noutra conexao enquanto a consulta corria, e colher o
    // resultado da sessao errada misturaria as duas grades.
    // Contagem sob demanda: colhida ANTES do bloco da grade, senao o
    // resultado de uma celula (o COUNT) substituiria as linhas exibidas.
    if (counting_document_id_ != 0) {
        for (auto& document : documents_) {
            if (document->id() != counting_document_id_) continue;

            Session& counter = session_for(*document);
            if (counter.busy()) break;

            if (auto result = counter.take_result();
                result.has_value() && result->row_count() > 0 &&
                result->column_count() > 0) {
                // COUNT(*) devolve texto pelo protocolo; converter aqui
                // evita depender do tipo que cada driver reporta.
                const std::string text(result->text(0, 0));

                // "Select row count": o numero vai para o aviso, e NAO para
                // o total da grade -- a consulta contada pode nao ser a que
                // esta' exibida.
                if (count_to_toast_) {
                    show_toast(std::string(TRF("Row count: %s", text.c_str())));
                } else
                try {
                    // A contagem e' da aba que a pediu, nao da que estiver
                    // a' vista agora.
                    const std::size_t shown = document->active_result_tab_id();
                    document->select_result_tab_by_id(document->executing_tab_id());
                    document->set_total_rows(
                        static_cast<std::size_t>(std::stoull(text)));
                    document->select_result_tab_by_id(shown);
                } catch (const std::exception&) {
                    // Numero ilegivel: deixa sem total, e o "+" volta. Um
                    // numero errado seria pior que a ausencia dele.
                }
            }

            document->set_executing(false);
            counting_document_id_ = 0;
            count_to_toast_       = false;
            break;
        }
    }

    // "Filter by value": os distintos da coluna chegaram. Mesmo canal da
    // grade, e pela mesma razao da contagem e' colhido ANTES dela.
    if (distinct_document_id_ != 0) {
        SqlDocument* asker = nullptr;
        for (auto& document : documents_) {
            if (document->id() == distinct_document_id_) asker = document.get();
        }
        if (asker == nullptr) {
            distinct_document_id_  = 0;
            grid_distinct_loading_ = false;
        } else if (!session_for(*asker).busy()) {
            grid_distinct_.clear();
            if (auto result = session_for(*asker).take_result();
                result.has_value() && result->column_count() >= 2) {
                for (std::size_t r = 0; r < result->row_count(); ++r) {
                    db::DistinctValue value;
                    value.is_null = result->is_null(r, 0);
                    if (!value.is_null) value.text = std::string(result->text(r, 0));
                    value.count = static_cast<std::size_t>(
                        std::strtoull(std::string(result->text(r, 1)).c_str(),
                                      nullptr, 10));
                    grid_distinct_.push_back(std::move(value));
                }
            } else if (asker->result().has_value()) {
                // A consulta dos distintos falhou (coluna de tipo sem
                // igualdade, por exemplo): os das linhas carregadas, dizendo
                // que sao so' esses.
                grid_distinct_ = db::distinct_values(*asker->result(),
                                                     grid_distinct_column_);
                grid_distinct_partial_ = true;
            }
            grid_distinct_loading_ = false;
            asker->set_executing(false);
            distinct_document_id_ = 0;
        }
    }

    // "Apply and commit": o COMMIT terminou; agora sim a releitura.
    if (reread_after_commit_ != 0) {
        SqlDocument* saved = nullptr;
        for (auto& document : documents_) {
            if (document->id() == reread_after_commit_) saved = document.get();
        }
        if (saved == nullptr) {
            reread_after_commit_ = 0;
        } else if (!session_for(*saved).busy() && executing_document_id_ == 0) {
            reread_after_commit_ = 0;
            if (!saved->paged_sql().empty()) execute_page(*saved, saved->page());
        }
    }

    Session* runner = nullptr;
    if (executing_document_id_ != 0) {
        for (auto& document : documents_) {
            if (document->id() == executing_document_id_) {
                runner = &session_for(*document);
                break;
            }
        }
    }

    if (runner != nullptr && !runner->busy()) {
        Session& active_runner = *runner;
        for (auto& document : documents_) {
            if (document->id() != executing_document_id_) continue;

            // O resultado vai para a aba que o PEDIU. O usuario pode ter
            // trocado de aba de resultado enquanto a consulta rodava; a que
            // ele esta' olhando volta ao fim da colheita.
            const std::size_t shown_tab = document->active_result_tab_id();
            const bool rerouted =
                document->executing_tab_id() != shown_tab &&
                document->select_result_tab_by_id(document->executing_tab_id());

            // Gravacao de edicoes: o buffer so' e' limpo quando os UPDATE
            // passaram. Limpar antes de saber perderia o trabalho se a
            // transacao falhasse -- e o usuario nao teria como refaze-lo.
            if (document->edits().has_changes() && saving_edits_) {
                saving_edits_ = false;
                if (active_runner.last_script_failed()) commit_after_save_ = false;
                if (!active_runner.last_script_failed()) {
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
                    if (commit_after_save_) {
                        // "Apply and commit": o COMMIT primeiro, a releitura
                        // quando ele terminar (ver acima) -- os dois usam o
                        // mesmo worker.
                        commit_after_save_ = false;
                        active_runner.commit_async();
                        reread_after_commit_ = document->id();
                    } else if (!document->paged_sql().empty()) {
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

            if (auto fresh = active_runner.take_result()) {
                // A pagina pediu uma linha a mais do que mostra. Se ela veio,
                // ha' mais resultado adiante -- e ela nao pode aparecer na
                // grade, senao o usuario veria 201 linhas ao pedir 200.
                if (document->paged()) {
                    // O tamanho usado NESTA consulta, nao o padrao: o
                    // usuario pode ter mudado a opcao entre executar e
                    // colher, e cortar no numero errado esconderia linhas
                    // legitimas ou deixaria a sonda visivel.
                    const std::size_t size = document->page_size();
                    const bool more = fresh->row_count() > size;
                    document->set_has_more(more);
                    if (more) fresh->hide_rows_beyond(size);
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
                document->bar_rules().prepare(*document->result());
                recompute_pivot(*document);
                document->set_edit_target(
                    db::find_edit_target(*document->result(),
                                         active_runner.schemas()));

                // Chave ainda nao lida: pede o carregamento. O alvo e'
                // recalculado no quadro seguinte, quando as constraints
                // chegarem -- sem isto, um SELECT numa tabela nunca expandida
                // ficaria somente leitura sem motivo real.
                if (document->edit_target().refusal ==
                        db::EditRefusal::key_not_loaded &&
                    !document->edit_target().table.empty()) {
                    active_runner.load_constraints_async(
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

            // "Export from Query": a janela abre quando o resultado chega.
            if (export_after_run_ == document->id()) {
                export_after_run_ = 0;
                if (document->result().has_value()) show_export_ = true;
            }

            // A mensagem do worker conta as linhas que CHEGARAM, incluindo a
            // linha-sonda da paginacao. Refaz aqui, onde se sabe que ela
            // existe: a barra de status dizer "201 linhas" depois de mostrar
            // "linhas 1-200" e' contradicao na mesma tela.
            if (document->paged() && document->result().has_value()) {
                const db::ResultSet& rs = *document->result();
                document->set_status(TRF("%zu row(s), %zu column(s)",
                                         rs.row_count(), rs.column_count()));
            } else {
                document->set_status(active_runner.status_message());
            }
            document->set_executing(false);

            // A releitura apos gravar (execute_page, acima) ja' fixou a aba
            // de destino dela; so' entao a vista do usuario volta.
            if (rerouted) document->select_result_tab_by_id(shown_tab);
            break;
        }

        // Nao zera quando uma RELEITURA acabou de ser disparada: ela reusa
        // executing_document_id_, e apaga-lo aqui faria o resultado novo
        // chegar sem ninguem para colher.
        if (rereading_after_save_) rereading_after_save_ = false;
        else                       executing_document_id_ = 0;
    }

    // Comandos escolhidos em menu no quadro anterior, e a fila de "executar
    // em abas separadas".
    poll_command_file();
    run_queued_commands();
    process_run_queue();
    tick_connections();

    // Atalhos. Os do editor SQL vem do registro (ui/commands.hpp), com a
    // tecla do perfil ativo: os globais aqui, os de contexto `editor` e
    // `navigator` onde cada painel e' desenhado. Registrados, e nao so'
    // rotulados no menu -- um atalho anunciado que nao funciona e' pior que
    // nenhum.
    dispatch_shortcuts(CommandContext::global);

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
    } else if (!grid_focused_ &&
               ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        // Com a GRADE em foco, Ctrl+S e' "Apply changes" -- o contexto
        // `resultset.focused` do DBeaver. Sem o teste, gravar a linha
        // salvaria tambem o script.
        //
        // Num editor de objeto, Ctrl+S e' o "Save" dele: revisa o SQL das
        // propriedades alteradas. Nao ha' arquivo para gravar.
        SqlDocument* target_document = active_document();
        if (target_document != nullptr && target_document->is_object()) {
            if (target_document->modified()) {
                save_object_edits(
                    *target_document,
                    session_for(*target_document)
                        .object_info(target_document->object()->ref)
                        .info);
            }
        } else {
            save_script_file(/*save_as=*/false);
        }
    }
    // Ctrl+W fecha o script, nos dois perfis. "Novo script" saiu daqui: a
    // tecla depende do perfil (Ctrl+] no DBeaver, Ctrl+T no C-Otter).
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W)) {
        close_document(active_document_);
    }
    // Commit, rollback, auto-commit e nova conexao sairam daqui: sao comandos
    // da tabela (app_commit...), com a tecla de cada perfil -- no DBeaver,
    // Ctrl+Shift+C e' "Advanced copy" da grade, e commit e' Ctrl+Alt+Shift+K.

    // "Buscar a proxima pagina" e "buscar tudo" (Ctrl+Alt+N, Ctrl+Shift+=)
    // sairam daqui: sao comandos da tabela (ui/commands.cpp), despachados
    // acima com a tecla do perfil ativo.


    // Desfazer e refazer, GLOBAIS.
    //
    // O menu ja' os oferecia, mas o atalho vinha do widget: so' funcionava
    // com o editor em foco. Clicar na grade e apertar Ctrl+Z nao fazia nada,
    // e o menu anunciava a tecla mesmo assim -- um atalho anunciado que nao
    // funciona e' pior que nenhum.
    //
    // O editor tambem le' estas teclas quando tem foco. Nao ha' duplicidade:
    // `ImGui::GetIO().WantTextInput` e' verdadeiro justamente quando ele esta'
    // consumindo teclado, e aqui a leitura e' pulada nesse caso.
    if (!ImGui::GetIO().WantTextInput && !grid_focused_) {
        if (SqlDocument* document = active_document()) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z) &&
                document->editor().CanUndo()) {
                document->editor().Undo();
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) &&
                document->editor().CanRedo()) {
                document->editor().Redo();
            }
        }
    }

    draw_menu_bar();
    draw_toolbar();
    draw_dockspace();

    tree_previous_active_ = active_connection_;
    draw_navigator_panel();
    draw_editor_panel();

    // Depois dos editores: o que foi digitado neste quadro ja' conta.
    autosave_scripts();

    // "Toggle results panel" esconde a parte de baixo inteira -- resultado e
    // os paineis que moram com ele --, e o editor ocupa o espaco.
    // Com um EDITOR DE OBJETO na frente, a parte de baixo tambem some: os
    // dados dele estao na aba Data, e o editor do DBeaver ocupa a altura toda.
    // Metade da janela dizendo "os dados estao na outra aba" seria espaco
    // tirado justamente das colunas e do DDL.
    const SqlDocument* front_document = active_document();
    const bool object_in_front = front_document != nullptr && front_document->is_object();
    if (bottom_hidden_for_object_ && !object_in_front && !results_hidden_) {
        reselect_result_ = 2;   // ao voltar, a aba "Result" e' a da frente
    }
    bottom_hidden_for_object_ = object_in_front;

    if (!results_hidden_ && !object_in_front) {
        if (reselect_result_ > 0 && --reselect_result_ == 0) {
            ImGui::SetWindowFocus("###ResultPanel");
            focus_editor_ = true;   // o teclado continua no texto
        }
        draw_grid_panel();
        if (show_log_) draw_query_log_panel();
        draw_output_panel();
        draw_variables_panel();
        draw_outline_panel();
    }
    draw_terminal_panel();
    draw_editor_extras();
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
    draw_object_forms();
    draw_import_data_window();
    draw_tool_windows();
    draw_app_windows();
    draw_value_panel();
    draw_grid_panels();
    draw_filter_settings();

    if (show_about_) draw_about_window();
    if (show_plan_) draw_plan_window();
    // Fechada (pelo X ou por concluir): repoe a marca, para a proxima
    // abertura voltar a pegar os padroes da conexao.
    if (show_export_) draw_export_window();
    else              export_defaults_applied_ = false;
    if (show_import_) draw_import_window();
    if (show_icons_) draw_icon_gallery();
    if (show_demo_)  ImGui::ShowDemoWindow(&show_demo_);

    draw_rename_tab();
    draw_goto_dialog();
    draw_fetch_all_confirm();
    draw_save_confirm();

    // Por ultimo: e' modal, e precisa ficar por cima de tudo que veio antes.
    draw_quit_confirm();
    draw_close_tab_confirm();
    draw_erase_connection_confirm();
    draw_auth_prompt();
}

// Renomear a aba de script.
//
// Fora do menu de contexto de proposito: um menu se fecha ao primeiro clique
// fora dele, e um campo de texto precisa sobreviver a varios -- e' a mesma
// razao dos formularios de DDL.
void MainShell::draw_rename_tab() {
    if (renaming_document_ == 0) return;

    constexpr const char* kPopup = "###RenameTab";
    ImGui::OpenPopup(kPopup);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(TRW("Rename tab", "###RenameTab"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        // Foco no campo ao abrir: renomear e' digitar, e obrigar um clique
        // no campo antes seria atrito num dialogo de um campo so'.
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

        ImGui::SetNextItemWidth(320);
        const bool entered = ImGui::InputText(
            "##rename", rename_buffer_, sizeof rename_buffer_,
            ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::TextColored(col4(colors().text_dim), "%s",
                           TR("Empty restores the default name."));
        ImGui::Spacing();

        const bool confirmed = entered || ImGui::Button(TR("OK"), ImVec2(100, 0));

        if (confirmed) {
            for (std::unique_ptr<SqlDocument>& document : documents_) {
                if (document->id() != renaming_document_) continue;
                // Vazio LIMPA o titulo: title() volta a derivar "Script N" ou
                // o nome do arquivo. Guardar a string vazia como titulo
                // deixaria a aba sem rotulo nenhum.
                document->set_title(rename_buffer_);
                break;
            }
            renaming_document_ = 0;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            renaming_document_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Executa a consulta SEM a reescrita de paginacao. Ver o comentario do
// atalho Ctrl+Shift+= para a razao de existir um caminho que contraria o
// ADR 0011.
void MainShell::fetch_all_rows(SqlDocument& document) {
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;
    if (document.paged_sql().empty()) return;

    // A ordenacao e o filtro escolhidos precisam sobreviver: sao parte do que
    // esta' na tela. So' o LIMIT/OFFSET e' que sai.
    const Connection* owner = connection_by_id(document.connection_id());
    const sql::Dialect& dialect =
        owner != nullptr ? sql::dialect_for(owner->profile.driver_id)
                         : active_dialect();

    const sql::PagedQuery paged = sql::make_unpaged_query(
        document.paged_sql(), dialect, document.sort(), document.filter());

    document.set_page(0);
    document.set_paged(false);   // deixa de ser pagina: a barra para de dizer "1-200 de"
    document.set_has_more(false);

    executing_document_id_ = document.id();
    document.set_executing(true);
    document.set_status({});

    target.execute_async(paged.sql);
}

void MainShell::draw_fetch_all_confirm() {
    if (confirm_fetch_all_ == 0) return;

    SqlDocument* document = nullptr;
    for (std::unique_ptr<SqlDocument>& d : documents_) {
        if (d->id() == confirm_fetch_all_) { document = d.get(); break; }
    }
    if (document == nullptr) { confirm_fetch_all_ = 0; return; }

    constexpr const char* kPopup = "###FetchAll";
    ImGui::OpenPopup(kPopup);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(TRW("Fetch all rows", "###FetchAll"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::size_t total = document->total_rows().value_or(0);

        ImGui::Text(TR("This result has %zu rows."), total);
        ImGui::TextColored(col4(colors().text_dim), "%s",
                           TR("Fetching all of them uses memory and may take "
                              "a while. You can cancel from the status bar."));
        ImGui::Spacing();
        // Exportar e' o caminho recomendado pelo ADR 0011 para varrer a
        // tabela; oferece-lo aqui evita que a espera seja a unica saida.
        ImGui::TextColored(col4(colors().text_dim), "%s",
                           TR("To save them to a file, use Export instead."));
        ImGui::Spacing();

        if (ImGui::Button(TR("Fetch all"), ImVec2(120, 0))) {
            fetch_all_rows(*document);
            confirm_fetch_all_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            confirm_fetch_all_ = 0;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Ir para a linha ou a coluna (resultset.grid.gotoRow / gotoColumn).
//
// Numerado a partir de 1 na tela, como a barra de status ja' mostra -- a
// primeira linha e' "1", nao "0". O indice interno continua base zero.
void MainShell::draw_goto_dialog() {
    if (!goto_open_) return;

    SqlDocument* document = active_document();
    if (document == nullptr || !document->result().has_value()) {
        goto_open_ = false;
        return;
    }
    const db::ResultSet& rs = *document->result();

    const bool by_row = goto_kind_ == GotoKind::row;
    const std::size_t count = by_row ? rs.row_count() : rs.column_count();
    if (count == 0) { goto_open_ = false; return; }

    constexpr const char* kPopup = "###GotoCell";
    ImGui::OpenPopup(kPopup);

    const ImGuiViewport* goto_vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(goto_vp->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(by_row ? TRW("Go to row", "###GotoCell")
                                      : TRW("Go to column", "###GotoCell"),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();

        ImGui::SetNextItemWidth(220);
        // CharsDecimal: uma letra aqui nao tem leitura possivel, e recusar a
        // digitacao diz isso antes de o usuario apertar OK.
        const bool entered = ImGui::InputText(
            "##goto", goto_buffer_, sizeof goto_buffer_,
            ImGuiInputTextFlags_CharsDecimal |
                ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::TextColored(col4(colors().text_dim), TR("1 to %zu"), count);

        // Na coluna, o numero sozinho nao diz para onde se vai: mostrar o
        // NOME confirma o destino antes do salto.
        if (!by_row && goto_buffer_[0] != '\0') {
            const auto typed = std::strtoull(goto_buffer_, nullptr, 10);
            if (typed >= 1 && typed <= count) {
                ImGui::TextColored(
                    col4(colors().accent_light), "%s",
                    rs.column(static_cast<std::size_t>(typed - 1))
                        .info().name.c_str());
            }
        }

        ImGui::Spacing();
        const bool confirmed = entered || ImGui::Button(TR("OK"), ImVec2(100, 0));

        if (confirmed) {
            const auto typed = std::strtoull(goto_buffer_, nullptr, 10);
            if (typed >= 1 && typed <= count) {
                // Fora da grade nao ha' selecao; criar uma aqui e' o que faz
                // o salto ser visivel.
                if (!has_selection_ || selected_document_ != document->id()) {
                    selected_document_ = document->id();
                    selected_row_      = 0;
                    selected_column_   = 0;
                    has_selection_     = true;
                }
                const auto index = static_cast<std::size_t>(typed - 1);
                if (by_row) selected_row_    = index;
                else        selected_column_ = index;
                // O bloco volta a ser uma celula: o destino do salto.
                anchor_row_    = selected_row_;
                anchor_column_ = selected_column_;
                scroll_to_selection_ = true;
                goto_open_ = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(100, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            goto_open_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
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

        // "Toggle editor layout": resultado embaixo (padrao) ou ao lado.
        //
        // O no' NOVO do corte e' o do RESULTADO; o que sobra -- e que herda o
        // papel de no' central do dockspace -- e' o do editor. Era o inverso,
        // e o no' central nunca se recolhe: com o resultado escondido
        // ("Toggle results panel", ou um editor de objeto na frente) a
        // metade de baixo ficava vazia em vez de ir para o editor.
        ImGuiID center_top = 0, center_bottom = 0;
        ImGui::DockBuilderSplitNode(center,
                                    side_by_side_ ? ImGuiDir_Right : ImGuiDir_Down,
                                    side_by_side_ ? 0.5f : 0.58f,
                                    &center_bottom, &center_top);

        // Uma arvore so' na lateral (ADR 0018). Eram dois paineis, Raft em
        // cima e Navigator embaixo; o DBeaver tem um.
        ImGui::DockBuilderDockWindow("###NavigatorPanel", left);
        ImGui::DockBuilderDockWindow("###ResultPanel",    center_bottom);
        ImGui::DockBuilderDockWindow("###QueriesPanel",   center_bottom);
        ImGui::DockBuilderFinish(dock_id);

        // Guardado para ancorar a janela de cada conexao NOVA no mesmo lugar
        // onde ficava a antiga "SQL". Sem isto, conectar a uma segunda base
        // faria a janela dela nascer flutuando no meio da tela.
        editor_dock_id_ = center_top;
        result_dock_id_ = center_bottom;
    }

    // A faixa das abas de um no' ancorado usa a cor de TITULO ativo quando a
    // janela dele tem o foco -- e essa cor (ambar a 40%) e' mais clara que a
    // aba selecionada: a aba virava um buraco escuro com uma faixa por cima
    // (relato do usuario, 2026-10-01). Aqui a faixa fica so' um tom acima do
    // fundo; quem marca o foco e' a ABA, que e' o que se le'. As barras de
    // titulo das janelas flutuantes (dialogos) nao passam por aqui e
    // continuam com o destaque.
    const Palette& palette = colors();
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,
                          col(mix(palette.bg_darkest, palette.accent, 0.10f)));
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::PopStyleColor();
    ImGui::End();
}

void MainShell::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu(TR("File"))) {
        command_menu_item(Command::app_new_connection);
        command_menu_item(Command::app_new_connection_url);
        if (ImGui::MenuItem(TR("Edit connection..."), nullptr, false,
                            session().state() == SessionState::connected)) {
            connection_dialog_.open_edit(active_profile_);
        }
        if (ImGui::MenuItem(TR("Import connections..."))) {
            show_import_ = true;
            import_scanned_ = false;
            import_status_.clear();
        }
        ImGui::Separator();
        // Do registro: a tecla ao lado e' a do perfil de atalhos ativo.
        command_menu_item(Command::new_script);
        command_menu_item(Command::open_script);
        if (ImGui::MenuItem(TR("Save script"), "Ctrl+S", false,
                            active_document() != nullptr)) {
            save_script_file(/*save_as=*/false);
        }
        if (ImGui::MenuItem(TR("Save script as..."), "Ctrl+Shift+S", false,
                            active_document() != nullptr)) {
            save_script_file(/*save_as=*/true);
        }
        if (ImGui::MenuItem(TR("Close tab"), "Ctrl+W",
                            false, !documents_.empty())) {
            close_document(active_document_);
        }
        command_menu_item(Command::edit_open_local_file);
        command_menu_item(Command::app_script_associate);
        command_menu_item(Command::app_show_in_explorer);
        command_menu_item(Command::app_disconnect);
        ImGui::Separator();
        if (ImGui::MenuItem(TR("Exit"), "Alt+F4")) request_quit();
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

    draw_navigate_menu();

    // "SQL Editor", como no DBeaver (`SQLEditorMenu`). Os tres comandos de
    // transacao ficam no fim: la' moram no menu Database.
    if (ImGui::BeginMenu(TR("SQL Editor"))) {
        const bool can_run = session().state() == SessionState::connected &&
                             !session().busy();
        draw_sql_editor_menu();

        ImGui::Separator();
        if (ImGui::BeginMenu(TR("Format"))) {
            command_menu_item(Command::format);
            command_menu_item(Command::morph_delimited);
            command_menu_item(Command::comment_single);
            command_menu_item(Command::comment_block);
            command_menu_item(Command::word_wrap);
            ImGui::EndMenu();
        }
        ImGui::Separator();

        const bool auto_commit = session().auto_commit();
        const bool in_txn = session().txn_state() != db::TxnState::idle;

        (void)can_run;
        (void)auto_commit;
        (void)in_txn;
        command_menu_item(Command::app_auto_commit);
        command_menu_item(Command::app_commit);
        command_menu_item(Command::app_rollback);
        command_menu_item(Command::run_script_native);
        command_menu_item(Command::ddl_by_result);
        ImGui::EndMenu();
    }

    draw_database_menu();
    draw_window_menu();

    if (ImGui::BeginMenu(TR("Help"))) {
        command_menu_item(Command::app_help);
        command_menu_item(Command::app_collect_diagnostics);
        command_menu_item(Command::app_clear_history);
        command_menu_item(Command::app_reset_settings);
        ImGui::Separator();
        // Seletor de tema: troca em tempo real, sem reiniciar.
        if (ImGui::BeginMenu(TR("Theme"))) {
            const std::string active_theme = current_theme().id;
            for (const Theme& theme : available_themes()) {
                const bool selected = active_theme == theme.id;
                if (ImGui::MenuItem(TR(theme.name.c_str()), nullptr, selected)) {
                    set_theme(theme.id);
                    // Lembrado para a proxima execucao: era esquecido ao
                    // fechar.
                    settings_.theme = std::string(theme.id);
                    save_settings();
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
                    settings_.language = language.code;
                    save_settings();
                }
            }
            ImGui::EndMenu();
        }

        // Perfil de atalhos e conjunto de icones: DBeaver ou C-Otter.
        draw_keymap_menu();
        draw_icon_set_menu();
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


bool MainShell::matches_filter(std::string_view name) const {
    // Filtro de objetos da conexao (core.object.filter.*): mascaras de
    // inclusao e exclusao, alem do campo de busca.
    if (!db::filter_accepts(object_filter_for(active_connection_), name)) return false;

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

// As contas do servidor, com os GRANTs de cada uma.
//
// Os grants são carregados por usuário, ao expandir: `SHOW GRANTS` é uma
// consulta por conta, e num servidor com 50 contas carregar tudo junto seriam
// 50 idas ao servidor para uma árvore que talvez nem seja aberta.
void MainShell::draw_users_folder() {
    const Palette& p = colors();
    const std::vector<db::UserMeta> users = session().users();

    folder_creates(db::ObjectType::role);
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

        // Conta bloqueada ou com senha expirada sai marcada: ela EXISTE, mas
        // não conecta -- e é essa a informação que importa ao olhar a lista.
        const bool usable = !user.locked && !user.expired;

        // Seta, icone, nome -- ver draw_folder_node. Aqui o icone vinha ANTES
        // da seta: a linha do usuario ficava um passo a' esquerda das pastas
        // irmas, com a seta entre o icone e o nome (visto na captura do SQL
        // Anywhere, onde a pasta Users fica ao lado de Roles).
        ImGui::BeginGroup();
        const bool open = ImGui::TreeNodeEx(
            "##user", ImGuiTreeNodeFlags_SpanAvailWidth |
                          (usable ? 0 : ImGuiTreeNodeFlags_Selected));
        const bool user_toggled = ImGui::IsItemToggledOpen();
        same_line_after_arrow();
        icon_inline(Icon::user, user.locked ? p.error : p.accent_light);
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(usable ? p.text : p.text_dim), "%s",
                           user.qualified().c_str());

        if (!usable) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.error), "%s",
                               user.locked ? TR("[locked]") : TR("[expired]"));
        }
        ImGui::EndGroup();
        if (open) ImGui::Indent();   // ver draw_relations_folder

        if (ImGui::IsItemHovered() && !user.plugin.empty()) {
            hint_fmt("%s", user.plugin.c_str());
        }

        // A conta como objeto: View User, Create New User, Rename, Delete,
        // Tools > Change password. O host vai em `parent` -- a conta e' o par.
        {
            db::ObjectRef ref;
            ref.type   = db::ObjectType::role;
            ref.name   = user.name;
            ref.parent = user.host;
            object_node(ref, user_toggled);
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
                ImGui::SameLine(0.0f, tree_label_gap());

                // O texto do GRANT é longo. Truncar na largura do painel e
                // mostrar o inteiro no tooltip é mais legível que quebrar em
                // três linhas cada um.
                ImGui::TextColored(col4(p.text), "%s", grant.c_str());
                ImGui::EndGroup();

                if (ImGui::IsItemHovered()) {
                    hint_fmt("%s", grant.c_str());
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
    // No SQL Anywhere as quatro listas sao propriedades e opcoes, com os nomes
    // que o Sybase Central lhes da'; engines, charsets e plugins sao do MySQL.
    const bool sa = session().is_sqlanywhere();

    if (!draw_folder_node(Icon::system_info, TR("System Info"), 0, false)) {
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
                hint_fmt("%s", variable.detail.c_str());
            }
        }
        ImGui::TreePop();
    };

    draw_variables(sa ? TR("Connection properties") : TR("Session status"), Icon::info,
                   session().session_status(), session().session_status_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::session_status); });

    draw_variables(sa ? TR("Server properties") : TR("Global status"), Icon::info,
                   session().global_status(), session().global_status_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::global_status); });

    draw_variables(sa ? TR("Connection options") : TR("Session variables"),
                   Icon::settings,
                   session().session_variables(),
                   session().session_variables_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::session_variables); });

    draw_variables(sa ? TR("Database properties") : TR("Global variables"),
                   Icon::settings,
                   session().global_variables(),
                   session().global_variables_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::global_variables); });

    if (sa) {
        // As opcoes gravadas no banco para PUBLIC: o padrao de toda conexao.
        draw_list_folder(Icon::setting, TR("Database options"),
                         db::CatalogList::settings, Icon::setting);
        ImGui::TreePop();
        return;
    }

    draw_variables(TR("Engines"), Icon::database,
                   session().engines(), session().engines_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::engines); });

    draw_variables(TR("Charsets"), Icon::data_type,
                   session().charsets(), session().charsets_loaded(),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::charsets); });

    // "User privileges" e "Plugins": os dois ultimos nos do System Info do
    // DBeaver para o MySQL.
    draw_variables(TR("User privileges"), Icon::grant,
                   session().server_info(Session::ServerInfo::privileges),
                   session().server_info_loaded(Session::ServerInfo::privileges),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::privileges); });

    draw_variables(TR("Plugins"), Icon::extension,
                   session().server_info(Session::ServerInfo::plugins),
                   session().server_info_loaded(Session::ServerInfo::plugins),
                   [this] { session().load_server_info_async(
                                Session::ServerInfo::plugins); });

    ImGui::TreePop();
}

// Pasta com contagem e um ícone. O número evita expandir só para descobrir que
// está vazio -- é o padrão do DBeaver (docs/NAVIGATOR-TREE.md).
bool MainShell::draw_folder_node(Icon icon, const char* label, std::size_t count,
                                 bool loaded) {
    // OTTER_EXPAND_TREE abre todas as pastas na captura de tela. Conferir os
    // icones de constraint, indice, FK e trigger exige chegar ate' o quarto
    // nivel da arvore, e clicar la' por automacao erra o alvo.
    static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
    if (expand_all) ImGui::SetNextItemOpen(true, ImGuiCond_Once);

    // "nav folder open|close <rotulo>" do canal de comandos: a primeira pasta
    // DESENHADA com esse rotulo (em ingles ou traduzido). E' como se abre
    // "Users" ou "Roles" para a captura sem o clique na seta.
    if (!tree_folder_request_.empty() &&
        (tree_folder_request_ == label || TR(tree_folder_request_.c_str()) == std::string_view(label))) {
        ImGui::SetNextItemOpen(tree_folder_request_open_, ImGuiCond_Always);
        tree_folder_request_.clear();
    }

    // SETA primeiro, ICONE depois -- a ordem do DBeaver, e de qualquer arvore
    // de sistema de arquivos.
    //
    // Era o inverso: o icone vinha antes do TreeNodeEx, e as setas de todos
    // os nos ficavam desalinhadas entre si, recuadas pela largura do icone.
    // Com a seta primeiro, todas as setas de um mesmo nivel se alinham, que
    // e' o que permite percorrer a arvore com o olho.
    //
    // O rotulo do no' e' vazio ("##id"): o texto e' desenhado depois do
    // icone, na mesma linha. Passar o label ao TreeNodeEx o poria ANTES do
    // icone, que e' justamente o que se quer evitar.
    const std::string node_id = std::string("##") + label;
    const bool open = ImGui::TreeNodeEx(
        node_id.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);

    // O menu da PASTA: "Create New <tipo>", como no DBeaver. Quem chama diz o
    // que a pasta cria (folder_creates) logo antes; o estado do no' e' lido
    // aqui porque o icone e o rotulo, desenhados depois, passam a ser o
    // "ultimo item".
    //
    // TODA pasta tem menu, mesmo a que nao cria nada (References, System
    // Info...): la' ele traz so' "Refresh". Sem menu nenhum, o botao direito
    // numa pasta parecia defeito -- e numa delas ("Databases") era mesmo.
    {
        const std::optional<FolderCreate> create = std::move(folder_create_);
        folder_create_.reset();

        const std::string popup = node_id + "##create";
        // "nav foldermenu <rotulo>" do canal de comandos abre o MESMO popup,
        // no mesmo no': e' como se confere o menu sem o clique direito.
        const bool requested = !folder_menu_request_.empty() &&
                               folder_menu_request_ == label;
        if (requested) folder_menu_request_.clear();
        if (requested || (ImGui::IsItemHovered() &&
                          ImGui::IsMouseReleased(ImGuiMouseButton_Right))) {
            ImGui::OpenPopup(popup.c_str());
        }
        if (ImGui::BeginPopup(popup.c_str())) {
            if (create.has_value()) {
                create_menu_item(create->type, create->schema, create->parent);
                if (create->type == db::ObjectType::function) {
                    create_menu_item(db::ObjectType::procedure, create->schema,
                                     create->parent);
                }
                ImGui::Separator();
            }
            if (ImGui::MenuItem(TR("Refresh"), "F5", false, !session().busy())) {
                session().reload_catalog_async();
            }
            ImGui::EndPopup();
        }
    }

    // `icon` e' o do CONTEUDO; a pasta em si depende do conjunto de icones
    // (ui/icon_images.hpp).
    same_line_after_arrow();
    icon_inline(folder_icon(icon), colors().accent_light);
    ImGui::SameLine(0.0f, tree_label_gap());
    ImGui::TextUnformatted(label);

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
    //
    // A pasta Tables leva tambem a tabela PARTICIONADA (relkind 'p'): no
    // DBeaver ela e' uma PostgreTable como as outras. Comparando so' o kind,
    // ela nao caia em pasta nenhuma -- sumia da arvore, e com ela a pasta
    // Partitions, que so' existe dentro da tabela-mae.
    const auto in_folder = [kind](db::ObjKind relation_kind) {
        return relation_kind == kind ||
               (kind == db::ObjKind::table &&
                relation_kind == db::ObjKind::partitioned_table);
    };

    std::size_t count = 0;
    for (const db::TableMeta& relation : schema.tables) {
        // Conta so' o que passa no filtro: "Tabelas (32)" com 3 visiveis
        // seria contradicao na mesma linha.
        if (in_folder(relation.kind) && matches_filter(relation.name)) ++count;
    }

    // A pasta aparece mesmo vazia, com (0) -- como no DBeaver, onde Tables,
    // Views e Materialized Views estao sempre no mesmo lugar. Escondida, quem
    // procura "Views" onde esta' acostumado nao acha, e nao sabe se o schema
    // nao tem views ou se o programa nao as mostra (diretiva 12).

    // "Open Declaration": se a relacao procurada esta' nesta pasta, ela abre.
    bool reveal_in_folder = false;
    if (revealing_here() && schema.name == tree_reveal_.schema) {
        for (const db::TableMeta& relation : schema.tables) {
            reveal_in_folder |= in_folder(relation.kind) &&
                                relation.name == tree_reveal_.relation;
        }
    }
    if (reveal_in_folder && reveal_forcing()) ImGui::SetNextItemOpen(true);

    const db::ObjectType object_type =
        kind == db::ObjKind::view                ? db::ObjectType::view
        : kind == db::ObjKind::materialized_view ? db::ObjectType::materialized_view
        : kind == db::ObjKind::foreign_table     ? db::ObjectType::foreign_table
                                                 : db::ObjectType::table;
    folder_creates(object_type, schema.name);
    if (!draw_folder_node(icon, label, count, schema.tables_loaded)) return;

    const Palette& p = colors();
    const std::uint32_t tint = kind == db::ObjKind::table ? p.accent : p.data;

    // A maior relacao da pasta: e' contra ela que a barra de cada uma mede.
    std::int64_t largest = 0;
    for (const db::TableMeta& relation : schema.tables) {
        if (in_folder(relation.kind)) {
            largest = (std::max)(largest, relation.size_bytes);
        }
    }

    bool first = true;
    for (const db::TableMeta& relation : schema.tables) {
        if (!in_folder(relation.kind)) continue;
        if (!matches_filter(relation.name)) continue;

        ImGui::PushID(relation.name.c_str());
        ImGui::BeginGroup();

        // So' a primeira de cada pasta: abrir as 32 encheria a arvore de ruido
        // e dispararia 32 consultas de catalogo de uma vez.
        static const bool expand_all = std::getenv("OTTER_EXPAND_TREE") != nullptr;
        if (expand_all && first) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        first = false;

        // Seta, icone, nome -- a ordem do DBeaver. Ver draw_folder_node.
        //
        // SpanAvailWidth faz o no' ocupar a linha inteira, para o duplo
        // clique e o menu de contexto pegarem tambem sobre o icone e o nome,
        // que sao desenhados DEPOIS dele.
        // O objeto de "Open Declaration" fica realcado enquanto o pedido
        // vale, e a arvore rola ate' ele.
        const bool revealed =
            reveal_in_folder && relation.name == tree_reveal_.relation;
        if (revealed && reveal_forcing()) ImGui::SetScrollHereY(0.35f);

        const bool open = ImGui::TreeNodeEx(
            "##rel", ImGuiTreeNodeFlags_SpanAvailWidth |
                         (revealed ? ImGuiTreeNodeFlags_Selected : 0));

        // Estado do NO' capturado aqui: o icone e o nome vem depois, e
        // IsItemHovered passaria a falar deles em vez do no'.
        const bool node_hovered = ImGui::IsItemHovered();
        const bool node_toggled = ImGui::IsItemToggledOpen();

        // Arrastar a relacao para o editor. O DBeaver faz o mesmo -- e' o
        // caminho mais curto entre "achei a tabela" e "escrevi a consulta".
        //
        // SourceNoDisableHover mantem o realce do no' durante o arrasto: sem
        // ele a linha apaga assim que o arrasto comeca, e nao se sabe mais o
        // que esta' sendo arrastado.
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
            const std::string qualified =
                db::qualified_name(schema.name, relation.name);

            // O payload carrega o nome QUALIFICADO: soltar "cliente" num
            // editor cuja conexao esta' noutro schema produziria SQL que nao
            // resolve. O '\0' vai junto para o alvo poder ler como C-string.
            ImGui::SetDragDropPayload("OTTER_RELATION", qualified.c_str(),
                                      qualified.size() + 1);

            icon_inline(icon, tint);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextUnformatted(qualified.c_str());
            ImGui::EndDragDropSource();
        }

        same_line_after_arrow();
        icon_inline(icon, tint);
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(kind == db::ObjKind::table ? p.text : p.data),
                           "%s", relation.name.c_str());

        // Duplo clique (e F4) abre o EDITOR do objeto, como no DBeaver --
        // na aba em que o usuario deixou o ultimo: quem so' quer os dados
        // cai em Data, quem estava vendo colunas cai em Properties.
        //
        // O TreeNode ja' consome o duplo clique para expandir; IsItemToggled
        // distingue os dois casos.
        if (node_hovered &&
            ((ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !node_toggled) ||
             ImGui::IsKeyPressed(ImGuiKey_F4, false))) {
            db::ObjectRef ref;
            ref.type   = object_type;
            ref.schema = schema.name;
            ref.name   = relation.name;
            open_object_editor(std::move(ref), relation_opens_data_);
        }

        // A coluna de tamanho, encostada a' direita com a barra -- a mesma
        // dos bancos. O texto solto depois do nome ("24 kB") ficava em
        // posicao diferente a cada linha e era cortado pela borda do painel.
        if (relation.size_bytes >= 0) {
            draw_size_bar(relation.size_pretty,
                          largest > 0 ? static_cast<float>(relation.size_bytes) /
                                            static_cast<float>(largest)
                                      : 0.0f);
        }

        // Fecha o grupo ANTES do menu: BeginPopupContextItem usa o ultimo
        // item, e o grupo faz esse "ultimo item" ser a linha inteira --
        // seta, icone, nome e tamanho.
        //
        // Sem o grupo, o menu se ligaria ao texto do tamanho, e clicar com o
        // direito sobre o nome da tabela nao abriria nada.
        ImGui::EndGroup();
        // O TreeNode acima abriu DENTRO do grupo, e EndGroup restaura o
        // recuo salvo em BeginGroup (imgui.cpp, DC.Indent = BackupIndent):
        // o recuo que o TreePush aplicou se perde, e o TreePop la' embaixo
        // recua um nivel a MAIS. Os filhos saiam no nivel do pai e tudo
        // depois do no' aberto deslizava para a esquerda. Reaplicado aqui,
        // o par TreePush/TreePop volta a fechar.
        if (open) ImGui::Indent();

        draw_relation_context_menu(schema, relation);

        if (ImGui::IsItemHovered() && !relation.comment.empty()) {
            Hint(relation.name).text(relation.comment).show();
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

    // A ordem das pastas e' a do `<tree>` do DBeaver para a tabela:
    // Columns, Constraints, Foreign Keys, Indexes, Dependencies, References,
    // Partitions, Child tables, Triggers, Rules, Policies. As listas que so'
    // o PostgreSQL tem ficam atras de `pg`.
    const bool pg = session().is_postgres();
    const bool ms = session().is_mssql();
    const bool sa = session().is_sqlanywhere();

    // --- Colunas -------------------------------------------------------------
    // O objeto-filho de uma linha destas pastas, para o editor e o menu.
    const auto child = [&schema, &table](db::ObjectType type, const std::string& name) {
        db::ObjectRef ref;
        ref.type   = type;
        ref.schema = schema.name;
        ref.name   = name;
        // O indice e' objeto do schema; os demais pertencem a' tabela.
        ref.parent = type == db::ObjectType::index ? std::string{} : table.name;
        return ref;
    };

    folder_creates(db::ObjectType::column, schema.name, table.name);
    if (draw_folder_node(Icon::column, TR("Columns"), table.columns.size(),
                         table.columns_loaded)) {
        if (!table.columns_loaded && !session().busy()) {
            session().load_columns_async(schema.name, table.name);
        }
        if (!table.columns_loaded) {
            ImGui::TextColored(col4(p.text_dim), TR("  loading..."));
        }

        for (const db::ColumnMeta& column : table.columns) {
            ImGui::PushID(column.name.c_str());
            ImGui::BeginGroup();
            icon_inline(column.primary_key ? Icon::key : Icon::column,
                        column.primary_key ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, tree_label_gap());

            ImGui::TextColored(col4(column.primary_key ? p.data_light : p.text),
                               "%s", column.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s%s%s",
                               column.type_name.c_str(),
                               column.primary_key ? "  PK" : "",
                               column.nullable ? "" : "  NOT NULL");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(column.name)
                    .accent(TR("Type"), column.type_name)
                    .row(TR("Primary key"), column.primary_key ? TR("yes") : "")
                    .row(TR("Nullable"), column.nullable ? TR("yes") : TR("no"))
                    .row(TR("Default"), column.default_value)
                    .text(column.comment)
                    .show();
            }
            object_node(child(db::ObjectType::column, column.name));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // --- Constraints ---------------------------------------------------------
    //
    // Uma view nao tem constraints nem chaves estrangeiras. O DBeaver nem
    // mostra as pastas nesse caso, e mostrar "(0)" sugeriria que a view
    // poderia ter uma.
    if (table.has_constraints()) {
        folder_creates(db::ObjectType::constraint, schema.name, table.name);
    }
    if (table.has_constraints() &&
        draw_folder_node(Icon::constraint, TR("Constraints"),
                         table.constraints.size(), table.constraints_loaded)) {
        if (!table.constraints_loaded && !session().busy()) {
            session().load_constraints_async(schema.name, table.name);
        }
        for (const db::ConstraintMeta& constraint : table.constraints) {
            const bool is_pk = constraint.kind == db::ObjKind::primary_key;
            ImGui::PushID(constraint.name.c_str());
            ImGui::BeginGroup();
            icon_inline(is_pk ? Icon::key : Icon::constraint,
                        is_pk ? p.data_light : p.text_dim);
            ImGui::SameLine(0.0f, tree_label_gap());

            ImGui::TextColored(col4(is_pk ? p.data_light : p.text), "%s",
                               constraint.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s",
                               std::string(db::to_string(constraint.kind)).c_str());
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(constraint.name).code(constraint.definition).show();
            }
            object_node(child(db::ObjectType::constraint, constraint.name));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // --- Chaves estrangeiras -------------------------------------------------
    if (table.is_real_table()) {
        folder_creates(db::ObjectType::foreign_key, schema.name, table.name);
    }
    if (table.is_real_table() &&
        draw_folder_node(Icon::foreign_key, TR("Foreign Keys"),
                         table.foreign_keys.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session().busy()) {
            session().load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& key : table.foreign_keys) {
            ImGui::PushID(key.name.c_str());
            ImGui::BeginGroup();
            icon_inline(Icon::foreign_key, p.accent_light);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextColored(col4(p.text), "%s", key.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "→ %s.%s", key.target_table.c_str(),
                               key.target_column.c_str());
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(key.source_column + " \xE2\x86\x92 " + key.target_table + "." +
                     key.target_column)
                    .row("ON UPDATE", key.on_update)
                    .row("ON DELETE", key.on_delete)
                    .code(key.definition)
                    .show();
            }
            object_node(child(db::ObjectType::foreign_key, key.name));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // --- Índices -------------------------------------------------------------
    //
    // A view comum nao tem indices, mas a materializada tem -- e' justamente
    // o que permite indexa-la como uma tabela.
    if (table.has_indexes()) {
        folder_creates(db::ObjectType::index, schema.name, table.name);
    }
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

            ImGui::PushID(index.name.c_str());
            ImGui::BeginGroup();
            icon_inline(Icon::index, color);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextColored(col4(color), "%s", index.name.c_str());

            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s  %s%s%s",
                               index.method.c_str(),
                               index.size_pretty.c_str(),
                               index.unique ? "  UNIQUE" : "",
                               index.valid ? "" : "  INVALID");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(index.name).code(index.definition).show();
            }
            object_node(child(db::ObjectType::index, index.name));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // --- Dependencias -------------------------------------------------------
    //
    // O que depende desta relacao: views, constraints, triggers, defaults.
    // E' a resposta a "posso remover isto?" antes de tentar.
    // No SQL Anywhere o servidor so' registra dependencia de VIEW (o que ela
    // usa): numa tabela a pasta sairia sempre vazia.
    if (pg || ms || (sa && !table.is_real_table())) {
        // A dependencia interna (o tipo-linha, o indice da PK) cai junto num
        // DROP; a marca separa as que de fato impedem.
        ListOptions dependencies;
        dependencies.on_suffix = "  (internal)";
        draw_list_folder(Icon::dependency, TR("Dependencies"),
                         db::CatalogList::dependencies, Icon::dependency,
                         schema.name, table.name, {}, dependencies);
    }

    // --- Referências ---------------------------------------------------------
    //
    // Quem aponta para esta tabela. Responder "o que depende disto?" é o que
    // mais falta num cliente SQL.
    if (table.is_real_table() &&
        draw_folder_node(Icon::references, TR("References"),
                         table.references.size(), table.keys_loaded)) {
        if (!table.keys_loaded && !session().busy()) {
            session().load_keys_async(schema.name, table.name);
        }
        for (const db::ForeignKeyMeta& reference : table.references) {
            icon_inline(Icon::references, p.warn);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextColored(col4(p.warn), "%s.%s",
                               reference.source_table.c_str(),
                               reference.source_column.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "→ %s",
                               reference.target_column.c_str());
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
    // No SQL Server o catalogo ainda nao le' as particoes: a pasta fica de
    // fora, em vez de dizer "not partitioned" de uma tabela que pode ser.
    if (!ms && !sa && table.is_real_table() &&
        draw_folder_node(Icon::partition, TR("Partitions"),
                         table.partitions.size(), table.partitions_loaded)) {
        if (!table.partitions_loaded && !session().busy()) {
            session().load_partitions_async(schema.name, table.name);
        }

        if (table.partitions_loaded && table.partitions.empty()) {
            ImGui::TextColored(col4(p.text_dim), TR("not partitioned"));
        }

        std::int64_t largest_partition = 0;
        for (const db::PartitionMeta& partition : table.partitions) {
            largest_partition =
                (std::max)(largest_partition, partition.size_bytes);
        }

        for (const db::PartitionMeta& partition : table.partitions) {
            ImGui::BeginGroup();
            icon_inline(Icon::partition, p.text_dim);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextColored(col4(p.text), "%s", partition.name.c_str());

            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s", partition.method.c_str());

            if (partition.size_bytes >= 0) {
                draw_size_bar(partition.size_pretty,
                              largest_partition > 0
                                  ? static_cast<float>(partition.size_bytes) /
                                        static_cast<float>(largest_partition)
                                  : 0.0f);
            }
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(partition.name)
                    .row(TR("Method"), partition.method)
                    .row(TR("Key"), partition.expression)
                    .text(partition.description)
                    .show();
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

    // --- Tabelas filhas (heranca) -------------------------------------------
    //
    // So' quando ha' alguma -- `visibleIf="object.hasSubClasses()"`. Numa
    // particionada as filhas sao as particoes, que ja' tem pasta propria.
    if (pg && table.kind == db::ObjKind::table && table.has_subclasses) {
        draw_list_folder(Icon::inheritance, TR("Child tables"),
                         db::CatalogList::child_tables, Icon::table, schema.name,
                         table.name);
    }

    // --- Triggers ------------------------------------------------------------
    //
    // A materialized view nao aceita trigger: ela e' atualizada por REFRESH,
    // nao por DML. A view comum aceita INSTEAD OF.
    if (table.has_triggers() && (pg || ms || sa)) {
        folder_creates(db::ObjectType::trigger, schema.name, table.name);
    }
    if (table.has_triggers() &&
        draw_folder_node(Icon::trigger, TR("Triggers"), table.triggers.size(),
                         table.triggers_loaded)) {
        if (!table.triggers_loaded && !session().busy()) {
            session().load_triggers_async(schema.name, table.name);
        }
        for (const db::TriggerMeta& trigger : table.triggers) {
            ImGui::PushID(trigger.name.c_str());
            ImGui::BeginGroup();
            icon_inline(Icon::trigger, trigger.enabled ? p.text_dim : p.error);
            ImGui::SameLine(0.0f, tree_label_gap());
            ImGui::TextColored(col4(trigger.enabled ? p.text : p.text_dim),
                               "%s", trigger.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s %s%s",
                               trigger.timing.c_str(), trigger.events.c_str(),
                               trigger.enabled ? "" : "  [off]");
            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                Hint(trigger.name).code(trigger.definition).show();
            }
            object_node(child(db::ObjectType::trigger, trigger.name));
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // --- Regras e politicas -------------------------------------------------
    //
    // Regra: tabela e view (a materializada nao aceita). Politica de RLS: so'
    // tabela de verdade.
    if (pg && !table.is_foreign() &&
        table.kind != db::ObjKind::materialized_view) {
        ListOptions rule_options;
        rule_options.off_suffix    = "  [off]";
        rule_options.object_type   = db::ObjectType::rule;
        rule_options.object_schema = schema.name;
        rule_options.object_parent = table.name;
        draw_list_folder(Icon::rule, TR("Rules"), db::CatalogList::rules,
                         Icon::rule, schema.name, table.name, {}, rule_options);
    }
    if (pg && table.is_real_table()) {
        ListOptions policy_options;
        policy_options.object_type   = db::ObjectType::policy;
        policy_options.object_schema = schema.name;
        policy_options.object_parent = table.name;
        draw_list_folder(Icon::policy, TR("Policies"), db::CatalogList::policies,
                         Icon::policy, schema.name, table.name, {}, policy_options);
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
    {
        // O no' sobre o qual os comandos de contexto `navigator` agem.
        db::ObjectRef tracked;
        tracked.type   = relation.kind == db::ObjKind::view ? db::ObjectType::view
                       : relation.kind == db::ObjKind::materialized_view
                             ? db::ObjectType::materialized_view
                             : db::ObjectType::table;
        tracked.schema = schema.name;
        tracked.name   = relation.name;
        nav_track(tracked);
    }
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

    db::ObjectRef object;
    object.type   = relation.kind == db::ObjKind::view ? db::ObjectType::view
                    : relation.kind == db::ObjKind::materialized_view
                          ? db::ObjectType::materialized_view
                    : relation.kind == db::ObjKind::foreign_table
                          ? db::ObjectType::foreign_table
                          : db::ObjectType::table;
    object.schema = schema.name;
    object.name   = relation.name;

    // O editor do objeto, como no DBeaver: "View Table" (F4) e "View Data".
    if (ImGui::MenuItem(relation.is_view() ? TR("View View") : TR("View Table"), "F4")) {
        open_object_editor(object);
    }
    if (ImGui::MenuItem(TR("View Data"))) {
        open_object_editor(object, /*data=*/true);
    }
    // O atalho antigo: os dados numa aba de SCRIPT, com o SELECT a' vista
    // para editar ("Read data in SQL console" no DBeaver).
    if (ImGui::MenuItem(TR("Read data in SQL console"))) {
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
                    hint_fmt("%s",
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

    // "Import Data" / "Export Data" do DBeaver. Exportar abre os dados no
    // editor do objeto e a janela de exportacao, ja' na consulta inteira.
    if (!relation.is_view() &&
        ImGui::MenuItem(TR("Import Data"), nullptr, false, can_alter)) {
        open_import(schema.name, relation.name);
    }
    if (ImGui::MenuItem(TR("Export Data"), nullptr, false, can_alter)) {
        open_object_editor(object, /*data=*/true);
        export_object_pending_ = true;
    }

    // Analyze, Vacuum, Truncate, Refresh, Reindex -- o "Tools" do DBeaver.
    if (!session().is_mysql()) draw_object_tools_menu(object);

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
    folder_creates(db::ObjectType::sequence, schema.name);
    if (!draw_folder_node(Icon::sequence, TR("Sequences"), schema.sequences.size(),
                          schema.sequences_loaded)) {
        return;
    }

    if (!schema.sequences_loaded && !session().busy()) {
        session().load_sequences_async(schema.name);
    }

    const Palette& p = colors();
    for (const db::SequenceMeta& sequence : schema.sequences) {
        ImGui::PushID(sequence.name.c_str());
        ImGui::BeginGroup();
        icon_inline(Icon::sequence, p.text_dim);
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(p.text), "%s", sequence.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "= %lld",
                           static_cast<long long>(sequence.last_value));
        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            Hint(sequence.name)
                .accent(TR("Last value"), std::to_string(sequence.last_value))
                .row(TR("Start"), std::to_string(sequence.start_value))
                .row(TR("Increment"), std::to_string(sequence.increment))
                .row(TR("Owned by"), sequence.owned_by)
                .text(sequence.comment)
                .show();
        }
        if (session().has_database_level() || session().is_sqlanywhere()) {
            db::ObjectRef ref;
            ref.type   = db::ObjectType::sequence;
            ref.schema = schema.name;
            ref.name   = sequence.name;
            object_node(ref);
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

void MainShell::draw_routines_folder(const db::SchemaMeta& schema) {
    // "Functions" no PostgreSQL, "Procedures" no MySQL -- o rotulo de cada
    // `<tree>` no DBeaver (o do SQL Server tambem diz "Procedures").
    const bool pg = session().is_postgres();
    folder_creates(db::ObjectType::function, schema.name);
    if (!draw_folder_node(pg ? Icon::function : Icon::procedure,
                          pg ? TR("Functions") : TR("Procedures"),
                          schema.routines.size(), schema.routines_loaded)) {
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

        // Seta, icone, nome -- ver draw_folder_node. Aqui o icone vinha
        // ANTES da seta, e as rotinas ficavam com a seta deslocada em relacao
        // a todos os outros nos do mesmo nivel.
        ImGui::BeginGroup();
        const bool open =
            ImGui::TreeNodeEx("##routine", ImGuiTreeNodeFlags_SpanAvailWidth);
        const bool routine_toggled = ImGui::IsItemToggledOpen();
        same_line_after_arrow();
        icon_inline(is_procedure ? Icon::procedure : Icon::function,
                    is_procedure ? p.data : p.accent_light);
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(p.text), "%s", routine.name.c_str());

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "(%s)", routine.arguments.c_str());

        // O retorno distingue funcao de procedure de relance, sem tooltip.
        if (!is_procedure && !routine.return_type.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.data), "→ %s", routine.return_type.c_str());
        }
        ImGui::EndGroup();
        if (open) ImGui::Indent();   // ver draw_relations_folder

        if (ImGui::IsItemHovered()) {
            Hint(routine.name)
                .row(TR("Arguments"), routine.arguments)
                .accent(TR("Returns"), is_procedure ? std::string{} : routine.return_type)
                .row(TR("Language"), routine.language)
                .text(routine.comment)
                .show();
        }
        {
            db::ObjectRef ref;
            ref.type = routine.aggregate ? db::ObjectType::aggregate
                       : is_procedure    ? db::ObjectType::procedure
                                         : db::ObjectType::function;
            ref.schema = schema.name;
            ref.name   = routine.name;
            // No PostgreSQL, so' os tipos: e' o que ALTER/DROP aceitam.
            ref.signature = pg ? routine.signature : std::string{};
            object_node(ref, routine_toggled);
        }

        if (open) {
            if (pg) {
                // Parametro de saida sai marcado: e' o que distingue uma
                // funcao que devolve varios valores.
                ListOptions parameters;
                parameters.on_suffix = "  OUT";
                draw_list_folder(Icon::parameter, TR("Function parameters"),
                                 db::CatalogList::routine_parameters,
                                 Icon::parameter, schema.name, routine.name,
                                 routine.arguments, parameters);
                draw_list_folder(Icon::dependency, TR("Dependencies"),
                                 db::CatalogList::routine_dependencies,
                                 Icon::dependency, schema.name, routine.name,
                                 routine.arguments);
            } else if (session().is_mssql() || session().is_sqlanywhere()) {
                ListOptions parameters;
                parameters.on_suffix = session().is_mssql() ? "  OUTPUT" : "  OUT";
                draw_list_folder(Icon::parameter, TR("Procedure parameters"),
                                 db::CatalogList::routine_parameters,
                                 Icon::parameter, schema.name, routine.name, {},
                                 parameters);
            }

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
    folder_creates(db::ObjectType::event, schema.name);
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
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(enabled ? p.text : p.text_dim), "%s",
                           event.name.c_str());

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "%s%s", event.schedule.c_str(),
                           enabled ? "" : "  [off]");
        ImGui::EndGroup();

        if (ImGui::IsItemHovered()) {
            Hint(event.name)
                .row(TR("Definer"), event.definer)
                .row(TR("Schedule"), event.schedule)
                .row(TR("Last executed"), event.last_executed.empty()
                                              ? TR("never executed")
                                              : event.last_executed.c_str())
                .code(event.definition)
                .show();
        }

        // View Event, Create New Event, Rename, Delete (docs/MYSQL-MAP.md).
        db::ObjectRef ref;
        ref.type   = db::ObjectType::event;
        ref.schema = schema.name;
        ref.name   = event.name;
        ImGui::PushID(event.name.c_str());
        object_node(ref);
        ImGui::PopID();
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

        // Enum e composto tem filhos para mostrar; domain e range cabem
        // inteiros no rotulo e no tooltip, entao nao viram no' expansivel.
        //
        // Seta, icone, nome. O tipo SEM filhos nao tem seta, mas ganha o
        // espaco dela: o icone cai na mesma coluna dos que tem.
        bool open         = false;
        bool type_toggled = false;
        if (type.has_children()) {
            open = ImGui::TreeNodeEx("##type", ImGuiTreeNodeFlags_SpanAvailWidth);
            type_toggled = ImGui::IsItemToggledOpen();
            same_line_after_arrow();
        } else {
            ImGui::Dummy(ImVec2(ImGui::GetTreeNodeToLabelSpacing(), 0.0f));
            same_line_after_arrow();
        }
        icon_inline(Icon::data_type, p.data_light);
        ImGui::SameLine(0.0f, tree_label_gap());
        ImGui::TextColored(col4(p.text), "%s", type.name.c_str());

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
        if (open) ImGui::Indent();   // ver draw_relations_folder

        if (ImGui::IsItemHovered()) {
            Hint(type.name)
                .accent(TR("Kind"), db::to_string(type.kind))
                .row(TR("Base type"), type.base_type.empty()
                                          ? std::string{}
                                          : type.base_type +
                                                (type.not_null ? " NOT NULL" : ""))
                .row(TR("Default"), type.default_value)
                .row(TR("Subtype"), type.subtype)
                .row(TR("Owner"), type.owner)
                .code(type.check_constraint)
                .text(type.comment)
                .show();
        }
        {
            db::ObjectRef ref;
            ref.type   = db::ObjectType::data_type;
            ref.schema = schema.name;
            ref.name   = type.name;
            object_node(ref, type_toggled);
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
                ImGui::SameLine(0.0f, tree_label_gap());
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
            ImGui::SameLine(0.0f, tree_label_gap());
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
            // A sessao do DOCUMENTO, nao a ativa: com duas conexoes abertas,
            // cancelar pela ativa interromperia a consulta da outra aba.
            //
            // Cancelar e' um PEDIDO ao servidor, nao uma ordem: uma consulta
            // que ja' estava devolvendo linhas termina normalmente. Por isso
            // a mensagem diz "pedido enviado", nao "cancelada".
            if (SqlDocument* document = active_document()) {
                const Status status = session_for(*document).cancel_query();
                document->set_status(status
                    ? std::string(TR("cancel requested"))
                    : status.error().to_string());
            }
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
            // O mesmo titulo da aba da conexao ativa, com o banco.
            const std::string active_title =
                active_connection_ < connections_.size()
                    ? connection_title(connections_[active_connection_])
                    : active_profile_.effective_name();
            std::snprintf(context, sizeof(context), "%s  ·  %s", active_title.c_str(),
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

// Abas de script de UMA conexao.
//
// So' desenha os documentos daquela conexao: os das outras vivem na janela
// delas. Antes havia uma barra unica com todos os scripts misturados, e um
// script de MySQL ficava ao lado de um de PostgreSQL sem nada distinguindo
// -- era possivel ver `public.` e crase na mesma tela.
void MainShell::draw_document_tabs(std::size_t connection_id) {
    constexpr ImGuiTabBarFlags flags =
        ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
        ImGuiTabBarFlags_FittingPolicyScroll |
        ImGuiTabBarFlags_TabListPopupButton;

    if (!ImGui::BeginTabBar("##doctabs", flags)) return;

    // Botão "+" ao lado das abas, como em navegadores. O script novo nasce
    // JA' na conexao desta janela -- e' o que o usuario esta' olhando.
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing |
                                      ImGuiTabItemFlags_NoTooltip)) {
        SqlDocument& created = new_document();
        created.set_connection_id(connection_id);
        focus_document_id_ = created.id();
        focus_editor_      = true;
    }

    std::optional<std::size_t> to_close;
    std::optional<std::size_t> to_close_others;

    for (std::size_t i = 0; i < documents_.size(); ++i) {
        SqlDocument& document = *documents_[i];
        if (document.connection_id() != connection_id) continue;

        ImGui::PushID(static_cast<int>(document.id()));

        // O sufixo ###id mantém a identidade da aba mesmo quando o título
        // muda (ao salvar com outro nome, por exemplo).
        const std::string label =
            document.title() + (document.modified() ? " *" : "") +
            "###doc" + std::to_string(document.id());

        ImGuiTabItemFlags item_flags = ImGuiTabItemFlags_None;
        if (document.pinned()) item_flags |= ImGuiTabItemFlags_Leading;

        // Pedido de trazer ESTA aba para a frente (o objeto ja' estava
        // aberto, ou acabou de ser).
        if (select_document_id_ == document.id()) {
            item_flags |= ImGuiTabItemFlags_SetSelected;
            select_document_id_ = 0;
        }
        if (document.modified()) item_flags |= ImGuiTabItemFlags_UnsavedDocument;

        bool open = true;
        const bool tab_visible =
            ImGui::BeginTabItem(label.c_str(), &open, item_flags);

        // O menu de contexto ANTES do corpo: BeginTabItem deixa a ABA como
        // ultimo item, mas draw_document_body desenha o editor dentro dele e
        // passa a ser o "ultimo item" -- o menu se ligava ao editor, e o
        // clique direito sobre a aba nao abria nada.
        //
        // Aqui o ultimo item ainda e' a aba, que e' o alvo que o usuario ve'.
        if (ImGui::BeginPopupContextItem("##tabmenu")) {
            // Renomear: set_title() existia desde sempre, sem nada que a
            // chamasse. Com varios scripts abertos, "Script 1..7" nao diz
            // qual e' qual -- e o nome do arquivo so' aparece ao salvar.
            if (ImGui::MenuItem(TR("Rename tab..."))) {
                renaming_document_ = document.id();
                std::snprintf(rename_buffer_, sizeof rename_buffer_, "%s",
                              document.title().c_str());
            }
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

        // O corpo DEPOIS do menu: ele muda qual e' o "ultimo item", e por
        // isso nao pode vir antes de BeginPopupContextItem.
        if (tab_visible) {
            active_document_ = i;
            draw_document_body(document);
            ImGui::EndTabItem();
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
    // A aba de um OBJETO nao e' um script: propriedades, dados e DDL.
    if (document.is_object()) {
        draw_object_editor(document);
        return;
    }

    // A sessao DESTE documento, nao a ativa: a aba executa contra a base a
    // que pertence, mesmo que outra conexao esteja selecionada no Raft.
    Session& target = session_for(document);
    const bool can_run = target.state() == SessionState::connected &&
                         !target.busy();

    (void)can_run;   // os botoes de executar estao na barra lateral

    // Os atalhos de contexto `editor`, so' com ESTA janela em foco -- ver
    // dispatch_shortcuts.
    const bool focused =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused) {
        editor_has_focus_ = true;
        dispatch_shortcuts(CommandContext::editor);
    }

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

    // Script de uma conexao que nao esta' aberta -- e' como voltam os scripts
    // da execucao anterior (ui/script_session.cpp), que nao conectam
    // sozinhos. Dito na propria aba, com o botao ao lado: os comandos de
    // executar ficam desabilitados, e sem isto nada explicaria por que.
    if (const SessionState state = target.state();
        (state == SessionState::disconnected || state == SessionState::failed) &&
        can_connect_from_tab(document.connection_id())) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().warn), "  %s", TR("not connected"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Connect"))) {
            tree_requests_.reconnect = document.connection_id();
        }
    } else if (state == SessionState::connecting) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim), "  %s", TR("connecting..."));
    }

    ImGui::Separator();

    // SQL e' codigo: aqui, e so' aqui, a fonte e' monoespacada. O resto da
    // interface usa a do sistema, como no DBeaver (ver load_ui_font).
    // Antes de Render(): a config e' lida quando o editor decide abrir o
    // popup, e aplica-la depois valeria so' no quadro seguinte.
    apply_completion_options(document);

    // A barra lateral a' esquerda do texto, como no DBeaver: executar em
    // cima, paineis embaixo.
    const float body_height = ImGui::GetContentRegionAvail().y;
    draw_editor_side_toolbar(body_height);
    ImGui::SameLine(0.0f, 2.0f);

    // "Switch active panel" e os dialogos devolvem o foco ao texto.
    if (focus_editor_ &&
        (focus_document_id_ == 0 || focus_document_id_ == document.id())) {
        document.editor().SetFocus();
        focus_editor_      = false;
        focus_document_id_ = 0;
    }

    ImFont* mono = mono_font();
    if (mono != nullptr) ImGui::PushFont(mono);
    document.editor().Render("##sql", ImGui::GetContentRegionAvail());
    if (mono != nullptr) ImGui::PopFont();

    // Soltar uma relacao arrastada da arvore.
    //
    // O alvo e' o retangulo do EDITOR, registrado logo apos Render(): o
    // TextEditor e' um widget de terceiro e nao chama BeginDragDropTarget,
    // entao quem o faz e' quem o desenha.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("OTTER_RELATION")) {
            const char* name = static_cast<const char*>(payload->Data);

            // Insere no CURSOR, nao no fim: quem arrasta para o meio de
            // "SELECT * FROM |" quer o nome ali, e acrescentar no fim
            // obrigaria a recortar e colar.
            //
            // Um espaco antes so' quando ha' texto imediatamente a' esquerda,
            // para nao colar o nome na palavra anterior nem abrir o arquivo
            // com um espaco solto.
            std::string insert(name);
            const TextEditor::DocPos at =
                document.editor().GetCurrentCursorPosition();
            if (at.index > 0) {
                const std::string line = document.editor().GetLineText(at.line);
                if (at.index <= line.size() &&
                    !std::isspace(static_cast<unsigned char>(line[at.index - 1]))) {
                    insert.insert(insert.begin(), ' ');
                }
            }

            // Pela area de transferencia: o TextEditor nao expoe inserir no
            // cursor, e Paste() e' o unico caminho que respeita o desfazer.
            //
            // O conteudo anterior e' restaurado -- arrastar uma tabela nao
            // deve apagar o que o usuario tinha copiado.
            const char* previous = ImGui::GetClipboardText();
            const std::string saved = previous != nullptr ? previous : "";

            ImGui::SetClipboardText(insert.c_str());
            document.editor().Paste();
            ImGui::SetClipboardText(saved.c_str());
        }
        ImGui::EndDragDropTarget();
    }
}

// UMA janela por conexao, com as abas de script dela dentro.
//
// Era uma janela unica "SQL" com todos os scripts misturados. O usuario
// abria uma segunda conexao, criava um script, e ele nascia ao lado dos da
// primeira -- sem nada dizendo a qual base pertencia. Como a execucao usava
// a conexao ATIVA, e nao a do documento, o script rodava contra quem
// estivesse selecionado no Raft naquele instante.
//
// Sendo janelas ancoraveis de verdade (e nao uma barra de abas interna), o
// ImGui as empilha como abas no mesmo no' do dock -- e da' para arrastar
// duas conexoes lado a lado para comparar.
void MainShell::draw_connection_editor(Connection& connection) {
    // Enquanto as abas DESTA conexao sao desenhadas, o SQL gerado e' no
    // dialeto dela (o editor de objeto monta ALTER, GRANT e DROP a cada
    // quadro). Reposto ao sair, para o resto da tela.
    struct DialectScope {
        db::QuoteStyle previous = db::sql_dialect();
        ~DialectScope() { db::set_sql_dialect(previous); }
    } dialect_scope;
    db::set_sql_dialect_for(connection.profile.driver_id);
    // Titulo = nome da conexao; ###id mantem a identidade da janela quando o
    // usuario renomeia a conexao (o ImGui identifica janela pelo nome, e
    // renomear a desancoraria do layout -- mesma razao do TRW).
    const std::string panel_id = "###SqlPanel" + std::to_string(connection.id);

    // Espaco a' esquerda do titulo para o icone de banco (pedido do usuario,
    // 2026-10-01): o ImGui nao poe widget dentro da aba de uma janela
    // ancorada, entao o rotulo reserva a largura com espacos e o icone e'
    // desenhado por cima, em draw_tab_database_icon. So' com a janela
    // ancorada no quadro anterior: flutuando, o titulo e' a barra da janela,
    // onde o icone nao e' desenhado e os espacos seriam so' um recuo.
    std::string padding;
    if (const ImGuiWindow* previous = ImGui::FindWindowByName(panel_id.c_str());
        // DockNode, e nao DockIsActive: com uma janela so' no no', o ImGui
        // limpa DockIsActive entre o fim de um quadro e o Begin do seguinte.
        previous != nullptr && previous->DockNode != nullptr) {
        const float wanted = ImGui::GetFontSize() + ImGui::GetStyle().ItemInnerSpacing.x;
        const float space  = ImGui::CalcTextSize(" ").x;
        padding.assign(static_cast<std::size_t>(std::ceil(wanted / space)), ' ');
    }
    const std::string title = padding + connection_title(connection) + panel_id;

    // Ancora a janela na primeira vez que ela aparece NESTA execucao; depois
    // disso, o que o usuario arrastou e' que vale.
    //
    // Era ImGuiCond_FirstUseEver, que confia no layout.ini: ele guarda o id
    // do no' de docking, e os ids mudam quando a arvore de paineis muda. Ao
    // sair o painel Raft (ADR 0018), o id gravado deixou de existir e a
    // janela do editor nasceu FLUTUANDO sobre a grade. O layout dos paineis
    // ja' e' remontado a cada execucao; a janela do editor segue a mesma
    // regra.
    if (editor_dock_id_ != 0 && !connection.docked) {
        ImGui::SetNextWindowDockID(editor_dock_id_, ImGuiCond_Always);
        connection.docked = true;
    }

    // Cor do tipo (Desenvolvimento/Teste/Producao) na aba da janela: e' o
    // aviso de que se esta' prestes a executar em producao.
    // O pedido de foco vale para a janela do script ATIVO.
    if (editor_focus_connection_ == connection.id) ImGui::SetNextWindowFocus();

    const db::ConnectionTypeInfo& type =
        db::connection_type_info(connection.profile.type);
    ImGui::PushStyleColor(ImGuiCol_Text, col(type.color));

    // O "x" na aba da janela fecha os scripts da conexao (pedido do usuario,
    // 2026-10-01): sem ele, uma conexao aberta ocupava a barra ate' o fim da
    // execucao. O ponto de "nao salvo" e' o mesmo das abas de script.
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
    if (connection_unsaved_documents(connection.id) > 0) {
        window_flags |= ImGuiWindowFlags_UnsavedDocument;
    }
    // O id ANTES das abas: se algo ali dentro criar uma sessao, o push_back
    // em connections_ invalida `connection`.
    const std::size_t connection_id = connection.id;
    bool keep_open = true;
    const bool open = ImGui::Begin(title.c_str(), &keep_open, window_flags);
    ImGui::PopStyleColor();
    if (!padding.empty()) draw_tab_database_icon(connection_id);

    // A janela em foco manda no resto da tela: Navigator, barra de status e
    // a conexao que um script novo herda passam a ser os desta.
    //
    // Sem isto, clicar na aba do MySQL deixava o Navigator listando os
    // schemas do PostgreSQL e a barra dizendo "PostgreSQL 18.2" -- a tela
    // inteira falando de uma base enquanto o editor falava de outra.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        for (std::size_t i = 0; i < connections_.size(); ++i) {
            if (connections_[i].id == connection.id) {
                active_connection_ = i;
                active_profile_    = connections_[i].profile;
                break;
            }
        }
    }

    if (open) draw_document_tabs(connection_id);
    ImGui::End();

    // Aplicado no quadro seguinte (apply_close_tab_request): aqui ainda se
    // esta' dentro do laco sobre connections_.
    if (!keep_open) close_tab_request_ = connection_id;
}

// O icone de banco na aba da janela da conexao: e' o "Select active schema"
// do DBeaver (Ctrl+0), que no editor mostra o banco corrente com o icone
// tree/database.svg -- aqui no lugar que o usuario pediu, a aba principal.
//
// Desenhado por cima da aba, na lista de desenho da janela que hospeda a
// barra de abas do dock: a aba e' do ImGui e nao recebe widgets. O clique e'
// lido a' mao; ele tambem seleciona a aba, o que e' o desejado.
void MainShell::draw_tab_database_icon(std::size_t connection_id) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    // Sem DockTabIsVisible: no ImGui ele quer dizer "aba selecionada", e as
    // abas de tras ficavam com o recuo do titulo e sem o icone.
    if (window == nullptr || !window->DockIsActive ||
        window->DockNode == nullptr || window->DockNode->HostWindow == nullptr) {
        return;
    }
    const Connection* connection = connection_by_id(connection_id);
    if (connection == nullptr) return;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float  size = ImGui::GetFontSize();
    const ImRect tab  = window->DC.DockTabItemRect;
    const ImVec2 min(tab.Min.x + style.FramePadding.x, tab.Min.y + style.FramePadding.y);
    const ImRect box(min, ImVec2(min.x + size, min.y + size));
    if (box.Max.x > tab.Max.x) return;   // aba estreita demais: so' o titulo

    const bool connected = connection->session->state() == SessionState::connected;
    ImGuiWindow* host    = window->DockNode->HostWindow;
    // Pela raiz da arvore de dock, nao por `host`: sobre a barra de abas o
    // ImGui da' como "janela sob o mouse" a propria janela ancorada (o
    // retangulo dela cobre a aba), e com `== host` o clique nunca chegava.
    // A raiz ainda exclui um popup ou janela solta por cima da aba.
    const ImGuiWindow* under = GImGui->HoveredWindow;
    const bool hovered = under != nullptr &&
                         under->RootWindowDockTree == window->RootWindowDockTree &&
                         ImGui::IsMouseHoveringRect(box.Min, box.Max, false);

    ImDrawList* dl = host->DrawList;
    dl->PushClipRect(tab.Min, tab.Max, false);
    const Palette& p = colors();
    if (hovered && connected) {
        dl->AddRectFilled(ImVec2(box.Min.x - 2, box.Min.y - 2), ImVec2(box.Max.x + 2, box.Max.y + 2),
                          col(with_alpha(p.accent, 0.25f)), 3.0f);
    }
    draw_icon_to(dl, Icon::database, box.GetCenter(), size * 0.9f,
                 col(with_alpha(p.text, connected ? 1.0f : 0.4f)), 1.4f);
    dl->PopClipRect();

    if (!hovered) return;
    // Desconectada nao ha' lista de bancos; a dica diz por que (diretiva 6).
    hint(connected ? TR("Select active database")
                   : TR("Connect to choose the database"));
    if (connected && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        open_database_picker(connection_id);
    }
}

void MainShell::open_database_picker(std::size_t connection_id) {
    app_.select_database     = true;
    app_.database_connection = connection_id;
    app_.search[0]           = '\0';
    app_.search_cursor       = -1;   // a lista poe no banco corrente
    app_.focus_search        = true;
}

void MainShell::switch_tab_database(std::size_t connection_id, const std::string& database) {
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t index = kNone;
    for (std::size_t i = 0; i < connections_.size(); ++i) {
        if (connections_[i].id == connection_id) index = i;
    }
    if (index == kNone) return;

    // MySQL: banco e schema sao a mesma coisa, e a troca e' um USE na mesma
    // sessao -- o "Select active schema" ja' faz isso.
    if (connections_[index].session->is_mysql()) {
        active_connection_ = index;
        active_profile_    = connections_[index].profile;
        set_default_schema(database);
        return;
    }

    // Os outros: uma sessao por banco sob a raiz (ADR 0018). O script vai
    // para ela, e a aba passa a ser a do banco escolhido.
    std::size_t root = index;
    if (const std::size_t parent = connections_[index].parent_id; parent != 0) {
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
    if (owner == index) return;
    const std::size_t owner_id = connections_[owner].id;

    // O script da aba: o ativo, se for desta janela; senao o primeiro dela.
    // Editor de objeto nao muda de banco (o objeto e' deste), entao sem
    // script a janela do banco ganha um novo -- como "nav database".
    SqlDocument* document = active_document();
    if (document == nullptr || document->connection_id() != connection_id ||
        document->is_object()) {
        document = nullptr;
        for (const std::unique_ptr<SqlDocument>& candidate : documents_) {
            if (candidate->connection_id() == connection_id && !candidate->is_object()) {
                document = candidate.get();
                break;
            }
        }
    }
    active_connection_ = owner;
    active_profile_    = connections_[owner].profile;
    tree_claim_        = owner_id;
    if (document == nullptr) {
        open_sql_tab(std::string{}, /*run=*/false);
        return;
    }
    document->set_connection_id(owner_id);
    select_document_id_ = document->id();
    focus_editor_       = true;
    focus_document_id_  = document->id();
}

void MainShell::draw_editor_panel() {
    // Recalculado a cada quadro por draw_document_body.
    editor_has_focus_ = false;

    // Sem conexao nenhuma, session() cria a Session vazia -- e' a ela que um
    // script aberto antes de conectar se amarra. Chamada pelo efeito
    // colateral; o valor nao interessa aqui.
    (void)session();

    apply_close_tab_request();

    // Abas cuja conexao foi fechada passam para a ativa, senao ficariam sem
    // janela onde aparecer -- some da tela o script que o usuario talvez nao
    // tenha salvo. Manter a aba e' o combinado; o que ela perde e' o vinculo
    // com a base que deixou de existir.
    for (std::unique_ptr<SqlDocument>& document : documents_) {
        if (connection_by_id(document->connection_id()) == nullptr) {
            document->set_connection_id(connections_[active_connection_].id);
        }
    }

    // "Maximize results panel": as janelas de editor nao sao desenhadas, e o
    // resultado ocupa o lugar delas. Os documentos continuam como estavam.
    if (results_maximized_) return;

    // A janela que deve vir para a frente neste quadro: a do script que pediu
    // o foco, ou a da aba que alguem mandou selecionar.
    //
    // Decidido ANTES do laco. Cada janela, ao desenhar a aba visivel dela,
    // faz desse script o ativo -- e perguntar "qual e' o ativo?" dentro do
    // laco respondia com o script da janela anterior. Era por isso que, com
    // duas conexoes reabertas ao iniciar, a aba que estava na frente ao sair
    // voltava atras da outra.
    // A aba que estava na frente ao sair (restore_scripts). O pedido e'
    // repetido por alguns quadros DEPOIS de o layout existir: no primeiro
    // quadro as janelas ainda nao estao ancoradas, a aba e' desenhada (o que
    // consome o pedido) e so' entao elas sao empilhadas -- com a primeira
    // conexao na frente, qualquer que fosse o script ativo. Mesma razao do
    // `reselect_result_` do painel de resultado.
    if (restore_front_frames_ > 0 && editor_dock_id_ != 0) {
        select_document_id_ = restore_front_document_;
        --restore_front_frames_;
    }

    editor_focus_connection_ = 0;
    if (focus_editor_ || select_document_id_ != 0) {
        const std::size_t wanted =
            select_document_id_ != 0 ? select_document_id_ : focus_document_id_;
        const SqlDocument* target = nullptr;
        for (const std::unique_ptr<SqlDocument>& document : documents_) {
            if (wanted != 0 && document->id() == wanted) target = document.get();
        }
        if (target == nullptr && focus_editor_) target = active_document();
        if (target != nullptr) editor_focus_connection_ = target->connection_id();
    }

    // Por indice, e com o tamanho de ANTES: se algo desenhado dentro da
    // janela abrir uma sessao (push_back em connections_), um iterador
    // ficaria invalido. A sessao nova ganha a janela no quadro seguinte.
    const std::size_t count = connections_.size();
    for (std::size_t i = 0; i < count && i < connections_.size(); ++i) {
        Connection& connection = connections_[i];

        // Uma conexao so' tem janela enquanto tem documento. Valia so' para
        // a sessao de um banco expandido na arvore (ADR 0018): expandir e'
        // olhar, e uma janela vazia por banco aberto encheria a area de
        // trabalho. Agora vale para todas -- e' o que deixa o programa abrir
        // sem aba nenhuma e o "x" da aba ter o que fechar.
        const bool has_document = std::any_of(
            documents_.begin(), documents_.end(),
            [&connection](const std::unique_ptr<SqlDocument>& document) {
                return document->connection_id() == connection.id;
            });
        if (!has_document) continue;
        draw_connection_editor(connection);
    }
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

    // "Set to default" ainda nao tem valor: quem decide e' o servidor.
    const bool is_default = edit != nullptr && edit->is_default;
    const bool is_null = edit != nullptr ? (edit->is_null || edit->is_default)
                                         : rs.is_null(row, column);
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

    // Selecao por cima de tudo -- inclusive da marca de alterado.
    //
    // A cor e' de ACENTO, nao a de "alterado": se as duas fossem parecidas,
    // uma celula selecionada pareceria alterada, e o usuario gravaria
    // esperando uma alteracao que nao existe.
    const bool selected = has_selection_ && selected_document_ == document.id() &&
                          selected_row_ == row && selected_column_ == column;
    if (selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               with_alpha(p.accent, 0.46f));
    } else if (grid_cell_in_selection(document, row, column)) {
        // O resto do bloco, mais fraco: da' para ver ate' onde a selecao vai
        // e ainda saber qual e' a celula corrente. Com 0,17 nao se via no
        // tema escuro -- a captura mostrava so' a celula corrente.
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                               with_alpha(p.accent, 0.26f));
    }

    // Guarda o inicio da celula: o alvo clicavel volta para ca' e cobre a
    // largura toda, incluindo a area do texto.
    const ImVec2 cell_origin = ImGui::GetCursorPos();
    const float  cell_width = std::max(ImGui::GetContentRegionAvail().x, 1.0f);

    // Barra na celula (ADR 0005). Desenhada ANTES do texto, no DrawList, e
    // nao como fundo da linha: TableSetBgColor pinta a celula INTEIRA, e uma
    // barra precisa de comprimento proprio -- e' o comprimento que carrega a
    // informacao.
    //
    // Nao aparece na linha marcada para exclusao nem na visao de registro: na
    // primeira o vermelho ja' diz o que importa, e na segunda nao ha' coluna
    // de valores para comparar -- uma barra sozinha nao tem contra o que ser
    // proporcional.
    if (!row_deleted && !document.record_mode() &&
        !document.bar_rules().empty()) {
        const db::CellBar bar = document.bar_rules().bar_for(rs, row, column);

        if (bar.visible && bar.fraction > 0.0f) {
            const ImVec2 screen = ImGui::GetCursorScreenPos();
            const float  height = ImGui::GetTextLineHeight();

            // Deixa uma margem no topo e na base: a barra encostada na borda
            // se confunde com a linha da tabela.
            constexpr float kInset = 2.0f;

            const float x0 = screen.x + bar.origin * cell_width;
            const float x1 = x0 + bar.fraction * cell_width;

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(x0, screen.y + kInset),
                ImVec2(x1, screen.y + height - kInset),
                with_alpha(bar.negative ? p.error : p.accent, 0.38f),
                2.0f);
        }
    }

    // Como NULL aparece, e se numeros vao para a direita: opcoes da pagina
    // "Editor de dados" do dialogo. Eram fixas no codigo.
    const db::EditorOptions& grid_options = editor_options_for(document);
    const char* null_label = grid_options.null_text.c_str();

    if (row_deleted) {
        // Sem editor: nao faz sentido alterar o que sera' excluido.
        ImGui::TextColored(col4(p.text_dim), "%s",
                           is_null ? null_label : std::string(value).c_str());
    } else if (is_default) {
        ImGui::TextColored(col4(p.text_dim), "%s", "[default]");
    } else if (is_null) {
        ImGui::TextColored(col4(p.text_dim), "%s", null_label);
    } else {
        const db::DataKind kind = rs.column(column).info().kind;

        // Alinhar numeros a' direita so' faz sentido na GRADE, onde eles
        // formam uma coluna e a virgula precisa casar. Na visao de registro
        // cada valor esta' sozinho, e empurra-lo para a direita o afasta do
        // proprio rotulo -- `limite` ficava a meia tela de "5000.00".
        if (db::is_right_aligned(kind) && !document.record_mode() &&
            grid_options.align_numbers_right) {
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
    //
    // O alvo clicavel existe TAMBEM no resultado somente leitura. Antes ele
    // so' era criado quando a grade podia editar: num JOIN nao dava para
    // selecionar uma celula com o mouse, nem copiar, nem abrir o menu -- e
    // sao justamente os resultados que mais se copia.
    //
    // InvisibleButton, e nao Dummy: Dummy reserva espaco mas nao e' item
    // interativo, e IsItemHovered() responderia sempre falso. Desenhado POR
    // CIMA do texto (cursor recuado): ao lado, a celula de valor curto teria
    // alvo so' na sobra.
    //
    // O PushID vem ANTES do botao. Depois, todas as celulas visiveis
    // compartilhavam o id "##cellhit" e o estado de uma vazava para a outra.
    ImGui::PushID(static_cast<int>(row * rs.column_count() + column));

    ImGui::SetCursorPos(cell_origin);
    ImGui::InvisibleButton("##cellhit",
                           ImVec2(cell_width, ImGui::GetTextLineHeight()),
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight);

    const bool left_click  = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool right_click = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    // O canto de baixo da celula corrente: e' onde abrem os menus pedidos
    // por comando.
    if (selected) {
        selected_cell_x_ = ImGui::GetItemRectMin().x;
        selected_cell_y_ = ImGui::GetItemRectMax().y;
    }

    if (left_click || right_click) {
        // Botao direito DENTRO do bloco selecionado preserva o bloco: o menu
        // vai agir sobre ele, e encolhe-lo para uma celula ao abrir o menu
        // faria "copiar" copiar outra coisa.
        if (!(right_click && grid_cell_in_selection(document, row, column))) {
            select_grid_cell(document, row, column,
                             left_click && ImGui::GetIO().KeyShift);
        }
        if (left_click) grid_drag_document_ = document.id();

        // Focar a janela do resultado EXPLICITAMENTE: clicar num
        // InvisibleButton nao da' foco a' janela que o contem, e sem foco
        // todas as teclas da grade ficam desligadas.
        ImGui::FocusWindow(ImGui::GetCurrentWindow()->RootWindow);
    }

    // Arrastar estende a selecao. AllowWhenBlockedByActiveItem: durante o
    // arrasto o item ativo e' a celula onde o botao desceu, e sem a flag
    // nenhuma outra contaria como "sob o mouse".
    if (grid_drag_document_ == document.id() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f) &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        (selected_row_ != row || selected_column_ != column)) {
        select_grid_cell(document, row, column, /*extend=*/true);
    }

    // Rolar ate' a selecao quando ela mudou por TECLADO. So' neste caso: com
    // o mouse a celula ja' esta' visivel por definicao. "Manter a' vista", e
    // nao "centralizar": centralizar a cada seta faz a grade inteira pular.
    if (selected && scroll_to_selection_) {
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeX |
                            ImGuiScrollFlags_KeepVisibleEdgeY);
        scroll_to_selection_ = false;
    }

    if (ImGui::IsItemHovered()) {
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !row_deleted) {
            start_inline_edit(document, rs, row, column);
        }

        // O valor original na dica: poder comparar sem desfazer.
        if (edit != nullptr) {
            const std::string original =
                rs.is_null(row, column) ? "[null]"
                                        : std::string(rs.text(row, column));
            hint_fmt(TR("was: %s"), original.c_str());
        }
    }

    // O menu da celula: todos os itens vem da tabela de comandos
    // (ui/grid_commands.cpp), na ordem do DBeaver.
    if (ImGui::BeginPopupContextItem("##cellmenu")) {
        draw_grid_cell_menu(document, rs);
        ImGui::EndPopup();
    }
    ImGui::PopID();
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
    const bool mssql = db::sql_dialect() == db::QuoteStyle::brackets;
    const bool anywhere = db::sql_dialect() == db::QuoteStyle::anywhere;
    std::snprintf(column_form_.type, sizeof column_form_.type, "%s",
                  mysql || anywhere ? "varchar(100)" : mssql ? "nvarchar(100)" : "text");

    // A lista de colunas alimenta o AFTER do MySQL.
    column_form_.existing.clear();
    for (const db::ColumnMeta& column : table.columns) {
        column_form_.existing.push_back(column.name);
    }
    column_form_.current = table;
}

void MainShell::open_create_table(const std::string& schema) {
    create_table_ = CreateTableForm{};
    create_table_.schema = schema;
    create_table_.open   = true;

    // Começa com UMA coluna de chave preenchida. Uma tabela sem PK não é
    // editável na grade (ADR 0014), e pedir ao usuário que descubra isso
    // depois de criá-la seria atrito evitável.
    const bool mysql = db::sql_dialect() == db::QuoteStyle::backticks;
    const bool mssql = db::sql_dialect() == db::QuoteStyle::brackets;
    const bool anywhere = db::sql_dialect() == db::QuoteStyle::anywhere;

    CreateTableForm::Column id;
    std::snprintf(id.name, sizeof id.name, "id");
    std::snprintf(id.type, sizeof id.type, "%s",
                  mysql      ? "INT AUTO_INCREMENT"
                  : mssql    ? "int IDENTITY(1,1)"
                  : anywhere ? "integer DEFAULT AUTOINCREMENT"
                             : "serial");
    id.nullable = false;
    id.key      = true;

    create_table_.columns.push_back(id);

    CreateTableForm::Column first;
    std::snprintf(first.type, sizeof first.type, "%s",
                  mysql || anywhere ? "varchar(100)" : mssql ? "nvarchar(100)" : "text");
    create_table_.columns.push_back(first);
}

void MainShell::open_create_view(const std::string& schema) {
    create_view_ = CreateViewForm{};
    create_view_.schema = schema;
    create_view_.open   = true;

    // Modelo com o schema já qualificado: a view guarda o corpo COMO ESCRITO,
    // e um FROM sem banco depende do banco corrente -- o que faz o MySQL
    // recusar com "No database selected".
    std::snprintf(create_view_.definition, sizeof create_view_.definition,
                  "SELECT *\n  FROM %s.", schema.c_str());
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

    // --- Tabela nova -------------------------------------------------------------

    if (create_table_.open) {
        ImGui::SetNextWindowSize(ImVec2(720, 0), ImGuiCond_Appearing);
        if (ImGui::Begin(TRW("New table", "###CreateTable"),
                         &create_table_.open,
                         ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_AlwaysAutoResize)) {

            ImGui::TextColored(col4(p.text_dim), "%s",
                               create_table_.schema.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(280);
            ImGui::InputText(TR("Name"), create_table_.name,
                             sizeof create_table_.name);

            ImGui::SetNextItemWidth(280);
            ImGui::InputText(TR("Comment"), create_table_.comment,
                             sizeof create_table_.comment);

            ImGui::Spacing();
            ImGui::TextColored(col4(p.text_dim), TR("Columns"));

            // Tabela de edição: nome, tipo, nulo, chave, e o botão de remover.
            // Uma tabela e não linhas soltas porque as colunas precisam estar
            // alinhadas para serem comparáveis de relance.
            constexpr ImGuiTableFlags flags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingFixedFit;

            std::size_t remove = create_table_.columns.size();

            if (ImGui::BeginTable("##newcols", 5, flags)) {
                ImGui::TableSetupColumn(TR("Name"),
                                        ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn(TR("Type"),
                                        ImGuiTableColumnFlags_WidthFixed, 200);
                ImGui::TableSetupColumn(TR("Null"),
                                        ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn(TR("Key"),
                                        ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableHeadersRow();

                for (std::size_t i = 0; i < create_table_.columns.size(); ++i) {
                    CreateTableForm::Column& column = create_table_.columns[i];

                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(i));

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##n", column.name, sizeof column.name);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputText("##t", column.type, sizeof column.type);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::Checkbox("##null", &column.nullable);

                    ImGui::TableSetColumnIndex(3);
                    // Marcar como chave implica NOT NULL: uma PK anulável não
                    // existe em SGBD nenhum, e deixar as duas caixas
                    // independentes geraria um DDL que o servidor recusa.
                    if (ImGui::Checkbox("##key", &column.key) && column.key) {
                        column.nullable = false;
                    }

                    ImGui::TableSetColumnIndex(4);
                    // A última coluna não se remove: uma tabela sem colunas
                    // não existe, e o gerador recusaria de todo jeito.
                    ImGui::BeginDisabled(create_table_.columns.size() <= 1);
                    if (ImGui::SmallButton("x")) remove = i;
                    ImGui::EndDisabled();

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (remove < create_table_.columns.size()) {
                create_table_.columns.erase(
                    create_table_.columns.begin() +
                    static_cast<std::ptrdiff_t>(remove));
            }

            if (ImGui::SmallButton(TR("Add column"))) {
                create_table_.columns.emplace_back();
            }

            ImGui::Separator();

            bool valid = create_table_.name[0] != 0;
            for (const CreateTableForm::Column& column : create_table_.columns) {
                if (column.name[0] == 0 || column.type[0] == 0) valid = false;
            }

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("Review SQL"), ImVec2(140, 0))) {
                std::vector<db::NewColumn> columns;
                std::vector<std::string>   keys;

                for (const CreateTableForm::Column& column :
                     create_table_.columns) {
                    db::NewColumn out;
                    out.name      = column.name;
                    out.type_name = column.type;
                    out.nullable  = column.nullable;
                    columns.push_back(std::move(out));

                    if (column.key) keys.emplace_back(column.name);
                }

                confirm_ddl(TRF("Create table %s", create_table_.name),
                            db::generate_create_table(create_table_.schema,
                                                      create_table_.name,
                                                      columns, keys,
                                                      create_table_.comment),
                            create_table_.schema, create_table_.name);
                create_table_.open = false;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                create_table_.open = false;
            }

            if (!valid) {
                ImGui::SameLine();
                ImGui::TextColored(col4(p.text_dim),
                                   TR("(every column needs a name and a type)"));
            }
        }
        ImGui::End();
    }

    // --- View nova ---------------------------------------------------------------

    if (create_view_.open) {
        ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_Appearing);
        if (ImGui::Begin(TRW("New view", "###CreateView"), &create_view_.open,
                         ImGuiWindowFlags_NoDocking)) {

            ImGui::TextColored(col4(p.text_dim), "%s",
                               create_view_.schema.c_str());
            ImGui::Separator();

            ImGui::SetNextItemWidth(280);
            ImGui::InputText(TR("Name"), create_view_.name,
                             sizeof create_view_.name);

            ImGui::Checkbox(TR("Replace if it exists"), &create_view_.or_replace);
            if (ImGui::IsItemHovered()) {
                hint_fmt("%s",
                                  TR("CREATE OR REPLACE preserves the grants on "
                                     "the view; dropping and recreating loses "
                                     "them"));
            }

            ImGui::Spacing();
            ImGui::TextColored(col4(p.text_dim), TR("Query"));

            const float footer = ImGui::GetFrameHeightWithSpacing() * 1.6f;
            ImGui::InputTextMultiline("##viewsql", create_view_.definition,
                                      sizeof create_view_.definition,
                                      ImVec2(-1, -footer));

            const bool valid = create_view_.name[0] != 0 &&
                               create_view_.definition[0] != 0;

            ImGui::BeginDisabled(!valid);
            if (ImGui::Button(TR("Review SQL"), ImVec2(140, 0))) {
                confirm_ddl(TRF("Create view %s", create_view_.name),
                            db::generate_create_view(create_view_.schema,
                                                     create_view_.name,
                                                     create_view_.definition,
                                                     create_view_.or_replace),
                            create_view_.schema, create_view_.name);
                create_view_.open = false;
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) {
                create_view_.open = false;
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
            if (db::sql_dialect() == db::QuoteStyle::double_quotes) {
                ImGui::Checkbox(TR("Concurrently"), &index_form_.concurrently);
                if (ImGui::IsItemHovered()) {
                    hint_fmt("%s",
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
        // No T-SQL e' BEGIN TRANSACTION: "BEGIN" sozinho abre um bloco.
        script.insert(script.begin(), std::string(db::transaction_begin_sql()));
        script.emplace_back(db::transaction_commit_sql());
    }

    ddl_pending_reload_ = true;
    session().execute_script_async(std::move(script));
}

void MainShell::save_pending_edits(SqlDocument& document, bool commit_after) {
    if (!document.edits().has_changes()) return;
    if (!document.result().has_value()) return;

    // A sessao do DOCUMENTO, nao a ativa: com duas conexoes abertas, gravar
    // pela ativa mandaria os UPDATE para o banco errado.
    Session& target = session_for(document);
    if (target.state() != SessionState::connected || target.busy()) return;

    auto updates = db::generate_changes(*document.result(),
                                        document.edit_target(),
                                        document.edits());
    if (!updates) {
        document.set_status(updates.error().to_string());
        return;
    }

    std::vector<std::string> statements;
    statements.reserve(updates->size() + 2);

    if (target.auto_commit()) {
        // Em transacao, mesmo em auto-commit: gravar cinco linhas e falhar
        // na terceira deixaria duas gravadas e tres nao -- estado que o
        // usuario nao pediu e nao consegue reproduzir (ADR 0014).
        statements.emplace_back(db::transaction_begin_sql());
        for (std::string& update : *updates) statements.push_back(std::move(update));
        statements.emplace_back(db::transaction_commit_sql());
        commit_after_save_ = false;
    } else {
        // Modo manual: as alteracoes entram na transacao que o usuario ja'
        // tem aberta, como no DBeaver. Um BEGIN aqui seria aninhado (aviso
        // do servidor), e um COMMIT encerraria o trabalho dele sem ele pedir
        // -- so' "Apply and commit" confirma, e pelo caminho do driver, que
        // e' quem sabe o estado da transacao.
        for (std::string& update : *updates) statements.push_back(std::move(update));
        commit_after_save_ = commit_after;
    }

    executing_document_id_ = document.id();
    document.set_executing(true);
    saving_edits_ = true;

    // O buffer so' e' limpo quando a gravacao termina sem erro -- ver a
    // colheita do resultado em draw().
    target.execute_script_async(std::move(statements));
}

// Menu de cor de uma coluna.
//
// Presets em vez de um formulário genérico: quem abre o menu quer "marcar os
// negativos de vermelho", não escolher operador, dois operandos e duas cores
// em RGB. O formulário completo existe na janela de regras, para quem precisa.
//
// A escolha é a mesma do DBeaver, que oferece "Set color by value" no menu da
// célula e o editor completo em Virtual Model.
// Menu de barra de uma coluna (ADR 0005).
//
// Tres ancoragens, e a escolha entre elas muda o que a coluna CONTA. Os
// rotulos dizem para que serve cada uma, e nao como funciona: "do zero" e
// "do menor valor" sao descricoes de implementacao, e quem esta' olhando uma
// coluna de temperaturas quer saber qual escolher, nao como cada uma calcula.
void MainShell::draw_bar_menu(SqlDocument& document, const db::ResultSet& rs,
                              std::size_t column) {
    const Palette& p = colors();
    const db::ColumnInfo& info = rs.column(column).info();

    // Barra em coluna de texto nao tem sentido: nao ha' o que ser
    // proporcional. Desabilitar e dizer por que, em vez de aceitar e nao
    // desenhar nada (diretiva 6).
    const bool numeric = db::is_right_aligned(info.kind);

    const auto add = [&](db::BarBaseline baseline) {
        db::BarSpec spec;
        spec.column   = info.name;
        spec.baseline = baseline;

        document.bar_rules().add(std::move(spec));
        if (document.result()) {
            document.bar_rules().prepare(*document.result());
        }
    };

    ImGui::BeginDisabled(!numeric);

    if (ImGui::MenuItem(TR("Bar from zero"))) {
        add(db::BarBaseline::from_zero);
    }
    if (ImGui::IsItemHovered()) {
        hint_fmt("%s", TR("for quantities: revenue, count, total"));
    }

    if (ImGui::MenuItem(TR("Bar over the column range"))) {
        add(db::BarBaseline::from_minimum);
    }
    if (ImGui::IsItemHovered()) {
        hint_fmt(
            "%s", TR("for narrow ranges far from zero, like 36.1..36.9, where "
                     "anchoring at zero makes every bar look the same"));
    }

    if (ImGui::MenuItem(TR("Bar centered on zero"))) {
        add(db::BarBaseline::centered_on_zero);
    }
    if (ImGui::IsItemHovered()) {
        hint_fmt("%s", TR("for variation and balance, where the sign "
                                   "is the point"));
    }

    ImGui::EndDisabled();

    if (!numeric) {
        ImGui::TextColored(col4(p.text_dim), TR("(numeric columns only)"));
    }
}

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
    // Pedido vindo do teclado (F11 / Shift+F11). Chega aqui e nao em
    // handle_grid_keys porque um popup do ImGui se ancora no ULTIMO item
    // desenhado: la' o ultimo item e' a celula; aqui, o cabecalho certo.
    if (grid_menu_request_ == column + 1) {
        grid_menu_request_ = 0;
        ImGui::OpenPopup("##colmenu");
    }

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

    // Barra na celula. Menu proprio, e nao um item dentro de "Cor": sao
    // respostas diferentes -- a cor diz "este valor esta' fora da faixa", a
    // barra diz "este valor comparado aos outros".
    if (ImGui::BeginMenu(TR("Bar"))) {
        draw_bar_menu(document, rs, column);
        ImGui::EndMenu();
    }

    if (document.bar_rules().affects_column(info.name)) {
        if (ImGui::MenuItem(TR("Remove the bar of this column"))) {
            db::BarRules& bars = document.bar_rules();
            for (std::size_t i = bars.specs().size(); i-- > 0;) {
                if (bars.specs()[i].column == info.name) bars.remove(i);
            }
            if (document.result()) bars.prepare(*document.result());
        }
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
        export_defaults_applied_ = false;
        return;
    }
    const db::ResultSet& rs = *document->result();

    // Padroes da conexao (pagina "Transferência de dados"), aplicados na
    // PRIMEIRA vez que a janela abre.
    //
    // So' na abertura: reaplicar a cada quadro desfaria o que o usuario
    // acabou de escolher aqui dentro. Os padroes decidem como a janela abre,
    // nao o que ela faz.
    if (!export_defaults_applied_) {
        export_defaults_applied_ = true;

        const db::EditorOptions& options = editor_options_for(*document);
        // Abre na consulta inteira quando ha' mais do que a pagina: exportar
        // 200 linhas de dois milhoes raramente e' o que se quer.
        export_whole_query_ = document->paged();

        if (options.export_format >= 0 && options.export_format <= 6) {
            export_options_.format =
                static_cast<db::ExportFormat>(options.export_format);
        }
        export_options_.write_header = options.export_write_header;
        export_options_.null_text    = options.export_null_text;
    }

    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_Appearing);
    // Fundo opaco (diretiva 13): o SQL do editor atravessava a previa.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool export_visible =
        ImGui::Begin(TRW("Export result", "###ExportResult"), &show_export_,
                     ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    if (export_visible) {

        // O que sera' exportado: a PAGINA, nao o resultado inteiro. Dizer
        // isso evita a surpresa de abrir o CSV e achar 200 linhas de dois
        // milhoes (diretiva 6).
        //
        // Com o resultado paginado ha' duas coisas para exportar, e a janela
        // pergunta qual: as linhas carregadas, ou a consulta inteira -- lida
        // do servidor em pedacos e gravada direto no arquivo, como o
        // assistente de transferencia do DBeaver.
        if (document->paged()) {
            if (ImGui::RadioButton(TR("All rows of the query"), export_whole_query_)) {
                export_whole_query_ = true;
            }
            if (ImGui::IsItemHovered()) {
                hint_fmt(TR(
                    "Runs the query again and writes every row to the file, "
                    "reading the server in chunks.\n"
                    "The filter and the sort order of the grid are kept."));
            }
            ImGui::SameLine();
            const std::string loaded = TRF("Loaded rows only (%zu)", rs.row_count());
            if (ImGui::RadioButton(loaded.c_str(), !export_whole_query_)) {
                export_whole_query_ = false;
            }
        } else {
            export_whole_query_ = false;
            ImGui::TextColored(col4(p.text_dim), TR("%zu row(s), %zu column(s)"),
                               rs.row_count(), rs.column_count());
        }

        ImGui::Separator();

        // --- Formato ---------------------------------------------------------
        static constexpr db::ExportFormat kFormats[] = {
            db::ExportFormat::csv,      db::ExportFormat::json,
            db::ExportFormat::markdown, db::ExportFormat::sql_insert,
            db::ExportFormat::html,     db::ExportFormat::xml,
            db::ExportFormat::txt,
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
                hint_fmt(TR(
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
        Session& target = session_for(*document);
        const Session::TransferState transfer = target.transfer_state();

        // A exportacao em curso terminou: o resultado vai para a linha de
        // estado, e a sessao volta a aceitar outra.
        if (transfer.finished) {
            if (!transfer.error.empty()) {
                export_status_ = transfer.error;
            } else if (transfer.cancelled) {
                export_status_ = std::string(
                    TRF("cancelled: %zu row(s) written to %s (partial file)",
                        transfer.rows, transfer.path.c_str()));
            } else {
                export_status_ = std::string(TRF("%zu row(s) written to %s",
                                                 transfer.rows,
                                                 transfer.path.c_str()));
            }
            target.clear_transfer();
        }

        ImGui::SameLine();
        if (transfer.running) {
            if (icon_text_button("##cancelexport", Icon::stop, TR("Cancel"),
                                 TR("Stop after the chunk being written"))) {
                target.cancel_transfer();
            }
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text), TR("%zu row(s) written..."),
                               transfer.rows);
        }

        const bool can_save =
            !transfer.running && !export_path_.empty() &&
            (!export_whole_query_ ||
             (target.state() == SessionState::connected && !target.busy()));
        bool save = false;
        if (!transfer.running) {
            save = icon_text_button(
                "##savefile", Icon::save, TR("Save to file"),
                export_whole_query_
                    ? TR("Run the query again and write every row to the file")
                    : TR("Write the result to the file above"),
                can_save);
        }
        // "export save" do canal de comandos aperta o mesmo botao.
        if (export_submit_ && can_save) save = true;
        export_submit_ = false;

        if (save) {
            if (export_whole_query_) {
                // A consulta como esta' na grade -- filtro e ordenacao --, sem
                // o LIMIT da pagina.
                const sql::PagedQuery whole = sql::make_unpaged_query(
                    document->paged_sql(), document_dialect(*document),
                    document->sort(), document->filter());
                export_status_.clear();
                target.export_query_async(whole.sql, export_options_, export_path_);
            } else if (auto status = db::export_to_file(rs, export_options_,
                                                        export_path_);
                       status) {
                export_status_ = std::string(TRF("%zu row(s) written to %s",
                                                 rs.row_count(),
                                                 export_path_.c_str()));
            } else {
                export_status_ = status.error().to_string();
            }
        }

        if (!export_status_.empty() && !transfer.running) {
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
        const std::size_t first = document.page() * document.page_size() + 1;
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

        // "+" em vez de um total: saber o total exige um COUNT(*), que varre
        // a tabela outra vez (ADR 0011). Um numero inventado seria pior que a
        // ausencia dele.
        //
        // O "+" e' CLICAVEL e faz essa contagem sob demanda -- o
        // `resultset.count` do DBeaver. Sob demanda, nunca automatica:
        // dispara-la a cada consulta transformaria toda paginacao no custo
        // que a paginacao existe para evitar.
        if (document.has_more()) {
            ImGui::SameLine();
            if (document.total_rows().has_value()) {
                ImGui::TextColored(col4(p.data), TR("of %zu"),
                                   *document.total_rows());
            } else {
                ImGui::BeginDisabled(!can_run);
                if (ImGui::SmallButton("+")) count_total_rows(document);
                ImGui::EndDisabled();

                if (ImGui::IsItemHovered()) {
                    hint_fmt("%s",
                                      TR("Count the whole result (one more "
                                         "full scan)"));
                }
            }
        }

        ImGui::SameLine();
        ImGui::TextColored(col4(p.text_dim), "|  %zu %s  |  %zu bytes",
                           rs.column_count(), TR("column(s)"),
                           rs.bytes_used());

        if (ImGui::IsItemHovered()) {
            hint_fmt(
                TR("The query was rewritten with LIMIT %zu.\n"
                   "See the executed SQL in the Queries tab."),
                document.page_size() + 1);
        }

        ImGui::SameLine();
        draw_record_mode_button(document);

        ImGui::SameLine();
        if (icon_button("##export", Icon::save, TR("Export result..."))) {
            show_export_ = true;
            export_status_.clear();
        }
        return;
    }

    // Sem paginacao: o resultado e' completo, e a contagem e' exata.
    draw_record_mode_button(document);
    ImGui::SameLine();

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

// Linha nova com os valores de `row`, exceto a chave primaria.
//
// Extraida do menu de contexto para a tecla Ctrl+Alt+Insert usar a MESMA
// regra: duas copias da logica divergiriam, e a que trata a chave errado
// produz um INSERT recusado pelo servidor depois de o usuario ja' ter
// preenchido o resto.
void MainShell::duplicate_row(SqlDocument& document, const db::ResultSet& rs,
                              std::size_t row, std::size_t anchor) {
    const std::size_t index = document.edits().add_row(anchor);
    const auto& keys = document.edit_target().key_columns;

    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        // A chave fica vazia. A fonte e' `key_columns` do alvo de edicao --
        // indices no ResultSet --, e nao o `primary_key` da coluna: o
        // resultado pode vir de um SELECT que nao trouxe essa informacao.
        if (std::find(keys.begin(), keys.end(), c) != keys.end()) continue;

        // O valor copiado e' o do BUFFER quando ha' edicao pendente:
        // duplicar deve copiar o que esta' na tela, nao o que esta' no banco.
        const db::CellEdit* pending = document.edits().find(row, c);

        if (pending != nullptr) {
            // "Set to default" pendente: a linha nova tambem fica sem valor,
            // e o INSERT a deixa de fora -- que e' pedir o DEFAULT.
            if (pending->is_default) continue;
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

// Botao que alterna grade <-> registro. Fica na barra do resultado, nao num
// menu: e' uma troca de VISAO, algo que se faz e desfaz varias vezes lendo um
// cadastro largo, e um menu por troca seria atrito.
void MainShell::draw_record_mode_button(SqlDocument& document) {
    const bool on = document.record_mode();
    if (icon_button("##recordmode", on ? Icon::table : Icon::record,
                    on ? TR("Back to the grid (Tab)")
                       : TR("Single record view (Tab)"))) {
        document.set_record_mode(!on);
    }
}

// Visao de registro unico: os atributos de UMA linha, em pilha.
//
// Reusa draw_grid_cell inteira -- edicao, cor condicional, marca de alterado
// e menu de contexto vem de graca. Uma implementacao propria do desenho da
// celula seria um segundo lugar para cada uma dessas regras divergir.
void MainShell::draw_record_view(SqlDocument& document, const db::ResultSet& rs) {
    const Palette& p = colors();

    if (rs.row_count() == 0) {
        ImGui::TextColored(col4(p.text_dim), TR("no rows"));
        return;
    }

    // A linha mostrada e' a SELECIONADA. Sem selecao, a primeira -- e' o que
    // o DBeaver faz em changeMode(), e evita uma visao vazia logo ao entrar.
    std::size_t row = 0;
    if (has_selection_ && selected_document_ == document.id() &&
        selected_row_ < rs.row_count()) {
        row = selected_row_;
    }

    // Navegacao entre registros. No modo registro as setas verticais andam
    // entre ATRIBUTOS -- e' a orientacao da tela -- entao trocar de registro
    // vai nos botoes e nas setas horizontais, como no DBeaver.
    ImGui::BeginDisabled(row == 0);
    if (icon_button("##prevrec", Icon::chevron_left, TR("Previous record"))) {
        select_grid_cell(document, row - 1, selected_column_, false);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text(TR("record %zu of %zu"), row + 1, rs.row_count());

    ImGui::SameLine();
    ImGui::BeginDisabled(row + 1 >= rs.row_count());
    if (icon_button("##nextrec", Icon::chevron_right, TR("Next record"))) {
        select_grid_cell(document, row + 1, selected_column_, false);
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

    if (!ImGui::BeginTable("##record", 3, kFlags)) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    // A coluna de valor NAO estica ate' a borda da janela.
    //
    // Esticando, um numero alinhado a' direita ficava a meia tela do proprio
    // rotulo -- ler "limite" e achar o valor exigia atravessar o vazio. 520 px
    // cabem o texto comum e o usuario pode arrastar quando precisar de mais.
    ImGui::TableSetupColumn(TR("Column"), ImGuiTableColumnFlags_WidthFixed, 200.0f);
    ImGui::TableSetupColumn(TR("Value"),  ImGuiTableColumnFlags_WidthFixed, 520.0f);
    ImGui::TableSetupColumn(TR("Type"),   ImGuiTableColumnFlags_WidthFixed, 140.0f);
    ImGui::TableHeadersRow();

    for (std::size_t c = 0; c < rs.column_count(); ++c) {
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const db::ColumnInfo& info = rs.column(c).info();

        // A chave em destaque: e' o que identifica o registro, e num cadastro
        // de 40 campos ela se perderia no meio dos outros.
        //
        // A fonte e' `key_columns` do alvo de edicao -- indices no ResultSet
        // --, e nao um campo da coluna: o ColumnInfo nao carrega essa
        // informacao, e o resultado pode vir de um SELECT que nao a trouxe.
        const auto& keys = document.edit_target().key_columns;
        if (std::find(keys.begin(), keys.end(), c) != keys.end()) {
            icon_inline(Icon::key, p.accent);
            ImGui::SameLine(0.0f, 4.0f);
        }
        ImGui::TextUnformatted(info.name.c_str());

        ImGui::TableSetColumnIndex(1);
        draw_grid_cell(document, rs, row, c);

        ImGui::TableSetColumnIndex(2);
        ImGui::TextColored(col4(p.text_dim), "%s", info.type_name.c_str());
    }

    ImGui::EndTable();
}

void MainShell::draw_grid_panel() {
    if (ImGui::Begin(TRW("Result", "###ResultPanel"))) {
        // O resultado pertence ao documento: trocar de aba troca a grade.
        SqlDocument* document = active_document();

        if (document != nullptr && document->is_object()) {
            // O editor de objeto mostra os dados na aba Data dele, como no
            // DBeaver. Desenhar a mesma grade aqui tambem daria duas grades do
            // mesmo documento disputando o teclado.
            ImGui::TextColored(col4(colors().text_dim),
                               TR("the data of this object is in its Data tab"));
        } else if (document == nullptr) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("run a query to see the result"));
        } else {
            // O documento pode ter varios resultados: as abas.
            draw_result_tabs(*document);
            draw_grid_view(*document);
        }
    }
    ImGui::End();
}

// A grade de UM documento, dentro da janela corrente: o painel "Result" para
// um script, a aba Data para um editor de objeto.
void MainShell::draw_grid_view(SqlDocument& document) {
    if (!document.result().has_value()) {
        ImGui::TextColored(col4(colors().text_dim),
                           TR("run a query to see the result"));
        // Um erro da última execução aparece mesmo sem resultado.
        if (!document.status().empty()) {
            ImGui::TextColored(col4(colors().error), "%s",
                               document.status().c_str());
        }
        return;
    }

    const db::ResultSet& rs = *document.result();
    const Palette& p = colors();

    // Teclado ANTES de desenhar: a celula selecionada precisa ja' estar
    // no lugar novo quando as celulas forem desenhadas, senao o destaque
    // e a rolagem ficam um quadro atrasados -- visivel como um piscar.
    handle_grid_keys(document, rs);

    draw_grid_toolbar(document, rs);
    if (rs.column_count() > 0) draw_session_actions(document, rs);
    draw_grid_filter_bar(document, rs);

    // Os menus que os comandos abrem (referencias, copiar como, filtrar
    // por valor). Antes de qualquer retorno antecipado: valem tambem no
    // modo registro e na apresentacao em texto.
    if (rs.column_count() > 0 && rs.row_count() > 0) {
        draw_grid_popups(document, rs);
    }

    // Alteracoes pendentes: contagem e os dois botoes. Fica acima da
    // grade, nao escondido num menu -- e' estado que o usuario precisa
    // ver sem procurar.
    if (document.edits().has_changes()) {
        const std::size_t rows = document.edits().touched_rows();

        icon_inline(Icon::warning, p.warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(p.warn),
                           TR("%zu change(s) in %zu row(s), not saved"),
                           document.edits().change_count(), rows);

        ImGui::SameLine();
        if (icon_text_button("##saveedits", Icon::commit, TR("Save changes"),
                             TR("Run the UPDATEs in a transaction"),
                             !session().busy())) {
            request_save_edits(document, /*commit_after=*/false);
        }
        ImGui::SameLine();
        if (icon_text_button("##discardedits", Icon::rollback,
                             TR("Discard"),
                             TR("Throw the pending changes away"))) {
            document.edits().clear();
        }
    } else if (!document.edit_target().editable() &&
               rs.row_count() > 0) {
        // Diz POR QUE nao da' para editar, em vez de deixar o usuario
        // tentar e nao conseguir (diretiva 6).
        ImGui::TextColored(col4(p.text_dim), TR("read-only: %s"),
                           TR(std::string(db::to_string(
                                  document.edit_target().refusal)).c_str()));
    }

    // Pivot SUBSTITUI a grade: as duas juntas duplicariam a tela sem
    // ajudar a ler nenhuma.
    if (document.pivot_active()) {
        draw_pivot_table(document, rs);
        return;
    }

    draw_group_bar(document, rs);
    draw_group_panel(document, rs);

    ImGui::Separator();

    if (rs.column_count() == 0) {
        ImGui::TextColored(col4(colors().ok), TR("command executed"));
        if (rs.affected_rows() >= 0) {
            ImGui::SameLine();
            ImGui::TextColored(col4(colors().text_dim),
                               TR(" (%lld row(s) affected)"),
                               static_cast<long long>(rs.affected_rows()));
        }
        return;
    }

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        // Hideable: "Hide columns" / "Show columns" da grade.
        ImGuiTableFlags_Hideable |
        // Ordem e largura valem para ESTE resultado, nesta execucao -- ver
        // o PushID abaixo.
        ImGuiTableFlags_NoSavedSettings |
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
            hint_fmt(
                TR("The grid cannot draw more than %zu columns.\n"
                   "Narrow the SELECT list to see the remaining ones."),
                kMaxColumns);
        }
    }

    // Modo registro: UMA linha por vez, os atributos em pilha. E' o
    // `toggleMode` do DBeaver, e existe para tabela larga -- com 40
    // colunas, a grade obriga a rolar na horizontal para ler um cadastro.
    if (document.record_mode()) {
        draw_record_view(document, rs);
        return;
    }

    GridView& view = document.grid_view();

    // A barra de baixo (gravar, linhas, navegacao, paineis) fica sempre
    // a' vista: a tabela cede a altura dela.
    const float bar_height =
        ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y + 2.0f;

    // Apresentacao em texto (`resultset.switchPresentation`).
    if (view.presentation == GridPresentation::text) {
        ImGui::BeginChild("##textarea", ImVec2(0.0f, -bar_height));
        draw_grid_text(document, rs);
        ImGui::EndChild();
        draw_grid_bottom_bar(document, rs);
        return;
    }

    // Soltou o botao: o arrasto de selecao acabou.
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) grid_drag_document_ = 0;

    // Uma tabela do ImGui POR FORMA de resultado (aba + nomes das colunas).
    //
    // Com um id so' ("##results") para todos, o ImGui reaproveitava a
    // ordem das colunas de um resultado no seguinte, casando-as pelo
    // NOME: `pedido` tem cliente_id na segunda posicao, e ao abrir
    // `cliente` a coluna cliente_id ia para a segunda posicao tambem --
    // a grade mostrava "nome, cliente_id, ..." para um SELECT que devolve
    // "cliente_id, nome, ...". Visto na tela, seguindo uma chave
    // estrangeira.
    ImGuiID shape = ImHashData(&columns, sizeof columns);
    for (int c = 0; c < columns; ++c) {
        const std::string& name =
            rs.column(static_cast<std::size_t>(c)).info().name;
        shape = ImHashStr(name.c_str(), name.size(), shape);
    }
    ImGui::PushID(static_cast<int>(document.id()));
    ImGui::PushID(static_cast<int>(document.active_result_tab_id()));
    ImGui::PushID(static_cast<int>(shape));

    // Zoom (`resultset.zoomIn` / `zoomOut`): so' a grade muda de tamanho.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * view.zoom);

    if (ImGui::BeginTable("##results", columns, flags,
                          ImVec2(0.0f, -bar_height))) {
        ImGui::TableSetupScrollFreeze(1, 1);   // cabecalho e 1a coluna fixos

        for (int c = 0; c < columns; ++c) {
            // Sem DefaultSort: a ordem inicial e' a do servidor. Ordenar
            // sem o usuario pedir esconderia a ordem natural do resultado,
            // que num SELECT com ORDER BY proprio e' justamente o ponto.
            ImGui::TableSetupColumn(
                rs.column(static_cast<std::size_t>(c)).info().name.c_str());
        }

        // Esconder, mover e ajustar largura -- o que os comandos pediram
        // -- e a ordem das colunas na tela, que a navegacao usa.
        apply_grid_table_requests(document, rs, columns);

        // Cabecalhos um a um, em vez de TableHeadersRow(): cada um ganha
        // menu de contexto proprio, com o filtro daquela coluna.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < columns; ++c) {
            if (view.is_hidden(static_cast<std::size_t>(c))) continue;
            ImGui::TableSetColumnIndex(c);
            const std::string& name =
                rs.column(static_cast<std::size_t>(c)).info().name;

            // Coluna filtrada leva um prefixo no rotulo, nao um icone ao
            // lado: TableHeader ocupa a largura toda da celula, e
            // qualquer SameLine depois dele desenha POR CIMA do texto.
            const bool filtered = !document.filter().expression_for(name).empty();

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

            draw_column_header_menu(document, rs,
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
            if (document.paged() && !session().busy()) {
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

                if (order.column != document.sort().column ||
                    order.descending != document.sort().descending) {
                    document.set_sort(std::move(order));
                    // Volta para a primeira pagina: continuar na pagina 5
                    // de uma ordem diferente nao corresponde a nada.
                    execute_page(document, 0);
                }
            }
        }

        // Uma linha NOVA, ainda nao inserida. Fundo verde: e' adicao, nao
        // alteracao -- a distincao importa antes de gravar.
        const auto draw_insertion = [&](std::size_t i) {
            const db::RowInsertion& insertion =
                document.edits().insertions()[i];

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(1000000 + i));

            for (int c = 0; c < columns; ++c) {
                const auto ci = static_cast<std::size_t>(c);
                if (view.is_hidden(ci)) continue;
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
                    document.edits().set_new_value(i, ci, buffer);
                }

                if (ImGui::BeginPopupContextItem("##newcellmenu")) {
                    if (ImGui::MenuItem(TR("Set NULL"))) {
                        document.edits().set_new_null(i, ci);
                    }
                    if (ImGui::MenuItem(TR("Remove row"))) {
                        document.edits().remove_new_row(i);
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        };

        // As linhas novas aparecem ONDE foram pedidas ("Add row" abaixo
        // da corrente, "insert before" acima). Copia dos indices: o
        // menu de uma linha nova pode remove-la no meio do laco.
        const std::size_t insertion_count = document.edits().insertions().size();
        const auto anchored_at = [&](std::size_t result_row) {
            for (std::size_t i = 0; i < insertion_count &&
                                    i < document.edits().insertions().size();
                 ++i) {
                if (document.edits().insertions()[i].anchor == result_row) {
                    draw_insertion(i);
                }
            }
        };

        // Virtualizacao: so' as linhas visiveis sao desenhadas. E' o que
        // torna 1M linhas viavel (ADR 0005).
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rs.row_count()));

        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto r = static_cast<std::size_t>(row);
                if (insertion_count > 0) anchored_at(r);

                ImGui::TableNextRow();

                // "Set row color": a linha inteira, por baixo das marcas
                // de celula (alterada, selecionada), que sao mais urgentes.
                if (!view.row_colors.empty()) {
                    if (const std::uint32_t tint = grid_row_color(document, rs, r)) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, tint);
                    }
                }

                for (int c = 0; c < columns; ++c) {
                    const auto ci = static_cast<std::size_t>(c);
                    if (view.is_hidden(ci)) continue;
                    ImGui::TableSetColumnIndex(c);
                    draw_grid_cell(document, rs, r, ci);
                }
            }
        }

        // As que ficam depois de todas (e as ancoradas alem do fim).
        for (std::size_t i = 0; i < document.edits().insertions().size(); ++i) {
            const std::size_t anchor = document.edits().insertions()[i].anchor;
            if (anchor >= rs.row_count()) draw_insertion(i);
        }

        ImGui::EndTable();
    }
    ImGui::PopFont();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();

    draw_grid_bottom_bar(document, rs);
}

void MainShell::draw_query_log_panel() {
    if (ImGui::Begin(TRW("Queries", "###QueriesPanel"))) {
        const std::vector<db::QueryLog> log = session().query_log();

        if (log.empty()) {
            ImGui::TextColored(col4(colors().text_dim), TR("no queries yet"));
            ImGui::End();
            return;
        }

        // Filtro por estado, como o SQLLogFilter do DBeaver. Depois de um
        // script de 40 comandos, achar o que falhou exige rolar a lista
        // inteira -- e as internas de catalogo enchem o log entre eles.
        ImGui::TextColored(col4(colors().text_dim), "%s", TR("Show:"));
        ImGui::SameLine();
        ImGui::Checkbox(TR("failed only"), &query_log_failed_only_);
        ImGui::SameLine();
        ImGui::Checkbox(TR("catalog queries"), &query_log_show_internal_);
        if (ImGui::IsItemHovered()) {
            hint_fmt("%s",
                              TR("The queries C-Otter runs on its own to read "
                                 "the catalog. DBeaver hides these."));
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Clear log"))) session().clear_query_log();

        // Contagem do que esta' VISIVEL, nao do log inteiro: com filtro
        // ligado, dizer "412 queries" e mostrar 3 linhas seria contradicao.
        std::size_t shown = 0;
        for (const db::QueryLog& entry : log) {
            if (query_log_failed_only_ && !entry.failed) continue;
            if (!query_log_show_internal_ && entry.internal) continue;
            ++shown;
        }

        if (shown == log.size()) {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("%zu query(s)  |  including internal catalog ones"),
                               log.size());
        } else {
            ImGui::TextColored(col4(colors().text_dim),
                               TR("%zu of %zu query(s)"), shown, log.size());
        }
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
                if (query_log_failed_only_ && !entry.failed) continue;
                if (!query_log_show_internal_ && entry.internal) continue;

                ImGui::PushID(static_cast<int>(i));
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

                // Selecionavel de largura total para o menu de contexto ter
                // onde pegar: TextUnformatted nao responde a clique.
                ImGui::Selectable(single_line.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns);

                if (ImGui::IsItemHovered()) {
                    // O erro junto do SQL: sem isto, descobrir POR QUE a
                    // query falhou exigia procurar a mensagem na barra, que
                    // ja' tinha sido substituida pela query seguinte.
                    if (entry.failed && !entry.error.empty()) {
                        hint_fmt("%s\n\n%s", entry.sql.c_str(),
                                          entry.error.c_str());
                    } else {
                        hint_fmt("%s", entry.sql.c_str());
                    }
                }

                // Menu de contexto, nas acoes do QueryLogViewer do DBeaver.
                if (ImGui::BeginPopupContextItem("##logmenu")) {
                    if (ImGui::MenuItem(TR("Copy SQL"))) {
                        ImGui::SetClipboardText(entry.sql.c_str());
                    }
                    if (ImGui::MenuItem(TR("Open in SQL editor"))) {
                        // Abre numa aba nova SEM executar: o log guarda o que
                        // ja' rodou, e reexecutar um UPDATE por engano ao
                        // inspecionar o historico seria destrutivo.
                        open_sql_tab(entry.sql, /*run=*/false);
                    }
                    if (ImGui::MenuItem(TR("Copy error"), nullptr, false,
                                        entry.failed && !entry.error.empty())) {
                        ImGui::SetClipboardText(entry.error.c_str());
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
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

            // Cadeado so' quando o canal esta' REALMENTE cifrado. Nao ha'
            // simbolo para "em claro": um icone de cadeado aberto some no meio
            // da barra, e a ausencia do fechado e' a informacao -- desde que
            // o cadeado nunca minta quando aparece.
            if (const std::string channel = session().secure_channel();
                !channel.empty()) {
                icon_inline(Icon::lock, colors().ok);
                if (ImGui::IsItemHovered()) {
                    hint_fmt("%s: %s", TR("Encrypted connection"),
                                      channel.c_str());
                }
                ImGui::SameLine();
            }

            // Estado da transacao, como o TransactionMonitorToolbar do
            // DBeaver: "Auto" fora de transacao, "None" em transacao sem
            // alteracoes, e a CONTAGEM quando ha' o que perder.
            //
            // A contagem e' o ponto: saber que ha' 37 alteracoes pendentes
            // muda a decisao de fechar a janela. Um rotulo fixo "transacao
            // aberta" nao diria quanto esta' em jogo.
            const db::TxnState txn = session().txn_state();
            const std::size_t pending = session().uncommitted_changes();

            const char* txn_label =
                session().auto_commit()        ? TR("Auto")
                : txn == db::TxnState::failed  ? TR("Failed")
                : pending > 0                  ? nullptr
                                               : TR("None");

            // Amarelo cresce com o que ha' a perder; vermelho quando a
            // transacao abortou e so' ROLLBACK e' aceito.
            const std::uint32_t txn_color =
                txn == db::TxnState::failed ? colors().error
                : pending > 0               ? colors().warn
                                            : colors().text_dim;

            if (txn_label != nullptr) {
                ImGui::TextColored(col4(txn_color), "%s", txn_label);
            } else {
                // So' o numero, como no DBeaver: "37" chama mais atencao que
                // "37 alteracoes" numa barra que se le' de relance.
                ImGui::TextColored(col4(txn_color), "%zu", pending);
            }

            if (ImGui::IsItemHovered()) {
                if (session().auto_commit()) {
                    hint_fmt("%s", TR("Auto-commit: each statement "
                                               "commits on its own."));
                } else if (txn == db::TxnState::failed) {
                    hint_fmt("%s", TR("Transaction aborted; only "
                                               "rollback is accepted."));
                } else {
                    hint_fmt(TR("%zu modifying statement(s) pending"),
                                      pending);
                }
            }
            ImGui::SameLine();

            // Schema corrente. O DBeaver o mostra porque um SELECT sem
            // qualificar depende dele -- e um search_path inesperado faz a
            // consulta certa ler a tabela errada.
            if (const std::string schema = session().current_schema();
                !schema.empty()) {
                ImGui::TextColored(col4(colors().text_dim), "| %s",
                                   schema.c_str());
                if (ImGui::IsItemHovered()) {
                    hint_fmt("%s", TR("Current schema"));
                }
                ImGui::SameLine();
            }

            // Fecha o grupo do estado da conexao antes da mensagem: sem esta
            // barra, "Auto" e "conectado | 3 schema(s)" ficavam colados e
            // pareciam uma frase so'.
            ImGui::TextColored(col4(colors().text_dim), "|");
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
                                db::ConnectionProfile{},
                                next_connection_id_++});
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

MainShell::Connection* MainShell::connection_by_id(std::size_t id) {
    for (Connection& connection : connections_) {
        if (connection.id == id) return &connection;
    }
    return nullptr;   // conexao fechada; a aba continua, sem poder executar
}

const MainShell::Connection* MainShell::connection_by_id(std::size_t id) const {
    return const_cast<MainShell*>(this)->connection_by_id(id);
}

// A sessao DO DOCUMENTO, nao a ativa.
//
// Se a conexao dele foi fechada, devolve a Session vazia de session(): ela
// responde disconnected para tudo, e os pontos que chamam isto ja' tratam
// esse estado (o botao Executar fica desabilitado).
Session& MainShell::session_for(const SqlDocument& document) {
    Connection* connection = connection_by_id(document.connection_id());
    return connection != nullptr ? *connection->session : session();
}

std::string MainShell::connection_title(const Connection& connection) const {
    // A sessao de OUTRO banco do servidor (ADR 0018) leva o nome da conexao
    // raiz: o perfil dela se chama "raiz / banco", e o banco ja' vai entre
    // parenteses.
    std::string name = connection.profile.effective_name();
    if (connection.parent_id != 0) {
        if (const Connection* root = connection_by_id(connection.parent_id)) {
            name = root->profile.effective_name();
        }
    }

    // O banco em que a sessao ESTA' (o servidor pode ter escolhido o padrao do
    // login); antes de conectar, o do perfil.
    std::string database = connection.session->database_name();
    // No MySQL o banco troca na MESMA sessao (USE, pelo icone da aba): vale o
    // corrente, nao o com que ela abriu.
    if (connection.session->is_mysql()) {
        if (std::string current = connection.session->current_schema(); !current.empty()) {
            database = std::move(current);
        }
    }
    if (database.empty()) database = connection.profile.database;

    // Perfil sem nome ja' se chama "banco@host": repetir o banco seria ruido.
    if (database.empty() || name.starts_with(database + "@")) return name;
    return name + " (" + database + ")";
}

Session& MainShell::open_connection(const db::ConnectionProfile& profile) {
    // Reusa a Session vazia criada por session(): abrir a primeira conexao
    // nao deve deixar uma aba morta para tras.
    const bool reuse_empty =
        connections_.size() == 1 &&
        connections_.front().session->state() == SessionState::disconnected;

    // A conexao ja' tem entrada, fechada: e' a de um script reaberto ao
    // iniciar (ui/script_session.cpp), ou uma que foi desconectada. Conecta
    // NELA -- uma segunda entrada deixaria os scripts presos a' primeira, que
    // nunca conectaria.
    const std::size_t existing = find_root_connection(profile);
    const bool reuse_existing =
        existing < connections_.size() &&
        connections_[existing].session->state() != SessionState::connected &&
        connections_[existing].session->state() != SessionState::connecting;

    if (reuse_existing) {
        connections_[existing].profile = profile;
        active_connection_ = existing;
    } else if (reuse_empty) {
        connections_.front().profile = profile;
        active_connection_ = 0;
    } else {
        connections_.push_back({std::make_unique<Session>(), profile,
                                next_connection_id_++});
        active_connection_ = connections_.size() - 1;
    }

    // Toda conexao nasce com um script vazio: conectar ja' deixa onde
    // digitar, sem exigir um clique em "+" antes.
    const std::size_t conn_id = connections_[active_connection_].id;
    bool has_document = false;
    for (const std::unique_ptr<SqlDocument>& document : documents_) {
        if (document->connection_id() == conn_id) {
            has_document = true;
            break;
        }
    }
    if (!has_document) {
        SqlDocument& document = new_document();
        document.set_connection_id(conn_id);
    }

    // A conexao recem-aberta passa a ser a ativa tambem para o perfil: sem
    // isto, conectar por duplo clique num perfil salvo deixava
    // active_profile_ apontando para a conexao ANTERIOR, e "Editar" abria o
    // perfil errado.
    active_profile_ = profile;

    Connection& opened = connections_[active_connection_];
    opened.parent_id   = 0;
    opened.expand_once = true;
    connect_session(opened);
    return *opened.session;
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

    // Nomes repetidos gravados antes da regra de nome unico -- ou vindos de
    // edicao manual do JSON -- sao corrigidos ja' na leitura, e gravados:
    // o Raft nunca chega a mostrar dois rotulos iguais.
    if (db::make_names_unique(saved_profiles_) > 0) persist_profiles();
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

db::ConnectionProfile MainShell::remember_profile(
    const db::ConnectionProfile& wanted) {
    db::ConnectionProfile profile = wanted;

    // Mesmo DRIVER + host + porta + banco + usuario e' a MESMA conexao, mesmo
    // que o nome tenha mudado: senao, editar o rotulo criaria uma entrada
    // duplicada.
    //
    // O driver entra na comparacao porque um PostgreSQL e um MySQL no mesmo
    // host podem ter porta, banco e usuario iguais -- e sem ele, trocar o
    // driver na aba do dialogo SOBRESCREVIA o perfil do outro banco, deixando
    // provider e driver apontando para o protocolo errado. O sintoma era um
    // timeout em "reading packet header" (mensagem do driver MySQL) ao
    // conectar num PostgreSQL.
    const auto same_target = [&profile](const db::StoredProfile& stored) {
        return stored.profile.driver_id == profile.driver_id &&
               stored.profile.host == profile.host &&
               stored.profile.port == profile.port &&
               stored.profile.database == profile.database &&
               stored.profile.user == profile.user;
    };

    const db::ProviderNames names = db::provider_for_driver(profile.driver_id);

    // Nome exibido que nenhum OUTRO perfil usa. O proprio perfil (o de mesmo
    // alvo) fica fora da lista: regravar "x" nao pode virar "x_1".
    std::vector<std::string> taken;
    for (const db::StoredProfile& stored : saved_profiles_) {
        if (!same_target(stored)) taken.push_back(stored.profile.effective_name());
    }
    if (const std::string name = db::unique_name(profile.effective_name(), taken);
        name != profile.effective_name()) {
        profile.name = name;
    }

    for (db::StoredProfile& stored : saved_profiles_) {
        if (!same_target(stored)) continue;

        const std::string id = stored.id;   // preserva o id e o raw_json
        stored.profile  = profile;
        stored.id       = id;

        // O provider acompanha o driver mesmo num perfil existente: um perfil
        // gravado antes desta correcao pode ter os dois divergindo.
        stored.provider = std::string(names.provider);
        stored.driver   = std::string(names.driver);

        persist_profiles();
        return profile;
    }

    db::StoredProfile fresh;
    fresh.profile   = profile;

    // Vem do DRIVER do perfil, e nao cravado em "postgresql": com o literal,
    // todo perfil MySQL criado na tela era gravado como PostgreSQL, e so' a
    // releitura revelava -- conectando com o protocolo errado.
    fresh.provider  = std::string(names.provider);
    fresh.driver    = std::string(names.driver);
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
    return profile;
}

void MainShell::draw_import_window() {
    const Palette& p = colors();

    // Larga o bastante para o motivo caber inteiro: "o driver MySQL ainda
    // nao foi implementado" cortado no meio nao informa nada.
    ImGui::SetNextWindowSize(ImVec2(1000, 560), ImGuiCond_Appearing);
    // O que importar uma candidata faz. A mesma conexao (driver + host + porta
    // + banco + usuario) nunca entra duas vezes: se ja' esta' aqui SEM senha e
    // a outra ferramenta tem a senha, e' a senha que vem -- o caso das
    // conexoes do pgAdmin importadas antes de o C-Otter saber decifra-las.
    //
    // O mesmo vale para o GRUPO: uma conexao que ja' esta' aqui na raiz vai
    // para o grupo da ferramenta de onde veio. `import_password` e
    // `import_group` se combinam (1, 4 ou 5).
    enum ImportAction : int {
        import_add = 0, import_password = 1, import_nothing = 2, import_group = 4,
    };
    const auto updates_existing = [](int action) {
        return action != import_add && action != import_nothing;
    };

    const auto target_of = [](const db::StoredProfile& stored) {
        const db::ConnectionProfile& profile = stored.profile;
        return stored.provider + "|" + profile.host + "|" + std::to_string(profile.port) +
               "|" + profile.database + "|" + profile.user;
    };
    const auto saved_with_target = [this, &target_of](const db::StoredProfile& candidate)
        -> db::StoredProfile* {
        const std::string wanted = target_of(candidate);
        for (db::StoredProfile& stored : saved_profiles_) {
            if (target_of(stored) == wanted) return &stored;
        }
        return nullptr;
    };

    // Opaca (diretiva 13): a janela flutua sobre a arvore, e com a
    // translucidez dos paineis os nomes de tras atravessavam a lista.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col(with_alpha(p.bg_darkest, 1.0f)));
    const bool import_visible = ImGui::Begin(TRW("Import connections", "###ImportDBeaver"),
                                             &show_import_, ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    if (import_visible) {

        // Varre uma vez ao abrir; reabrir a janela nao deve reler o disco a
        // cada quadro.
        if (!import_scanned_) {
            import_scanned_ = true;
            import_selected_.clear();
            import_actions_.clear();

            // As tres ferramentas da primeira execucao (ADR 0023): DBeaver,
            // pgAdmin e SSMS, sem repetir o mesmo alvo.
            import_candidates_ = std::move(db::external_profiles().profiles);

            for (const db::StoredProfile& stored : import_candidates_) {
                int action = import_add;
                if (const db::StoredProfile* existing = saved_with_target(stored)) {
                    if (existing->profile.password.empty() &&
                        !stored.profile.password.empty()) {
                        action |= import_password;
                    }
                    // So' quem esta' na RAIZ: uma conexao que o usuario ja' pos
                    // numa pasta fica onde ele a pos.
                    if (existing->profile.folder.empty() && !stored.profile.folder.empty()) {
                        action |= import_group;
                    }
                    if (action == import_add) action = import_nothing;
                }
                import_actions_.push_back(action);
                // Vem marcado o que da' para usar e ainda falta aqui; o resto
                // fica desmarcado mas visivel, com o motivo.
                import_selected_.push_back(stored.supported && action != import_nothing);
            }
        }

        if (import_candidates_.empty()) {
            ImGui::TextColored(col4(p.text_dim),
                               TR("No saved connections of DBeaver, pgAdmin or SQL "
                                  "Server Management Studio were found on this machine."));
            ImGui::End();
            return;
        }

        ImGui::TextColored(col4(p.text_dim),
                           TR("%zu connection(s) found in DBeaver, pgAdmin and SSMS. "
                              "Nothing is written back to them."),
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
                ImGui::BeginDisabled(!stored.supported ||
                                     import_actions_[i] == import_nothing);
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
                if (stored.supported && import_actions_[i] == import_nothing) {
                    ImGui::TextColored(col4(p.text_dim), TR("already here"));
                } else if (stored.supported && import_actions_[i] == import_password) {
                    ImGui::TextColored(col4(p.ok),
                                       TR("already here without a password: adds the password"));
                } else if (stored.supported && import_actions_[i] == import_group) {
                    ImGui::TextColored(col4(p.ok), TR("already here: moves to the group %s"),
                                       stored.profile.folder.c_str());
                } else if (stored.supported && updates_existing(import_actions_[i])) {
                    ImGui::TextColored(
                        col4(p.ok),
                        TR("already here: adds the password and moves to the group %s"),
                        stored.profile.folder.c_str());
                } else if (stored.supported) {
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

        if (import_passwords_only_) {
            import_passwords_only_ = false;
            for (std::size_t i = 0; i < import_selected_.size(); ++i) {
                import_selected_[i] = import_candidates_[i].supported &&
                                      updates_existing(import_actions_[i]);
            }
        }

        std::size_t picked = 0;
        for (const bool selected : import_selected_) {
            if (selected) ++picked;
        }

        ImGui::Separator();
        if (!import_status_.empty()) {
            ImGui::TextColored(col4(p.ok), "%s", import_status_.c_str());
        }

        const bool clicked =
            icon_text_button("##doimport", Icon::save, TR("Import selected"),
                             TR("Copy the selected connections into C-Otter"),
                             picked > 0);
        const bool submitted = import_connections_submit_ && picked > 0;
        import_connections_submit_ = false;
        if (clicked || submitted) {
            std::size_t imported = 0;
            std::size_t passwords = 0;
            std::size_t moved = 0;
            for (std::size_t i = 0; i < import_candidates_.size(); ++i) {
                if (!import_selected_[i]) continue;

                if (db::StoredProfile* existing = saved_with_target(import_candidates_[i])) {
                    // Ja' esta' aqui: so' entra o que faltava -- a senha, e o
                    // grupo de quem estava na raiz. O nome e o resto do perfil
                    // sao de quem ja' o editou.
                    if (existing->profile.password.empty() &&
                        !import_candidates_[i].profile.password.empty()) {
                        existing->profile.password      = import_candidates_[i].profile.password;
                        existing->profile.save_password = true;
                        ++passwords;
                    }
                    if (existing->profile.folder.empty() &&
                        !import_candidates_[i].profile.folder.empty()) {
                        existing->profile.folder = import_candidates_[i].profile.folder;
                        ++moved;
                    }
                    continue;
                }

                // Id novo: o do DBeaver pertence ao arquivo dele, e reusa-lo
                // criaria confusao se as duas ferramentas divergirem.
                db::StoredProfile copy = import_candidates_[i];
                copy.id.clear();
                saved_profiles_.push_back(std::move(copy));
                ++imported;
            }
            // Um "localhost" que ja' existe aqui repetiria nomes. O importado
            // e' que ganha o sufixo.
            db::make_names_unique(saved_profiles_);
            persist_profiles();
            import_status_ = std::string(
                TRF("%zu connection(s) imported, %zu password(s) added, %zu moved to a group",
                    imported, passwords, moved));
            // A lista muda de estado: o que entrou agora "ja' esta' aqui".
            import_scanned_ = false;
        }

        ImGui::SameLine();
        if (icon_text_button("##rescan", Icon::refresh, TR("Rescan"),
                             TR("Read the connections of the other tools again"))) {
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
        {Icon::lock, "lock"},           {Icon::record, "record"},
        {Icon::filter, "filter"},
        // Faltavam na galeria: os desenhos existiam e nao eram conferiveis
        // aqui, que e' o unico lugar onde se ve' todos lado a lado.
        {Icon::partition, "partition"},   {Icon::event, "event"},
        {Icon::user, "user"},             {Icon::grant, "grant"},
        {Icon::pg_server, "pg_server"},   {Icon::my_server, "my_server"},
        {Icon::ms_server, "ms_server"},   {Icon::sa_server, "sa_server"},
        {Icon::foreign_table, "foreign_table"},
        {Icon::aggregate, "aggregate"},
        {Icon::dependency, "dependency"},
        {Icon::rule, "rule"},
        {Icon::policy, "policy"},
        {Icon::inheritance, "inheritance"},
        {Icon::parameter, "parameter"},
        {Icon::event_trigger, "event_trigger"},
        {Icon::storage, "storage"},
        {Icon::foreign_wrapper, "foreign_wrapper"},
        {Icon::foreign_server, "foreign_server"},
        {Icon::user_mapping, "user_mapping"},
        {Icon::setting, "setting"},
        {Icon::role_group, "role_group"},
        {Icon::access_method, "access_method"},
        {Icon::operator_class, "operator_class"},
        {Icon::operator_family, "operator_family"},
        {Icon::encoding, "encoding"},
        {Icon::collation, "collation"},
        {Icon::language, "language"},
        {Icon::extension_available, "extension_available"},
        {Icon::administer, "administer"},
        {Icon::system_info, "system_info"},
        {Icon::sessions, "sessions"},
        {Icon::locks, "locks"},
        {Icon::synonym, "synonym"},
        {Icon::job, "job"},
        {Icon::job_step, "job_step"},
        {Icon::job_schedule, "job_schedule"},
        {Icon::play_new, "play_new"},
        {Icon::play_script, "play_script"},
        {Icon::plan, "plan"},
        {Icon::ai, "ai"},
        {Icon::terminal, "terminal"},
        {Icon::server_output, "server_output"},
        {Icon::exec_log, "exec_log"},
        {Icon::variables, "variables"},
        {Icon::outline, "outline"},
        {Icon::folder_database, "folder_database"},
        {Icon::folder_schema, "folder_schema"},
        {Icon::folder_table, "folder_table"},
        {Icon::folder_view, "folder_view"},
        {Icon::folder_link, "folder_link"},
        {Icon::folder_user, "folder_user"},
        {Icon::folder_constraint, "folder_constraint"},
        {Icon::folder_columns, "folder_columns"},
        {Icon::folder_admin, "folder_admin"},
        {Icon::folder_info, "folder_info"},
        {Icon::object_page, "object_page"},
        {Icon::accept, "accept"},
        {Icon::reject, "reject"},
        {Icon::row_add, "row_add"},
        {Icon::row_copy, "row_copy"},
        {Icon::row_edit, "row_edit"},
        {Icon::row_delete, "row_delete"},
        {Icon::panels, "panels"},
        {Icon::panel_calc, "panel_calc"},
        {Icon::panel_grouping, "panel_grouping"},
        {Icon::panel_metadata, "panel_metadata"},
        {Icon::panel_references, "panel_references"},
        {Icon::filter_apply, "filter_apply"},
        {Icon::filter_reset, "filter_reset"},
        {Icon::filter_config, "filter_config"},
        {Icon::filter_value, "filter_value"},
        {Icon::grid_mode, "grid_mode"},
        {Icon::generic_server, "generic_server"},
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

namespace otter::ui {

void MainShell::reapply_palettes() {
    for (auto& document : documents_) apply_editor_palette(document->editor());
}

} // namespace otter::ui

namespace otter::ui {

// Pedido do usuario (2026-10-01): "Ao abrir e a pasta nao existir ou estiver
// vazia deve copiar as conexoes salvas, se existirem, do DBeaver, do pgAdmin
// e do MS SQL Server Management Studio."
//
// So' na PRIMEIRA execucao (pasta ausente ou vazia): quem apagou as conexoes
// de proposito nao as quer de volta a cada abertura. Depois disso,
// "File > Import connections" continua la'.
//
// O que ja' estiver gravado fica, e as das outras ferramentas se SOMAM --
// sem repetir o mesmo alvo.
void MainShell::import_external_on_first_run(bool fresh) {
    if (!fresh) return;
    const db::StoreLocation location = db::otter_store_location();

    std::vector<db::StoredProfile> profiles;
    if (auto existing = db::load_profiles(location)) profiles = std::move(*existing);

    const auto target = [](const db::StoredProfile& stored) {
        const db::ConnectionProfile& p = stored.profile;
        return stored.provider + "|" + p.host + "|" + std::to_string(p.port) + "|" +
               p.database + "|" + p.user;
    };
    std::set<std::string> known;
    for (const db::StoredProfile& stored : profiles) known.insert(target(stored));

    db::ExternalProfiles found = db::external_profiles();
    std::size_t added = 0;
    for (db::StoredProfile& stored : found.profiles) {
        if (!known.insert(target(stored)).second) continue;
        profiles.push_back(std::move(stored));
        ++added;
    }
    if (added == 0) return;

    db::make_names_unique(profiles);

    std::error_code ec;
    std::filesystem::create_directories(location.directory, ec);
    if (auto status = db::save_profiles(location, profiles); !status) {
        import_status_ = status.error().to_string();
        return;
    }
    show_toast(TRF("%zu connection(s) imported from DBeaver (%zu), pgAdmin (%zu) "
                   "and SSMS (%zu)",
                   added, found.from_dbeaver, found.from_pgadmin, found.from_ssms));
}

} // namespace otter::ui
