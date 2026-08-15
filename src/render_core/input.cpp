#include "render_core/input.h"

#include "render_core/dom.h"
#include "render_core/event.h"
#include "render_core/feature_config.h"
#include "render_core/form_control.h"
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
#include "render_core/form_submission.h"
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
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

bool naturally_focusable_node(const Node* node) {
    return node != nullptr && (node->tag_name == "button" || node->tag_name == "input" ||
        node->tag_name == "select" || node->tag_name == "textarea" ||
        (node->tag_name == "summary" && node->parent != nullptr && node->parent->tag_name == "details") ||
        (node->tag_name == "a" && !node->attribute("href").empty()));
}

bool parse_tab_index(const std::string& value, int& output) {
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (end == value.c_str() || end == nullptr || *end != '\0' || parsed < -32768 || parsed > 32767) {
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

bool focusable_node(const Node* node) {
    if (node == nullptr || node->type != NodeType::Element || disabled_target(node)) {
        return false;
    }
    const auto tab_index = node->attributes.find("tabindex");
    if (tab_index != node->attributes.end()) {
        int value = 0;
        if (parse_tab_index(tab_index->second, value)) {
            return value >= 0;
        }
    }
    return naturally_focusable_node(node);
}

bool node_is_descendant_or_self(const Node* node, const Node* ancestor) {
    for (const Node* current = node; current != nullptr; current = current->parent) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
const Node* find_open_select(const Node& root) {
    std::vector<const Node*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const Node* current = pending.back();
        pending.pop_back();
        if (select_popup_is_open(*current)) {
            return current;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return nullptr;
}

bool contains_rect(Rect rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
        x < safe_edge(rect.x, rect.width) && y < safe_edge(rect.y, rect.height);
}
#endif

bool toggle_details_from_summary(const Node* node) {
    if (node == nullptr || node->type != NodeType::Element || node->tag_name != "summary" ||
        node->parent == nullptr || node->parent->tag_name != "details") {
        return false;
    }
    Node* details = mutable_node(node->parent);
    if (details->attributes.find("open") != details->attributes.end()) {
        details->remove_attribute("open");
    } else {
        details->set_attribute("open", "");
    }
    Event event("toggle", false, false);
    dispatch_event(*details, event);
    return true;
}

void mark_interaction_style_dirty(const Node* node, bool enabled) {
    if (enabled && node != nullptr) {
        mark_dirty(*mutable_node(node), DomDirtyStyle | DomDirtyLayout);
    }
}

void collect_focusable_nodes(const LayoutBox& box, std::vector<const Node*>& nodes) {
    if (!box.style.visibility_hidden && focusable_node(box.node) &&
        std::find(nodes.begin(), nodes.end(), box.node) == nodes.end()) {
        nodes.push_back(box.node);
    }
    for (const auto& child : box.children) {
        collect_focusable_nodes(*child, nodes);
    }
}

const Node* find_autofocus_node(const LayoutBox& root) {
    std::vector<const LayoutBox*> pending;
    pending.push_back(&root);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        if (!current->style.visibility_hidden && focusable_node(current->node) &&
            current->node->attributes.find("autofocus") != current->node->attributes.end()) {
            return current->node;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend(); ++it) {
            pending.push_back(it->get());
        }
    }
    return nullptr;
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

InputController::InputController(const LayerNode& layer_tree,
                                 InteractionInvalidationOptions invalidation_options)
    : layer_tree_(layer_tree),
      invalidation_options_(invalidation_options) {
    if (layer_tree_.box != nullptr) {
        set_focused_node(find_autofocus_node(*layer_tree_.box));
    }
}

InputController::~InputController() {
    for (Node* node : observed_nodes_) {
        if (node != nullptr) node->remove_destroy_observer(observed_node_destroyed, this);
    }
}

const Node* InputController::hovered_node() const {
    return hovered_node_;
}

const Node* InputController::active_node() const {
    return active_node_;
}

const Node* InputController::focused_node() const {
    return focused_node_;
}

const Node* InputController::modal_root() const {
    return modal_root_;
}

void InputController::set_focused_node(const Node* node) {
    if (!accepts_node(node) || (node != nullptr && !visible_node(node))) {
        node = nullptr;
    }
    if (focused_node_ == node) {
        return;
    }
    const Node* previous = focused_node_;
    mark_interaction_style_dirty(previous, invalidation_options_.focus_style);
    mark_interaction_style_dirty(node, invalidation_options_.focus_style);
    focused_node_ = node;
    observe_node(node);
    if (previous != nullptr) {
        Event blur("blur", false, false);
        dispatch_event(*mutable_node(previous), blur);
    }
    if (focused_node_ != nullptr) {
        Event focus("focus", false, false);
        dispatch_event(*mutable_node(focused_node_), focus);
    }
}

void InputController::set_interaction_state(const Node* hovered_node,
                                            const Node* active_node,
                                            const Node* focused_node) {
    hovered_node_ = accepts_node(hovered_node) && visible_node(hovered_node) ? hovered_node : nullptr;
    active_node_ = accepts_node(active_node) && visible_node(active_node) ? active_node : nullptr;
    focused_node_ = accepts_node(focused_node) && visible_node(focused_node) ? focused_node : nullptr;
    active_box_ = find_layout_box_for_node(layer_tree_.box, active_node_);
    observe_node(hovered_node_);
    observe_node(active_node_);
    observe_node(focused_node_);
    unobserve_unused_nodes();
}

void InputController::set_modal_root(const Node* node) {
    if (node == modal_root_) {
        return;
    }
    modal_root_ = visible_node(node) ? node : nullptr;
    observe_node(modal_root_);
    clear_pointer_state();
    if (modal_root_ == nullptr) {
        return;
    }
    if (!accepts_node(focused_node_)) {
        set_focused_node(find_autofocus_node(*focus_root_box()));
        if (focused_node_ == nullptr) {
            focus_next();
        }
    }
}

const Node* InputController::pointer_move(const PointerInput& input) {
    HitTestResult result = hit(input.x, input.y);
    const Node* target = result ? result.node : nullptr;
    if (disabled_target(target)) {
        return target;
    }
    update_hover(target, input);
    if (target != nullptr && hovered_node_ != target) {
        return nullptr;
    }
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
    return event.target_destroyed() ? nullptr : target;
}

const Node* InputController::pointer_down(const PointerInput& input) {
    HitTestResult result = hit(input.x, input.y);
    const Node* target = result ? result.node : nullptr;
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    if (layer_tree_.box != nullptr && layer_tree_.box->node != nullptr) {
        const Node* open_select = find_open_select(*layer_tree_.box->node);
        if (open_select != nullptr && target != open_select) {
            set_select_popup_open(*mutable_node(open_select), false);
        }
    }
#endif
    if (disabled_target(target)) {
        set_active_node(nullptr);
        return target;
    }
    update_hover(target, input);
    if (target != nullptr && hovered_node_ != target) {
        set_active_node(nullptr);
        return nullptr;
    }
    set_active_node(target, result ? result.box : nullptr);
    set_focused_node(focusable_node(target) ? target : nullptr);
    if (target != nullptr && active_node_ != target) {
        return nullptr;
    }
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
    if (pointer.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    MouseEvent touch = make_mouse_event("touchstart", input);
    dispatch_mouse_event(target, touch);
    if (touch.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    MouseEvent event = make_mouse_event("mousedown", input);
    dispatch_mouse_event(target, event);
    if (event.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    return target;
}

const Node* InputController::pointer_up(const PointerInput& input) {
    const Node* target = hit_node(input.x, input.y);
    const Node* activation_target = active_node_;
    if (disabled_target(target) || disabled_target(activation_target)) {
        set_active_node(nullptr);
        return target;
    }
    update_hover(target, input);
    if (target != nullptr && hovered_node_ != target) {
        set_active_node(nullptr);
        return nullptr;
    }
    MouseEvent pointer = make_mouse_event("pointerup", input);
    dispatch_mouse_event(target, pointer);
    if (pointer.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    MouseEvent touch = make_mouse_event("touchend", input);
    dispatch_mouse_event(target, touch);
    if (touch.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    MouseEvent event = make_mouse_event("mouseup", input);
    dispatch_mouse_event(target, event);
    if (event.target_destroyed()) {
        set_active_node(nullptr);
        return nullptr;
    }
    if (target != nullptr && target == activation_target && active_node_ == activation_target) {
        if (is_form_control(*activation_target) &&
            form_control_kind(*activation_target) != FormControlKind::Range) {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
            if (form_control_kind(*activation_target) == FormControlKind::Select && select_popup_is_open(*activation_target)) {
                const LayoutBox* select_box = active_box_;
                const int option_count = form_control_option_count(*activation_target);
                const int row_height = select_box != nullptr
                    ? std::max(20, select_box->style.line_height > 0
                        ? select_box->style.line_height
                        : select_box->style.font_size + std::max(6, select_box->style.font_size / 3))
                    : 20;
                const Rect viewport = layer_tree_.bounds;
                const SelectPopupGeometry geometry = select_box != nullptr
                    ? select_popup_geometry(select_box->rect, viewport, option_count, row_height)
                    : SelectPopupGeometry{};
                if (contains_rect(geometry.rect, input.x, input.y)) {
                    const int option_index = geometry.first_option_index +
                        (input.y - geometry.rect.y) / std::max(1, geometry.row_height);
                    if (!form_control_option_disabled(*activation_target, option_index) &&
                        set_form_control_selected_index(*mutable_node(activation_target), option_index)) {
                        set_select_popup_open(*mutable_node(activation_target), false);
                        dispatch_simple_event(activation_target, "input");
                        if (active_node_ != activation_target) {
                            set_active_node(nullptr);
                            return nullptr;
                        }
                        dispatch_simple_event(activation_target, "change");
                        if (active_node_ != activation_target) {
                            set_active_node(nullptr);
                            return nullptr;
                        }
                    } else {
                        set_select_popup_open(*mutable_node(activation_target), false);
                    }
                } else {
                    set_select_popup_open(*mutable_node(activation_target), false);
                }
            } else
#endif
            if (activate_form_control(*mutable_node(activation_target))) {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
                if (form_control_kind(*activation_target) != FormControlKind::Select) {
                    dispatch_simple_event(activation_target, "input");
                    if (active_node_ != activation_target) {
                        set_active_node(nullptr);
                        return nullptr;
                    }
                    dispatch_simple_event(activation_target, "change");
                    if (active_node_ != activation_target) {
                        set_active_node(nullptr);
                        return nullptr;
                    }
                }
#else
                dispatch_simple_event(activation_target, "input");
                if (active_node_ != activation_target) {
                    set_active_node(nullptr);
                    return nullptr;
                }
                dispatch_simple_event(activation_target, "change");
                if (active_node_ != activation_target) {
                    set_active_node(nullptr);
                    return nullptr;
                }
#endif
            }
        }
        if (active_node_ == activation_target && form_control_kind(*activation_target) == FormControlKind::Range) {
            dispatch_simple_event(activation_target, "change");
            if (active_node_ != activation_target) {
                set_active_node(nullptr);
                return nullptr;
            }
        }
        MouseEvent click = make_mouse_event("click", input);
        dispatch_mouse_event(activation_target, click);
        if (click.target_destroyed() || active_node_ != activation_target) {
            set_active_node(nullptr);
            return nullptr;
        }
        if (!click.default_prevented()) {
            toggle_details_from_summary(activation_target);
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
            if (active_node_ == activation_target) {
                request_form_submit_from_control(*mutable_node(activation_target));
                if (active_node_ != activation_target) {
                    set_active_node(nullptr);
                    return nullptr;
                }
                reset_form_from_control(*mutable_node(activation_target));
            }
#endif
        }
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
    return event.target_destroyed() ? nullptr : target;
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
    const Node* const target = focused_node_;
    if (input.code == KeyCode::Backspace && backspace_control(*mutable_node(target))) {
        dispatch_simple_event(target, "input");
        return true;
    }
    if ((input.code == KeyCode::Enter || input.code == KeyCode::Tab) &&
        complete_text_control_from_datalist(*mutable_node(target))) {
        dispatch_simple_event(target, "input");
        if (focused_node_ == target) {
            dispatch_simple_event(target, "change");
        }
        return true;
    }
    if (input.code == KeyCode::ArrowDown && step_select_control(*mutable_node(target), 1)) {
        dispatch_simple_event(target, "input");
        if (focused_node_ == target) {
            dispatch_simple_event(target, "change");
        }
        return true;
    }
    if (input.code == KeyCode::ArrowUp && step_select_control(*mutable_node(target), -1)) {
        dispatch_simple_event(target, "input");
        if (focused_node_ == target) {
            dispatch_simple_event(target, "change");
        }
        return true;
    }
    if ((input.code == KeyCode::Space || input.code == KeyCode::Enter) &&
        is_form_control(*target) &&
        activate_form_control(*mutable_node(target))) {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        if (form_control_kind(*target) != FormControlKind::Select) {
            dispatch_simple_event(target, "input");
            if (focused_node_ == target) {
                dispatch_simple_event(target, "change");
            }
        }
#else
        dispatch_simple_event(target, "input");
        if (focused_node_ == target) {
            dispatch_simple_event(target, "change");
        }
#endif
        return true;
    }
    return false;
}

bool InputController::set_control_value(Node& node, std::string value) {
    if (!is_form_control(node) || !set_form_control_value(node, std::move(value))) {
        return false;
    }
    observe_node(&node);
    dispatch_simple_event(&node, "input");
    if (observes_node(&node)) {
        dispatch_simple_event(&node, "change");
    }
    unobserve_unused_nodes();
    return true;
}

bool InputController::set_control_checked(Node& node, bool checked) {
    if (!is_form_control(node) || !set_form_control_checked(node, checked)) {
        return false;
    }
    observe_node(&node);
    dispatch_simple_event(&node, "input");
    if (observes_node(&node)) {
        dispatch_simple_event(&node, "change");
    }
    unobserve_unused_nodes();
    return true;
}

bool InputController::set_control_selected_index(Node& node, int selected_index) {
    if (!is_form_control(node) || !set_form_control_selected_index(node, selected_index)) {
        return false;
    }
    observe_node(&node);
    dispatch_simple_event(&node, "input");
    if (observes_node(&node)) {
        dispatch_simple_event(&node, "change");
    }
    unobserve_unused_nodes();
    return true;
}

const Node* InputController::focus_next() {
    const LayoutBox* root = focus_root_box();
    if (root == nullptr) {
        return nullptr;
    }
    std::vector<const Node*> nodes;
    collect_focusable_nodes(*root, nodes);
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
    const LayoutBox* root = focus_root_box();
    if (root == nullptr) {
        return nullptr;
    }
    std::vector<const Node*> nodes;
    collect_focusable_nodes(*root, nodes);
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
    const Node* const target = focused_node_;
    if (is_form_control(*target) && activate_form_control(*mutable_node(target))) {
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        if (form_control_kind(*target) != FormControlKind::Select) {
            dispatch_simple_event(target, "input");
            if (focused_node_ != target) {
                return true;
            }
            dispatch_simple_event(target, "change");
            if (focused_node_ != target) {
                return true;
            }
        }
#else
        dispatch_simple_event(target, "input");
        if (focused_node_ != target) {
            return true;
        }
        dispatch_simple_event(target, "change");
        if (focused_node_ != target) {
            return true;
        }
#endif
    }
    PointerInput synthetic;
    MouseEvent click = make_mouse_event("click", synthetic);
    dispatch_mouse_event(target, click);
    if (click.target_destroyed() || focused_node_ != target) {
        return true;
    }
    if (!click.default_prevented()) {
        toggle_details_from_summary(target);
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
        request_form_submit_from_control(*mutable_node(target));
        if (focused_node_ == target) {
            reset_form_from_control(*mutable_node(target));
        }
#endif
    }
    return true;
}

void InputController::clear_pointer_state() {
    set_hovered_node(nullptr);
    set_active_node(nullptr);
}

HitTestResult InputController::hit(int x, int y) const {
    HitTestResult result = hit_tester_.hit_test(layer_tree_, x, y);
    if (!result || accepts_node(result.node)) {
        return result;
    }
    return {};
}

const Node* InputController::hit_node(int x, int y) const {
    HitTestResult result = hit(x, y);
    return result ? result.node : nullptr;
}

bool InputController::contains_node(const Node* node) const {
    return node != nullptr && find_layout_box_for_node(layer_tree_.box, node) != nullptr;
}

bool InputController::visible_node(const Node* node) const {
    const LayoutBox* box = find_layout_box_for_node(layer_tree_.box, node);
    return box != nullptr && !box->style.visibility_hidden;
}

bool InputController::accepts_node(const Node* node) const {
    return modal_root_ == nullptr || (node != nullptr && node_is_descendant_or_self(node, modal_root_));
}

const LayoutBox* InputController::focus_root_box() const {
    if (modal_root_ == nullptr) {
        return layer_tree_.box;
    }
    return find_layout_box_for_node(layer_tree_.box, modal_root_);
}

void InputController::set_hovered_node(const Node* node) {
    if (hovered_node_ != node) {
        mark_interaction_style_dirty(hovered_node_, invalidation_options_.hover_style);
        mark_interaction_style_dirty(node, invalidation_options_.hover_style);
    }
    hovered_node_ = node;
    observe_node(node);
    unobserve_unused_nodes();
}

void InputController::set_active_node(const Node* node, const LayoutBox* box) {
    if (active_node_ != node) {
        mark_interaction_style_dirty(active_node_, invalidation_options_.active_style);
        mark_interaction_style_dirty(node, invalidation_options_.active_style);
    }
    active_node_ = node;
    active_box_ = box;
    observe_node(node);
    unobserve_unused_nodes();
}

void InputController::observed_node_destroyed(Node& node, void* context) {
    auto* controller = static_cast<InputController*>(context);
    if (controller == nullptr) return;
    if (controller->hovered_node_ == &node) controller->hovered_node_ = nullptr;
    if (controller->active_node_ == &node) {
        controller->active_node_ = nullptr;
        controller->active_box_ = nullptr;
    }
    if (controller->focused_node_ == &node) controller->focused_node_ = nullptr;
    if (controller->modal_root_ == &node) controller->modal_root_ = nullptr;
    controller->observed_nodes_.erase(
        std::remove(controller->observed_nodes_.begin(), controller->observed_nodes_.end(), &node),
        controller->observed_nodes_.end());
}

void InputController::observe_node(const Node* node) {
    if (node == nullptr) return;
    Node* mutable_node_ptr = const_cast<Node*>(node);
    if (std::find(observed_nodes_.begin(), observed_nodes_.end(), mutable_node_ptr) == observed_nodes_.end()) {
        mutable_node_ptr->add_destroy_observer(observed_node_destroyed, this);
        observed_nodes_.push_back(mutable_node_ptr);
    }
}

bool InputController::observes_node(const Node* node) const {
    return node != nullptr && std::find(observed_nodes_.begin(), observed_nodes_.end(), node) != observed_nodes_.end();
}

void InputController::unobserve_unused_nodes() {
    observed_nodes_.erase(
        std::remove_if(observed_nodes_.begin(), observed_nodes_.end(), [this](Node* node) {
            const bool used = node != nullptr &&
                (hovered_node_ == node || active_node_ == node || focused_node_ == node || modal_root_ == node);
            if (!used && node != nullptr) node->remove_destroy_observer(observed_node_destroyed, this);
            return !used;
        }),
        observed_nodes_.end());
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

bool InputController::dispatch_simple_event(const Node* target, const char* type) const {
    if (target == nullptr) {
        return false;
    }
    Event event(type, true, false);
    dispatch_event(*target, event);
    return !event.target_destroyed();
}

void InputController::update_hover(const Node* next_hover, const PointerInput& input) {
    if (next_hover == hovered_node_) {
        return;
    }
    observe_node(next_hover);
    if (hovered_node_ != nullptr) {
        MouseEvent out = make_mouse_event("mouseout", input);
        dispatch_event(*hovered_node_, out);
    }
    if (next_hover != nullptr && !observes_node(next_hover)) {
        set_hovered_node(nullptr);
        return;
    }
    set_hovered_node(next_hover);
    if (hovered_node_ != nullptr) {
        MouseEvent over = make_mouse_event("mouseover", input);
        dispatch_event(*hovered_node_, over);
    }
}

} // namespace jellyframe
