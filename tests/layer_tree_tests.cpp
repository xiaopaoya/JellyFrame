#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace wearweb;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct BuiltPipeline {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    std::unique_ptr<RenderObject> render_tree;
    std::unique_ptr<LayoutBox> layout_tree;
    std::unique_ptr<LayerNode> layer_tree;

    BuiltPipeline(std::unique_ptr<Node> document_in,
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

BuiltPipeline build_pipeline(const char* html, const char* css) {
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
    return BuiltPipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                         std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

const LayerNode* find_layer_with_reason(const LayerNode& layer, LayerReason reason) {
    if ((layer.reasons & reason) != 0U) {
        return &layer;
    }
    for (const auto& child : layer.children) {
        const LayerNode* found = find_layer_with_reason(*child, reason);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void overflow_hidden_creates_clip_layer() {
    auto pipeline = build_pipeline("<body><section class='clip'><p>Visible</p></section></body>",
                                   ".clip { overflow: hidden; height: 20px; background: #ffffff; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOverflowClip);
    check(layer != nullptr, "overflow layer exists");
    check(layer->type == LayerType::Clip, "overflow layer is clip layer");
    check(layer->has_clip, "overflow layer has clip");
}

void opacity_layer_flattens_alpha() {
    auto pipeline = build_pipeline("<body><section class='fade'>Faded</section></body>",
                                   ".fade { opacity: .5; background: #000000; }");

    const LayerNode* layer = find_layer_with_reason(*pipeline.layer_tree, LayerReasonOpacity);
    check(layer != nullptr, "opacity layer exists");
    check(layer->type == LayerType::Composited, "opacity layer is composited");

    LayerTreeBuilder layer_tree_builder;
    DisplayList flattened = layer_tree_builder.flatten(*pipeline.layer_tree);
    bool found_translucent_fill = false;
    for (const DisplayCommand& command : flattened) {
        if (command.type == DisplayCommandType::FillRect && command.color.a > 0 && command.color.a < 255) {
            found_translucent_fill = true;
        }
    }
    check(found_translucent_fill, "flatten applies layer opacity");
}

void z_index_orders_child_layers() {
    auto pipeline = build_pipeline("<body><div class='back'>Back</div><div class='front'>Front</div></body>",
                                   ".back { position: relative; z-index: 5; }"
                                   ".front { position: relative; z-index: 10; }");

    check(pipeline.layer_tree->children.size() >= 2, "positioned children create layers");
    const LayerNode* previous = nullptr;
    for (const auto& child : pipeline.layer_tree->children) {
        if ((child->reasons & LayerReasonZIndex) == 0U) {
            continue;
        }
        if (previous != nullptr) {
            check(previous->z_index <= child->z_index, "z-index children are sorted");
        }
        previous = child.get();
    }
}

} // namespace

int main() {
    try {
        overflow_hidden_creates_clip_layer();
        opacity_layer_flattens_alpha();
        z_index_orders_child_layers();
    } catch (const std::exception& error) {
        std::cerr << "layer tree test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layer tree tests passed\n";
    return 0;
}
