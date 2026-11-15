#pragma once

#include "render_core/bitmap_font.h"

#include <cstddef>

#ifndef JELLYFRAME_ESP32S3_LEGACY_BITMAP_FONT_ENABLED
#define JELLYFRAME_ESP32S3_LEGACY_BITMAP_FONT_ENABLED 1
#endif

namespace jellyframe_esp32s3 {

#if JELLYFRAME_ESP32S3_LEGACY_BITMAP_FONT_ENABLED
const jellyframe::BitmapFont& bringup_font();

jellyframe::BitmapFontContext make_bringup_font_context(int scale = 2);

struct LegacyBitmapFontContext {};

struct LegacyBitmapFontStats {
    const char* family = "";
    const char* coverage = "";
    std::size_t face_count = 0;
    std::size_t coverage_count = 0;
    std::size_t glyph_count = 0;
    std::size_t bitmap_bytes = 0;
    std::uint8_t bits_per_pixel = 0;
};

LegacyBitmapFontContext make_legacy_bitmap_font_context();

const LegacyBitmapFontStats& legacy_bitmap_font_stats();

jellyframe::TextMeasureProvider make_legacy_bitmap_text_measure_provider();

jellyframe::TextPainter make_legacy_bitmap_text_painter();

bool legacy_app_font_measure_callback(const std::string& text,
                                      int font_size,
                                      int font_weight,
                                      jellyframe::TextMetrics* metrics,
                                      void* context);

bool legacy_app_font_measure_range_callback(const char* data,
                                            std::size_t length,
                                            int font_size,
                                            int font_weight,
                                            jellyframe::TextMetrics* metrics,
                                            void* context);

bool legacy_app_font_paint_callback(jellyframe::FrameBuffer& target,
                                    jellyframe::Rect rect,
                                    jellyframe::Color color,
                                    const std::string& text,
                                    int font_size,
                                    int font_weight,
                                    jellyframe::TextCommandAlign align,
                                    bool single_line,
                                    void* context);
#endif

inline jellyframe::TextMeasureProvider legacy_bitmap_text_measure_provider() {
#if JELLYFRAME_ESP32S3_LEGACY_BITMAP_FONT_ENABLED
    return make_legacy_bitmap_text_measure_provider();
#else
    return {};
#endif
}

inline jellyframe::TextPainter legacy_bitmap_text_painter() {
#if JELLYFRAME_ESP32S3_LEGACY_BITMAP_FONT_ENABLED
    return make_legacy_bitmap_text_painter();
#else
    return {};
#endif
}

} // namespace jellyframe_esp32s3
