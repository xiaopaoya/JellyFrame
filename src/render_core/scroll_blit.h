#pragma once

#include "render_core/geometry.h"

#include <cstddef>

namespace jellyframe {

struct FrameBuffer;

enum class ScrollBlitMode {
    None,
    FastBlit,
    FullRepaint,
};

struct ScrollBlitPlan {
    ScrollBlitMode mode = ScrollBlitMode::None;
    Rect move_source;
    Rect move_destination;
    Rect exposed_strip;
    int previous_scroll_y = 0;
    int current_scroll_y = 0;
    int delta_y = 0;
    std::size_t moved_pixels = 0;
    std::size_t exposed_pixels = 0;
};

const char* scroll_blit_mode_name(ScrollBlitMode mode);

ScrollBlitPlan plan_vertical_scroll_blit(int viewport_width,
                                         int viewport_height,
                                         int content_height,
                                         int previous_scroll_y,
                                         int requested_scroll_y);

// Moves the reusable rows for a FastBlit plan inside a framebuffer viewport.
// The caller must repaint plan.exposed_strip afterwards.
bool apply_vertical_scroll_blit(FrameBuffer& framebuffer,
                                Rect viewport,
                                const ScrollBlitPlan& plan);

} // namespace jellyframe
