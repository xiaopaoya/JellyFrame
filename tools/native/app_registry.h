#pragma once

#include "app_package.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe_example {

struct InstalledAppEntry {
    std::string id;
    std::string name;
    std::string role = "app";
    std::string version_name;
    int version_code = 0;
    std::string entry = "/index.html";
    std::string script_mode = "classic";
    bool network_allowed = false;
    std::string bundle_file;
    std::size_t bundle_size = 0;
    std::size_t resource_count = 0;
    std::string installed_at_utc;
};

struct InstalledAppRegistry {
    std::vector<InstalledAppEntry> apps;
};

inline std::filesystem::path registry_json_path(const std::filesystem::path& store) {
    return store / "registry.json";
}

inline std::filesystem::path registry_bundles_dir(const std::filesystem::path& store) {
    return store / "bundles";
}

inline std::filesystem::path registry_staging_dir(const std::filesystem::path& store) {
    return store / "staging";
}

inline std::string json_escape_text(std::string_view value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            output += "\\\\";
            break;
        case '"':
            output += "\\\"";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

inline std::string sanitize_registry_filename(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '_' || ch == '-' || ch == '.') {
            output.push_back(ch);
        } else {
            output.push_back('_');
        }
    }
    while (!output.empty() && (output.front() == '.' || output.front() == '_')) {
        output.erase(output.begin());
    }
    while (!output.empty() && (output.back() == '.' || output.back() == '_')) {
        output.pop_back();
    }
    return output.empty() ? std::string("app") : output;
}

inline std::string utc_now_compact() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    return std::to_string(seconds) + "Z";
}

inline std::size_t file_size_or_zero(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0U : static_cast<std::size_t>(size);
}

inline bool json_find_bool(const std::string& json, std::string_view key, bool& value) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    std::size_t index = colon + 1;
    while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    if (json.compare(index, 4, "true") == 0) {
        value = true;
        return true;
    }
    if (json.compare(index, 5, "false") == 0) {
        value = false;
        return true;
    }
    return false;
}

inline std::vector<std::string_view> split_top_level_objects(std::string_view array_text) {
    std::vector<std::string_view> objects;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t object_start = std::string_view::npos;
    for (std::size_t index = 0; index < array_text.size(); ++index) {
        const char ch = array_text[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                object_start = index;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string_view::npos) {
                objects.push_back(array_text.substr(object_start, index - object_start + 1));
                object_start = std::string_view::npos;
            }
        }
    }
    return objects;
}

inline InstalledAppEntry parse_registry_entry(std::string_view object_text) {
    const std::string object(object_text);
    InstalledAppEntry entry;
    json_find_string(object, "id", entry.id);
    json_find_string(object, "name", entry.name);
    json_find_string(object, "role", entry.role);
    json_find_string(object, "versionName", entry.version_name);
    json_find_string(object, "entry", entry.entry);
    json_find_string(object, "script", entry.script_mode);
    json_find_string(object, "bundleFile", entry.bundle_file);
    json_find_string(object, "installedAtUtc", entry.installed_at_utc);
    json_find_int(object, "versionCode", entry.version_code);
    int parsed_size = 0;
    if (json_find_int(object, "bundleSize", parsed_size) && parsed_size > 0) {
        entry.bundle_size = static_cast<std::size_t>(parsed_size);
    }
    int parsed_count = 0;
    if (json_find_int(object, "resourceCount", parsed_count) && parsed_count > 0) {
        entry.resource_count = static_cast<std::size_t>(parsed_count);
    }
    json_find_bool(object, "networkAllowed", entry.network_allowed);
    if (entry.name.empty()) {
        entry.name = entry.id;
    }
    if (entry.version_name.empty()) {
        entry.version_name = "0.0.0";
    }
    return entry;
}

inline InstalledAppRegistry load_installed_app_registry(const std::filesystem::path& store) {
    InstalledAppRegistry registry;
    const std::string text = read_text_file_limited(registry_json_path(store), 1024 * 1024);
    if (text.empty()) {
        return registry;
    }
    const std::size_t apps_key = text.find("\"apps\"");
    if (apps_key == std::string::npos) {
        return registry;
    }
    const std::size_t open = text.find('[', apps_key);
    const std::size_t close = text.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return registry;
    }
    for (const std::string_view object : split_top_level_objects(std::string_view(text).substr(open + 1, close - open - 1))) {
        InstalledAppEntry entry = parse_registry_entry(object);
        if (!entry.id.empty() && !entry.bundle_file.empty()) {
            registry.apps.push_back(std::move(entry));
        }
    }
    std::sort(registry.apps.begin(), registry.apps.end(), [](const InstalledAppEntry& left, const InstalledAppEntry& right) {
        return left.id < right.id;
    });
    return registry;
}

inline void write_installed_app_registry(const std::filesystem::path& store, const InstalledAppRegistry& registry) {
    std::filesystem::create_directories(store);
    std::filesystem::path temp_path = registry_json_path(store);
    temp_path += ".tmp";
    std::ofstream output(temp_path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open registry for writing");
    }
    output << "{\n";
    output << "  \"format\": \"jellyframe.installed_apps.registry\",\n";
    output << "  \"formatVersion\": 0,\n";
    output << "  \"apps\": [\n";
    for (std::size_t index = 0; index < registry.apps.size(); ++index) {
        const InstalledAppEntry& app = registry.apps[index];
        output << "    {\n";
        output << "      \"id\": \"" << json_escape_text(app.id) << "\",\n";
        output << "      \"name\": \"" << json_escape_text(app.name) << "\",\n";
        output << "      \"role\": \"" << json_escape_text(app.role) << "\",\n";
        output << "      \"versionName\": \"" << json_escape_text(app.version_name) << "\",\n";
        output << "      \"versionCode\": " << app.version_code << ",\n";
        output << "      \"entry\": \"" << json_escape_text(app.entry) << "\",\n";
        output << "      \"script\": \"" << json_escape_text(app.script_mode) << "\",\n";
        output << "      \"networkAllowed\": " << (app.network_allowed ? "true" : "false") << ",\n";
        output << "      \"bundleFile\": \"" << json_escape_text(app.bundle_file) << "\",\n";
        output << "      \"bundleSize\": " << app.bundle_size << ",\n";
        output << "      \"resourceCount\": " << app.resource_count << ",\n";
        output << "      \"installedAtUtc\": \"" << json_escape_text(app.installed_at_utc) << "\"\n";
        output << "    }" << (index + 1 < registry.apps.size() ? "," : "") << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    output.close();
    std::error_code remove_error;
    std::filesystem::remove(registry_json_path(store), remove_error);
    std::filesystem::rename(temp_path, registry_json_path(store));
}

inline std::filesystem::path installed_app_bundle_path(const std::filesystem::path& store,
                                                       const InstalledAppEntry& entry) {
    return registry_bundles_dir(store) / entry.bundle_file;
}

inline const InstalledAppEntry* find_installed_app(const InstalledAppRegistry& registry, std::string_view app_id) {
    for (const InstalledAppEntry& app : registry.apps) {
        if (app.id == app_id) {
            return &app;
        }
    }
    return nullptr;
}

inline std::filesystem::path find_installed_app_bundle_path(const std::filesystem::path& store, std::string_view app_id) {
    const InstalledAppRegistry registry = load_installed_app_registry(store);
    const InstalledAppEntry* entry = find_installed_app(registry, app_id);
    if (entry == nullptr) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    return installed_app_bundle_path(store, *entry);
}

inline InstalledAppEntry install_bundle_into_registry(const std::filesystem::path& store,
                                                     const std::filesystem::path& bundle_path,
                                                     std::size_t max_input_bytes) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    const std::filesystem::path absolute_bundle = std::filesystem::absolute(bundle_path);
    AppPackage package = load_jfapp_bundle(absolute_bundle, max_input_bytes);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    const std::size_t bundle_size = file_size_or_zero(absolute_bundle);
    const std::string bundle_file = sanitize_registry_filename(package.manifest.id) + "-" +
        std::to_string(package.manifest.version_code) + "-" + std::to_string(bundle_size) + ".jfapp";
    std::filesystem::create_directories(registry_bundles_dir(absolute_store));
    std::filesystem::create_directories(registry_staging_dir(absolute_store));
    const std::filesystem::path stage_path = registry_staging_dir(absolute_store) / (bundle_file + ".staging");
    const std::filesystem::path final_path = registry_bundles_dir(absolute_store) / bundle_file;
    std::error_code cleanup_error;
    std::filesystem::remove(stage_path, cleanup_error);
    std::filesystem::copy_file(absolute_bundle, stage_path, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(final_path, cleanup_error);
    std::filesystem::rename(stage_path, final_path);

    InstalledAppEntry entry;
    entry.id = package.manifest.id;
    entry.name = package.manifest.name;
    entry.role = package.manifest.role.empty() ? "app" : package.manifest.role;
    entry.version_name = package.manifest.version_name.empty() ? "0.0.0" : package.manifest.version_name;
    entry.version_code = package.manifest.version_code;
    entry.entry = package.manifest.entry;
    entry.script_mode = package.manifest.script_mode;
    entry.network_allowed = package.manifest.network_allowed;
    entry.bundle_file = bundle_file;
    entry.bundle_size = bundle_size;
    entry.resource_count = package.bundle_entries.size();
    entry.installed_at_utc = utc_now_compact();

    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == entry.id;
    });
    std::string old_bundle_file;
    if (existing == registry.apps.end()) {
        registry.apps.push_back(entry);
    } else {
        old_bundle_file = existing->bundle_file;
        *existing = entry;
    }
    std::sort(registry.apps.begin(), registry.apps.end(), [](const InstalledAppEntry& left, const InstalledAppEntry& right) {
        return left.id < right.id;
    });
    write_installed_app_registry(absolute_store, registry);
    if (!old_bundle_file.empty() && old_bundle_file != bundle_file) {
        std::error_code error;
        std::filesystem::remove(registry_bundles_dir(absolute_store) / old_bundle_file, error);
    }
    return entry;
}

inline InstalledAppEntry remove_bundle_from_registry(const std::filesystem::path& store, std::string_view app_id) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == app_id;
    });
    if (existing == registry.apps.end()) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    InstalledAppEntry removed = *existing;
    registry.apps.erase(existing);
    write_installed_app_registry(absolute_store, registry);
    std::error_code error;
    std::filesystem::remove(installed_app_bundle_path(absolute_store, removed), error);
    return removed;
}

} // namespace jellyframe_example
