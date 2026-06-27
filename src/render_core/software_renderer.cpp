#include "render_core/software_renderer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace jellyframe {
namespace {

Rect intersect_rect(Rect left, Rect right) {
    const int x1 = std::max(left.x, right.x);
    const int y1 = std::max(left.y, right.y);
    const int x2 = std::min(left.x + left.width, right.x + right.width);
    const int y2 = std::min(left.y + left.height, right.y + right.height);
    if (x2 <= x1 || y2 <= y1) {
        return Rect{x1, y1, 0, 0};
    }
    return Rect{x1, y1, x2 - x1, y2 - y1};
}

bool empty_rect(Rect rect) {
    return rect.width <= 0 || rect.height <= 0;
}

bool contains_rect(Rect outer, Rect inner) {
    return !empty_rect(inner) &&
        inner.x >= outer.x &&
        inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
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
    return normalized;
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = clamp_u8(alpha);
    return color;
}

Color with_coverage(Color color, int coverage) {
    if (coverage >= 255) {
        return color;
    }
    if (coverage <= 0) {
        color.a = 0;
        return color;
    }
    color.a = clamp_u8((static_cast<int>(color.a) * coverage + 127) / 255);
    return color;
}

Color lerp_color_255(Color first, Color second, int t) {
    t = std::max(0, std::min(255, t));
    return Color{
        clamp_u8((static_cast<int>(first.r) * (255 - t) + static_cast<int>(second.r) * t + 127) / 255),
        clamp_u8((static_cast<int>(first.g) * (255 - t) + static_cast<int>(second.g) * t + 127) / 255),
        clamp_u8((static_cast<int>(first.b) * (255 - t) + static_cast<int>(second.b) * t + 127) / 255),
        clamp_u8((static_cast<int>(first.a) * (255 - t) + static_cast<int>(second.a) * t + 127) / 255),
    };
}

void blend_pixel(FrameBuffer& target, int x, int y, Color source) {
    if (!target.contains(x, y) || source.a == 0) {
        return;
    }
    Color& destination = target.pixel(x, y);
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
        return clamp_u8((premul + out_a / 2) / out_a);
    };

    destination = Color{
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        clamp_u8(out_a),
    };
}

Rect clipped_target_rect(const FrameBuffer& target, Rect rect) {
    return intersect_rect(rect, target_rect(target));
}

Rect clipped_target_rect(const FrameBuffer& target, Rect rect, Rect clip) {
    return intersect_rect(clipped_target_rect(target, rect), clip);
}

int rounded_rect_coverage(Rect rect, int radius, int x, int y) {
    if (radius <= 0) {
        return 255;
    }
    radius = std::min(radius, std::min(rect.width, rect.height) / 2);
    if (radius <= 0) {
        return 255;
    }

    const int left = rect.x;
    const int top = rect.y;
    const int right = rect.x + rect.width;
    const int bottom = rect.y + rect.height;
    int cx = 0;
    int cy = 0;
    if (x < left + radius && y < top + radius) {
        cx = left + radius;
        cy = top + radius;
    } else if (x >= right - radius && y < top + radius) {
        cx = right - radius;
        cy = top + radius;
    } else if (x < left + radius && y >= bottom - radius) {
        cx = left + radius;
        cy = bottom - radius;
    } else if (x >= right - radius && y >= bottom - radius) {
        cx = right - radius;
        cy = bottom - radius;
    } else {
        return 255;
    }

    constexpr int kSubpixel = 4;
    const int center_x = cx * kSubpixel;
    const int center_y = cy * kSubpixel;
    const int radius_scaled = radius * kSubpixel;
    const int radius_squared = radius_scaled * radius_scaled;
    int covered = 0;
    for (int sy = 0; sy < kSubpixel; ++sy) {
        const int sample_y = y * kSubpixel + sy;
        const int dy = sample_y - center_y;
        for (int sx = 0; sx < kSubpixel; ++sx) {
            const int sample_x = x * kSubpixel + sx;
            const int dx = sample_x - center_x;
            if (dx * dx + dy * dy <= radius_squared) {
                ++covered;
            }
        }
    }
    return (covered * 255 + 8) / 16;
}

void fill_rect(FrameBuffer& target, Rect rect, Color color, int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect);
    if (empty_rect(clipped) || color.a == 0) {
        return;
    }
    if (color.a == 255 && border_radius <= 0) {
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            Color* row = target.pixels.data() + static_cast<std::size_t>(y * target.width + clipped.x);
            std::fill(row, row + clipped.width, color);
        }
        return;
    }
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

void fill_rect_clipped(FrameBuffer& target, Rect rect, Rect clip, Color color, int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect, clip);
    if (empty_rect(clipped) || color.a == 0) {
        return;
    }
    if (color.a == 255 && border_radius <= 0) {
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            Color* row = target.pixels.data() + static_cast<std::size_t>(y * target.width + clipped.x);
            std::fill(row, row + clipped.width, color);
        }
        return;
    }
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
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
    if (border_radius <= 0) {
        fill_rect(target, Rect{rect.x, rect.y, rect.width, stroke_width}, color);
        fill_rect(target, Rect{rect.x, rect.y + rect.height - stroke_width, rect.width, stroke_width}, color);
        fill_rect(target, Rect{rect.x, rect.y, stroke_width, rect.height}, color);
        fill_rect(target, Rect{rect.x + rect.width - stroke_width, rect.y, stroke_width, rect.height}, color);
        return;
    }

    const Rect inner{
        rect.x + stroke_width,
        rect.y + stroke_width,
        std::max(0, rect.width - stroke_width * 2),
        std::max(0, rect.height - stroke_width * 2),
    };
    const int inner_radius = std::max(0, border_radius - stroke_width);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int outer_coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (outer_coverage <= 0) {
                continue;
            }
            const int inner_coverage = empty_rect(inner) ? 0 : rounded_rect_coverage(inner, inner_radius, x, y);
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
    if (border_radius <= 0) {
        fill_rect_clipped(target, Rect{rect.x, rect.y, rect.width, stroke_width}, clip, color);
        fill_rect_clipped(target, Rect{rect.x, rect.y + rect.height - stroke_width, rect.width, stroke_width}, clip, color);
        fill_rect_clipped(target, Rect{rect.x, rect.y, stroke_width, rect.height}, clip, color);
        fill_rect_clipped(target, Rect{rect.x + rect.width - stroke_width, rect.y, stroke_width, rect.height}, clip, color);
        return;
    }

    const Rect inner{
        rect.x + stroke_width,
        rect.y + stroke_width,
        std::max(0, rect.width - stroke_width * 2),
        std::max(0, rect.height - stroke_width * 2),
    };
    const int inner_radius = std::max(0, border_radius - stroke_width);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int outer_coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (outer_coverage <= 0) {
                continue;
            }
            const int inner_coverage = empty_rect(inner) ? 0 : rounded_rect_coverage(inner, inner_radius, x, y);
            const int stroke_coverage = std::max(0, outer_coverage - inner_coverage);
            if (stroke_coverage <= 0) {
                continue;
            }
            blend_pixel(target, x, y, with_coverage(color, stroke_coverage));
        }
    }
}

void fill_linear_gradient(FrameBuffer& target,
                          Rect rect,
                          Color first,
                          Color second,
                          GradientAxis axis,
                          int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect);
    if (empty_rect(clipped)) {
        return;
    }
    if (axis == GradientAxis::Vertical) {
        const int denom = std::max(1, rect.height - 1);
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            const Color row = lerp_color_255(first, second, ((y - rect.y) * 255) / denom);
            for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
                const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
                if (coverage <= 0) {
                    continue;
                }
                blend_pixel(target, x, y, with_coverage(row, coverage));
            }
        }
        return;
    }

    const int denom = std::max(1, rect.width - 1);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (coverage <= 0) {
                continue;
            }
            const Color color = lerp_color_255(first, second, ((x - rect.x) * 255) / denom);
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

void fill_linear_gradient_clipped(FrameBuffer& target,
                                  Rect rect,
                                  Rect clip,
                                  Color first,
                                  Color second,
                                  GradientAxis axis,
                                  int border_radius = 0) {
    Rect clipped = clipped_target_rect(target, rect, clip);
    if (empty_rect(clipped)) {
        return;
    }
    if (axis == GradientAxis::Vertical) {
        const int denom = std::max(1, rect.height - 1);
        for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
            const Color row = lerp_color_255(first, second, ((y - rect.y) * 255) / denom);
            for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
                const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
                if (coverage <= 0) {
                    continue;
                }
                blend_pixel(target, x, y, with_coverage(row, coverage));
            }
        }
        return;
    }

    const int denom = std::max(1, rect.width - 1);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (coverage <= 0) {
                continue;
            }
            const Color color = lerp_color_255(first, second, ((x - rect.x) * 255) / denom);
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
}

int conic_percent_from_top_clockwise(int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return 0;
    }
    if (dy < 0) {
        const int up = -dy;
        if (dx >= 0) {
            return (dx * 25) / std::max(1, dx + up);
        }
        const int left = -dx;
        return 75 + (up * 25) / std::max(1, left + up);
    }
    if (dx >= 0) {
        return 25 + (dy * 25) / std::max(1, dx + dy);
    }
    const int left = -dx;
    return 50 + (left * 25) / std::max(1, left + dy);
}

void fill_conic_gradient_region(FrameBuffer& target,
                                Rect rect,
                                Rect clipped,
                                Color first,
                                Color second,
                                int stop_percent,
                                int border_radius) {
    if (empty_rect(clipped)) {
        return;
    }
    stop_percent = std::max(0, std::min(100, stop_percent));
    const int center_x2 = rect.x * 2 + rect.width;
    const int center_y2 = rect.y * 2 + rect.height;
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            const int coverage = rounded_rect_coverage(rect, border_radius, x, y);
            if (coverage <= 0) {
                continue;
            }
            const int dx = x * 2 + 1 - center_x2;
            const int dy = y * 2 + 1 - center_y2;
            const Color color = conic_percent_from_top_clockwise(dx, dy) < stop_percent ? first : second;
            blend_pixel(target, x, y, with_coverage(color, coverage));
        }
    }
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
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::size_t width = 1;
    char glyph = static_cast<char>(lead);
    if (lead >= 0x80U) {
        glyph = '?';
        if ((lead & 0xe0U) == 0xc0U) {
            width = 2;
        } else if ((lead & 0xf0U) == 0xe0U) {
            width = 3;
        } else if ((lead & 0xf8U) == 0xf0U) {
            width = 4;
        }
    }
    index += std::min(width, text.size() - index);
    return glyph;
}

void draw_text(FrameBuffer& target,
               Rect rect,
               Color color,
               const std::string& text,
               int font_size,
               int font_weight,
               TextCommandAlign align,
               bool single_line,
               TextPainter text_painter,
               DiagnosticSink* diagnostics) {
    (void)single_line;
    if (color.a == 0 || empty_rect(rect)) {
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
        if (cursor_x + glyph_width > rect.x + rect.width) {
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

void composite_buffer(FrameBuffer& target, const FrameBuffer& source, int dst_x, int dst_y, float opacity) {
    const Rect target_bounds = target_rect(target);
    Rect copy_rect = intersect_rect(Rect{dst_x, dst_y, source.width, source.height}, target_bounds);
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

Color lerp_color_fixed(Color left, Color right, int t256) {
    return Color{
        clamp_u8((static_cast<int>(left.r) * (256 - t256) + static_cast<int>(right.r) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.g) * (256 - t256) + static_cast<int>(right.g) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.b) * (256 - t256) + static_cast<int>(right.b) * t256 + 128) >> 8),
        clamp_u8((static_cast<int>(left.a) * (256 - t256) + static_cast<int>(right.a) * t256 + 128) >> 8),
    };
}

Color sample_bilinear(const FrameBuffer& source, int local_x, int local_y, int dst_width, int dst_height) {
    if (source.width <= 1 || source.height <= 1 || dst_width <= 1 || dst_height <= 1) {
        const int src_x = std::min(source.width - 1, (local_x * source.width) / std::max(1, dst_width));
        const int src_y = std::min(source.height - 1, (local_y * source.height) / std::max(1, dst_height));
        return source.pixel(src_x, src_y);
    }

    const int fx = (local_x * (source.width - 1) * 256) / std::max(1, dst_width - 1);
    const int fy = (local_y * (source.height - 1) * 256) / std::max(1, dst_height - 1);
    const int base_x = std::min(source.width - 1, fx >> 8);
    const int base_y = std::min(source.height - 1, fy >> 8);
    const int next_x = std::min(source.width - 1, base_x + 1);
    const int next_y = std::min(source.height - 1, base_y + 1);
    const int tx = fx & 0xff;
    const int ty = fy & 0xff;

    const Color top = lerp_color_fixed(source.pixel(base_x, base_y), source.pixel(next_x, base_y), tx);
    const Color bottom = lerp_color_fixed(source.pixel(base_x, next_y), source.pixel(next_x, next_y), tx);
    return lerp_color_fixed(top, bottom, ty);
}

void composite_scaled_buffer(FrameBuffer& target,
                             const FrameBuffer& source,
                             Rect destination,
                             float opacity,
                             bool smooth) {
    const Rect target_bounds = target_rect(target);
    Rect copy_rect = intersect_rect(destination, target_bounds);
    if (empty_rect(copy_rect) || source.width <= 0 || source.height <= 0) {
        return;
    }
    for (int y = 0; y < copy_rect.height; ++y) {
        for (int x = 0; x < copy_rect.width; ++x) {
            const int local_x = copy_rect.x + x - destination.x;
            const int local_y = copy_rect.y + y - destination.y;
            Color source_pixel;
            if (smooth) {
                source_pixel = sample_bilinear(source, local_x, local_y, destination.width, destination.height);
            } else {
                const int src_y = std::min(source.height - 1,
                                           (local_y * source.height) / std::max(1, destination.height));
                const int src_x = std::min(source.width - 1,
                                           (local_x * source.width) / std::max(1, destination.width));
                source_pixel = source.pixel(src_x, src_y);
            }
            blend_pixel(target,
                        copy_rect.x + x,
                        copy_rect.y + y,
                        with_opacity(source_pixel, opacity));
        }
    }
}

Rect transformed_destination_rect(Rect source_rect,
                                  const Transform2D& transform,
                                  int origin_x_percent,
                                  int origin_y_percent) {
    const float scaled_width = std::max(1.0F, static_cast<float>(source_rect.width) * transform.scale_x);
    const float scaled_height = std::max(1.0F, static_cast<float>(source_rect.height) * transform.scale_y);
    const float origin_x = static_cast<float>(source_rect.x) +
        static_cast<float>(source_rect.width) * static_cast<float>(origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(source_rect.y) +
        static_cast<float>(source_rect.height) * static_cast<float>(origin_y_percent) / 100.0F;
    const float left = origin_x -
        scaled_width * static_cast<float>(origin_x_percent) / 100.0F;
    const float top = origin_y -
        scaled_height * static_cast<float>(origin_y_percent) / 100.0F;

    constexpr float kPi = 3.14159265358979323846F;
    const float radians = transform.rotate_degrees * kPi / 180.0F;
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    const float corners[4][2] = {
        {left, top},
        {left + scaled_width, top},
        {left, top + scaled_height},
        {left + scaled_width, top + scaled_height},
    };
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;
    for (int index = 0; index < 4; ++index) {
        const float dx = corners[index][0] - origin_x;
        const float dy = corners[index][1] - origin_y;
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

    const float origin_x = static_cast<float>(source_rect.x) +
        static_cast<float>(source_rect.width) * static_cast<float>(origin_x_percent) / 100.0F;
    const float origin_y = static_cast<float>(source_rect.y) +
        static_cast<float>(source_rect.height) * static_cast<float>(origin_y_percent) / 100.0F;
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

bool offscreen_fits_budget(Rect bounds, SoftwareCompositor::Options options) {
    if (bounds.width <= 0 || bounds.height <= 0) {
        return false;
    }
    if (options.max_offscreen_pixels == 0) {
        return true;
    }
    const std::size_t pixels = static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height);
    return pixels <= options.max_offscreen_pixels;
}

bool framebuffer_fits_budget(int width, int height, SoftwareCompositor::Options options) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (options.max_framebuffer_pixels == 0) {
        return true;
    }
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return pixels <= options.max_framebuffer_pixels;
}

void rasterize_with_opacity(const SoftwareRasterizer& rasterizer,
                            const DisplayList& display_list,
                            FrameBuffer& target,
                            Rect clip,
                            int offset_x,
                            int offset_y,
                            float opacity) {
    if (opacity >= 0.999F) {
        rasterizer.rasterize(display_list, target, clip, offset_x, offset_y);
        return;
    }
    for (const DisplayCommand& source : display_list) {
        DisplayCommand command = source;
        command.color = with_opacity(command.color, opacity);
        command.color2 = with_opacity(command.color2, opacity);
        rasterizer.rasterize(command, target, clip, offset_x, offset_y);
    }
}

} // namespace

FrameBuffer::FrameBuffer(int width_in, int height_in, Color clear_color) {
    resize(width_in, height_in, clear_color);
}

void FrameBuffer::resize(int new_width, int new_height, Color clear_color) {
    width = std::max(0, new_width);
    height = std::max(0, new_height);
    const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels.assign(pixel_count, clear_color);
}

void FrameBuffer::clear(Color clear_color) {
    std::fill(pixels.begin(), pixels.end(), clear_color);
}

bool FrameBuffer::contains(int x, int y) const {
    return x >= 0 && y >= 0 && x < width && y < height;
}

Color& FrameBuffer::pixel(int x, int y) {
    return pixels[static_cast<std::size_t>(y * width + x)];
}

const Color& FrameBuffer::pixel(int x, int y) const {
    return pixels[static_cast<std::size_t>(y * width + x)];
}

SoftwareRasterizer::SoftwareRasterizer(TextPainter text_painter, DiagnosticSink* diagnostics)
    : SoftwareRasterizer(text_painter, {}, diagnostics) {}

SoftwareRasterizer::SoftwareRasterizer(TextPainter text_painter,
                                       ImagePainter image_painter,
                                       DiagnosticSink* diagnostics)
    : text_painter_(text_painter), image_painter_(image_painter), diagnostics_(diagnostics) {}

void SoftwareRasterizer::rasterize(const DisplayList& display_list,
                                   FrameBuffer& target,
                                   Rect clip,
                                   int offset_x,
                                   int offset_y) const {
    for (const DisplayCommand& command : display_list) {
        rasterize(command, target, clip, offset_x, offset_y);
    }
}

void SoftwareRasterizer::rasterize(const DisplayCommand& command,
                                   FrameBuffer& target,
                                   Rect clip,
                                   int offset_x,
                                   int offset_y) const {
    Rect rect = command.rect;
    rect.x += offset_x;
    rect.y += offset_y;
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
    case DisplayCommandType::ConicGradient:
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
                      command.text_align,
                      command.text_single_line,
                      text_painter_,
                      diagnostics_);
            break;
        }
        FrameBuffer text_buffer(rect.width, rect.height, Color{0, 0, 0, 0});
        draw_text(text_buffer,
                  Rect{0, 0, rect.width, rect.height},
                  command.color,
                  command.text,
                  command.font_size,
                  command.font_weight,
                  command.text_align,
                  command.text_single_line,
                  text_painter_,
                  diagnostics_);
        composite_buffer_clipped(target, text_buffer, rect.x, rect.y, clip, 1.0F);
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
        FrameBuffer image_buffer(rect.width, rect.height, Color{0, 0, 0, 0});
        if (!image_painter_.paint(image_buffer,
                                  Rect{0, 0, rect.width, rect.height},
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
            fill_rect(image_buffer, Rect{0, 0, rect.width, rect.height}, Color{226, 232, 240, 255});
        }
        composite_buffer_clipped(target, image_buffer, rect.x, rect.y, clip, 1.0F);
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
    : rasterizer_(text_painter, image_painter, options.diagnostics), options_(options) {}

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
    render_into(root, target, background, nullptr, 0);
}

void SoftwareCompositor::render_into(const LayerNode& root,
                                     FrameBuffer& target,
                                     Color background,
                                     const Rect* dirty_rects,
                                     std::size_t dirty_rect_count) const {
    if (target.width <= 0 || target.height <= 0) {
        return;
    }
    if (dirty_rects == nullptr || dirty_rect_count == 0) {
        target.clear(background);
        composite_layer(root, target, Rect{0, 0, target.width, target.height}, 0, 0, 1.0F);
        return;
    }
    if (dirty_rect_count == 1) {
        const Rect dirty = intersect_rect(dirty_rects[0], target_rect(target));
        if (!empty_rect(dirty)) {
            fill_rect(target, dirty, background);
            composite_layer(root, target, dirty, 0, 0, 1.0F);
        }
        return;
    }
    const std::vector<Rect> normalized_dirty_rects =
        normalize_dirty_rects(dirty_rects, dirty_rect_count, target_rect(target));
    for (const Rect dirty : normalized_dirty_rects) {
        fill_rect(target, dirty, background);
        composite_layer(root, target, dirty, 0, 0, 1.0F);
    }
}

void SoftwareCompositor::composite_layer(const LayerNode& layer,
                                         FrameBuffer& target,
                                         Rect clip,
                                         int offset_x,
                                         int offset_y,
                                         float inherited_opacity) const {
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
    const bool needs_offscreen = layer.type == LayerType::Composited || layer.opacity < 0.999F;
    if (needs_offscreen) {
        Rect source_bounds = layer.bounds;
        source_bounds.x += layer_offset_x;
        source_bounds.y += layer_offset_y;
        const bool has_scale_or_rotate =
            std::abs(layer.transform.scale_x - 1.0F) >= 0.001F ||
            std::abs(layer.transform.scale_y - 1.0F) >= 0.001F ||
            std::abs(layer.transform.rotate_degrees) >= 0.001F;
        const Rect destination = has_scale_or_rotate
            ? transformed_destination_rect(source_bounds,
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
        if (!offscreen_fits_budget(offscreen_bounds, options_)) {
            report_diagnostic(options_.diagnostics,
                              DiagnosticStage::Paint,
                              DiagnosticSeverity::Warning,
                              "paint-offscreen-budget",
                              "Offscreen compositing buffer exceeded budget; layer was painted by direct opacity fallback",
                              std::to_string(offscreen_bounds.width) + "x" + std::to_string(offscreen_bounds.height));
            rasterize_with_opacity(rasterizer_,
                                   layer.display_list,
                                   target,
                                   layer_clip,
                                   layer_offset_x,
                                   layer_offset_y,
                                   layer_opacity);
            for (const auto& child : layer.children) {
                composite_layer(*child, target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity);
            }
            return;
        }

        FrameBuffer offscreen(offscreen_bounds.width, offscreen_bounds.height, Color{0, 0, 0, 0});
        const int child_offset_x = layer_offset_x - offscreen_bounds.x;
        const int child_offset_y = layer_offset_y - offscreen_bounds.y;
        const Rect offscreen_clip{0, 0, offscreen_bounds.width, offscreen_bounds.height};
        rasterizer_.rasterize(layer.display_list, offscreen, offscreen_clip, child_offset_x, child_offset_y);
        for (const auto& child : layer.children) {
            composite_layer(*child, offscreen, offscreen_clip, child_offset_x, child_offset_y, 1.0F);
        }
        if (has_scale_or_rotate) {
            composite_transformed_buffer(target,
                                         offscreen,
                                         offscreen_bounds,
                                         destination,
                                         layer.transform,
                                         layer.transform_origin_x_percent,
                                         layer.transform_origin_y_percent,
                                         layer_clip,
                                         layer_opacity,
                                         options_.smooth_scaled_layers);
        } else {
            composite_buffer(target, offscreen, offscreen_bounds.x, offscreen_bounds.y, layer_opacity);
        }
        return;
    }

    rasterize_with_opacity(rasterizer_, layer.display_list, target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity);
    for (const auto& child : layer.children) {
        composite_layer(*child, target, layer_clip, layer_offset_x, layer_offset_y, layer_opacity);
    }
}

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
