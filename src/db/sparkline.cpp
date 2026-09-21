#include "db/sparkline.hpp"

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

} // namespace

void BarRules::add(BarSpec spec) {
    // Uma barra por coluna. Duas na mesma coluna desenhariam uma por cima da
    // outra, e a de baixo seria invisível -- o usuário veria a regra na lista
    // e nada na tela.
    const auto it = std::find_if(
        specs_.begin(), specs_.end(),
        [&](const BarSpec& existing) { return existing.column == spec.column; });

    if (it != specs_.end()) *it = std::move(spec);
    else                    specs_.push_back(std::move(spec));
}

void BarRules::remove(std::size_t index) {
    if (index < specs_.size()) {
        specs_.erase(specs_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

void BarRules::clear() noexcept {
    specs_.clear();
    prepared_.clear();
}

void BarRules::prepare(const ResultSet& rs) {
    prepared_.assign(specs_.size(), Prepared{});

    for (std::size_t i = 0; i < specs_.size(); ++i) {
        const BarSpec& spec = specs_[i];
        Prepared& out = prepared_[i];

        for (std::size_t c = 0; c < rs.column_count(); ++c) {
            if (rs.column(c).info().name == spec.column) {
                out.column = c;
                out.found  = true;
                break;
            }
        }
        if (!out.found) continue;

        double minimum = 0.0;
        double maximum = 0.0;
        bool   any = false;

        for (std::size_t r = 0; r < rs.row_count(); ++r) {
            if (rs.is_null(r, out.column)) continue;

            double value = 0.0;
            if (!to_number(rs.text(r, out.column), value)) continue;

            if (!any) { minimum = maximum = value; any = true; }
            else {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }

        out.numeric = any;
        out.minimum = minimum;
        out.maximum = maximum;
    }
}

CellBar BarRules::bar_for(const ResultSet& rs, std::size_t row,
                          std::size_t column) const {
    CellBar bar;

    // NULL não recebe barra. Uma barra de comprimento zero seria
    // indistinguível de um valor mínimo legítimo -- e são coisas diferentes.
    if (rs.is_null(row, column)) return bar;

    for (std::size_t i = 0; i < specs_.size() && i < prepared_.size(); ++i) {
        const BarSpec& spec = specs_[i];
        const Prepared& prep = prepared_[i];

        if (!spec.enabled || !prep.found || prep.column != column) continue;
        if (!prep.numeric) continue;

        double value = 0.0;
        if (!to_number(rs.text(row, column), value)) continue;

        bar.visible  = true;
        bar.negative = value < 0.0;

        switch (spec.baseline) {
            case BarBaseline::from_zero: {
                // A escala vai de zero ao maior valor ABSOLUTO da coluna, e
                // não ao máximo: numa coluna que vai de -900 a 100, escalar
                // pelo máximo daria uma barra nove vezes maior que a régua.
                const double extent =
                    std::max(std::abs(prep.minimum), std::abs(prep.maximum));
                if (extent <= 0.0) return bar;

                bar.fraction = static_cast<float>(
                    std::min(1.0, std::abs(value) / extent));
                break;
            }

            case BarBaseline::from_minimum: {
                const double span = prep.maximum - prep.minimum;

                // Coluna constante: todas as barras cheias. Zero seria pior --
                // a coluna pareceria vazia, como se não houvesse dado.
                if (span <= 0.0) { bar.fraction = 1.0f; break; }

                bar.fraction = static_cast<float>(
                    std::clamp((value - prep.minimum) / span, 0.0, 1.0));
                break;
            }

            case BarBaseline::centered_on_zero: {
                const double extent =
                    std::max(std::abs(prep.minimum), std::abs(prep.maximum));
                if (extent <= 0.0) return bar;

                // Metade da largura para cada lado. A barra parte do meio e
                // cresce para a direita quando positiva, para a esquerda
                // quando negativa.
                const float half = static_cast<float>(
                    std::min(1.0, std::abs(value) / extent)) * 0.5f;

                bar.fraction = half;
                bar.origin   = bar.negative ? 0.5f - half : 0.5f;
                break;
            }
        }
        return bar;
    }
    return bar;
}

bool BarRules::affects_column(std::string_view name) const {
    return std::any_of(specs_.begin(), specs_.end(),
                       [&](const BarSpec& spec) {
                           return spec.enabled && spec.column == name;
                       });
}

bool BarRules::shows_value(std::size_t column) const {
    for (std::size_t i = 0; i < specs_.size() && i < prepared_.size(); ++i) {
        if (prepared_[i].found && prepared_[i].column == column) {
            return specs_[i].show_value;
        }
    }
    return true;
}

} // namespace otter::db
