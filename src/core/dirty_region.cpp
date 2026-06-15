#include "core/dirty_region.h"

#include <algorithm>

namespace jellyframe {
namespace {

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
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

Rect union_rect(Rect left, Rect right) {
    if (empty_rect(left)) {
        return right;
    }
    if (empty_rect(right)) {
        return left;
    }
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(left.x + left.width, right.x + right.width);
    const int y2 = std::max(left.y + left.height, right.y + right.height);
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

Rect subtree_bounds(const LayoutBox& box) {
    Rect bounds = box.rect;
    for (const auto& child : box.children) {
        bounds = union_rect(bounds, subtree_bounds(*child));
    }
    return bounds;
}

const LayoutBox* find_box_for_node(const LayoutBox& box, const Node& node) {
    if (box.node == &node) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_box_for_node(*child, node);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

bool has_local_tree_dirty(const Node& node) {
    if ((node.local_dirty_flags & DomDirtyTree) != 0U) {
        return true;
    }
    for (const auto& child : node.children) {
        if (has_local_tree_dirty(*child)) {
            return true;
        }
    }
    return false;
}

void append_dirty_nodes(const Node& node, std::vector<const Node*>& output) {
    if (node.local_dirty_flags != DomDirtyNone) {
        output.push_back(&node);
    }
    for (const auto& child : node.children) {
        if (child->dirty_flags != DomDirtyNone) {
            append_dirty_nodes(*child, output);
        }
    }
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

} // namespace

std::vector<Rect> compute_dirty_rects(const Node& document,
                                      const LayoutBox* previous_layout,
                                      const LayoutBox* current_layout,
                                      const DirtyRegionOptions& options) {
    std::vector<Rect> rects;
    if (document.dirty_flags == DomDirtyNone) {
        return rects;
    }
    if (empty_rect(options.viewport) || previous_layout == nullptr || current_layout == nullptr ||
        has_local_tree_dirty(document)) {
        if (!empty_rect(options.viewport)) {
            rects.push_back(options.viewport);
        }
        return rects;
    }

    std::vector<const Node*> dirty_nodes;
    append_dirty_nodes(document, dirty_nodes);
    if (dirty_nodes.empty()) {
        rects.push_back(options.viewport);
        return rects;
    }

    const std::size_t max_rects = std::max<std::size_t>(1, options.max_rects);
    for (const Node* node : dirty_nodes) {
        Rect dirty{};
        if (const LayoutBox* old_box = find_box_for_node(*previous_layout, *node)) {
            dirty = union_rect(dirty, subtree_bounds(*old_box));
        }
        if (const LayoutBox* new_box = find_box_for_node(*current_layout, *node)) {
            dirty = union_rect(dirty, subtree_bounds(*new_box));
        }
        append_coalesced(rects, expand_rect(dirty, options.expansion_px), options.viewport, max_rects);
    }
    if (rects.empty()) {
        rects.push_back(options.viewport);
    }
    return rects;
}

} // namespace jellyframe
