#include "core/bitmap_font.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace jellyframe {
namespace {

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
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

std::uint32_t consume_utf8_codepoint(const std::string& text, std::size_t& index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    std::uint32_t codepoint = lead;
    std::size_t width = 1;
    if ((lead & 0xe0U) == 0xc0U && index + 1 < text.size()) {
        width = 2;
        codepoint = ((lead & 0x1fU) << 6U) |
            (static_cast<unsigned char>(text[index + 1]) & 0x3fU);
    } else if ((lead & 0xf0U) == 0xe0U && index + 2 < text.size()) {
        width = 3;
        codepoint = ((lead & 0x0fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 2]) & 0x3fU);
    } else if ((lead & 0xf8U) == 0xf0U && index + 3 < text.size()) {
        width = 4;
        codepoint = ((lead & 0x07U) << 18U) |
            ((static_cast<unsigned char>(text[index + 1]) & 0x3fU) << 12U) |
            ((static_cast<unsigned char>(text[index + 2]) & 0x3fU) << 6U) |
            (static_cast<unsigned char>(text[index + 3]) & 0x3fU);
    }
    index += std::min(width, text.size() - index);
    return codepoint;
}

int context_scale(const BitmapFontContext& context) {
    return std::max(1, context.scale);
}

int glyph_advance(const BitmapFont& font, const BitmapFontGlyph* glyph) {
    if (glyph != nullptr) {
        return glyph->advance > 0 ? glyph->advance : glyph->width;
    }
    return std::max(1, static_cast<int>(font.fallback_advance));
}

int glyph_bytes_per_row(const BitmapFontGlyph& glyph) {
    return glyph.bytes_per_row > 0 ? glyph.bytes_per_row : (glyph.width + 7) / 8;
}

void draw_glyph(FrameBuffer& target,
                int x,
                int y,
                Color color,
                const BitmapFontGlyph& glyph,
                int scale,
                int stroke_passes) {
    if (glyph.rows == nullptr || glyph.width == 0 || glyph.height == 0) {
        return;
    }
    const int bytes_per_row = glyph_bytes_per_row(glyph);
    for (int row = 0; row < glyph.height; ++row) {
        for (int col = 0; col < glyph.width; ++col) {
            const std::uint8_t bits =
                glyph.rows[static_cast<std::size_t>(row * bytes_per_row + col / 8)];
            if ((bits & (1U << (7 - (col % 8)))) == 0U) {
                continue;
            }
            for (int pass = 0; pass < stroke_passes; ++pass) {
                for (int py = 0; py < scale; ++py) {
                    for (int px = 0; px < scale; ++px) {
                        blend_pixel(target, x + col * scale + px + pass, y + row * scale + py, color);
                    }
                }
            }
        }
    }
}

} // namespace

const BitmapFontGlyph* find_bitmap_glyph(const BitmapFont& font, std::uint32_t codepoint) {
    if (font.glyphs == nullptr) {
        return nullptr;
    }
    for (std::size_t index = 0; index < font.glyph_count; ++index) {
        if (font.glyphs[index].codepoint == codepoint) {
            return &font.glyphs[index];
        }
    }
    return nullptr;
}

TextMetrics measure_bitmap_text(const BitmapFontContext& context,
                                const std::string& text,
                                int font_size,
                                int font_weight) {
    (void)font_size;
    if (context.font == nullptr) {
        return fallback_text_metrics(text, font_size, font_weight);
    }
    const BitmapFont& font = *context.font;
    const int scale = context_scale(context);
    int width = 0;
    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        width += glyph_advance(font, find_bitmap_glyph(font, codepoint)) * scale;
    }
    return TextMetrics{
        width,
        std::max(1, static_cast<int>(font.line_height)) * scale,
    };
}

bool bitmap_font_measure_callback(const std::string& text,
                                  int font_size,
                                  int font_weight,
                                  TextMetrics* metrics,
                                  void* context) {
    if (metrics == nullptr || context == nullptr) {
        return false;
    }
    *metrics = measure_bitmap_text(*static_cast<const BitmapFontContext*>(context), text, font_size, font_weight);
    return true;
}

bool bitmap_font_paint_callback(FrameBuffer& target,
                                Rect rect,
                                Color color,
                                const std::string& text,
                                int font_size,
                                int font_weight,
                                TextCommandAlign align,
                                bool single_line,
                                void* context) {
    (void)single_line;
    if (context == nullptr || color.a == 0 || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const auto& font_context = *static_cast<const BitmapFontContext*>(context);
    if (font_context.font == nullptr) {
        return false;
    }
    const BitmapFont& font = *font_context.font;
    const int scale = context_scale(font_context);
    const TextMetrics metrics = measure_bitmap_text(font_context, text, font_size, font_weight);
    int cursor_x = rect.x;
    if (align == TextCommandAlign::Center) {
        cursor_x += std::max(0, (rect.width - metrics.width) / 2);
    } else if (align == TextCommandAlign::End) {
        cursor_x += std::max(0, rect.width - metrics.width);
    }
    const int glyph_height = std::max(1, static_cast<int>(font.line_height)) * scale;
    const int cursor_y = rect.y + std::max(0, (rect.height - glyph_height) / 2);
    const int stroke_passes = font_weight >= 600 ? 2 : 1;

    for (std::size_t index = 0; index < text.size();) {
        const std::uint32_t codepoint = consume_utf8_codepoint(text, index);
        const BitmapFontGlyph* glyph = find_bitmap_glyph(font, codepoint);
        if (glyph != nullptr) {
            draw_glyph(target, cursor_x, cursor_y, color, *glyph, scale, stroke_passes);
        }
        cursor_x += glyph_advance(font, glyph) * scale;
        if (cursor_x >= rect.x + rect.width) {
            break;
        }
    }
    return true;
}

} // namespace jellyframe
