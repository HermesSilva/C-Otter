// C-Otter -- ui/theme.hpp
//
// Paleta derivada do logo (Midia/Logo.png), extraida com tools/extract_palette.py.
// A lontra e' marrom, o cilindro de banco e' teal, a agua e' teal escuro.
#pragma once

#include <cstdint>

struct ImGuiStyle;

namespace otter::ui {

// Cores da marca, em 0xAABBGGRR (ordem que o ImGui usa em ImU32).
namespace palette {

// Pelo da lontra -- cor primaria da marca.
inline constexpr std::uint32_t fur          = 0xFF3C60A8;  // #A8603C
inline constexpr std::uint32_t fur_light    = 0xFF6090CC;  // #CC9060
inline constexpr std::uint32_t fur_dark     = 0xFF0C183C;  // #3C180C

// Cilindro de banco de dados e agua.
inline constexpr std::uint32_t data         = 0xFF908430;  // #308490
inline constexpr std::uint32_t data_light   = 0xFFB0A450;
inline constexpr std::uint32_t water        = 0xFF6B5A22;

// Barriga da lontra -- fundo claro.
inline constexpr std::uint32_t cream        = 0xFFCCE4F0;  // #F0E4CC

// Tons neutros do tema escuro.
inline constexpr std::uint32_t bg_darkest   = 0xFF1A1613;
inline constexpr std::uint32_t bg_dark      = 0xFF241E1A;
inline constexpr std::uint32_t bg_medium    = 0xFF302823;
inline constexpr std::uint32_t bg_light     = 0xFF3D332C;
inline constexpr std::uint32_t text         = 0xFFE8E4E0;
inline constexpr std::uint32_t text_dim     = 0xFF9A9490;

// Semanticas.
inline constexpr std::uint32_t ok           = 0xFF5CB85C;
inline constexpr std::uint32_t warn         = 0xFF3CA8DC;
inline constexpr std::uint32_t error        = 0xFF4C4CDC;

} // namespace palette

// Aplica o tema da lontra ao estilo global do ImGui.
void apply_otter_theme(ImGuiStyle& style);

} // namespace otter::ui
