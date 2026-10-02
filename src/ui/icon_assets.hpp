// C-Otter -- ui/icon_assets.hpp
//
// Os icones originais do DBeaver, embutidos. A tabela e' GERADA por
// tools/embed_icons.py a partir de assets/icons/dbeaver/ (icon_assets.cpp).
#pragma once

#include "ui/icons.hpp"

#include <span>

namespace otter::ui {

struct IconAsset {
    Icon icon;

    // SVG: o texto do arquivo, rasterizado em execucao no tamanho do DPI.
    const char* svg;

    // PNG (icones de driver): pixels RGBA ja' decodificados, no tamanho
    // original e, quando existe, no dobro.
    const unsigned char* rgba;
    int                  rgba_size;
    const unsigned char* rgba_2x;
    int                  rgba_2x_size;
};

[[nodiscard]] std::span<const IconAsset> icon_assets();

} // namespace otter::ui
