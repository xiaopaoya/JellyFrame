#pragma once

#include "render_core/software_renderer.h"
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace jellyframe::native {

template<class Compositor, class = void> struct HasCheckedPaint : std::false_type {};
template<class Compositor> struct HasCheckedPaint<Compositor, std::void_t<decltype(
    std::declval<const Compositor&>().render_into_checked(std::declval<const LayerNode&>(),
        std::declval<FrameBuffer&>(), Color{}, nullptr, std::size_t{}).complete())>> : std::true_type {};

template<class Compositor>
inline void paint_into(Compositor& compositor, const LayerNode& tree,
                       FrameBuffer& target, Color background,
                       const Rect* dirty = nullptr, std::size_t count = 0) {
    if constexpr (HasCheckedPaint<Compositor>::value) {
        const auto result = compositor.render_into_checked(tree, target, background, dirty, count);
        if (!result.complete()) throw std::runtime_error("Core paint refused (status=" +
            std::to_string(static_cast<unsigned>(result.status)) + ")");
    } else {
        compositor.render_into(tree, target, background, dirty, count);
    }
}

inline FrameBuffer paint_frame(SoftwareCompositor& compositor, const LayerNode& tree,
                               int width, int height, Color background,
                               std::size_t max_pixels) {
    if (width <= 0 || height <= 0 || (max_pixels &&
        static_cast<std::uint64_t>(width) * static_cast<unsigned>(height) > max_pixels))
        throw std::runtime_error("framebuffer budget exceeded");
    if constexpr (HasCheckedPaint<SoftwareCompositor>::value) {
        FrameBuffer target(width, height, background);
        paint_into(compositor, tree, target, background);
        return target;
    } else {
        return compositor.render(tree, width, height, background);
    }
}

} // namespace jellyframe::native
