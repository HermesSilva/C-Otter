// C-Otter -- db/coloring.hpp
//
// Formatação condicional da grade: regras de cor por valor (ADR 0005).
//
// Mapeado do `DBVColorOverride` do DBeaver, que guarda por coluna um operador,
// os valores de comparação, e as cores de texto e de fundo -- mais um modo
// "range", que interpola entre duas cores e dá o mapa de calor.
//
// Por que aqui e não na UI: decidir a cor de uma célula é lógica de dados,
// testável sem janela. A UI só pinta o que esta função devolve.
//
// A avaliação roda por célula visível a cada quadro. Com 200 linhas × 20
// colunas são 4000 chamadas por quadro, e por isso o caminho comum -- nenhuma
// regra para a coluna -- sai antes de qualquer alocação.
#pragma once

#include "db/result_set.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace otter::db {

enum class ColorOp : std::uint8_t {
    equals,
    not_equals,
    greater,
    greater_equals,
    less,
    less_equals,
    between,
    is_null,
    is_not_null,
    contains,
    starts_with,

    // Gradiente entre duas cores, do menor ao maior valor da COLUNA. É o mapa
    // de calor: não compara com um valor fixo, compara com o resto da coluna.
    range,
};

[[nodiscard]] std::string_view to_string(ColorOp op) noexcept;

// Quantos valores de comparação o operador consome. `is_null` não usa nenhum,
// `between` e `range` usam dois, o resto usa um.
[[nodiscard]] std::size_t operand_count(ColorOp op) noexcept;

// Uma regra. `column` é o NOME, não o índice: o índice muda quando o usuário
// reordena as colunas da grade, e a regra passaria a colorir outra coisa.
struct ColorRule {
    std::string column;
    ColorOp     op = ColorOp::equals;

    std::string value;     // primeiro operando
    std::string value2;    // segundo, para between e range

    // 0 significa "não mexer": uma regra pode pintar só o fundo, ou só o
    // texto, e forçar as duas obrigaria o usuário a escolher uma cor de texto
    // que ele não queria mudar.
    std::uint32_t foreground = 0;
    std::uint32_t background = 0;

    // Colore a LINHA inteira, não só a célula que casou. É como se marca
    // "pedido cancelado" sem ter de repetir a regra em cada coluna.
    bool whole_row = false;

    bool enabled = true;
};

// O que aplicar a uma célula. Ausente quando nenhuma regra casou.
struct CellColor {
    std::uint32_t foreground = 0;
    std::uint32_t background = 0;

    [[nodiscard]] bool empty() const noexcept {
        return foreground == 0 && background == 0;
    }
};

// Conjunto de regras de um resultado, com o que for caro pré-calculado.
//
// O mínimo e o máximo de cada coluna em modo `range` são calculados UMA vez,
// aqui, e não a cada célula: varrer a coluna inteira por célula seria O(n²) e
// travaria a grade com 200 linhas.
class ColorRules {
public:
    void add(ColorRule rule);
    void remove(std::size_t index);
    void clear() noexcept;

    [[nodiscard]] const std::vector<ColorRule>& rules() const noexcept {
        return rules_;
    }
    [[nodiscard]] bool empty() const noexcept { return rules_.empty(); }

    // Recalcula o que depende do resultado (mínimos e máximos das colunas em
    // modo range). Chamar depois de trocar o resultado ou as regras.
    void prepare(const ResultSet& rs);

    // Cor de uma célula. Vazia quando nenhuma regra casa.
    //
    // A PRIMEIRA regra que casa vence, na ordem em que foram acrescentadas --
    // como no DBeaver. Misturar as cores de duas regras daria um tom que
    // ninguém escolheu.
    [[nodiscard]] CellColor color_for(const ResultSet& rs, std::size_t row,
                                      std::size_t column) const;

    // Uma coluna tem regra? A grade consulta isto uma vez por coluna, e não
    // por célula, para pular o laço inteiro quando não há.
    [[nodiscard]] bool affects_column(std::string_view name) const;

    // Alguma regra colore a linha toda? Quando não há, a grade nem precisa
    // varrer as outras colunas ao pintar uma célula.
    [[nodiscard]] bool has_row_rules() const noexcept { return has_row_rules_; }

private:
    struct Prepared {
        bool   numeric = false;   // a coluna é numérica e tem faixa?
        double minimum = 0.0;
        double maximum = 0.0;
        std::size_t column = 0;   // índice resolvido a partir do nome
        bool   found = false;
    };

    std::vector<ColorRule> rules_;
    std::vector<Prepared>  prepared_;
    bool                   has_row_rules_ = false;
};

// Interpola entre duas cores no espaço sRGB.
//
// Exposta para teste: a interpolação ingênua em sRGB escurece o meio do
// gradiente (verde→vermelho passa por um marrom sujo), mas é o que o DBeaver
// faz e o que o usuário espera ver -- um gradiente "correto" em Lab ficaria
// diferente do que ele configurou em outra ferramenta.
[[nodiscard]] std::uint32_t blend(std::uint32_t from, std::uint32_t to,
                                  double t) noexcept;

} // namespace otter::db
