#pragma once

#include "render_core/animation_timeline.h"
#include "render_core/dirty_region.h"
#include "render_core/host.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace jellyframe {

struct FrameScratch {
    DirtyRegionResult dirty_region;
    DirtyRegionScratch dirty_region_scratch;
    std::vector<StyleOverride> style_overrides;

    void reserve_from_budgets(const HostBudgets& budgets) {
        const std::size_t max_dirty_rects = std::max<std::size_t>(1, budgets.max_dirty_rects);
        dirty_region.rects.reserve(max_dirty_rects);
        const std::size_t dirty_bounds_capacity =
            max_dirty_rects > std::numeric_limits<std::size_t>::max() / 2U
                ? max_dirty_rects
                : max_dirty_rects * 2U;
        dirty_region_scratch.node_bounds.reserve(std::max<std::size_t>(2, dirty_bounds_capacity));
        style_overrides.reserve(std::max<std::size_t>(1, budgets.max_active_animations));
    }

    void begin_frame() {
        clear_dirty_region();
        dirty_region_scratch.clear();
        style_overrides.clear();
    }

    void end_frame() {
        dirty_region_scratch.clear();
        style_overrides.clear();
    }

    void release() {
        std::vector<Rect>().swap(dirty_region.rects);
        dirty_region_scratch.release();
        std::vector<StyleOverride>().swap(style_overrides);
        dirty_region.mode = DirtyRegionMode::Clean;
        dirty_region.fallback_reason = DirtyRegionFallbackReason::None;
    }

private:
    void clear_dirty_region() {
        dirty_region.rects.clear();
        dirty_region.mode = DirtyRegionMode::Clean;
        dirty_region.fallback_reason = DirtyRegionFallbackReason::None;
    }
};

} // namespace jellyframe
