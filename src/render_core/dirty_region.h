#pragma once

#include "render_core/dom.h"
#include "render_core/geometry.h"
#include "render_core/layout.h"

#include <array>
#include <cstddef>
#include <vector>

namespace jellyframe {

struct LayerNode;

enum class DirtyRegionMode {
    Clean,
    DirtyRects,
    FullFrame,
};

enum class DirtyRegionFallbackReason {
    None,
    InvalidViewport,
    MissingLayout,
    TreeDirty,
    NoDirtyBounds,
    EmptyAfterClipping,
    DirtyAreaTooLarge,
};

constexpr std::size_t kDirtyRegionFallbackReasonCount =
    static_cast<std::size_t>(DirtyRegionFallbackReason::DirtyAreaTooLarge) + 1;

const char* dirty_region_mode_name(DirtyRegionMode mode);
const char* dirty_region_fallback_reason_name(DirtyRegionFallbackReason reason);
std::size_t dirty_region_fallback_reason_index(DirtyRegionFallbackReason reason);

struct DirtyRegionOptions {
    Rect viewport;
    std::size_t max_rects = 8;
    int expansion_px = 2;
    // Optional layer snapshots for transient overlays such as a core-rendered
    // select popup. The caller must keep both trees alive until this pass ends.
    const LayerNode* previous_layer_tree = nullptr;
    const LayerNode* current_layer_tree = nullptr;
};

struct DirtyRegionResult {
    std::vector<Rect> rects;
    DirtyRegionMode mode = DirtyRegionMode::Clean;
    DirtyRegionFallbackReason fallback_reason = DirtyRegionFallbackReason::None;
};

// Optional host-side present planning. The cost unit is an equivalent pixel:
// ports may model a flush setup cost without exposing display-specific types.
struct DirtyRectCoalescingOptions {
    std::size_t max_rects = 8;
    std::size_t per_rect_overhead_pixels = 0;
    int max_extra_area_percent = 100;
};

struct DirtyRectCoalescingResult {
    std::size_t input_rect_count = 0;
    std::size_t output_rect_count = 0;
    std::size_t input_area = 0;
    std::size_t output_area = 0;
    std::size_t estimated_cost_before = 0;
    std::size_t estimated_cost_after = 0;
    std::size_t forced_merges = 0;
};

struct DirtyNodeBounds {
    const Node* node = nullptr;
    Rect bounds;
};

struct DirtyRegionScratch {
    std::vector<DirtyNodeBounds> node_bounds;
    std::vector<Rect> transient_bounds;

    void clear() {
        node_bounds.clear();
        transient_bounds.clear();
    }

    void release() {
        std::vector<DirtyNodeBounds>().swap(node_bounds);
        std::vector<Rect>().swap(transient_bounds);
    }
};

struct DirtyRegionStatistics {
    std::size_t clean_frames = 0;
    std::size_t dirty_rect_frames = 0;
    std::size_t full_frame_frames = 0;
    std::size_t total_rects = 0;
    std::size_t total_dirty_area = 0;
    std::array<std::size_t, kDirtyRegionFallbackReasonCount> fallback_reasons{};
};

void record_dirty_region_result(DirtyRegionStatistics& statistics, const DirtyRegionResult& result);
std::size_t dirty_region_fallback_count(const DirtyRegionStatistics& statistics,
                                        DirtyRegionFallbackReason reason);
std::size_t dirty_region_area(const DirtyRegionResult& result);
std::size_t dirty_region_viewport_area(Rect viewport);
int dirty_region_area_percent(const DirtyRegionResult& result, Rect viewport);
bool dirty_region_should_repaint_incrementally(const DirtyRegionResult& result,
                                               Rect viewport,
                                               int max_area_percent);

// Clips input to viewport and optionally merges rectangles when doing so lowers
// the caller-defined present cost. This utility never changes frame planning;
// ports opt in and retain ownership of the resulting presentation policy.
void coalesce_dirty_rects_into(const Rect* input,
                               std::size_t input_count,
                               Rect viewport,
                               const DirtyRectCoalescingOptions& options,
                               std::vector<Rect>& output,
                               DirtyRectCoalescingResult* result = nullptr);

DirtyRegionResult compute_dirty_region(const Node& document,
                                       const LayoutBox* previous_layout,
                                       const LayoutBox* current_layout,
                                       const DirtyRegionOptions& options);

void compute_dirty_region_into(const Node& document,
                               const LayoutBox* previous_layout,
                               const LayoutBox* current_layout,
                               const DirtyRegionOptions& options,
                               DirtyRegionResult& result,
                               DirtyRegionScratch* scratch = nullptr);

std::vector<Rect> compute_dirty_rects(const Node& document,
                                      const LayoutBox* previous_layout,
                                      const LayoutBox* current_layout,
                                      const DirtyRegionOptions& options);

} // namespace jellyframe
