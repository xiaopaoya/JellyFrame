#pragma once

#include "core/dom.h"
#include "core/geometry.h"
#include "core/layout.h"

#include <cstddef>
#include <vector>

namespace jellyframe {

struct DirtyRegionOptions {
    Rect viewport;
    std::size_t max_rects = 8;
    int expansion_px = 2;
};

std::vector<Rect> compute_dirty_rects(const Node& document,
                                      const LayoutBox* previous_layout,
                                      const LayoutBox* current_layout,
                                      const DirtyRegionOptions& options);

} // namespace jellyframe
