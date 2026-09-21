// C-Otter -- ui/sql_document.hpp
//
// Um documento SQL aberto: editor, resultado e estado proprios.
//
// Cada aba e' um documento independente, como no DBeaver -- varios scripts
// abertos ao mesmo tempo, cada um com seu resultado.
#pragma once

#include "TextEditor.h"

#include "db/aggregate.hpp"
#include "db/edit.hpp"
#include "db/result_set.hpp"
#include "sql/paging.hpp"

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

    // --- Paginacao (ADR 0011) ------------------------------------------------
    //
    // Estado da navegacao entre paginas do resultado. Fica no documento, nao
    // na sessao: cada aba tem sua consulta e sua posicao.

    // Consulta cuja pagina esta' sendo exibida. Guardada como escrita pelo
    // usuario -- e' dela que cada pagina e' derivada.
    [[nodiscard]] const std::string& paged_sql() const noexcept {
        return paged_sql_;
    }
    void set_paged_sql(std::string sql) { paged_sql_ = std::move(sql); }

    [[nodiscard]] std::size_t page() const noexcept { return page_; }
    void set_page(std::size_t page) noexcept { page_ = page; }

    // Verdadeiro quando a consulta foi reescrita com LIMIT/OFFSET. Quando e'
    // falso, o resultado e' completo e os botoes de pagina somem.
    [[nodiscard]] bool paged() const noexcept { return paged_; }
    void set_paged(bool paged) noexcept { paged_ = paged; }

    // Veio a linha extra pedida alem do tamanho da pagina? Se sim, ha' mais
    // resultado adiante. Nao e' um total: um COUNT(*) custaria outra varredura.
    [[nodiscard]] bool has_more() const noexcept { return has_more_; }
    void set_has_more(bool more) noexcept { has_more_ = more; }

    // Ordenacao pedida no cabecalho da grade. Vai para o servidor junto com
    // a pagina: ordenar so' as 200 linhas visiveis daria a ordem errada.
    [[nodiscard]] const sql::SortOrder& sort() const noexcept { return sort_; }
    void set_sort(sql::SortOrder sort) { sort_ = std::move(sort); }

    // Filtro por coluna, pedido no cabecalho. Vai para o servidor junto com
    // a pagina, pelo mesmo motivo da ordenacao.
    [[nodiscard]] const sql::ColumnFilter& filter() const noexcept {
        return filter_;
    }
    void set_filter(sql::ColumnFilter filter) { filter_ = std::move(filter); }

    // --- Agrupamento e totais (ADR 0005) -------------------------------------
    [[nodiscard]] db::GroupSpec& group_spec() noexcept { return group_spec_; }
    [[nodiscard]] const db::GroupSpec& group_spec() const noexcept {
        return group_spec_;
    }

    [[nodiscard]] const db::GroupResult& groups() const noexcept {
        return groups_;
    }
    void set_groups(db::GroupResult groups) { groups_ = std::move(groups); }

    // --- Edicao (ADR 0014) ---------------------------------------------------
    [[nodiscard]] db::EditBuffer& edits() noexcept { return edits_; }
    [[nodiscard]] const db::EditBuffer& edits() const noexcept { return edits_; }

    // Onde gravar. Recalculado quando o resultado muda.
    [[nodiscard]] const db::EditTarget& edit_target() const noexcept {
        return edit_target_;
    }
    void set_edit_target(db::EditTarget target) {
        edit_target_ = std::move(target);
    }

    void reset_paging() {
        paged_sql_.clear();
        page_     = 0;
        paged_    = false;
        has_more_ = false;
        sort_     = {};
        filter_   = {};
    }

private:
    std::size_t                 id_;
    std::unique_ptr<TextEditor> editor_;
    std::string                 title_;
    std::string                 file_path_;
    std::string                 status_;
    std::optional<db::ResultSet> result_;
    std::string                 paged_sql_;
    sql::SortOrder              sort_;
    sql::ColumnFilter           filter_;
    db::EditBuffer              edits_;
    db::EditTarget              edit_target_;
    db::GroupSpec               group_spec_;
    db::GroupResult             groups_;
    std::size_t                 page_ = 0;
    std::size_t                 save_point_ = 0;
    bool                        pinned_ = false;
    bool                        executing_ = false;
    bool                        paged_ = false;
    bool                        has_more_ = false;
};

} // namespace otter::ui
