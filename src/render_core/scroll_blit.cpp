#include "render_core/scroll_blit.h"

#include <algorithm>
#include <cstdlib>

namespace jellyframe {
namespace {

std::size_t rect_pixels(Rect rect) {
    if (rect.width <= 0 || rect.height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height);
}

int clamp_scroll_y(int value, int viewport_height, int content_height) {
    return std::max(0, std::min(value, std::max(0, content_height - viewport_height)));
}

} // namespace

const char* scroll_blit_mode_name(ScrollBlitMode mode) {
    switch (mode) {
    case ScrollBlitMode::None:
        return "none";
    case ScrollBlitMode::FastBlit:
        return "fast-blit";
    case ScrollBlitMode::FullRepaint:
        return "full-repaint";
    }
    return "unknown";
}

ScrollBlitPlan plan_vertical_scroll_blit(int viewport_width,
                                         int viewport_height,
                                         int content_height,
                                         int previous_scroll_y,
                                         int requested_scroll_y) {
    ScrollBlitPlan plan;
    if (viewport_width <= 0 || viewport_height <= 0 || content_height <= 0) {
        plan.mode = ScrollBlitMode::FullRepaint;
        return plan;
    }

    plan.previous_scroll_y = clamp_scroll_y(previous_scroll_y, viewport_height, content_height);
    plan.current_scroll_y = clamp_scroll_y(requested_scroll_y, viewport_height, content_height);
    plan.delta_y = plan.current_scroll_y - plan.previous_scroll_y;
    if (plan.delta_y == 0) {
        return plan;
    }

    const int abs_delta = std::abs(plan.delta_y);
    if (abs_delta >= viewport_height) {
        plan.mode = ScrollBlitMode::FullRepaint;
        plan.exposed_strip = Rect{0, 0, viewport_width, viewport_height};
        plan.exposed_pixels = rect_pixels(plan.exposed_strip);
        return plan;
    }

    const int moved_height = viewport_height - abs_delta;
    plan.mode = ScrollBlitMode::FastBlit;
    if (plan.delta_y > 0) {
        plan.move_source = Rect{0, abs_delta, viewport_width, moved_height};
        plan.move_destination = Rect{0, 0, viewport_width, moved_height};
        plan.exposed_strip = Rect{0, moved_height, viewport_width, abs_delta};
    } else {
        plan.move_source = Rect{0, 0, viewport_width, moved_height};
        plan.move_destination = Rect{0, abs_delta, viewport_width, moved_height};
        plan.exposed_strip = Rect{0, 0, viewport_width, abs_delta};
    }
    plan.moved_pixels = rect_pixels(plan.move_destination);
    plan.exposed_pixels = rect_pixels(plan.exposed_strip);
    return plan;
}

} // namespace jellyframe
