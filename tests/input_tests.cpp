#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/input.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace wearweb;

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
    std::unique_ptr<RenderObject> render_tree;
    std::unique_ptr<LayoutBox> layout_tree;
    std::unique_ptr<LayerNode> layer_tree;

    Pipeline(std::unique_ptr<Node> document_in,
             Stylesheet stylesheet_in,
             StyleResolver resolver_in,
             std::unique_ptr<RenderObject> render_tree_in,
             std::unique_ptr<LayoutBox> layout_tree_in,
             std::unique_ptr<LayerNode> layer_tree_in)
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

void pointer_hover_down_up_and_click() {
    auto pipeline = build_pipeline();
    Node* one = find_by_id(*pipeline.document, "one");
    check(one != nullptr, "button one exists");

    std::vector<std::string> events;
    one->add_event_listener("mouseover", [&](Event&) { events.push_back("over"); });
    one->add_event_listener("mousemove", [&](Event&) { events.push_back("move"); });
    one->add_event_listener("mousedown", [&](Event&) { events.push_back("down"); });
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
    check(input.pointer_down(pointer) == one, "down hits first button");
    check(input.active_node() == one, "active state set");
    check(input.focused_node() == one, "focus state set");
    pointer.buttons = 0;
    check(input.pointer_up(pointer) == one, "up hits first button");
    check(input.active_node() == nullptr, "active state cleared");

    check(events.size() == 5, "all pointer events fired");
    check(events[0] == "over", "mouseover first");
    check(events[1] == "move", "mousemove second");
    check(events[2] == "down", "mousedown third");
    check(events[3] == "up", "mouseup fourth");
    check(events[4] == "click", "click synthesized");
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

} // namespace

int main() {
    try {
        pointer_hover_down_up_and_click();
        click_requires_same_active_target();
        wheel_dispatches_to_hit_target();
    } catch (const std::exception& error) {
        std::cerr << "input test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "input tests passed\n";
    return 0;
}
