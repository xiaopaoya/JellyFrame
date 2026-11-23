#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#include "jellyframe_esp32s3_input.h"
#include "jellyframe_esp32s3_installed_frame_admission.h"
#include "jellyframe_jerry_port_fatal.h"

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "render_core/budget.h"
#include "render_core/document_script.h"
#include "render_core/document_style.h"
#include "render_core/html_parser.h"
#include "render_core/software_renderer.h"
#include "script/script_task_worker_runtime.h"
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM
#include "app_runtime/core_system_font_catalog.h"
#endif

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameInstalledScript";
constexpr std::size_t kMaxCommands = 384;
constexpr std::size_t kMaxFrameBytes = 64u * 1024u;
constexpr std::size_t kMaxScriptBytes = 48u * 1024u;
// A zero-tick delay only yields to peers and can starve lower-priority tasks.
constexpr TickType_t kWorkerPollTicks = std::max<TickType_t>(1, pdMS_TO_TICKS(4));
constexpr TickType_t kSupervisorPollTicks = std::max<TickType_t>(1, pdMS_TO_TICKS(8));
constexpr std::size_t kSupervisorStackBytes = 12u * 1024u;

jellyframe::HostBudgets installed_script_budgets(int width, int height) {
    jellyframe::HostBudgets budgets;
    budgets.max_dom_nodes = 192;
    budgets.max_dom_depth = 20;
    budgets.max_attributes_per_element = 20;
    budgets.max_css_rules = 64;
    budgets.max_css_declarations_per_rule = 32;
    budgets.max_render_objects = 192;
    budgets.max_layout_boxes = 192;
    budgets.max_layers = 64;
    budgets.max_display_commands = kMaxCommands;
    budgets.max_dirty_rects = 8;
    budgets.max_input_events_per_frame = 16;
    budgets.max_timers = 8;
    budgets.max_timer_callbacks_per_frame = 4;
    budgets.max_event_listeners = 48;
    budgets.max_resource_bytes = kMaxScriptBytes;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    budgets.max_script_execution_checks = 2000;
    budgets.script_execution_check_interval = 64;
    budgets.max_active_animations = 4;
    return budgets;
}

jellyframe::ScriptTaskAppFrameCodecOptions frame_codec_options(const jellyframe::AppInstalledTextPipeline& pipeline) {
    jellyframe::ScriptTaskAppFrameCodecOptions options;
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    options.max_groups = 64;
    options.max_group_depth = 8;
#endif
    options.version = installed_text_pipeline_frame_version(pipeline);
    options.max_commands = kMaxCommands;
    options.max_text_bytes = 12u * 1024u;
    options.max_input_targets = 48;
    options.max_payload_bytes = kMaxFrameBytes;
    options.max_clips = 64;
    options.max_clip_depth = 8;
    return options;
}

jellyframe::ScriptTaskSupervisorOptions supervisor_options() {
    jellyframe::ScriptTaskSupervisorOptions options;
    options.worker_inbox = {16, 256};
    options.frame_mailbox = {8, 256};
    options.frame_leases = {4, kMaxFrameBytes, 2 * kMaxFrameBytes};
    options.max_service_tombstones = 8;
    options.max_native_release_intents = 8;
    options.service_request_mailbox = {8, 64};
    options.service_payload_leases = {4, 8u * 1024u, 16u * 1024u};
    options.fatal_mailbox = {4, 64};
    return options;
}

} // namespace

struct InstalledBundleScriptSession {
    mutable portMUX_TYPE resource_lock = portMUX_INITIALIZER_UNLOCKED;
    InstalledScriptResourceTelemetry resources;
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> fatal_seen{false};
    std::atomic<int> vm_fatal_code{-1};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> ui_started{false};
    std::atomic<std::uint8_t> init_status{0xff};
    std::atomic<std::uint8_t> fatal_reason{0};
    std::atomic<std::uint32_t> input_posted{0};
    std::atomic<std::uint32_t> input_rejected{0};
    std::atomic<std::uint32_t> input_unsupported{0};
    std::atomic<std::uint32_t> input_packet_seq{0};
    std::atomic<std::uint32_t> js_mutation_seq{0};
    std::atomic<std::uint32_t> published_frame_seq{0};
    std::atomic<std::uint32_t> accepted_frame_seq{0};
    std::atomic<std::uint32_t> present_failures{0};
    std::atomic<std::uint8_t> render_status{0xff};
    std::atomic<bool> malformed_v4_probe_published{false};
    std::atomic<bool> malformed_v4_probe_rejected{false};
    std::atomic<std::uint32_t> accepted_after_malformed_v4_probe{0};
    std::uint32_t app_instance_id = 0;
    std::uint32_t generation = 0;
    int width = 0;
    int height = 0;
    jellyframe::AppInstalledTextPipeline text_pipeline;
    std::string app_id;
    std::string entry_path;
    std::string entry_document;
    std::string author_css;
    std::vector<jellyframe::DocumentScript> scripts;
    std::shared_ptr<const struct InstalledAppResources> runtime_resources;
    jellyframe::AppRuntimeHost* host = nullptr;
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession script_session{};
    BoardInputQueue input_queue;
    SemaphoreHandle_t stopped = nullptr;
    TaskHandle_t supervisor_task = nullptr;
};

namespace {

InstalledScriptHeapObservation observe_heap() {
    return {
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))};
}

// Only the supervisor samples live task handles, before it deletes either task.
// Heap/stack queries run outside the short value-copy critical section.
void sample_resources(InstalledBundleScriptSession& state, TaskHandle_t worker, TaskHandle_t ui) {
    const auto heap = observe_heap();
    // ESP-IDF Xtensa StackType_t is uint8_t, so this HWM unit is bytes.
    const auto worker_hwm = worker == nullptr ? 0u : static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(worker));
    const auto ui_hwm = ui == nullptr ? 0u : static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(ui));
    portENTER_CRITICAL(&state.resource_lock);
    auto& resource = state.resources;
    resource.heap_current = heap;
    auto& low = resource.heap_sampled_min;
    low.internal_free = std::min(low.internal_free, heap.internal_free);
    low.internal_largest = std::min(low.internal_largest, heap.internal_largest);
    low.psram_free = std::min(low.psram_free, heap.psram_free);
    low.psram_largest = std::min(low.psram_largest, heap.psram_largest);
    resource.worker_stack_hwm_bytes = worker_hwm;
    resource.ui_stack_hwm_bytes = ui_hwm;
    ++resource.resource_samples;
    portEXIT_CRITICAL(&state.resource_lock);
}

jellyframe::ScriptTaskWorkerRuntimeOptions worker_options(const InstalledBundleScriptSession& state) {
    jellyframe::ScriptTaskWorkerRuntimeOptions options;
    options.budgets = installed_script_budgets(state.width, state.height);
    options.script = jellyframe::script_runtime_options_from_host_budgets(options.budgets);
    options.prepare_document = [](jellyframe::Node& document, void* raw) {
        const auto& session = *static_cast<const InstalledBundleScriptSession*>(raw);
        return prepare_document_styles(document, session.entry_path);
    };
    options.prepare_document_user = const_cast<InstalledBundleScriptSession*>(&state);
    options.viewport = {0, 0, state.width, state.height};
    options.frame_codec = frame_codec_options(state.text_pipeline);
    options.input_codec = {32, 256};
    options.service_request_codec = {64};
    options.text_system = state.host != nullptr
        ? state.host->fonts().core_text_system()
        : nullptr;
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    if (state.runtime_resources) {
        state.runtime_resources->bind_worker_resources(options);
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        options.shaped_layout.font_family_hash = state.text_pipeline.font_family_hash;
#endif
    }
#endif
    return options;
}

InstalledBundleScriptTaskTelemetry telemetry_snapshot(const InstalledBundleScriptSession& state) {
    InstalledBundleScriptTaskTelemetry telemetry;
    portENTER_CRITICAL(&state.resource_lock);
    telemetry.resources = state.resources;
    portEXIT_CRITICAL(&state.resource_lock);
    telemetry.initialized = state.initialized.load();
    telemetry.fatal = state.fatal_seen.load();
    telemetry.worker_started = state.worker_started.load();
    telemetry.ui_started = state.ui_started.load();
    telemetry.init_status = state.init_status.load();
    telemetry.fatal_reason = state.fatal_reason.load();
    telemetry.scripts = static_cast<std::uint32_t>(state.scripts.size());
    telemetry.input_posted = state.input_posted.load();
    telemetry.input_rejected = state.input_rejected.load();
    telemetry.input_unsupported = state.input_unsupported.load();
    telemetry.input_queue_dropped = state.input_queue.dropped_count();
    telemetry.input_seq = state.input_packet_seq.load();
    telemetry.mutation_seq = state.js_mutation_seq.load();
    telemetry.published_seq = state.published_frame_seq.load();
    telemetry.accepted_seq = state.accepted_frame_seq.load();
    telemetry.presents_failed = state.present_failures.load();
    telemetry.render_status = state.render_status.load();
    telemetry.malformed_v4_probe_published = state.malformed_v4_probe_published.load();
    telemetry.malformed_v4_probe_rejected = state.malformed_v4_probe_rejected.load();
    telemetry.accepted_after_malformed_v4_probe = state.accepted_after_malformed_v4_probe.load();
    return telemetry;
}

#if defined(CONFIG_JELLYFRAME_ESP32S3_INSTALLED_SCRIPT_FRAME_V4_MALFORMED_PROBE) && \
    CONFIG_JELLYFRAME_ESP32S3_INSTALLED_SCRIPT_FRAME_V4_MALFORMED_PROBE
bool publish_malformed_v3_with_v4_source_clip(InstalledBundleScriptSession& state) {
    if (state.protocol == nullptr) return false;
    jellyframe::ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, state.width, state.height};
    frame.clips.push_back({{8, 8, state.width - 16, state.height - 16}, 12, jellyframe::kScriptTaskNoParentClip});
    jellyframe::DisplayCommand fill;
    fill.type = jellyframe::DisplayCommandType::FillRect;
    fill.rect = {12, 12, state.width - 24, state.height - 24};
    fill.color = {24, 96, 176, 255};
    fill.transform.enabled = true;
    fill.transform.source_clip_index = 0;
    frame.display_list.push_back(fill);
    frame.display_clip_indices.push_back(0);
    std::vector<std::uint8_t> encoded;
    if (jellyframe::encode_script_task_app_frame(frame, frame_codec_options(state.text_pipeline), encoded) !=
            jellyframe::ScriptTaskAppFrameCodecStatus::Accepted ||
        encoded.size() < 36 + 28 + 104) {
        return false;
    }
    // Keep the v4 command record intact while falsely claiming its v3 header.
    // The normal UI decoder is configured for v4 and must reject this packet.
    encoded[4] = 3;
    const bool published = state.protocol->publish_frame(state.script_session, encoded).accepted();
    state.malformed_v4_probe_published.store(published);
    ESP_LOGI(kTag, "v4 malformed probe app=%s published=%u bytes=%u", state.app_id.c_str(), published ? 1u : 0u,
             static_cast<unsigned>(encoded.size()));
    return published;
}
#endif

void post_board_input(InstalledBundleScriptSession& state,
                      const BoardInputEvent& event,
                      std::uint32_t& next_sequence);

void worker_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    state->worker_started.store(true);
    try {
    jellyframe::ScriptTaskWorkerRuntime runtime(state->script_session, worker_options(*state));
    const auto initialized = runtime.initialize(*state->protocol, state->entry_document, state->author_css);
    state->init_status.store(static_cast<std::uint8_t>(initialized));
    state->initialized.store(initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted);
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        runtime.set_host_time_ms(static_cast<std::uint64_t>(esp_timer_get_time() / 1000));
        for (const jellyframe::DocumentScript& script : state->scripts) {
            if (state->stop.load()) break;
            const auto result = runtime.eval_with_supervisor(*state->protocol, script.source, script.name);
            if (!result.ok || runtime.fatal()) break;
        }
        if (!state->stop.load() && !runtime.fatal()) {
            (void)runtime.publish_frame(*state->protocol);
#if defined(CONFIG_JELLYFRAME_ESP32S3_INSTALLED_SCRIPT_FRAME_V4_MALFORMED_PROBE) && \
    CONFIG_JELLYFRAME_ESP32S3_INSTALLED_SCRIPT_FRAME_V4_MALFORMED_PROBE
            (void)publish_malformed_v3_with_v4_source_clip(*state);
#endif
        }
    }
    if (initialized != jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        const jellyframe::ScriptTaskWorkerRuntimeFatalRecord fatal = runtime.fatal_record();
        state->fatal_reason.store(static_cast<std::uint8_t>(fatal.reason));
        state->fatal_seen.store(true);
        // A rejected worker has no valid DOM/VM to service. Terminate the
        // session through the same protected lifecycle path as a runtime fatal.
        (void)runtime.publish_fatal(*state->protocol);
    }
    ESP_LOGI(kTag, "worker app=%s initialized=%d init_status=%u fatal_reason=%u scripts=%u",
             state->app_id.c_str(), initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted ? 1 : 0,
             static_cast<unsigned>(state->init_status.load()), static_cast<unsigned>(state->fatal_reason.load()),
             static_cast<unsigned>(state->scripts.size()));
    std::uint32_t input_sequence = 1;
    while (!state->stop.load() && !runtime.fatal()) {
        // Read one event at a time so pending pointer moves can coalesce
        // in the board queue while the worker is busy.
        BoardInputEvent event;
        if (state->accepted_frame_seq.load() != 0 && state->input_queue.dequeue(event)) {
            post_board_input(*state, event, input_sequence);
        }
        const auto input = runtime.process_one(*state->protocol);
        const auto callbacks = runtime.pump_callbacks(
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000), *state->protocol);
        const auto telemetry = runtime.telemetry();
        state->input_packet_seq.store(telemetry.input_packet_seq);
        state->js_mutation_seq.store(telemetry.js_mutation_seq);
        state->published_frame_seq.store(telemetry.published_frame_seq);
        if (input.fatal || callbacks.fatal) break;
        vTaskDelay(kWorkerPollTicks);
    }
    if (runtime.fatal()) {
        state->fatal_seen.store(true);
        state->fatal_reason.store(static_cast<std::uint8_t>(runtime.fatal_record().reason));
        (void)runtime.publish_fatal(*state->protocol);
    }
    runtime.stop();
    } catch (const std::bad_alloc&) {
        // Construction can fail before a worker exists to publish its fatal
        // record. Report through allocation-free session state so the
        // supervisor can complete the ordinary protected teardown.
        state->init_status.store(static_cast<std::uint8_t>(jellyframe::ScriptTaskWorkerRuntimeInitStatus::BudgetRejected));
        state->fatal_reason.store(static_cast<std::uint8_t>(jellyframe::ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded));
        state->fatal_seen.store(true);
        state->stop.store(true);
    } catch (const std::length_error&) {
        state->init_status.store(static_cast<std::uint8_t>(jellyframe::ScriptTaskWorkerRuntimeInitStatus::BudgetRejected));
        state->fatal_reason.store(static_cast<std::uint8_t>(jellyframe::ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded));
        state->fatal_seen.store(true);
        state->stop.store(true);
    }
    state->worker_done.store(true);
    // The supervisor reclaims this WithCaps task after its JerryScript state
    // has been destroyed. Self-deletion would need an extra cleanup task.
    vTaskSuspend(nullptr);
}

void post_board_input(InstalledBundleScriptSession& state,
                      const BoardInputEvent& event,
                      std::uint32_t& next_sequence) {
    if (state.protocol == nullptr || state.stop.load()) return;
    jellyframe::ScriptTaskInputEvent input;
    input.x = event.x;
    input.y = event.y;
    input.delta_x = event.delta_x;
    input.delta_y = event.delta_y;
    switch (event.kind) {
    case BoardInputKind::PointerDown:
        input.kind = jellyframe::ScriptTaskInputKind::PointerDown;
        input.button = 0;
        input.buttons = 1;
        break;
    case BoardInputKind::PointerMove:
        input.kind = jellyframe::ScriptTaskInputKind::PointerMove;
        input.button = 0;
        input.buttons = 1;
        break;
    case BoardInputKind::PointerUp:
        input.kind = jellyframe::ScriptTaskInputKind::PointerUp;
        input.button = 0;
        break;
    case BoardInputKind::Wheel: input.kind = jellyframe::ScriptTaskInputKind::Wheel; break;
    default:
        state.input_unsupported.fetch_add(1);
        return;
    }
    const auto posted = jellyframe::post_script_task_input(
        *state.protocol, state.script_session, next_sequence++, input, {32, 256});
    if (posted.accepted()) {
        state.input_posted.fetch_add(1);
    } else {
        state.input_rejected.fetch_add(1);
    }
}

void run_installed_bundle_ui(InstalledBundleScriptSession* state) {
    boards::BoardRuntime board = boards::initialize_selected_board();
    const auto fail = [&] {
        state->fatal_seen.store(true);
        state->stop.store(true);
        boards::release_board_runtime(board);
    };
    const int width = board.profile.display.width;
    const int height = board.profile.display.height;
    if (width != state->width || height != state->height || width <= 0 || height <= 0) {
        ESP_LOGE(kTag, "ui viewport mismatch expected=%dx%d actual=%dx%d",
                 state->width, state->height, width, height);
        fail();
        return;
    }
    boards::attach_input_queue(board, &state->input_queue);
    jellyframe::FrameBuffer framebuffer(width, height, {248, 250, 252, 255});
    if (framebuffer.width != width || framebuffer.height != height) {
        ESP_LOGE(kTag, "ui framebuffer allocation failed for %dx%d", width, height);
        fail();
        return;
    }
    const std::size_t pixel_count = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixel_count]);
    if (!packed) {
        fail();
        return;
    }
    Rgb565Panel panel;
    panel.width = width;
    panel.height = height;
    panel.stride_pixels = width;
    panel.packed_flush = board.packed_flush;
    panel.packed_scroll_flush = board.packed_scroll_flush;
    panel.reset_scroll = board.reset_scroll;
    panel.flush_context = board.flush_context;
    panel.pixels = packed.get();
    panel.packed_pixels = packed.get();
    panel.packed_pixel_capacity = pixel_count;
    auto packed_sink = make_rgb565_sink(panel);
    auto region_sink = make_packed_rgb565_sink(panel);
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED && JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    const bool partial_enabled = board.profile.id == boards::BoardId::WaveshareEsp32s3TouchLcd147 &&
        board.hardware_display_ready && installed_text_pipeline_frame_version(state->text_pipeline) == 10;
#else
    constexpr bool partial_enabled = false;
#endif
    const jellyframe::HostFrameSink sink = partial_enabled
        ? jellyframe::embedded_packed_rgb565_sink(region_sink)
        : jellyframe::embedded_frame_sink(packed_sink);
    jellyframe::ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.max_clip_depth = 8;
    renderer_options.max_temporary_pixels = pixel_count;
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    // Canonical replay charges one staging frame before Core offscreen work.
    renderer_options.max_temporary_pixels = 2 * pixel_count;
    if (state->runtime_resources) {
        state->runtime_resources->bind_ui_resources(renderer_options);
    }
#endif
    const jellyframe::ScriptTaskFrameRenderer renderer(renderer_options);
    jellyframe::SoftwareRasterizerScratch scratch;
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    jellyframe::ScriptTaskAcceptedAppFrame previous;
#endif
    while (!state->stop.load()) {
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
        jellyframe::ScriptTaskAcceptedAppFrame frame;
        const auto take_status = take_installed_core_text_frame(
            *state->protocol, state->script_session, frame_codec_options(state->text_pipeline), frame);
        const std::uint32_t packet_sequence = frame.sequence();
#else
        jellyframe::ScriptTaskAppFrame frame;
        std::uint32_t packet_sequence = 0;
        const auto take_status = jellyframe::take_script_task_app_frame(
            *state->protocol, state->script_session, frame_codec_options(state->text_pipeline), frame, &packet_sequence);
#endif
        if (take_status == jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            jellyframe::ScriptTaskFrameDamage damage{
                jellyframe::ScriptTaskFrameDamageKind::Full, {0, 0, width, height}};
            auto render_status = jellyframe::ScriptTaskFrameRenderStatus::Accepted;
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
            if (partial_enabled && previous.valid() &&
                previous.session() == frame.session() &&
                previous.text_catalog() == frame.text_catalog() &&
                previous.image_provider() == frame.image_provider()) {
                damage = jellyframe::plan_script_task_group_damage(renderer, previous.frame(), frame.frame());
            }
            const bool unchanged = damage.kind == jellyframe::ScriptTaskFrameDamageKind::Unchanged;
            const bool rendered = unchanged || render_installed_core_text_frame(renderer, frame, framebuffer,
                {248, 250, 252, 255}, &damage.rect, 1, &scratch, &render_status);
#else
            constexpr bool unchanged = false;
            const bool rendered = renderer.render_into(frame, framebuffer, {248, 250, 252, 255},
                                                       &damage.rect, 1, &scratch, &render_status);
#endif
            state->render_status.store(static_cast<std::uint8_t>(render_status));
            const bool presented = rendered && !unchanged &&
                jellyframe::present_frame(framebuffer, sink, &damage.rect, 1);
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
            if (partial_enabled && !unchanged) {
                // A failed flush may have already changed some panel pixels.
                // The next accepted frame must repaint and submit the whole screen.
                if (presented) previous = std::move(frame);
                else previous = {};
            }
#endif
            if (presented || unchanged) {
                state->accepted_frame_seq.store(packet_sequence);
                if (presented && state->malformed_v4_probe_rejected.load()) {
                    state->accepted_after_malformed_v4_probe.fetch_add(1);
                }
            } else {
                state->present_failures.fetch_add(1);
                ESP_LOGW(kTag, "ui present rejected app=%s take=%u render=%u",
                         state->app_id.c_str(), static_cast<unsigned>(take_status),
                         static_cast<unsigned>(render_status));
            }
        } else if (take_status == jellyframe::ScriptTaskAppFrameTakeStatus::DecodeRejected &&
                   state->malformed_v4_probe_published.load()) {
            state->malformed_v4_probe_rejected.store(true);
            ESP_LOGI(kTag, "v4 malformed probe rejected app=%s", state->app_id.c_str());
        }
        vTaskDelay(kWorkerPollTicks);
    }
    scratch.release();
    boards::release_board_runtime(board);
}

void ui_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    state->ui_started.store(true);
    run_installed_bundle_ui(state);
    state->ui_done.store(true);
    // The supervisor deletes this suspended WithCaps task after the packed
    // surface, raster scratch and board runtime have all been released.
    vTaskSuspend(nullptr);
}

// A Jerry fatal leaves live C++ owners on the worker stack and can leave VM
// locks held. Keep this supervisor stack, protocol and session alive until
// the lifecycle task resets the device. The semaphore only wakes a pending
// stop call; that call must distinguish quarantine from normal completion.
void quarantine_on_vm_fatal(InstalledBundleScriptSession& state, TaskHandle_t worker) {
    TaskHandle_t owner = nullptr;
    int code = -1;
    if (worker == nullptr || state.worker_done.load() ||
        !jellyframe_jerry_port_try_get_fatal(&owner, &code) || owner != worker) return;
    state.stop.store(true);
    state.vm_fatal_code.store(code);
    xSemaphoreGive(state.stopped);
    for (;;) vTaskSuspend(nullptr);
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->host == nullptr || state->stopped == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    {
    jellyframe::ScriptTaskSupervisor protocol(supervisor_options());
    state->script_session = begin_installed_script_session(
        protocol, state->app_instance_id, state->runtime_resources);
    state->protocol = &protocol;
    jellyframe::ScriptTaskServiceBridge bridge(*state->host, protocol, {8});
    TaskHandle_t worker = nullptr;
    TaskHandle_t ui = nullptr;
    BaseType_t worker_created = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    BaseType_t ui_created = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    if (!state->stop.load()) {
        // This Runtime profile reserves internal RAM for the protocol and
        // transport. The worker owns a comparatively deep JerryScript stack,
        // so place it in the PSRAM-capable task allocator like the UI task.
        worker_created = xTaskCreateWithCaps(
            worker_entry, "jf_app_script", CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE,
            state, 6, &worker, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ui_created = xTaskCreateWithCaps(ui_entry, "jf_app_script_ui", CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
                                         state, CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY, &ui,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (worker_created != pdPASS || ui_created != pdPASS) {
        state->fatal_seen.store(true);
        if (worker_created != pdPASS) state->init_status.store(0xfe);
        if (worker_created != pdPASS) state->worker_done.store(true);
        if (ui_created != pdPASS) state->ui_done.store(true);
        state->stop.store(true);
    }
    ESP_LOGI(kTag, "session app=%s generation=%u worker=%d ui=%d viewport=%dx%d",
             state->app_id.c_str(), static_cast<unsigned>(state->generation),
             worker_created == pdPASS ? 1 : 0, ui_created == pdPASS ? 1 : 0, state->width, state->height);
    quarantine_on_vm_fatal(*state, worker);
    sample_resources(*state, worker, ui);
    std::int64_t last_resource_us = esp_timer_get_time();
    jellyframe::AppFrameScratch completion_scratch;
    completion_scratch.reserve_from_options({8, 4, 8, 2048, 1});
    while (!state->stop.load() && !state->worker_done.load()) {
        quarantine_on_vm_fatal(*state, worker);
        const std::int64_t now_us = esp_timer_get_time();
        if (now_us - last_resource_us >= 1000000) {
            sample_resources(*state, worker, ui);
            last_resource_us = now_us;
        }
        (void)bridge.pump_service_requests();
        (void)bridge.pump(completion_scratch);
        jellyframe::ScriptTaskPacket fatal;
        if (protocol.take_fatal(fatal)) {
            state->fatal_seen.store(true);
            state->stop.store(true);
            ESP_LOGE(kTag, "fatal app=%s generation=%u payload=%u", state->app_id.c_str(),
                     static_cast<unsigned>(state->generation), static_cast<unsigned>(fatal.payload.size()));
        }
        vTaskDelay(kSupervisorPollTicks);
    }
    // Stop accepting input and pumping services. Wait for cooperative worker
    // and UI exit before touching the protocol in teardown: a VM fatal may
    // otherwise leave owners or synchronization held indefinitely.
    state->stop.store(true);
    while (!state->worker_done.load() || !state->ui_done.load()) {
        quarantine_on_vm_fatal(*state, worker);
        vTaskDelay(kSupervisorPollTicks);
    }
    (void)protocol.begin_teardown(state->script_session);
    (void)bridge.begin_teardown(state->script_session);
    sample_resources(*state, worker, ui);
    if (worker != nullptr) vTaskDeleteWithCaps(worker);
    if (ui != nullptr) vTaskDeleteWithCaps(ui);
    // AppInstalledBundleBinding owns the Runtime host and bundle lease. This
    // task may retire worker-local protocol state, but lifecycle performs the
    // host teardown only after stop_installed_bundle_script_task() returns.
    (void)bridge.complete_teardown(state->script_session);
    (void)protocol.complete_teardown(state->script_session);
    completion_scratch.release();
    state->protocol = nullptr;
    }
    ESP_LOGI(kTag,
             "session stopped app=%s fatal=%d input_posted=%u input_rejected=%u input_unsupported=%u input_dropped=%u input_seq=%u mutation_seq=%u published_seq=%u accepted_seq=%u presents_failed=%u",
             state->app_id.c_str(), state->fatal_seen.load() ? 1 : 0,
             static_cast<unsigned>(state->input_posted.load()), static_cast<unsigned>(state->input_rejected.load()),
             static_cast<unsigned>(state->input_unsupported.load()),
             static_cast<unsigned>(state->input_queue.dropped_count()),
             static_cast<unsigned>(state->input_packet_seq.load()), static_cast<unsigned>(state->js_mutation_seq.load()),
             static_cast<unsigned>(state->published_frame_seq.load()), static_cast<unsigned>(state->accepted_frame_seq.load()),
             static_cast<unsigned>(state->present_failures.load()));
    xSemaphoreGive(state->stopped);
    // stop_installed_bundle_script_task() owns the final external deletion.
    // No session state is accessed after this handoff.
    vTaskSuspend(nullptr);
}

} // namespace

bool start_installed_bundle_script_task(std::string app_id,
                                        std::uint32_t generation,
                                        std::uint32_t app_instance_id,
                                        std::string entry_path,
                                        std::string entry_document,
                                        InstalledResourceSnapshot resources,
                                        jellyframe::AppRuntimeHost& host,
                                        InstalledBundleScriptSession*& session,
                                        jellyframe::AppInstalledTextPipeline text_pipeline) {
    session = nullptr;
    if (jellyframe_jerry_port_try_get_fatal(nullptr, nullptr)) return false;
    if (!installed_text_pipeline_supported(text_pipeline)) return false;
    const boards::BoardProfile& profile = boards::selected_board_profile();
    if (app_id.empty() || app_instance_id == 0 || entry_path.empty() || entry_document.empty() ||
        profile.display.width <= 0 || profile.display.height <= 0 || !resources.rebuild_views()) {
        return false;
    }
    const jellyframe::HostBudgets budgets = installed_script_budgets(profile.display.width, profile.display.height);
    ResourceLoadStats stats;
    ResourceBundleContext resource_context = make_resource_context(budgets, entry_path, resources.bundle, &stats);
    jellyframe::HtmlParser parser;
    const std::unique_ptr<jellyframe::Node> document = parser.parse(
        entry_document, jellyframe::html_parser_options_from_budgets(budgets));
    if (!document || !prepare_document_styles(*document, entry_path)) return false;
    resource_context.prepare_stylesheets = true;
    std::string author_css = jellyframe::combine_author_css(
        "", *document, load_linked_stylesheet, &resource_context,
        jellyframe::document_style_collection_options_from_budgets(budgets));
    std::vector<jellyframe::DocumentScript> scripts = jellyframe::collect_classic_scripts(
        *document, load_classic_script, &resource_context,
        jellyframe::document_script_collection_options_from_budgets(budgets));
    std::size_t script_bytes = 0;
    for (const jellyframe::DocumentScript& script : scripts) {
        if (script.source.size() > budgets.max_resource_bytes ||
            script_bytes > budgets.max_resource_bytes - script.source.size()) {
            return false;
        }
        script_bytes += script.source.size();
    }
    if (author_css.size() > budgets.max_resource_bytes || stats.missing_loads != 0 || stats.rejected_loads != 0) {
        return false;
    }
    auto* next = new (std::nothrow) InstalledBundleScriptSession();
    if (next == nullptr) return false;
    next->stopped = xSemaphoreCreateBinary();
    if (next->stopped == nullptr) {
        delete next;
        return false;
    }
    next->app_id = std::move(app_id);
    next->generation = generation;
    next->app_instance_id = app_instance_id;
    next->width = profile.display.width;
    next->height = profile.display.height;
    next->entry_path = entry_path;
    next->entry_document = std::move(entry_document);
    next->author_css = std::move(author_css);
    next->scripts = std::move(scripts);
    next->host = &host;
    next->text_pipeline = std::move(text_pipeline);
#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM
    if (!host.attach_core_system_font_catalog_view(
            jellyframe::kNotoSansScSystemFontCatalog,
            jellyframe::kNotoSansScSystemFontCatalogSize)) {
        vSemaphoreDelete(next->stopped);
        delete next;
        return false;
    }
#endif
#if JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    auto static_font_owner = std::shared_ptr<const void>(
        jellyframe::kNotoSansScSystemFontCatalog, [](const void*) {});
    if (!create_installed_app_resources(std::move(resources), budgets, entry_path,
            jellyframe::kNotoSansScSystemFontCatalog,
            jellyframe::kNotoSansScSystemFontCatalogSize,
            std::move(static_font_owner), esp_timer_get_time, next->runtime_resources)) {
        vSemaphoreDelete(next->stopped);
        delete next;
        return false;
    }
#endif
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && JELLYFRAME_ESP32S3_INSTALLED_TEXT_ENABLED
    if (next->text_pipeline.status == jellyframe::AppInstalledTextPipelineStatus::Ready) {
        if (next->text_pipeline.frame_version != jellyframe::kRuntimeShapedFrameVersion || !next->text_pipeline.binding || !next->runtime_resources) {
            vSemaphoreDelete(next->stopped); delete next; return false;
        }
        auto selected = std::make_shared<InstalledAppResources>(*next->runtime_resources);
        selected->shaped_text_binding = next->text_pipeline.binding;
        // Shaped admission refuses image DOM; an unused image adapter must not
        // accidentally turn an otherwise image-free shaped package into v10.
        selected->image_provider.reset();
        next->runtime_resources = std::move(selected);
    }
#endif
    next->resources.heap_start = observe_heap();
    next->resources.heap_current = next->resources.heap_start;
    next->resources.heap_sampled_min = next->resources.heap_start;
    next->resources.resource_samples = 1;
    const BaseType_t created = xTaskCreateWithCaps(
        supervisor_entry, "jf_app_supervisor", kSupervisorStackBytes, next, 7,
        &next->supervisor_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        vSemaphoreDelete(next->stopped);
        delete next;
        return false;
    }
    ESP_LOGI(kTag, "launch prepared app=%s css=%u scripts=%u resources=%u loads=%u missing=%u rejected=%u",
             next->app_id.c_str(), static_cast<unsigned>(next->author_css.size()),
             static_cast<unsigned>(next->scripts.size()), static_cast<unsigned>(resources.entries.size()),
             static_cast<unsigned>(stats.successful_loads), static_cast<unsigned>(stats.missing_loads),
             static_cast<unsigned>(stats.rejected_loads));
    session = next;
    return true;
}

bool stop_installed_bundle_script_task(InstalledBundleScriptSession*& session,
                                       std::uint32_t timeout_ms,
                                       InstalledBundleScriptTaskTelemetry* telemetry) {
    if (session == nullptr) return true;
    if (session->stopped == nullptr) return false;
    if (session->vm_fatal_code.load() != -1) return false;
    session->stop.store(true);
    if (xSemaphoreTake(session->stopped, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    if (session->vm_fatal_code.load() != -1) return false;
    if (telemetry != nullptr) *telemetry = telemetry_snapshot(*session);
    if (session->supervisor_task != nullptr) {
        vTaskDeleteWithCaps(session->supervisor_task);
        session->supervisor_task = nullptr;
    }
    vSemaphoreDelete(session->stopped);
    delete session;
    session = nullptr;
    return true;
}

bool installed_bundle_script_task_has_fatal(const InstalledBundleScriptSession* session) {
    return session != nullptr && session->fatal_seen.load();
}

InstalledScriptVmFault installed_bundle_script_task_vm_fault(const InstalledBundleScriptSession* session) {
    if (session == nullptr) return {};
    const int code = session->vm_fatal_code.load();
    return code == -1 ? InstalledScriptVmFault{} :
        InstalledScriptVmFault{code, session->generation, session->ui_done.load()};
}

InstalledBundleScriptTaskTelemetry installed_bundle_script_task_telemetry(const InstalledBundleScriptSession* session) {
    return session == nullptr ? InstalledBundleScriptTaskTelemetry{} : telemetry_snapshot(*session);
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_installed_bundle_script_task(std::string, std::uint32_t, std::uint32_t, std::string, std::string,
                                        InstalledResourceSnapshot, jellyframe::AppRuntimeHost&,
                                        InstalledBundleScriptSession*&, jellyframe::AppInstalledTextPipeline) { return false; }
bool stop_installed_bundle_script_task(InstalledBundleScriptSession*&, std::uint32_t,
                                       InstalledBundleScriptTaskTelemetry*) { return false; }
bool installed_bundle_script_task_has_fatal(const InstalledBundleScriptSession*) { return false; }
InstalledScriptVmFault installed_bundle_script_task_vm_fault(const InstalledBundleScriptSession*) { return {}; }
InstalledBundleScriptTaskTelemetry installed_bundle_script_task_telemetry(const InstalledBundleScriptSession*) { return {}; }
} // namespace jellyframe_esp32s3

#endif
