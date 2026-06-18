#pragma once

#include "core/css_parser.h"
#include "core/dirty_region.h"
#include "core/frame_loop.h"
#include "core/host.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"

#include <algorithm>
#include <cstddef>

namespace jellyframe {

inline HtmlParserOptions html_parser_options_from_budgets(const HostBudgets& budgets) {
    HtmlParserOptions options;
    options.max_nodes = std::max<std::size_t>(1, budgets.max_dom_nodes);
    options.max_depth = std::max<std::size_t>(1, budgets.max_dom_depth);
    options.max_attributes_per_element = std::max<std::size_t>(1, budgets.max_attributes_per_element);
    return options;
}

inline CssParserOptions css_parser_options_from_budgets(const HostBudgets& budgets) {
    CssParserOptions options;
    options.max_rules = std::max<std::size_t>(1, budgets.max_css_rules);
    options.max_declarations_per_rule = std::max<std::size_t>(1, budgets.max_css_declarations_per_rule);
    return options;
}

inline RenderTreeOptions render_tree_options_from_budgets(const HostBudgets& budgets) {
    return RenderTreeOptions{std::max<std::size_t>(1, budgets.max_render_objects)};
}

inline LayoutEngineOptions layout_engine_options_from_budgets(const HostBudgets& budgets) {
    return LayoutEngineOptions{std::max<std::size_t>(1, budgets.max_layout_boxes)};
}

inline LayerTreeBuilderOptions layer_tree_options_from_budgets(const HostBudgets& budgets) {
    return LayerTreeBuilderOptions{
        std::max<std::size_t>(1, budgets.max_layers),
        std::max<std::size_t>(1, budgets.max_display_commands),
    };
}

inline DirtyRegionOptions dirty_region_options_from_budgets(const HostBudgets& budgets,
                                                            Rect viewport,
                                                            int expansion_px = 2) {
    return DirtyRegionOptions{viewport, std::max<std::size_t>(1, budgets.max_dirty_rects), expansion_px};
}

inline FrameLoopOptions frame_loop_options_from_budgets(const HostBudgets& budgets) {
    return FrameLoopOptions{
        budgets.max_input_events_per_frame,
        budgets.max_timer_callbacks_per_frame,
    };
}

inline bool framebuffer_size_fits_budget(int width, int height, const HostBudgets& budgets) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return pixels <= std::max<std::size_t>(1, budgets.max_framebuffer_pixels);
}

inline SoftwareCompositor::Options software_compositor_options_from_budgets(const HostBudgets& budgets) {
    const std::size_t pixels = std::max<std::size_t>(1, budgets.max_framebuffer_pixels);
    return SoftwareCompositor::Options{pixels, pixels};
}

} // namespace jellyframe
