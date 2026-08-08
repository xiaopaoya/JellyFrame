#pragma once

#include "render_core/feature_config.h"

#include <vector>

namespace jellyframe {

struct LayoutBox;

#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
std::vector<const LayoutBox*> ordered_flex_paint_children(const LayoutBox& box);
#endif

} // namespace jellyframe
