#include "render_core/style_repaint.h"

#include "render_core/text_layout_reuse.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace jellyframe {
namespace {

bool edge_sizes_equal(const EdgeSizes& left, const EdgeSizes& right) {
    return left.top == right.top && left.right == right.right &&
        left.bottom == right.bottom && left.left == right.left;
}

bool color_equal(Color left, Color right) {
    return left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a;
}

bool layout_fields_equal(const Style& left, const Style& right) {
    return left.display == right.display &&
        edge_sizes_equal(left.margin, right.margin) &&
        left.margin_left_auto == right.margin_left_auto &&
        left.margin_right_auto == right.margin_right_auto &&
        edge_sizes_equal(left.padding, right.padding) &&
        edge_sizes_equal(left.border_width, right.border_width) &&
        left.background_overlay_packed == right.background_overlay_packed &&
        left.width == right.width &&
        left.height == right.height &&
        left.min_width == right.min_width &&
        left.min_height == right.min_height &&
        left.max_width == right.max_width &&
        left.max_height == right.max_height &&
        left.width_percent == right.width_percent &&
        left.height_percent == right.height_percent &&
        left.min_width_percent == right.min_width_percent &&
        left.min_height_percent == right.min_height_percent &&
        left.max_width_percent == right.max_width_percent &&
        left.max_height_percent == right.max_height_percent &&
        left.aspect_ratio_width == right.aspect_ratio_width &&
        left.aspect_ratio_height == right.aspect_ratio_height &&
        left.font_size == right.font_size &&
        left.font_size_specified == right.font_size_specified &&
        left.font_weight == right.font_weight &&
        left.font_weight_specified == right.font_weight_specified &&
        left.font_family_hash == right.font_family_hash &&
        left.font_family_specified == right.font_family_specified &&
        left.line_height == right.line_height &&
        left.line_height_specified == right.line_height_specified &&
        left.text_indent == right.text_indent &&
        left.text_indent_specified == right.text_indent_specified &&
        left.letter_spacing == right.letter_spacing &&
        left.letter_spacing_specified == right.letter_spacing_specified &&
        left.text_transform == right.text_transform &&
        left.text_transform_specified == right.text_transform_specified &&
        left.white_space_nowrap == right.white_space_nowrap &&
        left.white_space_specified == right.white_space_specified &&
        left.text_overflow_ellipsis == right.text_overflow_ellipsis &&
        left.text_overflow_specified == right.text_overflow_specified &&
        left.position == right.position &&
        left.overflow_wrap_anywhere == right.overflow_wrap_anywhere &&
        left.overflow_wrap_specified == right.overflow_wrap_specified &&
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
        left.flex_direction == right.flex_direction &&
        left.flex_wrap == right.flex_wrap &&
        left.flex_grow == right.flex_grow &&
        left.flex_shrink == right.flex_shrink &&
        left.flex_basis == right.flex_basis &&
        left.flex_order == right.flex_order &&
        left.align_self == right.align_self &&
        left.align_content == right.align_content &&
        left.grid_min_track_width == right.grid_min_track_width &&
        left.grid_template_column_count == right.grid_template_column_count &&
        left.grid_template_column_widths == right.grid_template_column_widths &&
        left.grid_template_row_count == right.grid_template_row_count &&
        left.grid_template_row_heights == right.grid_template_row_heights &&
        left.grid_auto_row_min == right.grid_auto_row_min &&
        left.grid_column_start == right.grid_column_start &&
        left.grid_column_span == right.grid_column_span &&
        left.grid_row_start == right.grid_row_start &&
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
    std::vector<std::pair<const RenderObject*, const RenderObject*>> pending;
    pending.push_back({&previous, &next});
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const RenderObject& left = *current.first;
        const RenderObject& right = *current.second;
        if (left.type != right.type || left.node != right.node ||
            left.children.size() != right.children.size() ||
            !layout_fields_equal(left.style, right.style)) {
            return false;
        }
        for (std::size_t index = 0; index < left.children.size(); ++index) {
            pending.push_back({left.children[index].get(), right.children[index].get()});
        }
    }
    return true;
}

bool dirty_flags_are_style_text_layout_only(DomDirtyFlags flags) {
    return (flags & ~(DomDirtyAttributes | DomDirtyStyle | DomDirtyText | DomDirtyLayout | DomDirtyPaint)) == 0U;
}

bool render_layout_shape_matches(const RenderObject& render, const LayoutBox& layout) {
    std::vector<std::pair<const RenderObject*, const LayoutBox*>> pending;
    pending.push_back({&render, &layout});
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const RenderObject& render_node = *current.first;
        const LayoutBox& layout_node = *current.second;
        if (render_node.node != layout_node.node || render_node.children.size() != layout_node.children.size()) {
            return false;
        }
        for (std::size_t index = 0; index < render_node.children.size(); ++index) {
            pending.push_back({render_node.children[index].get(), layout_node.children[index].get()});
        }
    }
    return true;
}

void apply_styles_iterative(const RenderObject& render, LayoutBox& layout) {
    std::vector<std::pair<const RenderObject*, LayoutBox*>> pending;
    pending.push_back({&render, &layout});
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const RenderObject& render_node = *current.first;
        LayoutBox& layout_node = *current.second;
        layout_node.style = render_node.style;
        for (std::size_t index = 0; index < render_node.children.size(); ++index) {
            pending.push_back({render_node.children[index].get(), layout_node.children[index].get()});
        }
    }
}

bool style_override_needed(const Style& previous, const Style& next) {
    return previous.transform != next.transform ||
        previous.opacity != next.opacity ||
        !color_equal(previous.background_color, next.background_color) ||
        !color_equal(previous.color, next.color);
}

void append_style_override(const RenderObject& object,
                           std::vector<StyleOverride>& overrides,
                           bool use_transform,
                           bool use_opacity,
                           bool use_background_color,
                           bool use_color) {
    if (object.node == nullptr) {
        return;
    }
    StyleOverride override;
    override.node = object.node;
    if (use_transform) {
        override.has_transform = true;
        override.transform = object.style.transform;
    }
    if (use_opacity) {
        override.has_opacity = true;
        override.opacity = object.style.opacity;
    }
    if (use_background_color) {
        override.has_background_color = true;
        override.background_color = object.style.background_color;
    }
    if (use_color) {
        override.has_color = true;
        override.color = object.style.color;
    }
    overrides.push_back(std::move(override));
}

void collect_style_overrides_iterative(const RenderObject& previous,
                                       const RenderObject& next,
                                       std::vector<StyleOverride>& previous_overrides,
                                       std::vector<StyleOverride>& current_overrides) {
    std::vector<std::pair<const RenderObject*, const RenderObject*>> pending;
    pending.push_back({&previous, &next});
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        const RenderObject& previous_node = *current.first;
        const RenderObject& next_node = *current.second;
        if (previous_node.node == next_node.node && style_override_needed(previous_node.style, next_node.style)) {
            const bool transform = previous_node.style.transform != next_node.style.transform;
            const bool opacity = previous_node.style.opacity != next_node.style.opacity;
            const bool background = !color_equal(previous_node.style.background_color, next_node.style.background_color);
            const bool color = !color_equal(previous_node.style.color, next_node.style.color);
            append_style_override(previous_node, previous_overrides, transform, opacity, background, color);
            append_style_override(next_node, current_overrides, transform, opacity, background, color);
        }
        const std::size_t child_count = std::min(previous_node.children.size(), next_node.children.size());
        for (std::size_t index = 0; index < child_count; ++index) {
            pending.push_back({previous_node.children[index].get(), next_node.children[index].get()});
        }
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
        dirty_text_nodes_have_stable_layout(document, current_layout, text_measure, false);
}

void collect_style_repaint_overrides(const RenderObject& previous_render_tree,
                                     const RenderObject& next_render_tree,
                                     std::vector<StyleOverride>& previous_overrides,
                                     std::vector<StyleOverride>& current_overrides) {
    previous_overrides.clear();
    current_overrides.clear();
    collect_style_overrides_iterative(previous_render_tree,
                                      next_render_tree,
                                      previous_overrides,
                                      current_overrides);
}

bool apply_render_styles_to_layout(const RenderObject& render_tree, LayoutBox& layout_tree) {
    if (!render_layout_shape_matches(render_tree, layout_tree)) {
        return false;
    }
    apply_styles_iterative(render_tree, layout_tree);
    return true;
}

} // namespace jellyframe
