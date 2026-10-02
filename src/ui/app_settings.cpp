#include "ui/app_settings.hpp"

#include "base/json.hpp"
#include "base/paths.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace otter::ui {

std::string app_settings_path() {
    namespace fs = std::filesystem;
    return (fs::path(data_directory()) / "settings.json").string();
}

AppSettings load_app_settings(const std::string& path) {
    AppSettings settings;

    std::ifstream in(path, std::ios::binary);
    if (!in) return settings;

    std::ostringstream text;
    text << in.rdbuf();

    const auto root = json::parse(text.str());
    if (!root || !root->is_object()) return settings;

    // Campo ausente fica com o padrao -- um arquivo de versao anterior
    // continua valendo.
    settings.keymap =
        std::string((*root)["keymap"].as_string(settings.keymap));
    settings.icons =
        std::string((*root)["icons"].as_string(settings.icons));
    settings.theme =
        std::string((*root)["theme"].as_string(settings.theme));
    settings.language     = std::string((*root)["language"].as_string());
    settings.side_by_side = (*root)["side-by-side"].as_bool(false);
    settings.confirm_data_save = (*root)["confirm-data-save"].as_bool(false);
    settings.multiple_results  = (*root)["multiple-results"].as_bool(false);
    settings.link_with_editor  = (*root)["link-with-editor"].as_bool(false);
    settings.connected_only    = (*root)["connected-only"].as_bool(false);

    for (const json::Value& folder : (*root)["folders"].as_array()) {
        if (!folder.as_string().empty()) {
            settings.folders.emplace_back(folder.as_string());
        }
    }
    for (const json::Value& folder : (*root)["closed-folders"].as_array()) {
        if (!folder.as_string().empty()) {
            settings.closed_folders.emplace_back(folder.as_string());
        }
    }
    for (const auto& [name, node] : (*root)["filters"].as_object()) {
        AppSettings::Filter filter;
        filter.enabled = node["enabled"].as_bool(true);
        filter.include = std::string(node["include"].as_string());
        filter.exclude = std::string(node["exclude"].as_string());
        settings.filters[name] = std::move(filter);
    }
    for (const json::Value& node : (*root)["bookmarks"].as_array()) {
        AppSettings::Bookmark bookmark;
        bookmark.title      = std::string(node["title"].as_string());
        bookmark.connection = std::string(node["connection"].as_string());
        bookmark.database   = std::string(node["database"].as_string());
        bookmark.type       = std::string(node["type"].as_string());
        bookmark.schema     = std::string(node["schema"].as_string());
        bookmark.name       = std::string(node["name"].as_string());
        bookmark.parent     = std::string(node["parent"].as_string());
        bookmark.signature  = std::string(node["signature"].as_string());
        if (!bookmark.name.empty()) settings.bookmarks.push_back(std::move(bookmark));
    }

    if (const json::Value& copy = (*root)["advanced-copy"]; copy.is_object()) {
        settings.copy_column_delimiter = std::string(
            copy["column-delimiter"].as_string(settings.copy_column_delimiter));
        settings.copy_row_delimiter = std::string(
            copy["row-delimiter"].as_string(settings.copy_row_delimiter));
        settings.copy_quote = std::string(copy["quote"].as_string(settings.copy_quote));
        settings.copy_quote_always = copy["quote-always"].as_bool(false);
        settings.copy_header       = copy["header"].as_bool(false);
        settings.copy_row_numbers  = copy["row-numbers"].as_bool(false);
        settings.copy_null_text    = std::string(copy["null-text"].as_string());
    }

    if (const json::Value& dash = (*root)["dashboard"]; dash.is_object()) {
        settings.dashboard_interval = static_cast<int>(
            dash["interval"].as_int(settings.dashboard_interval));
        settings.dashboard_points = static_cast<int>(
            dash["points"].as_int(settings.dashboard_points));
        for (const json::Value& chart : dash["charts"].as_array()) {
            settings.dashboard_charts.emplace_back(chart.as_string());
        }
    }
    if (settings.dashboard_interval < 1)  settings.dashboard_interval = 1;
    if (settings.dashboard_points < 10)   settings.dashboard_points = 10;

    if (const json::Value& window = (*root)["window"]; window.is_object()) {
        settings.window.x         = static_cast<int>(window["x"].as_int(0));
        settings.window.y         = static_cast<int>(window["y"].as_int(0));
        settings.window.width     = static_cast<int>(window["width"].as_int(0));
        settings.window.height    = static_cast<int>(window["height"].as_int(0));
        settings.window.maximized = window["maximized"].as_bool(false);
    }
    return settings;
}

bool save_app_settings(const std::string& path, const AppSettings& settings) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    json::Object root;
    root["keymap"] = json::Value(settings.keymap);
    root["icons"]  = json::Value(settings.icons);
    root["theme"]        = json::Value(settings.theme);
    root["language"]     = json::Value(settings.language);
    root["side-by-side"] = json::Value(settings.side_by_side);
    root["confirm-data-save"] = json::Value(settings.confirm_data_save);
    root["multiple-results"]  = json::Value(settings.multiple_results);
    root["link-with-editor"]  = json::Value(settings.link_with_editor);
    root["connected-only"]    = json::Value(settings.connected_only);

    json::Array folders;
    for (const std::string& folder : settings.folders) folders.emplace_back(folder);
    root["folders"] = json::Value(std::move(folders));

    json::Array closed_folders;
    for (const std::string& folder : settings.closed_folders) {
        closed_folders.emplace_back(folder);
    }
    root["closed-folders"] = json::Value(std::move(closed_folders));

    json::Object filters;
    for (const auto& [name, filter] : settings.filters) {
        json::Object node;
        node["enabled"] = json::Value(filter.enabled);
        node["include"] = json::Value(filter.include);
        node["exclude"] = json::Value(filter.exclude);
        filters[name] = json::Value(std::move(node));
    }
    root["filters"] = json::Value(std::move(filters));

    json::Array bookmarks;
    for (const AppSettings::Bookmark& bookmark : settings.bookmarks) {
        json::Object node;
        node["title"]      = json::Value(bookmark.title);
        node["connection"] = json::Value(bookmark.connection);
        node["database"]   = json::Value(bookmark.database);
        node["type"]       = json::Value(bookmark.type);
        node["schema"]     = json::Value(bookmark.schema);
        node["name"]       = json::Value(bookmark.name);
        node["parent"]     = json::Value(bookmark.parent);
        node["signature"]  = json::Value(bookmark.signature);
        bookmarks.emplace_back(std::move(node));
    }
    root["bookmarks"] = json::Value(std::move(bookmarks));

    json::Object copy;
    copy["column-delimiter"] = json::Value(settings.copy_column_delimiter);
    copy["row-delimiter"]    = json::Value(settings.copy_row_delimiter);
    copy["quote"]            = json::Value(settings.copy_quote);
    copy["quote-always"]     = json::Value(settings.copy_quote_always);
    copy["header"]           = json::Value(settings.copy_header);
    copy["row-numbers"]      = json::Value(settings.copy_row_numbers);
    copy["null-text"]        = json::Value(settings.copy_null_text);
    root["advanced-copy"] = json::Value(std::move(copy));

    json::Object dash;
    dash["interval"] = json::Value(static_cast<double>(settings.dashboard_interval));
    dash["points"]   = json::Value(static_cast<double>(settings.dashboard_points));
    json::Array charts;
    for (const std::string& chart : settings.dashboard_charts) charts.emplace_back(chart);
    dash["charts"] = json::Value(std::move(charts));
    root["dashboard"] = json::Value(std::move(dash));

    json::Object window;
    window["x"]         = json::Value(static_cast<double>(settings.window.x));
    window["y"]         = json::Value(static_cast<double>(settings.window.y));
    window["width"]     = json::Value(static_cast<double>(settings.window.width));
    window["height"]    = json::Value(static_cast<double>(settings.window.height));
    window["maximized"] = json::Value(settings.window.maximized);
    root["window"] = json::Value(std::move(window));

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << json::serialize(json::Value(std::move(root)));
    return static_cast<bool>(out);
}

AppSettings ensure_app_settings(const std::string& path) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        const AppSettings defaults;
        // Falhar aqui (pasta so' de leitura) nao impede o programa de abrir:
        // ele segue com os padroes, e a proxima gravacao avisa na tela.
        save_app_settings(path, defaults);
        return defaults;
    }
    return load_app_settings(path);
}

} // namespace otter::ui
