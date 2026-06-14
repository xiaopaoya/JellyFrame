#include "core/document_style.h"

namespace wearweb {
namespace {

void append_text_descendants(const Node& node, std::string& output) {
    if (node.type == NodeType::Text) {
        output.append(node.text);
        output.push_back('\n');
        return;
    }
    for (const auto& child : node.children) {
        append_text_descendants(*child, output);
    }
}

void collect_style_nodes(const Node& node, std::string& output) {
    if (node.type == NodeType::Element && node.tag_name == "style") {
        append_text_descendants(node, output);
        return;
    }
    for (const auto& child : node.children) {
        collect_style_nodes(*child, output);
    }
}

} // namespace

std::string collect_embedded_style_text(const Node& document) {
    std::string output;
    collect_style_nodes(document, output);
    return output;
}

std::string combine_author_css(const std::string& external_css, const Node& document) {
    std::string combined = external_css;
    const std::string embedded = collect_embedded_style_text(document);
    if (!combined.empty() && !embedded.empty()) {
        combined.push_back('\n');
    }
    combined.append(embedded);
    return combined;
}

} // namespace wearweb
