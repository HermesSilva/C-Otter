#include "db/connection_store.hpp"

#include "base/aes.hpp"
#include "base/json.hpp"

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
    {"mssql",      "",  "SQL Server driver is planned for a later phase"},
    {"sqlserver",  "",  "SQL Server driver is planned for a later phase"},
};

void apply_driver(StoredProfile& stored) {
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

    for (const auto& [key, value] : config["properties"].as_object()) {
        profile.driver_properties[key] = std::string(value.as_string());
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

    config["url"] = json::Value(
        "jdbc:" + scheme + "://" + profile.host + ":" +
        std::to_string(profile.port) + "/" + profile.database);

    if (!profile.driver_properties.empty()) {
        json::Object properties;
        for (const auto& [key, value] : profile.driver_properties) {
            properties[key] = json::Value(value);
        }
        config["properties"] = json::Value(std::move(properties));
    }

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
        if (!stored.profile.save_password) continue;
        if (stored.profile.password.empty()) continue;

        json::Object connection;
        if (!stored.profile.user.empty()) {
            connection["user"] = json::Value(stored.profile.user);
        }
        connection["password"] = json::Value(stored.profile.password);

        json::Object entry;
        entry["#connection"] = json::Value(std::move(connection));
        root[stored.id] = json::Value(std::move(entry));
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
    return location_from_directory(user_config_root() / "C-Otter");
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

        profiles.push_back(std::move(stored));
    }
    return profiles;
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

    json::Object root;
    root["folders"]     = json::Value(json::Object{});
    root["connections"] = json::Value(std::move(connections));

    OTTER_RETURN_IF_ERROR(write_file(
        location.data_sources, json::serialize(json::Value(std::move(root)), 2)));

    return write_credentials(location, with_ids);
}

} // namespace otter::db
