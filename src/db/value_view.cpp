#include "db/value_view.hpp"

#include <algorithm>
#include <cctype>

namespace otter::db {
namespace {

constexpr char kHex[] = "0123456789abcdef";

std::string_view trimmed(std::string_view text) {
    const auto is_space = [](char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    };
    while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))   text.remove_suffix(1);
    return text;
}

// Parece JSON? Começa com '{' ou '[' e termina com o fecho correspondente.
//
// Verificação barata de propósito: rodar um parser completo por célula
// visível, a cada quadro, custaria caro para decidir só qual botão mostrar.
// Um falso positivo apenas oferece o visualizador de JSON num texto que não
// é JSON -- e ele mostra o texto como veio.
bool looks_json(std::string_view text) {
    text = trimmed(text);
    if (text.size() < 2) return false;

    return (text.front() == '{' && text.back() == '}') ||
           (text.front() == '[' && text.back() == ']');
}

} // namespace

std::string_view to_string(ValueView view) noexcept {
    switch (view) {
        case ValueView::plain:     return "text";
        case ValueView::multiline: return "multiline text";
        case ValueView::json:      return "JSON";
        case ValueView::binary:    return "binary";
        case ValueView::boolean:   return "boolean";
    }
    return "text";
}

bool is_true(std::string_view value) noexcept {
    // O MySQL não tem booleano: BOOLEAN é apelido de TINYINT(1), e o valor
    // chega como "0" ou "1". O PostgreSQL manda "t"/"f". Tratar só um dos
    // dois deixaria metade das colunas booleanas sem editor.
    return value == "1" || value == "t" || value == "true" ||
           value == "TRUE" || value == "True" || value == "y" ||
           value == "yes" || value == "Y" || value == "YES";
}

bool looks_boolean(std::string_view value) noexcept {
    if (is_true(value)) return true;

    return value == "0" || value == "f" || value == "false" ||
           value == "FALSE" || value == "False" || value == "n" ||
           value == "no" || value == "N" || value == "NO";
}

ValueView choose_view(DataKind kind, std::string_view sample) {
    switch (kind) {
        case DataKind::json:    return ValueView::json;
        case DataKind::binary:  return ValueView::binary;
        case DataKind::boolean: return ValueView::boolean;

        case DataKind::integer:
            // Correção pelo conteúdo: no MySQL um BOOLEAN chega como
            // TINYINT(1), e o servidor não diz que é booleano. Um inteiro
            // que só vale 0 ou 1 PODE ser booleano -- oferecer o editor não
            // custa, e não oferecê-lo deixaria toda coluna booleana do MySQL
            // sem ele.
            return looks_boolean(sample) ? ValueView::boolean
                                         : ValueView::plain;

        default:
            break;
    }

    // Texto: JSON gravado numa coluna `text` é comum, e o servidor não avisa.
    if (looks_json(sample)) return ValueView::json;

    return sample.find('\n') != std::string_view::npos ? ValueView::multiline
                                                       : ValueView::plain;
}

std::string format_json(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);

    int  depth = 0;
    bool in_string = false;
    bool escaped = false;

    const auto newline = [&out](int level) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };

    for (const char c : text) {
        // Dentro de string, NADA é estrutura: uma chave `{` num valor de
        // texto não abre nível, e uma vírgula não quebra linha. Ignorar isso
        // é o defeito clássico de um formatador ingênuo.
        if (in_string) {
            out.push_back(c);

            if (escaped)        escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"')  in_string = false;
            continue;
        }

        switch (c) {
            case '"':
                in_string = true;
                out.push_back(c);
                break;

            case '{':
            case '[':
                out.push_back(c);
                ++depth;
                newline(depth);
                break;

            case '}':
            case ']':
                --depth;
                // Nível negativo significa fecho a mais: JSON malformado.
                // Continuar em zero mantém a saída legível em vez de recuar
                // para a esquerda com indentação negativa.
                if (depth < 0) depth = 0;
                newline(depth);
                out.push_back(c);
                break;

            case ',':
                out.push_back(c);
                newline(depth);
                break;

            case ':':
                out.push_back(c);
                out.push_back(' ');
                break;

            case ' ':
            case '\n':
            case '\r':
            case '\t':
                break;   // o espaço original é descartado; nós indentamos

            default:
                out.push_back(c);
                break;
        }
    }

    // String não fechada: o texto é inválido, e devolvê-lo COMO VEIO é mais
    // honesto que entregar uma indentação inventada sobre algo quebrado.
    if (in_string) return std::string(text);

    return out;
}

std::string format_hex(std::span<const std::byte> data, std::size_t max_bytes) {
    const std::size_t shown = std::min(data.size(), max_bytes);

    std::string out;
    out.reserve(shown * 4 + shown / 16 * 16);

    for (std::size_t offset = 0; offset < shown; offset += 16) {
        // Deslocamento em hexadecimal, 8 dígitos: é o que permite localizar
        // um byte num arquivo grande sem contar linhas.
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHex[(offset >> shift) & 0xF]);
        }
        out.append("  ");

        const std::size_t end = std::min(offset + 16, shown);

        for (std::size_t i = offset; i < offset + 16; ++i) {
            if (i < end) {
                const auto byte = static_cast<unsigned char>(data[i]);
                out.push_back(kHex[byte >> 4]);
                out.push_back(kHex[byte & 0xF]);
            } else {
                out.append("  ");   // alinha a coluna ASCII na última linha
            }
            out.push_back(' ');

            // Espaço extra no meio: separa os dois blocos de 8, como todo
            // visualizador hexadecimal faz. Sem isso, contar a posição de um
            // byte dentro da linha exige o dedo na tela.
            if (i == offset + 7) out.push_back(' ');
        }

        out.append(" |");
        for (std::size_t i = offset; i < end; ++i) {
            const auto byte = static_cast<unsigned char>(data[i]);

            // Só o ASCII imprimível. Um byte de controle desenhado como
            // caractere quebraria o alinhamento da coluna -- e 0x00
            // truncaria a linha em quem lê como C-string.
            out.push_back(byte >= 0x20 && byte < 0x7F
                              ? static_cast<char>(byte)
                              : '.');
        }
        out.append("|\n");
    }

    if (data.size() > shown) {
        out += "... " + std::to_string(data.size() - shown) +
               " more bytes not shown\n";
    }
    return out;
}

} // namespace otter::db
