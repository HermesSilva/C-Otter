#include "ui/connection_dialog.hpp"
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
    ImGui::TextColored(col4(palette::text_dim), "(?)");
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

constexpr std::array<const char*, 8> kCategories = {
    "Todos", "Popular", "SQL", "NoSQL", "Analítico",
    "Arquivos", "Embarcado", "Séries temporais",
};

} // namespace

// Catalogo espelhando o do DBeaver. Os indisponiveis aparecem esmaecidos com o
// motivo -- esconder a lista inteira daria a impressao de que o produto so'
// fala com PostgreSQL por design.
std::vector<DriverEntry> driver_catalog() {
    return {
        {"postgresql", "PostgreSQL", "Popular", 5432, true, nullptr},
        {"mysql",      "MySQL",      "Popular", 3306, false, "protocolo em desenvolvimento"},
        {"mariadb",    "MariaDB",    "Popular", 3306, false, "protocolo em desenvolvimento"},
        {"sqlite",     "SQLite",     "Embarcado", 0,  false, "planejado para a fase 3"},
        {"mssql",      "SQL Server", "Popular", 1433, false, "TDS planejado para a fase 3"},
        {"oracle",     "Oracle",     "Popular", 1521, false, "planejado para a fase 3"},
        {"db2",        "Db2 for LUW","SQL",    50000, false, "fora do escopo da v1"},
        {"clickhouse", "ClickHouse", "Analítico", 8123, false, "fora do escopo da v1"},
        {"duckdb",     "DuckDB",     "Analítico", 0,  false, "fora do escopo da v1"},
        {"snowflake",  "Snowflake",  "Analítico", 443, false, "fora do escopo da v1"},
        {"h2",         "H2",         "Embarcado", 8082, false, "fora do escopo da v1"},
        {"firebird",   "Firebird",   "SQL",     3050, false, "fora do escopo da v1"},
        {"csv",        "CSV",        "Arquivos", 0,   false, "fora do escopo da v1"},
        {"mongodb",    "MongoDB",    "NoSQL",  27017, false, "fora do escopo da v1"},
        {"redis",      "Redis",      "NoSQL",   6379, false, "fora do escopo da v1"},
        {"cassandra",  "Cassandra",  "NoSQL",   9042, false, "fora do escopo da v1"},
        {"influxdb",   "InfluxDB",   "Séries temporais", 8086, false, "fora do escopo da v1"},
        {"timescale",  "TimescaleDB","Séries temporais", 5432, false, "usa o driver PostgreSQL"},
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

    const char* title = editing_ ? "Editar conexão###ConnDialog"
                                 : "Nova conexão###ConnDialog";

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
    ImGui::TextColored(col4(palette::fur_light), "Selecione o banco de dados");
    ImGui::TextColored(col4(palette::text_dim),
                       "Escolha o driver para a nova conexão.");
    ImGui::Separator();

    // Coluna de categorias, como no DBeaver.
    ImGui::BeginChild("##categories", ImVec2(150, -46), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(kCategories.size()); ++i) {
        if (ImGui::Selectable(kCategories[static_cast<std::size_t>(i)],
                              category_index_ == i)) {
            category_index_ = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##drivers", ImVec2(0, -46));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Filtrar drivers...",
                             driver_filter_, sizeof(driver_filter_));
    ImGui::Separator();

    const std::string_view category =
        kCategories[static_cast<std::size_t>(category_index_)];

    if (ImGui::BeginTable("##driverlist", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Driver", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Categoria", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthFixed, 210.0f);

        for (const DriverEntry& driver : driver_catalog()) {
            if (category != "Todos" && category != driver.category) continue;
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
            ImGui::TextColored(col4(palette::text_dim), "%s", driver.category);

            ImGui::TableSetColumnIndex(2);
            if (driver.available) {
                ImGui::TextColored(col4(palette::ok), "disponível");
            } else {
                ImGui::TextColored(col4(palette::text_dim), "%s", driver.note);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::Separator();

    const bool can_advance = profile_.driver_id == "postgresql";
    ImGui::BeginDisabled(!can_advance);
    if (ImGui::Button("Avançar >", ImVec2(110, 0))) step_ = Step::configure;
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(110, 0))) visible_ = false;

    if (!can_advance) {
        ImGui::SameLine();
        ImGui::TextColored(col4(palette::text_dim),
                           "  selecione um driver disponível");
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
    ImGui::TextColored(col4(palette::text_dim), " | %s",
                       profile_.effective_name().c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (ImGui::BeginTabBar("##conntabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Principal")) {
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
        if (ImGui::BeginTabItem("SSH")) {
            draw_tab_ssh();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("SSL")) {
            draw_tab_ssl();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Proxy")) {
            draw_tab_proxy();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Inicialização")) {
            draw_tab_initialization();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Geral")) {
            draw_tab_general();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Rodapé fixo com as ações.
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 72.0f);
    ImGui::Separator();

    if (!editing_) {
        if (ImGui::Button("< Voltar", ImVec2(100, 0))) step_ = Step::select_driver;
        ImGui::SameLine();
    }

    ImGui::BeginDisabled(feedback.busy);
    if (ImGui::Button("Testar conexão", ImVec2(130, 0)) && on_connect_) {
        on_connect_(profile_);
    }
    ImGui::SameLine();
    if (ImGui::Button(editing_ ? "Salvar" : "Concluir", ImVec2(100, 0))) {
        if (on_save_) on_save_(profile_);
        if (on_connect_) on_connect_(profile_);
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(100, 0))) visible_ = false;

    if (feedback.busy) {
        ImGui::SameLine();
        const float t = static_cast<float>(ImGui::GetTime());
        ImVec4 pulse = col4(palette::data_light);
        pulse.w = 0.4f + 0.6f * std::abs(std::sin(t * 3.0f));
        ImGui::TextColored(pulse, "  ● conectando...");
    } else if (feedback.failed) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(col4(palette::error), "%s", feedback.message.c_str());
        ImGui::PopTextWrapPos();
    } else if (feedback.succeeded) {
        ImGui::TextColored(col4(palette::ok), "%s", feedback.message.c_str());
    }
}

void ConnectionDialog::draw_tab_main() {
    ImGui::BeginChild("##main", ImVec2(0, -80));

    ImGui::TextColored(col4(palette::data), "Servidor");
    ImGui::Separator();

    ImGui::SetNextItemWidth(320);
    input_string("Host", profile_.host, 128);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16("Porta", profile_.port);

    ImGui::SetNextItemWidth(320);
    input_string("Banco de dados", profile_.database, 128);

    ImGui::Spacing();
    ImGui::TextColored(col4(palette::data), "Autenticação");
    ImGui::Separator();

    static constexpr const char* kAuthModels[] = {
        "Banco de dados nativo", "Sem autenticação", "Ident / Peer",
        "Kerberos", "AWS IAM",
    };
    int auth = static_cast<int>(profile_.auth_model);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Método", &auth, kAuthModels,
                     IM_ARRAYSIZE(kAuthModels))) {
        profile_.auth_model = static_cast<db::AuthModel>(auth);
    }

    const bool needs_credentials =
        profile_.auth_model == db::AuthModel::database_native;

    ImGui::BeginDisabled(!needs_credentials);
    ImGui::SetNextItemWidth(320);
    input_string("Usuário", profile_.user, 128);

    ImGui::SetNextItemWidth(320);
    input_string("Senha", profile_.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::Checkbox("Salvar senha", &profile_.save_password);
    help_marker("A senha é guardada no cofre do sistema operacional "
                "(DPAPI no Windows). Ainda não implementado — a senha só "
                "vive nesta sessão.");
    ImGui::EndDisabled();

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_postgres() {
    ImGui::BeginChild("##pg", ImVec2(0, -80));

    ImGui::TextColored(col4(palette::data), "Navegador");
    ImGui::Separator();

    ImGui::Checkbox("Mostrar todos os bancos",
                    &profile_.postgres.show_non_default_databases);
    help_marker("Lista todos os bancos do servidor, não apenas o conectado.");

    ImGui::Checkbox("Mostrar bancos template",
                    &profile_.postgres.show_template_databases);
    help_marker("Inclui template0 e template1.");

    ImGui::Checkbox("Mostrar bancos sem acesso",
                    &profile_.postgres.show_unavailable_databases);
    help_marker("Inclui bancos aos quais o usuário não tem permissão de conectar.");

    ImGui::Spacing();
    ImGui::TextColored(col4(palette::data), "Desempenho");
    ImGui::Separator();

    ImGui::Checkbox("Ler estatísticas de tamanho",
                    &profile_.postgres.show_database_statistics);
    help_marker("Calcula o tamanho em disco de tabelas e índices. Em bancos "
                "muito grandes, torna a expansão da árvore mais lenta.");

    ImGui::Checkbox("Ler todos os tipos de dado",
                    &profile_.postgres.read_all_data_types);
    help_marker("Inclui tipos raros e de sistema. Deixa o carregamento de "
                "metadados mais lento.");

    ImGui::Checkbox("Ler colunas das chaves",
                    &profile_.postgres.read_keys_with_columns);
    help_marker("Carrega as colunas de cada chave junto com a chave. Útil "
                "para inferência de JOIN; custa uma consulta a mais.");

    ImGui::Checkbox("Usar prepared statements",
                    &profile_.postgres.use_prepared_statements);

    ImGui::Spacing();
    ImGui::TextColored(col4(palette::data), "SQL");
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string("Role da sessão", profile_.postgres.session_role, 64);
    help_marker("Executa SET ROLE ao abrir a conexão.");

    ImGui::Checkbox("Substituir fuso horário legado",
                    &profile_.postgres.replace_legacy_timezone);
    help_marker("Converte timestamptz do formato antigo para o atual.");

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_driver_properties() {
    ImGui::BeginChild("##driverprops", ImVec2(0, -80));

    ImGui::TextColored(col4(palette::data), "Propriedades do driver");
    ImGui::TextColored(col4(palette::text_dim),
                       "Parâmetros passados diretamente ao driver na conexão.");
    ImGui::Separator();

    if (ImGui::BeginTable("##props", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Propriedade");
        ImGui::TableSetupColumn("Valor");
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
    ImGui::InputTextWithHint("##newkey", "propriedade",
                             property_key_, sizeof(property_key_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##newvalue", "valor",
                             property_value_, sizeof(property_value_));
    ImGui::SameLine();
    if (ImGui::Button("Adicionar") && property_key_[0] != '\0') {
        profile_.driver_properties[property_key_] = property_value_;
        property_key_[0] = '\0';
        property_value_[0] = '\0';
    }

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_ssh() {
    ImGui::BeginChild("##ssh", ImVec2(0, -80));

    ImGui::Checkbox("Usar túnel SSH", &profile_.ssh.enabled);
    ImGui::TextColored(col4(palette::warn),
                       "Não implementado — a configuração é salva, mas o túnel "
                       "não é estabelecido.");
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssh.enabled);

    ImGui::SetNextItemWidth(320);
    input_string("Host SSH", profile_.ssh.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16("Porta##ssh", profile_.ssh.port);

    ImGui::SetNextItemWidth(320);
    input_string("Usuário##ssh", profile_.ssh.user, 64);

    static constexpr const char* kAuthTypes[] = {
        "Senha", "Chave pública", "Agente SSH",
    };
    int auth = static_cast<int>(profile_.ssh.auth);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Autenticação##ssh", &auth, kAuthTypes,
                     IM_ARRAYSIZE(kAuthTypes))) {
        profile_.ssh.auth = static_cast<db::SshAuthType>(auth);
    }

    if (profile_.ssh.auth == db::SshAuthType::password) {
        ImGui::SetNextItemWidth(320);
        input_string("Senha##ssh", profile_.ssh.password, 128,
                     ImGuiInputTextFlags_Password);
    } else if (profile_.ssh.auth == db::SshAuthType::public_key) {
        ImGui::SetNextItemWidth(320);
        input_string("Chave privada", profile_.ssh.private_key_path, 260);
        ImGui::SetNextItemWidth(320);
        input_string("Passphrase", profile_.ssh.passphrase, 128,
                     ImGuiInputTextFlags_Password);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(120);
    input_seconds("Timeout (s)##ssh", profile_.ssh.connect_timeout);
    ImGui::SetNextItemWidth(120);
    input_seconds("Keep-alive (s)##ssh", profile_.ssh.keep_alive);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_ssl() {
    ImGui::BeginChild("##ssl", ImVec2(0, -80));

    ImGui::Checkbox("Usar SSL", &profile_.ssl.enabled);
    ImGui::TextColored(col4(palette::warn),
                       "Não implementado — o protocolo ainda não negocia TLS.");
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.ssl.enabled);

    static constexpr const char* kModes[] = {
        "disable", "allow", "prefer", "require", "verify-ca", "verify-full",
    };
    int mode = static_cast<int>(profile_.ssl.mode);
    ImGui::SetNextItemWidth(220);
    if (ImGui::Combo("Modo", &mode, kModes, IM_ARRAYSIZE(kModes))) {
        profile_.ssl.mode = static_cast<db::SslMode>(mode);
    }
    help_marker("require exige criptografia; verify-ca valida o certificado do "
                "servidor; verify-full valida também o nome do host.");

    ImGui::SetNextItemWidth(400);
    input_string("Certificado da CA", profile_.ssl.root_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string("Certificado do cliente", profile_.ssl.client_cert_path, 260);
    ImGui::SetNextItemWidth(400);
    input_string("Chave do cliente", profile_.ssl.client_key_path, 260);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_proxy() {
    ImGui::BeginChild("##proxy", ImVec2(0, -80));

    ImGui::Checkbox("Usar proxy SOCKS", &profile_.proxy.enabled);
    ImGui::TextColored(col4(palette::warn), "Não implementado.");
    ImGui::Separator();

    ImGui::BeginDisabled(!profile_.proxy.enabled);

    ImGui::SetNextItemWidth(320);
    input_string("Host##proxy", profile_.proxy.host, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    input_uint16("Porta##proxy", profile_.proxy.port);

    ImGui::SetNextItemWidth(320);
    input_string("Usuário##proxy", profile_.proxy.user, 64);
    ImGui::SetNextItemWidth(320);
    input_string("Senha##proxy", profile_.proxy.password, 128,
                 ImGuiInputTextFlags_Password);

    ImGui::EndDisabled();
    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_initialization() {
    ImGui::BeginChild("##init", ImVec2(0, -80));

    ImGui::TextColored(col4(palette::data), "Transações");
    ImGui::Separator();

    ImGui::Checkbox("Auto-commit", &profile_.auto_commit);
    help_marker("Desligado, cada alteração exige commit explícito. "
                "Conexões de produção começam com auto-commit desligado.");

    ImGui::Checkbox("Conexão somente leitura", &profile_.read_only);
    help_marker("Bloqueia INSERT, UPDATE, DELETE e DDL no cliente.");

    ImGui::Spacing();
    ImGui::TextColored(col4(palette::data), "Sessão");
    ImGui::Separator();

    ImGui::SetNextItemWidth(260);
    input_string("Schema padrão", profile_.default_schema, 64);
    help_marker("Define o search_path ao conectar.");

    ImGui::Text("Consultas de inicialização");
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
    ImGui::TextColored(col4(palette::data), "Conexão");
    ImGui::Separator();

    ImGui::SetNextItemWidth(120);
    input_seconds("Timeout (s)", profile_.connect_timeout);

    ImGui::Checkbox("Keep-alive", &profile_.keep_alive);
    if (profile_.keep_alive) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        input_seconds("Intervalo (s)", profile_.keep_alive_interval);
    }

    ImGui::Checkbox("Fechar conexões ociosas", &profile_.close_idle_connections);

    ImGui::EndChild();
}

void ConnectionDialog::draw_tab_general() {
    ImGui::BeginChild("##general", ImVec2(0, -80));

    ImGui::TextColored(col4(palette::data), "Identificação");
    ImGui::Separator();

    ImGui::SetNextItemWidth(360);
    input_string("Nome da conexão", profile_.name, 128);
    help_marker("Vazio usa \"banco@host\".");

    ImGui::SetNextItemWidth(360);
    input_string("Descrição", profile_.description, 256);

    ImGui::SetNextItemWidth(260);
    input_string("Pasta", profile_.folder, 128);
    help_marker("Agrupa a conexão na árvore. Use / para subpastas.");

    ImGui::Spacing();
    ImGui::TextColored(col4(palette::data), "Tipo de conexão");
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
    ImGui::TextColored(col4(palette::text_dim),
                       "auto-commit: %s | confirmar execução: %s | "
                       "confirmar alteração de dados: %s",
                       current.auto_commit ? "ligado" : "desligado",
                       current.confirm_execute ? "sim" : "não",
                       current.confirm_data_change ? "sim" : "não");

    ImGui::EndChild();
}

} // namespace otter::ui
