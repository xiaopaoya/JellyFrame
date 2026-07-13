#pragma once

#include "app_package.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace jellyframe_example {

struct InstalledAppEntry {
    std::string id;
    std::string name;
    std::string role = "app";
    std::string status = "installed";
    bool enabled = true;
    std::string version_name;
    int version_code = 0;
    std::string entry = "/index.html";
    std::string script_mode = "classic";
    bool network_allowed = false;
    std::string bundle_file;
    std::size_t bundle_size = 0;
    std::size_t resource_count = 0;
    std::string installed_at_utc;
    std::string updated_at_utc;
    bool has_failure = false;
    std::string failure_reason;
    std::string failure_message;
    std::string failed_at_utc;
    bool has_rollback = false;
    std::string rollback_name;
    std::string rollback_role = "app";
    std::string rollback_version_name;
    int rollback_version_code = 0;
    std::string rollback_entry = "/index.html";
    std::string rollback_script_mode = "classic";
    bool rollback_network_allowed = false;
    std::string rollback_bundle_file;
    std::size_t rollback_bundle_size = 0;
    std::size_t rollback_resource_count = 0;
    std::string rollback_installed_at_utc;
    std::string rollback_updated_at_utc;
};

struct InstalledAppRegistry {
    std::vector<InstalledAppEntry> apps;
};

struct InstallCandidate {
    std::filesystem::path bundle_path;
    std::string bundle_sha256;
};

inline void sha256_transform(std::array<std::uint32_t, 8>& state, const std::uint8_t* block) {
    static constexpr std::array<std::uint32_t, 64> kRoundConstants = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        words[index] = (static_cast<std::uint32_t>(block[index * 4]) << 24U) |
            (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16U) |
            (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8U) |
            static_cast<std::uint32_t>(block[index * 4 + 3]);
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const std::uint32_t s0 = ((words[index - 15] >> 7U) | (words[index - 15] << 25U)) ^
            ((words[index - 15] >> 18U) | (words[index - 15] << 14U)) ^ (words[index - 15] >> 3U);
        const std::uint32_t s1 = ((words[index - 2] >> 17U) | (words[index - 2] << 15U)) ^
            ((words[index - 2] >> 19U) | (words[index - 2] << 13U)) ^ (words[index - 2] >> 10U);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint32_t s1 = ((e >> 6U) | (e << 26U)) ^ ((e >> 11U) | (e << 21U)) ^ ((e >> 25U) | (e << 7U));
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choice + kRoundConstants[index] + words[index];
        const std::uint32_t s0 = ((a >> 2U) | (a << 30U)) ^ ((a >> 13U) | (a << 19U)) ^ ((a >> 22U) | (a << 10U));
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

inline std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open bundle for hashing: " + path.string());
    }
    std::array<std::uint32_t, 8> state = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> block{};
    std::uint64_t byte_count = 0;
    while (input.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()))) {
        byte_count += block.size();
        sha256_transform(state, block.data());
    }
    const std::streamsize tail_count = input.gcount();
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("failed while hashing bundle: " + path.string());
    }
    byte_count += static_cast<std::uint64_t>(tail_count);
    block[static_cast<std::size_t>(tail_count)] = 0x80U;
    for (std::size_t index = static_cast<std::size_t>(tail_count) + 1; index < block.size(); ++index) {
        block[index] = 0;
    }
    if (tail_count >= 56) {
        sha256_transform(state, block.data());
        block.fill(0);
    }
    const std::uint64_t bit_count = byte_count * 8U;
    for (std::size_t index = 0; index < 8; ++index) {
        block[63 - index] = static_cast<std::uint8_t>(bit_count >> (index * 8U));
    }
    sha256_transform(state, block.data());
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t word : state) {
        output << std::setw(8) << word;
    }
    return output.str();
}

struct AppManagerAppState {
    InstalledAppEntry app;
    bool launchable = false;
    bool rollback_ready = false;
};

struct AppManagerStateSummary {
    std::size_t app_count = 0;
    std::size_t launchable_count = 0;
    std::size_t failed_count = 0;
    std::size_t rollback_ready_count = 0;
};

struct AppManagerState {
    std::vector<AppManagerAppState> apps;
    AppManagerStateSummary summary;
};

struct RegistryStorageRecovery {
    std::size_t stale_staging_files_removed = 0;
    std::size_t orphan_bundle_files_removed = 0;
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

inline std::filesystem::path registry_data_dir(const std::filesystem::path& store) {
    return store / "data";
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

inline std::filesystem::path registry_app_data_dir(const std::filesystem::path& store, std::string_view app_id) {
    return registry_data_dir(store) / sanitize_registry_filename(app_id);
}

inline void candidate_json_skip_whitespace(std::string_view text, std::size_t& position) {
    while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0) {
        ++position;
    }
}

inline bool candidate_json_read_string(std::string_view text, std::size_t& position, std::string& output) {
    if (position >= text.size() || text[position] != '"') {
        return false;
    }
    output.clear();
    ++position;
    while (position < text.size()) {
        const char ch = text[position++];
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (position >= text.size()) {
                return false;
            }
            const char escaped = text[position++];
            output.push_back(escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped == 't' ? '\t' : escaped);
            continue;
        }
        output.push_back(ch);
    }
    return false;
}

inline bool candidate_json_value_end(std::string_view text, std::size_t position, std::size_t& end) {
    if (position >= text.size()) {
        return false;
    }
    if (text[position] == '"') {
        std::string ignored;
        if (!candidate_json_read_string(text, position, ignored)) {
            return false;
        }
        end = position;
        return true;
    }
    if (text[position] != '{' && text[position] != '[') {
        while (position < text.size() && text[position] != ',' && text[position] != '}') {
            ++position;
        }
        end = position;
        return true;
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; position < text.size(); ++position) {
        const char ch = text[position];
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
        } else if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            if (--depth == 0) {
                end = position + 1;
                return true;
            }
        }
    }
    return false;
}

inline bool candidate_json_is_single_object(std::string_view text) {
    std::size_t position = 0;
    candidate_json_skip_whitespace(text, position);
    if (position >= text.size() || text[position] != '{') {
        return false;
    }
    std::size_t end = 0;
    if (!candidate_json_value_end(text, position, end)) {
        return false;
    }
    candidate_json_skip_whitespace(text, end);
    return end == text.size();
}

inline bool candidate_json_find_direct_value(std::string_view object,
                                             std::string_view key,
                                             std::string_view& value) {
    std::size_t position = 0;
    candidate_json_skip_whitespace(object, position);
    if (position >= object.size() || object[position++] != '{') {
        return false;
    }
    while (position < object.size()) {
        candidate_json_skip_whitespace(object, position);
        if (position < object.size() && object[position] == '}') {
            return false;
        }
        std::string member;
        if (!candidate_json_read_string(object, position, member)) {
            return false;
        }
        candidate_json_skip_whitespace(object, position);
        if (position >= object.size() || object[position++] != ':') {
            return false;
        }
        candidate_json_skip_whitespace(object, position);
        const std::size_t value_begin = position;
        std::size_t value_end = 0;
        if (!candidate_json_value_end(object, position, value_end)) {
            return false;
        }
        if (member == key) {
            value = object.substr(value_begin, value_end - value_begin);
            return true;
        }
        position = value_end;
        candidate_json_skip_whitespace(object, position);
        if (position < object.size() && object[position] == ',') {
            ++position;
            continue;
        }
        return false;
    }
    return false;
}

inline bool candidate_json_find_direct_string(std::string_view object, std::string_view key, std::string& value) {
    std::string_view encoded;
    if (!candidate_json_find_direct_value(object, key, encoded)) {
        return false;
    }
    std::size_t position = 0;
    if (!candidate_json_read_string(encoded, position, value)) {
        return false;
    }
    candidate_json_skip_whitespace(encoded, position);
    return position == encoded.size();
}

inline bool candidate_json_find_direct_int(std::string_view object, std::string_view key, int& value) {
    std::string_view encoded;
    if (!candidate_json_find_direct_value(object, key, encoded)) {
        return false;
    }
    std::size_t position = 0;
    candidate_json_skip_whitespace(encoded, position);
    if (position >= encoded.size() || !std::isdigit(static_cast<unsigned char>(encoded[position]))) {
        return false;
    }
    int parsed = 0;
    while (position < encoded.size() && std::isdigit(static_cast<unsigned char>(encoded[position])) != 0) {
        if (parsed > 100000000) {
            return false;
        }
        parsed = parsed * 10 + (encoded[position++] - '0');
    }
    candidate_json_skip_whitespace(encoded, position);
    if (position != encoded.size()) {
        return false;
    }
    value = parsed;
    return true;
}

inline bool candidate_json_find_direct_bool(std::string_view object, std::string_view key, bool& value) {
    std::string_view encoded;
    if (!candidate_json_find_direct_value(object, key, encoded)) {
        return false;
    }
    std::size_t position = 0;
    candidate_json_skip_whitespace(encoded, position);
    const std::string_view literal = encoded.substr(position);
    const std::size_t end = literal.find_last_not_of(" \t\r\n");
    const std::string_view trimmed = end == std::string_view::npos ? std::string_view{} : literal.substr(0, end + 1);
    if (trimmed == "true") {
        value = true;
        return true;
    }
    if (trimmed == "false") {
        value = false;
        return true;
    }
    return false;
}

inline bool candidate_is_sha256(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
    });
}

inline InstallCandidate load_verified_install_candidate(const std::filesystem::path& candidate_path,
                                                        std::size_t max_bundle_bytes) {
    const std::filesystem::path absolute_candidate = std::filesystem::absolute(candidate_path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(absolute_candidate, error)) {
        throw std::runtime_error("install candidate is not a regular file: " + absolute_candidate.string());
    }
    const std::uintmax_t candidate_size = std::filesystem::file_size(absolute_candidate, error);
    if (error || candidate_size > 128U * 1024U) {
        throw std::runtime_error("install candidate is too large or unreadable: " + absolute_candidate.string());
    }
    const std::string candidate = read_text_file_limited(absolute_candidate, 128U * 1024U);
    std::string format;
    int format_version = -1;
    std::string_view bundle;
    std::string_view signature;
    bool user_approval = false;
    if (!candidate_json_is_single_object(candidate) ||
        !candidate_json_find_direct_string(candidate, "format", format) || format != "jellyframe.install_candidate" ||
        !candidate_json_find_direct_int(candidate, "formatVersion", format_version) || format_version != 0 ||
        !candidate_json_find_direct_value(candidate, "bundle", bundle) ||
        !candidate_json_find_direct_value(candidate, "signature", signature) ||
        !candidate_json_find_direct_bool(candidate, "userApproval", user_approval)) {
        throw std::runtime_error("invalid install candidate: required fields are missing or malformed");
    }
    std::string bundle_path_text;
    std::string expected_sha256;
    std::string signature_status;
    if (!candidate_json_find_direct_string(bundle, "path", bundle_path_text) || bundle_path_text.empty() ||
        !candidate_json_find_direct_string(bundle, "sha256", expected_sha256) || !candidate_is_sha256(expected_sha256)) {
        throw std::runtime_error("invalid install candidate: bundle.path and bundle.sha256 are required");
    }
    if (!candidate_json_find_direct_string(signature, "status", signature_status) || signature_status != "trusted") {
        throw std::runtime_error("signature-not-trusted: install candidate signature is not trusted");
    }
    if (!user_approval) {
        throw std::runtime_error("user-approval-required: install candidate requires user approval");
    }
    std::filesystem::path bundle_path(bundle_path_text);
    if (!bundle_path.is_absolute()) {
        bundle_path = absolute_candidate.parent_path() / bundle_path;
    }
    bundle_path = std::filesystem::absolute(bundle_path);
    if (!std::filesystem::is_regular_file(bundle_path, error)) {
        throw std::runtime_error("install candidate bundle is not a regular file: " + bundle_path.string());
    }
    const std::uintmax_t bundle_size = std::filesystem::file_size(bundle_path, error);
    if (error || bundle_size > max_bundle_bytes) {
        throw std::runtime_error("install candidate bundle is too large or unreadable: " + bundle_path.string());
    }
    std::transform(expected_sha256.begin(), expected_sha256.end(), expected_sha256.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (sha256_file_hex(bundle_path) != expected_sha256) {
        throw std::runtime_error("bundle-hash-mismatch: install candidate bundle sha256 mismatch");
    }
    return {bundle_path, std::move(expected_sha256)};
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
    json_find_string(object, "status", entry.status);
    json_find_string(object, "versionName", entry.version_name);
    json_find_string(object, "entry", entry.entry);
    json_find_string(object, "script", entry.script_mode);
    json_find_string(object, "bundleFile", entry.bundle_file);
    json_find_string(object, "installedAtUtc", entry.installed_at_utc);
    json_find_string(object, "updatedAtUtc", entry.updated_at_utc);
    json_find_bool(object, "enabled", entry.enabled);
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
    if (entry.status.empty()) {
        entry.status = "installed";
    }
    std::size_t failure_open = 0;
    std::size_t failure_close = 0;
    if (json_find_object_range(object, "failure", failure_open, failure_close)) {
        const std::string failure(object.data() + failure_open, failure_close - failure_open);
        json_find_string(failure, "reason", entry.failure_reason);
        json_find_string(failure, "message", entry.failure_message);
        json_find_string(failure, "failedAtUtc", entry.failed_at_utc);
        entry.has_failure = !entry.failure_reason.empty();
    }
    std::size_t rollback_open = 0;
    std::size_t rollback_close = 0;
    if (json_find_object_range(object, "rollback", rollback_open, rollback_close)) {
        const std::string rollback(object.data() + rollback_open, rollback_close - rollback_open);
        json_find_string(rollback, "name", entry.rollback_name);
        json_find_string(rollback, "role", entry.rollback_role);
        json_find_string(rollback, "versionName", entry.rollback_version_name);
        json_find_string(rollback, "entry", entry.rollback_entry);
        json_find_string(rollback, "script", entry.rollback_script_mode);
        json_find_string(rollback, "bundleFile", entry.rollback_bundle_file);
        json_find_string(rollback, "installedAtUtc", entry.rollback_installed_at_utc);
        json_find_string(rollback, "updatedAtUtc", entry.rollback_updated_at_utc);
        json_find_bool(rollback, "networkAllowed", entry.rollback_network_allowed);
        json_find_int(rollback, "versionCode", entry.rollback_version_code);
        int rollback_size = 0;
        if (json_find_int(rollback, "bundleSize", rollback_size) && rollback_size > 0) {
            entry.rollback_bundle_size = static_cast<std::size_t>(rollback_size);
        }
        int rollback_count = 0;
        if (json_find_int(rollback, "resourceCount", rollback_count) && rollback_count > 0) {
            entry.rollback_resource_count = static_cast<std::size_t>(rollback_count);
        }
        entry.has_rollback = !entry.rollback_bundle_file.empty();
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

inline RegistryStorageRecovery recover_registry_storage(const std::filesystem::path& store,
                                                        std::filesystem::path preserved_bundle = {}) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    if (!preserved_bundle.empty()) {
        preserved_bundle = std::filesystem::absolute(preserved_bundle).lexically_normal();
    }
    const InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    RegistryStorageRecovery recovery;
    std::unordered_set<std::string> referenced_bundles;
    referenced_bundles.reserve(registry.apps.size() * 2U);
    for (const InstalledAppEntry& app : registry.apps) {
        referenced_bundles.insert(app.bundle_file);
        if (app.has_rollback && !app.rollback_bundle_file.empty()) {
            referenced_bundles.insert(app.rollback_bundle_file);
        }
    }

    std::error_code error;
    const std::filesystem::path staging = registry_staging_dir(absolute_store);
    std::filesystem::create_directories(staging, error);
    error.clear();
    for (std::filesystem::directory_iterator it(staging, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) {
            error.clear();
            continue;
        }
        std::error_code remove_error;
        if (std::filesystem::remove(it->path(), remove_error)) {
            ++recovery.stale_staging_files_removed;
        }
    }

    error.clear();
    const std::filesystem::path bundles = registry_bundles_dir(absolute_store);
    std::filesystem::create_directories(bundles, error);
    error.clear();
    for (std::filesystem::directory_iterator it(bundles, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".jfapp") {
            error.clear();
            continue;
        }
        const std::string filename = it->path().filename().string();
        if (referenced_bundles.find(filename) != referenced_bundles.end() ||
            (!preserved_bundle.empty() && it->path().lexically_normal() == preserved_bundle)) {
            continue;
        }
        std::error_code remove_error;
        if (std::filesystem::remove(it->path(), remove_error)) {
            ++recovery.orphan_bundle_files_removed;
        }
    }
    return recovery;
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
        output << "      \"status\": \"" << json_escape_text(app.status) << "\",\n";
        output << "      \"enabled\": " << (app.enabled ? "true" : "false") << ",\n";
        output << "      \"versionName\": \"" << json_escape_text(app.version_name) << "\",\n";
        output << "      \"versionCode\": " << app.version_code << ",\n";
        output << "      \"entry\": \"" << json_escape_text(app.entry) << "\",\n";
        output << "      \"script\": \"" << json_escape_text(app.script_mode) << "\",\n";
        output << "      \"networkAllowed\": " << (app.network_allowed ? "true" : "false") << ",\n";
        output << "      \"bundleFile\": \"" << json_escape_text(app.bundle_file) << "\",\n";
        output << "      \"bundleSize\": " << app.bundle_size << ",\n";
        output << "      \"resourceCount\": " << app.resource_count << ",\n";
        output << "      \"installedAtUtc\": \"" << json_escape_text(app.installed_at_utc) << "\",\n";
        output << "      \"updatedAtUtc\": \"" << json_escape_text(app.updated_at_utc.empty() ? app.installed_at_utc : app.updated_at_utc) << "\"";
        if (app.has_failure && !app.failure_reason.empty()) {
            output << ",\n";
            output << "      \"failure\": {\n";
            output << "        \"reason\": \"" << json_escape_text(app.failure_reason) << "\",\n";
            output << "        \"message\": \"" << json_escape_text(app.failure_message) << "\",\n";
            output << "        \"failedAtUtc\": \"" << json_escape_text(app.failed_at_utc) << "\"\n";
            output << "      }";
        }
        if (app.has_rollback && !app.rollback_bundle_file.empty()) {
            output << ",\n";
            output << "      \"rollback\": {\n";
            output << "        \"name\": \"" << json_escape_text(app.rollback_name.empty() ? app.name : app.rollback_name) << "\",\n";
            output << "        \"role\": \"" << json_escape_text(app.rollback_role.empty() ? "app" : app.rollback_role) << "\",\n";
            output << "        \"versionName\": \"" << json_escape_text(app.rollback_version_name) << "\",\n";
            output << "        \"versionCode\": " << app.rollback_version_code << ",\n";
            output << "        \"entry\": \"" << json_escape_text(app.rollback_entry.empty() ? "/index.html" : app.rollback_entry) << "\",\n";
            output << "        \"script\": \"" << json_escape_text(app.rollback_script_mode.empty() ? "classic" : app.rollback_script_mode) << "\",\n";
            output << "        \"networkAllowed\": " << (app.rollback_network_allowed ? "true" : "false") << ",\n";
            output << "        \"bundleFile\": \"" << json_escape_text(app.rollback_bundle_file) << "\",\n";
            output << "        \"bundleSize\": " << app.rollback_bundle_size << ",\n";
            output << "        \"resourceCount\": " << app.rollback_resource_count << ",\n";
            output << "        \"installedAtUtc\": \"" << json_escape_text(app.rollback_installed_at_utc.empty() ? app.installed_at_utc : app.rollback_installed_at_utc) << "\",\n";
            output << "        \"updatedAtUtc\": \"" << json_escape_text(app.rollback_updated_at_utc) << "\"\n";
            output << "      }\n";
        } else {
            output << "\n";
        }
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

inline bool installed_app_launchable(const InstalledAppEntry& entry) {
    return entry.enabled && entry.status == "installed";
}

inline bool installed_app_rollback_ready(const InstalledAppEntry& entry) {
    return entry.has_rollback && !entry.rollback_bundle_file.empty();
}

inline AppManagerState app_manager_state_from_registry(const InstalledAppRegistry& registry) {
    AppManagerState state;
    state.apps.reserve(registry.apps.size());
    for (const InstalledAppEntry& entry : registry.apps) {
        AppManagerAppState app_state;
        app_state.app = entry;
        app_state.launchable = installed_app_launchable(entry);
        app_state.rollback_ready = installed_app_rollback_ready(entry);
        state.summary.app_count += 1;
        state.summary.launchable_count += app_state.launchable ? 1 : 0;
        state.summary.failed_count += entry.status == "failed" ? 1 : 0;
        state.summary.rollback_ready_count += app_state.rollback_ready ? 1 : 0;
        state.apps.push_back(std::move(app_state));
    }
    return state;
}

inline AppManagerState load_app_manager_state(const std::filesystem::path& store) {
    return app_manager_state_from_registry(load_installed_app_registry(store));
}

inline std::filesystem::path find_installed_app_bundle_path(const std::filesystem::path& store, std::string_view app_id) {
    const InstalledAppRegistry registry = load_installed_app_registry(store);
    const InstalledAppEntry* entry = find_installed_app(registry, app_id);
    if (entry == nullptr) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    if (!installed_app_launchable(*entry)) {
        throw std::runtime_error("app is not launchable: " + std::string(app_id));
    }
    return installed_app_bundle_path(store, *entry);
}

inline InstalledAppEntry install_bundle_into_registry(const std::filesystem::path& store,
                                                     const std::filesystem::path& bundle_path,
                                                     std::size_t max_input_bytes,
                                                     bool allow_downgrade = false) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    const std::filesystem::path absolute_bundle = std::filesystem::absolute(bundle_path);
    recover_registry_storage(absolute_store, absolute_bundle);
    AppPackage package = load_jfapp_bundle(absolute_bundle, max_input_bytes);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == package.manifest.id;
    });
    if (existing != registry.apps.end() && !allow_downgrade && package.manifest.version_code < existing->version_code) {
        throw std::runtime_error(
            "downgrade install is blocked: " + package.manifest.id + " " +
            std::to_string(package.manifest.version_code) + " < " +
            std::to_string(existing->version_code) + "; use --allow-downgrade or rollback");
    }
    const std::size_t bundle_size = file_size_or_zero(absolute_bundle);
    const std::string bundle_sha256 = sha256_file_hex(absolute_bundle);
    const std::string bundle_file = sanitize_registry_filename(package.manifest.id) + "-" +
        std::to_string(package.manifest.version_code) + "-" + bundle_sha256.substr(0, 12) + ".jfapp";
    if (existing != registry.apps.end() && existing->bundle_file == bundle_file &&
        std::filesystem::is_regular_file(installed_app_bundle_path(absolute_store, *existing))) {
        return *existing;
    }
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
    entry.status = "installed";
    entry.enabled = true;
    entry.version_name = package.manifest.version_name.empty() ? "0.0.0" : package.manifest.version_name;
    entry.version_code = package.manifest.version_code;
    entry.entry = package.manifest.entry;
    entry.script_mode = package.manifest.script_mode;
    entry.network_allowed = package.manifest.network_allowed;
    entry.bundle_file = bundle_file;
    entry.bundle_size = bundle_size;
    entry.resource_count = package.bundle_entries.size();
    entry.installed_at_utc = utc_now_compact();
    entry.updated_at_utc = entry.installed_at_utc;

    std::string obsolete_rollback_file;
    if (existing == registry.apps.end()) {
        registry.apps.push_back(entry);
    } else {
        entry.installed_at_utc = existing->installed_at_utc.empty() ? entry.installed_at_utc : existing->installed_at_utc;
        entry.enabled = existing->enabled;
        entry.status = existing->status;
        entry.has_failure = existing->has_failure;
        entry.failure_reason = existing->failure_reason;
        entry.failure_message = existing->failure_message;
        entry.failed_at_utc = existing->failed_at_utc;
        if (existing->has_rollback &&
            existing->rollback_bundle_file != existing->bundle_file &&
            existing->rollback_bundle_file != entry.bundle_file) {
            obsolete_rollback_file = existing->rollback_bundle_file;
        }
        if (existing->bundle_file != entry.bundle_file) {
            entry.has_rollback = true;
            entry.rollback_name = existing->name;
            entry.rollback_role = existing->role;
            entry.rollback_version_name = existing->version_name;
            entry.rollback_version_code = existing->version_code;
            entry.rollback_entry = existing->entry;
            entry.rollback_script_mode = existing->script_mode;
            entry.rollback_network_allowed = existing->network_allowed;
            entry.rollback_bundle_file = existing->bundle_file;
            entry.rollback_bundle_size = existing->bundle_size;
            entry.rollback_resource_count = existing->resource_count;
            entry.rollback_installed_at_utc = existing->installed_at_utc;
            entry.rollback_updated_at_utc = existing->updated_at_utc;
        } else {
            entry.has_rollback = existing->has_rollback;
            entry.rollback_name = existing->rollback_name;
            entry.rollback_role = existing->rollback_role;
            entry.rollback_version_name = existing->rollback_version_name;
            entry.rollback_version_code = existing->rollback_version_code;
            entry.rollback_entry = existing->rollback_entry;
            entry.rollback_script_mode = existing->rollback_script_mode;
            entry.rollback_network_allowed = existing->rollback_network_allowed;
            entry.rollback_bundle_file = existing->rollback_bundle_file;
            entry.rollback_bundle_size = existing->rollback_bundle_size;
            entry.rollback_resource_count = existing->rollback_resource_count;
            entry.rollback_installed_at_utc = existing->rollback_installed_at_utc;
            entry.rollback_updated_at_utc = existing->rollback_updated_at_utc;
        }
        *existing = entry;
    }
    std::sort(registry.apps.begin(), registry.apps.end(), [](const InstalledAppEntry& left, const InstalledAppEntry& right) {
        return left.id < right.id;
    });
    write_installed_app_registry(absolute_store, registry);
    if (!obsolete_rollback_file.empty()) {
        std::error_code error;
        std::filesystem::remove(registry_bundles_dir(absolute_store) / obsolete_rollback_file, error);
    }
    return entry;
}

inline InstalledAppEntry install_candidate_into_registry(const std::filesystem::path& store,
                                                         const std::filesystem::path& candidate_path,
                                                         std::size_t max_input_bytes,
                                                         bool allow_downgrade = false) {
    const InstallCandidate candidate = load_verified_install_candidate(candidate_path, max_input_bytes);
    return install_bundle_into_registry(store, candidate.bundle_path, max_input_bytes, allow_downgrade);
}

inline InstalledAppEntry rollback_installed_app(const std::filesystem::path& store, std::string_view app_id) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == app_id;
    });
    if (existing == registry.apps.end()) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    if (!existing->has_rollback || existing->rollback_bundle_file.empty()) {
        throw std::runtime_error("app has no rollback bundle: " + std::string(app_id));
    }
    const std::filesystem::path rollback_path = registry_bundles_dir(absolute_store) / existing->rollback_bundle_file;
    if (!std::filesystem::is_regular_file(rollback_path)) {
        throw std::runtime_error("rollback bundle is missing: " + rollback_path.string());
    }

    InstalledAppEntry restored = *existing;
    restored.name = existing->rollback_name.empty() ? existing->name : existing->rollback_name;
    restored.role = existing->rollback_role.empty() ? "app" : existing->rollback_role;
    restored.version_name = existing->rollback_version_name;
    restored.version_code = existing->rollback_version_code;
    restored.entry = existing->rollback_entry.empty() ? "/index.html" : existing->rollback_entry;
    restored.script_mode = existing->rollback_script_mode.empty() ? "classic" : existing->rollback_script_mode;
    restored.network_allowed = existing->rollback_network_allowed;
    restored.bundle_file = existing->rollback_bundle_file;
    restored.bundle_size = existing->rollback_bundle_size;
    restored.resource_count = existing->rollback_resource_count;
    restored.status = "installed";
    restored.enabled = true;
    restored.installed_at_utc = existing->rollback_installed_at_utc.empty() ?
        existing->installed_at_utc : existing->rollback_installed_at_utc;
    restored.updated_at_utc = utc_now_compact();
    restored.has_rollback = true;
    restored.rollback_name = existing->name;
    restored.rollback_role = existing->role;
    restored.rollback_version_name = existing->version_name;
    restored.rollback_version_code = existing->version_code;
    restored.rollback_entry = existing->entry;
    restored.rollback_script_mode = existing->script_mode;
    restored.rollback_network_allowed = existing->network_allowed;
    restored.rollback_bundle_file = existing->bundle_file;
    restored.rollback_bundle_size = existing->bundle_size;
    restored.rollback_resource_count = existing->resource_count;
    restored.rollback_installed_at_utc = existing->installed_at_utc;
    restored.rollback_updated_at_utc = existing->updated_at_utc;
    *existing = restored;
    write_installed_app_registry(absolute_store, registry);
    return restored;
}

inline InstalledAppEntry set_installed_app_enabled(const std::filesystem::path& store,
                                                   std::string_view app_id,
                                                   bool enabled) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == app_id;
    });
    if (existing == registry.apps.end()) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    existing->enabled = enabled;
    existing->status = enabled ? "installed" : "disabled";
    existing->updated_at_utc = utc_now_compact();
    if (enabled) {
        existing->has_failure = false;
        existing->failure_reason.clear();
        existing->failure_message.clear();
        existing->failed_at_utc.clear();
    }
    const InstalledAppEntry updated = *existing;
    write_installed_app_registry(absolute_store, registry);
    return updated;
}

inline InstalledAppEntry mark_installed_app_failed(const std::filesystem::path& store,
                                                   std::string_view app_id,
                                                   std::string_view reason,
                                                   std::string_view message) {
    const std::filesystem::path absolute_store = std::filesystem::absolute(store);
    InstalledAppRegistry registry = load_installed_app_registry(absolute_store);
    auto existing = std::find_if(registry.apps.begin(), registry.apps.end(), [&](const InstalledAppEntry& app) {
        return app.id == app_id;
    });
    if (existing == registry.apps.end()) {
        throw std::runtime_error("app is not installed: " + std::string(app_id));
    }
    const std::string now = utc_now_compact();
    existing->enabled = false;
    existing->status = "failed";
    existing->updated_at_utc = now;
    existing->has_failure = true;
    existing->failure_reason = std::string(reason);
    existing->failure_message = std::string(message);
    existing->failed_at_utc = now;
    const InstalledAppEntry updated = *existing;
    write_installed_app_registry(absolute_store, registry);
    return updated;
}

inline bool delete_registry_app_data(const std::filesystem::path& store, std::string_view app_id) {
    const std::filesystem::path path = registry_app_data_dir(std::filesystem::absolute(store), app_id);
    std::error_code exists_error;
    if (!std::filesystem::exists(path, exists_error)) {
        return false;
    }
    if (!std::filesystem::is_directory(path)) {
        throw std::runtime_error("app data path is not a directory: " + path.string());
    }
    std::error_code remove_error;
    std::filesystem::remove_all(path, remove_error);
    if (remove_error) {
        throw std::runtime_error("failed to delete app data: " + path.string());
    }
    return true;
}

inline InstalledAppEntry remove_bundle_from_registry(const std::filesystem::path& store,
                                                     std::string_view app_id,
                                                     bool delete_data = true) {
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
    if (removed.has_rollback && !removed.rollback_bundle_file.empty() && removed.rollback_bundle_file != removed.bundle_file) {
        std::filesystem::remove(registry_bundles_dir(absolute_store) / removed.rollback_bundle_file, error);
    }
    if (delete_data) {
        delete_registry_app_data(absolute_store, app_id);
    }
    return removed;
}

} // namespace jellyframe_example
