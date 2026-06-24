#include "render_core/css_parser.h"
#include "render_core/animation_timeline.h"
#include "render_core/animation_invalidation.h"
#include "render_core/frame_scratch.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/text_repaint.h"

#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

using namespace jellyframe;

namespace {

using Clock = std::chrono::steady_clock;

std::string make_card_html(int count) {
    std::ostringstream html;
    html << "<!doctype html><html><body><main id='app' class='shell'><form id='search' class='search-box'>"
         << "<input class='search-input' name='q'><button class='primary'>Search</button></form>";
    for (int i = 0; i < count; ++i) {
        html << "<article class='card metric-card' data-index='" << i << "'>"
             << "<h2>Metric " << i << "</h2><p><strong>" << (60 + i % 40)
             << "</strong> units</p></article>";
    }
    html << "</main></body></html>";
    return html.str();
}

std::string make_card_css() {
    return "body { margin: 0; padding: 0; background: #f8fafc; color: #111827; }"
           ".shell { display: grid; padding: 16px; }"
           "#search.search-box { display: block; width: 320px; padding: 12px; background: #ffffff; border: 1px solid #cbd5e1; opacity: .96; }"
           ".search-input { display: block; width: 280px; padding: 8px; background: #ffffff; color: #111827; }"
           "button.primary { display: inline-block; padding: 8px; background: #2563eb; color: white; }"
           ".card.metric-card { display: block; margin: 8px; padding: 12px; background: #ffffff; border-radius: 12px; overflow: hidden; }"
           ".card metric { color: oklch(50% 0.2 30); }"
           "@supports (backdrop-filter: blur(8px)) { .card { backdrop-filter: blur(8px); } }";
}

template <typename Fn>
double average_microseconds(int iterations, Fn fn) {
    const auto begin = Clock::now();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    const auto end = Clock::now();
    const auto total = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
    return static_cast<double>(total) / static_cast<double>(iterations);
}

void print_result(const char* name, int iterations, double average_us) {
    std::cout << name << " iterations=" << iterations << " avg_us=" << average_us << '\n';
}

void print_style_statistics(const StyleResolverStatistics& statistics) {
    std::cout << "style_candidate_cache entries=" << statistics.candidate_cache_entries
              << " rule_refs=" << statistics.candidate_cache_rule_refs
              << " hits=" << statistics.candidate_cache_hits
              << " misses=" << statistics.candidate_cache_misses
              << " clears=" << statistics.candidate_cache_clears << '\n';
}

const LayoutBox* find_first_layout_by_class(const LayoutBox& box, const char* class_name) {
    if (box.node != nullptr && box.node->type == NodeType::Element && box.node->has_class(class_name)) {
        return &box;
    }
    for (const auto& child : box.children) {
        if (const LayoutBox* found = find_first_layout_by_class(*child, class_name)) {
            return found;
        }
    }
    return nullptr;
}

Style animated_style(float opacity, const char* transform, Color background) {
    Style style;
    style.opacity = opacity;
    style.transform = transform;
    style.background_color = background;
    style.transitions[0] = StyleTransition{
        AnimatableProperty::Opacity,
        180,
        0,
        AnimationTimingFunction::EaseOut,
    };
    style.transitions[1] = StyleTransition{
        AnimatableProperty::Transform,
        180,
        0,
        AnimationTimingFunction::EaseOut,
    };
    style.transitions[2] = StyleTransition{
        AnimatableProperty::BackgroundColor,
        180,
        0,
        AnimationTimingFunction::Linear,
    };
    style.transition_count = 3;
    return style;
}

DisplayCommand fill_command(Rect rect, Color color, int radius = 0) {
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = rect;
    command.color = color;
    command.border_radius = radius;
    return command;
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

Node* find_first_element_by_id(Node& node, const std::string& id) {
    if (node.type == NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (auto& child : node.children) {
        if (Node* found = find_first_element_by_id(*child, id)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_render_core_microbench [card_count=80] [iterations=200]\n";
        return 0;
    }
    const int card_count = argc >= 2 ? std::stoi(argv[1]) : 80;
    const int iterations = argc >= 3 ? std::stoi(argv[2]) : 200;
    const std::string html = make_card_html(card_count);
    const std::string css = make_card_css();

    HtmlParser html_parser;
    CssParser css_parser;

    print_result("html_parse", iterations, average_microseconds(iterations, [&] {
        auto document = html_parser.parse(html);
        (void)document;
    }));

    print_result("css_parse", iterations, average_microseconds(iterations, [&] {
        auto stylesheet = css_parser.parse(css);
        (void)stylesheet;
    }));

    auto document = html_parser.parse(html);
    auto stylesheet = css_parser.parse(css);

    print_result("render_tree", iterations, average_microseconds(iterations, [&] {
        StyleResolver resolver(stylesheet);
        RenderTreeBuilder builder(resolver);
        MonotonicArena arena;
        auto render_tree = builder.build(*document, arena);
        (void)render_tree;
    }));

    StyleResolver resolver(stylesheet);
    RenderTreeBuilder builder(resolver);
    MonotonicArena render_tree_arena;
    auto render_tree = builder.build(*document, render_tree_arena);

    print_result("layout", iterations, average_microseconds(iterations, [&] {
        LayoutEngine layout_engine(resolver);
        MonotonicArena layout_arena;
        auto layout_tree = layout_engine.layout(*render_tree, 360, layout_arena);
        (void)layout_tree;
    }));

    LayoutEngine layout_engine(resolver);
    MonotonicArena layout_arena;
    auto layout_tree = layout_engine.layout(*render_tree, 360, layout_arena);

    print_result("layer_tree", iterations, average_microseconds(iterations, [&] {
        LayerTreeBuilder layer_tree_builder;
        MonotonicArena layer_arena;
        auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);
        (void)layer_tree;
    }));

    LayerTreeBuilder layer_tree_builder;
    MonotonicArena layer_arena;
    auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);

    print_result("flatten_layers", iterations, average_microseconds(iterations, [&] {
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        (void)display_list;
    }));

    print_result("full_pipeline", iterations, average_microseconds(iterations, [&] {
        auto local_document = html_parser.parse(html);
        auto local_stylesheet = css_parser.parse(css);
        StyleResolver local_resolver(local_stylesheet);
        RenderTreeBuilder local_builder(local_resolver);
        MonotonicArena local_render_tree_arena;
        auto local_render_tree = local_builder.build(*local_document, local_render_tree_arena);
        LayoutEngine local_layout(local_resolver);
        MonotonicArena local_layout_arena;
        auto local_layout_tree = local_layout.layout(*local_render_tree, 360, local_layout_arena);
        LayerTreeBuilder local_layer_tree_builder;
        MonotonicArena local_layer_arena;
        auto local_layer_tree = local_layer_tree_builder.build(*local_layout_tree, local_layer_arena);
        DisplayList display_list = local_layer_tree_builder.flatten(*local_layer_tree);
        (void)display_list;
    }));

    DisplayList rounded_commands;
    for (int row = 0; row < 6; ++row) {
        for (int column = 0; column < 6; ++column) {
            rounded_commands.push_back(fill_command(Rect{column * 52, row * 42, 44, 34},
                                                    Color{20, 184, 166, 255},
                                                    12));
        }
    }
    print_result("rounded_rect_aa_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{255, 255, 255, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(rounded_commands, target, Rect{0, 0, 320, 260});
    }));

    LayerNode scale_root;
    scale_root.type = LayerType::Root;
    scale_root.bounds = Rect{0, 0, 160, 160};
    auto scale_child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    scale_child->type = LayerType::Composited;
    scale_child->bounds = Rect{40, 40, 64, 64};
    scale_child->transform.scale_x = 1.35F;
    scale_child->transform.scale_y = 1.35F;
    scale_child->display_list.push_back(fill_command(Rect{40, 40, 64, 64}, Color{255, 255, 255, 255}, 10));
    scale_child->display_list.push_back(fill_command(Rect{40, 40, 32, 64}, Color{14, 165, 233, 255}, 10));
    scale_root.children.push_back(std::move(scale_child));
    print_result("scaled_layer_bilinear", iterations, average_microseconds(iterations, [&] {
        const FrameBuffer target = SoftwareCompositor().render(scale_root, 160, 160, Color{15, 23, 42, 255});
        (void)target;
    }));

    Node animation_node(NodeType::Element);
    animation_node.tag_name = "div";
    const Style from_style = animated_style(0.25F, "translate(0px, 0px) scale(1)", Color{30, 64, 175, 255});
    const Style to_style = animated_style(1.0F, "translate(12px, 6px) scale(1.08)", Color{59, 130, 246, 255});
    FrameScratch frame_scratch;
    HostBudgets budgets;
    budgets.max_active_animations = 4;
    frame_scratch.reserve_from_budgets(budgets);

    print_result("animation_timeline_empty_sample", iterations, average_microseconds(iterations, [&] {
        frame_scratch.begin_frame();
        AnimationTimeline timeline(AnimationTimelineOptions{4, nullptr});
        const bool sampled = timeline.sample(90, frame_scratch.style_overrides);
        (void)sampled;
    }));

    print_result("animation_timeline_active_sample", iterations, average_microseconds(iterations, [&] {
        frame_scratch.begin_frame();
        AnimationTimeline timeline(AnimationTimelineOptions{4, nullptr});
        timeline.start_transitions(animation_node, from_style, to_style, 0);
        const bool sampled = timeline.sample(90, frame_scratch.style_overrides);
        (void)sampled;
    }));

    StyleAnimation keyframe_animation;
    keyframe_animation.name = "pulse";
    keyframe_animation.duration_ms = 180;
    keyframe_animation.timing = AnimationTimingFunction::EaseOut;
    keyframe_animation.infinite = true;
    CssKeyframesRule keyframes;
    keyframes.name = "pulse";
    keyframes.from_declarations.push_back(CssDeclaration{"opacity", ".25", false});
    keyframes.from_declarations.push_back(CssDeclaration{"transform", "translate(0px,0px) scale(1)", false});
    keyframes.to_declarations.push_back(CssDeclaration{"opacity", "1", false});
    keyframes.to_declarations.push_back(CssDeclaration{"transform", "translate(12px,6px) scale(1.08)", false});
    print_result("keyframe_animation_sample", iterations, average_microseconds(iterations, [&] {
        frame_scratch.begin_frame();
        AnimationTimeline timeline(AnimationTimelineOptions{4, nullptr});
        timeline.ensure_keyframe_animation(animation_node, from_style, keyframe_animation, keyframes, 0);
        const bool sampled = timeline.sample(90, frame_scratch.style_overrides);
        (void)sampled;
    }));

    const LayoutBox* animated_box = find_first_layout_by_class(*layout_tree, "metric-card");
    if (animated_box != nullptr && animated_box->node != nullptr) {
        StyleOverride previous;
        previous.node = animated_box->node;
        previous.has_transform = true;
        previous.transform = "translate(0px,0px) scale(1)";
        StyleOverride current;
        current.node = animated_box->node;
        current.has_transform = true;
        current.transform = "translate(12px,6px) scale(1.08)";
        std::vector<StyleOverride> previous_overrides{previous};
        std::vector<StyleOverride> current_overrides{current};
        print_result("animation_dirty_region", iterations, average_microseconds(iterations, [&] {
            frame_scratch.begin_frame();
            compute_animation_dirty_region_into(*layout_tree,
                                                previous_overrides,
                                                current_overrides,
                                                AnimationInvalidationOptions{Rect{0, 0, 360, layout_tree->rect.height},
                                                                             budgets.max_dirty_rects,
                                                                             3},
                                                frame_scratch.dirty_region);
        }));
    }

    auto text_document = html_parser.parse("<body><p id='frame'>01</p></body>");
    auto text_stylesheet = css_parser.parse("p { margin: 0; font-size: 10px; line-height: 12px; }");
    StyleResolver text_resolver(text_stylesheet);
    RenderTreeBuilder text_builder(text_resolver);
    auto text_render_tree = text_builder.build(*text_document);
    LayoutEngine text_layout_engine(text_resolver, fixed_text_measure());
    auto text_layout_tree = text_layout_engine.layout(*text_render_tree, 240);
    Node* frame_node = find_first_element_by_id(*text_document, "frame");
    if (frame_node != nullptr) {
        clear_dirty_flags(*text_document);
        bool toggle = false;
        print_result("text_repaint_reuse_check", iterations, average_microseconds(iterations, [&] {
            frame_node->set_text_content(toggle ? "01" : "02");
            toggle = !toggle;
            const bool reusable =
                text_dirty_can_reuse_layout(*text_document, *text_layout_tree, fixed_text_measure());
            clear_dirty_flags(*text_document);
            (void)reusable;
        }));
    }
    print_style_statistics(resolver.statistics());

    return 0;
}
