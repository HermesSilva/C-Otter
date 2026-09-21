#include "ui/file_dialog.hpp"

#ifdef _WIN32

// windows.h antes de qualquer coisa: shobjidl depende dele, e GLFW define
// APIENTRY de um jeito que conflita se a ordem inverter (C4005).
#include <windows.h>
#include <shobjidl.h>

#include <string>

namespace otter::ui {
namespace {

// UTF-8 <-> UTF-16. A API do Windows fala UTF-16; o resto do programa, UTF-8.
std::wstring widen(std::string_view text) {
    if (text.empty()) return {};

    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) return {};

    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), size);
    return out;
}

std::string narrow(const wchar_t* text) {
    if (text == nullptr) return {};

    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 1) return {};

    // size inclui o terminador; a string nao deve inclui-lo.
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr,
                        nullptr);
    return out;
}

// RAII do COM. CoInitializeEx pode devolver RPC_E_CHANGED_MODE se a thread ja'
// foi inicializada com outro modelo -- nesse caso nao chamamos CoUninitialize,
// senao derrubariamos a inicializacao de quem veio antes.
class ComScope {
public:
    ComScope() {
        const HRESULT hr =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                        COINIT_DISABLE_OLE1DDE);
        owns_ = SUCCEEDED(hr);
    }
    ~ComScope() { if (owns_) CoUninitialize(); }

    ComScope(const ComScope&)            = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    bool owns_ = false;
};

// Converte a lista de filtros para o formato do IFileDialog, mantendo as
// strings vivas ate' o dialogo fechar.
struct FilterStorage {
    std::vector<std::wstring>       strings;
    std::vector<COMDLG_FILTERSPEC>  specs;
};

FilterStorage build_filters(const std::vector<FileFilter>& filters) {
    FilterStorage storage;
    storage.strings.reserve(filters.size() * 2 + 2);

    for (const FileFilter& filter : filters) {
        storage.strings.push_back(widen(filter.label));
        storage.strings.push_back(widen(filter.pattern));
    }
    // "Todos os arquivos" no fim: quem salvou um .txt por engano precisa
    // conseguir reabri-lo.
    storage.strings.emplace_back(L"All files");
    storage.strings.emplace_back(L"*.*");

    for (std::size_t i = 0; i + 1 < storage.strings.size(); i += 2) {
        storage.specs.push_back(
            {storage.strings[i].c_str(), storage.strings[i + 1].c_str()});
    }
    return storage;
}

std::optional<std::string> run_dialog(const CLSID& clsid, bool saving,
                                      std::string_view title,
                                      const std::vector<FileFilter>& filters,
                                      std::string_view suggested_name,
                                      std::string_view initial_path) {
    const ComScope com;

    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    const std::wstring wide_title = widen(title);
    if (!wide_title.empty()) dialog->SetTitle(wide_title.c_str());

    const FilterStorage storage = build_filters(filters);
    if (!storage.specs.empty()) {
        dialog->SetFileTypes(static_cast<UINT>(storage.specs.size()),
                             storage.specs.data());
        dialog->SetFileTypeIndex(1);

        // Extensao automatica: o usuario digita "relatorio" e sai
        // "relatorio.sql". Sem isto, um arquivo sem extensao nao abriria no
        // programa certo depois.
        if (!filters.empty()) {
            std::string_view pattern = filters.front().pattern;
            if (pattern.starts_with("*.")) {
                dialog->SetDefaultExtension(widen(pattern.substr(2)).c_str());
            }
        }
    }

    if (saving && !suggested_name.empty()) {
        dialog->SetFileName(widen(suggested_name).c_str());
    }

    if (!initial_path.empty()) {
        IShellItem* folder = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(
                widen(initial_path).c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
            dialog->SetFolder(folder);
            folder->Release();
        }
    }

    std::optional<std::string> chosen;

    // Modal sobre a janela ativa: sem o dono, o dialogo pode aparecer atras.
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                chosen = narrow(path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }

    dialog->Release();
    return chosen;
}

} // namespace

std::optional<std::string> open_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view initial_path) {
    return run_dialog(CLSID_FileOpenDialog, /*saving=*/false, title, filters,
                      {}, initial_path);
}

std::optional<std::string> save_file_dialog(
    std::string_view title, const std::vector<FileFilter>& filters,
    std::string_view suggested_name, std::string_view initial_path) {
    return run_dialog(CLSID_FileSaveDialog, /*saving=*/true, title, filters,
                      suggested_name, initial_path);
}

} // namespace otter::ui

#endif // _WIN32
