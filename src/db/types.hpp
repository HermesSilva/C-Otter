// C-Otter -- db/types.hpp
//
// Sistema de tipos da camada de dados. Substitui o mapeamento do JDBC por um
// contrato proprio, definido pelo que o nucleo precisa e nao pelo que uma API
// de terceiros oferece (ADR 0001).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace otter::db {

// Categoria logica do valor. E' o que a grade usa para escolher alinhamento,
// formatacao e editor -- nao o tipo especifico do SGBD.
enum class DataKind : std::uint8_t {
    unknown,
    boolean,
    integer,
    floating,
    numeric,      // decimal exato: dinheiro nao cabe em double
    string,
    binary,
    date,
    time,
    timestamp,
    interval,
    uuid,
    json,
    array,
    geometry,
    lob,          // carregado sob demanda
};

[[nodiscard]] std::string_view to_string(DataKind kind) noexcept;

// Alinhamento a direita para numeros e' convencao universal de planilha; ler
// uma coluna de valores desalinhada e' desconfortavel.
[[nodiscard]] constexpr bool is_right_aligned(DataKind kind) noexcept {
    return kind == DataKind::integer || kind == DataKind::floating ||
           kind == DataKind::numeric;
}

// Descricao de uma coluna do resultado.
struct ColumnInfo {
    std::string   name;
    std::string   type_name;      // nome do tipo no SGBD: "int4", "varchar"
    DataKind      kind = DataKind::unknown;
    std::uint32_t type_oid = 0;   // identificador nativo (OID no PostgreSQL)
    std::int32_t  size = -1;      // -1 = variavel
    std::int16_t  precision = 0;  // para numeric
    std::int16_t  scale = 0;
    bool          nullable = true;

    // De onde a coluna veio, informado pelo RowDescription do protocolo v3.
    //
    // Zero significa que a coluna nao e' de uma tabela: e' expressao,
    // agregado ou constante. E' o que permite saber se o resultado da' para
    // editar sem analisar o FROM da consulta, que quebraria com alias,
    // subconsulta e CTE (ADR 0014).
    std::uint32_t source_table_oid = 0;
    std::int16_t  source_column = 0;   // attnum na tabela de origem

    // A mesma informacao por NOME, para os SGBDs que nao tem OID.
    //
    // O MySQL identifica a origem assim: o ColumnDefinition41 traz o banco, a
    // tabela real e o nome real da coluna. `source_table` e' a tabela REAL, e
    // nao o apelido usado na consulta -- um UPDATE contra o apelido nao
    // existe. Vazio tem o mesmo sentido que `source_table_oid == 0`: a coluna
    // e' expressao, agregado ou constante, e nao da' para editar.
    std::string   source_schema;        // banco, no MySQL
    std::string   source_table;
    std::string   source_column_name;   // nome real, quando ha' apelido

    // A coluna veio de uma tabela? Cobre as duas formas de identificacao.
    [[nodiscard]] bool has_source() const noexcept {
        return source_table_oid != 0 || !source_table.empty();
    }
};

} // namespace otter::db
