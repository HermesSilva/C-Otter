// C-Otter -- ui/theme.hpp
//
// Sistema de temas. Tres variantes embutidas -- Escuro, Claro e Ambar --
// derivadas da paleta do logo (Midia/Logo.png), extraida com
// tools/extract_palette.py.
//
// As cores nao sao constantes globais: vem do tema ativo, para que trocar de
// tema em tempo real funcione sem recompilar nem reiniciar.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiStyle;

namespace otter::ui {

// Papeis semanticos. A UI pede "a cor de acento", nao "#A8603C" -- assim uma
// troca de tema atinge tudo de uma vez.
struct Palette {
    // Fundos, do mais profundo ao mais claro.
    std::uint32_t bg_darkest;
    std::uint32_t bg_dark;
    std::uint32_t bg_medium;
    std::uint32_t bg_light;

    // Texto.
    std::uint32_t text;
    std::uint32_t text_dim;
    std::uint32_t text_bright;

    // Marca: o pelo da lontra e a agua.
    std::uint32_t accent;        // primaria (pelo)
    std::uint32_t accent_light;
    std::uint32_t accent_dark;
    std::uint32_t data;          // secundaria (cilindro de banco)
    std::uint32_t data_light;

    // Semanticas.
    std::uint32_t ok;
    std::uint32_t warn;
    std::uint32_t error;
    std::uint32_t info;

    // Realce de sintaxe do editor.
    std::uint32_t syntax_keyword;
    std::uint32_t syntax_string;
    std::uint32_t syntax_number;
    std::uint32_t syntax_comment;

    // Glow: brilho sutil em elementos ativos. `glow_strength` em 0..1 controla
    // a intensidade; 0 desliga o efeito por completo.
    std::uint32_t glow;
    float         glow_strength;

    // Transparencia de paineis e popups, em 0..1.
    float         surface_alpha;
};

struct Theme {
    std::string name;         // chave de traducao
    std::string id;           // "dark", "light", "amber"
    Palette     palette;
    bool        is_dark;      // decide contraste de bordas e sombras
};

[[nodiscard]] const std::vector<Theme>& available_themes();

// Tema ativo. Todo desenho consulta isto.
[[nodiscard]] const Theme& current_theme();
[[nodiscard]] const Palette& colors();

// Troca o tema e reaplica o estilo do ImGui. Devolve false se o id nao existir.
bool set_theme(std::string_view id);

// Aplica o tema ativo ao estilo global do ImGui.
void apply_theme(ImGuiStyle& style);

// --- Utilitarios de cor ------------------------------------------------------

// Ajusta o canal alfa de uma cor 0xAABBGGRR.
[[nodiscard]] constexpr std::uint32_t with_alpha(std::uint32_t color,
                                                 float alpha) noexcept {
    const auto a = static_cast<std::uint32_t>(alpha * 255.0f) & 0xFFu;
    return (color & 0x00FFFFFFu) | (a << 24);
}

// Mistura duas cores; t=0 devolve a, t=1 devolve b.
[[nodiscard]] std::uint32_t mix(std::uint32_t a, std::uint32_t b, float t) noexcept;

// Clareia ou escurece mantendo o alfa.
[[nodiscard]] std::uint32_t lighten(std::uint32_t color, float amount) noexcept;
[[nodiscard]] std::uint32_t darken(std::uint32_t color, float amount) noexcept;

} // namespace otter::ui
