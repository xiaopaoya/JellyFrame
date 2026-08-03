#include "render_core/dirty_region.h"

#include "render_core/layer_tree.h"

#include <algorithm>

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

std::size_t rect_area(Rect rect) {
    if (empty_rect(rect)) {
        return 0;
    }
    const auto width = static_cast<unsigned long long>(rect.width);
    const auto height = static_cast<unsigned long long>(rect.height);
    const unsigned long long area = width * height;
    constexpr unsigned long long max_size = static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max());
    return area > max_size ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(area);
}

std::size_t saturating_add(std::size_t left, std::size_t right) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left + right;
}

std::size_t saturating_multiply(std::size_t left, std::size_t right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    if (left > std::numeric_limits<std::size_t>::max() / right) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left * right;
}

std::size_t percent_of_area(std::size_t area, int percent) {
    if (area == 0 || percent <= 0) {
        return 0;
    }
    const std::size_t safe_percent = static_cast<std::size_t>(percent);
    return saturating_add(saturating_multiply(area / 100U, safe_percent),
                          saturating_multiply(area % 100U, safe_percent) / 100U);
}

std::size_t area_for_percent(std::size_t area, int percent) {
    if (percent <= 0) {
        return 0;
    }
    if (percent >= 100 || area == 0) {
        return area;
    }
    const auto safe_percent = static_cast<std::size_t>(percent);
    return (area / 100U) * safe_percent + ((area % 100U) * safe_percent) / 100U;
}

std::size_t rect_cost(Rect rect, std::size_t per_rect_overhead_pixels) {
    return saturating_add(rect_area(rect), per_rect_overhead_pixels);
}

std::size_t rect_list_area(const std::vector<Rect>& rects) {
    std::size_t area = 0;
    for (Rect rect : rects) {
        area = saturating_add(area, rect_area(rect));
    }
    return area;
}

std::size_t rect_list_cost(const std::vector<Rect>& rects, std::size_t per_rect_overhead_pixels) {
    std::size_t cost = 0;
    for (Rect rect : rects) {
        cost = saturating_add(cost, rect_cost(rect, per_rect_overhead_pixels));
    }
    return cost;
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

void append_transient_layer_bounds(const LayerNode& root, std::vector<Rect>& output) {
    std::vector<const LayerNode*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const LayerNode* current = pending.back();
        pending.pop_back();
        if ((current->reasons & LayerReasonTransientOverlay) != 0U) {
            output.push_back(current->bounds);
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

void reset_result(DirtyRegionResult& result) {
    result.rects.clear();
    result.mode = DirtyRegionMode::Clean;
    result.fallback_reason = DirtyRegionFallbackReason::None;
}

void set_full_frame_result(DirtyRegionResult& result, Rect viewport, DirtyRegionFallbackReason reason) {
    result.mode = DirtyRegionMode::FullFrame;
    result.fallback_reason = reason;
    if (!empty_rect(viewport)) {
        result.rects.push_back(viewport);
    }
}

} // namespace

const char* dirty_region_mode_name(DirtyRegionMode mode) {
    switch (mode) {
    case DirtyRegionMode::Clean:
        return "clean";
    case DirtyRegionMode::DirtyRects:
        return "dirty-rects";
    case DirtyRegionMode::FullFrame:
        return "full-frame";
    }
    return "unknown";
}

const char* dirty_region_fallback_reason_name(DirtyRegionFallbackReason reason) {
    switch (reason) {
    case DirtyRegionFallbackReason::None:
        return "none";
    case DirtyRegionFallbackReason::InvalidViewport:
        return "invalid-viewport";
    case DirtyRegionFallbackReason::MissingLayout:
        return "missing-layout";
    case DirtyRegionFallbackReason::TreeDirty:
        return "tree-dirty";
    case DirtyRegionFallbackReason::NoDirtyBounds:
        return "no-dirty-bounds";
    case DirtyRegionFallbackReason::EmptyAfterClipping:
        return "empty-after-clipping";
    case DirtyRegionFallbackReason::DirtyAreaTooLarge:
        return "dirty-area-too-large";
    }
    return "unknown";
}

std::size_t dirty_region_fallback_reason_index(DirtyRegionFallbackReason reason) {
    switch (reason) {
    case DirtyRegionFallbackReason::None:
        return 0;
    case DirtyRegionFallbackReason::InvalidViewport:
        return 1;
    case DirtyRegionFallbackReason::MissingLayout:
        return 2;
    case DirtyRegionFallbackReason::TreeDirty:
        return 3;
    case DirtyRegionFallbackReason::NoDirtyBounds:
        return 4;
    case DirtyRegionFallbackReason::EmptyAfterClipping:
        return 5;
    case DirtyRegionFallbackReason::DirtyAreaTooLarge:
        return 6;
    }
    return 0;
}

void record_dirty_region_result(DirtyRegionStatistics& statistics, const DirtyRegionResult& result) {
    switch (result.mode) {
    case DirtyRegionMode::Clean:
        ++statistics.clean_frames;
        break;
    case DirtyRegionMode::DirtyRects:
        ++statistics.dirty_rect_frames;
        break;
    case DirtyRegionMode::FullFrame:
        ++statistics.full_frame_frames;
        break;
    }
    statistics.total_rects += result.rects.size();
    for (Rect rect : result.rects) {
        statistics.total_dirty_area = saturating_add(statistics.total_dirty_area, rect_area(rect));
    }
    ++statistics.fallback_reasons[dirty_region_fallback_reason_index(result.fallback_reason)];
}

std::size_t dirty_region_fallback_count(const DirtyRegionStatistics& statistics,
                                        DirtyRegionFallbackReason reason) {
    return statistics.fallback_reasons[dirty_region_fallback_reason_index(reason)];
}

std::size_t dirty_region_area(const DirtyRegionResult& result) {
    std::size_t total = 0;
    for (Rect rect : result.rects) {
        total = saturating_add(total, rect_area(rect));
    }
    return total;
}

std::size_t dirty_region_viewport_area(Rect viewport) {
    return rect_area(viewport);
}

int dirty_region_area_percent(const DirtyRegionResult& result, Rect viewport) {
    const std::size_t viewport_area = dirty_region_viewport_area(viewport);
    if (viewport_area == 0) {
        return 0;
    }
    const std::size_t dirty_area = dirty_region_area(result);
    if (dirty_area == 0) {
        return 0;
    }
    if (dirty_area >= viewport_area) {
        return 100;
    }
    if (dirty_area > std::numeric_limits<std::size_t>::max() / 100U) {
        for (int percent = 1; percent < 100; ++percent) {
            if (dirty_area <= area_for_percent(viewport_area, percent)) {
                return percent;
            }
        }
        return 100;
    }
    const std::size_t scaled_area = dirty_area * 100U;
    return static_cast<int>(1U + (scaled_area - 1U) / viewport_area);
}

bool dirty_region_should_repaint_incrementally(const DirtyRegionResult& result,
                                               Rect viewport,
                                               int max_area_percent) {
    if (result.mode != DirtyRegionMode::DirtyRects || result.rects.empty() || max_area_percent <= 0) {
        return false;
    }
    const std::size_t viewport_area = dirty_region_viewport_area(viewport);
    if (viewport_area == 0) {
        return false;
    }
    const std::size_t dirty_area = dirty_region_area(result);
    return dirty_area <= area_for_percent(viewport_area, max_area_percent);
}

void coalesce_dirty_rects_into(const Rect* input,
                               std::size_t input_count,
                               Rect viewport,
                               const DirtyRectCoalescingOptions& options,
                               std::vector<Rect>& output,
                               DirtyRectCoalescingResult* result) {
    output.clear();
    DirtyRectCoalescingResult local_result;
    if (input == nullptr || input_count == 0 || empty_rect(viewport)) {
        if (result != nullptr) {
            *result = local_result;
        }
        return;
    }

    for (std::size_t index = 0; index < input_count; ++index) {
        const Rect clipped = intersect_rect(input[index], viewport);
        if (empty_rect(clipped)) {
            continue;
        }
        output.push_back(clipped);
        ++local_result.input_rect_count;
        local_result.input_area = saturating_add(local_result.input_area, rect_area(clipped));
    }
    local_result.estimated_cost_before = rect_list_cost(output, options.per_rect_overhead_pixels);

    const std::size_t max_rects = std::max<std::size_t>(1, options.max_rects);
    const int max_extra_area_percent = std::max(0, options.max_extra_area_percent);
    while (output.size() > 1) {
        const bool forced = output.size() > max_rects;
        std::size_t best_left = output.size();
        std::size_t best_right = output.size();
        std::size_t best_extra_area = std::numeric_limits<std::size_t>::max();
        std::size_t best_merged_cost = std::numeric_limits<std::size_t>::max();
        std::size_t best_savings = 0;

        for (std::size_t left = 0; left + 1 < output.size(); ++left) {
            for (std::size_t right = left + 1; right < output.size(); ++right) {
                const Rect merged = union_rect(output[left], output[right]);
                const std::size_t pair_area = saturating_add(rect_area(output[left]), rect_area(output[right]));
                const std::size_t merged_area = rect_area(merged);
                const std::size_t extra_area = merged_area > pair_area ? merged_area - pair_area : 0;
                const std::size_t pair_cost = saturating_add(
                    rect_cost(output[left], options.per_rect_overhead_pixels),
                    rect_cost(output[right], options.per_rect_overhead_pixels));
                const std::size_t merged_cost = rect_cost(merged, options.per_rect_overhead_pixels);
                const bool profitable =
                    extra_area <= percent_of_area(pair_area, max_extra_area_percent) && merged_cost < pair_cost;
                if (!profitable && !forced) {
                    continue;
                }

                const std::size_t savings = pair_cost > merged_cost ? pair_cost - merged_cost : 0;
                const bool better = best_left == output.size() ||
                                    (forced
                                         ? (extra_area < best_extra_area ||
                                            (extra_area == best_extra_area && merged_cost < best_merged_cost))
                                         : (savings > best_savings ||
                                            (savings == best_savings && extra_area < best_extra_area)));
                if (better) {
                    best_left = left;
                    best_right = right;
                    best_extra_area = extra_area;
                    best_merged_cost = merged_cost;
                    best_savings = savings;
                }
            }
        }
        if (best_left == output.size()) {
            break;
        }

        output[best_left] = union_rect(output[best_left], output[best_right]);
        output.erase(output.begin() + static_cast<std::ptrdiff_t>(best_right));
        if (forced) {
            ++local_result.forced_merges;
        }
    }

    local_result.output_rect_count = output.size();
    local_result.output_area = rect_list_area(output);
    local_result.estimated_cost_after = rect_list_cost(output, options.per_rect_overhead_pixels);
    if (result != nullptr) {
        *result = local_result;
    }
}

DirtyRegionResult compute_dirty_region(const Node& document,
                                       const LayoutBox* previous_layout,
                                       const LayoutBox* current_layout,
                                       const DirtyRegionOptions& options) {
    DirtyRegionResult result;
    DirtyRegionScratch scratch;
    compute_dirty_region_into(document, previous_layout, current_layout, options, result, &scratch);
    return result;
}

void compute_dirty_region_into(const Node& document,
                               const LayoutBox* previous_layout,
                               const LayoutBox* current_layout,
                               const DirtyRegionOptions& options,
                               DirtyRegionResult& result,
                               DirtyRegionScratch* scratch) {
    reset_result(result);
    if (document.dirty_flags == DomDirtyNone) {
        return;
    }
    if (empty_rect(options.viewport)) {
        set_full_frame_result(result, options.viewport, DirtyRegionFallbackReason::InvalidViewport);
        return;
    }
    if (has_local_tree_dirty(document)) {
        set_full_frame_result(result, options.viewport, DirtyRegionFallbackReason::TreeDirty);
        return;
    }
    if (previous_layout == nullptr || current_layout == nullptr) {
        set_full_frame_result(result, options.viewport, DirtyRegionFallbackReason::MissingLayout);
        return;
    }

    DirtyRegionScratch local_scratch;
    DirtyRegionScratch& active_scratch = scratch == nullptr ? local_scratch : *scratch;
    active_scratch.clear();
    std::vector<DirtyNodeBounds>& dirty_bounds = active_scratch.node_bounds;
    append_dirty_bounds_from_layout(*previous_layout, dirty_bounds);
    append_dirty_bounds_from_layout(*current_layout, dirty_bounds);
    if (options.previous_layer_tree != nullptr) {
        append_transient_layer_bounds(*options.previous_layer_tree, active_scratch.transient_bounds);
    }
    if (options.current_layer_tree != nullptr) {
        append_transient_layer_bounds(*options.current_layer_tree, active_scratch.transient_bounds);
    }
    if (dirty_bounds.empty() && active_scratch.transient_bounds.empty()) {
        set_full_frame_result(result, options.viewport, DirtyRegionFallbackReason::NoDirtyBounds);
        return;
    }

    const std::size_t max_rects = std::max<std::size_t>(1, options.max_rects);
    for (const DirtyNodeBounds& bounds : dirty_bounds) {
        append_coalesced(result.rects, expand_rect(bounds.bounds, options.expansion_px), options.viewport, max_rects);
    }
    for (Rect bounds : active_scratch.transient_bounds) {
        append_coalesced(result.rects, expand_rect(bounds, options.expansion_px), options.viewport, max_rects);
    }
    if (result.rects.empty()) {
        set_full_frame_result(result, options.viewport, DirtyRegionFallbackReason::EmptyAfterClipping);
        return;
    }

    result.mode = DirtyRegionMode::DirtyRects;
}

std::vector<Rect> compute_dirty_rects(const Node& document,
                                      const LayoutBox* previous_layout,
                                      const LayoutBox* current_layout,
                                      const DirtyRegionOptions& options) {
    return compute_dirty_region(document, previous_layout, current_layout, options).rects;
}

} // namespace jellyframe
