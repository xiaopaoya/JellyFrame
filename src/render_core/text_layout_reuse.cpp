#include "render_core/text_layout_reuse.h"

#include "render_core/text_normalization.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace jellyframe {
namespace {

std::uint32_t consume_utf8_codepoint(const std::string& text, std::size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U && index + 1 < text.size()) {
        width = 2;
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
    } else if ((lead & 0xf0U) == 0xe0U && index + 2 < text.size()) {
        width = 3;
        codepoint = ((lead & 0x0fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
    } else if ((lead & 0xf8U) == 0xf0U && index + 3 < text.size()) {
        width = 4;
        codepoint = ((lead & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
    }
    index += std::min(width, text.size() - index);
    return codepoint;
}

bool has_text_wrap_opportunity(const std::string& text) {
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        if (codepoint == ' ' || codepoint == '-' || codepoint == '/' ||
            codepoint == 0x3001U || codepoint == 0x3002U || codepoint >= 0x2e80U) {
            return true;
        }
    }
    return false;
}

bool collect_dirty_text_nodes(const Node& node, std::vector<const Node*>& dirty_text_nodes) {
    std::vector<const Node*> pending;
    pending.reserve(16);
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if ((current->local_dirty_flags & DomDirtyText) != 0U) {
            if (current->type != NodeType::Text) {
                return false;
            }
            dirty_text_nodes.push_back(current);
        }
        if (current->dirty_flags == DomDirtyNone) {
            continue;
        }
        for (const auto& child : current->children) {
            if (child->dirty_flags != DomDirtyNone) {
                pending.push_back(child.get());
            }
        }
    }
    return true;
}

bool contains_dirty_node(const std::vector<const Node*>& dirty_text_nodes, const Node* node) {
    return std::find(dirty_text_nodes.begin(), dirty_text_nodes.end(), node) != dirty_text_nodes.end();
}

bool text_box_can_reuse_layout(const LayoutBox& box, const TextMeasureProvider& text_measure) {
    if (box.node == nullptr || box.node->type != NodeType::Text) {
        return false;
    }
    const std::string text = normalized_render_text(*box.node);
    if (text.empty() || has_text_wrap_opportunity(text)) {
        return false;
    }
    const TextMetrics metrics = measure_text(text_measure, text, box.style.font_size, box.style.font_weight);
    const int measured_width = metrics.width + 1;
    const bool same_intrinsic_width = measured_width == box.rect.width;
    const bool fixed_min_width_still_contains_text =
        box.style.min_width >= 0 && box.style.min_width == box.rect.width && measured_width <= box.rect.width;
    if (!same_intrinsic_width && !fixed_min_width_still_contains_text) {
        return false;
    }
    const int line_height = box.style.line_height > 0 ? box.style.line_height : metrics.line_height;
    return box.rect.height == line_height;
}

std::size_t validate_dirty_text_boxes(const LayoutBox& layout,
                                      const std::vector<const Node*>& dirty_text_nodes,
                                      const TextMeasureProvider& text_measure) {
    std::size_t matched = 0;
    std::vector<const LayoutBox*> pending;
    pending.reserve(16);
    pending.push_back(&layout);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        if (contains_dirty_node(dirty_text_nodes, current->node)) {
            if (!text_box_can_reuse_layout(*current, text_measure)) {
                return 0;
            }
            ++matched;
        }
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return matched;
}

} // namespace

bool dirty_text_nodes_have_stable_layout(const Node& document,
                                         const LayoutBox& layout,
                                         const TextMeasureProvider& text_measure,
                                         bool require_text_dirty) {
    if ((document.dirty_flags & DomDirtyText) == 0U) {
        return !require_text_dirty;
    }
    std::vector<const Node*> dirty_text_nodes;
    dirty_text_nodes.reserve(4);
    if (!collect_dirty_text_nodes(document, dirty_text_nodes) || dirty_text_nodes.empty()) {
        return false;
    }
    return validate_dirty_text_boxes(layout, dirty_text_nodes, text_measure) == dirty_text_nodes.size();
}

} // namespace jellyframe
