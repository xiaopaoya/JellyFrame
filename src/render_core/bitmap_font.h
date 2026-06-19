#pragma once

#include "render_core/geometry.h"
#include "render_core/software_renderer.h"
#include "render_core/text_backend.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace jellyframe {

struct BitmapFontGlyph {
    std::uint32_t codepoint = 0;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    std::uint8_t advance = 0;
    std::uint8_t bytes_per_row = 0;
    const std::uint8_t* rows = nullptr;
};

struct BitmapFont {
    // Glyphs must be sorted by ascending Unicode codepoint; lookup is binary.
    const BitmapFontGlyph* glyphs = nullptr;
    std::size_t glyph_count = 0;
    std::uint8_t line_height = 0;
    std::uint8_t fallback_advance = 0;
};

struct BitmapFontContext {
    const BitmapFont* font = nullptr;
    int scale = 1;
};

const BitmapFontGlyph* find_bitmap_glyph(const BitmapFont& font, std::uint32_t codepoint);
TextMetrics measure_bitmap_text(const BitmapFontContext& context,
                                const std::string& text,
                                int font_size,
                                int font_weight);

bool bitmap_font_measure_callback(const std::string& text,
                                  int font_size,
                                  int font_weight,
                                  TextMetrics* metrics,
                                  void* context);

bool bitmap_font_paint_callback(FrameBuffer& target,
                                Rect rect,
                                Color color,
                                const std::string& text,
                                int font_size,
                                int font_weight,
                                TextCommandAlign align,
                                bool single_line,
                                void* context);

} // namespace jellyframe
