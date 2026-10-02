#include "ui/platform_open.hpp"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
// O projeto ja' define as duas pela linha de comando; redefinir e' aviso, e
// aviso aqui e' erro.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <cstdlib>
#endif

namespace otter::ui {

std::string url_encode(std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";

    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        const bool safe = (byte >= 'A' && byte <= 'Z') ||
                          (byte >= 'a' && byte <= 'z') ||
                          (byte >= '0' && byte <= '9') || byte == '-' ||
                          byte == '_' || byte == '.' || byte == '~';
        if (safe) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        }
    }
    return out;
}

bool open_url(const std::string& url) {
    // So' a web. Sem isto a funcao abriria QUALQUER coisa que o sistema
    // associe -- um caminho de executavel, um esquema de protocolo de outro
    // programa --, e o texto vem de dentro de um script SQL.
    if (!url.starts_with("https://") && !url.starts_with("http://")) return false;

#ifdef _WIN32
    const HINSTANCE result =
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    // A URL ja' vem codificada por url_encode: nao ha' aspa nem espaco nela.
    // Aspas simples em volta por via das duvidas, e recusa se houver alguma.
    if (url.find('\'') != std::string::npos) return false;
    const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#endif
}

bool open_path(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;

#ifdef _WIN32
    const HINSTANCE result = ShellExecuteW(
        nullptr, L"open", std::filesystem::path(path).wstring().c_str(), nullptr,
        nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    if (path.find('\'') != std::string::npos) return false;
    const std::string command = "xdg-open '" + path + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#endif
}

bool reveal_in_file_manager(const std::string& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return false;

#ifdef _WIN32
    // "/select,<arquivo>": o Explorer abre a pasta com o arquivo marcado.
    const std::wstring arguments =
        L"/select,\"" + std::filesystem::path(path).wstring() + L"\"";
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                           arguments.c_str(), nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(result) > 32;
#else
    const std::string folder = std::filesystem::path(path).parent_path().string();
    if (folder.find('\'') != std::string::npos) return false;
    const std::string command = "xdg-open '" + folder + "' >/dev/null 2>&1 &";
    return std::system(command.c_str()) == 0;
#endif
}

} // namespace otter::ui
