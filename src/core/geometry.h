#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct EdgeSizes {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

enum class DisplayCommandType {
    FillRect,
    StrokeRect,
    LinearGradient,
    Text,
};

enum class TextCommandAlign {
    Start,
    Center,
    End,
};

struct DisplayCommand {
    DisplayCommandType type = DisplayCommandType::FillRect;
    Rect rect;
    Color color;
    Color color2;
    std::string text;
    int border_radius = 0;
    int font_size = 14;
    int font_weight = 400;
    TextCommandAlign text_align = TextCommandAlign::Start;
    bool text_single_line = true;
};

using DisplayList = std::vector<DisplayCommand>;

} // namespace jellyframe
