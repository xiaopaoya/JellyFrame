#pragma once

#include "render_core/dom.h"
#include "render_core/layout.h"
#include "render_core/text_backend.h"

namespace jellyframe {

bool dirty_text_nodes_have_stable_layout(const Node& document,
                                         const LayoutBox& layout,
                                         const TextMeasureProvider& text_measure,
                                         bool require_text_dirty);

} // namespace jellyframe
