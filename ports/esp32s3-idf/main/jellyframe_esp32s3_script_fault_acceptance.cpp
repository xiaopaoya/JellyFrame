#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_FAULT_RECOVERY_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_FAULT_RECOVERY_ACCEPTANCE && \
    defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_fatal_codec.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "render_core/budget.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/software_renderer.h"
#include "script/script_task_worker_runtime.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;
constexpr char kTag[] = "JellyFrameScriptFault";
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr std::uint32_t kCyclesPerFault = 30;
constexpr std::string_view kHtml =
    "<!doctype html><html><body><main><h1>Recovery</h1>"
    "<p id='status'>Replacement ready</p></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;padding:18px;background:#0f172a;color:#f8fafc;}"
    "main{padding:16px;background:#1e293b;border-radius:12px;}"
    "h1{font-size:22px;color:#86efac;}p{font-size:16px;}";
constexpr std::string_view kNormalJs =
    "const s=document.getElementById('status'); s.textContent='Replacement ready';";

enum class FaultKind : std::uint8_t {
    ScriptException,
    ScriptWatchdog,
    BudgetExceeded,
    LoadFailure,
    AppExit,
    NormalReplacement,
    FatalBoundary,
};

const char* fault_name(FaultKind kind) {
    switch (kind) {
    case FaultKind::ScriptException: return "script-exception";
    case FaultKind::ScriptWatchdog: return "script-watchdog";
    case FaultKind::BudgetExceeded: return "budget-exceeded";
    case FaultKind::LoadFailure: return "load-failure";
    case FaultKind::AppExit: return "app-exit";
    case FaultKind::NormalReplacement: return "normal-replacement";
    case FaultKind::FatalBoundary: return "script-fatal";
    }
    return "unknown";
}

struct FaultState {
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> worker_fatal{false};
    std::atomic<bool> accept_frames{false};
    std::atomic<bool> launcher_pending{false};
    std::atomic<bool> replacement_ok{false};
    std::atomic<std::uint32_t> current_app_id{0};
    std::atomic<std::uint32_t> current_generation{0};
    std::atomic<std::uint32_t> current_epoch{0};
    std::atomic<std::uint32_t> cycles{0};
    std::atomic<std::uint32_t> fatal_records{0};
    std::atomic<std::uint32_t> recovery_cycles{0};
    std::atomic<std::uint32_t> launcher_presents{0};
    std::atomic<std::uint32_t> replacement_frames{0};
    std::atomic<std::uint32_t> teardown_failures{0};
    std::atomic<std::uint32_t> boundary_fatal_records{0};
    std::atomic<std::uint32_t> boundary_recovery_cycles{0};
    std::atomic<std::uint32_t> stale_packets{0};
    std::atomic<std::uint32_t> watchdogs{0};
    std::atomic<std::uint32_t> panics{0};
    std::atomic<std::uint32_t> resets{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
    std::atomic<UBaseType_t> supervisor_stack_min{0};
    std::atomic<UBaseType_t> worker_stack_min{0};
    std::atomic<UBaseType_t> ui_stack_min{0};
    ScriptTaskSupervisor* protocol = nullptr;
    ScriptAppSession session{};
    SemaphoreHandle_t ready = nullptr;
    TaskHandle_t supervisor_task = nullptr;
    TaskHandle_t worker_task = nullptr;
    TaskHandle_t ui_task = nullptr;
    FaultKind kind = FaultKind::ScriptException;
    std::uint32_t cycle = 0;
    SemaphoreHandle_t ui_started = nullptr;
};

void update_memory(FaultState& state) {
    const auto internal = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const auto psram = static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto old = state.internal_free_min.load();
    while (internal < old && !state.internal_free_min.compare_exchange_weak(old, internal)) {}
    old = state.psram_free_min.load();
    while (psram < old && !state.psram_free_min.compare_exchange_weak(old, psram)) {}
}

void update_stack(std::atomic<UBaseType_t>& target, UBaseType_t value) {
    UBaseType_t old = target.load();
    while ((old == 0 || value < old) && !target.compare_exchange_weak(old, value)) {}
}

ScriptTaskSupervisorOptions protocol_options() {
    ScriptTaskSupervisorOptions options;
    options.worker_inbox = {8, 128};
    options.frame_mailbox = {8, 0};
    options.frame_leases = {4, 64 * 1024, 128 * 1024};
    options.max_service_tombstones = 16;
    options.max_native_release_intents = 4;
    options.service_request_mailbox = {8, 64};
    options.service_payload_leases = {4, 256, 1024};
    options.fatal_mailbox = {4, 64};
    return options;
}

HostBudgets fault_budgets() {
    HostBudgets budgets;
    budgets.max_dom_nodes = 64;
    budgets.max_dom_depth = 12;
    budgets.max_attributes_per_element = 12;
    budgets.max_css_rules = 16;
    budgets.max_css_declarations_per_rule = 16;
    budgets.max_render_objects = 64;
    budgets.max_layout_boxes = 64;
    budgets.max_layers = 16;
    budgets.max_display_commands = 128;
    budgets.max_dirty_rects = 8;
    budgets.max_input_events_per_frame = 8;
    budgets.max_timers = 8;
    budgets.max_timer_callbacks_per_frame = 4;
    budgets.max_event_listeners = 8;
    budgets.max_resource_bytes = 16 * 1024;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(kWidth) * kHeight;
    budgets.max_script_execution_checks = 512;
    budgets.script_execution_check_interval = 32;
    return budgets;
}

ScriptTaskWorkerRuntimeOptions worker_options() {
    const auto budgets = fault_budgets();
    ScriptTaskWorkerRuntimeOptions options;
    options.budgets = budgets;
    options.script = script_runtime_options_from_host_budgets(budgets);
    options.viewport = {0, 0, kWidth, kHeight};
    // The recovery fixture only publishes a small replacement frame. Keep
    // its per-worker codec reservation bounded so repeated session teardown
    // cannot amplify internal-heap fragmentation.
    options.frame_codec = {64, 4 * 1024, 8, 4 * 1024};
    options.input_codec = {8, 128};
    options.service_request_codec = {32};
    return options;
}

ScriptAppSession session_snapshot(const FaultState& state) {
    return {state.current_app_id.load(), state.current_generation.load(), state.current_epoch.load()};
}

void publish_normal_frame(FaultState& state, ScriptTaskWorkerRuntime& runtime) {
    const auto result = runtime.publish_frame(*state.protocol);
    if (result.accepted()) {
        ESP_LOGI(kTag, "fault_replacement_frame cycle=%u accepted=1", static_cast<unsigned>(state.cycle));
        state.replacement_ok.store(true);
    }
}

void fault_worker_entry(void* raw) {
    auto* state = static_cast<FaultState*>(raw);
    if (state == nullptr || state->protocol == nullptr) { vTaskDelete(nullptr); return; }
    const FaultKind kind = state->kind;
    const ScriptAppSession session = state->session;
    ScriptTaskWorkerRuntime runtime(session, worker_options());
    std::string html(kHtml);
    std::string css(kCss);
    if (kind == FaultKind::BudgetExceeded) html.assign(20 * 1024, 'x');
    if (kind == FaultKind::LoadFailure) html.clear();
    const auto initialized = runtime.initialize(html, css);
    ESP_LOGI(kTag, "fault_worker cycle=%u kind=%s initialized=%d session=%" PRIu32 "/%" PRIu32 "/%" PRIu32,
             static_cast<unsigned>(state->cycle), fault_name(kind),
             initialized == ScriptTaskWorkerRuntimeInitStatus::Accepted ? 1 : 0,
             session.app_instance_id, session.generation, session.worker_epoch);
    if (initialized == ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        if (kind == FaultKind::ScriptException) {
            (void)runtime.eval_with_supervisor(*state->protocol,
                "throw new Error('p3 fault harness');", "fault_lab.js");
        } else if (kind == FaultKind::FatalBoundary) {
            const bool injected = runtime.inject_c_safe_fatal_for_test(*state->protocol, 0x50333346);
            ESP_LOGI(kTag, "fault_worker cycle=%u kind=script-fatal c_safe_injected=%d",
                     static_cast<unsigned>(state->cycle), injected ? 1 : 0);
        } else if (kind == FaultKind::ScriptWatchdog) {
            (void)runtime.eval_with_supervisor(*state->protocol,
                "for(let i=0;i<1000000;i++){}", "fault_watchdog.js");
        } else if (kind == FaultKind::AppExit) {
            ESP_LOGI(kTag, "fault_worker cycle=%u kind=app-exit requested=1", static_cast<unsigned>(state->cycle));
        } else {
            (void)runtime.eval_with_supervisor(*state->protocol, kNormalJs, "replacement.js");
            publish_normal_frame(*state, runtime);
        }
    }
    if (runtime.fatal()) {
        state->worker_fatal.store(true);
        if (runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog) {
            state->watchdogs.fetch_add(1);
        }
        (void)runtime.publish_fatal(*state->protocol);
    }
    update_stack(state->worker_stack_min, uxTaskGetStackHighWaterMark(nullptr));
    runtime.stop();
    state->worker_done.store(true);
    vTaskDelete(nullptr);
}

void present_launcher(FaultState& state, FrameBuffer& framebuffer, const HostFrameSink& sink) {
    framebuffer.clear({8, 12, 24, 255});
    const Rect full{0, 0, framebuffer.width, framebuffer.height};
    if (present_frame(framebuffer, sink, &full, 1)) {
        state.launcher_presents.fetch_add(1);
        ESP_LOGI(kTag, "fault_launcher_present ok=1");
    }
}

void ui_entry(void* raw) {
    auto* state = static_cast<FaultState*>(raw);
    if (state == nullptr || xSemaphoreTake(state->ready, portMAX_DELAY) != pdTRUE) { vTaskDeleteWithCaps(nullptr); return; }
    boards::BoardRuntime board = boards::initialize_selected_board();
    boards::attach_input_queue(board, nullptr);
    const int width = board.profile.display.width > 0 ? board.profile.display.width : kWidth;
    const int height = board.profile.display.height > 0 ? board.profile.display.height : kHeight;
    FrameBuffer framebuffer(width, height, {8, 12, 24, 255});
    const std::size_t pixels = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixels]);
    Rgb565Panel panel;
    panel.pixels = packed.get(); panel.width = width; panel.height = height; panel.stride_pixels = width;
    panel.packed_flush = board.packed_flush; panel.packed_scroll_flush = board.packed_scroll_flush;
    panel.reset_scroll = board.reset_scroll; panel.flush_context = board.flush_context;
    panel.packed_pixels = packed.get(); panel.packed_pixel_capacity = pixels;
    auto packed_sink = make_rgb565_sink(panel);
    const HostFrameSink sink = embedded_frame_sink(packed_sink);
    const SoftwareRasterizer rasterizer(make_production_text_painter());
    xSemaphoreGive(state->ui_started);
    bool launcher_drawn = false;
    while (!state->stop.load()) {
        if (state->launcher_pending.exchange(false)) {
            present_launcher(*state, framebuffer, sink);
            launcher_drawn = true;
        }
        if (state->accept_frames.load()) {
            const ScriptAppSession session = session_snapshot(*state);
            ScriptTaskAppFrame frame;
            std::uint32_t sequence = 0;
            if (take_script_task_app_frame(*state->protocol, session, {64, 4 * 1024, 8, 4 * 1024}, frame, &sequence) ==
                ScriptTaskAppFrameTakeStatus::Accepted) {
                framebuffer.clear({8, 12, 24, 255});
                rasterizer.rasterize(frame.display_list, framebuffer, frame.viewport);
                const Rect full{0, 0, width, height};
                if (present_frame(framebuffer, sink, &full, 1)) {
                    state->replacement_frames.fetch_add(1);
                }
                launcher_drawn = false;
            }
        }
        update_stack(state->ui_stack_min, uxTaskGetStackHighWaterMark(nullptr));
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    (void)launcher_drawn;
    boards::release_board_runtime(board);
    vTaskDeleteWithCaps(nullptr);
}

bool retire_session(FaultState& state, ScriptTaskSupervisor& protocol,
                    ScriptTaskServiceBridge& bridge, AppRuntimeHost& host,
                    const ScriptAppSession& session) {
    state.accept_frames.store(false);
    const auto protocol_begin = protocol.begin_teardown(session);
    const auto bridge_begin = bridge.begin_teardown(session);
    const auto blocked_restart = protocol.begin(session.app_instance_id + 1);
    ScriptAppSession unrelated = session;
    ++unrelated.worker_epoch;
    const auto wrong_complete = protocol.complete_teardown(unrelated);
    const auto host_end = host.terminate_current(AppTeardownReason::RuntimeError);
    const auto bridge_end = bridge.complete_teardown(session);
    const auto protocol_end = protocol.complete_teardown(session);
    const bool ok = bridge_begin.awaiting_in_flight_host_completions == 0 &&
        bridge_end.retired_records == 0 && host_end.app_instance_id == session.app_instance_id &&
        protocol_begin.session == session && !blocked_restart.valid() &&
        !wrong_complete.session.valid() && protocol_end.session == session;
    if (!ok) state.teardown_failures.fetch_add(1);
    return ok;
}

bool launch_worker(FaultState& state, AppRuntimeHost& host, ScriptTaskSupervisor& protocol,
                   FaultKind kind, std::uint32_t cycle) {
    const auto app = host.launch("org.jellyframe.fixture.script_fault_lab", AppRole::App);
    state.session = protocol.begin(app.id);
    state.current_app_id.store(state.session.app_instance_id);
    state.current_generation.store(state.session.generation);
    state.current_epoch.store(state.session.worker_epoch);
    state.kind = kind;
    state.cycle = cycle;
    state.worker_done.store(false);
    state.worker_fatal.store(false);
    state.replacement_ok.store(false);
    state.accept_frames.store(true);
    const BaseType_t created = xTaskCreate(fault_worker_entry, "jf_fault_worker",
        CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE / sizeof(StackType_t), &state, 6, &state.worker_task);
    if (created != pdPASS) {
        state.teardown_failures.fetch_add(1);
        ESP_LOGE(kTag, "fault_worker_create failed kind=%s cycle=%u", fault_name(kind),
                 static_cast<unsigned>(cycle));
        return false;
    }
    return true;
}

void drain_fatals(FaultState& state, ScriptTaskSupervisor& protocol,
                   FaultKind kind, std::uint32_t cycle, const ScriptAppSession& session) {
    ScriptTaskPacket fatal;
    while (protocol.take_fatal(fatal)) {
        state.fatal_records.fetch_add(1);
        const bool session_matches = fatal.session == session;
        if (!session_matches) state.stale_packets.fetch_add(1);
        ScriptTaskFatalRecord record;
        const bool decoded = decode_script_task_fatal(fatal.payload, {40}, record) ==
            ScriptTaskFatalCodecStatus::Accepted;
        if (kind == FaultKind::FatalBoundary && session_matches && decoded &&
            record.reason == static_cast<std::uint8_t>(ScriptTaskWorkerRuntimeFatalReason::ScriptFatal)) {
            state.boundary_fatal_records.fetch_add(1);
        }
        ESP_LOGI(kTag, "fault_value cycle=%u kind=%s reason=%u session=%" PRIu32 "/%" PRIu32 "/%" PRIu32
                       " matches=%d decoded=%d",
                static_cast<unsigned>(cycle), fault_name(kind),
                fatal.payload.size() >= 2 ? static_cast<unsigned>(fatal.payload[1]) : 0,
                session.app_instance_id, session.generation, session.worker_epoch, session_matches ? 1 : 0,
                decoded ? 1 : 0);
    }
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<FaultState*>(raw);
    if (state == nullptr) vTaskDelete(nullptr);
    AppRuntimeHost host({4, 4, 4, 1024, 0});
    ScriptTaskSupervisor protocol(protocol_options());
    ScriptTaskServiceBridge bridge(host, protocol, {4});
    state->protocol = &protocol;
    const BaseType_t ui_created = xTaskCreateWithCaps(ui_entry, "jf_fault_ui", CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
        state, CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY, &state->ui_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(kTag, "script_fault_create ui=%d worker_stack_bytes=%u ui_stack_bytes=%u",
             ui_created == pdPASS ? 1 : 0, static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE),
             static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE));
    xSemaphoreGive(state->ready);
    if (ui_created != pdPASS || xSemaphoreTake(state->ui_started, pdMS_TO_TICKS(5000)) != pdTRUE) {
        state->teardown_failures.fetch_add(1);
        state->stop.store(true);
        vTaskDelete(nullptr);
        return;
    }
    const FaultKind kinds[] = {FaultKind::ScriptException, FaultKind::ScriptWatchdog,
                               FaultKind::BudgetExceeded, FaultKind::LoadFailure, FaultKind::AppExit,
                               FaultKind::FatalBoundary};
    for (FaultKind kind : kinds) {
        for (std::uint32_t cycle = 1; cycle <= kCyclesPerFault && !state->stop.load(); ++cycle) {
            update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
            if (!launch_worker(*state, host, protocol, kind, cycle)) break;
            const std::int64_t deadline = esp_timer_get_time() + 5000000;
            while (!state->worker_done.load() && esp_timer_get_time() < deadline) {
                drain_fatals(*state, protocol, kind, cycle, state->session);
                update_memory(*state);
                update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
                vTaskDelay(pdMS_TO_TICKS(8));
            }
            drain_fatals(*state, protocol, kind, cycle, state->session);
            if (!state->worker_done.load()) state->teardown_failures.fetch_add(1);
            vTaskDelay(pdMS_TO_TICKS(20));
            state->launcher_pending.store(true);
            const auto fault_session = state->session;
            (void)retire_session(*state, protocol, bridge, host, fault_session);
            state->current_app_id.store(0); state->current_generation.store(0); state->current_epoch.store(0);
            // Replacement worker proves that only the failed App was isolated.
            const std::uint32_t replacement_frames_before = state->replacement_frames.load();
            if (!launch_worker(*state, host, protocol, FaultKind::NormalReplacement, cycle)) break;
            const std::int64_t replacement_deadline = esp_timer_get_time() + 3000000;
            while (!state->worker_done.load() && esp_timer_get_time() < replacement_deadline) {
                vTaskDelay(pdMS_TO_TICKS(8));
            }
            // The worker's accepted frame is the lifecycle proof. Keep the
            // session alive briefly so the UI can consume/present it, but do
            // not make teardown depend on a cross-task telemetry race.
            vTaskDelay(pdMS_TO_TICKS(200));
            const bool replacement_ok = state->replacement_ok.load() &&
                state->replacement_frames.load() > replacement_frames_before;
            const auto replacement_session = state->session;
            (void)retire_session(*state, protocol, bridge, host, replacement_session);
            if (replacement_ok) {
                state->recovery_cycles.fetch_add(1);
                if (kind == FaultKind::FatalBoundary) state->boundary_recovery_cycles.fetch_add(1);
            } else {
                state->teardown_failures.fetch_add(1);
            }
            state->launcher_pending.store(true);
            state->cycles.fetch_add(1);
            update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
            ESP_LOGI(kTag, "fault_cycle kind=%s cycle=%u recovered=%d launcher_pending=1",
                     fault_name(kind), static_cast<unsigned>(cycle), replacement_ok ? 1 : 0);
        }
    }
    const std::uint32_t expected_cycles = kCyclesPerFault * static_cast<std::uint32_t>(std::size(kinds));
    const std::uint32_t expected_fatals = kCyclesPerFault * 5;
    if (state->cycles.load() != expected_cycles || state->recovery_cycles.load() != expected_cycles ||
        state->fatal_records.load() != expected_fatals ||
        state->boundary_fatal_records.load() != kCyclesPerFault ||
        state->boundary_recovery_cycles.load() != kCyclesPerFault || state->stale_packets.load() != 0) {
        state->teardown_failures.fetch_add(1);
    }
    state->launcher_pending.store(true);
    state->stop.store(true);
    update_memory(*state);
    update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
    ESP_LOGI(kTag, "script_fault_telemetry cycles=%u recovery_cycles=%u fatal_records=%u "
              "launcher_presents=%u replacement_frames=%u teardown_failures=%u stale_packets=%u "
              "boundary_fatal_records=%u boundary_recovery_cycles=%u "
             "worker_stack_low_water_words=%u ui_stack_low_water_words=%u "
             "supervisor_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u status=%s",
             static_cast<unsigned>(state->cycles.load()), static_cast<unsigned>(state->recovery_cycles.load()),
              static_cast<unsigned>(state->fatal_records.load()), static_cast<unsigned>(state->launcher_presents.load()),
              static_cast<unsigned>(state->replacement_frames.load()), static_cast<unsigned>(state->teardown_failures.load()),
              static_cast<unsigned>(state->stale_packets.load()),
              static_cast<unsigned>(state->boundary_fatal_records.load()),
              static_cast<unsigned>(state->boundary_recovery_cycles.load()),
             static_cast<unsigned>(state->worker_stack_min.load()), static_cast<unsigned>(state->ui_stack_min.load()),
             static_cast<unsigned>(state->supervisor_stack_min.load()),
             static_cast<unsigned>(state->internal_free_min.load()),
             static_cast<unsigned>(state->psram_free_min.load()), state->teardown_failures.load() == 0 ? "smoke-pass" : "fail");
    vTaskDelete(nullptr);
}

} // namespace

bool start_script_fault_recovery_acceptance_task() {
    auto* state = new (std::nothrow) FaultState();
    if (state == nullptr) return false;
    state->ready = xSemaphoreCreateBinary();
    state->ui_started = xSemaphoreCreateBinary();
    if (state->ready == nullptr || state->ui_started == nullptr) {
        if (state->ready != nullptr) vSemaphoreDelete(state->ready);
        if (state->ui_started != nullptr) vSemaphoreDelete(state->ui_started);
        delete state;
        return false;
    }
    const BaseType_t result = xTaskCreate(supervisor_entry, "jf_fault_supervisor", 12288 / sizeof(StackType_t),
                                          state, 7, &state->supervisor_task);
    if (result != pdPASS) {
        vSemaphoreDelete(state->ready);
        vSemaphoreDelete(state->ui_started);
        delete state;
        return false;
    }
    return true;
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_script_fault_recovery_acceptance_task() { return false; }
} // namespace jellyframe_esp32s3

#endif
