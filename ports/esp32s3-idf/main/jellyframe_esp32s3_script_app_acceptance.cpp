#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_APP_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_APP_ACCEPTANCE

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"
#include "jellyframe_esp32s3_input.h"

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "render_core/budget.h"
#include "render_core/embedded_framebuffer.h"
#include "render_core/software_renderer.h"
#include "script/script_task_worker_runtime.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

constexpr const char* kTag = "JellyFrameScriptApp";
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr std::string_view kHtml =
    "<!doctype html><html><body><main id='app'>"
    "<h1>JerryScript</h1><p id='status'>Touch the button</p>"
    "<button id='action'>Tap</button></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;padding:18px;background:#101827;color:#f8fafc;}"
    "main{padding:16px;background:#1f2a44;border-radius:12px;}"
    "h1{font-size:22px;color:#7dd3fc;}p{font-size:16px;}"
    "button{padding:12px;background:#34d399;color:#052e16;border-radius:8px;}";
constexpr std::string_view kJs =
    "let n=0; const status=document.getElementById('status');"
    "document.getElementById('action').addEventListener('click',function(){"
    "n=n+1; status.textContent='Tapped '+n;});";

struct ScriptAppState {
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> fatal_seen{false};
    std::atomic<std::uint32_t> input_posted{0};
    std::atomic<std::uint32_t> input_rejected{0};
    std::atomic<std::uint32_t> frames_published{0};
    std::atomic<std::uint32_t> frames_presented{0};
    std::atomic<std::uint32_t> present_failures{0};
    std::atomic<std::uint32_t> worker_fatal_count{0};
    std::atomic<std::uint32_t> last_input_packet_sequence{0};
    std::atomic<std::uint32_t> last_js_mutation_sequence{0};
    std::atomic<std::uint32_t> last_published_frame_sequence{0};
    std::atomic<std::uint32_t> last_accepted_frame_packet_sequence{0};
    std::atomic<UBaseType_t> worker_stack_low_water{0};
    std::atomic<UBaseType_t> ui_stack_low_water{0};
    std::atomic<UBaseType_t> supervisor_stack_low_water{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
    BoardInputQueue input_queue;
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession session{};
    SemaphoreHandle_t ready = nullptr;
    TaskHandle_t supervisor_task = nullptr;
    TaskHandle_t worker_task = nullptr;
    TaskHandle_t ui_task = nullptr;
};

void update_memory(ScriptAppState& state) {
    const auto internal = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const auto psram = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    auto current = state.internal_free_min.load();
    while (internal < current && !state.internal_free_min.compare_exchange_weak(current, internal)) {}
    current = state.psram_free_min.load();
    while (psram < current && !state.psram_free_min.compare_exchange_weak(current, psram)) {}
}

void update_stack(std::atomic<UBaseType_t>& target, UBaseType_t value) {
    UBaseType_t old = target.load();
    while ((old == 0 || value < old) && !target.compare_exchange_weak(old, value)) {}
}

jellyframe::HostBudgets script_budgets() {
    jellyframe::HostBudgets budgets;
    budgets.max_dom_nodes = 128;
    budgets.max_dom_depth = 16;
    budgets.max_attributes_per_element = 16;
    budgets.max_css_rules = 32;
    budgets.max_css_declarations_per_rule = 24;
    budgets.max_render_objects = 128;
    budgets.max_layout_boxes = 128;
    budgets.max_layers = 32;
    budgets.max_display_commands = 256;
    budgets.max_dirty_rects = 8;
    budgets.max_input_events_per_frame = 16;
    budgets.max_timers = 8;
    budgets.max_timer_callbacks_per_frame = 4;
    budgets.max_event_listeners = 32;
    budgets.max_resource_bytes = 48 * 1024;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(kWidth) * kHeight;
    budgets.max_script_execution_checks = 2000;
    budgets.script_execution_check_interval = 64;
    budgets.max_active_animations = 4;
    return budgets;
}

jellyframe::ScriptTaskSupervisorOptions protocol_options() {
    return jellyframe::ScriptTaskSupervisorOptions{
        {8, 256}, {8, 256}, {4, 64 * 1024, 128 * 1024}, 8, 4, {8, 64}, {4, 8 * 1024, 16 * 1024}, {4, 64}};
}

jellyframe::ScriptTaskWorkerRuntimeOptions worker_options() {
    const jellyframe::HostBudgets budgets = script_budgets();
    jellyframe::ScriptTaskWorkerRuntimeOptions options;
    options.budgets = budgets;
    options.script = jellyframe::jerryscript_runtime_options_from_host_budgets(budgets);
    options.viewport = jellyframe::Rect{0, 0, kWidth, kHeight};
    options.frame_codec = {256, 48 * 1024, 32, 64 * 1024};
    options.input_codec = {32, 256};
    options.service_request_codec = {64};
    return options;
}

void worker_entry(void* raw) {
    auto* state = static_cast<ScriptAppState*>(raw);
    if (state == nullptr || state->protocol == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    jellyframe::ScriptTaskWorkerRuntime runtime(state->session, worker_options());
    const auto initialized = runtime.initialize(kHtml, kCss);
    ESP_LOGI(kTag, "script_worker initialized=%d scripting=1 task_runtime=1", initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted);
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        ESP_LOGI(kTag, "script_worker eval_begin bytes=%u", static_cast<unsigned>(kJs.size()));
        const auto eval = runtime.eval_with_supervisor(*state->protocol, kJs, "script_ui_input.js");
        ESP_LOGI(kTag, "script_worker eval_done ok=%d", eval.ok ? 1 : 0);
        const auto published = runtime.publish_frame(*state->protocol);
        ESP_LOGI(kTag, "script_worker frame_publish accepted=%d", published.accepted() ? 1 : 0);
        if (published.accepted()) {
            state->frames_published.fetch_add(1);
            state->last_published_frame_sequence.store(runtime.telemetry().published_frame_seq);
            ESP_LOGI(kTag, "script_p3_1_worker event=initial frame_published=1 published_frame_seq=%" PRIu32,
                     runtime.telemetry().published_frame_seq);
        }
    }
    while (!state->stop.load() && !runtime.fatal()) {
        const auto step = runtime.process_one(*state->protocol);
        if (step.packet_consumed) {
            const auto telemetry = runtime.telemetry();
            state->last_input_packet_sequence.store(telemetry.input_packet_seq);
            if (step.dom_mutated) {
                state->last_js_mutation_sequence.store(telemetry.js_mutation_seq);
            }
            if (step.frame_published) {
                state->frames_published.fetch_add(1);
                state->last_published_frame_sequence.store(telemetry.published_frame_seq);
            }
            ESP_LOGI(kTag,
                     "script_p3_1_worker input_packet_seq=%" PRIu32 " input_accepted=%d dom_mutated=%d "
                     "js_mutation_seq=%" PRIu32 " frame_published=%d published_frame_seq=%" PRIu32 " fatal=%d",
                     telemetry.input_packet_seq, step.input_accepted ? 1 : 0, step.dom_mutated ? 1 : 0,
                     telemetry.js_mutation_seq, step.frame_published ? 1 : 0,
                     telemetry.published_frame_seq, step.fatal ? 1 : 0);
        }
        const auto callbacks = runtime.pump_callbacks(
            static_cast<std::uint64_t>(esp_timer_get_time() / 1000), *state->protocol);
        if (callbacks.frame_published) state->frames_published.fetch_add(1);
        update_memory(*state);
        update_stack(state->worker_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    if (runtime.fatal()) {
        state->fatal_seen.store(true);
        state->worker_fatal_count.fetch_add(1);
        (void) runtime.publish_fatal(*state->protocol);
    }
    runtime.stop();
    state->worker_done.store(true);
    update_stack(state->worker_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    update_memory(*state);
    vTaskDelete(nullptr);
}

bool post_board_input(ScriptAppState& state, const BoardInputEvent& event, std::uint32_t& sequence) {
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
    default: return false;
    }
    const std::uint32_t packet_sequence = sequence++;
    const auto result = jellyframe::post_script_task_input(
        *state.protocol, state.session, packet_sequence, input, {32, 256});
    if (result.accepted()) {
        state.input_posted.fetch_add(1);
        ESP_LOGI(kTag, "script_p3_1_input_posted packet_seq=%" PRIu32 " kind=%u x=%d y=%d",
                 packet_sequence, static_cast<unsigned>(input.kind), input.x, input.y);
    } else {
        state.input_rejected.fetch_add(1);
        ESP_LOGW(kTag, "script_p3_1_input_rejected packet_seq=%" PRIu32 " kind=%u",
                 packet_sequence, static_cast<unsigned>(input.kind));
    }
    return result.accepted();
}

void ui_entry(void* raw) {
    auto* state = static_cast<ScriptAppState*>(raw);
    if (state == nullptr || xSemaphoreTake(state->ready, portMAX_DELAY) != pdTRUE) {
        vTaskDeleteWithCaps(nullptr);
        return;
    }
    ESP_LOGI(kTag, "script_ui entry ready=1");
    boards::BoardRuntime board = boards::initialize_selected_board();
    ESP_LOGI(kTag, "script_ui board_ready=%d display_ready=%d",
             board.profile.display.width > 0 ? 1 : 0, board.hardware_display_ready ? 1 : 0);
    boards::attach_input_queue(board, &state->input_queue);
    const int width = board.profile.display.width > 0 ? board.profile.display.width : kWidth;
    const int height = board.profile.display.height > 0 ? board.profile.display.height : kHeight;
    jellyframe::FrameBuffer framebuffer(width, height, jellyframe::Color{16, 24, 39, 255});
    const std::size_t pixels = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixels]);
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
    panel.packed_pixel_capacity = pixels;
    auto packed_sink = make_rgb565_sink(panel);
    const jellyframe::HostFrameSink sink = jellyframe::embedded_frame_sink(packed_sink);
    const jellyframe::SoftwareRasterizer rasterizer(make_production_text_painter());
    std::uint32_t input_sequence = 1;
    ESP_LOGI(kTag, "ui_task kind=script-app viewport=%dx%d hardware_display=%d", width, height, board.hardware_display_ready ? 1 : 0);
    while (!state->stop.load()) {
        jellyframe::ScriptTaskAppFrame frame;
        std::uint32_t accepted_frame_packet_sequence = 0;
        if (jellyframe::take_script_task_app_frame(*state->protocol, state->session,
                                                   {256, 48 * 1024, 32, 64 * 1024}, frame,
                                                   &accepted_frame_packet_sequence) ==
            jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            framebuffer.clear(jellyframe::Color{16, 24, 39, 255});
            rasterizer.rasterize(frame.display_list, framebuffer, frame.viewport);
            const jellyframe::Rect full{0, 0, width, height};
            const bool ok = jellyframe::present_frame(framebuffer, sink, &full, 1);
            if (ok) {
                state->frames_presented.fetch_add(1);
                state->last_accepted_frame_packet_sequence.store(accepted_frame_packet_sequence);
            } else {
                state->present_failures.fetch_add(1);
            }
            ESP_LOGI(kTag, "script_p3_1_ui accepted_frame_packet_seq=%" PRIu32 " present_ok=%d",
                     accepted_frame_packet_sequence, ok ? 1 : 0);
            if (state->protocol != nullptr) {
                // This is a scalar acknowledgment; no frame lease escapes UI.
                state->protocol->current();
            }
        }
        BoardInputEvent event;
        std::size_t input_count = 0;
        while (input_count++ < 8 && state->input_queue.dequeue(event)) {
            (void) post_board_input(*state, event, input_sequence);
        }
        update_memory(*state);
        update_stack(state->ui_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    boards::release_board_runtime(board);
    update_stack(state->ui_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    vTaskDeleteWithCaps(nullptr);
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<ScriptAppState*>(raw);
    if (state == nullptr) vTaskDelete(nullptr);
    jellyframe::AppRuntimeHost host({8, 4, 8, 2048, 1});
    jellyframe::ScriptTaskSupervisor protocol(protocol_options());
    const auto app = host.launch("org.jellyframe.fixture.script_ui_input", jellyframe::AppRole::App);
    state->session = protocol.begin(app.id);
    state->protocol = &protocol;
    jellyframe::ScriptTaskServiceBridge bridge(host, protocol, {8});
    xSemaphoreGive(state->ready);
    const BaseType_t worker_created = xTaskCreate(
        worker_entry, "jf_script_worker",
        CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE / sizeof(StackType_t),
        state, 6, &state->worker_task);
    const BaseType_t ui_created = xTaskCreateWithCaps(
        ui_entry, "jf_script_ui",
        CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE,
        state, CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY, &state->ui_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_LOGI(kTag, "script_task_create worker=%d ui=%d worker_stack_bytes=%u ui_stack_bytes=%u",
             worker_created == pdPASS ? 1 : 0, ui_created == pdPASS ? 1 : 0,
             static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE),
             static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE));
    std::int64_t next_telemetry_us = 0;
    while (!state->stop.load() && !state->worker_done.load()) {
        (void) bridge.pump_service_requests();
        jellyframe::AppFrameScratch scratch;
        scratch.reserve_from_options({8, 4, 8, 2048, 1});
        (void) bridge.pump(scratch);
        jellyframe::ScriptTaskPacket fatal;
        if (protocol.take_fatal(fatal)) {
            state->fatal_seen.store(true);
            ESP_LOGE(kTag, "script_fatal_value session=%" PRIu32 " generation=%" PRIu32
                           " epoch=%" PRIu32 " payload=%u",
                     state->session.app_instance_id, state->session.generation,
                     state->session.worker_epoch, static_cast<unsigned>(fatal.payload.size()));
            state->stop.store(true);
            break;
        }
        const std::int64_t now_us = esp_timer_get_time();
        if (next_telemetry_us == 0 || now_us >= next_telemetry_us) {
            next_telemetry_us = now_us + 5000000;
            update_memory(*state);
            update_stack(state->supervisor_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
            ESP_LOGI(kTag,
                     "script_app_telemetry scripting=1 service_gateway=1 ui_frame_bridge=1 "
                     "input_posted=%u input_rejected=%u frames_published=%u frames_presented=%u "
                     "present_failures=%u fatal=%u input_packet_seq=%u js_mutation_seq=%u "
                     "published_frame_seq=%u ui_accepted_frame_packet_seq=%u "
                     "worker_stack_low_water_words=%u ui_stack_low_water_words=%u "
                     "supervisor_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u status=running",
                     static_cast<unsigned>(state->input_posted.load()),
                     static_cast<unsigned>(state->input_rejected.load()),
                     static_cast<unsigned>(state->frames_published.load()),
                     static_cast<unsigned>(state->frames_presented.load()),
                     static_cast<unsigned>(state->present_failures.load()),
                     static_cast<unsigned>(state->worker_fatal_count.load()),
                     static_cast<unsigned>(state->last_input_packet_sequence.load()),
                     static_cast<unsigned>(state->last_js_mutation_sequence.load()),
                     static_cast<unsigned>(state->last_published_frame_sequence.load()),
                     static_cast<unsigned>(state->last_accepted_frame_packet_sequence.load()),
                     static_cast<unsigned>(state->worker_stack_low_water.load()),
                     static_cast<unsigned>(state->ui_stack_low_water.load()),
                     static_cast<unsigned>(state->supervisor_stack_low_water.load()),
                     static_cast<unsigned>(state->internal_free_min.load()),
                     static_cast<unsigned>(state->psram_free_min.load()));
        }
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    update_stack(state->supervisor_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    ESP_LOGI(kTag, "script_app_telemetry scripting=1 service_gateway=1 ui_frame_bridge=1 input_posted=%u input_rejected=%u frames_published=%u frames_presented=%u present_failures=%u fatal=%u input_packet_seq=%u js_mutation_seq=%u published_frame_seq=%u ui_accepted_frame_packet_seq=%u worker_stack_low_water_words=%u ui_stack_low_water_words=%u supervisor_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u status=%s",
             static_cast<unsigned>(state->input_posted.load()), static_cast<unsigned>(state->input_rejected.load()),
             static_cast<unsigned>(state->frames_published.load()), static_cast<unsigned>(state->frames_presented.load()),
             static_cast<unsigned>(state->present_failures.load()), static_cast<unsigned>(state->worker_fatal_count.load()),
             static_cast<unsigned>(state->last_input_packet_sequence.load()),
             static_cast<unsigned>(state->last_js_mutation_sequence.load()),
             static_cast<unsigned>(state->last_published_frame_sequence.load()),
             static_cast<unsigned>(state->last_accepted_frame_packet_sequence.load()),
             static_cast<unsigned>(state->worker_stack_low_water.load()),
             static_cast<unsigned>(state->ui_stack_low_water.load()),
             static_cast<unsigned>(state->supervisor_stack_low_water.load()),
             static_cast<unsigned>(state->internal_free_min.load()), static_cast<unsigned>(state->psram_free_min.load()),
             state->fatal_seen.load() ? "blocked-fatal" : "smoke-pass");
    state->stop.store(true);
    (void) protocol.begin_teardown(state->session);
    (void) bridge.begin_teardown(state->session);
    (void) host.terminate_current(jellyframe::AppTeardownReason::NormalExit);
    (void) bridge.complete_teardown(state->session);
    (void) protocol.complete_teardown(state->session);
    vTaskDelete(nullptr);
}

} // namespace

bool start_script_app_acceptance_task() {
    auto* state = new (std::nothrow) ScriptAppState();
    if (state == nullptr) {
        ESP_LOGE(kTag, "script_app_create state_alloc=0");
        return false;
    }
    state->ready = xSemaphoreCreateBinary();
    const BaseType_t result = state->ready == nullptr
        ? errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY
        : xTaskCreate(supervisor_entry, "jf_script_supervisor",
                      12288 / sizeof(StackType_t), state, 7, &state->supervisor_task);
    ESP_LOGI(kTag, "script_app_create ready=%d task_result=%d internal=%u psram=%u",
             state->ready != nullptr ? 1 : 0, static_cast<int>(result),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    if (result != pdPASS) {
        if (state->ready != nullptr) vSemaphoreDelete(state->ready);
        delete state;
        return false;
    }
    return true;
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_script_app_acceptance_task() { return false; }
} // namespace jellyframe_esp32s3

#endif
