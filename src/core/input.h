#pragma once

#include "core/event.h"
#include "core/hit_test.h"
#include "core/layer_tree.h"

namespace wearweb {

enum class PointerButton {
    None = -1,
    Primary = 0,
    Middle = 1,
    Secondary = 2,
};

struct InputModifiers {
    bool alt = false;
    bool ctrl = false;
    bool meta = false;
    bool shift = false;
};

struct PointerInput {
    int x = 0;
    int y = 0;
    PointerButton button = PointerButton::None;
    int buttons = 0;
    InputModifiers modifiers;
};

struct WheelInput {
    int x = 0;
    int y = 0;
    int delta_x = 0;
    int delta_y = 0;
    InputModifiers modifiers;
};

class InputController {
public:
    explicit InputController(const LayerNode& layer_tree);

    const Node* hovered_node() const;
    const Node* active_node() const;
    const Node* focused_node() const;

    const Node* pointer_move(const PointerInput& input);
    const Node* pointer_down(const PointerInput& input);
    const Node* pointer_up(const PointerInput& input);
    const Node* wheel(const WheelInput& input);
    void clear_pointer_state();

private:
    const LayerNode& layer_tree_;
    HitTester hit_tester_;
    const Node* hovered_node_ = nullptr;
    const Node* active_node_ = nullptr;
    const Node* focused_node_ = nullptr;

    const Node* hit_node(int x, int y) const;
    MouseEvent make_mouse_event(const char* type, const PointerInput& input) const;
    WheelEvent make_wheel_event(const WheelInput& input) const;
    void dispatch_mouse_event(const Node* target, MouseEvent& event) const;
    void update_hover(const Node* next_hover, const PointerInput& input);
};

} // namespace wearweb
