#include "render_core/css_parser.h"
#include "render_core/bitmap_font.h"
#include "render_core/bitmap_font_resource.h"
#include "render_core/feature_config.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool has_diagnostic_message_fragment(const VectorDiagnosticSink& sink, const std::string& fragment) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool rejecting_text_painter(FrameBuffer&,
                            Rect,
                            Color,
                            const std::string&,
                            int,
                            int,
                            TextCommandAlign,
                            bool,
                            void*) {
    return false;
}

bool center_pixel_text_painter(FrameBuffer& target,
                               Rect rect,
                               Color color,
                               const std::string&,
                               int,
                               int,
                               TextCommandAlign,
                               bool,
                               void*) {
    const int x = rect.x + rect.width / 2;
    const int y = rect.y + rect.height / 2;
    if (target.contains(x, y)) {
        target.pixel(x, y) = color;
    }
    return true;
}

struct TextPaintCounter {
    int calls = 0;
};

struct ReplayTimingClock {
    const std::uint64_t* samples = nullptr;
    std::size_t sample_count = 0;
    std::size_t calls = 0;
};

std::uint64_t replay_timing_clock(void* raw_context) {
    auto* clock = static_cast<ReplayTimingClock*>(raw_context);
    if (clock == nullptr || clock->samples == nullptr || clock->sample_count == 0) {
        return 0;
    }
    const std::size_t index = std::min(clock->calls, clock->sample_count - 1);
    ++clock->calls;
    return clock->samples[index];
}

bool counting_text_painter(FrameBuffer& target,
                           Rect rect,
                           Color color,
                           const std::string&,
                           int,
                           int,
                           TextCommandAlign,
                           bool,
                           void* context) {
    auto* counter = static_cast<TextPaintCounter*>(context);
    if (counter != nullptr) {
        ++counter->calls;
    }
    if (target.contains(rect.x, rect.y)) {
        target.pixel(rect.x, rect.y) = color;
    }
    return true;
}

struct ImagePaintProbe {
    std::uint32_t expected_handle = 0;
    ObjectFit fit = ObjectFit::Fill;
    ObjectPosition position;
    ImageRendering rendering = ImageRendering::Auto;
    int calls = 0;
};

bool probe_image_painter(FrameBuffer& target,
                         Rect rect,
                         std::uint32_t image_handle,
                         ObjectFit object_fit,
                         ObjectPosition object_position,
                         ImageRendering image_rendering,
                         void* raw_context) {
    auto* probe = static_cast<ImagePaintProbe*>(raw_context);
    if (probe == nullptr || image_handle != probe->expected_handle) {
        return false;
    }
    probe->fit = object_fit;
    probe->position = object_position;
    probe->rendering = image_rendering;
    ++probe->calls;
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        for (int x = rect.x; x < rect.x + rect.width; ++x) {
            if (target.contains(x, y)) {
                target.pixel(x, y) = Color{220, 38, 38, 255};
            }
        }
    }
    return true;
}

const LayoutBox* find_first_text_box(const LayoutBox& box) {
    if (box.node != nullptr && box.node->type == NodeType::Text) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_text_box(*child);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void fill_rect_rasterizes_pixels() {
    FrameBuffer frame_buffer(8, 8, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{2, 2, 3, 3};
    command.color = Color{0, 0, 0, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 8, 8});

    check(frame_buffer.pixel(2, 2).r == 0, "fill rect writes covered pixel");
    check(frame_buffer.pixel(0, 0).r == 255, "fill rect leaves outside pixel");
}

void rounded_image_clip_preserves_corner_underpaint() {
    FrameBuffer frame_buffer(16, 16, Color{255, 255, 255, 255});
    ImagePaintProbe probe;
    probe.expected_handle = 17;
    SoftwareRasterizer rasterizer({}, ImagePainter{probe_image_painter, &probe});
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{2, 2, 10, 10};
    command.image_handle = 17;
    command.border_radius = encode_corner_radii(CornerRadii{5, 5, 5, 5});
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 16, 16});
    check(frame_buffer.pixel(2, 2).r == 255, "rounded image leaves the outer corner underpaint visible");
    check(frame_buffer.pixel(7, 2).r < 255, "rounded image paints the antialiased top edge");
    check(frame_buffer.pixel(7, 7).r == 220, "rounded image paints its center");
    check(probe.calls == 1, "rounded image resolves through the normal image painter once");
}

void linear_gradient_rasterizes_top_and_bottom_colors() {
    FrameBuffer frame_buffer(3, 4, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::LinearGradient;
    command.rect = Rect{0, 0, 3, 4};
    command.color = Color{0, 0, 0, 255};
    command.color2 = Color{120, 60, 30, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 3, 4});

#if JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    check(frame_buffer.pixel(1, 0).r == 0, "gradient top row uses first color");
    check(frame_buffer.pixel(1, 3).r == 120, "gradient bottom row uses second color");
    check(frame_buffer.pixel(1, 2).r > frame_buffer.pixel(1, 1).r, "gradient interpolates rows");
#else
    check(frame_buffer.pixel(1, 0).r == 0, "disabled gradient starts with its solid fallback");
    check(frame_buffer.pixel(1, 3).r == 0, "disabled gradient does not rasterize the second color");
    check(frame_buffer.pixel(1, 2).r == 0, "disabled gradient has no interpolation output");
#endif
}

void horizontal_linear_gradient_rasterizes_left_and_right_colors() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(4, 3, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::LinearGradient;
    command.rect = Rect{0, 0, 4, 3};
    command.color = Color{0, 0, 0, 255};
    command.color2 = Color{90, 45, 15, 255};
    command.gradient_axis = GradientAxis::Horizontal;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 4, 3});

    check(frame_buffer.pixel(0, 1).r == 0, "horizontal gradient left column uses first color");
    check(frame_buffer.pixel(3, 1).r == 90, "horizontal gradient right column uses second color");
    check(frame_buffer.pixel(2, 1).r > frame_buffer.pixel(1, 1).r, "horizontal gradient interpolates columns");
}

void diagonal_linear_gradient_rasterizes_corner_colors() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(5, 5, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::LinearGradient;
    command.rect = Rect{0, 0, 5, 5};
    command.color = Color{0, 0, 0, 255};
    command.color2 = Color{200, 100, 40, 255};
    command.gradient_axis = GradientAxis::DiagonalDownRight;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 5, 5});

    check(frame_buffer.pixel(0, 0).r == 0, "diagonal gradient starts with first corner color");
    check(frame_buffer.pixel(4, 4).r == 200, "diagonal gradient reaches second corner color");
    check(frame_buffer.pixel(4, 0).r > 0 && frame_buffer.pixel(4, 0).r < 200,
          "diagonal gradient interpolates across the off-axis corner");
}

void opaque_linear_gradient_fast_path_preserves_dirty_clip() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(5, 5, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::LinearGradient;
    command.rect = Rect{0, 0, 5, 5};
    command.color = Color{0, 0, 0, 255};
    command.color2 = Color{200, 100, 50, 255};

    rasterizer.rasterize(command, frame_buffer, Rect{1, 1, 2, 2});

    check(frame_buffer.pixel(0, 0).r == 255, "opaque gradient fast path preserves pixels outside dirty clip");
    check(frame_buffer.pixel(1, 1).r == 49 && frame_buffer.pixel(1, 1).g == 25,
          "opaque gradient fast path preserves vertical interpolation inside dirty clip");
    check(frame_buffer.pixel(2, 2).r == 100 && frame_buffer.pixel(2, 2).g == 50,
          "opaque gradient fast path preserves later dirty rows");
}

void opaque_linear_gradient_fast_path_preserves_all_axis_interpolation() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    const Color first{12, 34, 56, 255};
    const Color second{212, 134, 6, 255};
    SoftwareRasterizer rasterizer;

    DisplayCommand horizontal;
    horizontal.type = DisplayCommandType::LinearGradient;
    horizontal.rect = Rect{1, 1, 5, 5};
    horizontal.color = first;
    horizontal.color2 = second;
    horizontal.gradient_axis = GradientAxis::Horizontal;
    FrameBuffer horizontal_target(7, 7, Color{255, 255, 255, 255});
    rasterizer.rasterize(horizontal, horizontal_target, Rect{2, 2, 3, 3});
    check(horizontal_target.pixel(2, 2).r == 61 && horizontal_target.pixel(2, 2).g == 59 &&
              horizontal_target.pixel(2, 2).b == 44,
          "opaque horizontal gradient keeps standard interpolation in a dirty clip");
    check(horizontal_target.pixel(4, 4).r == 162 && horizontal_target.pixel(4, 4).g == 109 &&
              horizontal_target.pixel(4, 4).b == 19,
          "opaque horizontal gradient keeps later interpolation in a dirty clip");

    DisplayCommand diagonal = horizontal;
    diagonal.gradient_axis = GradientAxis::DiagonalDownRight;
    FrameBuffer diagonal_target(7, 7, Color{255, 255, 255, 255});
    rasterizer.rasterize(diagonal, diagonal_target, Rect{2, 2, 3, 3});
    check(diagonal_target.pixel(2, 2).r == 61 && diagonal_target.pixel(2, 2).g == 59 &&
              diagonal_target.pixel(2, 2).b == 44,
          "opaque diagonal-down-right gradient keeps its first dirty sample");
    check(diagonal_target.pixel(4, 4).r == 162 && diagonal_target.pixel(4, 4).g == 109 &&
              diagonal_target.pixel(4, 4).b == 19,
          "opaque diagonal-down-right gradient keeps its final dirty sample");

    diagonal.gradient_axis = GradientAxis::DiagonalDownLeft;
    FrameBuffer reverse_diagonal_target(7, 7, Color{255, 255, 255, 255});
    rasterizer.rasterize(diagonal, reverse_diagonal_target, Rect{2, 2, 3, 3});
    check(reverse_diagonal_target.pixel(2, 2).r == 112 && reverse_diagonal_target.pixel(2, 2).g == 84 &&
              reverse_diagonal_target.pixel(2, 2).b == 31,
          "opaque diagonal-down-left gradient keeps its first dirty sample");
    check(reverse_diagonal_target.pixel(4, 2).r == 61 && reverse_diagonal_target.pixel(4, 2).g == 59 &&
              reverse_diagonal_target.pixel(4, 2).b == 44,
          "opaque diagonal-down-left gradient keeps its mirrored dirty sample");
}

void conic_gradient_rasterizes_clockwise_progress() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(9, 9, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::ConicGradient;
    command.rect = Rect{0, 0, 9, 9};
    command.color = Color{220, 20, 20, 255};
    command.color2 = Color{20, 40, 220, 255};
    command.gradient_stop_percent = 50;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 9, 9});

    check(frame_buffer.pixel(4, 0).r == 220, "conic gradient starts at top with first color");
    check(frame_buffer.pixel(8, 4).r == 220, "conic gradient paints right side before stop");
    check(frame_buffer.pixel(4, 8).b == 220, "conic gradient switches at bottom stop");
    check(frame_buffer.pixel(0, 4).b == 220, "conic gradient keeps second color on left side");
}

void radial_gradient_rasterizes_center_to_edge() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(9, 9, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::RadialGradient;
    command.rect = Rect{0, 0, 9, 9};
    command.color = Color{240, 250, 255, 255};
    command.color2 = Color{20, 80, 140, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 9, 9});

    check(frame_buffer.pixel(4, 4).r > 220, "radial gradient keeps center near first color");
    check(frame_buffer.pixel(0, 0).b == 140, "radial gradient reaches edge color at far corner");
    check(frame_buffer.pixel(2, 4).r > frame_buffer.pixel(0, 4).r,
          "radial gradient interpolates by distance from center");
}

void radial_gradient_keeps_diagonal_falloff_close_to_axis() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(129, 129, Color{0, 0, 0, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::RadialGradient;
    command.rect = Rect{0, 0, 129, 129};
    command.color = Color{255, 255, 255, 255};
    command.color2 = Color{0, 0, 0, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 129, 129});

    const int axis = frame_buffer.pixel(112, 64).r;
    const int diagonal = frame_buffer.pixel(98, 98).r;
    check(std::abs(axis - diagonal) <= 3,
          "radial gradient keeps near-equal axis and diagonal distances circular instead of octagonal");
}

void positioned_radial_gradient_moves_highlight_center() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(11, 11, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::RadialGradient;
    command.rect = Rect{0, 0, 11, 11};
    command.color = Color{240, 250, 255, 255};
    command.color2 = Color{20, 80, 140, 255};
    command.gradient_axis = GradientAxis::RadialPosition;
    command.gradient_stop_percent = 80 * 101 + 20;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 11, 11});

    check(frame_buffer.pixel(8, 2).r > frame_buffer.pixel(5, 5).r,
          "positioned radial gradient moves the highlight toward its declared center");
}

void soft_box_shadow_fades_outside_rounded_card() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(20, 20, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::BoxShadow;
    command.rect = Rect{2, 2, 16, 16};
    command.color = Color{0, 0, 0, 96};
    command.border_radius = 7;
    command.stroke_width = 3;
    command.gradient_stop_percent = 3;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 20, 20});

    check(frame_buffer.pixel(10, 17).r < 255, "soft shadow paints within its bounded outer extent");
    check(frame_buffer.pixel(10, 18).r == 255, "soft shadow leaves pixels outside its extent untouched");
    check(frame_buffer.pixel(10, 17).r > frame_buffer.pixel(10, 16).r,
          "soft shadow fades instead of drawing a flat translucent rectangle");
}

void circular_box_shadow_keeps_diagonal_falloff_close_to_axis() {
#if !JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED
    return;
#endif
    FrameBuffer frame_buffer(52, 52, Color{0, 0, 0, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::BoxShadow;
    command.rect = Rect{0, 0, 52, 52};
    command.color = Color{255, 255, 255, 255};
    command.border_radius = 26;
    command.stroke_width = 6;
    command.gradient_stop_percent = 8;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 52, 52});

    const int axis = frame_buffer.pixel(26, 2).r;
    const int diagonal = frame_buffer.pixel(42, 42).r;
    check(axis > 0 && diagonal > 0, "circular box shadow reaches equal-distance axis and diagonal samples");
    check(std::abs(axis - diagonal) <= 6,
          "circular box shadow keeps the diagonal falloff close to the axis without an octagonal halo");
}

void rounded_stroke_keeps_corner_pixels_clear() {
    FrameBuffer frame_buffer(12, 12, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::StrokeRect;
    command.rect = Rect{1, 1, 10, 10};
    command.color = Color{0, 0, 0, 255};
    command.border_radius = 5;
    command.stroke_width = 2;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 12, 12});

    check(frame_buffer.pixel(1, 1).r == 255, "rounded stroke leaves outer corner clear");
    check(frame_buffer.pixel(6, 2).r == 0, "rounded stroke paints top edge");
}

void square_stroke_paints_all_four_edges() {
    FrameBuffer frame_buffer(14, 14, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::StrokeRect;
    command.rect = Rect{2, 2, 10, 10};
    command.color = Color{0, 0, 0, 255};
    command.stroke_width = 2;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 14, 14});

    check(frame_buffer.pixel(6, 2).r == 0, "square stroke paints the top edge");
    check(frame_buffer.pixel(6, 11).r == 0, "square stroke paints the bottom edge");
    check(frame_buffer.pixel(2, 6).r == 0, "square stroke paints the left edge");
    check(frame_buffer.pixel(11, 6).r == 0, "square stroke paints the right edge");
    check(frame_buffer.pixel(6, 6).r == 255, "square stroke keeps the center hollow");
}

void rounded_stroke_keeps_straight_edges_visible() {
    FrameBuffer frame_buffer(24, 16, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::StrokeRect;
    command.rect = Rect{2, 2, 20, 10};
    command.color = Color{0, 0, 0, 255};
    command.border_radius = 4;
    command.stroke_width = 2;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 24, 16});

    check(frame_buffer.pixel(12, 2).r == 0, "rounded stroke paints the top edge center");
    check(frame_buffer.pixel(12, 11).r == 0, "rounded stroke paints the bottom edge center");
    check(frame_buffer.pixel(2, 2).r == 255, "rounded stroke keeps the outer corner clear");
}

void rounded_fill_antialiases_edge_pixels() {
    FrameBuffer frame_buffer(12, 12, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{1, 1, 10, 10};
    command.color = Color{0, 0, 0, 255};
    command.border_radius = 5;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 12, 12});

    const Color edge = frame_buffer.pixel(3, 1);
    check(edge.r > 0 && edge.r < 255, "rounded fill edge is partially covered");
    check(frame_buffer.pixel(6, 6).r == 0, "rounded fill center remains sharp");
    check(frame_buffer.pixel(1, 1).r == 255, "rounded fill outer corner remains clear");
}

void per_corner_rounded_rect_keeps_square_bottom_left() {
    FrameBuffer frame_buffer(24, 24, Color{0, 0, 0, 255});
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{2, 2, 18, 18};
    command.color = Color{255, 255, 255, 255};
    command.border_radius = encode_corner_radii(CornerRadii{8, 4, 2, 0});
    SoftwareRasterizer rasterizer;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 24, 24});
    check(frame_buffer.pixel(2, 19).r == 255, "square bottom-left corner remains filled");
    check(frame_buffer.pixel(2, 2).r < 255, "rounded top-left corner keeps antialiased coverage");
}

void source_over_alpha_composites() {
    FrameBuffer frame_buffer(1, 1, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{0, 0, 1, 1};
    command.color = Color{0, 0, 0, 128};
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 1, 1});

    const Color pixel = frame_buffer.pixel(0, 0);
    check(pixel.r >= 126 && pixel.r <= 128, "alpha blend red channel");
    check(pixel.g >= 126 && pixel.g <= 128, "alpha blend green channel");
    check(pixel.b >= 126 && pixel.b <= 128, "alpha blend blue channel");
    check(pixel.a == 255, "opaque destination remains opaque");
}

void clipping_limits_rasterization() {
    FrameBuffer frame_buffer(4, 4, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = Rect{0, 0, 4, 4};
    command.color = Color{0, 0, 0, 255};
    rasterizer.rasterize(command, frame_buffer, Rect{1, 1, 2, 2});

    check(frame_buffer.pixel(0, 0).r == 255, "clip keeps outside pixel");
    check(frame_buffer.pixel(1, 1).r == 0, "clip paints inside pixel");
    check(frame_buffer.pixel(2, 2).r == 0, "clip paints opposite inside pixel");
    check(frame_buffer.pixel(3, 3).r == 255, "clip keeps far outside pixel");
}

void image_command_uses_injected_painter() {
    FrameBuffer frame_buffer(8, 8, Color{255, 255, 255, 255});
    ImagePaintProbe probe{42, ObjectFit::Fill, {}, ImageRendering::Auto, 0};
    SoftwareRasterizer rasterizer({}, ImagePainter{probe_image_painter, &probe});
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{1, 2, 3, 2};
    command.image_handle = 42;
    command.object_fit = ObjectFit::Contain;
    command.object_position = ObjectPosition{100, 0};
    command.image_rendering = ImageRendering::Pixelated;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 8, 8});

    check(probe.calls == 1, "image painter called once");
    check(probe.fit == ObjectFit::Contain, "image painter receives object-fit");
    check(probe.position.x_percent == 100 && probe.position.y_percent == 0,
          "image painter receives object-position");
    check(probe.rendering == ImageRendering::Pixelated, "image painter receives image-rendering");
    check(frame_buffer.pixel(1, 2).r == 220 && frame_buffer.pixel(1, 2).g == 38,
          "image painter writes covered pixel");
    check(frame_buffer.pixel(0, 0).r == 255, "image painter leaves outside pixel");
}

void image_command_falls_back_without_painter() {
    VectorDiagnosticSink diagnostics;
    FrameBuffer frame_buffer(4, 4, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer({}, &diagnostics);
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{1, 1, 2, 2};
    command.image_handle = 9;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 4, 4});

    check(frame_buffer.pixel(1, 1).r == 226 && frame_buffer.pixel(1, 1).g == 232,
          "image fallback paints placeholder");
    check(has_diagnostic_code(diagnostics, "paint-image-fallback"), "image fallback diagnostic");
}

void compositor_renders_pipeline_non_empty() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='card'><h1>OK</h1><p>Paint</p></section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { background: white; padding: 4px; }"
        ".card { background: #e5e7eb; border: 1px solid #111827; padding: 8px; opacity: .8; }"
        "h1 { color: #111827; }"
        "p { color: #2563eb; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    SoftwareCompositor compositor;
    FrameBuffer frame_buffer = compositor.render(*layer_tree, 120, 96, Color{255, 255, 255, 255});

    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0, "pipeline renders non-background pixels");
}

void wrapped_text_layout_keeps_descent_padding() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p>Long wearable interface text wraps onto several compact display lines.</p></body>");
    StyleResolver resolver(css_parser.parse("p { font-size: 18px; width: 90px; margin: 0; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 120);

    const LayoutBox* text_box = find_first_text_box(*layout_tree);
    check(text_box != nullptr, "text layout box exists");
    check(text_box->rect.height > 44, "wrapped text keeps descent padding");
}

bool fixed_text_measure(const std::string&,
                        int,
                        int font_weight,
                        TextMetrics* metrics,
                        void*) {
    if (metrics == nullptr || font_weight < 600) {
        return false;
    }
    metrics->width = 32;
    metrics->line_height = 21;
    return true;
}

void layout_uses_injected_text_measurement() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p><strong>Measured</strong></p></body>");
    StyleResolver resolver(css_parser.parse("p { width: 120px; margin: 0; } strong { font-weight: 700; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver, TextMeasureProvider{fixed_text_measure, nullptr});
    auto layout_tree = layout_engine.layout(*render_tree, 160);

    const LayoutBox* text_box = find_first_text_box(*layout_tree);
    check(text_box != nullptr, "measured text box exists");
    check(text_box->rect.width == 32, "layout uses injected text width without extra pad");
    check(text_box->rect.height == 21, "layout uses injected line height");
}

void dirty_render_only_updates_requested_clip() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><section class='panel'>A</section></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } .panel { width: 80px; height: 40px; background: #000000; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 100);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);

    FrameBuffer frame_buffer(100, 60, Color{255, 255, 255, 255});
    SoftwareCompositor compositor;
    const Rect dirty{0, 0, 40, 60};
    compositor.render_into(*layer_tree, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1);

    check(frame_buffer.pixel(10, 10).r == 0, "dirty clip paints inside requested area");
    check(frame_buffer.pixel(70, 10).r == 255, "dirty clip leaves outside area untouched");
}

DisplayCommand black_fill(Rect rect) {
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = Color{0, 0, 0, 255};
    return command;
}

void dirty_render_preserves_original_rounded_geometry() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 80, 40};
    root.display_list.push_back(black_fill(Rect{10, 4, 60, 28}));
    root.display_list.back().border_radius = 12;

    FrameBuffer frame_buffer(80, 40, Color{255, 255, 255, 255});
    SoftwareCompositor compositor;
    const Rect dirty{35, 0, 10, 40};
    compositor.render_into(root, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1);

    check(frame_buffer.pixel(35, 5).r == 0, "dirty clip does not create a new left rounded edge");
    check(frame_buffer.pixel(44, 5).r == 0, "dirty clip does not create a new right rounded edge");
    check(frame_buffer.pixel(20, 5).r == 255, "outside dirty clip remains untouched");
}

void compositor_clips_children_to_rounded_overflow() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 40, 40};
    DisplayCommand background = black_fill(Rect{0, 0, 40, 40});
    background.color = Color{255, 255, 255, 255};
    root.display_list.push_back(background);

    auto clip = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    clip->type = LayerType::Clip;
    clip->bounds = Rect{8, 8, 24, 24};
    clip->clip_rect = clip->bounds;
    clip->has_clip = true;
    clip->clip_border_radius = 8;
    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Paint;
    child->bounds = Rect{0, 0, 40, 40};
    DisplayCommand fill = black_fill(Rect{0, 0, 40, 40});
    fill.color = Color{20, 120, 240, 255};
    child->display_list.push_back(fill);
    clip->children.push_back(std::move(child));
    root.children.push_back(std::move(clip));

    const FrameBuffer frame = SoftwareCompositor().render(root, 40, 40, Color{255, 255, 255, 255});
    check(frame.pixel(8, 8).r == 255 && frame.pixel(8, 8).g == 255,
          "rounded overflow clip excludes the top-left corner");
    check(frame.pixel(20, 8).b > 200,
          "rounded overflow clip keeps the top edge away from the corner");
    check(frame.pixel(20, 20).b > 200,
          "rounded overflow clip keeps child content in the center");
}

void rasterizer_applies_value_rounded_clip_chain() {
    DisplayCommand fill = black_fill(Rect{0, 0, 40, 40});
    fill.color = Color{20, 120, 240, 255};
    FrameBuffer frame(40, 40, Color{255, 255, 255, 255});
    const RasterClip clips[] = {
        {{8, 8, 24, 24}, 8},
        {{12, 12, 16, 16}, 5},
    };

    SoftwareRasterizerStatistics statistics;
    SoftwareRasterizer rasterizer({}, nullptr, {0, &statistics});
    rasterizer.rasterize_clipped(fill,
                                 frame,
                                 Rect{0, 0, 40, 40},
                                 0,
                                 0,
                                 clips,
                                 2);

    check(frame.pixel(8, 8).r == 255 && frame.pixel(8, 8).g == 255,
          "value clip chain excludes the outer rounded corner");
    check(frame.pixel(12, 12).r == 255 && frame.pixel(12, 12).g == 255,
          "value clip chain excludes the inner rounded corner");
    check(frame.pixel(20, 20).b > 200,
          "value clip chain preserves the center of the command");
    check(statistics.rounded_clip_replayed_commands_by_type[
              static_cast<std::size_t>(DisplayCommandType::FillRect)] == 1,
          "single rounded clip command is attributed after temporary-surface replay");
    check(statistics.rounded_clip_replay_candidate_pixels_by_type[
              static_cast<std::size_t>(DisplayCommandType::FillRect)] == 256,
          "single rounded clip command attributes its clipped temporary-surface area");
}

void rasterizer_batches_consecutive_value_clip_commands() {
    DisplayCommand commands[2] = {
        black_fill(Rect{0, 0, 40, 40}),
        black_fill(Rect{16, 16, 8, 8}),
    };
    commands[0].color = Color{20, 120, 240, 255};
    commands[1].color = Color{15, 23, 42, 255};
    const RasterClip clips[] = {{{8, 8, 24, 24}, 8}};

    FrameBuffer batched(40, 40, Color{255, 255, 255, 255});
    FrameBuffer sequential(40, 40, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer;
    rasterizer.rasterize_clipped(commands, 2, batched, {0, 0, 40, 40}, 0, 0, clips, 1);
    rasterizer.rasterize_clipped(commands[0], sequential, {0, 0, 40, 40}, 0, 0, clips, 1);
    rasterizer.rasterize_clipped(commands[1], sequential, {0, 0, 40, 40}, 0, 0, clips, 1);

    check(batched.pixels.size() == sequential.pixels.size(), "batched frame size matches sequential frame");
    for (std::size_t index = 0; index < batched.pixels.size(); ++index) {
        const Color left = batched.pixels[index];
        const Color right = sequential.pixels[index];
        check(left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a,
              "batched rounded clip commands preserve opaque paint order and coverage");
    }
}

void rasterizer_tracks_opaque_rounded_clip_compositing_without_changing_alpha() {
    DisplayCommand opaque = black_fill(Rect{0, 0, 40, 40});
    opaque.color = Color{20, 120, 240, 255};
    DisplayCommand translucent = black_fill(Rect{16, 16, 8, 8});
    translucent.type = DisplayCommandType::LinearGradient;
    translucent.color = Color{240, 80, 70, 128};
    translucent.color2 = Color{220, 60, 120, 128};
    const DisplayCommand commands[] = {opaque, translucent};
    const RasterClip clips[] = {{{8, 8, 24, 24}, 8}};
    FrameBuffer batched(40, 40, Color{255, 255, 255, 255});
    FrameBuffer sequential(40, 40, Color{255, 255, 255, 255});
    SoftwareRasterizerStatistics statistics;
    SoftwareRasterizer accelerated({}, nullptr, {0, &statistics});
    SoftwareRasterizer reference;

    accelerated.rasterize_clipped(commands, 2, batched, {0, 0, 40, 40}, 0, 0, clips, 1);
    reference.rasterize_clipped(commands[0], sequential, {0, 0, 40, 40}, 0, 0, clips, 1);
    reference.rasterize_clipped(commands[1], sequential, {0, 0, 40, 40}, 0, 0, clips, 1);

    for (std::size_t index = 0; index < batched.pixels.size(); ++index) {
        const Color left = batched.pixels[index];
        const Color right = sequential.pixels[index];
        check(left.r == right.r && left.g == right.g && left.b == right.b && left.a == right.a,
              "opaque rounded composite fast path preserves translucent paint order");
    }
    check(statistics.rounded_clip_opaque_direct_pixels > 0,
          "opaque rounded clip composite records direct inner pixels");
    check(statistics.rounded_clip_blended_pixels > 0,
          "rounded clip composite retains blended edge or translucent pixels");
    check(statistics.rounded_clip_full_coverage_pixels > 0 &&
              statistics.rounded_clip_coverage_sampled_pixels > 0,
          "rounded clip composite distinguishes center rows from antialiased corner rows");
    check(statistics.rounded_clip_replayed_commands_by_type[
              static_cast<std::size_t>(DisplayCommandType::FillRect)] == 1 &&
              statistics.rounded_clip_replayed_commands_by_type[
                  static_cast<std::size_t>(DisplayCommandType::LinearGradient)] == 1,
          "rounded clip composite attributes command types replayed into its temporary surface");
    check(statistics.rounded_clip_replay_candidate_pixels_by_type[
              static_cast<std::size_t>(DisplayCommandType::FillRect)] == 576 &&
              statistics.rounded_clip_replay_candidate_pixels_by_type[
                  static_cast<std::size_t>(DisplayCommandType::LinearGradient)] == 64,
          "rounded clip composite attributes overlapping candidate replay areas by command type");
}

void rasterizer_records_opt_in_rounded_clip_replay_timing() {
    DisplayCommand fill = black_fill(Rect{0, 0, 40, 40});
    fill.color = Color{20, 120, 240, 255};
    DisplayCommand gradient = fill;
    gradient.type = DisplayCommandType::LinearGradient;
    gradient.color2 = Color{70, 180, 255, 255};
    const DisplayCommand commands[] = {fill, gradient};
    const RasterClip clips[] = {{{8, 8, 24, 24}, 8}};
    const std::uint64_t samples[] = {100, 107, 110, 121};
    ReplayTimingClock clock{samples, 4, 0};
    SoftwareRasterizerStatistics statistics;
    SoftwareRasterizer rasterizer({},
                                  nullptr,
                                  {0, &statistics, {replay_timing_clock, &clock}});
    FrameBuffer frame(40, 40, Color{255, 255, 255, 255});

    rasterizer.rasterize_clipped(commands, 2, frame, {0, 0, 40, 40}, 0, 0, clips, 1);

    check(clock.calls == 4, "opt-in timing reads a clock only around rounded command replay");
    check(statistics.rounded_clip_replay_microseconds_by_type[
              static_cast<std::size_t>(DisplayCommandType::FillRect)] == 7 &&
              statistics.rounded_clip_replay_microseconds_by_type[
                  static_cast<std::size_t>(DisplayCommandType::LinearGradient)] == 11 &&
              statistics.rounded_clip_replay_microseconds == 18,
          "opt-in timing attributes rounded command replay duration by type");
    check(statistics.rounded_clip_replay_timing_invalid_samples == 0,
          "monotonic replay timing does not produce invalid samples");

    const std::uint64_t invalid_samples[] = {50, 49};
    ReplayTimingClock invalid_clock{invalid_samples, 2, 0};
    SoftwareRasterizerStatistics invalid_statistics;
    SoftwareRasterizer invalid_rasterizer({},
                                          nullptr,
                                          {0, &invalid_statistics, {replay_timing_clock, &invalid_clock}});
    invalid_rasterizer.rasterize_clipped(fill, frame, {0, 0, 40, 40}, 0, 0, clips, 1);
    check(invalid_statistics.rounded_clip_replay_microseconds == 0 &&
              invalid_statistics.rounded_clip_replay_timing_invalid_samples == 1,
          "non-monotonic replay timing samples are rejected without underflow");
}

void rasterizer_skips_rounded_clip_surface_when_dirty_rect_misses_corners() {
    DisplayCommand fill = black_fill(Rect{0, 0, 40, 40});
    fill.color = Color{20, 120, 240, 255};
    const RasterClip clips[] = {{{0, 0, 40, 40}, 12}};
    FrameBuffer frame(40, 40, Color{255, 255, 255, 255});
    SoftwareRasterizerScratch scratch;
    scratch.temporary_surface = FrameBuffer(1, 1, Color{1, 2, 3, 255});
    SoftwareRasterizerStatistics statistics;
    SoftwareRasterizer rasterizer({}, nullptr, {0, &statistics});

    rasterizer.rasterize_clipped(&fill, 1, frame, {12, 16, 16, 8}, 0, 0, clips, 1, &scratch);

    check(frame.pixel(12, 16).b > 200 && frame.pixel(27, 23).b > 200,
          "centered dirty clip paints without changing rounded coverage");
    check(frame.pixel(11, 16).r == 255 && frame.pixel(28, 23).r == 255,
          "rounded dirty fast path preserves pixels outside the dirty rect");
    check(scratch.temporary_surface.width == 1 && scratch.temporary_surface.height == 1,
          "centered dirty clip does not allocate a rounded temporary surface");
    check(statistics.rounded_clip_rectangular_fast_paths == 1 && statistics.rounded_clip_runs == 0,
          "centered dirty clip records the rectangular rounded-clip fast path");
}

void compositor_offsets_rounded_overflow_clip_with_layer_transform() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 48, 40};

    auto clip = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    clip->type = LayerType::Clip;
    clip->bounds = Rect{8, 8, 24, 24};
    clip->clip_rect = clip->bounds;
    clip->has_clip = true;
    clip->clip_border_radius = 8;
    clip->transform.translate_x = 4.0F;
    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Paint;
    child->bounds = Rect{0, 0, 40, 40};
    DisplayCommand fill = black_fill(Rect{0, 0, 40, 40});
    fill.color = Color{20, 120, 240, 255};
    child->display_list.push_back(fill);
    clip->children.push_back(std::move(child));
    root.children.push_back(std::move(clip));

    const FrameBuffer frame = SoftwareCompositor().render(root, 48, 40, Color{255, 255, 255, 255});
    check(frame.pixel(12, 8).r == 255 && frame.pixel(12, 8).g == 255,
          "translated rounded overflow clip excludes its moved corner");
    check(frame.pixel(24, 8).b > 200,
          "translated rounded overflow clip keeps its moved top edge");
}

void dirty_render_skips_contained_dirty_rects() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 80, 40};
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{10, 10, 20, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "once";
    command.font_size = 10;
    command.text_single_line = true;
    root.display_list.push_back(std::move(command));

    FrameBuffer frame_buffer(80, 40, Color{255, 255, 255, 255});
    TextPaintCounter counter;
    SoftwareCompositor compositor(TextPainter{counting_text_painter, &counter});
    const Rect dirty_rects[] = {
        Rect{0, 0, 50, 30},
        Rect{12, 12, 4, 4},
        Rect{45, 0, 20, 30},
        Rect{0, 0, 50, 30},
    };
    compositor.render_into(root, frame_buffer, Color{255, 255, 255, 255}, dirty_rects, 3);

    check(counter.calls == 1, "contained, duplicate, and overlapping dirty rects are rendered once");
    check(frame_buffer.pixel(10, 10).r == 0, "normalized dirty rect still paints content");
}

void compositor_skips_covered_opaque_fill_prefix() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 8, 8};
    DisplayCommand red = black_fill(Rect{0, 0, 8, 8});
    red.color = Color{220, 38, 38, 255};
    DisplayCommand green = black_fill(Rect{0, 0, 8, 8});
    green.color = Color{22, 163, 74, 255};
    DisplayCommand blue = black_fill(Rect{0, 0, 8, 8});
    blue.color = Color{37, 99, 235, 255};
    root.display_list = {red, green, blue};

    FrameBuffer frame_buffer(8, 8, Color{255, 0, 255, 255});
    const Rect dirty{1, 1, 6, 6};
    SoftwareCompositor().render_into(root, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1);

    const Color painted = frame_buffer.pixel(3, 3);
    check(painted.r == 37 && painted.g == 99 && painted.b == 235,
          "last opaque prefix fill covers the dirty clip");
    check(frame_buffer.pixel(0, 0).r == 255 && frame_buffer.pixel(0, 0).b == 255,
          "opaque prefix optimization keeps pixels outside dirty clip");
}

void compositor_keeps_non_fill_prefix_side_effects() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 8, 8};
    DisplayCommand text;
    text.type = DisplayCommandType::Text;
    text.rect = Rect{1, 1, 4, 4};
    text.color = Color{0, 0, 0, 255};
    text.text = "x";
    root.display_list.push_back(std::move(text));
    DisplayCommand cover = black_fill(Rect{0, 0, 8, 8});
    cover.color = Color{255, 255, 255, 255};
    root.display_list.push_back(std::move(cover));

    TextPaintCounter counter;
    FrameBuffer frame_buffer(8, 8, Color{0, 0, 0, 255});
    SoftwareCompositor compositor(TextPainter{counting_text_painter, &counter});
    compositor.render_into(root, frame_buffer, Color{255, 0, 255, 255});

    check(counter.calls == 1, "non-fill prefix remains rasterized before opaque cover");
    check(frame_buffer.pixel(2, 2).r == 255, "later opaque fill still determines final pixels");
}

void compositor_keeps_rounded_fill_underpaint() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 8, 8};
    DisplayCommand red = black_fill(Rect{0, 0, 8, 8});
    red.color = Color{220, 38, 38, 255};
    DisplayCommand blue = black_fill(Rect{0, 0, 8, 8});
    blue.color = Color{37, 99, 235, 255};
    blue.border_radius = 3;
    root.display_list = {red, blue};

    FrameBuffer frame_buffer(8, 8, Color{0, 0, 0, 255});
    SoftwareCompositor().render_into(root, frame_buffer, Color{255, 255, 255, 255});

    check(frame_buffer.pixel(0, 0).r == 220, "rounded fill keeps opaque underpaint at corner");
    check(frame_buffer.pixel(4, 4).b == 235, "rounded fill paints its covered center");
}

DisplayCommand white_fill(Rect rect) {
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = Color{255, 255, 255, 255};
    return command;
}

void compositor_smooths_scaled_layers() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 4, 4};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->bounds = Rect{1, 1, 2, 2};
    child->transform.scale_x = 2.0F;
    child->transform.scale_y = 2.0F;
    child->display_list.push_back(white_fill(Rect{1, 1, 2, 2}));
    child->display_list.push_back(black_fill(Rect{1, 1, 1, 2}));
    root.children.push_back(std::move(child));

    const FrameBuffer smooth = SoftwareCompositor().render(root, 4, 4, Color{255, 255, 255, 255});
    SoftwareCompositor::Options nearest_options;
    nearest_options.smooth_scaled_layers = false;
    const FrameBuffer nearest =
        SoftwareCompositor({}, nearest_options).render(root, 4, 4, Color{255, 255, 255, 255});

    check(smooth.pixel(1, 1).r > 0 && smooth.pixel(1, 1).r < 255,
          "scaled layer has bilinear intermediate pixel");
    check(nearest.pixel(1, 1).r == 0, "nearest scaled layer keeps hard edge");
}

void compositor_degrades_oversized_offscreen_layers_without_crashing() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 2, 1};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->opacity = 0.5F;
    child->bounds = Rect{0, 0, 2, 1};
    child->display_list.push_back(black_fill(Rect{0, 0, 1, 1}));
    child->display_list.push_back(black_fill(Rect{0, 0, 1, 1}));
    root.children.push_back(std::move(child));

    const Color white{255, 255, 255, 255};
    const FrameBuffer precise = SoftwareCompositor().render(root, 2, 1, white);
    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options options;
    options.max_offscreen_pixels = 1;
    options.diagnostics = &diagnostics;
    const FrameBuffer degraded =
        SoftwareCompositor({}, options).render(root, 2, 1, white);

    check(precise.pixel(0, 0).r > degraded.pixel(0, 0).r,
          "offscreen budget fallback uses bounded direct compositing");
    check(degraded.pixel(1, 0).r == 255, "fallback keeps untouched pixels");
    check(has_diagnostic_code(diagnostics, "paint-offscreen-budget"), "offscreen fallback is reported");
}

void compositor_keeps_composited_paint_outside_layout_bounds() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 20, 20};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->opacity = 0.8F;
    child->bounds = Rect{6, 6, 8, 8};
    child->display_list.push_back(black_fill(Rect{2, 2, 16, 16}));
    root.children.push_back(std::move(child));

    const FrameBuffer output =
        SoftwareCompositor().render(root, 20, 20, Color{255, 255, 255, 255});
    check(output.pixel(3, 3).r < 255,
          "composited paint outside the layout box is not clipped by the offscreen surface");
    check(output.pixel(1, 1).r == 255, "expanded offscreen surface remains bounded to painted content");
}

void compositor_keeps_nested_composited_paint_outside_parent_layout_bounds() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 64, 64};

    auto parent = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    parent->type = LayerType::Composited;
    parent->opacity = 0.8F;
    parent->bounds = Rect{30, 30, 10, 10};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->bounds = Rect{34, 34, 6, 6};
    child->transform.scale_x = 0.75F;
    child->transform.scale_y = 0.75F;
    DisplayCommand red = black_fill(Rect{22, 33, 18, 6});
    red.color = Color{220, 38, 38, 255};
    child->display_list.push_back(red);
    parent->children.push_back(std::move(child));
    root.children.push_back(std::move(parent));

    const FrameBuffer output =
        SoftwareCompositor().render(root, 64, 64, Color{255, 255, 255, 255});
    check(output.pixel(27, 35).r < 250 && output.pixel(27, 35).g < 250,
          "parent compositing bounds include transformed child paint outside the parent layout box");
    check(output.pixel(20, 35).r == 255,
          "nested visual bounds remain limited to the transformed child paint");
}

void compositor_expanded_visual_bounds_keep_border_box_transform_origin() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 20, 20};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->bounds = Rect{8, 8, 4, 4};
    child->transform.scale_x = 0.5F;
    child->transform.scale_y = 0.5F;
    child->display_list.push_back(black_fill(Rect{4, 8, 8, 4}));
    root.children.push_back(std::move(child));

    SoftwareCompositor::Options options;
    options.smooth_scaled_layers = false;
    const FrameBuffer output =
        SoftwareCompositor({}, options).render(root, 20, 20, Color{255, 255, 255, 255});
    check(output.pixel(10, 9).r == 0,
          "expanded visual bounds scale around the border-box transform origin");
    check(output.pixel(6, 9).r == 255,
          "expanded visual bounds do not move the transform origin to the paint bounds");
}

void compositor_bounds_nested_live_offscreen_pixels() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 2, 2};

    auto parent = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    parent->type = LayerType::Composited;
    parent->opacity = 0.5F;
    parent->bounds = Rect{0, 0, 2, 2};
    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->opacity = 0.5F;
    child->bounds = Rect{0, 0, 2, 2};
    child->display_list.push_back(black_fill(Rect{0, 0, 2, 2}));
    parent->children.push_back(std::move(child));
    root.children.push_back(std::move(parent));

    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options options;
    options.max_offscreen_pixels = 4;
    options.diagnostics = &diagnostics;
    const FrameBuffer output = SoftwareCompositor({}, options).render(root, 2, 2, Color{255, 255, 255, 255});

    check(output.pixel(0, 0).r < 255, "aggregate offscreen fallback still paints visible content");
    check(has_diagnostic_code(diagnostics, "paint-offscreen-budget"),
          "nested offscreen aggregate cap is reported with the stable budget code");
    check(has_diagnostic_message_fragment(diagnostics, "aggregate live budget"),
          "nested offscreen rejection distinguishes aggregate live usage");
}

void compositor_skips_oversized_transformed_layers_instead_of_painting_them_untransformed() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 4, 4};

    auto child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    child->type = LayerType::Composited;
    child->bounds = Rect{0, 0, 4, 4};
    child->transform.scale_x = 0.5F;
    child->transform.scale_y = 0.5F;
    child->display_list.push_back(black_fill(Rect{0, 0, 4, 4}));
    root.children.push_back(std::move(child));

    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options options;
    options.max_offscreen_pixels = 1;
    options.diagnostics = &diagnostics;
    const FrameBuffer output = SoftwareCompositor({}, options).render(root, 4, 4, Color{255, 255, 255, 255});

    check(output.pixel(0, 0).r == 255,
          "oversized transformed layer does not silently paint an untransformed result");
    check(has_diagnostic_code(diagnostics, "paint-transform-budget"),
          "transformed budget fallback is reported");
}

void compositor_rejects_oversized_framebuffer_before_allocation() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 4, 4};
    root.display_list.push_back(black_fill(Rect{0, 0, 4, 4}));

    VectorDiagnosticSink diagnostics;
    SoftwareCompositor::Options rejecting_options;
    rejecting_options.max_framebuffer_pixels = 3;
    rejecting_options.diagnostics = &diagnostics;
    const FrameBuffer rejected =
        SoftwareCompositor({}, rejecting_options).render(root, 4, 4, Color{255, 255, 255, 255});
    const FrameBuffer accepted =
        SoftwareCompositor({}, SoftwareCompositor::Options{16, 0}).render(root, 4, 4, Color{255, 255, 255, 255});

    check(rejected.width == 0 && rejected.height == 0 && rejected.pixels.empty(),
          "framebuffer budget rejects oversized render before allocation");
    check(accepted.width == 4 && accepted.height == 4, "framebuffer at budget renders normally");
    check(has_diagnostic_code(diagnostics, "paint-framebuffer-budget"), "framebuffer rejection is reported");
}

void rasterizer_reports_text_fallback() {
    VectorDiagnosticSink diagnostics;
    SoftwareRasterizer rasterizer({}, &diagnostics);
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 80, 20};
    command.color = Color{0, 0, 0, 255};
    command.text = "\xe4\xb8\xad\xe6\x96\x87";
    command.font_size = 14;
    command.text_single_line = true;
    FrameBuffer frame(80, 20, Color{255, 255, 255, 255});

    rasterizer.rasterize(command, frame, Rect{0, 0, 80, 20});

    check(has_diagnostic_code(diagnostics, "paint-non-ascii-fallback"), "non-ascii fallback is reported");

    VectorDiagnosticSink backend_diagnostics;
    SoftwareRasterizer rejecting_rasterizer(TextPainter{rejecting_text_painter, nullptr}, &backend_diagnostics);
    command.text = "ASCII";
    rejecting_rasterizer.rasterize(command, frame, Rect{0, 0, 80, 20});
    check(has_diagnostic_code(backend_diagnostics, "paint-text-backend-failed"),
          "text backend rejection is reported");
}

void dirty_text_clip_preserves_original_text_geometry() {
    FrameBuffer frame_buffer(40, 10, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{center_pixel_text_painter, nullptr});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 40, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "center";
    command.font_size = 10;
    command.text_single_line = true;

    rasterizer.rasterize(command, frame_buffer, Rect{20, 0, 5, 10});

    check(frame_buffer.pixel(20, 5).r == 0, "partial text clip keeps original command geometry");
    check(frame_buffer.pixel(22, 5).r == 255, "partial text clip does not recenter text inside dirty rect");
}

void rasterizer_scratch_reuses_clipped_command_storage() {
    FrameBuffer frame_buffer(40, 10, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{center_pixel_text_painter, nullptr});
    SoftwareRasterizerScratch scratch;
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 40, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "center";
    command.font_size = 10;
    command.text_single_line = true;
    const Rect dirty{20, 0, 5, 10};

    rasterizer.rasterize(command, frame_buffer, dirty, 0, 0, &scratch);
    const std::size_t capacity = scratch.temporary_surface.pixels.capacity();
    check(capacity >= 50, "scratch retains clipped text surface storage");

    rasterizer.rasterize(command, frame_buffer, dirty, 0, 0, &scratch);
    check(scratch.temporary_surface.pixels.capacity() == capacity,
          "repeated clipped text paint reuses scratch storage");
    check(frame_buffer.pixel(20, 5).r == 0, "scratch preserves clipped text output");

    scratch.release();
    check(scratch.temporary_surface.pixels.empty() && scratch.temporary_surface.pixels.capacity() == 0,
          "scratch release returns temporary surface storage");
}

void rasterizer_bounds_clipped_temporary_surfaces() {
    FrameBuffer frame_buffer(40, 10, Color{255, 255, 255, 255});
    VectorDiagnosticSink diagnostics;
    SoftwareRasterizer rasterizer(TextPainter{center_pixel_text_painter, nullptr},
                                  &diagnostics,
                                  SoftwareRasterizerOptions{16});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 40, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "center";
    command.font_size = 10;
    command.text_single_line = true;

    rasterizer.rasterize(command, frame_buffer, Rect{20, 0, 5, 10});

    check(frame_buffer.pixel(20, 5).r == 255, "oversized clipped text surface is skipped safely");
    check(has_diagnostic_code(diagnostics, "paint-transient-surface-budget"),
          "clipped temporary surface budget rejection is reported");

    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 40, 10};
    root.display_list.push_back(command);
    SoftwareCompositor::Options compositor_options;
    compositor_options.max_offscreen_pixels = 16;
    compositor_options.diagnostics = &diagnostics;
    SoftwareCompositor compositor(TextPainter{center_pixel_text_painter, nullptr}, compositor_options);
    const Rect dirty{20, 0, 5, 10};
    compositor.render_into(root, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1);
    check(frame_buffer.pixel(20, 5).r == 255,
          "compositor propagates the temporary surface budget to clipped paint");
}

void compositor_scratch_reuses_clipped_command_storage() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = Rect{0, 0, 40, 10};
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 40, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "center";
    command.font_size = 10;
    command.text_single_line = true;
    root.display_list.push_back(command);

    FrameBuffer frame_buffer(40, 10, Color{255, 255, 255, 255});
    SoftwareCompositor::Scratch scratch;
    const Rect dirty{20, 0, 5, 10};
    SoftwareCompositor compositor(TextPainter{center_pixel_text_painter, nullptr});
    compositor.render_into(root, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1, &scratch);
    const std::size_t capacity = scratch.rasterizer.temporary_surface.pixels.capacity();
    check(capacity >= 50, "compositor retains clipped command scratch storage");

    compositor.render_into(root, frame_buffer, Color{255, 255, 255, 255}, &dirty, 1, &scratch);
    check(scratch.rasterizer.temporary_surface.pixels.capacity() == capacity,
          "compositor reuses clipped command scratch storage");
}

void rasterizer_scratch_reuses_clipped_image_storage() {
    FrameBuffer frame_buffer(40, 10, Color{255, 255, 255, 255});
    ImagePaintProbe probe;
    probe.expected_handle = 41;
    SoftwareRasterizer rasterizer({}, ImagePainter{probe_image_painter, &probe});
    SoftwareRasterizerScratch scratch;
    DisplayCommand command;
    command.type = DisplayCommandType::Image;
    command.rect = Rect{0, 0, 40, 10};
    command.image_handle = probe.expected_handle;
    const Rect dirty{20, 0, 5, 10};

    rasterizer.rasterize(command, frame_buffer, dirty, 0, 0, &scratch);
    const std::size_t capacity = scratch.temporary_surface.pixels.capacity();
    check(capacity >= 50, "scratch retains clipped image surface storage");

    rasterizer.rasterize(command, frame_buffer, dirty, 0, 0, &scratch);
    check(probe.calls == 2, "image painter runs for each clipped repaint");
    check(scratch.temporary_surface.pixels.capacity() == capacity,
          "repeated clipped image paint reuses scratch storage");
    check(frame_buffer.pixel(20, 0).r == 220, "scratch preserves clipped image output");
}

struct FrameSinkProbe {
    int width = 0;
    int height = 0;
    std::size_t dirty_count = 0;
};

bool probe_present(const HostFrameBufferView& frame,
                   const Rect*,
                   std::size_t dirty_count,
                   void* context) {
    auto* probe = static_cast<FrameSinkProbe*>(context);
    probe->width = frame.width;
    probe->height = frame.height;
    probe->dirty_count = dirty_count;
    return frame.pixels != nullptr;
}

void frame_sink_receives_framebuffer_view_and_dirty_rects() {
    FrameBuffer frame_buffer(8, 6, Color{255, 255, 255, 255});
    FrameSinkProbe probe;
    const HostFrameSink sink{probe_present, &probe};
    const Rect dirty{1, 1, 2, 2};

    check(present_frame(frame_buffer, sink, &dirty, 1), "frame sink present succeeds");
    check(probe.width == 8 && probe.height == 6, "frame sink receives dimensions");
    check(probe.dirty_count == 1, "frame sink receives dirty count");
}

void bitmap_font_backend_measures_and_paints() {
    static constexpr std::uint8_t rows_a[] = {
        0b01000000,
        0b10100000,
        0b11100000,
        0b10100000,
        0b10100000,
    };
    static constexpr std::uint8_t rows_cjk[] = {
        0b11111111, 0b10000000,
        0b00010000, 0b00000000,
        0b11111111, 0b10000000,
        0b00010000, 0b00000000,
        0b11111111, 0b10000000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 3, 5, 4, 1, rows_a},
        BitmapFontGlyph{0x4e2d, 9, 5, 10, 2, rows_cjk},
    };
    static constexpr BitmapFont font{glyphs, 2, 6, 4};
    BitmapFontContext context{&font, 2};

    TextMetrics metrics;
    check(bitmap_font_measure_callback("AA", 12, 400, &metrics, &context), "bitmap font measure callback succeeds");
    check(metrics.width == 16 && metrics.line_height == 12, "bitmap font metrics scale advances");
    check(bitmap_font_measure_callback("\xe4\xb8\xad?", 12, 400, &metrics, &context),
          "bitmap font measures utf-8 text");
    check(metrics.width == 28 && metrics.line_height == 12, "wide utf-8 glyph and fallback advance are stable");

    FrameBuffer frame_buffer(32, 16, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 32, 16};
    command.color = Color{0, 0, 0, 255};
    command.text = "A";
    command.font_size = 12;
    command.text_align = TextCommandAlign::Center;
    command.text_single_line = true;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 32, 16});

    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0,
          "bitmap font painter writes pixels");
    check(frame_buffer.pixel(0, 0).r == 255, "centered bitmap glyph leaves left edge empty");

    FrameBuffer utf8_frame_buffer(40, 16, Color{255, 255, 255, 255});
    DisplayCommand utf8_command;
    utf8_command.type = DisplayCommandType::Text;
    utf8_command.rect = Rect{0, 0, 40, 16};
    utf8_command.color = Color{0, 0, 0, 255};
    utf8_command.text = "\xe4\xb8\xad?";
    utf8_command.font_size = 12;
    utf8_command.text_single_line = true;
    rasterizer.rasterize(utf8_command, utf8_frame_buffer, Rect{0, 0, 40, 16});

    check(count_non_background_pixels(utf8_frame_buffer, Color{255, 255, 255, 255}) > 20,
          "wide utf-8 glyph and missing fallback draw visible pixels");
}

void bitmap_font_lookup_uses_sorted_codepoints() {
    static constexpr std::uint8_t row[] = {0b10000000};
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x20, 1, 1, 2, 1, row},
        BitmapFontGlyph{0x41, 1, 1, 3, 1, row},
        BitmapFontGlyph{0x4e2d, 1, 1, 4, 1, row},
        BitmapFontGlyph{0x1f600, 1, 1, 5, 1, row},
    };
    static constexpr BitmapFont font{glyphs, 4, 2, 1};

    const BitmapFontGlyph* ascii = find_bitmap_glyph(font, 0x41);
    const BitmapFontGlyph* cjk = find_bitmap_glyph(font, 0x4e2d);
    const BitmapFontGlyph* emoji = find_bitmap_glyph(font, 0x1f600);

    check(ascii != nullptr && ascii->advance == 3, "bitmap font lookup finds ASCII glyph");
    check(cjk != nullptr && cjk->advance == 4, "bitmap font lookup finds CJK glyph");
    check(emoji != nullptr && emoji->advance == 5, "bitmap font lookup finds high codepoint glyph");
    check(find_bitmap_glyph(font, 0x42) == nullptr, "bitmap font lookup reports missing glyph");
}

void bitmap_font_scaling_bold_and_missing_glyphs_are_stable() {
    static constexpr std::uint8_t rows_a[] = {
        0b10000000,
        0b10000000,
        0b10000000,
    };
    static constexpr std::uint8_t rows_wide[] = {
        0b11111111, 0b10000000,
        0b10000000, 0b10000000,
        0b11111111, 0b10000000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x41, 1, 3, 2, 1, rows_a},
        BitmapFontGlyph{0xff0c, 9, 3, 10, 2, rows_wide},
    };
    static constexpr BitmapFont font{glyphs, 2, 4, 3};
    BitmapFontContext context{&font, 3};

    const TextMetrics metrics = measure_bitmap_text(context, "A\xef\xbc\x8c?", 18, 700);
    check(metrics.width == 46, "bitmap font metrics include scale, wide punctuation, fallback and bold stroke");
    check(metrics.line_height == 12, "bitmap font metrics scale line height");

    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand normal;
    normal.type = DisplayCommandType::Text;
    normal.rect = Rect{0, 0, 64, 24};
    normal.color = Color{0, 0, 0, 255};
    normal.text = "A";
    normal.font_size = 18;
    normal.font_weight = 400;
    normal.text_single_line = true;

    DisplayCommand bold = normal;
    bold.font_weight = 700;

    FrameBuffer normal_frame(64, 24, Color{255, 255, 255, 255});
    FrameBuffer bold_frame(64, 24, Color{255, 255, 255, 255});
    rasterizer.rasterize(normal, normal_frame, Rect{0, 0, 64, 24});
    rasterizer.rasterize(bold, bold_frame, Rect{0, 0, 64, 24});

    const int normal_pixels = count_non_background_pixels(normal_frame, Color{255, 255, 255, 255});
    const int bold_pixels = count_non_background_pixels(bold_frame, Color{255, 255, 255, 255});
    check(bold_pixels > normal_pixels, "bitmap font bold approximation paints an extra stroke");

    DisplayCommand missing = normal;
    missing.text = "\xf0\x9f\x98\x80";
    FrameBuffer missing_frame(64, 24, Color{255, 255, 255, 255});
    rasterizer.rasterize(missing, missing_frame, Rect{0, 0, 64, 24});
    check(count_non_background_pixels(missing_frame, Color{255, 255, 255, 255}) > 8,
          "missing high-codepoint glyph draws a visible fallback box");
}

void bitmap_font_bold_metrics_include_extra_stroke() {
    static constexpr std::uint8_t rows_w[] = {
        0b10001000,
        0b10001000,
        0b10101000,
        0b10101000,
        0b01010000,
    };
    static constexpr BitmapFontGlyph glyphs[] = {
        BitmapFontGlyph{0x57, 5, 5, 6, 1, rows_w},
    };
    static constexpr BitmapFont font{glyphs, 1, 6, 5};
    BitmapFontContext context{&font, 1};

    const TextMetrics normal = measure_bitmap_text(context, "W", 12, 400);
    const TextMetrics bold = measure_bitmap_text(context, "W", 12, 700);
    check(bold.width == normal.width + 1, "bold bitmap metrics reserve the synthetic stroke");

    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, bold.width, 10};
    command.color = Color{0, 0, 0, 255};
    command.text = "W";
    command.font_size = 12;
    command.font_weight = 700;
    command.text_single_line = true;

    FrameBuffer frame_buffer(bold.width, 10, Color{255, 255, 255, 255});
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, bold.width, 10});
    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 0,
          "bold synthetic stroke paints inside measured text rect");
}

void jffont_resource_loads_bitmap_font_view() {
    const std::vector<std::uint8_t> bytes = {
        'J', 'F', 'F', 'O', 'N', 'T', '0', 0,
        0x20, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
        0x08, 0x08, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x07, 0x00, 0x00, 0x00, 0x05, 0x07, 0x06, 0x01,
        0x2d, 0x4e, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x01,
        0x20, 0x50, 0x88, 0xf8, 0x88, 0x88, 0x88,
        0x10, 0x10, 0xfe, 0x92, 0x92, 0xfe, 0x10, 0x10,
    };

    BitmapFontResource resource;
    check(resource.load_jffont(bytes.data(), bytes.size()), "jffont resource loads");
    check(resource.valid(), "jffont resource exposes valid font");
    BitmapFontContext context{&resource.font(), 1};

    const TextMetrics metrics = measure_bitmap_text(context, "A\xe4\xb8\xad?", 8, 400);
    check(metrics.width == 22 && metrics.line_height == 8, "jffont metrics reuse bitmap backend");

    FrameBuffer frame_buffer(32, 12, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 32, 12};
    command.color = Color{0, 0, 0, 255};
    command.text = "A\xe4\xb8\xad";
    command.font_size = 8;
    command.text_single_line = true;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 32, 12});
    check(count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) > 10,
          "jffont glyphs paint through bitmap backend");

    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[0] = 0;
    check(!resource.load_jffont(corrupted.data(), corrupted.size()), "jffont rejects bad magic");
    check(!resource.valid(), "failed jffont load clears font view");

    corrupted = bytes;
    corrupted[40] = 0x01;
    corrupted[41] = 0x00;
    corrupted[42] = 0x00;
    corrupted[43] = 0x00;
    check(!resource.load_jffont(corrupted.data(), corrupted.size()), "jffont rejects short glyph row data");
}

void jffont_v1_coverage_glyphs_antialias_text_edges() {
    const std::vector<std::uint8_t> bytes = {
        'J', 'F', 'F', 'O', 'N', 'T', '0', 0,
        0x20, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
        0x03, 0x03, 0x04, 0x00, 0x20, 0x00, 0x00, 0x00,
        0x30, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x03, 0x03, 0x04, 0x02,
        0xf0, 0x00, 0x70, 0x00, 0xf0, 0x00,
    };

    BitmapFontResource resource;
    check(resource.load_jffont(bytes.data(), bytes.size()), "jffont v1 coverage resource loads");
    const BitmapFontGlyph* glyph = find_bitmap_glyph(resource.font(), 0x41);
    check(glyph != nullptr && glyph->bits_per_pixel == 4, "jffont v1 exposes 4bpp glyph");

    std::vector<std::uint8_t> corrupted = bytes;
    corrupted[19] = 0x01;
    BitmapFontResource rejected_resource;
    check(!rejected_resource.load_jffont(corrupted.data(), corrupted.size()), "jffont v1 rejects unknown flags");

    BitmapFontContext context{&resource.font(), 1};
    FrameBuffer frame_buffer(8, 6, Color{255, 255, 255, 255});
    SoftwareRasterizer rasterizer(TextPainter{bitmap_font_paint_callback, &context});
    DisplayCommand command;
    command.type = DisplayCommandType::Text;
    command.rect = Rect{0, 0, 8, 6};
    command.color = Color{0, 0, 0, 255};
    command.text = "A";
    command.font_size = 3;
    command.text_single_line = true;
    rasterizer.rasterize(command, frame_buffer, Rect{0, 0, 8, 6});

    bool has_gray_edge = false;
    for (int y = 0; y < frame_buffer.height; ++y) {
        for (int x = 0; x < frame_buffer.width; ++x) {
            const Color pixel = frame_buffer.pixel(x, y);
            if (pixel.r > 0 && pixel.r < 255) {
                has_gray_edge = true;
            }
        }
    }
    check(has_gray_edge, "coverage glyph paints intermediate edge pixels");
}

} // namespace

int main() {
    try {
        fill_rect_rasterizes_pixels();
        rounded_image_clip_preserves_corner_underpaint();
        linear_gradient_rasterizes_top_and_bottom_colors();
        horizontal_linear_gradient_rasterizes_left_and_right_colors();
        diagonal_linear_gradient_rasterizes_corner_colors();
        opaque_linear_gradient_fast_path_preserves_dirty_clip();
        opaque_linear_gradient_fast_path_preserves_all_axis_interpolation();
        conic_gradient_rasterizes_clockwise_progress();
        radial_gradient_rasterizes_center_to_edge();
        radial_gradient_keeps_diagonal_falloff_close_to_axis();
        positioned_radial_gradient_moves_highlight_center();
        soft_box_shadow_fades_outside_rounded_card();
        circular_box_shadow_keeps_diagonal_falloff_close_to_axis();
        rounded_stroke_keeps_corner_pixels_clear();
        square_stroke_paints_all_four_edges();
        rounded_stroke_keeps_straight_edges_visible();
        rounded_fill_antialiases_edge_pixels();
        per_corner_rounded_rect_keeps_square_bottom_left();
        source_over_alpha_composites();
        clipping_limits_rasterization();
        image_command_uses_injected_painter();
        image_command_falls_back_without_painter();
        compositor_renders_pipeline_non_empty();
        wrapped_text_layout_keeps_descent_padding();
        layout_uses_injected_text_measurement();
        dirty_render_only_updates_requested_clip();
        dirty_render_preserves_original_rounded_geometry();
        compositor_clips_children_to_rounded_overflow();
        rasterizer_applies_value_rounded_clip_chain();
        rasterizer_batches_consecutive_value_clip_commands();
        rasterizer_tracks_opaque_rounded_clip_compositing_without_changing_alpha();
        rasterizer_records_opt_in_rounded_clip_replay_timing();
        rasterizer_skips_rounded_clip_surface_when_dirty_rect_misses_corners();
        compositor_offsets_rounded_overflow_clip_with_layer_transform();
        dirty_render_skips_contained_dirty_rects();
        compositor_skips_covered_opaque_fill_prefix();
        compositor_keeps_non_fill_prefix_side_effects();
        compositor_keeps_rounded_fill_underpaint();
        rasterizer_reports_text_fallback();
        dirty_text_clip_preserves_original_text_geometry();
        rasterizer_scratch_reuses_clipped_command_storage();
        rasterizer_bounds_clipped_temporary_surfaces();
        compositor_scratch_reuses_clipped_command_storage();
        rasterizer_scratch_reuses_clipped_image_storage();
        compositor_smooths_scaled_layers();
        compositor_degrades_oversized_offscreen_layers_without_crashing();
        compositor_keeps_composited_paint_outside_layout_bounds();
        compositor_keeps_nested_composited_paint_outside_parent_layout_bounds();
        compositor_expanded_visual_bounds_keep_border_box_transform_origin();
        compositor_bounds_nested_live_offscreen_pixels();
        compositor_skips_oversized_transformed_layers_instead_of_painting_them_untransformed();
        compositor_rejects_oversized_framebuffer_before_allocation();
        frame_sink_receives_framebuffer_view_and_dirty_rects();
        bitmap_font_backend_measures_and_paints();
        bitmap_font_lookup_uses_sorted_codepoints();
        bitmap_font_scaling_bold_and_missing_glyphs_are_stable();
        bitmap_font_bold_metrics_include_extra_stroke();
        jffont_resource_loads_bitmap_font_view();
        jffont_v1_coverage_glyphs_antialias_text_edges();
    } catch (const std::exception& error) {
        std::cerr << "software renderer test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "software renderer tests passed\n";
    return 0;
}
