// C-Otter -- verificacao do tunel SSH com o cliente `ssh` de verdade.
//
//     spike_ssh_live                               so' o caminho de FALHA
//     spike_ssh_live <perfil> <host> <porta> <usuario> [<chave>]
//
// Sem argumentos confere o que da' para conferir sem um servidor SSH: o `ssh`
// e' achado e iniciado, a recusa do servidor chega como mensagem (e nao como
// uma espera ate' o tempo esgotar), e o processo nao fica para tras.
//
// Com argumentos, abre o tunel de verdade e roda uma consulta pelo perfil
// salvo -- o banco do perfil e' alcancado A PARTIR do servidor SSH.
#include "db/connection_store.hpp"
#include "db/registry.hpp"
#include "db/ssh_tunnel.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

} // namespace

int main(int argc, char** argv) {
    const std::string program = find_program("ssh");
    std::printf("ssh: %s\n", program.empty() ? "(nao encontrado)" : program.c_str());
    check(!program.empty(), "o cliente ssh existe no PATH");
    if (program.empty()) return 1;

    std::printf("\nfalha: nada escuta na porta\n");
    {
        db::SshEndpoint ssh;
        ssh.host            = "127.0.0.1";
        ssh.port            = 9;   // discard: nada escuta
        ssh.user            = "ninguem";
        ssh.connect_timeout = std::chrono::seconds(5);

        const auto started = std::chrono::steady_clock::now();
        auto tunnel = db::SshTunnel::open(ssh, "localhost", 5432);
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        check(!tunnel.has_value(), "o tunel nao abre");
        if (!tunnel) std::printf("         %s\n", tunnel.error().to_string().c_str());
        check(!tunnel && tunnel.error().to_string().find("ninguem@127.0.0.1:9") !=
                             std::string::npos,
              "o erro diz para ONDE era o tunel");
        check(!tunnel &&
                  tunnel.error().to_string().find("without a message") == std::string::npos &&
                  tunnel.error().to_string().find("failed: ") != std::string::npos,
              "o erro traz o que o ssh disse");
        std::printf("         em %lld ms\n", static_cast<long long>(took.count()));
        check(took < std::chrono::seconds(8), "a recusa chega antes do tempo limite");
    }

    if (argc >= 5) {
        std::printf("\ntunel de verdade\n");
        auto profiles = db::load_profiles(db::otter_store_location());
        const db::ConnectionProfile* found = nullptr;
        if (profiles) {
            for (const auto& stored : *profiles) {
                if (stored.profile.effective_name() == argv[1]) found = &stored.profile;
            }
        }
        if (found == nullptr) {
            std::printf("perfil nao encontrado: %s\n", argv[1]);
            return 2;
        }

        db::SshEndpoint ssh;
        ssh.host = argv[2];
        ssh.port = static_cast<std::uint16_t>(std::atoi(argv[3]));
        ssh.user = argv[4];
        if (argc >= 6) ssh.key_path = argv[5];

        db::ConnConfig config = found->to_conn_config();
        auto tunnel = db::SshTunnel::open(ssh, config.host, config.port);
        if (!tunnel) std::printf("         %s\n", tunnel.error().to_string().c_str());
        check(tunnel.has_value(), "o tunel abre");

        if (tunnel) {
            config.host = "127.0.0.1";
            config.port = tunnel->local_port();
            std::printf("         porta local %u\n", static_cast<unsigned>(config.port));

            db::Driver* driver = db::find_driver(config.driver_id);
            auto holt = driver != nullptr ? driver->connect(config)
                                          : Result<std::unique_ptr<db::Holt>>(
                                                fail(Errc::not_found, "driver"));
            if (!holt) std::printf("         %s\n", holt.error().to_string().c_str());
            check(holt.has_value(), "o banco conecta pela ponta local do tunel");
            if (holt) {
                auto result = (*holt)->query("SELECT 40 + 2");
                check(result && result->text(0, 0) == "42", "a consulta volta pelo tunel");
            }
            check(tunnel->alive(), "o ssh continua de pe' durante a sessao");
        }
    }

    std::printf("\n%d verificacoes, %d falha(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
