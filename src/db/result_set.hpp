// C-Otter -- db/result_set.hpp
//
// Resultado em formato COLUNAR (ADR 0001 #3).
//
// Cada coluna e' um buffer contiguo com os bytes dos valores mais um indice de
// offsets -- o mesmo esquema do Apache Arrow. Isso elimina a alocacao por
// celula, a razao estrutural do DBeaver consumir 1,5-3 GB num SELECT de 1M
// linhas. A grade le direto destes buffers, sem copia e sem boxing.
//
// NOTA sobre a arena: esta versao usa std::vector, que ja' entrega o ganho
// principal (um buffer por coluna em vez de um objeto por celula). Migrar para
// otter::Arena elimina tambem o realloc durante o fetch; fica para quando o
// benchmark de 1M linhas mostrar que vale -- otimizar antes de medir e' como o
// codigo fica complicado sem motivo.
#pragma once

#include "db/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace otter::db {

// Armazenamento de uma coluna. Valores variaveis ficam concatenados em `data`,
// delimitados por `offsets` -- o mesmo esquema do Arrow.
class Column {
public:
    Column() = default;
    explicit Column(ColumnInfo info) : info_(std::move(info)) {}

    [[nodiscard]] const ColumnInfo& info() const noexcept { return info_; }
    [[nodiscard]] std::size_t row_count() const noexcept {
        return offsets_.empty() ? 0 : offsets_.size() - 1;
    }

    [[nodiscard]] bool is_null(std::size_t row) const noexcept {
        return row < nulls_.size() && nulls_[row];
    }

    // Bytes crus do valor. Vazio quando nulo -- use is_null() para distinguir
    // nulo de string vazia: confundir os dois e' erro classico de cliente SQL.
    [[nodiscard]] std::span<const std::byte> raw(std::size_t row) const noexcept {
        if (row + 1 >= offsets_.size() || is_null(row)) return {};
        const std::size_t begin = offsets_[row];
        const std::size_t end   = offsets_[row + 1];
        return std::span<const std::byte>(data_.data() + begin, end - begin);
    }

    // Texto do valor. Valido enquanto a arena viver.
    [[nodiscard]] std::string_view text(std::size_t row) const noexcept {
        const std::span<const std::byte> bytes = raw(row);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    [[nodiscard]] std::size_t bytes_used() const noexcept { return data_.size(); }

private:
    friend class ResultSetBuilder;

    ColumnInfo             info_;
    std::vector<std::byte> data_;
    std::vector<std::size_t> offsets_{0};   // row_count + 1 entradas
    std::vector<bool>      nulls_;
};

class ResultSet {
public:
    ResultSet() = default;

    [[nodiscard]] std::size_t column_count() const noexcept { return columns_.size(); }
    [[nodiscard]] std::size_t row_count() const noexcept { return row_count_; }

    [[nodiscard]] const Column& column(std::size_t index) const { return columns_[index]; }
    [[nodiscard]] std::span<const Column> columns() const noexcept { return columns_; }

    // Indice da coluna pelo nome, sem diferenciar maiusculas.
    [[nodiscard]] std::optional<std::size_t> find_column(std::string_view name) const;

    // Atalho de leitura por (linha, coluna).
    [[nodiscard]] std::string_view text(std::size_t row, std::size_t col) const {
        return columns_[col].text(row);
    }
    [[nodiscard]] bool is_null(std::size_t row, std::size_t col) const {
        return columns_[col].is_null(row);
    }

    // Linhas afetadas por INSERT/UPDATE/DELETE, quando nao ha' resultado.
    [[nodiscard]] std::int64_t affected_rows() const noexcept { return affected_rows_; }

    [[nodiscard]] std::size_t bytes_used() const noexcept;

    // Esconde as linhas alem de `count`, sem liberar memoria.
    //
    // A paginacao pede uma linha a mais que a pagina para saber se ha' proxima
    // (ADR 0011). Essa linha sobra nao pode aparecer na grade: o usuario veria
    // 201 linhas depois de pedir 200. Aqui so' o contador muda -- os dados
    // ficam onde estao, e nenhuma copia acontece.
    void hide_rows_beyond(std::size_t count) noexcept {
        if (count < row_count_) row_count_ = count;
    }

private:
    friend class ResultSetBuilder;

    std::vector<Column> columns_;
    std::size_t         row_count_ = 0;
    std::int64_t        affected_rows_ = -1;
};

// Monta um ResultSet coluna a coluna. Os drivers usam isto; o consumidor
// recebe apenas o ResultSet pronto e imutavel.
class ResultSetBuilder {
public:
    void add_column(ColumnInfo info);

    // Acrescenta um valor a uma coluna. Deve ser chamado na ordem das colunas
    // dentro de cada linha.
    void append(std::size_t column, std::span<const std::byte> value);
    void append_text(std::size_t column, std::string_view value);
    void append_null(std::size_t column);

    void set_row_count(std::size_t count) { result_.row_count_ = count; }
    void set_affected_rows(std::int64_t count) { result_.affected_rows_ = count; }

    [[nodiscard]] ResultSet take() { return std::move(result_); }

private:
    ResultSet result_;
};

} // namespace otter::db
