#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace otter::ui {
namespace {

// --- Componentes de cor ------------------------------------------------------
// Formato 0xAABBGGRR, como o ImU32 do ImGui.

constexpr std::uint32_t channel(std::uint32_t color, int shift) {
    return (color >> shift) & 0xFFu;
}

constexpr std::uint32_t pack(std::uint32_t r, std::uint32_t g,
                             std::uint32_t b, std::uint32_t a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

ImVec4 to_vec4(std::uint32_t c) {
    return ImVec4(static_cast<float>(channel(c, 0))  / 255.0f,
                  static_cast<float>(channel(c, 8))  / 255.0f,
                  static_cast<float>(channel(c, 16)) / 255.0f,
                  static_cast<float>(channel(c, 24)) / 255.0f);
}

// --- Temas -------------------------------------------------------------------

// Escuro: a identidade original, derivada do logo.
Palette dark_palette() {
    Palette p{};
    p.bg_darkest   = 0xFF1A1613;
    p.bg_dark      = 0xFF241E1A;
    p.bg_medium    = 0xFF302823;
    p.bg_light     = 0xFF3D332C;

    p.text         = 0xFFE8E4E0;
    p.text_dim     = 0xFF9A9490;
    p.text_bright  = 0xFFFFFCF8;

    p.accent       = 0xFF3C60A8;   // #A8603C -- pelo da lontra
    p.accent_light = 0xFF6090CC;
    p.accent_dark  = 0xFF0C183C;
    p.data         = 0xFF908430;   // #308490 -- cilindro de banco
    p.data_light   = 0xFFB0A450;

    p.ok           = 0xFF5CB85C;
    p.warn         = 0xFF3CA8DC;
    p.error        = 0xFF4C4CDC;
    p.info         = 0xFFB0A450;

    p.syntax_keyword = p.accent_light;
    p.syntax_string  = 0xFF7CC47C;
    p.syntax_number  = 0xFFB0A450;
    p.syntax_comment = p.text_dim;

    p.glow           = p.data_light;
    p.glow_strength  = 0.55f;
    p.surface_alpha  = 0.94f;
    return p;
}

// Claro: mesma marca, fundos invertidos. O acento escurece para manter
// contraste sobre branco.
Palette light_palette() {
    Palette p{};
    p.bg_darkest   = 0xFFE8EDF2;
    p.bg_dark      = 0xFFF0F4F8;
    p.bg_medium    = 0xFFF8FAFC;
    p.bg_light     = 0xFFFFFFFF;

    p.text         = 0xFF2A2420;
    p.text_dim     = 0xFF706A66;
    p.text_bright  = 0xFF141010;

    p.accent       = 0xFF2E4A84;   // pelo, escurecido para contraste
    p.accent_light = 0xFF3C60A8;
    p.accent_dark  = 0xFF1C2E50;
    p.data         = 0xFF6E6424;
    p.data_light   = 0xFF908430;

    p.ok           = 0xFF3A8C3A;
    p.warn         = 0xFF2080B0;
    p.error        = 0xFF3030B0;
    p.info         = 0xFF6E6424;

    p.syntax_keyword = 0xFF2E4A84;
    p.syntax_string  = 0xFF2A7A2A;
    p.syntax_number  = 0xFF6E6424;
    p.syntax_comment = p.text_dim;

    // Glow discreto em tema claro: brilho sobre fundo claro vira borrao.
    p.glow           = p.accent;
    p.glow_strength  = 0.25f;
    p.surface_alpha  = 0.97f;
    return p;
}

// Ambar: fundo quase preto com dominante quente, acento ambar luminoso.
// E' onde o glow rende mais.
Palette amber_palette() {
    Palette p{};
    p.bg_darkest   = 0xFF0A0D14;
    p.bg_dark      = 0xFF10141E;
    p.bg_medium    = 0xFF181E2A;
    p.bg_light     = 0xFF222A38;

    p.text         = 0xFFD8E4F0;
    p.text_dim     = 0xFF7888A0;
    p.text_bright  = 0xFFF0F8FF;

    p.accent       = 0xFF14A0F0;   // ambar #F0A014
    p.accent_light = 0xFF40C0FF;
    p.accent_dark  = 0xFF0A70B0;
    p.data         = 0xFFD0A040;
    p.data_light   = 0xFFF0C060;

    p.ok           = 0xFF60D090;
    p.warn         = 0xFF40C0FF;
    p.error        = 0xFF6060FF;
    p.info         = 0xFFF0C060;

    p.syntax_keyword = 0xFF40C0FF;
    p.syntax_string  = 0xFF80E0A0;
    p.syntax_number  = 0xFFF0C060;
    p.syntax_comment = p.text_dim;

    p.glow           = p.accent_light;
    p.glow_strength  = 1.0f;
    p.surface_alpha  = 0.88f;      // mais translucido: reforca o ar futurista
    return p;
}

std::vector<Theme> build_themes() {
    return {
        {"Dark",  "dark",  dark_palette(),  true},
        {"Light", "light", light_palette(), false},
        {"Amber", "amber", amber_palette(), true},
    };
}

std::size_t g_active = 0;

} // namespace

std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    auto lerp = [t](std::uint32_t x, std::uint32_t y) {
        return static_cast<std::uint32_t>(
            static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * t);
    };
    return pack(lerp(channel(a, 0),  channel(b, 0)),
                lerp(channel(a, 8),  channel(b, 8)),
                lerp(channel(a, 16), channel(b, 16)),
                lerp(channel(a, 24), channel(b, 24)));
}

std::uint32_t lighten(std::uint32_t color, float amount) noexcept {
    return mix(color, pack(255, 255, 255, channel(color, 24)), amount);
}

std::uint32_t darken(std::uint32_t color, float amount) noexcept {
    return mix(color, pack(0, 0, 0, channel(color, 24)), amount);
}

const std::vector<Theme>& available_themes() {
    static const std::vector<Theme> themes = build_themes();
    return themes;
}

const Theme& current_theme() {
    const std::vector<Theme>& themes = available_themes();
    return themes[std::min(g_active, themes.size() - 1)];
}

const Palette& colors() { return current_theme().palette; }

bool set_theme(std::string_view id) {
    const std::vector<Theme>& themes = available_themes();
    for (std::size_t i = 0; i < themes.size(); ++i) {
        if (themes[i].id != id) continue;
        g_active = i;
        if (ImGui::GetCurrentContext() != nullptr) {
            apply_theme(ImGui::GetStyle());
        }
        return true;
    }
    return false;
}

void apply_theme(ImGuiStyle& style) {
    const Theme& theme = current_theme();
    const Palette& p = theme.palette;

    ImVec4* c = style.Colors;

    // Superfícies translúcidas: o fundo aparece por trás dos painéis, o que dá
    // profundidade sem sombra pintada.
    const ImVec4 window_bg = to_vec4(with_alpha(p.bg_dark, p.surface_alpha));
    // O popup e' OPACO. Translucido, o texto da arvore aparecia atras dos
    // itens de menu e se misturava aos atalhos ("F3" seguido de um "commit"
    // fantasma da barra) -- visto na captura do menu "SQL Editor".
    const ImVec4 popup_bg  = to_vec4(with_alpha(p.bg_darkest, 1.0f));

    c[ImGuiCol_Text]                  = to_vec4(p.text);
    c[ImGuiCol_TextDisabled]          = to_vec4(p.text_dim);
    c[ImGuiCol_WindowBg]              = window_bg;
    c[ImGuiCol_ChildBg]               = to_vec4(with_alpha(p.bg_dark, 0.0f));
    c[ImGuiCol_PopupBg]               = popup_bg;
    c[ImGuiCol_Border]                = to_vec4(with_alpha(
        mix(p.bg_light, p.accent, 0.30f), 0.55f));
    c[ImGuiCol_BorderShadow]          = to_vec4(0x00000000u);

    c[ImGuiCol_FrameBg]               = to_vec4(with_alpha(p.bg_medium, 0.80f));
    c[ImGuiCol_FrameBgHovered]        = to_vec4(mix(p.bg_medium, p.accent, 0.30f));
    c[ImGuiCol_FrameBgActive]         = to_vec4(mix(p.bg_medium, p.accent, 0.50f));

    c[ImGuiCol_TitleBg]               = to_vec4(p.bg_darkest);
    c[ImGuiCol_TitleBgActive]         = to_vec4(mix(p.bg_darkest, p.accent, 0.40f));
    c[ImGuiCol_TitleBgCollapsed]      = to_vec4(with_alpha(p.bg_darkest, 0.70f));
    c[ImGuiCol_MenuBarBg]             = to_vec4(with_alpha(p.bg_darkest, 0.92f));

    c[ImGuiCol_ScrollbarBg]           = to_vec4(with_alpha(p.bg_darkest, 0.40f));
    c[ImGuiCol_ScrollbarGrab]         = to_vec4(with_alpha(p.bg_light, 0.80f));
    c[ImGuiCol_ScrollbarGrabHovered]  = to_vec4(mix(p.bg_light, p.accent, 0.50f));
    c[ImGuiCol_ScrollbarGrabActive]   = to_vec4(p.accent);

    c[ImGuiCol_CheckMark]             = to_vec4(p.data_light);
    c[ImGuiCol_SliderGrab]            = to_vec4(p.accent);
    c[ImGuiCol_SliderGrabActive]      = to_vec4(p.accent_light);

    // Botões planos: sem preenchimento em repouso, cor só no hover. É o que dá
    // o aspecto moderno -- borda e fundo constantes poluem a barra.
    c[ImGuiCol_Button]                = to_vec4(with_alpha(p.bg_light, 0.0f));
    c[ImGuiCol_ButtonHovered]         = to_vec4(with_alpha(p.accent, 0.35f));
    c[ImGuiCol_ButtonActive]          = to_vec4(with_alpha(p.accent, 0.60f));

    c[ImGuiCol_Header]                = to_vec4(with_alpha(p.accent, 0.28f));
    c[ImGuiCol_HeaderHovered]         = to_vec4(with_alpha(p.accent, 0.45f));
    c[ImGuiCol_HeaderActive]          = to_vec4(with_alpha(p.accent, 0.62f));

    c[ImGuiCol_Separator]             = to_vec4(with_alpha(p.bg_light, 0.60f));
    c[ImGuiCol_SeparatorHovered]      = to_vec4(p.accent);
    c[ImGuiCol_SeparatorActive]       = to_vec4(p.accent_light);

    c[ImGuiCol_ResizeGrip]            = to_vec4(with_alpha(p.accent, 0.20f));
    c[ImGuiCol_ResizeGripHovered]     = to_vec4(with_alpha(p.accent, 0.50f));
    c[ImGuiCol_ResizeGripActive]      = to_vec4(p.accent);

    c[ImGuiCol_Tab]                   = to_vec4(with_alpha(p.bg_darkest, 0.70f));
    c[ImGuiCol_TabHovered]            = to_vec4(mix(p.bg_medium, p.accent, 0.45f));
    c[ImGuiCol_TabSelected]           = to_vec4(mix(p.bg_medium, p.accent, 0.32f));
    c[ImGuiCol_TabSelectedOverline]   = to_vec4(p.data_light);
    c[ImGuiCol_TabDimmed]             = to_vec4(with_alpha(p.bg_darkest, 0.55f));
    c[ImGuiCol_TabDimmedSelected]     = to_vec4(mix(p.bg_darkest, p.accent, 0.22f));

    c[ImGuiCol_DockingPreview]        = to_vec4(with_alpha(p.data, 0.45f));
    c[ImGuiCol_DockingEmptyBg]        = to_vec4(p.bg_darkest);

    c[ImGuiCol_PlotLines]             = to_vec4(p.data);
    c[ImGuiCol_PlotLinesHovered]      = to_vec4(p.accent_light);
    c[ImGuiCol_PlotHistogram]         = to_vec4(p.accent);
    c[ImGuiCol_PlotHistogramHovered]  = to_vec4(p.accent_light);

    // Tabelas: a grade é o coração do produto (ADR 0005).
    c[ImGuiCol_TableHeaderBg]         = to_vec4(mix(p.bg_darkest, p.accent, 0.26f));
    c[ImGuiCol_TableBorderStrong]     = to_vec4(with_alpha(
        mix(p.bg_light, p.accent, 0.30f), 0.70f));
    c[ImGuiCol_TableBorderLight]      = to_vec4(with_alpha(p.bg_light, 0.45f));
    c[ImGuiCol_TableRowBg]            = to_vec4(0x00000000u);
    c[ImGuiCol_TableRowBgAlt]         = to_vec4(with_alpha(p.bg_medium, 0.30f));

    c[ImGuiCol_TextSelectedBg]        = to_vec4(with_alpha(p.data, 0.40f));
    c[ImGuiCol_DragDropTarget]        = to_vec4(p.data);
    c[ImGuiCol_NavHighlight]          = to_vec4(p.accent);
    c[ImGuiCol_NavWindowingHighlight] = to_vec4(with_alpha(p.text, 0.70f));
    c[ImGuiCol_NavWindowingDimBg]     = to_vec4(with_alpha(p.bg_darkest, 0.60f));
    c[ImGuiCol_ModalWindowDimBg]      = to_vec4(with_alpha(p.bg_darkest, 0.70f));

    // Geometria: cantos generosos e bordas finas, para um ar leve.
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(9, 6);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 5);
    style.CellPadding       = ImVec2(6, 3);   // grade compacta

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBarBorderSize  = 2.0f;

    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 11.0f;

    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Bordas mais suaves em tema claro, onde o contraste já é alto.
    if (!theme.is_dark) style.WindowBorderSize = 1.0f;
}

} // namespace otter::ui
