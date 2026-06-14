#include "core/input.h"

#include "core/dom.h"
#include "core/form_control.h"

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

Node* mutable_node(const Node* node) {
    return const_cast<Node*>(node);
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

void InputController::set_focused_node(const Node* node) {
    focused_node_ = node;
}

const Node* InputController::pointer_move(const PointerInput& input) {
    HitTestResult result = hit(input.x, input.y);
    const Node* target = result ? result.node : nullptr;
    update_hover(target, input);
    if (active_box_ != nullptr && active_box_->node != nullptr && input.buttons != 0 &&
        form_control_kind(*active_box_->node) == FormControlKind::Range) {
        if (set_range_value_from_local_x(*mutable_node(active_box_->node),
                                         input.x - active_box_->rect.x,
                                         active_box_->rect.width)) {
            dispatch_simple_event(active_box_->node, "input");
        }
    }
    MouseEvent event = make_mouse_event("mousemove", input);
    dispatch_mouse_event(target, event);
    return target;
}

const Node* InputController::pointer_down(const PointerInput& input) {
    HitTestResult result = hit(input.x, input.y);
    const Node* target = result ? result.node : nullptr;
    update_hover(target, input);
    active_node_ = target;
    active_box_ = result ? result.box : nullptr;
    focused_node_ = target;
    if (active_box_ != nullptr && active_box_->node != nullptr &&
        form_control_kind(*active_box_->node) == FormControlKind::Range) {
        if (set_range_value_from_local_x(*mutable_node(active_box_->node),
                                         input.x - active_box_->rect.x,
                                         active_box_->rect.width)) {
            dispatch_simple_event(active_box_->node, "input");
        }
    }
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
        if (active_node_ != nullptr && is_form_control(*active_node_) &&
            form_control_kind(*active_node_) != FormControlKind::Range) {
            if (activate_form_control(*mutable_node(active_node_))) {
                dispatch_simple_event(active_node_, "input");
                dispatch_simple_event(active_node_, "change");
            }
        }
        if (active_node_ != nullptr && form_control_kind(*active_node_) == FormControlKind::Range) {
            dispatch_simple_event(active_node_, "change");
        }
        MouseEvent click = make_mouse_event("click", input);
        dispatch_mouse_event(target, click);
    }
    active_node_ = nullptr;
    active_box_ = nullptr;
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

bool InputController::text_input(const std::string& utf8_text) {
    if (focused_node_ == nullptr) {
        return false;
    }
    if (!append_text_to_control(*mutable_node(focused_node_), utf8_text)) {
        return false;
    }
    dispatch_simple_event(focused_node_, "input");
    return true;
}

bool InputController::key_down(const KeyInput& input) {
    if (focused_node_ == nullptr) {
        return false;
    }
    if (input.code == KeyCode::Backspace && backspace_control(*mutable_node(focused_node_))) {
        dispatch_simple_event(focused_node_, "input");
        return true;
    }
    if ((input.code == KeyCode::Space || input.code == KeyCode::Enter) &&
        is_form_control(*focused_node_) &&
        activate_form_control(*mutable_node(focused_node_))) {
        dispatch_simple_event(focused_node_, "input");
        dispatch_simple_event(focused_node_, "change");
        return true;
    }
    return false;
}

void InputController::clear_pointer_state() {
    hovered_node_ = nullptr;
    active_node_ = nullptr;
    active_box_ = nullptr;
}

HitTestResult InputController::hit(int x, int y) const {
    return hit_tester_.hit_test(layer_tree_, x, y);
}

const Node* InputController::hit_node(int x, int y) const {
    HitTestResult result = hit(x, y);
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

void InputController::dispatch_simple_event(const Node* target, const char* type) const {
    if (target == nullptr) {
        return;
    }
    Event event(type, true, false);
    dispatch_event(*target, event);
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
