#include "db/pivot.hpp"

#include "db/ddl.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace otter::db {
namespace {

// Marcador de nulo na chave. Duas linhas com NULL na mesma coluna pertencem
// ao MESMO grupo, como no GROUP BY do SQL -- ainda que NULL <> NULL numa
// comparação.
constexpr std::string_view kNullKey = "\x01NULL";

std::string key_of(const ResultSet& rs, std::size_t row, std::size_t column) {
    return rs.is_null(row, column) ? std::string(kNullKey)
                                   : std::string(rs.text(row, column));
}

std::string display(std::string_view key) {
    return key == kNullKey ? "[null]" : std::string(key);
}

} // namespace

PivotResult pivot(const ResultSet& rs, const PivotSpec& spec, bool partial,
                  std::size_t max_columns) {
    PivotResult out;
    out.partial      = partial;
    out.rows_covered = rs.row_count();

    if (spec.rows.empty() || spec.column >= rs.column_count() ||
        spec.value >= rs.column_count()) {
        return out;
    }
    for (const std::size_t column : spec.rows) {
        if (column >= rs.column_count()) return out;
    }

    // Os valores distintos da coluna pivotada, ordenados. std::set em vez de
    // hash: a ordem das colunas geradas precisa ser estável e previsível --
    // "jan, fev, mar" embaralhado a cada execução seria inutilizável.
    std::set<std::string> distinct;
    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        distinct.insert(key_of(rs, row, spec.column));
    }
    out.distinct_values = distinct.size();

    // Cardinalidade alta gera colunas demais. Truncar é melhor que travar a
    // grade -- mas truncar em SILÊNCIO faria o usuário concluir que os dados
    // não existem.
    if (distinct.size() > max_columns) {
        out.truncated = true;
        auto it = distinct.begin();
        std::advance(it, static_cast<std::ptrdiff_t>(max_columns));
        distinct.erase(it, distinct.end());
    }

    out.headers.reserve(distinct.size());
    std::map<std::string, std::size_t> header_index;

    for (const std::string& value : distinct) {
        header_index[value] = out.headers.size();
        out.headers.push_back(display(value));
    }

    // Uma linha por chave, e dentro dela um acumulador por coluna gerada.
    //
    // std::map pela mesma razão do set: a ordem das linhas sai ordenada, que
    // é como o usuário espera ver.
    struct Bucket {
        std::vector<std::vector<std::size_t>> cells;   // linhas por coluna
    };
    std::map<std::vector<std::string>, Bucket> buckets;

    for (std::size_t row = 0; row < rs.row_count(); ++row) {
        const std::string value = key_of(rs, row, spec.column);

        const auto found = header_index.find(value);
        if (found == header_index.end()) continue;   // caiu no truncamento

        std::vector<std::string> key;
        key.reserve(spec.rows.size());
        for (const std::size_t column : spec.rows) {
            key.push_back(key_of(rs, row, column));
        }

        Bucket& bucket = buckets[std::move(key)];
        if (bucket.cells.empty()) bucket.cells.resize(out.headers.size());
        bucket.cells[found->second].push_back(row);
    }

    out.rows.reserve(buckets.size());

    for (const auto& [key, bucket] : buckets) {
        PivotRow pivot_row;
        pivot_row.keys.reserve(key.size());
        for (const std::string& part : key) pivot_row.keys.push_back(display(part));

        pivot_row.cells.resize(out.headers.size());

        for (std::size_t column = 0; column < out.headers.size(); ++column) {
            if (column >= bucket.cells.size() || bucket.cells[column].empty()) {
                // Célula vazia: a combinação (linha, coluna) não existe nos
                // dados. Mostrar "0" seria mentira -- zero é um valor, ausente
                // é outra coisa.
                pivot_row.cells[column].text = "-";
                continue;
            }
            pivot_row.cells[column] =
                aggregate_rows(rs, bucket.cells[column], spec.value, spec.function);
        }
        out.rows.push_back(std::move(pivot_row));
    }

    // Totais por coluna gerada, sobre TODAS as linhas que caem nela.
    out.totals.resize(out.headers.size());
    for (std::size_t column = 0; column < out.headers.size(); ++column) {
        std::vector<std::size_t> rows;
        for (const auto& [key, bucket] : buckets) {
            if (column >= bucket.cells.size()) continue;
            rows.insert(rows.end(), bucket.cells[column].begin(),
                        bucket.cells[column].end());
        }
        if (rows.empty()) {
            out.totals[column].text = "-";
        } else {
            out.totals[column] =
                aggregate_rows(rs, rows, spec.value, spec.function);
        }
    }
    return out;
}

std::string build_pivot_query(std::string_view sql, const ResultSet& rs,
                              const PivotSpec& spec,
                              const std::vector<std::string>& headers) {
    if (spec.rows.empty() || sql.empty() || headers.empty()) return {};
    if (spec.column >= rs.column_count() || spec.value >= rs.column_count()) {
        return {};
    }

    const std::string pivot_column =
        quote_if_needed(rs.column(spec.column).info().name);
    const std::string value_column =
        quote_if_needed(rs.column(spec.value).info().name);

    std::string columns;
    std::string grouping;

    for (const std::size_t column : spec.rows) {
        if (column >= rs.column_count()) return {};

        const std::string name = quote_if_needed(rs.column(column).info().name);
        if (!columns.empty()) { columns += ", "; grouping += ", "; }
        columns  += name;
        grouping += name;
    }

    // Um agregado CONDICIONAL por valor distinto.
    //
    // Não usamos PIVOT: ele só existe no SQL Server e no Oracle. O CASE é
    // padrão SQL e funciona em PostgreSQL e MySQL -- e é o que o próprio
    // DBeaver gera para os bancos que não têm PIVOT.
    const std::string function =
        spec.function == Aggregate::count ? "count"
      : spec.function == Aggregate::average ? "avg"
      : spec.function == Aggregate::minimum ? "min"
      : spec.function == Aggregate::maximum ? "max"
                                            : "sum";

    for (const std::string& header : headers) {
        // "[null]" é a nossa representação de nulo, não um valor do banco: a
        // comparação tem de ser IS NULL, e não = '[null]'.
        const std::string condition =
            header == "[null]"
                ? pivot_column + " IS NULL"
                : pivot_column + " = " + quote_literal(header);

        columns += ",\n         " + function + "(CASE WHEN " + condition +
                   " THEN " + value_column + " END) AS " +
                   quote_if_needed(header);
    }

    // Envolve, em vez de anexar: a consulta original pode já ter GROUP BY,
    // ORDER BY ou LIMIT (mesmo raciocínio do ADR 0011).
    // O ';' final do usuário não pode ir para dentro da subconsulta: o
    // servidor recusa apontando para o ')', o que manda procurar no lugar
    // errado. Foi o que apareceu na tela ao pivotar no servidor.
    return "SELECT " + columns + "\n  FROM (\n" +
           std::string(strip_trailing_semicolon(sql)) +
           "\n) AS otter_pivot\n GROUP BY " + grouping +
           "\n ORDER BY " + grouping;
}

} // namespace otter::db
