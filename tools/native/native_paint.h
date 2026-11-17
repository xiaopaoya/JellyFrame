#pragma once

#include "render_core/software_renderer.h"
#include <cstdint>
#include <stdexcept>

namespace jellyframe::native {

inline void paint_into(SoftwareCompositor& compositor, const LayerNode& tree,
                       FrameBuffer& target, Color background,
                       const Rect* dirty = nullptr, std::size_t count = 0) {
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
    const auto result = compositor.render_into_checked(tree, target, background, dirty, count);
    if (!result.complete()) throw std::runtime_error("Core paint refused (status=" +
        std::to_string(static_cast<unsigned>(result.status)) + ")");
#else
    compositor.render_into(tree, target, background, dirty, count);
#endif
}

inline FrameBuffer paint_frame(SoftwareCompositor& compositor, const LayerNode& tree,
                               int width, int height, Color background,
                               std::size_t max_pixels) {
#if defined(JELLYFRAME_RENDER_CORE_CONTEXTUAL_TEXT_ENABLED)
    if (width <= 0 || height <= 0 || (max_pixels &&
        static_cast<std::uint64_t>(width) * static_cast<unsigned>(height) > max_pixels))
        throw std::runtime_error("framebuffer budget exceeded");
    FrameBuffer target(width, height, background);
    paint_into(compositor, tree, target, background);
    return target;
#else
    (void)max_pixels;
    return compositor.render(tree, width, height, background);
#endif
}

} // namespace jellyframe::native
