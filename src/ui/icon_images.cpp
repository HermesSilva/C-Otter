#include "ui/icon_images.hpp"

#include "ui/icon_assets.hpp"

#include "nanosvg.h"
#include "nanosvgrast.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <utility>

namespace otter::ui {
namespace {

IconSet        g_set = IconSet::dbeaver;
TextureFactory g_factory;

// (icone, tamanho) -> textura. Poucas dezenas de entradas: cada icone aparece
// em um ou dois tamanhos (arvore, barra).
std::map<std::pair<int, int>, ImTextureID> g_textures;

const IconAsset* find_asset(Icon icon) noexcept {
    for (const IconAsset& asset : icon_assets()) {
        if (asset.icon == icon) return &asset;
    }
    return nullptr;
}

// Contas de tamanho em size_t desde o primeiro fator: `int * int` estoura
// antes de ser promovido, e misturar os dois e' conversao de sinal implicita.
constexpr std::size_t rgba_bytes(int side) noexcept {
    return static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 4;
}

constexpr std::size_t pixel_offset(int x, int y, int side) noexcept {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(side) +
            static_cast<std::size_t>(x)) * 4;
}

// Reamostra por MEDIA de area. Vizinho mais proximo serrilharia ao reduzir o
// PNG de 32 para um tamanho intermediario (20, 24).
std::vector<unsigned char> resample(const unsigned char* source, int source_size,
                                    int pixels) {
    std::vector<unsigned char> out(rgba_bytes(pixels));
    const double step = static_cast<double>(source_size) / pixels;

    for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
            const int x0 = static_cast<int>(x * step);
            const int y0 = static_cast<int>(y * step);
            const int x1 = (std::max)(x0 + 1, (std::min)(source_size,
                                                         static_cast<int>((x + 1) * step + 0.999)));
            const int y1 = (std::max)(y0 + 1, (std::min)(source_size,
                                                         static_cast<int>((y + 1) * step + 0.999)));

            // Media ponderada pelo alfa: sem isso a borda transparente
            // escurece a cor do pixel vizinho.
            double r = 0, g = 0, b = 0, a = 0;
            int    count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    const unsigned char* p =
                        source + pixel_offset(sx, sy, source_size);
                    const double alpha = p[3] / 255.0;
                    r += p[0] * alpha;
                    g += p[1] * alpha;
                    b += p[2] * alpha;
                    a += alpha;
                    ++count;
                }
            }

            unsigned char* q = out.data() + pixel_offset(x, y, pixels);
            if (a > 0.0) {
                q[0] = static_cast<unsigned char>(r / a + 0.5);
                q[1] = static_cast<unsigned char>(g / a + 0.5);
                q[2] = static_cast<unsigned char>(b / a + 0.5);
            }
            q[3] = static_cast<unsigned char>(a / count * 255.0 + 0.5);
        }
    }
    return out;
}

} // namespace

void set_icon_set(IconSet set) noexcept { g_set = set; }

IconSet icon_set() noexcept { return g_set; }

std::string_view icon_set_id(IconSet set) noexcept {
    return set == IconSet::otter ? "otter" : "dbeaver";
}

IconSet icon_set_from_id(std::string_view id) noexcept {
    return id == "otter" ? IconSet::otter : IconSet::dbeaver;
}

void set_icon_texture_factory(TextureFactory factory) {
    g_factory = std::move(factory);
    g_textures.clear();
}

Icon folder_icon(Icon content) noexcept {
    if (g_set != IconSet::dbeaver) return content;

    // Do `<tree>` do plugin.xml do PostgreSQL: o atributo icon="#..." de
    // cada <folder>, resolvido por DBIcon.java.
    switch (content) {
        case Icon::database:          return Icon::folder_database;
        case Icon::schema:            return Icon::folder_schema;
        case Icon::table:
        case Icon::partition:
        case Icon::inheritance:       return Icon::folder_table;
        case Icon::foreign_table:     return Icon::folder_link;
        case Icon::view:
        case Icon::materialized_view: return Icon::folder_view;
        case Icon::column:
        case Icon::parameter:         return Icon::folder_columns;
        case Icon::constraint:        return Icon::folder_constraint;
        case Icon::role:
        case Icon::role_group:
        case Icon::user:
        case Icon::user_mapping:      return Icon::folder_user;
        case Icon::extension:
        case Icon::administer:        return Icon::folder_admin;
        case Icon::storage:
        case Icon::system_info:       return Icon::folder_info;
        // O resto usa um id que o DBIcon nao conhece ("#indexes",
        // "#procedures", "#sequences"...), e cai na pasta comum.
        default:                      return Icon::folder;
    }
}

bool icon_has_image(Icon icon) noexcept { return find_asset(icon) != nullptr; }

std::vector<unsigned char> rasterize_icon(Icon icon, int pixels) {
    const IconAsset* asset = find_asset(icon);
    if (asset == nullptr || pixels <= 0) return {};

    if (asset->svg == nullptr) {
        // PNG: a versao que cobre o tamanho pedido, reduzida se preciso.
        const bool use_double = asset->rgba_2x != nullptr && pixels > asset->rgba_size;
        const unsigned char* source = use_double ? asset->rgba_2x : asset->rgba;
        const int size = use_double ? asset->rgba_2x_size : asset->rgba_size;
        if (source == nullptr || size <= 0) return {};

        if (size == pixels) {
            return std::vector<unsigned char>(
                source, source + rgba_bytes(size));
        }
        return resample(source, size, pixels);
    }

    // nsvgParse ESCREVE no texto que recebe; a copia preserva o original.
    std::string text(asset->svg);
    NSVGimage* image = nsvgParse(text.data(), "px", 96.0f);
    if (image == nullptr) return {};

    std::vector<unsigned char> out;
    const float side = (std::max)(image->width, image->height);
    if (side > 0.0f) {
        if (NSVGrasterizer* rasterizer = nsvgCreateRasterizer()) {
            out.assign(rgba_bytes(pixels), 0);
            nsvgRasterize(rasterizer, image, 0.0f, 0.0f,
                          static_cast<float>(pixels) / side, out.data(), pixels,
                          pixels, pixels * 4);
            nsvgDeleteRasterizer(rasterizer);
        }
    }
    nsvgDelete(image);
    return out;
}

ImTextureID icon_texture(Icon icon, int pixels) {
    if (g_set != IconSet::dbeaver || !g_factory || pixels <= 0) return ImTextureID{};

    const auto key = std::make_pair(static_cast<int>(icon), pixels);
    if (const auto it = g_textures.find(key); it != g_textures.end()) {
        return it->second;
    }

    ImTextureID texture{};
    const std::vector<unsigned char> rgba = rasterize_icon(icon, pixels);
    if (!rgba.empty()) texture = g_factory(rgba.data(), pixels, pixels);

    // Guardado mesmo quando falhou: sem original, a pergunta se repetiria a
    // cada quadro para cada icone desenhado.
    g_textures.emplace(key, texture);
    return texture;
}

} // namespace otter::ui
