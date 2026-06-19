#pragma once

#include "render_core/input.h"

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32s3 {

enum class BoardInputKind {
    PointerDown,
    PointerMove,
    PointerUp,
    Wheel,
    FocusNext,
    FocusPrevious,
    Activate,
    Backspace,
    Text,
};

struct BoardInputEvent {
    BoardInputKind kind = BoardInputKind::PointerMove;
    int x = 0;
    int y = 0;
    int delta_x = 0;
    int delta_y = 0;
    char text[16] = {};
};

class BoardInputQueue {
public:
    bool enqueue(const BoardInputEvent& event);
    bool dequeue(BoardInputEvent& event);
    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    std::uint32_t dropped_count() const;

private:
    static constexpr std::size_t kCapacity = 16;

    BoardInputEvent events_[kCapacity]{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
    std::size_t count_ = 0;
    std::uint32_t dropped_count_ = 0;
};

struct BoardInputDispatchStats {
    std::uint32_t dispatched = 0;
    std::uint32_t pointer_events = 0;
    std::uint32_t wheel_events = 0;
    std::uint32_t focus_events = 0;
    std::uint32_t text_events = 0;
    std::uint32_t activation_events = 0;
};

BoardInputDispatchStats dispatch_input_events(BoardInputQueue& queue,
                                              jellyframe::InputController& controller,
                                              std::size_t max_events);

} // namespace jellyframe_esp32s3
