#pragma once

#include "render_core/bitmap_font.h"

#include <array>
#include <stdexcept>

namespace jellyframe::benchmark {

// A deliberately small shared font, not a system-font or shaping benchmark.
inline constexpr std::uint8_t kClockMasks[][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0x70, 0x88, 0x98, 0xa8, 0xc8, 0x88, 0x70},
    {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xf8},
    {0x10, 0x30, 0x50, 0x90, 0xf8, 0x10, 0x10},
    {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70},
    {0, 0x20, 0x20, 0, 0x20, 0x20, 0},
};
inline constexpr BitmapFontGlyph kClockGlyphs[] = {
    {' ', 5, 7, 6, 1, kClockMasks[0], 1},
    {'0', 5, 7, 6, 1, kClockMasks[1], 1},
    {'2', 5, 7, 6, 1, kClockMasks[2], 1},
    {'4', 5, 7, 6, 1, kClockMasks[3], 1},
    {'8', 5, 7, 6, 1, kClockMasks[4], 1},
    {':', 5, 7, 6, 1, kClockMasks[5], 1},
};
inline constexpr BitmapFont kClockFont{kClockGlyphs, 6, 7, 6};
inline constexpr int kClockScale = 2;
inline constexpr int kClockLineCount = 8;
inline constexpr int kClockLineWidth = 144;
inline constexpr int kClockLineHeight = 20;

inline std::array<DisplayCommand, kClockLineCount> clock_text_commands(Color color) {
    std::array<DisplayCommand, kClockLineCount> commands;
    for (int i = 0; i < kClockLineCount; ++i) {
        auto& command = commands[static_cast<std::size_t>(i)];
        command.type = DisplayCommandType::Text;
        command.rect = Rect{14, 20 + i * 32, kClockLineWidth, kClockLineHeight};
        command.color = color;
        command.text = i % 2 == 0 ? "08:42 20:48" : "24:00 08:42";
        command.font_size = 14;
        command.font_weight = 400;
        command.text_align = TextCommandAlign::Center;
        command.text_single_line = true;
    }
    return commands;
}

// Independent output oracle: map target pixels back into the immutable mask.
// Does not use Core text measurement, placement or rasterization helpers.
inline bool clock_text_ink(int x, int y) {
    const int relative_y = y - 20;
    if (relative_y < 0 || relative_y / 32 >= kClockLineCount) return false;
    const int line = relative_y / 32;
    const int glyph_y = relative_y % 32 - 3;
    const int glyph_x = x - 20; // centered 132px advance within a 144px box
    if (glyph_y < 0 || glyph_y >= 14 || glyph_x < 0 || glyph_x >= 132) return false;
    const char* text = line % 2 == 0 ? "08:42 20:48" : "24:00 08:42";
    const int column = glyph_x % 12 / 2;
    if (column >= 5) return false;
    const char codepoint = text[glyph_x / 12];
    for (const auto& glyph : kClockGlyphs) {
        if (glyph.codepoint == static_cast<std::uint32_t>(codepoint)) {
            return (glyph.rows[glyph_y / 2] & (0x80U >> column)) != 0;
        }
    }
    throw std::runtime_error("clock fixture contains an unsupported glyph");
}

} // namespace jellyframe::benchmark
