#pragma once

#include "render_core/bitmap_font.h"

#include <cstddef>

namespace jellyframe_esp32s3 {

const jellyframe::BitmapFont& bringup_font();

jellyframe::BitmapFontContext make_bringup_font_context(int scale = 2);

struct AppFontContext {};

struct ProductionFontStats {
    const char* family = "";
    const char* coverage = "";
    std::size_t face_count = 0;
    std::size_t coverage_count = 0;
    std::size_t glyph_count = 0;
    std::size_t bitmap_bytes = 0;
    std::uint8_t bits_per_pixel = 0;
};

AppFontContext make_app_font_context();

const ProductionFontStats& production_font_stats();

jellyframe::TextMeasureProvider make_production_text_measure_provider();

jellyframe::TextPainter make_production_text_painter();

bool app_font_measure_callback(const std::string& text,
                               int font_size,
                               int font_weight,
                               jellyframe::TextMetrics* metrics,
                               void* context);

bool app_font_paint_callback(jellyframe::FrameBuffer& target,
                             jellyframe::Rect rect,
                             jellyframe::Color color,
                             const std::string& text,
                             int font_size,
                             int font_weight,
                             jellyframe::TextCommandAlign align,
                             bool single_line,
                             void* context);

} // namespace jellyframe_esp32s3
