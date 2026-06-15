#include "core/css_parser.h"
#include "core/dirty_region.h"
#include "core/dom.h"
#include "core/form_control.h"
#include "core/html_parser.h"
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

struct LayoutFixture {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    StyleResolver resolver;
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;

    LayoutFixture(std::unique_ptr<Node> document_in,
                  Stylesheet stylesheet_in,
                  StyleResolver resolver_in,
                  RenderObjectPtr render_tree_in,
                  LayoutBoxPtr layout_tree_in)
        : document(std::move(document_in)),
          stylesheet(std::move(stylesheet_in)),
          resolver(std::move(resolver_in)),
          render_tree(std::move(render_tree_in)),
          layout_tree(std::move(layout_tree_in)) {}
};

LayoutFixture build_layout(std::unique_ptr<Node> document, const char* css, int viewport_width) {
    CssParser css_parser;
    Stylesheet stylesheet = css_parser.parse(css);
    StyleResolver resolver(stylesheet);
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*render_tree, viewport_width);
    return LayoutFixture(std::move(document), std::move(stylesheet), std::move(resolver),
                         std::move(render_tree), std::move(layout_tree));
}

Node* first_text(Node& node) {
    if (node.type == NodeType::Text) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = first_text(*child);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Node* first_element(Node& node, const char* tag) {
    if (node.type == NodeType::Element && node.tag_name == tag) {
        return &node;
    }
    for (const auto& child : node.children) {
        Node* found = first_element(*child, tag);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void text_dirty_generates_local_rect() {
    HtmlParser html_parser;
    auto initial = build_layout(
        html_parser.parse("<body><p id='a'>Alpha</p><p id='b'>Beta</p></body>"),
        "p { width: 120px; margin: 0; font-size: 16px; }",
        240);
    clear_dirty_flags(*initial.document);
    Node* text = first_text(*initial.document);
    check(text != nullptr, "text node exists");
    text->set_text("Alpha changed");

    auto previous_layout = std::move(initial.layout_tree);
    auto next = build_layout(std::move(initial.document),
                             "p { width: 120px; margin: 0; font-size: 16px; }",
                             240);
    const std::vector<Rect> rects =
        compute_dirty_rects(*next.document,
                            previous_layout.get(),
                            next.layout_tree.get(),
                            DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(!rects.empty(), "text dirty produces rect");
    check(rects.front().width < 240 || rects.front().height < 200, "text dirty is not full viewport");
}

void tree_dirty_falls_back_to_full_viewport() {
    HtmlParser html_parser;
    auto initial = build_layout(html_parser.parse("<body><main><p>Alpha</p></main></body>"), "", 240);
    clear_dirty_flags(*initial.document);
    Node* main = first_element(*initial.document, "main");
    check(main != nullptr, "main exists");
    main->append_child(make_element("section"));

    auto previous_layout = std::move(initial.layout_tree);
    auto next = build_layout(std::move(initial.document), "", 240);
    const std::vector<Rect> rects =
        compute_dirty_rects(*next.document,
                            previous_layout.get(),
                            next.layout_tree.get(),
                            DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(rects.size() == 1, "tree dirty produces one conservative rect");
    check(rects.front().x == 0 && rects.front().y == 0 &&
              rects.front().width == 240 && rects.front().height == 200,
          "tree dirty falls back to full viewport");
}

void paint_dirty_reuses_layout_and_generates_local_rect() {
    HtmlParser html_parser;
    auto fixture = build_layout(
        html_parser.parse("<body><input id='name' value='A'><p>Stable</p></body>"),
        "input { width: 120px; height: 24px; margin: 0; } p { margin: 0; }",
        240);
    clear_dirty_flags(*fixture.document);
    Node* input = first_element(*fixture.document, "input");
    check(input != nullptr, "input exists");
    check(append_text_to_control(*input, "B"), "control value changes");
    check((subtree_dirty_flags(*fixture.document) & DomDirtyPaint) != 0U, "control change is paint dirty");
    check(!dirty_requires_render_or_layout(subtree_dirty_flags(*fixture.document)),
          "paint dirty does not require render/layout");

    const std::vector<Rect> rects =
        compute_dirty_rects(*fixture.document,
                            fixture.layout_tree.get(),
                            fixture.layout_tree.get(),
                            DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(!rects.empty(), "paint dirty produces rect");
    check(rects.front().width < 240 || rects.front().height < 200, "paint dirty is not full viewport");
}

} // namespace

int main() {
    try {
        text_dirty_generates_local_rect();
        tree_dirty_falls_back_to_full_viewport();
        paint_dirty_reuses_layout_and_generates_local_rect();
    } catch (const std::exception& error) {
        std::cerr << "dirty region test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "dirty region tests passed\n";
    return 0;
}
