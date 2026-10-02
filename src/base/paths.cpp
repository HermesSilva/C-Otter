#include "base/paths.hpp"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>

#  include <cstdint>
#  include <vector>
#endif

namespace otter {

std::string executable_directory() {
    namespace fs = std::filesystem;
    std::error_code ec;

#if defined(_WIN32)
    // Largo, e por wchar_t: uma pasta com acento no caminho nao sobrevive a'
    // versao ANSI.
    static wchar_t buffer[32768];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, 32768);
    if (length == 0 || length >= 32768) return {};
    return fs::path(std::wstring(buffer, length)).parent_path().string();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    const fs::path path = fs::canonical(buffer.data(), ec);
    return ec ? std::string{} : path.parent_path().string();
#else
    const fs::path path = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : path.parent_path().string();
#endif
}

std::string data_directory() {
    namespace fs = std::filesystem;

    // Sem a pasta do executavel (nao deveria acontecer), a pasta corrente:
    // melhor um lugar previsivel do que nenhum.
    std::string directory = executable_directory();
    if (directory.empty()) directory = ".";
    return (fs::path(directory) / ".C-Otter").string();
}

} // namespace otter
