#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/text_repaint.h"

#include <iostream>
#include <stdexcept>
#include <string>

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
    RenderObjectPtr render_tree;
    LayoutBoxPtr layout_tree;
};

Fixture build_fixture(const std::string& html, const std::string& css) {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    StyleResolver resolver(css_parser.parse(css));
    RenderTreeBuilder render_builder(resolver);
    auto render_tree = render_builder.build(*document);
    LayoutEngine layout_engine(resolver, fixed_text_measure());
    auto layout_tree = layout_engine.layout(*render_tree, 240);
    clear_dirty_flags(*document);
    return Fixture{std::move(document), std::move(render_tree), std::move(layout_tree)};
}

void same_width_single_line_text_can_reuse_layout() {
    auto fixture = build_fixture("<body><p id='frame'>01</p></body>",
                                 "p { margin: 0; font-size: 10px; line-height: 12px; }");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(frame != nullptr, "frame element exists");
    frame->set_text_content("02");

    check(text_dirty_can_reuse_layout(*fixture.document, *fixture.layout_tree, fixed_text_measure()),
          "same width single-line text can reuse layout");
}

void changed_width_text_keeps_layout_conservative() {
    auto fixture = build_fixture("<body><p id='frame'>99</p></body>",
                                 "p { margin: 0; font-size: 10px; line-height: 12px; }");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(frame != nullptr, "frame element exists");
    frame->set_text_content("100");

    check(!text_dirty_can_reuse_layout(*fixture.document, *fixture.layout_tree, fixed_text_measure()),
          "changed text width does not reuse layout");
}

void wrapping_text_keeps_layout_conservative() {
    auto fixture = build_fixture("<body><p id='label'>AA</p></body>",
                                 "p { margin: 0; font-size: 10px; line-height: 12px; }");
    Node* label = find_element_by_id(*fixture.document, "label");
    check(label != nullptr, "label element exists");
    label->set_text_content("A B");

    check(!text_dirty_can_reuse_layout(*fixture.document, *fixture.layout_tree, fixed_text_measure()),
          "wrapping text does not reuse layout");
}

void style_dirty_keeps_layout_conservative() {
    auto fixture = build_fixture("<body><p id='frame'>01</p></body>",
                                 "p { margin: 0; font-size: 10px; line-height: 12px; }");
    Node* frame = find_element_by_id(*fixture.document, "frame");
    check(frame != nullptr, "frame element exists");
    frame->set_text_content("02");
    frame->set_attribute("class", "active");

    check(!text_dirty_can_reuse_layout(*fixture.document, *fixture.layout_tree, fixed_text_measure()),
          "style-affecting dirty flags do not reuse layout");
}

} // namespace

int main() {
    try {
        same_width_single_line_text_can_reuse_layout();
        changed_width_text_keeps_layout_conservative();
        wrapping_text_keeps_layout_conservative();
        style_dirty_keeps_layout_conservative();
    } catch (const std::exception& error) {
        std::cerr << "text repaint test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "text repaint tests passed\n";
    return 0;
}
