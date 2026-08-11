#include "render_core/software_renderer.h"

#include "render_core/text_scan.h"
#include "render_core/modern_paint.h"
#include "render_core/feature_config.h"
#include "render_core/raster_primitives.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#ifdef JELLYFRAME_ENABLE_IMAGE_FILE_IO
#include <fstream>
#endif
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace jellyframe {
namespace {

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const std::int64_t left_right = static_cast<std::int64_t>(left.x) + static_cast<std::int64_t>(left.width);
    const std::int64_t right_right = static_cast<std::int64_t>(right.x) + static_cast<std::int64_t>(right.width);
    const std::int64_t left_bottom = static_cast<std::int64_t>(left.y) + static_cast<std::int64_t>(left.height);
    const std::int64_t right_bottom = static_cast<std::int64_t>(right.y) + static_cast<std::int64_t>(right.height);
    const int x2 = clamp_int64_to_int(std::min(left_right, right_right));
    const int y2 = clamp_int64_to_int(std::min(left_bottom, right_bottom));
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

Rect union_rect(Rect left, Rect right) {
    if (empty_rect(left)) {
        return right;
    }
    if (empty_rect(right)) {
        return left;
    }
    const int x1 = std::min(left.x, right.x);
    const int y1 = std::min(left.y, right.y);
    const int x2 = std::max(safe_edge(left.x, left.width), safe_edge(right.x, right.width));
    const int y2 = std::max(safe_edge(left.y, left.height), safe_edge(right.y, right.height));
    return Rect{x1, y1, safe_span(x1, x2), safe_span(y1, y2)};
}

bool contains_rect(Rect outer, Rect inner) {
    return !empty_rect(inner) &&
        inner.x >= outer.x &&
        inner.y >= outer.y &&
        static_cast<std::int64_t>(inner.x) + static_cast<std::int64_t>(inner.width) <=
            static_cast<std::int64_t>(outer.x) + static_cast<std::int64_t>(outer.width) &&
        static_cast<std::int64_t>(inner.y) + static_cast<std::int64_t>(inner.height) <=
            static_cast<std::int64_t>(outer.y) + static_cast<std::int64_t>(outer.height);
}

bool checked_pixel_count(int width, int height, std::size_t& output) {
    output = 0;
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h) {
        return false;
    }
    output = w * h;
    return true;
}

Rect target_rect(const FrameBuffer& target) {
    return Rect{0, 0, target.width, target.height};
}

std::vector<Rect> normalize_dirty_rects(const Rect* dirty_rects,
                                        std::size_t dirty_rect_count,
                                        Rect target) {
    std::vector<Rect> normalized;
    normalized.reserve(dirty_rect_count);
    for (std::size_t index = 0; index < dirty_rect_count; ++index) {
        const Rect dirty = intersect_rect(dirty_rects[index], target);
        if (empty_rect(dirty)) {
            continue;
        }
        bool covered = false;
        for (const Rect& existing : normalized) {
            if (contains_rect(existing, dirty)) {
                covered = true;
                break;
            }
        }
        if (covered) {
            continue;
        }
        normalized.erase(
            std::remove_if(normalized.begin(), normalized.end(), [dirty](Rect existing) {
                return contains_rect(dirty, existing);
            }),
            normalized.end());
        normalized.push_back(dirty);
    }
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t left = 0; left + 1 < normalized.size() && !merged; ++left) {
            for (std::size_t right = left + 1; right < normalized.size(); ++right) {
                if (empty_rect(intersect_rect(normalized[left], normalized[right]))) {
                    continue;
                }
                normalized[left] = union_rect(normalized[left], normalized[right]);
                normalized.erase(normalized.begin() + static_cast<std::ptrdiff_t>(right));
                merged = true;
                break;
            }
        }
    }
    return normalized;
}

std::uint8_t clamp_u8(int value) {
    return raster_clamp_u8(value);
}

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = clamp_u8(alpha);
    return color;
}

Rect clipped_target_rect(const FrameBuffer& target, Rect rect) {
    return intersect_rect(rect, target_rect(target));
}

Rect clipped_target_rect(const FrameBuffer& target, Rect rect, Rect clip) {
    return intersect_rect(clipped_target_rect(target, rect), clip);
}

void fill_opaque_region(FrameBuffer& target, Rect rect, Rect clip, Color color) {
    const Rect visible = clipped_target_rect(target, rect, clip);
    const int y_end = safe_edge(visible.y, visible.height);
    for (int y = visible.y; y < y_end; ++y) {
        Color* row = target.pixels.data() + static_cast<std::size_t>(y) *
            static_cast<std::size_t>(target.width) + static_cast<std::size_t>(visible.x);
        std::fill(row, row + visible.width, color);
    }
}

void fill_opaque_rounded_rect(FrameBuffer& target, Rect rect, Rect clip, Color color, int border_radius) {
    const CornerRadii radii = decode_corner_radii(border_radius);
    if (!has_corner_radius(border_radius)) {
        fill_opaque_region(target, rect, clip, color);
        return;
    }
    if (!(radii.top_left == radii.top_right && radii.top_left == radii.bottom_right &&
          radii.top_left == radii.bottom_left)) {
        const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
        const Rect visible = clipped_target_rect(target, rect, clip);
        for (int y = visible.y; y < visible.y + visible.height; ++y) {
            for (int x = visible.x; x < visible.x + visible.width; ++x) {
                const int coverage = rounded_rect_coverage(rounded, x, y);
                if (coverage == 255) {
                    target.pixel(x, y) = color;
                }
                else if (coverage > 0) blend_pixel(target, x, y, with_coverage(color, coverage));
            }
        }
        return;
    }
    border_radius = std::min(radii.top_left, std::min(rect.width, rect.height) / 2);

    fill_opaque_region(target,
                       Rect{rect.x, rect.y + border_radius, rect.width, rect.height - border_radius * 2},
                       clip,
                       color);
    fill_opaque_region(target,
                       Rect{rect.x + border_radius, rect.y, rect.width - border_radius * 2, border_radius},
                       clip,
                       color);
    fill_opaque_region(target,
                       Rect{rect.x + border_radius,
                            rect.y + rect.height - border_radius,
                            rect.width - border_radius * 2,
                            border_radius},
                       clip,
                       color);

    const Rect corners[] = {
        Rect{rect.x, rect.y, border_radius, border_radius},
        Rect{safe_edge(rect.x, safe_edge(rect.width, -border_radius)), rect.y, border_radius, border_radius},
        Rect{rect.x, safe_edge(rect.y, safe_edge(rect.height, -border_radius)), border_radius, border_radius},
        Rect{safe_edge(rect.x, safe_edge(rect.width, -border_radius)),
             safe_edge(rect.y, safe_edge(rect.height, -border_radius)),
             border_radius,
             border_radius},
    };
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    for (const Rect corner : corners) {
        const Rect visible = clipped_target_rect(target, corner, clip);
        const int y_end = safe_edge(visible.y, visible.height);
        const int x_end = safe_edge(visible.x, visible.width);
        for (int y = visible.y; y < y_end; ++y) {
            Color* row = target.pixels.data() + static_cast<std::size_t>(y) *
                static_cast<std::size_t>(target.width) + static_cast<std::size_t>(visible.x);
            for (int x = visible.x; x < x_end; ++x) {
                const int coverage = rounded_rect_coverage(rounded, x, y);
                if (coverage == 255) {
                    row[x - visible.x] = color;
                } else if (coverage > 0) {
                    blend_pixel(target, x, y, with_coverage(color, coverage));
                }
            }
        }
    }
}

void fill_rect(FrameBuffer& target, Rect rect, Color color, int border_radius = 0) {
    const Rect clip = target_rect(target);
    const Rect clipped = clipped_target_rect(target, rect, clip);
    if (empty_rect(clipped) || color.a == 0) {
        return;
    }
    if (color.a == 255) {
        fill_opaque_rounded_rect(target, rect, clip, color, border_radius);
        return;
    }
    const int y_end = safe_edge(clipped.y, clipped.height);
    const int x_end = safe_edge(clipped.x, clipped.width);
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    for (int y = clipped.y; y < y_end; ++y) {
        for (int x = clipped.x; x < x_end; ++x) {
            const int coverage = rounded_rect_coverage(rounded, x, y);
            if (coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

void fill_rect_clipped(FrameBuffer& target, Rect rect, Rect clip, Color color, int border_radius = 0) {
    const Rect clipped = clipped_target_rect(target, rect, clip);
    if (empty_rect(clipped) || color.a == 0) {
        return;
    }
    if (color.a == 255) {
        fill_opaque_rounded_rect(target, rect, clip, color, border_radius);
        return;
    }
    const RasterRoundedRect rounded = prepare_rounded_rect(rect, border_radius);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rounded, x, y);
            if (coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

void stroke_rect(FrameBuffer& target, Rect rect, Color color, int stroke_width, int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect);
    if (empty_rect(clipped) || color.a == 0 || stroke_width <= 0) {
        return;
    }
    stroke_width = std::min(stroke_width, std::max(1, std::min(rect.width, rect.height) / 2));
    if (!has_corner_radius(border_radius)) {
        fill_rect(target, Rect{rect.x, rect.y, rect.width, stroke_width}, color);
        fill_rect(target, Rect{rect.x, safe_edge(rect.y, safe_edge(rect.height, -stroke_width)), rect.width, stroke_width}, color);
        fill_rect(target, Rect{rect.x, rect.y, stroke_width, rect.height}, color);
        fill_rect(target, Rect{safe_edge(rect.x, safe_edge(rect.width, -stroke_width)), rect.y, stroke_width, rect.height}, color);
        return;
    }

    const Rect inner{
        rect.x + stroke_width,
        rect.y + stroke_width,
        std::max(0, rect.width - stroke_width * 2),
        std::max(0, rect.height - stroke_width * 2),
    };
    const int inner_radius = expand_corner_radii(border_radius, -stroke_width);
    const RasterRoundedRect outer = prepare_rounded_rect(rect, border_radius);
    const RasterRoundedRect inner_geometry = prepare_rounded_rect(inner, inner_radius);
    const int y_end = safe_edge(clipped.y, clipped.height);
    const int x_end = safe_edge(clipped.x, clipped.width);
    for (int y = clipped.y; y < y_end; ++y) {
        for (int x = clipped.x; x < x_end; ++x) {
            const int outer_coverage = rounded_rect_coverage(outer, x, y);
            if (outer_coverage <= 0) {
                continue;
            }
            const int inner_coverage = empty_rect(inner) ? 0 : rounded_rect_coverage(inner_geometry, x, y);
            const int stroke_coverage = std::max(0, outer_coverage - inner_coverage);
            if (stroke_coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, stroke_coverage));
        }
    }
}

void stroke_rect_clipped(FrameBuffer& target, Rect rect, Rect clip, Color color, int stroke_width, int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect, clip);
    if (empty_rect(clipped) || color.a == 0 || stroke_width <= 0) {
        return;
    }
    stroke_width = std::min(stroke_width, std::max(1, std::min(rect.width, rect.height) / 2));
    if (!has_corner_radius(border_radius)) {
        fill_rect_clipped(target, Rect{rect.x, rect.y, rect.width, stroke_width}, clip, color);
        fill_rect_clipped(target, Rect{rect.x, safe_edge(rect.y, safe_edge(rect.height, -stroke_width)), rect.width, stroke_width}, clip, color);
        fill_rect_clipped(target, Rect{rect.x, rect.y, stroke_width, rect.height}, clip, color);
        fill_rect_clipped(target, Rect{safe_edge(rect.x, safe_edge(rect.width, -stroke_width)), rect.y, stroke_width, rect.height}, clip, color);
        return;
    }

    const Rect inner{
        rect.x + stroke_width,
        rect.y + stroke_width,
        std::max(0, rect.width - stroke_width * 2),
        std::max(0, rect.height - stroke_width * 2),
    };
    const int inner_radius = expand_corner_radii(border_radius, -stroke_width);
    const RasterRoundedRect outer = prepare_rounded_rect(rect, border_radius);
    const RasterRoundedRect inner_geometry = prepare_rounded_rect(inner, inner_radius);
    const int y_end = safe_edge(clipped.y, clipped.height);
    const int x_end = safe_edge(clipped.x, clipped.width);
    for (int y = clipped.y; y < y_end; ++y) {
        for (int x = clipped.x; x < x_end; ++x) {
            const int outer_coverage = rounded_rect_coverage(outer, x, y);
            if (outer_coverage <= 0) {
                continue;
            }
            const int inner_coverage = empty_rect(inner) ? 0 : rounded_rect_coverage(inner_geometry, x, y);
            const int stroke_coverage = std::max(0, outer_coverage - inner_coverage);
            if (stroke_coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, stroke_coverage));
        }
    }
}

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED

void fill_linear_gradient(FrameBuffer& target,
                          Rect rect,
                          Color first,
                          Color second,
                          GradientAxis axis,
                          int border_radius = 0) {
    modern_paint_fill_linear_gradient(target, rect, target_rect(target), first, second, axis, border_radius);
}

void fill_linear_gradient_clipped(FrameBuffer& target,
                                  Rect rect,
                                  Rect clip,
                                  Color first,
                                  Color second,
                                  GradientAxis axis,
                                  int border_radius = 0) {
    modern_paint_fill_linear_gradient(target, rect, clip, first, second, axis, border_radius);
}

void fill_conic_gradient_region(FrameBuffer& target,
                                Rect rect,
                                Rect clipped,
                                Color first,
                                Color second,
                                int stop_percent,
                                int border_radius) {
    modern_paint_fill_conic_gradient_region(target, rect, clipped, first, second, stop_percent, border_radius);
}

void fill_conic_gradient(FrameBuffer& target,
                         Rect rect,
                         Color first,
                         Color second,
                         int stop_percent,
                         int border_radius = 0) {
    fill_conic_gradient_region(target,
                               rect,
                               clipped_target_rect(target, rect),
                               first,
                               second,
                               stop_percent,
                               border_radius);
}

void fill_conic_gradient_clipped(FrameBuffer& target,
                                 Rect rect,
                                 Rect clip,
                                 Color first,
                                 Color second,
                                 int stop_percent,
                                 int border_radius = 0) {
    fill_conic_gradient_region(target,
                               rect,
                               clipped_target_rect(target, rect, clip),
                               first,
                               second,
                               stop_percent,
                               border_radius);
}

void fill_radial_gradient_region(FrameBuffer& target,
                                 Rect rect,
                                 Rect clipped,
                                 Color center_color,
                                 Color edge_color,
                                 GradientAxis axis,
                                 int packed_position,
                                 int border_radius) {
    modern_paint_fill_radial_gradient_region(target,
                                             rect,
                                             clipped,
                                             center_color,
                                             edge_color,
                                             axis,
                                             packed_position,
                                             border_radius);
}

void fill_radial_gradient(FrameBuffer& target,
                          Rect rect,
                          Color center_color,
                          Color edge_color,
                          GradientAxis axis,
                          int packed_position,
                          int border_radius = 0) {
    fill_radial_gradient_region(target,
                                rect,
                                clipped_target_rect(target, rect),
                                center_color,
                                edge_color,
                                axis,
                                packed_position,
                                border_radius);
}

void fill_radial_gradient_clipped(FrameBuffer& target,
                                  Rect rect,
                                  Rect clip,
                                  Color center_color,
                                  Color edge_color,
                                  GradientAxis axis,
                                  int packed_position,
                                  int border_radius = 0) {
    fill_radial_gradient_region(target,
                                rect,
                                clipped_target_rect(target, rect, clip),
                                center_color,
                                edge_color,
                                axis,
                                packed_position,
                                border_radius);
}

void fill_soft_box_shadow(FrameBuffer& target, Rect rect, Rect clip, Color color, int outer_radius,
                          int extent, int blur) {
    modern_paint_fill_soft_box_shadow(target, rect, clip, color, outer_radius, extent, blur);
}

#endif

std::array<std::uint8_t, 7> glyph_rows(char raw_ch) {
    const char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(raw_ch)));
    switch (ch) {
    case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    case '6': return {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c};
    case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    case 'C': return {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f};
    case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    case 'G': return {0x0f, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0f};
    case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
    case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11};
    case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f};
    case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    default: return {0x1f, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    }
}

char fallback_glyph_for_codepoint(const std::string& text, std::size_t& index) {
    const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
    return codepoint < 0x80U ? static_cast<char>(codepoint) : '?';
}

void draw_text(FrameBuffer& target,
               Rect rect,
               Color color,
               const std::string& text,
               int font_size,
               int font_weight,
               std::uint32_t font_family_hash,
               TextCommandAlign align,
               bool single_line,
               TextPainter text_painter,
               DiagnosticSink* diagnostics) {
    (void)single_line;
    if (color.a == 0 || empty_rect(rect)) {
        return;
    }
    if (font_family_hash != 0 && text_painter.paint_family != nullptr &&
        text_painter.paint_family(target,
                                  rect,
                                  color,
                                  text,
                                  font_size,
                                  font_weight,
                                  font_family_hash,
                                  align,
                                  single_line,
                                  text_painter.context)) {
        return;
    }
    if (text_painter.paint != nullptr &&
        text_painter.paint(target, rect, color, text, font_size, font_weight, align, single_line, text_painter.context)) {
        return;
    }
    const bool has_non_ascii = std::any_of(text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) >= 0x80U;
    });
    if (text_painter.paint != nullptr) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Warning,
                          "paint-text-backend-failed",
                          "Text painter rejected a text command; built-in bitmap fallback was used",
                          text);
    }
    if (has_non_ascii) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Warning,
                          "paint-non-ascii-fallback",
                          "Built-in text fallback cannot draw real non-ASCII glyphs",
                          text);
    }
    const int scale = font_size >= 22 ? 2 : 1;
    const int glyph_width = 5 * scale;
    const int advance = 6 * scale;
    const int glyph_height = 7 * scale;
    const int stroke_passes = font_weight >= 600 ? 2 : 1;
    int glyph_count = 0;
    for (std::size_t index = 0; index < text.size();) {
        fallback_glyph_for_codepoint(text, index);
        ++glyph_count;
    }
    const int text_width = std::min(rect.width, glyph_count * advance);
    int cursor_x = rect.x;
    if (align == TextCommandAlign::Center) {
        cursor_x += std::max(0, (rect.width - text_width) / 2);
    } else if (align == TextCommandAlign::End) {
        cursor_x += std::max(0, rect.width - text_width);
    }
    const int baseline_y = rect.y + std::max(0, (rect.height - glyph_height) / 2);
    for (std::size_t index = 0; index < text.size();) {
        if (static_cast<std::int64_t>(cursor_x) + glyph_width >
            static_cast<std::int64_t>(safe_edge(rect.x, rect.width))) {
            break;
        }
        const char ch = fallback_glyph_for_codepoint(text, index);
        const std::array<std::uint8_t, 7> rows = glyph_rows(ch);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[static_cast<std::size_t>(row)] & (1U << (4 - col))) == 0U) {
                    continue;
                }
                for (int pass = 0; pass < stroke_passes; ++pass) {
                    fill_rect(target,
                              Rect{cursor_x + col * scale + pass, baseline_y + row * scale, scale, scale},
                              color);
                }
            }
        }
        cursor_x += advance;
    }
}

void composite_buffer_clipped(FrameBuffer& target, const FrameBuffer& source, int dst_x, int dst_y, Rect clip, float opacity) {
    const Rect target_bounds = target_rect(target);
    Rect copy_rect = intersect_rect(Rect{dst_x, dst_y, source.width, source.height}, target_bounds);
    copy_rect = intersect_rect(copy_rect, clip);
    if (empty_rect(copy_rect)) {
        return;
    }
    const int src_x = copy_rect.x - dst_x;
    const int src_y = copy_rect.y - dst_y;
    for (int y = 0; y < copy_rect.height; ++y) {
        for (int x = 0; x < copy_rect.width; ++x) {
            blend_pixel(target,
                        copy_rect.x + x,
                        copy_rect.y + y,
                        with_opacity(source.pixel(src_x + x, src_y + y), opacity));
        }
    }
}

void apply_rounded_clip(FrameBuffer& surface, Rect clip, int border_radius) {
    if (!has_corner_radius(border_radius)) {
        return;
    }
    const Rect visible = intersect_rect(clip, target_rect(surface));
    if (empty_rect(visible)) {
        surface.clear(Color{0, 0, 0, 0});
        return;
    }
    const RasterRoundedRect rounded = prepare_rounded_rect(clip, border_radius);
    for (int y = 0; y < surface.height; ++y) {
        for (int x = 0; x < surface.width; ++x) {
            if (x < visible.x || y < visible.y ||
                x >= safe_edge(visible.x, visible.width) ||
                y >= safe_edge(visible.y, visible.height)) {
                surface.pixel(x, y) = Color{0, 0, 0, 0};
                continue;
            }
            surface.pixel(x, y) = with_coverage(surface.pixel(x, y), rounded_rect_coverage(rounded, x, y));
        }
    }
}

Color lerp_color_fixed(Color left, Color right, int t256) {
    return Color{
        clamp_u8((static_cast<int>(left.r) * (256 - t256) + static_cast<int>(right.r) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.g) * (256 - t256) + static_cast<int>(right.g) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.b) * (256 - t256) + static_cast<int>(right.b) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.a) * (256 - t256) + static_cast<int>(right.a) * t256 + 128) >> 8),
    };
}

Rect transformed_destination_rect(Rect source_rect,
                                  Rect transform_reference_rect,
                                  const Transform2D& transform,
                                  int origin_x_percent,
                                  int origin_y_percent) {
    const float origin_x = static_cast<float>(transform_reference_rect.x) +
        static_cast<float>(transform_reference_rect.width) * static_cast<float>(origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(transform_reference_rect.y) +
        static_cast<float>(transform_reference_rect.height) * static_cast<float>(origin_y_percent) / 100.0F;

    constexpr float kPi = 3.14159265358979323846F;
    const float radians = transform.rotate_degrees * kPi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float corners[4][2] = {
        {static_cast<float>(source_rect.x), static_cast<float>(source_rect.y)},
        {static_cast<float>(safe_edge(source_rect.x, source_rect.width)), static_cast<float>(source_rect.y)},
        {static_cast<float>(source_rect.x), static_cast<float>(safe_edge(source_rect.y, source_rect.height))},
        {static_cast<float>(safe_edge(source_rect.x, source_rect.width)),
         static_cast<float>(safe_edge(source_rect.y, source_rect.height))},
    };
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    for (int index = 0; index < 4; ++index) {
        const float dx = (corners[index][0] - origin_x) * transform.scale_x;
        const float dy = (corners[index][1] - origin_y) * transform.scale_y;
        const float x = origin_x + dx * c - dy * s;
        const float y = origin_y + dx * s + dy * c;
        if (index == 0) {
            min_x = max_x = x;
            min_y = max_y = y;
        } else {
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    const int x = static_cast<int>(std::floor(min_x));
    const int y = static_cast<int>(std::floor(min_y));
    const int right = static_cast<int>(std::ceil(max_x));
    const int bottom = static_cast<int>(std::ceil(max_y));
    return Rect{x, y, std::max(1, right - x), std::max(1, bottom - y)};
}

void composite_transformed_buffer(FrameBuffer& target,
                                  const FrameBuffer& source,
                                  Rect source_rect,
                                  Rect transform_reference_rect,
                                  Rect destination,
                                  const Transform2D& transform,
                                  int origin_x_percent,
                                  int origin_y_percent,
                                  Rect clip,
                                  float opacity,
                                  bool smooth) {
    Rect copy_rect = intersect_rect(destination, target_rect(target));
    copy_rect = intersect_rect(copy_rect, clip);
    if (empty_rect(copy_rect) || source.width <= 0 || source.height <= 0) {
        return;
    }

    const float origin_x = static_cast<float>(transform_reference_rect.x) +
        static_cast<float>(transform_reference_rect.width) * static_cast<float>(origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(transform_reference_rect.y) +
        static_cast<float>(transform_reference_rect.height) * static_cast<float>(origin_y_percent) / 100.0F;
    constexpr float kPi = 3.14159265358979323846F;
    const float radians = transform.rotate_degrees * kPi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    for (int y = 0; y < copy_rect.height; ++y) {
        const float target_y = static_cast<float>(copy_rect.y + y) + 0.5F;
        for (int x = 0; x < copy_rect.width; ++x) {
            const float target_x = static_cast<float>(copy_rect.x + x) + 0.5F;
            const float dx = target_x - origin_x;
            const float dy = target_y - origin_y;
            const float unrotated_x = origin_x + dx * c + dy * s;
            const float unrotated_y = origin_y - dx * s + dy * c;
            const float source_world_x = origin_x +
                (unrotated_x - origin_x) / std::max(0.01F, transform.scale_x);
            const float source_world_y = origin_y +
                (unrotated_y - origin_y) / std::max(0.01F, transform.scale_y);
            const float source_x = source_world_x - static_cast<float>(source_rect.x);
            const float source_y = source_world_y - static_cast<float>(source_rect.y);
            if (source_x < 0.0F || source_y < 0.0F ||
                source_x >= static_cast<float>(source.width) ||
                source_y >= static_cast<float>(source.height)) {
                continue;
            }
            Color source_pixel;
            if (smooth) {
                const int sx = std::max(0, std::min(source.width - 1, static_cast<int>(source_x)));
                const int sy = std::max(0, std::min(source.height - 1, static_cast<int>(source_y)));
                const int nx = std::min(source.width - 1, sx + 1);
                const int ny = std::min(source.height - 1, sy + 1);
                const int tx = std::max(0, std::min(255, static_cast<int>((source_x - static_cast<float>(sx)) * 256.0F)));
                const int ty = std::max(0, std::min(255, static_cast<int>((source_y - static_cast<float>(sy)) * 256.0F)));
                const Color top = lerp_color_fixed(source.pixel(sx, sy), source.pixel(nx, sy), tx);
                const Color bottom = lerp_color_fixed(source.pixel(sx, ny), source.pixel(nx, ny), tx);
                source_pixel = lerp_color_fixed(top, bottom, ty);
            } else {
                const int sx = std::max(0, std::min(source.width - 1, static_cast<int>(source_x)));
                const int sy = std::max(0, std::min(source.height - 1, static_cast<int>(source_y)));
                source_pixel = source.pixel(sx, sy);
            }
            blend_pixel(target,
                        copy_rect.x + x,
                        copy_rect.y + y,
                        with_opacity(source_pixel, opacity));
        }
    }
}

int round_transform_offset(float value) {
    return static_cast<int>(value >= 0.0F ? value + 0.5F : value - 0.5F);
}

bool offscreen_fits_budget(Rect bounds,
                           SoftwareCompositor::Options options,
                           std::size_t active_pixels,
                           std::size_t& pixels) {
    pixels = 0;
    if (!checked_pixel_count(bounds.width, bounds.height, pixels)) {
        return false;
    }
    if (options.max_offscreen_pixels == 0) {
        return true;
    }
    return active_pixels <= options.max_offscreen_pixels &&
        pixels <= options.max_offscreen_pixels - active_pixels;
}

bool framebuffer_fits_budget(int width, int height, SoftwareCompositor::Options options) {
    std::size_t pixels = 0;
    if (!checked_pixel_count(width, height, pixels)) {
        return false;
    }
    if (options.max_framebuffer_pixels == 0) {
        return true;
    }
    return pixels <= options.max_framebuffer_pixels;
}

struct OpaqueFillPrefix {
    std::size_t first_command = 0;
    bool covers_clip = false;
};

OpaqueFillPrefix opaque_fill_prefix(const DisplayList& display_list,
                                    Rect clip,
                                    int offset_x,
                                    int offset_y) {
    OpaqueFillPrefix prefix;
    for (std::size_t index = 0; index < display_list.size(); ++index) {
        const DisplayCommand& command = display_list[index];
        if (command.type != DisplayCommandType::FillRect ||
            command.color.a != 255 ||
            command.border_radius != 0) {
            break;
        }
        Rect rect = command.rect;
        rect.x += offset_x;
        rect.y += offset_y;
        if (contains_rect(rect, clip)) {
            prefix.first_command = index;
            prefix.covers_clip = true;
        }
    }
    return prefix;
}

bool root_opaque_fill_covers(const LayerNode& root, Rect clip) {
    if (root.type == LayerType::Composited || root.opacity < 0.999F || root.has_transform) {
        return false;
    }
    if (root.has_clip && !contains_rect(root.clip_rect, clip)) {
        return false;
    }
    return opaque_fill_prefix(root.display_list, clip, 0, 0).covers_clip;
}

Rect direct_layer_source_bounds(const LayerNode& layer) {
    Rect bounds = layer.bounds;
    for (const DisplayCommand& command : layer.display_list) {
        bounds = union_rect(bounds, command.rect);
    }
    return bounds;
}

std::size_t reserve_composite_bounds_entry(SoftwareCompositor::Scratch& scratch) {
    const std::size_t index = scratch.active_composite_bounds++;
    if (index == scratch.composite_bounds.size()) {
        scratch.composite_bounds.push_back({});
    }
    SoftwareCompositor::Scratch::CompositeBoundsEntry& entry = scratch.composite_bounds[index];
    entry.source_bounds = Rect{};
    entry.visual_bounds = Rect{};
    entry.children.clear();
    return index;
}

void prepare_composite_bounds(const LayerNode& root, SoftwareCompositor::Scratch& scratch) {
    struct PendingLayer {
        const LayerNode* layer = nullptr;
        std::size_t bounds_index = 0;
        std::size_t next_child = 0;
    };

    scratch.active_composite_bounds = 0;
    const std::size_t root_index = reserve_composite_bounds_entry(scratch);
    std::vector<PendingLayer> pending;
    pending.push_back(PendingLayer{&root, root_index, 0});
    while (!pending.empty()) {
        PendingLayer& current = pending.back();
        if (current.next_child < current.layer->children.size()) {
            const LayerNode& child = *current.layer->children[current.next_child++];
            const std::size_t child_index = reserve_composite_bounds_entry(scratch);
            scratch.composite_bounds[current.bounds_index].children.push_back(child_index);
            pending.push_back(PendingLayer{&child, child_index, 0});
            continue;
        }

        SoftwareCompositor::Scratch::CompositeBoundsEntry& entry =
            scratch.composite_bounds[current.bounds_index];
        Rect source_bounds = direct_layer_source_bounds(*current.layer);
        for (const std::size_t child_index : entry.children) {
            source_bounds = union_rect(source_bounds, scratch.composite_bounds[child_index].visual_bounds);
        }
        entry.source_bounds = source_bounds;

        Rect transformed_source = source_bounds;
        transformed_source.x += round_transform_offset(current.layer->transform.translate_x);
        transformed_source.y += round_transform_offset(current.layer->transform.translate_y);
        const bool has_scale_or_rotate =
            std::abs(current.layer->transform.scale_x - 1.0F) >= 0.001F ||
            std::abs(current.layer->transform.scale_y - 1.0F) >= 0.001F ||
            std::abs(current.layer->transform.rotate_degrees) >= 0.001F;
        if (!has_scale_or_rotate) {
            entry.visual_bounds = transformed_source;
        } else {
            Rect transform_reference_bounds = current.layer->bounds;
            transform_reference_bounds.x += round_transform_offset(current.layer->transform.translate_x);
            transform_reference_bounds.y += round_transform_offset(current.layer->transform.translate_y);
            entry.visual_bounds = transformed_destination_rect(transformed_source,
                                                                transform_reference_bounds,
                                                                current.layer->transform,
                                                                current.layer->transform_origin_x_percent,
                                                                current.layer->transform_origin_y_percent);
        }
        pending.pop_back();
    }
}

void rasterize_with_opacity(const SoftwareRasterizer& rasterizer,
                            const DisplayList& display_list,
                            FrameBuffer& target,
                            Rect clip,
                            int offset_x,
                            int offset_y,
                            float opacity,
                            std::size_t first_command = 0,
                            SoftwareRasterizerScratch* scratch = nullptr) {
    if (opacity >= 0.999F) {
        for (std::size_t index = first_command; index < display_list.size(); ++index) {
            rasterizer.rasterize(display_list[index], target, clip, offset_x, offset_y, scratch);
        }
        return;
    }
    for (std::size_t index = first_command; index < display_list.size(); ++index) {
        const DisplayCommand& source = display_list[index];
        DisplayCommand command = source;
        command.color = with_opacity(command.color, opacity);
        command.color2 = with_opacity(command.color2, opacity);
        rasterizer.rasterize(command, target, clip, offset_x, offset_y, scratch);
    }
}

} // namespace

FrameBuffer::FrameBuffer(int width_in, int height_in, Color clear_color) {
    resize(width_in, height_in, clear_color);
}

void FrameBuffer::resize(int new_width, int new_height, Color clear_color) {
    std::size_t pixel_count = 0;
    if (!checked_pixel_count(new_width, new_height, pixel_count)) {
        width = 0;
        height = 0;
        pixels.clear();
        return;
    }
    try {
        pixels.resize(pixel_count);
        std::fill(pixels.begin(), pixels.end(), clear_color);
    } catch (const std::bad_alloc&) {
        width = 0;
        height = 0;
        pixels.clear();
        return;
    } catch (const std::length_error&) {
        width = 0;
        height = 0;
        pixels.clear();
        return;
    }
    width = new_width;
    height = new_height;
}

void FrameBuffer::clear(Color clear_color) {
    std::fill(pixels.begin(), pixels.end(), clear_color);
}

void SoftwareRasterizerScratch::release() {
    FrameBuffer{}.pixels.swap(temporary_surface.pixels);
    temporary_surface.width = 0;
    temporary_surface.height = 0;
}

bool FrameBuffer::contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

Color& FrameBuffer::pixel(int x, int y) {
    assert(contains(x, y));
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    return pixels[index];
}

const Color& FrameBuffer::pixel(int x, int y) const {
    assert(contains(x, y));
    const std::size_t index =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    return pixels[index];
}

SoftwareRasterizer::SoftwareRasterizer(TextPainter text_painter,
                                       DiagnosticSink* diagnostics,
                                       SoftwareRasterizerOptions options)
    : SoftwareRasterizer(text_painter, {}, diagnostics, options) {}

SoftwareRasterizer::SoftwareRasterizer(TextPainter text_painter,
                                       ImagePainter image_painter,
                                       DiagnosticSink* diagnostics,
                                       SoftwareRasterizerOptions options)
    : text_painter_(text_painter), image_painter_(image_painter), diagnostics_(diagnostics), options_(options) {}

bool SoftwareRasterizer::prepare_temporary_surface(FrameBuffer& surface, int width, int height) const {
    std::size_t pixels = 0;
    if (!checked_pixel_count(width, height, pixels) ||
        (options_.max_temporary_pixels != 0 && pixels > options_.max_temporary_pixels)) {
        report_diagnostic(diagnostics_,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Warning,
                          "paint-transient-surface-budget",
                          "Clipped paint temporary surface exceeded budget; command was skipped",
                          std::to_string(width) + "x" + std::to_string(height));
        return false;
    }
    surface.resize(width, height, Color{0, 0, 0, 0});
    return surface.width == width && surface.height == height && surface.pixels.size() == pixels;
}

void SoftwareRasterizer::rasterize(const DisplayList& display_list,
                                   FrameBuffer& target,
                                   Rect clip,
                                   int offset_x,
                                   int offset_y) const {
    rasterize(display_list, target, clip, offset_x, offset_y, nullptr);
}

void SoftwareRasterizer::rasterize(const DisplayList& display_list,
                                   FrameBuffer& target,
                                   Rect clip,
                                   int offset_x,
                                   int offset_y,
                                   SoftwareRasterizerScratch* scratch) const {
    for (const DisplayCommand& command : display_list) {
        rasterize(command, target, clip, offset_x, offset_y, scratch);
    }
}

void SoftwareRasterizer::rasterize(const DisplayCommand& command,
                                   FrameBuffer& target,
                                   Rect clip,
                                   int offset_x,
                                   int offset_y,
                                   SoftwareRasterizerScratch* scratch) const {
    Rect rect = command.rect;
    rect.x = safe_add(rect.x, offset_x);
    rect.y = safe_add(rect.y, offset_y);
    const Rect clipped = intersect_rect(rect, clip);
    if (empty_rect(clipped)) {
        return;
    }

    switch (command.type) {
    case DisplayCommandType::FillRect:
        if (contains_rect(clip, rect)) {
            fill_rect(target, rect, command.color, command.border_radius);
        } else {
            fill_rect_clipped(target, rect, clip, command.color, command.border_radius);
        }
        break;
    case DisplayCommandType::LinearGradient:
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        if (contains_rect(clip, rect)) {
            fill_linear_gradient(target,
                                 rect,
                                 command.color,
                                 command.color2,
                                 command.gradient_axis,
                                 command.border_radius);
        } else {
            fill_linear_gradient_clipped(target,
                                         rect,
                                         clip,
                                         command.color,
                                         command.color2,
                                         command.gradient_axis,
                                         command.border_radius);
        }
        break;
#else
        fill_rect_clipped(target, rect, clip, command.color, command.border_radius);
        break;
#endif
    case DisplayCommandType::ConicGradient:
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        if (contains_rect(clip, rect)) {
            fill_conic_gradient(target,
                                rect,
                                command.color,
                                command.color2,
                                command.gradient_stop_percent,
                                command.border_radius);
        } else {
            fill_conic_gradient_clipped(target,
                                        rect,
                                        clip,
                                        command.color,
                                        command.color2,
                                        command.gradient_stop_percent,
                                        command.border_radius);
        }
        break;
#else
        fill_rect_clipped(target, rect, clip, command.color, command.border_radius);
        break;
#endif
    case DisplayCommandType::RadialGradient:
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        if (contains_rect(clip, rect)) {
            fill_radial_gradient(target,
                                 rect,
                                 command.color,
                                 command.color2,
                                 command.gradient_axis,
                                 command.gradient_stop_percent,
                                 command.border_radius);
        } else {
            fill_radial_gradient_clipped(target,
                                         rect,
                                         clip,
                                         command.color,
                                         command.color2,
                                         command.gradient_axis,
                                         command.gradient_stop_percent,
                                         command.border_radius);
        }
        break;
#else
        fill_rect_clipped(target, rect, clip, command.color, command.border_radius);
        break;
#endif
    case DisplayCommandType::BoxShadow:
#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
        fill_soft_box_shadow(target,
                             rect,
                             clip,
                             command.color,
                             command.border_radius,
                             command.stroke_width,
                             command.gradient_stop_percent);
        break;
#else
        break;
#endif
    case DisplayCommandType::StrokeRect:
        if (contains_rect(clip, rect)) {
            stroke_rect(target, rect, command.color, command.stroke_width, command.border_radius);
        } else {
            stroke_rect_clipped(target, rect, clip, command.color, command.stroke_width, command.border_radius);
        }
        break;
    case DisplayCommandType::Text: {
        if (rect.width <= 0 || rect.height <= 0) {
            break;
        }
        if (contains_rect(clip, rect)) {
            draw_text(target,
                      rect,
                      command.color,
                      command.text,
                      command.font_size,
                      command.font_weight,
                      command.font_family_hash,
                      command.text_align,
                      command.text_single_line,
                      text_painter_,
                      diagnostics_);
            break;
        }
        const Rect visible = intersect_rect(rect, clip);
        if (empty_rect(visible)) {
            break;
        }
        FrameBuffer local_buffer;
        FrameBuffer& text_buffer = scratch != nullptr ? scratch->temporary_surface : local_buffer;
        if (!prepare_temporary_surface(text_buffer, visible.width, visible.height)) {
            break;
        }
        draw_text(text_buffer,
                  Rect{rect.x - visible.x, rect.y - visible.y, rect.width, rect.height},
                  command.color,
                  command.text,
                  command.font_size,
                  command.font_weight,
                  command.font_family_hash,
                  command.text_align,
                  command.text_single_line,
                  text_painter_,
                  diagnostics_);
        composite_buffer_clipped(target, text_buffer, visible.x, visible.y, clip, 1.0F);
        break;
    }
    case DisplayCommandType::Image: {
        if (image_painter_.paint == nullptr ||
            rect.width <= 0 ||
            rect.height <= 0) {
            report_diagnostic(diagnostics_,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "paint-image-fallback",
                              "Image command could not be painted; placeholder was used",
                              std::to_string(command.image_handle));
            fill_rect_clipped(target, rect, clip, Color{226, 232, 240, 255});
            break;
        }
        if (has_corner_radius(command.border_radius)) {
            const Rect visible = intersect_rect(rect, clip);
            if (empty_rect(visible)) {
                break;
            }
            FrameBuffer local_buffer;
            FrameBuffer& image_buffer = scratch != nullptr ? scratch->temporary_surface : local_buffer;
            if (!prepare_temporary_surface(image_buffer, visible.width, visible.height)) {
                fill_rect_clipped(target, rect, clip, Color{226, 232, 240, 255});
                break;
            }
            if (!image_painter_.paint(image_buffer,
                                      Rect{rect.x - visible.x, rect.y - visible.y, rect.width, rect.height},
                                      command.image_handle,
                                      command.object_fit,
                                      command.object_position,
                                      command.image_rendering,
                                      image_painter_.context)) {
                report_diagnostic(diagnostics_,
                                  DiagnosticStage::Paint,
                                  DiagnosticSeverity::Warning,
                                  "paint-image-fallback",
                                  "Image command could not be painted; placeholder was used",
                                  std::to_string(command.image_handle));
                fill_rect(image_buffer,
                          Rect{rect.x - visible.x, rect.y - visible.y, rect.width, rect.height},
                          Color{226, 232, 240, 255});
            }
            const RasterRoundedRect rounded = prepare_rounded_rect(rect, command.border_radius);
            for (int y = visible.y; y < visible.y + visible.height; ++y) {
                for (int x = visible.x; x < visible.x + visible.width; ++x) {
                    blend_pixel(target,
                                x,
                                y,
                                with_coverage(image_buffer.pixel(x - visible.x, y - visible.y),
                                              rounded_rect_coverage(rounded, x, y)));
                }
            }
            break;
        }
        if (contains_rect(clip, rect)) {
            if (!image_painter_.paint(target,
                                      rect,
                                      command.image_handle,
                                      command.object_fit,
                                      command.object_position,
                                      command.image_rendering,
                                      image_painter_.context)) {
                report_diagnostic(diagnostics_,
                                  DiagnosticStage::Paint,
                                  DiagnosticSeverity::Warning,
                                  "paint-image-fallback",
                                  "Image command could not be painted; placeholder was used",
                                  std::to_string(command.image_handle));
                fill_rect(target, rect, Color{226, 232, 240, 255});
            }
            break;
        }
        const Rect visible = intersect_rect(rect, clip);
        if (empty_rect(visible)) {
            break;
        }
        FrameBuffer local_buffer;
        FrameBuffer& image_buffer = scratch != nullptr ? scratch->temporary_surface : local_buffer;
        if (!prepare_temporary_surface(image_buffer, visible.width, visible.height)) {
            fill_rect_clipped(target, rect, clip, Color{226, 232, 240, 255});
            break;
        }
        if (!image_painter_.paint(image_buffer,
                                  Rect{rect.x - visible.x, rect.y - visible.y, rect.width, rect.height},
                                  command.image_handle,
                                  command.object_fit,
                                  command.object_position,
                                  command.image_rendering,
                                  image_painter_.context)) {
            report_diagnostic(diagnostics_,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "paint-image-fallback",
                              "Image command could not be painted; placeholder was used",
                              std::to_string(command.image_handle));
            fill_rect(image_buffer,
                      Rect{rect.x - visible.x, rect.y - visible.y, rect.width, rect.height},
                      Color{226, 232, 240, 255});
        }
        composite_buffer_clipped(target, image_buffer, visible.x, visible.y, clip, 1.0F);
        break;
    }
    }
}

SoftwareCompositor::SoftwareCompositor()
    : SoftwareCompositor(TextPainter{}) {}

SoftwareCompositor::SoftwareCompositor(TextPainter text_painter)
    : SoftwareCompositor(text_painter, Options{}) {}

SoftwareCompositor::SoftwareCompositor(TextPainter text_painter, Options options)
    : SoftwareCompositor(text_painter, {}, options) {}

SoftwareCompositor::SoftwareCompositor(TextPainter text_painter, ImagePainter image_painter)
    : SoftwareCompositor(text_painter, image_painter, Options{}) {}

SoftwareCompositor::SoftwareCompositor(TextPainter text_painter, ImagePainter image_painter, Options options)
    : rasterizer_(text_painter,
                  image_painter,
                  options.diagnostics,
                  SoftwareRasterizerOptions{options.max_offscreen_pixels}),
      options_(options) {}

FrameBuffer SoftwareCompositor::render(const LayerNode& root,
                                       int viewport_width,
                                       int viewport_height,
                                       Color background) const {
    if (!framebuffer_fits_budget(viewport_width, viewport_height, options_)) {
        report_diagnostic(options_.diagnostics,
                          DiagnosticStage::Paint,
                          DiagnosticSeverity::Error,
                          "paint-framebuffer-budget",
                          "Primary framebuffer exceeded the configured pixel budget",
                          std::to_string(viewport_width) + "x" + std::to_string(viewport_height));
        return {};
    }
    FrameBuffer target(viewport_width, viewport_height, background);
    render_into(root, target, background);
    return target;
}

void SoftwareCompositor::render_into(const LayerNode& root, FrameBuffer& target, Color background) const {
    render_into(root, target, background, nullptr, 0, nullptr);
}

void SoftwareCompositor::Scratch::release() {
    rasterizer.release();
    std::vector<CompositeBoundsEntry>().swap(composite_bounds);
    active_composite_bounds = 0;
}

void SoftwareCompositor::render_into(const LayerNode& root,
                                     FrameBuffer& target,
                                     Color background,
                                     const Rect* dirty_rects,
                                     std::size_t dirty_rect_count,
                                     Scratch* scratch) const {
    if (target.width <= 0 || target.height <= 0) {
        return;
    }
    Scratch local_scratch;
    Scratch& active_scratch = scratch != nullptr ? *scratch : local_scratch;
    prepare_composite_bounds(root, active_scratch);
    const Scratch::CompositeBoundsEntry& root_bounds = active_scratch.composite_bounds.front();
    SoftwareRasterizerScratch* rasterizer_scratch = &active_scratch.rasterizer;
    if (dirty_rects == nullptr || dirty_rect_count == 0) {
        const Rect full_target = target_rect(target);
        if (!root_opaque_fill_covers(root, full_target)) {
            target.clear(background);
        }
        composite_layer(root, root_bounds, target, full_target, 0, 0, 1.0F, 0, rasterizer_scratch, &active_scratch);
        return;
    }
    if (dirty_rect_count == 1) {
        const Rect dirty = intersect_rect(dirty_rects[0], target_rect(target));
        if (!empty_rect(dirty)) {
            if (!root_opaque_fill_covers(root, dirty)) {
                fill_rect(target, dirty, background);
            }
            composite_layer(root, root_bounds, target, dirty, 0, 0, 1.0F, 0, rasterizer_scratch, &active_scratch);
        }
        return;
    }
    const std::vector<Rect> normalized_dirty_rects =
        normalize_dirty_rects(dirty_rects, dirty_rect_count, target_rect(target));
    for (const Rect dirty : normalized_dirty_rects) {
        if (!root_opaque_fill_covers(root, dirty)) {
            fill_rect(target, dirty, background);
        }
        composite_layer(root, root_bounds, target, dirty, 0, 0, 1.0F, 0, rasterizer_scratch, &active_scratch);
    }
}

void SoftwareCompositor::composite_layer(const LayerNode& layer,
                                         const Scratch::CompositeBoundsEntry& bounds,
                                         FrameBuffer& target,
                                         Rect clip,
                                         int offset_x,
                                         int offset_y,
                                         float inherited_opacity,
                                         std::size_t active_offscreen_pixels,
                                         SoftwareRasterizerScratch* scratch,
                                         const Scratch* compositor_scratch) const {
    const int transform_x = round_transform_offset(layer.transform.translate_x);
    const int transform_y = round_transform_offset(layer.transform.translate_y);
    const int layer_offset_x = offset_x + transform_x;
    const int layer_offset_y = offset_y + transform_y;
    Rect layer_clip = clip;
    if (layer.has_clip) {
        Rect translated_clip = layer.clip_rect;
        translated_clip.x += layer_offset_x;
        translated_clip.y += layer_offset_y;
        layer_clip = intersect_rect(layer_clip, translated_clip);
        if (empty_rect(layer_clip)) {
            return;
        }
    }

    const float layer_opacity = inherited_opacity * layer.opacity;
    const bool needs_offscreen = layer.type == LayerType::Composited ||
        layer.opacity < 0.999F || has_corner_radius(layer.clip_border_radius);
    if (needs_offscreen) {
        Rect source_bounds = bounds.source_bounds;
        source_bounds.x += layer_offset_x;
        source_bounds.y += layer_offset_y;
        Rect transform_reference_bounds = layer.bounds;
        transform_reference_bounds.x += layer_offset_x;
        transform_reference_bounds.y += layer_offset_y;
        const bool has_scale_or_rotate =
            std::abs(layer.transform.scale_x - 1.0F) >= 0.001F ||
            std::abs(layer.transform.scale_y - 1.0F) >= 0.001F ||
            std::abs(layer.transform.rotate_degrees) >= 0.001F;
        const Rect destination = has_scale_or_rotate
            ? transformed_destination_rect(source_bounds,
                                           transform_reference_bounds,
                                           layer.transform,
                                           layer.transform_origin_x_percent,
                                           layer.transform_origin_y_percent)
            : source_bounds;
        Rect visible_destination = intersect_rect(destination, layer_clip);
        visible_destination = intersect_rect(visible_destination, target_rect(target));
        if (empty_rect(visible_destination)) {
            return;
        }
        Rect offscreen_bounds = has_scale_or_rotate ? source_bounds : visible_destination;
        std::size_t offscreen_pixels = 0;
        if (!offscreen_fits_budget(offscreen_bounds, options_, active_offscreen_pixels, offscreen_pixels)) {
            if (has_scale_or_rotate) {
                report_diagnostic(options_.diagnostics,
                                  DiagnosticStage::Paint,
                                  DiagnosticSeverity::Warning,
                                  "paint-transform-budget",
                                  "Transformed layer exceeded the aggregate live offscreen pixel budget and was skipped",
                                  std::to_string(offscreen_bounds.width) + "x" +
                                      std::to_string(offscreen_bounds.height));
                return;
            }
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "paint-offscreen-budget",
                              "Offscreen compositing buffer exceeded aggregate live budget; layer was painted by direct opacity fallback",
                              std::to_string(offscreen_bounds.width) + "x" + std::to_string(offscreen_bounds.height));
            rasterize_with_opacity(rasterizer_,
                                   layer.display_list,
                                   target,
                                   layer_clip,
                                   layer_offset_x,
                                   layer_offset_y,
                                   layer_opacity,
                                   0,
                                   scratch);
            for (std::size_t index = 0; index < layer.children.size(); ++index) {
                composite_layer(*layer.children[index],
                                compositor_scratch->composite_bounds[bounds.children[index]],
                                target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity,
                                active_offscreen_pixels, scratch, compositor_scratch);
            }
            return;
        }

        FrameBuffer offscreen;
        try {
            offscreen.resize(offscreen_bounds.width, offscreen_bounds.height, Color{0, 0, 0, 0});
        } catch (const std::bad_alloc&) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "paint-offscreen-allocation-failed",
                              "Offscreen compositing allocation failed; layer was painted by direct opacity fallback",
                              std::to_string(offscreen_bounds.width) + "x" + std::to_string(offscreen_bounds.height));
            rasterize_with_opacity(rasterizer_, layer.display_list, target, layer_clip,
                                   layer_offset_x, layer_offset_y, layer_opacity, 0, scratch);
            for (std::size_t index = 0; index < layer.children.size(); ++index) {
                composite_layer(*layer.children[index],
                                compositor_scratch->composite_bounds[bounds.children[index]],
                                target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity,
                                active_offscreen_pixels, scratch, compositor_scratch);
            }
            return;
        }
        const int child_offset_x = layer_offset_x - offscreen_bounds.x;
        const int child_offset_y = layer_offset_y - offscreen_bounds.y;
        const Rect offscreen_clip{0, 0, offscreen_bounds.width, offscreen_bounds.height};
        Rect local_layer_clip = offscreen_clip;
        if (layer.has_clip) {
            local_layer_clip = intersect_rect(
                local_layer_clip,
                Rect{layer_clip.x - offscreen_bounds.x,
                     layer_clip.y - offscreen_bounds.y,
                     layer_clip.width,
                     layer_clip.height});
        }
        rasterizer_.rasterize(layer.display_list,
                              offscreen,
                              local_layer_clip,
                              child_offset_x,
                              child_offset_y,
                              scratch);
        const Rect child_clip = local_layer_clip;
        for (std::size_t index = 0; index < layer.children.size(); ++index) {
            composite_layer(*layer.children[index],
                            compositor_scratch->composite_bounds[bounds.children[index]],
                            offscreen, child_clip, child_offset_x, child_offset_y, 1.0F,
                            active_offscreen_pixels + offscreen_pixels, scratch, compositor_scratch);
        }
        if (has_corner_radius(layer.clip_border_radius)) {
            apply_rounded_clip(offscreen,
                               Rect{layer.clip_rect.x - offscreen_bounds.x,
                                    layer.clip_rect.y - offscreen_bounds.y,
                                    layer.clip_rect.width,
                                    layer.clip_rect.height},
                               layer.clip_border_radius);
        }
        if (has_scale_or_rotate) {
            composite_transformed_buffer(target,
                                         offscreen,
                                         offscreen_bounds,
                                         transform_reference_bounds,
                                         destination,
                                         layer.transform,
                                         layer.transform_origin_x_percent,
                                         layer.transform_origin_y_percent,
                                         layer_clip,
                                         layer_opacity,
                                         options_.smooth_scaled_layers);
        } else {
            composite_buffer_clipped(target,
                                     offscreen,
                                     offscreen_bounds.x,
                                     offscreen_bounds.y,
                                     layer_clip,
                                     layer_opacity);
        }
        return;
    }

    const OpaqueFillPrefix prefix = layer_opacity >= 0.999F
        ? opaque_fill_prefix(layer.display_list, layer_clip, layer_offset_x, layer_offset_y)
        : OpaqueFillPrefix{};
    rasterize_with_opacity(rasterizer_,
                           layer.display_list,
                           target,
                           layer_clip,
                           layer_offset_x,
                           layer_offset_y,
                           layer_opacity,
                           prefix.first_command,
                           scratch);
    for (std::size_t index = 0; index < layer.children.size(); ++index) {
        composite_layer(*layer.children[index],
                        compositor_scratch->composite_bounds[bounds.children[index]],
                        target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity,
                        active_offscreen_pixels, scratch, compositor_scratch);
    }
}

#ifdef JELLYFRAME_ENABLE_IMAGE_FILE_IO
void write_ppm(const FrameBuffer& frame_buffer, const std::string& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open output image");
    }
    output << "P6\n" << frame_buffer.width << ' ' << frame_buffer.height << "\n255\n";
    for (const Color& pixel : frame_buffer.pixels) {
        const Color flattened = pixel.a == 255 ? pixel : Color{
            clamp_u8((pixel.r * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.g * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.b * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            255,
        };
        output.put(static_cast<char>(flattened.r));
        output.put(static_cast<char>(flattened.g));
        output.put(static_cast<char>(flattened.b));
    }
}

void write_bmp(const FrameBuffer& frame_buffer, const std::string& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open output image");
    }

    const int row_stride = ((frame_buffer.width * 3 + 3) / 4) * 4;
    const int pixel_bytes = row_stride * frame_buffer.height;
    const int file_bytes = 14 + 40 + pixel_bytes;

    const auto put_u16 = [&](std::uint16_t value) {
        output.put(static_cast<char>(value & 0xffU));
        output.put(static_cast<char>((value >> 8U) & 0xffU));
    };
    const auto put_u32 = [&](std::uint32_t value) {
        output.put(static_cast<char>(value & 0xffU));
        output.put(static_cast<char>((value >> 8U) & 0xffU));
        output.put(static_cast<char>((value >> 16U) & 0xffU));
        output.put(static_cast<char>((value >> 24U) & 0xffU));
    };

    output.put('B');
    output.put('M');
    put_u32(static_cast<std::uint32_t>(file_bytes));
    put_u16(0);
    put_u16(0);
    put_u32(14 + 40);

    put_u32(40);
    put_u32(static_cast<std::uint32_t>(frame_buffer.width));
    put_u32(static_cast<std::uint32_t>(frame_buffer.height));
    put_u16(1);
    put_u16(24);
    put_u32(0);
    put_u32(static_cast<std::uint32_t>(pixel_bytes));
    put_u32(2835);
    put_u32(2835);
    put_u32(0);
    put_u32(0);

    std::array<char, 3> padding{0, 0, 0};
    for (int y = frame_buffer.height - 1; y >= 0; --y) {
        for (int x = 0; x < frame_buffer.width; ++x) {
            Color pixel = frame_buffer.pixel(x, y);
            if (pixel.a != 255) {
                pixel = Color{
                    clamp_u8((pixel.r * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
                    clamp_u8((pixel.g * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
                    clamp_u8((pixel.b * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
                    255,
                };
            }
            output.put(static_cast<char>(pixel.b));
            output.put(static_cast<char>(pixel.g));
            output.put(static_cast<char>(pixel.r));
        }
        output.write(padding.data(), row_stride - frame_buffer.width * 3);
    }
}

void write_image(const FrameBuffer& frame_buffer, const std::string& path) {
    if (path.size() >= 4) {
        const std::string extension = path.substr(path.size() - 4);
        if (extension == ".ppm" || extension == ".PPM") {
            write_ppm(frame_buffer, path);
            return;
        }
    }
    write_bmp(frame_buffer, path);
}
#endif

std::size_t count_non_background_pixels(const FrameBuffer& frame_buffer, Color background) {
    std::size_t count = 0;
    for (const Color& pixel : frame_buffer.pixels) {
        if (pixel.r != background.r || pixel.g != background.g ||
            pixel.b != background.b || pixel.a != background.a) {
            ++count;
        }
    }
    return count;
}

HostFrameBufferView frame_buffer_view(const FrameBuffer& frame_buffer) {
    return HostFrameBufferView{
        frame_buffer.width,
        frame_buffer.height,
        frame_buffer.width,
        frame_buffer.pixels.empty() ? nullptr : frame_buffer.pixels.data(),
    };
}

bool present_frame(const FrameBuffer& frame_buffer,
                   const HostFrameSink& frame_sink,
                   const Rect* dirty_rects,
                   std::size_t dirty_rect_count) {
    if (frame_sink.present == nullptr) {
        return false;
    }
    const HostFrameBufferView view = frame_buffer_view(frame_buffer);
    return frame_sink.present(view, dirty_rects, dirty_rect_count, frame_sink.context);
}

} // namespace jellyframe
