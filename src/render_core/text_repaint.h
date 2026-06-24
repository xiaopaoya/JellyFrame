#pragma once

#include "render_core/dom.h"
#include "render_core/layout.h"
#include "render_core/text_backend.h"

namespace jellyframe {

bool text_dirty_can_reuse_layout(const Node& document,
                                 const LayoutBox& layout,
                                 const TextMeasureProvider& text_measure);

} // namespace jellyframe
