#include "core/css_parser.h"
#include "core/budget.h"
#include "core/embedded_framebuffer.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/pipeline_statistics.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace jellyframe;

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    int width = 300;
    int height = 300;
    int cards = 60;
    int iterations = 100;
    double bus_mhz = 40.0;
    double bus_efficiency = 0.85;
    double flush_overhead_us = 40.0;
};

struct VirtualPanel {
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    double bus_mhz = 40.0;
    double bus_efficiency = 0.85;
    double flush_overhead_us = 40.0;
    std::uint64_t flushes = 0;
    std::uint64_t pixels = 0;
    std::uint64_t bytes = 0;
    double virtual_us = 0.0;
};

std::string make_card_html(int count) {
    std::ostringstream html;
    html << "<!doctype html><html><body><main id='app' class='shell'>"
         << "<form id='search' class='search-box'>"
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
           ".shell { display: grid; padding: 10px; gap: 6px; }"
           "#search.search-box { display: block; width: 260px; padding: 10px; background: #ffffff; border: 1px solid #cbd5e1; }"
           ".search-input { display: block; width: 220px; padding: 7px; background: #ffffff; color: #111827; }"
           "button.primary { display: inline-block; padding: 7px; background: #2563eb; color: white; }"
           ".card.metric-card { display: block; margin: 5px; padding: 9px; background: #ffffff; border-radius: 8px; overflow: hidden; }"
           ".card strong { color: #059669; }";
}

HostBudgets make_budgets(const Options& options) {
    HostBudgets budgets;
    const auto estimated_nodes = static_cast<std::size_t>(options.cards) * 6U + 64U;
    budgets.max_dom_nodes = std::min<std::size_t>(1536, std::max<std::size_t>(512, estimated_nodes));
    budgets.max_render_objects = budgets.max_dom_nodes;
    budgets.max_layout_boxes = budgets.max_dom_nodes;
    budgets.max_dom_depth = 48;
    budgets.max_attributes_per_element = 24;
    budgets.max_css_rules = 512;
    budgets.max_css_declarations_per_rule = 96;
    budgets.max_layers = 128;
    budgets.max_display_commands = 2048;
    budgets.max_dirty_rects = 8;
    budgets.max_timers = 0;
    budgets.max_input_events_per_frame = 0;
    budgets.max_timer_callbacks_per_frame = 0;
    budgets.max_event_listeners = 0;
    budgets.max_resource_bytes = 128 * 1024;
    budgets.max_framebuffer_pixels =
        static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height);
    return budgets;
}

int parse_int_arg(char** argv, int index, int fallback) {
    return argv[index] != nullptr ? std::atoi(argv[index]) : fallback;
}

double parse_double_arg(char** argv, int index, double fallback) {
    return argv[index] != nullptr ? std::atof(argv[index]) : fallback;
}

Options parse_options(int argc, char** argv) {
    Options options;
    if (argc > 1) {
        options.width = parse_int_arg(argv, 1, options.width);
    }
    if (argc > 2) {
        options.height = parse_int_arg(argv, 2, options.height);
    }
    if (argc > 3) {
        options.cards = parse_int_arg(argv, 3, options.cards);
    }
    if (argc > 4) {
        options.iterations = parse_int_arg(argv, 4, options.iterations);
    }
    if (argc > 5) {
        options.bus_mhz = parse_double_arg(argv, 5, options.bus_mhz);
    }
    if (argc > 6) {
        options.bus_efficiency = parse_double_arg(argv, 6, options.bus_efficiency);
    }
    if (argc > 7) {
        options.flush_overhead_us = parse_double_arg(argv, 7, options.flush_overhead_us);
    }
    options.width = std::max(64, options.width);
    options.height = std::max(64, options.height);
    options.cards = std::max(1, options.cards);
    options.iterations = std::max(1, options.iterations);
    options.bus_mhz = std::max(1.0, options.bus_mhz);
    options.bus_efficiency = std::max(0.05, std::min(1.0, options.bus_efficiency));
    options.flush_overhead_us = std::max(0.0, options.flush_overhead_us);
    return options;
}

double estimate_flush_us(const VirtualPanel& panel, Rect dirty) {
    const std::uint64_t pixels = static_cast<std::uint64_t>(dirty.width) *
        static_cast<std::uint64_t>(dirty.height);
    const std::uint64_t bytes = pixels * 2U;
    const double effective_bits_per_us = panel.bus_mhz * panel.bus_efficiency;
    return panel.flush_overhead_us + (static_cast<double>(bytes) * 8.0) / effective_bits_per_us;
}

bool virtual_flush(Rect dirty, void* context) {
    if (context == nullptr || dirty.width <= 0 || dirty.height <= 0) {
        return false;
    }
    auto* panel = static_cast<VirtualPanel*>(context);
    const std::uint64_t pixels = static_cast<std::uint64_t>(dirty.width) *
        static_cast<std::uint64_t>(dirty.height);
    panel->flushes += 1U;
    panel->pixels += pixels;
    panel->bytes += pixels * 2U;
    panel->virtual_us += estimate_flush_us(*panel, dirty);
    return true;
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

void print_metric(const std::string& name, double value) {
    std::cout << std::left << std::setw(34) << name
              << std::right << std::fixed << std::setprecision(2) << value << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    const HostBudgets budgets = make_budgets(options);
    const HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
    const CssParserOptions css_options = css_parser_options_from_budgets(budgets);
    const RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
    const LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
    const LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
    const std::string html = make_card_html(options.cards);
    const std::string css = make_card_css();

    std::cout << "JellyFrame virtual board benchmark\n";
    std::cout << "viewport=" << options.width << "x" << options.height
              << " cards=" << options.cards
              << " iterations=" << options.iterations << '\n';
    std::cout << "panel=rgb565_spi bus_mhz=" << options.bus_mhz
              << " efficiency=" << options.bus_efficiency
              << " flush_overhead_us=" << options.flush_overhead_us << "\n\n";

    HtmlParser html_parser;
    CssParser css_parser;

    const double html_parse_us = average_microseconds(options.iterations, [&] {
        auto document = html_parser.parse(html, html_options);
        (void)document;
    });

    const double css_parse_us = average_microseconds(options.iterations, [&] {
        auto stylesheet = css_parser.parse(css, css_options);
        (void)stylesheet;
    });

    auto document = html_parser.parse(html, html_options);
    auto stylesheet = css_parser.parse(css, css_options);

    const double render_tree_us = average_microseconds(options.iterations, [&] {
        StyleResolver resolver(stylesheet);
        RenderTreeBuilder builder(resolver, render_options);
        MonotonicArena arena;
        auto render_tree = builder.build(*document, arena);
        (void)render_tree;
    });

    StyleResolver resolver(stylesheet);
    RenderTreeBuilder builder(resolver, render_options);
    MonotonicArena render_tree_arena;
    auto render_tree = builder.build(*document, render_tree_arena);

    const double layout_us = average_microseconds(options.iterations, [&] {
        LayoutEngine layout_engine(resolver, {}, layout_options);
        MonotonicArena layout_arena;
        auto layout_tree = layout_engine.layout(*render_tree, options.width, layout_arena);
        (void)layout_tree;
    });

    LayoutEngine layout_engine(resolver, {}, layout_options);
    MonotonicArena layout_arena;
    auto layout_tree = layout_engine.layout(*render_tree, options.width, layout_arena);

    const double layer_tree_us = average_microseconds(options.iterations, [&] {
        LayerTreeBuilder layer_tree_builder(layer_options);
        MonotonicArena layer_arena;
        auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);
        (void)layer_tree;
    });

    LayerTreeBuilder layer_tree_builder(layer_options);
    MonotonicArena layer_arena;
    auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);

    const double flatten_layers_us = average_microseconds(options.iterations, [&] {
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        (void)display_list;
    });

    constexpr Color background{248, 250, 252, 255};
    SoftwareCompositor compositor;
    FrameBuffer frame_buffer(options.width, options.height, background);
    const double render_frame_us = average_microseconds(options.iterations, [&] {
        compositor.render_into(*layer_tree, frame_buffer, background);
    });
    DisplayList final_display_list = layer_tree_builder.flatten(*layer_tree);
    const PipelineStatistics pipeline_statistics = collect_pipeline_statistics(PipelineStatisticsInput{
        document.get(),
        render_tree.get(),
        layout_tree.get(),
        layer_tree.get(),
        &final_display_list,
        &frame_buffer,
        &render_tree_arena,
        &layout_arena,
        &layer_arena,
        html.size() + css.size(),
    });

    auto rgb565 = std::make_unique<std::uint16_t[]>(
        static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height));
    VirtualPanel panel;
    panel.width = options.width;
    panel.height = options.height;
    panel.stride_pixels = options.width;
    panel.bus_mhz = options.bus_mhz;
    panel.bus_efficiency = options.bus_efficiency;
    panel.flush_overhead_us = options.flush_overhead_us;

    EmbeddedFrameBufferSink embedded_sink{
        EmbeddedFrameBufferTarget{
            options.width,
            options.height,
            EmbeddedPixelFormat::Rgb565,
            reinterpret_cast<std::uint8_t*>(rgb565.get()),
            static_cast<std::size_t>(options.width) * static_cast<std::size_t>(options.height) * 2U,
            static_cast<std::size_t>(options.width) * 2U,
        },
        virtual_flush,
        &panel,
    };
    const HostFrameSink frame_sink = embedded_frame_sink(embedded_sink);
    const Rect full_dirty{0, 0, options.width, options.height};

    const double present_rgb565_us = average_microseconds(options.iterations, [&] {
        panel.flushes = 0;
        panel.pixels = 0;
        panel.bytes = 0;
        panel.virtual_us = 0.0;
        if (!present_frame(frame_buffer, frame_sink, &full_dirty, 1)) {
            std::cerr << "present_frame failed\n";
            std::exit(2);
        }
    });
    const double virtual_flush_us = panel.virtual_us;
    const std::uint64_t last_flush_bytes = panel.bytes;

    const double full_pipeline_us = average_microseconds(options.iterations, [&] {
        auto local_document = html_parser.parse(html, html_options);
        auto local_stylesheet = css_parser.parse(css, css_options);
        StyleResolver local_resolver(local_stylesheet);
        RenderTreeBuilder local_builder(local_resolver, render_options);
        MonotonicArena local_render_tree_arena;
        auto local_render_tree = local_builder.build(*local_document, local_render_tree_arena);
        LayoutEngine local_layout(local_resolver, {}, layout_options);
        MonotonicArena local_layout_arena;
        auto local_layout_tree = local_layout.layout(*local_render_tree, options.width, local_layout_arena);
        LayerTreeBuilder local_layer_tree_builder(layer_options);
        MonotonicArena local_layer_arena;
        auto local_layer_tree = local_layer_tree_builder.build(*local_layout_tree, local_layer_arena);
        DisplayList display_list = local_layer_tree_builder.flatten(*local_layer_tree);
        (void)display_list;
    });

    const double steady_frame_estimate_us = render_frame_us + present_rgb565_us + virtual_flush_us;
    const double cold_pipeline_frame_estimate_us =
        full_pipeline_us + render_frame_us + present_rgb565_us + virtual_flush_us;

    print_metric("html_parse_cpu_avg_us", html_parse_us);
    print_metric("css_parse_cpu_avg_us", css_parse_us);
    print_metric("render_tree_cpu_avg_us", render_tree_us);
    print_metric("layout_cpu_avg_us", layout_us);
    print_metric("layer_tree_cpu_avg_us", layer_tree_us);
    print_metric("flatten_layers_cpu_avg_us", flatten_layers_us);
    print_metric("render_frame_cpu_avg_us", render_frame_us);
    print_metric("present_rgb565_cpu_avg_us", present_rgb565_us);
    print_metric("virtual_flush_avg_us", virtual_flush_us);
    print_metric("full_pipeline_cpu_avg_us", full_pipeline_us);
    print_metric("steady_frame_estimate_us", steady_frame_estimate_us);
    print_metric("steady_frame_estimate_fps", 1000000.0 / steady_frame_estimate_us);
    print_metric("cold_pipeline_frame_estimate_us", cold_pipeline_frame_estimate_us);
    print_metric("cold_pipeline_frame_estimate_fps", 1000000.0 / cold_pipeline_frame_estimate_us);
    std::cout << "last_flush_bytes=" << last_flush_bytes
              << " last_flush_pixels=" << panel.pixels
              << " last_flushes=" << panel.flushes << '\n';
    std::cout << "pipeline_estimated_bytes=" << pipeline_statistics.estimated_heap_bytes
              << " framebuffer_bytes=" << pipeline_statistics.framebuffer_bytes
              << " resource_bytes=" << pipeline_statistics.resource_bytes
              << " render_arena_bytes=" << pipeline_statistics.render_arena.used_bytes
              << " layout_arena_bytes=" << pipeline_statistics.layout_arena.used_bytes
              << " layer_arena_bytes=" << pipeline_statistics.layer_arena.used_bytes << '\n';

    return 0;
}
