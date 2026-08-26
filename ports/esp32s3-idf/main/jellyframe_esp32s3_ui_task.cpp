#include "jellyframe_esp32s3_ui_task.h"

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
#include "jellyframe_esp32s3_image.h"
#endif
#include "jellyframe_esp32s3_input.h"
#include "jellyframe_esp32s3_resources.h"

#include "app_runtime/app_host.h"
#include "device_runtime_contracts/device_bundle.h"
#include "render_core/bitmap_font.h"
#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/document_style.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/frame_loop.h"
#include "render_core/frame_scratch.h"
#include "render_core/form_control.h"
#include "render_core/form_submission.h"
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
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#ifndef CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION
#define CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION 0
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
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

#ifndef CONFIG_JELLYFRAME_ESP32S3_PERSISTENT_STYLE_RESOLVER
#define CONFIG_JELLYFRAME_ESP32S3_PERSISTENT_STYLE_RESOLVER 1
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES
#define CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES 32
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_RGB565_GRADIENT_DITHER
#define CONFIG_JELLYFRAME_ESP32S3_RGB565_GRADIENT_DITHER 1
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_USE_PACKED_RGB565_SINK
#define CONFIG_JELLYFRAME_ESP32S3_USE_PACKED_RGB565_SINK 0
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

#ifndef CONFIG_JELLYFRAME_ESP32S3_RUN_POWER_ACCEPTANCE
#define CONFIG_JELLYFRAME_ESP32S3_RUN_POWER_ACCEPTANCE 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_BAND_SHELL_AUTOROUTE
#define CONFIG_JELLYFRAME_ESP32S3_BAND_SHELL_AUTOROUTE 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_RUN_IMAGE_ACCEPTANCE
#define CONFIG_JELLYFRAME_ESP32S3_RUN_IMAGE_ACCEPTANCE 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_RUN_APP_RUNTIME_RECOVERY_ACCEPTANCE
#define CONFIG_JELLYFRAME_ESP32S3_RUN_APP_RUNTIME_RECOVERY_ACCEPTANCE 0
#endif

#ifndef CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
#define CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER 0
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

#ifndef CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_FALLBACK_PROBE
#define CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_FALLBACK_PROBE 0
#endif

namespace jellyframe_esp32s3 {

struct InstalledBundleUiSession {
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t stopped = nullptr;
};

namespace {

constexpr const char* kTag = "JellyFrameUi";
constexpr std::string_view kTimerUrl = "/timer.html";
constexpr std::string_view kResourceFailureUrl = "/resource_failure.html";
constexpr std::string_view kImageAcceptanceUrl = "/image_acceptance.html";
constexpr std::string_view kBandShellUrl = "/band_shell.html";
constexpr std::string_view kFlexGridAcceptanceUrl = "/flex_grid_acceptance.html";
constexpr std::string_view kFormsAdvancedAcceptanceUrl = "/forms_advanced_acceptance.html";
constexpr std::string_view kGradientFastpathUrl = "/gradient_fastpath.html";
constexpr std::string_view kScrollDemoUrl = "/scroll_demo.html";
constexpr std::string_view kScrollBenchFullUrl = "/scroll_bench.html";
constexpr std::string_view kScrollBenchTextUrl = "/scroll_bench_text.html";
constexpr std::string_view kScrollBenchCardsUrl = "/scroll_bench_cards.html";
constexpr std::string_view kScrollBenchBackgroundUrl = "/scroll_bench_background.html";
constexpr std::string_view kScrollBenchClearUrl = "/scroll_bench_clear.html";
constexpr std::string_view kScrollBenchPanelUrl = "/scroll_bench_panel.html";
constexpr jellyframe::Color kBackground{248, 250, 252, 255};
constexpr int kScrollIndicatorRepaintWidth = 8;
constexpr std::uint32_t kBandAutorouteTransitionCount = 30;
constexpr std::uint64_t kBandAutorouteIntervalUs = 500000ULL;

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
    std::uint64_t cold_document_load_us = 0;
    std::uint64_t cold_pipeline_build_us = 0;
    std::uint32_t first_frame_us = 0;
    std::uint32_t first_present_us = 0;
    bool first_present_ok = false;
    std::uint32_t frames = 0;
    std::uint32_t full_frames = 0;
    std::uint32_t dirty_frames = 0;
    std::uint32_t idle_frames = 0;
    std::uint32_t input_events = 0;
    std::uint32_t completion_events = 0;
    std::uint32_t flushes = 0;
    std::uint32_t band_route_transitions = 0;
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
    std::uint32_t panel_scroll_probe_injections = 0;
    std::uint32_t panel_scroll_probe_recoveries = 0;
    std::uint32_t panel_scroll_probe_reentries = 0;
    std::uint64_t panel_scroll_setup_us = 0;
    std::uint64_t panel_scroll_recovery_compose_us = 0;
    std::uint64_t layer_build_us = 0;
    std::uint64_t compose_us = 0;
    std::uint32_t pipeline_rebuilds = 0;
    std::uint32_t active_frames = 0;
    std::uint64_t active_frame_us = 0;
    std::uint64_t input_dispatch_us = 0;
    std::uint64_t frame_planning_us = 0;
    std::uint64_t pipeline_rebuild_us = 0;
    std::uint64_t render_tree_build_us = 0;
    std::uint64_t layout_us = 0;
    std::uint64_t pipeline_layer_build_us = 0;
    std::uint64_t input_bind_us = 0;
    std::uint32_t screen_power_offs = 0;
    std::uint32_t screen_power_ons = 0;
    std::uint32_t screen_power_failures = 0;
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
    AppFontContext font_context{};
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    BmpImageAdapter image_adapter;
#endif
    jellyframe::TextMeasureProvider text_measure{};
    jellyframe::TextPainter text_painter{};
    jellyframe::FrameScratch frame_scratch;
    jellyframe::AppFrameScratch app_scratch;
    jellyframe::SoftwareCompositor::Scratch compositor_scratch;
    std::unique_ptr<jellyframe::Node> document;
    std::unique_ptr<jellyframe::Stylesheet> stylesheet;
    std::unique_ptr<jellyframe::StyleResolver> style_resolver;
    PipelineCache pipeline;
    std::unique_ptr<jellyframe::InputController> input_controller;
    std::unique_ptr<jellyframe::FrameBuffer> frame_buffer;
    std::unique_ptr<std::uint16_t[]> packed_rgb565;
    std::unique_ptr<std::uint16_t[]> packed_rgb565_scratch;
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
    bool band_shell = false;
    bool forms_advanced_acceptance = false;
    std::uint32_t forms_replay_cycles = 0;
    std::uint32_t forms_replay_passes = 0;
    std::uint32_t forms_submit_actions = 0;
    std::uint32_t forms_reset_actions = 0;
    std::uint64_t next_forms_replay_us = 0;
    bool image_acceptance = false;
    bool app_runtime_recovery_acceptance = false;
    bool app_runtime_recovery_complete = false;
    std::uint32_t image_acceptance_cycles = 0;
    bool gradient_fastpath_benchmark = false;
    bool scroll_benchmark = false;
    bool scroll_autorun = false;
    bool layer_tree_has_gradients = false;
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
    bool power_acceptance = false;
    bool screen_is_on = true;
    std::uint64_t next_power_transition_us = 0;
    bool panel_scroll_probe_awaiting_reentry = false;
    bool installed_bundle_app = false;
    std::string installed_app_id;
    std::uint32_t installed_generation = 0;
    std::string installed_entry_document;
    InstalledResourceSnapshot installed_resources;
    InstalledBundleUiSession* installed_session = nullptr;
};

const char* ui_task_kind(const TimerUiTaskContext& context) {
    if (context.installed_bundle_app) {
        return "installed-bundle";
    }
    if (context.scroll_benchmark) {
        return "scroll";
    }
    if (context.gradient_fastpath_benchmark) {
        return "gradient-fastpath";
    }
    if (context.image_acceptance) {
        return "image-acceptance";
    }
    if (context.app_runtime_recovery_acceptance) {
        return "app-runtime-recovery-native";
    }
    return context.band_shell ? "band-shell" : "timer";
}

const char* ui_task_mode(const TimerUiTaskContext& context) {
    if (context.gradient_fastpath_benchmark) {
        return "fixed-30hz";
    }
    if (context.scroll_benchmark) {
        return context.scroll_autorun ? "autorun" : "interactive";
    }
    return "interactive";
}

bool panel_scroll_candidate(const TimerUiTaskContext& context, std::size_t dirty_count) {
    if (!(CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION ||
          CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION) ||
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

const char* panel_scroll_backend_name(const TimerUiTaskContext& context) {
    switch (context.board_runtime.profile.id) {
    case boards::BoardId::WaveshareEsp32s3TouchLcd147:
        return "ws147-jd9853";
    case boards::BoardId::WaveshareEsp32s3TouchLcd169:
        return "ws169-st7789";
    case boards::BoardId::GenericQemu:
        return "none";
    }
    return "none";
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
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    // Advertise the same bounded image contract that this binary actually links.
    capabilities.media.supports_image_decode = true;
    capabilities.media.preferred_decoded_image_format = jellyframe::HostPixelFormat::Rgb565;
    capabilities.media.max_image_width = 96;
    capabilities.media.max_image_height = 96;
    capabilities.media.max_decoded_image_bytes = 32 * 1024;
#endif
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
    const double rebuild_count = static_cast<double>(std::max<std::uint32_t>(1, telemetry.pipeline_rebuilds));
    const std::uint64_t measured_active_us = telemetry.input_dispatch_us + telemetry.frame_planning_us +
        telemetry.pipeline_rebuild_us + telemetry.compose_us + telemetry.present_us_total;
    const std::uint64_t active_other_us = telemetry.active_frame_us > measured_active_us
        ? telemetry.active_frame_us - measured_active_us
        : 0;

    ESP_LOGI(kTag,
             "port_telemetry case=%s app=%s workload=%s panel_scroll_backend=%s rgb565_sink=%s frames=%u full=%u dirty=%u idle=%u input=%u completions=%u flushes=%u packed_bytes=%llu frame_ms_avg=%.2f frame_ms_p50=%.2f frame_ms_p95=%.2f frame_ms_p99=%.2f frame_ms_max=%.2f present_ms_avg=%.2f present_ms_p50=%.2f present_ms_p95=%.2f present_ms_p99=%.2f present_ms_max=%.2f layer_build_ms_total=%.2f layer_build_ms_per_flush=%.3f compose_ms_total=%.2f compose_ms_per_flush=%.3f framebuffer_scroll_blits=%u framebuffer_scroll_blit_ms_per_step=%.3f scroll_reuse_compose_ms_per_step=%.3f panel_scroll_mode=%d panel_scroll_steps=%u panel_scroll_fallbacks=%u panel_scroll_wraps=%u panel_scroll_cpu_blits_elided=%u panel_scroll_recovery_compose_ms_total=%.2f panel_scroll_setup_ms_total=%.2f panel_scroll_setup_ms_per_step=%.3f rgba8888_to_rgb565_ms_total=%.2f rgba8888_to_rgb565_ms_per_flush=%.3f scratch_copy_ms_total=%.2f scratch_copy_ms_per_flush=%.3f rgb565_convert_ms_total=%.2f rgb565_convert_ms_per_chunk=%.3f panel_window_ms_total=%.2f panel_window_ms_per_chunk=%.3f dma_submit_ms_total=%.2f dma_submit_ms_per_chunk=%.3f dma_wait_ms_total=%.2f dma_wait_ms_per_chunk=%.3f present_other_ms_total=%.2f present_other_ms_per_flush=%.3f dma_chunks=%u scroll_steps=%u scroll_visible_pixels=%llu scroll_visible_pixels_per_step=%.0f scroll_exposed_pixels=%llu scroll_exposed_pixels_per_step=%.0f internal_ram_peak=%u psram_peak=%u internal_free_min=%u psram_free_min=%u largest_internal_before=%u largest_internal_min=%u largest_psram_before=%u largest_psram_min=%u screen_power_offs=%u screen_power_ons=%u screen_power_failures=%u",
             context.telemetry_case,
             context.telemetry_app_id,
             context.scroll_workload,
             panel_scroll_backend_name(context),
             CONFIG_JELLYFRAME_ESP32S3_USE_PACKED_RGB565_SINK ? "packed" : "framebuffer",
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
             (CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_ACCELERATION ||
              CONFIG_JELLYFRAME_WS169_PANEL_SCROLL_ACCELERATION) ? 1 : 0,
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
             static_cast<unsigned>(telemetry.min_largest_spiram),
             static_cast<unsigned>(telemetry.screen_power_offs),
             static_cast<unsigned>(telemetry.screen_power_ons),
             static_cast<unsigned>(telemetry.screen_power_failures));

    if (context.image_acceptance) {
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
        const auto& image = context.image_adapter.stats();
        ESP_LOGI(kTag,
                 "image_adapter requests=%u cache_hits=%u decoded=%u missing=%u corrupt=%u oversized=%u unsupported=%u budget_rejected=%u paint_calls=%u paint_failures=%u decoded_bytes=%u decode_ms=%.3f cycles=%u",
                 static_cast<unsigned>(image.requests),
                 static_cast<unsigned>(image.cache_hits),
                 static_cast<unsigned>(image.decoded),
                 static_cast<unsigned>(image.missing),
                 static_cast<unsigned>(image.corrupt),
                 static_cast<unsigned>(image.oversized),
                 static_cast<unsigned>(image.unsupported),
                 static_cast<unsigned>(image.budget_rejected),
                 static_cast<unsigned>(image.paint_calls),
                 static_cast<unsigned>(image.paint_failures),
                 static_cast<unsigned>(image.decoded_bytes),
                 static_cast<double>(image.decode_us) / 1000.0,
                 static_cast<unsigned>(context.image_acceptance_cycles));
#else
        ESP_LOGE(kTag, "image acceptance selected without the BMP adapter");
#endif
    }

    ESP_LOGI(kTag,
             "port_pipeline_telemetry active_frames=%u active_frame_ms_total=%.2f band_route_transitions=%u pipeline_rebuilds=%u input_dispatch_ms_total=%.2f input_dispatch_ms_per_active=%.3f frame_planning_ms_total=%.2f frame_planning_ms_per_active=%.3f pipeline_rebuild_ms_total=%.2f pipeline_rebuild_ms_per_rebuild=%.3f render_tree_ms_total=%.2f render_tree_ms_per_rebuild=%.3f layout_ms_total=%.2f layout_ms_per_rebuild=%.3f pipeline_layer_tree_ms_total=%.2f pipeline_layer_tree_ms_per_rebuild=%.3f input_bind_ms_total=%.2f input_bind_ms_per_rebuild=%.3f active_other_ms_total=%.2f active_other_ms_per_active=%.3f",
             static_cast<unsigned>(telemetry.active_frames),
             static_cast<double>(telemetry.active_frame_us) / 1000.0,
             static_cast<unsigned>(telemetry.band_route_transitions),
             static_cast<unsigned>(telemetry.pipeline_rebuilds),
             static_cast<double>(telemetry.input_dispatch_us) / 1000.0,
             static_cast<double>(telemetry.input_dispatch_us) /
                 static_cast<double>(std::max<std::uint32_t>(1, telemetry.active_frames)) / 1000.0,
             static_cast<double>(telemetry.frame_planning_us) / 1000.0,
             static_cast<double>(telemetry.frame_planning_us) /
                 static_cast<double>(std::max<std::uint32_t>(1, telemetry.active_frames)) / 1000.0,
             static_cast<double>(telemetry.pipeline_rebuild_us) / 1000.0,
             static_cast<double>(telemetry.pipeline_rebuild_us) / rebuild_count / 1000.0,
             static_cast<double>(telemetry.render_tree_build_us) / 1000.0,
             static_cast<double>(telemetry.render_tree_build_us) / rebuild_count / 1000.0,
             static_cast<double>(telemetry.layout_us) / 1000.0,
             static_cast<double>(telemetry.layout_us) / rebuild_count / 1000.0,
             static_cast<double>(telemetry.pipeline_layer_build_us) / 1000.0,
             static_cast<double>(telemetry.pipeline_layer_build_us) / rebuild_count / 1000.0,
             static_cast<double>(telemetry.input_bind_us) / 1000.0,
             static_cast<double>(telemetry.input_bind_us) / rebuild_count / 1000.0,
             static_cast<double>(active_other_us) / 1000.0,
             static_cast<double>(active_other_us) /
                 static_cast<double>(std::max<std::uint32_t>(1, telemetry.active_frames)) / 1000.0);

    if (CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_FALLBACK_PROBE) {
        ESP_LOGI(kTag,
                 "panel_scroll_fallback_probe injections=%u recoveries=%u reentries=%u awaiting_reentry=%d",
                 static_cast<unsigned>(telemetry.panel_scroll_probe_injections),
                 static_cast<unsigned>(telemetry.panel_scroll_probe_recoveries),
                 static_cast<unsigned>(telemetry.panel_scroll_probe_reentries),
                 context.panel_scroll_probe_awaiting_reentry ? 1 : 0);
    }

    if (context.style_resolver != nullptr) {
        const jellyframe::StyleResolverStatistics statistics = context.style_resolver->statistics();
        ESP_LOGI(kTag,
                 "style_resolver_cache persistent=1 capacity=%u entries=%u rule_refs=%u hits=%u misses=%u clears=%u bypasses=%u",
                 static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES),
                 static_cast<unsigned>(statistics.candidate_cache_entries),
                 static_cast<unsigned>(statistics.candidate_cache_rule_refs),
                 static_cast<unsigned>(statistics.candidate_cache_hits),
                 static_cast<unsigned>(statistics.candidate_cache_misses),
                 static_cast<unsigned>(statistics.candidate_cache_clears),
                 static_cast<unsigned>(statistics.candidate_cache_bypasses));
    } else {
        ESP_LOGI(kTag,
                 "style_resolver_cache persistent=0 capacity=%u entries=0 rule_refs=0 hits=0 misses=0 clears=0 bypasses=0",
                 static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES));
    }

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
    const ResourceBundle* resource_bundle = context.installed_bundle_app
        ? &context.installed_resources.bundle
        : nullptr;
    ResourceBundleContext resource_context = resource_bundle != nullptr
        ? make_resource_context(context.budgets, context.document_url, *resource_bundle, &stats)
        : make_resource_context(context.budgets, context.document_url, &stats);

    std::string html;
    if (context.installed_bundle_app) {
        html = context.installed_entry_document;
        if (html.empty()) {
            ESP_LOGE(kTag, "installed app entry is empty app=%s generation=%u",
                     context.installed_app_id.c_str(), static_cast<unsigned>(context.installed_generation));
            return false;
        }
    } else {
        if (!load_resource(jellyframe::HostResourceRequest{jellyframe::HostResourceKind::Other, context.document_url, {}},
                           html,
                           &resource_context)) {
            ESP_LOGE(kTag, "ui task failed: %s not found in resource bundle", std::string(context.document_url).c_str());
            return false;
        }
    }

    jellyframe::HtmlParser html_parser;
    context.document = html_parser.parse(html, jellyframe::html_parser_options_from_budgets(context.budgets));
    if (!context.document) {
        ESP_LOGE(kTag, "ui task failed: document parse failed");
        return false;
    }

#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    context.image_adapter.configure(context.budgets, context.document_url, resource_bundle);
#endif

    const std::string css = jellyframe::combine_author_css("",
                                                           *context.document,
                                                           load_linked_stylesheet,
                                                           &resource_context,
                                                           jellyframe::document_style_collection_options_from_budgets(context.budgets));
    const std::vector<jellyframe::DocumentScript> scripts = jellyframe::collect_classic_scripts(
        *context.document,
        load_classic_script,
        &resource_context,
        jellyframe::document_script_collection_options_from_budgets(context.budgets));
    jellyframe::CssParser css_parser;
    jellyframe::Stylesheet stylesheet = css_parser.parse(
        css, jellyframe::css_parser_options_from_budgets(context.budgets, context.width, context.height));
#if CONFIG_JELLYFRAME_ESP32S3_PERSISTENT_STYLE_RESOLVER
    jellyframe::StyleResolverOptions style_options;
    style_options.max_candidate_cache_entries =
        static_cast<std::size_t>(CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES);
    context.style_resolver = std::make_unique<jellyframe::StyleResolver>(std::move(stylesheet), style_options);
#else
    context.stylesheet = std::make_unique<jellyframe::Stylesheet>(std::move(stylesheet));
#endif

    ESP_LOGI(kTag,
             "ui_task resources entry=%s html_bytes=%u css_bytes=%u scripts=%u loads=%u missing=%u rejected=%u installed=%d generation=%u",
             context.installed_bundle_app ? context.installed_app_id.c_str() : std::string(context.document_url).c_str(),
             static_cast<unsigned>(html.size()),
             static_cast<unsigned>(css.size()),
             static_cast<unsigned>(scripts.size()),
             static_cast<unsigned>(stats.successful_loads),
             static_cast<unsigned>(stats.missing_loads),
             static_cast<unsigned>(stats.rejected_loads),
             context.installed_bundle_app ? 1 : 0,
             static_cast<unsigned>(context.installed_generation));
    return true;
}

void signal_installed_session(TimerUiTaskContext& context) {
    if (context.installed_session != nullptr && context.installed_session->stopped != nullptr) {
        xSemaphoreGive(context.installed_session->stopped);
    }
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
    options.text_measure = context.text_measure;
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    options.image_resolver = make_bmp_image_resolver(const_cast<BmpImageAdapter&>(context.image_adapter));
#endif
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

// The active layer tree is arena-owned and is retired before a paint-only
// rebuild. Keep a short-lived heap-owned copy so dirty-region calculation can
// compare transient overlay bounds across that rebuild.
jellyframe::LayerNodePtr clone_layer_tree(const jellyframe::LayerNode& source) {
    jellyframe::LayerNodePtr copy(new (std::nothrow) jellyframe::LayerNode,
                                  jellyframe::LayerNodeDeleter{false});
    if (!copy) {
        return {};
    }
    copy->type = source.type;
    copy->reasons = source.reasons;
    copy->box = source.box;
    copy->bounds = source.bounds;
    copy->clip_rect = source.clip_rect;
    copy->has_clip = source.has_clip;
    copy->opacity = source.opacity;
    copy->transform = source.transform;
    copy->transform_origin_x_percent = source.transform_origin_x_percent;
    copy->transform_origin_y_percent = source.transform_origin_y_percent;
    copy->has_transform = source.has_transform;
    copy->scroll_y = source.scroll_y;
    copy->max_scroll_y = source.max_scroll_y;
    copy->z_index = source.z_index;
    copy->source_order = source.source_order;
    copy->display_list = source.display_list;
    copy->children.reserve(source.children.size());
    for (const auto& child : source.children) {
        if (child == nullptr) {
            copy->children.emplace_back(nullptr, jellyframe::LayerNodeDeleter{false});
            continue;
        }
        jellyframe::LayerNodePtr child_copy = clone_layer_tree(*child);
        if (!child_copy) {
            return {};
        }
        copy->children.push_back(std::move(child_copy));
    }
    return copy;
}

bool layer_tree_contains_gradient(const jellyframe::LayerNode& layer) {
    for (const jellyframe::DisplayCommand& command : layer.display_list) {
        if (command.type == jellyframe::DisplayCommandType::LinearGradient ||
            command.type == jellyframe::DisplayCommandType::ConicGradient ||
            command.type == jellyframe::DisplayCommandType::RadialGradient) {
            return true;
        }
    }
    for (const auto& child : layer.children) {
        if (layer_tree_contains_gradient(*child)) {
            return true;
        }
    }
    return false;
}

void update_rgb565_gradient_dither_policy(TimerUiTaskContext& context) {
    const bool has_gradients = context.pipeline.layer_tree != nullptr &&
        layer_tree_contains_gradient(*context.pipeline.layer_tree);
    const bool ordered_dither = CONFIG_JELLYFRAME_ESP32S3_RGB565_GRADIENT_DITHER && has_gradients;
    const bool changed = context.layer_tree_has_gradients != has_gradients ||
        context.panel.ordered_dither != ordered_dither;
    context.layer_tree_has_gradients = has_gradients;
    context.panel.ordered_dither = ordered_dither;
    if (changed) {
        ESP_LOGI(kTag,
                 "rgb565_gradient_dither enabled=%d gradients=%d",
                 ordered_dither ? 1 : 0,
                 has_gradients ? 1 : 0);
    }
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

bool supports_framebuffer_scroll_blit(const jellyframe::LayerNode& layer) {
    return (layer.reasons & jellyframe::LayerReasonRoundedClip) == 0U;
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
    if (context.has_explicit_dirty_rect && strip_plan.mode == jellyframe::ScrollBlitMode::FastBlit &&
        supports_framebuffer_scroll_blit(*layer)) {
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

jellyframe::Node* find_node_by_id(jellyframe::Node& node, std::string_view id) {
    if (node.type == jellyframe::NodeType::Element && node.attribute("id") == id) {
        return &node;
    }
    for (auto& child : node.children) {
        if (child != nullptr) {
            if (jellyframe::Node* found = find_node_by_id(*child, id)) {
                return found;
            }
        }
    }
    return nullptr;
}

const jellyframe::LayoutBox* find_layout_box_for_node(const jellyframe::LayoutBox& box,
                                                       const jellyframe::Node* node) {
    if (box.node == node) {
        return &box;
    }
    for (const auto& child : box.children) {
        if (child != nullptr) {
            if (const jellyframe::LayoutBox* found = find_layout_box_for_node(*child, node)) {
                return found;
            }
        }
    }
    return nullptr;
}

void enqueue_forms_replay(TimerUiTaskContext& context) {
    if (!context.forms_advanced_acceptance || context.forms_replay_cycles >= 30) {
        return;
    }
    ++context.forms_replay_cycles;
    // Exercise the board-independent input path before the direct default-action observation.
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::FocusNext});
    BoardInputEvent text;
    text.kind = BoardInputKind::Text;
    std::snprintf(text.text, sizeof(text.text), "%u", static_cast<unsigned>(context.forms_replay_cycles));
    context.input_queue.enqueue(text);
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::Activate});
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::FocusNext});
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::Activate});

    // Exercise the core-rendered select overlay using the current layout
    // geometry. The first outside click closes it without changing the value;
    // the second open followed by the second row selects the alternate value.
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    const jellyframe::Node* select_node = find_node_by_id(*context.document, "select");
    const jellyframe::LayoutBox* select_box = context.pipeline.layout_tree != nullptr
        ? find_layout_box_for_node(*context.pipeline.layout_tree, select_node)
        : nullptr;
    if (select_box != nullptr && select_node != nullptr) {
        const jellyframe::Rect select_rect = select_box->rect;
        const int select_x = select_rect.x + std::max(1, select_rect.width / 2);
        const int select_y = select_rect.y + std::max(1, select_rect.height / 2);
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerDown, select_x, select_y});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerUp, select_x, select_y});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerDown, 2, 2});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerUp, 2, 2});

        const int row_height = std::max(20, select_box->style.line_height > 0
            ? select_box->style.line_height
            : select_box->style.font_size + std::max(6, select_box->style.font_size / 3));
        const jellyframe::SelectPopupGeometry popup = jellyframe::select_popup_geometry(
            select_rect,
            jellyframe::Rect{0, 0, context.width, context.height},
            jellyframe::form_control_option_count(*select_node),
            row_height);
        const int option_x = popup.rect.x + std::max(1, popup.rect.width / 2);
        const int option_y = popup.rect.y + popup.row_height + std::max(1, popup.row_height / 2);
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerDown, select_x, select_y});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerUp, select_x, select_y});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerDown, option_x, option_y});
        context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerUp, option_x, option_y});
    }
#endif
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerDown, 28, 202});
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerMove, 144, 202});
    context.input_queue.enqueue(BoardInputEvent{BoardInputKind::PointerUp, 144, 202});
}

void observe_forms_default_actions(TimerUiTaskContext& context) {
    if (!context.forms_advanced_acceptance || context.document == nullptr ||
        context.forms_replay_cycles == 0 || context.forms_replay_cycles == context.forms_replay_passes) {
        return;
    }
    jellyframe::Node* form = find_node_by_id(*context.document, "form");
    jellyframe::Node* name = find_node_by_id(*context.document, "name");
    jellyframe::Node* check = find_node_by_id(*context.document, "check");
    jellyframe::Node* submit = find_node_by_id(*context.document, "submit");
    jellyframe::Node* reset = find_node_by_id(*context.document, "reset");
    if (form == nullptr || name == nullptr || check == nullptr || submit == nullptr || reset == nullptr) {
        ESP_LOGE(kTag, "forms_acceptance fixture nodes unavailable");
        return;
    }
    jellyframe::set_form_control_value(*name, "changed");
    jellyframe::set_form_control_checked(*check, true);
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    if (jellyframe::request_form_submit_from_control(*submit).submitted) {
        ++context.forms_submit_actions;
    }
    if (jellyframe::reset_form_from_control(*reset)) {
        ++context.forms_reset_actions;
    }
    const bool reset_restored = jellyframe::form_control_value(*name) == "seed" &&
        !jellyframe::form_control_checked(*check);
#else
    const bool submit_suppressed = !jellyframe::request_form_submit_from_control(*submit).submitted;
    const bool reset_suppressed = !jellyframe::reset_form_from_control(*reset);
    const bool reset_restored = submit_suppressed && reset_suppressed &&
        jellyframe::form_control_value(*name) == "changed" && jellyframe::form_control_checked(*check);
#endif
    ++context.forms_replay_passes;
    ESP_LOGI(kTag,
             "forms_acceptance cycle=%u/30 default_actions=%s submit=%u reset=%u state_ok=%d",
             static_cast<unsigned>(context.forms_replay_cycles),
#if JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
             "enabled",
#else
             "suppressed",
#endif
             static_cast<unsigned>(context.forms_submit_actions),
             static_cast<unsigned>(context.forms_reset_actions),
             reset_restored ? 1 : 0);
}

bool rebuild_pipeline(TimerUiTaskContext& context) {
    const std::uint64_t rebuild_start = esp_timer_get_time();
    const InputInteractionState input_state = take_input_interaction_state(context);
    context.pipeline.render_tree.reset();
    context.pipeline.layout_tree.reset();
    context.pipeline.layer_tree.reset();
    context.pipeline.render_arena.rewind();
    context.pipeline.layout_arena.rewind();
    context.pipeline.layer_arena.rewind();

#if CONFIG_JELLYFRAME_ESP32S3_PERSISTENT_STYLE_RESOLVER
    if (context.style_resolver == nullptr) {
        return false;
    }
    jellyframe::StyleResolver& style_resolver = *context.style_resolver;
#else
    if (context.stylesheet == nullptr) {
        return false;
    }
    jellyframe::StyleResolverOptions style_options;
    style_options.max_candidate_cache_entries =
        static_cast<std::size_t>(CONFIG_JELLYFRAME_ESP32S3_STYLE_RESOLVER_CACHE_ENTRIES);
    jellyframe::StyleResolver transient_style_resolver(*context.stylesheet, style_options);
    jellyframe::StyleResolver& style_resolver = transient_style_resolver;
#endif
    update_heap_telemetry(context.telemetry);
    jellyframe::RenderTreeBuilder render_builder(style_resolver,
        jellyframe::render_tree_options_from_budgets(context.budgets));
    const std::uint64_t render_tree_start = esp_timer_get_time();
    context.pipeline.render_tree = render_builder.build(*context.document, context.pipeline.render_arena);
    context.telemetry.render_tree_build_us +=
        static_cast<std::uint64_t>(esp_timer_get_time() - render_tree_start);
    update_heap_telemetry(context.telemetry);
    if (!context.pipeline.render_tree) {
        return false;
    }

    jellyframe::LayoutEngine layout_engine(style_resolver,
                                           context.text_measure,
                                           jellyframe::layout_engine_options_from_budgets(context.budgets));
    const std::uint64_t layout_start = esp_timer_get_time();
    context.pipeline.layout_tree =
        layout_engine.layout(*context.pipeline.render_tree, context.width, context.height, context.pipeline.layout_arena);
    context.telemetry.layout_us += static_cast<std::uint64_t>(esp_timer_get_time() - layout_start);
    update_heap_telemetry(context.telemetry);
    if (!context.pipeline.layout_tree) {
        return false;
    }

    jellyframe::LayerTreeBuilder layer_builder(make_layer_tree_options(context));
    const std::uint64_t layer_build_start = esp_timer_get_time();
    context.pipeline.layer_tree = layer_builder.build(*context.pipeline.layout_tree, context.pipeline.layer_arena);
    const std::uint64_t layer_build_us = static_cast<std::uint64_t>(esp_timer_get_time() - layer_build_start);
    context.telemetry.layer_build_us += layer_build_us;
    context.telemetry.pipeline_layer_build_us += layer_build_us;
    update_heap_telemetry(context.telemetry);
    if (!context.pipeline.layer_tree) {
        return false;
    }
    update_rgb565_gradient_dither_policy(context);
    const std::uint64_t input_bind_start = esp_timer_get_time();
    bind_input_controller(context, input_state);
    context.telemetry.input_bind_us += static_cast<std::uint64_t>(esp_timer_get_time() - input_bind_start);
    context.telemetry.pipeline_rebuild_us += static_cast<std::uint64_t>(esp_timer_get_time() - rebuild_start);
    ++context.telemetry.pipeline_rebuilds;
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
                                           std::size_t& dirty_count,
                                           const jellyframe::LayerNode* previous_layer_tree = nullptr,
                                           const jellyframe::LayerNode* current_layer_tree = nullptr,
                                           bool layer_snapshot_failed = false) {
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

    if (!layer_snapshot_failed &&
        (update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::CurrentLayout ||
         update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::PreviousAndCurrentLayout)) {
        const jellyframe::LayoutBox* previous_layout =
            update_plan.dirty_rect_mode == jellyframe::FrameDirtyRectMode::CurrentLayout
                ? context.pipeline.layout_tree.get()
                : nullptr;
        jellyframe::DirtyRegionOptions dirty_options = jellyframe::dirty_region_options_from_budgets(
            context.budgets,
            jellyframe::Rect{0, 0, context.width, context.height});
        dirty_options.previous_layer_tree = previous_layer_tree;
        dirty_options.current_layer_tree = current_layer_tree;
        jellyframe::compute_dirty_region_into(*context.document,
                                              previous_layout,
                                              context.pipeline.layout_tree.get(),
                                              dirty_options,
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
    jellyframe::LayerNodePtr previous_layer_snapshot;
    bool layer_snapshot_failed = false;
    if (update_plan.action == jellyframe::FrameUpdateAction::RepaintExisting &&
        context.pipeline.layer_tree != nullptr) {
        previous_layer_snapshot = clone_layer_tree(*context.pipeline.layer_tree);
        layer_snapshot_failed = previous_layer_snapshot == nullptr;
        if (layer_snapshot_failed) {
            ESP_LOGW(kTag, "dirty_region layer snapshot allocation failed; using full-frame fallback");
        }
    }

    const bool rebuild_existing_layer =
        update_plan.action == jellyframe::FrameUpdateAction::RepaintExisting &&
        context.pipeline.layout_tree != nullptr;
    if (rebuild_existing_layer) {
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
        update_rgb565_gradient_dither_policy(context);
        bind_input_controller(context, input_state);
    }

    std::size_t dirty_count = 0;
    const jellyframe::Rect* dirty_rects = choose_dirty_rects(context,
                                                              update_plan,
                                                              dirty_count,
                                                              previous_layer_snapshot.get(),
                                                              context.pipeline.layer_tree.get(),
                                                              layer_snapshot_failed);
    if (dirty_rects == nullptr || dirty_count == 0) {
        return true;
    }

    const jellyframe::Rect full_viewport{0, 0, context.width, context.height};
    const bool inject_panel_scroll_fallback =
        CONFIG_JELLYFRAME_WS147_PANEL_SCROLL_FALLBACK_PROBE &&
        context.telemetry.panel_scroll_probe_injections == 0 &&
        context.telemetry.panel_scroll_steps >= 30 && context.panel.packed_scroll_mapped;
    if (inject_panel_scroll_fallback) {
        ++context.telemetry.panel_scroll_probe_injections;
        ESP_LOGI(kTag,
                 "panel_scroll_fallback_probe phase=inject step=%u mapped_before=1 reason=forced-ineligible-frame",
                 static_cast<unsigned>(context.telemetry.panel_scroll_steps));
    }
    const bool use_panel_scroll = !inject_panel_scroll_fallback &&
        panel_scroll_candidate(context, dirty_count);
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

    jellyframe::ImagePainter image_painter{};
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    image_painter = make_bmp_image_painter(context.image_adapter);
#endif
    jellyframe::SoftwareCompositor compositor(context.text_painter,
        image_painter,
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
#if CONFIG_JELLYFRAME_ESP32S3_USE_PACKED_RGB565_SINK
    jellyframe::EmbeddedPackedRgb565Sink packed_sink = make_packed_rgb565_sink(context.panel);
    const jellyframe::HostFrameSink frame_sink = jellyframe::embedded_packed_rgb565_sink(packed_sink);
#else
    context.panel.pixels = context.packed_rgb565.get();
    jellyframe::EmbeddedFrameBufferSink framebuffer_sink = make_rgb565_sink(context.panel);
    const jellyframe::HostFrameSink frame_sink = jellyframe::embedded_frame_sink(framebuffer_sink);
#endif
    const jellyframe::Rect exposed_strip = context.has_framebuffer_scroll_blit
        ? jellyframe::Rect{
              context.framebuffer_scroll_viewport.x + context.framebuffer_scroll_blit.exposed_strip.x,
              context.framebuffer_scroll_viewport.y + context.framebuffer_scroll_blit.exposed_strip.y,
              context.framebuffer_scroll_blit.exposed_strip.width,
              context.framebuffer_scroll_blit.exposed_strip.height,
          }
        : jellyframe::Rect{};
    const bool can_use_panel_scroll = CONFIG_JELLYFRAME_ESP32S3_USE_PACKED_RGB565_SINK &&
        use_panel_scroll && can_reuse_scroll_pixels &&
        compose_dirty_count == 1 && exposed_strip.x == 0 && exposed_strip.width == context.width &&
        exposed_strip.height > 0 && exposed_strip.height < context.height;
    bool force_full_normal_present = false;
    if (!can_use_panel_scroll && context.panel.packed_scroll_mapped) {
        if (!reset_rgb565_packed_scroll(context.panel)) {
            if (inject_panel_scroll_fallback) {
                ESP_LOGE(kTag, "panel_scroll_fallback_probe phase=reset ok=0");
            }
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
    if (inject_panel_scroll_fallback) {
        if (ok && force_full_normal_present && !context.panel.packed_scroll_mapped) {
            ++context.telemetry.panel_scroll_probe_recoveries;
            context.panel_scroll_probe_awaiting_reentry = true;
            ESP_LOGI(kTag,
                     "panel_scroll_fallback_probe phase=full-present ok=1 mapped_after=0 bytes=%u",
                     static_cast<unsigned>(context.panel.flushed_bytes - bytes_before));
        } else {
            ESP_LOGE(kTag,
                     "panel_scroll_fallback_probe phase=full-present ok=0 mapped_after=%d force_full=%d",
                     context.panel.packed_scroll_mapped ? 1 : 0,
                     force_full_normal_present ? 1 : 0);
        }
    } else if (can_use_panel_scroll && ok && context.panel_scroll_probe_awaiting_reentry) {
        context.panel_scroll_probe_awaiting_reentry = false;
        ++context.telemetry.panel_scroll_probe_reentries;
        ESP_LOGI(kTag,
                 "panel_scroll_fallback_probe phase=reentry ok=1 step=%u mapped_after=1",
                 static_cast<unsigned>(context.telemetry.panel_scroll_steps));
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
    context.packed_rgb565_scratch.reset(new (std::nothrow) std::uint16_t[pixel_count]);
    if (!context.frame_buffer || !context.packed_rgb565 || !context.packed_rgb565_scratch) {
        ESP_LOGE(kTag, "timer ui buffer allocation failed: rgba_pixels=%u packed_rgb565_pixels=%u scratch_pixels=%u",
                 static_cast<unsigned>(static_cast<std::size_t>(context.width) * context.height),
                 static_cast<unsigned>(pixel_count),
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
    context.panel.scratch_pixels = context.packed_rgb565_scratch.get();
    context.panel.scratch_pixel_capacity = pixel_count;
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

void set_band_view(TimerUiTaskContext& context, const char* active_id) {
    static constexpr const char* kViewIds[] = {
        "view-home",
        "view-apps",
        "view-activity",
        "view-weather",
        "view-quick",
        "view-notices",
    };
    for (const char* view_id : kViewIds) {
        if (jellyframe::Node* view = find_by_id(*context.document, view_id)) {
            view->set_attribute("class", std::string("band-view") +
                (std::string_view(view_id) == active_id ? " active" : ""));
        }
    }
    ++context.telemetry.band_route_transitions;
    ESP_LOGI(kTag, "band_shell route=%s count=%u", active_id,
             static_cast<unsigned>(context.telemetry.band_route_transitions));
}

void schedule_band_autoroute(TimerUiTaskContext& context) {
    static constexpr const char* kRouteSequence[] = {
        "view-activity", "view-home",
        "view-weather", "view-home",
        "view-quick", "view-home",
        "view-notices", "view-home",
        "view-apps", "view-home",
    };
    if (context.telemetry.band_route_transitions >= kBandAutorouteTransitionCount) {
        return;
    }
    const std::size_t index = context.telemetry.band_route_transitions %
        (sizeof(kRouteSequence) / sizeof(kRouteSequence[0]));
    set_band_view(context, kRouteSequence[index]);
}

#if CONFIG_JELLYFRAME_ESP32S3_RUN_APP_RUNTIME_RECOVERY_ACCEPTANCE
bool run_native_app_runtime_recovery_preflight() {
    jellyframe::AppRuntimeHostOptions options;
    options.max_in_flight_jobs = 4;
    options.max_completion_events_per_frame = 4;
    options.max_host_handles = 4;
    options.max_host_handle_bytes = 1024;
    options.max_app_fonts = 1;
    jellyframe::AppRuntimeHost host(options);

    constexpr std::array<jellyframe::AppTeardownReason, 3> kReasons{
        jellyframe::AppTeardownReason::RuntimeError,
        jellyframe::AppTeardownReason::BudgetExceeded,
        jellyframe::AppTeardownReason::LoadFailure,
    };
    constexpr std::uint32_t kCyclesPerReason = 30;
    bool all_passed = true;
    std::uint32_t total_failures = 0;
    std::uint32_t min_internal_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    std::uint32_t min_spiram_free =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    for (const jellyframe::AppTeardownReason reason : kReasons) {
        std::uint32_t reason_failures = 0;
        for (std::uint32_t cycle = 0; cycle < kCyclesPerReason; ++cycle) {
            const jellyframe::AppInstance app = host.launch("org.jellyframe.fixture.native-failure",
                                                             jellyframe::AppRole::App);
            const jellyframe::HostServiceSubmitResult request =
                host.submit_current(jellyframe::HostServiceJobKind::NetworkFetch);
            const std::uint32_t handle = host.allocate_current_handle(
                jellyframe::HostServiceHandleKind::FetchResponse, 64);
            const bool completion_queued = request.accepted && handle != 0 &&
                host.push_completion(jellyframe::HostServiceCompletion{
                    request.job_id,
                    jellyframe::HostServiceJobKind::NetworkFetch,
                    jellyframe::HostServiceStatus::Completed,
                    app.id,
                    handle,
                    0,
                    64,
                });
            const jellyframe::AppTeardownResult teardown = host.terminate_current(reason);
            const bool cleaned = teardown.app_instance_id == app.id && teardown.reason == reason &&
                teardown.crashed && teardown.cancelled_requests == 1 &&
                teardown.discarded_completions == 1 && teardown.released_handles == 1 &&
                host.requests().empty() && host.completions().empty() &&
                host.handles().active_count() == 0;
            const jellyframe::AppInstance launcher = host.launch(
                "org.jellyframe.system.launcher", jellyframe::AppRole::Launcher);
            const bool launcher_ready = launcher.active() && launcher.role == jellyframe::AppRole::Launcher &&
                host.current_app_instance_id() == launcher.id;
            if (!completion_queued || !cleaned || !launcher_ready) {
                all_passed = false;
                ++reason_failures;
                ++total_failures;
            }
            min_internal_free = std::min(
                min_internal_free,
                static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
            min_spiram_free = std::min(
                min_spiram_free,
                static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        }
        ESP_LOGI(kTag,
                 "app_runtime_native_recovery reason=%s cycles=%u failures=%u",
                 jellyframe::app_teardown_reason_name(reason),
                 static_cast<unsigned>(kCyclesPerReason),
                 static_cast<unsigned>(reason_failures));
    }
    host.exit_current();
    ESP_LOGI(kTag,
             "app_runtime_native_recovery_summary scripting=0 cycles=%u failures=%u system_shell_retained=1 internal_free_min=%u spiram_free_min=%u",
             static_cast<unsigned>(kCyclesPerReason * kReasons.size()),
             static_cast<unsigned>(total_failures),
             static_cast<unsigned>(min_internal_free),
             static_cast<unsigned>(min_spiram_free));
    return all_passed && total_failures == 0;
}
#endif

void bind_band_navigation(TimerUiTaskContext& context) {
    struct Route {
        const char* control_id;
        const char* view_id;
    };
    static constexpr Route kRoutes[] = {
        {"home-to-activity", "view-activity"},
        {"home-to-apps", "view-apps"},
        {"home-to-weather", "view-weather"},
        {"home-to-quick", "view-quick"},
        {"home-to-notices", "view-notices"},
        {"apps-home", "view-home"},
        {"apps-activity", "view-activity"},
        {"apps-weather", "view-weather"},
        {"apps-quick", "view-quick"},
        {"activity-home", "view-home"},
        {"weather-home", "view-home"},
        {"quick-home", "view-home"},
        {"notices-home", "view-home"},
    };
    for (const Route& route : kRoutes) {
        if (jellyframe::Node* control = find_by_id(*context.document, route.control_id)) {
            TimerUiTaskContext* const context_ptr = &context;
            control->add_event_listener("click", [context_ptr, view_id = route.view_id](jellyframe::Event&) {
                set_band_view(*context_ptr, view_id);
            });
        } else {
            ESP_LOGW(kTag, "band shell control is missing: %s", route.control_id);
        }
    }
}

void run_retained_ui_task(void* raw_context) {
    std::unique_ptr<TimerUiTaskContext> context(static_cast<TimerUiTaskContext*>(raw_context));
    context->board_runtime = boards::initialize_selected_board();
    const auto& board = context->board_runtime.profile;
    context->width = board.display.width > 0 ? board.display.width : CONFIG_JELLYFRAME_BENCH_VIEWPORT_WIDTH;
    context->height = board.display.height > 0 ? board.display.height : CONFIG_JELLYFRAME_BENCH_VIEWPORT_HEIGHT;
    context->capabilities = make_ui_capabilities(board, context->width, context->height);
    context->budgets = context->capabilities.budgets;
#if CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER
    context->image_adapter.configure(context->budgets, context->document_url);
#endif
    context->font_context = make_app_font_context();
    context->text_measure = jellyframe::TextMeasureProvider{app_font_measure_callback,
                                                            &context->font_context};
    context->text_painter = jellyframe::TextPainter{app_font_paint_callback,
                                                    &context->font_context};
    const auto& font_stats = production_font_stats();
    ESP_LOGI(kTag,
             "font_pack family=\"%s\" coverage=%s faces=%u coverage_chars=%u glyphs=%u bitmap_bytes=%u bits_per_pixel=%u",
             font_stats.family,
             font_stats.coverage,
             static_cast<unsigned>(font_stats.face_count),
             static_cast<unsigned>(font_stats.coverage_count),
             static_cast<unsigned>(font_stats.glyph_count),
             static_cast<unsigned>(font_stats.bitmap_bytes),
             static_cast<unsigned>(font_stats.bits_per_pixel));
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
             ui_task_kind(*context),
             ui_task_mode(*context),
             board.name,
             context->width,
             context->height,
             context->board_runtime.hardware_display_ready ? 1 : 0,
             context->board_runtime.hardware_status != nullptr ? context->board_runtime.hardware_status : "",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

    const std::uint64_t cold_document_load_start = esp_timer_get_time();
    if (!load_timer_document(*context) || !prepare_buffers(*context)) {
        boards::release_board_runtime(context->board_runtime);
        signal_installed_session(*context);
        // Free DOM, pipeline, frame buffers, and input state before deleting
        // the FreeRTOS task. vTaskDelete() does not unwind this C++ frame.
        context.reset();
        vTaskDelete(nullptr);
        return;
    }
    context->telemetry.cold_document_load_us =
        static_cast<std::uint64_t>(esp_timer_get_time() - cold_document_load_start);
    context->timer_running = !context->scroll_benchmark && !context->band_shell && !context->gradient_fastpath_benchmark &&
        CONFIG_JELLYFRAME_ESP32S3_TIMER_UI_AUTOSTART;
    if (context->band_shell) {
        bind_band_navigation(*context);
    } else if (!context->installed_bundle_app && !context->scroll_benchmark && !context->gradient_fastpath_benchmark && !context->forms_advanced_acceptance) {
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
        const std::uint64_t power_now_us = esp_timer_get_time();
        if (context->power_acceptance && context->telemetry.first_present_ok &&
            context->board_runtime.screen_power != nullptr) {
            if (context->next_power_transition_us == 0) {
                context->next_power_transition_us = power_now_us + 5000000ULL;
            }
            if (power_now_us >= context->next_power_transition_us) {
                const bool turn_on = !context->screen_is_on;
                if (context->board_runtime.screen_power(turn_on, context->board_runtime.flush_context)) {
                    context->screen_is_on = turn_on;
                    if (turn_on) {
                        ++context->telemetry.screen_power_ons;
                        force_first_frame = true;
                    } else {
                        ++context->telemetry.screen_power_offs;
                    }
                    context->next_power_transition_us = power_now_us + (turn_on ? 5000000ULL : 2000000ULL);
                    ESP_LOGI(kTag,
                             "screen_power_transition state=%s offs=%u ons=%u",
                             turn_on ? "on" : "off",
                             static_cast<unsigned>(context->telemetry.screen_power_offs),
                             static_cast<unsigned>(context->telemetry.screen_power_ons));
                } else {
                    ++context->telemetry.screen_power_failures;
                    context->next_power_transition_us = power_now_us + 1000000ULL;
                    ESP_LOGE(kTag,
                             "screen_power_transition failed target=%s failures=%u",
                             turn_on ? "on" : "off",
                             static_cast<unsigned>(context->telemetry.screen_power_failures));
                }
            }
        }
        context->frame_scratch.begin_frame();
        context->app_scratch.begin_frame();
        const std::uint64_t frame_start = esp_timer_get_time();
        const bool first_frame = force_first_frame;

        if (context->forms_advanced_acceptance && context->forms_replay_cycles < 30 &&
            power_now_us >= context->next_forms_replay_us) {
            enqueue_forms_replay(*context);
            context->next_forms_replay_us = power_now_us + 250000ULL;
        }
        jellyframe::FrameLoopPendingWork pending;
        pending.pending_input_events = context->input_queue.size();
        const bool band_autoroute_due = context->band_shell &&
            CONFIG_JELLYFRAME_ESP32S3_BAND_SHELL_AUTOROUTE &&
            context->telemetry.band_route_transitions < kBandAutorouteTransitionCount;
        const bool timer_due = ((!context->scroll_benchmark && !context->band_shell && !context->gradient_fastpath_benchmark) ||
                                context->scroll_autorun || context->gradient_fastpath_benchmark ||
                                band_autoroute_due) &&
            esp_timer_get_time() >= next_tick_us;
        pending.pending_timer_callbacks = timer_due ? 1 : 0;

        if (!context->pipeline.layer_tree) {
            const std::uint64_t cold_pipeline_start = esp_timer_get_time();
            if (!rebuild_pipeline(*context)) {
                ESP_LOGE(kTag, "ui task failed: initial pipeline build failed");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            if (first_frame) {
                context->telemetry.cold_pipeline_build_us =
                    static_cast<std::uint64_t>(esp_timer_get_time() - cold_pipeline_start);
            }
        }

        const jellyframe::FrameLoopWorkPlan work_plan =
            jellyframe::plan_frame_loop_work(pending, loop_options);
        if (!context->input_controller) {
            ESP_LOGE(kTag, "ui task input controller is unavailable");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        context->pending_scroll_drag_delta = 0;
        const std::uint64_t input_dispatch_start = esp_timer_get_time();
        const BoardInputDispatchStats input_stats =
            dispatch_input_events(context->input_queue,
                                  *context->input_controller,
                                  work_plan.input_events_to_dispatch,
                                  observe_scroll_input,
                                  context.get());
        context->telemetry.input_dispatch_us +=
            static_cast<std::uint64_t>(esp_timer_get_time() - input_dispatch_start);
        context->telemetry.input_events += input_stats.dispatched;
        observe_forms_default_actions(*context);

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
            } else if (context->band_shell && CONFIG_JELLYFRAME_ESP32S3_BAND_SHELL_AUTOROUTE) {
                if (!force_first_frame) {
                    schedule_band_autoroute(*context);
                }
                next_tick_us = esp_timer_get_time() + kBandAutorouteIntervalUs;
            } else if (context->image_acceptance) {
                if (!force_first_frame && context->image_acceptance_cycles < 30) {
                    ++context->image_acceptance_cycles;
                    context->document->set_attribute(
                        "data-cycle", std::to_string(context->image_acceptance_cycles));
                    ESP_LOGI(kTag, "image_acceptance cycle=%u/30",
                             static_cast<unsigned>(context->image_acceptance_cycles));
                }
                next_tick_us = esp_timer_get_time() + 500000ULL;
            } else if (!context->band_shell && !context->gradient_fastpath_benchmark && !context->forms_advanced_acceptance) {
                if (!force_first_frame && context->timer_running) {
                    ++context->elapsed_seconds;
                }
                update_timer_text(*context);
                next_tick_us = esp_timer_get_time() + 1000000ULL;
            } else if (context->gradient_fastpath_benchmark) {
                next_tick_us = esp_timer_get_time() + 33333ULL;
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

        const std::uint64_t frame_planning_start = esp_timer_get_time();
        const jellyframe::DomDirtyFlags dirty_flags = force_first_frame
            ? jellyframe::DomDirtyTree | jellyframe::DomDirtyLayout
            : jellyframe::subtree_dirty_flags(*context->document);
        jellyframe::FrameLoopPlan frame_plan =
            jellyframe::plan_frame_loop(pending, dirty_flags, cache_state(*context), loop_options);
        if (context->gradient_fastpath_benchmark && work_plan.timer_callbacks_to_pump > 0) {
            frame_plan.update.action = jellyframe::FrameUpdateAction::RepaintExisting;
            frame_plan.update.dirty_rect_mode = jellyframe::FrameDirtyRectMode::FullFrame;
            frame_plan.update.reason = jellyframe::FrameUpdateReason::PaintOnlyDirty;
            frame_plan.update.can_reuse_render_and_layout = true;
            frame_plan.update.needs_previous_layout = false;
            frame_plan.update.needs_full_framebuffer = false;
        }
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
        context->telemetry.frame_planning_us +=
            static_cast<std::uint64_t>(esp_timer_get_time() - frame_planning_start);
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
        if (first_frame && frame_plan.update.action != jellyframe::FrameUpdateAction::None) {
            context->telemetry.first_frame_us = frame_us;
            context->telemetry.first_present_us = present_us;
            context->telemetry.first_present_ok = presented;
            ESP_LOGI(kTag,
                     "port_cold_start_telemetry case=%s resource_parse_ms=%.3f pipeline_build_ms=%.3f first_frame_ms=%.3f first_present_ms=%.3f first_present_ok=%d internal_free=%u psram_free=%u largest_internal=%u largest_psram=%u",
                     context->telemetry_case,
                     static_cast<double>(context->telemetry.cold_document_load_us) / 1000.0,
                     static_cast<double>(context->telemetry.cold_pipeline_build_us) / 1000.0,
                     static_cast<double>(context->telemetry.first_frame_us) / 1000.0,
                     static_cast<double>(context->telemetry.first_present_us) / 1000.0,
                     context->telemetry.first_present_ok ? 1 : 0,
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        }
#if CONFIG_JELLYFRAME_ESP32S3_RUN_APP_RUNTIME_RECOVERY_ACCEPTANCE
        if (context->app_runtime_recovery_acceptance && !context->app_runtime_recovery_complete &&
            first_frame && presented) {
            context->app_runtime_recovery_complete = true;
            if (!run_native_app_runtime_recovery_preflight()) {
                ESP_LOGE(kTag, "app runtime native recovery preflight failed; native system shell remains active");
            }
        }
#endif
        ++context->telemetry.frames;
        if (frame_plan.update.action != jellyframe::FrameUpdateAction::None) {
            ++context->telemetry.active_frames;
            context->telemetry.active_frame_us += frame_us;
        }
        context->telemetry.frame_us_total += frame_us;
        context->telemetry.frame_us_max = std::max(context->telemetry.frame_us_max, frame_us);
        context->telemetry.frame_histogram.record(frame_us);
        context->telemetry.present_us_total += present_us;
        context->telemetry.present_us_max = std::max(context->telemetry.present_us_max, present_us);
        if (present_us > 0) {
            context->telemetry.present_histogram.record(present_us);
        }
        update_heap_telemetry(context->telemetry);

        const bool suppress_periodic_frame_log =
            ((context->scroll_benchmark && context->scroll_autorun) || context->gradient_fastpath_benchmark) &&
            context->telemetry.frames > 1 && input_stats.dispatched == 0 && presented;
        if ((frame_plan.update.action != jellyframe::FrameUpdateAction::None || input_stats.dispatched > 0) &&
            !suppress_periodic_frame_log) {
            const jellyframe::Rect first_dirty_rect = context->frame_scratch.dirty_region.rects.empty()
                ? jellyframe::Rect{}
                : context->frame_scratch.dirty_region.rects.front();
            ESP_LOGI(kTag,
                     "ui_task_frame kind=%s frame=%u elapsed=%u running=%d clicks=%u scroll_y=%d drag=%d action=%s reason=%s dirty_flags=0x%08x dirty_mode=%s dirty_rects=%u dirty_rect0=%d,%d,%d,%d input=%u queue_left=%u present_us=%u ok=%d stack_free=%u",
                     ui_task_kind(*context),
                     static_cast<unsigned>(context->telemetry.frames),
                     static_cast<unsigned>(context->elapsed_seconds),
                     context->timer_running ? 1 : 0,
                     static_cast<unsigned>(context->button_clicks),
                     context->scroll_y,
                     context->scroll_gesture.dragging() ? 1 : 0,
                     jellyframe::frame_update_action_name(frame_plan.update.action),
                     jellyframe::frame_update_reason_name(frame_plan.update.reason),
                     static_cast<unsigned>(dirty_flags),
                     jellyframe::dirty_region_mode_name(context->frame_scratch.dirty_region.mode),
                     static_cast<unsigned>(context->frame_scratch.dirty_region.rects.size()),
                     first_dirty_rect.x,
                     first_dirty_rect.y,
                     first_dirty_rect.width,
                     first_dirty_rect.height,
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
        if (context->installed_bundle_app && ulTaskNotifyTake(pdTRUE, 0) != 0) {
            break;
        }
        if ((context->scroll_benchmark && context->scroll_autorun) || context->gradient_fastpath_benchmark) {
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
    boards::release_board_runtime(context->board_runtime);
    signal_installed_session(*context);
    // Installed app tasks own their Render Core state. Releasing it here is
    // required before the lifecycle endpoint may launch a new generation.
    context.reset();
    vTaskDelete(nullptr);
}

} // namespace

bool start_ui_task(TimerUiTaskContext* context, const char* task_name, TaskHandle_t* handle = nullptr) {
    if (context == nullptr) {
        return false;
    }
    TaskHandle_t task = nullptr;
    const BaseType_t ok = xTaskCreate(run_retained_ui_task,
                                      task_name,
                                      CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
                                      context,
                                      CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY,
                                      &task);
    if (ok != pdPASS) {
        delete context;
        ESP_LOGE(kTag, "UI task creation failed");
        return false;
    }
    if (handle != nullptr) {
        *handle = task;
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

bool start_band_shell_ui_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "band shell UI task context allocation failed");
        return false;
    }
    context->document_url = kBandShellUrl;
    context->telemetry_case = "band_shell_ui_cumulative";
    context->telemetry_app_id = "org.jellyframe.system.band_shell";
    context->band_shell = true;
    return start_ui_task(context, "jellyframe_band");
}

bool start_flex_grid_acceptance_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "Flex/Grid acceptance context allocation failed");
        return false;
    }
    context->document_url = kFlexGridAcceptanceUrl;
    context->telemetry_case = "flex_grid_acceptance_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.flex-grid";
    return start_ui_task(context, "jellyframe_flex_grid");
}

bool start_forms_advanced_acceptance_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "forms.advanced acceptance context allocation failed");
        return false;
    }
    context->document_url = kFormsAdvancedAcceptanceUrl;
    context->telemetry_case = "forms_advanced_acceptance_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.forms-advanced";
    context->forms_advanced_acceptance = true;
    return start_ui_task(context, "jellyframe_forms");
}

bool start_gradient_fastpath_ui_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "gradient fast-path UI task context allocation failed");
        return false;
    }
    context->document_url = kGradientFastpathUrl;
    context->telemetry_case = "opaque_linear_gradient_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.gradient_fastpath";
    context->gradient_fastpath_benchmark = true;
    return start_ui_task(context, "jellyframe_gradient");
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

bool start_power_acceptance_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "panel power acceptance task context allocation failed");
        return false;
    }
    context->telemetry_case = "screen_power_acceptance_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.screen-power";
    context->power_acceptance = true;
    return start_ui_task(context, "jellyframe_power");
}

bool start_resource_failure_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "resource failure task context allocation failed");
        return false;
    }
    context->document_url = kResourceFailureUrl;
    context->telemetry_case = "resource_failure_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.resource-failure";
    return start_ui_task(context, "jellyframe_resource");
}

bool start_image_acceptance_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "image acceptance task context allocation failed");
        return false;
    }
    context->document_url = kImageAcceptanceUrl;
    context->telemetry_case = "image_acceptance_cumulative";
    context->telemetry_app_id = "org.jellyframe.bringup.image";
    context->image_acceptance = true;
    return start_ui_task(context, "jellyframe_image");
}

bool start_app_runtime_recovery_acceptance_task() {
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (context == nullptr) {
        ESP_LOGE(kTag, "app runtime recovery task context allocation failed");
        return false;
    }
    context->document_url = kBandShellUrl;
    context->band_shell = true;
    context->app_runtime_recovery_acceptance = true;
    context->telemetry_case = "app_runtime_native_recovery_cumulative";
    context->telemetry_app_id = "org.jellyframe.system.launcher";
    return start_ui_task(context, "jellyframe_app_recovery");
}

bool start_installed_bundle_ui_task(std::string app_id,
                                    std::uint32_t generation,
                                    std::string entry_path,
                                    std::string entry_document,
                                    InstalledResourceSnapshot resources,
                                    InstalledBundleUiSession*& session) {
    session = nullptr;
    if (app_id.empty() || app_id.size() > jellyframe::kDeviceBundleMaxAppIdBytes ||
        entry_path.empty() || entry_path.size() > jellyframe::kDeviceBundleMaxEntryPathBytes ||
        entry_document.empty() || entry_document.size() > 16u * 1024u ||
        !resources.rebuild_views()) {
        return false;
    }
    auto* next_session = new (std::nothrow) InstalledBundleUiSession();
    auto* context = new (std::nothrow) TimerUiTaskContext();
    if (next_session == nullptr || context == nullptr) {
        delete next_session;
        delete context;
        return false;
    }
    next_session->stopped = xSemaphoreCreateBinary();
    if (next_session->stopped == nullptr) {
        delete next_session;
        delete context;
        return false;
    }
    context->installed_bundle_app = true;
    context->installed_app_id = std::move(app_id);
    context->installed_generation = generation;
    context->document_url = std::move(entry_path);
    context->installed_entry_document = std::move(entry_document);
    context->installed_resources = std::move(resources);
    if (!context->installed_resources.rebuild_views()) {
        vSemaphoreDelete(next_session->stopped);
        delete next_session;
        delete context;
        return false;
    }
    context->installed_session = next_session;
    context->telemetry_case = "installed_bundle_ui_cumulative";
    context->telemetry_app_id = context->installed_app_id.c_str();
    if (!start_ui_task(context, "jellyframe_app", &next_session->task)) {
        vSemaphoreDelete(next_session->stopped);
        delete next_session;
        return false;
    }
    session = next_session;
    return true;
}

bool stop_installed_bundle_ui_task(InstalledBundleUiSession*& session, std::uint32_t timeout_ms) {
    if (session == nullptr) {
        return true;
    }
    if (session->task == nullptr || session->stopped == nullptr) {
        return false;
    }
    xTaskNotifyGive(session->task);
    const BaseType_t stopped = xSemaphoreTake(session->stopped, pdMS_TO_TICKS(timeout_ms));
    if (stopped != pdTRUE) {
        return false;
    }
    vSemaphoreDelete(session->stopped);
    delete session;
    session = nullptr;
    return true;
}

} // namespace jellyframe_esp32s3
