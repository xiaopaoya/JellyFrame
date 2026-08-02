#include "render_core/modern_paint.h"

#include "render_core/software_renderer.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace jellyframe {

bool modern_paint_fill_opaque_linear_gradient_fast(FrameBuffer& target,
                                                    Rect rect,
                                                    Rect clipped,
                                                    Color first,
                                                    Color second,
                                                    GradientAxis axis,
                                                    int border_radius) {
    if (clipped.width <= 0 || clipped.height <= 0 || first.a != 255 || second.a != 255 ||
        has_corner_radius(border_radius)) {
        return false;
    }

    if (axis == GradientAxis::Vertical) {
        const int denom = std::max(1, safe_add(rect.height, -1));
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            const Color row = modern_paint_lerp_color_255(
                first, second, modern_paint_progress_255(y - rect.y, denom));
            Color* destination = target.pixels.data() + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(target.width) + static_cast<std::size_t>(clipped.x);
            std::fill_n(destination, clipped.width, row);
        }
        return true;
    }

    if (axis == GradientAxis::Horizontal) {
        const int denom = std::max(1, safe_add(rect.width, -1));
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            Color* destination = target.pixels.data() + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(target.width) + static_cast<std::size_t>(clipped.x);
            for (int x = 0; x < clipped.width; ++x) {
                destination[x] = modern_paint_lerp_color_255(
                    first, second, modern_paint_progress_255(clipped.x + x - rect.x, denom));
            }
        }
        return true;
    }

    const int width_denom = std::max(1, safe_add(rect.width, -1));
    const int height_denom = std::max(1, safe_add(rect.height, -1));
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        Color* destination = target.pixels.data() + static_cast<std::size_t>(y) *
            static_cast<std::size_t>(target.width) + static_cast<std::size_t>(clipped.x);
        const int vertical = modern_paint_progress_255(y - rect.y, height_denom);
        for (int x = 0; x < clipped.width; ++x) {
            const int horizontal = modern_paint_progress_255(clipped.x + x - rect.x, width_denom);
            const int progress = axis == GradientAxis::DiagonalDownLeft
                ? ((255 - horizontal) + vertical) / 2
                : (horizontal + vertical) / 2;
            destination[x] = modern_paint_lerp_color_255(first, second, progress);
        }
    }
    return true;
}

void modern_paint_fill_linear_gradient(FrameBuffer& target,
                                       Rect rect,
                                       Rect clip,
                                       Color first,
                                       Color second,
                                       GradientAxis axis,
                                       int border_radius) {
    const Rect clipped = raster_clip_rect(target, rect, clip);
    if (raster_empty_rect(clipped)) {
        return;
    }
    if (modern_paint_fill_opaque_linear_gradient_fast(target, rect, clipped, first, second, axis, border_radius)) {
        return;
    }
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    if (axis == GradientAxis::Vertical) {
        const int denom = std::max(1, safe_add(rect.height, -1));
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            const Color row = modern_paint_lerp_color_255(
                first, second, modern_paint_progress_255(y - rect.y, denom));
            for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
                const int coverage = rounded_rect_coverage(rounded, x, y);
                if (coverage > 0) {
                    blend_pixel(target, x, y, with_coverage(row, coverage));
                }
            }
        }
        return;
    }
    if (axis == GradientAxis::Horizontal) {
        const int denom = std::max(1, safe_add(rect.width, -1));
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
                const int coverage = rounded_rect_coverage(rounded, x, y);
                if (coverage > 0) {
                    const Color color = modern_paint_lerp_color_255(
                        first, second, modern_paint_progress_255(x - rect.x, denom));
                    blend_pixel(target, x, y, with_coverage(color, coverage));
                }
            }
        }
        return;
    }

    const int width_denom = std::max(1, safe_add(rect.width, -1));
    const int height_denom = std::max(1, safe_add(rect.height, -1));
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rounded, x, y);
            if (coverage <= 0) {
                continue;
            }
            const int horizontal = modern_paint_progress_255(x - rect.x, width_denom);
            const int vertical = modern_paint_progress_255(y - rect.y, height_denom);
            const int progress = axis == GradientAxis::DiagonalDownLeft
                ? ((255 - horizontal) + vertical) / 2
                : (horizontal + vertical) / 2;
            blend_pixel(target, x, y,
                        with_coverage(modern_paint_lerp_color_255(first, second, progress), coverage));
        }
    }
}

void modern_paint_fill_conic_gradient_region(FrameBuffer& target,
                                             Rect rect,
                                             Rect clipped,
                                             Color first,
                                             Color second,
                                             int stop_percent,
                                             int border_radius) {
    if (raster_empty_rect(clipped)) {
        return;
    }
    stop_percent = std::max(0, std::min(100, stop_percent));
    const std::int64_t center_x2 = static_cast<std::int64_t>(rect.x) * 2 + rect.width;
    const std::int64_t center_y2 = static_cast<std::int64_t>(rect.y) * 2 + rect.height;
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rounded, x, y);
            if (coverage <= 0) {
                continue;
            }
            const int dx = static_cast<int>(static_cast<std::int64_t>(x) * 2 + 1 - center_x2);
            const int dy = static_cast<int>(static_cast<std::int64_t>(y) * 2 + 1 - center_y2);
            const Color color = modern_paint_conic_percent_from_top_clockwise(dx, dy) < stop_percent
                ? first : second;
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

void modern_paint_fill_radial_gradient_region(FrameBuffer& target,
                                              Rect rect,
                                              Rect clipped,
                                              Color center_color,
                                              Color edge_color,
                                              GradientAxis axis,
                                              int packed_position,
                                              int border_radius) {
    if (raster_empty_rect(clipped)) {
        return;
    }
    std::int64_t center_x2 = static_cast<std::int64_t>(rect.x) * 2 + rect.width;
    std::int64_t center_y2 = static_cast<std::int64_t>(rect.y) * 2 + rect.height;
    if (axis == GradientAxis::RadialPosition) {
        const int x_percent = std::max(0, std::min(100, packed_position / 101));
        const int y_percent = std::max(0, std::min(100, packed_position % 101));
        center_x2 = static_cast<std::int64_t>(rect.x) * 2 +
            (static_cast<std::int64_t>(rect.width) * 2 * x_percent + 50) / 100;
        center_y2 = static_cast<std::int64_t>(rect.y) * 2 +
            (static_cast<std::int64_t>(rect.height) * 2 * y_percent + 50) / 100;
    }
    const int radius2 = std::max(1, std::max(rect.width, rect.height));
    const int gradient_scale = (255 << 16) / radius2;
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rounded, x, y);
            if (coverage <= 0) {
                continue;
            }
            const std::int64_t dx2 = static_cast<std::int64_t>(x) * 2 + 1 - center_x2;
            const std::int64_t dy2 = static_cast<std::int64_t>(y) * 2 + 1 - center_y2;
            const int distance = modern_paint_euclidean_distance_half_px(dx2, dy2);
            const int t = static_cast<int>(std::clamp<std::int64_t>(
                (static_cast<std::int64_t>(distance) * gradient_scale) >> 16, 0, 255));
            blend_pixel(target, x, y,
                        with_coverage(modern_paint_lerp_color_255(center_color, edge_color, t), coverage));
        }
    }
}

void modern_paint_fill_soft_box_shadow(FrameBuffer& target,
                                       Rect rect,
                                       Rect clip,
                                       Color color,
                                       int outer_radius,
                                       int extent,
                                       int blur) {
    const Rect clipped = raster_clip_rect(target, rect, clip);
    if (raster_empty_rect(clipped) || color.a == 0 || extent <= 0) {
        return;
    }
    const std::int64_t twice_extent64 = static_cast<std::int64_t>(extent) * 2;
    const int twice_extent = static_cast<int>(std::clamp(
        twice_extent64,
        static_cast<std::int64_t>(std::numeric_limits<int>::min()),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    const std::int64_t core_x64 = static_cast<std::int64_t>(rect.x) + extent;
    const std::int64_t core_y64 = static_cast<std::int64_t>(rect.y) + extent;
    const Rect core{
        static_cast<int>(std::clamp(core_x64,
                                    static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                    static_cast<std::int64_t>(std::numeric_limits<int>::max()))),
        static_cast<int>(std::clamp(core_y64,
                                    static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                    static_cast<std::int64_t>(std::numeric_limits<int>::max()))),
        std::max(0, safe_add(rect.width, safe_negate(twice_extent))),
        std::max(0, safe_add(rect.height, safe_negate(twice_extent))),
    };
    const int core_radius = std::max(0, safe_add(outer_radius, safe_negate(extent)));
    const std::int64_t fade_distance64 = static_cast<std::int64_t>(std::max(1, blur)) * 2;
    const int fade_distance2 = static_cast<int>(std::clamp(
        fade_distance64,
        static_cast<std::int64_t>(2),
        static_cast<std::int64_t>(std::numeric_limits<int>::max())));
    const std::int64_t fade_squared = static_cast<std::int64_t>(fade_distance2) * fade_distance2;
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int distance = modern_paint_rounded_rect_outside_distance_half_px_resolved(
                core, core_radius, x, y);
            if (distance >= fade_distance2) {
                continue;
            }
            const int remaining = fade_distance2 - distance;
            const int coverage = static_cast<int>(
                (static_cast<std::int64_t>(remaining) * remaining * 255) /
                std::max<std::int64_t>(1, fade_squared));
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

} // namespace jellyframe
