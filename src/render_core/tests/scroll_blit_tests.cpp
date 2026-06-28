#include "render_core/scroll_blit.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void downward_scroll_moves_visible_rows_up() {
    const ScrollBlitPlan plan = plan_vertical_scroll_blit(100, 80, 240, 10, 22);
    check(plan.mode == ScrollBlitMode::FastBlit, "small downward scroll uses fast blit");
    check(plan.delta_y == 12, "downward delta is positive");
    check(plan.move_source.x == 0 && plan.move_source.y == 12 &&
              plan.move_source.width == 100 && plan.move_source.height == 68,
          "downward source skips rows that scrolled out");
    check(plan.move_destination.x == 0 && plan.move_destination.y == 0 &&
              plan.move_destination.width == 100 && plan.move_destination.height == 68,
          "downward destination starts at top");
    check(plan.exposed_strip.x == 0 && plan.exposed_strip.y == 68 &&
              plan.exposed_strip.width == 100 && plan.exposed_strip.height == 12,
          "downward exposed strip is bottom");
    check(plan.moved_pixels == 6800 && plan.exposed_pixels == 1200, "downward pixel counts");
}

void upward_scroll_moves_visible_rows_down() {
    const ScrollBlitPlan plan = plan_vertical_scroll_blit(64, 40, 200, 70, 55);
    check(plan.mode == ScrollBlitMode::FastBlit, "small upward scroll uses fast blit");
    check(plan.delta_y == -15, "upward delta is negative");
    check(plan.move_source.x == 0 && plan.move_source.y == 0 &&
              plan.move_source.width == 64 && plan.move_source.height == 25,
          "upward source starts at top");
    check(plan.move_destination.x == 0 && plan.move_destination.y == 15 &&
              plan.move_destination.width == 64 && plan.move_destination.height == 25,
          "upward destination moves down");
    check(plan.exposed_strip.x == 0 && plan.exposed_strip.y == 0 &&
              plan.exposed_strip.width == 64 && plan.exposed_strip.height == 15,
          "upward exposed strip is top");
}

void scroll_inputs_are_clamped_to_content_bounds() {
    const ScrollBlitPlan plan = plan_vertical_scroll_blit(32, 30, 70, 100, 44);
    check(plan.previous_scroll_y == 40, "previous scroll clamps to max");
    check(plan.current_scroll_y == 40, "requested scroll clamps to max");
    check(plan.mode == ScrollBlitMode::None, "equal clamped scroll does not repaint");
}

void large_scroll_falls_back_to_full_repaint() {
    const ScrollBlitPlan plan = plan_vertical_scroll_blit(48, 32, 160, 0, 60);
    check(plan.mode == ScrollBlitMode::FullRepaint, "large scroll uses full repaint");
    check(plan.exposed_strip.width == 48 && plan.exposed_strip.height == 32, "full repaint covers viewport");
    check(plan.exposed_pixels == 1536, "full repaint reports viewport pixels");
}

void invalid_viewport_falls_back_to_full_repaint() {
    const ScrollBlitPlan plan = plan_vertical_scroll_blit(0, 32, 160, 0, 4);
    check(plan.mode == ScrollBlitMode::FullRepaint, "invalid viewport is conservative");
    check(std::string(scroll_blit_mode_name(plan.mode)) == "full-repaint", "mode name is stable");
}

} // namespace

int main() {
    try {
        downward_scroll_moves_visible_rows_up();
        upward_scroll_moves_visible_rows_down();
        scroll_inputs_are_clamped_to_content_bounds();
        large_scroll_falls_back_to_full_repaint();
        invalid_viewport_falls_back_to_full_repaint();
    } catch (const std::exception& error) {
        std::cerr << "scroll blit tests failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "scroll blit tests passed\n";
    return 0;
}
