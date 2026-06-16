#pragma once

#include "core/event.h"
#include "core/hit_test.h"
#include "core/layer_tree.h"

#include <string>

namespace jellyframe {

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

enum class KeyCode {
    Unknown,
    Backspace,
    Enter,
    Space,
    Tab,
    ArrowUp,
    ArrowDown,
};

struct KeyInput {
    KeyCode code = KeyCode::Unknown;
    InputModifiers modifiers;
};

class InputController {
public:
    explicit InputController(const LayerNode& layer_tree);

    const Node* hovered_node() const;
    const Node* active_node() const;
    const Node* focused_node() const;
    void set_focused_node(const Node* node);
    void set_interaction_state(const Node* hovered_node, const Node* active_node, const Node* focused_node);

    const Node* pointer_move(const PointerInput& input);
    const Node* pointer_down(const PointerInput& input);
    const Node* pointer_up(const PointerInput& input);
    const Node* wheel(const WheelInput& input);
    bool text_input(const std::string& utf8_text);
    bool key_down(const KeyInput& input);
    const Node* focus_next();
    const Node* focus_previous();
    bool activate_focused();
    void clear_pointer_state();

private:
    const LayerNode& layer_tree_;
    HitTester hit_tester_;
    const Node* hovered_node_ = nullptr;
    const Node* active_node_ = nullptr;
    const Node* focused_node_ = nullptr;
    const LayoutBox* active_box_ = nullptr;

    HitTestResult hit(int x, int y) const;
    const Node* hit_node(int x, int y) const;
    void set_hovered_node(const Node* node);
    void set_active_node(const Node* node, const LayoutBox* box = nullptr);
    MouseEvent make_mouse_event(const char* type, const PointerInput& input) const;
    WheelEvent make_wheel_event(const WheelInput& input) const;
    void dispatch_mouse_event(const Node* target, MouseEvent& event) const;
    void dispatch_simple_event(const Node* target, const char* type) const;
    void update_hover(const Node* next_hover, const PointerInput& input);
};

} // namespace jellyframe
