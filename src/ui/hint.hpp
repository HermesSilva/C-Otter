// C-Otter -- ui/hint.hpp
//
// As dicas (tooltips) da interface: um cartao de cantos arredondados, borda na
// cor de destaque, faixa de titulo, e linhas "rotulo ... valor" alinhadas.
//
// Pedido do usuario (2026-09-30), com uma captura de referencia: "Altere os
// Hints para um modelo como este ou superior (...) tudo deve ser elegante,
// suave, simples e funcional". O tooltip padrao do ImGui e' uma caixa de texto
// corrido; aqui a dica tem estrutura, e aparece com um esmaecer curto em vez
// de saltar na tela.
//
// TODA dica passa por este arquivo -- nada de ImGui::SetTooltip pela
// interface. E' o que mantem o desenho unico.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace otter::ui {

// Dica estruturada. Monta-se e chama-se show() quando o item esta' sob o
// mouse:
//
//     if (ImGui::IsItemHovered()) {
//         Hint(table.name).row("Rows", "1.2M").row("Size", "24M")
//                         .text(table.comment).show();
//     }
//
// Linha, texto ou codigo vazios sao ignorados: quem chama nao precisa de um
// `if` para cada campo opcional.
class Hint {
public:
    explicit Hint(std::string_view title = {});

    // O atalho do comando, desenhado como tecla na faixa do titulo.
    Hint& shortcut(std::string_view keys);

    // "rotulo ... valor". O valor da linha `accent` sai na cor de destaque --
    // para o dado principal do cartao.
    Hint& row(std::string_view label, std::string_view value);
    Hint& accent(std::string_view label, std::string_view value);

    // Paragrafo corrido, com quebra de linha.
    Hint& text(std::string_view paragraph);

    // Trecho de SQL, em fonte de codigo. Muito longo, e' cortado.
    Hint& code(std::string_view source);

    void show() const;

private:
    struct Block {
        enum class Kind { row, text, code };
        Kind        kind = Kind::text;
        std::string label;
        std::string value;
        bool        accent = false;
    };

    std::string        title_;
    std::string        shortcut_;
    std::vector<Block> blocks_;
};

// Dica de texto simples. Reconhece a convencao dos botoes --
// "Rotulo (Ctrl+X)" na primeira linha, explicacao nas seguintes -- e a
// desenha com titulo e tecla.
void hint(std::string_view text);

// O mesmo, com formato printf. Substitui ImGui::SetTooltip.
void hint_fmt(const char* format, ...);

// As partes de um texto de dica, como hint() as entende. Sem estado nem
// desenho: serve ao teste.
struct HintParts {
    std::string title;      // vazio quando o texto nao segue a convencao
    std::string shortcut;
    std::string body;
};
[[nodiscard]] HintParts parse_hint_text(std::string_view text);

} // namespace otter::ui
