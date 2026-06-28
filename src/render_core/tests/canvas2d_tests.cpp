#include "render_core/canvas2d.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

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

} // namespace

int main() {
    canvas_surface_is_lazy_and_budgeted();
    oversized_canvas_is_rejected();
    drawing_updates_pixels_and_marks_paint_dirty();
    path_stroke_draws_line();
    std::cout << "canvas2d tests passed\n";
    return 0;
}
