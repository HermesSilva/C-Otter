// C-Otter -- ui/sql_document.hpp
//
// Um documento SQL aberto: editor, resultado e estado proprios.
//
// Cada aba e' um documento independente, como no DBeaver -- varios scripts
// abertos ao mesmo tempo, cada um com seu resultado.
#pragma once

#include "TextEditor.h"

#include "db/aggregate.hpp"
#include "db/coloring.hpp"
#include "db/sparkline.hpp"
#include "db/pivot.hpp"
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

    // Conexao a que este script pertence, pelo id ESTAVEL da conexao (nao
    // pelo indice, que muda quando outra e' fechada).
    //
    // Existe porque executar usava sempre a conexao ativa: abrir uma segunda
    // conexao e voltar a uma aba da primeira fazia o Ctrl+Enter rodar contra
    // a base errada -- e o realce misturava os dialetos na mesma tela, com
    // `public.` do PostgreSQL ao lado da crase do MySQL.
    [[nodiscard]] std::size_t connection_id() const noexcept {
        return connection_id_;
    }
    void set_connection_id(std::size_t id) noexcept { connection_id_ = id; }

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

    // Tamanho de pagina EFETIVAMENTE usado nesta consulta.
    //
    // Guardado aqui, e nao relido do perfil: a colheita do resultado precisa
    // do mesmo valor para cortar a linha-sonda, e mudar a opcao durante a
    // consulta daria o numero errado.
    [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }
    void set_page_size(std::size_t size) noexcept { page_size_ = size; }

    // Total de linhas do resultado inteiro, quando o usuario pediu a
    // contagem (o `resultset.count` do DBeaver).
    //
    // `optional` e nao um sentinela: "ainda nao contei" e "contei e deu zero"
    // sao estados diferentes, e zero e' um total legitimo.
    [[nodiscard]] const std::optional<std::size_t>& total_rows() const noexcept {
        return total_rows_;
    }
    void set_total_rows(std::size_t total) noexcept { total_rows_ = total; }

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

    // Tabela dinamica (ADR 0005). Quando ativa, SUBSTITUI a grade: mostrar as
    // duas ao mesmo tempo duplicaria a tela sem ajudar a ler nenhuma.
    [[nodiscard]] bool pivot_active() const noexcept { return pivot_active_; }
    void set_pivot_active(bool active) noexcept { pivot_active_ = active; }

    // Modo registro: UMA linha por vez, atributos em pilha. E' o `toggleMode`
    // do DBeaver, e existe para tabela larga -- com 40 colunas, a grade
    // obriga a rolar na horizontal para ler um cadastro.
    //
    // Por DOCUMENTO, nao global: uma aba lendo um cadastro e outra lendo um
    // relatorio querem visoes diferentes ao mesmo tempo.
    [[nodiscard]] bool record_mode() const noexcept { return record_mode_; }
    void set_record_mode(bool on) noexcept { record_mode_ = on; }

    [[nodiscard]] db::PivotSpec& pivot_spec() noexcept { return pivot_spec_; }
    [[nodiscard]] const db::PivotSpec& pivot_spec() const noexcept {
        return pivot_spec_;
    }

    [[nodiscard]] db::PivotResult& pivot_result() noexcept { return pivot_; }
    [[nodiscard]] const db::PivotResult& pivot_result() const noexcept {
        return pivot_;
    }

    // Regras de cor condicional (ADR 0005).
    //
    // Por DOCUMENTO, e nao global: duas abas podem mostrar consultas
    // diferentes, e uma regra sobre "situacao" nao faz sentido numa aba que
    // nao tem essa coluna.
    // Barras na celula (ADR 0005). Vivem ao lado das regras de cor porque
    // sao a mesma ideia -- ler a coluna sem ler os numeros -- por dois meios
    // diferentes, e o usuario costuma querer os dois na mesma coluna.
    [[nodiscard]] db::BarRules& bar_rules() noexcept { return bar_rules_; }
    [[nodiscard]] const db::BarRules& bar_rules() const noexcept {
        return bar_rules_;
    }

    [[nodiscard]] db::ColorRules& color_rules() noexcept { return color_rules_; }
    [[nodiscard]] const db::ColorRules& color_rules() const noexcept {
        return color_rules_;
    }

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
        // A contagem pertence a UMA consulta: mante-la apos executar outra
        // exibiria o total da anterior ao lado das linhas da nova.
        total_rows_.reset();
    }

private:
    std::size_t                 id_;
    std::size_t                 connection_id_ = 0;
    std::unique_ptr<TextEditor> editor_;
    std::string                 title_;
    std::string                 file_path_;
    std::string                 status_;
    std::optional<db::ResultSet> result_;
    std::string                 paged_sql_;
    sql::SortOrder              sort_;
    sql::ColumnFilter           filter_;
    db::EditBuffer              edits_;
    db::BarRules                bar_rules_;
    db::ColorRules              color_rules_;

    db::PivotSpec               pivot_spec_;
    db::PivotResult             pivot_;
    bool                        pivot_active_ = false;
    bool                        record_mode_ = false;
    db::EditTarget              edit_target_;
    db::GroupSpec               group_spec_;
    db::GroupResult             groups_;
    std::optional<std::size_t>  total_rows_;
    std::size_t                 page_size_ = 200;   // sql::kDefaultPageSize
    std::size_t                 page_ = 0;
    std::size_t                 save_point_ = 0;
    bool                        pinned_ = false;
    bool                        executing_ = false;
    bool                        paged_ = false;
    bool                        has_more_ = false;
};

} // namespace otter::ui
