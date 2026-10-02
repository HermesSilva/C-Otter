// C-Otter -- ui/grid_view.hpp
//
// Como UM resultado esta' sendo mostrado na grade: colunas escondidas, zoom,
// apresentacao, cores de linha. Estado de VISAO -- nao muda o que esta' no
// banco nem o que a consulta devolveu, e por isso vive separado do buffer de
// edicao (db/edit.hpp).
//
// Fica na aba de resultado (SqlDocument::ResultTab): duas abas mostrando
// consultas diferentes escondem colunas diferentes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace otter::ui {

// "Set row color" (`resultset.grid.selectRowColor`): as linhas cuja coluna
// tem este valor ganham a cor. Pelo NOME da coluna e pelo valor, nao pelo
// indice da linha -- a cor acompanha o dado quando a pagina muda ou a ordem
// troca.
struct GridRowColor {
    std::string   column;
    std::string   value;
    bool          is_null = false;
    std::uint32_t color   = 0;
};

enum class GridPresentation : std::uint8_t {
    grid,   // a tabela
    text,   // texto puro, colunas alinhadas -- para copiar num e-mail ou num chamado
};

struct GridView {
    // Por indice de coluna do resultado. Menor que o resultado = as que
    // faltam estao visiveis.
    std::vector<bool> hidden;

    // Posicao na tela -> indice de coluna, sem as escondidas. Copiado da
    // tabela a cada quadro: o usuario reordena arrastando o cabecalho, e
    // "seta para a direita" tem de ir para a coluna que ESTA' a' direita.
    std::vector<std::size_t> order;

    std::vector<GridRowColor> row_colors;

    GridPresentation presentation = GridPresentation::grid;
    float            zoom = 1.0f;

    // O texto da barra de filtro enquanto e' digitado. So' vira filtro com
    // Enter -- a cada tecla seria uma consulta por caractere.
    char filter_text[512] = "";

    // --- Pedidos de um quadro, atendidos dentro do BeginTable ------------------
    //
    // Mover, esconder e ajustar largura sao operacoes da TABELA do ImGui, que
    // so' existe entre BeginTable e EndTable. O comando deixa o pedido; o
    // desenho o atende.
    std::size_t move_column = static_cast<std::size_t>(-1);
    int         move_delta  = 0;
    bool        fit_values  = false;
    bool        fit_screen  = false;
    bool        focus_filter = false;

    [[nodiscard]] bool is_hidden(std::size_t column) const noexcept {
        return column < hidden.size() && hidden[column];
    }
    void set_hidden(std::size_t column, bool value) {
        if (column >= hidden.size()) hidden.resize(column + 1, false);
        hidden[column] = value;
    }
    [[nodiscard]] std::size_t hidden_count() const noexcept {
        std::size_t count = 0;
        for (const bool h : hidden) count += h ? 1 : 0;
        return count;
    }

    // Consulta NOVA na aba: o que dizia respeito a's colunas da anterior nao
    // se aplica. Zoom e apresentacao sao gosto do usuario, e ficam.
    void reset_for_new_query() {
        hidden.clear();
        order.clear();
        row_colors.clear();
        filter_text[0] = '\0';
    }
};

} // namespace otter::ui
