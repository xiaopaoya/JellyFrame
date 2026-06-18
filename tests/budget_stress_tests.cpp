#include "core/budget.h"
#include "core/css_parser.h"
#include "core/dirty_region.h"
#include "core/dom.h"
#include "core/frame_update.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"

#include <iostream>
#include <sstream>
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

std::size_t count_render_objects_for_test(const RenderObject& object) {
    std::size_t count = 0;
    std::vector<const RenderObject*> pending;
    pending.push_back(&object);
    while (!pending.empty()) {
        const RenderObject* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

std::size_t count_layout_boxes_for_test(const LayoutBox& box) {
    std::size_t count = 0;
    std::vector<const LayoutBox*> pending;
    pending.push_back(&box);
    while (!pending.empty()) {
        const LayoutBox* current = pending.back();
        pending.pop_back();
        ++count;
        for (const auto& child : current->children) {
            pending.push_back(child.get());
        }
    }
    return count;
}

std::string repeated_cards_html(int count) {
    std::ostringstream html;
    html << "<body><main id='app'>";
    for (int i = 0; i < count; ++i) {
        html << "<section class='card' data-i='" << i << "' data-extra='x'>"
             << "<h2>Title " << i << "</h2><p>Body " << i << "</p></section>";
    }
    html << "</main></body>";
    return html.str();
}

std::string repeated_rules_css(int count) {
    std::ostringstream css;
    for (int i = 0; i < count; ++i) {
        css << ".card.rule" << i << " { color: #111827; background: #ffffff; "
            << "padding: 4px; margin: 2px; border: 1px solid #000000; }\n";
    }
    return css.str();
}

HostBudgets tiny_budgets() {
    HostBudgets budgets;
    budgets.max_dom_nodes = 18;
    budgets.max_dom_depth = 8;
    budgets.max_attributes_per_element = 1;
    budgets.max_css_rules = 3;
    budgets.max_css_declarations_per_rule = 2;
    budgets.max_render_objects = 10;
    budgets.max_layout_boxes = 9;
    budgets.max_layers = 3;
    budgets.max_display_commands = 6;
    budgets.max_dirty_rects = 1;
    budgets.max_framebuffer_pixels = 100 * 100;
    return budgets;
}

void parser_budget_diagnostics_are_explicit_and_bounded() {
    HtmlParser parser;
    HostBudgets budgets = tiny_budgets();
    budgets.max_dom_nodes = 6;
    budgets.max_dom_depth = 4;
    budgets.max_attributes_per_element = 1;

    const HtmlParseResult result = parser.parse_with_diagnostics(
        "<body><main><section data-a='1' data-b='2'><div><p><span>Deep</span></p></div></section>"
        "<article>Too much</article><article>Still too much</article></main></body>",
        html_parser_options_from_budgets(budgets));

    check(result.document != nullptr, "parser returns a document under tight budgets");
    check((result.diagnostics & HtmlParserDiagnosticNodeLimit) != 0U,
          "parser reports node budget pressure");
    check((result.diagnostics & HtmlParserDiagnosticDepthLimit) != 0U,
          "parser reports depth budget pressure");
    check((result.diagnostics & HtmlParserDiagnosticAttributeLimit) != 0U,
          "parser reports attribute budget pressure");

    const DomStatistics statistics = compute_dom_statistics(*result.document);
    check(statistics.node_count <= budgets.max_dom_nodes + 3,
          "synthesized document structure remains bounded around node budget");
    check(statistics.max_attributes_per_element <= budgets.max_attributes_per_element,
          "attribute budget is enforced per element");
}

void css_budget_limits_rules_and_declarations() {
    CssParser parser;
    const HostBudgets budgets = tiny_budgets();
    const Stylesheet stylesheet =
        parser.parse(repeated_rules_css(12), css_parser_options_from_budgets(budgets));

    check(stylesheet.size() <= budgets.max_css_rules, "css rule budget is enforced");
    for (const CssRule& rule : stylesheet) {
        check(rule.declarations.size() <= budgets.max_css_declarations_per_rule,
              "css declaration budget is enforced");
    }
}

void full_pipeline_stays_bounded_with_tiny_budgets() {
    const HostBudgets budgets = tiny_budgets();
    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(repeated_cards_html(32), html_parser_options_from_budgets(budgets));
    Stylesheet stylesheet = css_parser.parse(
        ".card { display: block; margin: 2px; padding: 4px; background: #ffffff; }"
        "h2 { font-size: 16px; } p { color: #111827; }",
        css_parser_options_from_budgets(budgets));
    StyleResolver resolver(std::move(stylesheet));

    RenderTreeBuilder render_builder(resolver, render_tree_options_from_budgets(budgets));
    auto render_tree = render_builder.build(*document);
    check(render_tree != nullptr, "render tree root exists under tiny budgets");
    check(count_render_objects_for_test(*render_tree) <= budgets.max_render_objects,
          "render tree stays within object budget");

    LayoutEngine layout_engine(resolver, {}, layout_engine_options_from_budgets(budgets));
    auto layout_tree = layout_engine.layout(*render_tree, 120);
    check(layout_tree != nullptr, "layout tree root exists under tiny budgets");
    check(count_layout_boxes_for_test(*layout_tree) <= budgets.max_layout_boxes,
          "layout tree stays within box budget");

    LayerTreeBuilder layer_builder(layer_tree_options_from_budgets(budgets));
    auto layer_tree = layer_builder.build(*layout_tree);
    DisplayList display_list = layer_builder.flatten(*layer_tree);
    check(count_layers(*layer_tree) <= budgets.max_layers, "layer tree stays within layer budget");
    check(display_list.size() <= budgets.max_display_commands,
          "flattened display list stays within command budget");
}

void dirty_and_framebuffer_budget_helpers_are_conservative() {
    const HostBudgets budgets = tiny_budgets();
    const Rect viewport{0, 0, 50, 50};
    const DirtyRegionOptions dirty_options = dirty_region_options_from_budgets(budgets, viewport);
    check(dirty_options.max_rects == 1, "dirty rect budget maps into options");
    check(framebuffer_size_fits_budget(100, 100, budgets), "framebuffer at budget is accepted");
    check(!framebuffer_size_fits_budget(101, 100, budgets), "framebuffer over budget is rejected");
    check(!framebuffer_size_fits_budget(0, 100, budgets), "invalid framebuffer size is rejected");

    FramePipelineCacheState cache;
    cache.has_render_tree = true;
    cache.has_layout_tree = true;
    cache.has_layer_tree = true;
    cache.has_framebuffer = true;
    cache.framebuffer_width = 50;
    cache.framebuffer_height = 50;
    cache.viewport = viewport;
    cache.content_height = 50;
    const FrameUpdatePlan plan = plan_frame_update(make_frame_update_state(DomDirtyPaint, cache));
    check(plan.action == FrameUpdateAction::RepaintExisting,
          "paint-only dirty still reuses cached pipeline under small budgets");
}

} // namespace

int main() {
    try {
        parser_budget_diagnostics_are_explicit_and_bounded();
        css_budget_limits_rules_and_declarations();
        full_pipeline_stays_bounded_with_tiny_budgets();
        dirty_and_framebuffer_budget_helpers_are_conservative();
    } catch (const std::exception& error) {
        std::cerr << "budget stress test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "budget stress tests passed\n";
    return 0;
}
