// C-Otter -- db/aggregate.hpp
//
// Agrupamento e totais sobre o ResultSet ja' baixado (ADR 0005).
//
// A honestidade e' o ponto: com paginacao, o que esta' na memoria e' UMA
// PAGINA. Somar 200 linhas de uma tabela de 2 milhoes e chamar o resultado de
// "total" e' mentira. Toda agregacao local carrega a marca `partial`, e a
// interface precisa mostra-la.
#pragma once

#include "db/result_set.hpp"

#include <optional>
#include <string>
#include <vector>

namespace otter::db {

enum class Aggregate : std::uint8_t {
    none,
    count,          // linhas, inclusive nulas
    count_non_null,
    count_distinct,
    sum,
    average,
    minimum,
    maximum,
};

[[nodiscard]] std::string_view to_string(Aggregate aggregate) noexcept;

// Uma agregacao faz sentido para esta coluna?
//
// Somar texto nao significa nada; contar, sim. Oferecer SUM numa coluna de
// nomes seria um campo que finge funcionar.
[[nodiscard]] bool aggregate_applies(Aggregate aggregate, DataKind kind) noexcept;

struct AggregateSpec {
    std::size_t column = 0;
    Aggregate   function = Aggregate::none;
};

// Valor agregado. `text` ja' vem formatado para exibicao.
struct AggregateValue {
    std::string text;
    double      number = 0.0;
    bool        numeric = false;   // `number` e' significativo?
};

// Um grupo: as chaves que o definem, os agregados e as linhas que o compoem.
struct Group {
    std::vector<std::string>    key_values;   // uma por coluna de agrupamento
    std::vector<AggregateValue> aggregates;   // na ordem de GroupSpec
    std::vector<std::size_t>    rows;         // indices no ResultSet
};

struct GroupSpec {
    std::vector<std::size_t>   group_by;
    std::vector<AggregateSpec> aggregates;

    [[nodiscard]] bool empty() const noexcept { return group_by.empty(); }
};

struct GroupResult {
    std::vector<Group>          groups;
    std::vector<AggregateValue> totals;     // agregados sobre TUDO

    // Verdadeiro quando o ResultSet cobre so' parte do resultado da consulta
    // -- ou seja, sempre que houver paginacao. A UI precisa dizer isso:
    // "soma de 200 de 2 milhoes" nao e' "a soma".
    bool partial = false;

    std::size_t rows_covered = 0;
};

// Agrupa e agrega. Uma passada sobre o buffer colunar.
//
// `partial` propaga para o resultado: quem chama sabe se o ResultSet e' uma
// pagina ou o resultado inteiro, e essa informacao nao pode se perder.
[[nodiscard]] GroupResult group_and_aggregate(const ResultSet& rs,
                                              const GroupSpec& spec,
                                              bool partial);

// Um agregado sobre uma coluna inteira, para a linha de totais.
[[nodiscard]] AggregateValue aggregate_column(const ResultSet& rs,
                                              std::size_t column,
                                              Aggregate function);

// Reescreve a consulta com GROUP BY, para agregar no SERVIDOR.
//
// E' a resposta honesta quando o resultado e' paginado: em vez de somar a
// pagina, pergunta ao banco. Devolve vazio quando a consulta nao pode ser
// envolvida com seguranca.
[[nodiscard]] std::string build_group_query(std::string_view sql,
                                            const ResultSet& rs,
                                            const GroupSpec& spec);

} // namespace otter::db
