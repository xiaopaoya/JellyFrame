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
    std::vector<const LayoutBox*> pending;
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

bool has_local_tree_dirty(const Node& node) {
    if ((node.dirty_flags & DomDirtyTree) == 0U) {
        return false;
    }
    std::vector<const Node*> pending;
    pending.push_back(&node);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if ((current->local_dirty_flags & DomDirtyTree) != 0U) {
            return true;
        }
        for (const auto& child : current->children) {
            if ((child->dirty_flags & DomDirtyTree) != 0U) {
                pending.push_back(child.get());
            }
        }
    }
    return false;
}

struct DirtyNodeBounds {
    const Node* node = nullptr;
    Rect bounds;
};

void merge_dirty_bounds(std::vector<DirtyNodeBounds>& output, const Node* node, Rect bounds) {
    if (node == nullptr || empty_rect(bounds)) {
        return;
    }
    for (DirtyNodeBounds& entry : output) {
        if (entry.node == node) {
            entry.bounds = union_rect(entry.bounds, bounds);
            return;
        }
    }
    output.push_back(DirtyNodeBounds{node, bounds});
}

void append_dirty_bounds_from_layout(const LayoutBox& layout, std::vector<DirtyNodeBounds>& output) {
    std::vector<const LayoutBox*> pending;
    pending.push_back(&layout);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        if (current->node != nullptr) {
            if (current->node->local_dirty_flags != DomDirtyNone) {
                merge_dirty_bounds(output, current->node, subtree_bounds(*current));
                continue;
            }
            if (current->node->dirty_flags == DomDirtyNone) {
                continue;
            }
        }
        for (const auto& child : current->children) {
            pending.push_back(child.get());
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

    std::vector<DirtyNodeBounds> dirty_bounds;
    append_dirty_bounds_from_layout(*previous_layout, dirty_bounds);
    append_dirty_bounds_from_layout(*current_layout, dirty_bounds);
    if (dirty_bounds.empty()) {
        rects.push_back(options.viewport);
        return rects;
    }

    const std::size_t max_rects = std::max<std::size_t>(1, options.max_rects);
    for (const DirtyNodeBounds& bounds : dirty_bounds) {
        append_coalesced(rects, expand_rect(bounds.bounds, options.expansion_px), options.viewport, max_rects);
    }
    if (rects.empty()) {
        rects.push_back(options.viewport);
    }
    return rects;
}

} // namespace jellyframe
