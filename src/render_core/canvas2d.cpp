#include "render_core/canvas2d.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace jellyframe {
namespace {

constexpr std::uint32_t kCanvasHandleMask = 0x80000000U;
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = kPi * 2.0;

bool translated_coordinate(int value, int translation, int& result) {
    const std::int64_t translated = static_cast<std::int64_t>(value) + translation;
    if (translated < std::numeric_limits<int>::min() || translated > std::numeric_limits<int>::max()) {
        return false;
    }
    result = static_cast<int>(translated);
    return true;
}

int parse_positive_int(const std::string& value, int fallback) {
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || parsed <= 0 || parsed > std::numeric_limits<int>::max()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

std::string_view trim_ascii(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

bool contains_ascii_word(std::string_view value, std::string_view word) {
    if (word.empty() || value.size() < word.size()) {
        return false;
    }
    for (std::size_t index = 0; index + word.size() <= value.size(); ++index) {
        bool matched = true;
        for (std::size_t offset = 0; offset < word.size(); ++offset) {
            const char normalized =
                static_cast<char>(std::tolower(static_cast<unsigned char>(value[index + offset])));
            if (normalized != word[offset]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

bool parse_canvas_font(std::string_view raw, Canvas2DState& state) {
    raw = trim_ascii(raw);
    if (raw.empty() || raw.size() > 96) {
        return false;
    }
    const std::size_t px = raw.find("px");
    if (px == std::string_view::npos || px == 0) {
        return false;
    }
    std::size_t size_begin = px;
    while (size_begin > 0 && std::isdigit(static_cast<unsigned char>(raw[size_begin - 1])) != 0) {
        --size_begin;
    }
    if (size_begin == px) {
        return false;
    }
    int font_size = 0;
    for (std::size_t index = size_begin; index < px; ++index) {
        font_size = font_size * 10 + (raw[index] - '0');
    }
    if (font_size <= 0 || font_size > 96) {
        return false;
    }

    int font_weight = contains_ascii_word(raw.substr(0, size_begin), "bold") ? 700 : 400;
    for (std::size_t index = 0; index + 2 < size_begin; ++index) {
        if (std::isdigit(static_cast<unsigned char>(raw[index])) == 0 ||
            std::isdigit(static_cast<unsigned char>(raw[index + 1])) == 0 ||
            std::isdigit(static_cast<unsigned char>(raw[index + 2])) == 0) {
            continue;
        }
        const int numeric_weight = (raw[index] - '0') * 100 + (raw[index + 1] - '0') * 10 + (raw[index + 2] - '0');
        if (numeric_weight >= 100 && numeric_weight <= 900) {
            font_weight = numeric_weight;
        }
    }

    std::string_view family = trim_ascii(raw.substr(px + 2));
    if (!family.empty() && family.front() == '/') {
        const std::size_t space = family.find(' ');
        family = space == std::string_view::npos ? std::string_view{} : trim_ascii(family.substr(space + 1));
    }
    state.font_size = font_size;
    state.font_weight = font_weight;
    state.font_family_hash = normalized_font_family_hash(family);
    state.font.assign(raw.begin(), raw.end());
    return true;
}

int canvas_width_for(const Node& node, int fallback) {
    return parse_positive_int(node.attribute("width"), fallback);
}

int canvas_height_for(const Node& node, int fallback) {
    return parse_positive_int(node.attribute("height"), fallback);
}

int hex_digit(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool parse_hex_pair(char high, char low, std::uint8_t& output) {
    const int hi = hex_digit(high);
    const int lo = hex_digit(low);
    if (hi < 0 || lo < 0) {
        return false;
    }
    output = static_cast<std::uint8_t>((hi << 4) | lo);
    return true;
}

bool parse_canvas_color(std::string_view raw, Color& output) {
    std::string value(raw);
    if (value == "black") {
        output = Color{0, 0, 0, 255};
        return true;
    }
    if (value == "white") {
        output = Color{255, 255, 255, 255};
        return true;
    }
    if (value == "red") {
        output = Color{255, 0, 0, 255};
        return true;
    }
    if (value == "green") {
        output = Color{0, 128, 0, 255};
        return true;
    }
    if (value == "blue") {
        output = Color{0, 0, 255, 255};
        return true;
    }
    if (value == "transparent") {
        output = Color{0, 0, 0, 0};
        return true;
    }
    if (value.size() == 4 && value[0] == '#') {
        const int r = hex_digit(value[1]);
        const int g = hex_digit(value[2]);
        const int b = hex_digit(value[3]);
        if (r < 0 || g < 0 || b < 0) {
            return false;
        }
        output = Color{
            static_cast<std::uint8_t>((r << 4) | r),
            static_cast<std::uint8_t>((g << 4) | g),
            static_cast<std::uint8_t>((b << 4) | b),
            255,
        };
        return true;
    }
    if ((value.size() == 7 || value.size() == 9) && value[0] == '#') {
        Color parsed;
        if (!parse_hex_pair(value[1], value[2], parsed.r) ||
            !parse_hex_pair(value[3], value[4], parsed.g) ||
            !parse_hex_pair(value[5], value[6], parsed.b)) {
            return false;
        }
        parsed.a = 255;
        if (value.size() == 9 && !parse_hex_pair(value[7], value[8], parsed.a)) {
            return false;
        }
        output = parsed;
        return true;
    }
    return false;
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

Color with_global_alpha(Color color, double global_alpha) {
    if (global_alpha >= 1.0) {
        return color;
    }
    if (global_alpha <= 0.0) {
        color.a = 0;
        return color;
    }
    color.a = clamp_u8(static_cast<int>(static_cast<double>(color.a) * global_alpha + 0.5));
    return color;
}

Color with_coverage(Color color, int coverage) {
    if (coverage >= 255) {
        return color;
    }
    if (coverage <= 0) {
        color.a = 0;
        return color;
    }
    color.a = clamp_u8((static_cast<int>(color.a) * coverage + 127) / 255);
    return color;
}

const Canvas2DGradient* find_gradient(const std::vector<Canvas2DGradient>& gradients,
                                            std::uint32_t gradient_id) {
    for (const Canvas2DGradient& gradient : gradients) {
        if (gradient.id == gradient_id) {
            return &gradient;
        }
    }
    return nullptr;
}

Color lerp_color(Color from, Color to, double t) {
    t = std::max(0.0, std::min(1.0, t));
    const auto channel = [t](std::uint8_t left, std::uint8_t right) {
        return clamp_u8(static_cast<int>(static_cast<double>(left) +
                                         (static_cast<double>(right) - static_cast<double>(left)) * t + 0.5));
    };
    return Color{
        channel(from.r, to.r),
        channel(from.g, to.g),
        channel(from.b, to.b),
        channel(from.a, to.a),
    };
}

Color sample_gradient_stops(const Canvas2DGradient& gradient, double offset) {
    if (gradient.stops.empty()) {
        return Color{0, 0, 0, 0};
    }
    if (gradient.stops.size() == 1) {
        return gradient.stops.front().color;
    }
    offset = std::max(0.0, std::min(1.0, offset));

    const Canvas2DGradientStop* previous = &gradient.stops.front();
    for (std::size_t index = 1; index < gradient.stops.size(); ++index) {
        const Canvas2DGradientStop& next = gradient.stops[index];
        if (offset <= next.offset) {
            const double span = std::max(0.000001, next.offset - previous->offset);
            return lerp_color(previous->color, next.color, (offset - previous->offset) / span);
        }
        previous = &next;
    }
    return gradient.stops.back().color;
}

Color sample_linear_gradient(const Canvas2DGradient& gradient, double x, double y) {
    const double dx = gradient.x1 - gradient.x0;
    const double dy = gradient.y1 - gradient.y0;
    const double length_squared = dx * dx + dy * dy;
    double offset = 0.0;
    if (length_squared > 0.000001) {
        offset = ((x - gradient.x0) * dx + (y - gradient.y0) * dy) / length_squared;
    }
    return sample_gradient_stops(gradient, offset);
}

Color sample_radial_gradient(const Canvas2DGradient& gradient, double x, double y) {
    const double dx = x - gradient.x0;
    const double dy = y - gradient.y0;
    const double radius = std::sqrt(dx * dx + dy * dy);
    return sample_gradient_stops(gradient, (radius - gradient.r0) / (gradient.r1 - gradient.r0));
}

Color color_for_style(const Canvas2DPaintStyle& style,
                      const std::vector<Canvas2DGradient>& gradients,
                      double x,
                      double y) {
    if (style.kind == Canvas2DPaintKind::LinearGradient) {
        if (const Canvas2DGradient* gradient = find_gradient(gradients, style.gradient_id)) {
            if (gradient->kind == Canvas2DGradientKind::Linear) {
                return sample_linear_gradient(*gradient, x, y);
            }
        }
    } else if (style.kind == Canvas2DPaintKind::RadialGradient) {
        if (const Canvas2DGradient* gradient = find_gradient(gradients, style.gradient_id)) {
            if (gradient->kind == Canvas2DGradientKind::Radial) {
                return sample_radial_gradient(*gradient, x, y);
            }
        }
    }
    return style.color;
}

void blend_pixel(Canvas2DSurface& surface, int x, int y, Color source) {
    if (x < 0 || y < 0 || x >= surface.width || y >= surface.height || source.a == 0) {
        return;
    }
    Color& destination =
        surface.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) +
                       static_cast<std::size_t>(x)];
    if (source.a == 255) {
        destination = source;
        return;
    }

    const int src_a = source.a;
    const int dst_a = destination.a;
    const int inv_src_a = 255 - src_a;
    const int out_a = src_a + ((dst_a * inv_src_a + 127) / 255);
    if (out_a == 0) {
        destination = Color{0, 0, 0, 0};
        return;
    }

    const auto blend_channel = [&](std::uint8_t src, std::uint8_t dst) {
        const int premul = src * src_a + ((dst * dst_a * inv_src_a + 127) / 255);
        return clamp_u8((premul + out_a / 2) / out_a);
    };

    destination = Color{
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        clamp_u8(out_a),
    };
}

void fill_rect_pixels(Canvas2DSurface& surface, int x, int y, int width, int height, Color color) {
    if (width <= 0 || height <= 0 || surface.width <= 0 || surface.height <= 0 || color.a == 0) {
        return;
    }
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(surface.width, x + width);
    const int bottom = std::min(surface.height, y + height);
    if (right <= left || bottom <= top) {
        return;
    }
    for (int row = top; row < bottom; ++row) {
        auto* line = surface.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(surface.width);
        if (color.a == 255) {
            std::fill(line + left, line + right, color);
        } else {
            for (int column = left; column < right; ++column) {
                blend_pixel(surface, column, row, color);
            }
        }
    }
}

void overwrite_rect_pixels(Canvas2DSurface& surface, int x, int y, int width, int height, Color color) {
    if (width <= 0 || height <= 0 || surface.width <= 0 || surface.height <= 0) {
        return;
    }
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(surface.width, x + width);
    const int bottom = std::min(surface.height, y + height);
    if (right <= left || bottom <= top) {
        return;
    }
    for (int row = top; row < bottom; ++row) {
        auto* line = surface.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(surface.width);
        std::fill(line + left, line + right, color);
    }
}

void fill_rect_paint(Canvas2DSurface& surface,
                     int x,
                     int y,
                     int width,
                     int height,
                     const Canvas2DPaintStyle& style,
                     double global_alpha,
                     const std::vector<Canvas2DGradient>& gradients) {
    if (style.kind == Canvas2DPaintKind::Solid) {
        fill_rect_pixels(surface, x, y, width, height, with_global_alpha(style.color, global_alpha));
        return;
    }
    const Canvas2DGradient* gradient = find_gradient(gradients, style.gradient_id);
    if (gradient == nullptr) {
        return;
    }
    if (width <= 0 || height <= 0 || surface.width <= 0 || surface.height <= 0) {
        return;
    }
    const int left = std::max(0, x);
    const int top = std::max(0, y);
    const int right = std::min(surface.width, x + width);
    const int bottom = std::min(surface.height, y + height);
    if (right <= left || bottom <= top) {
        return;
    }
    for (int row = top; row < bottom; ++row) {
        for (int column = left; column < right; ++column) {
            const Color color = with_global_alpha(color_for_style(style, gradients, column + 0.5, row + 0.5),
                                                  global_alpha);
            blend_pixel(surface, column, row, color);
        }
    }
}

void draw_line(Canvas2DSurface& surface,
               Canvas2DPoint from,
               Canvas2DPoint to,
               const Canvas2DPaintStyle& style,
               double global_alpha,
               int line_width,
               const std::vector<Canvas2DGradient>& gradients) {
    const double x0 = static_cast<double>(from.x);
    const double y0 = static_cast<double>(from.y);
    const double dx = static_cast<double>(to.x - from.x);
    const double dy = static_cast<double>(to.y - from.y);
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= 0.0) {
        blend_pixel(surface,
                    from.x,
                    from.y,
                    with_global_alpha(color_for_style(style, gradients, from.x, from.y), global_alpha));
        return;
    }

    const double half_width = std::max(1.0, static_cast<double>(line_width)) * 0.5;
    const double aa_radius = half_width + 1.0;
    const double inner_radius = std::max(0.0, aa_radius - 1.0);
    const double aa_radius_squared = aa_radius * aa_radius;
    const double inner_radius_squared = inner_radius * inner_radius;
    const double feather_squared = std::max(0.0001, aa_radius_squared - inner_radius_squared);
    const int pad = std::max(1, static_cast<int>(std::ceil(aa_radius)));
    const int left = std::max(0, std::min(from.x, to.x) - pad);
    const int right = std::min(surface.width - 1, std::max(from.x, to.x) + pad);
    const int top = std::max(0, std::min(from.y, to.y) - pad);
    const int bottom = std::min(surface.height - 1, std::max(from.y, to.y) + pad);

    for (int y = top; y <= bottom; ++y) {
        const double py = static_cast<double>(y) + 0.5;
        for (int x = left; x <= right; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            double t = ((px - x0) * dx + (py - y0) * dy) / length_squared;
            t = std::max(0.0, std::min(1.0, t));
            const double nearest_x = x0 + t * dx;
            const double nearest_y = y0 + t * dy;
            const double distance_x = px - nearest_x;
            const double distance_y = py - nearest_y;
            const double distance_squared = distance_x * distance_x + distance_y * distance_y;
            if (distance_squared >= aa_radius_squared) {
                continue;
            }
            const int alpha = distance_squared <= inner_radius_squared
                ? 255
                : static_cast<int>(((aa_radius_squared - distance_squared) / feather_squared) * 255.0 + 0.5);
            const Color color = with_global_alpha(color_for_style(style, gradients, px, py), global_alpha);
            blend_pixel(surface, x, y, with_coverage(color, alpha));
        }
    }
}

bool push_path_point(Canvas2DSurface& surface, const Canvas2DPolicy& policy, Canvas2DPoint point) {
    if (surface.path.size() >= std::max<std::size_t>(1, policy.max_path_points)) {
        return false;
    }
    surface.path.push_back(point);
    return true;
}

bool append_quadratic_points(Canvas2DSurface& surface,
                             const Canvas2DPolicy& policy,
                             double control_x,
                             double control_y,
                             double x,
                             double y) {
    if (surface.path.empty() || !std::isfinite(control_x) || !std::isfinite(control_y) ||
        !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const Canvas2DPoint start = surface.path.back();
    const double span = std::max({std::abs(control_x - start.x), std::abs(control_y - start.y),
                                  std::abs(x - control_x), std::abs(y - control_y)});
    const int segments = std::max(2, std::min(24, static_cast<int>(std::ceil(span / 4.0))));
    if (surface.path.size() + static_cast<std::size_t>(segments) >
        std::max<std::size_t>(1, policy.max_path_points)) {
        return false;
    }
    for (int index = 1; index <= segments; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(segments);
        const double inv = 1.0 - t;
        const int px = static_cast<int>(std::round(inv * inv * start.x + 2.0 * inv * t * control_x + t * t * x));
        const int py = static_cast<int>(std::round(inv * inv * start.y + 2.0 * inv * t * control_y + t * t * y));
        if (surface.path.back().x != px || surface.path.back().y != py) {
            if (!push_path_point(surface, policy, Canvas2DPoint{px, py})) {
                return false;
            }
        }
    }
    return true;
}

bool append_cubic_points(Canvas2DSurface& surface,
                         const Canvas2DPolicy& policy,
                         double control1_x,
                         double control1_y,
                         double control2_x,
                         double control2_y,
                         double x,
                         double y) {
    if (surface.path.empty() || !std::isfinite(control1_x) || !std::isfinite(control1_y) ||
        !std::isfinite(control2_x) || !std::isfinite(control2_y) || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const Canvas2DPoint start = surface.path.back();
    const double span = std::max({std::abs(control1_x - start.x), std::abs(control1_y - start.y),
                                  std::abs(control2_x - control1_x), std::abs(control2_y - control1_y),
                                  std::abs(x - control2_x), std::abs(y - control2_y)});
    const int segments = std::max(3, std::min(32, static_cast<int>(std::ceil(span / 4.0))));
    if (surface.path.size() + static_cast<std::size_t>(segments) >
        std::max<std::size_t>(1, policy.max_path_points)) {
        return false;
    }
    for (int index = 1; index <= segments; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(segments);
        const double inv = 1.0 - t;
        const int px = static_cast<int>(std::round(inv * inv * inv * start.x +
                                                    3.0 * inv * inv * t * control1_x +
                                                    3.0 * inv * t * t * control2_x + t * t * t * x));
        const int py = static_cast<int>(std::round(inv * inv * inv * start.y +
                                                    3.0 * inv * inv * t * control1_y +
                                                    3.0 * inv * t * t * control2_y + t * t * t * y));
        if (surface.path.back().x != px || surface.path.back().y != py) {
            if (!push_path_point(surface, policy, Canvas2DPoint{px, py})) {
                return false;
            }
        }
    }
    return true;
}

bool append_arc_points(Canvas2DSurface& surface,
                       const Canvas2DPolicy& policy,
                       double x,
                       double y,
                       double radius,
                       double start_angle,
                       double end_angle,
                       bool anticlockwise) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        !std::isfinite(start_angle) || !std::isfinite(end_angle) || radius <= 0.0) {
        return false;
    }

    double sweep = end_angle - start_angle;
    if (!anticlockwise && sweep < 0.0) {
        sweep = std::fmod(sweep, kTwoPi) + kTwoPi;
    } else if (anticlockwise && sweep > 0.0) {
        sweep = std::fmod(sweep, kTwoPi) - kTwoPi;
    }
    if (std::abs(sweep) >= kTwoPi) {
        sweep = anticlockwise ? -kTwoPi : kTwoPi;
    }
    if (std::abs(sweep) < 0.000001) {
        return true;
    }

    const int arc_pixels = static_cast<int>(std::ceil(std::abs(sweep) * radius));
    const int segments = std::max(4, std::min(96, (arc_pixels + 3) / 4));
    const std::size_t max_points = std::max<std::size_t>(1, policy.max_path_points);
    if (surface.path.size() + static_cast<std::size_t>(segments) + 1 > max_points) {
        return false;
    }
    for (int index = 0; index <= segments; ++index) {
        const double angle = start_angle + sweep * static_cast<double>(index) / static_cast<double>(segments);
        const int px = static_cast<int>(std::round(x + std::cos(angle) * radius));
        const int py = static_cast<int>(std::round(y + std::sin(angle) * radius));
        if (!surface.path.empty() && surface.path.back().x == px && surface.path.back().y == py) {
            continue;
        }
        if (!push_path_point(surface, policy, Canvas2DPoint{px, py})) {
            return false;
        }
    }
    return true;
}

bool fill_polygon(Canvas2DSurface& surface,
                  const std::vector<Canvas2DPoint>& points,
                  const Canvas2DPaintStyle& style,
                  double global_alpha,
                  const std::vector<Canvas2DGradient>& gradients) {
    if (points.size() < 3 || surface.width <= 0 || surface.height <= 0) {
        return false;
    }
    int min_y = points.front().y;
    int max_y = points.front().y;
    for (const Canvas2DPoint& point : points) {
        min_y = std::min(min_y, point.y);
        max_y = std::max(max_y, point.y);
    }
    const int top = std::max(0, min_y);
    const int bottom = std::min(surface.height - 1, max_y);
    if (bottom < top) {
        return false;
    }

    std::vector<int>& intersections = surface.fill_intersections;
    if (intersections.capacity() < points.size()) {
        intersections.reserve(points.size());
    }
    for (int y = top; y <= bottom; ++y) {
        intersections.clear();
        const double scan_y = static_cast<double>(y) + 0.5;
        for (std::size_t index = 0; index < points.size(); ++index) {
            const Canvas2DPoint& a = points[index];
            const Canvas2DPoint& b = points[(index + 1) % points.size()];
            const double ay = static_cast<double>(a.y);
            const double by = static_cast<double>(b.y);
            if ((ay <= scan_y && by > scan_y) || (by <= scan_y && ay > scan_y)) {
                const double t = (scan_y - ay) / (by - ay);
                intersections.push_back(static_cast<int>(std::round(static_cast<double>(a.x) +
                                                                     t * static_cast<double>(b.x - a.x))));
            }
        }
        if (intersections.size() < 2) {
            continue;
        }
        std::sort(intersections.begin(), intersections.end());
        for (std::size_t index = 0; index + 1 < intersections.size(); index += 2) {
            const int left = std::max(0, intersections[index]);
            const int right = std::min(surface.width - 1, intersections[index + 1]);
            if (right >= left) {
                fill_rect_paint(surface, left, y, right - left + 1, 1, style, global_alpha, gradients);
            }
        }
    }
    return true;
}

} // namespace

Canvas2DRegistry::Canvas2DRegistry(Canvas2DPolicy policy)
    : policy_(policy) {}

void Canvas2DRegistry::set_policy(Canvas2DPolicy policy) {
    clear();
    policy_ = policy;
}

void Canvas2DRegistry::set_text_backend(TextMeasureProvider measure, TextPainter painter) {
    text_measure_ = measure;
    text_painter_ = painter;
}

void Canvas2DRegistry::clear() {
    surfaces_.clear();
    gradients_.clear();
    next_handle_ = 1;
    next_gradient_id_ = 1;
}

std::size_t Canvas2DRegistry::total_pixels() const {
    std::size_t total = 0;
    for (const auto& surface : surfaces_) {
        total += static_cast<std::size_t>(surface.width) * static_cast<std::size_t>(surface.height);
    }
    return total;
}

std::uint32_t Canvas2DRegistry::ensure_surface(Node& node) {
    if (!policy_.enabled || node.type != NodeType::Element || node.tag_name != "canvas") {
        return 0;
    }
    if (Canvas2DSurface* existing = surface_for(node)) {
        return existing->handle;
    }
    if (surfaces_.size() >= std::max<std::size_t>(1, policy_.max_surfaces)) {
        return 0;
    }
    const int width = canvas_width_for(node, policy_.default_width);
    const int height = canvas_height_for(node, policy_.default_height);
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels == 0 || pixels > policy_.max_surface_pixels || total_pixels() + pixels > policy_.max_total_pixels) {
        return 0;
    }

    Canvas2DSurface surface;
    surface.handle = kCanvasHandleMask | next_handle_++;
    if (next_handle_ == 0 || (next_handle_ & kCanvasHandleMask) != 0) {
        next_handle_ = 1;
    }
    surface.node = &node;
    surface.width = width;
    surface.height = height;
    surface.pixels.assign(pixels, Color{0, 0, 0, 0});
    surfaces_.push_back(std::move(surface));
    mark_dirty(node, DomDirtyPaint);
    return surfaces_.back().handle;
}

std::uint32_t Canvas2DRegistry::handle_for(const Node& node) const {
    const Canvas2DSurface* found = surface_for(node);
    return found != nullptr ? found->handle : 0;
}

const Canvas2DSurface* Canvas2DRegistry::surface(std::uint32_t handle) const {
    if (!is_canvas2d_handle(handle)) {
        return nullptr;
    }
    for (const auto& surface : surfaces_) {
        if (surface.handle == handle) {
            return &surface;
        }
    }
    return nullptr;
}

Canvas2DSurface* Canvas2DRegistry::mutable_surface(std::uint32_t handle) {
    if (!is_canvas2d_handle(handle)) {
        return nullptr;
    }
    for (auto& surface : surfaces_) {
        if (surface.handle == handle) {
            return &surface;
        }
    }
    return nullptr;
}

Canvas2DSurface* Canvas2DRegistry::surface_for(Node& node) {
    return const_cast<Canvas2DSurface*>(static_cast<const Canvas2DRegistry&>(*this).surface_for(node));
}

const Canvas2DSurface* Canvas2DRegistry::surface_for(const Node& node) const {
    for (const auto& surface : surfaces_) {
        if (surface.node == &node) {
            return &surface;
        }
    }
    return nullptr;
}

const Canvas2DGradient* Canvas2DRegistry::gradient(std::uint32_t gradient_id) const {
    return find_gradient(gradients_, gradient_id);
}

bool Canvas2DRegistry::gradient_exists(std::uint32_t gradient_id) const {
    return gradient(gradient_id) != nullptr;
}

bool Canvas2DRegistry::set_fill_style(Node& node, std::string_view value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    Color parsed;
    if (surface == nullptr || !parse_canvas_color(value, parsed)) {
        return false;
    }
    surface->state.fill_style = Canvas2DPaintStyle{Canvas2DPaintKind::Solid, parsed, 0};
    return true;
}

bool Canvas2DRegistry::set_stroke_style(Node& node, std::string_view value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    Color parsed;
    if (surface == nullptr || !parse_canvas_color(value, parsed)) {
        return false;
    }
    surface->state.stroke_style = Canvas2DPaintStyle{Canvas2DPaintKind::Solid, parsed, 0};
    return true;
}

bool Canvas2DRegistry::set_fill_gradient(Node& node, std::uint32_t gradient_id) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    const Canvas2DGradient* found = gradient(gradient_id);
    if (surface == nullptr || found == nullptr) {
        return false;
    }
    const Canvas2DPaintKind kind = found->kind == Canvas2DGradientKind::Radial
                                       ? Canvas2DPaintKind::RadialGradient
                                       : Canvas2DPaintKind::LinearGradient;
    surface->state.fill_style = Canvas2DPaintStyle{kind, Color{0, 0, 0, 255}, gradient_id};
    return true;
}

bool Canvas2DRegistry::set_stroke_gradient(Node& node, std::uint32_t gradient_id) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    const Canvas2DGradient* found = gradient(gradient_id);
    if (surface == nullptr || found == nullptr) {
        return false;
    }
    const Canvas2DPaintKind kind = found->kind == Canvas2DGradientKind::Radial
                                       ? Canvas2DPaintKind::RadialGradient
                                       : Canvas2DPaintKind::LinearGradient;
    surface->state.stroke_style = Canvas2DPaintStyle{kind, Color{0, 0, 0, 255}, gradient_id};
    return true;
}

bool Canvas2DRegistry::set_line_width(Node& node, double value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(value) || value <= 0) {
        return false;
    }
    surface->state.line_width = std::max(1, std::min(32, static_cast<int>(std::round(value))));
    return true;
}

bool Canvas2DRegistry::set_global_alpha(Node& node, double value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(value) || value < 0.0 || value > 1.0) {
        return false;
    }
    surface->state.global_alpha = value;
    return true;
}

bool Canvas2DRegistry::set_font(Node& node, std::string_view value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    Canvas2DState next = surface->state;
    if (!parse_canvas_font(value, next)) {
        return false;
    }
    surface->state = std::move(next);
    return true;
}

bool Canvas2DRegistry::translate(Node& node, double x, double y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const long delta_x = std::lround(x);
    const long delta_y = std::lround(y);
    const long next_x = static_cast<long>(surface->state.translate_x) + delta_x;
    const long next_y = static_cast<long>(surface->state.translate_y) + delta_y;
    if (next_x < std::numeric_limits<int>::min() || next_x > std::numeric_limits<int>::max() ||
        next_y < std::numeric_limits<int>::min() || next_y > std::numeric_limits<int>::max()) {
        return false;
    }
    surface->state.translate_x = static_cast<int>(next_x);
    surface->state.translate_y = static_cast<int>(next_y);
    return true;
}

Color Canvas2DRegistry::fill_style(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->state.fill_style.color : Color{0, 0, 0, 255};
}

Color Canvas2DRegistry::stroke_style(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->state.stroke_style.color : Color{0, 0, 0, 255};
}

int Canvas2DRegistry::line_width(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->state.line_width : 1;
}

double Canvas2DRegistry::global_alpha(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->state.global_alpha : 1.0;
}

std::string Canvas2DRegistry::font(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->state.font : "10px sans-serif";
}

bool Canvas2DRegistry::save(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->state_stack.size() >= std::max<std::size_t>(1, policy_.max_state_stack_depth)) {
        return false;
    }
    surface->state_stack.push_back(surface->state);
    return true;
}

bool Canvas2DRegistry::restore(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->state_stack.empty()) {
        return false;
    }
    surface->state = surface->state_stack.back();
    surface->state_stack.pop_back();
    return true;
}

bool Canvas2DRegistry::clear_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    int left = 0;
    int top = 0;
    if (!translated_coordinate(x, surface->state.translate_x, left) ||
        !translated_coordinate(y, surface->state.translate_y, top)) {
        return false;
    }
    overwrite_rect_pixels(*surface, left, top, width, height, Color{0, 0, 0, 0});
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::fill_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    int left = 0;
    int top = 0;
    if (!translated_coordinate(x, surface->state.translate_x, left) ||
        !translated_coordinate(y, surface->state.translate_y, top)) {
        return false;
    }
    fill_rect_paint(*surface, left, top, width, height,
                    surface->state.fill_style, surface->state.global_alpha, gradients_);
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::stroke_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const int line = std::max(1, surface->state.line_width);
    int left = 0;
    int top = 0;
    if (!translated_coordinate(x, surface->state.translate_x, left) ||
        !translated_coordinate(y, surface->state.translate_y, top)) {
        return false;
    }
    fill_rect_paint(*surface, left, top, width, line, surface->state.stroke_style, surface->state.global_alpha, gradients_);
    fill_rect_paint(*surface,
                    left,
                    top + height - line,
                    width,
                    line,
                    surface->state.stroke_style,
                    surface->state.global_alpha,
                    gradients_);
    fill_rect_paint(*surface, left, top, line, height, surface->state.stroke_style, surface->state.global_alpha, gradients_);
    fill_rect_paint(*surface,
                    left + width - line,
                    top,
                    line,
                    height,
                    surface->state.stroke_style,
                    surface->state.global_alpha,
                    gradients_);
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::begin_path(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path.clear();
    surface->path_closed = false;
    return true;
}

bool Canvas2DRegistry::move_to(Node& node, int x, int y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path.clear();
    surface->path_closed = false;
    int translated_x = 0;
    int translated_y = 0;
    if (!translated_coordinate(x, surface->state.translate_x, translated_x) ||
        !translated_coordinate(y, surface->state.translate_y, translated_y)) {
        return false;
    }
    push_path_point(*surface, policy_, Canvas2DPoint{translated_x, translated_y});
    return true;
}

bool Canvas2DRegistry::line_to(Node& node, int x, int y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path_closed = false;
    int translated_x = 0;
    int translated_y = 0;
    if (!translated_coordinate(x, surface->state.translate_x, translated_x) ||
        !translated_coordinate(y, surface->state.translate_y, translated_y)) {
        return false;
    }
    return push_path_point(*surface, policy_, Canvas2DPoint{translated_x, translated_y});
}

bool Canvas2DRegistry::quadratic_curve_to(Node& node,
                                              double control_x,
                                              double control_y,
                                              double x,
                                              double y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(control_x) || !std::isfinite(control_y) ||
        !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const double translated_control_x = control_x + surface->state.translate_x;
    const double translated_control_y = control_y + surface->state.translate_y;
    const double translated_x = x + surface->state.translate_x;
    const double translated_y = y + surface->state.translate_y;
    const double minimum = static_cast<double>(std::numeric_limits<int>::min());
    const double maximum = static_cast<double>(std::numeric_limits<int>::max());
    if (!std::isfinite(translated_control_x) || !std::isfinite(translated_control_y) ||
        !std::isfinite(translated_x) || !std::isfinite(translated_y) ||
        translated_control_x < minimum || translated_control_x > maximum ||
        translated_control_y < minimum || translated_control_y > maximum ||
        translated_x < minimum || translated_x > maximum ||
        translated_y < minimum || translated_y > maximum) {
        return false;
    }
    surface->path_closed = false;
    return append_quadratic_points(*surface, policy_,
                                   translated_control_x, translated_control_y,
                                   translated_x, translated_y);
}

bool Canvas2DRegistry::bezier_curve_to(Node& node,
                                           double control1_x,
                                           double control1_y,
                                           double control2_x,
                                           double control2_y,
                                           double x,
                                           double y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(control1_x) || !std::isfinite(control1_y) ||
        !std::isfinite(control2_x) || !std::isfinite(control2_y) || !std::isfinite(x) || !std::isfinite(y)) {
        return false;
    }
    const double translated[] = {
        control1_x + surface->state.translate_x, control1_y + surface->state.translate_y,
        control2_x + surface->state.translate_x, control2_y + surface->state.translate_y,
        x + surface->state.translate_x, y + surface->state.translate_y,
    };
    const double minimum = static_cast<double>(std::numeric_limits<int>::min());
    const double maximum = static_cast<double>(std::numeric_limits<int>::max());
    for (double value : translated) {
        if (!std::isfinite(value) || value < minimum || value > maximum) {
            return false;
        }
    }
    surface->path_closed = false;
    return append_cubic_points(*surface, policy_, translated[0], translated[1], translated[2], translated[3],
                               translated[4], translated[5]);
}

bool Canvas2DRegistry::arc(Node& node,
                           double x,
                           double y,
                           double radius,
                           double start_angle,
                           double end_angle,
                           bool anticlockwise) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path_closed = false;
    return append_arc_points(*surface, policy_, x + surface->state.translate_x, y + surface->state.translate_y,
                             radius, start_angle, end_angle, anticlockwise);
}

bool Canvas2DRegistry::close_path(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->path.size() < 2) {
        return false;
    }
    surface->path_closed = true;
    return true;
}

bool Canvas2DRegistry::fill(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->path.size() < 3) {
        return false;
    }
    if (!fill_polygon(*surface, surface->path, surface->state.fill_style, surface->state.global_alpha, gradients_)) {
        return false;
    }
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::stroke(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->path.size() < 2) {
        return false;
    }
    const int line_width = surface->state.line_width;
    for (std::size_t index = 1; index < surface->path.size(); ++index) {
        draw_line(*surface,
                  surface->path[index - 1],
                  surface->path[index],
                  surface->state.stroke_style,
                  surface->state.global_alpha,
                  line_width,
                  gradients_);
    }
    if (surface->path_closed) {
        draw_line(*surface,
                  surface->path.back(),
                  surface->path.front(),
                  surface->state.stroke_style,
                  surface->state.global_alpha,
                  line_width,
                  gradients_);
    }
    mark_dirty(node, DomDirtyPaint);
    return true;
}

Canvas2DTextMetrics Canvas2DRegistry::measure_text(Node& node, std::string_view text) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return Canvas2DTextMetrics{};
    }
    const std::string owned_text(text);
    const TextMetrics metrics =
        jellyframe::measure_text(text_measure_,
                                 owned_text,
                                 surface->state.font_size,
                                 surface->state.font_weight,
                                 surface->state.font_family_hash);
    return Canvas2DTextMetrics{static_cast<double>(metrics.width)};
}

bool Canvas2DRegistry::fill_text(Node& node, std::string_view text, double x, double y, double max_width) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || text.empty() || !std::isfinite(x) || !std::isfinite(y) ||
        (max_width != 0.0 && !std::isfinite(max_width))) {
        return false;
    }
    const std::string owned_text(text);
    const TextMetrics metrics =
        jellyframe::measure_text(text_measure_,
                                 owned_text,
                                 surface->state.font_size,
                                 surface->state.font_weight,
                                 surface->state.font_family_hash);
    const int left = static_cast<int>(std::round(x)) + surface->state.translate_x;
    const int baseline = static_cast<int>(std::round(y)) + surface->state.translate_y;
    int width = max_width > 0.0 ? static_cast<int>(std::round(max_width)) : metrics.width;
    if (width <= 0) {
        width = metrics.width;
    }
    const int available_width = surface->width - left;
    if (available_width <= 0) {
        return false;
    }
    width = std::max(1, std::min(width, available_width));
    const int line_height = std::max(1, metrics.line_height);
    FrameBuffer target;
    target.width = surface->width;
    target.height = surface->height;
    target.pixels.swap(surface->pixels);

    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{left, baseline - line_height, width, line_height};
    command.color = with_global_alpha(color_for_style(surface->state.fill_style, gradients_, left, baseline),
                                      surface->state.global_alpha);
    command.text = owned_text;
    command.font_size = surface->state.font_size;
    command.font_weight = surface->state.font_weight;
    command.font_family_hash = surface->state.font_family_hash;
    command.text_align = TextCommandAlign::Start;
    command.text_single_line = true;
    SoftwareRasterizer rasterizer(text_painter_);
    rasterizer.rasterize(command, target, Rect{0, 0, surface->width, surface->height});

    surface->pixels.swap(target.pixels);
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::draw_image(Node& destination,
                                  const Node& source,
                                  int source_x,
                                  int source_y,
                                  int source_width,
                                  int source_height,
                                  int destination_x,
                                  int destination_y,
                                  int destination_width,
                                  int destination_height) {
    if (&destination == &source || source_width <= 0 || source_height <= 0 ||
        destination_width <= 0 || destination_height <= 0) {
        return false;
    }
    Canvas2DSurface* target = mutable_surface(ensure_surface(destination));
    const Canvas2DSurface* input = surface_for(source);
    if (target == nullptr || input == nullptr || source_x < 0 || source_y < 0 ||
        source_x > input->width - source_width || source_y > input->height - source_height) {
        return false;
    }

    if (!translated_coordinate(destination_x, target->state.translate_x, destination_x) ||
        !translated_coordinate(destination_y, target->state.translate_y, destination_y)) {
        return false;
    }
    const int left = std::max(0, destination_x);
    const int top = std::max(0, destination_y);
    const int right = std::min(target->width, destination_x + destination_width);
    const int bottom = std::min(target->height, destination_y + destination_height);
    if (left >= right || top >= bottom) {
        return true;
    }
    for (int y = top; y < bottom; ++y) {
        const std::int64_t scaled_y = static_cast<std::int64_t>(y - destination_y) * source_height;
        const int input_y = source_y + static_cast<int>(scaled_y / destination_height);
        for (int x = left; x < right; ++x) {
            const std::int64_t scaled_x = static_cast<std::int64_t>(x - destination_x) * source_width;
            const int input_x = source_x + static_cast<int>(scaled_x / destination_width);
            const Color color = input->pixels[static_cast<std::size_t>(input_y) * input->width + input_x];
            blend_pixel(*target, x, y, with_global_alpha(color, target->state.global_alpha));
        }
    }
    mark_dirty(destination, DomDirtyPaint);
    return true;
}

std::uint32_t Canvas2DRegistry::create_linear_gradient(double x0, double y0, double x1, double y1) {
    if (!policy_.enabled || gradients_.size() >= policy_.max_gradients ||
        !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
        return 0;
    }
    Canvas2DGradient gradient;
    gradient.kind = Canvas2DGradientKind::Linear;
    gradient.id = next_gradient_id_++;
    if (next_gradient_id_ == 0) {
        next_gradient_id_ = 1;
    }
    gradient.x0 = x0;
    gradient.y0 = y0;
    gradient.x1 = x1;
    gradient.y1 = y1;
    gradients_.push_back(std::move(gradient));
    return gradients_.back().id;
}

std::uint32_t Canvas2DRegistry::create_linear_gradient(Node& node,
                                                        double x0,
                                                        double y0,
                                                        double x1,
                                                        double y1) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return 0;
    }
    return create_linear_gradient(x0 + surface->state.translate_x,
                                  y0 + surface->state.translate_y,
                                  x1 + surface->state.translate_x,
                                  y1 + surface->state.translate_y);
}

std::uint32_t Canvas2DRegistry::create_radial_gradient(double x0,
                                                        double y0,
                                                        double r0,
                                                        double x1,
                                                        double y1,
                                                        double r1) {
    if (!policy_.enabled || gradients_.size() >= policy_.max_gradients ||
        !std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(r0) ||
        !std::isfinite(x1) || !std::isfinite(y1) || !std::isfinite(r1) ||
        x0 != x1 || y0 != y1 || r0 < 0.0 || r1 <= r0) {
        return 0;
    }
    Canvas2DGradient gradient;
    gradient.id = next_gradient_id_++;
    if (next_gradient_id_ == 0) {
        next_gradient_id_ = 1;
    }
    gradient.kind = Canvas2DGradientKind::Radial;
    gradient.x0 = x0;
    gradient.y0 = y0;
    gradient.x1 = x1;
    gradient.y1 = y1;
    gradient.r0 = r0;
    gradient.r1 = r1;
    gradients_.push_back(std::move(gradient));
    return gradients_.back().id;
}

std::uint32_t Canvas2DRegistry::create_radial_gradient(Node& node,
                                                        double x0,
                                                        double y0,
                                                        double r0,
                                                        double x1,
                                                        double y1,
                                                        double r1) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return 0;
    }
    return create_radial_gradient(x0 + surface->state.translate_x,
                                  y0 + surface->state.translate_y,
                                  r0,
                                  x1 + surface->state.translate_x,
                                  y1 + surface->state.translate_y,
                                  r1);
}

bool Canvas2DRegistry::add_color_stop(std::uint32_t gradient_id, double offset, std::string_view color) {
    if (!std::isfinite(offset) || offset < 0.0 || offset > 1.0) {
        return false;
    }
    Canvas2DGradient* found = nullptr;
    for (Canvas2DGradient& gradient : gradients_) {
        if (gradient.id == gradient_id) {
            found = &gradient;
            break;
        }
    }
    Color parsed;
    const std::size_t max_stops = found != nullptr && found->kind == Canvas2DGradientKind::Radial
                                      ? std::min<std::size_t>(2, policy_.max_gradient_stops)
                                      : policy_.max_gradient_stops;
    if (found == nullptr || found->stops.size() >= max_stops || !parse_canvas_color(color, parsed)) {
        return false;
    }
    found->stops.push_back(Canvas2DGradientStop{offset, parsed});
    std::stable_sort(found->stops.begin(), found->stops.end(), [](const auto& left, const auto& right) {
        return left.offset < right.offset;
    });
    return true;
}

bool is_canvas2d_handle(std::uint32_t handle) {
    return (handle & kCanvasHandleMask) != 0;
}

} // namespace jellyframe
