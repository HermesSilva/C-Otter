#include "ui/hint.hpp"

#include "ui/app_window.hpp"   // mono_font()
#include "ui/theme.hpp"

#include "imgui.h"
#include "imgui_internal.h"   // ShadeVertsLinearColorGradientKeepAlpha

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace otter::ui {
namespace {

// O atraso evita que a dica pisque enquanto o mouse so' atravessa a arvore; o
// esmaecer e' curto o bastante para nao parecer lentidao.
constexpr double kDelay = 0.22;
constexpr double kFade  = 0.14;
// Depois de uma dica visivel, a proxima aparece sem atraso -- como nos
// sistemas operacionais: quem esta' lendo dicas em sequencia nao espera cada
// uma de novo.
constexpr double kWarm  = 0.45;

// Um trecho de codigo maior que isto vira uma parede; o resto esta' a um
// clique, no proprio objeto.
constexpr int kMaxCodeLines = 14;

ImGuiID g_key          = 0;
double  g_start        = 0.0;
double  g_last_visible = -10.0;
int     g_last_frame   = -10;

// 0..1: quanto da dica ja' apareceu.
float appear(ImGuiID key) {
    const int    frame = ImGui::GetFrameCount();
    const double now   = ImGui::GetTime();

    if (key != g_key || frame > g_last_frame + 1) {
        const bool warm = now - g_last_visible < kWarm;
        g_key   = key;
        g_start = warm ? now - kDelay : now;
    }
    g_last_frame = frame;

    const double t = (now - g_start - kDelay) / kFade;
    if (t <= 0.0) return 0.0f;

    g_last_visible = now;
    const float x = static_cast<float>(std::min(t, 1.0));
    return x * x * (3.0f - 2.0f * x);   // smoothstep
}

float luminance(std::uint32_t color) noexcept {
    const float r = static_cast<float>(color & 0xFFu) / 255.0f;
    const float g = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
    const float b = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// A cor com o alfa do estilo aplicado: e' por onde o esmaecer alcanca o que
// e' desenhado direto na lista.
ImU32 ink(std::uint32_t color) {
    return ImGui::GetColorU32(static_cast<ImU32>(color));
}

float snap(float v) { return std::floor(v + 0.5f); }

// Negrito sem segunda fonte: o mesmo texto meio pixel ao lado.
void bold_text(ImDrawList* dl, ImVec2 at, ImU32 color, const std::string& text,
               float wrap = 0.0f) {
    ImFont*     font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const char* begin = text.c_str();
    const char* end   = begin + text.size();
    dl->AddText(font, size, at, color, begin, end, wrap);
    dl->AddText(font, size, ImVec2(at.x + 0.6f, at.y), color, begin, end, wrap);
}

bool is_key_name(std::string_view word) {
    static constexpr std::string_view kNames[] = {
        "Ctrl", "Alt", "Shift", "Cmd", "Win", "Tab", "Enter", "Esc", "Space",
        "Del", "Delete", "Ins", "Insert", "Home", "End", "PgUp", "PgDn",
        "Up", "Down", "Left", "Right", "Backspace",
    };
    if (std::any_of(std::begin(kNames), std::end(kNames),
                    [word](std::string_view name) { return name == word; })) {
        return true;
    }
    // F1..F24
    if (word.size() >= 2 && word.size() <= 3 && word.front() == 'F') {
        return std::all_of(word.begin() + 1, word.end(), [](char c) {
            return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    }
    // Uma tecla so': letra, digito ou sinal.
    return word.size() == 1;
}

// "Ctrl+Shift+Enter", "F4, F12", "Ctrl+\"?
bool is_shortcut(std::string_view text) {
    if (text.empty() || text.size() > 48) return false;

    std::size_t begin = 0;
    bool        any   = false;
    while (begin <= text.size()) {
        std::size_t end = text.find(", ", begin);
        if (end == std::string_view::npos) end = text.size();
        std::string_view chord = text.substr(begin, end - begin);
        if (chord.empty()) return false;

        // As teclas do acorde, separadas por '+'. O '+' final e' a propria
        // tecla ("Ctrl++").
        std::size_t at = 0;
        while (at < chord.size()) {
            std::size_t plus = chord.find('+', at + 1);
            if (plus == std::string_view::npos) plus = chord.size();
            if (!is_key_name(chord.substr(at, plus - at))) return false;
            at = plus + 1;
        }
        any   = true;
        begin = end + 2;
    }

    // Uma letra solta entre parenteses -- "(s)" de plural -- nao e' atalho.
    return any && text.size() > 1;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

std::vector<std::string> split_chords(const std::string& shortcut) {
    std::vector<std::string> chords;
    std::size_t begin = 0;
    while (begin < shortcut.size()) {
        std::size_t end = shortcut.find(", ", begin);
        if (end == std::string::npos) end = shortcut.size();
        chords.push_back(shortcut.substr(begin, end - begin));
        begin = end + 2;
    }
    return chords;
}

} // namespace

HintParts parse_hint_text(std::string_view text) {
    HintParts parts;
    text = trim(text);

    const std::size_t      newline = text.find('\n');
    const std::string_view first =
        trim(newline == std::string_view::npos ? text : text.substr(0, newline));

    // "Rotulo (Ctrl+X)": o atalho entre parenteses no FIM da primeira linha.
    if (!first.empty() && first.back() == ')') {
        const std::size_t open = first.rfind('(');
        if (open != std::string_view::npos && open > 0) {
            const std::string_view keys =
                first.substr(open + 1, first.size() - open - 2);
            if (is_shortcut(keys)) {
                parts.title    = std::string(trim(first.substr(0, open)));
                parts.shortcut = std::string(keys);
                if (newline != std::string_view::npos) {
                    parts.body = std::string(trim(text.substr(newline + 1)));
                }
                return parts;
            }
        }
    }

    parts.body = std::string(text);
    return parts;
}

Hint::Hint(std::string_view title) : title_(trim(title)) {}

Hint& Hint::shortcut(std::string_view keys) {
    shortcut_ = std::string(trim(keys));
    return *this;
}

Hint& Hint::row(std::string_view label, std::string_view value) {
    value = trim(value);
    if (!value.empty()) {
        blocks_.push_back({Block::Kind::row, std::string(label), std::string(value), false});
    }
    return *this;
}

Hint& Hint::accent(std::string_view label, std::string_view value) {
    value = trim(value);
    if (!value.empty()) {
        blocks_.push_back({Block::Kind::row, std::string(label), std::string(value), true});
    }
    return *this;
}

Hint& Hint::text(std::string_view paragraph) {
    paragraph = trim(paragraph);
    if (!paragraph.empty()) {
        blocks_.push_back({Block::Kind::text, {}, std::string(paragraph), false});
    }
    return *this;
}

Hint& Hint::code(std::string_view source) {
    source = trim(source);
    if (source.empty()) return *this;

    std::string kept;
    int         lines = 1;
    for (const char c : source) {
        if (c == '\r') continue;
        if (c == '\n' && ++lines > kMaxCodeLines) {
            kept += "\n...";
            break;
        }
        // A fonte de codigo nao tem largura de tabulacao propria.
        if (c == '\t') kept += "    ";
        else           kept += c;
    }
    blocks_.push_back({Block::Kind::code, {}, std::move(kept), false});
    return *this;
}

void Hint::show() const {
    if (title_.empty() && blocks_.empty()) return;

    const ImGuiID key = ImHashStr(
        title_.empty() ? blocks_.front().value.c_str() : title_.c_str());
    const float alpha = appear(key);
    if (alpha <= 0.0f) return;

    const Palette& p = colors();

    // A fonte da INTERFACE, no tamanho corrente: a dica pode nascer de dentro
    // do editor, onde a fonte empilhada e' a de codigo.
    ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0], 0.0f);
    ImFont* mono = mono_font();

    const float fs       = ImGui::GetFontSize();
    const float pad_x    = snap(fs * 0.80f);
    const float pad_y    = snap(fs * 0.55f);
    const float row_gap  = snap(fs * 0.38f);
    const float col_gap  = snap(fs * 1.60f);
    const float code_pad = snap(fs * 0.45f);
    const float chip_pad = snap(fs * 0.38f);
    const float rounding = snap(fs * 0.55f);
    const float wrap     = fs * 30.0f;

    const bool has_body = !blocks_.empty();
    const std::vector<std::string> chords = split_chords(shortcut_);

    // --- Medidas -------------------------------------------------------------
    float label_w = 0.0f;
    float value_w = 0.0f;
    std::vector<ImVec2> sizes(blocks_.size());

    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        const Block& block = blocks_[i];
        switch (block.kind) {
            case Block::Kind::row: {
                label_w = (std::max)(label_w, ImGui::CalcTextSize(block.label.c_str()).x);
                sizes[i] = ImGui::CalcTextSize(block.value.c_str(), nullptr, false,
                                               wrap * 0.72f);
                // O negrito falso avanca meio pixel.
                value_w = (std::max)(value_w, sizes[i].x + 1.0f);
                break;
            }
            case Block::Kind::text:
                sizes[i] = ImGui::CalcTextSize(block.value.c_str(), nullptr, false, wrap);
                break;
            case Block::Kind::code: {
                if (mono != nullptr) ImGui::PushFont(mono, 0.0f);
                sizes[i] = ImGui::CalcTextSize(block.value.c_str(), nullptr, false,
                                               wrap * 1.25f);
                if (mono != nullptr) ImGui::PopFont();
                sizes[i].x += code_pad * 2.0f;
                sizes[i].y += code_pad * 2.0f;
                break;
            }
        }
    }

    float chips_w = 0.0f;
    for (const std::string& chord : chords) {
        chips_w += ImGui::CalcTextSize(chord.c_str()).x + chip_pad * 2.0f + 4.0f;
    }
    const float title_w = title_.empty() ? 0.0f
                                         : ImGui::CalcTextSize(title_.c_str()).x + 1.0f;
    const float head_w =
        title_w + (chips_w > 0.0f ? chips_w + (title_w > 0.0f ? fs : 0.0f) : 0.0f);

    float content_w = head_w;
    if (label_w > 0.0f || value_w > 0.0f) {
        content_w = (std::max)(content_w, label_w + col_gap + value_w);
    }
    for (std::size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].kind != Block::Kind::row) {
            content_w = (std::max)(content_w, sizes[i].x);
        }
    }

    const bool  has_head = head_w > 0.0f;
    const float head_h   = has_head ? fs + snap(fs * 0.80f) : 0.0f;

    float body_h = 0.0f;
    if (has_body) {
        body_h = pad_y;
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            if (i > 0) {
                // Entre grupos diferentes (linhas x texto) vai um fio; o
                // espaco dobra para ele respirar.
                const bool group_change =
                    (blocks_[i].kind == Block::Kind::row) !=
                    (blocks_[i - 1].kind == Block::Kind::row);
                body_h += group_change ? row_gap * 2.0f + 1.0f : row_gap;
            }
            body_h += sizes[i].y;
        }
        body_h += pad_y;
    }

    const ImVec2 total(snap(content_w + pad_x * 2.0f), snap(head_h + body_h));

    // --- Janela --------------------------------------------------------------
    // O cartao e' desenhado a' mao: a janela da dica fica sem fundo nem borda,
    // e so' reserva o espaco.
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));

    if (ImGui::BeginTooltip()) {
        // Sobe quatro pixels enquanto aparece: o movimento curto e' o que faz
        // o esmaecer parecer suave em vez de so' lento.
        const ImVec2 base = ImGui::GetCursorScreenPos();
        const ImVec2 min(base.x, base.y + snap((1.0f - alpha) * 4.0f));
        const ImVec2 max(min.x + total.x, min.y + total.y);
        ImGui::Dummy(ImVec2(total.x, total.y + 4.0f));

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Sombra: aneis concentricos cada vez mais fracos, fora do cartao.
        // O recorte da janela e' aberto para ela caber.
        dl->PushClipRectFullScreen();
        const bool  dark   = current_theme().is_dark;
        const float shadow = dark ? 0.085f : 0.045f;
        for (int i = 7; i >= 1; --i) {
            const float grow = static_cast<float>(i) * 1.6f;
            dl->AddRectFilled(ImVec2(min.x - grow, min.y - grow + 3.0f),
                              ImVec2(max.x + grow, max.y + grow + 4.0f),
                              ink(with_alpha(0x000000u, shadow * (1.0f - static_cast<float>(i) / 9.0f))),
                              rounding + grow);
        }

        // Fundo opaco: a dica cobre texto, e texto atras de texto nao se le.
        const std::uint32_t card = with_alpha(mix(p.bg_darkest, p.bg_dark, 0.55f), 1.0f);
        dl->AddRectFilled(min, max, ink(card), rounding);

        // --- Faixa do titulo ---------------------------------------------------
        if (has_head && has_body) {
            const ImVec2 head_max(max.x, min.y + head_h);
            const int    first_vertex = dl->VtxBuffer.Size;
            dl->AddRectFilled(min, head_max, IM_COL32_WHITE, rounding,
                              ImDrawFlags_RoundCornersTop);
            ImGui::ShadeVertsLinearColorGradientKeepAlpha(
                dl, first_vertex, dl->VtxBuffer.Size, min, ImVec2(max.x, head_max.y),
                ink(with_alpha(lighten(p.accent, 0.06f), 1.0f)),
                ink(with_alpha(darken(p.accent, 0.16f), 1.0f)));

            const std::uint32_t on_accent =
                luminance(p.accent) > 0.62f ? 0xFF14100Cu : 0xFFFFFFFFu;
            const float text_y = min.y + snap((head_h - fs) * 0.5f);
            if (!title_.empty()) {
                bold_text(dl, ImVec2(min.x + pad_x, text_y), ink(on_accent), title_);
            }

            float chip_x = max.x - pad_x + 4.0f;
            for (std::size_t i = chords.size(); i-- > 0;) {
                const float w = ImGui::CalcTextSize(chords[i].c_str()).x + chip_pad * 2.0f;
                chip_x -= w + 4.0f;
                const ImVec2 chip_min(chip_x, text_y - 2.0f);
                const ImVec2 chip_max(chip_x + w, text_y + fs + 2.0f);
                dl->AddRectFilled(chip_min, chip_max,
                                  ink(with_alpha(0x000000u, 0.26f)), snap(fs * 0.28f));
                dl->AddText(ImVec2(chip_x + chip_pad, text_y), ink(on_accent),
                            chords[i].c_str());
            }
        } else if (has_head) {
            // So' o rotulo e a tecla: um cartao de uma linha, sem faixa.
            const float text_y = min.y + snap((head_h - fs) * 0.5f);
            if (!title_.empty()) {
                dl->AddText(ImVec2(min.x + pad_x, text_y), ink(p.text_bright),
                            title_.c_str());
            }
            float chip_x = max.x - pad_x + 4.0f;
            for (std::size_t i = chords.size(); i-- > 0;) {
                const float w = ImGui::CalcTextSize(chords[i].c_str()).x + chip_pad * 2.0f;
                chip_x -= w + 4.0f;
                const ImVec2 chip_min(chip_x, text_y - 2.0f);
                const ImVec2 chip_max(chip_x + w, text_y + fs + 2.0f);
                dl->AddRectFilled(chip_min, chip_max,
                                  ink(with_alpha(p.accent, 0.22f)), snap(fs * 0.28f));
                dl->AddRect(chip_min, chip_max, ink(with_alpha(p.accent, 0.55f)),
                            snap(fs * 0.28f));
                dl->AddText(ImVec2(chip_x + chip_pad, text_y), ink(p.accent_light),
                            chords[i].c_str());
            }
        }

        // --- Corpo -------------------------------------------------------------
        float y = min.y + head_h + pad_y;
        const float left  = min.x + pad_x;
        const float right = max.x - pad_x;

        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            const Block& block = blocks_[i];

            if (i > 0) {
                const bool group_change =
                    (block.kind == Block::Kind::row) !=
                    (blocks_[i - 1].kind == Block::Kind::row);
                if (group_change) {
                    y += row_gap;
                    dl->AddLine(ImVec2(left, y), ImVec2(right, y),
                                ink(with_alpha(p.text_dim, 0.22f)));
                    y += row_gap + 1.0f;
                } else {
                    y += row_gap;
                }
            }

            switch (block.kind) {
                case Block::Kind::row: {
                    dl->AddText(ImVec2(left, y), ink(p.text_dim), block.label.c_str());

                    const bool single_line = sizes[i].y <= fs * 1.5f;
                    // Uma linha: encostada a' direita, como numa ficha.
                    // Varias: comecam na coluna do valor, senao o texto
                    // quebrado fica com a margem esquerda em serrilha.
                    const float x = single_line
                                        ? right - sizes[i].x - 1.0f
                                        : right - value_w;
                    bold_text(dl, ImVec2(snap(x), y),
                              ink(block.accent ? p.accent_light : p.text_bright),
                              block.value, wrap * 0.72f);
                    break;
                }
                case Block::Kind::text:
                    dl->AddText(ImGui::GetFont(), fs, ImVec2(left, y), ink(p.text),
                                block.value.c_str(),
                                block.value.c_str() + block.value.size(), wrap);
                    break;
                case Block::Kind::code: {
                    dl->AddRectFilled(ImVec2(left, y),
                                      ImVec2(right, y + sizes[i].y),
                                      ink(with_alpha(p.bg_darkest, 0.85f)),
                                      snap(fs * 0.35f));
                    ImFont* font = mono != nullptr ? mono : ImGui::GetFont();
                    dl->AddText(font, fs, ImVec2(left + code_pad, y + code_pad),
                                ink(p.data_light), block.value.c_str(),
                                block.value.c_str() + block.value.size(),
                                wrap * 1.25f);
                    break;
                }
            }
            y += sizes[i].y;
        }

        // A borda por ultimo, por cima da faixa: um contorno so', continuo.
        dl->AddRect(min, max, ink(with_alpha(p.accent, 0.90f)), rounding, 0, 1.2f);
        dl->PopClipRect();

        ImGui::EndTooltip();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopFont();
}

void hint(std::string_view text) {
    const HintParts parts = parse_hint_text(text);
    Hint card(parts.title);
    card.shortcut(parts.shortcut);

    // Paragrafos separados por linha em branco viram blocos proprios: o
    // espaco entre eles passa a ser o do cartao, nao o de uma linha vazia.
    std::string_view body = parts.body;
    while (!body.empty()) {
        const std::size_t      gap = body.find("\n\n");
        const std::string_view paragraph =
            gap == std::string_view::npos ? body : body.substr(0, gap);
        card.text(paragraph);
        if (gap == std::string_view::npos) break;
        body.remove_prefix(gap + 2);
    }
    card.show();
}

void hint_fmt(const char* format, ...) {
    char    buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    hint(buffer);
}

} // namespace otter::ui
