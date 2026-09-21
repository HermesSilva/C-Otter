#include "ui/connection_dialog.hpp"
#include "base/i18n.hpp"
#include "ui/icons.hpp"
#include "ui/theme.hpp"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace otter::ui {
namespace {

ImVec4 col4(std::uint32_t c) {
    return ImGui::ColorConvertU32ToFloat4(static_cast<ImU32>(c));
}

// Campo de texto ligado a um std::string. O ImGui trabalha com buffer cru;
// isto evita espalhar arrays de char pela estrutura de configuracao.
bool input_string(const char* label, std::string& value, std::size_t capacity = 256,
                  ImGuiInputTextFlags flags = 0) {
    std::vector<char> buffer(std::max(capacity, value.size() + 1), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());

    if (ImGui::InputText(label, buffer.data(), buffer.size(), flags)) {
        value = buffer.data();
        return true;
    }
    return false;
}

bool input_uint16(const char* label, std::uint16_t& value) {
    int temporary = value;
    if (ImGui::InputInt(label, &temporary, 0, 0)) {
        value = static_cast<std::uint16_t>(std::clamp(temporary, 0, 65535));
        return true;
    }
    return false;
}

bool input_seconds(const char* label, std::chrono::seconds& value) {
    int temporary = static_cast<int>(value.count());
    if (ImGui::InputInt(label, &temporary, 0, 0)) {
        value = std::chrono::seconds(std::max(0, temporary));
        return true;
    }
    return false;
}

// Rotulo com tooltip de ajuda, como o "(?)" do DBeaver.
void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextColored(col4(colors().text_dim), "(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto it = std::search(
        haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        });
    return it != haystack.end();
}

// Em ingles: a chave de traducao e' o proprio texto (base/i18n.hpp), e a
// comparacao com DriverEntry::category tambem usa estes valores.
constexpr std::array<const char*, 8> kCategories = {
    "All", "Popular", "SQL", "NoSQL", "Analytical",
    "Files", "Embedded", "Timeseries",
};

} // namespace

// Catalogo espelhando o do DBeaver. Os indisponiveis aparecem esmaecidos com o
// motivo -- esconder a lista inteira daria a impressao de que o produto so'
// fala com PostgreSQL por design.
std::vector<DriverEntry> driver_catalog() {
    // Categorias e notas em ingles: sao chaves de traducao, resolvidas com
    // TR() no momento de desenhar.
    return {
        {"postgresql", "PostgreSQL",  "Popular",    5432, true,  nullptr},
        {"mysql",      "MySQL",       "Popular",    3306, false, "protocol in development"},
        {"mariadb",    "MariaDB",     "Popular",    3306, false, "protocol in development"},
        {"sqlite",     "SQLite",      "Embedded",      0, false, "planned for phase 3"},
        {"mssql",      "SQL Server",  "Popular",    1433, false, "TDS planned for phase 3"},
        {"oracle",     "Oracle",      "Popular",    1521, false, "planned for phase 3"},
        {"db2",        "Db2 for LUW", "SQL",       50000, false, "out of scope for v1"},
        {"clickhouse", "ClickHouse",  "Analytical", 8123, false, "out of scope for v1"},
        {"duckdb",     "DuckDB",      "Analytical",    0, false, "out of scope for v1"},
        {"snowflake",  "Snowflake",   "Analytical",  443, false, "out of scope for v1"},
        {"h2",         "H2",          "Embedded",   8082, false, "out of scope for v1"},
        {"firebird",   "Firebird",    "SQL",        3050, false, "out of scope for v1"},
        {"csv",        "CSV",         "Files",         0, false, "out of scope for v1"},
        {"mongodb",    "MongoDB",     "NoSQL",     27017, false, "out of scope for v1"},
        {"redis",      "Redis",       "NoSQL",      6379, false, "out of scope for v1"},
        {"cassandra",  "Cassandra",   "NoSQL",      9042, false, "out of scope for v1"},
        {"influxdb",   "InfluxDB",    "Timeseries", 8086, false, "out of scope for v1"},
        {"timescale",  "TimescaleDB", "Timeseries", 5432, false, "uses the PostgreSQL driver"},
    };
}

ConnectionDialog::ConnectionDialog() = default;

void ConnectionDialog::open_new() {
    profile_ = db::ConnectionProfile{};
    step_    = Step::select_driver;
    editing_ = false;
    visible_ = true;
}

void ConnectionDialog::open_edit(const db::ConnectionProfile& profile) {
    profile_ = profile;
    step_    = Step::configure;
    editing_ = true;
    visible_ = true;
}

void ConnectionDialog::draw(const Feedback& feedback) {
    if (!visible_) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_Appearing);

    const char* title = editing_ ? TR("Edit connection###ConnDialog")
                                 : TR("New connection###ConnDialog");

    if (ImGui::Begin(title, &visible_, ImGuiWindowFlags_NoDocking)) {
        if (step_ == Step::select_driver) {
            draw_driver_catalog();
        } else {
            draw_configuration(feedback);
        }
    }
    ImGui::End();
}

void ConnectionDialog::draw_driver_catalog() {
    ImGui::TextColored(col4(colors().accent_light), TR("Select the database"));
    ImGui::TextColored(col4(colors().text_dim),
                       TR("Choose the driver for the new connection."));
    ImGui::Separator();

    // Coluna de categorias, como no DBeaver.
    ImGui::BeginChild("##categories", ImVec2(150, -46), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(kCategories.size()); ++i) {
        if (ImGui::Selectable(TR(kCategories[static_cast<std::size_t>(i)]),
                              category_index_ == i)) {
            category_index_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##drivers", ImVec2(0, -46));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", TR("Filter drivers..."),
                             driver_filter_, sizeof(driver_filter_));
    ImGui::Separator();

    const std::string_view category =
        kCategories[static_cast<std::size_t>(category_index_)];

    if (ImGui::BeginTable("##driverlist", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn(TR("Driver"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Category"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn(TR("Status"), ImGuiTableColumnFlags_WidthFixed, 210.0f);

        for (const DriverEntry& driver : driver_catalog()) {
            if (category != "All" && category != driver.category) continue;
            if (!contains_ci(driver.name, driver_filter_)) continue;

            ImGui::TableNextRow();
            ImGui::PushID(driver.id);

            ImGui::TableSetColumnIndex(0);
            ImGui::BeginDisabled(!driver.available);

            const bool selected = profile_.driver_id == driver.id;
            if (ImGui::Selectable(driver.name, selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                profile_.driver_id = driver.id;
                profile_.port      = driver.default_port;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    step_ = Step::configure;
                }
            }
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(col4(colors().text_dim), "%s", TR(driver.category));

            ImGui::TableSetColumnIndex(2);
            if (driver.available) {
                ImGui::TextColored(col4(colors().ok), TR("available"));
            } else {
                ImGui::TextColored(col4(colors().text_dim), "%s", TR(driver.note));
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Separator();

    const bool can_advance = profile_.driver_id == "postgresql";
    ImGui::BeginDisabled(!can_advance);
    if (ImGui::Button(TR("Next >"), ImVec2(110, 0))) step_ = Step::configure;
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(110, 0))) visible_ = false;

    if (!can_advance) {
        ImGui::SameLine();
        ImGui::TextColored(col4(colors().text_dim),
                           TR("  select an available driver"));
    }
}

void ConnectionDialog::draw_configuration(const Feedback& feedback) {
    // Faixa colorida do tipo de conexão -- o mesmo recurso que o DBeaver usa
    // para diferenciar produção de desenvolvimento num olhar.
    const db::ConnectionTypeInfo& type = db::connection_type_info(profile_.type);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col4(type.color & 0x40FFFFFFu));
    ImGui::BeginChild("##typeband", ImVec2(0, 26), ImGuiChildFlags_None);
    ImGui::TextColored(col4(type.color), "  %s", type.name);
    ImGui::SameLine();
    ImGui::TextColored(col4(colors().text_dim), " | %s",
                       profile_.effective_name().c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (ImGui::BeginTabBar("##conntabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(TR("Main"))) {
            draw_tab_main();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("PostgreSQL")) {
            draw_tab_postgres();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Driver")) {
            draw_tab_driver_properties();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("SSH"))) {
            draw_tab_ssh();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("SSL"))) {
            draw_tab_ssl();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("Proxy"))) {
            draw_tab_proxy();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("Initialization"))) {
            draw_tab_initialization();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(TR("General"))) {
            draw_tab_general();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Rodapé fixo com as ações.
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 72.0f);
    ImGui::Separator();

    if (!editing_) {
        if (ImGui::Button(TR("< Back"), ImVec2(100, 0))) step_ = Step::select_driver;
        ImGui::SameLine();
    }

    ImGui::BeginDisabled(feedback.busy);
    if (ImGui::Button(TR("Test connection"), ImVec2(130, 0)) && on_connect_) {
        on_connect_(profile_);
    }
    ImGui::SameLine();
    if (ImGui::Button(editing_ ? TR("Save") : TR("Finish"), ImVec2(100, 0))) {
        if (on_save_) on_save_(profile_);
        if (on_connect_) on_connect_(profile_);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(TR("Cancel"), ImVec2(100, 0))) visible_ = false;

    if (feedback.busy) {
        ImGui::SameLine();
        const float t = static_cast<float>(ImGui::GetTime());
        ImVec4 pulse = col4(colors().data_light);
        pulse.w = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
        ImGui::TextColored(pulse, TR("  * connecting..."));
    } else if (feedback.failed) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(colors().error), "%s", feedback.message.c_str());
        ImGui::PopTextWrapPos();
    } else if (feedback.succeeded) {
        ImGui::TextColored(col4(colors().ok), "%s", feedback.message.c_str());
    }
}

void ConnectionDialog::draw_tab_main() {
    ImGui::BeginChild("##main", ImVec2(0, -80));

    ImGui::TextColored(col4(colors().data), TR("Server"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(320);
    input_string(TR("Host"), profile_.host, 128);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port"), profile_.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Database"), profile_.database, 128);

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Authentication"));
    ImGui::Separator();

    static constexpr const char* kAuthModels[] = {
        "Banco de dados nativo", "Sem autenticação", "Ident / Peer",
        "Kerberos", "AWS IAM",
    };
    int auth = static_cast<int>(profile_.auth_model);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Method"), &auth, kAuthModels,
                     IM_ARRAYSIZE(kAuthModels))) {
        profile_.auth_model = static_cast<db::AuthModel>(auth);
    }

    const bool needs_credentials =
        profile_.auth_model == db::AuthModel::database_native;

    ImGui::BeginDisabled(!needs_credentials);
    ImGui::SetNextItemWidth(320);
    input_string(TR("User"), profile_.user, 128);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Password"), profile_.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::Checkbox(TR("Save password"), &profile_.save_password);

    // O aviso e' literal de proposito: a criptografia do arquivo usa a chave
    // fixa do DBeaver, que e' publica no codigo-fonte dele. Deixar o usuario
    // supor que ha' um cofre por tras seria o tipo de campo que finge
    // funcionar (diretiva 6; ADR 0012).
    help_marker(TR(
        "The password is stored in credentials-config.json, encrypted the "
        "same way DBeaver does it.\n\n"
        "That encryption uses a fixed key published in DBeaver's source "
        "code: it protects against a casual look at the file, and against "
        "nothing more. Leave it off for credentials that matter."));

    if (profile_.save_password) {
        icon_inline(Icon::warning, colors().warn);
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextColored(col4(colors().warn),
                           TR("weak encryption, for DBeaver compatibility"));
    }
    ImGui::EndDisabled();

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_postgres() {
    ImGui::BeginChild("##pg", ImVec2(0, -80));

    ImGui::TextColored(col4(colors().data), TR("Navigator settings"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Show all databases"),
                    &profile_.postgres.show_non_default_databases);
    help_marker("Lista todos os bancos do servidor, não apenas o conectado.");

    ImGui::Checkbox(TR("Show template databases"),
                    &profile_.postgres.show_template_databases);
    help_marker("Inclui template0 e template1.");

    ImGui::Checkbox(TR("Show inaccessible databases"),
                    &profile_.postgres.show_unavailable_databases);
    help_marker("Inclui bancos aos quais o usuário não tem permissão de conectar.");

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Performance"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Read size statistics"),
                    &profile_.postgres.show_database_statistics);
    help_marker("Calcula o tamanho em disco de tabelas e índices. Em bancos "
                "muito grandes, torna a expansão da árvore mais lenta.");

    ImGui::Checkbox(TR("Read all data types"),
                    &profile_.postgres.read_all_data_types);
    help_marker("Inclui tipos raros e de sistema. Deixa o carregamento de "
                "metadados mais lento.");

    ImGui::Checkbox(TR("Read key columns"),
                    &profile_.postgres.read_keys_with_columns);
    help_marker("Carrega as colunas de cada chave junto com a chave. Útil "
                "para inferência de JOIN; custa uma consulta a mais.");

    ImGui::Checkbox(TR("Use prepared statements"),
                    &profile_.postgres.use_prepared_statements);

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), "SQL");
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string(TR("Session role"), profile_.postgres.session_role, 64);
    help_marker("Executa SET ROLE ao abrir a conexão.");

    ImGui::Checkbox(TR("Replace legacy timezone"),
                    &profile_.postgres.replace_legacy_timezone);
    help_marker("Converte timestamptz do formato antigo para o atual.");

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_driver_properties() {
    ImGui::BeginChild("##driverprops", ImVec2(0, -80));

    ImGui::TextColored(col4(colors().data), TR("Driver properties"));
    ImGui::TextColored(col4(colors().text_dim),
                       TR("Parameters passed directly to the driver on connect."));
    ImGui::Separator();

    if (ImGui::BeginTable("##props", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(TR("Property"));
        ImGui::TableSetupColumn(TR("Value"));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableHeadersRow();

        std::string to_remove;
        for (auto& [key, value] : profile_.driver_properties) {
            ImGui::TableNextRow();
            ImGui::PushID(key.c_str());

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(key.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            input_string("##value", value, 256);

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("×")) to_remove = key;

            ImGui::PopID();
        }
        ImGui::EndTable();

        if (!to_remove.empty()) profile_.driver_properties.erase(to_remove);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##newkey", TR("property"),
                             property_key_, sizeof(property_key_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##newvalue", TR("value"),
                             property_value_, sizeof(property_value_));
    ImGui::SameLine();
    if (ImGui::Button(TR("Add")) && property_key_[0] != '\0') {
        profile_.driver_properties[property_key_] = property_value_;
        property_key_[0] = '\0';
        property_value_[0] = '\0';
    }

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_ssh() {
    ImGui::BeginChild("##ssh", ImVec2(0, -80));

    ImGui::Checkbox(TR("Use SSH tunnel"), &profile_.ssh.enabled);
    ImGui::TextColored(col4(colors().warn),
                       "Não implementado — a configuração é salva, mas o túnel "
                       "não é estabelecido.");
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssh.enabled);

    ImGui::SetNextItemWidth(320);
    input_string(TR("SSH host"), profile_.ssh.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port##ssh"), profile_.ssh.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("User##ssh"), profile_.ssh.user, 64);

    // Sem static nem constexpr: TR() resolve em runtime e o rotulo precisa
    // mudar quando o usuario troca de idioma.
    const char* const kAuthTypes[] = {
        TR("Password"), TR("Public key"), TR("SSH agent"),
    };
    int auth = static_cast<int>(profile_.ssh.auth);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Authentication##ssh"), &auth, kAuthTypes,
                     IM_ARRAYSIZE(kAuthTypes))) {
        profile_.ssh.auth = static_cast<db::SshAuthType>(auth);
    }

    if (profile_.ssh.auth == db::SshAuthType::password) {
        ImGui::SetNextItemWidth(320);
        input_string(TR("Password##ssh"), profile_.ssh.password, 128,
                     ImGuiInputTextFlags_Password);
    } else if (profile_.ssh.auth == db::SshAuthType::public_key) {
        ImGui::SetNextItemWidth(320);
        input_string(TR("Private key"), profile_.ssh.private_key_path, 260);
        ImGui::SetNextItemWidth(320);
        input_string("Passphrase", profile_.ssh.passphrase, 128,
                     ImGuiInputTextFlags_Password);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Timeout (s)##ssh"), profile_.ssh.connect_timeout);
    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Keep-alive (s)##ssh"), profile_.ssh.keep_alive);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_ssl() {
    ImGui::BeginChild("##ssl", ImVec2(0, -80));

    ImGui::Checkbox(TR("Use SSL"), &profile_.ssl.enabled);
    ImGui::TextColored(col4(colors().warn),
                       "Não implementado — o protocolo ainda não negocia TLS.");
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssl.enabled);

    static constexpr const char* kModes[] = {
        "disable", "allow", "prefer", "require", "verify-ca", "verify-full",
    };
    int mode = static_cast<int>(profile_.ssl.mode);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo(TR("Mode"), &mode, kModes, IM_ARRAYSIZE(kModes))) {
        profile_.ssl.mode = static_cast<db::SslMode>(mode);
    }
    help_marker("require exige criptografia; verify-ca valida o certificado do "
                "servidor; verify-full valida também o nome do host.");

    ImGui::SetNextItemWidth(400);
    input_string(TR("CA certificate"), profile_.ssl.root_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string(TR("Client certificate"), profile_.ssl.client_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string(TR("Client key"), profile_.ssl.client_key_path, 260);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_proxy() {
    ImGui::BeginChild("##proxy", ImVec2(0, -80));

    ImGui::Checkbox(TR("Use SOCKS proxy"), &profile_.proxy.enabled);
    ImGui::TextColored(col4(colors().warn), TR("Not implemented."));
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.proxy.enabled);

    ImGui::SetNextItemWidth(320);
    input_string(TR("Host##proxy"), profile_.proxy.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16(TR("Port##proxy"), profile_.proxy.port);

    ImGui::SetNextItemWidth(320);
    input_string(TR("User##proxy"), profile_.proxy.user, 64);
    ImGui::SetNextItemWidth(320);
    input_string(TR("Password##proxy"), profile_.proxy.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_initialization() {
    ImGui::BeginChild("##init", ImVec2(0, -80));

    ImGui::TextColored(col4(colors().data), TR("Transactions"));
    ImGui::Separator();

    ImGui::Checkbox(TR("Auto-commit"), &profile_.auto_commit);
    help_marker("Desligado, cada alteração exige commit explícito. "
                "Conexões de produção começam com auto-commit desligado.");

    ImGui::Checkbox(TR("Read-only connection"), &profile_.read_only);
    help_marker("Bloqueia INSERT, UPDATE, DELETE e DDL no cliente.");

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Session"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string(TR("Default schema"), profile_.default_schema, 64);
    help_marker("Define o search_path ao conectar.");

    ImGui::Text(TR("Initialization queries"));
    help_marker("Executadas na ordem, logo após a conexão ser estabelecida.");

    std::vector<char> buffer(
        std::max<std::size_t>(2048, profile_.bootstrap_queries.size() + 1), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s",
                  profile_.bootstrap_queries.c_str());
    if (ImGui::InputTextMultiline("##bootstrap", buffer.data(), buffer.size(),
                                  ImVec2(-1, 90))) {
        profile_.bootstrap_queries = buffer.data();
    }

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Connection"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    input_seconds(TR("Timeout (s)"), profile_.connect_timeout);

    ImGui::Checkbox(TR("Keep-alive"), &profile_.keep_alive);
    if (profile_.keep_alive) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        input_seconds(TR("Interval (s)"), profile_.keep_alive_interval);
    }

    ImGui::Checkbox(TR("Close idle connections"), &profile_.close_idle_connections);

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_general() {
    ImGui::BeginChild("##general", ImVec2(0, -80));

    ImGui::TextColored(col4(colors().data), TR("Identification"));
    ImGui::Separator();

    ImGui::SetNextItemWidth(360);
    input_string(TR("Connection name"), profile_.name, 128);
    help_marker("Vazio usa \"banco@host\".");

    ImGui::SetNextItemWidth(360);
    input_string(TR("Description"), profile_.description, 256);

    ImGui::SetNextItemWidth(260);
    input_string(TR("Folder"), profile_.folder, 128);
    help_marker("Agrupa a conexão na árvore. Use / para subpastas.");

    ImGui::Spacing();
    ImGui::TextColored(col4(colors().data), TR("Connection type"));
    ImGui::Separator();

    for (int i = 0; i < 3; ++i) {
        const auto type = static_cast<db::ConnectionType>(i);
        const db::ConnectionTypeInfo& info = db::connection_type_info(type);

        ImGui::PushStyleColor(ImGuiCol_Text, info.color);
        if (ImGui::RadioButton(info.name, profile_.type == type)) {
            profile_.type = type;
            // Produção começa sem auto-commit: alteração acidental exige
            // commit explícito para virar permanente.
            profile_.auto_commit = info.auto_commit;
        }
        ImGui::PopStyleColor();
        if (i < 2) ImGui::SameLine();
    }

    const db::ConnectionTypeInfo& current = db::connection_type_info(profile_.type);
    ImGui::Spacing();
    ImGui::TextColored(col4(colors().text_dim),
                       "auto-commit: %s | confirmar execução: %s | "
                       "confirmar alteração de dados: %s",
                       current.auto_commit ? "ligado" : "desligado",
                       current.confirm_execute ? "sim" : "não",
                       current.confirm_data_change ? "sim" : "não");

    ImGui::EndChild();
}

} // namespace otter::ui


