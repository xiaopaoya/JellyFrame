#include "core/text_normalization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace jellyframe {
namespace {

template <std::size_t Size>
bool contains_name(const std::array<std::string_view, Size>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool preserving_tag(std::string_view tag_name) {
    static constexpr std::array<std::string_view, 5> kPreservingElements = {
        "pre", "script", "style", "textarea", "title"
    };
    return contains_name(kPreservingElements, tag_name);
}

bool ascii_space(char ch) {
    return std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::string collapse_render_whitespace(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    bool last_was_space = false;
    for (char ch : value) {
        if (ascii_space(ch)) {
            if (!last_was_space) {
                output.push_back(' ');
                last_was_space = true;
            }
        } else {
            output.push_back(ch);
            last_was_space = false;
        }
    }
    return output;
}

} // namespace

bool preserves_dom_text_whitespace(const Node& node) {
    for (const Node* current = node.parent; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element && preserving_tag(current->tag_name)) {
            return true;
        }
    }
    return false;
}

bool is_collapsible_whitespace_text(const Node& text_node) {
    if (text_node.type != NodeType::Text || preserves_dom_text_whitespace(text_node)) {
        return false;
    }
    return std::all_of(text_node.text.begin(), text_node.text.end(), ascii_space);
}

std::string normalized_render_text(const Node& text_node) {
    if (text_node.type != NodeType::Text || preserves_dom_text_whitespace(text_node)) {
        return text_node.text;
    }
    return collapse_render_whitespace(text_node.text);
}

} // namespace jellyframe
