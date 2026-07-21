#include "render_core/css_parser.h"
#include "render_core/animation_timeline.h"
#include "render_core/animation_invalidation.h"
#include "render_core/canvas2d.h"
#include "render_core/dirty_region.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/frame_scratch.h"
#include "render_core/html_parser.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/scroll_blit.h"
#include "render_core/software_renderer.h"
#include "render_core/style_repaint.h"
#include "render_core/text_repaint.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string make_custom_property_html(int count) {
    std::ostringstream html;
    html << "<!doctype html><html><body><main class='theme'>";
    for (int i = 0; i < count; ++i) {
        html << "<section class='cluster'><article class='card tone-" << (i % 3)
             << "' style='--local-gap:" << (6 + i % 5) << "px'>"
             << "<h2>Card " << i << "</h2><p>Signal " << (40 + i % 60)
             << "</p><button class='action'>Open</button></article></section>";
    }
    html << "</main></body></html>";
    return html.str();
}

std::string make_custom_property_css() {
    return ":root { --fg: #0f172a; --panel: #f8fafc; --accent: #2563eb; }"
           ".theme { --panel: #ffffff; color: var(--fg); }"
           ".cluster { --accent: #0f766e; margin: var(--local-gap, 8px); }"
           ".card { display: block; padding: var(--local-gap, 8px); color: var(--fg); "
           "background: var(--panel); border-color: var(--accent); }"
           ".tone-1 { --accent: #dc2626; }"
           ".tone-2 { --panel: #eef2ff; }"
           ".action { color: var(--panel); background: var(--accent); }";
}

std::string make_logical_hsl_css() {
    return "body { margin: 0; }"
           ".shell { inline-size: 100%; padding-inline: 12px 16px; padding-block: 8px; }"
           ".card { margin-inline: 6px; padding-block: 8px 10px; min-block-size: 32px; "
           "color: hsl(210 50% 40% / 90%); background: linear-gradient(hsl(210 60% 98%), #ffffff); }"
           ".metric-card { place-content: center space-between; }";
}

std::string make_flex_order_html(int count) {
    std::ostringstream html;
    html << "<!doctype html><html><body><main class='ordered'>";
    for (int index = 0; index < count; ++index) {
        html << "<div class='item rank-" << (index % 3) << "'>" << index << "</div>";
    }
    html << "</main></body></html>";
    return html.str();
}

std::string make_flex_order_css() {
    return "body { margin: 0; }"
           ".ordered { display: flex; flex-wrap: wrap; width: 320px; gap: 4px; }"
           ".item { width: 36px; height: 20px; background: #ffffff; }"
           ".rank-0 { order: 1; } .rank-1 { order: -1; } .rank-2 { order: 0; }";
}

std::string make_single_level_nested_css() {
    return ".card { color: #102030; padding: 8px; "
           "&:hover { color: hsl(210 70% 45%); } "
           "& .metric { color: #2563eb; } }";
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
              << " clears=" << statistics.candidate_cache_clears
              << " bypasses=" << statistics.candidate_cache_bypasses << '\n';
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
    style.transitions = {
        StyleTransition{AnimatableProperty::Opacity, 180, 0, AnimationTimingFunction::EaseOut},
        StyleTransition{AnimatableProperty::Transform, 180, 0, AnimationTimingFunction::EaseOut},
        StyleTransition{AnimatableProperty::BackgroundColor, 180, 0, AnimationTimingFunction::Linear},
    };
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

int parse_positive_int_arg(const char* value, const char* name) {
    if (value == nullptr) {
        throw std::invalid_argument(std::string("missing ") + name);
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE || parsed < 1 || parsed > INT_MAX) {
        throw std::invalid_argument(std::string("invalid positive integer for ") + name + ": " + value);
    }
    return static_cast<int>(parsed);
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

void collect_nodes(const Node& node, std::vector<const Node*>& nodes) {
    nodes.push_back(&node);
    for (const auto& child : node.children) {
        collect_nodes(*child, nodes);
    }
}

} // namespace

int run_render_core_microbench(int argc, char** argv) {
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "usage: jellyframe_render_core_microbench [card_count=80] [iterations=200]\n";
        return 0;
    }
    const int card_count = argc >= 2 ? parse_positive_int_arg(argv[1], "card_count") : 80;
    const int iterations = argc >= 3 ? parse_positive_int_arg(argv[2], "iterations") : 200;
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

    auto stylesheet = css_parser.parse(css);
    print_result("style_resolver_construct", iterations, average_microseconds(iterations, [&] {
        StyleResolver resolver(stylesheet);
        (void)resolver;
    }));

    const std::string nested_css = make_single_level_nested_css();
    print_result("single_level_nesting_css_parse", iterations, average_microseconds(iterations, [&] {
        auto stylesheet = css_parser.parse(nested_css);
        (void)stylesheet;
    }));

    auto document = html_parser.parse(html);

    std::vector<const Node*> nodes;
    collect_nodes(*document, nodes);
    print_result("style_resolve", iterations, average_microseconds(iterations, [&] {
        StyleResolver style_resolver(stylesheet);
        StyleResolveContext context;
        for (const Node* node : nodes) {
            const Style style = style_resolver.resolve(*node, context);
            (void)style;
        }
    }));

    StyleResolver retained_style_resolver(stylesheet);
    print_result("style_resolve_retained_candidates", iterations, average_microseconds(iterations, [&] {
        StyleResolveContext context;
        for (const Node* node : nodes) {
            const Style style = retained_style_resolver.resolve(*node, context);
            (void)style;
        }
    }));
    print_style_statistics(retained_style_resolver.statistics());

    auto custom_document = html_parser.parse(make_custom_property_html(card_count));
    auto custom_stylesheet = css_parser.parse(make_custom_property_css());
    std::vector<const Node*> custom_nodes;
    collect_nodes(*custom_document, custom_nodes);
    StyleResolver naive_custom_resolver(custom_stylesheet);
    print_result("custom_property_style_resolve_naive", iterations, average_microseconds(iterations, [&] {
        for (const Node* node : custom_nodes) {
            const Style style = naive_custom_resolver.resolve(*node);
            (void)style;
        }
    }));
    print_result("custom_property_style_resolve", iterations, average_microseconds(iterations, [&] {
        StyleResolver custom_resolver(custom_stylesheet);
        StyleResolveContext context;
        for (const Node* node : custom_nodes) {
            const Style style = custom_resolver.resolve(*node, context);
            (void)style;
        }
    }));

    auto logical_document = html_parser.parse(html);
    auto logical_stylesheet = css_parser.parse(make_logical_hsl_css());
    std::vector<const Node*> logical_nodes;
    collect_nodes(*logical_document, logical_nodes);
    print_result("logical_hsl_style_resolve", iterations, average_microseconds(iterations, [&] {
        StyleResolver logical_resolver(logical_stylesheet);
        StyleResolveContext context;
        for (const Node* node : logical_nodes) {
            const Style style = logical_resolver.resolve(*node, context);
            (void)style;
        }
    }));

    auto flex_order_document = html_parser.parse(make_flex_order_html(card_count));
    auto flex_order_stylesheet = css_parser.parse(make_flex_order_css());
    StyleResolver flex_order_resolver(flex_order_stylesheet);
    RenderTreeBuilder flex_order_builder(flex_order_resolver);
    MonotonicArena flex_order_render_arena;
    auto flex_order_render_tree = flex_order_builder.build(*flex_order_document, flex_order_render_arena);
    print_result("flex_order_layout", iterations, average_microseconds(iterations, [&] {
        LayoutEngine flex_order_layout(flex_order_resolver);
        MonotonicArena flex_order_layout_arena;
        auto flex_order_layout_tree = flex_order_layout.layout(*flex_order_render_tree, 360, flex_order_layout_arena);
        (void)flex_order_layout_tree;
    }));

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

    MonotonicArena reused_layer_arena;
    print_result("layer_tree_rebuild_arena_rewind", iterations, average_microseconds(iterations, [&] {
        auto reused_layer_tree = layer_tree_builder.build(*layout_tree, reused_layer_arena);
        reused_layer_tree.reset();
        reused_layer_arena.rewind();
    }));

    print_result("flatten_layers", iterations, average_microseconds(iterations, [&] {
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        (void)display_list;
    }));

    DisplayList reusable_display_list;
    print_result("flatten_layers_reuse", iterations, average_microseconds(iterations, [&] {
        layer_tree_builder.flatten_into(*layer_tree, reusable_display_list);
    }));

    DisplayList retained_layout_display_list;
    print_result("retained_layout_display_pipeline", iterations, average_microseconds(iterations, [&] {
        LayerTreeBuilder retained_layer_builder;
        MonotonicArena retained_layer_arena;
        auto retained_layer_tree = retained_layer_builder.build(*layout_tree, retained_layer_arena);
        retained_layer_builder.flatten_into(*retained_layer_tree, retained_layout_display_list);
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

    DisplayCommand opaque_screen_gradient;
    opaque_screen_gradient.type = DisplayCommandType::LinearGradient;
    opaque_screen_gradient.rect = Rect{0, 0, 172, 320};
    opaque_screen_gradient.color = Color{22, 71, 87, 255};
    opaque_screen_gradient.color2 = Color{6, 22, 31, 255};
    print_result("opaque_linear_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(172, 320, Color{0, 0, 0, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(opaque_screen_gradient, target, Rect{0, 0, 172, 320});
    }));
    opaque_screen_gradient.gradient_axis = GradientAxis::Horizontal;
    print_result("opaque_horizontal_linear_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(172, 320, Color{0, 0, 0, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(opaque_screen_gradient, target, Rect{0, 0, 172, 320});
    }));
    opaque_screen_gradient.gradient_axis = GradientAxis::DiagonalDownRight;
    print_result("opaque_diagonal_linear_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(172, 320, Color{0, 0, 0, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(opaque_screen_gradient, target, Rect{0, 0, 172, 320});
    }));
    FrameBuffer rgb565_dither_source(172, 320, Color{0, 0, 0, 255});
    for (int y = 0; y < rgb565_dither_source.height; ++y) {
        for (int x = 0; x < rgb565_dither_source.width; ++x) {
            const int shade = 34 + (x * 96 + y * 125) / 491;
            rgb565_dither_source.pixel(x, y) = Color{
                static_cast<std::uint8_t>(shade),
                static_cast<std::uint8_t>(shade + 18),
                static_cast<std::uint8_t>(shade + 36),
                255,
            };
        }
    }
    std::vector<std::uint16_t> rgb565_dither_pixels(172U * 320U);
    EmbeddedPackedRgb565Sink rgb565_dither_sink{
        EmbeddedPixelFormat::Rgb565,
        rgb565_dither_pixels.data(),
        rgb565_dither_pixels.size(),
        true,
        nullptr,
        nullptr,
    };
    print_result("packed_rgb565_dither_present", iterations, average_microseconds(iterations, [&] {
        (void)present_to_packed_rgb565(frame_buffer_view(rgb565_dither_source),
                                       nullptr,
                                       0,
                                       rgb565_dither_sink);
    }));

    DisplayList conic_commands;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            DisplayCommand command;
            command.type = DisplayCommandType::ConicGradient;
            command.rect = Rect{column * 72 + 8, row * 62 + 8, 52, 52};
            command.color = Color{34, 204, 136, 255};
            command.color2 = Color{16, 32, 48, 96};
            command.gradient_stop_percent = 40 + ((row * 4 + column) % 6) * 10;
            command.border_radius = 26;
            conic_commands.push_back(command);
        }
    }
    print_result("conic_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{8, 16, 24, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(conic_commands, target, Rect{0, 0, 320, 260});
    }));

    DisplayList radial_commands;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            DisplayCommand command;
            command.type = DisplayCommandType::RadialGradient;
            command.rect = Rect{column * 72 + 8, row * 62 + 8, 56, 46};
            command.color = Color{236, 254, 255, 220};
            command.color2 = Color{14, 116, 144, 72};
            command.border_radius = 18;
            radial_commands.push_back(command);
        }
    }
    print_result("radial_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{8, 16, 24, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(radial_commands, target, Rect{0, 0, 320, 260});
    }));

    DisplayList diagonal_gradient_commands;
    DisplayList soft_shadow_commands;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            DisplayCommand gradient;
            gradient.type = DisplayCommandType::LinearGradient;
            gradient.rect = Rect{column * 72 + 8, row * 62 + 8, 56, 46};
            gradient.color = Color{44, 52, 66, 255};
            gradient.color2 = Color{16, 18, 24, 255};
            gradient.gradient_axis = GradientAxis::DiagonalDownRight;
            gradient.border_radius = 14;
            diagonal_gradient_commands.push_back(gradient);

            DisplayCommand shadow;
            shadow.type = DisplayCommandType::BoxShadow;
            shadow.rect = Rect{column * 72 + 3, row * 62 + 5, 66, 56};
            shadow.color = Color{0, 0, 0, 72};
            shadow.border_radius = 19;
            shadow.stroke_width = 5;
            shadow.gradient_stop_percent = 5;
            soft_shadow_commands.push_back(shadow);
        }
    }
    print_result("diagonal_gradient_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{8, 16, 24, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(diagonal_gradient_commands, target, Rect{0, 0, 320, 260});
    }));
    DisplayList layered_hydrogel_commands = diagonal_gradient_commands;
    for (const DisplayCommand& base : radial_commands) {
        DisplayCommand highlight = base;
        highlight.color = Color{236, 254, 255, 44};
        highlight.color2 = Color{0, 0, 0, 0};
        highlight.gradient_axis = GradientAxis::RadialPosition;
        highlight.gradient_stop_percent = 80 * 101 + 12;
        layered_hydrogel_commands.push_back(highlight);
    }
    print_result("layered_hydrogel_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{8, 16, 24, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(layered_hydrogel_commands, target, Rect{0, 0, 320, 260});
    }));
    print_result("soft_box_shadow_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{8, 16, 24, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(soft_shadow_commands, target, Rect{0, 0, 320, 260});
    }));
    DisplayCommand circular_shadow;
    circular_shadow.type = DisplayCommandType::BoxShadow;
    circular_shadow.rect = Rect{26, 98, 120, 120};
    circular_shadow.color = Color{200, 255, 90, 56};
    circular_shadow.border_radius = 60;
    circular_shadow.stroke_width = 10;
    circular_shadow.gradient_stop_percent = 9;
    print_result("circular_box_shadow_exact_raster", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(172, 320, Color{18, 22, 26, 255});
        SoftwareRasterizer rasterizer;
        rasterizer.rasterize(circular_shadow, target, Rect{0, 0, 172, 320});
    }));

    LayerNode dirty_root;
    dirty_root.type = LayerType::Root;
    dirty_root.bounds = Rect{0, 0, 320, 260};
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 8; ++column) {
            dirty_root.display_list.push_back(fill_command(Rect{column * 38, row * 30, 30, 22},
                                                           Color{30, 64, 175, 255},
                                                           6));
        }
    }
    const std::array<Rect, 6> contained_dirty_rects{{
        Rect{0, 0, 160, 130},
        Rect{20, 20, 30, 30},
        Rect{0, 0, 160, 130},
        Rect{180, 20, 70, 80},
        Rect{190, 30, 20, 20},
        Rect{180, 20, 70, 80},
    }};
    print_result("dirty_rect_replay_contained", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(320, 260, Color{255, 255, 255, 255});
        SoftwareCompositor compositor;
        compositor.render_into(dirty_root,
                               target,
                               Color{255, 255, 255, 255},
                               contained_dirty_rects.data(),
                               contained_dirty_rects.size());
    }));

    DisplayCommand clipped_text;
    clipped_text.type = DisplayCommandType::Text;
    clipped_text.rect = Rect{0, 0, 240, 24};
    clipped_text.color = Color{15, 23, 42, 255};
    clipped_text.text = "Retained dirty text";
    clipped_text.font_size = 14;
    const Rect text_dirty{120, 0, 24, 24};
    SoftwareRasterizer text_rasterizer;
    print_result("dirty_text_clip_transient_surface", iterations, average_microseconds(iterations, [&] {
        FrameBuffer target(240, 24, Color{255, 255, 255, 255});
        text_rasterizer.rasterize(clipped_text, target, text_dirty);
    }));

    SoftwareRasterizerScratch text_scratch;
    FrameBuffer scratch_target(240, 24, Color{255, 255, 255, 255});
    print_result("dirty_text_clip_reused_surface", iterations, average_microseconds(iterations, [&] {
        text_rasterizer.rasterize(clipped_text, scratch_target, text_dirty, 0, 0, &text_scratch);
    }));

    const std::array<Rect, 3> adjacent_dirty_rects{{
        Rect{20, 40, 20, 29},
        Rect{42, 40, 20, 29},
        Rect{64, 40, 20, 29},
    }};
    const DirtyRectCoalescingOptions dirty_rect_coalescing_options{8, 256, 10};
    std::vector<Rect> coalesced_dirty_rects;
    coalesced_dirty_rects.reserve(adjacent_dirty_rects.size());
    print_result("dirty_rect_coalescing_adjacent", iterations, average_microseconds(iterations, [&] {
        coalesce_dirty_rects_into(adjacent_dirty_rects.data(),
                                  adjacent_dirty_rects.size(),
                                  Rect{0, 0, 172, 320},
                                  dirty_rect_coalescing_options,
                                  coalesced_dirty_rects);
        if (coalesced_dirty_rects.size() != 1) {
            throw std::runtime_error("adjacent dirty rectangles did not coalesce");
        }
    }));

    print_result("scroll_blit_plan", iterations, average_microseconds(iterations, [&] {
        const ScrollBlitPlan plan = plan_vertical_scroll_blit(320, 240, 720, 96, 112);
        (void)plan;
    }));

    FrameBuffer scroll_blit_target(320, 240, Color{15, 23, 42, 255});
    const ScrollBlitPlan scroll_blit_plan = plan_vertical_scroll_blit(320, 240, 720, 96, 112);
    print_result("scroll_framebuffer_blit", iterations, average_microseconds(iterations, [&] {
        if (!apply_vertical_scroll_blit(scroll_blit_target, Rect{0, 0, 320, 240}, scroll_blit_plan)) {
            throw std::runtime_error("scroll framebuffer blit plan was rejected");
        }
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

    LayerNode rotate_root;
    rotate_root.type = LayerType::Root;
    rotate_root.bounds = Rect{0, 0, 160, 160};
    auto rotate_child = LayerNodePtr(new LayerNode, LayerNodeDeleter{false});
    rotate_child->type = LayerType::Composited;
    rotate_child->bounds = Rect{76, 34, 8, 72};
    rotate_child->transform.rotate_degrees = 42.0F;
    rotate_child->transform_origin_x_percent = 50;
    rotate_child->transform_origin_y_percent = 100;
    rotate_child->display_list.push_back(fill_command(Rect{76, 34, 8, 72}, Color{236, 253, 245, 255}, 4));
    rotate_root.children.push_back(std::move(rotate_child));
    print_result("rotated_layer_bilinear", iterations, average_microseconds(iterations, [&] {
        const FrameBuffer target = SoftwareCompositor().render(rotate_root, 160, 160, Color{15, 23, 42, 255});
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

    auto opacity_document = html_parser.parse("<body><div class='fade'>Fade</div></body>");
    StyleResolver opacity_resolver(css_parser.parse(
        ".fade { display: block; width: 80px; height: 36px; opacity: .5; background: #123456; }"));
    RenderTreeBuilder opacity_render_builder(opacity_resolver);
    auto opacity_render_tree = opacity_render_builder.build(*opacity_document);
    LayoutEngine opacity_layout_engine(opacity_resolver, fixed_text_measure());
    auto opacity_layout_tree = opacity_layout_engine.layout(*opacity_render_tree, 160);
    LayerTreeBuilder opacity_layer_builder;
    auto opacity_layer_tree = opacity_layer_builder.build(*opacity_layout_tree);
    const LayoutBox* opacity_box = find_first_layout_by_class(*opacity_layout_tree, "fade");
    if (opacity_box != nullptr && opacity_box->node != nullptr) {
        std::vector<StyleOverride> opacity_overrides(1);
        opacity_overrides.front().node = opacity_box->node;
        opacity_overrides.front().has_opacity = true;
        LayerTreeOverrideScratch opacity_scratch;
        opacity_scratch.pending.reserve(count_layers(*opacity_layer_tree));
        bool lower_opacity = false;
        print_result("opacity_layer_override_reuse", iterations, average_microseconds(iterations, [&] {
            opacity_overrides.front().opacity = lower_opacity ? 0.25F : 0.75F;
            lower_opacity = !lower_opacity;
            if (!apply_opacity_overrides_to_layer_tree(*opacity_layer_tree,
                                                       opacity_overrides,
                                                       opacity_scratch)) {
                throw std::runtime_error("opacity layer override reuse was rejected");
            }
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

    auto typography_document = html_parser.parse(
        "<body><p class='spaced'>TelemetryStatusLabelWithoutSpaces</p>"
        "<p class='wrapped'>ABCDEFGHIJKLMNOPQRSTUVWXYZ</p></body>");
    auto plain_typography_document = html_parser.parse(
        "<body><p>TelemetryStatusLabelWithoutSpaces</p><p>ABCDEFGHIJKLMNOPQRSTUVWXYZ</p></body>");
    StyleResolver plain_typography_resolver(css_parser.parse(
        "p { margin: 0; width: 96px; font-size: 12px; line-height: 14px; }"));
    RenderTreeBuilder plain_typography_builder(plain_typography_resolver);
    auto plain_typography_render_tree = plain_typography_builder.build(*plain_typography_document);
    LayoutEngine plain_typography_layout_engine(plain_typography_resolver, fixed_text_measure());
    LayerTreeBuilderOptions plain_typography_layer_options;
    plain_typography_layer_options.text_measure = fixed_text_measure();
    LayerTreeBuilder plain_typography_layer_builder(plain_typography_layer_options);
    print_result("text_plain_layout", iterations, average_microseconds(iterations, [&] {
        auto plain_typography_layout_tree = plain_typography_layout_engine.layout(*plain_typography_render_tree, 172, 320);
        (void)plain_typography_layout_tree;
    }));
    auto plain_typography_layout_tree = plain_typography_layout_engine.layout(*plain_typography_render_tree, 172, 320);
    print_result("text_plain_layer", iterations, average_microseconds(iterations, [&] {
        auto plain_typography_layer_tree = plain_typography_layer_builder.build(*plain_typography_layout_tree);
        (void)plain_typography_layer_tree;
    }));
    StyleResolver typography_resolver(css_parser.parse(
        "p { margin: 0; width: 96px; font-size: 12px; line-height: 14px; }"
        ".spaced { letter-spacing: 1px; }"
        ".wrapped { overflow-wrap: anywhere; }"));
    RenderTreeBuilder typography_builder(typography_resolver);
    auto typography_render_tree = typography_builder.build(*typography_document);
    LayoutEngine typography_layout_engine(typography_resolver, fixed_text_measure());
    LayerTreeBuilderOptions typography_layer_options;
    typography_layer_options.text_measure = fixed_text_measure();
    LayerTreeBuilder typography_layer_builder(typography_layer_options);
    print_result("text_spacing_anywhere_layout", iterations, average_microseconds(iterations, [&] {
        auto typography_layout_tree = typography_layout_engine.layout(*typography_render_tree, 172, 320);
        (void)typography_layout_tree;
    }));
    auto typography_layout_tree = typography_layout_engine.layout(*typography_render_tree, 172, 320);
    print_result("text_spacing_anywhere_layer", iterations, average_microseconds(iterations, [&] {
        auto typography_layer_tree = typography_layer_builder.build(*typography_layout_tree);
        (void)typography_layer_tree;
    }));

    auto style_document = html_parser.parse(
        "<body><button id='pulse' class='pill'>Open</button><strong id='frame'>01</strong></body>");
    auto style_stylesheet = css_parser.parse(
        ".pill { display: block; width: 80px; height: 20px; background-color: #111111; }"
        ".pill.active { background-color: #222222; transform: scale(1.05); }"
        "strong { display: block; font-size: 10px; line-height: 12px; }");
    StyleResolver style_reuse_resolver(style_stylesheet);
    RenderTreeBuilder style_reuse_builder(style_reuse_resolver);
    auto previous_style_render_tree = style_reuse_builder.build(*style_document);
    LayoutEngine style_reuse_layout_engine(style_reuse_resolver, fixed_text_measure());
    auto style_reuse_layout_tree =
        style_reuse_layout_engine.layout(*previous_style_render_tree, 240);
    Node* style_pulse = find_first_element_by_id(*style_document, "pulse");
    Node* style_frame = find_first_element_by_id(*style_document, "frame");
    if (style_pulse != nullptr && style_frame != nullptr) {
        clear_dirty_flags(*style_document);
        style_frame->set_text_content("02");
        style_pulse->set_attribute("class", "pill active");
        auto next_style_render_tree = style_reuse_builder.build(*style_document);
        print_result("style_repaint_reuse_check", iterations, average_microseconds(iterations, [&] {
            const bool reusable = style_dirty_can_reuse_layout(*style_document,
                                                               *previous_style_render_tree,
                                                               *next_style_render_tree,
                                                               *style_reuse_layout_tree,
                                                               fixed_text_measure());
            (void)reusable;
        }));
        print_result("retained_style_apply_layout", iterations, average_microseconds(iterations, [&] {
            const bool applied = apply_render_styles_to_layout(*next_style_render_tree, *style_reuse_layout_tree);
            (void)applied;
        }));
        print_result("retained_style_layer_tree", iterations, average_microseconds(iterations, [&] {
            LayerTreeBuilder retained_layer_builder;
            MonotonicArena retained_layer_arena;
            auto retained_layer_tree = retained_layer_builder.build(*style_reuse_layout_tree, retained_layer_arena);
            (void)retained_layer_tree;
        }));
        DisplayList retained_display_list;
        print_result("retained_style_display_pipeline", iterations, average_microseconds(iterations, [&] {
            LayerTreeBuilder retained_layer_builder;
            MonotonicArena retained_layer_arena;
            auto retained_layer_tree = retained_layer_builder.build(*style_reuse_layout_tree, retained_layer_arena);
            retained_layer_builder.flatten_into(*retained_layer_tree, retained_display_list);
        }));
    }

    auto canvas = make_element("canvas");
    canvas->set_attribute("width", "120");
    canvas->set_attribute("height", "80");
    Canvas2DRegistry canvas_registry(Canvas2DPolicy{true, 1, 120 * 80, 120 * 80, 120, 80});
    canvas_registry.ensure_surface(*canvas);
    canvas_registry.set_fill_style(*canvas, "#1d9bf0");
    print_result("canvas2d_fill_rect", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        for (int index = 0; index < 24; ++index) {
            canvas_registry.fill_rect(*canvas, index * 5, 80 - (index % 10 + 1) * 7, 3, (index % 10 + 1) * 7);
        }
    }));
    canvas_registry.set_stroke_style(*canvas, "#ffffff");
    canvas_registry.set_line_width(*canvas, 2);
    print_result("canvas2d_path_stroke", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.begin_path(*canvas);
        canvas_registry.move_to(*canvas, 0, 70);
        for (int index = 1; index < 24; ++index) {
            canvas_registry.line_to(*canvas, index * 5, 70 - (index % 8) * 8);
        }
        canvas_registry.stroke(*canvas);
    }));
    print_result("canvas2d_arc_stroke", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.begin_path(*canvas);
        canvas_registry.arc(*canvas, 60.0, 40.0, 28.0, -1.57079632679, 4.18879020479, false);
        canvas_registry.stroke(*canvas);
    }));
    canvas_registry.set_fill_style(*canvas, "#2dd4bf");
    canvas_registry.set_global_alpha(*canvas, 0.65);
    print_result("canvas2d_fill_path", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.begin_path(*canvas);
        canvas_registry.move_to(*canvas, 60, 40);
        canvas_registry.arc(*canvas, 60.0, 40.0, 30.0, -1.57079632679, 2.61799387799, false);
        canvas_registry.close_path(*canvas);
        canvas_registry.fill(*canvas);
    }));
    canvas_registry.set_font(*canvas, "bold 16px system-ui");
    canvas_registry.set_fill_style(*canvas, "#ffffff");
    print_result("canvas2d_measure_text", iterations, average_microseconds(iterations, [&] {
        const Canvas2DTextMetrics metrics = canvas_registry.measure_text(*canvas, "82%");
        (void)metrics;
    }));
    print_result("canvas2d_fill_text", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.fill_text(*canvas, "82%", 42.0, 44.0);
    }));
    auto canvas_source = make_element("canvas");
    canvas_source->set_attribute("width", "24");
    canvas_source->set_attribute("height", "16");
    Canvas2DRegistry canvas_copy_registry(Canvas2DPolicy{true, 2, 120 * 80, 120 * 80 + 24 * 16, 120, 80});
    canvas_copy_registry.ensure_surface(*canvas);
    canvas_copy_registry.set_fill_style(*canvas_source, "#2dd4bf");
    canvas_copy_registry.fill_rect(*canvas_source, 0, 0, 24, 16);
    print_result("canvas2d_draw_image_scaled", iterations, average_microseconds(iterations, [&] {
        canvas_copy_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_copy_registry.draw_image(*canvas, *canvas_source, 0, 0, 24, 16, 12, 8, 96, 64);
    }));
    const std::uint32_t canvas_gradient = canvas_registry.create_linear_gradient(0.0, 0.0, 120.0, 0.0);
    canvas_registry.add_color_stop(canvas_gradient, 0.0, "#0f766e");
    canvas_registry.add_color_stop(canvas_gradient, 1.0, "#facc15");
    canvas_registry.set_fill_gradient(*canvas, canvas_gradient);
    print_result("canvas2d_linear_gradient_fill_rect", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.fill_rect(*canvas, 0, 0, 120, 80);
    }));
    const std::uint32_t canvas_radial_gradient =
        canvas_registry.create_radial_gradient(60.0, 40.0, 0.0, 60.0, 40.0, 60.0);
    canvas_registry.add_color_stop(canvas_radial_gradient, 0.0, "#0f766e");
    canvas_registry.add_color_stop(canvas_radial_gradient, 1.0, "#facc15");
    canvas_registry.set_fill_gradient(*canvas, canvas_radial_gradient);
    print_result("canvas2d_radial_gradient_fill_rect", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.fill_rect(*canvas, 0, 0, 120, 80);
    }));
    canvas_registry.set_fill_style(*canvas, "#2dd4bf");
    print_result("canvas2d_translate_fill_rect", iterations, average_microseconds(iterations, [&] {
        canvas_registry.clear_rect(*canvas, 0, 0, 120, 80);
        canvas_registry.save(*canvas);
        canvas_registry.translate(*canvas, 12.0, 8.0);
        canvas_registry.fill_rect(*canvas, 0, 0, 96, 64);
        canvas_registry.restore(*canvas);
    }));
    canvas_registry.begin_path(*canvas);
    canvas_registry.move_to(*canvas, 4, 64);
    print_result("canvas2d_quadratic_curve_stroke", iterations, average_microseconds(iterations, [&] {
        canvas_registry.begin_path(*canvas);
        canvas_registry.move_to(*canvas, 4, 64);
        for (int index = 0; index < 8; ++index) {
            canvas_registry.quadratic_curve_to(*canvas, 10.0 + index * 14.0, 10.0 + (index % 3) * 12.0,
                                               18.0 + index * 14.0, 60.0 - (index % 4) * 10.0);
        }
        canvas_registry.stroke(*canvas);
    }));
    print_result("canvas2d_bezier_curve_stroke", iterations, average_microseconds(iterations, [&] {
        canvas_registry.begin_path(*canvas);
        canvas_registry.move_to(*canvas, 4, 64);
        for (int index = 0; index < 6; ++index) {
            canvas_registry.bezier_curve_to(*canvas,
                                            8.0 + index * 18.0, 8.0 + (index % 3) * 14.0,
                                            16.0 + index * 18.0, 12.0 + (index % 2) * 20.0,
                                            22.0 + index * 18.0, 60.0 - (index % 4) * 10.0);
        }
        canvas_registry.stroke(*canvas);
    }));
    print_style_statistics(resolver.statistics());

    return 0;
}

int main(int argc, char** argv) {
    try {
        return run_render_core_microbench(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "jellyframe_render_core_microbench failed: " << error.what() << '\n';
        return 1;
    }
}
