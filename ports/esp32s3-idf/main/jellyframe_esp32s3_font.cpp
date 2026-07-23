#include "jellyframe_esp32s3_font.h"

#include "jellyframe_esp32s3_noto_sans_sc_font.h"

#include <cstdint>
#include <cstdlib>

namespace jellyframe_esp32s3 {
namespace {

constexpr std::uint8_t kGlyphA[] = {
    0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
};
constexpr std::uint8_t kGlyphB[] = {
    0xf0, 0x88, 0x88, 0xf0, 0x88, 0x88, 0xf0,
};
constexpr std::uint8_t kGlyphC[] = {
    0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70,
};
constexpr std::uint8_t kGlyphM[] = {
    0x88, 0xd8, 0xa8, 0xa8, 0x88, 0x88, 0x88,
};
constexpr std::uint8_t kGlyphO[] = {
    0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70,
};
constexpr std::uint8_t kGlyphP[] = {
    0xf0, 0x88, 0x88, 0xf0, 0x80, 0x80, 0x80,
};
constexpr std::uint8_t kGlyphS[] = {
    0x78, 0x80, 0x80, 0x70, 0x08, 0x08, 0xf0,
};
constexpr std::uint8_t kGlyphT[] = {
    0xf8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
};
constexpr std::uint8_t kGlyphU[] = {
    0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70,
};
constexpr std::uint8_t kGlyph0[] = {
    0x70, 0x88, 0x98, 0xa8, 0xc8, 0x88, 0x70,
};
constexpr std::uint8_t kGlyph1[] = {
    0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70,
};
constexpr std::uint8_t kGlyph2[] = {
    0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xf8,
};
constexpr std::uint8_t kGlyph3[] = {
    0xf0, 0x08, 0x08, 0x70, 0x08, 0x08, 0xf0,
};
constexpr std::uint8_t kGlyph4[] = {
    0x10, 0x30, 0x50, 0x90, 0xf8, 0x10, 0x10,
};
constexpr std::uint8_t kGlyph5[] = {
    0xf8, 0x80, 0x80, 0xf0, 0x08, 0x08, 0xf0,
};
constexpr std::uint8_t kGlyph6[] = {
    0x70, 0x80, 0x80, 0xf0, 0x88, 0x88, 0x70,
};
constexpr std::uint8_t kGlyph7[] = {
    0xf8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40,
};
constexpr std::uint8_t kGlyph8[] = {
    0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70,
};
constexpr std::uint8_t kGlyph9[] = {
    0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70,
};
constexpr std::uint8_t kGlyphSpace[] = {0x00};
constexpr std::uint8_t kGlyphColon[] = {
    0x00, 0x40, 0x40, 0x00, 0x00, 0x40, 0x40,
};
constexpr std::uint8_t kGlyphCjkMiddle[] = {
    0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10,
};

constexpr jellyframe::BitmapFontGlyph kGlyphs[] = {
    {0x20, 3, 1, 4, 1, kGlyphSpace},
    {0x30, 5, 7, 6, 1, kGlyph0}, {0x31, 5, 7, 6, 1, kGlyph1},
    {0x32, 5, 7, 6, 1, kGlyph2}, {0x33, 5, 7, 6, 1, kGlyph3},
    {0x34, 5, 7, 6, 1, kGlyph4}, {0x35, 5, 7, 6, 1, kGlyph5},
    {0x36, 5, 7, 6, 1, kGlyph6}, {0x37, 5, 7, 6, 1, kGlyph7},
    {0x38, 5, 7, 6, 1, kGlyph8}, {0x39, 5, 7, 6, 1, kGlyph9},
    {0x3a, 2, 7, 3, 1, kGlyphColon},
    {0x41, 5, 7, 6, 1, kGlyphA}, {0x42, 5, 7, 6, 1, kGlyphB},
    {0x43, 5, 7, 6, 1, kGlyphC}, {0x4d, 5, 7, 6, 1, kGlyphM},
    {0x4f, 5, 7, 6, 1, kGlyphO}, {0x50, 5, 7, 6, 1, kGlyphP},
    {0x53, 5, 7, 6, 1, kGlyphS}, {0x54, 5, 7, 6, 1, kGlyphT},
    {0x55, 5, 7, 6, 1, kGlyphU}, {0x4e2d, 8, 8, 9, 1, kGlyphCjkMiddle},
};

constexpr jellyframe::BitmapFont kBringupFont{
    kGlyphs,
    sizeof(kGlyphs) / sizeof(kGlyphs[0]),
    8,
    6,
};

struct FontFamilyContext {
    const jellyframe_esp32s3_generated::FontFace* faces = nullptr;
    std::size_t face_count = 0;
};

FontFamilyContext kProductionFontFamily{
    jellyframe_esp32s3_generated::kNotoSansScFaces,
    jellyframe_esp32s3_generated::kNotoSansScFaceCount,
};

const ProductionFontStats kProductionFontStats{
    "Noto Sans SC",
    jellyframe_esp32s3_generated::kNotoSansScCoverageName,
    jellyframe_esp32s3_generated::kNotoSansScFaceCount,
    jellyframe_esp32s3_generated::kNotoSansScCoverageCount,
    jellyframe_esp32s3_generated::kNotoSansScGlyphCount,
    jellyframe_esp32s3_generated::kNotoSansScBitmapByteCount,
    jellyframe_esp32s3_generated::kNotoSansScBitsPerPixel,
};

const jellyframe_esp32s3_generated::FontFace* choose_face(const FontFamilyContext& family,
                                                          int font_size,
                                                          int font_weight) {
    if (family.faces == nullptr || family.face_count == 0) {
        return nullptr;
    }
    const auto* best = &family.faces[0];
    int best_score = std::abs(best->pixel_size - font_size) * 16 +
        std::abs(best->weight - font_weight);
    for (std::size_t index = 1; index < family.face_count; ++index) {
        const auto& candidate = family.faces[index];
        const int score = std::abs(candidate.pixel_size - font_size) * 16 +
            std::abs(candidate.weight - font_weight);
        if (score < best_score) {
            best = &candidate;
            best_score = score;
        }
    }
    return best;
}

bool production_measure_callback(const std::string& text,
                                 int font_size,
                                 int font_weight,
                                 jellyframe::TextMetrics* metrics,
                                 void*) {
    if (metrics == nullptr) {
        return false;
    }
    const auto* face = choose_face(kProductionFontFamily, font_size, font_weight);
    if (face == nullptr || face->font == nullptr) {
        return false;
    }
    *metrics = jellyframe::measure_bitmap_text(
        jellyframe::BitmapFontContext{face->font, 1}, text, font_size, face->weight);
    return true;
}

bool production_paint_callback(jellyframe::FrameBuffer& target,
                               jellyframe::Rect rect,
                               jellyframe::Color color,
                               const std::string& text,
                               int font_size,
                               int font_weight,
                               jellyframe::TextCommandAlign align,
                               bool single_line,
                               void*) {
    const auto* face = choose_face(kProductionFontFamily, font_size, font_weight);
    if (face == nullptr || face->font == nullptr) {
        return false;
    }
    jellyframe::BitmapFontContext context{face->font, 1};
    return jellyframe::bitmap_font_paint_callback(
        target, rect, color, text, font_size, face->weight, align, single_line, &context);
}

} // namespace

const jellyframe::BitmapFont& bringup_font() {
    return kBringupFont;
}

jellyframe::BitmapFontContext make_bringup_font_context(int scale) {
    return jellyframe::BitmapFontContext{&kBringupFont, scale};
}

AppFontContext make_app_font_context() {
    return {};
}

const ProductionFontStats& production_font_stats() {
    return kProductionFontStats;
}

jellyframe::TextMeasureProvider make_production_text_measure_provider() {
    return jellyframe::TextMeasureProvider{production_measure_callback, nullptr};
}

jellyframe::TextPainter make_production_text_painter() {
    return jellyframe::TextPainter{production_paint_callback, nullptr};
}

bool app_font_measure_callback(const std::string& text,
                               int font_size,
                               int font_weight,
                               jellyframe::TextMetrics* metrics,
                               void* context) {
    return production_measure_callback(text, font_size, font_weight, metrics, context);
}

bool app_font_paint_callback(jellyframe::FrameBuffer& target,
                             jellyframe::Rect rect,
                             jellyframe::Color color,
                             const std::string& text,
                             int font_size,
                             int font_weight,
                             jellyframe::TextCommandAlign align,
                             bool single_line,
                             void* context) {
    return production_paint_callback(target, rect, color, text, font_size, font_weight,
                                     align, single_line, context);
}

} // namespace jellyframe_esp32s3
