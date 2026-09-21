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
}

std::string SqlDocument::title() const {
    if (!title_.empty()) return title_;

    if (!file_path_.empty()) {
        return std::filesystem::path(file_path_).filename().string();
    }
    return std::string(TR("Script")) + " " + std::to_string(id_);
}

void SqlDocument::set_file_path(std::string path) {
    file_path_ = std::move(path);
    // O titulo passa a vir do arquivo; um nome dado antes seria confuso.
    title_.clear();
}

bool SqlDocument::modified() const {
    return editor_->GetUndoIndex() != save_point_;
}

void SqlDocument::mark_saved() {
    save_point_ = editor_->GetUndoIndex();
}

std::string SqlDocument::sql_to_execute() const {
    // Com seleção, executa só ela -- comportamento esperado de cliente SQL.
    if (editor_->CurrentCursorHasSelection()) {
        return editor_->GetSectionText(editor_->GetCurrentCursorSelection());
    }
    return editor_->GetText();
}

} // namespace otter::ui
