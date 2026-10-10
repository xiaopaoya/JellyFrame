#include "render_core/hit_test.h"

#include "render_core/dom.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jellyframe {
namespace {

bool contains(Rect rect, int x, int y) {
    const std::int64_t right = static_cast<std::int64_t>(rect.x) + static_cast<std::int64_t>(rect.width);
    const std::int64_t bottom = static_cast<std::int64_t>(rect.y) + static_cast<std::int64_t>(rect.height);
    return x >= rect.x && y >= rect.y &&
        static_cast<std::int64_t>(x) < right &&
        static_cast<std::int64_t>(y) < bottom;
}

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

struct TransformedPoint {
    float x = 0.0F;
    float y = 0.0F;
};

TransformedPoint apply_layer_transform(const LayerNode& layer,
                                      int translate_x,
                                      int translate_y,
                                      float x,
                                      float y) {
    constexpr float kPi = 3.14159265358979323846F;
    const float origin_x = static_cast<float>(safe_add(layer.bounds.x, translate_x)) +
        static_cast<float>(layer.bounds.width) * static_cast<float>(layer.transform_origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(safe_add(layer.bounds.y, translate_y)) +
        static_cast<float>(layer.bounds.height) * static_cast<float>(layer.transform_origin_y_percent) / 100.0F;
    const float radians = layer.transform.rotate_degrees * kPi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float scale_x = layer.transform.scale_x;
    const float scale_y = layer.transform.scale_y;
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
        std::abs(scale_x) < 0.0001F || std::abs(scale_y) < 0.0001F) {
        return {x, y};
    }
    const float dx = x - origin_x;
    const float dy = y - origin_y;
    return {
        origin_x + (cosine * dx + sine * dy) / scale_x - static_cast<float>(translate_x),
        origin_y + (-sine * dx + cosine * dy) / scale_y - static_cast<float>(translate_y),
    };
}

Rect transformed_layer_bounds(const LayerNode& layer, int translate_x, int translate_y) {
    constexpr float kPi = 3.14159265358979323846F;
    const float origin_x = static_cast<float>(safe_add(layer.bounds.x, translate_x)) +
        static_cast<float>(layer.bounds.width) * static_cast<float>(layer.transform_origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(safe_add(layer.bounds.y, translate_y)) +
        static_cast<float>(layer.bounds.height) * static_cast<float>(layer.transform_origin_y_percent) / 100.0F;
    const float radians = layer.transform.rotate_degrees * kPi / 180.0F;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float corners[4][2] = {
        {static_cast<float>(layer.bounds.x + translate_x), static_cast<float>(layer.bounds.y + translate_y)},
        {static_cast<float>(safe_edge(layer.bounds.x, layer.bounds.width) + translate_x), static_cast<float>(layer.bounds.y + translate_y)},
        {static_cast<float>(layer.bounds.x + translate_x), static_cast<float>(safe_edge(layer.bounds.y, layer.bounds.height) + translate_y)},
        {static_cast<float>(safe_edge(layer.bounds.x, layer.bounds.width) + translate_x), static_cast<float>(safe_edge(layer.bounds.y, layer.bounds.height) + translate_y)},
    };
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    for (int index = 0; index < 4; ++index) {
        const float dx = (corners[index][0] - origin_x) * layer.transform.scale_x;
        const float dy = (corners[index][1] - origin_y) * layer.transform.scale_y;
        const float x = origin_x + dx * cosine - dy * sine;
        const float y = origin_y + dx * sine + dy * cosine;
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
    const int left = floor_float_to_int(min_x);
    const int top = floor_float_to_int(min_y);
    const int right = ceil_float_to_int(max_x);
    const int bottom = ceil_float_to_int(max_y);
    return {left, top, std::max(1, safe_span(left, right)), std::max(1, safe_span(top, bottom))};
}

const Node* event_target_for(const Node* node) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->type == NodeType::Element) {
        return node;
    }
    for (const Node* ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (ancestor->type == NodeType::Element) {
            return ancestor;
        }
    }
    return node;
}

HitTestResult make_result(const LayoutBox& box, int x, int y) {
    HitTestResult result;
    result.box = &box;
    result.node = event_target_for(box.node);
    result.local_x = safe_add(x, safe_negate(box.rect.x));
    result.local_y = safe_add(y, safe_negate(box.rect.y));
    return result;
}

} // namespace

HitTestResult HitTester::hit_test(const LayoutBox& root, int x, int y) const {
    return hit_test_box(root, x, y);
}

HitTestResult HitTester::hit_test(const LayerNode& root, int x, int y) const {
    return hit_test_layer(root, x, y, Rect{}, false, 0, 0);
}

HitTestResult HitTester::hit_test_box(const LayoutBox& box, int x, int y) const {
    if (!contains(box.rect, x, y)) {
        return {};
    }

    for (auto it = box.children.rbegin(); it != box.children.rend(); ++it) {
        HitTestResult child_result = hit_test_box(**it, x, y);
        if (child_result) {
            return child_result;
        }
    }

    if (box.node == nullptr) {
        return {};
    }
    if (box.style.visibility_hidden) {
        return {};
    }
    return make_result(box, x, y);
}

HitTestResult HitTester::hit_test_layer(const LayerNode& layer,
                                        int x,
                                        int y,
                                        Rect clip,
                                        bool has_clip,
                                        int offset_x,
                                        int offset_y) const {
    const int transform_x = round_float_to_int(layer.transform.translate_x);
    const int transform_y = round_float_to_int(layer.transform.translate_y);
    const int layer_offset_x = safe_add(offset_x, transform_x);
    const int layer_offset_y = safe_add(offset_y, transform_y);
    const bool transformed = std::abs(layer.transform.scale_x - 1.0F) >= 0.0001F ||
        std::abs(layer.transform.scale_y - 1.0F) >= 0.0001F ||
        std::abs(layer.transform.rotate_degrees) >= 0.0001F;
    const Rect hit_bounds = transformed
        ? transformed_layer_bounds(layer, layer_offset_x, layer_offset_y)
        : Rect{safe_add(layer.bounds.x, layer_offset_x), safe_add(layer.bounds.y, layer_offset_y),
               layer.bounds.width, layer.bounds.height};
    const TransformedPoint local_point = transformed
        ? apply_layer_transform(layer, layer_offset_x, layer_offset_y, static_cast<float>(x), static_cast<float>(y))
        : TransformedPoint{static_cast<float>(x - layer_offset_x), static_cast<float>(y - layer_offset_y)};
    const int hit_x = round_float_to_int(local_point.x);
    const int hit_y = round_float_to_int(local_point.y);
    if (transformed && !contains(hit_bounds, x, y)) {
        return {};
    }
    if (layer.has_clip) {
        Rect translated_clip = layer.clip_rect;
        translated_clip.x = safe_add(translated_clip.x, layer_offset_x);
        translated_clip.y = safe_add(translated_clip.y, layer_offset_y);
        if (has_corner_radius(layer.clip_border_radius) &&
            rounded_rect_coverage(translated_clip, layer.clip_border_radius, x, y) == 0) {
            return {};
        }
        clip = has_clip ? intersect_rect(clip, translated_clip) : translated_clip;
        has_clip = true;
        if (empty_rect(clip) || !contains(clip, x, y)) {
            return {};
        }
    } else if (has_clip && !contains(clip, x, y)) {
        return {};
    }

    for (auto it = layer.children.rbegin(); it != layer.children.rend(); ++it) {
        HitTestResult child_result = hit_test_layer(**it, transformed ? hit_x : x, transformed ? hit_y : y,
                                                    clip, has_clip, layer_offset_x, layer_offset_y);
        if (child_result) {
            return child_result;
        }
    }

    // A clipped layer can reject a point before the fallback DOM walk below.
    // Keep that layer's DOM subtree from becoming hittable through the parent
    // fallback, especially at rounded corners.
    bool blocked_by_rounded_child = false;
    for (const auto& child : layer.children) {
        if (child == nullptr || !child->has_clip) {
            continue;
        }
        Rect translated_clip = child->clip_rect;
        translated_clip.x = safe_add(translated_clip.x, layer_offset_x);
        translated_clip.y = safe_add(translated_clip.y, layer_offset_y);
        if (contains(translated_clip, x, y) &&
            has_corner_radius(child->clip_border_radius) &&
            rounded_rect_coverage(translated_clip, child->clip_border_radius, x, y) == 0) {
            blocked_by_rounded_child = true;
            break;
        }
    }
    if (blocked_by_rounded_child && layer.box != nullptr && !layer.box->style.visibility_hidden) {
        const int document_x = safe_add(x, safe_negate(layer_offset_x));
        const int document_y = safe_add(safe_add(y, safe_negate(layer_offset_y)), layer.scroll_y);
        return make_result(*layer.box, document_x, safe_add(document_y, safe_negate(layer.scroll_y)));
    }

    Rect visual_bounds = hit_bounds;
    if (layer.box == nullptr || !contains(visual_bounds, x, y) || layer.box->style.visibility_hidden) {
        return {};
    }
    const int document_x = transformed ? hit_x : safe_add(x, safe_negate(layer_offset_x));
    const int document_y = transformed ? safe_add(hit_y, layer.scroll_y) :
        safe_add(safe_add(y, safe_negate(layer_offset_y)), layer.scroll_y);
    if (layer.scroll_y > 0) {
        for (auto it = layer.box->children.rbegin(); it != layer.box->children.rend(); ++it) {
            HitTestResult child_result = hit_test_box(**it, document_x, document_y);
            if (child_result) {
                return child_result;
            }
        }
        return make_result(*layer.box, document_x, safe_add(document_y, safe_negate(layer.scroll_y)));
    }
    HitTestResult result = hit_test_box(*layer.box, document_x, document_y);
    if (result) {
        return result;
    }
    return make_result(*layer.box, document_x, document_y);
}

} // namespace jellyframe
