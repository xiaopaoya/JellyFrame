#include "render_core/modern_paint.h"
#include "render_core/raster_primitives.h"

#include <cassert>
#include <iostream>
#include <limits>

using namespace jellyframe;

int modern_paint_tests_main() {
    const Color midpoint = modern_paint_lerp_color_255(Color{0, 10, 20, 255},
                                                        Color{100, 110, 120, 255},
                                                        128);
    assert(midpoint.r == 50 && midpoint.g == 60 && midpoint.b == 70);
    assert(modern_paint_conic_percent_from_top_clockwise(0, -10) == 0);
    assert(modern_paint_conic_percent_from_top_clockwise(10, 0) == 25);
    assert(modern_paint_conic_percent_from_top_clockwise(0, 10) == 50);
    assert(modern_paint_conic_percent_from_top_clockwise(-10, 0) == 75);
    assert(modern_paint_euclidean_distance_half_px(0, 10) == 10);
    assert(modern_paint_rounded_rect_outside_distance_half_px(Rect{0, 0, 20, 20}, 10, 10, 10) == 0);
    const Rect rounded_rect{4, 6, 20, 14};
    const int rounded_radius = encode_corner_radii(CornerRadii{5, 3, 7, 2});
    const RasterRoundedRect prepared = prepare_rounded_rect(rounded_rect, rounded_radius);
    for (int y = 6; y < 20; ++y) {
        for (int x = 4; x < 24; ++x) {
            assert(rounded_rect_coverage(prepared, x, y) ==
                   rounded_rect_coverage(rounded_rect, rounded_radius, x, y));
        }
    }
    assert(safe_add(std::numeric_limits<int>::max(), 1) == std::numeric_limits<int>::max());
    assert(safe_add(std::numeric_limits<int>::min(), -1) == std::numeric_limits<int>::min());
    assert(safe_negate(std::numeric_limits<int>::min()) == std::numeric_limits<int>::max());
    assert(safe_span(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) ==
           std::numeric_limits<int>::max());
    assert(expand_corner_radii(127, std::numeric_limits<int>::max()) == 127);
    assert(expand_corner_radii(1, std::numeric_limits<int>::min()) == 0);
    (void)modern_paint_rounded_rect_outside_distance_half_px(
        Rect{std::numeric_limits<int>::min(), std::numeric_limits<int>::min(),
             std::numeric_limits<int>::max(), std::numeric_limits<int>::max()},
        127,
        std::numeric_limits<int>::max() - 1,
        std::numeric_limits<int>::max() - 1);
    std::cout << "modern paint math tests passed\n";
    return 0;
}
