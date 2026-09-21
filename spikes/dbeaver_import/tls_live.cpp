// C-Otter -- verificacao do TLS contra os servidores locais.
//
// Por que existe: o aperto de mao TLS nao e' testavel em unidade. O que
// importa saber -- se o servidor ACEITA o upgrade, se a sessao continua
// funcionando depois dele, se o `require` falha quando deve -- so' aparece
// falando com um servidor de verdade (diretiva 2).
//
// Usa o perfil salvo, para nao ter senha na linha de comando.
#include "db/connection_store.hpp"
#include "db/holt.hpp"
#include "db/registry.hpp"

#include <cstdio>
#include <string>

using namespace otter;

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    if (!ok) ++failures;
    std::printf("  [%s] %s\n", ok ? " OK " : "FALHA", what.c_str());
}

// Conecta com um modo de SSL e devolve o canal negociado -- vazio quando em
// claro, mensagem de erro prefixada por "erro: " quando a conexao falhou.
std::string try_connect(const db::ConnectionProfile& profile,
                        db::SslMode mode, bool enabled) {
    db::ConnConfig config = profile.to_conn_config();
    config.ssl_mode = enabled ? mode : db::SslMode::disable;

    db::Driver* driver = db::find_driver(config.driver_id);
    if (driver == nullptr) return "erro: driver desconhecido";

    auto holt = driver->connect(config);
    if (!holt) return "erro: " + holt.error().to_string();

    const std::string channel = (*holt)->secure_channel();

    // Uma consulta DEPOIS do upgrade: o aperto de mao pode ter sucesso e o
    // enquadramento dos pacotes seguintes estar errado -- foi o risco de
    // rotear send/receive pelo canal.
    auto result = (*holt)->query("SELECT 1 + 1");
    if (!result) return "erro: consulta apos TLS: " + result.error().to_string();

    return channel.empty() ? std::string("(em claro)") : channel;
}

} // namespace

int main(int argc, char** argv) {
    const std::string profile_name = argc > 1 ? argv[1] : "localhost";

    auto profiles = db::load_profiles(db::otter_store_location());
    if (!profiles) {
        std::printf("nao consegui ler os perfis: %s\n",
                    profiles.error().to_string().c_str());
        return 1;
    }

    const db::ConnectionProfile* profile = nullptr;
    for (const auto& stored : *profiles) {
        if (stored.profile.effective_name() == profile_name) {
            profile = &stored.profile;
            break;
        }
    }
    if (profile == nullptr) {
        std::printf("perfil '%s' nao encontrado\n", profile_name.c_str());
        return 1;
    }

    std::printf("perfil: %s (%s em %s:%u)\n\n",
                profile->effective_name().c_str(), profile->driver_id.c_str(),
                profile->host.c_str(), static_cast<unsigned>(profile->port));

    std::printf("sem TLS\n");
    const std::string plain = try_connect(*profile, db::SslMode::disable, false);
    std::printf("  canal: %s\n", plain.c_str());
    check(plain == "(em claro)", "sem TLS conecta e reporta canal em claro");

    std::printf("\nrequire\n");
    const std::string required =
        try_connect(*profile, db::SslMode::require, true);
    std::printf("  canal: %s\n", required.c_str());
    check(required.rfind("erro:", 0) != 0, "require conecta");
    check(required.find("TLS") != std::string::npos,
          "require negocia TLS e reporta o protocolo");

    // verify-full contra um servidor local com certificado autoassinado DEVE
    // falhar. Se passasse, a verificacao nao estaria acontecendo -- e o modo
    // seria um rotulo sem efeito, que e' pior que nao existir.
    std::printf("\nverify-full (certificado autoassinado -- deve recusar)\n");
    const std::string verified =
        try_connect(*profile, db::SslMode::verify_full, true);
    std::printf("  canal: %s\n", verified.c_str());
    check(verified.rfind("erro:", 0) == 0,
          "verify-full recusa certificado que a cadeia do sistema nao valida");

    std::printf("\n%d verificacoes, %d falharam\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
