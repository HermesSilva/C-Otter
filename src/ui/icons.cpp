#include "ui/icons.hpp"

#include "ui/theme.hpp"

#include "imgui.h"

#include <cmath>

namespace otter::ui {
namespace {

// Os icones sao desenhados numa caixa normalizada de -0.5..0.5 e escalados
// para `size`. Assim um mesmo desenho serve para a barra e para a arvore.
struct Canvas {
    ImDrawList*   dl;
    ImVec2        center;
    float         size;
    std::uint32_t color;
    float         thickness;

    [[nodiscard]] ImVec2 at(float x, float y) const {
        return ImVec2(center.x + x * size, center.y + y * size);
    }

    void line(float x1, float y1, float x2, float y2) const {
        dl->AddLine(at(x1, y1), at(x2, y2), color, thickness);
    }

    void rect(float x1, float y1, float x2, float y2, float rounding = 0.0f) const {
        dl->AddRect(at(x1, y1), at(x2, y2), color, rounding * size,
                    ImDrawFlags_None, thickness);
    }

    void rect_filled(float x1, float y1, float x2, float y2,
                     float rounding = 0.0f) const {
        dl->AddRectFilled(at(x1, y1), at(x2, y2), color, rounding * size);
    }

    void circle(float x, float y, float radius, int segments = 0) const {
        dl->AddCircle(at(x, y), radius * size, color, segments, thickness);
    }

    void circle_filled(float x, float y, float radius) const {
        dl->AddCircleFilled(at(x, y), radius * size, color, 0);
    }

    // Elipse achatada: base e topo do cilindro de banco de dados.
    void ellipse(float x, float y, float rx, float ry, bool filled = false) const {
        constexpr int kSegments = 24;
        ImVec2 points[kSegments];
        for (int i = 0; i < kSegments; ++i) {
            const float a = 2.0f * 3.14159265f * static_cast<float>(i) / kSegments;
            points[i] = at(x + rx * std::cos(a), y + ry * std::sin(a));
        }
        if (filled) {
            dl->AddConvexPolyFilled(points, kSegments, color);
        } else {
            dl->AddPolyline(points, kSegments, color, ImDrawFlags_Closed, thickness);
        }
    }

    // Arco por angulos em graus, sentido horario a partir do leste.
    void arc(float x, float y, float radius, float from_deg, float to_deg) const {
        dl->PathArcTo(at(x, y), radius * size,
                      from_deg * 3.14159265f / 180.0f,
                      to_deg * 3.14159265f / 180.0f, 20);
        dl->PathStroke(color, ImDrawFlags_None, thickness);
    }

    void triangle_filled(float x1, float y1, float x2, float y2,
                         float x3, float y3) const {
        dl->AddTriangleFilled(at(x1, y1), at(x2, y2), at(x3, y3), color);
    }
};

void draw_connect(const Canvas& c) {
    // Plugue: dois pinos e um corpo.
    c.line(-0.18f, -0.42f, -0.18f, -0.18f);
    c.line( 0.18f, -0.42f,  0.18f, -0.18f);
    c.rect(-0.32f, -0.18f, 0.32f, 0.10f, 0.12f);
    c.line(0.0f, 0.10f, 0.0f, 0.42f);
}

void draw_disconnect(const Canvas& c) {
    draw_connect(c);
    // Barra diagonal de "desligado".
    c.line(-0.40f, 0.40f, 0.40f, -0.40f);
}

void draw_play(const Canvas& c) {
    c.triangle_filled(-0.22f, -0.34f, -0.22f, 0.34f, 0.34f, 0.0f);
}

void draw_stop(const Canvas& c) {
    c.rect_filled(-0.28f, -0.28f, 0.28f, 0.28f, 0.10f);
}

void draw_commit(const Canvas& c) {
    // Check.
    c.dl->PathLineTo(c.at(-0.32f, 0.02f));
    c.dl->PathLineTo(c.at(-0.10f, 0.26f));
    c.dl->PathLineTo(c.at( 0.34f, -0.24f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness * 1.3f);
}

void draw_rollback(const Canvas& c) {
    // Seta circular anti-horaria.
    c.arc(0.0f, 0.02f, 0.30f, 150.0f, 400.0f);
    c.triangle_filled(-0.34f, -0.06f, -0.14f, -0.06f, -0.24f, -0.30f);
}

void draw_database(const Canvas& c) {
    // Cilindro: topo elipse, laterais, base em arco.
    c.ellipse(0.0f, -0.26f, 0.32f, 0.13f);
    c.line(-0.32f, -0.26f, -0.32f, 0.22f);
    c.line( 0.32f, -0.26f,  0.32f, 0.22f);
    c.arc(0.0f, 0.22f, 0.32f, 0.0f, 180.0f);
    c.arc(0.0f, -0.02f, 0.32f, 10.0f, 170.0f);
}

void draw_table(const Canvas& c) {
    c.rect(-0.34f, -0.30f, 0.34f, 0.30f, 0.08f);
    c.line(-0.34f, -0.12f, 0.34f, -0.12f);   // cabeçalho
    c.line(-0.34f,  0.08f, 0.34f,  0.08f);
    c.line(-0.06f, -0.12f, -0.06f, 0.30f);
}

void draw_view(const Canvas& c) {
    // Olho.
    c.dl->PathLineTo(c.at(-0.38f, 0.0f));
    c.dl->PathBezierCubicCurveTo(c.at(-0.18f, -0.28f), c.at(0.18f, -0.28f),
                                 c.at(0.38f, 0.0f), 16);
    c.dl->PathBezierCubicCurveTo(c.at(0.18f, 0.28f), c.at(-0.18f, 0.28f),
                                 c.at(-0.38f, 0.0f), 16);
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
    c.circle_filled(0.0f, 0.0f, 0.11f);
}

void draw_column(const Canvas& c) {
    c.rect(-0.16f, -0.34f, 0.16f, 0.34f, 0.08f);
    c.line(-0.16f, -0.14f, 0.16f, -0.14f);
    c.line(-0.16f,  0.06f, 0.16f,  0.06f);
}

void draw_key(const Canvas& c) {
    c.circle(-0.16f, -0.02f, 0.17f);
    c.line(0.0f, 0.02f, 0.36f, 0.02f);
    c.line(0.24f, 0.02f, 0.24f, 0.18f);
    c.line(0.36f, 0.02f, 0.36f, 0.22f);
}

void draw_folder(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.36f,  0.28f));
    c.dl->PathLineTo(c.at(-0.36f, -0.22f));
    c.dl->PathLineTo(c.at(-0.06f, -0.22f));
    c.dl->PathLineTo(c.at( 0.02f, -0.10f));
    c.dl->PathLineTo(c.at( 0.36f, -0.10f));
    c.dl->PathLineTo(c.at( 0.36f,  0.28f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

void draw_refresh(const Canvas& c) {
    c.arc(0.0f, 0.0f, 0.30f, 40.0f, 300.0f);
    c.triangle_filled(0.16f, -0.30f, 0.36f, -0.30f, 0.26f, -0.08f);
}

void draw_search(const Canvas& c) {
    c.circle(-0.08f, -0.08f, 0.22f);
    c.line(0.09f, 0.09f, 0.34f, 0.34f);
}

void draw_settings(const Canvas& c) {
    // Engrenagem: círculo com dentes radiais.
    c.circle(0.0f, 0.0f, 0.16f);
    for (int i = 0; i < 8; ++i) {
        const float a = 2.0f * 3.14159265f * static_cast<float>(i) / 8.0f;
        const float cx = std::cos(a);
        const float sy = std::sin(a);
        c.line(cx * 0.22f, sy * 0.22f, cx * 0.36f, sy * 0.36f);
    }
}

void draw_plus(const Canvas& c) {
    c.line(0.0f, -0.30f, 0.0f, 0.30f);
    c.line(-0.30f, 0.0f, 0.30f, 0.0f);
}

void draw_close(const Canvas& c) {
    c.line(-0.24f, -0.24f, 0.24f, 0.24f);
    c.line( 0.24f, -0.24f, -0.24f, 0.24f);
}

void draw_pin(const Canvas& c) {
    c.line(0.0f, 0.06f, 0.0f, 0.38f);
    c.rect_filled(-0.20f, -0.34f, 0.20f, 0.06f, 0.06f);
}

void draw_save(const Canvas& c) {
    c.rect(-0.32f, -0.32f, 0.32f, 0.32f, 0.08f);
    c.rect_filled(-0.18f, -0.32f, 0.18f, -0.08f);   // obturador
    c.rect(-0.20f, 0.06f, 0.20f, 0.32f);            // etiqueta
}

void draw_open(const Canvas& c) {
    draw_folder(c);
    c.line(0.0f, 0.22f, 0.0f, -0.02f);
    c.triangle_filled(-0.10f, 0.02f, 0.10f, 0.02f, 0.0f, 0.22f);
}

void draw_copy(const Canvas& c) {
    c.rect(-0.34f, -0.34f, 0.10f, 0.16f, 0.08f);
    c.rect(-0.10f, -0.16f, 0.34f, 0.34f, 0.08f);
}

void draw_chevron_right(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.12f, -0.26f));
    c.dl->PathLineTo(c.at( 0.16f,  0.0f));
    c.dl->PathLineTo(c.at(-0.12f,  0.26f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_chevron_down(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.26f, -0.12f));
    c.dl->PathLineTo(c.at( 0.0f,   0.16f));
    c.dl->PathLineTo(c.at( 0.26f, -0.12f));
    c.dl->PathStroke(c.color, ImDrawFlags_None, c.thickness);
}

void draw_warning(const Canvas& c) {
    c.dl->PathLineTo(c.at(0.0f, -0.34f));
    c.dl->PathLineTo(c.at(0.36f, 0.30f));
    c.dl->PathLineTo(c.at(-0.36f, 0.30f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
    c.line(0.0f, -0.12f, 0.0f, 0.08f);
    c.circle_filled(0.0f, 0.20f, 0.04f);
}

void draw_error(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.line(-0.14f, -0.14f, 0.14f, 0.14f);
    c.line( 0.14f, -0.14f, -0.14f, 0.14f);
}

void draw_info(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.circle_filled(0.0f, -0.16f, 0.045f);
    c.line(0.0f, -0.02f, 0.0f, 0.18f);
}

void draw_clock(const Canvas& c) {
    c.circle(0.0f, 0.0f, 0.32f);
    c.line(0.0f, 0.0f, 0.0f, -0.18f);
    c.line(0.0f, 0.0f, 0.14f, 0.08f);
}

void draw_filter(const Canvas& c) {
    c.dl->PathLineTo(c.at(-0.34f, -0.26f));
    c.dl->PathLineTo(c.at( 0.34f, -0.26f));
    c.dl->PathLineTo(c.at( 0.10f,  0.02f));
    c.dl->PathLineTo(c.at( 0.10f,  0.30f));
    c.dl->PathLineTo(c.at(-0.10f,  0.20f));
    c.dl->PathLineTo(c.at(-0.10f,  0.02f));
    c.dl->PathStroke(c.color, ImDrawFlags_Closed, c.thickness);
}

// Halo suave atras do icone. Varias circunferencias concentricas com alfa
// decrescente aproximam um blur gaussiano sem shader nem textura.
void draw_glow(ImDrawList* dl, const ImVec2& center, float radius,
               std::uint32_t color, float strength) {
    if (strength <= 0.0f) return;

    constexpr int kLayers = 5;
    for (int i = kLayers; i > 0; --i) {
        const float t = static_cast<float>(i) / kLayers;
        const float alpha = strength * 0.12f * (1.0f - t) * (1.0f - t);
        dl->AddCircleFilled(center, radius * (0.6f + t * 0.9f),
                            with_alpha(color, alpha), 20);
    }
}

} // namespace

float toolbar_button_size() {
    return ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f + 6.0f;
}

void draw_icon(Icon icon, const ImVec2& center, float size, std::uint32_t color,
               float thickness) {
    const Canvas c{ImGui::GetWindowDrawList(), center, size, color, thickness};

    switch (icon) {
        case Icon::connect:       draw_connect(c);       break;
        case Icon::disconnect:    draw_disconnect(c);    break;
        case Icon::play:          draw_play(c);          break;
        case Icon::stop:          draw_stop(c);          break;
        case Icon::commit:        draw_commit(c);        break;
        case Icon::rollback:      draw_rollback(c);      break;
        case Icon::database:      draw_database(c);      break;
        case Icon::table:         draw_table(c);         break;
        case Icon::view:          draw_view(c);          break;
        case Icon::column:        draw_column(c);        break;
        case Icon::key:           draw_key(c);           break;
        case Icon::folder:        draw_folder(c);        break;
        case Icon::refresh:       draw_refresh(c);       break;
        case Icon::search:        draw_search(c);        break;
        case Icon::settings:      draw_settings(c);      break;
        case Icon::plus:          draw_plus(c);          break;
        case Icon::close:         draw_close(c);         break;
        case Icon::pin:           draw_pin(c);           break;
        case Icon::save:          draw_save(c);          break;
        case Icon::open:          draw_open(c);          break;
        case Icon::copy:          draw_copy(c);          break;
        case Icon::chevron_right: draw_chevron_right(c); break;
        case Icon::chevron_down:  draw_chevron_down(c);  break;
        case Icon::warning:       draw_warning(c);       break;
        case Icon::error:         draw_error(c);         break;
        case Icon::info:          draw_info(c);          break;
        case Icon::clock:         draw_clock(c);         break;
        case Icon::filter:        draw_filter(c);        break;
    }
}

bool icon_button(const char* id, Icon icon, const char* tooltip, bool enabled,
                 std::uint32_t tint) {
    const Palette& p = colors();
    const float box = toolbar_button_size();

    ImGui::BeginDisabled(!enabled);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(box, box));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 center(origin.x + box * 0.5f, origin.y + box * 0.5f);

    // Glow no hover, antes do fundo: fica por trás de tudo.
    if (hovered && enabled) {
        draw_glow(dl, center, box * 0.5f,
                  tint != 0 ? tint : p.glow, p.glow_strength);
    }

    if (enabled && (hovered || active)) {
        dl->AddRectFilled(origin, ImVec2(origin.x + box, origin.y + box),
                          with_alpha(p.accent, active ? 0.45f : 0.24f),
                          ImGui::GetStyle().FrameRounding);
    }

    const std::uint32_t color =
        !enabled ? with_alpha(p.text_dim, 0.45f)
                 : (tint != 0 ? tint : (hovered ? p.text_bright : p.text));

    draw_icon(icon, center, box * 0.52f, color);

    ImGui::EndDisabled();

    if (hovered && tooltip != nullptr && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed && enabled;
}

bool icon_text_button(const char* id, Icon icon, const char* label,
                      const char* tooltip, bool enabled) {
    const Palette& p = colors();
    const ImGuiStyle& style = ImGui::GetStyle();

    const float box = toolbar_button_size();
    const float text_width = ImGui::CalcTextSize(label).x;
    const float width = box + text_width + style.FramePadding.x;

    ImGui::BeginDisabled(!enabled);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, box));
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 icon_center(origin.x + box * 0.5f, origin.y + box * 0.5f);

    if (hovered && enabled) {
        draw_glow(dl, icon_center, box * 0.5f, p.glow, p.glow_strength);
    }
    if (enabled && (hovered || active)) {
        dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + box),
                          with_alpha(p.accent, active ? 0.45f : 0.24f),
                          style.FrameRounding);
    }

    const std::uint32_t color =
        !enabled ? with_alpha(p.text_dim, 0.45f)
                 : (hovered ? p.text_bright : p.text);

    draw_icon(icon, icon_center, box * 0.52f, color);
    dl->AddText(ImVec2(origin.x + box,
                       origin.y + (box - ImGui::GetFontSize()) * 0.5f),
                color, label);

    ImGui::EndDisabled();

    if (hovered && tooltip != nullptr && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed && enabled;
}

void icon_inline(Icon icon, std::uint32_t color, float scale) {
    const float box = ImGui::GetFontSize() * scale;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center(origin.x + box * 0.5f,
                        origin.y + ImGui::GetFontSize() * 0.5f);

    draw_icon(icon, center, box * 0.9f, color, 1.4f);
    ImGui::Dummy(ImVec2(box, ImGui::GetFontSize()));
}

} // namespace otter::ui
