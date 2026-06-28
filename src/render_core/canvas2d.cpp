#include "render_core/canvas2d.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace jellyframe {
namespace {

constexpr std::uint32_t kCanvasHandleMask = 0x80000000U;

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

void write_pixel(Canvas2DSurface& surface, int x, int y, Color color) {
    if (x < 0 || y < 0 || x >= surface.width || y >= surface.height) {
        return;
    }
    surface.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(surface.width) +
                   static_cast<std::size_t>(x)] = color;
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
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

void draw_line(Canvas2DSurface& surface, Canvas2DPoint from, Canvas2DPoint to, Color color, int line_width) {
    const double x0 = static_cast<double>(from.x);
    const double y0 = static_cast<double>(from.y);
    const double dx = static_cast<double>(to.x - from.x);
    const double dy = static_cast<double>(to.y - from.y);
    const double length_squared = dx * dx + dy * dy;
    if (length_squared <= 0.0) {
        blend_pixel(surface, from.x, from.y, color);
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
            blend_pixel(surface, x, y, with_coverage(color, alpha));
        }
    }
}

} // namespace

Canvas2DRegistry::Canvas2DRegistry(Canvas2DPolicy policy)
    : policy_(policy) {}

void Canvas2DRegistry::set_policy(Canvas2DPolicy policy) {
    clear();
    policy_ = policy;
}

void Canvas2DRegistry::clear() {
    surfaces_.clear();
    next_handle_ = 1;
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

bool Canvas2DRegistry::set_fill_style(Node& node, std::string_view value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    Color parsed;
    if (surface == nullptr || !parse_canvas_color(value, parsed)) {
        return false;
    }
    surface->fill_style = parsed;
    return true;
}

bool Canvas2DRegistry::set_stroke_style(Node& node, std::string_view value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    Color parsed;
    if (surface == nullptr || !parse_canvas_color(value, parsed)) {
        return false;
    }
    surface->stroke_style = parsed;
    return true;
}

bool Canvas2DRegistry::set_line_width(Node& node, double value) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || !std::isfinite(value) || value <= 0) {
        return false;
    }
    surface->line_width = std::max(1, std::min(32, static_cast<int>(std::round(value))));
    return true;
}

Color Canvas2DRegistry::fill_style(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->fill_style : Color{0, 0, 0, 255};
}

Color Canvas2DRegistry::stroke_style(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->stroke_style : Color{0, 0, 0, 255};
}

int Canvas2DRegistry::line_width(const Node& node) const {
    const Canvas2DSurface* surface = surface_for(node);
    return surface != nullptr ? surface->line_width : 1;
}

bool Canvas2DRegistry::clear_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    fill_rect_pixels(*surface, x, y, width, height, Color{0, 0, 0, 0});
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::fill_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    fill_rect_pixels(*surface, x, y, width, height, surface->fill_style);
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::stroke_rect(Node& node, int x, int y, int width, int height) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const int line = std::max(1, surface->line_width);
    fill_rect_pixels(*surface, x, y, width, line, surface->stroke_style);
    fill_rect_pixels(*surface, x, y + height - line, width, line, surface->stroke_style);
    fill_rect_pixels(*surface, x, y, line, height, surface->stroke_style);
    fill_rect_pixels(*surface, x + width - line, y, line, height, surface->stroke_style);
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool Canvas2DRegistry::begin_path(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path.clear();
    return true;
}

bool Canvas2DRegistry::move_to(Node& node, int x, int y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    surface->path.clear();
    surface->path.push_back(Canvas2DPoint{x, y});
    return true;
}

bool Canvas2DRegistry::line_to(Node& node, int x, int y) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr) {
        return false;
    }
    if (surface->path.empty()) {
        surface->path.push_back(Canvas2DPoint{x, y});
    } else {
        surface->path.push_back(Canvas2DPoint{x, y});
    }
    return true;
}

bool Canvas2DRegistry::stroke(Node& node) {
    Canvas2DSurface* surface = mutable_surface(ensure_surface(node));
    if (surface == nullptr || surface->path.size() < 2) {
        return false;
    }
    for (std::size_t index = 1; index < surface->path.size(); ++index) {
        draw_line(*surface, surface->path[index - 1], surface->path[index], surface->stroke_style, surface->line_width);
    }
    mark_dirty(node, DomDirtyPaint);
    return true;
}

bool is_canvas2d_handle(std::uint32_t handle) {
    return (handle & kCanvasHandleMask) != 0;
}

} // namespace jellyframe
