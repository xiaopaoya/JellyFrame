#pragma once

#include "core/document_script.h"
#include "core/document_style.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace wearweb_example {

inline std::string read_file_limited(const std::filesystem::path& path, std::size_t max_input_bytes) {
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

struct StylesheetLoadContext {
    std::filesystem::path base_dir;
    std::size_t max_input_bytes = 512 * 1024;
};

struct ScriptLoadContext {
    std::filesystem::path base_dir;
    std::size_t max_input_bytes = 512 * 1024;
};

inline bool is_local_stylesheet_href(std::string_view href) {
    return !href.empty() &&
        href.find(':') == std::string_view::npos &&
        href.rfind("//", 0) != 0;
}

inline bool load_linked_stylesheet(std::string_view href, std::string& output, void* raw_context) {
    if (!is_local_stylesheet_href(href) || raw_context == nullptr) {
        return false;
    }
    const auto* context = static_cast<const StylesheetLoadContext*>(raw_context);
    const std::filesystem::path path = context->base_dir / std::filesystem::path(std::string(href));
    output = read_file_limited(path, context->max_input_bytes);
    return !output.empty();
}

inline bool is_local_script_src(std::string_view src) {
    return !src.empty() &&
        src.find(':') == std::string_view::npos &&
        src.rfind("//", 0) != 0;
}

inline bool load_linked_script(std::string_view src, std::string& output, void* raw_context) {
    if (!is_local_script_src(src) || raw_context == nullptr) {
        return false;
    }
    const auto* context = static_cast<const ScriptLoadContext*>(raw_context);
    const std::filesystem::path path = context->base_dir / std::filesystem::path(std::string(src));
    output = read_file_limited(path, context->max_input_bytes);
    return !output.empty();
}

inline std::string read_author_css_for_document(const std::string& css_path,
                                                const wearweb::Node& document,
                                                std::size_t max_input_bytes) {
    const std::filesystem::path path(css_path);
    StylesheetLoadContext context;
    context.base_dir = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    context.max_input_bytes = max_input_bytes;
    const std::string external_css = read_file_limited(path, max_input_bytes);
    return wearweb::combine_author_css(external_css, document, wearweb_example::load_linked_stylesheet, &context);
}

} // namespace wearweb_example
