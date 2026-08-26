#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#include "jellyframe_esp32s3_input.h"

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

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameInstalledScript";
constexpr std::size_t kMaxCommands = 384;
constexpr std::size_t kMaxFrameBytes = 64u * 1024u;
constexpr std::size_t kMaxScriptBytes = 48u * 1024u;
constexpr std::uint32_t kWorkerPollMs = 4;
constexpr std::uint32_t kSupervisorPollMs = 8;

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

jellyframe::ScriptTaskAppFrameCodecOptions frame_codec_options() {
    jellyframe::ScriptTaskAppFrameCodecOptions options;
    options.max_commands = kMaxCommands;
    options.max_text_bytes = 12u * 1024u;
    options.max_input_targets = 48;
    options.max_payload_bytes = kMaxFrameBytes;
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
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> initialized{false};
    std::atomic<bool> fatal_seen{false};
    std::atomic<bool> worker_started{false};
    std::atomic<bool> ui_started{false};
    std::atomic<std::uint8_t> init_status{0xff};
    std::atomic<std::uint8_t> fatal_reason{0};
    std::atomic<std::uint32_t> input_packet_seq{0};
    std::atomic<std::uint32_t> js_mutation_seq{0};
    std::atomic<std::uint32_t> published_frame_seq{0};
    std::atomic<std::uint32_t> accepted_frame_seq{0};
    std::atomic<std::uint32_t> present_failures{0};
    std::uint32_t app_instance_id = 0;
    std::uint32_t generation = 0;
    int width = 0;
    int height = 0;
    std::string app_id;
    std::string entry_document;
    std::string author_css;
    std::vector<jellyframe::DocumentScript> scripts;
    jellyframe::AppRuntimeHost* host = nullptr;
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession script_session{};
    BoardInputQueue input_queue;
    SemaphoreHandle_t stopped = nullptr;
    TaskHandle_t supervisor_task = nullptr;
};

namespace {

jellyframe::ScriptTaskWorkerRuntimeOptions worker_options(const InstalledBundleScriptSession& state) {
    jellyframe::ScriptTaskWorkerRuntimeOptions options;
    options.budgets = installed_script_budgets(state.width, state.height);
    options.script = jellyframe::jerryscript_runtime_options_from_host_budgets(options.budgets);
    options.viewport = {0, 0, state.width, state.height};
    options.frame_codec = frame_codec_options();
    options.input_codec = {32, 256};
    options.service_request_codec = {64};
    return options;
}

InstalledBundleScriptTaskTelemetry telemetry_snapshot(const InstalledBundleScriptSession& state) {
    InstalledBundleScriptTaskTelemetry telemetry;
    telemetry.initialized = state.initialized.load();
    telemetry.fatal = state.fatal_seen.load();
    telemetry.worker_started = state.worker_started.load();
    telemetry.ui_started = state.ui_started.load();
    telemetry.init_status = state.init_status.load();
    telemetry.fatal_reason = state.fatal_reason.load();
    telemetry.scripts = static_cast<std::uint32_t>(state.scripts.size());
    telemetry.input_seq = state.input_packet_seq.load();
    telemetry.mutation_seq = state.js_mutation_seq.load();
    telemetry.published_seq = state.published_frame_seq.load();
    telemetry.accepted_seq = state.accepted_frame_seq.load();
    telemetry.presents_failed = state.present_failures.load();
    return telemetry;
}

void worker_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    state->worker_started.store(true);
    jellyframe::ScriptTaskWorkerRuntime runtime(state->script_session, worker_options(*state));
    const auto initialized = runtime.initialize(state->entry_document, state->author_css);
    state->init_status.store(static_cast<std::uint8_t>(initialized));
    state->initialized.store(initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted);
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        for (const jellyframe::DocumentScript& script : state->scripts) {
            if (state->stop.load()) break;
            const auto result = runtime.eval_with_supervisor(*state->protocol, script.source, script.name);
            if (!result.ok || runtime.fatal()) break;
        }
        if (!state->stop.load() && !runtime.fatal()) {
            (void)runtime.publish_frame(*state->protocol);
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
    while (!state->stop.load() && !runtime.fatal()) {
        const auto input = runtime.process_one(*state->protocol);
        const auto callbacks = runtime.pump_callbacks(
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000), *state->protocol);
        const auto telemetry = runtime.telemetry();
        state->input_packet_seq.store(telemetry.input_packet_seq);
        state->js_mutation_seq.store(telemetry.js_mutation_seq);
        state->published_frame_seq.store(telemetry.published_frame_seq);
        if (input.fatal || callbacks.fatal) break;
        vTaskDelay(pdMS_TO_TICKS(kWorkerPollMs));
    }
    if (runtime.fatal()) {
        state->fatal_seen.store(true);
        state->fatal_reason.store(static_cast<std::uint8_t>(runtime.fatal_record().reason));
        (void)runtime.publish_fatal(*state->protocol);
    }
    runtime.stop();
    state->worker_done.store(true);
    vTaskDelete(nullptr);
}

bool post_board_input(InstalledBundleScriptSession& state,
                      const BoardInputEvent& event,
                      std::uint32_t& next_sequence) {
    if (state.protocol == nullptr || state.stop.load()) return false;
    jellyframe::ScriptTaskInputEvent input;
    input.x = event.x;
    input.y = event.y;
    input.delta_x = event.delta_x;
    input.delta_y = event.delta_y;
    switch (event.kind) {
    case BoardInputKind::PointerDown: input.kind = jellyframe::ScriptTaskInputKind::PointerDown; break;
    case BoardInputKind::PointerMove: input.kind = jellyframe::ScriptTaskInputKind::PointerMove; break;
    case BoardInputKind::PointerUp: input.kind = jellyframe::ScriptTaskInputKind::PointerUp; break;
    case BoardInputKind::Wheel: input.kind = jellyframe::ScriptTaskInputKind::Wheel; break;
    default: return false;
    }
    const auto posted = jellyframe::post_script_task_input(
        *state.protocol, state.script_session, next_sequence++, input, {32, 256});
    return posted.accepted();
}

void ui_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    state->ui_started.store(true);
    boards::BoardRuntime board = boards::initialize_selected_board();
    const int width = board.profile.display.width;
    const int height = board.profile.display.height;
    if (width != state->width || height != state->height || width <= 0 || height <= 0) {
        ESP_LOGE(kTag, "ui viewport mismatch expected=%dx%d actual=%dx%d",
                 state->width, state->height, width, height);
        state->fatal_seen.store(true);
        state->stop.store(true);
        boards::release_board_runtime(board);
        state->ui_done.store(true);
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    boards::attach_input_queue(board, &state->input_queue);
    jellyframe::FrameBuffer framebuffer(width, height, {248, 250, 252, 255});
    const std::size_t pixel_count = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixel_count]);
    if (!packed) {
        state->fatal_seen.store(true);
        state->stop.store(true);
        boards::release_board_runtime(board);
        state->ui_done.store(true);
        vTaskDeleteWithCaps(nullptr);
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
    const jellyframe::HostFrameSink sink = jellyframe::embedded_frame_sink(packed_sink);
    jellyframe::ScriptTaskFrameRendererOptions renderer_options;
    renderer_options.max_clip_depth = 8;
    renderer_options.max_temporary_pixels = pixel_count;
    const jellyframe::ScriptTaskFrameRenderer renderer(make_production_text_painter(), renderer_options);
    jellyframe::SoftwareRasterizerScratch scratch;
    std::uint32_t input_sequence = 1;
    while (!state->stop.load()) {
        jellyframe::ScriptTaskAppFrame frame;
        std::uint32_t packet_sequence = 0;
        const auto take_status = jellyframe::take_script_task_app_frame(
            *state->protocol, state->script_session, frame_codec_options(), frame, &packet_sequence);
        if (take_status == jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            const jellyframe::Rect full{0, 0, width, height};
            jellyframe::ScriptTaskFrameRenderStatus render_status;
            const bool rendered = renderer.render_into(frame, framebuffer, {248, 250, 252, 255},
                                                       &full, 1, &scratch, &render_status);
            const bool presented = rendered && jellyframe::present_frame(framebuffer, sink, &full, 1);
            if (presented) {
                state->accepted_frame_seq.store(packet_sequence);
            } else {
                state->present_failures.fetch_add(1);
                ESP_LOGW(kTag, "ui present rejected app=%s take=%u render=%u",
                         state->app_id.c_str(), static_cast<unsigned>(take_status),
                         static_cast<unsigned>(render_status));
            }
        }
        BoardInputEvent event;
        std::size_t event_count = 0;
        while (event_count++ < 8 && state->input_queue.dequeue(event)) {
            (void)post_board_input(*state, event, input_sequence);
        }
        vTaskDelay(pdMS_TO_TICKS(kWorkerPollMs));
    }
    scratch.release();
    boards::release_board_runtime(board);
    state->ui_done.store(true);
    vTaskDeleteWithCaps(nullptr);
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<InstalledBundleScriptSession*>(raw);
    if (state == nullptr || state->host == nullptr || state->stopped == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    jellyframe::ScriptTaskSupervisor protocol(supervisor_options());
    state->script_session = protocol.begin(state->app_instance_id);
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
    jellyframe::AppFrameScratch completion_scratch;
    completion_scratch.reserve_from_options({8, 4, 8, 2048, 1});
    while (!state->stop.load() && !state->worker_done.load()) {
        (void)bridge.pump_service_requests();
        (void)bridge.pump(completion_scratch);
        jellyframe::ScriptTaskPacket fatal;
        if (protocol.take_fatal(fatal)) {
            state->fatal_seen.store(true);
            state->stop.store(true);
            ESP_LOGE(kTag, "fatal app=%s generation=%u payload=%u", state->app_id.c_str(),
                     static_cast<unsigned>(state->generation), static_cast<unsigned>(fatal.payload.size()));
        }
        vTaskDelay(pdMS_TO_TICKS(kSupervisorPollMs));
    }
    // Input is gated before this point. Teardown then waits until the worker
    // has destroyed its private JerryScript/DOM state and UI has dropped all
    // decoded frame values before host lifecycle ownership can change.
    state->stop.store(true);
    (void)protocol.begin_teardown(state->script_session);
    (void)bridge.begin_teardown(state->script_session);
    while (!state->worker_done.load() || !state->ui_done.load()) {
        vTaskDelay(pdMS_TO_TICKS(kSupervisorPollMs));
    }
    // The script supervisor is the only task that touches AppRuntimeHost
    // while a worker is active. After both consumers have stopped, retire the
    // host instance before completing service teardown; Device Runtime later
    // releases the installed-bundle lease through its binding.
    (void)state->host->terminate_current(
        state->fatal_seen.load() ? jellyframe::AppTeardownReason::RuntimeError
                                 : jellyframe::AppTeardownReason::NormalExit);
    (void)bridge.complete_teardown(state->script_session);
    (void)protocol.complete_teardown(state->script_session);
    completion_scratch.release();
    state->protocol = nullptr;
    ESP_LOGI(kTag,
             "session stopped app=%s fatal=%d input_seq=%u mutation_seq=%u published_seq=%u accepted_seq=%u presents_failed=%u",
             state->app_id.c_str(), state->fatal_seen.load() ? 1 : 0,
             static_cast<unsigned>(state->input_packet_seq.load()), static_cast<unsigned>(state->js_mutation_seq.load()),
             static_cast<unsigned>(state->published_frame_seq.load()), static_cast<unsigned>(state->accepted_frame_seq.load()),
             static_cast<unsigned>(state->present_failures.load()));
    xSemaphoreGive(state->stopped);
    vTaskDelete(nullptr);
}

} // namespace

bool start_installed_bundle_script_task(std::string app_id,
                                        std::uint32_t generation,
                                        std::uint32_t app_instance_id,
                                        std::string entry_path,
                                        std::string entry_document,
                                        InstalledResourceSnapshot resources,
                                        jellyframe::AppRuntimeHost& host,
                                        InstalledBundleScriptSession*& session) {
    session = nullptr;
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
    if (!document) return false;
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
    next->entry_document = std::move(entry_document);
    next->author_css = std::move(author_css);
    next->scripts = std::move(scripts);
    next->host = &host;
    const BaseType_t created = xTaskCreate(supervisor_entry, "jf_app_supervisor", 12288 / sizeof(StackType_t),
                                           next, 7, &next->supervisor_task);
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
    session->stop.store(true);
    if (xSemaphoreTake(session->stopped, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return false;
    if (telemetry != nullptr) *telemetry = telemetry_snapshot(*session);
    vSemaphoreDelete(session->stopped);
    delete session;
    session = nullptr;
    return true;
}

bool installed_bundle_script_task_has_fatal(const InstalledBundleScriptSession* session) {
    return session != nullptr && session->fatal_seen.load();
}

InstalledBundleScriptTaskTelemetry installed_bundle_script_task_telemetry(const InstalledBundleScriptSession* session) {
    return session == nullptr ? InstalledBundleScriptTaskTelemetry{} : telemetry_snapshot(*session);
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_installed_bundle_script_task(std::string, std::uint32_t, std::uint32_t, std::string, std::string,
                                        InstalledResourceSnapshot, jellyframe::AppRuntimeHost&,
                                        InstalledBundleScriptSession*&) { return false; }
bool stop_installed_bundle_script_task(InstalledBundleScriptSession*&, std::uint32_t,
                                       InstalledBundleScriptTaskTelemetry*) { return false; }
bool installed_bundle_script_task_has_fatal(const InstalledBundleScriptSession*) { return false; }
InstalledBundleScriptTaskTelemetry installed_bundle_script_task_telemetry(const InstalledBundleScriptSession*) { return {}; }
} // namespace jellyframe_esp32s3

#endif
