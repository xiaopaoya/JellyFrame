#pragma once

#include "render_core/bitmap_font.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3_generated {

struct FontFace {
    const jellyframe::BitmapFont* font;
    int pixel_size;
    int weight;
};

extern const FontFace kNotoSansScFaces[];
extern const std::size_t kNotoSansScFaceCount;
extern const std::size_t kNotoSansScCoverageCount;
extern const std::size_t kNotoSansScGlyphCount;
extern const std::size_t kNotoSansScBitmapByteCount;
extern const std::uint8_t kNotoSansScBitsPerPixel;
extern const char kNotoSansScCoverageName[];

} // namespace jellyframe_esp32s3_generated
