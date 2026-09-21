// C-Otter -- db/export.hpp
//
// Escreve um ResultSet em CSV, JSON, Markdown ou INSERTs.
//
// Formatos escolhidos entre os 11 do DBeaver por frequencia de uso real:
// CSV para planilha, JSON para API, Markdown para documentacao e SQL para
// mover dados entre bancos. XML, HTML, DbUnit e codigo-fonte ficam de fora --
// cada um vale um exportador quando alguem precisar.
#pragma once

#include "base/error.hpp"
#include "db/result_set.hpp"

#include <string>
#include <string_view>

namespace otter::db {

enum class ExportFormat : std::uint8_t {
    csv,
    json,
    markdown,
    sql_insert,
};

[[nodiscard]] std::string_view to_string(ExportFormat format) noexcept;
[[nodiscard]] std::string_view file_extension(ExportFormat format) noexcept;

struct ExportOptions {
    ExportFormat format = ExportFormat::csv;

    // --- CSV -----------------------------------------------------------------
    char        delimiter = ',';
    bool        write_header = true;

    // Texto que representa NULL. Vazio e' o padrao do DBeaver e o certo para
    // reimportar; "[null]" seria confundido com o literal.
    std::string null_text;

    // Protecao contra CSV injection (OWASP).
    //
    // Um valor comecando por '=', '+', '-' ou '@' vira formula quando a
    // planilha abre o arquivo -- e formula executa. Prefixar com apostrofo
    // neutraliza sem alterar o que o usuario ve' na celula.
    //
    // LIGADO por padrao: quem exporta dados de banco raramente controla o que
    // ha' neles.
    bool        escape_formulas = true;

    // --- SQL -----------------------------------------------------------------
    std::string table_name = "tabela";   // destino dos INSERTs

    // Um INSERT por linha, ou um so' com varias tuplas. Multi-linha e' bem
    // mais rapido de carregar; uma por linha e' mais facil de editar a mao.
    bool        one_statement_per_row = true;
};

// Serializa o resultado inteiro para texto.
[[nodiscard]] std::string export_to_string(const ResultSet& rs,
                                           const ExportOptions& options);

// Grava em arquivo. Cria os diretorios que faltarem.
[[nodiscard]] Status export_to_file(const ResultSet& rs,
                                    const ExportOptions& options,
                                    std::string_view path);

} // namespace otter::db
