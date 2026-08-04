#include "app_runtime/script_task_input_dispatch.h"

#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {
struct Pipeline {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
    LayerNodePtr layer_tree;
};

Pipeline make_pipeline() {
    HtmlParser parser;
    CssParser css;
    auto document = parser.parse("<body><button id='ok'>OK</button></body>");
    Stylesheet stylesheet = css.parse("button { display: block; width: 80px; height: 24px; margin: 0; }");
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render(resolver);
    auto render_tree = render.build(*document);
    LayoutEngine layout(resolver);
    auto layout_tree = layout.layout(*render_tree, 160, 80);
    LayerTreeBuilder layers;
    auto layer_tree = layers.build(*layout_tree);
    return {std::move(document), std::move(stylesheet), std::move(resolver), std::move(render_tree),
            std::move(layout_tree), std::move(layer_tree)};
}

Node* find_id(Node& node, const char* id) {
    if (node.attribute("id") == id) return &node;
    for (const auto& child : node.children) if (Node* found = find_id(*child, id)) return found;
    return nullptr;
}

void normalized_packets_dispatch_only_inside_worker_input_controller() {
    Pipeline pipeline = make_pipeline();
    Node* button = find_id(*pipeline.document, "ok");
    assert(button != nullptr);
    int clicks = 0;
    button->add_event_listener("click", [&clicks](Event&) { ++clicks; });
    InputController controller(*pipeline.layer_tree);
    ScriptTaskInputCodecOptions options{0, 32};
    ScriptTaskInputEvent down;
    down.kind = ScriptTaskInputKind::PointerDown;
    down.x = 4; down.y = 4; down.button = 0; down.buttons = 1;
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_input(down, options, bytes) == ScriptTaskInputCodecStatus::Accepted);
    assert(dispatch_script_task_input_packet(controller, {ScriptTaskPacketKind::Input, {1, 1, 1}, 1, 0, bytes}, options).handled);
    down.kind = ScriptTaskInputKind::PointerUp;
    down.buttons = 0;
    assert(encode_script_task_input(down, options, bytes) == ScriptTaskInputCodecStatus::Accepted);
    assert(dispatch_script_task_input_packet(controller, {ScriptTaskPacketKind::Input, {1, 1, 1}, 2, 0, bytes}, options).handled);
    assert(clicks == 1);
}

void invalid_packets_and_key_codes_are_rejected_without_dom_dispatch() {
    Pipeline pipeline = make_pipeline();
    InputController controller(*pipeline.layer_tree);
    ScriptTaskInputCodecOptions options{8, 40};
    assert(dispatch_script_task_input_packet(controller, {ScriptTaskPacketKind::ServiceCompletion, {1, 1, 1}, 1, 0, {}}, options).status ==
           ScriptTaskInputDispatchStatus::PacketRejected);
    ScriptTaskInputEvent key;
    key.kind = ScriptTaskInputKind::KeyDown;
    key.key_code = 999;
    assert(dispatch_script_task_input(controller, key).status == ScriptTaskInputDispatchStatus::EventRejected);
}
}

int script_task_input_dispatch_tests_main() {
    normalized_packets_dispatch_only_inside_worker_input_controller();
    invalid_packets_and_key_codes_are_rejected_without_dom_dispatch();
    std::cout << "script task input dispatch tests passed\n";
    return 0;
}
