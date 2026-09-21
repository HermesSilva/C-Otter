#include "db/coloring.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace otter::db {
namespace {

bool to_number(std::string_view text, double& out) noexcept {
    if (text.empty()) return false;

    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

// Comparação sem diferenciar maiúsculas, para `contains` e `starts_with`.
//
// Quem digita "cancelado" espera achar "CANCELADO": obrigar a acertar a caixa
// tornaria a regra inútil na prática.
bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;

    const auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [&lower](char a, char b) { return lower(a) == lower(b); });
    return it != haystack.end();
}

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) return false;

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(text[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

// Compara dois valores. Numérico quando AMBOS convertem; texto caso contrário.
//
// Comparar "10" com "9" como texto daria "10 < 9", que é o resultado errado e
// o defeito mais comum numa comparação frouxa.
int compare(std::string_view left, std::string_view right) {
    double a = 0.0;
    double b = 0.0;
    if (to_number(left, a) && to_number(right, b)) {
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }
    return left.compare(right) < 0 ? -1 : (left == right ? 0 : 1);
}

} // namespace

std::string_view to_string(ColorOp op) noexcept {
    switch (op) {
        case ColorOp::equals:         return "equals";
        case ColorOp::not_equals:     return "not equals";
        case ColorOp::greater:        return "greater than";
        case ColorOp::greater_equals: return "greater or equal";
        case ColorOp::less:           return "less than";
        case ColorOp::less_equals:    return "less or equal";
        case ColorOp::between:        return "between";
        case ColorOp::is_null:        return "is null";
        case ColorOp::is_not_null:    return "is not null";
        case ColorOp::contains:       return "contains";
        case ColorOp::starts_with:    return "starts with";
        case ColorOp::range:          return "gradient";
    }
    return "unknown";
}

std::size_t operand_count(ColorOp op) noexcept {
    switch (op) {
        case ColorOp::is_null:
        case ColorOp::is_not_null:
            return 0;
        case ColorOp::between:
        case ColorOp::range:
            return 2;
        default:
            return 1;
    }
}

std::uint32_t blend(std::uint32_t from, std::uint32_t to, double t) noexcept {
    // Fora de [0,1] seria extrapolação: uma cor que nenhuma das duas pontas
    // tem, aparecendo nos extremos da faixa.
    t = std::clamp(t, 0.0, 1.0);

    std::uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const double a = static_cast<double>((from >> shift) & 0xFF);
        const double b = static_cast<double>((to >> shift) & 0xFF);
        const auto   mixed = static_cast<std::uint32_t>(a + (b - a) * t + 0.5);
        out |= (mixed & 0xFF) << shift;
    }
    return out;
}

void ColorRules::add(ColorRule rule) {
    if (rule.whole_row) has_row_rules_ = true;
    rules_.push_back(std::move(rule));
    prepared_.emplace_back();
}

void ColorRules::remove(std::size_t index) {
    if (index >= rules_.size()) return;

    rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
    prepared_.erase(prepared_.begin() + static_cast<std::ptrdiff_t>(index));

    has_row_rules_ = false;
    for (const ColorRule& rule : rules_) {
        if (rule.whole_row) has_row_rules_ = true;
    }
}

void ColorRules::clear() noexcept {
    rules_.clear();
    prepared_.clear();
    has_row_rules_ = false;
}

void ColorRules::prepare(const ResultSet& rs) {
    prepared_.assign(rules_.size(), Prepared{});

    for (std::size_t i = 0; i < rules_.size(); ++i) {
        const ColorRule& rule = rules_[i];
        Prepared& state = prepared_[i];

        // Resolve o nome para índice UMA vez. Por célula, isso seria uma busca
        // linear nas colunas a cada uma das milhares de chamadas por quadro.
        const auto index = rs.find_column(rule.column);
        if (!index) continue;

        state.column = *index;
        state.found  = true;

        if (rule.op != ColorOp::range) continue;

        // Mínimo e máximo da coluna, para o gradiente. Calculado aqui, e não
        // por célula: varrer a coluna a cada célula seria O(n²), e com 200
        // linhas a grade já engasgaria.
        bool first = true;
        for (std::size_t row = 0; row < rs.row_count(); ++row) {
            if (rs.is_null(row, state.column)) continue;

            double value = 0.0;
            if (!to_number(rs.text(row, state.column), value)) continue;

            if (first) {
                state.minimum = state.maximum = value;
                first = false;
            } else {
                state.minimum = std::min(state.minimum, value);
                state.maximum = std::max(state.maximum, value);
            }
        }
        state.numeric = !first;
    }
}

bool ColorRules::affects_column(std::string_view name) const {
    for (const ColorRule& rule : rules_) {
        if (rule.enabled && rule.column == name) return true;
    }
    return false;
}

CellColor ColorRules::color_for(const ResultSet& rs, std::size_t row,
                                std::size_t column) const {
    if (rules_.empty()) return {};

    for (std::size_t i = 0; i < rules_.size(); ++i) {
        const ColorRule& rule = rules_[i];
        if (!rule.enabled) continue;
        if (i >= prepared_.size() || !prepared_[i].found) continue;

        const Prepared& state = prepared_[i];

        // Regra de célula só vale na própria coluna; regra de linha vale em
        // todas, mas o VALOR testado é sempre o da coluna da regra.
        if (!rule.whole_row && state.column != column) continue;

        const bool is_null = rs.is_null(row, state.column);
        const std::string_view value =
            is_null ? std::string_view{} : rs.text(row, state.column);

        bool matched = false;

        switch (rule.op) {
            case ColorOp::is_null:     matched = is_null; break;
            case ColorOp::is_not_null: matched = !is_null; break;

            case ColorOp::range: {
                if (is_null || !state.numeric) break;

                double number = 0.0;
                if (!to_number(value, number)) break;

                // Faixa degenerada (todos os valores iguais): usa a cor do
                // início. Dividir por zero daria NaN e uma cor aleatória.
                const double span = state.maximum - state.minimum;
                const double t = span > 0.0
                                     ? (number - state.minimum) / span
                                     : 0.0;

                CellColor out;
                if (rule.foreground != 0 || rule.background != 0) {
                    // No modo gradiente, `foreground` e `background` são as
                    // duas PONTAS do degradê de fundo -- não texto e fundo.
                    out.background = blend(rule.foreground, rule.background, t);
                }
                return out;
            }

            default: {
                // Nulo não casa com comparação de valor: `NULL > 10` é
                // desconhecido em SQL, e pintar como se fosse falso ou
                // verdadeiro seria inventar uma resposta.
                if (is_null) break;

                switch (rule.op) {
                    case ColorOp::equals:
                        matched = compare(value, rule.value) == 0;
                        break;
                    case ColorOp::not_equals:
                        matched = compare(value, rule.value) != 0;
                        break;
                    case ColorOp::greater:
                        matched = compare(value, rule.value) > 0;
                        break;
                    case ColorOp::greater_equals:
                        matched = compare(value, rule.value) >= 0;
                        break;
                    case ColorOp::less:
                        matched = compare(value, rule.value) < 0;
                        break;
                    case ColorOp::less_equals:
                        matched = compare(value, rule.value) <= 0;
                        break;
                    case ColorOp::between:
                        matched = compare(value, rule.value) >= 0 &&
                                  compare(value, rule.value2) <= 0;
                        break;
                    case ColorOp::contains:
                        matched = contains_ci(value, rule.value);
                        break;
                    case ColorOp::starts_with:
                        matched = starts_with_ci(value, rule.value);
                        break;
                    default:
                        break;
                }
                break;
            }
        }

        if (!matched) continue;

        // A PRIMEIRA regra que casa vence. Misturar as cores de duas daria um
        // tom que ninguém escolheu.
        return CellColor{rule.foreground, rule.background};
    }
    return {};
}

} // namespace otter::db
