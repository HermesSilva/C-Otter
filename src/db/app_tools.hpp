// C-Otter -- db/app_tools.hpp
//
// As regras puras por tras dos comandos de aplicacao e do navegador
// (ui/app_commands.cpp): nada aqui abre conexao nem desenha. Estao juntas
// porque cada uma e' pequena, e separadas da UI porque sao o que se testa.
#pragma once

#include "base/error.hpp"
#include "db/catalog.hpp"
#include "db/connection_config.hpp"
#include "db/edit.hpp"
#include "db/grid_ops.hpp"
#include "db/result_set.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// --- "New connection from JDBC URL" (core.new.connection.from.url) -------------
//
// Aceita o que se copia de uma configuracao de aplicacao:
//
//   jdbc:postgresql://host:5432/banco?user=u&password=p&sslmode=require
//   postgresql://usuario:senha@host:5432/banco        (libpq, tambem postgres://)
//   jdbc:mysql://host:3306/banco      mysql://...      jdbc:mariadb://...
//
// Parametro que nao e' user/password/ssl vira propriedade do driver.
[[nodiscard]] Result<ConnectionProfile> profile_from_url(std::string_view url);

// --- Pastas de conexao (core.new.folder) -------------------------------------------
//
// Uma pasta e' um CAMINHO: "Clientes/Producao". E' o que o DBeaver grava no
// campo `folder` de cada conexao (DataSourceFolder.getFolderPath).

// Sem espacos nas pontas, sem barra sobrando nem partes vazias: " a // b/ " -> "a/b".
[[nodiscard]] std::string folder_normalize(std::string_view path);
// "a/b/c" -> "a/b"; "a" -> "".
[[nodiscard]] std::string folder_parent(std::string_view path);
// "a/b/c" -> "c".
[[nodiscard]] std::string folder_leaf(std::string_view path);
// ("a", "b") -> "a/b"; ("", "b") -> "b".
[[nodiscard]] std::string folder_join(std::string_view parent, std::string_view name);
// `path` e' a pasta `folder` ou esta' dentro dela? ("ab" NAO esta' em "a".)
[[nodiscard]] bool folder_contains(std::string_view folder, std::string_view path);
// Troca o prefixo `from` por `to` -- renomear e mover. Com `to` = o pai de
// `from`, e' apagar a pasta: o que havia nela sobe um nivel. Fora de `from`,
// o caminho volta como veio.
[[nodiscard]] std::string folder_rebase(std::string_view path, std::string_view from,
                                        std::string_view to);

// --- "Advanced copy" (core.edit.copy.special) -----------------------------------
//
// As opcoes do dialogo do DBeaver (ResultSetHandlerCopySpecial.CopyConfigDialog).
struct CopyOptions {
    std::string column_delimiter = "\t";
    std::string row_delimiter    = "\n";
    std::string quote            = "\"";
    bool        quote_always     = false;
    bool        copy_header      = false;
    bool        copy_row_numbers = false;
    std::string null_text;
    // Numero da primeira linha da selecao (1 + deslocamento da pagina).
    std::size_t first_row_number = 1;
};

[[nodiscard]] std::string advanced_copy(const ResultSet& rs, const EditBuffer& edits,
                                        const GridSelection& selection,
                                        const CopyOptions& options);

// "\t", "\n", "\\" digitados num campo de texto viram o caractere.
[[nodiscard]] std::string unescape_delimiter(std::string_view text);
[[nodiscard]] std::string escape_delimiter(std::string_view text);

// --- "Generate UUID" (core.generate.uuid) ----------------------------------------
//
// UUID versao 4 a partir de 16 bytes aleatorios -- separado da fonte dos
// bytes para o teste conferir versao e variante.
[[nodiscard]] std::string format_uuid_v4(std::array<std::uint8_t, 16> bytes);
[[nodiscard]] std::string generate_uuid();

// --- "Execute stored procedure" (core.procedure.execute) -------------------------
//
// O script que chama a rotina: `CALL` para procedure, `SELECT * FROM` para
// funcao, com um marcador nomeado por parametro de entrada (":nome"), que o
// editor pergunta ao executar.
//
// No dialeto do SQL Server (db::sql_dialect()) sai `EXEC [s].[p] @a = :a`,
// com uma variavel declarada para cada parametro OUTPUT e um SELECT delas no
// fim -- e' assim que se le' o que a procedure devolveu.
[[nodiscard]] std::string routine_call_sql(std::string_view schema,
                                           const RoutineMeta& routine, bool mysql);

// --- "Generate DDL by ResultSet" (ui.editors.sql.generate.ddl.by.resultSet) ------
//
// CREATE TABLE com as colunas do resultado, no tipo que o servidor informou.
[[nodiscard]] std::string create_table_from_result(const ResultSet& rs,
                                                   std::string_view table, bool mysql);

// --- Filtro de objetos da arvore (core.object.filter.*) ---------------------------
//
// O `DBSObjectFilter` do DBeaver: listas de mascaras de inclusao e exclusao,
// com `*` e `%` como curinga, sem diferenciar maiusculas.
struct ObjectFilter {
    bool                     enabled = true;
    std::vector<std::string> include;
    std::vector<std::string> exclude;

    [[nodiscard]] bool empty() const noexcept {
        return include.empty() && exclude.empty();
    }
};

[[nodiscard]] bool mask_matches(std::string_view mask, std::string_view name);
[[nodiscard]] bool filter_accepts(const ObjectFilter& filter, std::string_view name);

// "a, b*; c" <-> lista. Separadores: virgula, ponto e virgula e quebra de linha.
[[nodiscard]] std::vector<std::string> split_masks(std::string_view text);
[[nodiscard]] std::string join_masks(const std::vector<std::string>& masks);

// --- Editor de texto ---------------------------------------------------------------

// "Join lines" (Ctrl+Shift+J): a linha seguinte sobe, com um espaco no lugar
// da quebra e do recuo.
[[nodiscard]] std::string join_lines(std::string_view first, std::string_view second);

// "Word completion" (Alt+/ do Eclipse): a proxima palavra do texto que comeca
// com `prefix`, depois de `after` (vazio = a primeira). Roda em ciclo.
[[nodiscard]] std::string complete_word(std::string_view text, std::string_view prefix,
                                        std::string_view after);

// --- Dashboard (ui.dashboard.*) ------------------------------------------------------
//
// Um grafico: uma consulta que devolve UMA linha, uma serie por coluna.
// `delta` = o servidor informa um contador acumulado, e o que se desenha e' a
// diferenca por segundo entre duas leituras.
struct DashboardChart {
    const char* id;
    const char* title;      // chave de TR()
    const char* sql;
    bool        delta;
    const char* unit;       // "", "/s", "bytes/s"
};

// Pelo driver do perfil: "postgresql", "mysql"/"mariadb", "sqlserver",
// "sqlanywhere".
//
// So' esta forma, de proposito: havia uma sobrecarga com `bool mysql`, e
// dashboard_catalog("sqlserver") escolhia ELA -- um literal converte para
// bool antes de converter para string_view -- devolvendo os graficos do MySQL.
[[nodiscard]] std::span<const DashboardChart> dashboard_catalog(std::string_view driver_id);

// Valor a desenhar a partir de duas leituras; para graficos `delta`, a taxa
// por segundo (nunca negativa: o contador zera quando o servidor reinicia).
[[nodiscard]] double dashboard_value(bool delta, double previous, double current,
                                     double seconds);

} // namespace otter::db
