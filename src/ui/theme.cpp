#include "ui/theme.hpp"

#include "imgui.h"

namespace otter::ui {
namespace {

// Converte 0xAABBGGRR para ImVec4 normalizado.
ImVec4 rgba(std::uint32_t c) {
    return ImVec4(
        static_cast<float>((c >> 0)  & 0xFF) / 255.0f,
        static_cast<float>((c >> 8)  & 0xFF) / 255.0f,
        static_cast<float>((c >> 16) & 0xFF) / 255.0f,
        static_cast<float>((c >> 24) & 0xFF) / 255.0f);
}

// Mistura duas cores, t=0 devolve a, t=1 devolve b.
ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

ImVec4 with_alpha(const ImVec4& c, float alpha) {
    return ImVec4(c.x, c.y, c.z, alpha);
}

} // namespace

void apply_otter_theme(ImGuiStyle& style) {
    const ImVec4 fur       = rgba(palette::fur);
    const ImVec4 fur_light = rgba(palette::fur_light);
    const ImVec4 data      = rgba(palette::data);
    const ImVec4 bg0       = rgba(palette::bg_darkest);
    const ImVec4 bg1       = rgba(palette::bg_dark);
    const ImVec4 bg2       = rgba(palette::bg_medium);
    const ImVec4 bg3       = rgba(palette::bg_light);
    const ImVec4 text      = rgba(palette::text);
    const ImVec4 text_dim  = rgba(palette::text_dim);

    ImVec4* c = style.Colors;

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = text_dim;
    c[ImGuiCol_WindowBg]              = bg1;
    c[ImGuiCol_ChildBg]               = bg1;
    c[ImGuiCol_PopupBg]               = bg0;
    c[ImGuiCol_Border]                = mix(bg3, fur, 0.25f);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]               = bg2;
    c[ImGuiCol_FrameBgHovered]        = mix(bg2, fur, 0.35f);
    c[ImGuiCol_FrameBgActive]         = mix(bg2, fur, 0.55f);

    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = mix(bg0, fur, 0.45f);
    c[ImGuiCol_TitleBgCollapsed]      = with_alpha(bg0, 0.75f);

    c[ImGuiCol_MenuBarBg]             = bg0;

    c[ImGuiCol_ScrollbarBg]           = with_alpha(bg0, 0.6f);
    c[ImGuiCol_ScrollbarGrab]         = bg3;
    c[ImGuiCol_ScrollbarGrabHovered]  = mix(bg3, fur, 0.5f);
    c[ImGuiCol_ScrollbarGrabActive]   = fur;

    c[ImGuiCol_CheckMark]             = data;
    c[ImGuiCol_SliderGrab]            = fur;
    c[ImGuiCol_SliderGrabActive]      = fur_light;

    c[ImGuiCol_Button]                = mix(bg2, fur, 0.30f);
    c[ImGuiCol_ButtonHovered]         = mix(bg2, fur, 0.60f);
    c[ImGuiCol_ButtonActive]          = fur;

    c[ImGuiCol_Header]                = mix(bg2, fur, 0.35f);
    c[ImGuiCol_HeaderHovered]         = mix(bg2, fur, 0.60f);
    c[ImGuiCol_HeaderActive]          = fur;

    c[ImGuiCol_Separator]             = mix(bg3, fur, 0.20f);
    c[ImGuiCol_SeparatorHovered]      = fur;
    c[ImGuiCol_SeparatorActive]       = fur_light;

    c[ImGuiCol_ResizeGrip]            = with_alpha(fur, 0.25f);
    c[ImGuiCol_ResizeGripHovered]     = with_alpha(fur, 0.60f);
    c[ImGuiCol_ResizeGripActive]      = fur;

    c[ImGuiCol_Tab]                   = mix(bg0, fur, 0.20f);
    c[ImGuiCol_TabHovered]            = mix(bg2, fur, 0.60f);
    c[ImGuiCol_TabSelected]           = mix(bg2, fur, 0.45f);
    c[ImGuiCol_TabSelectedOverline]   = data;          // acento teal na aba ativa
    c[ImGuiCol_TabDimmed]             = bg0;
    c[ImGuiCol_TabDimmedSelected]     = mix(bg0, fur, 0.30f);

    c[ImGuiCol_DockingPreview]        = with_alpha(data, 0.45f);
    c[ImGuiCol_DockingEmptyBg]        = bg0;

    c[ImGuiCol_PlotLines]             = data;
    c[ImGuiCol_PlotLinesHovered]      = fur_light;
    c[ImGuiCol_PlotHistogram]         = fur;
    c[ImGuiCol_PlotHistogramHovered]  = fur_light;

    // Tabelas: a grade e' o coracao do produto (ADR 0005).
    c[ImGuiCol_TableHeaderBg]         = mix(bg0, fur, 0.30f);
    c[ImGuiCol_TableBorderStrong]     = mix(bg3, fur, 0.35f);
    c[ImGuiCol_TableBorderLight]      = bg3;
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = with_alpha(bg2, 0.35f);

    c[ImGuiCol_TextSelectedBg]        = with_alpha(data, 0.40f);
    c[ImGuiCol_DragDropTarget]        = data;
    c[ImGuiCol_NavHighlight]          = fur;
    c[ImGuiCol_NavWindowingHighlight] = with_alpha(text, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = with_alpha(bg0, 0.60f);
    c[ImGuiCol_ModalWindowDimBg]      = with_alpha(bg0, 0.70f);

    // Geometria: cantos suaves, como uma lontra.
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(8, 5);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 5);
    style.CellPadding       = ImVec2(6, 3);   // grade compacta: mais linhas visiveis

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBarBorderSize  = 2.0f;

    style.ScrollbarSize     = 13.0f;
    style.GrabMinSize       = 11.0f;

    style.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_None;   // sem botao de colapso
}

} // namespace otter::ui
