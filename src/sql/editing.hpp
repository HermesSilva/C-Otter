// C-Otter -- sql/editing.hpp
//
// O que os comandos do editor SQL precisam saber sobre o TEXTO, sem tela:
// qual instrucao esta' sob o cursor, onde comeca a proxima, qual e' o
// colchete que fecha este, como virar uma selecao em lista delimitada.
//
// Fica em otter_sql, e nao na UI, pelo mesmo motivo do lexer (ADR 0007): sao
// regras que precisam de teste, e a UI nao e' testavel em unidade. A UI so'
// converte cursor em offset e aplica o resultado.
#pragma once

#include "sql/dialect.hpp"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace otter::sql {

// --- Posicao ---------------------------------------------------------------------

// Linha e coluna, base zero. A coluna conta CARACTERES (pontos de codigo
// UTF-8), que e' como o editor enderea o cursor -- contar bytes poria o
// cursor no meio de um "ç".
struct TextPosition {
    std::size_t line = 0;
    std::size_t column = 0;
};

[[nodiscard]] std::size_t offset_of(std::string_view text, TextPosition position);
[[nodiscard]] TextPosition position_of(std::string_view text, std::size_t offset);

// --- Instrucoes ------------------------------------------------------------------

// Uma instrucao do script, em offsets de byte: [begin, end).
struct StatementRange {
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] bool empty() const noexcept { return end <= begin; }
};

// As instrucoes NAO vazias do script, na ordem.
[[nodiscard]] std::vector<StatementRange> statement_ranges(
    std::string_view script, const Dialect& dialect);

// A instrucao sob o cursor -- o que Ctrl+Enter executa no DBeaver.
//
// Cursor entre duas instrucoes (na linha em branco, ou logo depois do ';')
// fica com a ANTERIOR: e' onde ele para ao terminar de digitar uma consulta,
// e executar a de baixo seria rodar o que o usuario nao esta' olhando.
// Nenhuma antes dele: a primeira depois.
[[nodiscard]] std::optional<StatementRange> statement_range_at(
    std::string_view script, const Dialect& dialect, std::size_t offset);

// Inicio da instrucao seguinte / anterior a' do cursor (Alt+Down / Alt+Up).
// Sem seguinte ou anterior, devolve vazio -- o cursor fica onde esta'.
[[nodiscard]] std::optional<std::size_t> next_statement_start(
    std::string_view script, const Dialect& dialect, std::size_t offset);
[[nodiscard]] std::optional<std::size_t> previous_statement_start(
    std::string_view script, const Dialect& dialect, std::size_t offset);

// --- Colchetes -------------------------------------------------------------------

// O par do colchete que esta' no cursor (em `offset` ou logo antes dele).
// Parenteses, colchetes e chaves; os que estao dentro de string ou
// comentario nao contam -- um ')' num literal nao fecha nada.
[[nodiscard]] std::optional<std::size_t> matching_bracket(
    std::string_view script, const Dialect& dialect, std::size_t offset);

// --- Transformacoes de selecao -----------------------------------------------------

// Envolve em /* */, ou desfaz se ja' estiver envolvido (Ctrl+Shift+/).
[[nodiscard]] std::string toggle_block_comment(std::string_view text);

// Remove espacos e tabulacoes no inicio e/ou no fim de CADA linha.
[[nodiscard]] std::string trim_lines(std::string_view text, bool leading,
                                     bool trailing);

// "Morph to delimited list": uma coluna colada de uma planilha vira
// `'a','b','c'`, pronta para um IN (...).
struct MorphOptions {
    std::string source_delimiters = "\t\n,";   // qualquer um destes separa
    std::string target_delimiter  = ",";
    std::string quote             = "\"";
    std::size_t wrap_line         = 80;        // 0 = nao quebra
    std::string leading_text;
    std::string trailing_text;
};
[[nodiscard]] std::string morph_delimited_list(std::string_view text,
                                               const MorphOptions& options);

// --- Variaveis (@set) --------------------------------------------------------------

// As variaveis do script: `@set nome = valor` define, `${nome}` usa.
using Variables = std::map<std::string, std::string>;

// Um comando de controle do cliente, que NAO vai ao servidor.
struct ControlCommand {
    enum class Kind { none, set, unset, echo };
    Kind        kind = Kind::none;
    std::string name;    // set / unset
    std::string value;   // set: o valor; echo: o texto
};

// A instrucao e' um comando de controle (`@set x = 1`, `@unset x`, `@echo oi`)?
[[nodiscard]] ControlCommand parse_control_command(std::string_view statement);

// Troca `${nome}` pelo valor. Variavel desconhecida fica como esta': o
// servidor aponta o erro no lugar, em vez de a consulta rodar com um buraco.
[[nodiscard]] std::string expand_variables(std::string_view sql,
                                           const Variables& variables);

// --- Templates -------------------------------------------------------------------

struct Template {
    std::string_view name;
    std::string_view description;
    std::string_view pattern;     // com ${variavel}, como no DBeaver
};

// Os cinco de templates/default-templates.xml do DBeaver.
[[nodiscard]] const std::vector<Template>& default_templates();

// O texto a inserir e onde deixar o cursor: no lugar da PRIMEIRA variavel,
// que e' o que o usuario preenche em seguida.
struct ExpandedTemplate {
    std::string text;
    std::size_t cursor = 0;       // offset dentro de `text`
    std::size_t select = 0;       // quantos bytes selecionar a partir dele
};
[[nodiscard]] ExpandedTemplate expand_template(const Template& entry);

// --- Estrutura (outline) -----------------------------------------------------------

// Uma linha do painel de estrutura: a instrucao resumida e onde ela comeca.
struct OutlineEntry {
    std::string label;     // "SELECT cliente", "UPDATE pedido"
    std::size_t offset = 0;
    std::size_t line = 0;
};
[[nodiscard]] std::vector<OutlineEntry> outline(std::string_view script,
                                                const Dialect& dialect);

} // namespace otter::sql
