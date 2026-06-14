#include "core/layer_tree.h"

#include <algorithm>
#include <utility>

namespace wearweb {
namespace {

bool has_border(const EdgeSizes& border) {
    return border.top > 0 || border.right > 0 || border.bottom > 0 || border.left > 0;
}

bool is_visible_background(Color color) {
    return color.a != 0;
}

bool has_overflow_clip(const Style& style) {
    return style.overflow == "hidden" || style.overflow == "scroll" ||
        style.overflow == "auto" || style.overflow == "clip";
}

bool is_positioned(const Style& style) {
    return !style.position.empty();
}

bool has_transform(const Style& style) {
    return !style.transform.empty();
}

bool has_shadow(const Style& style) {
    return !style.box_shadow.empty() && style.box_shadow != "none";
}

void push_fill_rect(DisplayList& display_list, Rect rect, Color color, int border_radius = 0) {
    if (rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = color;
    command.color2 = color;
    command.border_radius = border_radius;
    display_list.push_back(std::move(command));
}

void push_border_rects(DisplayList& display_list, Rect rect, const EdgeSizes& border, Color color) {
    if (!has_border(border) || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return;
    }
    push_fill_rect(display_list, Rect{rect.x, rect.y, rect.width, border.top}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y + rect.height - border.bottom, rect.width, border.bottom}, color);
    push_fill_rect(display_list, Rect{rect.x, rect.y, border.left, rect.height}, color);
    push_fill_rect(display_list, Rect{rect.x + rect.width - border.right, rect.y, border.right, rect.height}, color);
}

void paint_box_self(const LayoutBox& box, DisplayList& display_list) {
    if (is_visible_background(box.style.background_color)) {
        push_fill_rect(display_list, box.rect, box.style.background_color, box.style.border_radius);
    }

    if (has_border(box.style.border_width)) {
        push_border_rects(display_list, box.rect, box.style.border_width, box.style.border_color);
    }

    if (box.node != nullptr && box.node->type == NodeType::Text) {
        DisplayCommand command;
        command.type = DisplayCommandType::Text;
        command.rect = box.rect;
        command.color = box.style.color;
        command.color2 = box.style.color;
        command.text = box.node->text;
        command.font_size = box.style.font_size;
        display_list.push_back(std::move(command));
    }
}

LayerReasons layer_reasons_for(const LayoutBox& box, bool root) {
    LayerReasons reasons = LayerReasonNone;
    if (root) {
        reasons |= LayerReasonRoot;
    }
    if (has_overflow_clip(box.style)) {
        reasons |= LayerReasonOverflowClip;
    }
    if (box.style.opacity < 0.999F) {
        reasons |= LayerReasonOpacity;
    }
    if (has_transform(box.style)) {
        reasons |= LayerReasonTransform;
    }
    if (is_positioned(box.style)) {
        reasons |= LayerReasonPositioned;
    }
    if (!box.style.z_index_auto) {
        reasons |= LayerReasonZIndex;
    }
    if (has_shadow(box.style)) {
        reasons |= LayerReasonShadow;
    }
    if (box.style.border_radius > 0 && has_overflow_clip(box.style)) {
        reasons |= LayerReasonRoundedClip;
    }
    return reasons;
}

LayerType layer_type_for(LayerReasons reasons) {
    if ((reasons & LayerReasonRoot) != 0U) {
        return LayerType::Root;
    }
    if ((reasons & (LayerReasonOpacity | LayerReasonTransform)) != 0U) {
        return LayerType::Composited;
    }
    if ((reasons & (LayerReasonPositioned | LayerReasonZIndex)) != 0U) {
        return LayerType::Stacking;
    }
    if ((reasons & (LayerReasonOverflowClip | LayerReasonRoundedClip)) != 0U) {
        return LayerType::Clip;
    }
    return LayerType::Paint;
}

bool needs_own_layer(LayerReasons reasons) {
    return (reasons & ~static_cast<LayerReasons>(LayerReasonRoot)) != 0U;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(left.x + left.width, right.x + right.width);
    const int y2 = std::min(left.y + left.height, right.y + right.height);
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = static_cast<std::uint8_t>(std::max(0, std::min(255, alpha)));
    return color;
}

void append_flattened_command(DisplayList& output, const DisplayCommand& command, Rect clip, bool has_clip, float opacity) {
    DisplayCommand flattened = command;
    if (has_clip) {
        flattened.rect = intersect_rect(flattened.rect, clip);
        if (empty_rect(flattened.rect)) {
            return;
        }
    }
    flattened.color = with_opacity(flattened.color, opacity);
    flattened.color2 = with_opacity(flattened.color2, opacity);
    if (flattened.color.a == 0) {
        return;
    }
    output.push_back(std::move(flattened));
}

void flatten_layer(const LayerNode& layer, DisplayList& output, Rect clip, bool has_clip, float opacity) {
    if (layer.has_clip) {
        clip = has_clip ? intersect_rect(clip, layer.clip_rect) : layer.clip_rect;
        has_clip = true;
        if (empty_rect(clip)) {
            return;
        }
    }

    const float layer_opacity = opacity * layer.opacity;
    for (const DisplayCommand& command : layer.display_list) {
        append_flattened_command(output, command, clip, has_clip, layer_opacity);
    }
    for (const auto& child : layer.children) {
        flatten_layer(*child, output, clip, has_clip, layer_opacity);
    }
}

void sort_layer_children(LayerNode& layer) {
    std::stable_sort(layer.children.begin(), layer.children.end(),
        [](const std::unique_ptr<LayerNode>& left, const std::unique_ptr<LayerNode>& right) {
            if (left->z_index != right->z_index) {
                return left->z_index < right->z_index;
            }
            return left->source_order < right->source_order;
        });
    for (const auto& child : layer.children) {
        sort_layer_children(*child);
    }
}

} // namespace

std::unique_ptr<LayerNode> LayerTreeBuilder::build(const LayoutBox& root) const {
    std::size_t next_source_order = 0;
    auto root_layer = std::make_unique<LayerNode>();
    root_layer->type = LayerType::Root;
    root_layer->reasons = layer_reasons_for(root, true);
    root_layer->box = &root;
    root_layer->bounds = root.rect;
    root_layer->clip_rect = root.rect;
    root_layer->has_clip = has_overflow_clip(root.style);
    root_layer->opacity = root.style.opacity;
    root_layer->z_index = root.style.z_index;
    root_layer->source_order = next_source_order++;

    paint_box_self(root, root_layer->display_list);
    build_children(root, *root_layer, next_source_order);
    sort_layer_children(*root_layer);
    return root_layer;
}

DisplayList LayerTreeBuilder::flatten(const LayerNode& root) const {
    DisplayList output;
    output.reserve(count_layer_display_commands(root));
    flatten_layer(root, output, Rect{}, false, 1.0F);
    return output;
}

void LayerTreeBuilder::build_children(const LayoutBox& box, LayerNode& layer, std::size_t& next_source_order) const {
    for (const auto& child : box.children) {
        build_box(*child, layer, next_source_order);
    }
}

void LayerTreeBuilder::build_box(const LayoutBox& box, LayerNode& parent_layer, std::size_t& next_source_order) const {
    const LayerReasons reasons = layer_reasons_for(box, false);
    if (needs_own_layer(reasons)) {
        auto child_layer = std::make_unique<LayerNode>();
        child_layer->type = layer_type_for(reasons);
        child_layer->reasons = reasons;
        child_layer->box = &box;
        child_layer->bounds = box.rect;
        child_layer->clip_rect = box.rect;
        child_layer->has_clip = (reasons & LayerReasonOverflowClip) != 0U;
        child_layer->opacity = box.style.opacity;
        child_layer->z_index = box.style.z_index_auto ? 0 : box.style.z_index;
        child_layer->source_order = next_source_order++;
        paint_box_self(box, child_layer->display_list);
        build_children(box, *child_layer, next_source_order);
        parent_layer.children.push_back(std::move(child_layer));
        return;
    }

    paint_box_self(box, parent_layer.display_list);
    build_children(box, parent_layer, next_source_order);
}

std::size_t count_layers(const LayerNode& layer) {
    std::size_t count = 1;
    for (const auto& child : layer.children) {
        count += count_layers(*child);
    }
    return count;
}

std::size_t count_layer_display_commands(const LayerNode& layer) {
    std::size_t count = layer.display_list.size();
    for (const auto& child : layer.children) {
        count += count_layer_display_commands(*child);
    }
    return count;
}

} // namespace wearweb
