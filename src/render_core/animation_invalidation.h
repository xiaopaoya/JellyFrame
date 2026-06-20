#pragma once

#include "render_core/animation_timeline.h"
#include "render_core/dirty_region.h"
#include "render_core/geometry.h"
#include "render_core/layout.h"

#include <cstddef>
#include <vector>

namespace jellyframe {

struct AnimationInvalidationOptions {
    Rect viewport;
    std::size_t max_rects = 8;
    int expansion_px = 2;
};

DirtyRegionResult compute_animation_dirty_region(const LayoutBox& layout,
                                                 const std::vector<StyleOverride>& previous_overrides,
                                                 const std::vector<StyleOverride>& current_overrides,
                                                 const AnimationInvalidationOptions& options);

void compute_animation_dirty_region_into(const LayoutBox& layout,
                                         const std::vector<StyleOverride>& previous_overrides,
                                         const std::vector<StyleOverride>& current_overrides,
                                         const AnimationInvalidationOptions& options,
                                         DirtyRegionResult& result);

} // namespace jellyframe
