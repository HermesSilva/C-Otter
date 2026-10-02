// C-Otter -- verificacao do proxy SOCKS5 contra um proxy de verdade.
//
// O teste unitario confere os bytes; se a conexao do banco SOBREVIVE a passar
// pelo proxy (o aperto de mao do protocolo, o TLS por cima do tunel, o
// cancelamento) so' aparece com os dois de pe' (diretiva 2).
//
// Antes, em dois terminais:
//
//     python tools/socks5_test_server.py --port 1081
//     python tools/socks5_test_server.py --port 1082 --user ana --password s3
//
//     spike_proxy_live <perfil> [<perfil> ...]
//
// Usa os perfis salvos, para nao ter senha na linha de comando. O log do
// proxy ("CONNECT host:porta") e' a prova de que a conexao passou por ele.
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

// Conecta pelo proxy e roda uma consulta. Devolve "ok" ou o erro.
std::string through(db::ConnConfig config, std::uint16_t proxy_port,
                    const std::string& user = {}, const std::string& password = {}) {
    config.proxy_host     = "127.0.0.1";
    config.proxy_port     = proxy_port;
    config.proxy_user     = user;
    config.proxy_password = password;

    db::Driver* driver = db::find_driver(config.driver_id);
    if (driver == nullptr) return "driver desconhecido";

    auto holt = driver->connect(config);
    if (!holt) return holt.error().to_string();

    auto result = (*holt)->query("SELECT 40 + 2");
    if (!result) return "consulta: " + result.error().to_string();
    if (result->row_count() != 1 || result->text(0, 0) != "42") return "resultado errado";
    return "ok";
}

} // namespace

int main(int argc, char** argv) {
    auto profiles = db::load_profiles(db::otter_store_location());
    if (!profiles) {
        std::printf("nao consegui ler os perfis\n");
        return 2;
    }

    for (int a = 1; a < argc; ++a) {
        const db::ConnectionProfile* found = nullptr;
        for (const auto& stored : *profiles) {
            if (stored.profile.effective_name() == argv[a]) found = &stored.profile;
        }
        if (found == nullptr) {
            std::printf("perfil nao encontrado: %s\n", argv[a]);
            return 2;
        }
        // So' servidor local: o teste nao sai da maquina.
        if (found->host != "localhost" && found->host != "127.0.0.1") {
            std::printf("perfil nao e' local: %s\n", argv[a]);
            return 2;
        }

        db::ConnConfig config = found->to_conn_config();
        config.ssl_mode = db::SslMode::disable;
        // O banco do perfil pode nao existir mais (perfil de outro teste); o
        // que se confere aqui e' o caminho, nao o banco.
        if (config.driver_id != "postgresql") config.database.clear();
        std::printf("\n%s (%s)\n", argv[a], config.driver_id.c_str());

        const std::string plain = through(config, 1081);
        if (plain != "ok") std::printf("         %s\n", plain.c_str());
        check(plain == "ok", "pelo proxy sem autenticacao");

        const std::string with_auth = through(config, 1082, "ana", "s3");
        if (with_auth != "ok") std::printf("         %s\n", with_auth.c_str());
        check(with_auth == "ok", "pelo proxy com usuario e senha");

        const std::string wrong = through(config, 1082, "ana", "errada");
        std::printf("         %s\n", wrong.c_str());
        check(wrong.find("refused the user or the password") != std::string::npos,
              "senha errada do proxy: recusa dita como tal");

        const std::string anonymous = through(config, 1082);
        std::printf("         %s\n", anonymous.c_str());
        check(anonymous.find("requires a user and a password") != std::string::npos,
              "proxy que exige senha, sem senha: dito como tal");

        // Destino que o proxy nao alcanca: o erro e' do DESTINO, via proxy.
        db::ConnConfig closed = config;
        closed.port = 1;
        const std::string refused = through(closed, 1081);
        std::printf("         %s\n", refused.c_str());
        check(refused.find("via SOCKS proxy") != std::string::npos,
              "destino fechado: o erro nomeia o destino e o proxy");

        // TLS por cima do tunel.
        db::ConnConfig secure = config;
        secure.ssl_mode = db::SslMode::require;

        // So' vale onde o servidor aceita TLS DIRETO: um servidor sem SSL
        // ligado recusaria com ou sem proxy, e isso nao e' defeito do tunel.
        std::string direct = "driver";
        if (db::Driver* driver = db::find_driver(secure.driver_id)) {
            auto holt = driver->connect(secure);
            direct = holt ? "ok" : holt.error().to_string();
        }
        if (direct != "ok") {
            std::printf("  [ -- ] TLS por dentro do proxy: PULADO, o servidor nao "
                        "aceita TLS nem direto\n         %s\n", direct.c_str());
        } else {
            const std::string tls = through(secure, 1081);
            std::printf("         TLS: %s\n", tls.c_str());
            check(tls == "ok", "TLS (require) por dentro do proxy");
        }
    }

    std::printf("\n%d verificacoes, %d falha(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
