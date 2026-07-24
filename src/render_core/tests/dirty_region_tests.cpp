#include "render_core/css_parser.h"
#include "render_core/dirty_region.h"
#include "render_core/dom.h"
#include "render_core/form_control.h"
#include "render_core/frame_update.h"
#include "render_core/html_parser.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"

#include <iostream>
#include <limits>
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

void collect_elements(Node& node, const char* tag, std::vector<Node*>& output) {
    if (node.type == NodeType::Element && node.tag_name == tag) {
        output.push_back(&node);
    }
    for (const auto& child : node.children) {
        collect_elements(*child, tag, output);
    }
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
    const DirtyRegionResult region =
        compute_dirty_region(*next.document,
                             previous_layout.get(),
                             next.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(!rects.empty(), "text dirty produces rect");
    check(rects.front().width < 240 || rects.front().height < 200, "text dirty is not full viewport");
    check(region.mode == DirtyRegionMode::DirtyRects, "text dirty reports dirty-rect mode");
    check(region.fallback_reason == DirtyRegionFallbackReason::None, "text dirty has no fallback reason");
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
    const DirtyRegionResult region =
        compute_dirty_region(*next.document,
                             previous_layout.get(),
                             next.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(rects.size() == 1, "tree dirty produces one conservative rect");
    check(rects.front().x == 0 && rects.front().y == 0 &&
              rects.front().width == 240 && rects.front().height == 200,
          "tree dirty falls back to full viewport");
    check(region.mode == DirtyRegionMode::FullFrame, "tree dirty reports full-frame mode");
    check(region.fallback_reason == DirtyRegionFallbackReason::TreeDirty,
          "tree dirty reports fallback reason");
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

void repeated_paint_dirty_updates_remain_bounded() {
    HtmlParser html_parser;
    auto fixture = build_layout(
        html_parser.parse("<body><input id='name' value='A'><p>Stable</p></body>"),
        "input { width: 120px; height: 24px; margin: 0; } p { margin: 0; }",
        240);
    clear_dirty_flags(*fixture.document);
    Node* input = first_element(*fixture.document, "input");
    check(input != nullptr, "input exists");

    for (int iteration = 0; iteration < 128; ++iteration) {
        check(append_text_to_control(*input, "x"), "control value changes repeatedly");
        FrameUpdateState state;
        state.dirty_flags = subtree_dirty_flags(*fixture.document);
        state.has_render_tree = true;
        state.has_layout_tree = true;
        state.has_layer_tree = true;
        state.has_framebuffer = true;
        state.framebuffer_width = 240;
        state.framebuffer_height = 200;
        state.viewport = Rect{0, 0, 240, 200};
        state.content_height = 200;
        const FrameUpdatePlan plan = plan_frame_update(state);
        check(plan.action == FrameUpdateAction::RepaintExisting,
              "repeated paint dirty reuses existing pipeline");
        check(plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout,
              "repeated paint dirty uses current layout");

        const std::vector<Rect> rects =
            compute_dirty_rects(*fixture.document,
                                fixture.layout_tree.get(),
                                fixture.layout_tree.get(),
                                DirtyRegionOptions{Rect{0, 0, 240, 200}, 2, 2});
        check(!rects.empty() && rects.size() <= 2, "repeated paint dirty stays bounded");
        check(rects.front().width < 240 || rects.front().height < 200,
              "repeated paint dirty does not become full viewport");
        clear_dirty_flags(*fixture.document);
        check(subtree_dirty_flags(*fixture.document) == DomDirtyNone,
              "dirty flags clear after repeated paint update");
    }
}

void multiple_dirty_nodes_are_coalesced_without_full_frame() {
    HtmlParser html_parser;
    auto fixture = build_layout(
        html_parser.parse("<body><p>A</p><p>B</p><p>C</p><p>D</p></body>"),
        "p { width: 80px; height: 20px; margin: 0; }",
        240);
    clear_dirty_flags(*fixture.document);

    std::vector<Node*> paragraphs;
    collect_elements(*fixture.document, "p", paragraphs);
    check(paragraphs.size() == 4, "paragraphs exist");
    mark_dirty(*paragraphs[0], DomDirtyPaint);
    mark_dirty(*paragraphs[1], DomDirtyPaint);
    mark_dirty(*paragraphs[2], DomDirtyPaint);
    mark_dirty(*paragraphs[3], DomDirtyPaint);

    const std::vector<Rect> rects =
        compute_dirty_rects(*fixture.document,
                            fixture.layout_tree.get(),
                            fixture.layout_tree.get(),
                            DirtyRegionOptions{Rect{0, 0, 240, 200}, 2, 2});

    check(!rects.empty(), "multiple dirty nodes produce rects");
    check(rects.size() <= 2, "multiple dirty nodes respect max rects");
    check(rects.front().width < 240 || rects.front().height < 200,
          "multiple dirty nodes do not immediately force full frame");
}

void clean_document_reports_clean_region() {
    HtmlParser html_parser;
    auto fixture = build_layout(html_parser.parse("<body><p>Clean</p></body>"), "", 240);
    clear_dirty_flags(*fixture.document);

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             fixture.layout_tree.get(),
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(region.rects.empty(), "clean region has no rects");
    check(region.mode == DirtyRegionMode::Clean, "clean region reports clean mode");
    check(region.fallback_reason == DirtyRegionFallbackReason::None, "clean region has no fallback reason");
    check(std::string(dirty_region_mode_name(region.mode)) == "clean", "clean mode name");
    check(std::string(dirty_region_fallback_reason_name(region.fallback_reason)) == "none",
          "clean fallback reason name");
}

void missing_layout_reports_full_frame_reason() {
    HtmlParser html_parser;
    auto fixture = build_layout(html_parser.parse("<body><input value='A'></body>"), "", 240);
    clear_dirty_flags(*fixture.document);
    Node* input = first_element(*fixture.document, "input");
    check(input != nullptr, "input exists");
    check(append_text_to_control(*input, "B"), "paint dirty exists");

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             nullptr,
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(region.mode == DirtyRegionMode::FullFrame, "missing layout reports full frame");
    check(region.fallback_reason == DirtyRegionFallbackReason::MissingLayout,
          "missing layout reason is explicit");
    check(region.rects.size() == 1 && region.rects.front().width == 240,
          "missing layout produces viewport rect");
}

void tree_dirty_reason_wins_over_missing_layout() {
    HtmlParser html_parser;
    auto fixture = build_layout(html_parser.parse("<body><main><p>Alpha</p></main></body>"), "", 240);
    clear_dirty_flags(*fixture.document);
    Node* main = first_element(*fixture.document, "main");
    check(main != nullptr, "main exists");
    main->append_child(make_element("section"));

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             nullptr,
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(region.mode == DirtyRegionMode::FullFrame, "tree dirty with missing layout reports full frame");
    check(region.fallback_reason == DirtyRegionFallbackReason::TreeDirty,
          "tree dirty reason wins over missing layout");
}

void invalid_viewport_reports_reason_without_rects() {
    HtmlParser html_parser;
    auto fixture = build_layout(html_parser.parse("<body><input value='A'></body>"), "", 240);
    clear_dirty_flags(*fixture.document);
    Node* input = first_element(*fixture.document, "input");
    check(input != nullptr, "input exists");
    check(append_text_to_control(*input, "B"), "paint dirty exists");

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             fixture.layout_tree.get(),
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 0, 0}, 8, 2});

    check(region.mode == DirtyRegionMode::FullFrame, "invalid viewport reports full frame");
    check(region.fallback_reason == DirtyRegionFallbackReason::InvalidViewport,
          "invalid viewport reason is explicit");
    check(region.rects.empty(), "invalid viewport cannot produce a rect");
    check(std::string(dirty_region_fallback_reason_name(region.fallback_reason)) == "invalid-viewport",
          "invalid viewport fallback reason name");
}

void dirty_node_missing_from_layout_reports_reason() {
    HtmlParser html_parser;
    auto fixture = build_layout(html_parser.parse("<body><p>Visible</p></body>"), "", 240);
    clear_dirty_flags(*fixture.document);
    Node* body = first_element(*fixture.document, "body");
    check(body != nullptr, "body exists");
    Node& detached_like = body->append_child(make_element("section"));
    clear_dirty_flags(*fixture.document);
    mark_dirty(detached_like, DomDirtyPaint);

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             fixture.layout_tree.get(),
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 200}, 8, 2});

    check(region.mode == DirtyRegionMode::FullFrame, "missing dirty bounds reports full frame");
    check(region.fallback_reason == DirtyRegionFallbackReason::NoDirtyBounds,
          "missing dirty bounds reason is explicit");
}

void clipped_dirty_bounds_report_empty_after_clipping() {
    HtmlParser html_parser;
    auto fixture = build_layout(
        html_parser.parse("<body><p>Outside viewport</p></body>"),
        "p { width: 100px; height: 20px; margin-top: 80px; }",
        240);
    clear_dirty_flags(*fixture.document);
    Node* paragraph = first_element(*fixture.document, "p");
    check(paragraph != nullptr, "paragraph exists");
    mark_dirty(*paragraph, DomDirtyPaint);

    const DirtyRegionResult region =
        compute_dirty_region(*fixture.document,
                             fixture.layout_tree.get(),
                             fixture.layout_tree.get(),
                             DirtyRegionOptions{Rect{0, 0, 240, 40}, 8, 0});

    check(region.mode == DirtyRegionMode::FullFrame, "clipped dirty bounds report full frame");
    check(region.fallback_reason == DirtyRegionFallbackReason::EmptyAfterClipping,
          "empty clipping fallback reason is explicit");
}

void dirty_region_statistics_accumulate_modes_reasons_and_area() {
    DirtyRegionStatistics statistics;

    DirtyRegionResult clean;
    record_dirty_region_result(statistics, clean);

    DirtyRegionResult local;
    local.mode = DirtyRegionMode::DirtyRects;
    local.rects.push_back(Rect{0, 0, 10, 5});
    local.rects.push_back(Rect{10, 0, 4, 5});
    record_dirty_region_result(statistics, local);

    DirtyRegionResult full;
    full.mode = DirtyRegionMode::FullFrame;
    full.fallback_reason = DirtyRegionFallbackReason::TreeDirty;
    full.rects.push_back(Rect{0, 0, 20, 10});
    record_dirty_region_result(statistics, full);

    check(statistics.clean_frames == 1, "statistics count clean frames");
    check(statistics.dirty_rect_frames == 1, "statistics count dirty rect frames");
    check(statistics.full_frame_frames == 1, "statistics count full frame fallbacks");
    check(statistics.total_rects == 3, "statistics count total rects");
    check(statistics.total_dirty_area == 270, "statistics accumulate dirty area");
    check(dirty_region_fallback_count(statistics, DirtyRegionFallbackReason::None) == 2,
          "statistics count non-fallback frames");
    check(dirty_region_fallback_count(statistics, DirtyRegionFallbackReason::TreeDirty) == 1,
          "statistics count tree dirty fallback");
    check(std::string(dirty_region_mode_name(DirtyRegionMode::FullFrame)) == "full-frame",
          "full frame mode name");
}

void dirty_region_cost_helpers_bound_incremental_repaint() {
    DirtyRegionResult clean;
    check(dirty_region_area(clean) == 0, "clean dirty area is zero");
    check(dirty_region_area_percent(clean, Rect{0, 0, 100, 100}) == 0,
          "clean dirty area percent is zero");
    check(!dirty_region_should_repaint_incrementally(clean, Rect{0, 0, 100, 100}, 70),
          "clean result is not an incremental repaint");

    DirtyRegionResult local;
    local.mode = DirtyRegionMode::DirtyRects;
    local.rects.push_back(Rect{0, 0, 40, 50});
    local.rects.push_back(Rect{40, 0, 10, 50});
    check(dirty_region_area(local) == 2500, "dirty area sums rects");
    check(dirty_region_viewport_area(Rect{0, 0, 100, 100}) == 10000,
          "viewport area helper");
    check(dirty_region_area_percent(local, Rect{0, 0, 100, 100}) == 25,
          "dirty area percent is rounded up conservatively");
    check(dirty_region_should_repaint_incrementally(local, Rect{0, 0, 100, 100}, 25),
          "dirty area at threshold can repaint incrementally");
    check(!dirty_region_should_repaint_incrementally(local, Rect{0, 0, 100, 100}, 24),
          "dirty area over threshold should use full repaint");

    DirtyRegionResult overlapping = local;
    overlapping.rects.push_back(Rect{0, 0, 100, 100});
    check(dirty_region_area_percent(overlapping, Rect{0, 0, 100, 100}) == 100,
          "overlapping dirty area estimate saturates at viewport percent");
    check(!dirty_region_should_repaint_incrementally(overlapping, Rect{0, 0, 100, 100}, 70),
          "large dirty area should not repaint incrementally");
    check(!dirty_region_should_repaint_incrementally(overlapping, Rect{0, 0, 100, 100}, 100),
          "over-viewport estimated area should not repaint incrementally even at 100 percent");

    DirtyRegionResult full;
    full.mode = DirtyRegionMode::FullFrame;
    full.fallback_reason = DirtyRegionFallbackReason::DirtyAreaTooLarge;
    full.rects.push_back(Rect{0, 0, 100, 100});
    check(!dirty_region_should_repaint_incrementally(full, Rect{0, 0, 100, 100}, 100),
          "full-frame result is never treated as incremental");
    check(std::string(dirty_region_fallback_reason_name(DirtyRegionFallbackReason::DirtyAreaTooLarge)) ==
              "dirty-area-too-large",
          "dirty area fallback reason name");
}

void dirty_rect_coalescing_keeps_distant_rects_without_overhead() {
    const Rect input[] = {Rect{0, 0, 10, 10}, Rect{80, 0, 10, 10}};
    std::vector<Rect> output;
    output.reserve(2);
    const std::size_t reserved_capacity = output.capacity();
    DirtyRectCoalescingResult result;
    coalesce_dirty_rects_into(input,
                              2,
                              Rect{0, 0, 100, 100},
                              DirtyRectCoalescingOptions{8, 0, 100},
                              output,
                              &result);
    check(output.size() == 2, "zero-overhead coalescing preserves distant rects");
    check(result.input_area == 200 && result.output_area == 200,
          "zero-overhead coalescing reports exact area");
    check(result.estimated_cost_before == 200 && result.estimated_cost_after == 200,
          "zero-overhead coalescing preserves cost");
    check(output.capacity() == reserved_capacity,
          "caller-owned coalescing scratch avoids allocation when capacity is reused");
}

void dirty_rect_coalescing_uses_generic_per_rect_cost() {
    const Rect input[] = {Rect{0, 0, 10, 10}, Rect{12, 0, 10, 10}};
    std::vector<Rect> output;
    DirtyRectCoalescingResult result;
    coalesce_dirty_rects_into(input,
                              2,
                              Rect{0, 0, 100, 100},
                              DirtyRectCoalescingOptions{8, 30, 25},
                              output,
                              &result);
    check(output.size() == 1, "per-rect setup cost merges nearby rects");
    check(output.front().x == 0 && output.front().width == 22,
          "nearby rect merge keeps enclosing bounds");
    check(result.estimated_cost_before == 260 && result.estimated_cost_after == 250,
          "coalescing reduces equivalent-pixel cost");
}

void dirty_rect_coalescing_respects_extra_area_budget() {
    const Rect input[] = {Rect{0, 0, 10, 10}, Rect{40, 0, 10, 10}};
    std::vector<Rect> output;
    coalesce_dirty_rects_into(input,
                              2,
                              Rect{0, 0, 100, 100},
                              DirtyRectCoalescingOptions{8, 100, 20},
                              output);
    check(output.size() == 2, "extra-area budget rejects expensive merge");
}

void dirty_rect_coalescing_forces_deterministic_low_extra_merge() {
    const Rect input[] = {Rect{0, 0, 10, 10}, Rect{12, 0, 10, 10}, Rect{80, 0, 10, 10}};
    std::vector<Rect> output;
    DirtyRectCoalescingResult result;
    coalesce_dirty_rects_into(input,
                              3,
                              Rect{0, 0, 100, 100},
                              DirtyRectCoalescingOptions{2, 0, 0},
                              output,
                              &result);
    check(output.size() == 2, "max rect count forces a merge");
    check(output.front().x == 0 && output.front().width == 22,
          "forced merge selects least extra-area pair deterministically");
    check(result.forced_merges == 1, "forced merge is observable to the host");
}

void dirty_rect_coalescing_clips_and_handles_large_areas() {
    const Rect input[] = {Rect{-10, 0, 20, 10}, Rect{20, 20, 0, 4}, Rect{200, 0, 10, 10}};
    std::vector<Rect> output;
    DirtyRectCoalescingResult result;
    coalesce_dirty_rects_into(input,
                              3,
                              Rect{0, 0, 100, 100},
                              DirtyRectCoalescingOptions{8, 0, 100},
                              output,
                              &result);
    check(output.size() == 1 && output.front().x == 0 && output.front().width == 10,
          "coalescing discards empty and out-of-viewport input");
    check(result.input_rect_count == 1 && result.input_area == 100,
          "coalescing statistics count clipped input only");

    const Rect large[] = {Rect{0, 0, 2147483647, 2147483647}};
    coalesce_dirty_rects_into(large,
                              1,
                              Rect{0, 0, 2147483647, 2147483647},
                              DirtyRectCoalescingOptions{},
                              output,
                              &result);
    check(result.input_area == result.output_area && result.output_area > 0,
          "coalescing area accounting handles maximum representable rectangles");
}

void dirty_region_area_handles_extreme_rects_safely() {
    DirtyRegionResult result;
    result.rects.push_back(Rect{std::numeric_limits<int>::max() - 1,
                                std::numeric_limits<int>::max() - 1,
                                2,
                                2});
    result.rects.push_back(Rect{0, 0, 1, 1});
    check(dirty_region_area(result) > 0, "extreme dirty region keeps a positive area");
    check(dirty_region_area_percent(result, Rect{0, 0, 100, 100}) == 100,
          "extreme dirty region saturates at viewport percent");
}

} // namespace

int main() {
    try {
        text_dirty_generates_local_rect();
        tree_dirty_falls_back_to_full_viewport();
        paint_dirty_reuses_layout_and_generates_local_rect();
        repeated_paint_dirty_updates_remain_bounded();
        multiple_dirty_nodes_are_coalesced_without_full_frame();
        clean_document_reports_clean_region();
        missing_layout_reports_full_frame_reason();
        tree_dirty_reason_wins_over_missing_layout();
        invalid_viewport_reports_reason_without_rects();
        dirty_node_missing_from_layout_reports_reason();
        clipped_dirty_bounds_report_empty_after_clipping();
        dirty_region_statistics_accumulate_modes_reasons_and_area();
        dirty_region_cost_helpers_bound_incremental_repaint();
        dirty_rect_coalescing_keeps_distant_rects_without_overhead();
        dirty_rect_coalescing_uses_generic_per_rect_cost();
        dirty_rect_coalescing_respects_extra_area_budget();
        dirty_rect_coalescing_forces_deterministic_low_extra_merge();
        dirty_rect_coalescing_clips_and_handles_large_areas();
        dirty_region_area_handles_extreme_rects_safely();
    } catch (const std::exception& error) {
        std::cerr << "dirty region test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "dirty region tests passed\n";
    return 0;
}
