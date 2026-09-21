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
    config.connect_timeout = connect_timeout;

    // O `enabled` e' o que a caixa da aba SSL controla; o modo so' vale
    // quando ela esta' marcada. Sem esse teste, um perfil com a caixa
    // desmarcada mas modo `require` gravado exigiria TLS sem a UI dizer.
    config.ssl_mode = ssl.enabled ? ssl.mode : SslMode::disable;

    // Propriedades do driver, da aba "Parâmetros internos". Iam para disco e
    // paravam ali: o ConnConfig tinha um campo `options` que ninguem
    // preenchia nem lia.
    config.driver_properties = driver_properties;
    return config;
}

} // namespace otter::db
