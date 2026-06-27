#include "render_core/display_invalidation.h"

#include <algorithm>
#include <vector>

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

bool intersects_any(Rect rect, const Rect* dirty_rects, std::size_t dirty_rect_count) {
    if (empty_rect(rect)) {
        return false;
    }
    for (std::size_t index = 0; index < dirty_rect_count; ++index) {
        if (!empty_rect(intersect_rect(rect, dirty_rects[index]))) {
            return true;
        }
    }
    return false;
}

std::size_t rect_area(Rect rect) {
    if (empty_rect(rect)) {
        return 0;
    }
    return static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height);
}

bool contains_rect(Rect outer, Rect inner) {
    return !empty_rect(inner) &&
        inner.x >= outer.x &&
        inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}

std::vector<Rect> normalize_dirty_rects(const Rect* dirty_rects, std::size_t dirty_rect_count) {
    std::vector<Rect> normalized;
    normalized.reserve(dirty_rect_count);
    for (std::size_t index = 0; index < dirty_rect_count; ++index) {
        const Rect dirty = dirty_rects[index];
        if (empty_rect(dirty)) {
            continue;
        }
        bool covered = false;
        for (const Rect& existing : normalized) {
            if (contains_rect(existing, dirty)) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }
        normalized.erase(
            std::remove_if(normalized.begin(), normalized.end(), [dirty](Rect existing) {
                return contains_rect(dirty, existing);
            }),
            normalized.end());
        normalized.push_back(dirty);
    }
    return normalized;
}

bool is_composited_layer(const LayerNode& layer) {
    return layer.type == LayerType::Composited || layer.opacity < 0.999F;
}

struct PendingLayer {
    const LayerNode* layer = nullptr;
    Rect clip;
    bool has_clip = false;
};

} // namespace

DisplayInvalidationResult analyze_display_invalidation(const LayerNode& root,
                                                       const Rect* dirty_rects,
                                                       std::size_t dirty_rect_count) {
    DisplayInvalidationResult result;
    if (dirty_rects == nullptr || dirty_rect_count == 0) {
        return result;
    }
    const std::vector<Rect> normalized_dirty_rects = normalize_dirty_rects(dirty_rects, dirty_rect_count);
    if (normalized_dirty_rects.empty()) {
        return result;
    }
    result.dirty_rect_count = normalized_dirty_rects.size();
    for (const Rect dirty : normalized_dirty_rects) {
        result.dirty_area += rect_area(dirty);
    }
    dirty_rects = normalized_dirty_rects.data();
    dirty_rect_count = normalized_dirty_rects.size();

    std::vector<PendingLayer> pending;
    pending.push_back(PendingLayer{&root, Rect{}, false});
    while (!pending.empty()) {
        const PendingLayer current = pending.back();
        pending.pop_back();
        const LayerNode* layer = current.layer;
        Rect clip = current.clip;
        bool has_clip = current.has_clip;
        ++result.layers_visited;

        if (layer->has_clip) {
            clip = has_clip ? intersect_rect(clip, layer->clip_rect) : layer->clip_rect;
            has_clip = true;
            if (empty_rect(clip)) {
                continue;
            }
        }

        const Rect layer_bounds = has_clip ? intersect_rect(layer->bounds, clip) : layer->bounds;
        const bool layer_intersects = intersects_any(layer_bounds, dirty_rects, dirty_rect_count);
        if (layer_intersects) {
            ++result.layers_intersecting;
            if (layer->has_clip) {
                ++result.clipped_layers_intersecting;
            }
            if (is_composited_layer(*layer)) {
                ++result.composited_layers_intersecting;
            }
        }

        for (const DisplayCommand& command : layer->display_list) {
            ++result.commands_visited;
            const Rect command_rect = has_clip ? intersect_rect(command.rect, clip) : command.rect;
            if (intersects_any(command_rect, dirty_rects, dirty_rect_count)) {
                ++result.commands_intersecting;
            }
        }
        for (const auto& child : layer->children) {
            pending.push_back(PendingLayer{child.get(), clip, has_clip});
        }
    }
    return result;
}

} // namespace jellyframe
