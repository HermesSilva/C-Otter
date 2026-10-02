// C-Otter -- db/grid_ops.hpp
//
// O que a grade de resultado faz com uma SELECAO de celulas, sem interface:
// recortar, copiar, colar, gerar SQL das linhas, somar, listar valores
// distintos e montar as consultas de navegacao por chave estrangeira.
//
// Fica aqui, e nao na UI, porque cada uma destas regras tem um jeito errado
// silencioso -- copiar o valor do banco em vez do editado, gerar um DELETE
// sem a chave inteira, somar texto -- e na UI nao ha' como testar.
#pragma once

#include "db/catalog.hpp"
#include "db/edit.hpp"
#include "db/result_set.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// Um bloco retangular da grade: linhas [row_first, row_last] e as colunas
// listadas, na ordem em que aparecem na tela (o usuario pode ter reordenado e
// escondido colunas).
struct GridSelection {
    std::size_t              row_first = 0;
    std::size_t              row_last  = 0;
    std::vector<std::size_t> columns;

    [[nodiscard]] std::size_t row_count() const noexcept {
        return row_last >= row_first ? row_last - row_first + 1 : 0;
    }
    [[nodiscard]] bool single_cell() const noexcept {
        return row_count() == 1 && columns.size() == 1;
    }
};

// O valor que esta' NA TELA: o do buffer de edicao tem precedencia sobre o do
// resultado. `is_null` distingue NULL de texto vazio.
struct CellValue {
    std::string text;
    bool        is_null = false;
};
[[nodiscard]] CellValue cell_value(const ResultSet& rs, const EditBuffer& edits,
                                   std::size_t row, std::size_t column);

// Um ResultSet so' com a selecao -- o que os exportadores (CSV, JSON,
// Markdown, INSERT) recebem em "Copy as".
[[nodiscard]] ResultSet slice(const ResultSet& rs, const EditBuffer& edits,
                              const GridSelection& selection);

// Texto para a area de transferencia: valores separados por tab, linhas por
// '\n' -- o que uma planilha cola em celulas. Uma celula so' sai sem
// separador nenhum. NULL sai vazio.
[[nodiscard]] std::string selection_to_text(const ResultSet& rs,
                                            const EditBuffer& edits,
                                            const GridSelection& selection,
                                            bool with_header = false);

// O inverso: texto da area de transferencia em linhas e colunas. Aceita
// "\r\n". Uma linha vazia no FIM (o '\n' final que a planilha poe) e'
// descartada; as do meio sao linhas de verdade.
[[nodiscard]] std::vector<std::vector<std::string>> parse_clipboard(
    std::string_view text);

// --- Script das linhas (resultset.generateScript) ------------------------------

enum class RowScript : std::uint8_t {
    select_by_key,   // SELECT ... WHERE chave = ...
    insert,          // INSERT com os valores da linha
    update,          // UPDATE de todas as colunas, WHERE chave
    delete_by_key,   // DELETE WHERE chave
};

// Um comando por linha selecionada. SELECT, UPDATE e DELETE exigem a chave:
// sem ela o WHERE alcancaria linhas demais, e a funcao falha em vez de gerar.
// INSERT so' precisa da tabela.
[[nodiscard]] Result<std::string> row_script(const ResultSet& rs,
                                             const EditBuffer& edits,
                                             const EditTarget& target,
                                             const GridSelection& selection,
                                             RowScript kind);

// --- Painel de calculo (column-aggregate) -------------------------------------

struct SelectionStats {
    std::size_t cells    = 0;
    std::size_t nulls    = 0;
    std::size_t distinct = 0;   // valores nao nulos diferentes

    // So' quando TODAS as celulas nao nulas sao numeros.
    bool        numeric = false;
    double      sum     = 0.0;
    double      average = 0.0;

    // Menor e maior valor: numerico quando `numeric`, senao lexicografico.
    std::string minimum;
    std::string maximum;
};
[[nodiscard]] SelectionStats selection_stats(const ResultSet& rs,
                                             const EditBuffer& edits,
                                             const GridSelection& selection);

// --- Valores distintos (filterMenu.distinct) -----------------------------------

struct DistinctValue {
    std::string text;
    bool        is_null = false;
    std::size_t count   = 0;
};

// Os valores de uma coluna nas linhas CARREGADAS, do mais frequente para o
// menos. E' a saida quando a consulta nao pode ser refeita no servidor.
[[nodiscard]] std::vector<DistinctValue> distinct_values(const ResultSet& rs,
                                                         std::size_t column,
                                                         std::size_t limit = 200);

// A consulta que pede os distintos ao SERVIDOR, sobre o resultado inteiro:
// `SELECT col, COUNT(*) FROM (consulta) GROUP BY 1 ORDER BY 2 DESC LIMIT n`.
[[nodiscard]] std::string distinct_query(std::string_view inner_sql,
                                         std::string_view column,
                                         std::size_t limit = 200);

// A expressao de filtro "igual a este valor": `= 'x'`, `= 42`, `IS NULL`.
[[nodiscard]] std::string equals_expression(const ColumnInfo& info,
                                            std::string_view value, bool is_null);

// --- Navegacao por chave estrangeira -------------------------------------------

// Uma consulta pronta e o titulo de onde ela leva.
struct LinkQuery {
    std::string title;   // "cliente", "pedido (cliente_id)"
    std::string sql;
};

// "Navigate link": a linha REFERENCIADA pela coluna `column`, que faz parte
// de uma chave estrangeira de `table`. Vazio (sql vazio) quando a coluna nao
// e' de FK nenhuma, ou quando o valor e' NULL -- nao ha' para onde ir.
[[nodiscard]] LinkQuery navigate_link_query(const ResultSet& rs,
                                            const EditBuffer& edits,
                                            const TableMeta& table,
                                            std::string_view schema,
                                            std::size_t row, std::size_t column);

// "References": as linhas de OUTRAS tabelas que apontam para esta. Uma
// consulta por chave estrangeira de `table.references` cujas colunas de
// destino estejam no resultado e nao sejam NULL na linha.
[[nodiscard]] std::vector<LinkQuery> reference_queries(const ResultSet& rs,
                                                       const EditBuffer& edits,
                                                       const TableMeta& table,
                                                       std::string_view schema,
                                                       std::size_t row);

} // namespace otter::db
