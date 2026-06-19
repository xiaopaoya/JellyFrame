#pragma once

#include "render_core/geometry.h"
#include "render_core/layer_tree.h"

#include <cstddef>

namespace jellyframe {

struct DisplayInvalidationResult {
    std::size_t dirty_rect_count = 0;
    std::size_t dirty_area = 0;
    std::size_t layers_visited = 0;
    std::size_t layers_intersecting = 0;
    std::size_t clipped_layers_intersecting = 0;
    std::size_t composited_layers_intersecting = 0;
    std::size_t commands_visited = 0;
    std::size_t commands_intersecting = 0;
};

DisplayInvalidationResult analyze_display_invalidation(const LayerNode& root,
                                                       const Rect* dirty_rects,
                                                       std::size_t dirty_rect_count);

} // namespace jellyframe
