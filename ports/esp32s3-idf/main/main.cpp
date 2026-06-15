#include "jellyframe_esp32s3_hal.h"
#include "jellyframe_esp32s3_resources.h"

#include "core/budget.h"
#include "core/css_parser.h"
#include "core/document_script.h"
#include "core/document_style.h"
#include "core/embedded_framebuffer.h"
#include "core/host.h"
#include "core/html_parser.h"
#include "core/layer_tree.h"
#include "core/layout.h"
#include "core/render_tree.h"
#include "core/software_renderer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace jellyframe;

namespace {

constexpr const char* tag = "JellyFrame";
constexpr Color kBackground{248, 250, 252, 255};
constexpr std::string_view kAppBaseUrl = "/app/index.html";

std::string make_card_html(int count) {
    std::ostringstream html;
    html << "<!doctype html><html><head>"
         << "<link rel='stylesheet' href='styles/benchmark.css'>"
         << "<script src='scripts/benchmark.js'></script>"
         << "<style>.metric-card h2 { margin: 0; }</style>"
         << "</head><body><main id='app' class='shell'>"
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
           ".shell { display: grid; padding: 8px; gap: 4px; }"
           "#search.search-box { display: block; width: 220px; padding: 8px; background: #ffffff; border: 1px solid #cbd5e1; }"
           ".search-input { display: block; width: 180px; padding: 6px; background: #ffffff; color: #111827; }"
           "button.primary { display: inline-block; padding: 6px; background: #2563eb; color: white; }"
           ".card.metric-card { display: block; margin: 4px; padding: 8px; background: #ffffff; border-radius: 8px; overflow: hidden; }"
           ".card strong { color: #059669; }";
}

std::size_t script_bytes(const std::vector<DocumentScript>& scripts) {
    std::size_t total = 0;
    for (const DocumentScript& script : scripts) {
        total += script.source.size();
    }
    return total;
}

std::size_t external_script_count(const std::vector<DocumentScript>& scripts) {
    std::size_t total = 0;
    for (const DocumentScript& script : scripts) {
        if (script.external) {
            ++total;
        }
    }
    return total;
}

struct StridedFlushProbe {
    std::uint32_t calls = 0;
    std::uint32_t pixels = 0;
    std::uint32_t bytes = 0;
    int last_stride = 0;
};

struct PackedFlushProbe {
    std::uint32_t calls = 0;
    std::uint32_t pixels = 0;
    std::uint32_t bytes = 0;
    Rect last_dirty{};
};

bool probe_strided_flush(const std::uint16_t* pixels,
                         int width,
                         int height,
                         int stride_pixels,
                         Rect dirty_rect,
                         void* context) {
    if (pixels == nullptr || width <= 0 || height <= 0 || stride_pixels < width || context == nullptr) {
        return false;
    }
    auto* probe = static_cast<StridedFlushProbe*>(context);
    ++probe->calls;
    const std::uint32_t dirty_pixels = dirty_rect.width > 0 && dirty_rect.height > 0
        ? static_cast<std::uint32_t>(dirty_rect.width) * static_cast<std::uint32_t>(dirty_rect.height)
        : 0;
    probe->pixels += dirty_pixels;
    probe->bytes += dirty_pixels * sizeof(std::uint16_t);
    probe->last_stride = stride_pixels;
    return true;
}

bool probe_packed_flush(const std::uint16_t* pixels,
                        Rect dirty_rect,
                        void* context) {
    if (pixels == nullptr || dirty_rect.width <= 0 || dirty_rect.height <= 0 || context == nullptr) {
        return false;
    }
    auto* probe = static_cast<PackedFlushProbe*>(context);
    ++probe->calls;
    const std::uint32_t dirty_pixels =
        static_cast<std::uint32_t>(dirty_rect.width) * static_cast<std::uint32_t>(dirty_rect.height);
    probe->pixels += dirty_pixels;
    probe->bytes += dirty_pixels * sizeof(std::uint16_t);
    probe->last_dirty = dirty_rect;
    return true;
}

HostBudgets make_budgets(int width, int height, int cards) {
    HostBudgets budgets;
    const auto estimated_nodes = static_cast<std::size_t>(cards) * 6U + 64U;
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
    budgets.max_event_listeners = 0;
    budgets.max_resource_bytes = 128 * 1024;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return budgets;
}

HostDeviceCapabilities make_device_capabilities(int width, int height, int cards) {
    HostDeviceCapabilities capabilities;
    capabilities.display.width = width;
    capabilities.display.height = height;
    capabilities.display.preferred_pixel_format = HostPixelFormat::Rgb565;
    capabilities.display.supports_partial_present = true;
    capabilities.display.has_full_framebuffer = true;
    capabilities.input.pointer = true;
    capabilities.input.touch = true;
    capabilities.memory.total_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    capabilities.memory.max_single_allocation_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    capabilities.memory.preferred_framebuffer_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(std::uint16_t);
    capabilities.budgets = make_budgets(width, height, cards);
    return capabilities;
}

void print_heap(const char* label) {
    ESP_LOGI(tag,
             "%s heap_free=%u heap_min=%u largest=%u internal_free=%u spiram_free=%u",
             label,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void print_budgets(const HostBudgets& budgets) {
    ESP_LOGI(tag,
             "budgets dom_nodes=%u css_rules=%u render_objects=%u layout_boxes=%u layers=%u display_commands=%u dirty_rects=%u resource_bytes=%u framebuffer_pixels=%u",
             static_cast<unsigned>(budgets.max_dom_nodes),
             static_cast<unsigned>(budgets.max_css_rules),
             static_cast<unsigned>(budgets.max_render_objects),
             static_cast<unsigned>(budgets.max_layout_boxes),
             static_cast<unsigned>(budgets.max_layers),
             static_cast<unsigned>(budgets.max_display_commands),
             static_cast<unsigned>(budgets.max_dirty_rects),
             static_cast<unsigned>(budgets.max_resource_bytes),
             static_cast<unsigned>(budgets.max_framebuffer_pixels));
}

void print_capabilities(const HostDeviceCapabilities& capabilities) {
    ESP_LOGI(tag,
             "device display=%dx%d pixel_format=rgb565 partial=%d heap=%u largest=%u framebuffer_bytes=%u",
             capabilities.display.width,
             capabilities.display.height,
             capabilities.display.supports_partial_present ? 1 : 0,
             static_cast<unsigned>(capabilities.memory.total_heap_bytes),
             static_cast<unsigned>(capabilities.memory.max_single_allocation_bytes),
             static_cast<unsigned>(capabilities.memory.preferred_framebuffer_bytes));
}

bool run_p2_resource_smoke(const HostBudgets& budgets) {
    jellyframe_esp32s3::ResourceLoadStats stats;
    jellyframe_esp32s3::ResourceBundleContext resource_context =
        jellyframe_esp32s3::make_resource_context(budgets, "/app/p2_smoke.html", &stats);

    std::string html;
    const bool loaded_html = jellyframe_esp32s3::load_resource(
        HostResourceRequest{HostResourceKind::Other, "/app/p2_smoke.html", {}},
        html,
        &resource_context);
    if (!loaded_html) {
        ESP_LOGE(tag, "p2_resource_smoke failed: document resource missing");
        return false;
    }

    HtmlParser html_parser;
    CssParser css_parser;
    auto document = html_parser.parse(html, html_parser_options_from_budgets(budgets));
    if (!document) {
        ESP_LOGE(tag, "p2_resource_smoke failed: document parse failed");
        return false;
    }

    const std::string css = combine_author_css(
        "",
        *document,
        jellyframe_esp32s3::load_linked_stylesheet,
        &resource_context);
    const Stylesheet stylesheet = css_parser.parse(css, css_parser_options_from_budgets(budgets));
    const std::vector<DocumentScript> scripts = collect_classic_scripts(
        *document,
        jellyframe_esp32s3::load_classic_script,
        &resource_context);

    jellyframe_esp32s3::ResourceLoadStats reject_stats;
    HostBudgets tiny_budget = budgets;
    tiny_budget.max_resource_bytes = 8;
    jellyframe_esp32s3::ResourceBundleContext tiny_context =
        jellyframe_esp32s3::make_resource_context(tiny_budget, "/app/p2_smoke.html", &reject_stats);
    std::string rejected_output;
    const bool oversized_blocked = !jellyframe_esp32s3::load_resource(
        HostResourceRequest{HostResourceKind::Stylesheet, "styles/benchmark.css", "/app/p2_smoke.html"},
        rejected_output,
        &tiny_context);

    ESP_LOGI(tag,
             "p2_resource_smoke html_bytes=%u css_bytes=%u css_rules=%u scripts=%u external_scripts=%u script_bytes=%u loads=%u missing=%u rejected=%u oversized_blocked=%d",
             static_cast<unsigned>(html.size()),
             static_cast<unsigned>(css.size()),
             static_cast<unsigned>(stylesheet.size()),
             static_cast<unsigned>(scripts.size()),
             static_cast<unsigned>(external_script_count(scripts)),
             static_cast<unsigned>(script_bytes(scripts)),
             static_cast<unsigned>(stats.successful_loads),
             static_cast<unsigned>(stats.missing_loads),
             static_cast<unsigned>(stats.rejected_loads + reject_stats.rejected_loads),
             oversized_blocked ? 1 : 0);

    return !css.empty() && !scripts.empty() && oversized_blocked;
}

template <typename Fn>
double average_microseconds(const jellyframe::HostClock& clock, int iterations, Fn fn) {
    const std::uint64_t start_us = esp_timer_get_time();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    const std::uint64_t end_us = esp_timer_get_time();
    (void)clock;
    return static_cast<double>(end_us - start_us) / static_cast<double>(iterations);
}

void print_result(const char* name, int iterations, double average_us) {
    ESP_LOGI(tag, "%s iterations=%d avg_us=%.2f", name, iterations, average_us);
}

bool enough_framebuffer_memory(int width, int height) {
    const std::size_t rgba_bytes = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * sizeof(Color);
    const std::size_t rgb565_bytes = static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * sizeof(std::uint16_t);
    const std::size_t required = rgba_bytes + rgb565_bytes;
    const std::size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const std::size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const bool has_room = free_bytes > required + 64 * 1024U && largest > rgba_bytes + 16 * 1024U;
    if (!has_room) {
        ESP_LOGW(tag,
                 "skip framebuffer benchmark: required=%u rgba=%u rgb565=%u heap_free=%u largest=%u",
                 static_cast<unsigned>(required),
                 static_cast<unsigned>(rgba_bytes),
                 static_cast<unsigned>(rgb565_bytes),
                 static_cast<unsigned>(free_bytes),
                 static_cast<unsigned>(largest));
    }
    return has_room;
}

bool enough_pipeline_scratch_memory() {
    const std::size_t free_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const std::size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const bool has_room = free_bytes > 128 * 1024U && largest > 64 * 1024U;
    if (!has_room) {
        ESP_LOGW(tag,
                 "skip full_pipeline benchmark: heap_free=%u largest=%u",
                 static_cast<unsigned>(free_bytes),
                 static_cast<unsigned>(largest));
    }
    return has_room;
}

void run_p3_display_smoke(const FrameBuffer& frame_buffer, int width, int height) {
    const Rect full_dirty{0, 0, width, height};
    StridedFlushProbe full_probe;
    jellyframe_esp32s3::Rgb565Panel full_panel;
    bool full_ok = false;
    {
        const std::size_t full_pixels = jellyframe_esp32s3::rgb565_buffer_pixels(width, height);
        std::unique_ptr<std::uint16_t[]> full_rgb565(new (std::nothrow) std::uint16_t[full_pixels]);
        if (full_rgb565) {
            full_panel.pixels = full_rgb565.get();
            full_panel.width = width;
            full_panel.height = height;
            full_panel.stride_pixels = width;
            full_panel.flush = probe_strided_flush;
            full_panel.flush_context = &full_probe;
            EmbeddedFrameBufferSink full_sink = jellyframe_esp32s3::make_rgb565_sink(full_panel);
            const HostFrameSink full_frame_sink = embedded_frame_sink(full_sink);
            full_ok = present_frame(frame_buffer, full_frame_sink, &full_dirty, 1);
        } else {
            ++full_panel.failed_flush_count;
        }
    }

    const int padded_stride = width + 8;
    const Rect partial_dirty{
        width / 4,
        height / 4,
        width / 3,
        height / 3,
    };
    PackedFlushProbe packed_probe;
    jellyframe_esp32s3::Rgb565Panel packed_panel;
    bool partial_ok = false;
    {
        const std::size_t partial_pixels =
            jellyframe_esp32s3::rgb565_buffer_pixels(width, height, padded_stride);
        const std::size_t scratch_pixels =
            static_cast<std::size_t>(partial_dirty.width) * static_cast<std::size_t>(partial_dirty.height);
        std::unique_ptr<std::uint16_t[]> partial_rgb565(new (std::nothrow) std::uint16_t[partial_pixels]);
        std::unique_ptr<std::uint16_t[]> scratch(new (std::nothrow) std::uint16_t[scratch_pixels]);
        if (partial_rgb565 && scratch) {
            packed_panel.pixels = partial_rgb565.get();
            packed_panel.width = width;
            packed_panel.height = height;
            packed_panel.stride_pixels = padded_stride;
            packed_panel.packed_flush = probe_packed_flush;
            packed_panel.flush_context = &packed_probe;
            packed_panel.scratch_pixels = scratch.get();
            packed_panel.scratch_pixel_capacity = scratch_pixels;
            EmbeddedFrameBufferSink packed_sink = jellyframe_esp32s3::make_rgb565_sink(packed_panel);
            const HostFrameSink packed_frame_sink = embedded_frame_sink(packed_sink);
            partial_ok = present_frame(frame_buffer, packed_frame_sink, &partial_dirty, 1);
        } else {
            ++packed_panel.failed_flush_count;
        }
    }

    ESP_LOGI(tag,
             "p3_display_smoke full_ok=%d full_flushes=%u full_pixels=%u full_bytes=%u full_stride=%d partial_ok=%d partial_flushes=%u partial_pixels=%u partial_bytes=%u packed_flushes=%u scratch_flushes=%u failed_flushes=%u last_dirty=%d,%d %dx%d",
             full_ok ? 1 : 0,
             static_cast<unsigned>(full_panel.flush_count),
             static_cast<unsigned>(full_panel.flushed_pixels),
             static_cast<unsigned>(full_panel.flushed_bytes),
             full_probe.last_stride,
             partial_ok ? 1 : 0,
             static_cast<unsigned>(packed_panel.flush_count),
             static_cast<unsigned>(packed_panel.flushed_pixels),
             static_cast<unsigned>(packed_panel.flushed_bytes),
             static_cast<unsigned>(packed_panel.packed_flush_count),
             static_cast<unsigned>(packed_panel.scratch_flush_count),
             static_cast<unsigned>(packed_panel.failed_flush_count),
             packed_probe.last_dirty.x,
             packed_probe.last_dirty.y,
             packed_probe.last_dirty.width,
             packed_probe.last_dirty.height);
}

void run_benchmark() {
    const int card_count = CONFIG_JELLYFRAME_BENCH_CARD_COUNT;
    const int iterations = CONFIG_JELLYFRAME_BENCH_ITERATIONS;
    const int viewport_width = CONFIG_JELLYFRAME_BENCH_VIEWPORT_WIDTH;
    const int viewport_height = CONFIG_JELLYFRAME_BENCH_VIEWPORT_HEIGHT;

    const HostClock clock = jellyframe_esp32s3::make_clock();
    const HostDeviceCapabilities capabilities =
        make_device_capabilities(viewport_width, viewport_height, card_count);
    const HostBudgets& budgets = capabilities.budgets;
    const HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
    const CssParserOptions css_options = css_parser_options_from_budgets(budgets);
    const RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
    const LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
    const LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);

    const std::string html = make_card_html(card_count);
    jellyframe_esp32s3::ResourceLoadStats benchmark_resource_stats;
    jellyframe_esp32s3::ResourceBundleContext benchmark_resource_context =
        jellyframe_esp32s3::make_resource_context(budgets, kAppBaseUrl, &benchmark_resource_stats);

    ESP_LOGI(tag,
             "JellyFrame ESP32-S3 benchmark cards=%d iterations=%d viewport=%dx%d",
             card_count,
             iterations,
             viewport_width,
             viewport_height);
    print_capabilities(capabilities);
    print_budgets(budgets);
    if (!run_p2_resource_smoke(budgets)) {
        ESP_LOGW(tag, "p2_resource_smoke reported incomplete resource loading");
    }
    print_heap("before");

    HtmlParser html_parser;
    CssParser css_parser;
    auto resource_document = html_parser.parse(html, html_options);
    const std::string css = resource_document
        ? combine_author_css(make_card_css(),
                             *resource_document,
                             jellyframe_esp32s3::load_linked_stylesheet,
                             &benchmark_resource_context)
        : make_card_css();
    const std::vector<DocumentScript> benchmark_scripts = resource_document
        ? collect_classic_scripts(*resource_document,
                                  jellyframe_esp32s3::load_classic_script,
                                  &benchmark_resource_context)
        : std::vector<DocumentScript>{};
    ESP_LOGI(tag,
             "benchmark_resources css_bytes=%u scripts=%u external_scripts=%u script_bytes=%u loads=%u missing=%u rejected=%u",
             static_cast<unsigned>(css.size()),
             static_cast<unsigned>(benchmark_scripts.size()),
             static_cast<unsigned>(external_script_count(benchmark_scripts)),
             static_cast<unsigned>(script_bytes(benchmark_scripts)),
             static_cast<unsigned>(benchmark_resource_stats.successful_loads),
             static_cast<unsigned>(benchmark_resource_stats.missing_loads),
             static_cast<unsigned>(benchmark_resource_stats.rejected_loads));

    print_result("html_parse", iterations, average_microseconds(clock, iterations, [&] {
        auto document = html_parser.parse(html, html_options);
        (void)document;
    }));

    print_result("css_parse", iterations, average_microseconds(clock, iterations, [&] {
        auto stylesheet = css_parser.parse(css, css_options);
        (void)stylesheet;
    }));

    auto document = html_parser.parse(html, html_options);
    auto stylesheet = css_parser.parse(css, css_options);

    print_result("render_tree", iterations, average_microseconds(clock, iterations, [&] {
        StyleResolver resolver(stylesheet);
        RenderTreeBuilder builder(resolver, render_options);
        MonotonicArena arena;
        auto render_tree = builder.build(*document, arena);
        (void)render_tree;
    }));

    StyleResolver resolver(stylesheet);
    RenderTreeBuilder builder(resolver, render_options);
    MonotonicArena render_tree_arena;
    auto render_tree = builder.build(*document, render_tree_arena);

    print_result("layout", iterations, average_microseconds(clock, iterations, [&] {
        LayoutEngine layout_engine(resolver, {}, layout_options);
        MonotonicArena layout_arena;
        auto layout_tree = layout_engine.layout(*render_tree, viewport_width, layout_arena);
        (void)layout_tree;
    }));

    LayoutEngine layout_engine(resolver, {}, layout_options);
    MonotonicArena layout_arena;
    auto layout_tree = layout_engine.layout(*render_tree, viewport_width, layout_arena);

    print_result("layer_tree", iterations, average_microseconds(clock, iterations, [&] {
        LayerTreeBuilder layer_tree_builder(layer_options);
        MonotonicArena layer_arena;
        auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);
        (void)layer_tree;
    }));

    LayerTreeBuilder layer_tree_builder(layer_options);
    MonotonicArena layer_arena;
    auto layer_tree = layer_tree_builder.build(*layout_tree, layer_arena);

    print_result("flatten_layers", iterations, average_microseconds(clock, iterations, [&] {
        DisplayList display_list = layer_tree_builder.flatten(*layer_tree);
        (void)display_list;
    }));

    bool rendered_frame = false;
    double render_frame_us = 0.0;
    double present_rgb565_us = 0.0;
    if (framebuffer_size_fits_budget(viewport_width, viewport_height, budgets) &&
        enough_framebuffer_memory(viewport_width, viewport_height)) {
        SoftwareCompositor compositor;
        FrameBuffer frame_buffer(viewport_width, viewport_height, kBackground);
        render_frame_us = average_microseconds(clock, iterations, [&] {
            compositor.render_into(*layer_tree, frame_buffer, kBackground);
        });
        rendered_frame = true;
        print_result("render_frame", iterations, render_frame_us);
        run_p3_display_smoke(frame_buffer, viewport_width, viewport_height);

#if CONFIG_JELLYFRAME_BENCH_PRESENT_RGB565
        auto rgb565 = std::make_unique<std::uint16_t[]>(
            jellyframe_esp32s3::rgb565_buffer_pixels(viewport_width, viewport_height));
        jellyframe_esp32s3::Rgb565Panel panel;
        panel.pixels = rgb565.get();
        panel.width = viewport_width;
        panel.height = viewport_height;
        panel.stride_pixels = viewport_width;
        jellyframe::EmbeddedFrameBufferSink embedded_sink = jellyframe_esp32s3::make_rgb565_sink(panel);
        const HostFrameSink frame_sink = embedded_frame_sink(embedded_sink);
        const Rect full_dirty{0, 0, viewport_width, viewport_height};

        present_rgb565_us = average_microseconds(clock, iterations, [&] {
            panel.flush_count = 0;
            panel.packed_flush_count = 0;
            panel.scratch_flush_count = 0;
            panel.failed_flush_count = 0;
            panel.flushed_pixels = 0;
            panel.flushed_bytes = 0;
            panel.last_dirty_rect = {};
            const bool ok = present_frame(frame_buffer, frame_sink, &full_dirty, 1);
            if (!ok) {
                ESP_LOGE(tag, "present_rgb565 failed");
            }
        });
        print_result("present_rgb565", iterations, present_rgb565_us);
        ESP_LOGI(tag,
                 "last_present flushes=%u pixels=%u",
                 static_cast<unsigned>(panel.flush_count),
                 static_cast<unsigned>(panel.flushed_pixels));
#endif
    }

    if (enough_pipeline_scratch_memory()) {
        print_result("full_pipeline", iterations, average_microseconds(clock, iterations, [&] {
            auto local_document = html_parser.parse(html, html_options);
            auto local_stylesheet = css_parser.parse(css, css_options);
            StyleResolver local_resolver(local_stylesheet);
            RenderTreeBuilder local_builder(local_resolver, render_options);
            MonotonicArena local_render_tree_arena;
            auto local_render_tree = local_builder.build(*local_document, local_render_tree_arena);
            LayoutEngine local_layout(local_resolver, {}, layout_options);
            MonotonicArena local_layout_arena;
            auto local_layout_tree = local_layout.layout(*local_render_tree, viewport_width, local_layout_arena);
            LayerTreeBuilder local_layer_tree_builder(layer_options);
            MonotonicArena local_layer_arena;
            auto local_layer_tree = local_layer_tree_builder.build(*local_layout_tree, local_layer_arena);
            DisplayList display_list = local_layer_tree_builder.flatten(*local_layer_tree);
            (void)display_list;
        }));
    }

    if (!rendered_frame) {
        ESP_LOGW(tag, "framebuffer stages skipped; rerun on ESP32-S3 with PSRAM or reduce viewport");
    } else {
        ESP_LOGI(tag,
                 "framebuffer_summary render_frame_avg_us=%.2f present_rgb565_avg_us=%.2f",
                 render_frame_us,
                 present_rgb565_us);
    }

    print_heap("after");
}

} // namespace

extern "C" void app_main(void) {
    run_benchmark();
    ESP_LOGI(tag, "benchmark complete");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
