#include "test_main.hpp"

#include "ui/theme.hpp"

#include <cmath>

using namespace otter::ui;

namespace {

// Luminancia relativa (WCAG 2.1), a partir de 0xAABBGGRR.
double luminance(std::uint32_t color) {
    auto channel = [color](int shift) {
        const double v = static_cast<double>((color >> shift) & 0xFF) / 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(0) + 0.7152 * channel(8) + 0.0722 * channel(16);
}

// Razao de contraste WCAG: 1:1 identico, 21:1 preto sobre branco.
double contrast(std::uint32_t a, std::uint32_t b) {
    const double la = luminance(a);
    const double lb = luminance(b);
    const double lighter = std::max(la, lb);
    const double darker  = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

} // namespace

OTTER_TEST(theme_has_three_variants) {
    const auto& themes = available_themes();
    OTTER_CHECK_EQ(themes.size(), std::size_t{3});
    OTTER_CHECK_EQ(themes[0].id, std::string{"dark"});
    OTTER_CHECK_EQ(themes[1].id, std::string{"light"});
    OTTER_CHECK_EQ(themes[2].id, std::string{"amber"});
}

OTTER_TEST(theme_switching_works) {
    OTTER_CHECK(set_theme("light"));
    OTTER_CHECK_EQ(current_theme().id, std::string{"light"});

    OTTER_CHECK(set_theme("amber"));
    OTTER_CHECK_EQ(current_theme().id, std::string{"amber"});

    OTTER_CHECK(set_theme("dark"));
    OTTER_CHECK_EQ(current_theme().id, std::string{"dark"});
}

OTTER_TEST(theme_unknown_id_is_rejected) {
    set_theme("dark");
    OTTER_CHECK(!set_theme("does-not-exist"));
    OTTER_CHECK_EQ(current_theme().id, std::string{"dark"});
}

// --- Contraste ---------------------------------------------------------------
//
// Texto ilegivel e' o defeito mais comum ao acrescentar um tema. Estes testes
// pegam isso sem precisar olhar a tela: foi assim que o texto claro sobre fundo
// claro do tema Light apareceu.

OTTER_TEST(theme_text_is_readable_on_background) {
    // WCAG AA para texto normal exige 4.5:1.
    constexpr double kMinimum = 4.5;

    for (const Theme& theme : available_themes()) {
        set_theme(theme.id);
        const Palette& p = colors();

        OTTER_CHECK(contrast(p.text, p.bg_dark)    >= kMinimum);
        OTTER_CHECK(contrast(p.text, p.bg_darkest) >= kMinimum);
        OTTER_CHECK(contrast(p.text, p.bg_medium)  >= kMinimum);
    }
    set_theme("dark");
}

OTTER_TEST(theme_dim_text_is_still_legible) {
    // Texto secundario: WCAG AA para texto grande, 3:1.
    constexpr double kMinimum = 3.0;

    for (const Theme& theme : available_themes()) {
        set_theme(theme.id);
        OTTER_CHECK(contrast(colors().text_dim, colors().bg_dark) >= kMinimum);
    }
    set_theme("dark");
}

OTTER_TEST(theme_syntax_colors_are_readable_in_editor) {
    // O editor pinta sobre bg_darkest. Foi exatamente aqui que o tema claro
    // falhou: cores de sintaxe pensadas para fundo escuro.
    constexpr double kMinimum = 3.5;

    for (const Theme& theme : available_themes()) {
        set_theme(theme.id);
        const Palette& p = colors();

        OTTER_CHECK(contrast(p.syntax_keyword, p.bg_darkest) >= kMinimum);
        OTTER_CHECK(contrast(p.syntax_string,  p.bg_darkest) >= kMinimum);
        OTTER_CHECK(contrast(p.syntax_number,  p.bg_darkest) >= kMinimum);
        OTTER_CHECK(contrast(p.syntax_comment, p.bg_darkest) >= 2.5);
    }
    set_theme("dark");
}

OTTER_TEST(theme_semantic_colors_are_visible) {
    constexpr double kMinimum = 3.0;

    for (const Theme& theme : available_themes()) {
        set_theme(theme.id);
        const Palette& p = colors();

        OTTER_CHECK(contrast(p.ok,    p.bg_dark) >= kMinimum);
        OTTER_CHECK(contrast(p.warn,  p.bg_dark) >= kMinimum);
        OTTER_CHECK(contrast(p.error, p.bg_dark) >= kMinimum);
    }
    set_theme("dark");
}

OTTER_TEST(theme_light_is_actually_light) {
    set_theme("light");
    // Fundo claro e texto escuro -- o inverso dos temas escuros.
    OTTER_CHECK(luminance(colors().bg_dark) > 0.5);
    OTTER_CHECK(luminance(colors().text) < 0.2);
    OTTER_CHECK(!current_theme().is_dark);
    set_theme("dark");
}

OTTER_TEST(theme_dark_variants_are_dark) {
    for (const char* id : {"dark", "amber"}) {
        set_theme(id);
        OTTER_CHECK(luminance(colors().bg_dark) < 0.15);
        OTTER_CHECK(current_theme().is_dark);
    }
    set_theme("dark");
}

// --- Utilitarios de cor ------------------------------------------------------

OTTER_TEST(theme_with_alpha_preserves_rgb) {
    constexpr std::uint32_t color = 0xFF3C60A8;
    const std::uint32_t faded = with_alpha(color, 0.5f);

    OTTER_CHECK_EQ(faded & 0x00FFFFFFu, color & 0x00FFFFFFu);
    OTTER_CHECK_EQ((faded >> 24) & 0xFFu, std::uint32_t{127});
}

OTTER_TEST(theme_mix_interpolates) {
    constexpr std::uint32_t black = 0xFF000000;
    constexpr std::uint32_t white = 0xFFFFFFFF;

    OTTER_CHECK_EQ(mix(black, white, 0.0f), black);
    OTTER_CHECK_EQ(mix(black, white, 1.0f), white);

    const std::uint32_t middle = mix(black, white, 0.5f);
    OTTER_CHECK_EQ(middle & 0xFFu, std::uint32_t{127});
}

OTTER_TEST(theme_mix_clamps_out_of_range) {
    constexpr std::uint32_t black = 0xFF000000;
    constexpr std::uint32_t white = 0xFFFFFFFF;

    OTTER_CHECK_EQ(mix(black, white, -1.0f), black);
    OTTER_CHECK_EQ(mix(black, white, 2.0f), white);
}

OTTER_TEST(theme_lighten_and_darken) {
    constexpr std::uint32_t gray = 0xFF808080;

    OTTER_CHECK(luminance(lighten(gray, 0.5f)) > luminance(gray));
    OTTER_CHECK(luminance(darken(gray, 0.5f)) < luminance(gray));

    // O alfa nao pode ser alterado.
    OTTER_CHECK_EQ((lighten(gray, 0.5f) >> 24) & 0xFFu, std::uint32_t{255});
}

OTTER_TEST(theme_glow_strength_is_normalized) {
    for (const Theme& theme : available_themes()) {
        set_theme(theme.id);
        OTTER_CHECK(colors().glow_strength >= 0.0f);
        OTTER_CHECK(colors().glow_strength <= 1.0f);
        OTTER_CHECK(colors().surface_alpha > 0.5f);
        OTTER_CHECK(colors().surface_alpha <= 1.0f);
    }
    set_theme("dark");
}
