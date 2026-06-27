#pragma once

#include "render_core/diagnostics.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace jellyframe_example {

constexpr std::uint32_t kJfappFnvOffset = 0x811c9dc5U;
constexpr std::uint32_t kJfappFnvPrime = 0x01000193U;
constexpr std::size_t kJfappHeaderSize = 56;
constexpr std::size_t kJfappResourceEntrySize = 28;

struct AppPackageManifest {
    std::string id;
    std::string name;
    std::string role = "app";
    std::string version_name;
    std::string min_jellyframe;
    int version_code = 0;
    std::string entry = "/index.html";
    std::string script_mode = "classic";
    int viewport_width = 0;
    int viewport_height = 0;
    bool network_allowed = false;
    bool storage_kv_allowed = false;
    bool audio_playback_allowed = false;
    bool sensor_accelerometer_allowed = false;
    bool sensor_gyroscope_allowed = false;
    bool sensor_heart_rate_allowed = false;
    bool sensor_ambient_light_allowed = false;
    bool location_position_allowed = false;
    bool background_network_while_suspended = false;
    bool background_network_while_screen_off = false;
    bool background_audio_while_suspended = false;
    bool background_audio_while_screen_off = false;
    bool background_sensors_while_suspended = false;
    bool background_sensors_while_screen_off = false;
    bool background_sensors_in_low_power = false;
    bool background_location_while_suspended = false;
    bool background_location_while_screen_off = false;
    bool background_location_in_low_power = false;
    std::vector<std::string> font_sources;
    std::vector<std::string> font_families;
};

struct BundleResourceEntry {
    std::string path;
    std::uint32_t path_hash = 0;
    std::uint16_t kind = 0;
    std::uint32_t payload_offset = 0;
    std::uint32_t payload_size = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t flags = 0;
};

struct AppPackage {
    std::filesystem::path root;
    AppPackageManifest manifest;
    std::vector<std::uint8_t> bundle_bytes;
    std::vector<BundleResourceEntry> bundle_entries;
    std::uint32_t bundle_payload_offset = 0;
};

struct PackageResourceStats {
    std::size_t successful_loads = 0;
    std::size_t missing_loads = 0;
    std::size_t rejected_loads = 0;
    std::size_t loaded_bytes = 0;
};

struct PackageResourceView {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    bool stable = false;
};

struct PackageResourceContext {
    std::filesystem::path root;
    std::string base_url = "/index.html";
    std::size_t max_input_bytes = 512 * 1024;
    PackageResourceStats* stats = nullptr;
    jellyframe::DiagnosticSink* diagnostics = nullptr;
    std::vector<std::uint8_t> bundle_bytes;
    std::vector<BundleResourceEntry> bundle_entries;
    std::uint32_t bundle_payload_offset = 0;

    bool bundle_mode() const {
        return !bundle_bytes.empty();
    }
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

inline std::vector<std::uint8_t> read_binary_file_limited(const std::filesystem::path& path,
                                                          std::size_t max_input_bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    std::vector<std::uint8_t> output;
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
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer);
        output.insert(output.end(), bytes, bytes + read);
        total += static_cast<std::size_t>(read);
    }
    return output;
}

inline std::uint16_t read_le16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

inline std::uint32_t read_le32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

inline void write_le32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xffU);
    data[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    data[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    data[3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

inline std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t size) {
    crc = ~crc;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

inline std::uint32_t crc32_bytes(const std::uint8_t* data, std::size_t size) {
    return crc32_update(0, data, size);
}

inline std::uint32_t fnv1a_32(std::string_view value) {
    std::uint32_t result = kJfappFnvOffset;
    for (const char ch : value) {
        result ^= static_cast<std::uint8_t>(ch);
        result *= kJfappFnvPrime;
    }
    return result;
}

inline bool byte_range_is_valid(std::size_t total, std::size_t offset, std::size_t size) {
    return offset <= total && size <= total - offset;
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
        jellyframe::report_diagnostic(context->diagnostics,
                                      jellyframe::DiagnosticStage::Package,
                                      jellyframe::DiagnosticSeverity::Warning,
                                      "package-resource-rejected",
                                      "Package resource URL was rejected because it is not a local app path",
                                      url);
        return false;
    }
    if (context->bundle_mode()) {
        const std::uint32_t resolved_hash = fnv1a_32(resolved);
        for (const BundleResourceEntry& entry : context->bundle_entries) {
            if (entry.path_hash != resolved_hash || entry.path != resolved) {
                continue;
            }
            const std::size_t absolute_payload_offset =
                static_cast<std::size_t>(context->bundle_payload_offset) + entry.payload_offset;
            if (entry.payload_size > context->max_input_bytes ||
                !byte_range_is_valid(context->bundle_bytes.size(), absolute_payload_offset, entry.payload_size)) {
                record_package_rejected(context);
                jellyframe::report_diagnostic(context->diagnostics,
                                              jellyframe::DiagnosticStage::Package,
                                              jellyframe::DiagnosticSeverity::Warning,
                                              "package-resource-rejected",
                                              "Bundle resource exceeds the loader budget or points outside the payload",
                                              resolved);
                return false;
            }
            const std::uint8_t* begin = context->bundle_bytes.data() + absolute_payload_offset;
            if (crc32_bytes(begin, entry.payload_size) != entry.crc32) {
                record_package_rejected(context);
                jellyframe::report_diagnostic(context->diagnostics,
                                              jellyframe::DiagnosticStage::Package,
                                              jellyframe::DiagnosticSeverity::Warning,
                                              "package-resource-crc-mismatch",
                                              "Bundle resource checksum did not match its index entry",
                                              resolved);
                return false;
            }
            output.assign(reinterpret_cast<const char*>(begin),
                          reinterpret_cast<const char*>(begin + entry.payload_size));
            if (output.empty()) {
                record_package_missing(context);
                return false;
            }
            record_package_success(context, output.size());
            return true;
        }
        record_package_missing(context);
        jellyframe::report_diagnostic(context->diagnostics,
                                      jellyframe::DiagnosticStage::Package,
                                      jellyframe::DiagnosticSeverity::Warning,
                                      "package-resource-missing",
                                      "Bundle resource was not present in the package index",
                                      resolved);
        return false;
    }
    output = read_text_file_limited(package_file_path(context->root, resolved), context->max_input_bytes);
    if (output.empty()) {
        record_package_missing(context);
        jellyframe::report_diagnostic(context->diagnostics,
                                      jellyframe::DiagnosticStage::Package,
                                      jellyframe::DiagnosticSeverity::Warning,
                                      "package-resource-missing",
                                      "Package resource could not be loaded or was empty",
                                      resolved);
        return false;
    }
    record_package_success(context, output.size());
    return true;
}

inline bool load_package_resource_view(std::string_view url,
                                       std::string_view base_url,
                                       PackageResourceView& output,
                                       PackageResourceContext* context) {
    output = PackageResourceView{};
    if (context == nullptr || !context->bundle_mode()) {
        return false;
    }
    std::string resolved;
    if (!resolve_package_url(url, base_url.empty() ? context->base_url : std::string(base_url), resolved)) {
        record_package_rejected(context);
        jellyframe::report_diagnostic(context->diagnostics,
                                      jellyframe::DiagnosticStage::Package,
                                      jellyframe::DiagnosticSeverity::Warning,
                                      "package-resource-rejected",
                                      "Package resource URL was rejected because it is not a local app path",
                                      url);
        return false;
    }
    const std::uint32_t resolved_hash = fnv1a_32(resolved);
    for (const BundleResourceEntry& entry : context->bundle_entries) {
        if (entry.path_hash != resolved_hash || entry.path != resolved) {
            continue;
        }
        const std::size_t absolute_payload_offset =
            static_cast<std::size_t>(context->bundle_payload_offset) + entry.payload_offset;
        if (entry.payload_size > context->max_input_bytes ||
            !byte_range_is_valid(context->bundle_bytes.size(), absolute_payload_offset, entry.payload_size)) {
            record_package_rejected(context);
            jellyframe::report_diagnostic(context->diagnostics,
                                          jellyframe::DiagnosticStage::Package,
                                          jellyframe::DiagnosticSeverity::Warning,
                                          "package-resource-rejected",
                                          "Bundle resource exceeds the loader budget or points outside the payload",
                                          resolved);
            return false;
        }
        const std::uint8_t* begin = context->bundle_bytes.data() + absolute_payload_offset;
        if (crc32_bytes(begin, entry.payload_size) != entry.crc32) {
            record_package_rejected(context);
            jellyframe::report_diagnostic(context->diagnostics,
                                          jellyframe::DiagnosticStage::Package,
                                          jellyframe::DiagnosticSeverity::Warning,
                                          "package-resource-crc-mismatch",
                                          "Bundle resource checksum did not match its index entry",
                                          resolved);
            return false;
        }
        if (entry.payload_size == 0) {
            record_package_missing(context);
            return false;
        }
        output = PackageResourceView{begin, entry.payload_size, true};
        record_package_success(context, entry.payload_size);
        return true;
    }
    record_package_missing(context);
    jellyframe::report_diagnostic(context->diagnostics,
                                  jellyframe::DiagnosticStage::Package,
                                  jellyframe::DiagnosticSeverity::Warning,
                                  "package-resource-missing",
                                  "Bundle resource was not present in the package index",
                                  resolved);
    return false;
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

inline bool json_find_object_range(std::string_view json,
                                   std::string_view key,
                                   std::size_t& object_open,
                                   std::size_t& object_close) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string_view::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string_view::npos) {
        return false;
    }
    const std::size_t open = json.find('{', colon + 1);
    if (open == std::string_view::npos) {
        return false;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t index = open; index < json.size(); ++index) {
        const char ch = json[index];
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
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                object_open = open;
                object_close = index + 1;
                return true;
            }
        }
    }
    return false;
}

inline bool json_find_bool(std::string_view json, std::string_view key, bool& value) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const std::size_t key_pos = json.find(needle);
    if (key_pos == std::string_view::npos) {
        return false;
    }
    const std::size_t colon = json.find(':', key_pos + needle.size());
    if (colon == std::string_view::npos) {
        return false;
    }
    std::size_t index = colon + 1;
    while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index])) != 0) {
        ++index;
    }
    if (json.substr(index, 4) == "true") {
        value = true;
        return true;
    }
    if (json.substr(index, 5) == "false") {
        value = false;
        return true;
    }
    return false;
}

inline bool json_find_nested_bool(const std::string& json,
                                  std::string_view object_key,
                                  std::string_view child_key,
                                  std::string_view field_key,
                                  bool& value) {
    std::size_t object_open = 0;
    std::size_t object_close = 0;
    if (!json_find_object_range(json, object_key, object_open, object_close)) {
        return false;
    }
    const std::string_view object(json.data() + object_open, object_close - object_open);
    std::size_t child_open = 0;
    std::size_t child_close = 0;
    if (!json_find_object_range(object, child_key, child_open, child_close)) {
        return false;
    }
    const std::string_view child(object.data() + child_open, child_close - child_open);
    return json_find_bool(child, field_key, value);
}

inline std::vector<std::string> json_collect_object_string_values(const std::string& json,
                                                                  std::string_view array_key,
                                                                  std::string_view field_key) {
    std::vector<std::string> values;
    const std::string array_needle = "\"" + std::string(array_key) + "\"";
    const std::string field_needle = "\"" + std::string(field_key) + "\"";
    const std::size_t key_pos = json.find(array_needle);
    if (key_pos == std::string::npos) {
        return values;
    }
    const std::size_t open = json.find('[', key_pos + array_needle.size());
    if (open == std::string::npos) {
        return values;
    }

    int array_depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t close = std::string::npos;
    for (std::size_t index = open; index < json.size(); ++index) {
        const char ch = json[index];
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
        } else if (ch == '[') {
            ++array_depth;
        } else if (ch == ']') {
            --array_depth;
            if (array_depth == 0) {
                close = index;
                break;
            }
        }
    }
    if (close == std::string::npos) {
        return values;
    }

    int object_depth = 0;
    bool object_in_string = false;
    bool object_escaped = false;
    std::size_t object_open = std::string::npos;
    for (std::size_t index = open + 1; index < close; ++index) {
        const char ch = json[index];
        if (object_in_string) {
            if (object_escaped) {
                object_escaped = false;
            } else if (ch == '\\') {
                object_escaped = true;
            } else if (ch == '"') {
                object_in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            object_in_string = true;
            continue;
        }
        if (ch == '{') {
            if (object_depth == 0) {
                object_open = index;
            }
            ++object_depth;
            continue;
        }
        if (ch != '}' || object_depth <= 0) {
            continue;
        }
        --object_depth;
        if (object_depth != 0 || object_open == std::string::npos) {
            continue;
        }

        const std::size_t object_close = index + 1;
        const std::size_t field_pos = json.find(field_needle, object_open);
        if (field_pos == std::string::npos || field_pos >= object_close) {
            object_open = std::string::npos;
            continue;
        }
        const std::size_t colon = json.find(':', field_pos + field_needle.size());
        if (colon == std::string::npos || colon >= object_close) {
            object_open = std::string::npos;
            continue;
        }
        std::size_t quote = json.find('"', colon + 1);
        if (quote == std::string::npos || quote >= object_close) {
            object_open = std::string::npos;
            continue;
        }
        std::string parsed;
        for (++quote; quote < object_close; ++quote) {
            const char ch = json[quote];
            if (ch == '"') {
                values.push_back(std::move(parsed));
                break;
            }
            if (ch == '\\' && quote + 1 < object_close) {
                const char escaped_ch = json[++quote];
                parsed.push_back(escaped_ch == 'n' ? '\n' : escaped_ch);
                continue;
            }
            parsed.push_back(ch);
        }
        object_open = std::string::npos;
    }
    return values;
}

inline AppPackageManifest parse_app_manifest_text(const std::string& json) {
    AppPackageManifest manifest;
    json_find_string(json, "id", manifest.id);
    json_find_string(json, "name", manifest.name);
    json_find_string(json, "role", manifest.role);
    json_find_string(json, "versionName", manifest.version_name);
    json_find_string(json, "entry", manifest.entry);
    json_find_string(json, "script", manifest.script_mode);
    json_find_string(json, "minJellyFrame", manifest.min_jellyframe);
    if (!json_find_int(json, "versionCode", manifest.version_code)) {
        json_find_int(json, "code", manifest.version_code);
    }
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
    manifest.storage_kv_allowed = json_array_contains_string(json, "capabilities", "storage.kv");
    manifest.audio_playback_allowed = json_array_contains_string(json, "capabilities", "media.audio.mp3");
    manifest.sensor_accelerometer_allowed =
        json_array_contains_string(json, "capabilities", "sensor.accelerometer");
    manifest.sensor_gyroscope_allowed = json_array_contains_string(json, "capabilities", "sensor.gyroscope");
    manifest.sensor_heart_rate_allowed = json_array_contains_string(json, "capabilities", "sensor.heart-rate");
    manifest.sensor_ambient_light_allowed =
        json_array_contains_string(json, "capabilities", "sensor.ambient-light");
    manifest.location_position_allowed = json_array_contains_string(json, "capabilities", "location.position");
    json_find_nested_bool(json,
                          "backgroundServices",
                          "network",
                          "whileSuspended",
                          manifest.background_network_while_suspended);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "network",
                          "whileScreenOff",
                          manifest.background_network_while_screen_off);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "audio",
                          "whileSuspended",
                          manifest.background_audio_while_suspended);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "audio",
                          "whileScreenOff",
                          manifest.background_audio_while_screen_off);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "sensors",
                          "whileSuspended",
                          manifest.background_sensors_while_suspended);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "sensors",
                          "whileScreenOff",
                          manifest.background_sensors_while_screen_off);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "sensors",
                          "inLowPower",
                          manifest.background_sensors_in_low_power);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "location",
                          "whileSuspended",
                          manifest.background_location_while_suspended);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "location",
                          "whileScreenOff",
                          manifest.background_location_while_screen_off);
    json_find_nested_bool(json,
                          "backgroundServices",
                          "location",
                          "inLowPower",
                          manifest.background_location_in_low_power);
    manifest.font_sources = json_collect_object_string_values(json, "fonts", "source");
    manifest.font_families = json_collect_object_string_values(json, "fonts", "family");
    if (manifest.font_families.size() != manifest.font_sources.size()) {
        manifest.font_families.clear();
    }
    for (std::string& source : manifest.font_sources) {
        std::string normalized;
        if (!normalize_app_path(source, normalized)) {
            throw std::runtime_error("manifest font source must be a local app path");
        }
        source = std::move(normalized);
    }

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

inline AppPackage load_jfapp_bundle(const std::filesystem::path& bundle_path, std::size_t max_input_bytes) {
    std::vector<std::uint8_t> bytes = read_binary_file_limited(bundle_path, max_input_bytes);
    if (bytes.size() < kJfappHeaderSize) {
        throw std::runtime_error("failed to read .jfapp header");
    }
    if (std::string_view(reinterpret_cast<const char*>(bytes.data()), 8) != std::string_view("JFAPPV0\0", 8)) {
        throw std::runtime_error("invalid .jfapp magic");
    }
    const std::uint16_t header_size = read_le16(bytes.data() + 8);
    const std::uint16_t format_version = read_le16(bytes.data() + 10);
    if (header_size != kJfappHeaderSize || format_version != 0) {
        throw std::runtime_error("unsupported .jfapp format version");
    }
    const std::uint32_t summary_offset = read_le32(bytes.data() + 16);
    const std::uint32_t summary_size = read_le32(bytes.data() + 20);
    const std::uint32_t index_offset = read_le32(bytes.data() + 24);
    const std::uint32_t resource_count = read_le32(bytes.data() + 28);
    const std::uint32_t strings_offset = read_le32(bytes.data() + 32);
    const std::uint32_t strings_size = read_le32(bytes.data() + 36);
    const std::uint32_t payload_offset = read_le32(bytes.data() + 40);
    const std::uint32_t payload_size = read_le32(bytes.data() + 44);
    const std::uint32_t expected_crc32 = read_le32(bytes.data() + 48);
    if (!byte_range_is_valid(bytes.size(), summary_offset, summary_size) ||
        !byte_range_is_valid(bytes.size(), index_offset, resource_count * kJfappResourceEntrySize) ||
        !byte_range_is_valid(bytes.size(), strings_offset, strings_size) ||
        !byte_range_is_valid(bytes.size(), payload_offset, payload_size)) {
        throw std::runtime_error(".jfapp contains an out-of-range section");
    }
    if (expected_crc32 != 0) {
        std::vector<std::uint8_t> crc_bytes = bytes;
        write_le32(crc_bytes.data() + 48, 0);
        if (crc32_bytes(crc_bytes.data(), crc_bytes.size()) != expected_crc32) {
            throw std::runtime_error(".jfapp checksum mismatch");
        }
    }

    std::string summary(reinterpret_cast<const char*>(bytes.data() + summary_offset),
                        reinterpret_cast<const char*>(bytes.data() + summary_offset + summary_size));
    std::vector<BundleResourceEntry> entries;
    entries.reserve(resource_count);
    for (std::uint32_t index = 0; index < resource_count; ++index) {
        const std::uint8_t* raw = bytes.data() + index_offset + index * kJfappResourceEntrySize;
        const std::uint32_t path_hash = read_le32(raw);
        const std::uint32_t path_offset = read_le32(raw + 4);
        const std::uint16_t path_size = read_le16(raw + 8);
        const std::uint16_t kind = read_le16(raw + 10);
        const std::uint32_t entry_payload_offset = read_le32(raw + 12);
        const std::uint32_t entry_payload_size = read_le32(raw + 16);
        const std::uint32_t entry_crc32 = read_le32(raw + 20);
        const std::uint32_t flags = read_le32(raw + 24);
        if (!byte_range_is_valid(strings_size, path_offset, path_size) ||
            !byte_range_is_valid(payload_size, entry_payload_offset, entry_payload_size)) {
            throw std::runtime_error(".jfapp resource entry points outside its section");
        }
        const char* path_begin = reinterpret_cast<const char*>(bytes.data() + strings_offset + path_offset);
        entries.push_back(BundleResourceEntry{
            std::string(path_begin, path_begin + path_size),
            path_hash,
            kind,
            entry_payload_offset,
            entry_payload_size,
            entry_crc32,
            flags,
        });
    }

    AppPackage package;
    package.root = std::filesystem::absolute(bundle_path).parent_path();
    package.manifest = parse_app_manifest_text(summary);
    package.bundle_payload_offset = payload_offset;
    package.bundle_entries = std::move(entries);
    package.bundle_bytes = std::move(bytes);
    return package;
}

inline AppPackage load_app_package(const std::filesystem::path& package_root, std::size_t max_input_bytes) {
    const std::filesystem::path root = std::filesystem::absolute(package_root);
    if (std::filesystem::is_regular_file(root)) {
        return load_jfapp_bundle(root, max_input_bytes);
    }
    const std::filesystem::path manifest_path = root / "jellyframe.app.json";
    std::string manifest_text = read_text_file_limited(manifest_path, max_input_bytes);
    if (manifest_text.empty()) {
        throw std::runtime_error("failed to read jellyframe.app.json");
    }
    return AppPackage{root, parse_app_manifest_text(manifest_text)};
}

} // namespace jellyframe_example
