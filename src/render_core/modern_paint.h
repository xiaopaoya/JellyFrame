#pragma once

#include "render_core/feature_config.h"
#include "render_core/geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED

namespace jellyframe {

struct FrameBuffer;

bool modern_paint_fill_opaque_linear_gradient_fast(FrameBuffer& target,
                                                    Rect rect,
                                                    Rect clipped,
                                                    Color first,
                                                    Color second,
                                                    GradientAxis axis,
                                                    int border_radius);
void modern_paint_fill_linear_gradient(FrameBuffer& target,
                                       Rect rect,
                                       Rect clip,
                                       Color first,
                                       Color second,
                                       GradientAxis axis,
                                       int border_radius);
void modern_paint_fill_conic_gradient_region(FrameBuffer& target,
                                             Rect rect,
                                             Rect clipped,
                                             Color first,
                                             Color second,
                                             int stop_percent,
                                             int border_radius);
void modern_paint_fill_radial_gradient_region(FrameBuffer& target,
                                              Rect rect,
                                              Rect clipped,
                                              Color center_color,
                                              Color edge_color,
                                              GradientAxis axis,
                                              int packed_position,
                                              int border_radius);
void modern_paint_fill_soft_box_shadow(FrameBuffer& target,
                                       Rect rect,
                                       Rect clip,
                                       Color color,
                                       int outer_radius,
                                       int extent,
                                       int blur);

// Pure bounded math shared by the optional CSS modern-paint raster paths.
// This header deliberately contains no framebuffer, host, or allocation API.
inline std::uint8_t modern_paint_clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

inline Color modern_paint_lerp_color_255(Color first, Color second, int t) {
    t = std::max(0, std::min(255, t));
    return Color{
        modern_paint_clamp_u8((static_cast<int>(first.r) * (255 - t) + static_cast<int>(second.r) * t + 127) / 255),
        modern_paint_clamp_u8((static_cast<int>(first.g) * (255 - t) + static_cast<int>(second.g) * t + 127) / 255),
        modern_paint_clamp_u8((static_cast<int>(first.b) * (255 - t) + static_cast<int>(second.b) * t + 127) / 255),
        modern_paint_clamp_u8((static_cast<int>(first.a) * (255 - t) + static_cast<int>(second.a) * t + 127) / 255),
    };
}

inline int modern_paint_euclidean_distance_half_px(std::int64_t dx, std::int64_t dy) {
    const auto absolute_u64 = [](std::int64_t value) -> std::uint64_t {
        if (value >= 0) {
            return static_cast<std::uint64_t>(value);
        }
        // Avoid negating INT64_MIN while retaining its exact magnitude.
        return static_cast<std::uint64_t>(-(value + 1)) + 1U;
    };
    const std::uint64_t absolute_dx = absolute_u64(dx);
    const std::uint64_t absolute_dy = absolute_u64(dy);
    const std::uint64_t major = std::max(absolute_dx, absolute_dy);
    const std::uint64_t minor = std::min(absolute_dx, absolute_dy);
    if (minor > std::numeric_limits<std::uint64_t>::max() / 13U) {
        return std::numeric_limits<int>::max();
    }
    const std::uint64_t correction = (minor * 13U) >> 5U;
    if (major > std::numeric_limits<std::uint64_t>::max() - correction) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::min<std::uint64_t>(
        std::numeric_limits<int>::max(), major + correction));
}

inline int modern_paint_progress_255(int delta, int denominator) {
    if (denominator <= 0) {
        return delta >= 0 ? 255 : 0;
    }
    const std::int64_t scaled = static_cast<std::int64_t>(delta) * 255 / denominator;
    return static_cast<int>(std::clamp<std::int64_t>(scaled, 0, 255));
}

inline int modern_paint_conic_percent_from_top_clockwise(int dx, int dy) {
    const std::int64_t dx64 = dx;
    const std::int64_t dy64 = dy;
    if (dx64 == 0 && dy64 == 0) return 0;
    if (dy64 < 0) {
        const std::int64_t up = -dy64;
        if (dx64 >= 0) {
            return static_cast<int>(dx64 * 25 /
                                    std::max<std::int64_t>(1, dx64 + up));
        }
        const std::int64_t left = -dx64;
        return 75 + static_cast<int>(up * 25 /
                                     std::max<std::int64_t>(1, left + up));
    }
    if (dx64 >= 0) {
        return 25 + static_cast<int>(dy64 * 25 /
                                     std::max<std::int64_t>(1, dx64 + dy64));
    }
    const std::int64_t left = -dx64;
    return 50 + static_cast<int>(left * 25 /
                                 std::max<std::int64_t>(1, left + dy64));
}

inline int modern_paint_rounded_rect_outside_distance_half_px_resolved(Rect rect,
                                                                        int border_radius,
                                                                        int x,
                                                                        int y) {
    border_radius = std::max(0, std::min(border_radius, std::max(0, std::min(rect.width, rect.height) / 2)));
    const std::int64_t left2 = static_cast<std::int64_t>(rect.x) * 2;
    const std::int64_t top2 = static_cast<std::int64_t>(rect.y) * 2;
    const std::int64_t right2 = static_cast<std::int64_t>(safe_edge(rect.x, rect.width)) * 2;
    const std::int64_t bottom2 = static_cast<std::int64_t>(safe_edge(rect.y, rect.height)) * 2;
    const std::int64_t radius2 = static_cast<std::int64_t>(border_radius) * 2;
    const std::int64_t sample_x2 = static_cast<std::int64_t>(x) * 2 + 1;
    const std::int64_t sample_y2 = static_cast<std::int64_t>(y) * 2 + 1;
    const std::int64_t center_x2 = std::max(left2 + radius2, std::min(right2 - radius2, sample_x2));
    const std::int64_t center_y2 = std::max(top2 + radius2, std::min(bottom2 - radius2, sample_y2));
    const std::int64_t dx = sample_x2 - center_x2;
    const std::int64_t dy = sample_y2 - center_y2;
    const std::int64_t dimension_delta = static_cast<std::int64_t>(rect.width) - rect.height;
    const bool circular = std::abs(dimension_delta) <= 1 &&
        border_radius == std::min(rect.width, rect.height) / 2;
    if (circular) {
        const std::int64_t max_int = std::numeric_limits<int>::max();
        int distance = std::numeric_limits<int>::max();
        if (std::abs(dx) <= max_int && std::abs(dy) <= max_int) {
            const std::int64_t squared_distance = dx * dx + dy * dy;
            const std::int64_t rounded_distance = static_cast<std::int64_t>(
                std::sqrt(static_cast<double>(squared_distance)) + 0.5);
            distance = clamp_int64_to_int(rounded_distance);
        }
        return std::max(0, distance - border_radius * 2);
    }
    return std::max(0, modern_paint_euclidean_distance_half_px(dx, dy) - border_radius * 2);
}

inline int modern_paint_rounded_rect_outside_distance_half_px(Rect rect,
                                                               int encoded_radius,
                                                               int x,
                                                               int y) {
    const CornerRadii radii = decode_corner_radii(encoded_radius);
    const int radius = std::max(std::max(radii.top_left, radii.top_right),
                                std::max(radii.bottom_right, radii.bottom_left));
    return modern_paint_rounded_rect_outside_distance_half_px_resolved(rect, radius, x, y);
}

} // namespace jellyframe

#endif
