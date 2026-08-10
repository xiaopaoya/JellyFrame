#include "render_core/animation_invalidation.h"

#include <algorithm>
#include <cmath>

namespace jellyframe {
namespace {

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

Rect union_rect(Rect left, Rect right) {
    if (empty_rect(left)) {
        return right;
    }
    if (empty_rect(right)) {
        return left;
    }
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::max(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

Rect expand_rect(Rect rect, int amount) {
    if (empty_rect(rect) || amount <= 0) {
        return rect;
    }
    rect.x -= amount;
    rect.y -= amount;
    rect.width += amount * 2;
    rect.height += amount * 2;
    return rect;
}

int round_transform_offset(float value) {
    return static_cast<int>(value >= 0.0F ? value + 0.5F : value - 0.5F);
}

constexpr int kMaxAnimatedEffectExtent = 128;

Rect rotated_scaled_bounds(Rect bounds, const Transform2D& transform, int origin_x_percent, int origin_y_percent) {
    const float scaled_width = std::max(1.0F, static_cast<float>(bounds.width) * transform.scale_x);
    const float scaled_height = std::max(1.0F, static_cast<float>(bounds.height) * transform.scale_y);
    const float origin_x = static_cast<float>(bounds.x) +
        static_cast<float>(bounds.width) * static_cast<float>(origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(bounds.y) +
        static_cast<float>(bounds.height) * static_cast<float>(origin_y_percent) / 100.0F;
    const float left = origin_x -
        scaled_width * static_cast<float>(origin_x_percent) / 100.0F;
    const float top = origin_y -
        scaled_height * static_cast<float>(origin_y_percent) / 100.0F;

    constexpr float kPi = 3.14159265358979323846F;
    const float radians = transform.rotate_degrees * kPi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float corners[4][2] = {
        {left, top},
        {left + scaled_width, top},
        {left, top + scaled_height},
        {left + scaled_width, top + scaled_height},
    };
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    for (int index = 0; index < 4; ++index) {
        const float dx = corners[index][0] - origin_x;
        const float dy = corners[index][1] - origin_y;
        const float x = origin_x + dx * c - dy * s;
        const float y = origin_y + dx * s + dy * c;
        if (index == 0) {
            min_x = max_x = x;
            min_y = max_y = y;
        } else {
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    const int x = static_cast<int>(std::floor(min_x));
    const int y = static_cast<int>(std::floor(min_y));
    const int right = static_cast<int>(std::ceil(max_x));
    const int bottom = static_cast<int>(std::ceil(max_y));
    return Rect{x, y, std::max(1, right - x), std::max(1, bottom - y)};
}

Rect subtree_bounds(const LayoutBox& box, std::vector<const LayoutBox*>& pending) {
    Rect bounds = box.rect;
    pending.clear();
    pending.reserve(box.children.size());
    for (const auto& child : box.children) {
        pending.push_back(child.get());
    }
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        bounds = union_rect(bounds, current->rect);
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return bounds;
}

const StyleOverride* find_override(const std::vector<StyleOverride>& overrides, const Node* node) {
    for (const StyleOverride& override : overrides) {
        if (override.node == node) {
            return &override;
        }
    }
    return nullptr;
}

Transform2D resolved_transform(const Style& base_style, const StyleOverride* override) {
    const std::string& transform_source =
        override != nullptr && override->has_transform ? override->transform : base_style.transform;
    Transform2D transform;
    if (!transform_source.empty() && !parse_css_transform_2d(transform_source, transform)) {
        transform = Transform2D{};
    }
    return transform;
}

Rect transformed_bounds(Rect bounds, const Style& base_style, const StyleOverride* override) {
    const Transform2D transform = resolved_transform(base_style, override);
    bounds.x += round_transform_offset(transform.translate_x);
    bounds.y += round_transform_offset(transform.translate_y);
    if (std::abs(transform.scale_x - 1.0F) >= 0.001F ||
        std::abs(transform.scale_y - 1.0F) >= 0.001F ||
        std::abs(transform.rotate_degrees) >= 0.001F) {
        return rotated_scaled_bounds(bounds,
                                     transform,
                                     base_style.transform_origin_x_percent,
                                     base_style.transform_origin_y_percent);
    }
    return bounds;
}

Rect expand_for_paint_effects(Rect bounds, const Style& style, const Transform2D& transform) {
    const auto bounded_extent = [](int value) {
        return std::clamp(value, 0, kMaxAnimatedEffectExtent);
    };
    const auto bounded_offset = [](int value) {
        return std::clamp(value, -kMaxAnimatedEffectExtent, kMaxAnimatedEffectExtent);
    };
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    const int scale_extent_x = static_cast<int>(std::ceil(std::clamp(
        std::abs(transform.scale_x), 1.0F, static_cast<float>(kMaxAnimatedEffectExtent))));
    const int scale_extent_y = static_cast<int>(std::ceil(std::clamp(
        std::abs(transform.scale_y), 1.0F, static_cast<float>(kMaxAnimatedEffectExtent))));

    if (style.box_shadow.enabled) {
        const BoxShadowStyle& shadow = style.box_shadow;
        const int extent = std::max(1, bounded_extent(static_cast<int>(shadow.blur) +
            std::max(0, static_cast<int>(shadow.spread))));
        const int offset_x = bounded_offset(static_cast<int>(shadow.offset_x));
        const int offset_y = bounded_offset(static_cast<int>(shadow.offset_y));
        const int shadow_left = (std::min(0, offset_x) - extent) * scale_extent_x;
        const int shadow_top = (std::min(0, offset_y) - extent) * scale_extent_y;
        const int shadow_right = (std::max(0, offset_x) + extent) * scale_extent_x;
        const int shadow_bottom = (std::max(0, offset_y) + extent) * scale_extent_y;
        left = std::min(left, shadow_left);
        top = std::min(top, shadow_top);
        right = std::max(right, shadow_right);
        bottom = std::max(bottom, shadow_bottom);
    }
    if (style.text_shadow.enabled) {
        const TextShadowStyle& shadow = style.text_shadow;
        const int extent = std::max(1, bounded_extent(static_cast<int>(shadow.blur)));
        const int offset_x = bounded_offset(static_cast<int>(shadow.offset_x));
        const int offset_y = bounded_offset(static_cast<int>(shadow.offset_y));
        left = std::min(left, (std::min(0, offset_x) - extent) * scale_extent_x);
        top = std::min(top, (std::min(0, offset_y) - extent) * scale_extent_y);
        right = std::max(right, (std::max(0, offset_x) + extent) * scale_extent_x);
        bottom = std::max(bottom, (std::max(0, offset_y) + extent) * scale_extent_y);
    }
    const int outline_extent = std::min(kMaxAnimatedEffectExtent,
                                        bounded_extent(style.outline_width) +
                                            bounded_extent(std::abs(style.outline_offset)));
    left = std::min(left, -outline_extent);
    top = std::min(top, -outline_extent);
    right = std::max(right, outline_extent);
    bottom = std::max(bottom, outline_extent);
    return Rect{safe_edge(bounds.x, left),
                safe_edge(bounds.y, top),
                safe_add(bounds.width, safe_add(-left, right)),
                safe_add(bounds.height, safe_add(-top, bottom))};
}

bool override_affects_paint(const StyleOverride& override) {
    return override.has_opacity || override.has_color || override.has_background_color || override.has_transform;
}

void append_coalesced(std::vector<Rect>& rects, Rect rect, Rect viewport, std::size_t max_rects) {
    rect = intersect_rect(rect, viewport);
    if (empty_rect(rect)) {
        return;
    }
    if (rects.size() >= max_rects && !rects.empty()) {
        rects.front() = union_rect(rects.front(), rect);
        return;
    }
    rects.push_back(rect);
}

void collect_animation_rects_iterative(const LayoutBox& box,
                                       const std::vector<StyleOverride>& previous_overrides,
                                       const std::vector<StyleOverride>& current_overrides,
                                       const AnimationInvalidationOptions& options,
                                       std::vector<const LayoutBox*>& pending_boxes,
                                       DirtyRegionResult& result) {
    std::vector<const LayoutBox*> pending;
    pending.push_back(&box);
    while (!pending.empty()) {
        const LayoutBox& current_box = *pending.back();
        pending.pop_back();
        if (current_box.node != nullptr) {
            const StyleOverride* previous = find_override(previous_overrides, current_box.node);
            const StyleOverride* current = find_override(current_overrides, current_box.node);
            if ((previous != nullptr && override_affects_paint(*previous)) ||
                (current != nullptr && override_affects_paint(*current))) {
                const Rect base_bounds = subtree_bounds(current_box, pending_boxes);
                const Transform2D previous_transform = resolved_transform(current_box.style, previous);
                const Transform2D current_transform = resolved_transform(current_box.style, current);
                Rect dirty = expand_for_paint_effects(
                    transformed_bounds(base_bounds, current_box.style, previous), current_box.style, previous_transform);
                dirty = union_rect(
                    dirty,
                    expand_for_paint_effects(
                        transformed_bounds(base_bounds, current_box.style, current), current_box.style, current_transform));
                append_coalesced(result.rects,
                                 expand_rect(dirty, options.expansion_px),
                                 options.viewport,
                                 std::max<std::size_t>(1, options.max_rects));
            }
        }
        for (const auto& child : current_box.children) {
            pending.push_back(child.get());
        }
    }
}

} // namespace

DirtyRegionResult compute_animation_dirty_region(const LayoutBox& layout,
                                                 const std::vector<StyleOverride>& previous_overrides,
                                                 const std::vector<StyleOverride>& current_overrides,
                                                 const AnimationInvalidationOptions& options) {
    DirtyRegionResult result;
    compute_animation_dirty_region_into(layout, previous_overrides, current_overrides, options, result);
    return result;
}

void compute_animation_dirty_region_into(const LayoutBox& layout,
                                         const std::vector<StyleOverride>& previous_overrides,
                                         const std::vector<StyleOverride>& current_overrides,
                                         const AnimationInvalidationOptions& options,
                                         DirtyRegionResult& result) {
    result.rects.clear();
    result.mode = DirtyRegionMode::Clean;
    result.fallback_reason = DirtyRegionFallbackReason::None;
    if (empty_rect(options.viewport) || (previous_overrides.empty() && current_overrides.empty())) {
        return;
    }
    std::vector<const LayoutBox*> pending_boxes;
    collect_animation_rects_iterative(layout, previous_overrides, current_overrides, options, pending_boxes, result);
    if (!result.rects.empty()) {
        result.mode = DirtyRegionMode::DirtyRects;
    } else {
        result.mode = DirtyRegionMode::FullFrame;
        result.fallback_reason = DirtyRegionFallbackReason::NoDirtyBounds;
        result.rects.push_back(options.viewport);
    }
}

void merge_animation_dirty_region_into(const LayoutBox& layout,
                                       const std::vector<StyleOverride>& previous_overrides,
                                       const std::vector<StyleOverride>& current_overrides,
                                       const AnimationInvalidationOptions& options,
                                       DirtyRegionResult& target,
                                       DirtyRegionResult& scratch) {
    if (previous_overrides.empty() && current_overrides.empty()) {
        return;
    }
    compute_animation_dirty_region_into(
        layout, previous_overrides, current_overrides, options, scratch);
    merge_dirty_region_into(target, scratch, options.max_rects);
}

} // namespace jellyframe
