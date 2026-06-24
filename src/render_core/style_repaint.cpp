#include "render_core/style_repaint.h"

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

bool edge_sizes_equal(const EdgeSizes& left, const EdgeSizes& right) {
    return left.top == right.top && left.right == right.right &&
        left.bottom == right.bottom && left.left == right.left;
}

bool layout_fields_equal(const Style& left, const Style& right) {
    return left.display == right.display &&
        edge_sizes_equal(left.margin, right.margin) &&
        left.margin_left_auto == right.margin_left_auto &&
        left.margin_right_auto == right.margin_right_auto &&
        edge_sizes_equal(left.padding, right.padding) &&
        edge_sizes_equal(left.border_width, right.border_width) &&
        left.width == right.width &&
        left.height == right.height &&
        left.min_width == right.min_width &&
        left.min_height == right.min_height &&
        left.max_width == right.max_width &&
        left.aspect_ratio_width == right.aspect_ratio_width &&
        left.aspect_ratio_height == right.aspect_ratio_height &&
        left.font_size == right.font_size &&
        left.font_size_specified == right.font_size_specified &&
        left.font_weight == right.font_weight &&
        left.font_weight_specified == right.font_weight_specified &&
        left.line_height == right.line_height &&
        left.line_height_specified == right.line_height_specified &&
        left.text_indent == right.text_indent &&
        left.text_indent_specified == right.text_indent_specified &&
        left.position == right.position &&
        left.inset_top == right.inset_top &&
        left.inset_right == right.inset_right &&
        left.inset_bottom == right.inset_bottom &&
        left.inset_left == right.inset_left &&
        left.inset_top_specified == right.inset_top_specified &&
        left.inset_right_specified == right.inset_right_specified &&
        left.inset_bottom_specified == right.inset_bottom_specified &&
        left.inset_left_specified == right.inset_left_specified &&
        left.box_sizing_border_box == right.box_sizing_border_box &&
        left.column_gap == right.column_gap &&
        left.row_gap == right.row_gap &&
        left.flex_wrap == right.flex_wrap &&
        left.flex_grow == right.flex_grow &&
        left.flex_shrink == right.flex_shrink &&
        left.flex_basis == right.flex_basis &&
        left.grid_min_track_width == right.grid_min_track_width &&
        left.grid_template_column_count == right.grid_template_column_count &&
        left.grid_template_column_widths == right.grid_template_column_widths &&
        left.grid_auto_row_min == right.grid_auto_row_min &&
        left.grid_column_span == right.grid_column_span &&
        left.grid_row_span == right.grid_row_span &&
        left.list_style_type == right.list_style_type &&
        left.list_style_type_specified == right.list_style_type_specified &&
        left.before_content_kind == right.before_content_kind &&
        left.before_content_text == right.before_content_text &&
        left.before_counter_name == right.before_counter_name &&
        left.before_counter_suffix == right.before_counter_suffix &&
        left.before_left == right.before_left &&
        left.before_left_specified == right.before_left_specified &&
        left.text_align == right.text_align &&
        left.text_align_specified == right.text_align_specified &&
        left.justify_content == right.justify_content &&
        left.align_items == right.align_items;
}

bool render_tree_shape_and_layout_fields_match(const RenderObject& previous,
                                               const RenderObject& next) {
    if (previous.type != next.type || previous.node != next.node ||
        previous.children.size() != next.children.size() ||
        !layout_fields_equal(previous.style, next.style)) {
        return false;
    }
    for (std::size_t index = 0; index < previous.children.size(); ++index) {
        if (!render_tree_shape_and_layout_fields_match(*previous.children[index], *next.children[index])) {
            return false;
        }
    }
    return true;
}

bool dirty_flags_are_style_text_layout_only(DomDirtyFlags flags) {
    return (flags & ~(DomDirtyAttributes | DomDirtyStyle | DomDirtyText | DomDirtyLayout | DomDirtyPaint)) == 0U;
}

bool collect_dirty_text_nodes(const Node& node, std::vector<const Node*>& dirty_text_nodes) {
    std::vector<const Node*> pending;
    pending.reserve(16);
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (current->local_dirty_flags != DomDirtyNone &&
            current->type == NodeType::Text &&
            (current->local_dirty_flags & DomDirtyText) != 0U) {
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
    if (dirty_text_nodes.empty()) {
        return 0;
    }
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

bool dirty_text_is_stable_if_present(const Node& document,
                                     const LayoutBox& layout,
                                     const TextMeasureProvider& text_measure) {
    if ((document.dirty_flags & DomDirtyText) == 0U) {
        return true;
    }
    std::vector<const Node*> dirty_text_nodes;
    dirty_text_nodes.reserve(4);
    if (!collect_dirty_text_nodes(document, dirty_text_nodes) || dirty_text_nodes.empty()) {
        return false;
    }
    return validate_dirty_text_boxes(layout, dirty_text_nodes, text_measure) == dirty_text_nodes.size();
}

bool render_layout_shape_matches(const RenderObject& render, const LayoutBox& layout) {
    if (render.node != layout.node || render.children.size() != layout.children.size()) {
        return false;
    }
    for (std::size_t index = 0; index < render.children.size(); ++index) {
        if (!render_layout_shape_matches(*render.children[index], *layout.children[index])) {
            return false;
        }
    }
    return true;
}

void apply_styles_recursive(const RenderObject& render, LayoutBox& layout) {
    layout.style = render.style;
    for (std::size_t index = 0; index < render.children.size(); ++index) {
        apply_styles_recursive(*render.children[index], *layout.children[index]);
    }
}

} // namespace

bool style_dirty_can_reuse_layout(const Node& document,
                                  const RenderObject& previous_render_tree,
                                  const RenderObject& next_render_tree,
                                  const LayoutBox& current_layout,
                                  const TextMeasureProvider& text_measure) {
    if ((document.dirty_flags & (DomDirtyAttributes | DomDirtyStyle)) == 0U ||
        (document.dirty_flags & DomDirtyTree) != 0U ||
        !dirty_flags_are_style_text_layout_only(document.dirty_flags)) {
        return false;
    }
    return render_tree_shape_and_layout_fields_match(previous_render_tree, next_render_tree) &&
        dirty_text_is_stable_if_present(document, current_layout, text_measure);
}

bool apply_render_styles_to_layout(const RenderObject& render_tree, LayoutBox& layout_tree) {
    if (!render_layout_shape_matches(render_tree, layout_tree)) {
        return false;
    }
    apply_styles_recursive(render_tree, layout_tree);
    return true;
}

} // namespace jellyframe
