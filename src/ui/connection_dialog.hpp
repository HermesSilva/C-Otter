// C-Otter -- ui/connection_dialog.hpp
//
// Assistente de conexao completo, equivalente ao do DBeaver: selecao de driver
// por catalogo, e uma ARVORE de categorias a' esquerda com a pagina escolhida
// a' direita -- a mesma estrutura de `EditConnectionWizard`.
//
// Eram abas horizontais ate' 2026-09-21. Ver docs/DIALOG-PARITY.md: a arvore
// nao e' enfeite, e' onde quem vem do DBeaver procura cada opcao.
#pragma once

#include "db/connection_config.hpp"

#include <functional>
#include <string>
#include <vector>

namespace otter::ui {

// Entrada do catalogo de drivers.
struct DriverEntry {
    const char*   id;
    const char*   name;
    const char*   category;
    std::uint16_t default_port;
    bool          available;      // implementado nesta versao
    const char*   note;           // por que nao esta' disponivel
};

[[nodiscard]] std::vector<DriverEntry> driver_catalog();

class ConnectionDialog {
public:
    using ConnectFn = std::function<void(const db::ConnectionProfile&)>;
    using SaveFn    = std::function<void(const db::ConnectionProfile&)>;

    // Estado externo da tentativa de conexao, para o dialogo refletir.
    struct Feedback {
        bool        busy = false;
        bool        failed = false;
        bool        succeeded = false;
        std::string message;
    };

    ConnectionDialog();

    void open_new();
    void open_edit(const db::ConnectionProfile& profile);
    void close() { visible_ = false; }

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] db::ConnectionProfile& profile() noexcept { return profile_; }

    // Tres acoes distintas, porque tem efeitos distintos:
    //
    //   on_test    conecta para conferir, sem criar uma conexao permanente
    //   on_connect conclui: cria a conexao e guarda o perfil
    //   on_save    guarda o perfil sem reconectar (modo edicao)
    void set_on_test(ConnectFn fn) { on_test_ = std::move(fn); }
    void set_on_connect(ConnectFn fn) { on_connect_ = std::move(fn); }
    void set_on_save(SaveFn fn) { on_save_ = std::move(fn); }

    void draw(const Feedback& feedback);

private:
    // Etapas do assistente, como no DBeaver.
    enum class Step { select_driver, configure };

    // Identifica cada pagina da arvore. A ordem e o aninhamento vivem em
    // `page_tree()`, no .cpp, espelhando EditConnectionWizard.addPages().
    enum class Page {
        connection_settings,   // a pagina do driver: host, porta, usuario
        initialization,
        transactions,
        driver_properties,     // "Internal parameters" no DBeaver
        general,
        metadata,              // opcoes do SGBD (hoje so' PostgreSQL)
        errors_timeouts,
        data_transfer,
        data_editor,
        data_editor_grid,
        binary_editor,
        data_formats,
        sql_editor,
        sql_completion,
        sql_code_editor,
        sql_formatting,
        sql_processing,
    };

    // Um no' da arvore da esquerda. `page` so' vale se `selectable`.
    struct PageNode {
        Page        page;
        const char* label;      // rotulo oficial do DBeaver, passa por TR()
        int         depth;      // 0 = raiz
        bool        selectable; // falso = categoria que so' agrupa
    };

    void draw_driver_catalog();
    void draw_configuration(const Feedback& feedback);
    void draw_page_tree();
    void draw_page_body();

    // Uma por pagina da arvore.
    void draw_page_connection_settings();
    void draw_page_metadata();
    void draw_page_driver_properties();
    void draw_page_initialization();
    void draw_page_transactions();
    void draw_page_general();

    // Continuam ABAS, dentro de "Connection settings": e' onde o DBeaver as
    // poe (ConnectionPageSettings), nao na raiz da arvore.
    void draw_tab_ssh();
    void draw_tab_ssl();
    void draw_tab_proxy();

    // Paginas cujo conteudo ainda nao existe no C-Otter. Dizem isso na tela,
    // em vez de fingir (diretriz 6).
    void draw_page_placeholder(const char* what);

    db::ConnectionProfile profile_;
    ConnectFn             on_test_;
    ConnectFn             on_connect_;
    SaveFn                on_save_;

    Step  step_ = Step::select_driver;
    bool  visible_ = false;
    bool  editing_ = false;

    // Pagina aberta. "Connection settings" e' a primeira, como no DBeaver.
    Page  page_ = Page::connection_settings;

    char  driver_filter_[64] = "";
    int   category_index_ = 0;

    // Buffer para nova propriedade de driver.
    char property_key_[64] = "";
    char property_value_[128] = "";
};

} // namespace otter::ui
