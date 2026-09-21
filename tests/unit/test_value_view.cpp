// Apresentação de valor por tipo, para o painel da grade.
//
// Duas armadilhas de resultado silenciosamente errado:
//
//   1. Um formatador de JSON ingênuo trata `{` DENTRO de uma string como
//      abertura de nível. O resultado sai com indentação crescente e nunca
//      volta -- e o valor exibido deixa de corresponder ao gravado.
//   2. O MySQL não tem booleano: BOOLEAN é apelido de TINYINT(1). Tratar só
//      "t"/"f" do PostgreSQL deixaria toda coluna booleana do MySQL sem
//      editor.
#include "test_main.hpp"

#include "db/value_view.hpp"

#include <string>
#include <vector>

using namespace otter::db;

namespace {

bool has(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::span<const std::byte> bytes_of(const std::vector<unsigned char>& v) {
    return {reinterpret_cast<const std::byte*>(v.data()), v.size()};
}

} // namespace

// --- Escolha da apresentação ------------------------------------------------------

OTTER_TEST(value_view_follows_the_declared_type) {
    OTTER_CHECK(choose_view(DataKind::json, "{}") == ValueView::json);
    OTTER_CHECK(choose_view(DataKind::binary, "") == ValueView::binary);
    OTTER_CHECK(choose_view(DataKind::boolean, "t") == ValueView::boolean);
    OTTER_CHECK(choose_view(DataKind::string, "oi") == ValueView::plain);
}

OTTER_TEST(value_view_detects_json_inside_a_text_column) {
    // JSON gravado numa coluna `text` é comum, e o servidor não avisa. Sem a
    // correção pelo conteúdo, o usuário veria uma linha só, ilegível.
    OTTER_CHECK(choose_view(DataKind::string, R"({"a":1})") == ValueView::json);
    OTTER_CHECK(choose_view(DataKind::string, "[1,2,3]") == ValueView::json);

    // Com espaços em volta continua valendo.
    OTTER_CHECK(choose_view(DataKind::string, "  {\"a\":1}  ") ==
                ValueView::json);

    // Mas um texto que só COMEÇA com chave não é JSON.
    OTTER_CHECK(choose_view(DataKind::string, "{isto nao fecha") !=
                ValueView::json);
}

OTTER_TEST(value_view_detects_mysql_boolean_as_tinyint) {
    // O MySQL manda BOOLEAN como TINYINT(1): o tipo declarado é INTEIRO, e o
    // servidor não diz que é booleano. Sem a correção pelo conteúdo, toda
    // coluna booleana do MySQL ficaria sem editor.
    OTTER_CHECK(choose_view(DataKind::integer, "1") == ValueView::boolean);
    OTTER_CHECK(choose_view(DataKind::integer, "0") == ValueView::boolean);

    // Um inteiro comum continua texto: 42 não é booleano.
    OTTER_CHECK(choose_view(DataKind::integer, "42") == ValueView::plain);
    OTTER_CHECK(choose_view(DataKind::integer, "-1") == ValueView::plain);
}

OTTER_TEST(value_view_marks_multiline_text) {
    OTTER_CHECK(choose_view(DataKind::string, "uma\nduas") ==
                ValueView::multiline);
    OTTER_CHECK(choose_view(DataKind::string, "uma linha") == ValueView::plain);
}

// --- Booleano ----------------------------------------------------------------------

OTTER_TEST(boolean_covers_both_dialects) {
    // PostgreSQL manda "t"/"f"; MySQL manda "1"/"0".
    OTTER_CHECK(is_true("t"));
    OTTER_CHECK(is_true("1"));
    OTTER_CHECK(is_true("true"));
    OTTER_CHECK(is_true("TRUE"));

    OTTER_CHECK(!is_true("f"));
    OTTER_CHECK(!is_true("0"));
    OTTER_CHECK(!is_true("false"));

    // Um valor que não é booleano não é verdadeiro nem reconhecido.
    OTTER_CHECK(!is_true("talvez"));
    OTTER_CHECK(!looks_boolean("talvez"));
    OTTER_CHECK(!looks_boolean("2"));
}

// --- JSON --------------------------------------------------------------------------

OTTER_TEST(json_indents_by_nesting) {
    const std::string out = format_json(R"({"a":1,"b":{"c":2}})");

    OTTER_CHECK(has(out, "\n  \"a\""));      // primeiro nível
    OTTER_CHECK(has(out, "\n    \"c\""));    // segundo
    OTTER_CHECK(has(out, ": "));             // espaço depois dos dois-pontos
}

OTTER_TEST(json_does_not_treat_braces_inside_a_string_as_structure) {
    // A armadilha clássica: `{` dentro de uma string abre um nível que nunca
    // fecha, e a indentação cresce até o fim do documento.
    const std::string out = format_json(R"({"texto":"tem { e } dentro","n":1})");

    // A chave "n" fica no PRIMEIRO nível, não no terceiro.
    OTTER_CHECK(has(out, "\n  \"n\""));
    OTTER_CHECK(!has(out, "\n      \"n\""));

    // E o conteúdo da string sai intacto.
    OTTER_CHECK(has(out, "tem { e } dentro"));
}

OTTER_TEST(json_keeps_escaped_quotes_inside_a_string) {
    // Uma aspa ESCAPADA não fecha a string. Sem tratar o escape, tudo depois
    // dela seria lido como estrutura.
    const std::string out = format_json(R"({"a":"diz \"oi\"","b":2})");

    OTTER_CHECK(has(out, R"(\"oi\")"));
    OTTER_CHECK(has(out, "\n  \"b\""));
}

OTTER_TEST(json_returns_malformed_input_unchanged) {
    // O valor pode ter sido gravado por outro sistema. Recusar-se a mostrá-lo
    // seria pior que mostrá-lo feio -- e travar seria muito pior.
    const std::string unterminated = R"({"a":"sem fechar)";
    OTTER_CHECK_EQ(format_json(unterminated), unterminated);

    // Fecho a mais não gera indentação negativa nem trava.
    const std::string extra = format_json("{\"a\":1}}}");
    OTTER_CHECK(!extra.empty());
}

OTTER_TEST(json_handles_an_empty_document) {
    OTTER_CHECK(format_json("").empty());
    OTTER_CHECK(!format_json("{}").empty());
}

// --- Hexadecimal --------------------------------------------------------------------

OTTER_TEST(hex_shows_offset_bytes_and_ascii) {
    const std::vector<unsigned char> data = {'O', 't', 't', 'e', 'r', 0x00, 0xFF};

    const std::string out = format_hex(bytes_of(data));

    OTTER_CHECK(has(out, "00000000"));        // deslocamento
    OTTER_CHECK(has(out, "4f 74 74 65 72"));  // os bytes
    OTTER_CHECK(has(out, "|Otter..|"));       // ASCII, com '.' no não imprimível
}

OTTER_TEST(hex_does_not_render_control_bytes_as_characters) {
    // Um byte de controle desenhado como caractere quebraria o alinhamento --
    // e 0x00 truncaria a linha em quem lê como C-string.
    const std::vector<unsigned char> data = {0x00, 0x01, 0x1F, 0x7F, 0x80};

    const std::string out = format_hex(bytes_of(data));
    OTTER_CHECK(has(out, "|.....|"));
}

OTTER_TEST(hex_aligns_the_ascii_column_on_a_short_last_line) {
    // A última linha quase nunca tem 16 bytes. Sem preencher o espaço dos
    // que faltam, a coluna ASCII sobe e a leitura fica torta.
    const std::vector<unsigned char> data(20, 'A');

    const std::string out = format_hex(bytes_of(data));

    // Duas linhas: a primeira com 16, a segunda com 4.
    std::size_t lines = 0;
    for (const char c : out) {
        if (c == '\n') ++lines;
    }
    OTTER_CHECK_EQ(lines, std::size_t{2});

    // As duas barras da coluna ASCII ficam na MESMA posição em cada linha.
    const std::size_t first  = out.find('|');
    const std::size_t second = out.find('|', out.find('\n'));
    const std::size_t line_start = out.find('\n') + 1;

    OTTER_CHECK_EQ(first, second - line_start);
}

OTTER_TEST(hex_truncates_and_says_how_much_was_left_out) {
    // Um BLOB de 200 MB formatado inteiro consumiria memória sem que ninguém
    // fosse ler além das primeiras linhas. Truncar em silêncio faria o
    // usuário concluir que o valor é menor do que é.
    const std::vector<unsigned char> data(100, 'x');

    const std::string out = format_hex(bytes_of(data), /*max_bytes=*/32);

    OTTER_CHECK(has(out, "68 more bytes"));   // 100 - 32
    OTTER_CHECK(out.size() < 500);
}

OTTER_TEST(hex_handles_empty_data) {
    OTTER_CHECK(format_hex({}).empty());
}
