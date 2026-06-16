#pragma once

#include "core/bitmap_font.h"

namespace jellyframe_esp32s3 {

const jellyframe::BitmapFont& bringup_font();

jellyframe::BitmapFontContext make_bringup_font_context(int scale = 2);

} // namespace jellyframe_esp32s3
