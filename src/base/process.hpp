// C-Otter -- base/process.hpp
//
// Um processo filho com a saida capturada: o que o tunel SSH (o cliente
// OpenSSH do sistema) e o backup/restore (pg_dump, pg_restore) precisam.
//
// Sem shell no meio: o programa e os argumentos vao separados, e um nome de
// banco com espaco ou aspas nao vira outro comando.
#pragma once

#include "base/error.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace otter {

struct ProcessOptions {
    std::string              program;   // caminho completo, ou nome a achar no PATH
    std::vector<std::string> arguments;

    // Variaveis ACRESCENTADAS ao ambiente herdado. E' por aqui que vai a
    // senha do pg_dump (PGPASSWORD): na linha de comando ela apareceria na
    // lista de processos de qualquer usuario da maquina.
    std::vector<std::pair<std::string, std::string>> environment;
};

class Process {
public:
    Process();
    ~Process();   // mata o filho que ainda estiver rodando

    Process(const Process&)            = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;

    // Inicia sem janela de console. stdout e stderr vem juntos, na ordem.
    [[nodiscard]] static Result<Process> start(const ProcessOptions& options);

    [[nodiscard]] bool started() const noexcept;

    // Nao bloqueia.
    [[nodiscard]] bool running();

    // O que o filho escreveu desde a ultima chamada. Nao bloqueia.
    [[nodiscard]] std::string read_output();

    // Vazio enquanto roda.
    [[nodiscard]] std::optional<int> exit_code();

    void kill() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// O caminho de um programa: `name` no PATH, ou em um dos `extra_directories`
// (o instalador do PostgreSQL nao poe o `bin` dele no PATH). Vazio se nao
// achou. No Windows o `.exe` e' acrescentado.
[[nodiscard]] std::string find_program(std::string_view name,
                                       const std::vector<std::string>& extra_directories = {});

// A linha de comando como o usuario a leria, para o log -- com aspas onde ha'
// espaco. So' para MOSTRAR: a execucao nao passa por ela.
[[nodiscard]] std::string display_command(const ProcessOptions& options);

// Como o Windows espera um argumento na linha de comando do CreateProcess
// (as regras de CommandLineToArgvW). Exposto para o teste: errar aqui e'
// deixar um argumento com aspas virar dois.
[[nodiscard]] std::string quote_windows_argument(std::string_view argument);

} // namespace otter
