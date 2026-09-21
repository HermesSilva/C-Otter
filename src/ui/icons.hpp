// C-Otter -- ui/icons.hpp
//
// Icones desenhados vetorialmente com o DrawList do ImGui.
//
// Sem fonte de icones nem atlas de imagem: os simbolos sao primitivas
// (linhas, arcos, poligonos) que escalam com o DPI sem borrar e herdam a cor
// do tema automaticamente. Tambem evita mais um arquivo na distribuicao.
#pragma once

#include <cstddef>
#include <cstdint>

struct ImVec2;
struct ImDrawList;

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

    // Tipos de objeto do banco. Cada um tem desenho proprio: reaproveitar um
    // simbolo generico para dois tipos diferentes torna a arvore ilegivel de
    // relance (diretiva 5 do CLAUDE.md).
    materialized_view,  // olho sobre disco: view com dados persistidos
    index,              // paginas com marcador
    constraint,         // escudo
    foreign_key,        // elo de corrente
    references,         // setas convergindo
    sequence,           // degraus ascendentes
    function,           // f(x)
    procedure,          // bloco com engrenagem
    trigger,            // raio
    data_type,          // chaves {} com nucleo
    extension,          // peca de quebra-cabeca
    role,               // silhueta com chave
    tablespace,         // discos empilhados
    schema,             // grade ramificada
};

// Numero de icones; serve para iterar sobre todos (galeria, testes).
inline constexpr std::size_t kIconCount =
    static_cast<std::size_t>(Icon::schema) + 1;

// Desenha o icone centrado em `center`, com `size` de lado.
void draw_icon(Icon icon, const ImVec2& center, float size, std::uint32_t color,
               float thickness = 1.6f);

// Variante que desenha num DrawList explicito, em vez do da janela corrente.
//
// Existe para o teste: comparar os vertices gerados por cada icone e' o unico
// jeito de provar que dois tipos de objeto nao compartilham desenho. Fora do
// teste, prefira a sobrecarga acima.
void draw_icon_to(ImDrawList* dl, Icon icon, const ImVec2& center, float size,
                  std::uint32_t color, float thickness = 1.6f);

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
