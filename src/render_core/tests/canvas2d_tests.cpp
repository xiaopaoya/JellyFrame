#include "render_core/canvas2d.h"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

bool fixed_measure(const std::string& text,
                   int font_size,
                   int,
                   TextMetrics* metrics,
                   void*) {
    if (metrics == nullptr) {
        return false;
    }
    metrics->width = static_cast<int>(text.size()) * std::max(1, font_size / 2);
    metrics->line_height = font_size + 4;
    return true;
}

bool marker_paint(FrameBuffer& target,
                  Rect rect,
                  Color color,
                  const std::string&,
                  int,
                  int,
                  TextCommandAlign,
                  bool,
                  void*) {
    if (target.contains(rect.x, rect.y)) {
        target.pixel(rect.x, rect.y) = color;
        return true;
    }
    return false;
}

void canvas_surface_is_lazy_and_budgeted() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        16,
        16,
        4,
        4,
    });
    auto canvas = make_element("canvas");
    assert(registry.handle_for(*canvas) == 0);

    const std::uint32_t handle = registry.ensure_surface(*canvas);
    assert(handle != 0);
    assert(is_canvas2d_handle(handle));
    assert(registry.handle_for(*canvas) == handle);

    auto second = make_element("canvas");
    assert(registry.ensure_surface(*second) == 0);
}

void oversized_canvas_is_rejected() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        4,
        16,
        64,
        4,
        4,
    });
    auto canvas = make_element("canvas");
    canvas->set_attribute("width", "12");
    canvas->set_attribute("height", "12");
    assert(registry.ensure_surface(*canvas) == 0);
}

void drawing_updates_pixels_and_marks_paint_dirty() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    clear_dirty_flags(*canvas);

    assert(registry.set_fill_style(*canvas, "#123456"));
    assert(registry.fill_rect(*canvas, 1, 1, 3, 2));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    const Color pixel = surface->pixels[1 * surface->width + 1];
    assert(pixel.r == 0x12);
    assert(pixel.g == 0x34);
    assert(pixel.b == 0x56);
    assert((canvas->dirty_flags & DomDirtyPaint) != 0);

    assert(registry.clear_rect(*canvas, 1, 1, 1, 1));
    const Color cleared = surface->pixels[1 * surface->width + 1];
    assert(cleared.a == 0);
}

void path_stroke_draws_line() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_stroke_style(*canvas, "#ffffff"));
    assert(registry.set_line_width(*canvas, 1));
    assert(registry.begin_path(*canvas));
    assert(registry.move_to(*canvas, 0, 0));
    assert(registry.line_to(*canvas, 7, 7));
    assert(registry.stroke(*canvas));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    assert(surface->pixels[7 * surface->width + 7].r == 255);
}

void path_stroke_antialiases_diagonal_edges() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_stroke_style(*canvas, "#ffffff"));
    assert(registry.set_line_width(*canvas, 1));
    assert(registry.begin_path(*canvas));
    assert(registry.move_to(*canvas, 0, 0));
    assert(registry.line_to(*canvas, 7, 7));
    assert(registry.stroke(*canvas));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);

    bool found_partial_edge = false;
    for (const Color& pixel : surface->pixels) {
        if (pixel.a > 0 && pixel.a < 255) {
            found_partial_edge = true;
            break;
        }
    }
    assert(found_partial_edge);
}

void global_alpha_affects_fill_rect() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_fill_style(*canvas, "#ff0000"));
    assert(registry.set_global_alpha(*canvas, 0.5));
    assert(registry.fill_rect(*canvas, 2, 2, 2, 2));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    const Color pixel = surface->pixels[2 * surface->width + 2];
    assert(pixel.r == 255);
    assert(pixel.a >= 126 && pixel.a <= 129);
}

void global_alpha_rejects_out_of_range_values() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_global_alpha(*canvas, 0.5));
    assert(!registry.set_global_alpha(*canvas, 2.0));
    assert(registry.global_alpha(*canvas) == 0.5);
    assert(!registry.set_global_alpha(*canvas, -0.1));
    assert(registry.global_alpha(*canvas) == 0.5);
}

void save_restore_restores_drawing_state() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_fill_style(*canvas, "#ff0000"));
    assert(registry.set_global_alpha(*canvas, 0.25));
    assert(registry.save(*canvas));
    assert(registry.set_fill_style(*canvas, "#00ff00"));
    assert(registry.set_global_alpha(*canvas, 1.0));
    assert(registry.restore(*canvas));
    assert(registry.fill_rect(*canvas, 1, 1, 1, 1));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    const Color pixel = surface->pixels[1 * surface->width + 1];
    assert(pixel.r == 255);
    assert(pixel.g == 0);
    assert(pixel.a >= 62 && pixel.a <= 65);
}

void state_stack_is_bounded() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
        16,
        2,
    });
    auto canvas = make_element("canvas");
    assert(registry.save(*canvas));
    assert(registry.save(*canvas));
    assert(!registry.save(*canvas));
    assert(registry.restore(*canvas));
    assert(registry.restore(*canvas));
    assert(!registry.restore(*canvas));
}

void path_point_budget_is_bounded() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        8,
        8,
        2,
        8,
    });
    auto canvas = make_element("canvas");
    assert(registry.begin_path(*canvas));
    assert(registry.move_to(*canvas, 0, 0));
    assert(registry.line_to(*canvas, 1, 1));
    assert(!registry.line_to(*canvas, 2, 2));
}

void arc_stroke_draws_ring_pixels() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        16,
        16,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_stroke_style(*canvas, "#ffffff"));
    assert(registry.set_line_width(*canvas, 2));
    assert(registry.begin_path(*canvas));
    assert(registry.arc(*canvas, 8.0, 8.0, 5.0, 0.0, 6.283185307179586, false));
    assert(registry.close_path(*canvas));
    assert(registry.stroke(*canvas));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    assert(surface->pixels[8 * surface->width + 13].r == 255);
    assert(surface->pixels[3 * surface->width + 8].a > 0);
}

void fill_path_fills_closed_polygon() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        16,
        16,
    });
    auto canvas = make_element("canvas");
    assert(registry.set_fill_style(*canvas, "#336699"));
    assert(registry.begin_path(*canvas));
    assert(registry.move_to(*canvas, 2, 2));
    assert(registry.line_to(*canvas, 12, 2));
    assert(registry.line_to(*canvas, 12, 12));
    assert(registry.line_to(*canvas, 2, 12));
    assert(registry.close_path(*canvas));
    assert(registry.fill(*canvas));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    const Color pixel = surface->pixels[6 * surface->width + 6];
    assert(pixel.r == 0x33);
    assert(pixel.g == 0x66);
    assert(pixel.b == 0x99);
}

void linear_gradient_fills_rect_and_is_budgeted() {
    Canvas2DPolicy policy;
    policy.max_surfaces = 1;
    policy.max_surface_pixels = 64;
    policy.max_total_pixels = 64;
    policy.default_width = 8;
    policy.default_height = 8;
    policy.max_gradients = 1;
    policy.max_gradient_stops = 2;
    Canvas2DRegistry registry(policy);
    auto canvas = make_element("canvas");
    const std::uint32_t gradient = registry.create_linear_gradient(0.0, 0.0, 8.0, 0.0);
    assert(gradient != 0);
    assert(registry.create_linear_gradient(0.0, 0.0, 8.0, 0.0) == 0);
    assert(registry.add_color_stop(gradient, 0.0, "#000000"));
    assert(registry.add_color_stop(gradient, 1.0, "#ffffff"));
    assert(!registry.add_color_stop(gradient, 0.5, "#ff0000"));
    assert(registry.set_fill_gradient(*canvas, gradient));
    assert(registry.fill_rect(*canvas, 0, 0, 8, 1));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    const Color left = surface->pixels[0];
    const Color right = surface->pixels[7];
    assert(left.r < right.r);
    assert(right.r > 200);

    Canvas2DPolicy no_gradient_policy = policy;
    no_gradient_policy.max_gradients = 0;
    Canvas2DRegistry no_gradient_registry(no_gradient_policy);
    assert(no_gradient_registry.create_linear_gradient(0.0, 0.0, 8.0, 0.0) == 0);
}

void text_metrics_and_fill_text_use_bound_backend() {
    Canvas2DRegistry registry(Canvas2DPolicy{
        true,
        1,
        64,
        64,
        32,
        24,
    });
    registry.set_text_backend(TextMeasureProvider{fixed_measure, nullptr},
                              TextPainter{marker_paint, nullptr});
    auto canvas = make_element("canvas");
    assert(registry.set_font(*canvas, "bold 16px system-ui"));
    assert(registry.font(*canvas) == "bold 16px system-ui");
    const Canvas2DTextMetrics metrics = registry.measure_text(*canvas, "Hi");
    assert(metrics.width == 16.0);
    assert(registry.set_fill_style(*canvas, "#abcdef"));
    assert(registry.fill_text(*canvas, "Hi", 3.0, 18.0));
    const Canvas2DSurface* surface = registry.surface(registry.handle_for(*canvas));
    assert(surface != nullptr);
    bool found_marker = false;
    for (const Color& color : surface->pixels) {
        if (color.r == 0xab && color.g == 0xcd && color.b == 0xef) {
            found_marker = true;
            break;
        }
    }
    assert(found_marker);
}

} // namespace

int main() {
    canvas_surface_is_lazy_and_budgeted();
    oversized_canvas_is_rejected();
    drawing_updates_pixels_and_marks_paint_dirty();
    path_stroke_draws_line();
    path_stroke_antialiases_diagonal_edges();
    global_alpha_affects_fill_rect();
    global_alpha_rejects_out_of_range_values();
    save_restore_restores_drawing_state();
    state_stack_is_bounded();
    path_point_budget_is_bounded();
    arc_stroke_draws_ring_pixels();
    fill_path_fills_closed_polygon();
    linear_gradient_fills_rect_and_is_budgeted();
    text_metrics_and_fill_text_use_bound_backend();
    std::cout << "canvas2d tests passed\n";
    return 0;
}
