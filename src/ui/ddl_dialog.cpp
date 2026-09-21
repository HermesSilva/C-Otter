#include "ui/ddl_dialog.hpp"

#include "base/i18n.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace otter::ui {
namespace {

ImVec4 col4(std::uint32_t c) {
    return ImVec4(static_cast<float>((c >> 0) & 0xFF) / 255.0f,
                  static_cast<float>((c >> 8) & 0xFF) / 255.0f,
                  static_cast<float>((c >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((c >> 24) & 0xFF) / 255.0f);
}

// O script inteiro como texto, para a area de edicao e para o clipboard.
std::string as_text(const db::AlterScript& script) {
    std::string out;
    for (const std::string& statement : script.statements) {
        if (statement.empty()) continue;   // back() num vazio seria UB

        out += statement;
        if (statement.back() != ';') out += ';';
        out += '\n';
    }
    return out;
}

// Divide o texto editado de volta em comandos, por ';' no fim da linha.
//
// Um divisor simples basta AQUI porque o conteudo e' DDL: nao ha' corpo de
// funcao com ';' dentro, que e' o caso que quebraria (e que o sql::split_script
// resolve, ao custo de um lexer inteiro).
std::vector<std::string> split_statements(const std::string& text) {
    std::vector<std::string> out;
    std::string current;

    for (const char c : text) {
        if (c == ';') {
            // Sem o ';': cada comando vai isolado para o servidor.
            bool empty = true;
            for (const char k : current) {
                if (k != ' ' && k != '\n' && k != '\t' && k != '\r') {
                    empty = false;
                    break;
                }
            }
            if (!empty) out.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    bool empty = true;
    for (const char k : current) {
        if (k != ' ' && k != '\n' && k != '\t' && k != '\r') { empty = false; break; }
    }
    if (!empty) out.push_back(current);
    return out;
}

} // namespace

void DdlDialog::open(std::string title, db::AlterScript script,
                     Confirm on_confirm) {
    title_        = std::move(title);
    script_       = std::move(script);
    on_confirm_   = std::move(on_confirm);
    visible_      = true;

    // Confirmacao SEMPRE recomeca desmarcada. Herdar o "sim" da janela
    // anterior e' como um DROP acidental acontece.
    acknowledged_ = false;
    editing_      = false;
    edited_       = as_text(script_);
}

bool DdlDialog::draw(bool can_execute, bool ddl_in_transaction) {
    if (!visible_) return false;

    bool executed = false;
    const Palette& p = colors();

    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);

    if (ImGui::Begin(TRW("Review SQL", "###DdlDialog"), &visible_,
                     ImGuiWindowFlags_NoDocking)) {

        ImGui::TextColored(col4(p.accent_light), "%s", title_.c_str());
        ImGui::Separator();

        // --- Erro de geracao --------------------------------------------------

        if (!script_.error.empty()) {
            ImGui::TextColored(col4(p.error), "%s", TR(script_.error.c_str()));
            ImGui::Spacing();
            if (ImGui::Button(TR("Close"), ImVec2(120, 0))) visible_ = false;
            ImGui::End();
            return false;
        }

        // --- Avisos ------------------------------------------------------------

        for (const std::string& warning : script_.warnings) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(col4(p.warn), "! %s", TR(warning.c_str()));
            ImGui::PopTextWrapPos();
        }

        // DDL nao transacional: a sequencia NAO e' atomica, e falhar no
        // terceiro comando deixa os dois primeiros aplicados. Dizer isso e'
        // o que permite ao usuario decidir se quer correr o risco.
        if (!ddl_in_transaction && script_.statements.size() > 1) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(
                col4(p.warn), "! %s",
                TR("this database commits each DDL statement on its own: if one "
                   "fails, the previous ones stay applied"));
            ImGui::PopTextWrapPos();
        }

        if (!script_.warnings.empty() ||
            (!ddl_in_transaction && script_.statements.size() > 1)) {
            ImGui::Spacing();
        }

        // --- O SQL --------------------------------------------------------------

        const float footer = ImGui::GetFrameHeightWithSpacing() * 3.2f;
        ImGui::BeginChild("##sql", ImVec2(0, -footer), ImGuiChildFlags_Borders);

        if (editing_) {
            // Buffer separado, como no resto do projeto: escrever direto no
            // std::string do ImGui exigiria callback de redimensionamento, e
            // um DDL nao passa de alguns KB.
            std::vector<char> buffer(
                std::max<std::size_t>(8192, edited_.size() + 1024), '\0');
            std::snprintf(buffer.data(), buffer.size(), "%s", edited_.c_str());

            if (ImGui::InputTextMultiline("##edit", buffer.data(), buffer.size(),
                                          ImVec2(-1, -1))) {
                edited_ = buffer.data();
            }
        } else {
            for (std::size_t i = 0; i < script_.statements.size(); ++i) {
                const bool destructive =
                    std::find(script_.destructive.begin(),
                              script_.destructive.end(),
                              i) != script_.destructive.end();

                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(col4(destructive ? p.error : p.data), "%s;",
                                   script_.statements[i].c_str());
                ImGui::PopTextWrapPos();

                if (destructive) {
                    ImGui::SameLine();
                    ImGui::TextColored(col4(p.error), " %s",
                                       TR("(cannot be undone)"));
                }
            }
        }
        ImGui::EndChild();

        // --- Rodape --------------------------------------------------------------

        if (script_.has_destructive()) {
            ImGui::TextColored(col4(p.error), "%s",
                               TR("This drops data or structure permanently."));
            ImGui::Checkbox(TR("I understand"), &acknowledged_);
        }

        const bool allowed =
            can_execute && (!script_.has_destructive() || acknowledged_);

        ImGui::BeginDisabled(!allowed);
        if (ImGui::Button(TR("Execute"), ImVec2(140, 0))) {
            const std::vector<std::string> statements =
                editing_ ? split_statements(edited_) : script_.statements;

            if (on_confirm_ && !statements.empty()) {
                on_confirm_(statements);
                executed = true;
                visible_ = false;
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(TR("Copy"), ImVec2(120, 0))) {
            ImGui::SetClipboardText(
                (editing_ ? edited_ : as_text(script_)).c_str());
        }

        ImGui::SameLine();
        if (ImGui::Button(editing_ ? TR("Stop editing") : TR("Edit"),
                          ImVec2(140, 0))) {
            // Sair da edicao DESCARTA o texto editado e volta ao gerado: o
            // contrario -- guardar edicoes invisiveis e executa-las depois --
            // seria surpresa desagradavel.
            if (editing_) edited_ = as_text(script_);
            editing_ = !editing_;
        }

        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"), ImVec2(120, 0))) visible_ = false;

        if (!can_execute) {
            ImGui::SameLine();
            ImGui::TextColored(col4(p.text_dim), "%s",
                               TR("(not connected, or busy)"));
        }
    }
    ImGui::End();
    return executed;
}

} // namespace otter::ui
