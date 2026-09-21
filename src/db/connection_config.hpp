// C-Otter -- db/connection_config.hpp
//
// Configuracao completa de uma conexao, equivalente ao que o DBeaver expoe no
// assistente de conexao: Main, PostgreSQL, Driver properties, SSH, Proxy, SSL,
// Initialization, Shell commands.
//
// Separado de ConnConfig (db/holt.hpp), que e' o subconjunto minimo que o
// driver precisa para abrir o socket.
#pragma once

#include "db/holt.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace otter::db {

// Como o usuario se identifica no servidor.
enum class AuthModel : std::uint8_t {
    database_native,   // usuario e senha do proprio SGBD
    no_auth,           // sem autenticacao (SQLite, alguns proxies)
    pg_ident,          // ident/peer do PostgreSQL
    kerberos,
    aws_iam,
};

// Tipo de conexao -- controla a cor do ambiente e as travas de seguranca.
// O DBeaver usa isto para pintar producao de vermelho e exigir confirmacao.
enum class ConnectionType : std::uint8_t {
    development,
    test,
    production,
};

struct ConnectionTypeInfo {
    const char*   name;
    std::uint32_t color;          // faixa de cor na UI
    bool          auto_commit;
    bool          confirm_execute;      // pergunta antes de executar
    bool          confirm_data_change;  // pergunta antes de UPDATE/DELETE
};

[[nodiscard]] const ConnectionTypeInfo& connection_type_info(ConnectionType type);

// --- Rede --------------------------------------------------------------------

enum class SshAuthType : std::uint8_t { password, public_key, agent };

struct SshTunnelConfig {
    bool        enabled = false;
    std::string host;
    std::uint16_t port = 22;
    std::string user;
    SshAuthType auth = SshAuthType::password;
    std::string password;
    std::string private_key_path;
    std::string passphrase;
    bool        save_password = false;
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds keep_alive{60};
};

struct SslConfig {
    bool        enabled = false;
    SslMode     mode = SslMode::prefer;
    std::string root_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
};

struct ProxyConfig {
    bool          enabled = false;
    std::string   host;
    std::uint16_t port = 1080;
    std::string   user;
    std::string   password;
};

// --- Especificidades do PostgreSQL ------------------------------------------
//
// Extraidas de PostgreConnectionPageAdvanced do DBeaver.

struct PostgresOptions {
    bool show_non_default_databases = false;  // mostrar outros bancos
    bool show_template_databases    = false;  // mostrar templates
    bool show_unavailable_databases = false;  // mostrar bancos sem acesso
    bool show_database_statistics   = false;  // ler tamanho dos objetos
    bool read_all_data_types        = false;  // incluir tipos raros
    bool read_keys_with_columns     = false;  // ler colunas das chaves
    bool replace_legacy_timezone    = false;  // timestamptz legado
    bool use_prepared_statements    = true;
    std::string session_role;                 // SET ROLE ao conectar
};

// --- Configuracao completa ---------------------------------------------------

struct ConnectionProfile {
    // Identificacao
    std::string    id;                 // gerado, estavel
    std::string    name;               // rotulo do usuario
    std::string    description;
    std::string    folder;             // pasta na arvore
    ConnectionType type = ConnectionType::development;
    std::uint32_t  color = 0;          // 0 = usar a cor do tipo

    // Driver
    std::string driver_id = "postgresql";

    // Servidor
    std::string   host = "localhost";
    std::uint16_t port = 5432;
    std::string   database;
    std::string   url;                 // alternativa ao host/porta
    bool          use_url = false;

    // Autenticacao
    AuthModel   auth_model = AuthModel::database_native;
    std::string user;
    std::string password;
    bool        save_password = false;

    // Rede
    SshTunnelConfig ssh;
    SslConfig       ssl;
    ProxyConfig     proxy;

    // Inicializacao
    bool        auto_commit = true;
    std::string default_schema;         // search_path inicial
    std::string bootstrap_queries;      // executadas ao conectar
    bool        read_only = false;
    std::chrono::seconds connect_timeout{10};
    bool        keep_alive = false;
    std::chrono::seconds keep_alive_interval{60};
    bool        close_idle_connections = false;

    // Propriedades livres do driver
    std::map<std::string, std::string> driver_properties;

    // Especificas do SGBD
    PostgresOptions postgres;

    // Nome sugerido quando o usuario nao informa um: "banco@host".
    [[nodiscard]] std::string effective_name() const;

    // Converte para o subconjunto que o driver consome.
    [[nodiscard]] struct ConnConfig to_conn_config() const;
};

} // namespace otter::db
