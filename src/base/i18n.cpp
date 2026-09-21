#include "base/i18n.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#endif

namespace otter::i18n {
namespace {

struct Catalog {
    Language                                     language;
    std::unordered_map<std::string, std::string> entries;
};

struct State {
    // deque, nao vector: guardamos um ponteiro para o catalogo ativo, e o
    // deque preserva referencias quando cresce. Com vector, registrar um
    // segundo idioma invalidaria o ponteiro do primeiro.
    std::deque<Catalog> catalogs;
    const Catalog*      active = nullptr;   // nullptr = inglês
    std::string         active_code = "en";

    // Textos vistos sem tradução, para o relatório de cobertura.
    std::unordered_set<std::string> missing;

    mutable std::mutex mutex;
};

State& state() {
    static State instance;
    return instance;
}

// Remove espaços das pontas e as aspas opcionais de um valor do .lang.
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' ||
            text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

// Desfaz os escapes que o formato .lang usa para caber numa linha.
std::string unescape(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
            out.push_back(text[i]);
            continue;
        }
        switch (text[++i]) {
            case 'n':  out.push_back('\n'); break;
            case 't':  out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '=':  out.push_back('=');  break;
            default:   out.push_back('\\'); out.push_back(text[i]); break;
        }
    }
    return out;
}

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\\': out += "\\\\"; break;
            case '=':  out += "\\=";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

} // namespace

// Definido em i18n_pt_br.cpp.
void register_pt_br();

void load_builtin_catalogs() {
    register_pt_br();
    // Novos idiomas embutidos entram aqui.
}

const std::vector<Language>& available_languages() {
    // Reconstruido a cada chamada porque catalogos podem ser registrados
    // depois; a referencia so' vale ate' a proxima chamada.
    static std::vector<Language> languages;

    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    languages.clear();
    languages.push_back({"en", "English", "English"});
    for (const Catalog& catalog : s.catalogs) {
        languages.push_back(catalog.language);
    }
    return languages;
}

std::string_view current_language() {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);
    return s.active_code;
}

bool set_language(std::string_view code) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    if (code == "en") {
        s.active = nullptr;
        s.active_code = "en";
        return true;
    }

    // Correspondência exata: "pt-BR" -> pt-BR.
    for (const Catalog& catalog : s.catalogs) {
        if (catalog.language.code == code) {
            s.active = &catalog;
            s.active_code = catalog.language.code;
            return true;
        }
    }

    // Fallback por idioma base: "pt-PT" ou "pt" casam com pt-BR. Melhor uma
    // variante próxima do que cair em inglês.
    const auto dash = code.find('-');
    const std::string_view base = code.substr(0, dash);

    for (const Catalog& catalog : s.catalogs) {
        const std::string_view catalog_code = catalog.language.code;
        const auto catalog_dash = catalog_code.find('-');
        if (catalog_code.substr(0, catalog_dash) == base) {
            s.active = &catalog;
            s.active_code = catalog.language.code;
            return true;
        }
    }
    return false;
}

std::string detect_system_language() {
#ifdef _WIN32
    wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) > 0) {
        char narrow[LOCALE_NAME_MAX_LENGTH] = {};
        WideCharToMultiByte(CP_UTF8, 0, buffer, -1, narrow, sizeof(narrow),
                            nullptr, nullptr);
        return narrow;   // ex.: "pt-BR"
    }
#else
    // LANG=pt_BR.UTF-8 -> pt-BR
    if (const char* lang = std::getenv("LANG")) {
        std::string value(lang);
        const auto dot = value.find('.');
        if (dot != std::string::npos) value.resize(dot);
        std::replace(value.begin(), value.end(), '_', '-');
        return value;
    }
#endif
    return "en";
}

const char* translate(const char* text) {
    if (text == nullptr || *text == '\0') return text;

    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    if (s.active == nullptr) return text;   // inglês é a fonte

    const auto it = s.active->entries.find(text);
    if (it != s.active->entries.end()) return it->second.c_str();

    // Sem tradução: registra e devolve o inglês. Degradar para o texto original
    // é o motivo de a chave ser a própria frase em inglês.
    s.missing.insert(text);
    return text;
}

const char* translate_window(const char* text, const char* id) {
    // Mesmos buffers rotativos do translate_format: varios paineis sao
    // desenhados no mesmo frame.
    constexpr std::size_t kSlots = 8;
    constexpr std::size_t kSize = 256;

    static thread_local std::array<std::array<char, kSize>, kSlots> buffers{};
    static thread_local std::size_t slot = 0;

    char* target = buffers[slot].data();
    slot = (slot + 1) % kSlots;

    std::snprintf(target, kSize, "%s%s", translate(text), id);
    return target;
}

const char* translate_format(const char* format, ...) {
    // Buffers rotativos: permitem várias chamadas TRF na mesma expressão, como
    // em ImGui::Text("%s %s", TRF(...), TRF(...)).
    constexpr std::size_t kSlots = 8;
    constexpr std::size_t kSize = 1024;

    static thread_local std::array<std::array<char, kSize>, kSlots> buffers{};
    static thread_local std::size_t slot = 0;

    char* target = buffers[slot].data();
    slot = (slot + 1) % kSlots;

    const char* pattern = translate(format);

    va_list args;
    va_start(args, format);
    std::vsnprintf(target, kSize, pattern, args);
    va_end(args);

    return target;
}

void register_catalog(std::string_view code, std::string_view name,
                      std::string_view native_name,
                      const char* const* pairs, std::size_t count) {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    Catalog catalog;
    catalog.language = {std::string(code), std::string(name),
                        std::string(native_name)};

    // pairs vem como [original, tradução, original, tradução, ...]
    for (std::size_t i = 0; i + 1 < count; i += 2) {
        catalog.entries.emplace(pairs[i], pairs[i + 1]);
    }

    // Substitui um catálogo de mesmo código, se já existir.
    const auto it = std::find_if(
        s.catalogs.begin(), s.catalogs.end(),
        [&code](const Catalog& c) { return c.language.code == code; });

    if (it != s.catalogs.end()) {
        *it = std::move(catalog);
    } else {
        s.catalogs.push_back(std::move(catalog));
    }
    // Sem reaponte: o deque preserva a referencia ao catalogo ativo.
}

void load_catalogs(std::string_view directory) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return;

    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".lang") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file) continue;

        // Cabeçalho: linhas "# code: pt-BR" e "# name: Português (Brasil)".
        std::string code = entry.path().stem().string();
        std::string name = code;
        std::string native_name = code;

        std::vector<std::string> storage;   // mantém vivos os c_str dos pares
        std::vector<const char*> pairs;

        std::string line;
        while (std::getline(file, line)) {
            const std::string_view view = trim(line);
            if (view.empty()) continue;

            if (view.front() == '#') {
                const std::string_view meta = trim(view.substr(1));
                if (meta.starts_with("code:")) {
                    code = trim(meta.substr(5));
                } else if (meta.starts_with("name:")) {
                    name = trim(meta.substr(5));
                } else if (meta.starts_with("native:")) {
                    native_name = trim(meta.substr(7));
                }
                continue;
            }

            // original=tradução, respeitando \= no original.
            std::size_t separator = std::string_view::npos;
            for (std::size_t i = 0; i < view.size(); ++i) {
                if (view[i] == '\\') { ++i; continue; }
                if (view[i] == '=') { separator = i; break; }
            }
            if (separator == std::string_view::npos) continue;

            std::string key   = unescape(trim(view.substr(0, separator)));
            std::string value = unescape(trim(view.substr(separator + 1)));
            if (key.empty() || value.empty()) continue;

            storage.push_back(std::move(key));
            storage.push_back(std::move(value));
        }

        pairs.reserve(storage.size());
        for (const std::string& text : storage) pairs.push_back(text.c_str());

        if (!pairs.empty()) {
            register_catalog(code, name, native_name, pairs.data(), pairs.size());
        }
    }
}

std::vector<std::string> missing_translations() {
    State& s = state();
    const std::lock_guard<std::mutex> lock(s.mutex);

    std::vector<std::string> result(s.missing.begin(), s.missing.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool export_template(std::string_view path) {
    std::ofstream file{std::string(path)};
    if (!file) return false;

    file << "# C-Otter language file\n"
         << "# code: xx\n"
         << "# name: Language Name\n"
         << "# native: Nome Nativo\n"
         << "#\n"
         << "# Formato: texto em inglês=tradução\n"
         << "# Escapes: \\n \\t \\\\ \\=\n\n";

    for (const std::string& text : missing_translations()) {
        file << escape(text) << "=\n";
    }
    return true;
}

} // namespace otter::i18n
