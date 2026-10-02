// C-Otter -- ui/icon_images.hpp
//
// Os icones do DBeaver como IMAGEM: o SVG original rasterizado no tamanho
// exato em que vai aparecer, e guardado como textura.
//
// Pedido do usuario (2026-09-30), duas vezes: "por que nao usar os icones do
// DBeaver?" e "nao e' possivel copiar e usar os icones originais?". Quem vem
// de la' reconhece o icone antes de ler o rotulo -- e' o argumento da
// diretiva 12 aplicado ao desenho.
//
// Os icones desenhados em codigo (ui/icons.cpp) continuam existindo: sao o
// conjunto "C-Otter", selecionavel no menu, e o que aparece para um icone
// sem equivalente no DBeaver.
#pragma once

#include "ui/icons.hpp"

#include "imgui.h"

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace otter::ui {

enum class IconSet : std::uint8_t {
    dbeaver,   // os originais do DBeaver, onde ha' equivalente
    otter,     // os vetoriais do C-Otter, na cor do tema
};

void    set_icon_set(IconSet set) noexcept;
[[nodiscard]] IconSet icon_set() noexcept;
[[nodiscard]] std::string_view icon_set_id(IconSet set) noexcept;        // "dbeaver"
[[nodiscard]] IconSet          icon_set_from_id(std::string_view id) noexcept;

// Quem sabe criar textura e' a janela (dona do contexto grafico). Sem
// fabrica registrada -- nos testes, por exemplo -- nao ha' imagem, e o
// desenho cai no vetorial.
using TextureFactory =
    std::function<ImTextureID(const unsigned char* rgba, int width, int height)>;
void set_icon_texture_factory(TextureFactory factory);

// O icone da PASTA que guarda objetos do tipo `content`.
//
// No conjunto do C-Otter a pasta usa o icone do proprio objeto ("Tables" com
// a grade da tabela). No DBeaver cada tipo de pasta tem o seu desenho -- e
// as que nao tem (Indexes, Functions, Sequences, Data types...) usam a pasta
// laranja comum. A regra fica aqui, num lugar so', em vez de em cada um dos
// cinquenta pontos que desenham uma pasta.
[[nodiscard]] Icon folder_icon(Icon content) noexcept;

// O icone tem original do DBeaver?
[[nodiscard]] bool icon_has_image(Icon icon) noexcept;

// Os pixels RGBA do icone com `pixels` de lado. Vazio se nao ha' original ou
// se o arquivo nao pode ser lido. Sem estado: serve ao teste.
[[nodiscard]] std::vector<unsigned char> rasterize_icon(Icon icon, int pixels);

// A textura do icone naquele tamanho, criada na primeira vez e guardada.
// Nulo quando o conjunto ativo e' o do C-Otter, quando nao ha' original, ou
// quando nao ha' fabrica.
[[nodiscard]] ImTextureID icon_texture(Icon icon, int pixels);

} // namespace otter::ui
