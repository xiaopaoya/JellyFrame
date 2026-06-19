#pragma once

#include "render_core/bitmap_font.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

class BitmapFontResource {
public:
    bool load_jffont(const std::uint8_t* data, std::size_t size);
    void clear();

    bool valid() const { return font_.glyphs != nullptr; }
    const BitmapFont& font() const { return font_; }

private:
    BitmapFont font_{};
    std::vector<BitmapFontGlyph> glyphs_;
};

} // namespace jellyframe
