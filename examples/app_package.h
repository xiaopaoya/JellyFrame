#pragma once

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe_example {

struct AppPackageManifest {
    std::string id;
    std::string name;
    std::string version_name;
    std::string min_jellyframe;
    int version_code = 0;
    std::string entry = "/index.html";
    std::string script_mode = "classic";
    int viewport_width = 0;
    int viewport_height = 0;
    bool network_allowed = false;
};

struct AppPackage {
    std::filesystem::path root;
    AppPackageManifest manifest;
};

struct PackageResourceStats {
    std::size_t successful_loads = 0;
    std::size_t missing_loads = 0;
    std::size_t rejected_loads = 0;
    std::size_t loaded_bytes = 0;
};

struct PackageResourceContext {
    std::filesystem::path root;
    std::string base_url = "/index.html";
    std::size_t max_input_bytes = 512 * 1024;
    PackageResourceStats* stats = nullptr;
};

inline std::string read_text_file_limited(const std::filesystem::path& path, std::size_t max_input_bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < max_input_bytes) {
        const std::size_t remaining = max_input_bytes - total;
        const std::size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        file.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize read = file.gcount();
        if (read <= 0) {
            break;
        }
        output.write(buffer, read);
        total += static_cast<std::size_t>(read);
    }
    return output.str();
}

inline void record_package_success(PackageResourceContext* context, std::size_t bytes) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->successful_loads;
        context->stats->loaded_bytes += bytes;
    }
}

inline void record_package_missing(PackageResourceContext* context) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->missing_loads;
    }
}

inline void record_package_rejected(PackageResourceContext* context) {
    if (context != nullptr && context->stats != nullptr) {
        ++context->stats->rejected_loads;
    }
}

inline bool is_local_package_url(std::string_view url) {
    return !url.empty() && url.find(':') == std::string_view::npos && url.rfind("//", 0) != 0;
}

inline std::string package_base_directory(std::string_view base_url) {
    if (base_url.empty() || base_url.front() != '/') {
        return "/";
    }
    const std::size_t slash = base_url.rfind('/');
    if (slash == std::string_view::npos || slash == 0) {
        return "/";
    }
    return std::string(base_url.substr(0, slash + 1));
}

inline bool normalize_app_path(std::string_view path, std::string& output) {
    if (path.empty() || path.front() != '/') {
        return false;
    }

    std::vector<std::string_view> segments;
    std::size_t index = 1;
    while (index <= path.size()) {
        const std::size_t next = path.find('/', index);
        const std::size_t end = next == std::string_view::npos ? path.size() : next;
        const std::string_view segment = path.substr(index, end - index);
        if (!segment.empty() && segment != ".") {
            if (segment == "..") {
                if (segments.empty()) {
                    return false;
                }
                segments.pop_back();
            } else {
                segments.push_back(segment);
            }
        }
        if (next == std::string_view::npos) {
            break;
        }
        index = next + 1;
    }

    output = "/";
    for (std::size_t segment_index = 0; segment_index < segments.size(); ++segment_index) {
        if (segment_index != 0) {
            output.push_back('/');
        }
        output.append(segments[segment_index]);
    }
    return true;
}

inline bool resolve_package_url(std::string_view url, std::string_view base_url, std::string& output) {
    if (!is_local_package_url(url)) {
        return false;
    }
    if (url.front() == '/') {
        return normalize_app_path(url, output);
    }
    const std::string joined = package_base_directory(base_url) + std::string(url);
    return normalize_app_path(joined, output);
}

inline std::filesystem::path package_file_path(const std::filesystem::path& root, std::string_view app_path) {
    std::filesystem::path relative;
    std::size_t index = app_path.empty() || app_path.front() != '/' ? 0 : 1;
    while (index <= app_path.size()) {
        const std::size_t next = app_path.find('/', index);
        const std::size_t end = next == std::string_view::npos ? app_path.size() : next;
        const std::string_view segment = app_path.substr(index, end - index);
        if (!segment.empty()) {
            relative /= std::filesystem::path(std::string(segment));
        }
        if (next == std::string_view::npos) {
            break;
        }
        index = next + 1;
    }
    return root / relative;
}

inline bool load_package_resource(std::string_view url,
                                  std::string_view base_url,
                                  std::string& output,
                                  PackageResourceContext* context) {
    output.clear();
    if (context == nullptr) {
        return false;
    }
    std::string resolved;
    if (!resolve_package_url(url, base_url.empty() ? context->base_url : std::string(base_url), resolved)) {
        record_package_rejected(context);
        return false;
    }
    output = read_text_file_limited(package_file_path(context->root, resolved), context->max_input_bytes);
    if (output.empty()) {
        record_package_missing(context);
        return false;
    }
    record_package_success(context, output.size());
    return true;
}

inline bool load_package_stylesheet(std::string_view href, std::string& output, void* raw_context) {
    auto* context = static_cast<PackageResourceContext*>(raw_context);
    return load_package_resource(href, {}, output, context);
}

inline bool load_package_script(std::string_view src, std::string& output, void* raw_context) {
    auto* context = static_cast<PackageResourceContext*>(raw_context);
    return load_package_resource(src, {}, output, context);
}

inline bool json_find_string(const std::string& json, std::string_view key, std::string& value) {
    // Minimal manifest reader for example shells. The authoritative validator is
    // tools/package_app.py, which uses a real JSON parser.
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    std::size_t quote = json.find('"', colon + 1);
    if (quote == std::string::npos) {
        return false;
    }
    std::string parsed;
    for (++quote; quote < json.size(); ++quote) {
        const char ch = json[quote];
        if (ch == '"') {
            value = std::move(parsed);
            return true;
        }
        if (ch == '\\' && quote + 1 < json.size()) {
            const char escaped = json[++quote];
            parsed.push_back(escaped == 'n' ? '\n' : escaped);
            continue;
        }
        parsed.push_back(ch);
    }
    return false;
}

inline bool json_find_int(const std::string& json, std::string_view key, int& value) {
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
    int parsed = 0;
    bool any = false;
    while (index < json.size() && std::isdigit(static_cast<unsigned char>(json[index])) != 0) {
        any = true;
        parsed = parsed * 10 + (json[index] - '0');
        ++index;
    }
    if (!any) {
        return false;
    }
    value = parsed;
    return true;
}

inline bool json_array_contains_string(const std::string& json, std::string_view key, std::string_view expected) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return false;
    }
    const std::size_t open = json.find('[', key_pos + needle.size());
    const std::size_t close = json.find(']', open == std::string::npos ? key_pos + needle.size() : open + 1);
    if (open == std::string::npos || close == std::string::npos) {
        return false;
    }
    return json.substr(open, close - open).find("\"" + std::string(expected) + "\"") != std::string::npos;
}

inline AppPackageManifest parse_app_manifest_text(const std::string& json) {
    AppPackageManifest manifest;
    json_find_string(json, "id", manifest.id);
    json_find_string(json, "name", manifest.name);
    json_find_string(json, "entry", manifest.entry);
    json_find_string(json, "script", manifest.script_mode);
    json_find_string(json, "minJellyFrame", manifest.min_jellyframe);
    json_find_int(json, "code", manifest.version_code);
    json_find_int(json, "width", manifest.viewport_width);
    json_find_int(json, "height", manifest.viewport_height);
    if (manifest.viewport_width <= 0) {
        json_find_int(json, "designWidth", manifest.viewport_width);
    }
    if (manifest.viewport_height <= 0) {
        json_find_int(json, "designHeight", manifest.viewport_height);
    }
    manifest.network_allowed =
        json_array_contains_string(json, "permissions", "network") ||
        json_array_contains_string(json, "capabilities", "network.fetch");

    std::string normalized_entry;
    if (!normalize_app_path(manifest.entry.empty() ? std::string_view{"/index.html"} : std::string_view{manifest.entry},
                            normalized_entry)) {
        throw std::runtime_error("manifest entry must be an absolute local app path");
    }
    manifest.entry = normalized_entry;
    if (manifest.id.empty()) {
        throw std::runtime_error("manifest id is required");
    }
    if (manifest.name.empty()) {
        manifest.name = manifest.id;
    }
    return manifest;
}

inline AppPackage load_app_package(const std::filesystem::path& package_root, std::size_t max_input_bytes) {
    const std::filesystem::path root = std::filesystem::absolute(package_root);
    const std::filesystem::path manifest_path = root / "jellyframe.app.json";
    std::string manifest_text = read_text_file_limited(manifest_path, max_input_bytes);
    if (manifest_text.empty()) {
        throw std::runtime_error("failed to read jellyframe.app.json");
    }
    return AppPackage{root, parse_app_manifest_text(manifest_text)};
}

} // namespace jellyframe_example
