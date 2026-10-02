#include "ui/sql_document.hpp"

#include "base/i18n.hpp"

#include <filesystem>

namespace otter::ui {

SqlDocument::SqlDocument(std::size_t id)
    : id_(id), editor_(std::make_unique<TextEditor>()) {
    editor_->SetLanguage(TextEditor::Language::Sql());
    editor_->SetShowWhitespacesEnabled(false);
    editor_->SetShowMatchingBrackets(true);
    editor_->SetCompletePairedGlyphs(true);
    editor_->SetTabSize(4);

    // Sempre ha' uma aba de resultado: os metodos de resultado falam com a
    // ativa, e nao podem encontrar o vetor vazio.
    add_result_tab();
}

// --- Abas de resultado ------------------------------------------------------------

std::size_t SqlDocument::add_result_tab() {
    auto tab = std::make_unique<ResultTab>();
    tab->id = next_tab_id_++;
    tabs_.push_back(std::move(tab));
    active_tab_ = tabs_.size() - 1;
    return tabs_.back()->id;
}

bool SqlDocument::select_result_tab_by_id(std::size_t id) {
    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i]->id == id) {
            active_tab_ = i;
            return true;
        }
    }
    return false;
}

void SqlDocument::close_result_tab(std::size_t index) {
    if (index >= tabs_.size()) return;

    if (tabs_.size() == 1) {
        // A ultima e' esvaziada em vez de fechada. O id novo impede que um
        // resultado ainda a caminho caia na aba que o usuario acabou de
        // limpar.
        const std::size_t id = next_tab_id_++;
        tabs_.front() = std::make_unique<ResultTab>();
        tabs_.front()->id = id;
        active_tab_ = 0;
        return;
    }

    tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(index));
    if (active_tab_ > index || active_tab_ >= tabs_.size()) {
        active_tab_ = active_tab_ > 0 ? active_tab_ - 1 : 0;
    }
}

void SqlDocument::set_result_tab_pinned(std::size_t index, bool pinned) {
    if (index < tabs_.size()) tabs_[index]->pinned = pinned;
}

void SqlDocument::set_result_tab_title(std::size_t index, std::string title) {
    if (index < tabs_.size()) tabs_[index]->title = std::move(title);
}

std::string SqlDocument::title() const {
    if (!title_.empty()) return title_;

    if (!file_path_.empty()) {
        // Sem ".sql": a aba mostra o nome do script, como no DBeaver. Outra
        // extensao (um .txt aberto de fora) fica, para dizer o que e'.
        const std::filesystem::path path(file_path_);
        std::string extension = path.extension().string();
        for (char& c : extension) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return extension == ".sql" ? path.stem().string() : path.filename().string();
    }
    if (!default_title_.empty()) return default_title_;
    return std::string(TR("Script")) + " " + std::to_string(id_);
}

void SqlDocument::set_file_path(std::string path) {
    file_path_ = std::move(path);
    // O titulo passa a vir do arquivo; um nome dado antes seria confuso.
    title_.clear();
}

bool SqlDocument::modified() const {
    // Num editor de objeto o texto e' o DDL lido do servidor: "modificado" e'
    // ter propriedade por gravar, ou o fonte diferente do que foi lido.
    if (object_.has_value()) {
        return !object_->edits.empty() ||
               (object_->source_loaded && editor_->GetText() != object_->source_original);
    }
    return editor_->GetUndoIndex() != save_point_;
}

void SqlDocument::mark_saved() {
    save_point_ = editor_->GetUndoIndex();
}

std::size_t SqlDocument::cursor_offset() const {
    const TextEditor::DocPos cursor = editor_->GetCurrentCursorPosition();
    return sql::offset_of(editor_->GetText(), {cursor.line, cursor.index});
}

std::string SqlDocument::sql_to_execute(const sql::Dialect& dialect) const {
    // Com seleção, executa só ela -- comportamento esperado de cliente SQL.
    if (editor_->CurrentCursorHasSelection()) {
        return editor_->GetSectionText(editor_->GetCurrentCursorSelection());
    }

    // Sem seleção, a INSTRUÇÃO sob o cursor -- o que Ctrl+Enter faz no
    // DBeaver. Devolvia o texto inteiro: num script de dez consultas, a
    // tecla de "executar esta" mandava as dez de uma vez como uma só, e o
    // servidor respondia com o resultado da última (ou com erro de sintaxe).
    const std::string text = editor_->GetText();
    const auto range = sql::statement_range_at(text, dialect, cursor_offset());
    if (!range) return {};
    return text.substr(range->begin, range->end - range->begin);
}

} // namespace otter::ui
