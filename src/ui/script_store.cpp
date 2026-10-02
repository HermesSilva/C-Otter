#include "ui/script_store.hpp"

#include "base/json.hpp"
#include "base/paths.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace otter::ui {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kExtension = ".sql";

bool ends_with_sql(std::string_view name) {
    if (name.size() < kExtension.size()) return false;
    const std::string_view tail = name.substr(name.size() - kExtension.size());
    return std::equal(tail.begin(), tail.end(), kExtension.begin(),
                      [](char a, char b) {
                          return (a >= 'A' && a <= 'Z' ? a - 'A' + 'a' : a) == b;
                      });
}

} // namespace

std::string scripts_directory() {
    if (const char* override_dir = std::getenv("OTTER_SCRIPT_DIR");
        override_dir != nullptr && override_dir[0] != '\0') {
        return override_dir;
    }
    std::string directory = executable_directory();
    if (directory.empty()) directory = ".";
    return (fs::path(directory) / ".script").string();
}

std::string script_session_path(const std::string& directory) {
    return (fs::path(directory) / "session.json").string();
}

ScriptSession load_script_session(const std::string& directory) {
    ScriptSession session;

    std::ifstream in(script_session_path(directory), std::ios::binary);
    if (!in) return session;

    std::ostringstream text;
    text << in.rdbuf();

    const auto root = json::parse(text.str());
    if (!root || !root->is_object()) return session;

    session.active = std::string((*root)["active"].as_string());
    for (const json::Value& node : (*root)["scripts"].as_array()) {
        ScriptEntry entry;
        entry.file          = std::string(node["file"].as_string());
        entry.connection_id = std::string(node["connection-id"].as_string());
        entry.connection    = std::string(node["connection"].as_string());
        entry.database      = std::string(node["database"].as_string());
        entry.title         = std::string(node["title"].as_string());
        entry.pinned        = node["pinned"].as_bool(false);
        entry.open          = node["open"].as_bool(true);
        if (!entry.file.empty()) session.scripts.push_back(std::move(entry));
    }
    return session;
}

std::string serialize_script_session(const ScriptSession& session) {
    json::Object root;
    root["active"] = json::Value(session.active);

    json::Array scripts;
    for (const ScriptEntry& entry : session.scripts) {
        json::Object node;
        node["file"]          = json::Value(entry.file);
        node["connection-id"] = json::Value(entry.connection_id);
        node["connection"]    = json::Value(entry.connection);
        node["database"]      = json::Value(entry.database);
        node["title"]         = json::Value(entry.title);
        node["pinned"]        = json::Value(entry.pinned);
        node["open"]          = json::Value(entry.open);
        scripts.emplace_back(std::move(node));
    }
    root["scripts"] = json::Value(std::move(scripts));
    return json::serialize(json::Value(std::move(root)));
}

bool save_script_session(const std::string& directory, const ScriptSession& session) {
    std::error_code ec;
    fs::create_directories(directory, ec);
    return write_script(script_session_path(directory),
                        serialize_script_session(session));
}

std::string script_path(const std::string& directory, const std::string& file) {
    const fs::path path(file);
    if (path.is_absolute()) return file;
    return (fs::path(directory) / path).string();
}

bool is_stored_script(const std::string& directory, const std::string& path) {
    std::error_code ec;
    const fs::path parent = fs::weakly_canonical(fs::path(path).parent_path(), ec);
    if (ec) return false;
    const fs::path home = fs::weakly_canonical(fs::path(directory), ec);
    if (ec) return false;
    return parent == home;
}

std::string script_file_field(const std::string& directory, const std::string& path) {
    if (is_stored_script(directory, path)) return fs::path(path).filename().string();
    return path;
}

std::string unique_script_name(const std::string& directory,
                               const std::vector<std::string>& taken) {
    const auto in_use = [&](const std::string& name) {
        if (std::find(taken.begin(), taken.end(), name) != taken.end()) return true;
        std::error_code ec;
        return fs::exists(fs::path(directory) / (name + std::string(kExtension)), ec);
    };

    // A mesma serie do DBeaver (ResourceUtils.getUniqueFile).
    std::string name = "Script";
    for (std::size_t n = 1; in_use(name); ++n) {
        name = "Script-" + std::to_string(n);
    }
    return name;
}

std::string script_file_name(std::string_view title) {
    std::string name;
    for (const char c : title) {
        const auto byte = static_cast<unsigned char>(c);
        const bool refused = byte < 0x20 || c == '<' || c == '>' || c == ':' ||
                             c == '"' || c == '/' || c == '\\' || c == '|' ||
                             c == '?' || c == '*';
        name.push_back(refused ? '_' : c);
    }
    // O Windows ignora ponto e espaco no fim do nome: "a." e "a" seriam o
    // mesmo arquivo.
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    if (name.empty()) name = "Script";
    if (!ends_with_sql(name)) name += kExtension;
    return name;
}

bool write_script(const std::string& path, std::string_view text) {
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);

    const fs::path temporary = fs::path(path + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) return false;
    }
    fs::rename(temporary, target, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

std::optional<std::string> read_script(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

std::vector<std::string> list_scripts(const std::string& directory) {
    std::vector<std::string> names;
    std::error_code ec;
    for (fs::directory_iterator it(directory, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string name = it->path().filename().string();
        if (ends_with_sql(name)) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace otter::ui
