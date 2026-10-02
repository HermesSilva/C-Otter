#include "db/connection_config.hpp"

#include "db/holt.hpp"

namespace otter::db {

const ConnectionTypeInfo& connection_type_info(ConnectionType type) {
    // Cores em 0xAABBGGRR, como o resto da UI.
    static const ConnectionTypeInfo kDevelopment{
        "Desenvolvimento", 0xFF5CB85C, true, false, false};
    static const ConnectionTypeInfo kTest{
        "Teste", 0xFF3CA8DC, true, false, false};
    // Producao: sem autocommit e com confirmacao. Um UPDATE sem WHERE em
    // producao e' incidente, nao erro de digitacao.
    static const ConnectionTypeInfo kProduction{
        "Produção", 0xFF4C4CDC, false, true, true};

    switch (type) {
        case ConnectionType::development: return kDevelopment;
        case ConnectionType::test:        return kTest;
        case ConnectionType::production:  return kProduction;
    }
    return kDevelopment;
}


std::string ConnectionProfile::effective_name() const {
    if (!name.empty()) return name;
    if (database.empty()) return host;
    return database + "@" + host;
}

ConnConfig ConnectionProfile::to_conn_config() const {
    ConnConfig config;
    config.driver_id       = driver_id;
    config.host            = host;
    config.port            = port;
    config.database        = database;
    config.user            = user;
    config.password        = password;
    config.integrated_auth = auth_model == AuthModel::windows;
    config.connect_timeout = connect_timeout;

    // O `enabled` e' o que a caixa da aba SSL controla; o modo so' vale
    // quando ela esta' marcada. Sem esse teste, um perfil com a caixa
    // desmarcada mas modo `require` gravado exigiria TLS sem a UI dizer.
    config.ssl_mode = ssl.enabled ? ssl.mode : SslMode::disable;

    // O tunel SSH tambem so' com a caixa marcada.
    if (ssh.enabled && !ssh.host.empty()) {
        config.ssh.host            = ssh.host;
        config.ssh.port            = ssh.port;
        config.ssh.user            = ssh.user;
        config.ssh.connect_timeout = ssh.connect_timeout;
        config.ssh.keep_alive      = ssh.keep_alive;
        config.ssh.password_auth   = ssh.auth == SshAuthType::password;
        if (ssh.auth == SshAuthType::public_key) {
            config.ssh.key_path           = ssh.private_key_path;
            config.ssh.key_has_passphrase = !ssh.passphrase.empty();
        }
    }

    // O proxy segue a mesma regra: os campos so' valem com a caixa marcada.
    if (proxy.enabled && !proxy.host.empty()) {
        config.proxy_host     = proxy.host;
        config.proxy_port     = proxy.port;
        config.proxy_user     = proxy.user;
        config.proxy_password = proxy.password;
    }

    // Propriedades do driver, da aba "Parâmetros internos". Iam para disco e
    // paravam ali: o ConnConfig tinha um campo `options` que ninguem
    // preenchia nem lia.
    config.driver_properties = driver_properties;

    // -1 = nao mexer. Os demais sao os valores de IsolationLevel; fora da
    // faixa e' tratado como "nao mexer", em vez de converter para um nivel
    // arbitrario -- um perfil corrompido nao deve trocar o isolamento.
    if (isolation_level >= 0 && isolation_level <= 3) {
        config.isolation_level =
            static_cast<IsolationLevel>(isolation_level);
    }

    SessionSetup setup;
    setup.session_role      = postgres.session_role;
    setup.default_schema    = default_schema;
    setup.bootstrap_queries = bootstrap_queries;
    setup.read_only         = read_only;
    config.init_statements    = session_setup_statements(driver_id, setup);
    config.ignore_init_errors = ignore_bootstrap_errors;
    config.auto_commit        = auto_commit;
    return config;
}

namespace {

// Aspas do SGBD, sem passar pelo dialeto global: esta funcao roda antes de a
// conexao existir, e o dialeto global e' o da conexao ATIVA.
std::string quoted(std::string_view name, char quote) {
    std::string out(1, quote);
    for (const char c : name) {
        out += c;
        if (c == quote) out += c;
    }
    out += quote;
    return out;
}

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
                             text.back() == '\r' || text.back() == ';')) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

std::vector<std::string> session_setup_statements(std::string_view driver_id,
                                                  const SessionSetup& setup) {
    const bool mysql = driver_id == "mysql";
    const bool mssql = driver_id == "sqlserver" || driver_id == "mssql";
    // SQL Anywhere: nao ha' "schema corrente" a trocar (o dono padrao e' o
    // usuario conectado, e SETUSER e' personificacao, nao caminho de busca)
    // nem sessao somente leitura. Nada a emitir para os dois.
    const bool anywhere = driver_id == "sqlanywhere";
    std::vector<std::string> out;

    // SET ROLE e' do PostgreSQL; o campo so' existe na pagina dele.
    if (!mysql && !mssql && !anywhere && !trimmed(setup.session_role).empty()) {
        out.push_back("SET ROLE " + quoted(trimmed(setup.session_role), '"'));
    }

    if (const std::string_view schema = trimmed(setup.default_schema);
        !schema.empty() && !anywhere) {
        // `public` continua no caminho: sem ele as extensoes instaladas la'
        // (e o que o usuario chama sem qualificar) somem.
        // SQL Server: o "schema padrao" da conexao e' o BANCO (USE); o schema
        // de cada usuario e' propriedade dele no servidor, nao da sessao.
        if (mssql) {
            std::string name = "[";
            for (const char c : schema) {
                name += c;
                if (c == ']') name += c;
            }
            out.push_back("USE " + name + "]");
        } else
        out.push_back(mysql ? "USE " + quoted(schema, '`')
                      : schema == "public"
                          ? std::string("SET search_path TO public")
                          : "SET search_path TO " + quoted(schema, '"') +
                                ", public");
    }

    // Uma consulta por linha. O DBeaver guarda uma LISTA; aqui o campo e' um
    // texto so', e a linha e' o separador que nao exige um analisador de SQL
    // neste nivel (connection_config nao depende de sql/).
    std::string_view rest = setup.bootstrap_queries;
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        const std::string_view line = trimmed(
            rest.substr(0, end == std::string_view::npos ? rest.size() : end));
        if (!line.empty() && !line.starts_with("--")) out.emplace_back(line);
        if (end == std::string_view::npos) break;
        rest.remove_prefix(end + 1);
    }

    // Por ultimo: as consultas de inicializacao podem precisar escrever.
    // O SQL Server nao tem sessao somente leitura: `read_only` fica a cargo
    // de quem conecta (ApplicationIntent e' so' roteamento). Nada a emitir.
    if (setup.read_only && !mssql && !anywhere) {
        out.push_back(mysql
            ? "SET SESSION TRANSACTION READ ONLY"
            : "SET SESSION CHARACTERISTICS AS TRANSACTION READ ONLY");
    }
    return out;
}

Status apply_session_setup(Holt& holt, const ConnConfig& config) {
    for (const std::string& sql : config.init_statements) {
        Status status = holt.execute(sql);
        if (status || config.ignore_init_errors) continue;
        return std::unexpected(status.error().with_context(
            "initialization statement failed: " + sql));
    }
    if (!config.auto_commit) {
        OTTER_RETURN_IF_ERROR(holt.set_auto_commit(false));
    }
    return {};
}

} // namespace otter::db
