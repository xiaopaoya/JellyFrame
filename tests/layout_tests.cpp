#include "core/arena.h"
#include "core/css_parser.h"
#include "core/html_parser.h"
#include "core/layout.h"
#include "core/render_tree.h"

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

const LayoutBox* find_first_by_tag(const LayoutBox& box, const std::string& tag_name) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->tag_name == tag_name) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_by_tag(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

const LayoutBox* find_first_by_id(const LayoutBox& box, const std::string& id) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->attribute("id") == id) {
        return &box;
    }
    for (const auto& child : box.children) {
        const LayoutBox* found = find_first_by_id(*child, id);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

void layout_tree_can_use_monotonic_arena() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>Hello</p><p>World</p></main></body>");
    StyleResolver resolver(css_parser.parse("main { padding: 4px; } p { margin: 0; font-size: 16px; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    MonotonicArena render_arena(256);
    auto render_tree = render_tree_builder.build(*document, render_arena);

    LayoutEngine layout_engine(resolver);
    MonotonicArena layout_arena(256);
    auto layout_tree = layout_engine.layout(*render_tree, 240, layout_arena);

    check(layout_tree != nullptr, "arena layout root exists");
    check(layout_arena.used_bytes() > 0, "arena layout consumes arena storage");
    check(find_first_by_tag(*layout_tree, "main") != nullptr, "arena layout contains main");
    check(find_first_by_tag(*layout_tree, "p") != nullptr, "arena layout contains paragraph");
    check(layout_tree->rect.height > 0, "arena layout computes geometry");
}

void layout_tree_respects_box_budget() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);

    LayoutEngine layout_engine(resolver, {}, LayoutEngineOptions{4});
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(count_layout_boxes(*layout_tree) == 4, "layout tree is capped by box budget");
}

void flex_row_distributes_grow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='a'></div><div id='b'></div><div id='fixed'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 300px; gap: 10px; }"
        "div { min-width: 0; height: 10px; }"
        "#a { flex: 1; }"
        "#b { flex: 2; }"
        "#fixed { width: 60px; flex-shrink: 0; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 320);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* fixed = find_first_by_id(*layout_tree, "fixed");
    check(a != nullptr && b != nullptr && fixed != nullptr, "flex fixture boxes exist");
    check(fixed->rect.width == 60, "fixed flex item keeps width");
    check(a->rect.width >= 70 && a->rect.width <= 75, "flex:1 gets one grow share");
    check(b->rect.width >= 145 && b->rect.width <= 150, "flex:2 gets two grow shares");
    check(b->rect.x >= a->rect.x + a->rect.width + 10, "flex gap preserved");
}

void flex_row_shrinks_basis_widths() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 180px; }"
        "div { min-width: 0; height: 10px; flex: 0 1 120px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "shrink flex fixture boxes exist");
    check(a->rect.width == 90 && b->rect.width == 90, "flex basis widths shrink evenly");
}

void positioned_layout_offsets_without_flow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='normal'></div><div id='badge'></div><div id='after'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { position: relative; width: 200px; padding: 10px; }"
        "div { height: 20px; }"
        "#normal { width: 40px; }"
        "#badge { position: absolute; top: 5px; right: 6px; width: 30px; height: 10px; }"
        "#after { width: 50px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* main = find_first_by_tag(*layout_tree, "main");
    const LayoutBox* normal = find_first_by_id(*layout_tree, "normal");
    const LayoutBox* badge = find_first_by_id(*layout_tree, "badge");
    const LayoutBox* after = find_first_by_id(*layout_tree, "after");
    check(main != nullptr && normal != nullptr && badge != nullptr && after != nullptr,
          "positioned fixture boxes exist");
    check(after->rect.y == normal->rect.y + normal->rect.height,
          "absolute child does not consume block flow height");
    check(badge->rect.x == main->rect.x + main->style.border_width.left + main->style.padding.left +
              200 - 6 - badge->rect.width,
          "absolute right offset uses parent content box");
    check(badge->rect.y == main->rect.y + main->style.border_width.top + main->style.padding.top + 5,
          "absolute top offset uses parent content box");
}

void relative_layout_offsets_visual_box_only() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { width: 120px; }"
        "div { height: 10px; width: 20px; }"
        "#a { position: relative; left: 7px; top: 3px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "relative fixture boxes exist");
    check(a->rect.x == 7 && a->rect.y == 3, "relative box is visually offset");
    check(b->rect.y == 10, "relative offset does not change following flow position");
}

} // namespace

int main() {
    try {
        layout_tree_can_use_monotonic_arena();
        layout_tree_respects_box_budget();
        flex_row_distributes_grow_space();
        flex_row_shrinks_basis_widths();
        positioned_layout_offsets_without_flow_space();
        relative_layout_offsets_visual_box_only();
    } catch (const std::exception& error) {
        std::cerr << "layout test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layout tests passed\n";
    return 0;
}
