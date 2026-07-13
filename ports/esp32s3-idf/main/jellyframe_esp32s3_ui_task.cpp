#include "jellyframe_esp32s3_ui_task.h"

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#include "jellyframe_esp32s3_input.h"
#include "jellyframe_esp32s3_resources.h"

#include "app_runtime/app_host.h"
#include "render_core/bitmap_font.h"
#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/document_style.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/frame_loop.h"
#include "render_core/frame_scratch.h"
#include "render_core/host.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/scroll_blit.h"
#include "render_core/scroll_gesture.h"
#include "render_core/text_repaint.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <vector>

#ifndef CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE
#define CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE 32768
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY
#define CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY 5
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_UI_TASK_TICK_MS
#define CONFIG_JELLYFRAME_ESP32S3_UI_TASK_TICK_MS 20
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_TIMER_UI_AUTOSTART
#define CONFIG_JELLYFRAME_ESP32S3_TIMER_UI_AUTOSTART 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_STEP_PIXELS
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_STEP_PIXELS 6
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_AUTORUN
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_AUTORUN 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_FULL
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_FULL 1
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_TEXT
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_TEXT 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CARDS
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CARDS 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_BACKGROUND
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_BACKGROUND 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CLEAR
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CLEAR 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL
#define CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL 0
#endif

#ifndef CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION
#define CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION 0
#endif

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameUi";
constexpr std::string_view kTimerUrl = "/timer.html";
constexpr std::string_view kScrollDemoUrl = "/scroll_demo.html";
constexpr std::string_view kScrollBenchFullUrl = "/scroll_bench.html";
constexpr std::string_view kScrollBenchTextUrl = "/scroll_bench_text.html";
constexpr std::string_view kScrollBenchCardsUrl = "/scroll_bench_cards.html";
constexpr std::string_view kScrollBenchBackgroundUrl = "/scroll_bench_background.html";
constexpr std::string_view kScrollBenchClearUrl = "/scroll_bench_clear.html";
constexpr std::string_view kScrollBenchPanelUrl = "/scroll_bench_panel.html";
constexpr jellyframe::Color kBackground{248, 250, 252, 255};
constexpr int kScrollIndicatorRepaintWidth = 8;

std::string_view scroll_benchmark_url() {
#if CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_TEXT
    return kScrollBenchTextUrl;
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CARDS
    return kScrollBenchCardsUrl;
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_BACKGROUND
    return kScrollBenchBackgroundUrl;
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CLEAR
    return kScrollBenchClearUrl;
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL
    return kScrollBenchPanelUrl;
#else
    return kScrollBenchFullUrl;
#endif
}

const char* scroll_benchmark_workload() {
#if CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_TEXT
    return "text";
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CARDS
    return "cards";
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_BACKGROUND
    return "background";
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_CLEAR
    return "clear";
#elif CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL
    return "panel";
#else
    return "full";
#endif
}

struct PipelineCache {
    jellyframe::RenderObjectPtr render_tree;
    jellyframe::LayoutBoxPtr layout_tree;
    jellyframe::LayerNodePtr layer_tree;
    jellyframe::MonotonicArena render_arena;
    jellyframe::MonotonicArena layout_arena;
    jellyframe::MonotonicArena layer_arena;
};

struct TimingHistogram {
    static constexpr std::uint32_t kBucketUs = 1000;
    static constexpr std::size_t kBucketCount = 128;

    std::uint32_t buckets[kBucketCount]{};
    std::uint32_t samples = 0;

    void record(std::uint32_t us) {
        const std::size_t bucket = std::min<std::size_t>(us / kBucketUs, kBucketCount - 1);
        ++buckets[bucket];
        ++samples;
    }

    std::uint32_t percentile_us(std::uint32_t percentile) const {
        if (samples == 0) {
            return 0;
        }
        const std::uint32_t rank = std::max<std::uint32_t>(1, (samples * percentile + 99) / 100);
        std::uint32_t seen = 0;
        for (std::size_t bucket = 0; bucket < kBucketCount; ++bucket) {
            seen += buckets[bucket];
            if (seen >= rank) {
                return static_cast<std::uint32_t>(bucket) * kBucketUs;
            }
        }
        return static_cast<std::uint32_t>(kBucketCount - 1) * kBucketUs;
    }
};

struct PortTelemetry {
    std::uint32_t frames = 0;
    std::uint32_t full_frames = 0;
    std::uint32_t dirty_frames = 0;
    std::uint32_t idle_frames = 0;
    std::uint32_t input_events = 0;
    std::uint32_t completion_events = 0;
    std::uint32_t flushes = 0;
    std::uint64_t packed_bytes = 0;
    std::uint64_t frame_us_total = 0;
    std::uint32_t frame_us_max = 0;
    std::uint64_t present_us_total = 0;
    std::uint32_t present_us_max = 0;
    std::uint64_t framebuffer_convert_us = 0;
    std::uint64_t scratch_copy_us = 0;
    std::uint64_t panel_convert_us = 0;
    std::uint64_t panel_window_setup_us = 0;
    std::uint64_t panel_dma_submit_us = 0;
    std::uint64_t panel_dma_wait_us = 0;
    std::uint32_t panel_dma_chunks = 0;
    std::uint32_t scroll_steps = 0;
    std::uint64_t scroll_visible_pixels = 0;
    std::uint64_t scroll_exposed_pixels = 0;
    std::uint32_t framebuffer_scroll_blits = 0;
    std::uint64_t framebuffer_scroll_blit_us = 0;
    std::uint64_t scroll_reuse_compose_us = 0;
    std::uint32_t panel_scroll_steps = 0;
    std::uint32_t panel_scroll_fallbacks = 0;
    std::uint32_t panel_scroll_wraps = 0;
    std::uint32_t panel_scroll_cpu_blits_elided = 0;
    std::uint64_t panel_scroll_setup_us = 0;
    std::uint64_t panel_scroll_recovery_compose_us = 0;
    std::uint64_t layer_build_us = 0;
    std::uint64_t compose_us = 0;
    TimingHistogram frame_histogram;
    TimingHistogram present_histogram;
    std::uint32_t min_internal_free = 0;
    std::uint32_t min_spiram_free = 0;
    std::uint32_t initial_internal_free = 0;
    std::uint32_t initial_spiram_free = 0;
    std::uint32_t initial_largest_internal = 0;
    std::uint32_t initial_largest_spiram = 0;
    std::uint32_t min_largest_internal = 0;
    std::uint32_t min_largest_spiram = 0;
};

struct TimerUiTaskContext {
    boards::BoardRuntime board_runtime;
    BoardInputQueue input_queue;
    jellyframe::HostBudgets budgets;
    jellyframe::HostDeviceCapabilities capabilities;
    jellyframe::BitmapFontContext font_context{};
    jellyframe::TextMeasureProvider text_measure{};
    jellyframe::TextPainter text_painter{};
    jellyframe::FrameScratch frame_scratch;
    jellyframe::AppFrameScratch app_scratch;
    jellyframe::SoftwareCompositor::Scratch compositor_scratch;
    std::unique_ptr<jellyframe::Node> document;
    jellyframe::Stylesheet stylesheet;
    PipelineCache pipeline;
    std::unique_ptr<jellyframe::InputController> input_controller;
    std::unique_ptr<jellyframe::FrameBuffer> frame_buffer;
    std::unique_ptr<std::uint16_t[]> packed_rgb565;
    Rgb565Panel panel;
    PortTelemetry telemetry;
    int width = 0;
    int height = 0;
    std::uint32_t elapsed_seconds = 0;
    std::uint32_t button_clicks = 0;
    bool timer_running = false;
    std::string_view document_url = kTimerUrl;
    const char* telemetry_case = "timer_ui_cumulative";
    const char* telemetry_app_id = "org.jellyframe.bringup.timer";
    const char* scroll_workload = "none";
    bool scroll_benchmark = false;
    bool scroll_autorun = false;
    jellyframe::Node* scroll_node = nullptr;
    int scroll_y = 0;
    int scroll_direction = 1;
    jellyframe::VerticalScrollGesture scroll_gesture;
    int pending_scroll_drag_delta = 0;
    bool has_explicit_dirty_rect = false;
    jellyframe::Rect explicit_dirty_rect{};
    bool has_framebuffer_scroll_blit = false;
    jellyframe::Rect framebuffer_scroll_viewport{};
    jellyframe::ScrollBlitPlan framebuffer_scroll_blit{};
};

bool panel_scroll_candidate(const TimerUiTaskContext& context, std::size_t dirty_count) {
    if (!CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION ||
        !CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL ||
        !context.scroll_benchmark || context.board_runtime.packed_scroll_flush == nullptr ||
        context.panel.packed_scroll_flush == nullptr || context.panel.reset_scroll == nullptr ||
        !context.has_framebuffer_scroll_blit || context.frame_buffer == nullptr ||
        context.pipeline.layer_tree == nullptr ||
        context.framebuffer_scroll_blit.mode != jellyframe::ScrollBlitMode::FastBlit ||
        context.framebuffer_scroll_viewport.x != 0 || context.framebuffer_scroll_viewport.y != 0 ||
        context.framebuffer_scroll_viewport.width != context.width ||
        context.framebuffer_scroll_viewport.height != context.height || dirty_count != 1) {
        return false;
    }
    const jellyframe::Rect& strip = context.framebuffer_scroll_blit.exposed_strip;
    return strip.x == 0 && strip.width == context.width &&
        strip.height > 0 && strip.height < context.height;
}

struct InputInteractionState {
    const jellyframe::Node* hovered = nullptr;
    const jellyframe::Node* active = nullptr;
    const jellyframe::Node* focused = nullptr;
};

InputInteractionState take_input_interaction_state(TimerUiTaskContext& context) {
    if (!context.input_controller) {
        return {};
    }
    const InputInteractionState state{
        context.input_controller->hovered_node(),
        context.input_controller->active_node(),
        context.input_controller->focused_node(),
    };
    context.input_controller.reset();
    return state;
}

void bind_input_controller(TimerUiTaskContext& context, InputInteractionState state) {
    if (!context.pipeline.layer_tree) {
        return;
    }
    const jellyframe::InteractionInvalidationOptions invalidation{
        false,
        false,
        false,
    };
    context.input_controller = std::make_unique<jellyframe::InputController>(
        *context.pipeline.layer_tree, invalidation);
    context.input_controller->set_interaction_state(state.hovered, state.active, state.focused);
}

jellyframe::Node* find_by_id(jellyframe::Node& root, const std::string& id) {
    std::vector<jellyframe::Node*> stack;
    stack.push_back(&root);
    while (!stack.empty()) {
        jellyframe::Node* node = stack.back();
        stack.pop_back();
        if (node->attribute("id") == id) {
            return node;
        }
        for (auto child = node->children.rbegin(); child != node->children.rend(); ++child) {
            stack.push_back(child->get());
        }
    }
    return nullptr;
}

std::string format_timer_time(std::uint32_t seconds) {
    const std::uint32_t minutes = seconds / 60U;
    const std::uint32_t remain = seconds % 60U;
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u",
                  static_cast<unsigned>(minutes % 100U),
                  static_cast<unsigned>(remain));
    return std::string(buffer);
}

jellyframe::HostBudgets make_ui_budgets(int width, int height) {
    jellyframe::HostBudgets budgets;
    budgets.max_dom_nodes = 1024;
    budgets.max_dom_depth = 48;
    budgets.max_attributes_per_element = 24;
    budgets.max_css_rules = 512;
    budgets.max_css_declarations_per_rule = 96;
    budgets.max_render_objects = 1024;
    budgets.max_layout_boxes = 1024;
    budgets.max_layers = 128;
    budgets.max_display_commands = 2048;
    budgets.max_dirty_rects = 8;
    budgets.max_timers = 24;
    budgets.max_event_listeners = 192;
    budgets.max_input_events_per_frame = 8;
    budgets.max_timer_callbacks_per_frame = 4;
    budgets.max_animation_callbacks_per_frame = 2;
    budgets.max_active_animations = 8;
    budgets.animation_frame_rate = 30;
    budgets.max_resource_bytes = 128 * 1024;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    return budgets;
}

jellyframe::HostDeviceCapabilities make_ui_capabilities(const boards::BoardProfile& board, int width, int height) {
    jellyframe::HostDeviceCapabilities capabilities;
    capabilities.display.width = width;
    capabilities.display.height = height;
    capabilities.display.preferred_pixel_format = jellyframe::HostPixelFormat::Rgb565;
    capabilities.display.supports_partial_present = true;
    capabilities.display.has_full_framebuffer = true;
    capabilities.input.pointer = true;
    capabilities.input.touch = board.display.has_touch;
    capabilities.memory.total_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    capabilities.memory.max_single_allocation_bytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    capabilities.memory.preferred_framebuffer_bytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * sizeof(std::uint16_t);
    capabilities.async.runs_jobs_off_ui_thread = false;
    capabilities.async.supports_cancel = false;
    capabilities.async.max_in_flight_jobs = 2;
    capabilities.async.max_completion_events_per_frame = 2;
    capabilities.budgets = make_ui_budgets(width, height);
    return capabilities;
}

void update_heap_telemetry(PortTelemetry& telemetry) {
    const std::uint32_t internal_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t spiram_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const std::uint32_t largest_internal =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t largest_spiram =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    telemetry.min_internal_free = telemetry.min_internal_free == 0
        ? internal_free
        : std::min(telemetry.min_internal_free, internal_free);
    telemetry.min_spiram_free = telemetry.min_spiram_free == 0
        ? spiram_free
        : std::min(telemetry.min_spiram_free, spiram_free);
    telemetry.min_largest_internal = telemetry.min_largest_internal == 0
        ? largest_internal
        : std::min(telemetry.min_largest_internal, largest_internal);
    telemetry.min_largest_spiram = telemetry.min_largest_spiram == 0
        ? largest_spiram
        : std::min(telemetry.min_largest_spiram, largest_spiram);
}

void print_telemetry(const PortTelemetry& telemetry, const TimerUiTaskContext& context) {
    const double frame_ms_avg = telemetry.frames == 0
        ? 0.0
        : static_cast<double>(telemetry.frame_us_total) / static_cast<double>(telemetry.frames) / 1000.0;
    const double present_ms_avg = telemetry.frames == 0
        ? 0.0
        : static_cast<double>(telemetry.present_us_total) / static_cast<double>(telemetry.frames) / 1000.0;
    const std::uint32_t internal_peak = telemetry.initial_internal_free > telemetry.min_internal_free
        ? telemetry.initial_internal_free - telemetry.min_internal_free
        : 0;
    const std::uint32_t psram_peak = telemetry.initial_spiram_free > telemetry.min_spiram_free
        ? telemetry.initial_spiram_free - telemetry.min_spiram_free
        : 0;
    const double flush_count = static_cast<double>(std::max<std::uint32_t>(1, telemetry.flushes));
    const double chunk_count = static_cast<double>(std::max<std::uint32_t>(1, telemetry.panel_dma_chunks));
    const double scratch_copy_ms_per_flush =
        static_cast<double>(telemetry.scratch_copy_us) / flush_count / 1000.0;
    const double framebuffer_convert_ms_per_flush =
        static_cast<double>(telemetry.framebuffer_convert_us) / flush_count / 1000.0;
    const double convert_ms_per_chunk =
        static_cast<double>(telemetry.panel_convert_us) / chunk_count / 1000.0;
    const double window_setup_ms_per_chunk =
        static_cast<double>(telemetry.panel_window_setup_us) / chunk_count / 1000.0;
    const double dma_submit_ms_per_chunk =
        static_cast<double>(telemetry.panel_dma_submit_us) / chunk_count / 1000.0;
    const double dma_wait_ms_per_chunk =
        static_cast<double>(telemetry.panel_dma_wait_us) / chunk_count / 1000.0;
    const double scroll_visible_pixels_per_step = telemetry.scroll_steps == 0
        ? 0.0
        : static_cast<double>(telemetry.scroll_visible_pixels) / static_cast<double>(telemetry.scroll_steps);
    const double scroll_exposed_pixels_per_step = telemetry.scroll_steps == 0
        ? 0.0
        : static_cast<double>(telemetry.scroll_exposed_pixels) / static_cast<double>(telemetry.scroll_steps);
    const double layer_build_ms_per_flush =
        static_cast<double>(telemetry.layer_build_us) / flush_count / 1000.0;
    const double compose_ms_per_flush =
        static_cast<double>(telemetry.compose_us) / flush_count / 1000.0;
    const double framebuffer_scroll_blit_ms_per_step = telemetry.framebuffer_scroll_blits == 0
        ? 0.0
        : static_cast<double>(telemetry.framebuffer_scroll_blit_us) /
            static_cast<double>(telemetry.framebuffer_scroll_blits) / 1000.0;
    const double scroll_reuse_compose_ms_per_step = telemetry.framebuffer_scroll_blits == 0
        ? 0.0
        : static_cast<double>(telemetry.scroll_reuse_compose_us) /
            static_cast<double>(telemetry.framebuffer_scroll_blits) / 1000.0;
    const double panel_scroll_setup_ms_per_step = telemetry.panel_scroll_steps == 0
        ? 0.0
        : static_cast<double>(telemetry.panel_scroll_setup_us) /
            static_cast<double>(telemetry.panel_scroll_steps) / 1000.0;
    const std::uint64_t measured_present_us = telemetry.framebuffer_convert_us + telemetry.scratch_copy_us +
        telemetry.panel_convert_us + telemetry.panel_window_setup_us + telemetry.panel_dma_submit_us +
        telemetry.panel_dma_wait_us;
    const std::uint64_t present_other_us = telemetry.present_us_total > measured_present_us
        ? telemetry.present_us_total - measured_present_us
        : 0;
    const double present_other_ms_per_flush = static_cast<double>(present_other_us) / flush_count / 1000.0;

    ESP_LOGI(kTag,
             "port_telemetry case=%s app=%s workload=%s frames=%u full=%u dirty=%u idle=%u input=%u completions=%u flushes=%u packed_bytes=%llu frame_ms_avg=%.2f frame_ms_p50=%.2f frame_ms_p95=%.2f frame_ms_p99=%.2f frame_ms_max=%.2f present_ms_avg=%.2f present_ms_p50=%.2f present_ms_p95=%.2f present_ms_p99=%.2f present_ms_max=%.2f layer_build_ms_total=%.2f layer_build_ms_per_flush=%.3f compose_ms_total=%.2f compose_ms_per_flush=%.3f framebuffer_scroll_blits=%u framebuffer_scroll_blit_ms_per_step=%.3f scroll_reuse_compose_ms_per_step=%.3f panel_scroll_mode=%d panel_scroll_steps=%u panel_scroll_fallbacks=%u panel_scroll_wraps=%u panel_scroll_cpu_blits_elided=%u panel_scroll_recovery_compose_ms_total=%.2f panel_scroll_setup_ms_total=%.2f panel_scroll_setup_ms_per_step=%.3f rgba8888_to_rgb565_ms_total=%.2f rgba8888_to_rgb565_ms_per_flush=%.3f scratch_copy_ms_total=%.2f scratch_copy_ms_per_flush=%.3f rgb565_convert_ms_total=%.2f rgb565_convert_ms_per_chunk=%.3f panel_window_ms_total=%.2f panel_window_ms_per_chunk=%.3f dma_submit_ms_total=%.2f dma_submit_ms_per_chunk=%.3f dma_wait_ms_total=%.2f dma_wait_ms_per_chunk=%.3f present_other_ms_total=%.2f present_other_ms_per_flush=%.3f dma_chunks=%u scroll_steps=%u scroll_visible_pixels=%llu scroll_visible_pixels_per_step=%.0f scroll_exposed_pixels=%llu scroll_exposed_pixels_per_step=%.0f internal_ram_peak=%u psram_peak=%u internal_free_min=%u psram_free_min=%u largest_internal_before=%u largest_internal_min=%u largest_psram_before=%u largest_psram_min=%u",
             context.telemetry_case,
             context.telemetry_app_id,
             context.scroll_workload,
             static_cast<unsigned>(telemetry.frames),
             static_cast<unsigned>(telemetry.full_frames),
             static_cast<unsigned>(telemetry.dirty_frames),
             static_cast<unsigned>(telemetry.idle_frames),
             static_cast<unsigned>(telemetry.input_events),
             static_cast<unsigned>(telemetry.completion_events),
             static_cast<unsigned>(telemetry.flushes),
             static_cast<unsigned long long>(telemetry.packed_bytes),
             frame_ms_avg,
             static_cast<double>(telemetry.frame_histogram.percentile_us(50)) / 1000.0,
             static_cast<double>(telemetry.frame_histogram.percentile_us(95)) / 1000.0,
             static_cast<double>(telemetry.frame_histogram.percentile_us(99)) / 1000.0,
             static_cast<double>(telemetry.frame_us_max) / 1000.0,
             present_ms_avg,
             static_cast<double>(telemetry.present_histogram.percentile_us(50)) / 1000.0,
             static_cast<double>(telemetry.present_histogram.percentile_us(95)) / 1000.0,
             static_cast<double>(telemetry.present_histogram.percentile_us(99)) / 1000.0,
             static_cast<double>(telemetry.present_us_max) / 1000.0,
             static_cast<double>(telemetry.layer_build_us) / 1000.0,
             layer_build_ms_per_flush,
             static_cast<double>(telemetry.compose_us) / 1000.0,
             compose_ms_per_flush,
             static_cast<unsigned>(telemetry.framebuffer_scroll_blits),
             framebuffer_scroll_blit_ms_per_step,
             scroll_reuse_compose_ms_per_step,
             CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION ? 1 : 0,
             static_cast<unsigned>(telemetry.panel_scroll_steps),
             static_cast<unsigned>(telemetry.panel_scroll_fallbacks),
             static_cast<unsigned>(telemetry.panel_scroll_wraps),
             static_cast<unsigned>(telemetry.panel_scroll_cpu_blits_elided),
             static_cast<double>(telemetry.panel_scroll_recovery_compose_us) / 1000.0,
             static_cast<double>(telemetry.panel_scroll_setup_us) / 1000.0,
             panel_scroll_setup_ms_per_step,
             static_cast<double>(telemetry.framebuffer_convert_us) / 1000.0,
             framebuffer_convert_ms_per_flush,
             static_cast<double>(telemetry.scratch_copy_us) / 1000.0,
             scratch_copy_ms_per_flush,
             static_cast<double>(telemetry.panel_convert_us) / 1000.0,
             convert_ms_per_chunk,
             static_cast<double>(telemetry.panel_window_setup_us) / 1000.0,
             window_setup_ms_per_chunk,
             static_cast<double>(telemetry.panel_dma_submit_us) / 1000.0,
             dma_submit_ms_per_chunk,
             static_cast<double>(telemetry.panel_dma_wait_us) / 1000.0,
             dma_wait_ms_per_chunk,
             static_cast<double>(present_other_us) / 1000.0,
             present_other_ms_per_flush,
             static_cast<unsigned>(telemetry.panel_dma_chunks),
             static_cast<unsigned>(telemetry.scroll_steps),
             static_cast<unsigned long long>(telemetry.scroll_visible_pixels),
             scroll_visible_pixels_per_step,
             static_cast<unsigned long long>(telemetry.scroll_exposed_pixels),
             scroll_exposed_pixels_per_step,
             static_cast<unsigned>(internal_peak),
             static_cast<unsigned>(psram_peak),
             static_cast<unsigned>(telemetry.min_internal_free),
             static_cast<unsigned>(telemetry.min_spiram_free),
             static_cast<unsigned>(telemetry.initial_largest_internal),
             static_cast<unsigned>(telemetry.min_largest_internal),
             static_cast<unsigned>(telemetry.initial_largest_spiram),
             static_cast<unsigned>(telemetry.min_largest_spiram));

    ESP_LOGI(kTag,
             "pipeline_arena render_used=%u render_capacity=%u layout_used=%u layout_capacity=%u layer_used=%u layer_capacity=%u clip_surface_used=%u clip_surface_capacity=%u",
             static_cast<unsigned>(context.pipeline.render_arena.used_bytes()),
             static_cast<unsigned>(context.pipeline.render_arena.capacity_bytes()),
             static_cast<unsigned>(context.pipeline.layout_arena.used_bytes()),
             static_cast<unsigned>(context.pipeline.layout_arena.capacity_bytes()),
             static_cast<unsigned>(context.pipeline.layer_arena.used_bytes()),
             static_cast<unsigned>(context.pipeline.layer_arena.capacity_bytes()),
             static_cast<unsigned>(context.compositor_scratch.rasterizer.temporary_surface.pixels.size() *
                                   sizeof(jellyframe::Color)),
             static_cast<unsigned>(context.compositor_scratch.rasterizer.temporary_surface.pixels.capacity() *
                                   sizeof(jellyframe::Color)));
}

bool load_timer_document(TimerUiTaskContext& context) {
    ResourceLoadStats stats;
    ResourceBundleContext resource_context = make_resource_context(context.budgets, context.document_url, &stats);

    std::string html;
    if (!load_resource(jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Other, context.document_url, {}},
                       html,
                       &resource_context)) {
        ESP_LOGE(kTag, "ui task failed: %s not found in resource bundle", std::string(context.document_url).c_str());
        return false;
    }

    jellyframe::HtmlParser html_parser;
    context.document = html_parser.parse(html, jellyframe::html_parser_options_from_budgets(context.budgets));
    if (!context.document) {
        ESP_LOGE(kTag, "ui task failed: document parse failed");
        return false;
    }

    const std::string css = jellyframe::combine_author_css("",
                                                           *context.document,
                                                           load_linked_stylesheet,
                                                           &resource_context);
    jellyframe::CssParser css_parser;
    context.stylesheet = css_parser.parse(css, jellyframe::css_parser_options_from_budgets(context.budgets));

    ESP_LOGI(kTag,
             "ui_task resources entry=%s html_bytes=%u css_bytes=%u loads=%u missing=%u rejected=%u",
             std::string(context.document_url).c_str(),
             static_cast<unsigned>(html.size()),
             static_cast<unsigned>(css.size()),
             static_cast<unsigned>(stats.successful_loads),
             static_cast<unsigned>(stats.missing_loads),
             static_cast<unsigned>(stats.rejected_loads));
    return true;
}

int resolve_scroll_y(const jellyframe::Node& node, int max_scroll_y, void* raw_context) {
    auto* context = static_cast<TimerUiTaskContext*>(raw_context);
    if (context == nullptr || !context->scroll_benchmark || context->scroll_node != &node) {
        return 0;
    }
    return std::max(0, std::min(context->scroll_y, max_scroll_y));
}

jellyframe::LayerTreeBuilderOptions make_layer_tree_options(const TimerUiTaskContext& context) {
    jellyframe::LayerTreeBuilderOptions options = jellyframe::layer_tree_options_from_budgets(context.budgets);
    if (context.scroll_benchmark) {
        options.scroll_resolver = jellyframe::ScrollOffsetResolver{resolve_scroll_y,
                                                                    const_cast<TimerUiTaskContext*>(&context)};
        options.paint_scroll_indicators = !CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL;
    }
    return options;
}

const jellyframe::LayerNode* find_layer_for_node(const jellyframe::LayerNode& layer,
                                                 const jellyframe::Node* node) {
    if (layer.box != nullptr && layer.box->node == node) {
        return &layer;
    }
    for (const auto& child : layer.children) {
        if (const jellyframe::LayerNode* found = find_layer_for_node(*child, node)) {
            return found;
        }
    }
    return nullptr;
}

const jellyframe::LayerNode* current_scroll_layer(TimerUiTaskContext& context) {
    if (!context.scroll_benchmark || context.pipeline.layer_tree == nullptr) {
        return nullptr;
    }
    if (context.scroll_node == nullptr) {
        context.scroll_node = find_by_id(*context.document, "feed");
    }
    return context.scroll_node != nullptr
        ? find_layer_for_node(*context.pipeline.layer_tree, context.scroll_node)
        : nullptr;
}

bool schedule_scroll_offset(TimerUiTaskContext& context, int requested_scroll_y) {
    context.has_framebuffer_scroll_blit = false;
    const jellyframe::LayerNode* layer = current_scroll_layer(context);
    if (layer == nullptr || layer->max_scroll_y <= 0) {
        return false;
    }
    const jellyframe::Rect visible_rect = layer->has_clip ? layer->clip_rect : layer->bounds;
    const jellyframe::ScrollBlitPlan strip_plan = jellyframe::plan_vertical_scroll_blit(
        visible_rect.width,
        visible_rect.height,
        visible_rect.height + layer->max_scroll_y,
        context.scroll_y,
        requested_scroll_y);
    if (strip_plan.delta_y == 0) {
        return false;
    }
    context.scroll_y = strip_plan.current_scroll_y;
    context.explicit_dirty_rect = visible_rect;
    context.has_explicit_dirty_rect = context.explicit_dirty_rect.width > 0 &&
        context.explicit_dirty_rect.height > 0;
    if (context.has_explicit_dirty_rect && strip_plan.mode == jellyframe::ScrollBlitMode::FastBlit) {
        context.framebuffer_scroll_viewport = visible_rect;
        context.framebuffer_scroll_blit = strip_plan;
        context.has_framebuffer_scroll_blit = true;
    }
    if (context.has_explicit_dirty_rect) {
        ++context.telemetry.scroll_steps;
        context.telemetry.scroll_visible_pixels += static_cast<std::uint64_t>(context.explicit_dirty_rect.width) *
            static_cast<std::uint64_t>(context.explicit_dirty_rect.height);
        context.telemetry.scroll_exposed_pixels += strip_plan.exposed_pixels;
    }
    return context.has_explicit_dirty_rect;
}

bool schedule_scroll_step(TimerUiTaskContext& context) {
    const jellyframe::LayerNode* layer = current_scroll_layer(context);
    if (layer == nullptr || layer->max_scroll_y <= 0) {
        return false;
    }
    const int step = std::max(1, CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_STEP_PIXELS);
    int next = context.scroll_y + context.scroll_direction * step;
    if (next >= layer->max_scroll_y) {
        next = layer->max_scroll_y;
        context.scroll_direction = -1;
    } else if (next <= 0) {
        next = 0;
        context.scroll_direction = 1;
    }
    return schedule_scroll_offset(context, next);
}

bool point_in_rect(int x, int y, const jellyframe::Rect& rect) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

bool observe_scroll_input(const BoardInputEvent& event, void* raw_context) {
    auto* context = static_cast<TimerUiTaskContext*>(raw_context);
    if (context == nullptr || !context->scroll_benchmark) {
        return false;
    }

    switch (event.kind) {
    case BoardInputKind::PointerDown: {
        const jellyframe::LayerNode* layer = current_scroll_layer(*context);
        const jellyframe::Rect viewport = layer != nullptr && layer->has_clip ? layer->clip_rect
                                                                             : (layer != nullptr ? layer->bounds
                                                                                                 : jellyframe::Rect{});
        if (layer != nullptr && point_in_rect(event.x, event.y, viewport)) {
            context->scroll_gesture.begin(event.y);
            ESP_LOGI(kTag, "scroll_drag phase=down x=%d y=%d scroll_y=%d", event.x, event.y, context->scroll_y);
        } else {
            context->scroll_gesture.cancel();
        }
        break;
    }
    case BoardInputKind::PointerMove: {
        const jellyframe::VerticalScrollGestureUpdate update = context->scroll_gesture.update(event.y);
        if (update.dragging_started && context->input_controller) {
            context->input_controller->clear_pointer_state();
        }
        if (update.dragging && update.delta_y != 0) {
            context->pending_scroll_drag_delta += update.delta_y;
        }
        return update.dragging;
    }
    case BoardInputKind::PointerUp: {
        const bool consumed = context->scroll_gesture.end();
        if (consumed) {
            ESP_LOGI(kTag, "scroll_drag phase=up scroll_y=%d", context->scroll_y);
        }
        return consumed;
    }
    default:
        break;
    }
    return false;
}

bool rebuild_pipeline(TimerUiTaskContext& context) {
    const InputInteractionState input_state = take_input_interaction_state(context);
    context.pipeline.render_tree.reset();
    context.pipeline.layout_tree.reset();
    context.pipeline.layer_tree.reset();
    context.pipeline.render_arena.rewind();
    context.pipeline.layout_arena.rewind();
    context.pipeline.layer_arena.rewind();

    jellyframe::StyleResolver resolver(context.stylesheet);
    jellyframe::RenderTreeBuilder render_builder(resolver,
        jellyframe::render_tree_options_from_budgets(context.budgets));
    context.pipeline.render_tree = render_builder.build(*context.document, context.pipeline.render_arena);
    if (!context.pipeline.render_tree) {
        return false;
    }

    jellyframe::LayoutEngine layout_engine(resolver,
                                           context.text_measure,
                                           jellyframe::layout_engine_options_from_budgets(context.budgets));
    context.pipeline.layout_tree =
        layout_engine.layout(*context.pipeline.render_tree, context.width, context.height, context.pipeline.layout_arena);
    if (!context.pipeline.layout_tree) {
        return false;
    }

    jellyframe::LayerTreeBuilder layer_builder(make_layer_tree_options(context));
    context.pipeline.layer_tree = layer_builder.build(*context.pipeline.layout_tree, context.pipeline.layer_arena);
    if (!context.pipeline.layer_tree) {
        return false;
    }
    bind_input_controller(context, input_state);
    return true;
}

jellyframe::FramePipelineCacheState cache_state(const TimerUiTaskContext& context) {
    jellyframe::FramePipelineCacheState state;
    state.has_render_tree = context.pipeline.render_tree != nullptr;
    state.has_layout_tree = context.pipeline.layout_tree != nullptr;
    state.has_layer_tree = context.pipeline.layer_tree != nullptr;
    state.has_framebuffer = context.frame_buffer != nullptr && !context.frame_buffer->pixels.empty();
    state.framebuffer_width = context.frame_buffer ? context.frame_buffer->width : 0;
    state.framebuffer_height = context.frame_buffer ? context.frame_buffer->height : 0;
    state.viewport = jellyframe::Rect{0, 0, context.width, context.height};
    state.content_height = context.height;
    return state;
}

const jellyframe::Rect* choose_dirty_rects(TimerUiTaskContext& context,
                                           const jellyframe::FrameUpdatePlan& update_plan,
                                           std::size_t& dirty_count) {
    static const jellyframe::Rect full_dirty_placeholder{};
    (void)full_dirty_placeholder;
    dirty_count = 0;

    if (context.has_explicit_dirty_rect) {
        context.frame_scratch.dirty_region.mode = jellyframe::DirtyRegionMode::DirtyRects;
        context.frame_scratch.dirty_region.rects.clear();
        context.frame_scratch.dirty_region.rects.push_back(context.explicit_dirty_rect);
        dirty_count = 1;
        return context.frame_scratch.dirty_region.rects.data();
    }

    if (update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::CurrentLayout ||
        update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::PreviousAndCurrentLayout) {
        const jellyframe::LayoutBox* previous_layout =
            update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::CurrentLayout
                ? context.pipeline.layout_tree.get()
                : nullptr;
        jellyframe::compute_dirty_region_into(*context.document,
                                              previous_layout,
                                              context.pipeline.layout_tree.get(),
                                              jellyframe::dirty_region_options_from_budgets(
                                                  context.budgets,
                                                  jellyframe::Rect{0, 0, context.width, context.height}),
                                              context.frame_scratch.dirty_region,
                                              &context.frame_scratch.dirty_region_scratch);
        if (context.frame_scratch.dirty_region.mode == jellyframe::DirtyRegionMode::DirtyRects &&
            !context.frame_scratch.dirty_region.rects.empty()) {
            dirty_count = context.frame_scratch.dirty_region.rects.size();
            return context.frame_scratch.dirty_region.rects.data();
        }
    }

    context.frame_scratch.dirty_region.mode = jellyframe::DirtyRegionMode::FullFrame;
    context.frame_scratch.dirty_region.rects.clear();
    context.frame_scratch.dirty_region.rects.push_back(jellyframe::Rect{0, 0, context.width, context.height});
    dirty_count = 1;
    return context.frame_scratch.dirty_region.rects.data();
}

bool render_and_present(TimerUiTaskContext& context,
                        const jellyframe::FrameUpdatePlan& update_plan,
                        std::uint32_t& present_us) {
    std::size_t dirty_count = 0;
    const jellyframe::Rect* dirty_rects = choose_dirty_rects(context, update_plan, dirty_count);
    if (dirty_rects == nullptr || dirty_count == 0) {
        return true;
    }

    if (update_plan.action == jellyframe::FrameUpdateAction::RepaintExisting &&
        context.pipeline.layout_tree != nullptr) {
        // The controller references the layer tree, so retire it before a paint-only rebuild.
        const InputInteractionState input_state = take_input_interaction_state(context);
        const std::uint64_t layer_build_start = esp_timer_get_time();
        context.pipeline.layer_tree.reset();
        context.pipeline.layer_arena.rewind();
        jellyframe::LayerTreeBuilder layer_builder(make_layer_tree_options(context));
        context.pipeline.layer_tree = layer_builder.build(*context.pipeline.layout_tree,
                                                           context.pipeline.layer_arena);
        context.telemetry.layer_build_us +=
            static_cast<std::uint64_t>(esp_timer_get_time() - layer_build_start);
        if (!context.pipeline.layer_tree) {
            return false;
        }
        bind_input_controller(context, input_state);
    }

    const jellyframe::Rect full_viewport{0, 0, context.width, context.height};
    const bool use_panel_scroll = panel_scroll_candidate(context, dirty_count);
    // A panel-scroll frame intentionally leaves most framebuffer rows stale.
    // Rebuild before a normal present can observe those rows again.
    const bool rebuild_framebuffer_for_normal_present =
        context.panel.packed_scroll_mapped && !use_panel_scroll;
    const bool can_reuse_scroll_pixels = context.has_framebuffer_scroll_blit &&
        context.frame_buffer != nullptr && context.pipeline.layer_tree != nullptr &&
        !rebuild_framebuffer_for_normal_present;
    std::array<jellyframe::Rect, 2> scroll_reuse_dirty_rects{};
    const jellyframe::Rect* compose_dirty_rects = dirty_rects;
    std::size_t compose_dirty_count = dirty_count;
    if (can_reuse_scroll_pixels) {
        bool applied = use_panel_scroll;
        if (!applied) {
            const std::uint64_t scroll_blit_start = esp_timer_get_time();
            applied = jellyframe::apply_vertical_scroll_blit(*context.frame_buffer,
                                                              context.framebuffer_scroll_viewport,
                                                              context.framebuffer_scroll_blit);
            context.telemetry.framebuffer_scroll_blit_us +=
                static_cast<std::uint64_t>(esp_timer_get_time() - scroll_blit_start);
        }
        if (applied) {
            const jellyframe::Rect& strip = context.framebuffer_scroll_blit.exposed_strip;
            scroll_reuse_dirty_rects[0] = jellyframe::Rect{
                context.framebuffer_scroll_viewport.x + strip.x,
                context.framebuffer_scroll_viewport.y + strip.y,
                strip.width,
                strip.height,
            };
            if (!CONFIG_JELLYFRAME_ESP32S3_SCROLL_BENCH_WORKLOAD_PANEL) {
                const int indicator_width = std::min(kScrollIndicatorRepaintWidth,
                                                     context.framebuffer_scroll_viewport.width);
                scroll_reuse_dirty_rects[1] = jellyframe::Rect{
                    context.framebuffer_scroll_viewport.x +
                        context.framebuffer_scroll_viewport.width - indicator_width,
                    context.framebuffer_scroll_viewport.y,
                    indicator_width,
                    context.framebuffer_scroll_viewport.height,
                };
                compose_dirty_count = 2;
            } else {
                compose_dirty_count = 1;
            }
            compose_dirty_rects = scroll_reuse_dirty_rects.data();
            if (use_panel_scroll) {
                ++context.telemetry.panel_scroll_cpu_blits_elided;
            } else {
                ++context.telemetry.framebuffer_scroll_blits;
            }
        } else {
            context.has_framebuffer_scroll_blit = false;
        }
    }

    jellyframe::SoftwareCompositor compositor(context.text_painter,
        jellyframe::software_compositor_options_from_budgets(context.budgets));
    const std::uint64_t compose_start = esp_timer_get_time();
    if (rebuild_framebuffer_for_normal_present) {
        compositor.render_into(*context.pipeline.layer_tree,
                               *context.frame_buffer,
                               kBackground,
                               &full_viewport,
                               1,
                               &context.compositor_scratch);
    } else {
        compositor.render_into(*context.pipeline.layer_tree,
                               *context.frame_buffer,
                               kBackground,
                               compose_dirty_rects,
                               compose_dirty_count,
                               &context.compositor_scratch);
    }
    const std::uint64_t compose_us = static_cast<std::uint64_t>(esp_timer_get_time() - compose_start);
    context.telemetry.compose_us += compose_us;
    if (can_reuse_scroll_pixels && compose_dirty_rects == scroll_reuse_dirty_rects.data()) {
        context.telemetry.scroll_reuse_compose_us += compose_us;
    }

    const std::uint32_t flush_before = context.panel.flush_count;
    const std::uint32_t bytes_before = context.panel.flushed_bytes;
    const std::uint64_t framebuffer_convert_before = context.panel.framebuffer_convert_us;
    const std::uint64_t copy_before = context.panel.scratch_copy_us;
    const std::uint64_t convert_before = context.panel.panel_convert_us;
    const std::uint64_t window_before = context.panel.panel_window_setup_us;
    const std::uint64_t scroll_setup_before = context.panel.panel_scroll_setup_us;
    const std::uint64_t submit_before = context.panel.panel_dma_submit_us;
    const std::uint64_t wait_before = context.panel.panel_dma_wait_us;
    const std::uint32_t chunks_before = context.panel.panel_dma_chunks;
    const std::uint32_t scroll_wraps_before = context.panel.packed_scroll_wrap_count;
    const std::uint32_t scroll_fallbacks_before = context.panel.packed_scroll_fallback_count;
    jellyframe::EmbeddedPackedRgb565Sink sink = make_packed_rgb565_sink(context.panel);
    const jellyframe::HostFrameSink frame_sink = jellyframe::embedded_packed_rgb565_sink(sink);
    const jellyframe::Rect exposed_strip = context.has_framebuffer_scroll_blit
        ? jellyframe::Rect{
              context.framebuffer_scroll_viewport.x + context.framebuffer_scroll_blit.exposed_strip.x,
              context.framebuffer_scroll_viewport.y + context.framebuffer_scroll_blit.exposed_strip.y,
              context.framebuffer_scroll_blit.exposed_strip.width,
              context.framebuffer_scroll_blit.exposed_strip.height,
          }
        : jellyframe::Rect{};
    const bool can_use_panel_scroll = use_panel_scroll && can_reuse_scroll_pixels &&
        compose_dirty_count == 1 && exposed_strip.x == 0 && exposed_strip.width == context.width &&
        exposed_strip.height > 0 && exposed_strip.height < context.height;
    bool force_full_normal_present = false;
    if (!can_use_panel_scroll && context.panel.packed_scroll_mapped) {
        if (!reset_rgb565_packed_scroll(context.panel)) {
            return false;
        }
        force_full_normal_present = true;
    }
    const std::uint64_t present_start = esp_timer_get_time();
    context.panel.framebuffer_convert_start_us = present_start;
    bool ok = true;
    if (can_use_panel_scroll) {
        ok = flush_rgb565_packed_scroll_strip(jellyframe::frame_buffer_view(*context.frame_buffer),
                                               context.panel,
                                               exposed_strip,
                                               context.framebuffer_scroll_blit.delta_y);
        if (ok) {
            ++context.telemetry.panel_scroll_steps;
        } else {
            if (!reset_rgb565_packed_scroll(context.panel)) {
                ok = false;
            } else {
                context.panel.framebuffer_convert_start_us = 0;
                const std::uint64_t recovery_compose_start = esp_timer_get_time();
                compositor.render_into(*context.pipeline.layer_tree,
                                       *context.frame_buffer,
                                       kBackground,
                                       &full_viewport,
                                       1,
                                       &context.compositor_scratch);
                const std::uint64_t recovery_compose_us =
                    static_cast<std::uint64_t>(esp_timer_get_time() - recovery_compose_start);
                context.telemetry.compose_us += recovery_compose_us;
                context.telemetry.panel_scroll_recovery_compose_us += recovery_compose_us;
                context.panel.framebuffer_convert_start_us = esp_timer_get_time();
                force_full_normal_present = true;
            }
        }
    }
    if (!can_use_panel_scroll || force_full_normal_present) {
        const jellyframe::Rect* present_dirty_rects = force_full_normal_present ? &full_viewport : dirty_rects;
        const std::size_t present_dirty_count = force_full_normal_present ? 1U : dirty_count;
        ok = jellyframe::present_frame(*context.frame_buffer,
                                       frame_sink,
                                       present_dirty_rects,
                                       present_dirty_count);
    }
    present_us = static_cast<std::uint32_t>(esp_timer_get_time() - present_start);
    context.panel.framebuffer_convert_start_us = 0;
    context.telemetry.flushes += context.panel.flush_count - flush_before;
    context.telemetry.packed_bytes += context.panel.flushed_bytes - bytes_before;
    context.telemetry.framebuffer_convert_us += context.panel.framebuffer_convert_us - framebuffer_convert_before;
    context.telemetry.scratch_copy_us += context.panel.scratch_copy_us - copy_before;
    context.telemetry.panel_convert_us += context.panel.panel_convert_us - convert_before;
    context.telemetry.panel_window_setup_us += context.panel.panel_window_setup_us - window_before;
    context.telemetry.panel_scroll_setup_us += context.panel.panel_scroll_setup_us - scroll_setup_before;
    context.telemetry.panel_dma_submit_us += context.panel.panel_dma_submit_us - submit_before;
    context.telemetry.panel_dma_wait_us += context.panel.panel_dma_wait_us - wait_before;
    context.telemetry.panel_dma_chunks += context.panel.panel_dma_chunks - chunks_before;
    context.telemetry.panel_scroll_wraps += context.panel.packed_scroll_wrap_count - scroll_wraps_before;
    context.telemetry.panel_scroll_fallbacks +=
        context.panel.packed_scroll_fallback_count - scroll_fallbacks_before;
    if (context.frame_scratch.dirty_region.mode == jellyframe::DirtyRegionMode::DirtyRects) {
        ++context.telemetry.dirty_frames;
    } else {
        ++context.telemetry.full_frames;
    }
    return ok;
}

bool prepare_buffers(TimerUiTaskContext& context) {
    context.frame_buffer = std::make_unique<jellyframe::FrameBuffer>(context.width, context.height, kBackground);
    const std::size_t pixel_count = rgb565_buffer_pixels(context.width, context.height);
    context.packed_rgb565.reset(new (std::nothrow) std::uint16_t[pixel_count]);
    if (!context.frame_buffer || !context.packed_rgb565) {
        ESP_LOGE(kTag, "timer ui buffer allocation failed: rgba_pixels=%u packed_rgb565_pixels=%u",
                 static_cast<unsigned>(static_cast<std::size_t>(context.width) * context.height),
                 static_cast<unsigned>(pixel_count));
        return false;
    }

    context.panel.width = context.width;
    context.panel.height = context.height;
    context.panel.stride_pixels = context.width;
    context.panel.packed_flush = context.board_runtime.packed_flush;
    context.panel.packed_scroll_flush = context.board_runtime.packed_scroll_flush;
    context.panel.reset_scroll = context.board_runtime.reset_scroll;
    context.panel.flush_context = context.board_runtime.flush_context;
    context.panel.packed_pixels = context.packed_rgb565.get();
    context.panel.packed_pixel_capacity = pixel_count;
    return true;
}

void update_timer_text(TimerUiTaskContext& context) {
    jellyframe::Node* time = find_by_id(*context.document, "time");
    if (time != nullptr) {
        time->set_text_content(format_timer_time(context.elapsed_seconds));
    }
}

void update_timer_state_text(TimerUiTaskContext& context) {
    jellyframe::Node* state = find_by_id(*context.document, "state");
    if (state != nullptr) {
        state->set_text_content(context.timer_running ? "STOP" : "START");
    }
}

void toggle_timer(TimerUiTaskContext& context) {
    context.timer_running = !context.timer_running;
    ++context.button_clicks;
    update_timer_state_text(context);
}

void bind_timer_events(TimerUiTaskContext& context) {
    jellyframe::Node* state = find_by_id(*context.document, "state");
    if (state != nullptr) {
        state->add_event_listener("click", [&context](jellyframe::Event&) {
            toggle_timer(context);
        });
    } else {
        ESP_LOGW(kTag, "timer ui state button is missing; start/stop interaction disabled");
    }
    update_timer_state_text(context);
}

void run_timer_ui_task(void* raw_context) {
    std::unique_ptr<TimerUiTaskContext> context(static_cast<TimerUiTaskContext*>(raw_context));
    context->board_runtime = boards::initialize_selected_board();
    const auto& board = context->board_runtime.profile;
    context->width = board.display.width > 0 ? board.display.width : CONFIG_JELLYFRAME_BENCH_VIEWPORT_WIDTH;
    context->height = board.display.height > 0 ? board.display.height : CONFIG_JELLYFRAME_BENCH_VIEWPORT_HEIGHT;
    context->capabilities = make_ui_capabilities(board, context->width, context->height);
    context->budgets = context->capabilities.budgets;
    context->font_context = make_bringup_font_context(2);
    context->text_measure = jellyframe::TextMeasureProvider{jellyframe::bitmap_font_measure_callback,
                                                            &context->font_context};
    context->text_painter = jellyframe::TextPainter{jellyframe::bitmap_font_paint_callback,
                                                    &context->font_context};
    context->frame_scratch.reserve_from_budgets(context->budgets);
    jellyframe::AppRuntimeHostOptions app_options;
    app_options.max_in_flight_jobs = context->capabilities.async.max_in_flight_jobs;
    app_options.max_completion_events_per_frame =
        context->capabilities.async.max_completion_events_per_frame;
    context->app_scratch.reserve_from_options(app_options);
    boards::attach_input_queue(context->board_runtime, &context->input_queue);

    context->telemetry.initial_internal_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    context->telemetry.initial_spiram_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    context->telemetry.initial_largest_internal =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    context->telemetry.initial_largest_spiram =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    update_heap_telemetry(context->telemetry);

    ESP_LOGI(kTag,
             "ui_task kind=%s mode=%s board=%s display=%dx%d hardware_ready=%d status=%s task_stack_free=%u",
             context->scroll_benchmark ? "scroll" : "timer",
             context->scroll_benchmark ? (context->scroll_autorun ? "autorun" : "interactive") : "interactive",
             board.name,
             context->width,
             context->height,
             context->board_runtime.hardware_display_ready ? 1 : 0,
             context->board_runtime.hardware_status != nullptr ? context->board_runtime.hardware_status : "",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    if (!load_timer_document(*context) || !prepare_buffers(*context)) {
        boards::release_board_runtime(context->board_runtime);
        vTaskDelete(nullptr);
        return;
    }
    context->timer_running = !context->scroll_benchmark && CONFIG_JELLYFRAME_ESP32S3_TIMER_UI_AUTOSTART;
    if (!context->scroll_benchmark) {
        bind_timer_events(*context);
    }

    jellyframe::FrameLoopOptions loop_options;
    loop_options.max_input_events_per_frame = context->budgets.max_input_events_per_frame;
    loop_options.max_timer_callbacks_per_frame = context->budgets.max_timer_callbacks_per_frame;
    loop_options.max_animation_callbacks_per_frame = context->budgets.max_animation_callbacks_per_frame;
    loop_options.animation_frame_rate = context->budgets.animation_frame_rate;

    bool force_first_frame = true;
    std::uint64_t next_tick_us = esp_timer_get_time();
    std::uint64_t last_telemetry_us = esp_timer_get_time();

    while (true) {
        context->frame_scratch.begin_frame();
        context->app_scratch.begin_frame();
        const std::uint64_t frame_start = esp_timer_get_time();

        jellyframe::FrameLoopPendingWork pending;
        pending.pending_input_events = context->input_queue.size();
        const bool timer_due = (!context->scroll_benchmark || context->scroll_autorun) &&
            esp_timer_get_time() >= next_tick_us;
        pending.pending_timer_callbacks = timer_due ? 1 : 0;

        if (!context->pipeline.layer_tree && !rebuild_pipeline(*context)) {
            ESP_LOGE(kTag, "ui task failed: initial pipeline build failed");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        const jellyframe::FrameLoopWorkPlan work_plan =
            jellyframe::plan_frame_loop_work(pending, loop_options);
        if (!context->input_controller) {
            ESP_LOGE(kTag, "ui task input controller is unavailable");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        context->pending_scroll_drag_delta = 0;
        const BoardInputDispatchStats input_stats =
            dispatch_input_events(context->input_queue,
                                  *context->input_controller,
                                  work_plan.input_events_to_dispatch,
                                  observe_scroll_input,
                                  context.get());
        context->telemetry.input_events += input_stats.dispatched;

        bool scroll_changed = false;
        if (context->scroll_benchmark && context->pending_scroll_drag_delta != 0) {
            scroll_changed = schedule_scroll_offset(
                *context, context->scroll_y + context->pending_scroll_drag_delta);
        }
        if (work_plan.timer_callbacks_to_pump > 0 || force_first_frame) {
            if (context->scroll_benchmark) {
                if (!force_first_frame && context->scroll_autorun && !context->scroll_gesture.active() && !scroll_changed) {
                    scroll_changed = schedule_scroll_step(*context);
                }
                if (context->scroll_autorun) {
                    next_tick_us = esp_timer_get_time() + 33333ULL;
                }
            } else {
                if (!force_first_frame && context->timer_running) {
                    ++context->elapsed_seconds;
                }
                update_timer_text(*context);
                next_tick_us = esp_timer_get_time() + 1000000ULL;
            }
        }
        if (context->scroll_benchmark && !context->scroll_gesture.active() && !scroll_changed) {
            const int inertia_delta = context->scroll_gesture.advance_inertia(true);
            if (inertia_delta != 0) {
                scroll_changed = schedule_scroll_offset(*context, context->scroll_y + inertia_delta);
            }
        }

        context->telemetry.completion_events +=
            static_cast<std::uint32_t>(context->app_scratch.accepted_completions.size());

        const jellyframe::DomDirtyFlags dirty_flags = force_first_frame
            ? jellyframe::DomDirtyTree | jellyframe::DomDirtyLayout
            : jellyframe::subtree_dirty_flags(*context->document);
        jellyframe::FrameLoopPlan frame_plan =
            jellyframe::plan_frame_loop(pending, dirty_flags, cache_state(*context), loop_options);
        if (scroll_changed) {
            frame_plan.update.action = jellyframe::FrameUpdateAction::RepaintExisting;
            frame_plan.update.dirty_rect_mode = jellyframe::FrameDirtyRectMode::CurrentLayout;
            frame_plan.update.reason = jellyframe::FrameUpdateReason::PaintOnlyDirty;
            frame_plan.update.can_reuse_render_and_layout = true;
            frame_plan.update.needs_previous_layout = false;
            frame_plan.update.needs_full_framebuffer = false;
        }
        if (frame_plan.update.action == jellyframe::FrameUpdateAction::RebuildPipeline &&
            frame_plan.update.reason == jellyframe::FrameUpdateReason::LayoutDirtyWithPreviousLayout &&
            context->pipeline.layout_tree != nullptr &&
            context->pipeline.layer_tree != nullptr &&
            jellyframe::text_dirty_can_reuse_layout(*context->document,
                                                     *context->pipeline.layout_tree,
                                                     context->text_measure)) {
            frame_plan.update.action = jellyframe::FrameUpdateAction::RepaintExisting;
            frame_plan.update.dirty_rect_mode = jellyframe::FrameDirtyRectMode::CurrentLayout;
            frame_plan.update.reason = jellyframe::FrameUpdateReason::TextDirtyStableLayout;
            frame_plan.update.can_reuse_render_and_layout = true;
            frame_plan.update.needs_previous_layout = false;
            frame_plan.update.needs_full_framebuffer = false;
        }
        if (frame_plan.update.action == jellyframe::FrameUpdateAction::None) {
            ++context->telemetry.idle_frames;
        } else if (frame_plan.update.action == jellyframe::FrameUpdateAction::RebuildPipeline) {
            if (!rebuild_pipeline(*context)) {
                ESP_LOGE(kTag, "ui task failed: pipeline rebuild failed");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        std::uint32_t present_us = 0;
        bool presented = true;
        if (frame_plan.update.action != jellyframe::FrameUpdateAction::None) {
            presented = render_and_present(*context, frame_plan.update, present_us);
            if (!presented) {
                ESP_LOGE(kTag, "ui task present failed");
            }
            jellyframe::clear_dirty_flags(*context->document);
            context->has_explicit_dirty_rect = false;
            context->has_framebuffer_scroll_blit = false;
            force_first_frame = false;
        }

        const std::uint32_t frame_us = static_cast<std::uint32_t>(esp_timer_get_time() - frame_start);
        ++context->telemetry.frames;
        context->telemetry.frame_us_total += frame_us;
        context->telemetry.frame_us_max = std::max(context->telemetry.frame_us_max, frame_us);
        context->telemetry.frame_histogram.record(frame_us);
        context->telemetry.present_us_total += present_us;
        context->telemetry.present_us_max = std::max(context->telemetry.present_us_max, present_us);
        if (present_us > 0) {
            context->telemetry.present_histogram.record(present_us);
        }
        update_heap_telemetry(context->telemetry);

        const bool suppress_autorun_frame_log = context->scroll_benchmark && context->scroll_autorun &&
            context->telemetry.frames > 1 && input_stats.dispatched == 0 && presented;
        if ((frame_plan.update.action != jellyframe::FrameUpdateAction::None || input_stats.dispatched > 0) &&
            !suppress_autorun_frame_log) {
            ESP_LOGI(kTag,
                     "ui_task_frame kind=%s frame=%u elapsed=%u running=%d clicks=%u scroll_y=%d drag=%d action=%s reason=%s dirty_mode=%s dirty_rects=%u input=%u queue_left=%u present_us=%u ok=%d stack_free=%u",
                     context->scroll_benchmark ? "scroll" : "timer",
                     static_cast<unsigned>(context->telemetry.frames),
                     static_cast<unsigned>(context->elapsed_seconds),
                     context->timer_running ? 1 : 0,
                     static_cast<unsigned>(context->button_clicks),
                     context->scroll_y,
                     context->scroll_gesture.dragging() ? 1 : 0,
                     jellyframe::frame_update_action_name(frame_plan.update.action),
                     jellyframe::frame_update_reason_name(frame_plan.update.reason),
                     jellyframe::dirty_region_mode_name(context->frame_scratch.dirty_region.mode),
                     static_cast<unsigned>(context->frame_scratch.dirty_region.rects.size()),
                     static_cast<unsigned>(input_stats.dispatched),
                     static_cast<unsigned>(context->input_queue.size()),
                     static_cast<unsigned>(present_us),
                     presented ? 1 : 0,
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        }

        const std::uint64_t now_us = esp_timer_get_time();
        if (now_us - last_telemetry_us >= 10000000ULL) {
            last_telemetry_us = now_us;
            print_telemetry(context->telemetry, *context);
        }

        context->frame_scratch.end_frame();
        context->app_scratch.end_frame();
        if (context->scroll_benchmark && context->scroll_autorun) {
            const std::uint64_t now_after_frame_us = esp_timer_get_time();
            if (now_after_frame_us < next_tick_us) {
                const std::uint64_t delay_us = next_tick_us - now_after_frame_us;
                // Round up so the fixed 30 Hz workload does not devolve into the
                // generic 20 ms polling cadence on a 1 kHz FreeRTOS tick.
                vTaskDelay(pdMS_TO_TICKS((delay_us + 999ULL) / 1000ULL));
            } else {
                taskYIELD();
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_JELLYFRAME_ESP32S3_UI_TASK_TICK_MS));
        }
    }
}

} // namespace

bool start_ui_task(TimerUiTaskContext* context, const char* task_name) {
    if (context == nullptr) {
        return false;
    }
    const BaseType_t ok = xTaskCreate(run_timer_ui_task,
                                       task_name,
                                       CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
                                       context,
                                       CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY,
                                       nullptr);
    if (ok != pdPASS) {
        delete context;
        ESP_LOGE(kTag, "UI task creation failed");
        return false;
    }
    return true;
}

bool start_timer_ui_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "timer UI task context allocation failed");
        return false;
    }
    return start_ui_task(context, "jellyframe_ui");
}

bool start_scroll_benchmark_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "scroll benchmark task context allocation failed");
        return false;
    }
    context->scroll_autorun = CONFIG_JELLYFRAME_ESP32S3_SCROLL_AUTORUN;
    context->document_url = context->scroll_autorun ? scroll_benchmark_url() : kScrollDemoUrl;
    context->telemetry_case = context->scroll_autorun ? "scroll_benchmark_cumulative" : "scroll_demo_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.scroll";
    context->scroll_workload = context->scroll_autorun ? scroll_benchmark_workload() : "interactive-full";
    context->scroll_benchmark = true;
    return start_ui_task(context, "jellyframe_scroll");
}

} // namespace jellyframe_esp32s3
