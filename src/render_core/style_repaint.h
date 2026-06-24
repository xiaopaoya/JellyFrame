#pragma once

#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/text_backend.h"

#include <vector>

namespace jellyframe {

bool style_dirty_can_reuse_layout(const Node& document,
                                  const RenderObject& previous_render_tree,
                                  const RenderObject& next_render_tree,
                                  const LayoutBox& current_layout,
                                  const TextMeasureProvider& text_measure);
void collect_style_repaint_overrides(const RenderObject& previous_render_tree,
                                     const RenderObject& next_render_tree,
                                     std::vector<StyleOverride>& previous_overrides,
                                     std::vector<StyleOverride>& current_overrides);
bool apply_render_styles_to_layout(const RenderObject& render_tree, LayoutBox& layout_tree);

} // namespace jellyframe
