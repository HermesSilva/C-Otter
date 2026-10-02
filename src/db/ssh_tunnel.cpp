#include "db/ssh_tunnel.hpp"

#include "net/socket.hpp"

#include <chrono>
#include <thread>

namespace otter::db {

std::string ssh_tunnel_refusal(const SshEndpoint& ssh) {
    if (ssh.host.empty()) return "the SSH host is required";
    if (ssh.user.empty()) return "the SSH user is required";
    if (ssh.password_auth) {
        return "SSH password authentication is not supported: the tunnel uses the "
               "system ssh client, which only reads a password from a terminal. "
               "Use a key or the SSH agent";
    }
    if (ssh.key_has_passphrase) {
        return "a private key with a passphrase needs the SSH agent: add it with "
               "ssh-add and choose 'SSH agent'";
    }
    return {};
}

std::vector<std::string> ssh_tunnel_arguments(const SshEndpoint& ssh,
                                              std::uint16_t local_port,
                                              std::string_view target_host,
                                              std::uint16_t target_port) {
    std::vector<std::string> out;

    // -N: so' o tunel, sem comando remoto nem shell.
    out.emplace_back("-N");

    // 127.0.0.1 explicito: sem ele o `ssh` escuta tambem em ::1, e com
    // GatewayPorts ligado, em todas as interfaces -- o banco ficaria exposto
    // a' rede local por uma porta sem senha.
    out.emplace_back("-L");
    out.push_back("127.0.0.1:" + std::to_string(local_port) + ":" +
                  std::string(target_host) + ":" + std::to_string(target_port));

    const auto option = [&out](std::string text) {
        out.emplace_back("-o");
        out.push_back(std::move(text));
    };

    // Nunca perguntar nada: nao ha' terminal. Uma pergunta deixaria o
    // processo parado ate' o tempo esgotar, sem dizer por que.
    option("BatchMode=yes");
    // A porta local ocupada (ou recusada pelo servidor) derruba o `ssh`, em
    // vez de deixa-lo vivo sem tunel.
    option("ExitOnForwardFailure=yes");
    // Servidor NOVO entra no known_hosts; um que MUDOU de chave e' recusado.
    // `no` aceitaria qualquer um -- inclusive quem estivesse no meio.
    option("StrictHostKeyChecking=accept-new");
    option("ConnectTimeout=" + std::to_string(ssh.connect_timeout.count()));
    if (ssh.keep_alive.count() > 0) {
        option("ServerAliveInterval=" + std::to_string(ssh.keep_alive.count()));
        option("ServerAliveCountMax=3");
    }

    if (!ssh.key_path.empty()) {
        out.emplace_back("-i");
        out.push_back(ssh.key_path);
        // So' a chave indicada: sem isto o `ssh` tenta antes todas as do
        // agente, e um servidor com MaxAuthTries baixo fecha antes de chegar
        // nela.
        option("IdentitiesOnly=yes");
    }

    out.emplace_back("-p");
    out.push_back(std::to_string(ssh.port));

    // "--" fecha as opcoes: um usuario ou host comecando por '-' nao pode
    // ser lido como opcao do ssh (ProxyCommand executaria um programa).
    out.emplace_back("--");
    out.push_back(ssh.user + "@" + ssh.host);
    return out;
}

Result<SshTunnel> SshTunnel::open(const SshEndpoint& ssh, std::string_view target_host,
                                  std::uint16_t target_port) {
    if (const std::string refusal = ssh_tunnel_refusal(ssh); !refusal.empty()) {
        return fail(Errc::not_supported, refusal);
    }

    const std::string program = find_program("ssh");
    if (program.empty()) {
        return fail(Errc::not_found,
                    "the ssh client was not found in the PATH: install OpenSSH to "
                    "use an SSH tunnel");
    }

    OTTER_ASSIGN_OR_RETURN(const std::uint16_t local_port, net::free_local_port());

    ProcessOptions options;
    options.program   = program;
    options.arguments = ssh_tunnel_arguments(ssh, local_port, target_host, target_port);

    SshTunnel tunnel;
    OTTER_ASSIGN_OR_RETURN(tunnel.process_, Process::start(options));
    tunnel.local_port_ = local_port;

    // Espera a porta local atender. O `ssh` so' a abre depois de autenticar,
    // entao "atendeu" e' "o tunel esta' de pe'".
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + ssh.connect_timeout + std::chrono::seconds(2);
    std::string said;

    while (clock::now() < deadline) {
        said += tunnel.process_.read_output();

        if (!tunnel.process_.running()) {
            said += tunnel.process_.read_output();
            while (!said.empty() && (said.back() == '\n' || said.back() == '\r')) {
                said.pop_back();
            }
            return fail(Errc::connection_failed,
                        "SSH tunnel to " + ssh.user + "@" + ssh.host + ":" +
                            std::to_string(ssh.port) + " failed: " +
                            (said.empty() ? std::string("ssh exited without a message")
                                          : said));
        }

        auto probe = net::Socket::connect("127.0.0.1", local_port,
                                          std::chrono::milliseconds(300));
        if (probe) return tunnel;

        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    tunnel.process_.kill();
    return fail(Errc::timed_out,
                "SSH tunnel to " + ssh.user + "@" + ssh.host + ":" +
                    std::to_string(ssh.port) + " timed out" +
                    (said.empty() ? std::string{} : ": " + said));
}

} // namespace otter::db
