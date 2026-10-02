// C-Otter -- db/export.hpp
//
// Escreve um ResultSet em CSV, JSON, Markdown, INSERTs, HTML, XML ou TXT.
//
// Sete dos formatos de texto do DBeaver: CSV para planilha, JSON para API,
// Markdown para documentacao, SQL para mover dados entre bancos, HTML e TXT
// para relatorio, XML para integracao. DbUnit, codigo-fonte e os binarios
// (XLSX, Parquet) ficam de fora: cada um e' um formato inteiro, e o que eles
// levam cabe num dos sete.
#pragma once

#include "base/error.hpp"
#include "db/result_set.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace otter::db {

enum class ExportFormat : std::uint8_t {
    csv,
    json,
    markdown,
    sql_insert,
    html,
    xml,
    txt,          // tabela de largura fixa, para colar em e-mail ou chamado
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

// Exportacao em PEDACOS: o arquivo sai de varios ResultSets seguidos, todos
// com as mesmas colunas.
//
// E' o que deixa exportar a consulta inteira, e nao so' a pagina carregada: o
// resultado de dois milhoes de linhas e' lido do servidor aos poucos (cursor)
// e cada pedaco e' escrito e descartado. O cabecalho sai com o primeiro
// pedaco; o rodape' (o `]` do JSON, o `</table>` do HTML), em finish().
class ExportStream {
public:
    explicit ExportStream(ExportOptions options);
    ~ExportStream();

    ExportStream(const ExportStream&)            = delete;
    ExportStream& operator=(const ExportStream&) = delete;

    // Cria os diretorios que faltarem.
    [[nodiscard]] Status open(std::string_view path);
    [[nodiscard]] Status write(const ResultSet& chunk);
    [[nodiscard]] Status finish();

    // Linhas escritas ate' agora.
    [[nodiscard]] std::size_t rows() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace otter::db
