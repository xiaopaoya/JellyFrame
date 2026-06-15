#include "core/hit_test.h"

#include "core/dom.h"

#include <algorithm>

namespace jellyframe {
namespace {

bool contains(Rect rect, int x, int y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
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
    return hit_test_layer(root, x, y, Rect{}, false);
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
    return make_result(box, x, y);
}

HitTestResult HitTester::hit_test_layer(const LayerNode& layer, int x, int y, Rect clip, bool has_clip) const {
    if (layer.has_clip) {
        clip = has_clip ? intersect_rect(clip, layer.clip_rect) : layer.clip_rect;
        has_clip = true;
        if (empty_rect(clip) || !contains(clip, x, y)) {
            return {};
        }
    } else if (has_clip && !contains(clip, x, y)) {
        return {};
    }

    for (auto it = layer.children.rbegin(); it != layer.children.rend(); ++it) {
        HitTestResult child_result = hit_test_layer(**it, x, y, clip, has_clip);
        if (child_result) {
            return child_result;
        }
    }

    if (layer.box == nullptr || !contains(layer.bounds, x, y)) {
        return {};
    }
    return hit_test_box(*layer.box, x, y);
}

} // namespace jellyframe
