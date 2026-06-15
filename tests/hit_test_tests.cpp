#include "core/css_parser.h"
#include "core/hit_test.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

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

Pipeline build_pipeline(const char* html, const char* css, int viewport_width = 240) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
    LayerTreeBuilder layer_tree_builder;
    auto layer_tree = layer_tree_builder.build(*layout_tree);
    return Pipeline(std::move(document), std::move(stylesheet), std::move(resolver),
                    std::move(render_tree), std::move(layout_tree), std::move(layer_tree));
}

void basic_hit_test_returns_deepest_element() {
    auto pipeline = build_pipeline(
        "<body><section class='card'><button id='go'>Go</button></section></body>",
        "body { padding: 0; } .card { padding: 8px; background: #ffffff; }"
        "button { display: inline-block; width: 80px; height: 24px; }");

    HitTester hit_tester;
    HitTestResult result = hit_tester.hit_test(*pipeline.layer_tree, 12, 12);

    check(static_cast<bool>(result), "hit result exists");
    check(result.node != nullptr && result.node->attribute("id") == "go", "button is hit target");
}

void layer_order_prefers_higher_z_index() {
    auto pipeline = build_pipeline(
        "<body><div id='back'>Back</div><div id='front'>Front</div></body>",
        "div { display: block; width: 100px; height: 40px; position: relative; }"
        "#back { background: #2563eb; z-index: 1; }"
        "#front { background: #dc2626; z-index: 2; margin: -40px; }");

    HitTester hit_tester;
    HitTestResult result = hit_tester.hit_test(*pipeline.layer_tree, 4, 4);

    check(static_cast<bool>(result), "z-index hit result exists");
    check(result.node != nullptr && result.node->attribute("id") == "front", "higher z-index layer wins");
}

void overflow_clip_blocks_outside_hits() {
    auto pipeline = build_pipeline(
        "<body><section class='clip'><button id='inside'>Inside</button></section></body>",
        ".clip { width: 80px; height: 20px; overflow: hidden; }"
        "button { display: block; width: 80px; height: 40px; }");

    HitTester hit_tester;
    HitTestResult inside = hit_tester.hit_test(*pipeline.layer_tree, 4, 4);
    HitTestResult clipped = hit_tester.hit_test(*pipeline.layer_tree, 4, 30);

    check(static_cast<bool>(inside), "inside clipped layer hit exists");
    check(inside.node != nullptr && inside.node->attribute("id") == "inside", "inside button hit");
    check(!clipped || clipped.node->attribute("id") != "inside", "outside clip does not hit button");
}

} // namespace

int main() {
    try {
        basic_hit_test_returns_deepest_element();
        layer_order_prefers_higher_z_index();
        overflow_clip_blocks_outside_hits();
    } catch (const std::exception& error) {
        std::cerr << "hit test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "hit test tests passed\n";
    return 0;
}
