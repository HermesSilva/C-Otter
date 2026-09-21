#include "ui/file_dialog.hpp"

#ifndef _WIN32

#include <array>
#include <cstdio>
#include <string>

namespace otter::ui {
namespace {

// Chama zenity ou kdialog, o que estiver instalado.
//
// Por que nao GTK direto: linkar GTK contradiz o link estatico total
// (ADR 0002) e arrastaria LGPL para dentro do binario. Um processo externo
// evita os dois problemas ao custo de depender de um utilitario do sistema.
//
// Quando nenhum existe, devolve vazio -- e a UI precisa dizer isso ao
// usuario, nao fingir que ele cancelou.
std::optional<std::string> run_command(const std::string& command) {
    std::FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) return std::nullopt;

    std::string out;
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        out += buffer.data();
    }

    const int status = pclose(pipe);
    if (status != 0 || out.empty()) return std::nullopt;   // cancelou

    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out.empty() ? std::nullopt : std::optional<std::string>(out);
}

bool has_command(const char* name) {
    const std::string probe = std::string("command -v ") + name +
                              " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

// Aspas simples ao redor, dobrando as internas: o caminho vai para um shell.
std::string shell_quote(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'') out += "'\\''";
        else           out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

std::string filter_args(const std::vector<FileFilter>& filters, bool zenity) {
    std::string args;
    for (const FileFilter& filter : filters) {
        if (zenity) {
            args += " --file-filter=" +
                    shell_quote(std::string(filter.label) + " | " +
                                std::string(filter.pattern));
        }
    }
    return args;
}

} // namespace

std::optional<std::string> open_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view initial_path) {

    if (has_command("zenity")) {
        std::string command = "zenity --file-selection --title=" +
                              shell_quote(title) +
                              filter_args(filters, /*zenity=*/true);
        if (!initial_path.empty()) {
            command += " --filename=" + shell_quote(initial_path);
        }
        return run_command(command);
    }

    if (has_command("kdialog")) {
        return run_command("kdialog --getopenfilename " +
                           shell_quote(initial_path.empty() ? "."
                                                            : initial_path) +
                           " --title " + shell_quote(title));
    }
    return std::nullopt;
}

std::optional<std::string> save_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view suggested_name, std::string_view initial_path) {

    const std::string start =
        initial_path.empty() ? std::string(suggested_name)
                             : std::string(initial_path) + "/" +
                                   std::string(suggested_name);

    if (has_command("zenity")) {
        std::string command = "zenity --file-selection --save "
                              "--confirm-overwrite --title=" +
                              shell_quote(title) +
                              filter_args(filters, /*zenity=*/true);
        if (!start.empty()) command += " --filename=" + shell_quote(start);
        return run_command(command);
    }

    if (has_command("kdialog")) {
        return run_command("kdialog --getsavefilename " +
                           shell_quote(start.empty() ? "." : start) +
                           " --title " + shell_quote(title));
    }
    return std::nullopt;
}

} // namespace otter::ui

#endif // !_WIN32
