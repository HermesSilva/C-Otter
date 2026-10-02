// C-Otter -- testes de base/process e db/ssh_tunnel.
//
// O que se confere aqui e' o que erra sem avisar: um argumento com aspas que
// vira dois, a senha indo parar na linha de comando, um `ssh` que aceita
// qualquer servidor ou fica esperando uma senha num terminal que nao existe.
#include "test_main.hpp"

#include "base/process.hpp"
#include "db/ssh_tunnel.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace otter;

namespace {

bool has(const std::vector<std::string>& arguments, std::string_view wanted) {
    return std::find(arguments.begin(), arguments.end(), wanted) != arguments.end();
}

// O valor que segue `-o` com este prefixo, ou vazio.
bool has_option(const std::vector<std::string>& arguments, std::string_view option) {
    for (std::size_t i = 0; i + 1 < arguments.size(); ++i) {
        if (arguments[i] == "-o" && arguments[i + 1] == option) return true;
    }
    return false;
}

db::SshEndpoint endpoint() {
    db::SshEndpoint ssh;
    ssh.host = "bastion.example";
    ssh.user = "ana";
    return ssh;
}

// Espera o processo terminar, juntando a saida.
std::string run_to_end(Process& process) {
    std::string out;
    for (int i = 0; i < 200 && process.running(); ++i) {
        out += process.read_output();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    out += process.read_output();
    return out;
}

} // namespace

// --- Linha de comando do Windows ---------------------------------------------------

OTTER_TEST(process_quotes_arguments_like_commandlinetoargv) {
    OTTER_CHECK(quote_windows_argument("simples") == "simples");
    OTTER_CHECK(quote_windows_argument("com espaco") == "\"com espaco\"");
    OTTER_CHECK(quote_windows_argument("") == "\"\"");

    // Aspa dentro: escapada, senao fecharia o argumento no meio.
    OTTER_CHECK(quote_windows_argument("a \"b\" c") == "\"a \\\"b\\\" c\"");

    // Barra no FIM de um argumento citado dobra: "C:\Meus Dados\" sem isso
    // escaparia a aspa de fechamento e engoliria o argumento seguinte.
    OTTER_CHECK(quote_windows_argument("C:\\Meus Dados\\") == "\"C:\\Meus Dados\\\\\"");

    // Barra que NAO precede aspa fica como esta'.
    OTTER_CHECK(quote_windows_argument("C:\\dir\\arq") == "C:\\dir\\arq");
    OTTER_CHECK(quote_windows_argument("a\\\"b") == "\"a\\\\\\\"b\"");
}

OTTER_TEST(process_reports_a_program_that_does_not_exist) {
    ProcessOptions options;
    options.program = "otter-programa-que-nao-existe-xyz";
    auto process = Process::start(options);
#ifdef _WIN32
    OTTER_CHECK(!process.has_value());
#else
    // No POSIX o fork da' certo e o exec falha: o filho sai com 127.
    OTTER_CHECK(process.has_value());
    if (process) {
        (void)run_to_end(*process);
        OTTER_CHECK(process->exit_code() == 127);
    }
#endif
}

OTTER_TEST(process_captures_output_exit_code_and_environment) {
    ProcessOptions options;
#ifdef _WIN32
    options.program = find_program("cmd");
    OTTER_CHECK(!options.program.empty());
    options.arguments = {"/c", "echo valor=%OTTER_TESTE% & exit 3"};
#else
    options.program   = "/bin/sh";
    options.arguments = {"-c", "echo valor=$OTTER_TESTE; exit 3"};
#endif
    // A variavel chega ao filho sem passar pela linha de comando -- e' por
    // onde vai a senha do pg_dump.
    options.environment = {{"OTTER_TESTE", "do ambiente"}};

    auto process = Process::start(options);
    OTTER_CHECK(process.has_value());
    if (!process) return;

    const std::string output = run_to_end(*process);
    OTTER_CHECK(output.find("valor=do ambiente") != std::string::npos);
    OTTER_CHECK(!process->running());
    OTTER_CHECK(process->exit_code() == 3);
}

OTTER_TEST(process_kill_stops_a_running_child) {
    ProcessOptions options;
#ifdef _WIN32
    options.program   = find_program("cmd");
    options.arguments = {"/c", "ping -n 30 127.0.0.1 > NUL"};
#else
    options.program   = "/bin/sh";
    options.arguments = {"-c", "sleep 30"};
#endif
    auto process = Process::start(options);
    OTTER_CHECK(process.has_value());
    if (!process) return;

    OTTER_CHECK(process->running());
    process->kill();
    OTTER_CHECK(!process->running());
    OTTER_CHECK(process->exit_code().has_value());
}

OTTER_TEST(process_find_program_returns_empty_when_missing) {
    OTTER_CHECK(find_program("otter-programa-que-nao-existe-xyz").empty());
}

// --- Tunel SSH -----------------------------------------------------------------------

OTTER_TEST(ssh_tunnel_arguments) {
    db::SshEndpoint ssh = endpoint();
    ssh.port = 2222;

    const auto arguments = db::ssh_tunnel_arguments(ssh, 50123, "db.interno", 5432);

    OTTER_CHECK(has(arguments, "-N"));
    // 127.0.0.1 explicito: o banco nao pode ficar exposto na rede local.
    OTTER_CHECK(has(arguments, "127.0.0.1:50123:db.interno:5432"));
    OTTER_CHECK(has(arguments, "2222"));
    OTTER_CHECK(arguments.back() == "ana@bastion.example");
    // "--" antes do destino: um host "-oProxyCommand=..." nao vira opcao.
    OTTER_CHECK(arguments[arguments.size() - 2] == "--");
}

OTTER_TEST(ssh_tunnel_never_prompts_and_never_trusts_a_changed_host) {
    const auto arguments = db::ssh_tunnel_arguments(endpoint(), 50123, "db", 5432);

    // Sem terminal, qualquer pergunta trava o processo ate' o tempo esgotar.
    OTTER_CHECK(has_option(arguments, "BatchMode=yes"));
    // Porta local ocupada derruba o ssh, em vez de deixa-lo vivo sem tunel.
    OTTER_CHECK(has_option(arguments, "ExitOnForwardFailure=yes"));
    // accept-new: aceita servidor NOVO, recusa o que MUDOU de chave. "no"
    // aceitaria quem estivesse no meio do caminho.
    OTTER_CHECK(has_option(arguments, "StrictHostKeyChecking=accept-new"));
    OTTER_CHECK(!has_option(arguments, "StrictHostKeyChecking=no"));
    OTTER_CHECK(has_option(arguments, "ConnectTimeout=10"));
    OTTER_CHECK(has_option(arguments, "ServerAliveInterval=60"));
}

OTTER_TEST(ssh_tunnel_key_file) {
    db::SshEndpoint ssh = endpoint();
    OTTER_CHECK(!has(db::ssh_tunnel_arguments(ssh, 1, "db", 2), "-i"));

    ssh.key_path = "C:\\Users\\ana\\.ssh\\id_ed25519";
    const auto arguments = db::ssh_tunnel_arguments(ssh, 1, "db", 2);
    OTTER_CHECK(has(arguments, "-i"));
    OTTER_CHECK(has(arguments, "C:\\Users\\ana\\.ssh\\id_ed25519"));
    OTTER_CHECK(has_option(arguments, "IdentitiesOnly=yes"));
}

OTTER_TEST(ssh_tunnel_refuses_what_it_cannot_do) {
    OTTER_CHECK(db::ssh_tunnel_refusal(endpoint()).empty());

    db::SshEndpoint ssh = endpoint();
    ssh.password_auth = true;
    OTTER_CHECK(db::ssh_tunnel_refusal(ssh).find("password") != std::string::npos);
    // E a recusa vem ANTES de iniciar o ssh.
    OTTER_CHECK(!db::SshTunnel::open(ssh, "db", 5432).has_value());

    ssh = endpoint();
    ssh.key_has_passphrase = true;
    OTTER_CHECK(db::ssh_tunnel_refusal(ssh).find("agent") != std::string::npos);

    ssh = endpoint();
    ssh.user.clear();
    OTTER_CHECK(!db::ssh_tunnel_refusal(ssh).empty());
    ssh = endpoint();
    ssh.host.clear();
    OTTER_CHECK(!db::ssh_tunnel_refusal(ssh).empty());
}
