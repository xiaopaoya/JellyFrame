#pragma once

#include "render_core/bitmap_font.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace jellyframe::benchmark {

// Fixed 1bpp subset mirrored from samples/fonts/bitmap/tiny.bdf.
inline constexpr std::uint8_t kTinyGlyphA[] = {0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88};
inline constexpr std::uint8_t kTinyGlyphCelsius[] = {0x62, 0x94, 0x98, 0x90, 0x90, 0x90, 0x62, 0x00};
inline constexpr std::uint8_t kTinyGlyphStar[] = {0x10, 0x10, 0x54, 0x38, 0xfe, 0x38, 0x54, 0x00};
inline constexpr std::uint8_t kTinyGlyphHeart[] = {0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 0x00};
inline constexpr std::uint8_t kTinyGlyphZhong[] = {0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10};
inline constexpr std::uint8_t kTinyGlyphXiao[] = {0x10, 0x10, 0x54, 0x54, 0x92, 0x10, 0x10, 0x10};

inline constexpr BitmapFontGlyph kTinyGlyphs[] = {
    {'A', 5, 7, 6, 1, kTinyGlyphA, 1},
    {0x2103, 8, 8, 8, 1, kTinyGlyphCelsius, 1},
    {0x2605, 8, 8, 8, 1, kTinyGlyphStar, 1},
    {0x2665, 8, 8, 8, 1, kTinyGlyphHeart, 1},
    {0x4e2d, 8, 8, 8, 1, kTinyGlyphZhong, 1},
    {0x5c0f, 8, 8, 8, 1, kTinyGlyphXiao, 1},
};
inline constexpr BitmapFont kTinyFont{kTinyGlyphs, std::size(kTinyGlyphs), 8, 8};
inline constexpr std::array<std::string_view, 4> kTinyTextLines{
    u8"A", u8"A\u4e2d\u5c0f", u8"\u4e2d\u5c0f\u2103", u8"\u2605\u2665A"};
inline constexpr int kTinyTextLineCount = static_cast<int>(kTinyTextLines.size());
inline constexpr int kTinyTextLineWidth = 144;
inline constexpr int kTinyTextLineHeight = 8;
inline constexpr int kTinyTextScale = 1;

inline std::array<DisplayCommand, kTinyTextLineCount> tiny_text_commands(Color color) {
    std::array<DisplayCommand, kTinyTextLineCount> commands;
    for (int i = 0; i < kTinyTextLineCount; ++i) {
        auto& command = commands[static_cast<std::size_t>(i)];
        command.type = DisplayCommandType::Text;
        command.rect = Rect{14, 20 + i * 32, kTinyTextLineWidth, kTinyTextLineHeight};
        command.color = color;
        command.text = kTinyTextLines[static_cast<std::size_t>(i)];
        command.font_size = 8;
        command.font_weight = 400;
        command.text_align = TextCommandAlign::Center;
        command.text_single_line = true;
    }
    return commands;
}

inline bool tiny_text_ink(int x, int y) {
    const int relative_y = y - 20;
    if (relative_y < 0 || relative_y / 32 >= kTinyTextLineCount) return false;
    const int line = relative_y / 32;
    const int glyph_y = relative_y % 32;
    std::uint32_t codepoints[4]{};
    int count = 0;
    for (std::size_t index = 0; index < kTinyTextLines[static_cast<std::size_t>(line)].size();) {
        const auto first = static_cast<unsigned char>(kTinyTextLines[static_cast<std::size_t>(line)][index++]);
        std::uint32_t codepoint = first;
        if ((first & 0xe0U) == 0xc0U) {
            codepoint = first & 0x1fU;
            codepoint = (codepoint << 6U) |
                (static_cast<unsigned char>(kTinyTextLines[static_cast<std::size_t>(line)][index++]) & 0x3fU);
        } else if ((first & 0xf0U) == 0xe0U) {
            codepoint = first & 0x0fU;
            for (int part = 0; part < 2; ++part) {
                codepoint = (codepoint << 6U) |
                    (static_cast<unsigned char>(kTinyTextLines[static_cast<std::size_t>(line)][index++]) & 0x3fU);
            }
        }
        if (count >= 4) throw std::runtime_error("tiny text fixture exceeds oracle capacity");
        codepoints[count++] = codepoint;
    }
    int advance = 0;
    for (int i = 0; i < count; ++i) {
        const auto* glyph = find_bitmap_glyph(kTinyFont, codepoints[i]);
        if (glyph == nullptr) throw std::runtime_error("tiny text fixture contains an unsupported glyph");
        advance += glyph->advance;
    }
    int cursor = 14 + std::max(0, (kTinyTextLineWidth - advance) / 2);
    for (int i = 0; i < count; ++i) {
        const auto* glyph = find_bitmap_glyph(kTinyFont, codepoints[i]);
        const int glyph_x = x - cursor;
        if (glyph_x >= 0 && glyph_x < glyph->width && glyph_y < glyph->height &&
            (glyph->rows[glyph_y] & (0x80U >> glyph_x)) != 0U) {
            return true;
        }
        cursor += glyph->advance;
    }
    return false;
}

} // namespace jellyframe::benchmark
