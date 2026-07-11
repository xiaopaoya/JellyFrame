#include "render_core/scroll_blit.h"

#include "render_core/software_renderer.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <limits>

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

bool rect_fits(Rect rect, int width, int height) {
    return rect.x >= 0 && rect.y >= 0 && rect.width >= 0 && rect.height >= 0 &&
        rect.x <= width && rect.y <= height && rect.width <= width - rect.x &&
        rect.height <= height - rect.y;
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

bool apply_vertical_scroll_blit(FrameBuffer& framebuffer,
                                Rect viewport,
                                const ScrollBlitPlan& plan) {
    if (plan.mode != ScrollBlitMode::FastBlit || viewport.width <= 0 || viewport.height <= 0 ||
        framebuffer.width <= 0 || framebuffer.height <= 0 || framebuffer.pixels.empty() ||
        plan.move_source.width != viewport.width || plan.move_destination.width != viewport.width ||
        plan.move_source.height != plan.move_destination.height || plan.move_source.height <= 0 ||
        plan.move_source.x != 0 || plan.move_destination.x != 0 ||
        !rect_fits(viewport, framebuffer.width, framebuffer.height) ||
        !rect_fits(plan.move_source, viewport.width, viewport.height) ||
        !rect_fits(plan.move_destination, viewport.width, viewport.height) ||
        static_cast<std::size_t>(framebuffer.width) >
            std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(framebuffer.height) ||
        framebuffer.pixels.size() < static_cast<std::size_t>(framebuffer.width) *
            static_cast<std::size_t>(framebuffer.height) ||
        static_cast<std::size_t>(viewport.width) >
            std::numeric_limits<std::size_t>::max() / sizeof(Color)) {
        return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(viewport.width) * sizeof(Color);
    const int source_y = viewport.y + plan.move_source.y;
    const int destination_y = viewport.y + plan.move_destination.y;
    const int rows = plan.move_source.height;
    const bool copy_top_down = destination_y < source_y;
    for (int index = 0; index < rows; ++index) {
        const int row = copy_top_down ? index : rows - 1 - index;
        Color* destination = framebuffer.pixels.data() +
            static_cast<std::size_t>(destination_y + row) * static_cast<std::size_t>(framebuffer.width) +
            static_cast<std::size_t>(viewport.x);
        const Color* source = framebuffer.pixels.data() +
            static_cast<std::size_t>(source_y + row) * static_cast<std::size_t>(framebuffer.width) +
            static_cast<std::size_t>(viewport.x);
        std::memmove(destination, source, row_bytes);
    }
    return true;
}

} // namespace jellyframe
