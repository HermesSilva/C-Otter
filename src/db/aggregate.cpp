#include "db/aggregate.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace otter::db {
namespace {

// Converte para double. Devolve falso quando o texto nao e' numero -- e' o
// que impede somar "N/A" como zero e produzir um total errado em silencio.
bool to_number(std::string_view text, double& out) noexcept {
    if (text.empty()) return false;

    const auto result = std::from_chars(text.data(),
                                        text.data() + text.size(), out);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size();
}

std::string format_number(double value) {
    char buffer[64];

    // Inteiro sai sem casas: uma contagem de 1234 nao deve virar "1234.00".
    if (value == std::floor(value) && std::abs(value) < 1e15) {
        std::snprintf(buffer, sizeof buffer, "%lld",
                      static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof buffer, "%.4g", value);
    }
    return buffer;
}

// Acumulador de uma agregacao sobre um conjunto de linhas.
struct Accumulator {
    Aggregate   function = Aggregate::none;
    std::size_t count = 0;
    std::size_t non_null = 0;
    double      sum = 0.0;
    double      minimum = 0.0;
    double      maximum = 0.0;
    bool        has_number = false;

    // Para min/max de texto e count_distinct.
    std::string text_min;
    std::string text_max;
    std::set<std::string> distinct;
    bool        has_text = false;

    void add(std::string_view value, bool is_null) {
        ++count;
        if (is_null) return;
        ++non_null;

        if (function == Aggregate::count_distinct) {
            distinct.emplace(value);
            return;
        }

        double number = 0.0;
        if (to_number(value, number)) {
            if (!has_number) {
                minimum = maximum = number;
                has_number = true;
            } else {
                minimum = std::min(minimum, number);
                maximum = std::max(maximum, number);
            }
            sum += number;
            return;
        }

        // Texto: min e max ainda fazem sentido (ordem lexicografica).
        if (!has_text) {
            text_min = text_max = std::string(value);
            has_text = true;
        } else {
            if (value < text_min) text_min = std::string(value);
            if (value > text_max) text_max = std::string(value);
        }
    }

    [[nodiscard]] AggregateValue value() const {
        AggregateValue out;

        switch (function) {
            case Aggregate::count:
                out.number = static_cast<double>(count);
                out.numeric = true;
                out.text = format_number(out.number);
                break;

            case Aggregate::count_non_null:
                out.number = static_cast<double>(non_null);
                out.numeric = true;
                out.text = format_number(out.number);
                break;

            case Aggregate::count_distinct:
                out.number = static_cast<double>(distinct.size());
                out.numeric = true;
                out.text = format_number(out.number);
                break;

            case Aggregate::sum:
                if (!has_number) { out.text = "-"; break; }
                out.number = sum;
                out.numeric = true;
                out.text = format_number(sum);
                break;

            case Aggregate::average:
                // Media sobre os NAO NULOS, como o AVG do SQL. Dividir pelo
                // total contando nulos como zero daria outro numero, e o
                // usuario espera o do SQL.
                if (!has_number || non_null == 0) { out.text = "-"; break; }
                out.number = sum / static_cast<double>(non_null);
                out.numeric = true;
                out.text = format_number(out.number);
                break;

            case Aggregate::minimum:
                if (has_number) {
                    out.number = minimum;
                    out.numeric = true;
                    out.text = format_number(minimum);
                } else if (has_text) {
                    out.text = text_min;
                } else {
                    out.text = "-";
                }
                break;

            case Aggregate::maximum:
                if (has_number) {
                    out.number = maximum;
                    out.numeric = true;
                    out.text = format_number(maximum);
                } else if (has_text) {
                    out.text = text_max;
                } else {
                    out.text = "-";
                }
                break;

            case Aggregate::none:
                break;
        }
        return out;
    }
};

// Nome da funcao no SQL, para a agregacao no servidor.
std::string_view sql_function(Aggregate aggregate) noexcept {
    switch (aggregate) {
        case Aggregate::count:          return "count";
        case Aggregate::count_non_null: return "count";
        case Aggregate::count_distinct: return "count";
        case Aggregate::sum:            return "sum";
        case Aggregate::average:        return "avg";
        case Aggregate::minimum:        return "min";
        case Aggregate::maximum:        return "max";
        case Aggregate::none:           break;
    }
    return "";
}

} // namespace

std::string_view to_string(Aggregate aggregate) noexcept {
    switch (aggregate) {
        case Aggregate::none:           return "none";
        case Aggregate::count:          return "count";
        case Aggregate::count_non_null: return "count non-null";
        case Aggregate::count_distinct: return "distinct";
        case Aggregate::sum:            return "sum";
        case Aggregate::average:        return "average";
        case Aggregate::minimum:        return "minimum";
        case Aggregate::maximum:        return "maximum";
    }
    return "unknown";
}

bool aggregate_applies(Aggregate aggregate, DataKind kind) noexcept {
    switch (aggregate) {
        // Contar funciona em qualquer coluna.
        case Aggregate::count:
        case Aggregate::count_non_null:
        case Aggregate::count_distinct:
            return true;

        // Somar texto nao significa nada. Oferecer SUM numa coluna de nomes
        // seria um campo que finge funcionar (diretiva 6).
        case Aggregate::sum:
        case Aggregate::average:
            return kind == DataKind::integer || kind == DataKind::floating ||
                   kind == DataKind::numeric;

        // Min e max valem para numero, texto e data -- tudo que tem ordem.
        case Aggregate::minimum:
        case Aggregate::maximum:
            return kind != DataKind::binary && kind != DataKind::json &&
                   kind != DataKind::array;

        case Aggregate::none:
            break;
    }
    return false;
}

AggregateValue aggregate_column(const ResultSet& rs, std::size_t column,
                                Aggregate function) {
    if (column >= rs.column_count() || function == Aggregate::none) return {};

    Accumulator accumulator;
    accumulator.function = function;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        accumulator.add(rs.text(row, column), rs.is_null(row, column));
    }
    return accumulator.value();
}

AggregateValue aggregate_rows(const ResultSet& rs,
                              const std::vector<std::size_t>& rows,
                              std::size_t column, Aggregate function) {
    if (column >= rs.column_count() || function == Aggregate::none) return {};

    Accumulator accumulator;
    accumulator.function = function;

    for (const std::size_t row : rows) {
        if (row >= rs.row_count()) continue;
        accumulator.add(rs.text(row, column), rs.is_null(row, column));
    }
    return accumulator.value();
}

GroupResult group_and_aggregate(const ResultSet& rs, const GroupSpec& spec,
                                bool partial) {
    GroupResult result;
    result.partial      = partial;
    result.rows_covered = rs.row_count();

    // Totais gerais: valem mesmo sem agrupamento -- e' a linha de totais.
    result.totals.reserve(spec.aggregates.size());
    for (const AggregateSpec& aggregate : spec.aggregates) {
        result.totals.push_back(
            aggregate_column(rs, aggregate.column, aggregate.function));
    }

    if (spec.group_by.empty()) return result;

    // Agrupa por chave composta. std::map em vez de hash: a ordem das chaves
    // sai ordenada, que e' como o usuario espera ver os grupos, e o custo de
    // ordenar depois seria o mesmo.
    std::map<std::vector<std::string>, std::vector<std::size_t>> buckets;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        std::vector<std::string> key;
        key.reserve(spec.group_by.size());

        for (const std::size_t column : spec.group_by) {
            if (column >= rs.column_count()) continue;
            // Nulo vira um marcador proprio: duas linhas com NULL na mesma
            // coluna pertencem ao mesmo grupo, como no GROUP BY do SQL.
            key.emplace_back(rs.is_null(row, column) ? "\x01NULL"
                                                     : std::string(rs.text(row, column)));
        }
        buckets[std::move(key)].push_back(row);
    }

    result.groups.reserve(buckets.size());

    for (auto& [key, rows] : buckets) {
        Group group;
        group.key_values = key;
        group.rows       = std::move(rows);

        // Restaura o marcador de nulo para exibicao.
        for (std::string& value : group.key_values) {
            if (value == "\x01NULL") value = "[null]";
        }

        group.aggregates.reserve(spec.aggregates.size());
        for (const AggregateSpec& aggregate : spec.aggregates) {
            Accumulator accumulator;
            accumulator.function = aggregate.function;

            for (const std::size_t row : group.rows) {
                accumulator.add(rs.text(row, aggregate.column),
                                rs.is_null(row, aggregate.column));
            }
            group.aggregates.push_back(accumulator.value());
        }
        result.groups.push_back(std::move(group));
    }
    return result;
}

std::string build_group_query(std::string_view sql, const ResultSet& rs,
                              const GroupSpec& spec) {
    if (spec.group_by.empty() || sql.empty()) return {};

    std::string columns;
    std::string grouping;

    for (const std::size_t column : spec.group_by) {
        if (column >= rs.column_count()) return {};

        const std::string name = quote_if_needed(rs.column(column).info().name);
        if (!columns.empty()) { columns += ", "; grouping += ", "; }
        columns  += name;
        grouping += name;
    }

    for (const AggregateSpec& aggregate : spec.aggregates) {
        if (aggregate.column >= rs.column_count()) return {};
        if (aggregate.function == Aggregate::none) continue;

        const ColumnInfo& info = rs.column(aggregate.column).info();
        const std::string name = quote_if_needed(info.name);

        std::string expression;
        if (aggregate.function == Aggregate::count) {
            expression = "count(*)";
        } else if (aggregate.function == Aggregate::count_distinct) {
            expression = "count(DISTINCT " + name + ")";
        } else {
            expression = std::string(sql_function(aggregate.function)) +
                         "(" + name + ")";
        }

        // Apelido com o nome da funcao e da coluna: sem ele o servidor
        // devolve "sum" para todas as somas e a grade fica ilegivel.
        columns += ", " + expression + " AS " +
                   quote_if_needed(std::string(to_string(aggregate.function)) +
                                   "_" + info.name);
    }

    // Envolve, em vez de anexar GROUP BY: a consulta original pode ja' ter
    // GROUP BY, ORDER BY ou LIMIT, e anexar produziria sintaxe invalida ou
    // agruparia o que ja' estava agrupado (mesmo raciocinio do filtro,
    // ADR 0011).
    return "SELECT " + columns + "\n  FROM (\n" +
           std::string(strip_trailing_semicolon(sql)) +
           "\n) AS otter_group\n GROUP BY " + grouping +
           "\n ORDER BY " + grouping;
}

} // namespace otter::db
