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
    "copy",      "chevron_right", "chevron_down", "warning", "error",
    "info",      "clock",      "filter",
    "materialized_view", "index", "constraint", "foreign_key",
    "references", "sequence",  "function",  "procedure",  "trigger",
    "data_type", "extension",  "role",      "tablespace", "schema",
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
