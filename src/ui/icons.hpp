// C-Otter -- ui/icons.hpp
//
// Icones desenhados vetorialmente com o DrawList do ImGui.
//
// Sem fonte de icones nem atlas de imagem: os simbolos sao primitivas
// (linhas, arcos, poligonos) que escalam com o DPI sem borrar e herdam a cor
// do tema automaticamente. Tambem evita mais um arquivo na distribuicao.
#pragma once

#include <cstdint>

struct ImVec2;

namespace otter::ui {

enum class Icon : std::uint8_t {
    connect,        // plugue
    disconnect,
    play,           // executar
    stop,
    commit,         // check
    rollback,       // seta em curva
    database,       // cilindro
    table,
    view,
    column,
    key,            // chave primaria
    folder,
    refresh,
    search,
    settings,       // engrenagem
    plus,
    close,
    pin,
    save,
    open,
    copy,
    chevron_right,
    chevron_down,
    warning,
    error,
    info,
    clock,
    filter,
};

// Desenha o icone centrado em `center`, com `size` de lado.
void draw_icon(Icon icon, const ImVec2& center, float size, std::uint32_t color,
               float thickness = 1.6f);

// Botao com icone e tooltip. `id` precisa ser unico no escopo do ImGui.
//
// O glow do tema e' aplicado no hover: um halo discreto atras do icone, que
// some por completo quando glow_strength e' 0 (tema claro, por exemplo).
bool icon_button(const char* id, Icon icon, const char* tooltip,
                 bool enabled = true, std::uint32_t tint = 0);

// Variante com rotulo a direita do icone.
bool icon_text_button(const char* id, Icon icon, const char* label,
                      const char* tooltip, bool enabled = true);

// Icone sem interacao, para arvores e tabelas.
void icon_inline(Icon icon, std::uint32_t color, float scale = 1.0f);

// Altura padrao de um botao da barra de ferramentas.
[[nodiscard]] float toolbar_button_size();

} // namespace otter::ui
