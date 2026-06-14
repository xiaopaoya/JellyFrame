#include "core/input.h"

#include "core/dom.h"

namespace wearweb {
namespace {

int button_to_int(PointerButton button) {
    return static_cast<int>(button);
}

void apply_modifiers(MouseEvent& event, const InputModifiers& modifiers) {
    event.alt_key = modifiers.alt;
    event.ctrl_key = modifiers.ctrl;
    event.meta_key = modifiers.meta;
    event.shift_key = modifiers.shift;
}

} // namespace

InputController::InputController(const LayerNode& layer_tree)
    : layer_tree_(layer_tree) {}

const Node* InputController::hovered_node() const {
    return hovered_node_;
}

const Node* InputController::active_node() const {
    return active_node_;
}

const Node* InputController::focused_node() const {
    return focused_node_;
}

const Node* InputController::pointer_move(const PointerInput& input) {
    const Node* target = hit_node(input.x, input.y);
    update_hover(target, input);
    MouseEvent event = make_mouse_event("mousemove", input);
    dispatch_mouse_event(target, event);
    return target;
}

const Node* InputController::pointer_down(const PointerInput& input) {
    const Node* target = hit_node(input.x, input.y);
    update_hover(target, input);
    active_node_ = target;
    focused_node_ = target;
    MouseEvent event = make_mouse_event("mousedown", input);
    dispatch_mouse_event(target, event);
    return target;
}

const Node* InputController::pointer_up(const PointerInput& input) {
    const Node* target = hit_node(input.x, input.y);
    update_hover(target, input);
    MouseEvent event = make_mouse_event("mouseup", input);
    dispatch_mouse_event(target, event);
    if (target != nullptr && target == active_node_) {
        MouseEvent click = make_mouse_event("click", input);
        dispatch_mouse_event(target, click);
    }
    active_node_ = nullptr;
    return target;
}

const Node* InputController::wheel(const WheelInput& input) {
    const Node* target = hit_node(input.x, input.y);
    WheelEvent event = make_wheel_event(input);
    if (target != nullptr) {
        dispatch_event(*target, event);
    }
    return target;
}

void InputController::clear_pointer_state() {
    hovered_node_ = nullptr;
    active_node_ = nullptr;
}

const Node* InputController::hit_node(int x, int y) const {
    HitTestResult result = hit_tester_.hit_test(layer_tree_, x, y);
    return result ? result.node : nullptr;
}

MouseEvent InputController::make_mouse_event(const char* type, const PointerInput& input) const {
    MouseEvent event(type, input.x, input.y, button_to_int(input.button), input.buttons);
    apply_modifiers(event, input.modifiers);
    return event;
}

WheelEvent InputController::make_wheel_event(const WheelInput& input) const {
    WheelEvent event(input.x, input.y, input.delta_x, input.delta_y);
    apply_modifiers(event, input.modifiers);
    return event;
}

void InputController::dispatch_mouse_event(const Node* target, MouseEvent& event) const {
    if (target != nullptr) {
        dispatch_event(*target, event);
    }
}

void InputController::update_hover(const Node* next_hover, const PointerInput& input) {
    if (next_hover == hovered_node_) {
        return;
    }
    if (hovered_node_ != nullptr) {
        MouseEvent out = make_mouse_event("mouseout", input);
        dispatch_event(*hovered_node_, out);
    }
    hovered_node_ = next_hover;
    if (hovered_node_ != nullptr) {
        MouseEvent over = make_mouse_event("mouseover", input);
        dispatch_event(*hovered_node_, over);
    }
}

} // namespace wearweb
