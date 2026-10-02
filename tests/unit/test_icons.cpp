// Prova que cada tipo de objeto tem desenho proprio.
//
// Por que existe: a arvore chegou a usar Icon::filter para indice, Icon::commit
// para constraint e Icon::settings para funcao -- tres tipos diferentes com o
// mesmo desenho, o que torna a arvore ilegivel de relance (diretiva 5).
//
// Comparar os nomes do enum nao provaria nada: dois valores distintos podem
// cair no mesmo `case`. Este teste compara os VERTICES que cada icone emite no
// DrawList, que e' exatamente o que o usuario ve.
#include "test_main.hpp"

#include "ui/icon_assets.hpp"
#include "ui/icon_images.hpp"
#include "ui/icons.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdio>
#include <map>
#include <string>

using namespace otter::ui;

namespace {

// Contexto ImGui sem backend, vivo enquanto o teste roda.
//
// Montar um ImDrawListSharedData a mao seria mais leve, mas depende de campos
// internos que mudam entre versoes do ImGui. Um contexto de verdade custa uns
// poucos milissegundos e nao quebra no proximo upgrade.
struct HeadlessImGui {
    ImGuiContext* ctx;

    HeadlessImGui() : ctx(ImGui::CreateContext()) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime   = 1.0f / 60.0f;
        // Sem isto o ImGui grava "imgui.ini" no diretorio de trabalho ao
        // destruir o contexto: rodar a suite deixava o arquivo na raiz do
        // repositorio, e o usuario o encontrou la'.
        io.IniFilename = nullptr;
        // Sem backend nao ha' fonte carregada; o atlas precisa existir para o
        // NewFrame nao abortar no assert de fonte.
        io.Fonts->AddFontDefault();
        io.Fonts->Build();

        // NewFrame e' obrigatorio, nao opcional: o ImDrawListSharedData so'
        // recebe CurveTessellationTol e a tabela ArcFastVtx dentro dele. Sem
        // isso, o primeiro arco divide por zero ao calcular o passo angular.
        ImGui::NewFrame();
    }
    ~HeadlessImGui() {
        ImGui::EndFrame();
        ImGui::DestroyContext(ctx);
    }

    HeadlessImGui(const HeadlessImGui&)            = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;
};

// Assinatura geometrica de um icone: a lista de vertices que ele produz,
// quantizada. A quantizacao absorve ruido de ponto flutuante sem mascarar
// diferencas reais -- 0,25 px e' bem menor que qualquer traco visivel.
std::string signature(Icon icon) {
    ImDrawList dl(ImGui::GetDrawListSharedData());
    dl.AddDrawCmd();
    dl.PushClipRectFullScreen();
    dl.PushTextureID(ImGui::GetIO().Fonts->TexID);

    // Tamanho grande: diferencas sutis entre dois desenhos aparecem melhor
    // com mais resolucao, e o teste nao esta' medindo custo de render.
    draw_icon_to(&dl, icon, ImVec2(0.0f, 0.0f), 100.0f, 0xFFFFFFFF, 1.6f);

    std::string out;
    out.reserve(static_cast<std::size_t>(dl.VtxBuffer.Size) * 16);
    for (int i = 0; i < dl.VtxBuffer.Size; ++i) {
        const ImVec2 p = dl.VtxBuffer[i].pos;
        out += std::to_string(static_cast<int>(p.x * 4.0f));
        out += ',';
        out += std::to_string(static_cast<int>(p.y * 4.0f));
        out += ';';
    }
    return out;
}

// Nomes na ordem exata do enum Icon, para a falha dizer QUAL par colidiu.
// Um teste que so' informa "dois icones sao iguais" obriga a caca manual.
constexpr const char* kIconNames[] = {
    "connect",   "disconnect", "play",      "stop",       "commit",
    "rollback",  "database",   "table",     "view",       "column",
    "key",       "folder",     "refresh",   "search",     "settings",
    "plus",      "close",      "pin",       "save",       "open",
    "copy",      "chevron_left", "chevron_right",
    "first_page", "last_page", "chevron_down",
    "warning",   "error",
    "info",      "clock",      "lock",       "record",     "filter",
    "materialized_view", "index", "constraint", "foreign_key",
    "references", "sequence",  "function",  "procedure",  "trigger",
    "data_type", "extension",  "role",      "tablespace", "schema",
    "pivot",       "partition",  "event",      "user",       "grant",
    "foreign_table",
    "aggregate",
    "dependency",
    "rule",
    "policy",
    "inheritance",
    "parameter",
    "event_trigger",
    "storage",
    "foreign_wrapper",
    "foreign_server",
    "user_mapping",
    "setting",
    "role_group",
    "access_method",
    "operator_class",
    "operator_family",
    "encoding",
    "collation",
    "language",
    "extension_available",
    "administer",
    "system_info",
    "sessions",
    "locks",
    "synonym",
    "job",
    "job_step",
    "job_schedule",
    "play_new",
    "play_script",
    "plan",
    "ai",
    "terminal",
    "server_output",
    "exec_log",
    "variables",
    "outline",
    "folder_database",
    "folder_schema",
    "folder_table",
    "folder_view",
    "folder_link",
    "folder_user",
    "folder_constraint",
    "folder_columns",
    "folder_admin",
    "folder_info",
    "object_page",
    "accept",
    "reject",
    "row_add",
    "row_copy",
    "row_edit",
    "row_delete",
    "panels",
    "panel_calc",
    "panel_grouping",
    "panel_metadata",
    "panel_references",
    "filter_apply",
    "filter_reset",
    "filter_config",
    "filter_value",
    "grid_mode",
    "pg_server",   "my_server",  "ms_server",  "sa_server",  "generic_server",
};
static_assert(IM_ARRAYSIZE(kIconNames) == kIconCount,
              "kIconNames ficou fora de sincronia com o enum Icon");

} // namespace

OTTER_TEST(icons_are_all_distinct) {
    // Compara TODOS os icones entre si, nao so' os de objeto.
    //
    // A primeira versao deste teste so' olhava os tipos de objeto, e passou
    // com uma regressao injetada de proposito (constraint voltando a usar o
    // desenho de commit) -- justamente porque commit e' icone de acao e
    // estava fora da lista. A dívida original era commit/constraint,
    // filter/index, settings/function: pares acao-objeto. Comparar so' um
    // dos lados nao pega nenhum deles.
    HeadlessImGui imgui;

    std::map<std::string, std::size_t> seen;

    for (std::size_t i = 0; i < kIconCount; ++i) {
        const std::string sig = signature(static_cast<Icon>(i));
        OTTER_CHECK(!sig.empty());

        const auto [it, inserted] = seen.emplace(sig, i);
        if (!inserted) {
            std::printf("      icones identicos: %s e %s\n",
                        kIconNames[it->second], kIconNames[i]);
        }
        OTTER_CHECK(inserted);
    }
}

OTTER_TEST(icons_every_enum_value_draws_something) {
    // Acrescentar um valor ao enum sem o `case` correspondente no switch
    // produz um icone invisivel -- silencioso, porque o compilador so' avisa
    // com -Wswitch e o default nao existe.
    HeadlessImGui imgui;
    for (std::size_t i = 0; i < kIconCount; ++i) {
        const Icon icon = static_cast<Icon>(i);
        OTTER_CHECK(!signature(icon).empty());
    }
}

OTTER_TEST(icons_dbeaver_originals_all_rasterize) {
    // Um SVG que o nanosvg nao entende (ou um PNG embutido com tamanho
    // errado) nao da' erro: da' uma imagem vazia, e o icone some da tela sem
    // aviso. Cada original precisa produzir pixels visiveis, nos dois
    // tamanhos em que aparece (arvore e barra, com e sem HiDPI).
    std::size_t count = 0;
    for (const IconAsset& asset : icon_assets()) {
        ++count;
        for (const int pixels : {16, 18, 24, 32}) {
            const std::vector<unsigned char> rgba = rasterize_icon(asset.icon, pixels);
            const bool sized =
                rgba.size() == static_cast<std::size_t>(pixels) * pixels * 4;
            if (!sized) {
                std::printf("      sem imagem: %s em %d px\n",
                            kIconNames[static_cast<std::size_t>(asset.icon)], pixels);
            }
            OTTER_CHECK(sized);
            if (!sized) continue;

            std::size_t visible = 0;
            for (std::size_t i = 3; i < rgba.size(); i += 4) {
                if (rgba[i] > 32) ++visible;
            }
            if (visible < 8) {
                std::printf("      imagem vazia: %s em %d px (%zu pixels)\n",
                            kIconNames[static_cast<std::size_t>(asset.icon)], pixels,
                            visible);
            }
            OTTER_CHECK(visible >= 8);
        }
    }
    // O conjunto nao pode encolher em silencio se o gerador falhar pela metade.
    OTTER_CHECK(count >= 90);
}

OTTER_TEST(icons_without_texture_factory_fall_back_to_vector) {
    // Sem janela nao ha' textura. O conjunto do DBeaver ativo nao pode, por
    // isso, deixar de desenhar: cai no vetorial.
    const IconSet before = icon_set();
    set_icon_set(IconSet::dbeaver);
    set_icon_texture_factory(nullptr);

    OTTER_CHECK(icon_has_image(Icon::table));
    OTTER_CHECK(!icon_texture(Icon::table, 16));

    HeadlessImGui imgui;
    OTTER_CHECK(!signature(Icon::table).empty());
    set_icon_set(before);
}

OTTER_TEST(icons_folder_icon_follows_the_icon_set) {
    const IconSet before = icon_set();

    // C-Otter: a pasta leva o icone do conteudo, como sempre levou.
    set_icon_set(IconSet::otter);
    OTTER_CHECK(folder_icon(Icon::table) == Icon::table);
    OTTER_CHECK(folder_icon(Icon::index) == Icon::index);

    // DBeaver: o icon="#..." de cada <folder> do plugin.xml do PostgreSQL.
    set_icon_set(IconSet::dbeaver);
    OTTER_CHECK(folder_icon(Icon::table) == Icon::folder_table);
    OTTER_CHECK(folder_icon(Icon::partition) == Icon::folder_table);
    OTTER_CHECK(folder_icon(Icon::foreign_table) == Icon::folder_link);
    OTTER_CHECK(folder_icon(Icon::view) == Icon::folder_view);
    OTTER_CHECK(folder_icon(Icon::materialized_view) == Icon::folder_view);
    OTTER_CHECK(folder_icon(Icon::column) == Icon::folder_columns);
    OTTER_CHECK(folder_icon(Icon::constraint) == Icon::folder_constraint);
    OTTER_CHECK(folder_icon(Icon::role) == Icon::folder_user);
    OTTER_CHECK(folder_icon(Icon::storage) == Icon::folder_info);
    OTTER_CHECK(folder_icon(Icon::system_info) == Icon::folder_info);
    OTTER_CHECK(folder_icon(Icon::administer) == Icon::folder_admin);
    // "#indexes", "#triggers", "#sequences" nao existem no DBIcon: pasta comum.
    OTTER_CHECK(folder_icon(Icon::index) == Icon::folder);
    OTTER_CHECK(folder_icon(Icon::trigger) == Icon::folder);
    OTTER_CHECK(folder_icon(Icon::sequence) == Icon::folder);

    OTTER_CHECK(icon_set_from_id(icon_set_id(IconSet::otter)) == IconSet::otter);
    OTTER_CHECK(icon_set_from_id("qualquer") == IconSet::dbeaver);
    set_icon_set(before);
}
