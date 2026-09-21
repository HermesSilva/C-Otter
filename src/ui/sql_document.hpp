// C-Otter -- ui/sql_document.hpp
//
// Um documento SQL aberto: editor, resultado e estado proprios.
//
// Cada aba e' um documento independente, como no DBeaver -- varios scripts
// abertos ao mesmo tempo, cada um com seu resultado.
#pragma once

#include "TextEditor.h"

#include "db/result_set.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace otter::ui {

class SqlDocument {
public:
    explicit SqlDocument(std::size_t id);

    [[nodiscard]] std::size_t id() const noexcept { return id_; }

    [[nodiscard]] TextEditor& editor() noexcept { return *editor_; }
    [[nodiscard]] const TextEditor& editor() const noexcept { return *editor_; }

    // Rotulo da aba: nome do arquivo, nome dado pelo usuario ou "Script N".
    [[nodiscard]] std::string title() const;
    void set_title(std::string title) { title_ = std::move(title); }

    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    void set_file_path(std::string path);

    // Alteracoes nao salvas.
    [[nodiscard]] bool modified() const;
    void mark_saved();

    // Aba fixada nao e' fechada por "fechar outras" e vem antes das demais.
    [[nodiscard]] bool pinned() const noexcept { return pinned_; }
    void set_pinned(bool pinned) noexcept { pinned_ = pinned; }

    [[nodiscard]] std::optional<db::ResultSet>& result() noexcept { return result_; }
    void set_result(db::ResultSet result) { result_ = std::move(result); }
    void clear_result() { result_.reset(); }

    // Mensagem do ultimo comando -- exibida no lugar da grade quando nao ha'
    // linhas (ex.: "comando executado, 3 linhas afetadas").
    [[nodiscard]] const std::string& status() const noexcept { return status_; }
    void set_status(std::string status) { status_ = std::move(status); }

    [[nodiscard]] bool executing() const noexcept { return executing_; }
    void set_executing(bool executing) noexcept { executing_ = executing; }

    // SQL a executar: a selecao, se houver; senao o texto inteiro.
    [[nodiscard]] std::string sql_to_execute() const;

private:
    std::size_t                 id_;
    std::unique_ptr<TextEditor> editor_;
    std::string                 title_;
    std::string                 file_path_;
    std::string                 status_;
    std::optional<db::ResultSet> result_;
    std::size_t                 save_point_ = 0;
    bool                        pinned_ = false;
    bool                        executing_ = false;
};

} // namespace otter::ui
