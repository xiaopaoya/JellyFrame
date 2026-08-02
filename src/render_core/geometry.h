#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
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

inline int clamp_int64_to_int(std::int64_t value) {
    return static_cast<int>(std::clamp(value,
                                       static_cast<std::int64_t>(std::numeric_limits<int>::min()),
                                       static_cast<std::int64_t>(std::numeric_limits<int>::max())));
}

inline int safe_edge(int origin, int extent) {
    const std::int64_t edge = static_cast<std::int64_t>(origin) + static_cast<std::int64_t>(extent);
    return clamp_int64_to_int(edge);
}

inline int safe_add(int left, int right) {
    return safe_edge(left, right);
}

inline int safe_negate(int value) {
    const std::int64_t negated = -static_cast<std::int64_t>(value);
    return clamp_int64_to_int(negated);
}

inline int safe_span(int start, int end) {
    if (end <= start) {
        return 0;
    }
    return clamp_int64_to_int(std::min<std::int64_t>(
        static_cast<std::int64_t>(std::numeric_limits<int>::max()),
        static_cast<std::int64_t>(end) - start));
}

inline bool checked_multiply(std::size_t left, std::size_t right, std::size_t& result) {
    if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

inline bool checked_add(std::size_t left, std::size_t right, std::size_t& result) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        return false;
    }
    result = left + right;
    return true;
}

struct EdgeSizes {
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

constexpr std::uint32_t kCornerRadiusMarker = 0x80000000U;

struct CornerRadii {
    int top_left = 0;
    int top_right = 0;
    int bottom_right = 0;
    int bottom_left = 0;
};

inline int encode_corner_radii(CornerRadii radii) {
    radii.top_left = std::max(0, std::min(127, radii.top_left));
    radii.top_right = std::max(0, std::min(127, radii.top_right));
    radii.bottom_right = std::max(0, std::min(127, radii.bottom_right));
    radii.bottom_left = std::max(0, std::min(127, radii.bottom_left));
    if (radii.top_left == radii.top_right && radii.top_left == radii.bottom_right &&
        radii.top_left == radii.bottom_left) {
        return radii.top_left;
    }
    return static_cast<int>(kCornerRadiusMarker |
                            static_cast<std::uint32_t>(radii.top_left) |
                            (static_cast<std::uint32_t>(radii.top_right) << 7U) |
                            (static_cast<std::uint32_t>(radii.bottom_right) << 14U) |
                            (static_cast<std::uint32_t>(radii.bottom_left) << 21U));
}

inline CornerRadii decode_corner_radii(int encoded) {
    const std::uint32_t packed = static_cast<std::uint32_t>(encoded);
    if ((packed & kCornerRadiusMarker) == 0U) {
        const int radius = std::max(0, encoded);
        return CornerRadii{radius, radius, radius, radius};
    }
    return CornerRadii{static_cast<int>(packed & 0x7FU), static_cast<int>((packed >> 7U) & 0x7FU),
                       static_cast<int>((packed >> 14U) & 0x7FU), static_cast<int>((packed >> 21U) & 0x7FU)};
}

inline bool has_corner_radius(int encoded) {
    const CornerRadii radii = decode_corner_radii(encoded);
    return radii.top_left > 0 || radii.top_right > 0 || radii.bottom_right > 0 || radii.bottom_left > 0;
}

inline int expand_corner_radii(int encoded, int amount) {
    CornerRadii radii = decode_corner_radii(encoded);
    const auto expand = [amount](int radius) {
        const std::int64_t value = static_cast<std::int64_t>(radius) + amount;
        return static_cast<int>(std::clamp(value, static_cast<std::int64_t>(0), static_cast<std::int64_t>(127)));
    };
    radii.top_left = expand(radii.top_left);
    radii.top_right = expand(radii.top_right);
    radii.bottom_right = expand(radii.bottom_right);
    radii.bottom_left = expand(radii.bottom_left);
    return encode_corner_radii(radii);
}

enum class DisplayCommandType : std::uint8_t {
    FillRect,
    StrokeRect,
    LinearGradient,
    ConicGradient,
    RadialGradient,
    BoxShadow,
    Text,
    Image,
};

enum class GradientAxis : std::uint8_t {
    Vertical,
    Horizontal,
    DiagonalDownRight,
    DiagonalDownLeft,
    RadialPosition,
};

enum class TextCommandAlign : std::uint8_t {
    Start,
    Center,
    End,
};

enum class TextTransform : std::uint8_t {
    None,
    Uppercase,
    Lowercase,
    Capitalize,
};

enum class ObjectFit : std::uint8_t {
    Fill,
    Contain,
    Cover,
    None,
    ScaleDown,
};

enum class ImageRendering : std::uint8_t {
    Auto,
    Pixelated,
    CrispEdges,
};

struct ObjectPosition {
    int x_percent = 50;
    int y_percent = 50;
};

struct DisplayCommand {
    DisplayCommandType type = DisplayCommandType::FillRect;
    Rect rect;
    Color color;
    Color color2;
    std::string text;
    int border_radius = 0;
    int stroke_width = 1;
    int font_size = 14;
    int font_weight = 400;
    std::uint32_t font_family_hash = 0;
    TextCommandAlign text_align = TextCommandAlign::Start;
    bool text_single_line = true;
    GradientAxis gradient_axis = GradientAxis::Vertical;
    int gradient_stop_percent = 100;
    std::uint32_t image_handle = 0;
    ObjectFit object_fit = ObjectFit::Fill;
    ObjectPosition object_position;
    ImageRendering image_rendering = ImageRendering::Auto;
};

using DisplayList = std::vector<DisplayCommand>;

} // namespace jellyframe
