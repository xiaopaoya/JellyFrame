#include "render_core/hit_test.h"

#include "render_core/dom.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
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
    const std::int64_t left_right = static_cast<std::int64_t>(left.x) + static_cast<std::int64_t>(left.width);
    const std::int64_t right_right = static_cast<std::int64_t>(right.x) + static_cast<std::int64_t>(right.width);
    const std::int64_t left_bottom = static_cast<std::int64_t>(left.y) + static_cast<std::int64_t>(left.height);
    const std::int64_t right_bottom = static_cast<std::int64_t>(right.y) + static_cast<std::int64_t>(right.height);
    const int x2 = static_cast<int>(std::min(left_right, right_right));
    const int y2 = static_cast<int>(std::min(left_bottom, right_bottom));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
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
    result.local_x = x - box.rect.x;
    result.local_y = y - box.rect.y;
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
    const int transform_x = layer.transform.translate_x >= 0.0F
        ? static_cast<int>(layer.transform.translate_x + 0.5F)
        : static_cast<int>(layer.transform.translate_x - 0.5F);
    const int transform_y = layer.transform.translate_y >= 0.0F
        ? static_cast<int>(layer.transform.translate_y + 0.5F)
        : static_cast<int>(layer.transform.translate_y - 0.5F);
    const int layer_offset_x = offset_x + transform_x;
    const int layer_offset_y = offset_y + transform_y;
    if (layer.has_clip) {
        Rect translated_clip = layer.clip_rect;
        translated_clip.x += layer_offset_x;
        translated_clip.y += layer_offset_y;
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
        HitTestResult child_result = hit_test_layer(**it, x, y, clip, has_clip, layer_offset_x, layer_offset_y);
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
        translated_clip.x += layer_offset_x;
        translated_clip.y += layer_offset_y;
        if (contains(translated_clip, x, y) &&
            has_corner_radius(child->clip_border_radius) &&
            rounded_rect_coverage(translated_clip, child->clip_border_radius, x, y) == 0) {
            blocked_by_rounded_child = true;
            break;
        }
    }
    if (blocked_by_rounded_child && layer.box != nullptr && !layer.box->style.visibility_hidden) {
        const int document_x = x - layer_offset_x;
        const int document_y = y - layer_offset_y + layer.scroll_y;
        return make_result(*layer.box, document_x, document_y - layer.scroll_y);
    }

    Rect visual_bounds = layer.bounds;
    visual_bounds.x += layer_offset_x;
    visual_bounds.y += layer_offset_y;
    if (layer.box == nullptr || !contains(visual_bounds, x, y) || layer.box->style.visibility_hidden) {
        return {};
    }
    const int document_x = x - layer_offset_x;
    const int document_y = y - layer_offset_y + layer.scroll_y;
    if (layer.scroll_y > 0) {
        for (auto it = layer.box->children.rbegin(); it != layer.box->children.rend(); ++it) {
            HitTestResult child_result = hit_test_box(**it, document_x, document_y);
            if (child_result) {
                return child_result;
            }
        }
        return make_result(*layer.box, document_x, document_y - layer.scroll_y);
    }
    HitTestResult result = hit_test_box(*layer.box, document_x, document_y);
    if (result) {
        return result;
    }
    return make_result(*layer.box, document_x, document_y);
}

} // namespace jellyframe
