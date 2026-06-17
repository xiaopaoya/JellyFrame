#pragma once

#include "core/dom.h"
#include "core/geometry.h"
#include "core/layout.h"

#include <cstddef>
#include <vector>

namespace jellyframe {

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
};

struct DirtyRegionOptions {
    Rect viewport;
    std::size_t max_rects = 8;
    int expansion_px = 2;
};

struct DirtyRegionResult {
    std::vector<Rect> rects;
    DirtyRegionMode mode = DirtyRegionMode::Clean;
    DirtyRegionFallbackReason fallback_reason = DirtyRegionFallbackReason::None;
};

DirtyRegionResult compute_dirty_region(const Node& document,
                                       const LayoutBox* previous_layout,
                                       const LayoutBox* current_layout,
                                       const DirtyRegionOptions& options);

std::vector<Rect> compute_dirty_rects(const Node& document,
                                      const LayoutBox* previous_layout,
                                      const LayoutBox* current_layout,
                                      const DirtyRegionOptions& options);

} // namespace jellyframe
