#include "render_core/input.h"

#include "render_core/dom.h"
#include "render_core/form_control.h"

#include <algorithm>
#include <vector>

namespace jellyframe {
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

bool disabled_target(const Node* node) {
    return node != nullptr && is_disabled_form_control(*node);
}

bool focusable_node(const Node* node) {
    if (node == nullptr || node->type != NodeType::Element || disabled_target(node)) {
        return false;
    }
    return node->tag_name == "button" || node->tag_name == "input" ||
        node->tag_name == "select" || node->tag_name == "textarea" ||
        (node->tag_name == "a" && !node->attribute("href").empty());
}

void mark_interaction_style_dirty(const Node* node) {
    if (node != nullptr) {
        mark_dirty(*mutable_node(node), DomDirtyStyle | DomDirtyLayout);
    }
}

void collect_focusable_nodes(const LayoutBox& box, std::vector<const Node*>& nodes) {
    if (focusable_node(box.node) &&
        std::find(nodes.begin(), nodes.end(), box.node) == nodes.end()) {
        nodes.push_back(box.node);
    }
    for (const auto& child : box.children) {
        collect_focusable_nodes(*child, nodes);
    }
}

const LayoutBox* find_layout_box_for_node(const LayoutBox* box, const Node* node) {
    if (box == nullptr || node == nullptr) {
        return nullptr;
    }
    if (box->node == node) {
        return box;
    }
    for (const auto& child : box->children) {
        const LayoutBox* found = find_layout_box_for_node(child.get(), node);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
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
    if (focused_node_ != node) {
        mark_interaction_style_dirty(focused_node_);
        mark_interaction_style_dirty(node);
    }
    focused_node_ = node;
}

void InputController::set_interaction_state(const Node* hovered_node,
                                            const Node* active_node,
                                            const Node* focused_node) {
    hovered_node_ = hovered_node;
    active_node_ = active_node;
    focused_node_ = focused_node;
    active_box_ = find_layout_box_for_node(layer_tree_.box, active_node);
}

const Node* InputController::pointer_move(const PointerInput& input) {
    HitTestResult result = hit(input.x, input.y);
    const Node* target = result ? result.node : nullptr;
    if (disabled_target(target)) {
        return target;
    }
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
    if (disabled_target(target)) {
        set_active_node(nullptr);
        return target;
    }
    update_hover(target, input);
    set_active_node(target, result ? result.box : nullptr);
    set_focused_node(target);
    if (active_box_ != nullptr && active_box_->node != nullptr &&
        form_control_kind(*active_box_->node) == FormControlKind::Range) {
        if (set_range_value_from_local_x(*mutable_node(active_box_->node),
                                         input.x - active_box_->rect.x,
                                         active_box_->rect.width)) {
            dispatch_simple_event(active_box_->node, "input");
        }
    }
    MouseEvent pointer = make_mouse_event("pointerdown", input);
    dispatch_mouse_event(target, pointer);
    MouseEvent touch = make_mouse_event("touchstart", input);
    dispatch_mouse_event(target, touch);
    MouseEvent event = make_mouse_event("mousedown", input);
    dispatch_mouse_event(target, event);
    return target;
}

const Node* InputController::pointer_up(const PointerInput& input) {
    const Node* target = hit_node(input.x, input.y);
    if (disabled_target(target) || disabled_target(active_node_)) {
        set_active_node(nullptr);
        return target;
    }
    update_hover(target, input);
    MouseEvent pointer = make_mouse_event("pointerup", input);
    dispatch_mouse_event(target, pointer);
    MouseEvent touch = make_mouse_event("touchend", input);
    dispatch_mouse_event(target, touch);
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
    set_active_node(nullptr);
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
    if ((input.code == KeyCode::Enter || input.code == KeyCode::Tab) &&
        complete_text_control_from_datalist(*mutable_node(focused_node_))) {
        dispatch_simple_event(focused_node_, "input");
        dispatch_simple_event(focused_node_, "change");
        return true;
    }
    if (input.code == KeyCode::ArrowDown && step_select_control(*mutable_node(focused_node_), 1)) {
        dispatch_simple_event(focused_node_, "input");
        dispatch_simple_event(focused_node_, "change");
        return true;
    }
    if (input.code == KeyCode::ArrowUp && step_select_control(*mutable_node(focused_node_), -1)) {
        dispatch_simple_event(focused_node_, "input");
        dispatch_simple_event(focused_node_, "change");
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

const Node* InputController::focus_next() {
    if (layer_tree_.box == nullptr) {
        return nullptr;
    }
    std::vector<const Node*> nodes;
    collect_focusable_nodes(*layer_tree_.box, nodes);
    if (nodes.empty()) {
        set_focused_node(nullptr);
        return nullptr;
    }
    auto current = std::find(nodes.begin(), nodes.end(), focused_node_);
    if (current == nodes.end() || ++current == nodes.end()) {
        set_focused_node(nodes.front());
    } else {
        set_focused_node(*current);
    }
    return focused_node_;
}

const Node* InputController::focus_previous() {
    if (layer_tree_.box == nullptr) {
        return nullptr;
    }
    std::vector<const Node*> nodes;
    collect_focusable_nodes(*layer_tree_.box, nodes);
    if (nodes.empty()) {
        set_focused_node(nullptr);
        return nullptr;
    }
    auto current = std::find(nodes.begin(), nodes.end(), focused_node_);
    if (current == nodes.end() || current == nodes.begin()) {
        set_focused_node(nodes.back());
    } else {
        set_focused_node(*(--current));
    }
    return focused_node_;
}

bool InputController::activate_focused() {
    if (focused_node_ == nullptr || disabled_target(focused_node_) || !focusable_node(focused_node_)) {
        return false;
    }
    if (is_form_control(*focused_node_) && activate_form_control(*mutable_node(focused_node_))) {
        dispatch_simple_event(focused_node_, "input");
        dispatch_simple_event(focused_node_, "change");
    }
    PointerInput synthetic;
    MouseEvent click = make_mouse_event("click", synthetic);
    dispatch_mouse_event(focused_node_, click);
    return true;
}

void InputController::clear_pointer_state() {
    set_hovered_node(nullptr);
    set_active_node(nullptr);
}

HitTestResult InputController::hit(int x, int y) const {
    return hit_tester_.hit_test(layer_tree_, x, y);
}

const Node* InputController::hit_node(int x, int y) const {
    HitTestResult result = hit(x, y);
    return result ? result.node : nullptr;
}

void InputController::set_hovered_node(const Node* node) {
    if (hovered_node_ != node) {
        mark_interaction_style_dirty(hovered_node_);
        mark_interaction_style_dirty(node);
    }
    hovered_node_ = node;
}

void InputController::set_active_node(const Node* node, const LayoutBox* box) {
    if (active_node_ != node) {
        mark_interaction_style_dirty(active_node_);
        mark_interaction_style_dirty(node);
    }
    active_node_ = node;
    active_box_ = box;
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
    set_hovered_node(next_hover);
    if (hovered_node_ != nullptr) {
        MouseEvent over = make_mouse_event("mouseover", input);
        dispatch_event(*hovered_node_, over);
    }
}

} // namespace jellyframe
