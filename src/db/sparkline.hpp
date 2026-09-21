// C-Otter -- src/db/sparkline.hpp
//
// Barra na célula, proporcional ao valor (ADR 0005, decisão 2).
//
// O DBeaver **não tem** isto -- é um dos poucos pontos em que o C-Otter vai
// além dele, e não atrás. A ideia é velha (Tufte chamou de sparkline) e o
// ganho é concreto: numa coluna de 200 números, a barra mostra a forma da
// distribuição sem que ninguém precise ler os números.
#pragma once

#include "db/result_set.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace otter::db {

// Como a barra é ancorada na faixa da coluna.
enum class BarBaseline : std::uint8_t {
    // Zero à esquerda: a barra mede o valor absoluto. É o que se quer para
    // quantidade, receita, contagem -- grandezas que começam no zero.
    from_zero,

    // O mínimo da coluna à esquerda. Para faixas estreitas e distantes do
    // zero (temperatura 36,1..36,9; ano 2019..2026), de onde a ancoragem no
    // zero produziria 200 barras visualmente idênticas.
    from_minimum,

    // Zero no MEIO, negativo à esquerda e positivo à direita. Para variação,
    // saldo, diferença -- onde o sinal é a informação principal.
    centered_on_zero,
};

struct BarSpec {
    std::string column;
    BarBaseline baseline = BarBaseline::from_zero;

    // Mostrar o número junto da barra.
    //
    // Ligado por padrão: a barra responde "quanto, comparado aos outros", e o
    // número responde "quanto exatamente". Trocar um pelo outro perde metade
    // da resposta, e a coluna costuma ter espaço para os dois.
    bool show_value = true;

    bool enabled = true;
};

// O que desenhar numa célula. `fraction` já está em 0..1 e é o que a UI
// multiplica pela largura -- a UI não refaz conta nenhuma.
struct CellBar {
    bool   visible = false;
    float  fraction = 0.0f;

    // Para `centered_on_zero`: de onde a barra parte, também em 0..1. Nos
    // outros modos é zero.
    float  origin = 0.0f;

    // Valor negativo pede cor própria: uma barra vermelha para a esquerda e
    // uma verde para a direita são lidas de relance; a mesma cor nos dois
    // lados obriga a procurar o meio.
    bool   negative = false;
};

// Barras de um resultado, com a faixa de cada coluna pré-calculada.
//
// Como em `ColorRules`, o mínimo e o máximo saem UMA vez em `prepare()`. Uma
// varredura por célula seria O(n²): com 200 linhas e 20 colunas são 800 mil
// conversões de texto para número por quadro, a 60 quadros por segundo.
class BarRules {
public:
    void add(BarSpec spec);
    void remove(std::size_t index);
    void clear() noexcept;

    [[nodiscard]] const std::vector<BarSpec>& specs() const noexcept {
        return specs_;
    }
    [[nodiscard]] bool empty() const noexcept { return specs_.empty(); }

    void prepare(const ResultSet& rs);

    [[nodiscard]] CellBar bar_for(const ResultSet& rs, std::size_t row,
                                  std::size_t column) const;

    // Esta coluna tem barra? A grade consulta uma vez por coluna, não por
    // célula.
    [[nodiscard]] bool affects_column(std::string_view name) const;

    // A barra desta coluna mostra o número ao lado?
    [[nodiscard]] bool shows_value(std::size_t column) const;

private:
    struct Prepared {
        std::size_t column = 0;
        bool        found = false;
        bool        numeric = false;
        double      minimum = 0.0;
        double      maximum = 0.0;
    };

    std::vector<BarSpec>  specs_;
    std::vector<Prepared> prepared_;
};

} // namespace otter::db
