#include "core/software_renderer.h"

#include <algorithm>
#include <array>
#include <cctype>
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

Rect target_rect(const FrameBuffer& target) {
    return Rect{0, 0, target.width, target.height};
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

Color with_opacity(Color color, float opacity) {
    const int alpha = static_cast<int>(static_cast<float>(color.a) * std::max(0.0F, std::min(1.0F, opacity)));
    color.a = clamp_u8(alpha);
    return color;
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

bool inside_rounded_rect(Rect rect, int radius, int x, int y) {
    if (radius <= 0) {
        return true;
    }
    radius = std::min(radius, std::min(rect.width, rect.height) / 2);
    const int left = rect.x;
    const int top = rect.y;
    const int right = rect.x + rect.width - 1;
    const int bottom = rect.y + rect.height - 1;
    int cx = x;
    int cy = y;
    if (x < left + radius && y < top + radius) {
        cx = left + radius;
        cy = top + radius;
    } else if (x > right - radius && y < top + radius) {
        cx = right - radius;
        cy = top + radius;
    } else if (x < left + radius && y > bottom - radius) {
        cx = left + radius;
        cy = bottom - radius;
    } else if (x > right - radius && y > bottom - radius) {
        cx = right - radius;
        cy = bottom - radius;
    } else {
        return true;
    }
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void fill_rect(FrameBuffer& target, Rect rect, Color color, int border_radius = 0) {
    Rect clipped = intersect_rect(rect, target_rect(target));
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
            if (!inside_rounded_rect(rect, border_radius, x, y)) {
                continue;
            }
            blend_pixel(target, x, y, color);
        }
    }
}

void fill_linear_gradient(FrameBuffer& target, Rect rect, Color top, Color bottom, int border_radius = 0) {
    Rect clipped = intersect_rect(rect, target_rect(target));
    if (empty_rect(clipped)) {
        return;
    }
    const int denom = std::max(1, rect.height - 1);
    for (int y = clipped.y; y < clipped.y + clipped.height; ++y) {
        const int t = std::max(0, std::min(255, ((y - rect.y) * 255) / denom));
        Color row{
            clamp_u8((static_cast<int>(top.r) * (255 - t) + static_cast<int>(bottom.r) * t + 127) / 255),
            clamp_u8((static_cast<int>(top.g) * (255 - t) + static_cast<int>(bottom.g) * t + 127) / 255),
            clamp_u8((static_cast<int>(top.b) * (255 - t) + static_cast<int>(bottom.b) * t + 127) / 255),
            clamp_u8((static_cast<int>(top.a) * (255 - t) + static_cast<int>(bottom.a) * t + 127) / 255),
        };
        for (int x = clipped.x; x < clipped.x + clipped.width; ++x) {
            if (!inside_rounded_rect(rect, border_radius, x, y)) {
                continue;
            }
            blend_pixel(target, x, y, row);
        }
    }
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
               TextPainter text_painter) {
    (void)single_line;
    if (color.a == 0 || empty_rect(rect)) {
        return;
    }
    if (text_painter.paint != nullptr &&
        text_painter.paint(target, rect, color, text, font_size, font_weight, align, single_line, text_painter.context)) {
        return;
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

SoftwareRasterizer::SoftwareRasterizer(TextPainter text_painter)
    : text_painter_(text_painter) {}

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
    rect = intersect_rect(rect, clip);
    if (empty_rect(rect)) {
        return;
    }

    switch (command.type) {
    case DisplayCommandType::FillRect:
        fill_rect(target, rect, command.color, command.border_radius);
        break;
    case DisplayCommandType::LinearGradient:
        fill_linear_gradient(target, rect, command.color, command.color2, command.border_radius);
        break;
    case DisplayCommandType::StrokeRect:
        fill_rect(target, Rect{rect.x, rect.y, rect.width, 1}, command.color);
        fill_rect(target, Rect{rect.x, rect.y + rect.height - 1, rect.width, 1}, command.color);
        fill_rect(target, Rect{rect.x, rect.y, 1, rect.height}, command.color);
        fill_rect(target, Rect{rect.x + rect.width - 1, rect.y, 1, rect.height}, command.color);
        break;
    case DisplayCommandType::Text:
        draw_text(target,
                  rect,
                  command.color,
                  command.text,
                  command.font_size,
                  command.font_weight,
                  command.text_align,
                  command.text_single_line,
                  text_painter_);
        break;
    }
}

SoftwareCompositor::SoftwareCompositor(TextPainter text_painter, Options options)
    : rasterizer_(text_painter), options_(options) {}

FrameBuffer SoftwareCompositor::render(const LayerNode& root,
                                       int viewport_width,
                                       int viewport_height,
                                       Color background) const {
    if (!framebuffer_fits_budget(viewport_width, viewport_height, options_)) {
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
    for (std::size_t index = 0; index < dirty_rect_count; ++index) {
        const Rect dirty = intersect_rect(dirty_rects[index], target_rect(target));
        if (empty_rect(dirty)) {
            continue;
        }
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
    Rect layer_clip = clip;
    if (layer.has_clip) {
        Rect translated_clip = layer.clip_rect;
        translated_clip.x += offset_x;
        translated_clip.y += offset_y;
        layer_clip = intersect_rect(layer_clip, translated_clip);
        if (empty_rect(layer_clip)) {
            return;
        }
    }

    const float layer_opacity = inherited_opacity * layer.opacity;
    const bool needs_offscreen = layer.type == LayerType::Composited || layer.opacity < 0.999F;
    if (needs_offscreen) {
        Rect bounds = layer.bounds;
        bounds.x += offset_x;
        bounds.y += offset_y;
        bounds = intersect_rect(bounds, layer_clip);
        bounds = intersect_rect(bounds, target_rect(target));
        if (empty_rect(bounds)) {
            return;
        }
        if (!offscreen_fits_budget(bounds, options_)) {
            rasterize_with_opacity(rasterizer_,
                                   layer.display_list,
                                   target,
                                   layer_clip,
                                   offset_x,
                                   offset_y,
                                   layer_opacity);
            for (const auto& child : layer.children) {
                composite_layer(*child, target, layer_clip, offset_x, offset_y, layer_opacity);
            }
            return;
        }

        FrameBuffer offscreen(bounds.width, bounds.height, Color{0, 0, 0, 0});
        const int child_offset_x = offset_x - bounds.x;
        const int child_offset_y = offset_y - bounds.y;
        const Rect offscreen_clip{0, 0, bounds.width, bounds.height};
        rasterizer_.rasterize(layer.display_list, offscreen, offscreen_clip, child_offset_x, child_offset_y);
        for (const auto& child : layer.children) {
            composite_layer(*child, offscreen, offscreen_clip, child_offset_x, child_offset_y, 1.0F);
        }
        composite_buffer(target, offscreen, bounds.x, bounds.y, layer_opacity);
        return;
    }

    rasterize_with_opacity(rasterizer_, layer.display_list, target, layer_clip, offset_x, offset_y, layer_opacity);
    for (const auto& child : layer.children) {
        composite_layer(*child, target, layer_clip, offset_x, offset_y, layer_opacity);
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
