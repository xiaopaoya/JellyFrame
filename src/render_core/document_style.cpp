#include "render_core/document_style.h"

#include <cctype>
#include <vector>

namespace jellyframe {
namespace {

void append_text_descendants(const Node& node, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Text) {
            output.append(current->text);
            output.push_back('\n');
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

void collect_style_nodes(const Node& node, std::string& output) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "style") {
            append_text_descendants(*current, output);
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
}

bool ascii_equal_case_insensitive(char left, char right) {
    return std::tolower(static_cast<unsigned char>(left)) ==
        std::tolower(static_cast<unsigned char>(right));
}

bool token_equals(std::string_view token, std::string_view expected) {
    if (token.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (!ascii_equal_case_insensitive(token[index], expected[index])) {
            return false;
        }
    }
    return true;
}

bool rel_has_stylesheet(std::string_view rel) {
    std::size_t index = 0;
    while (index < rel.size()) {
        while (index < rel.size() && std::isspace(static_cast<unsigned char>(rel[index])) != 0) {
            ++index;
        }
        const std::size_t begin = index;
        while (index < rel.size() && std::isspace(static_cast<unsigned char>(rel[index])) == 0) {
            ++index;
        }
        if (begin != index && token_equals(rel.substr(begin, index - begin), "stylesheet")) {
            return true;
        }
    }
    return false;
}

void append_css_chunk(std::string& output, const std::string& css) {
    if (css.empty()) {
        return;
    }
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\n');
    }
    output.append(css);
    if (!output.empty() && output.back() != '\n') {
        output.push_back('\n');
    }
}

void collect_document_author_css(const Node& node,
                                 std::string& output,
                                 StylesheetLoadCallback load_stylesheet,
                                 void* context) {
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->type == NodeType::Element && current->tag_name == "link" &&
            rel_has_stylesheet(current->attribute("rel")) && !current->attribute("href").empty() &&
            load_stylesheet != nullptr) {
            std::string linked_css;
            if (load_stylesheet(current->attribute("href"), linked_css, context)) {
                append_css_chunk(output, linked_css);
            }
            continue;
        }
        if (current->type == NodeType::Element && current->tag_name == "style") {
            std::string embedded_css;
            append_text_descendants(*current, embedded_css);
            append_css_chunk(output, embedded_css);
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
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

std::string combine_author_css(const std::string& external_css,
                               const Node& document,
                               StylesheetLoadCallback load_stylesheet,
                               void* context) {
    std::string combined = external_css;
    collect_document_author_css(document, combined, load_stylesheet, context);
    return combined;
}

} // namespace jellyframe
