#include "render_core/arena.h"
#include "render_core/css_parser.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

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

bool has_diagnostic_code(const VectorDiagnosticSink& sink, const std::string& code) {
    for (const Diagnostic& diagnostic : sink.diagnostics()) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
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

void layout_tree_reports_box_budget_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><p>A</p><p>B</p><p>C</p></main></body>");
    StyleResolver resolver(css_parser.parse(""));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.max_layout_boxes = 4;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(count_layout_boxes(*layout_tree) == 4, "layout budget still caps boxes");
    check(has_diagnostic_code(diagnostics, "layout-box-limit"), "layout budget diagnostic is reported");
}

void layout_tree_reports_depth_budget_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><section><p>Deep</p></section></main></body>");
    StyleResolver resolver(css_parser.parse("body, main, section, p { margin: 0; }"));
    RenderTreeBuilder render_tree_builder(resolver);
    auto render_tree = render_tree_builder.build(*document);
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.max_layout_depth = 2;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*render_tree, 240);

    check(layout_tree != nullptr, "depth budget layout root exists");
    check(has_diagnostic_code(diagnostics, "layout-depth-limit"), "layout depth diagnostic is reported");
    const LayoutBox* main = find_first_by_tag(*layout_tree, "main");
    check(main == nullptr || main->rect.height == 0, "layout beyond depth budget is skipped");
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

void flex_row_justifies_and_aligns_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 200px; height: 50px; justify-content: center; align-items: center; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "justify/align flex fixture boxes exist");
    check(a->rect.x == 60 && b->rect.x == 100, "center justification places row as a group");
    check(a->rect.y == 20 && b->rect.y == 20, "center alignment places items vertically");
}

void flex_cross_axis_stretch_resolves_auto_size() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='row'><div id='row-auto'></div><div id='row-fixed'></div></main>"
        "<main id='column'><div id='column-auto'></div><div id='column-fixed'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; align-items: stretch; }"
        "#row { width: 100px; height: 40px; }"
        "#row-auto { width: 20px; } #row-fixed { width: 20px; height: 10px; }"
        "#column { flex-direction: column; width: 60px; height: 40px; }"
        "#column-auto { height: 10px; } #column-fixed { width: 20px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 100);

    const LayoutBox* row_auto = find_first_by_id(*layout_tree, "row-auto");
    const LayoutBox* row_fixed = find_first_by_id(*layout_tree, "row-fixed");
    const LayoutBox* column_auto = find_first_by_id(*layout_tree, "column-auto");
    const LayoutBox* column_fixed = find_first_by_id(*layout_tree, "column-fixed");
    check(row_auto != nullptr && row_fixed != nullptr && column_auto != nullptr && column_fixed != nullptr,
          "cross-axis stretch fixture boxes exist");
    check(row_auto->rect.height == 40, "row stretch fills the line for an auto-height item");
    check(row_fixed->rect.height == 10, "row stretch preserves an explicit height");
    check(column_auto->rect.width == 60, "column stretch fills the line for an auto-width item");
    check(column_fixed->rect.width == 20, "column stretch preserves an explicit width");
}

void flex_cross_axis_alignment_uses_outer_size_and_align_self() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='center'></div><div id='end'></div><div id='self'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 120px; height: 50px; align-items: center; }"
        "div { width: 20px; height: 10px; margin-top: 4px; margin-bottom: 6px; }"
        "#end { align-self: flex-end; } #self { align-self: stretch; height: auto; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 140, 70);

    const LayoutBox* center = find_first_by_id(*layout_tree, "center");
    const LayoutBox* end = find_first_by_id(*layout_tree, "end");
    const LayoutBox* self = find_first_by_id(*layout_tree, "self");
    check(center != nullptr && end != nullptr && self != nullptr,
          "cross-axis alignment fixture boxes exist");
    check(center->rect.y == 19, "center aligns the item's outer margin box");
    check(end->rect.y == 34, "end aligns the item's outer margin box");
    check(self->rect.y == 4 && self->rect.height == 40,
          "align-self stretch fills the line while preserving margins");
}

void flex_wrap_aligns_items_within_each_line() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='short'></div><div id='tall'></div><div id='next'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; width: 90px; align-items: center; column-gap: 5px; row-gap: 7px; }"
        "div { width: 40px; }"
        "#short { height: 10px; } #tall { height: 20px; } #next { width: 90px; height: 8px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 110, 80);

    const LayoutBox* short_item = find_first_by_id(*layout_tree, "short");
    const LayoutBox* tall = find_first_by_id(*layout_tree, "tall");
    const LayoutBox* next = find_first_by_id(*layout_tree, "next");
    check(short_item != nullptr && tall != nullptr && next != nullptr,
          "wrapped cross-axis alignment fixture boxes exist");
    check(short_item->rect.y == 5 && tall->rect.y == 0,
          "wrapped items align within the first line's cross size");
    check(next->rect.y == 27, "wrapped next line starts after line height and row gap");
}

void flex_row_supports_end_and_space_evenly_justification() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='end'><div id='end-a'></div><div id='end-b'></div></main>"
        "<main id='even'><div id='even-a'></div><div id='even-b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 200px; height: 20px; }"
        "#end { justify-content: flex-end; }"
        "#even { justify-content: space-evenly; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* end_a = find_first_by_id(*layout_tree, "end-a");
    const LayoutBox* end_b = find_first_by_id(*layout_tree, "end-b");
    const LayoutBox* even_a = find_first_by_id(*layout_tree, "even-a");
    const LayoutBox* even_b = find_first_by_id(*layout_tree, "even-b");
    check(end_a != nullptr && end_b != nullptr && even_a != nullptr && even_b != nullptr,
          "extended justify-content fixture boxes exist");
    check(end_a->rect.x == 120 && end_b->rect.x == 160,
          "flex-end places the row against the content end");
    check(even_a->rect.x == 40 && even_b->rect.x == 120,
          "space-evenly distributes equal leading, internal and trailing space");
}

void flex_wrap_stacks_lines_with_row_gap() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; width: 90px; column-gap: 5px; row-gap: 7px; }"
        "div { width: 50px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "wrapped flex fixture boxes exist");
    check(a->rect.x == 0 && a->rect.y == 0, "first wrapped flex item starts first line");
    check(b->rect.x == 0 && b->rect.y == 17, "second wrapped flex item moves to next row with row-gap");
}

void flex_wrap_align_content_distributes_lines() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-wrap: wrap; align-content: space-between; width: 90px; height: 40px; row-gap: 7px; }"
        "div { width: 90px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 100);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "align-content fixture boxes exist");
    check(a->rect.y == 0 && b->rect.y == 30,
          "align-content space-between distributes wrapped rows inside fixed height");
}

void flex_column_distributes_vertical_space_and_aligns_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><div id='a'></div><div id='b'></div><div id='c'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-direction: column; width: 200px; height: 100px; "
        "justify-content: space-between; align-items: center; }"
        "#c { align-self: flex-end; }"
        "div { width: 40px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* c = find_first_by_id(*layout_tree, "c");
    check(a != nullptr && b != nullptr && c != nullptr, "column flex fixture boxes exist");
    check(a->rect.x == 80 && b->rect.x == 80,
          "center alignment places column items on the cross axis");
    check(c->rect.x == 160, "align-self overrides the parent cross-axis alignment");
    check(a->rect.y == 0 && b->rect.y == 45 && c->rect.y == 90,
          "space-between distributes column items on the main axis");
}

void flex_column_distributes_grow_space() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; flex-direction: column; width: 100px; height: 100px; }"
        "div { flex: 1; min-height: 0; }"
        "#b { flex-grow: 3; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    check(a != nullptr && b != nullptr, "growing column flex fixture boxes exist");
    check(a->rect.height == 25 && b->rect.height == 75, "column grow factors divide available height");
}

void flex_order_reorders_in_flow_layout_without_touching_default_path() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='a'></div><div id='b'></div><div id='c'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: flex; width: 90px; }"
        "div { width: 20px; height: 10px; }"
        "#a { order: 2; } #b { order: -1; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 100);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* c = find_first_by_id(*layout_tree, "c");
    check(a != nullptr && b != nullptr && c != nullptr, "flex order fixture boxes exist");
    check(b->rect.x == 0 && c->rect.x == 20 && a->rect.x == 40,
          "nonzero order uses stable ascending flex item placement");
}

void flex_column_resolves_percent_height_against_containing_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='fill'></div><div id='footer'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; height: 120px; }"
        "main { display: flex; flex-direction: column; width: 100px; height: 100%; }"
        "#fill { flex: 1; }"
        "#footer { height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 120);

    const LayoutBox* fill = find_first_by_id(*layout_tree, "fill");
    const LayoutBox* footer = find_first_by_id(*layout_tree, "footer");
    check(fill != nullptr && footer != nullptr, "percent-height column flex fixture boxes exist");
    check(fill->rect.height == 110 && footer->rect.y == 110,
          "column flex resolves percentage container height before grow distribution");
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

void logical_inset_layout_uses_ltr_physical_offsets() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main><div id='badge'></div></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { position: relative; width: 100px; height: 50px; }"
        "#badge { position: absolute; inset: 6px 8px auto 4px; height: 10px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 120, 80);

    const LayoutBox* badge = find_first_by_id(*layout_tree, "badge");
    check(badge != nullptr, "logical inset fixture exists");
    check(badge->rect.x == 4 && badge->rect.y == 6 && badge->rect.width == 88,
          "logical inset expands to the same LTR absolute-position geometry as physical offsets");
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

void border_box_sizing_keeps_declared_width_and_height() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='panel'><span>Text</span></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#panel { box-sizing: border-box; width: 144px; height: 144px; padding: 8px; border: 2px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 200);

    const LayoutBox* panel = find_first_by_id(*layout_tree, "panel");
    check(panel != nullptr, "border-box fixture exists");
    check(panel->rect.width == 144, "border-box width keeps declared border box");
    check(panel->rect.height == 144, "border-box height keeps declared border box");
}

void percentage_width_and_height_use_containing_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='card'></section><aside id='cap'></aside></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; width: 100%; height: 100%; }"
        "#screen { width: 100%; height: 100%; }"
        "#card { width: 50%; height: 25%; min-width: 80px; }"
        "#cap { width: 100%; height: 100%; max-height: 120px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 320, 240);

    const LayoutBox* body = find_first_by_tag(*layout_tree, "body");
    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    const LayoutBox* cap = find_first_by_id(*layout_tree, "cap");
    check(body != nullptr && screen != nullptr && card != nullptr && cap != nullptr,
          "percentage fixture boxes exist");
    check(body->rect.width == 320 && body->rect.height == 240, "body percentages resolve to viewport");
    check(screen->rect.width == 320 && screen->rect.height == 240, "child percentages resolve to parent");
    check(card->rect.width == 160 && card->rect.height == 60, "nested percentages resolve cheaply");
    check(cap->rect.width == 320 && cap->rect.height == 120, "max-height clamps percentage height");
}

void responsive_layout_matrix_keeps_same_app_inside_three_targets() {
    struct Target {
        int width;
        int height;
        bool narrow;
    };
    constexpr Target targets[] = {
        {300, 300, false},
        {320, 240, false},
        {172, 320, true},
    };

    for (const Target target : targets) {
        HtmlParser html_parser;
        CssParser css_parser;
        auto document = html_parser.parse(
            "<body><main id='screen'><section id='primary'><p id='primary-label'>Primary content</p></section>"
            "<section id='secondary'><p id='secondary-label'>Secondary content</p></section></main></body>");
        CssParserOptions css_options;
        css_options.media_viewport_width = target.width;
        css_options.media_viewport_height = target.height;
        VectorDiagnosticSink css_diagnostics;
        css_options.diagnostics = &css_diagnostics;
        StyleResolver resolver(css_parser.parse(
            "body { margin: 0; width: 100%; height: 100%; }"
            "#screen { display: flex; flex-direction: row; box-sizing: border-box; width: 100%; "
            "height: 100%; gap: 8px; padding: 8px; }"
            "section { flex: 1 1 0; min-width: 0; height: auto; box-sizing: border-box; }"
            "p { margin: 0; width: 100%; overflow-wrap: anywhere; }"
            "@media (max-width: 200px) { #screen { flex-direction: column; gap: 4px; } "
            "section { width: 100%; flex: 0 0 40px; } }",
            css_options));
        RenderTreeBuilder render_tree_builder(resolver);
        auto render_tree = render_tree_builder.build(*document);
        VectorDiagnosticSink layout_diagnostics;
        LayoutEngineOptions layout_options;
        layout_options.diagnostics = &layout_diagnostics;
        LayoutEngine layout_engine(resolver, {}, layout_options);
        auto layout_tree = layout_engine.layout(*render_tree, target.width, target.height);

        const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
        const LayoutBox* primary = find_first_by_id(*layout_tree, "primary");
        const LayoutBox* secondary = find_first_by_id(*layout_tree, "secondary");
        check(screen != nullptr && primary != nullptr && secondary != nullptr,
              "responsive matrix boxes exist");
        check(screen->rect.width == target.width && screen->rect.height == target.height,
              "responsive matrix root fills every target viewport");
        const int content_left = screen->rect.x + screen->style.padding.left + screen->style.border_width.left;
        const int content_right = screen->rect.x + screen->rect.width -
            screen->style.padding.right - screen->style.border_width.right;
        check(primary->rect.x >= content_left && secondary->rect.x >= content_left &&
                  primary->rect.x + primary->rect.width <= content_right &&
                  secondary->rect.x + secondary->rect.width <= content_right,
              "responsive matrix children stay inside the padded viewport");
        if (target.narrow) {
            check(screen->style.flex_direction == FlexDirection::Column,
                  "narrow target selects the column media branch");
            check(primary->rect.width == secondary->rect.width &&
                      primary->rect.y < secondary->rect.y &&
                      secondary->rect.y >= primary->rect.y + primary->rect.height + 4,
                  "narrow target stacks full-width cards with the declared gap");
        } else {
            check(screen->style.flex_direction == FlexDirection::Row,
                  "wide targets keep the row media branch");
            check(primary->rect.y == secondary->rect.y &&
                      primary->rect.x < secondary->rect.x &&
                      primary->rect.width == secondary->rect.width &&
                      secondary->rect.x >= primary->rect.x + primary->rect.width + 8,
                  "wide targets distribute equal flex cards with the declared gap");
        }
        check(target.narrow
                  ? !has_diagnostic_code(css_diagnostics, "css-media-query-not-matched")
                  : has_diagnostic_code(css_diagnostics, "css-media-query-not-matched"),
              "responsive matrix reports only the non-selected media branch as unmatched");
        check(!has_diagnostic_code(layout_diagnostics, "visual-horizontal-overflow") &&
                  !has_diagnostic_code(layout_diagnostics, "visual-vertical-paint-overflow"),
              "responsive matrix has no viewport overflow diagnostic");
    }
}

void border_box_percent_width_accounts_for_edges() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='screen'><section id='card'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { width: 200px; }"
        "#card { box-sizing: border-box; width: 100%; height: 20px; padding: 10px; border: 2px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 240, 160);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    check(screen != nullptr && card != nullptr, "border-box percent fixture boxes exist");
    check(card->rect.width == 200, "border-box percent width keeps parent-sized border box");
    check(card->rect.x + card->rect.width <= screen->rect.x + screen->rect.width,
          "border-box percent child does not overflow parent horizontally");
}

void max_width_percent_clamps_nested_border_box() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='card'><div id='inner'></div></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { width: 172px; }"
        "#card { box-sizing: border-box; width: 300px; max-width: 100%; padding: 6px; border: 1px solid #000; }"
        "#inner { box-sizing: border-box; width: 100%; height: 18px; padding: 4px; border: 1px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 320);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* card = find_first_by_id(*layout_tree, "card");
    const LayoutBox* inner = find_first_by_id(*layout_tree, "inner");
    check(screen != nullptr && card != nullptr && inner != nullptr,
          "max-width percent fixture boxes exist");
    check(card->rect.width == 172, "max-width:100% clamps declared border-box width");
    check(card->rect.x + card->rect.width <= screen->rect.x + screen->rect.width,
          "clamped card stays inside narrow parent");
    check(inner->rect.x + inner->rect.width <= card->rect.x + card->rect.width - card->style.border_width.right,
          "nested border-box percent item stays inside clamped card");
}

void max_width_percent_clamps_nested_flex_grid_items() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main id='screen'><section id='grid'><article id='left'><div id='left-inner'></div></article>"
        "<article id='right'><div id='right-inner'></div></article></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#screen { display: flex; box-sizing: border-box; width: 172px; padding: 6px; border: 1px solid #000; }"
        "#grid { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; flex: 1; min-width: 0; }"
        "article { box-sizing: border-box; width: 300px; max-width: 100%; padding: 5px; border: 1px solid #000; }"
        "article div { box-sizing: border-box; width: 100%; height: 18px; padding: 3px; border: 1px solid #000; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 172, 320);

    const LayoutBox* screen = find_first_by_id(*layout_tree, "screen");
    const LayoutBox* grid = find_first_by_id(*layout_tree, "grid");
    const LayoutBox* left = find_first_by_id(*layout_tree, "left");
    const LayoutBox* right = find_first_by_id(*layout_tree, "right");
    const LayoutBox* left_inner = find_first_by_id(*layout_tree, "left-inner");
    const LayoutBox* right_inner = find_first_by_id(*layout_tree, "right-inner");
    check(screen != nullptr && grid != nullptr && left != nullptr && right != nullptr &&
              left_inner != nullptr && right_inner != nullptr,
          "nested flex grid fixture boxes exist");
    const int screen_content_right = screen->rect.x + screen->rect.width -
        screen->style.border_width.right - screen->style.padding.right;
    check(grid->rect.x + grid->rect.width <= screen_content_right,
          "flex grid fits its border-box parent on a narrow target");
    check(left->rect.x + left->rect.width <= grid->rect.x + grid->rect.width &&
              right->rect.x + right->rect.width <= grid->rect.x + grid->rect.width,
          "max-width clamps each grid card inside the flex item");
    check(left_inner->rect.x + left_inner->rect.width <= left->rect.x + left->rect.width &&
              right_inner->rect.x + right_inner->rect.width <= right->rect.x + right->rect.width,
          "nested percent children stay inside clamped grid cards");
}

void grid_places_fixed_columns_and_spans() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><section id='a'></section><section id='b'></section><section id='wide'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: grid; grid-template-columns: 40px 1fr; width: 120px; gap: 5px; }"
        "section { height: 10px; }"
        "#wide { grid-column: span 2; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160);

    const LayoutBox* a = find_first_by_id(*layout_tree, "a");
    const LayoutBox* b = find_first_by_id(*layout_tree, "b");
    const LayoutBox* wide = find_first_by_id(*layout_tree, "wide");
    check(a != nullptr && b != nullptr && wide != nullptr, "grid fixture boxes exist");
    check(a->rect.x == 0 && a->rect.width == 40, "fixed grid column width applies");
    check(b->rect.x == 45 && b->rect.width == 75, "flexible grid column fills remaining width");
    check(wide->rect.x == 0 && wide->rect.y == 15 && wide->rect.width == 120,
          "span grid item moves to next row and covers both columns");
}

void grid_places_explicit_rows_and_numeric_lines() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(
        "<body><main><section id='top'></section><section id='side'></section>"
        "<section id='bottom'></section></main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "main { display: grid; width: 120px; height: 90px; grid-template-columns: 40px 1fr;"
        " grid-template-rows: 20px 1fr; gap: 5px; }"
        "section { box-sizing: border-box; }"
        "#top { grid-column: 1 / 3; grid-row: 1; }"
        "#side { grid-column: 1; grid-row: 2; }"
        "#bottom { grid-column: 2; grid-row: 2 / span 1; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*document, 160, 120);

    const LayoutBox* top = find_first_by_id(*layout_tree, "top");
    const LayoutBox* side = find_first_by_id(*layout_tree, "side");
    const LayoutBox* bottom = find_first_by_id(*layout_tree, "bottom");
    check(top != nullptr && side != nullptr && bottom != nullptr, "explicit grid fixture boxes exist");
    check(top->rect.x == 0 && top->rect.y == 0 && top->rect.width == 120,
          "numeric grid end line spans both columns");
    check(side->rect.x == 0 && side->rect.y == 25 && side->rect.width == 40,
          "numeric first column and second row resolve");
    check(bottom->rect.x == 45 && bottom->rect.y == 25 && bottom->rect.width == 75,
          "numeric start plus span resolves remaining cell");
}

void grid_row_budget_uses_non_overlapping_block_fallback() {
    std::string html = "<body><main id='grid'>";
    for (int index = 0; index <= 128; ++index) {
        html += "<section id='cell-" + std::to_string(index) + "'>" + std::to_string(index) + "</section>";
    }
    html += "</main></body>";

    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html);
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; } main { display: grid; width: 80px; row-gap: 1px; }"
        "section { height: 4px; box-sizing: border-box; }"));
    VectorDiagnosticSink diagnostics;
    LayoutEngineOptions options;
    options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, {}, options);
    auto layout_tree = layout_engine.layout(*document, 80);

    const LayoutBox* penultimate = find_first_by_id(*layout_tree, "cell-127");
    const LayoutBox* fallback = find_first_by_id(*layout_tree, "cell-128");
    check(penultimate != nullptr && fallback != nullptr, "bounded grid fixture boxes exist");
    check(fallback->rect.y >= penultimate->rect.y + penultimate->rect.height,
          "row-budget fallback does not overlap the final tracked grid item");
    check(fallback->rect.width <= 80, "row-budget fallback remains within the grid content width");
    check(has_diagnostic_code(diagnostics, "grid-placement-budget"), "row-budget fallback is diagnosed");
}

void nowrap_text_overflow_reports_diagnostic() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><p id='status'>SuperLongStatusLabelWithoutBreaks</p></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#status { width: 40px; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }"));
    VectorDiagnosticSink diagnostics;
    LayoutEngine layout_engine(resolver, {}, LayoutEngineOptions{4096, &diagnostics});
    auto layout_tree = layout_engine.layout(*document, 160, 120);
    (void) layout_tree;

    bool found = false;
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.code == "layout-text-overflow" ||
            diagnostic.code == "layout-text-overflow-ellipsis") {
            found = true;
            check(diagnostic.detail.find("text=\"SuperLongStatusLabelWithoutBreaks\"") != std::string::npos,
                  "overflow diagnostic includes text snippet");
            check(diagnostic.detail.find("measuredWidth=") != std::string::npos,
                  "overflow diagnostic includes measured width");
            check(diagnostic.detail.find("availableWidth=") != std::string::npos,
                  "overflow diagnostic includes available width");
            check(diagnostic.detail.find("node=\"p#status\"") != std::string::npos,
                  "overflow diagnostic includes node label");
            check(diagnostic.detail.find("path=\"") != std::string::npos &&
                  diagnostic.detail.find("p#status") != std::string::npos,
                  "overflow diagnostic includes stable dom path");
            break;
        }
    }
    check(found, "nowrap overflow emits layout diagnostic");
}

void layout_saturates_extreme_box_values() {
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse("<body><main id='extreme'>Content</main></body>");
    StyleResolver resolver(css_parser.parse(
        "body { margin: 0; }"
        "#extreme { width: 2147483647px; padding: 2147483647px; "
        "border-width: 2147483647px; margin: 2147483647px; }"));
    LayoutEngine layout_engine(resolver);
    auto layout_tree = layout_engine.layout(*RenderTreeBuilder(resolver).build(*document), 240, 240);
    const LayoutBox* extreme = find_first_by_id(*layout_tree, "extreme");
    check(extreme != nullptr, "extreme layout fixture exists");
    check(extreme->rect.width >= 0 && extreme->rect.height >= 0,
          "extreme box arithmetic does not wrap dimensions negative");
}

} // namespace

int main() {
    try {
        layout_tree_can_use_monotonic_arena();
        layout_tree_respects_box_budget();
        layout_tree_reports_box_budget_diagnostic();
        layout_saturates_extreme_box_values();
        layout_tree_reports_depth_budget_diagnostic();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        flex_row_distributes_grow_space();
        flex_row_shrinks_basis_widths();
        flex_row_justifies_and_aligns_items();
        flex_cross_axis_stretch_resolves_auto_size();
        flex_cross_axis_alignment_uses_outer_size_and_align_self();
        flex_wrap_aligns_items_within_each_line();
        flex_row_supports_end_and_space_evenly_justification();
        flex_wrap_stacks_lines_with_row_gap();
        flex_wrap_align_content_distributes_lines();
        flex_column_distributes_vertical_space_and_aligns_items();
        flex_column_distributes_grow_space();
        flex_order_reorders_in_flow_layout_without_touching_default_path();
        flex_column_resolves_percent_height_against_containing_box();
#endif
        positioned_layout_offsets_without_flow_space();
        logical_inset_layout_uses_ltr_physical_offsets();
        relative_layout_offsets_visual_box_only();
        border_box_sizing_keeps_declared_width_and_height();
        percentage_width_and_height_use_containing_box();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        responsive_layout_matrix_keeps_same_app_inside_three_targets();
#endif
        border_box_percent_width_accounts_for_edges();
        max_width_percent_clamps_nested_border_box();
#if JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED
        max_width_percent_clamps_nested_flex_grid_items();
        grid_places_fixed_columns_and_spans();
        grid_places_explicit_rows_and_numeric_lines();
        grid_row_budget_uses_non_overlapping_block_fallback();
#endif
        nowrap_text_overflow_reports_diagnostic();
    } catch (const std::exception& error) {
        std::cerr << "layout test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "layout tests passed\n";
    return 0;
}
