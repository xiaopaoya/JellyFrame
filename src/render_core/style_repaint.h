#pragma once

#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/text_backend.h"

namespace jellyframe {

bool style_dirty_can_reuse_layout(const Node& document,
                                  const RenderObject& previous_render_tree,
                                  const RenderObject& next_render_tree,
                                  const LayoutBox& current_layout,
                                  const TextMeasureProvider& text_measure);
bool apply_render_styles_to_layout(const RenderObject& render_tree, LayoutBox& layout_tree);

} // namespace jellyframe
