#pragma once

namespace jellyframe {

struct VerticalScrollGestureOptions {
    int drag_start_threshold_px = 4;
    int inertia_min_delta_px = 2;
    int inertia_decay_numerator = 3;
    int inertia_decay_denominator = 4;
    int max_inertia_steps = 12;
};

struct VerticalScrollGestureUpdate {
    bool dragging_started = false;
    bool dragging = false;
    int delta_y = 0;
};

class VerticalScrollGesture {
public:
    explicit VerticalScrollGesture(VerticalScrollGestureOptions options = {})
        : options_(sanitize_options(options)) {}

    void begin(int y) {
        active_ = true;
        dragging_ = false;
        start_y_ = y;
        last_y_ = y;
        last_delta_y_ = 0;
        stop_inertia();
    }

    VerticalScrollGestureUpdate update(int y) {
        if (!active_) {
            return {};
        }

        const int total_delta = y - start_y_;
        if (!dragging_ && absolute(total_delta) < options_.drag_start_threshold_px) {
            last_y_ = y;
            return {};
        }

        VerticalScrollGestureUpdate result;
        result.dragging_started = !dragging_;
        dragging_ = true;
        result.dragging = true;
        result.delta_y = last_y_ - y;
        last_y_ = y;
        last_delta_y_ = result.delta_y;
        return result;
    }

    bool end() {
        const bool consumed = dragging_;
        active_ = false;
        dragging_ = false;
        if (!consumed || absolute(last_delta_y_) < options_.inertia_min_delta_px) {
            stop_inertia();
            return consumed;
        }
        inertia_delta_y_ = last_delta_y_;
        inertia_steps_left_ = options_.max_inertia_steps;
        return consumed;
    }

    void cancel_drag() {
        active_ = false;
        dragging_ = false;
        last_delta_y_ = 0;
    }

    void cancel() {
        cancel_drag();
        stop_inertia();
    }

    void stop_inertia() {
        inertia_delta_y_ = 0;
        inertia_steps_left_ = 0;
    }

    int advance_inertia(bool presentation_allowed) {
        if (!presentation_allowed || inertia_steps_left_ <= 0 ||
            absolute(inertia_delta_y_) < options_.inertia_min_delta_px) {
            stop_inertia();
            return 0;
        }

        const int delta = inertia_delta_y_;
        --inertia_steps_left_;
        inertia_delta_y_ = (inertia_delta_y_ * options_.inertia_decay_numerator) /
            options_.inertia_decay_denominator;
        if (inertia_steps_left_ <= 0 || absolute(inertia_delta_y_) < options_.inertia_min_delta_px) {
            stop_inertia();
        }
        return delta;
    }

    bool active() const {
        return active_;
    }

    bool dragging() const {
        return dragging_;
    }

    bool has_inertia() const {
        return inertia_steps_left_ > 0;
    }

private:
    static int absolute(int value) {
        return value < 0 ? -value : value;
    }

    static VerticalScrollGestureOptions sanitize_options(VerticalScrollGestureOptions options) {
        options.drag_start_threshold_px = options.drag_start_threshold_px < 0
            ? 0 : options.drag_start_threshold_px;
        options.inertia_min_delta_px = options.inertia_min_delta_px < 1
            ? 1 : options.inertia_min_delta_px;
        options.inertia_decay_numerator = options.inertia_decay_numerator < 0
            ? 0 : options.inertia_decay_numerator;
        options.inertia_decay_denominator = options.inertia_decay_denominator < 1
            ? 1 : options.inertia_decay_denominator;
        options.inertia_decay_numerator = options.inertia_decay_numerator > options.inertia_decay_denominator
            ? options.inertia_decay_denominator : options.inertia_decay_numerator;
        options.max_inertia_steps = options.max_inertia_steps < 0 ? 0 : options.max_inertia_steps;
        return options;
    }

    VerticalScrollGestureOptions options_;
    int start_y_ = 0;
    int last_y_ = 0;
    int last_delta_y_ = 0;
    int inertia_delta_y_ = 0;
    int inertia_steps_left_ = 0;
    bool active_ = false;
    bool dragging_ = false;
};

} // namespace jellyframe
