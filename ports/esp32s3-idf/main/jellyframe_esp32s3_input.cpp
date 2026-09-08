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

bool BoardInputQueue::discard_oldest_move_locked() {
    for (std::size_t offset = 0; offset < count_; ++offset) {
        const std::size_t index = (head_ + offset) % kCapacity;
        if (events_[index].kind != BoardInputKind::PointerMove) {
            continue;
        }

        // Move samples are replaceable state. Remove one in-place while the
        // queue lock is held, preserving every discrete event's order.
        for (std::size_t shift = offset; shift + 1 < count_; ++shift) {
            const std::size_t current = (head_ + shift) % kCapacity;
            const std::size_t next = (head_ + shift + 1) % kCapacity;
            events_[current] = events_[next];
        }
        tail_ = (tail_ + kCapacity - 1) % kCapacity;
        --count_;
        ++dropped_count_;
        return true;
    }
    return false;
}

bool BoardInputQueue::enqueue(const BoardInputEvent& event) {
    portENTER_CRITICAL(&lock_);
    // Pointer moves are state samples, not discrete actions. Retaining the
    // newest adjacent sample prevents a slow render/present cycle from making
    // a drag replay stale coordinates after the finger has stopped moving.
    if (event.kind == BoardInputKind::PointerMove && count_ != 0) {
        const std::size_t previous = (tail_ + kCapacity - 1) % kCapacity;
        if (events_[previous].kind == BoardInputKind::PointerMove) {
            events_[previous] = event;
            ++coalesced_move_count_;
            portEXIT_CRITICAL(&lock_);
            return true;
        }
    }
    if (count_ == kCapacity) {
        // Pointer moves are lossy samples. Make room for the newest sample
        // or for a release event before rejecting a genuinely discrete event.
        if (!discard_oldest_move_locked()) {
            ++dropped_count_;
            portEXIT_CRITICAL(&lock_);
            return false;
        }
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

std::uint32_t BoardInputQueue::coalesced_move_count() const {
    portENTER_CRITICAL(&lock_);
    const std::uint32_t coalesced = coalesced_move_count_;
    portEXIT_CRITICAL(&lock_);
    return coalesced;
}

BoardInputDispatchStats dispatch_input_events(BoardInputQueue& queue,
                                              jellyframe::InputController& controller,
                                              std::size_t max_events,
                                              BoardInputEventObserver observer,
                                              void* observer_context) {
    BoardInputDispatchStats stats;
    BoardInputEvent event;
    while (stats.dispatched < max_events && queue.dequeue(event)) {
        ++stats.dispatched;
        if (observer != nullptr && observer(event, observer_context)) {
            if (event.kind == BoardInputKind::PointerDown ||
                event.kind == BoardInputKind::PointerMove ||
                event.kind == BoardInputKind::PointerUp) {
                ++stats.pointer_events;
            }
            continue;
        }
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
