#pragma once

#include "render_core/software_renderer.h"

#include <algorithm>
#include <cstdint>

namespace jellyframe {

inline bool raster_empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

inline Rect raster_intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::min(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

inline Rect raster_clip_rect(const FrameBuffer& target, Rect rect, Rect clip) {
    return raster_intersect_rect(raster_intersect_rect(rect, Rect{0, 0, target.width, target.height}), clip);
}

inline std::uint8_t raster_clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

inline Color with_coverage(Color color, int coverage) {
    if (coverage >= 255) {
        return color;
    }
    if (coverage <= 0) {
        color.a = 0;
        return color;
    }
    color.a = raster_clamp_u8((static_cast<int>(color.a) * coverage + 127) / 255);
    return color;
}

inline void blend_color(Color& destination, Color source) {
    if (source.a == 0) {
        return;
    }
    if (source.a == 255) {
        destination = source;
        return;
    }

    const int src_a = source.a;
    const int dst_a = destination.a;
    const int inv_src_a = 255 - src_a;
    const int out_a = src_a + ((dst_a * inv_src_a + 127) / 255);
    if (out_a == 0) {
        destination = Color{0, 0, 0, 0};
        return;
    }

    const auto blend_channel = [&](std::uint8_t src, std::uint8_t dst) {
        const int premul = src * src_a + ((dst * dst_a * inv_src_a + 127) / 255);
        return raster_clamp_u8((premul + out_a / 2) / out_a);
    };

    destination = Color{
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        raster_clamp_u8(out_a),
    };
}

inline void blend_pixel(FrameBuffer& target, int x, int y, Color source) {
    if (!target.contains(x, y)) {
        return;
    }
    blend_color(target.pixel(x, y), source);
}

// Decode and clamp once per paint command. Rounded coverage is queried per
// pixel by several raster paths, so repeating this work there is avoidable.
struct RasterRoundedRect {
    CornerRadii radii{};
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    bool rounded = false;
};

inline RasterRoundedRect prepare_rounded_rect(Rect rect, int encoded_radius) {
    CornerRadii radii = decode_corner_radii(encoded_radius);
    const int max_radius = std::max(0, std::min(rect.width, rect.height) / 2);
    radii.top_left = std::min(radii.top_left, max_radius);
    radii.top_right = std::min(radii.top_right, max_radius);
    radii.bottom_right = std::min(radii.bottom_right, max_radius);
    radii.bottom_left = std::min(radii.bottom_left, max_radius);
    return RasterRoundedRect{
        radii,
        rect.x,
        rect.y,
        safe_edge(rect.x, rect.width),
        safe_edge(rect.y, rect.height),
        radii.top_left > 0 || radii.top_right > 0 || radii.bottom_right > 0 || radii.bottom_left > 0,
    };
}

struct RoundedRectCoverage {
    int value = 0;
    // True only when the 4x4 subpixel circle test was evaluated. Callers can
    // profile rounded-clip work without treating an interior 255 result as a
    // free rectangular result.
    bool sampled = false;
};

inline RoundedRectCoverage rounded_rect_coverage_detail(const RasterRoundedRect& geometry, int x, int y) {
    if (!geometry.rounded) {
        return {255, false};
    }
    const CornerRadii& radii = geometry.radii;
    if (radii.top_left <= 0 && radii.top_right <= 0 && radii.bottom_right <= 0 && radii.bottom_left <= 0) {
        return {255, false};
    }

    const std::int64_t left = geometry.left;
    const std::int64_t top = geometry.top;
    const std::int64_t right = geometry.right;
    const std::int64_t bottom = geometry.bottom;
    if (static_cast<std::int64_t>(x) < left || static_cast<std::int64_t>(x) >= right ||
        static_cast<std::int64_t>(y) < top || static_cast<std::int64_t>(y) >= bottom) {
        return {0, false};
    }
    std::int64_t cx = 0;
    std::int64_t cy = 0;
    int radius = 0;
    if (static_cast<std::int64_t>(x) < left + radii.top_left &&
        static_cast<std::int64_t>(y) < top + radii.top_left) {
        radius = radii.top_left;
        cx = left + radius;
        cy = top + radius;
    } else if (static_cast<std::int64_t>(x) >= right - radii.top_right &&
               static_cast<std::int64_t>(y) < top + radii.top_right) {
        radius = radii.top_right;
        cx = right - radius;
        cy = top + radius;
    } else if (static_cast<std::int64_t>(x) < left + radii.bottom_left &&
               static_cast<std::int64_t>(y) >= bottom - radii.bottom_left) {
        radius = radii.bottom_left;
        cx = left + radius;
        cy = bottom - radius;
    } else if (static_cast<std::int64_t>(x) >= right - radii.bottom_right &&
               static_cast<std::int64_t>(y) >= bottom - radii.bottom_right) {
        radius = radii.bottom_right;
        cx = right - radius;
        cy = bottom - radius;
    } else {
        return {255, false};
    }

    constexpr int kSubpixel = 4;
    const std::int64_t center_x = cx * kSubpixel;
    const std::int64_t center_y = cy * kSubpixel;
    const auto saturated_multiply = [](std::uint64_t left, std::uint64_t right) {
        if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return left * right;
    };
    const std::uint64_t radius_scaled = saturated_multiply(static_cast<std::uint64_t>(radius), kSubpixel);
    const std::uint64_t radius_squared = saturated_multiply(radius_scaled, radius_scaled);
    const auto saturated_square = [saturated_multiply](std::int64_t value) {
        const std::uint64_t magnitude = value < 0
            ? static_cast<std::uint64_t>(-(value + 1)) + 1U
            : static_cast<std::uint64_t>(value);
        return saturated_multiply(magnitude, magnitude);
    };
    const auto saturated_add = [](std::uint64_t left, std::uint64_t right) {
        if (std::numeric_limits<std::uint64_t>::max() - left < right) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return left + right;
    };
    int covered = 0;
    for (int sy = 0; sy < kSubpixel; ++sy) {
        const std::int64_t sample_y = static_cast<std::int64_t>(y) * kSubpixel + sy;
        const std::int64_t dy = sample_y - center_y;
        for (int sx = 0; sx < kSubpixel; ++sx) {
            const std::int64_t sample_x = static_cast<std::int64_t>(x) * kSubpixel + sx;
            const std::int64_t dx = sample_x - center_x;
            if (saturated_add(saturated_square(dx), saturated_square(dy)) <= radius_squared) {
                ++covered;
            }
        }
    }
    return {(covered * 255 + 8) / 16, true};
}

inline int rounded_rect_coverage(const RasterRoundedRect& geometry, int x, int y) {
    return rounded_rect_coverage_detail(geometry, x, y).value;
}

inline int rounded_rect_coverage(Rect rect, int encoded_radius, int x, int y) {
    return rounded_rect_coverage(prepare_rounded_rect(rect, encoded_radius), x, y);
}

} // namespace jellyframe
