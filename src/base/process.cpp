#include "base/process.hpp"

#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace otter {

std::string quote_windows_argument(std::string_view argument) {
    // Sem espaco, tab nem aspas: vai como esta'.
    if (!argument.empty() &&
        argument.find_first_of(" \t\n\v\"") == std::string_view::npos) {
        return std::string(argument);
    }

    // As regras do CommandLineToArgvW: N barras antes de uma aspa viram 2N+1
    // (a aspa literal); N barras no FIM do argumento citado viram 2N (para
    // nao escaparem a aspa de fechamento).
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (const char c : argument) {
        if (c == '\\') {
            ++backslashes;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

std::string display_command(const ProcessOptions& options) {
    std::string out = quote_windows_argument(options.program);
    for (const std::string& argument : options.arguments) {
        out += " " + quote_windows_argument(argument);
    }
    return out;
}

std::string find_program(std::string_view name,
                         const std::vector<std::string>& extra_directories) {
    namespace fs = std::filesystem;

#ifdef _WIN32
    const std::string file = std::string(name) + ".exe";
    constexpr char kSeparator = ';';
#else
    const std::string file(name);
    constexpr char kSeparator = ':';
#endif

    std::vector<std::string> directories = extra_directories;
    if (const char* path = std::getenv("PATH")) {
        std::string_view rest(path);
        while (!rest.empty()) {
            const std::size_t end = rest.find(kSeparator);
            directories.emplace_back(rest.substr(0, end));
            if (end == std::string_view::npos) break;
            rest.remove_prefix(end + 1);
        }
    }

    for (const std::string& directory : directories) {
        if (directory.empty()) continue;
        std::error_code ec;
        const fs::path candidate = fs::path(directory) / file;
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
    }
    return {};
}

#ifdef _WIN32

namespace {

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), length);
    return out;
}

} // namespace

struct Process::Impl {
    HANDLE process = nullptr;
    HANDLE output = nullptr;   // ponta de leitura do pipe
    std::optional<int> code;
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

Process::~Process() {
    if (!impl_) return;
    kill();
    if (impl_->output != nullptr) CloseHandle(impl_->output);
    if (impl_->process != nullptr) CloseHandle(impl_->process);
}

Process::Process(Process&&) noexcept = default;

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            kill();
            if (impl_->output != nullptr) CloseHandle(impl_->output);
            if (impl_->process != nullptr) CloseHandle(impl_->process);
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Result<Process> Process::start(const ProcessOptions& options) {
    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength        = sizeof inherit;
    inherit.bInheritHandle = TRUE;

    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
        return fail(Errc::io_error, "cannot create a pipe for the process output");
    }
    // A ponta de LEITURA fica so' aqui: herdada, o filho a manteria aberta e
    // o fim da saida nunca chegaria.
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

    // stdin = NUL: um programa que resolva perguntar (a senha do ssh) recebe
    // fim de arquivo e desiste, em vez de esperar para sempre por um teclado
    // que nao existe.
    HANDLE null_input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &inherit, OPEN_EXISTING, 0, nullptr);

    std::wstring command = widen(quote_windows_argument(options.program));
    for (const std::string& argument : options.arguments) {
        command += L" " + widen(quote_windows_argument(argument));
    }

    // Ambiente: o herdado, mais (ou trocando) as variaveis pedidas. Bloco
    // UTF-16 de "nome=valor\0", fechado por outro \0.
    std::wstring environment;
    if (!options.environment.empty()) {
        std::vector<std::wstring> names;
        for (const auto& [name, value] : options.environment) {
            names.push_back(widen(name));
            environment += names.back() + L"=" + widen(value);
            environment.push_back(L'\0');
        }
        if (LPWCH inherited = GetEnvironmentStringsW()) {
            for (const wchar_t* entry = inherited; *entry != L'\0';
                 entry += wcslen(entry) + 1) {
                const std::wstring_view text(entry);
                const std::size_t eq = text.find(L'=', 1);
                bool replaced = false;
                for (const std::wstring& name : names) {
                    if (eq != std::wstring_view::npos &&
                        _wcsnicmp(entry, name.c_str(), eq) == 0 && name.size() == eq) {
                        replaced = true;
                    }
                }
                if (!replaced) {
                    environment += text;
                    environment.push_back(L'\0');
                }
            }
            FreeEnvironmentStringsW(inherited);
        }
        environment.push_back(L'\0');
    }

    STARTUPINFOW startup = {};
    startup.cb         = sizeof startup;
    startup.dwFlags    = STARTF_USESTDHANDLES;
    startup.hStdInput  = null_input;
    startup.hStdOutput = write_end;
    startup.hStdError  = write_end;

    PROCESS_INFORMATION info = {};
    const BOOL created = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.empty() ? nullptr : environment.data(), nullptr, &startup, &info);

    const DWORD error = created ? 0 : GetLastError();
    CloseHandle(write_end);
    if (null_input != INVALID_HANDLE_VALUE) CloseHandle(null_input);

    if (!created) {
        CloseHandle(read_end);
        return fail(Errc::not_found,
                    "cannot start " + options.program +
                        (error == ERROR_FILE_NOT_FOUND ? " (program not found)"
                                                       : " (error " + std::to_string(error) + ")"));
    }
    CloseHandle(info.hThread);

    Process process;
    process.impl_->process = info.hProcess;
    process.impl_->output  = read_end;
    return process;
}

bool Process::started() const noexcept { return impl_ && impl_->process != nullptr; }

bool Process::running() {
    if (!started() || impl_->code.has_value()) return false;
    if (WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) return true;

    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    impl_->code = static_cast<int>(code);
    return false;
}

std::string Process::read_output() {
    std::string out;
    if (!impl_ || impl_->output == nullptr) return out;

    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(impl_->output, nullptr, 0, nullptr, &available, nullptr) ||
            available == 0) {
            break;
        }
        char buffer[4096];
        DWORD read = 0;
        const DWORD want = available < sizeof buffer ? available : sizeof buffer;
        if (!ReadFile(impl_->output, buffer, want, &read, nullptr) || read == 0) break;
        out.append(buffer, read);
    }
    return out;
}

std::optional<int> Process::exit_code() {
    (void)running();
    return impl_ ? impl_->code : std::nullopt;
}

void Process::kill() noexcept {
    if (!started() || impl_->code.has_value()) return;
    if (WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
        TerminateProcess(impl_->process, 1);
        WaitForSingleObject(impl_->process, 2000);
    }
    DWORD code = 0;
    GetExitCodeProcess(impl_->process, &code);
    impl_->code = static_cast<int>(code);
}

#else   // POSIX

struct Process::Impl {
    pid_t pid = -1;
    int   output = -1;
    std::optional<int> code;
};

Process::Process() : impl_(std::make_unique<Impl>()) {}

Process::~Process() {
    if (!impl_) return;
    kill();
    if (impl_->output >= 0) ::close(impl_->output);
}

Process::Process(Process&&) noexcept = default;

Process& Process::operator=(Process&& other) noexcept {
    if (this != &other) {
        if (impl_) {
            kill();
            if (impl_->output >= 0) ::close(impl_->output);
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}

Result<Process> Process::start(const ProcessOptions& options) {
    int fds[2];
    if (::pipe(fds) != 0) {
        return fail(Errc::io_error, "cannot create a pipe for the process output");
    }

    // Montado ANTES do fork: entre fork e exec so' chamadas async-signal-safe.
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(options.program.c_str()));
    for (const std::string& argument : options.arguments) {
        argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return fail(Errc::io_error, "cannot start " + options.program);
    }

    if (pid == 0) {
        for (const auto& [name, value] : options.environment) {
            ::setenv(name.c_str(), value.c_str(), 1);
        }
        const int null_input = ::open("/dev/null", O_RDONLY);
        if (null_input >= 0) ::dup2(null_input, STDIN_FILENO);
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        ::execvp(options.program.c_str(), argv.data());
        ::_exit(127);   // exec falhou: o mesmo codigo do shell
    }

    ::close(fds[1]);
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);

    Process process;
    process.impl_->pid    = pid;
    process.impl_->output = fds[0];
    return process;
}

bool Process::started() const noexcept { return impl_ && impl_->pid > 0; }

bool Process::running() {
    if (!started() || impl_->code.has_value()) return false;

    int status = 0;
    const pid_t done = ::waitpid(impl_->pid, &status, WNOHANG);
    if (done == 0) return true;
    impl_->code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return false;
}

std::string Process::read_output() {
    std::string out;
    if (!impl_ || impl_->output < 0) return out;

    char buffer[4096];
    for (;;) {
        const ssize_t read = ::read(impl_->output, buffer, sizeof buffer);
        if (read <= 0) break;
        out.append(buffer, static_cast<std::size_t>(read));
    }
    return out;
}

std::optional<int> Process::exit_code() {
    (void)running();
    return impl_ ? impl_->code : std::nullopt;
}

void Process::kill() noexcept {
    if (!started() || impl_->code.has_value()) return;
    int status = 0;
    if (::waitpid(impl_->pid, &status, WNOHANG) == 0) {
        ::kill(impl_->pid, SIGTERM);
        ::waitpid(impl_->pid, &status, 0);
    }
    impl_->code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

#endif

} // namespace otter
