#include "jellyframe_esp32s3_input.h"

#include <string>

namespace jellyframe_esp32s3 {
namespace {

std::size_t bounded_text_length(const char* text, std::size_t capacity) {
    std::size_t length = 0;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

} // namespace

bool BoardInputQueue::enqueue(const BoardInputEvent& event) {
    portENTER_CRITICAL(&lock_);
    if (count_ == kCapacity) {
        ++dropped_count_;
        portEXIT_CRITICAL(&lock_);
        return false;
    }
    events_[tail_] = event;
    tail_ = (tail_ + 1) % kCapacity;
    ++count_;
    portEXIT_CRITICAL(&lock_);
    return true;
}

bool BoardInputQueue::dequeue(BoardInputEvent& event) {
    portENTER_CRITICAL(&lock_);
    if (count_ == 0) {
        portEXIT_CRITICAL(&lock_);
        return false;
    }
    event = events_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    portEXIT_CRITICAL(&lock_);
    return true;
}

void BoardInputQueue::clear() {
    portENTER_CRITICAL(&lock_);
    head_ = 0;
    tail_ = 0;
    count_ = 0;
    portEXIT_CRITICAL(&lock_);
}

std::size_t BoardInputQueue::size() const {
    portENTER_CRITICAL(&lock_);
    const std::size_t count = count_;
    portEXIT_CRITICAL(&lock_);
    return count;
}

std::size_t BoardInputQueue::capacity() const {
    return kCapacity;
}

std::uint32_t BoardInputQueue::dropped_count() const {
    portENTER_CRITICAL(&lock_);
    const std::uint32_t dropped = dropped_count_;
    portEXIT_CRITICAL(&lock_);
    return dropped;
}

BoardInputDispatchStats dispatch_input_events(BoardInputQueue& queue,
                                              jellyframe::InputController& controller,
                                              std::size_t max_events) {
    BoardInputDispatchStats stats;
    BoardInputEvent event;
    while (stats.dispatched < max_events && queue.dequeue(event)) {
        ++stats.dispatched;
        switch (event.kind) {
        case BoardInputKind::PointerDown:
            controller.pointer_down(jellyframe::PointerInput{
                event.x,
                event.y,
                jellyframe::PointerButton::Primary,
                1,
                {},
            });
            ++stats.pointer_events;
            break;
        case BoardInputKind::PointerMove:
            controller.pointer_move(jellyframe::PointerInput{
                event.x,
                event.y,
                jellyframe::PointerButton::Primary,
                1,
                {},
            });
            ++stats.pointer_events;
            break;
        case BoardInputKind::PointerUp:
            controller.pointer_up(jellyframe::PointerInput{
                event.x,
                event.y,
                jellyframe::PointerButton::Primary,
                0,
                {},
            });
            ++stats.pointer_events;
            break;
        case BoardInputKind::Wheel:
            controller.wheel(jellyframe::WheelInput{event.x, event.y, event.delta_x, event.delta_y, {}});
            ++stats.wheel_events;
            break;
        case BoardInputKind::FocusNext:
            controller.focus_next();
            ++stats.focus_events;
            break;
        case BoardInputKind::FocusPrevious:
            controller.focus_previous();
            ++stats.focus_events;
            break;
        case BoardInputKind::Activate:
            controller.activate_focused();
            ++stats.activation_events;
            break;
        case BoardInputKind::Backspace:
            controller.key_down(jellyframe::KeyInput{jellyframe::KeyCode::Backspace, {}});
            ++stats.text_events;
            break;
        case BoardInputKind::Text: {
            const std::size_t length = bounded_text_length(event.text, sizeof(event.text));
            if (length > 0) {
                controller.text_input(std::string(event.text, length));
            }
            ++stats.text_events;
            break;
        }
        }
    }
    return stats;
}

} // namespace jellyframe_esp32s3
