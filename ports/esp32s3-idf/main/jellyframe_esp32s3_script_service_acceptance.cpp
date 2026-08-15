#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_SERVICE_ECHO_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_SERVICE_ECHO_ACCEPTANCE && \
    defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME

#include "boards/waveshare_touch_lcd_boards.h"
#include "jellyframe_esp32s3_font.h"
#include "jellyframe_esp32s3_hal.h"

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_contract.h"
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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <memory>
#include <new>
#include <string_view>
#include <optional>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr char kTag[] = "JellyFrameScriptEcho";
constexpr int kWidth = 172;
constexpr int kHeight = 320;
constexpr std::uint32_t kExpectedCompletions = 100;
constexpr std::uint32_t kExpectedCancellations = 30;
constexpr std::uint32_t kExpectedBoundaryCompletions = 5;
constexpr std::uint32_t kExpectedGenerationStale = 30;
constexpr std::int64_t kSoakDurationUs = 30LL * 60LL * 1000000LL;
constexpr std::int64_t kSoakRelaunchPeriodUs = 5LL * 60LL * 1000000LL;
constexpr std::uint32_t kEchoByte = 0x45; // 'E', deterministic provider marker.
constexpr int kScenario = CONFIG_JELLYFRAME_ESP32S3_SCRIPT_SERVICE_ACCEPTANCE_SCENARIO;
constexpr std::string_view kHtml =
    "<!doctype html><html><body><main><h1>Service echo</h1>"
    "<p id='status'>Waiting</p></main></body></html>";
constexpr std::string_view kCss =
    "body{margin:0;padding:18px;background:#101827;color:#f8fafc;}"
    "main{padding:16px;background:#1f2a44;border-radius:12px;}"
    "h1{font-size:22px;color:#7dd3fc;}p{font-size:16px;}";
// Each next request is submitted by the preceding JS completion callback, so
// the host/provider queue never hides a failed request behind a burst.
constexpr std::string_view kJs =
    "let completed=0; const target=100; const status=document.getElementById('status');"
    "function issue(){services.request(3,function(c){"
    "if(!c.ok || c.payloadBytes.length<4 || c.payloadBytes[0]!==69){"
    "status.textContent='Echo error'; throw new Error('echo payload');}"
    "completed=completed+1; status.textContent='Echo '+completed;"
    "if(completed<target) issue();});} issue();";

constexpr std::string_view kQueuedCancelJs =
    "let issued=0; const target=30; const status=document.getElementById('status');"
    "function issue(){if(issued>=target){status.textContent='Queued cancel done';return;}"
    "const request=services.request(3,function(){throw new Error('queued callback');});"
    "if(!services.cancel(request)){throw new Error('queued cancel');}"
    "issued=issued+1; status.textContent='Queued '+issued; setTimeout(issue,40);} issue();";

constexpr std::string_view kInFlightCancelJs =
    "let issued=0; const target=30; const status=document.getElementById('status');"
    "function issue(){if(issued>=target){status.textContent='In-flight cancel done';return;}"
    "const request=services.request(3,function(){throw new Error('inflight callback');});"
    "setTimeout(function(){if(!services.cancel(request)){throw new Error('inflight cancel');}"
    "issued=issued+1; status.textContent='In-flight '+issued; setTimeout(issue,180);},40);} issue();";

constexpr std::string_view kGenerationStaleJs =
    "const status=document.getElementById('status');"
    "services.request(3,function(){status.textContent='STALE CALLBACK'; throw new Error('stale callback');});"
    "status.textContent='Generation pending';";

constexpr std::string_view kMixedSoakJs =
    "let completed=0; const status=document.getElementById('status');"
    "function issue(){services.request(3,function(c){"
    "if(!c.ok || c.payloadBytes.length<4 || c.payloadBytes[0]!==69){"
    "status.textContent='Soak error'; throw new Error('soak payload');}"
    "completed=completed+1; status.textContent='Soak '+completed;"
    "setTimeout(issue,1000);});} issue();";

constexpr std::string_view kPayloadBoundaryJs =
    "let step=0; const status=document.getElementById('status');"
    "function issue(){services.request(3,function(c){step=step+1;"
    "if(step===1 && (!c.ok || c.payloadBytes.length!==0)) throw new Error('empty payload');"
    "if(step===2 && (!c.ok || c.payloadBytes.length!==256 || c.payloadBytes[0]!==69)) throw new Error('max payload');"
    "if(step===3 && (c.ok || c.payloadBytes.length!==0)) throw new Error('over payload');"
    "if(step===4 && (c.ok || c.payloadBytes.length!==0)) throw new Error('copy failure');"
    "if(step===5 && (c.ok || c.payloadBytes.length!==0)) throw new Error('provider failure');"
    "status.textContent='Boundary '+step; if(step<5) issue();});} issue();";

std::string_view script_source() {
    if (kScenario == 1) return kQueuedCancelJs;
    if (kScenario == 2) return kInFlightCancelJs;
    if (kScenario == 3) return kGenerationStaleJs;
    if (kScenario == 4) return kPayloadBoundaryJs;
    if (kScenario == 5) return kMixedSoakJs;
    return kJs;
}

const char* scenario_name() {
    switch (kScenario) {
    case 0: return "success";
    case 1: return "queued-cancel";
    case 2: return "inflight-cancel";
    case 3: return "generation-stale";
    case 4: return "payload-boundary";
    case 5: return "mixed-soak";
    default: return "invalid";
    }
}

struct EchoProviderRequest {
    HostServiceRequest request;
};

struct EchoProviderResult {
    HostServiceRequest request;
    HostServiceStatus status = HostServiceStatus::Failed;
    std::uint32_t error_code = 0;
    std::size_t payload_size = 0;
};

class EchoProvider final {
public:
    bool start() {
        requests_ = xQueueCreate(16, sizeof(EchoProviderRequest));
        results_ = xQueueCreate(16, sizeof(EchoProviderResult));
        done_ = xSemaphoreCreateBinary();
        if (requests_ == nullptr || results_ == nullptr || done_ == nullptr) {
            stop();
            return false;
        }
        running_.store(true);
        return xTaskCreate(provider_entry, "jf_echo_provider", 6144, this, 4, &task_) == pdPASS;
    }

    void stop() {
        running_.store(false);
        if (task_ != nullptr) {
            xTaskNotifyGive(task_);
            (void)xSemaphoreTake(done_, pdMS_TO_TICKS(250));
            task_ = nullptr;
        }
        if (requests_ != nullptr) {
            vQueueDelete(requests_);
            requests_ = nullptr;
        }
        if (results_ != nullptr) {
            vQueueDelete(results_);
            results_ = nullptr;
        }
        if (done_ != nullptr) {
            vSemaphoreDelete(done_);
            done_ = nullptr;
        }
    }

    bool submit(const HostServiceRequest& request) {
        if (requests_ == nullptr) return false;
        EchoProviderRequest value{request};
        return xQueueSend(requests_, &value, 0) == pdTRUE;
    }

    bool take(EchoProviderResult& result) {
        return results_ != nullptr && xQueueReceive(results_, &result, 0) == pdTRUE;
    }

    std::uint32_t popped() const { return popped_.load(); }
    std::uint32_t completed() const { return completed_.load(); }
    UBaseType_t stack_low_water() const { return stack_low_water_.load(); }

private:
    static void provider_entry(void* raw) {
        auto* self = static_cast<EchoProvider*>(raw);
        EchoProviderRequest request;
        while (self->running_.load()) {
            UBaseType_t old = self->stack_low_water_.load();
            const UBaseType_t current = uxTaskGetStackHighWaterMark(nullptr);
            while ((old == 0 || current < old) &&
                   !self->stack_low_water_.compare_exchange_weak(old, current)) {}
            if (xQueueReceive(self->requests_, &request, pdMS_TO_TICKS(100)) != pdTRUE) {
                continue;
            }
            self->popped_.fetch_add(1);
            // This delay is the asynchronous provider boundary. The provider
            // has only copied scalar request data and never sees AppRuntimeHost.
            vTaskDelay(pdMS_TO_TICKS(120));
            EchoProviderResult result;
            result.request = request.request;
            result.status = HostServiceStatus::Completed;
            result.payload_size = 4;
            if (kScenario == 4) {
                switch (request.request.job_id) {
                case 1: result.payload_size = 0; break;       // empty payload
                case 2: result.payload_size = 256; break;     // maximum lease
                case 3: result.payload_size = 257; break;     // copy overflow
                case 4: result.payload_size = 256; break;     // forced copy failure
                case 5:
                    result.status = HostServiceStatus::Failed;
                    result.error_code = 0xE401;
                    result.payload_size = 0;
                    break;
                default: break;
                }
            }
            if (xQueueSend(self->results_, &result, pdMS_TO_TICKS(100)) == pdTRUE) {
                self->completed_.fetch_add(1);
            }
        }
        UBaseType_t old = self->stack_low_water_.load();
        const UBaseType_t current = uxTaskGetStackHighWaterMark(nullptr);
        while ((old == 0 || current < old) &&
               !self->stack_low_water_.compare_exchange_weak(old, current)) {}
        xSemaphoreGive(self->done_);
        vTaskDelete(nullptr);
    }

    QueueHandle_t requests_ = nullptr;
    QueueHandle_t results_ = nullptr;
    SemaphoreHandle_t done_ = nullptr;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<std::uint32_t> popped_{0};
    std::atomic<std::uint32_t> completed_{0};
    std::atomic<UBaseType_t> stack_low_water_{0};
};

struct EchoState {
    std::atomic<bool> stop{false};
    std::atomic<bool> worker_stop{false};
    std::atomic<bool> worker_done{false};
    std::atomic<bool> fatal{false};
    std::atomic<std::uint32_t> completion_callbacks{0};
    std::atomic<std::uint32_t> completion_dom_mutations{0};
    std::atomic<std::uint32_t> provider_submitted{0};
    std::atomic<std::uint32_t> provider_completed{0};
    std::atomic<std::uint32_t> frames_published{0};
    std::atomic<std::uint32_t> frames_presented{0};
    std::atomic<std::uint32_t> present_failures{0};
    std::atomic<std::uint32_t> payload_copied{0};
    std::atomic<std::uint32_t> payload_released{0};
    std::atomic<std::uint32_t> bridge_source_releases{0};
    std::atomic<std::uint32_t> completion_delivered{0};
    std::atomic<std::uint32_t> completion_stale{0};
    std::atomic<std::uint32_t> completion_cancelled{0};
    std::atomic<std::uint32_t> completion_rejected{0};
    std::atomic<std::uint32_t> completion_identity_kind_rejected{0};
    std::atomic<std::uint32_t> completion_identity_app_rejected{0};
    std::atomic<std::uint32_t> completion_identity_token_rejected{0};
    std::atomic<std::uint32_t> completion_identity_pending_preserved{0};
    std::atomic<std::uint32_t> completion_validation_failures{0};
    std::atomic<std::uint32_t> provider_rejected_handle_releases{0};
    std::atomic<std::uint32_t> host_events_observed{0};
    std::atomic<std::uint32_t> request_cancelled{0};
    std::atomic<std::uint32_t> payload_empty{0};
    std::atomic<std::uint32_t> payload_max{0};
    std::atomic<std::uint32_t> payload_overlimit{0};
    std::atomic<std::uint32_t> payload_copy_failure{0};
    std::atomic<std::uint32_t> provider_failures{0};
    std::atomic<std::uint32_t> stale_session_reopened{0};
    std::atomic<std::uint32_t> host_stale_handles_released{0};
    std::atomic<std::uint32_t> soak_relaunches{0};
    std::atomic<std::uint32_t> input_rejected{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
    std::atomic<UBaseType_t> supervisor_stack_min{0};
    std::atomic<UBaseType_t> worker_stack_min{0};
    std::atomic<UBaseType_t> ui_stack_min{0};
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession session{};
    SemaphoreHandle_t ready = nullptr;
    SemaphoreHandle_t ui_started = nullptr;
    TaskHandle_t supervisor_task = nullptr;
    TaskHandle_t worker_task = nullptr;
    TaskHandle_t ui_task = nullptr;
};

void update_memory(EchoState& state) {
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

jellyframe::HostBudgets service_budgets() {
    jellyframe::HostBudgets budgets;
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
    budgets.max_resource_bytes = 32 * 1024;
    budgets.max_framebuffer_pixels = static_cast<std::size_t>(kWidth) * kHeight;
    budgets.max_script_execution_checks = 2000;
    budgets.script_execution_check_interval = 64;
    return budgets;
}

jellyframe::ScriptTaskWorkerRuntimeOptions worker_options() {
    const auto budgets = service_budgets();
    jellyframe::ScriptTaskWorkerRuntimeOptions options;
    options.budgets = budgets;
    options.script = jellyframe::jerryscript_runtime_options_from_host_budgets(budgets);
    options.viewport = {0, 0, kWidth, kHeight};
    options.frame_codec = {128, 32 * 1024, 16, 64 * 1024};
    options.input_codec = {16, 128};
    options.service_request_codec = {64};
    return options;
}

jellyframe::ScriptTaskSupervisorOptions protocol_options() {
    jellyframe::ScriptTaskSupervisorOptions options;
    options.input_mailbox = {16, 128};
    options.frame_mailbox = {16, 0};
    options.frame_leases = {8, 64 * 1024, 128 * 1024};
    options.max_service_tombstones = 32;
    options.max_native_release_intents = 8;
    options.service_request_mailbox = {16, 64};
    options.service_payload_leases = {8, 256, 2048};
    options.fatal_mailbox = {4, 64};
    return options;
}

bool copy_echo_payload(void* raw,
                       const jellyframe::HostServiceCompletion& completion,
                       jellyframe::ScriptTaskServicePayloadWriter& output) {
    auto* state = static_cast<EchoState*>(raw);
    if (kScenario == 4) {
        if (completion.status != jellyframe::HostServiceStatus::Completed) {
            return true;
        }
        std::vector<std::uint8_t> payload;
        switch (completion.job_id) {
        case 2:
            payload.assign(256, kEchoByte);
            if (state != nullptr) state->payload_max.fetch_add(1);
            if (!output.append(payload)) return false;
            if (state != nullptr) state->payload_copied.fetch_add(1);
            return true;
        case 3:
            payload.assign(257, kEchoByte);
            if (state != nullptr) state->payload_overlimit.fetch_add(1);
            return output.append(payload);
        case 4:
            if (state != nullptr) state->payload_copy_failure.fetch_add(1);
            return false;
        default:
            // Empty payloads have no host handle and do not enter this copy
            // hook; the provider result is still delivered as a zero-length
            // completed value.
            return output.append(nullptr, 0);
        }
    }
    if (completion.status != jellyframe::HostServiceStatus::Completed || completion.byte_count != 4) {
        return completion.status != jellyframe::HostServiceStatus::Completed;
    }
    const std::array<std::uint8_t, 4> payload{
        kEchoByte,
        static_cast<std::uint8_t>(completion.client_token & 0xffU),
        static_cast<std::uint8_t>(completion.job_id & 0xffU),
        0x01,
    };
    if (!output.append(payload.data(), payload.size())) return false;
    if (state != nullptr) state->payload_copied.fetch_add(1);
    return true;
}

struct EchoAdapter {
    EchoState* state = nullptr;
    jellyframe::AppRuntimeHost* host = nullptr;
};

bool release_echo_handle(void* raw, const jellyframe::HostServiceCompletion& completion) {
    auto* adapter = static_cast<EchoAdapter*>(raw);
    if (adapter == nullptr || adapter->host == nullptr || completion.result_handle == 0) return false;
    jellyframe::HostHandleInfo info;
    if (!adapter->host->handles().lookup_copy(completion.result_handle, info) ||
        info.app_instance_id != completion.app_instance_id ||
        !adapter->host->handles().release(completion.result_handle)) {
        return false;
    }
    if (adapter->state != nullptr) adapter->state->payload_released.fetch_add(1);
    return true;
}

bool push_provider_completion(EchoState& state,
                              jellyframe::AppRuntimeHost& host,
                              const jellyframe::HostServiceCompletion& completion) {
    if (host.push_completion(completion)) {
        state.provider_completed.fetch_add(1);
        return true;
    }
    state.completion_rejected.fetch_add(1);
    if (completion.result_handle != 0 && host.handles().release(completion.result_handle)) {
        state.provider_rejected_handle_releases.fetch_add(1);
    }
    return false;
}

bool validate_completion_identity(EchoState& state,
                                  jellyframe::AppRuntimeHost& host,
                                  const jellyframe::HostServiceCompletion& completion) {
    const std::size_t in_flight_before = host.requests().in_flight_size();
    jellyframe::HostServiceCompletion wrong_kind = completion;
    wrong_kind.kind = completion.kind == jellyframe::HostServiceJobKind::ComputeJob
        ? jellyframe::HostServiceJobKind::Other
        : jellyframe::HostServiceJobKind::ComputeJob;
    wrong_kind.result_handle = host.handles().allocate(
        jellyframe::HostServiceHandleKind::Other, completion.app_instance_id, 1, nullptr, completion.client_token);
    const bool kind_rejected = !host.push_completion(wrong_kind);
    const bool probe_released = wrong_kind.result_handle != 0 && host.handles().release(wrong_kind.result_handle);

    jellyframe::HostServiceCompletion wrong_app = completion;
    ++wrong_app.app_instance_id;
    wrong_app.result_handle = 0;
    const bool app_rejected = !host.push_completion(wrong_app);

    jellyframe::HostServiceCompletion wrong_token = completion;
    ++wrong_token.client_token;
    wrong_token.result_handle = 0;
    const bool token_rejected = !host.push_completion(wrong_token);
    const bool pending_preserved = host.requests().in_flight_size() == in_flight_before;

    if (kind_rejected) state.completion_identity_kind_rejected.fetch_add(1);
    if (app_rejected) state.completion_identity_app_rejected.fetch_add(1);
    if (token_rejected) state.completion_identity_token_rejected.fetch_add(1);
    if (pending_preserved) state.completion_identity_pending_preserved.fetch_add(1);
    if (probe_released) state.provider_rejected_handle_releases.fetch_add(1);
    const bool valid = kind_rejected && app_rejected && token_rejected && pending_preserved && probe_released;
    if (!valid) state.completion_validation_failures.fetch_add(1);
    return valid;
}

void worker_entry(void* raw) {
    auto* state = static_cast<EchoState*>(raw);
    if (state == nullptr || state->protocol == nullptr) { vTaskDelete(nullptr); return; }
    jellyframe::ScriptTaskWorkerRuntime runtime(state->session, worker_options());
    const auto initialized = runtime.initialize(kHtml, kCss);
    ESP_LOGI(kTag, "script_echo_worker initialized=%d scripting=1 task_runtime=1", initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted);
    if (initialized == jellyframe::ScriptTaskWorkerRuntimeInitStatus::Accepted) {
        const auto eval = runtime.eval_with_supervisor(*state->protocol, script_source(), "script_service_echo.js");
        ESP_LOGI(kTag, "script_echo_eval ok=%d", eval.ok ? 1 : 0);
        const auto initial = runtime.publish_frame(*state->protocol);
        if (initial.accepted()) state->frames_published.fetch_add(1);
        ESP_LOGI(kTag, "script_echo_frame event=initial accepted=%d", initial.accepted() ? 1 : 0);
    }
    while (!state->stop.load() && !state->worker_stop.load() && !runtime.fatal()) {
        const auto step = runtime.process_one(*state->protocol);
        if (step.service_completion_consumed) {
            if (step.dom_mutated) {
                state->completion_dom_mutations.fetch_add(1);
            }
            if (step.service_status == jellyframe::HostServiceStatus::Completed && step.dom_mutated) {
                state->completion_callbacks.fetch_add(1);
            }
            if (step.frame_published) state->frames_published.fetch_add(1);
            ESP_LOGI(kTag,
                     "script_echo_completion request_id=%" PRIu32 " status=%u dom_mutated=%d frame_published=%d "
                     "published_frame_seq=%" PRIu32,
                     step.service_request_id, static_cast<unsigned>(step.service_status),
                     step.dom_mutated ? 1 : 0, step.frame_published ? 1 : 0,
                     runtime.telemetry().published_frame_seq);
        }
        const auto callbacks = runtime.pump_callbacks(static_cast<std::uint64_t>(esp_timer_get_time() / 1000), *state->protocol);
        if (callbacks.frame_published) state->frames_published.fetch_add(1);
        update_memory(*state);
        if (kScenario == 0 && state->completion_callbacks.load() >= kExpectedCompletions) {
            state->worker_done.store(true);
        }
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    if (runtime.fatal()) {
        state->fatal.store(true);
        ESP_LOGE(kTag, "script_echo_fatal reason=%u diagnostic=%" PRIu32,
                 static_cast<unsigned>(runtime.fatal_record().reason), runtime.fatal_record().diagnostic_code);
        (void)runtime.publish_fatal(*state->protocol);
    }
    runtime.stop();
    // Do not let the supervisor create the replacement session until all
    // worker-owned JerryScript and render-pipeline resources are released.
    update_stack(state->worker_stack_min, uxTaskGetStackHighWaterMark(nullptr));
    state->worker_done.store(true);
    vTaskDelete(nullptr);
}

bool start_worker(EchoState& state) {
    state.worker_done.store(false);
    state.worker_stop.store(false);
    const BaseType_t created = xTaskCreate(worker_entry, "jf_echo_worker",
        CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE / sizeof(StackType_t),
        &state, 6, &state.worker_task);
    return created == pdPASS;
}

void ui_entry(void* raw) {
    auto* state = static_cast<EchoState*>(raw);
    if (state == nullptr || xSemaphoreTake(state->ready, portMAX_DELAY) != pdTRUE) { vTaskDeleteWithCaps(nullptr); return; }
    boards::BoardRuntime board = boards::initialize_selected_board();
    boards::attach_input_queue(board, nullptr);
    const int width = board.profile.display.width > 0 ? board.profile.display.width : kWidth;
    const int height = board.profile.display.height > 0 ? board.profile.display.height : kHeight;
    jellyframe::FrameBuffer framebuffer(width, height, {16, 24, 39, 255});
    const std::size_t pixels = rgb565_buffer_pixels(width, height);
    std::unique_ptr<std::uint16_t[]> packed(new (std::nothrow) std::uint16_t[pixels]);
    Rgb565Panel panel;
    panel.pixels = packed.get();
    panel.width = width;
    panel.height = height;
    panel.stride_pixels = width;
    panel.packed_flush = board.packed_flush;
    panel.packed_scroll_flush = board.packed_scroll_flush;
    panel.reset_scroll = board.reset_scroll;
    panel.flush_context = board.flush_context;
    panel.packed_pixels = packed.get();
    panel.packed_pixel_capacity = pixels;
    auto packed_sink = make_rgb565_sink(panel);
    auto sink = jellyframe::embedded_frame_sink(packed_sink);
    const jellyframe::SoftwareRasterizer rasterizer(make_production_text_painter());
    ESP_LOGI(kTag, "script_echo_ui kind=service-echo viewport=%dx%d hardware_display=%d",
             width, height, board.hardware_display_ready ? 1 : 0);
    xSemaphoreGive(state->ui_started);
    while (!state->stop.load()) {
        jellyframe::ScriptTaskAppFrame frame;
        std::uint32_t sequence = 0;
        if (jellyframe::take_script_task_app_frame(*state->protocol, state->session,
                                                   {128, 32 * 1024, 16, 64 * 1024}, frame, &sequence) ==
            jellyframe::ScriptTaskAppFrameTakeStatus::Accepted) {
            framebuffer.clear({16, 24, 39, 255});
            rasterizer.rasterize(frame.display_list, framebuffer, frame.viewport);
            const jellyframe::Rect full{0, 0, width, height};
            const bool ok = jellyframe::present_frame(framebuffer, sink, &full, 1);
            if (ok) state->frames_presented.fetch_add(1); else state->present_failures.fetch_add(1);
            ESP_LOGI(kTag, "script_echo_ui_frame sequence=%" PRIu32 " present_ok=%d", sequence, ok ? 1 : 0);
        }
        update_stack(state->ui_stack_min, uxTaskGetStackHighWaterMark(nullptr));
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    boards::release_board_runtime(board);
    vTaskDeleteWithCaps(nullptr);
}

void supervisor_entry(void* raw) {
    auto* state = static_cast<EchoState*>(raw);
    if (state == nullptr) vTaskDelete(nullptr);
    jellyframe::AppRuntimeHost host({8, 8, 8, 2048, 0});
    jellyframe::ScriptTaskSupervisor protocol(protocol_options());
    const auto app = host.launch("org.jellyframe.fixture.script_service_echo", jellyframe::AppRole::App);
    state->session = protocol.begin(app.id);
    state->protocol = &protocol;
    EchoAdapter adapter{state, &host};
    const std::size_t bridge_payload_bytes = kScenario == 4 ? 256 : 16;
    jellyframe::ScriptTaskServiceBridge bridge(host, protocol,
        {8, bridge_payload_bytes, copy_echo_payload, state, release_echo_handle, &adapter});
    EchoProvider provider;
    if (!provider.start()) {
        ESP_LOGE(kTag, "echo_provider_start failed");
        state->fatal.store(true);
        state->stop.store(true);
        vTaskDelete(nullptr);
        return;
    }
    const BaseType_t ui_created = xTaskCreateWithCaps(ui_entry, "jf_echo_ui",
        CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE, state, CONFIG_JELLYFRAME_ESP32S3_UI_TASK_PRIORITY,
        &state->ui_task, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    xSemaphoreGive(state->ready);
    const bool ui_ready = ui_created == pdPASS &&
        xSemaphoreTake(state->ui_started, pdMS_TO_TICKS(5000)) == pdTRUE;
    const BaseType_t worker_created = ui_ready && start_worker(*state) ? pdPASS : pdFAIL;
    ESP_LOGI(kTag, "script_task_create worker=%d ui=%d provider=1 worker_stack_bytes=%u ui_stack_bytes=%u",
             worker_created == pdPASS ? 1 : 0, ui_ready ? 1 : 0,
             static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE),
             static_cast<unsigned>(CONFIG_JELLYFRAME_ESP32S3_UI_TASK_STACK_SIZE));
    AppFrameScratch scratch;
    scratch.reserve_from_options({8, 8, 8, 2048, 0});
    const HostServiceCompletion audio_event{0, HostServiceJobKind::AudioCommand,
                                            HostServiceStatus::Completed, app.id};
    AppFrameScratch event_scratch;
    event_scratch.reserve_from_options({8, 8, 8, 2048, 0});
    if (!host.push_completion(audio_event)) {
        state->completion_validation_failures.fetch_add(1);
    } else {
        const auto event_pump = host.pump_frame_completions(event_scratch);
        const bool observed = event_pump.accepted == 1 && event_scratch.accepted_completions.size() == 1 &&
            event_scratch.accepted_completions.front().job_id == 0 &&
            event_scratch.accepted_completions.front().kind == HostServiceJobKind::AudioCommand;
        if (observed) state->host_events_observed.fetch_add(1);
        else state->completion_validation_failures.fetch_add(1);
    }
    // Keep the old provider result out of the host completion queue until the
    // replacement session is current.  AppRuntimeHost intentionally discards
    // queued completions during terminate_current(), so pushing it before the
    // session switch would test cancellation rather than generation-stale
    // completion ownership.
    std::optional<EchoProviderResult> deferred_stale_result;
    std::int64_t next_log = 0;
    const std::int64_t soak_started = esp_timer_get_time();
    std::int64_t next_soak_relaunch = soak_started + kSoakRelaunchPeriodUs;
    while (!state->stop.load() && !state->fatal.load()) {
        const auto requests = bridge.pump_service_requests();
        if (requests.host_rejected != 0) state->completion_rejected.fetch_add(static_cast<std::uint32_t>(requests.host_rejected));
        state->request_cancelled.fetch_add(static_cast<std::uint32_t>(requests.cancelled));
        HostServiceRequest request;
        while (host.pop_worker_request(request)) {
            if (provider.submit(request)) state->provider_submitted.fetch_add(1);
            else state->completion_rejected.fetch_add(1);
        }
        EchoProviderResult result;
        while (provider.take(result)) {
            if (kScenario == 3 && !deferred_stale_result.has_value()) {
                deferred_stale_result = result;
                continue;
            }
            const std::uint32_t handle = result.payload_size == 0
                ? 0
                : host.handles().allocate(HostServiceHandleKind::Other,
                                           result.request.app_instance_id,
                                           result.payload_size,
                                           nullptr,
                                           result.request.client_token);
            HostServiceCompletion completion{result.request.job_id, result.request.kind, result.status,
                                              result.request.app_instance_id, handle, result.error_code,
                                              handle != 0 ? static_cast<std::uint32_t>(result.payload_size) : 0U,
                                              result.request.client_token};
            if (result.payload_size != 0 && handle == 0) {
                completion.status = HostServiceStatus::BudgetExceeded;
            }
            if (result.status == HostServiceStatus::Failed) {
                completion.status = result.status;
                state->provider_failures.fetch_add(1);
            }
            if (kScenario == 4 && result.request.job_id == 1 &&
                result.status == HostServiceStatus::Completed) {
                state->payload_empty.fetch_add(1);
            }
            if (kScenario == 0 && state->provider_completed.load() == 0 &&
                !validate_completion_identity(*state, host, completion)) {
                state->fatal.store(true);
            }
            if (!push_provider_completion(*state, host, completion) && kScenario == 0) {
                state->fatal.store(true);
            }
        }
        if (kScenario == 3 &&
            state->stale_session_reopened.load() < kExpectedGenerationStale &&
            deferred_stale_result.has_value() &&
            deferred_stale_result->request.app_instance_id == host.current_app_instance_id() &&
            provider.popped() >= state->stale_session_reopened.load() + 1) {
            const ScriptAppSession retired_session = state->session;
            state->worker_stop.store(true);
            const std::int64_t worker_deadline = esp_timer_get_time() + 1000000;
            while (!state->worker_done.load() && esp_timer_get_time() < worker_deadline) {
                update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
                vTaskDelay(pdMS_TO_TICKS(4));
            }
            if (!state->worker_done.load()) {
                state->fatal.store(true);
                ESP_LOGE(kTag, "script_echo_generation_stale worker_stop_timeout round=%u",
                         static_cast<unsigned>(state->stale_session_reopened.load() + 1));
                break;
            }
            // A self-deleting FreeRTOS task releases its stack/control block
            // from the idle task. Let that reclamation run before creating the
            // next private worker session.
            vTaskDelay(pdMS_TO_TICKS(60));
            (void)protocol.begin_teardown(retired_session);
            (void)bridge.begin_teardown(retired_session);
            (void)host.terminate_current(jellyframe::AppTeardownReason::NormalExit);
            (void)bridge.complete_teardown(retired_session);
            (void)protocol.complete_teardown(retired_session);
            const auto reopened = host.launch("org.jellyframe.fixture.script_service_echo", jellyframe::AppRole::App);
            const auto reopened_session = protocol.begin(reopened.id);
            state->session = reopened_session;
            const bool terminal_replacement =
                reopened_session.valid() &&
                state->stale_session_reopened.load() + 1 >= kExpectedGenerationStale;
            const bool worker_started = reopened_session.valid() &&
                (terminal_replacement || start_worker(*state));
            if (reopened_session.valid() && worker_started) {
                state->stale_session_reopened.fetch_add(1);
                ESP_LOGI(kTag, "script_echo_generation_stale retired_app=%" PRIu32 " retired_generation=%" PRIu32
                         " new_app=%" PRIu32 " new_generation=%" PRIu32,
                         retired_session.app_instance_id, retired_session.generation,
                         reopened_session.app_instance_id, reopened_session.generation);

                if (deferred_stale_result.has_value() &&
                    deferred_stale_result->request.app_instance_id != host.current_app_instance_id()) {
                    const auto stale_result = *deferred_stale_result;
                    const std::uint32_t handle = stale_result.payload_size == 0
                        ? 0
                        : host.handles().allocate(HostServiceHandleKind::Other,
                                                   stale_result.request.app_instance_id,
                                                   stale_result.payload_size,
                                                   nullptr,
                                                   stale_result.request.client_token);
                    HostServiceCompletion stale_completion{
                        stale_result.request.job_id,
                        stale_result.request.kind,
                        stale_result.status,
                        stale_result.request.app_instance_id,
                        handle,
                        stale_result.error_code,
                        handle != 0 ? static_cast<std::uint32_t>(stale_result.payload_size) : 0U,
                        stale_result.request.client_token};
                    if (handle == 0 && stale_result.payload_size != 0) {
                        stale_completion.status = HostServiceStatus::BudgetExceeded;
                    }
                    if (!push_provider_completion(*state, host, stale_completion)) {
                        state->fatal.store(true);
                    }
                    deferred_stale_result.reset();
                }
            } else {
                state->fatal.store(true);
                ESP_LOGE(kTag, "script_echo_generation_stale reopen_or_worker_failed reopened=%d worker=%d",
                         reopened_session.valid() ? 1 : 0, worker_started ? 1 : 0);
            }
        }
        if (kScenario == 5 &&
            esp_timer_get_time() < soak_started + kSoakDurationUs &&
            esp_timer_get_time() >= next_soak_relaunch) {
            const ScriptAppSession retired = state->session;
            state->worker_stop.store(true);
            const std::int64_t worker_deadline = esp_timer_get_time() + 1000000;
            while (!state->worker_done.load() && esp_timer_get_time() < worker_deadline) {
                update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
                vTaskDelay(pdMS_TO_TICKS(4));
            }
            if (!state->worker_done.load()) {
                state->fatal.store(true);
                ESP_LOGE(kTag, "script_echo_soak worker_stop_timeout relaunch=%u",
                         static_cast<unsigned>(state->soak_relaunches.load() + 1));
            } else {
                (void)protocol.begin_teardown(retired);
                (void)bridge.begin_teardown(retired);
                (void)host.terminate_current(jellyframe::AppTeardownReason::AppSwitch);
                (void)bridge.complete_teardown(retired);
                (void)protocol.complete_teardown(retired);
                const auto replacement = host.launch(
                    "org.jellyframe.fixture.script_service_soak", jellyframe::AppRole::App);
                const auto replacement_session = protocol.begin(replacement.id);
                state->session = replacement_session;
                if (!replacement_session.valid() || !start_worker(*state)) {
                    state->fatal.store(true);
                    ESP_LOGE(kTag, "script_echo_soak relaunch_failed relaunch=%u",
                             static_cast<unsigned>(state->soak_relaunches.load() + 1));
                } else {
                    state->soak_relaunches.fetch_add(1);
                    ESP_LOGI(kTag, "script_echo_soak relaunch=%u app=%" PRIu32 " generation=%" PRIu32,
                             static_cast<unsigned>(state->soak_relaunches.load()),
                             replacement.id, replacement_session.generation);
                }
            }
            next_soak_relaunch += kSoakRelaunchPeriodUs;
        }
        const auto completions = bridge.pump(scratch);
        state->completion_delivered.fetch_add(static_cast<std::uint32_t>(completions.delivered));
        state->completion_stale.fetch_add(static_cast<std::uint32_t>(completions.stale + completions.host.stale));
        state->completion_cancelled.fetch_add(static_cast<std::uint32_t>(completions.cancelled));
        state->host_stale_handles_released.fetch_add(
            static_cast<std::uint32_t>(completions.host.released_stale_handles));
        state->bridge_source_releases.fetch_add(static_cast<std::uint32_t>(completions.released_completion_sources));
        if (next_log == 0 || esp_timer_get_time() >= next_log) {
            next_log = esp_timer_get_time() + (kScenario == 5 ? 10000000 : 5000000);
            update_memory(*state);
            ESP_LOGI(kTag, "script_echo_telemetry scenario=%s callbacks=%u completion_dom_mutations=%u provider_submitted=%u provider_popped=%u provider_completed=%u "
                     "request_cancelled=%u completion_delivered=%u completion_cancelled=%u completion_stale=%u payload_copied=%u "
                     "payload_released=%u bridge_source_releases=%u payload_empty=%u payload_max=%u payload_overlimit=%u payload_copy_failure=%u provider_failures=%u "
                     "stale_session_reopened=%u host_stale_handles_released=%u soak_relaunches=%u frames_published=%u frames_presented=%u present_failures=%u "
                     "completion_identity_kind_rejected=%u completion_identity_app_rejected=%u completion_identity_token_rejected=%u "
                     "completion_identity_pending_preserved=%u completion_validation_failures=%u provider_rejected_handle_releases=%u host_events_observed=%u "
                     "bridge_active=%u worker_stack_low_water_words=%u ui_stack_low_water_words=%u "
                     "supervisor_stack_low_water_words=%u provider_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u status=running",
                     scenario_name(),
                     static_cast<unsigned>(state->completion_callbacks.load()),
                     static_cast<unsigned>(state->completion_dom_mutations.load()),
                     static_cast<unsigned>(state->provider_submitted.load()),
                     static_cast<unsigned>(provider.popped()),
                     static_cast<unsigned>(state->provider_completed.load()),
                     static_cast<unsigned>(state->request_cancelled.load()),
                     static_cast<unsigned>(state->completion_delivered.load()),
                     static_cast<unsigned>(state->completion_cancelled.load()),
                     static_cast<unsigned>(state->completion_stale.load()),
                     static_cast<unsigned>(state->payload_copied.load()),
                     static_cast<unsigned>(state->payload_released.load()),
                     static_cast<unsigned>(state->bridge_source_releases.load()),
                     static_cast<unsigned>(state->payload_empty.load()),
                     static_cast<unsigned>(state->payload_max.load()),
                     static_cast<unsigned>(state->payload_overlimit.load()),
                     static_cast<unsigned>(state->payload_copy_failure.load()),
                     static_cast<unsigned>(state->provider_failures.load()),
                     static_cast<unsigned>(state->stale_session_reopened.load()),
                     static_cast<unsigned>(state->host_stale_handles_released.load()),
                     static_cast<unsigned>(state->soak_relaunches.load()),
                     static_cast<unsigned>(state->frames_published.load()),
                     static_cast<unsigned>(state->frames_presented.load()),
                     static_cast<unsigned>(state->present_failures.load()),
                     static_cast<unsigned>(state->completion_identity_kind_rejected.load()),
                     static_cast<unsigned>(state->completion_identity_app_rejected.load()),
                     static_cast<unsigned>(state->completion_identity_token_rejected.load()),
                     static_cast<unsigned>(state->completion_identity_pending_preserved.load()),
                     static_cast<unsigned>(state->completion_validation_failures.load()),
                     static_cast<unsigned>(state->provider_rejected_handle_releases.load()),
                     static_cast<unsigned>(state->host_events_observed.load()),
                     static_cast<unsigned>(bridge.active_request_count()),
                     static_cast<unsigned>(state->worker_stack_min.load()),
                     static_cast<unsigned>(state->ui_stack_min.load()),
                     static_cast<unsigned>(state->supervisor_stack_min.load()),
                     static_cast<unsigned>(provider.stack_low_water()),
                     static_cast<unsigned>(state->internal_free_min.load()),
                     static_cast<unsigned>(state->psram_free_min.load()));
        }
        bool scenario_done = false;
        if (kScenario == 0) {
            scenario_done = state->completion_callbacks.load() >= kExpectedCompletions &&
                state->provider_completed.load() >= kExpectedCompletions &&
                state->completion_identity_kind_rejected.load() == 1 &&
                state->completion_identity_app_rejected.load() == 1 &&
                state->completion_identity_token_rejected.load() == 1 &&
                state->completion_identity_pending_preserved.load() == 1 &&
                state->completion_validation_failures.load() == 0 &&
                state->provider_rejected_handle_releases.load() == 1 &&
                state->host_events_observed.load() == 1;
        } else if (kScenario == 1) {
            scenario_done = state->request_cancelled.load() >= kExpectedCancellations &&
                provider.popped() == 0;
        } else if (kScenario == 2) {
            scenario_done = state->request_cancelled.load() >= kExpectedCancellations &&
                state->completion_cancelled.load() >= kExpectedCancellations &&
                provider.popped() >= kExpectedCancellations &&
                provider.completed() >= kExpectedCancellations;
        } else if (kScenario == 4) {
            scenario_done = provider.completed() >= kExpectedBoundaryCompletions &&
                state->completion_dom_mutations.load() >= kExpectedBoundaryCompletions &&
                state->payload_empty.load() == 1 && state->payload_max.load() == 1 &&
                state->payload_overlimit.load() == 1 && state->payload_copy_failure.load() == 1 &&
                state->provider_failures.load() == 1;
        } else if (kScenario == 3) {
            scenario_done = state->stale_session_reopened.load() >= kExpectedGenerationStale &&
                state->completion_stale.load() >= kExpectedGenerationStale &&
                provider.completed() >= kExpectedGenerationStale;
        } else if (kScenario == 5) {
            scenario_done = esp_timer_get_time() - soak_started >= kSoakDurationUs;
        }
        const bool no_active_requests = kScenario == 3 || bridge.active_request_count() == 0;
        if (scenario_done && no_active_requests &&
            state->frames_presented.load() >= state->frames_published.load()) {
            state->stop.store(true);
        }
        update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
        update_memory(*state);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
    state->stop.store(true);
    provider.stop();
    if (state->session.valid()) {
        (void)protocol.begin_teardown(state->session);
        (void)bridge.begin_teardown(state->session);
        (void)host.terminate_current(jellyframe::AppTeardownReason::NormalExit);
        (void)bridge.complete_teardown(state->session);
        (void)protocol.complete_teardown(state->session);
    }
    update_memory(*state);
    update_stack(state->supervisor_stack_min, uxTaskGetStackHighWaterMark(nullptr));
    ESP_LOGI(kTag, "script_echo_telemetry scenario=%s callbacks=%u completion_dom_mutations=%u provider_submitted=%u provider_popped=%u provider_completed=%u "
             "request_cancelled=%u completion_delivered=%u completion_cancelled=%u completion_stale=%u payload_copied=%u "
             "payload_released=%u bridge_source_releases=%u payload_empty=%u payload_max=%u payload_overlimit=%u payload_copy_failure=%u provider_failures=%u "
             "stale_session_reopened=%u host_stale_handles_released=%u soak_relaunches=%u frames_published=%u frames_presented=%u present_failures=%u "
             "completion_identity_kind_rejected=%u completion_identity_app_rejected=%u completion_identity_token_rejected=%u "
             "completion_identity_pending_preserved=%u completion_validation_failures=%u provider_rejected_handle_releases=%u host_events_observed=%u "
             "bridge_active=%u worker_stack_low_water_words=%u ui_stack_low_water_words=%u "
             "supervisor_stack_low_water_words=%u provider_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u status=%s",
             scenario_name(),
             static_cast<unsigned>(state->completion_callbacks.load()),
             static_cast<unsigned>(state->completion_dom_mutations.load()),
             static_cast<unsigned>(state->provider_submitted.load()),
             static_cast<unsigned>(provider.popped()),
             static_cast<unsigned>(state->provider_completed.load()),
             static_cast<unsigned>(state->request_cancelled.load()),
             static_cast<unsigned>(state->completion_delivered.load()),
             static_cast<unsigned>(state->completion_cancelled.load()),
             static_cast<unsigned>(state->completion_stale.load()),
             static_cast<unsigned>(state->payload_copied.load()),
             static_cast<unsigned>(state->payload_released.load()),
             static_cast<unsigned>(state->bridge_source_releases.load()),
             static_cast<unsigned>(state->payload_empty.load()),
             static_cast<unsigned>(state->payload_max.load()),
             static_cast<unsigned>(state->payload_overlimit.load()),
             static_cast<unsigned>(state->payload_copy_failure.load()),
             static_cast<unsigned>(state->provider_failures.load()),
             static_cast<unsigned>(state->stale_session_reopened.load()),
             static_cast<unsigned>(state->host_stale_handles_released.load()),
             static_cast<unsigned>(state->soak_relaunches.load()),
             static_cast<unsigned>(state->frames_published.load()),
             static_cast<unsigned>(state->frames_presented.load()),
             static_cast<unsigned>(state->present_failures.load()),
             static_cast<unsigned>(state->completion_identity_kind_rejected.load()),
             static_cast<unsigned>(state->completion_identity_app_rejected.load()),
             static_cast<unsigned>(state->completion_identity_token_rejected.load()),
             static_cast<unsigned>(state->completion_identity_pending_preserved.load()),
             static_cast<unsigned>(state->completion_validation_failures.load()),
             static_cast<unsigned>(state->provider_rejected_handle_releases.load()),
             static_cast<unsigned>(state->host_events_observed.load()),
             static_cast<unsigned>(bridge.active_request_count()),
             static_cast<unsigned>(state->worker_stack_min.load()),
             static_cast<unsigned>(state->ui_stack_min.load()),
             static_cast<unsigned>(state->supervisor_stack_min.load()),
             static_cast<unsigned>(provider.stack_low_water()),
             static_cast<unsigned>(state->internal_free_min.load()),
             static_cast<unsigned>(state->psram_free_min.load()),
             state->fatal.load() ? "fatal" : "smoke-pass");
    vTaskDelete(nullptr);
}

} // namespace

bool start_script_service_echo_acceptance_task() {
    auto* state = new (std::nothrow) EchoState();
    if (state == nullptr) return false;
    state->ready = xSemaphoreCreateBinary();
    state->ui_started = xSemaphoreCreateBinary();
    if (state->ready == nullptr || state->ui_started == nullptr) {
        if (state->ready != nullptr) vSemaphoreDelete(state->ready);
        if (state->ui_started != nullptr) vSemaphoreDelete(state->ui_started);
        delete state;
        return false;
    }
    const BaseType_t result = xTaskCreate(supervisor_entry, "jf_echo_supervisor", 12288 / sizeof(StackType_t),
                                          state, 7, &state->supervisor_task);
    ESP_LOGI(kTag, "script_echo_create ready=%d task_result=%d", state->ready != nullptr ? 1 : 0, static_cast<int>(result));
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
bool start_script_service_echo_acceptance_task() { return false; }
} // namespace jellyframe_esp32s3

#endif
