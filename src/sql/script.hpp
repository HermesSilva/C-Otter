// C-Otter -- sql/script.hpp
//
// Separacao de um script em statements e analise de escopo.
//
// O splitter parece trivial e nao e': precisa respeitar strings, comentarios,
// $$ ... $$ do PostgreSQL, DELIMITER do MySQL e blocos BEGIN/END. Um split
// ingenuo por ';' quebra em qualquer funcao armazenada.
#pragma once

#include "sql/lexer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace otter::sql {

struct Statement {
    std::string_view text;       // fatia do script, sem o delimitador
    std::size_t      offset = 0;
    std::size_t      line = 0;   // linha inicial, base zero

    [[nodiscard]] bool empty() const noexcept;
};

// Separa o script em statements executaveis.
[[nodiscard]] std::vector<Statement> split_script(std::string_view script,
                                                  const Dialect& dialect);

// Statement que contem a posicao dada; util para "executar o statement sob o
// cursor".
[[nodiscard]] std::optional<Statement> statement_at(std::string_view script,
                                                    const Dialect& dialect,
                                                    std::size_t offset);

// --- Analise de escopo, para o completion (ADR 0004, camada 2) --------------

// Uma tabela referenciada na query, com o alias se houver.
struct TableRef {
    std::string name;
    std::string alias;
    std::string schema;
};

// O que faz sentido sugerir na posicao do cursor.
enum class CompletionContext {
    unknown,
    table_expected,     // depois de FROM, JOIN, INTO, UPDATE
    column_expected,    // depois de SELECT, WHERE, ON, GROUP BY, ORDER BY, SET
    alias_member,       // depois de "alias." -- so' colunas daquela tabela
    schema_member,      // depois de "schema." -- tabelas daquele schema
};

struct ScopeInfo {
    CompletionContext     context = CompletionContext::unknown;
    std::vector<TableRef> tables;        // tabelas visiveis na query
    std::string           qualifier;     // texto antes do '.', se houver
    std::string           prefix;        // palavra parcial sob o cursor
};

// Analisa o statement sob o cursor e devolve o que faz sentido sugerir.
//
// Tolerante a erro por necessidade: o texto esta' incompleto enquanto o
// usuario digita.
[[nodiscard]] ScopeInfo analyze_scope(std::string_view script,
                                      const Dialect& dialect,
                                      std::size_t cursor_offset);

} // namespace otter::sql
