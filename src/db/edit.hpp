// C-Otter -- db/edit.hpp
//
// Edicao de dados pela grade: decide se da' para editar e gera os UPDATE.
//
// A regra que define tudo, do ADR 0014: sem chave primaria (ou unica) nao ha'
// como escrever o WHERE, e sem WHERE confiavel a grade NAO edita -- diz o
// motivo em vez de oferecer um campo que falha ao salvar.
#pragma once

#include "base/error.hpp"
#include "db/catalog.hpp"
#include "db/result_set.hpp"

#include <map>
#include <string>
#include <vector>

namespace otter::db {

// Por que um resultado nao pode ser editado. Vai para a interface: o usuario
// precisa saber que a grade e' somente leitura E por que (diretiva 6).
enum class EditRefusal : std::uint8_t {
    none,
    no_result,
    multiple_tables,     // JOIN: nao ha' para onde escrever
    no_source_table,     // so' expressoes e agregados
    key_not_loaded,      // as constraints da tabela ainda nao foram lidas
    no_key,              // a tabela nao tem PK nem constraint unica
    key_not_selected,    // tem PK, mas ela nao esta' no SELECT
};

[[nodiscard]] std::string_view to_string(EditRefusal refusal) noexcept;

// O que e' preciso saber para gerar um UPDATE.
struct EditTarget {
    std::string schema;
    std::string table;

    // Indices, no ResultSet, das colunas que formam a chave.
    std::vector<std::size_t> key_columns;

    EditRefusal refusal = EditRefusal::none;

    [[nodiscard]] bool editable() const noexcept {
        return refusal == EditRefusal::none;
    }
};

// Descobre se o resultado da' para editar.
//
// Usa `source_table_oid` das colunas, que vem do RowDescription -- analisar o
// FROM da consulta quebraria com alias, subconsulta e CTE.
//
// `schemas` fornece as chaves: a tabela precisa estar no catalogo com suas
// constraints ja' carregadas.
[[nodiscard]] EditTarget find_edit_target(
    const ResultSet& rs, const std::vector<SchemaMeta>& schemas);

// Uma celula alterada, ainda nao gravada.
struct CellEdit {
    std::size_t row = 0;
    std::size_t column = 0;
    std::string value;
    bool        is_null = false;
};

// Buffer de alteracoes pendentes de um resultado.
//
// Editar em buffer, e nao gravar a cada tecla, e' o que permite desistir --
// e evita um UPDATE por caractere digitado.
class EditBuffer {
public:
    void set(std::size_t row, std::size_t column, std::string value);
    void set_null(std::size_t row, std::size_t column);
    void clear();

    // Desfaz a alteracao de uma celula, se houver.
    void revert(std::size_t row, std::size_t column);

    [[nodiscard]] bool empty() const noexcept { return edits_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return edits_.size(); }

    [[nodiscard]] const CellEdit* find(std::size_t row,
                                       std::size_t column) const;

    // Quantas LINHAS distintas foram tocadas -- e' o numero de UPDATEs que
    // serao gerados, e o que faz sentido mostrar ao usuario.
    [[nodiscard]] std::size_t touched_rows() const;

    [[nodiscard]] const std::map<std::pair<std::size_t, std::size_t>, CellEdit>&
    edits() const noexcept { return edits_; }

private:
    std::map<std::pair<std::size_t, std::size_t>, CellEdit> edits_;
};

// Gera um UPDATE por linha alterada.
//
// Falha quando o alvo nao e' editavel, ou quando uma coluna da chave esta'
// nula na linha -- `WHERE id = NULL` nunca casa, e gerar isso produziria um
// UPDATE que altera zero linhas em silencio.
[[nodiscard]] Result<std::vector<std::string>> generate_updates(
    const ResultSet& rs, const EditTarget& target, const EditBuffer& buffer);

} // namespace otter::db
