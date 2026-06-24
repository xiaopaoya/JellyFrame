#include "render_core/animation_invalidation.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/style_repaint.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool fixed_measure(const std::string& text,
                   int,
                   int,
                   TextMetrics* metrics,
                   void*) {
    if (metrics == nullptr) {
        return false;
    }
    metrics->width = static_cast<int>(text.size()) * 8;
    metrics->line_height = 12;
    return true;
}

TextMeasureProvider fixed_text_measure() {
    return TextMeasureProvider{fixed_measure, nullptr};
}

Node* find_element_by_id(Node& node, const std::string& id) {
    if (node.type == NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (auto& child : node.children) {
        if (Node* found = find_element_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

struct Fixture {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
};

Fixture build_fixture(const std::string& html, const std::string& css) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout_engine(resolver, fixed_text_measure());
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    clear_dirty_flags(*document);
    return Fixture{
        std::move(document),
        std::move(stylesheet),
        std::move(resolver),
        std::move(render_tree),
        std::move(layout_tree),
    };
}

RenderObjectPtr rebuild_render_tree(const Node& document, const StyleResolver& resolver) {
    RenderTreeBuilder builder(resolver);
    return builder.build(document);
}

void paint_only_class_change_can_reuse_layout() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); opacity: .9; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "paint-only class change can reuse layout");
    check(apply_render_styles_to_layout(*next_render, *fixture.layout_tree),
          "new paint styles can be applied to retained layout");

    std::vector<StyleOverride> previous_overrides;
    std::vector<StyleOverride> current_overrides;
    collect_style_repaint_overrides(*fixture.render_tree,
                                    *next_render,
                                    previous_overrides,
                                    current_overrides);
    check(previous_overrides.size() == 1 && current_overrides.size() == 1,
          "paint-only class change produces repaint overrides");
    check(previous_overrides.front().has_transform && current_overrides.front().has_transform,
          "transform repaint override is captured");
    check(previous_overrides.front().has_background_color &&
              current_overrides.front().has_background_color,
          "background repaint override is captured");
    check(previous_overrides.front().has_opacity && current_overrides.front().has_opacity,
          "opacity repaint override is captured");
}

void layout_class_change_stays_conservative() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; }"
        ".pill.active { width: 120px; transform: scale(1.05); }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "layout-affecting class change does not reuse layout");
}

void transform_class_change_invalidates_old_and_new_bounds() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button></body>",
        ".pill { display: block; width: 80px; height: 20px; transform: translate(0px, 0px); }"
        ".pill.active { transform: translate(40px, 0px); background-color: #222222; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    check(pulse != nullptr, "pulse exists");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "transform-only class change can reuse layout");
    std::vector<StyleOverride> previous_overrides;
    std::vector<StyleOverride> current_overrides;
    collect_style_repaint_overrides(*fixture.render_tree,
                                    *next_render,
                                    previous_overrides,
                                    current_overrides);
    check(apply_render_styles_to_layout(*next_render, *fixture.layout_tree),
          "new transform style can be applied");
    const DirtyRegionResult region =
        compute_animation_dirty_region(*fixture.layout_tree,
                                       previous_overrides,
                                       current_overrides,
                                       AnimationInvalidationOptions{Rect{0, 0, 240, 120}, 4, 0});
    check(region.mode == DirtyRegionMode::DirtyRects && !region.rects.empty(),
          "transform style change produces dirty rect");
    check(region.rects.front().width >= 120,
          "transform dirty rect covers old and translated bounds");
}

void class_change_with_stable_text_can_reuse_layout() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button><strong id='frame'>01</strong></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); }"
        "strong { display: block; font-size: 10px; line-height: 12px; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(pulse != nullptr && frame != nullptr, "nodes exist");
    frame->set_text_content("02");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(style_dirty_can_reuse_layout(*fixture.document,
                                       *fixture.render_tree,
                                       *next_render,
                                       *fixture.layout_tree,
                                       fixed_text_measure()),
          "paint-only class change plus stable text can reuse layout");
}

void class_change_with_wider_text_stays_conservative() {
    auto fixture = build_fixture(
        "<body><button id='pulse' class='pill'>Open</button><strong id='frame'>99</strong></body>",
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); }"
        "strong { display: block; font-size: 10px; line-height: 12px; }");
    Node* pulse = find_element_by_id(*fixture.document, "pulse");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(pulse != nullptr && frame != nullptr, "nodes exist");
    frame->set_text_content("100");
    pulse->set_attribute("class", "pill active");
    auto next_render = rebuild_render_tree(*fixture.document, fixture.resolver);

    check(!style_dirty_can_reuse_layout(*fixture.document,
                                        *fixture.render_tree,
                                        *next_render,
                                        *fixture.layout_tree,
                                        fixed_text_measure()),
          "paint-only class change with wider text does not reuse layout");
}

} // namespace

int main() {
    try {
        paint_only_class_change_can_reuse_layout();
        layout_class_change_stays_conservative();
        transform_class_change_invalidates_old_and_new_bounds();
        class_change_with_stable_text_can_reuse_layout();
        class_change_with_wider_text_stays_conservative();
    } catch (const std::exception& error) {
        std::cerr << "style repaint test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "style repaint tests passed\n";
    return 0;
}
