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
#include "sql/editing.hpp"
#include "sql/paging.hpp"
#include "ui/grid_view.hpp"
#include "ui/object_view.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

    // Rotulo da aba: o nome dado pelo usuario; senao o do arquivo, sem a
    // extensao (como o DBeaver mostra); senao o nome reservado para a aba
    // nova ("Script-2").
    [[nodiscard]] std::string title() const;
    void set_title(std::string title) { title_ = std::move(title); }
    // So' o nome dado pelo usuario -- e' o que vai para o indice da sessao.
    [[nodiscard]] const std::string& custom_title() const noexcept { return title_; }

    // O nome de uma aba ainda sem arquivo, escolhido por quem a cria para
    // nao repetir o de um script em disco (ui/script_store.hpp). E' tambem o
    // nome com que ela sera' gravada.
    [[nodiscard]] const std::string& default_title() const noexcept {
        return default_title_;
    }
    void set_default_title(std::string title) { default_title_ = std::move(title); }

    [[nodiscard]] const std::string& file_path() const noexcept { return file_path_; }
    // O usuario escolheu o arquivo (abrir, salvar como): o titulo passa a
    // vir dele.
    void set_file_path(std::string path);
    // O arquivo da gravacao automatica: so' o caminho; o titulo dado pelo
    // usuario fica.
    void set_storage_path(std::string path) { file_path_ = std::move(path); }

    // --- Gravacao automatica (ui/script_store.hpp) ----------------------------
    //
    // O script e' gravado sozinho pouco depois de a digitacao parar. O estado
    // fica aqui, por documento: cada aba tem o seu relogio.
    struct Autosave {
        std::size_t seen_index = 0;     // indice de desfazer ja' observado
        double      changed_at = 0.0;   // quando mudou pela ultima vez
        bool        pending    = false; // ha' mudanca ainda nao gravada
        bool        on_disk    = false; // o arquivo existe
        // Nao gravar mais: o usuario mandou apagar este script.
        bool        off        = false;
    };
    [[nodiscard]] Autosave& autosave() noexcept { return autosave_; }
    [[nodiscard]] const Autosave& autosave() const noexcept { return autosave_; }

    // Alteracoes nao salvas.
    [[nodiscard]] bool modified() const;
    void mark_saved();

    // --- Editor de objeto (ui/object_view.hpp) -------------------------------
    //
    // Com isto preenchido a aba NAO e' um script: mostra as propriedades, os
    // dados e o DDL de um objeto do banco. O editor de texto dela guarda o
    // DDL (ou o fonte da funcao), e o resultado, os dados da tabela.
    [[nodiscard]] bool is_object() const noexcept { return object_.has_value(); }
    [[nodiscard]] ObjectView* object() noexcept {
        return object_.has_value() ? &*object_ : nullptr;
    }
    [[nodiscard]] const ObjectView* object() const noexcept {
        return object_.has_value() ? &*object_ : nullptr;
    }
    void set_object(ObjectView view) { object_ = std::move(view); }

    // Aba fixada nao e' fechada por "fechar outras" e vem antes das demais.
    [[nodiscard]] bool pinned() const noexcept { return pinned_; }
    void set_pinned(bool pinned) noexcept { pinned_ = pinned; }

    [[nodiscard]] std::optional<db::ResultSet>& result() noexcept { return tab().result; }
    void set_result(db::ResultSet result) { tab().result = std::move(result); }
    void clear_result() { tab().result.reset(); }

    // Mensagem do ultimo comando -- exibida no lugar da grade quando nao ha'
    // linhas (ex.: "comando executado, 3 linhas afetadas").
    [[nodiscard]] const std::string& status() const noexcept { return tab().status; }
    void set_status(std::string status) { tab().status = std::move(status); }

    [[nodiscard]] bool executing() const noexcept { return executing_; }
    void set_executing(bool executing) noexcept {
        executing_ = executing;
        if (executing) executing_tab_id_ = tabs_[active_tab_]->id;
    }

    // SQL a executar: a selecao, se houver; senao a INSTRUCAO sob o cursor
    // (sql/editing.hpp). O dialeto decide onde uma instrucao termina.
    [[nodiscard]] std::string sql_to_execute(const sql::Dialect& dialect) const;

    // Posicao do cursor em bytes dentro do texto -- a unidade do analisador.
    [[nodiscard]] std::size_t cursor_offset() const;

    // Variaveis do script: `@set nome = valor` define, `${nome}` usa
    // (sql/editing.hpp). Por documento, como o contexto de execucao do
    // DBeaver: duas abas nao compartilham variaveis.
    [[nodiscard]] sql::Variables& variables() noexcept { return variables_; }
    [[nodiscard]] const sql::Variables& variables() const noexcept {
        return variables_;
    }

    // --- Paginacao (ADR 0011) ------------------------------------------------
    //
    // Estado da navegacao entre paginas do resultado. Fica no documento, nao
    // na sessao: cada aba tem sua consulta e sua posicao.

    // Consulta cuja pagina esta' sendo exibida. Guardada como escrita pelo
    // usuario -- e' dela que cada pagina e' derivada.
    [[nodiscard]] const std::string& paged_sql() const noexcept {
        return tab().paged_sql;
    }
    void set_paged_sql(std::string sql) { tab().paged_sql = std::move(sql); }

    [[nodiscard]] std::size_t page() const noexcept { return tab().page; }
    void set_page(std::size_t page) noexcept { tab().page = page; }

    // Verdadeiro quando a consulta foi reescrita com LIMIT/OFFSET. Quando e'
    // falso, o resultado e' completo e os botoes de pagina somem.
    [[nodiscard]] bool paged() const noexcept { return tab().paged; }
    void set_paged(bool paged) noexcept { tab().paged = paged; }

    // Veio a linha extra pedida alem do tamanho da pagina? Se sim, ha' mais
    // resultado adiante. Nao e' um total: um COUNT(*) custaria outra varredura.
    [[nodiscard]] bool has_more() const noexcept { return tab().has_more; }
    void set_has_more(bool more) noexcept { tab().has_more = more; }

    // Tamanho de pagina EFETIVAMENTE usado nesta consulta.
    //
    // Guardado aqui, e nao relido do perfil: a colheita do resultado precisa
    // do mesmo valor para cortar a linha-sonda, e mudar a opcao durante a
    // consulta daria o numero errado.
    [[nodiscard]] std::size_t page_size() const noexcept { return tab().page_size; }
    void set_page_size(std::size_t size) noexcept { tab().page_size = size; }

    // Total de linhas do resultado inteiro, quando o usuario pediu a
    // contagem (o `resultset.count` do DBeaver).
    //
    // `optional` e nao um sentinela: "ainda nao contei" e "contei e deu zero"
    // sao estados diferentes, e zero e' um total legitimo.
    [[nodiscard]] const std::optional<std::size_t>& total_rows() const noexcept {
        return tab().total_rows;
    }
    void set_total_rows(std::size_t total) noexcept { tab().total_rows = total; }

    // Ordenacao pedida no cabecalho da grade. Vai para o servidor junto com
    // a pagina: ordenar so' as 200 linhas visiveis daria a ordem errada.
    [[nodiscard]] const sql::SortOrder& sort() const noexcept { return tab().sort; }
    void set_sort(sql::SortOrder sort) { tab().sort = std::move(sort); }

    // Filtro por coluna, pedido no cabecalho. Vai para o servidor junto com
    // a pagina, pelo mesmo motivo da ordenacao.
    [[nodiscard]] const sql::ColumnFilter& filter() const noexcept {
        return tab().filter;
    }
    void set_filter(sql::ColumnFilter filter) { tab().filter = std::move(filter); }

    // --- Agrupamento e totais (ADR 0005) -------------------------------------
    [[nodiscard]] db::GroupSpec& group_spec() noexcept { return tab().group_spec; }
    [[nodiscard]] const db::GroupSpec& group_spec() const noexcept {
        return tab().group_spec;
    }

    [[nodiscard]] const db::GroupResult& groups() const noexcept {
        return tab().groups;
    }
    void set_groups(db::GroupResult groups) { tab().groups = std::move(groups); }

    // --- Edicao (ADR 0014) ---------------------------------------------------
    [[nodiscard]] db::EditBuffer& edits() noexcept { return tab().edits; }
    [[nodiscard]] const db::EditBuffer& edits() const noexcept { return tab().edits; }

    // Tabela dinamica (ADR 0005). Quando ativa, SUBSTITUI a grade: mostrar as
    // duas ao mesmo tempo duplicaria a tela sem ajudar a ler nenhuma.
    [[nodiscard]] bool pivot_active() const noexcept { return tab().pivot_active; }
    void set_pivot_active(bool active) noexcept { tab().pivot_active = active; }

    // Modo registro: UMA linha por vez, atributos em pilha. E' o `toggleMode`
    // do DBeaver, e existe para tabela larga -- com 40 colunas, a grade
    // obriga a rolar na horizontal para ler um cadastro.
    //
    // Por DOCUMENTO, nao global: uma aba lendo um cadastro e outra lendo um
    // relatorio querem visoes diferentes ao mesmo tempo.
    [[nodiscard]] bool record_mode() const noexcept { return tab().record_mode; }
    void set_record_mode(bool on) noexcept { tab().record_mode = on; }

    [[nodiscard]] db::PivotSpec& pivot_spec() noexcept { return tab().pivot_spec; }
    [[nodiscard]] const db::PivotSpec& pivot_spec() const noexcept {
        return tab().pivot_spec;
    }

    [[nodiscard]] db::PivotResult& pivot_result() noexcept { return tab().pivot; }
    [[nodiscard]] const db::PivotResult& pivot_result() const noexcept {
        return tab().pivot;
    }

    // Regras de cor condicional (ADR 0005).
    //
    // Por DOCUMENTO, e nao global: duas abas podem mostrar consultas
    // diferentes, e uma regra sobre "situacao" nao faz sentido numa aba que
    // nao tem essa coluna.
    // Barras na celula (ADR 0005). Vivem ao lado das regras de cor porque
    // sao a mesma ideia -- ler a coluna sem ler os numeros -- por dois meios
    // diferentes, e o usuario costuma querer os dois na mesma coluna.
    [[nodiscard]] db::BarRules& bar_rules() noexcept { return tab().bar_rules; }
    [[nodiscard]] const db::BarRules& bar_rules() const noexcept {
        return tab().bar_rules;
    }

    [[nodiscard]] db::ColorRules& color_rules() noexcept { return tab().color_rules; }
    [[nodiscard]] const db::ColorRules& color_rules() const noexcept {
        return tab().color_rules;
    }

    // Onde gravar. Recalculado quando o resultado muda.
    [[nodiscard]] const db::EditTarget& edit_target() const noexcept {
        return tab().edit_target;
    }
    void set_edit_target(db::EditTarget target) {
        tab().edit_target = std::move(target);
    }

    void reset_paging() {
        tab().paged_sql.clear();
        tab().page     = 0;
        tab().paged    = false;
        tab().has_more = false;
        tab().sort     = {};
        tab().filter   = {};
        // A contagem pertence a UMA consulta: mante-la apos executar outra
        // exibiria o total da anterior ao lado das linhas da nova.
        tab().total_rows.reset();
        tab().view.reset_for_new_query();
    }

    // Como o resultado esta' sendo mostrado: colunas escondidas, zoom,
    // apresentacao (ui/grid_view.hpp).
    [[nodiscard]] GridView& grid_view() noexcept { return tab().view; }
    [[nodiscard]] const GridView& grid_view() const noexcept { return tab().view; }


    // --- Abas de resultado ----------------------------------------------------
    //
    // O DBeaver guarda varios resultados por script: Ctrl+Enter reaproveita a
    // aba atual, Ctrl+\ abre outra, e uma aba FIXADA nunca e' sobrescrita.
    // Havia um resultado so' por script -- rodar a segunda consulta apagava a
    // primeira, e nao dava para comparar duas.
    //
    // Todo o estado que pertence a UM resultado (linhas, pagina, ordenacao,
    // edicoes pendentes, agrupamento, pivot) mora na aba. Os metodos acima
    // falam sempre com a aba ATIVA, e por isso o resto do programa nao mudou.
    struct ResultTab {
        std::size_t                  id = 0;
        std::string                  title;
        bool                         pinned = false;

        std::string                  status;
        std::optional<db::ResultSet> result;
        std::string                  paged_sql;
        sql::SortOrder               sort;
        sql::ColumnFilter            filter;
        db::EditBuffer               edits;
        db::BarRules                 bar_rules;
        db::ColorRules               color_rules;
        db::PivotSpec                pivot_spec;
        db::PivotResult              pivot;
        bool                         pivot_active = false;
        bool                         record_mode = false;
        db::EditTarget               edit_target;
        db::GroupSpec                group_spec;
        db::GroupResult              groups;
        std::optional<std::size_t>   total_rows;
        std::size_t                  page_size = 200;   // sql::kDefaultPageSize
        std::size_t                  page = 0;
        bool                         paged = false;
        bool                         has_more = false;
        GridView                     view;
    };

    [[nodiscard]] std::size_t result_tab_count() const noexcept {
        return tabs_.size();
    }
    [[nodiscard]] std::size_t active_result_tab() const noexcept {
        return active_tab_;
    }
    [[nodiscard]] const ResultTab& result_tab(std::size_t index) const {
        return *tabs_[index];
    }
    void select_result_tab(std::size_t index) {
        if (index < tabs_.size()) active_tab_ = index;
    }
    // Pelo id, que sobrevive ao fechamento de outra aba. Falso se sumiu.
    bool select_result_tab_by_id(std::size_t id);
    [[nodiscard]] std::size_t active_result_tab_id() const {
        return tabs_[active_tab_]->id;
    }

    // Abre uma aba nova, vazia, e a torna a ativa. Devolve o id.
    std::size_t add_result_tab();
    // Fecha. A ultima nunca fecha: e' esvaziada -- o painel de resultado
    // precisa de uma aba para mostrar "run a query".
    void close_result_tab(std::size_t index);
    void set_result_tab_pinned(std::size_t index, bool pinned);
    void set_result_tab_title(std::size_t index, std::string title);

    // A aba que vai RECEBER o resultado da execucao em curso. Fixada quando
    // a execucao comeca: o usuario pode trocar de aba enquanto a consulta
    // roda, e o resultado nao pode cair na que ele estiver olhando.
    [[nodiscard]] std::size_t executing_tab_id() const noexcept {
        return executing_tab_id_;
    }

private:
    ResultTab&       tab() noexcept { return *tabs_[active_tab_]; }
    const ResultTab& tab() const noexcept { return *tabs_[active_tab_]; }

    // unique_ptr: os ponteiros para a aba ativa (edicoes, regras de cor)
    // nao podem mudar quando o vetor cresce.
    sql::Variables variables_;
    std::vector<std::unique_ptr<ResultTab>> tabs_;
    std::size_t                 active_tab_ = 0;
    std::size_t                 next_tab_id_ = 1;
    std::size_t                 executing_tab_id_ = 0;

    std::size_t                 id_;
    std::size_t                 connection_id_ = 0;
    std::unique_ptr<TextEditor> editor_;
    std::string                 title_;
    std::string                 default_title_;
    std::string                 file_path_;
    Autosave                    autosave_;

    std::optional<ObjectView>   object_;

    std::size_t                 save_point_ = 0;
    bool                        pinned_ = false;
    bool                        executing_ = false;
};

} // namespace otter::ui
