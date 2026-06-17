#include "core/css_parser.h"
#include "core/display_invalidation.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include <iostream>
#include <stdexcept>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

LayerNodePtr build_layer_tree(const char* html, const char* css) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    StyleResolver resolver(css_parser.parse(css));
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    LayerTreeBuilder layer_builder;
    return layer_builder.build(*layout_tree);
}

void empty_dirty_rects_report_no_work() {
    auto layer_tree = build_layer_tree("<body><p>A</p></body>", "body { margin: 0; } p { width: 80px; }");
    const DisplayInvalidationResult result = analyze_display_invalidation(*layer_tree, nullptr, 0);
    check(result.dirty_rect_count == 0, "empty dirty rect count");
    check(result.layers_visited == 0, "empty dirty rects skip layer traversal");
    check(result.commands_visited == 0, "empty dirty rects skip command traversal");
}

void local_dirty_rect_reports_intersecting_commands_and_layers() {
    auto layer_tree = build_layer_tree(
        "<body><section class='a'>A</section><section class='b'>B</section></body>",
        "body { margin: 0; } section { width: 80px; height: 30px; margin: 0; } "
        ".a { background: #000000; } .b { background: #777777; }");
    const Rect dirty{0, 0, 90, 35};
    const DisplayInvalidationResult result = analyze_display_invalidation(*layer_tree, &dirty, 1);
    check(result.dirty_rect_count == 1, "dirty rect count");
    check(result.dirty_area == 3150, "dirty area is accumulated");
    check(result.layers_visited >= 1, "layers are visited");
    check(result.layers_intersecting >= 1, "root layer intersects dirty rect");
    check(result.commands_visited >= 2, "display commands are counted");
    check(result.commands_intersecting > 0, "dirty rect intersects at least one command");
    check(result.commands_intersecting < result.commands_visited,
          "local dirty rect leaves some display commands untouched");
}

void clipped_and_composited_layers_are_visible_in_diagnostics() {
    auto layer_tree = build_layer_tree(
        "<body><div class='clip'><div class='fade'>A</div></div></body>",
        "body { margin: 0; } .clip { width: 100px; height: 50px; overflow: hidden; } "
        ".fade { width: 80px; height: 40px; opacity: 0.5; background: #000000; }");
    const Rect dirty{0, 0, 100, 60};
    const DisplayInvalidationResult result = analyze_display_invalidation(*layer_tree, &dirty, 1);
    check(result.clipped_layers_intersecting > 0, "clipped layer is counted");
    check(result.composited_layers_intersecting > 0, "composited layer is counted");
}

} // namespace

int main() {
    try {
        empty_dirty_rects_report_no_work();
        local_dirty_rect_reports_intersecting_commands_and_layers();
        clipped_and_composited_layers_are_visible_in_diagnostics();
    } catch (const std::exception& error) {
        std::cerr << "display invalidation test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "display invalidation tests passed\n";
    return 0;
}
