#include "render_core/scroll_gesture.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void drag_threshold_preserves_tap_behavior() {
    VerticalScrollGesture gesture;
    gesture.begin(100);
    const VerticalScrollGestureUpdate update = gesture.update(97);
    check(!update.dragging, "movement below threshold does not start a drag");
    check(!gesture.end(), "movement below threshold does not consume a tap");
    check(!gesture.has_inertia(), "tap does not start inertia");
}

void drag_starts_once_and_reports_relative_motion() {
    VerticalScrollGesture gesture;
    gesture.begin(100);
    gesture.update(98);
    const VerticalScrollGestureUpdate started = gesture.update(94);
    check(started.dragging_started && started.dragging, "threshold crossing starts drag once");
    check(started.delta_y == 4, "drag reports scroll delta opposite finger movement");
    const VerticalScrollGestureUpdate continued = gesture.update(88);
    check(!continued.dragging_started && continued.dragging && continued.delta_y == 6,
          "continued drag reports incremental motion");
    check(gesture.end(), "drag consumes pointer release");
    check(gesture.has_inertia(), "fast drag arms bounded inertia");
}

void inertia_decays_and_stops_when_present_is_blocked() {
    VerticalScrollGesture gesture;
    gesture.begin(20);
    gesture.update(12);
    gesture.end();
    check(gesture.advance_inertia(true) == 8, "first inertia step keeps latest drag delta");
    check(gesture.advance_inertia(true) == 6, "inertia decays with integer ratio");
    check(gesture.advance_inertia(false) == 0, "blocked presentation cancels inertia");
    check(!gesture.has_inertia(), "blocked presentation leaves no latent inertia");
}

void cancellation_clears_drag_and_inertia() {
    VerticalScrollGesture gesture;
    gesture.begin(20);
    gesture.update(10);
    gesture.end();
    gesture.cancel();
    check(!gesture.active() && !gesture.dragging() && !gesture.has_inertia(),
          "cancellation clears every gesture phase");
}

} // namespace

int main() {
    try {
        drag_threshold_preserves_tap_behavior();
        drag_starts_once_and_reports_relative_motion();
        inertia_decays_and_stops_when_present_is_blocked();
        cancellation_clears_drag_and_inertia();
    } catch (const std::exception& error) {
        std::cerr << "scroll gesture tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "scroll gesture tests passed\n";
    return 0;
}
