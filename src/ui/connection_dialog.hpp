// C-Otter -- ui/connection_dialog.hpp
//
// Assistente de conexao completo, equivalente ao do DBeaver: selecao de driver
// por catalogo, e abas Principal / PostgreSQL / Driver / SSH / SSL / Proxy /
// Inicializacao / Geral.
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

    void draw_driver_catalog();
    void draw_configuration(const Feedback& feedback);

    void draw_tab_main();
    void draw_tab_postgres();
    void draw_tab_driver_properties();
    void draw_tab_ssh();
    void draw_tab_ssl();
    void draw_tab_proxy();
    void draw_tab_initialization();
    void draw_tab_general();

    db::ConnectionProfile profile_;
    ConnectFn             on_test_;
    ConnectFn             on_connect_;
    SaveFn                on_save_;

    Step  step_ = Step::select_driver;
    bool  visible_ = false;
    bool  editing_ = false;

    char  driver_filter_[64] = "";
    int   category_index_ = 0;

    // Buffer para nova propriedade de driver.
    char property_key_[64] = "";
    char property_value_[128] = "";
};

} // namespace otter::ui
