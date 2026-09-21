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
#include <set>
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

// Linha marcada para exclusao. Guarda os valores da chave, nao o indice: o
// indice muda se o resultado for relido, e a chave identifica a linha no
// banco.
struct RowDeletion {
    std::size_t row = 0;                    // linha na grade, para desenhar
    std::vector<std::string> key_values;    // na ordem de EditTarget::key_columns
};

// Linha nova, ainda nao inserida. As colunas ausentes ficam com o DEFAULT da
// tabela -- e' por isso que o INSERT lista so' o que foi preenchido.
struct RowInsertion {
    // Coluna -> valor. Indices do ResultSet, para casar com o cabecalho.
    std::map<std::size_t, std::string> values;
    std::map<std::size_t, bool>        nulls;
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

    // Quantas LINHAS distintas serao afetadas: alteradas, excluidas ou
    // inseridas. E' o numero de comandos que a gravacao vai gerar, e o que
    // faz sentido mostrar ao usuario.
    [[nodiscard]] std::size_t touched_rows() const;

    [[nodiscard]] const std::map<std::pair<std::size_t, std::size_t>, CellEdit>&
    edits() const noexcept { return edits_; }

    // --- Exclusao -----------------------------------------------------------
    void mark_deleted(std::size_t row);
    void unmark_deleted(std::size_t row);
    [[nodiscard]] bool is_deleted(std::size_t row) const;

    [[nodiscard]] const std::set<std::size_t>& deleted() const noexcept {
        return deleted_;
    }

    // --- Insercao -----------------------------------------------------------
    //
    // Devolve o indice da linha nova no vetor de insercoes; a grade a desenha
    // depois das linhas do resultado.
    std::size_t add_row();
    void remove_new_row(std::size_t index);
    void set_new_value(std::size_t index, std::size_t column,
                       std::string value);
    void set_new_null(std::size_t index, std::size_t column);

    [[nodiscard]] const std::vector<RowInsertion>& insertions() const noexcept {
        return insertions_;
    }

    // Ha' algo a gravar? Alteracao, exclusao ou insercao.
    [[nodiscard]] bool has_changes() const noexcept {
        return !edits_.empty() || !deleted_.empty() || !insertions_.empty();
    }

    [[nodiscard]] std::size_t change_count() const noexcept {
        return edits_.size() + deleted_.size() + insertions_.size();
    }

private:
    std::map<std::pair<std::size_t, std::size_t>, CellEdit> edits_;
    std::set<std::size_t>      deleted_;
    std::vector<RowInsertion>  insertions_;
};

// Gera um UPDATE por linha alterada.
//
// Falha quando o alvo nao e' editavel, ou quando uma coluna da chave esta'
// nula na linha -- `WHERE id = NULL` nunca casa, e gerar isso produziria um
// UPDATE que altera zero linhas em silencio.
[[nodiscard]] Result<std::vector<std::string>> generate_updates(
    const ResultSet& rs, const EditTarget& target, const EditBuffer& buffer);

// Gera INSERT, UPDATE e DELETE na ordem em que devem rodar.
//
// A ordem importa: INSERT antes de DELETE evita violar chave estrangeira
// quando a linha nova referencia algo que a exclusao removeria -- e e' a
// ordem que o usuario espera ao ver o resultado depois.
[[nodiscard]] Result<std::vector<std::string>> generate_changes(
    const ResultSet& rs, const EditTarget& target, const EditBuffer& buffer);

} // namespace otter::db
