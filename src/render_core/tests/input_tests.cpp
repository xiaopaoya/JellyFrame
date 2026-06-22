#include "render_core/css_parser.h"
#include "render_core/form_control.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Pipeline {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
    LayerNodePtr layer_tree;

    Pipeline(std::unique_ptr<Node> document_in,
             Stylesheet stylesheet_in,
             StyleResolver resolver_in,
             RenderObjectPtr render_tree_in,
             LayoutBoxPtr layout_tree_in,
             LayerNodePtr layer_tree_in)
        : document(std::move(document_in)),
          stylesheet(std::move(stylesheet_in)),
          resolver(std::move(resolver_in)),
          render_tree(std::move(render_tree_in)),
          layout_tree(std::move(layout_tree_in)),
          layer_tree(std::move(layer_tree_in)) {}
};

Pipeline build_pipeline() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><button id='one'>One</button><button id='two'>Two</button></body>");
    Stylesheet stylesheet = css_parser.parse(
        "button { display: block; width: 80px; height: 24px; margin: 0; }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 160);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    return Pipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                    std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

Node* find_by_id(Node& node, const std::string& id) {
    if (node.attribute("id") == id) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = find_by_id(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_box_by_id(const LayoutBox& box, const std::string& id) {
    if (box.node != nullptr && box.node->attribute("id") == id) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_box_by_id(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Pipeline build_form_pipeline(const char* html, const char* css = "") {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    return Pipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                    std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

void pointer_hover_down_up_and_click() {
    auto pipeline = build_pipeline();
    Node* one = find_by_id(*pipeline.document, "one");
    check(one != nullptr, "button one exists");

    std::vector<std::string> events;
    one->add_event_listener("mouseover", [&](Event&) { events.push_back("over"); });
    one->add_event_listener("mousemove", [&](Event&) { events.push_back("move"); });
    one->add_event_listener("pointerdown", [&](Event&) { events.push_back("pointerdown"); });
    one->add_event_listener("touchstart", [&](Event&) { events.push_back("touchstart"); });
    one->add_event_listener("mousedown", [&](Event&) { events.push_back("down"); });
    one->add_event_listener("pointerup", [&](Event&) { events.push_back("pointerup"); });
    one->add_event_listener("touchend", [&](Event&) { events.push_back("touchend"); });
    one->add_event_listener("mouseup", [&](Event&) { events.push_back("up"); });
    one->add_event_listener("click", [&](Event&) { events.push_back("click"); });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = 4;
    pointer.y = 4;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;

    check(input.pointer_move(pointer) == one, "move hits first button");
    check(input.hovered_node() == one, "hover state set");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyStyle) != 0U,
          "hover state marks style dirty");
    clear_dirty_flags(*pipeline.document);
    check(input.pointer_down(pointer) == one, "down hits first button");
    check(input.active_node() == one, "active state set");
    check(input.focused_node() == one, "focus state set");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyStyle) != 0U,
          "active/focus state marks style dirty");
    pointer.buttons = 0;
    check(input.pointer_up(pointer) == one, "up hits first button");
    check(input.active_node() == nullptr, "active state cleared");

    check(events.size() == 9, "all pointer events fired");
    check(events[0] == "over", "mouseover first");
    check(events[1] == "move", "mousemove second");
    check(events[2] == "pointerdown", "pointerdown before mousedown");
    check(events[3] == "touchstart", "touchstart before mousedown");
    check(events[4] == "down", "mousedown after pointer aliases");
    check(events[5] == "pointerup", "pointerup before mouseup");
    check(events[6] == "touchend", "touchend before mouseup");
    check(events[7] == "up", "mouseup after pointer aliases");
    check(events[8] == "click", "click synthesized");
}

void pointer_events_without_dynamic_style_keep_document_clean() {
    auto pipeline = build_pipeline();
    Node* one = find_by_id(*pipeline.document, "one");
    check(one != nullptr, "button one exists");

    std::vector<std::string> events;
    one->add_event_listener("mouseover", [&](Event&) { events.push_back("over"); });
    one->add_event_listener("mousemove", [&](Event&) { events.push_back("move"); });
    one->add_event_listener("mousedown", [&](Event&) { events.push_back("down"); });
    one->add_event_listener("mouseup", [&](Event&) { events.push_back("up"); });
    one->add_event_listener("click", [&](Event&) { events.push_back("click"); });

    InteractionInvalidationOptions invalidation;
    invalidation.hover_style = false;
    invalidation.active_style = false;
    invalidation.focus_style = false;
    InputController input(*pipeline.layer_tree, invalidation);
    clear_dirty_flags(*pipeline.document);

    PointerInput pointer;
    pointer.x = 4;
    pointer.y = 4;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;

    check(input.pointer_move(pointer) == one, "move still hits first button");
    check(input.hovered_node() == one, "hover state still updates");
    check(subtree_dirty_flags(*pipeline.document) == DomDirtyNone,
          "hover without matching dynamic CSS keeps document clean");

    check(input.pointer_down(pointer) == one, "down still hits first button");
    check(input.active_node() == one, "active state still updates");
    check(input.focused_node() == one, "focus state still updates");
    check(subtree_dirty_flags(*pipeline.document) == DomDirtyNone,
          "active/focus without matching dynamic CSS keeps document clean");

    pointer.buttons = 0;
    check(input.pointer_up(pointer) == one, "up still hits first button");
    check(input.active_node() == nullptr, "active state still clears");
    check(subtree_dirty_flags(*pipeline.document) == DomDirtyNone,
          "active clear without matching dynamic CSS keeps document clean");

    check(events.size() == 5, "mouse events still dispatched");
    check(events[0] == "over", "mouseover still dispatched");
    check(events[1] == "move", "mousemove still dispatched");
    check(events[2] == "down", "mousedown still dispatched");
    check(events[3] == "up", "mouseup still dispatched");
    check(events[4] == "click", "click still dispatched");
}

void click_requires_same_active_target() {
    auto pipeline = build_pipeline();
    Node* one = find_by_id(*pipeline.document, "one");
    Node* two = find_by_id(*pipeline.document, "two");
    check(one != nullptr && two != nullptr, "buttons exist");

    int one_clicks = 0;
    int two_clicks = 0;
    one->add_event_listener("click", [&](Event&) { ++one_clicks; });
    two->add_event_listener("click", [&](Event&) { ++two_clicks; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = 4;
    pointer.y = 4;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);

    pointer.x = 4;
    pointer.y = 44;
    pointer.buttons = 0;
    input.pointer_up(pointer);

    check(one_clicks == 0, "first button did not click after release elsewhere");
    check(two_clicks == 0, "second button did not click without matching down");
}

void wheel_dispatches_to_hit_target() {
    auto pipeline = build_pipeline();
    Node* two = find_by_id(*pipeline.document, "two");
    check(two != nullptr, "button two exists");

    int wheel_count = 0;
    int observed_delta = 0;
    two->add_event_listener("wheel", [&](Event& event) {
        ++wheel_count;
        auto* wheel_event = dynamic_cast<WheelEvent*>(&event);
        check(wheel_event != nullptr, "wheel event type");
        observed_delta = wheel_event->delta_y;
    });

    InputController input(*pipeline.layer_tree);
    WheelInput wheel;
    wheel.x = 4;
    wheel.y = 44;
    wheel.delta_y = -120;
    check(input.wheel(wheel) == two, "wheel hits second button");
    check(wheel_count == 1, "wheel listener called");
    check(observed_delta == -120, "wheel delta preserved");
}

void text_input_updates_focused_control_value() {
    auto pipeline = build_form_pipeline("<body><input id='name' value='A'></body>",
                                        "input { width: 120px; height: 24px; }");
    Node* input_node = find_by_id(*pipeline.document, "name");
    const LayoutBox* input_box = find_box_by_id(*pipeline.layout_tree, "name");
    check(input_node != nullptr && input_box != nullptr, "text input exists");

    int input_events = 0;
    input_node->add_event_listener("input", [&](Event&) { ++input_events; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = input_box->rect.x + 2;
    pointer.y = input_box->rect.y + 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);
    clear_dirty_flags(*pipeline.document);

    check(input.text_input("B"), "text input accepted");
    check(form_control_display_text(*input_node) == "AB", "text input value updated");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyPaint) != 0U,
          "text input marks document dirty for repaint");
    check(!dirty_requires_render_or_layout(subtree_dirty_flags(*pipeline.document)),
          "text input can reuse render/layout");
    clear_dirty_flags(*pipeline.document);
    KeyInput key;
    key.code = KeyCode::Backspace;
    check(input.key_down(key), "backspace accepted");
    check(form_control_display_text(*input_node) == "A", "backspace updates value");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyPaint) != 0U,
          "backspace marks document dirty for repaint");
    check(input_events == 2, "text input events dispatched");
}

void datalist_completion_updates_text_control() {
    auto pipeline = build_form_pipeline(
        "<body><input id='search' list='suggest'><datalist id='suggest'>"
        "<option value='HTML5 semantic'></option><option value='CSS Grid'></option>"
        "</datalist></body>",
        "input { width: 120px; height: 24px; }");
    Node* input_node = find_by_id(*pipeline.document, "search");
    const LayoutBox* input_box = find_box_by_id(*pipeline.layout_tree, "search");
    check(input_node != nullptr && input_box != nullptr, "datalist input exists");

    int changes = 0;
    input_node->add_event_listener("change", [&](Event&) { ++changes; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = input_box->rect.x + 2;
    pointer.y = input_box->rect.y + 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);
    check(input.text_input("cs"), "datalist prefix typed");
    clear_dirty_flags(*pipeline.document);

    KeyInput key;
    key.code = KeyCode::Tab;
    check(input.key_down(key), "tab completes from datalist");
    check(form_control_display_text(*input_node) == "CSS Grid", "datalist completion selects matching option");
    check(changes == 1, "datalist completion dispatches change");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyPaint) != 0U,
          "datalist completion marks document dirty for repaint");
}

void deep_form_control_helpers_are_iterative() {
    auto document = make_element("document");
    Node& body = document->append_child(make_element("body"));
    Node& input = body.append_child(make_element("input"));
    input.attributes["list"] = "suggest";

    Node* current = &body;
    for (int depth = 0; depth < 4096; ++depth) {
        current = &current->append_child(make_element("div"));
    }
    Node& datalist = current->append_child(make_element("datalist"));
    datalist.attributes["id"] = "suggest";
    Node& datalist_option = datalist.append_child(make_element("option"));
    datalist_option.attributes["value"] = "Deep Match";

    Node& textarea = current->append_child(make_element("textarea"));
    Node* text_parent = &textarea;
    for (int depth = 0; depth < 128; ++depth) {
        text_parent = &text_parent->append_child(make_element("span"));
    }
    text_parent->append_child(make_text("Nested text"));

    Node& select = current->append_child(make_element("select"));
    Node& group = select.append_child(make_element("optgroup"));
    group.attributes["label"] = "Nested";
    group.append_child(make_element("option")).append_child(make_text("One"));
    Node& selected = group.append_child(make_element("option"));
    selected.attributes["selected"] = "";
    selected.append_child(make_text("Two"));

    ensure_form_control_state(input).value = "deep";
    check(complete_text_control_from_datalist(input), "deep datalist completion works");
    check(form_control_display_text(input) == "Deep Match", "deep datalist completion value");
    check(form_control_display_text(textarea) == "Nested text", "deep textarea text collected");
    check(form_control_selected_index(select) == 1, "deep select selected index");
    check(form_control_display_text(select) == "Two", "deep select display text");
}

void checkbox_click_toggles_checked_state() {
    auto pipeline = build_form_pipeline("<body><input id='agree' type='checkbox'></body>");
    Node* checkbox = find_by_id(*pipeline.document, "agree");
    const LayoutBox* box = find_box_by_id(*pipeline.layout_tree, "agree");
    check(checkbox != nullptr && box != nullptr, "checkbox exists");

    int changes = 0;
    checkbox->add_event_listener("change", [&](Event&) { ++changes; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = box->rect.x + 2;
    pointer.y = box->rect.y + 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);

    check(ensure_form_control_state(*checkbox).checked, "checkbox checked after click");
    check(changes == 1, "checkbox change event dispatched");
}

void range_drag_updates_value() {
    auto pipeline = build_form_pipeline("<body><input id='volume' type='range' min='0' max='100' value='0'></body>");
    Node* range = find_by_id(*pipeline.document, "volume");
    const LayoutBox* box = find_box_by_id(*pipeline.layout_tree, "volume");
    check(range != nullptr && box != nullptr, "range exists");

    int input_events = 0;
    range->add_event_listener("input", [&](Event&) { ++input_events; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = box->rect.x + box->rect.width - 1;
    pointer.y = box->rect.y + box->rect.height / 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);

    const int value = std::stoi(ensure_form_control_state(*range).value);
    check(value >= 95, "range value updates from pointer");
    check(input_events == 1, "range input event dispatched");
}

void select_click_cycles_selected_option() {
    auto pipeline = build_form_pipeline(
        "<body><select id='choice'><option>One</option><option>Two</option></select></body>");
    Node* select = find_by_id(*pipeline.document, "choice");
    const LayoutBox* box = find_box_by_id(*pipeline.layout_tree, "choice");
    check(select != nullptr && box != nullptr, "select exists");

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = box->rect.x + 2;
    pointer.y = box->rect.y + 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);

    check(form_control_display_text(*select) == "Two", "select cycles selected option");
}

void unchanged_form_activation_stays_clean() {
    auto pipeline = build_form_pipeline(
        "<body><input id='choice' type='radio' checked><select id='single'><option>One</option></select></body>");
    Node* radio = find_by_id(*pipeline.document, "choice");
    Node* select = find_by_id(*pipeline.document, "single");
    check(radio != nullptr && select != nullptr, "unchanged form controls exist");

    check(ensure_form_control_state(*radio).checked, "radio starts checked");
    clear_dirty_flags(*pipeline.document);
    check(!activate_form_control(*radio), "checked radio activation is a no-op");
    check(subtree_dirty_flags(*pipeline.document) == DomDirtyNone, "checked radio no-op stays clean");

    check(form_control_display_text(*select) == "One", "single select starts at only option");
    clear_dirty_flags(*pipeline.document);
    check(!activate_form_control(*select), "single-option select activation is a no-op");
    check(subtree_dirty_flags(*pipeline.document) == DomDirtyNone, "single-option select no-op stays clean");
}

void select_arrow_keys_work_through_optgroups() {
    auto pipeline = build_form_pipeline(
        "<body><select id='choice'><optgroup label='A'><option>One</option><option>Two</option></optgroup>"
        "<optgroup label='B'><option>Three</option></optgroup></select></body>");
    Node* select = find_by_id(*pipeline.document, "choice");
    const LayoutBox* box = find_box_by_id(*pipeline.layout_tree, "choice");
    check(select != nullptr && box != nullptr, "optgroup select exists");

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.x = box->rect.x + 2;
    pointer.y = box->rect.y + 2;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);
    clear_dirty_flags(*pipeline.document);

    KeyInput key;
    key.code = KeyCode::ArrowDown;
    check(input.key_down(key), "arrow down selects next option");
    check(form_control_display_text(*select) == "Three", "select arrow down crosses optgroup");
    key.code = KeyCode::ArrowUp;
    check(input.key_down(key), "arrow up selects previous option");
    check(form_control_display_text(*select) == "Two", "select arrow up crosses optgroup");
    check((subtree_dirty_flags(*pipeline.document) & DomDirtyPaint) != 0U,
          "select keyboard change marks document dirty for repaint");
}

void disabled_control_ignores_pointer_and_text_input() {
    auto pipeline = build_form_pipeline("<body><input id='name' disabled value='A'><button id='go' disabled>Go</button></body>",
                                        "input, button { display: block; width: 120px; height: 24px; }");
    Node* input_node = find_by_id(*pipeline.document, "name");
    Node* button = find_by_id(*pipeline.document, "go");
    const LayoutBox* input_box = find_box_by_id(*pipeline.layout_tree, "name");
    const LayoutBox* button_box = find_box_by_id(*pipeline.layout_tree, "go");
    check(input_node != nullptr && button != nullptr && input_box != nullptr && button_box != nullptr,
          "disabled controls exist");

    int clicks = 0;
    int input_events = 0;
    button->add_event_listener("click", [&](Event&) { ++clicks; });
    input_node->add_event_listener("input", [&](Event&) { ++input_events; });

    InputController input(*pipeline.layer_tree);
    PointerInput pointer;
    pointer.button = PointerButton::Primary;
    pointer.buttons = 1;
    pointer.x = button_box->rect.x + 2;
    pointer.y = button_box->rect.y + 2;
    input.pointer_down(pointer);
    pointer.buttons = 0;
    input.pointer_up(pointer);
    check(clicks == 0, "disabled button ignores click");

    input.set_focused_node(input_node);
    check(!input.text_input("B"), "disabled text input rejects text");
    check(form_control_display_text(*input_node) == "A", "disabled input value unchanged");
    check(input_events == 0, "disabled input dispatches no input event");
}

void focus_navigation_skips_disabled_and_activates() {
    auto pipeline = build_form_pipeline(
        "<body><button id='first'>First</button><button id='off' disabled>Off</button>"
        "<input id='agree' type='checkbox'><select id='choice'><option>One</option><option>Two</option></select></body>",
        "button, input, select { display: block; width: 120px; height: 24px; }");
    Node* first = find_by_id(*pipeline.document, "first");
    Node* disabled = find_by_id(*pipeline.document, "off");
    Node* checkbox = find_by_id(*pipeline.document, "agree");
    Node* select = find_by_id(*pipeline.document, "choice");
    check(first != nullptr && disabled != nullptr && checkbox != nullptr && select != nullptr,
          "focus navigation fixture exists");

    int first_clicks = 0;
    int checkbox_clicks = 0;
    first->add_event_listener("click", [&](Event&) { ++first_clicks; });
    checkbox->add_event_listener("click", [&](Event&) { ++checkbox_clicks; });

    InputController input(*pipeline.layer_tree);
    check(input.focus_next() == first, "focus starts at first control");
    check(input.activate_focused(), "hardware activate clicks focused button");
    check(first_clicks == 1, "focused button receives click");
    check(input.focus_next() == checkbox, "focus skips disabled control");
    check(input.activate_focused(), "hardware activate toggles checkbox");
    check(ensure_form_control_state(*checkbox).checked, "focused checkbox toggled");
    check(checkbox_clicks == 1, "focused checkbox receives click");
    check(input.focus_next() == select, "focus advances to select");
    check(input.activate_focused(), "hardware activate cycles select");
    check(form_control_display_text(*select) == "Two", "focused select cycles value");
    check(input.focus_next() == first, "focus wraps forward");
    check(input.focus_previous() == select, "focus wraps backward");
}

} // namespace

int main() {
    try {
        pointer_hover_down_up_and_click();
        pointer_events_without_dynamic_style_keep_document_clean();
        click_requires_same_active_target();
        wheel_dispatches_to_hit_target();
        text_input_updates_focused_control_value();
        checkbox_click_toggles_checked_state();
        range_drag_updates_value();
        select_click_cycles_selected_option();
        unchanged_form_activation_stays_clean();
        datalist_completion_updates_text_control();
        deep_form_control_helpers_are_iterative();
        select_arrow_keys_work_through_optgroups();
        disabled_control_ignores_pointer_and_text_input();
        focus_navigation_skips_disabled_and_activates();
    } catch (const std::exception& error) {
        std::cerr << "input test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "input tests passed\n";
    return 0;
}
