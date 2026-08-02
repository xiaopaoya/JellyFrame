#include "render_core/canvas2d.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

int canvas2d_tests_main() {
    Node canvas(NodeType::Element);
    Canvas2DRegistry registry;

    assert(!registry.policy().enabled);
    assert(registry.ensure_surface(canvas) == 0);
    assert(registry.handle_for(canvas) == 0);
    assert(registry.surface(0) == nullptr);
    assert(!registry.fill_rect(canvas, 0, 0, 10, 10));
    assert(!registry.begin_path(canvas));
    assert(!registry.fill_text(canvas, "disabled", 0, 0));
    assert(registry.create_linear_gradient(0, 0, 1, 1) == 0);
    assert(!registry.add_color_stop(0, 0.5, "#fff"));
    assert(!is_canvas2d_handle(0));

    std::cout << "canvas2d disabled tests passed\n";
    return 0;
}
