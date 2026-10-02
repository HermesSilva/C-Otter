// C-Otter -- db/import.hpp
//
// Importar um CSV para uma tabela: o "Import Data" do DBeaver.
//
// Mapa do assistente do DBeaver (data transfer, origem CSV): arquivo,
// delimitador, aspas, cabecalho, texto de NULL, o mapeamento coluna do arquivo
// -> coluna da tabela (por nome, ajustavel), "truncate target table before
// load" e o tamanho do lote.
//
// Aqui fica a REGRA -- ler o CSV e gerar os INSERTs -- sem janela e sem
// servidor, para ser testada: e' o tipo de codigo que erra em silencio (um
// campo com quebra de linha entre aspas vira duas linhas; um vazio vira
// string onde deveria ser NULL).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

struct CsvOptions {
    char        delimiter = ',';
    char        quote = '"';
    bool        header = true;

    // O texto que significa NULL. Vale so' para campo SEM aspas: `a,,b` tem um
    // NULL no meio; `a,"",b` tem uma string vazia. E' a distincao do RFC 4180
    // que deixa um CSV exportado voltar igual.
    std::string null_text;
};

struct CsvTable {
    std::vector<std::string> header;   // nomes; "column1"... se nao ha' cabecalho

    // nullopt = NULL.
    std::vector<std::vector<std::optional<std::string>>> rows;

    // Linhas do arquivo alem de `max_rows`, nao carregadas (previa).
    bool truncated = false;

    // Primeira linha malformada: numero de campos diferente do cabecalho,
    // aspas que nao fecham. Vazio = arquivo integro.
    std::string error;
};

// Adivinha o delimitador pela primeira linha: o que mais aparece entre
// virgula, ponto e virgula, tab e barra vertical. Planilha em portugues
// exporta com ';' -- abrir assumindo ',' daria uma coluna so'.
[[nodiscard]] char detect_delimiter(std::string_view text) noexcept;

// Le' o CSV inteiro (ou as primeiras `max_rows` linhas de dados). Aceita BOM
// UTF-8, CRLF e LF, e quebra de linha dentro de campo entre aspas.
[[nodiscard]] CsvTable parse_csv(std::string_view text, const CsvOptions& options,
                                 std::size_t max_rows = static_cast<std::size_t>(-1));

struct ImportPlan {
    std::string schema;
    std::string table;

    // Para cada coluna do ARQUIVO, a coluna da tabela que a recebe. Vazio =
    // a coluna do arquivo e' ignorada.
    std::vector<std::string> columns;

    bool        truncate_first = false;
    std::size_t batch_rows = 500;
};

// Mapeamento inicial: cada coluna do arquivo vai para a coluna da tabela de
// mesmo nome (sem diferenciar maiusculas); as que nao casam ficam ignoradas.
[[nodiscard]] std::vector<std::string> match_columns(
    const std::vector<std::string>& file_columns,
    const std::vector<std::string>& table_columns);

struct ImportScript {
    std::vector<std::string> statements;   // TRUNCATE (se pedido) + INSERTs em lote
    std::size_t              rows = 0;
    std::string              error;        // nada mapeado, tabela sem nome...
};

// Os comandos da carga. Os valores vao como literais de TEXTO: o servidor os
// converte para o tipo da coluna, e a recusa (uma data invalida) vem dele,
// com a linha do lote -- melhor que adivinhar o tipo aqui e converter errado.
// O dialeto de citacao e' o corrente (db/ddl.hpp).
[[nodiscard]] ImportScript generate_import(const CsvTable& data, const ImportPlan& plan);

} // namespace otter::db
