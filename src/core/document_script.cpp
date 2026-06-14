#include "core/document_script.h"

#include <cctype>

namespace wearweb {
namespace {

void append_text_descendants(const Node& node, std::string& output) {
    if (node.type == NodeType::Text) {
        output.append(node.text);
        return;
    }
    for (const auto& child : node.children) {
        append_text_descendants(*child, output);
    }
}

bool ascii_equal_case_insensitive(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
        std::tolower(static_cast<unsigned char>(right));
}

bool ascii_equals(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!ascii_equal_case_insensitive(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

std::string trim_ascii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

bool is_classic_script_type(std::string_view raw_type) {
    const std::string trimmed = trim_ascii(raw_type);
    const std::size_t semicolon = trimmed.find(';');
    const std::string type = semicolon == std::string::npos
        ? trimmed
        : trim_ascii(std::string_view(trimmed).substr(0, semicolon));
    return type.empty() ||
        ascii_equals(type, "text/javascript") ||
        ascii_equals(type, "application/javascript") ||
        ascii_equals(type, "text/ecmascript") ||
        ascii_equals(type, "application/ecmascript") ||
        ascii_equals(type, "classic");
}

void collect_scripts(const Node& node,
                     std::vector<DocumentScript>& scripts,
                     ScriptLoadCallback load_script,
                     void* context) {
    if (node.type == NodeType::Element && node.tag_name == "script") {
        if (!is_classic_script_type(node.attribute("type"))) {
            return;
        }
        const std::string& src = node.attribute("src");
        if (!src.empty()) {
            if (load_script == nullptr) {
                return;
            }
            std::string source;
            if (load_script(src, source, context) && !source.empty()) {
                scripts.push_back(DocumentScript{std::move(source), src, true});
            }
            return;
        }
        std::string source;
        append_text_descendants(node, source);
        if (!source.empty()) {
            scripts.push_back(DocumentScript{std::move(source), "(inline script)", false});
        }
        return;
    }

    for (const auto& child : node.children) {
        collect_scripts(*child, scripts, load_script, context);
    }
}

} // namespace

std::vector<DocumentScript> collect_classic_scripts(const Node& document) {
    return collect_classic_scripts(document, nullptr, nullptr);
}

std::vector<DocumentScript> collect_classic_scripts(const Node& document,
                                                    ScriptLoadCallback load_script,
                                                    void* context) {
    std::vector<DocumentScript> scripts;
    collect_scripts(document, scripts, load_script, context);
    return scripts;
}

} // namespace wearweb
