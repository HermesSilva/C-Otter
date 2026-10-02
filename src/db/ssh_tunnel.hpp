// C-Otter -- db/ssh_tunnel.hpp
//
// Tunel SSH para alcancar um banco que so' a rede do servidor SSH enxerga --
// a aba "SSH" do dialogo de conexao do DBeaver.
//
// O tunel e' o cliente OpenSSH do SISTEMA (`ssh -N -L`), rodando como
// processo filho. ADR 0021: implementar SSH2 aqui seria um protocolo de
// criptografia inteiro para manter, e as bibliotecas prontas (libssh2,
// libssh) trazem OpenSSL ou licenca LGPL para o link estatico (ADR 0002). O
// cliente OpenSSH ja' vem com Windows 10+, macOS e toda distribuicao Linux, e
// le' as mesmas chaves, o mesmo agente e o mesmo `known_hosts` que o usuario
// ja' tem.
//
// O preco: sem senha digitada. O `ssh` so' aceita senha por um terminal, e
// aqui nao ha' um -- a autenticacao e' por chave ou pelo agente. A tela diz
// isso em vez de oferecer um campo que nao funciona (diretiva 6).
#pragma once

#include "base/error.hpp"
#include "base/process.hpp"
#include "db/holt.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace otter::db {

// Os argumentos do `ssh` para o tunel. Funcao pura, para o teste: um
// argumento errado aqui e' uma conexao que pede senha num terminal que nao
// existe, ou que aceita qualquer servidor.
[[nodiscard]] std::vector<std::string> ssh_tunnel_arguments(
    const SshEndpoint& ssh, std::uint16_t local_port, std::string_view target_host,
    std::uint16_t target_port);

// Por que este perfil nao pode abrir o tunel, ou vazio se pode. Conferido
// ANTES de iniciar o processo: a recusa vem com o motivo, e nao como
// "Permission denied" trinta segundos depois.
[[nodiscard]] std::string ssh_tunnel_refusal(const SshEndpoint& ssh);

class SshTunnel {
public:
    // Inicia o `ssh` e espera a porta local aceitar conexao (ou o processo
    // morrer, ou o tempo do perfil esgotar). O erro traz o que o `ssh` disse.
    [[nodiscard]] static Result<SshTunnel> open(const SshEndpoint& ssh,
                                                std::string_view target_host,
                                                std::uint16_t target_port);

    // Onde o banco passa a atender: 127.0.0.1 nesta porta.
    [[nodiscard]] std::uint16_t local_port() const noexcept { return local_port_; }

    // O processo continua de pe'?
    [[nodiscard]] bool alive() { return process_.running(); }

    // O que o `ssh` escreveu desde a ultima leitura (avisos, queda do tunel).
    [[nodiscard]] std::string output() { return process_.read_output(); }

private:
    Process       process_;
    std::uint16_t local_port_ = 0;
};

} // namespace otter::db
