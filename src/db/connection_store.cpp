#include "db/connection_store.hpp"

#include "base/aes.hpp"
#include "base/json.hpp"
#include "base/paths.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace otter::db {
namespace {

namespace fs = std::filesystem;

// Chave do `credentials-config.json` do DBeaver.
//
// Constante publica no codigo-fonte dele
// (BaseProjectImpl.java, LOCAL_KEY_CACHE). Nao protege contra ninguem com
// acesso ao arquivo -- o ADR 0012 registra por que aceitamos e o que a
// interface diz ao usuario.
constexpr crypto::AesKey kDBeaverKey = {
    0xBA, 0xBB, 0x4A, 0x9F, 0x77, 0x4A, 0xB8, 0x53,
    0xC9, 0x6C, 0x2D, 0x65, 0x3D, 0xFE, 0x54, 0x4A,
};

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Status write_file(const fs::path& path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Grava num temporario e renomeia: um crash no meio da escrita deixaria o
    // arquivo truncado, e com ele o usuario perderia todas as conexoes.
    const fs::path temporary = fs::path(path).concat(".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return fail(Errc::io_error,
                        "cannot write " + temporary.string());
        }
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!file) {
            return fail(Errc::io_error, "write failed: " + temporary.string());
        }
    }

    fs::rename(temporary, path, ec);
    if (ec) {
        // Em Windows, rename sobre arquivo existente falha; remove e repete.
        fs::remove(path, ec);
        fs::rename(temporary, path, ec);
        if (ec) {
            return fail(Errc::io_error,
                        "cannot replace " + path.string() + ": " + ec.message());
        }
    }
    return {};
}

std::string env_or_empty(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string{};
}

fs::path user_config_root() {
#ifdef _WIN32
    const std::string appdata = env_or_empty("APPDATA");
    if (!appdata.empty()) return fs::path(appdata);
    return fs::path(env_or_empty("USERPROFILE")) / "AppData" / "Roaming";
#else
    const std::string xdg = env_or_empty("XDG_CONFIG_HOME");
    if (!xdg.empty()) return fs::path(xdg);
    return fs::path(env_or_empty("HOME")) / ".config";
#endif
}

StoreLocation location_from_directory(const fs::path& directory) {
    StoreLocation location;
    location.directory    = directory.string();
    location.data_sources = (directory / "data-sources.json").string();
    location.credentials  = (directory / "credentials-config.json").string();
    return location;
}

// --- Mapeamento driver do DBeaver -> driver do C-Otter ----------------------
//
// O `provider` do DBeaver e' o identificador estavel ("postgresql", "mysql");
// o `driver` distingue versoes ("mysql8", "postgres-jdbc"). Mapeamos pelo
// provider, que e' o que importa para escolher o protocolo.
struct DriverMapping {
    std::string_view provider;
    std::string_view otter_driver;
    std::string_view reason_when_missing;
};

constexpr DriverMapping kDriverMappings[] = {
    {"postgresql", "postgresql", ""},
    {"mysql",      "mysql", ""},
    {"mariadb",    "mysql", ""},   // mesmo protocolo, mesmo driver
    {"sqlite",     "",  "SQLite driver is not implemented yet"},
    {"generic",    "",  "generic JDBC has no equivalent without a JVM"},
    {"oracle",     "",  "Oracle driver is planned for a later phase"},
    {"mssql",      "sqlserver", ""},
    {"sqlserver",  "sqlserver", ""},
    {"sqlanywhere", "sqlanywhere", ""},
};

// Os drivers Sybase do DBeaver ("sybase_jtds", "sybase_jconn" e "sypase_jconn"
// -- o erro de grafia e' do plugin.xml dele) moram no provider "mssql", junto
// do SQL Server, mas falam TDS 5.0, nao o 7.x. O que decide o protocolo aqui
// e' o DRIVER: mapea-los pelo provider os mandaria ao driver do SQL Server,
// que nem passa do login.
bool is_sybase_driver(std::string_view driver) noexcept {
    return driver.starts_with("sybase") || driver.starts_with("sypase");
}

void apply_driver(StoredProfile& stored) {
    if (is_sybase_driver(stored.driver)) {
        stored.supported = true;
        stored.profile.driver_id = "sqlanywhere";
        return;
    }

    for (const DriverMapping& mapping : kDriverMappings) {
        if (stored.provider != mapping.provider) continue;

        if (mapping.otter_driver.empty()) {
            stored.supported = false;
            stored.unsupported_reason = std::string(mapping.reason_when_missing);
        } else {
            stored.supported = true;
            stored.profile.driver_id = std::string(mapping.otter_driver);
        }
        return;
    }

    // Texto fixo, sem interpolar o nome: a mensagem e' chave de traducao
    // (diretiva 8), e uma chave montada em tempo de execucao nunca casaria
    // com o catalogo. O provider ja' aparece na coluna Driver da tabela.
    stored.supported = false;
    stored.unsupported_reason = "this database is not supported yet";
}

// PostgreConstants.PROP_SHOW_* do DBeaver ("@dbeaver-" + nome + "@").
constexpr const char* kShowNonDefaultDb  = "@dbeaver-show-non-default-db@";
constexpr const char* kShowTemplateDb    = "@dbeaver-show-template-db@";
constexpr const char* kShowUnavailableDb = "@dbeaver-show-unavailable-db@";
// PostgreConstants.PROP_CHOSEN_ROLE: a role do "SET ROLE" ao conectar.
constexpr const char* kChosenRole        = "@dbeaver-chosen-role@";
// SQLServerConstants.PROP_AUTHENTICATION: "SQL_SERVER_PASSWORD" ou
// "WINDOWS_INTEGRATED" (os nomes do enum SQLServerAuthentication).
constexpr const char* kMssqlAuthentication = "@dbeaver-authentication@";

ConnectionType type_from_string(std::string_view text) noexcept {
    if (text == "prod" || text == "production") return ConnectionType::production;
    if (text == "test")                         return ConnectionType::test;
    return ConnectionType::development;
}

std::string_view type_to_string(ConnectionType type) noexcept {
    switch (type) {
        case ConnectionType::production:  return "prod";
        case ConnectionType::test:        return "test";
        case ConnectionType::development: break;
    }
    return "dev";
}

// Id no estilo do DBeaver: "<driver>-<timestamp em hex>-<aleatorio>".
//
// O formato importa pouco; ser unico e estavel importa muito, porque e' a
// chave que liga a conexao a' sua senha no outro arquivo.
std::string generate_id(std::string_view driver) {
    static std::mt19937_64 generator{std::random_device{}()};

    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*s-%llx-%llx",
                  static_cast<int>(driver.size()), driver.data(),
                  static_cast<unsigned long long>(generator() & 0xFFFFFFFFFFFF),
                  static_cast<unsigned long long>(generator() & 0xFFFFFFFF));
    return buffer;
}

StoredProfile profile_from_json(const std::string& id,
                                const json::Value& node) {
    StoredProfile stored;
    stored.id       = id;
    stored.provider = std::string(node["provider"].as_string());
    stored.driver   = std::string(node["driver"].as_string());

    ConnectionProfile& profile = stored.profile;
    profile.id   = id;
    profile.name = std::string(node["name"].as_string());
    profile.description = std::string(node["description"].as_string());
    profile.folder      = std::string(node["folder"].as_string());
    profile.save_password = node["save-password"].as_bool();
    profile.isolation_level =
        static_cast<int>(node["otter-isolation"].as_number(-1));

    // Preferencias do editor. Ausentes -- num perfil importado do DBeaver,
    // ou gravado antes desta versao -- ficam no padrao de EditorOptions.
    if (const json::Value& ed = node["otter-editor"]; ed.is_object()) {
        EditorOptions& editor = profile.editor;
        const EditorOptions defaults;

        editor.keyword_case = static_cast<int>(
            ed["keyword-case"].as_number(defaults.keyword_case));
        editor.indent_width = static_cast<int>(
            ed["indent-width"].as_number(defaults.indent_width));
        editor.river_style = ed["river-style"].as_bool(defaults.river_style);
        editor.wrap_select_after = static_cast<std::size_t>(
            ed["wrap-select-after"].as_number(
                static_cast<double>(defaults.wrap_select_after)));

        editor.complete_on_typing =
            ed["complete-on-typing"].as_bool(defaults.complete_on_typing);
        editor.complete_in_comments =
            ed["complete-in-comments"].as_bool(defaults.complete_in_comments);
        editor.complete_in_strings =
            ed["complete-in-strings"].as_bool(defaults.complete_in_strings);
        editor.auto_insert_single =
            ed["auto-insert-single"].as_bool(defaults.auto_insert_single);
        editor.complete_delay_ms = static_cast<int>(
            ed["complete-delay-ms"].as_number(defaults.complete_delay_ms));

        editor.tab_size = static_cast<int>(
            ed["tab-size"].as_number(defaults.tab_size));
        editor.show_line_numbers =
            ed["show-line-numbers"].as_bool(defaults.show_line_numbers);
        editor.auto_indent = ed["auto-indent"].as_bool(defaults.auto_indent);
        editor.show_matching_brackets =
            ed["show-matching-brackets"].as_bool(defaults.show_matching_brackets);
        editor.show_whitespace =
            ed["show-whitespace"].as_bool(defaults.show_whitespace);
        editor.page_size = static_cast<int>(
            ed["page-size"].as_number(defaults.page_size));
        editor.hex_limit_kb = static_cast<int>(
            ed["hex-limit-kb"].as_number(defaults.hex_limit_kb));
        editor.export_format = static_cast<int>(
            ed["export-format"].as_number(defaults.export_format));
        editor.export_write_header =
            ed["export-write-header"].as_bool(defaults.export_write_header);
        editor.export_null_text =
            std::string(ed["export-null-text"].as_string());

        if (const json::Value& nt = ed["null-text"];
            nt.kind() == json::Kind::string) {
            editor.null_text = std::string(nt.as_string());
        }
        editor.align_numbers_right =
            ed["align-numbers-right"].as_bool(defaults.align_numbers_right);
        editor.word_wrap = ed["word-wrap"].as_bool(defaults.word_wrap);
        editor.code_folding = ed["code-folding"].as_bool(defaults.code_folding);
        editor.stop_script_on_error =
            ed["stop-script-on-error"].as_bool(defaults.stop_script_on_error);
    }

    const json::Value& config = node["configuration"];
    profile.host     = std::string(config["host"].as_string("localhost"));
    profile.database = std::string(config["database"].as_string());
    profile.url      = std::string(config["url"].as_string());
    profile.user     = std::string(config["user"].as_string());
    profile.type     = type_from_string(config["type"].as_string("dev"));

    // A porta vem como string no DBeaver; as_int aceita as duas formas.
    const std::int64_t port = config["port"].as_int(0);
    if (port > 0 && port <= 65535) {
        profile.port = static_cast<std::uint16_t>(port);
    }

    profile.close_idle_connections = config["closeIdleConnection"].as_bool();
    profile.read_only              = config["read-only"].as_bool();
    if (config["auto-commit"].kind() == json::Kind::boolean) {
        profile.auto_commit = config["auto-commit"].as_bool();
    }

    // RegistryConstants do DBeaver: "keepAlive" e "closeIdle" em SEGUNDOS;
    // keepAlive > 0 e' o que liga o ping (la' nao ha' caixa separada).
    if (const std::int64_t seconds = config["keepAlive"].as_int(0); seconds > 0) {
        profile.keep_alive          = true;
        profile.keep_alive_interval = std::chrono::seconds(seconds);
    }
    if (const std::int64_t seconds = config["closeIdle"].as_int(0); seconds > 0) {
        profile.close_idle_interval = std::chrono::seconds(seconds);
    }

    // "bootstrap": o que roda ao conectar. Estes campos existiam no dialogo e
    // NAO eram gravados -- o schema padrao e as consultas de inicializacao
    // morriam ao fechar o programa (diretiva 6).
    if (const json::Value& bootstrap = config["bootstrap"]; bootstrap.is_object()) {
        if (bootstrap["autocommit"].kind() == json::Kind::boolean) {
            profile.auto_commit = bootstrap["autocommit"].as_bool();
        }
        profile.default_schema =
            std::string(bootstrap["defaultSchema"].as_string());
        if (profile.default_schema.empty()) {
            // No MySQL o DBeaver grava o banco padrao como catalogo.
            profile.default_schema =
                std::string(bootstrap["defaultCatalog"].as_string());
        }
        profile.ignore_bootstrap_errors = bootstrap["ignoreErrors"].as_bool();

        std::string queries;
        for (const json::Value& query : bootstrap["query"].as_array()) {
            if (!queries.empty()) queries += '\n';
            queries += query.as_string();
        }
        profile.bootstrap_queries = std::move(queries);
    }

    for (const auto& [key, value] : config["properties"].as_object()) {
        profile.driver_properties[key] = std::string(value.as_string());
    }

    // Opcoes da arvore do PostgreSQL. As chaves e os valores ("true" como
    // TEXTO) sao os do DBeaver -- PostgreConstants.PROP_SHOW_*, conferidos num
    // data-sources.json real --, para que um perfil importado traga
    // "Show all databases" como estava la'.
    //
    // Nao eram lidas nem gravadas: as caixas do dialogo existiam e o valor
    // morria ao fechar o programa (diretiva 6).
    const json::Value& provider = config["provider-properties"];
    const auto provider_flag = [&provider](const char* key) {
        return provider[key].as_string() == "true";
    };
    profile.postgres.show_non_default_databases =
        provider_flag(kShowNonDefaultDb);
    profile.postgres.show_template_databases = provider_flag(kShowTemplateDb);
    profile.postgres.show_unavailable_databases =
        provider_flag(kShowUnavailableDb);
    profile.postgres.session_role =
        std::string(provider[kChosenRole].as_string());

    // SQL Server: autenticacao do Windows. Sem a propriedade (perfil vindo do
    // SSMS, ou gravado antes dela existir), usuario vazio quer dizer a conta
    // do Windows -- nao ha' login por senha sem usuario.
    if ((stored.provider == "sqlserver" || stored.provider == "mssql") &&
        !is_sybase_driver(stored.driver)) {
        const std::string_view authentication =
            provider[kMssqlAuthentication].as_string();
        if (authentication == "WINDOWS_INTEGRATED" ||
            (authentication.empty() && profile.user.empty())) {
            profile.auth_model = AuthModel::windows;
        }
    }

    // SSL. No DBeaver e' um "handler" de rede, com id por driver e chaves
    // DIFERENTES em cada um -- extraido de PostgreConstants.java e
    // MySQLConstants.java, registrado em docs/DBEAVER-MAP.md.
    //
    //   postgre_ssl -> "sslMode": disable|require|verify-ca|verify-full
    //   mysql_ssl   -> "ssl.require" e "ssl.verify.server", dois booleanos
    //
    // O `enabled` do handler manda: com ele falso, o modo gravado e' resto de
    // uma configuracao desligada e nao deve exigir TLS.
    const json::Value& handlers = config["handlers"];
    for (const char* handler_id : {"postgre_ssl", "mysql_ssl"}) {
        const json::Value& handler = handlers[handler_id];
        if (handler.kind() != json::Kind::object) continue;

        profile.ssl.enabled = handler["enabled"].as_bool();

        const json::Value& properties = handler["properties"];
        if (const std::string_view mode = properties["sslMode"].as_string();
            !mode.empty()) {
            profile.ssl.mode = ssl_mode_from_string(mode);
        } else if (properties["ssl.verify.server"].as_bool()) {
            profile.ssl.mode = SslMode::verify_full;
        } else {
            profile.ssl.mode = SslMode::require;
        }

        profile.ssl.root_cert_path =
            std::string(properties["ssl.ca.cert"].as_string());
        profile.ssl.client_cert_path =
            std::string(properties["ssl.client.cert"].as_string());
        profile.ssl.client_key_path =
            std::string(properties["ssl.client.key"].as_string());
        break;
    }

    // Tunel SSH e proxy SOCKS: handlers "ssh_tunnel" e "socks_proxy", com as
    // chaves de SSHConstants / SocksConstants. As abas existiam, a conexao as
    // usava, e o perfil era gravado SEM elas -- o tunel sumia ao reabrir.
    if (const json::Value& ssh = handlers["ssh_tunnel"]; ssh.is_object()) {
        const json::Value& properties = ssh["properties"];
        profile.ssh.enabled = ssh["enabled"].as_bool();
        profile.ssh.host    = std::string(properties["host"].as_string());
        if (const std::int64_t value = properties["port"].as_int(0);
            value > 0 && value <= 65535) {
            profile.ssh.port = static_cast<std::uint16_t>(value);
        }
        profile.ssh.user          = std::string(ssh["user"].as_string());
        profile.ssh.save_password = ssh["save-password"].as_bool();

        const std::string_view auth = properties["authType"].as_string();
        profile.ssh.auth = auth == "PUBLIC_KEY" ? SshAuthType::public_key
                         : auth == "AGENT"      ? SshAuthType::agent
                                                : SshAuthType::password;
        profile.ssh.private_key_path =
            std::string(properties["keyPath"].as_string());

        // Milissegundos no DBeaver, segundos no perfil.
        if (const std::int64_t ms = properties["aliveInterval"].as_int(0); ms > 0) {
            profile.ssh.keep_alive = std::chrono::seconds(ms / 1000);
        }
        if (const std::int64_t ms = properties["sshConnectTimeout"].as_int(0);
            ms > 0) {
            profile.ssh.connect_timeout = std::chrono::seconds(ms / 1000);
        }
    }
    if (const json::Value& socks = handlers["socks_proxy"]; socks.is_object()) {
        const json::Value& properties = socks["properties"];
        profile.proxy.enabled = socks["enabled"].as_bool();
        profile.proxy.host = std::string(properties["socks-host"].as_string());
        if (const std::int64_t value = properties["socks-port"].as_int(0);
            value > 0 && value <= 65535) {
            profile.proxy.port = static_cast<std::uint16_t>(value);
        }
        profile.proxy.user = std::string(socks["user"].as_string());
    }

    apply_driver(stored);

    // Guarda o no' inteiro: regravar um perfil importado nao deve apagar
    // campos de uma versao do DBeaver mais nova que a nossa.
    stored.raw_json = json::serialize(node, 0);
    return stored;
}

json::Value profile_to_json(const StoredProfile& stored) {
    const ConnectionProfile& profile = stored.profile;

    json::Object config;
    config["host"] = json::Value(profile.host);
    config["port"] = json::Value(std::to_string(profile.port));
    if (!profile.database.empty()) {
        config["database"] = json::Value(profile.database);
    }
    if (!profile.user.empty()) config["user"] = json::Value(profile.user);
    config["type"] = json::Value(std::string(type_to_string(profile.type)));
    config["configurationType"] = json::Value(std::string("MANUAL"));
    config["auth-model"] = json::Value(std::string("native"));
    config["auto-commit"] = json::Value(profile.auto_commit);
    if (profile.read_only) config["read-only"] = json::Value(true);
    if (profile.close_idle_connections) {
        config["closeIdleConnection"] = json::Value(true);
        config["closeIdle"] = json::Value(
            static_cast<double>(profile.close_idle_interval.count()));
    }
    if (profile.keep_alive && profile.keep_alive_interval.count() > 0) {
        config["keepAlive"] = json::Value(
            static_cast<double>(profile.keep_alive_interval.count()));
    }

    // "bootstrap", no formato de DataSourceSerializerModern. So' quando ha' o
    // que dizer, como o DBeaver (bootstrap.hasData()).
    {
        json::Object bootstrap;
        if (!profile.default_schema.empty()) {
            bootstrap[stored.provider == "mysql" ? "defaultCatalog"
                                                 : "defaultSchema"] =
                json::Value(profile.default_schema);
        }
        if (profile.ignore_bootstrap_errors) {
            bootstrap["ignoreErrors"] = json::Value(true);
        }

        json::Array queries;
        std::string_view rest = profile.bootstrap_queries;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            std::string_view line =
                rest.substr(0, end == std::string_view::npos ? rest.size() : end);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.remove_suffix(1);
            }
            if (!line.empty()) queries.emplace_back(std::string(line));
            if (end == std::string_view::npos) break;
            rest.remove_prefix(end + 1);
        }
        if (!queries.empty()) bootstrap["query"] = json::Value(std::move(queries));

        if (!bootstrap.empty()) {
            bootstrap["autocommit"] = json::Value(profile.auto_commit);
            config["bootstrap"] = json::Value(std::move(bootstrap));
        }
    }

    // url no formato JDBC: o DBeaver a usa para exibir e reconectar. Escrever
    // algo que ele entenda e' o ponto de gravar no formato dele.
    //
    // O sub-protocolo vem do PROVIDER, nao de um prefixo fixo: com
    // "jdbc:postgresql://" cravado aqui, um perfil MySQL era gravado com uma
    // url que dizia PostgreSQL -- e o campo aparecia errado na tela do
    // DBeaver. Foi assim que o defeito apareceu, olhando o arquivo gravado.
    const std::string scheme =
        stored.provider == "mysql"   ? "mysql"
      : stored.provider == "mariadb" ? "mariadb"
      : stored.provider == "sqlite"  ? "sqlite"
      : stored.provider.empty()      ? "postgresql"
                                     : stored.provider;

    // O SQL Anywhere e' gravado como o DBeaver o alcanca: o driver "Sybase
    // jConnect", cuja url leva o banco em ServiceName.
    if (profile.driver_id == "sqlanywhere") {
        config["url"] = json::Value(
            "jdbc:sybase:Tds:" + profile.host + ":" + std::to_string(profile.port) +
            (profile.database.empty() ? std::string{}
                                      : "?ServiceName=" + profile.database));
    } else
    // O SQL Server tem forma propria: ";databaseName=", nao "/banco".
    if (scheme == "sqlserver" || scheme == "mssql") {
        config["url"] = json::Value(
            "jdbc:sqlserver://" + profile.host + ":" + std::to_string(profile.port) +
            (profile.database.empty() ? std::string{}
                                      : ";databaseName=" + profile.database));
    } else {
        config["url"] = json::Value(
            "jdbc:" + scheme + "://" + profile.host + ":" +
            std::to_string(profile.port) + "/" + profile.database);
    }

    if (!profile.driver_properties.empty()) {
        json::Object properties;
        for (const auto& [key, value] : profile.driver_properties) {
            properties[key] = json::Value(value);
        }
        config["properties"] = json::Value(std::move(properties));
    }

    // So' as ligadas: ausente e' o padrao (falso) nos dois programas.
    {
        json::Object provider;
        const auto put = [&provider](const char* key, bool value) {
            if (value) provider[key] = json::Value(std::string("true"));
        };
        put(kShowNonDefaultDb,  profile.postgres.show_non_default_databases);
        put(kShowTemplateDb,    profile.postgres.show_template_databases);
        put(kShowUnavailableDb, profile.postgres.show_unavailable_databases);
        if (!profile.postgres.session_role.empty()) {
            provider[kChosenRole] = json::Value(profile.postgres.session_role);
        }
        if ((stored.provider == "sqlserver" || stored.provider == "mssql") &&
            profile.driver_id != "sqlanywhere") {
            provider[kMssqlAuthentication] = json::Value(std::string(
                profile.auth_model == AuthModel::windows ? "WINDOWS_INTEGRATED"
                                                         : "SQL_SERVER_PASSWORD"));
        }
        if (!provider.empty()) {
            config["provider-properties"] = json::Value(std::move(provider));
        }
    }

    // SSL no formato do handler do DBeaver. Gravar so' quando ligado: o
    // DBeaver tambem omite handler desabilitado, e um handler vazio no
    // arquivo aparece como aba configurada na tela dele.
    json::Object handlers;
    if (profile.ssl.enabled) {
        const bool is_mysql = stored.provider == "mysql";

        json::Object properties;
        if (is_mysql) {
            // O MySQL nao tem "modo": tem dois booleanos. `require` vira
            // verificacao desligada, que e' o que o modo significa.
            properties["ssl.require"] = json::Value(true);
            properties["ssl.verify.server"] = json::Value(
                profile.ssl.mode == SslMode::verify_ca ||
                profile.ssl.mode == SslMode::verify_full);
        } else {
            properties["sslMode"] =
                json::Value(std::string(to_string(profile.ssl.mode)));
        }
        if (!profile.ssl.root_cert_path.empty()) {
            properties["ssl.ca.cert"] = json::Value(profile.ssl.root_cert_path);
        }
        if (!profile.ssl.client_cert_path.empty()) {
            properties["ssl.client.cert"] =
                json::Value(profile.ssl.client_cert_path);
        }
        if (!profile.ssl.client_key_path.empty()) {
            properties["ssl.client.key"] =
                json::Value(profile.ssl.client_key_path);
        }

        json::Object handler;
        handler["type"]       = json::Value(std::string("CONFIG"));
        handler["enabled"]    = json::Value(true);
        handler["properties"] = json::Value(std::move(properties));

        handlers[is_mysql ? "mysql_ssl" : "postgre_ssl"] =
            json::Value(std::move(handler));
    }

    // Tunel e proxy: gravados mesmo desligados quando ha' host -- quem
    // desmarca a caixa para testar sem o tunel nao quer redigitar tudo. As
    // SENHAS vao para o arquivo de credenciais, nunca para este.
    if (!profile.ssh.host.empty()) {
        json::Object properties;
        properties["host"] = json::Value(profile.ssh.host);
        properties["port"] = json::Value(static_cast<double>(profile.ssh.port));
        properties["authType"] = json::Value(std::string(
            profile.ssh.auth == SshAuthType::public_key ? "PUBLIC_KEY"
          : profile.ssh.auth == SshAuthType::agent      ? "AGENT"
                                                        : "PASSWORD"));
        if (!profile.ssh.private_key_path.empty()) {
            properties["keyPath"] = json::Value(profile.ssh.private_key_path);
        }
        properties["aliveInterval"] = json::Value(
            static_cast<double>(profile.ssh.keep_alive.count() * 1000));
        properties["sshConnectTimeout"] = json::Value(
            static_cast<double>(profile.ssh.connect_timeout.count() * 1000));

        json::Object handler;
        handler["type"]    = json::Value(std::string("TUNNEL"));
        handler["enabled"] = json::Value(profile.ssh.enabled);
        if (!profile.ssh.user.empty()) {
            handler["user"] = json::Value(profile.ssh.user);
        }
        handler["save-password"] = json::Value(profile.ssh.save_password);
        handler["properties"]    = json::Value(std::move(properties));
        handlers["ssh_tunnel"]   = json::Value(std::move(handler));
    }
    if (!profile.proxy.host.empty()) {
        json::Object properties;
        properties["socks-host"] = json::Value(profile.proxy.host);
        properties["socks-port"] =
            json::Value(static_cast<double>(profile.proxy.port));

        json::Object handler;
        handler["type"]    = json::Value(std::string("PROXY"));
        handler["enabled"] = json::Value(profile.proxy.enabled);
        if (!profile.proxy.user.empty()) {
            handler["user"] = json::Value(profile.proxy.user);
        }
        handler["properties"]   = json::Value(std::move(properties));
        handlers["socks_proxy"] = json::Value(std::move(handler));
    }
    if (!handlers.empty()) config["handlers"] = json::Value(std::move(handlers));

    json::Object node;
    node["provider"] = json::Value(
        stored.provider.empty() ? std::string("postgresql") : stored.provider);
    node["driver"] = json::Value(
        stored.driver.empty() ? std::string("postgres-jdbc") : stored.driver);
    node["name"] = json::Value(profile.name);
    if (!profile.description.empty()) {
        node["description"] = json::Value(profile.description);
    }
    if (!profile.folder.empty()) node["folder"] = json::Value(profile.folder);
    node["save-password"] = json::Value(profile.save_password);

    // Nivel de isolamento: so' quando o usuario escolheu um (-1 = padrao do
    // servidor). Prefixo "otter-" pelo mesmo motivo das preferencias do
    // editor -- o DBeaver ignora o que nao conhece.
    if (profile.isolation_level >= 0) {
        node["otter-isolation"] =
            json::Value(static_cast<double>(profile.isolation_level));
    }

    // Preferencias do editor, por conexao. Num objeto proprio com prefixo
    // "otter-": o DBeaver ignora chaves que nao conhece, e um perfil que
    // passe pelas duas ferramentas nao perde nem corrompe nada.
    //
    // Gravadas so' quando DIFEREM do padrao, para o arquivo nao encher de
    // linhas que nao dizem nada -- e para um perfil importado do DBeaver
    // continuar parecendo um perfil do DBeaver.
    {
        const EditorOptions defaults;
        const EditorOptions& editor = profile.editor;
        json::Object ed;

        if (editor.keyword_case != defaults.keyword_case) {
            ed["keyword-case"] = json::Value(
                static_cast<double>(editor.keyword_case));
        }
        if (editor.indent_width != defaults.indent_width) {
            ed["indent-width"] = json::Value(
                static_cast<double>(editor.indent_width));
        }
        if (editor.river_style != defaults.river_style) {
            ed["river-style"] = json::Value(editor.river_style);
        }
        if (editor.wrap_select_after != defaults.wrap_select_after) {
            ed["wrap-select-after"] = json::Value(
                static_cast<double>(editor.wrap_select_after));
        }
        if (editor.complete_on_typing != defaults.complete_on_typing) {
            ed["complete-on-typing"] = json::Value(editor.complete_on_typing);
        }
        if (editor.complete_in_comments != defaults.complete_in_comments) {
            ed["complete-in-comments"] = json::Value(editor.complete_in_comments);
        }
        if (editor.complete_in_strings != defaults.complete_in_strings) {
            ed["complete-in-strings"] = json::Value(editor.complete_in_strings);
        }
        if (editor.auto_insert_single != defaults.auto_insert_single) {
            ed["auto-insert-single"] = json::Value(editor.auto_insert_single);
        }
        if (editor.complete_delay_ms != defaults.complete_delay_ms) {
            ed["complete-delay-ms"] = json::Value(
                static_cast<double>(editor.complete_delay_ms));
        }
        if (editor.tab_size != defaults.tab_size) {
            ed["tab-size"] = json::Value(static_cast<double>(editor.tab_size));
        }
        if (editor.show_line_numbers != defaults.show_line_numbers) {
            ed["show-line-numbers"] = json::Value(editor.show_line_numbers);
        }
        if (editor.auto_indent != defaults.auto_indent) {
            ed["auto-indent"] = json::Value(editor.auto_indent);
        }
        if (editor.show_matching_brackets != defaults.show_matching_brackets) {
            ed["show-matching-brackets"] =
                json::Value(editor.show_matching_brackets);
        }
        if (editor.show_whitespace != defaults.show_whitespace) {
            ed["show-whitespace"] = json::Value(editor.show_whitespace);
        }
        if (editor.page_size != defaults.page_size) {
            ed["page-size"] = json::Value(static_cast<double>(editor.page_size));
        }
        if (editor.hex_limit_kb != defaults.hex_limit_kb) {
            ed["hex-limit-kb"] =
                json::Value(static_cast<double>(editor.hex_limit_kb));
        }
        if (editor.export_format != defaults.export_format) {
            ed["export-format"] =
                json::Value(static_cast<double>(editor.export_format));
        }
        if (editor.export_write_header != defaults.export_write_header) {
            ed["export-write-header"] =
                json::Value(editor.export_write_header);
        }
        if (!editor.export_null_text.empty()) {
            ed["export-null-text"] = json::Value(editor.export_null_text);
        }
        if (editor.null_text != defaults.null_text) {
            ed["null-text"] = json::Value(editor.null_text);
        }
        if (editor.align_numbers_right != defaults.align_numbers_right) {
            ed["align-numbers-right"] =
                json::Value(editor.align_numbers_right);
        }
        if (editor.word_wrap != defaults.word_wrap) {
            ed["word-wrap"] = json::Value(editor.word_wrap);
        }
        if (editor.code_folding != defaults.code_folding) {
            ed["code-folding"] = json::Value(editor.code_folding);
        }
        if (editor.stop_script_on_error != defaults.stop_script_on_error) {
            ed["stop-script-on-error"] =
                json::Value(editor.stop_script_on_error);
        }

        if (!ed.empty()) node["otter-editor"] = json::Value(std::move(ed));
    }
    node["configuration"] = json::Value(std::move(config));

    return json::Value(std::move(node));
}

// --- Credenciais ------------------------------------------------------------
//
// Formato do DBeaver, decifrado:
//
//   { "<id da conexao>": { "#connection": { "user": "...", "password": "..." } } }

json::Value read_credentials(const StoreLocation& location) {
    const std::string raw = read_file(location.credentials);
    if (raw.empty()) return {};

    const auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(raw.data()), raw.size());

    auto plain = crypto::aes128_cbc_decrypt(bytes, kDBeaverKey);
    if (!plain) return {};   // chave diferente: segue sem senha

    const std::string text(plain->begin(), plain->end());
    auto parsed = json::parse(text);
    if (!parsed) return {};

    return *parsed;
}

Status write_credentials(const StoreLocation& location,
                         const std::vector<StoredProfile>& profiles) {
    json::Object root;

    for (const StoredProfile& stored : profiles) {
        const ConnectionProfile& profile = stored.profile;
        json::Object entry;

        if (profile.save_password && !profile.password.empty()) {
            json::Object connection;
            if (!profile.user.empty()) {
                connection["user"] = json::Value(profile.user);
            }
            connection["password"] = json::Value(profile.password);
            entry["#connection"] = json::Value(std::move(connection));
        }

        // Senhas dos handlers de rede, sob "network/<id>" como no DBeaver.
        // A do tunel so' com "salvar senha" da aba SSH; a frase da chave vai
        // no mesmo campo `password`, que e' onde o DBeaver a guarda.
        if (profile.ssh.save_password) {
            const std::string& secret =
                profile.ssh.auth == SshAuthType::public_key ? profile.ssh.passphrase
                                                            : profile.ssh.password;
            if (!secret.empty()) {
                json::Object ssh;
                if (!profile.ssh.user.empty()) {
                    ssh["user"] = json::Value(profile.ssh.user);
                }
                ssh["password"] = json::Value(secret);
                entry["network/ssh_tunnel"] = json::Value(std::move(ssh));
            }
        }
        if (profile.save_password && !profile.proxy.password.empty()) {
            json::Object socks;
            if (!profile.proxy.user.empty()) {
                socks["user"] = json::Value(profile.proxy.user);
            }
            socks["password"] = json::Value(profile.proxy.password);
            entry["network/socks_proxy"] = json::Value(std::move(socks));
        }

        if (!entry.empty()) root[stored.id] = json::Value(std::move(entry));
    }

    // Nenhuma senha a guardar: apaga o arquivo em vez de deixar um vazio
    // cifrado, que so' geraria duvida sobre o que ha' dentro.
    if (root.empty()) {
        std::error_code ec;
        std::filesystem::remove(location.credentials, ec);
        return {};
    }

    const std::string text = json::serialize(json::Value(std::move(root)), 2);

    auto iv = crypto::random_iv();
    if (!iv) return std::unexpected(iv.error());

    const auto bytes = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    const std::vector<std::uint8_t> cipher =
        crypto::aes128_cbc_encrypt(bytes, kDBeaverKey, *iv);

    return write_file(location.credentials,
                      std::string_view(reinterpret_cast<const char*>(cipher.data()),
                                       cipher.size()));
}

} // namespace

bool StoreLocation::exists() const {
    std::error_code ec;
    return std::filesystem::exists(data_sources, ec);
}

StoreLocation otter_store_location() {
    return location_from_directory(fs::path(data_directory()));
}

StoreLocation legacy_store_location() {
    return location_from_directory(user_config_root() / "C-Otter");
}

bool import_legacy_store(const StoreLocation& from, const StoreLocation& to) {
    if (to.exists() || !from.exists()) return false;

    std::error_code ec;
    fs::create_directories(to.directory, ec);
    if (ec) return false;

    fs::copy_file(from.data_sources, to.data_sources,
                  fs::copy_options::skip_existing, ec);
    if (ec) return false;

    // As senhas sao opcionais: um perfil pode nao ter nenhuma salva.
    if (fs::exists(from.credentials, ec)) {
        fs::copy_file(from.credentials, to.credentials,
                      fs::copy_options::skip_existing, ec);
    }
    return true;
}

std::vector<StoreLocation> dbeaver_store_locations() {
    std::vector<StoreLocation> found;
    std::error_code ec;

    // O DBeaver guarda os workspaces em DBeaverData/workspace<N>/<projeto>/.dbeaver.
    // Varremos em vez de fixar "workspace6/General": o numero muda entre
    // versoes e o projeto pode ter outro nome.
    const fs::path root = user_config_root() / "DBeaverData";
    if (!fs::exists(root, ec)) return found;

    for (const auto& workspace : fs::directory_iterator(root, ec)) {
        if (!workspace.is_directory(ec)) continue;

        for (const auto& project : fs::directory_iterator(workspace.path(), ec)) {
            if (!project.is_directory(ec)) continue;

            const fs::path config = project.path() / ".dbeaver";
            if (!fs::exists(config / "data-sources.json", ec)) continue;

            found.push_back(location_from_directory(config));
        }
    }
    return found;
}

Result<std::vector<StoredProfile>> load_profiles(const StoreLocation& location) {
    std::vector<StoredProfile> profiles;

    const std::string raw = read_file(location.data_sources);
    if (raw.empty()) return profiles;   // ausente = nenhuma conexao salva

    auto root = json::parse(raw);
    if (!root) {
        return std::unexpected(
            root.error().with_context("reading " + location.data_sources));
    }

    const json::Value credentials = read_credentials(location);

    for (const auto& [id, node] : (*root)["connections"].as_object()) {
        StoredProfile stored = profile_from_json(id, node);

        const json::Value& entry = credentials[id]["#connection"];
        if (!entry.is_null()) {
            // O usuario pode estar so' no arquivo de credenciais.
            if (stored.profile.user.empty()) {
                stored.profile.user = std::string(entry["user"].as_string());
            }
            stored.profile.password = std::string(entry["password"].as_string());
        }

        if (const json::Value& ssh = credentials[id]["network/ssh_tunnel"];
            !ssh.is_null()) {
            SshTunnelConfig& tunnel = stored.profile.ssh;
            if (tunnel.user.empty()) tunnel.user = std::string(ssh["user"].as_string());
            (tunnel.auth == SshAuthType::public_key ? tunnel.passphrase
                                                    : tunnel.password) =
                std::string(ssh["password"].as_string());
        }
        if (const json::Value& socks = credentials[id]["network/socks_proxy"];
            !socks.is_null()) {
            ProxyConfig& proxy = stored.profile.proxy;
            if (proxy.user.empty()) proxy.user = std::string(socks["user"].as_string());
            proxy.password = std::string(socks["password"].as_string());
        }

        profiles.push_back(std::move(stored));
    }
    return profiles;
}

ProviderNames provider_for_driver(std::string_view driver_id) noexcept {
    // Os nomes de driver sao os que o DBeaver grava -- "mysql8" e
    // "postgres-jdbc" --, para que um perfil criado aqui abra la'.
    if (driver_id == "mysql") return {"mysql", "mysql8"};
    if (driver_id == "sqlserver" || driver_id == "mssql") {
        return {"sqlserver", "microsoft"};
    }
    // O DBeaver nao tem driver de SQL Anywhere: quem o usa la' conecta pelo
    // "Sybase jConnect", do plugin do SQL Server. E' o par que faz um perfil
    // criado aqui abrir la'.
    if (driver_id == "sqlanywhere") return {"mssql", "sybase_jconn"};

    // O padrao e' PostgreSQL, que e' o driver padrao do ConnConfig. Um
    // driver desconhecido gravado como PostgreSQL e' melhor que um provider
    // vazio: o vazio nao casa com nada em apply_driver e o perfil voltaria
    // como "nao suportado".
    return {"postgresql", "postgres-jdbc"};
}

Status save_profiles(const StoreLocation& location,
                     const std::vector<StoredProfile>& profiles) {
    // O id gerado precisa ser o MESMO nos dois arquivos: `data-sources.json`
    // guarda a conexao sob ele, e `credentials-config.json` guarda a senha
    // sob a mesma chave. Gerar um id local aqui e passar o vetor original
    // adiante -- com `id` ainda vazio -- fazia a senha ser gravada sob a
    // chave "", e nenhum perfil a encontrava na releitura.
    //
    // O sintoma na tela era "Access denied (using password: NO)" numa conexao
    // recem-importada que dizia ter senha salva.
    std::vector<StoredProfile> with_ids = profiles;
    for (StoredProfile& stored : with_ids) {
        if (stored.id.empty()) stored.id = generate_id(stored.profile.driver_id);
    }

    json::Object connections;
    for (const StoredProfile& stored : with_ids) {
        connections[stored.id] = profile_to_json(stored);
    }

    // As pastas, como o DBeaver as grava: uma entrada por CAMINHO
    // ("Clientes/Producao"), os ancestrais incluidos. Ficava sempre vazio --
    // o DBeaver recria a pasta a partir do campo `folder` da conexao, mas uma
    // ferramenta que leia so' esta lista nao veria nenhuma.
    json::Object folders;
    for (const StoredProfile& stored : with_ids) {
        const std::string& path = stored.profile.folder;
        for (std::size_t slash = path.find('/');; slash = path.find('/', slash + 1)) {
            const std::string part = path.substr(0, slash);
            if (!part.empty() && folders.find(part) == folders.end()) {
                folders[part] = json::Value(json::Object{});
            }
            if (slash == std::string::npos) break;
        }
    }

    json::Object root;
    root["folders"]     = json::Value(std::move(folders));
    root["connections"] = json::Value(std::move(connections));

    OTTER_RETURN_IF_ERROR(write_file(
        location.data_sources, json::serialize(json::Value(std::move(root)), 2)));

    return write_credentials(location, with_ids);
}

std::string unique_name(std::string_view wanted,
                        const std::vector<std::string>& taken) {
    const auto used = [&taken](std::string_view name) {
        return std::find(taken.begin(), taken.end(), name) != taken.end();
    };
    if (!used(wanted)) return std::string(wanted);

    for (std::size_t n = 1;; ++n) {
        std::string candidate = std::string(wanted) + "_" + std::to_string(n);
        if (!used(candidate)) return candidate;
    }
}

std::size_t make_names_unique(std::vector<StoredProfile>& profiles) {
    std::vector<std::string> taken;
    taken.reserve(profiles.size());

    // Os nomes de TODOS entram antes de renomear: senao o segundo "x" viraria
    // "x_1" mesmo que um "x_1" legitimo viesse depois na lista, e os dois
    // colidiriam.
    for (const StoredProfile& stored : profiles) {
        taken.push_back(stored.profile.effective_name());
    }

    std::size_t renamed = 0;
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const std::string current = profiles[i].profile.effective_name();
        const bool repeated =
            std::find(taken.begin(), taken.begin() + static_cast<std::ptrdiff_t>(i),
                      current) != taken.begin() + static_cast<std::ptrdiff_t>(i);
        if (!repeated) continue;

        const std::string fresh = unique_name(current, taken);
        profiles[i].profile.name = fresh;
        taken[i] = fresh;
        ++renamed;
    }
    return renamed;
}

void resolve_driver(StoredProfile& stored) { apply_driver(stored); }

std::string user_config_directory() { return user_config_root().string(); }

} // namespace otter::db
