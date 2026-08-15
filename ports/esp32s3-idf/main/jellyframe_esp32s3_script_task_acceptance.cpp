#include "jellyframe_esp32s3_ui_task.h"
#include "sdkconfig.h"

#if defined(CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_PROTOCOL_ACCEPTANCE) && \
    CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_PROTOCOL_ACCEPTANCE && \
    defined(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME) && \
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_service_request_codec.h"
#include "app_runtime/script_task_worker_inbox.h"
#include "render_core/input.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace jellyframe_esp32s3 {
namespace {

constexpr char kTag[] = "ScriptTaskValue";
constexpr std::uint32_t kNormalCycles = 100;
constexpr std::uint32_t kTeardownCycles = 30;
constexpr TickType_t kWait = pdMS_TO_TICKS(3000);
constexpr int kViewportWidth = 172;
constexpr int kViewportHeight = 320;

struct CompletionSink final : jellyframe::ScriptTaskServiceCompletionSink {
    bool handle_script_task_service_completion(
        const jellyframe::ScriptTaskServiceCompletion& completion) override {
        last = completion;
        ++calls;
        return true;
    }

    jellyframe::ScriptTaskServiceCompletion last;
    std::uint32_t calls = 0;
};

struct FixtureState {
    jellyframe::ScriptTaskSupervisor* protocol = nullptr;
    jellyframe::ScriptAppSession session;
    TaskHandle_t supervisor_task = nullptr;
    TaskHandle_t worker_task = nullptr;
    TaskHandle_t ui_task = nullptr;
    SemaphoreHandle_t stale_ui_gate = nullptr;
    SemaphoreHandle_t worker_exit_ack = nullptr;
    SemaphoreHandle_t worker_control_gate = nullptr;
    SemaphoreHandle_t worker_ui_gate = nullptr;
    SemaphoreHandle_t supervisor_control_gate = nullptr;
    SemaphoreHandle_t supervisor_ui_gate = nullptr;
    SemaphoreHandle_t ui_work_gate = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> teardown_worker{false};
    std::atomic<bool> replacement_worker{false};
    std::atomic<bool> stale_ui_pending{false};

    std::atomic<std::uint32_t> normal_cycles{0};
    std::atomic<std::uint32_t> normal_completions{0};
    std::atomic<std::uint32_t> normal_frames{0};
    std::atomic<std::uint32_t> rejected_completions{0};
    std::atomic<std::uint32_t> host_rejected{0};
    std::atomic<std::uint32_t> ui_frames{0};
    std::atomic<std::uint32_t> ui_frame_rejects{0};
    std::atomic<std::uint32_t> ui_stale_frames{0};
    std::atomic<std::uint32_t> channel_isolation_failures{0};
    std::atomic<std::uint32_t> teardown_cycles{0};
    std::atomic<std::uint32_t> late_completions_stale{0};
    std::atomic<std::uint32_t> teardown_frame_discards{0};
    std::atomic<std::uint32_t> replacement_frames{0};
    std::atomic<std::uint32_t> failures{0};
    std::atomic<std::uint32_t> worker_create_failures{0};
    std::atomic<std::uint32_t> worker_exit_acks{0};
    std::atomic<UBaseType_t> worker_stack_low_water{0};
    std::atomic<UBaseType_t> ui_stack_low_water{0};
    std::atomic<UBaseType_t> supervisor_stack_low_water{0};
    std::atomic<std::uint32_t> internal_free_min{0xffffffffU};
    std::atomic<std::uint32_t> psram_free_min{0xffffffffU};
};

void update_watermarks(FixtureState& state) {
    const std::uint32_t internal = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    const std::uint32_t psram = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    std::uint32_t old = state.internal_free_min.load();
    while (internal < old && !state.internal_free_min.compare_exchange_weak(old, internal)) {}
    old = state.psram_free_min.load();
    while (psram < old && !state.psram_free_min.compare_exchange_weak(old, psram)) {}
}

void update_stack_watermark(std::atomic<UBaseType_t>& target, UBaseType_t value) {
    UBaseType_t old = target.load();
    if (old == 0 || value < old) {
        target.store(value);
    }
}

void log_teardown_failure(FixtureState& state, std::uint32_t cycle, const char* point) {
    state.failures.fetch_add(1);
    ESP_LOGE(kTag, "teardown failure cycle=%u point=%s", static_cast<unsigned>(cycle), point);
}

bool wait_gate(SemaphoreHandle_t gate) {
    return gate != nullptr && xSemaphoreTake(gate, kWait) == pdTRUE;
}

void signal_gate(SemaphoreHandle_t gate) {
    if (gate != nullptr) {
        xSemaphoreGive(gate);
    }
}

jellyframe::ScriptTaskSupervisorOptions protocol_options() {
    jellyframe::ScriptTaskSupervisorOptions options;
    options.input_mailbox = {4, 128};
    options.frame_mailbox = {4, 0};
    options.frame_leases = {2, 1024, 2048};
    options.max_service_tombstones = 16;
    options.max_native_release_intents = 4;
    options.service_request_mailbox = {4, 64};
    options.service_payload_leases = {2, 256, 512};
    return options;
}

jellyframe::ScriptTaskAppFrameCodecOptions frame_options() {
    return {4, 64, 2, 1024};
}

jellyframe::ScriptTaskAppFrame make_frame(std::uint32_t token) {
    jellyframe::ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, kViewportWidth, kViewportHeight};
    jellyframe::DisplayCommand command;
    command.type = jellyframe::DisplayCommandType::FillRect;
    command.rect = frame.viewport;
    command.color = {12, static_cast<std::uint8_t>(token & 0xffU), 56, 255};
    frame.display_list.push_back(command);
    frame.input_targets.push_back({0xA5000000U | (token & 0xffffU), frame.viewport, true});
    return frame;
}

void complete_host_requests(jellyframe::AppRuntimeHost& host,
                            std::vector<jellyframe::HostServiceRequest>& popped) {
    jellyframe::HostServiceRequest request;
    while (host.pop_worker_request(request)) {
        popped.push_back(request);
    }
    for (const jellyframe::HostServiceRequest& request_to_complete : popped) {
        host.push_completion({request_to_complete.job_id,
                              request_to_complete.kind,
                              jellyframe::HostServiceStatus::Completed,
                              request_to_complete.app_instance_id,
                              0,
                              0,
                              4,
                              request_to_complete.client_token});
    }
    popped.clear();
}

bool pump_supervisor(FixtureState& state,
                     jellyframe::ScriptTaskServiceBridge& bridge,
                     jellyframe::AppRuntimeHost& host,
                     jellyframe::AppFrameScratch& scratch,
                     std::uint32_t* rejected_out = nullptr) {
    const jellyframe::ScriptTaskServiceRequestPumpResult requests =
        bridge.pump_service_requests();
    state.host_rejected.fetch_add(static_cast<std::uint32_t>(requests.host_rejected));
    if (rejected_out != nullptr) {
        *rejected_out = static_cast<std::uint32_t>(requests.host_rejected);
    }
    std::vector<jellyframe::HostServiceRequest> popped;
    complete_host_requests(host, popped);
    const jellyframe::ScriptTaskServiceBridgePumpResult completions = bridge.pump(scratch);
    update_watermarks(state);
    return requests.invalid_packets == 0 && requests.invalid_sessions == 0 &&
           requests.invalid_tokens == 0 && !completions.worker_inbox_full;
}

bool consume_completion(FixtureState& state,
                        jellyframe::LayerNode& private_root,
                        CompletionSink& sink,
                        std::uint32_t expected_request_id) {
    jellyframe::InputController input(private_root);
    const jellyframe::ScriptTaskWorkerInboxDispatchResult result =
        jellyframe::take_and_dispatch_script_task_worker_packet(
            *state.protocol, input, sink, {0, 128});
    if (!result.handled || sink.calls == 0 || sink.last.request_id != expected_request_id) {
        state.failures.fetch_add(1);
        return false;
    }
    return true;
}

void worker_entry(void* argument) {
    auto* state = static_cast<FixtureState*>(argument);
    if (state == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    const jellyframe::ScriptAppSession session = state->session;
    jellyframe::LayerNode private_root;
    private_root.type = jellyframe::LayerType::Root;
    private_root.bounds = {0, 0, kViewportWidth, kViewportHeight};
    CompletionSink sink;
    jellyframe::ScriptTaskAppFramePublisher publisher(frame_options());

    if (state->teardown_worker.load()) {
        const std::uint32_t request_id = 0x70000000U | state->teardown_cycles.load();
        const jellyframe::ScriptTaskServiceRequest request{
            jellyframe::HostServiceJobKind::ComputeJob, request_id, request_id + 1, 0, 1, 1000};
        const auto posted = jellyframe::post_script_task_service_request(
            *state->protocol, session, request_id, request, {64});
        if (!posted.accepted()) {
            log_teardown_failure(*state, state->teardown_cycles.load(), "worker-post");
        }
        signal_gate(state->supervisor_control_gate);
        if (wait_gate(state->worker_control_gate)) {
            const auto published = publisher.publish(
                *state->protocol, session, make_frame(request_id));
            if (!published.accepted()) {
                log_teardown_failure(*state, state->teardown_cycles.load(), "worker-publish");
            }
            state->stale_ui_pending.store(true);
            signal_gate(state->ui_work_gate);
        } else {
            log_teardown_failure(*state, state->teardown_cycles.load(), "worker-wait-supervisor");
        }
        signal_gate(state->supervisor_control_gate);
        if (state->worker_exit_ack != nullptr) {
            signal_gate(state->worker_exit_ack);
            state->worker_exit_acks.fetch_add(1);
        }
        update_stack_watermark(state->worker_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelete(nullptr);
        return;
    }

    if (state->replacement_worker.load()) {
        const std::uint32_t token = 0x80000000U | state->teardown_cycles.load();
        const auto published = publisher.publish(*state->protocol, session, make_frame(token));
        if (!published.accepted()) {
            log_teardown_failure(*state, state->teardown_cycles.load(), "replacement-publish");
        } else {
            state->replacement_frames.fetch_add(1);
            signal_gate(state->ui_work_gate);
            if (!wait_gate(state->worker_ui_gate)) {
                log_teardown_failure(*state, state->teardown_cycles.load(), "replacement-wait-ui");
            }
        }
        signal_gate(state->supervisor_control_gate);
        if (state->worker_exit_ack != nullptr) {
            signal_gate(state->worker_exit_ack);
            state->worker_exit_acks.fetch_add(1);
        }
        update_stack_watermark(state->worker_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
        vTaskDelete(nullptr);
        return;
    }

    for (std::uint32_t cycle = 0; cycle < kNormalCycles; ++cycle) {
        const std::uint32_t request_id = 1000U + cycle;
        const jellyframe::ScriptTaskServiceRequest request{
            jellyframe::HostServiceJobKind::ComputeJob, request_id, request_id + 1, 0, 1, 1000};
        const auto posted = jellyframe::post_script_task_service_request(
            *state->protocol, session, request_id, request, {64});
        const auto published = publisher.publish(*state->protocol, session, make_frame(cycle));
        if (!posted.accepted() || !published.accepted()) {
            state->failures.fetch_add(1);
            break;
        }
        state->normal_cycles.fetch_add(1);
        state->normal_frames.fetch_add(1);
        signal_gate(state->supervisor_control_gate);
        signal_gate(state->ui_work_gate);
        if (!wait_gate(state->worker_control_gate) ||
            !wait_gate(state->worker_ui_gate)) {
            state->failures.fetch_add(1);
            break;
        }
        if (consume_completion(*state, private_root, sink, request_id)) {
            state->normal_completions.fetch_add(1);
        }
        update_watermarks(*state);
    }

    // Two requests in one service mailbox pass exercise host capacity: the
    // first is accepted, the second receives a terminal BudgetExceeded value.
    const std::uint32_t rejected_id = 0x5001;
    for (std::uint32_t index = 0; index < 2; ++index) {
        const std::uint32_t request_id = rejected_id + index;
        const jellyframe::ScriptTaskServiceRequest request{
            jellyframe::HostServiceJobKind::ComputeJob, request_id, request_id + 1, 0, 1, 1000};
        if (!jellyframe::post_script_task_service_request(
                *state->protocol, session, request_id, request, {64}).accepted()) {
            state->failures.fetch_add(1);
        }
    }
    const auto published = publisher.publish(*state->protocol, session, make_frame(rejected_id));
    if (!published.accepted()) {
        state->failures.fetch_add(1);
    }
    signal_gate(state->supervisor_control_gate);
    signal_gate(state->ui_work_gate);
    if (!wait_gate(state->worker_control_gate) ||
        !wait_gate(state->worker_ui_gate)) {
        state->failures.fetch_add(1);
    }
    if (consume_completion(*state, private_root, sink, rejected_id)) {
        state->normal_completions.fetch_add(1);
    }
    if (consume_completion(*state, private_root, sink, rejected_id + 1)) {
        if (sink.last.status == jellyframe::HostServiceStatus::BudgetExceeded) {
            state->rejected_completions.fetch_add(1);
        } else {
            state->failures.fetch_add(1);
        }
    }
    signal_gate(state->supervisor_control_gate);
    if (state->worker_exit_ack != nullptr) {
        signal_gate(state->worker_exit_ack);
        state->worker_exit_acks.fetch_add(1);
    }
    update_stack_watermark(state->worker_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

void ui_entry(void* argument) {
    auto* state = static_cast<FixtureState*>(argument);
    if (state == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    while (!state->stop.load()) {
        if (!wait_gate(state->ui_work_gate)) {
            state->failures.fetch_add(1);
            break;
        }
        if (state->stop.load()) {
            signal_gate(state->supervisor_ui_gate);
            break;
        }
        if (state->stale_ui_pending.exchange(false)) {
            if (xSemaphoreTake(state->stale_ui_gate, kWait) != pdTRUE) {
                state->failures.fetch_add(1);
                break;
            }
            jellyframe::ScriptTaskAppFrame stale_frame;
            const auto status = jellyframe::take_script_task_app_frame(
                *state->protocol, state->session, frame_options(), stale_frame);
            if (status != jellyframe::ScriptTaskAppFrameTakeStatus::NoFrame) {
                state->channel_isolation_failures.fetch_add(1);
                ESP_LOGE(kTag, "stale UI frame status=%d", static_cast<int>(status));
            } else {
                state->ui_stale_frames.fetch_add(1);
            }
            signal_gate(state->supervisor_ui_gate);
            continue;
        }
        jellyframe::ScriptTaskAppFrame frame;
        const auto status = jellyframe::take_script_task_app_frame(
            *state->protocol, state->session, frame_options(), frame);
        if (status != jellyframe::ScriptTaskAppFrameTakeStatus::Accepted ||
            frame.viewport.width != kViewportWidth || frame.viewport.height != kViewportHeight ||
            frame.display_list.size() != 1 || frame.input_targets.size() != 1 ||
            jellyframe::resolve_script_task_input_target(frame, 10, 10) == 0) {
            state->ui_frame_rejects.fetch_add(1);
            state->failures.fetch_add(1);
        } else {
            state->ui_frames.fetch_add(1);
        }
        signal_gate(state->worker_ui_gate);
        update_stack_watermark(state->ui_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
        update_watermarks(*state);
    }
    update_stack_watermark(state->ui_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    vTaskDelete(nullptr);
}

void supervisor_entry(void* argument) {
    auto* state = static_cast<FixtureState*>(argument);
    if (state == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    jellyframe::AppRuntimeHost host({1, 4, 4, 1024, 0});
    jellyframe::ScriptTaskSupervisor protocol(protocol_options());
    jellyframe::AppFrameScratch scratch;
    scratch.reserve_from_options({2, 4, 4, 1024, 0});
    jellyframe::ScriptTaskServiceBridge bridge(host, protocol, {4});
    const jellyframe::AppInstance first = host.launch("org.jellyframe.script.value-protocol", jellyframe::AppRole::App);
    const jellyframe::ScriptAppSession first_session = protocol.begin(first.id);
    state->protocol = &protocol;
    state->session = first_session;
    state->supervisor_task = xTaskGetCurrentTaskHandle();
    state->ui_task = nullptr;
    state->worker_exit_ack = xSemaphoreCreateBinary();
    state->worker_control_gate = xSemaphoreCreateBinary();
    state->worker_ui_gate = xSemaphoreCreateBinary();
    state->supervisor_control_gate = xSemaphoreCreateBinary();
    state->supervisor_ui_gate = xSemaphoreCreateBinary();
    state->ui_work_gate = xSemaphoreCreateBinary();
    if (state->worker_exit_ack == nullptr || state->worker_control_gate == nullptr ||
        state->worker_ui_gate == nullptr || state->supervisor_control_gate == nullptr ||
        state->supervisor_ui_gate == nullptr || state->ui_work_gate == nullptr) {
        state->failures.fetch_add(1);
        state->stop.store(true);
        vTaskDelete(nullptr);
        return;
    }

    const BaseType_t ui_created = xTaskCreatePinnedToCore(
        ui_entry, "script_task_ui", 8192, state, 4, &state->ui_task, tskNO_AFFINITY);
    if (ui_created != pdPASS) {
        state->failures.fetch_add(1);
        state->stop.store(true);
        vTaskDelete(nullptr);
        return;
    }
    const BaseType_t worker_created = xTaskCreatePinnedToCore(
        worker_entry, "script_task_worker", CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE,
        state, 5, &state->worker_task, tskNO_AFFINITY);
    if (worker_created != pdPASS) {
        state->failures.fetch_add(1);
        state->worker_create_failures.fetch_add(1);
        ESP_LOGE(kTag, "initial worker create failed internal_free=%u largest_internal=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        state->stop.store(true);
        signal_gate(state->ui_work_gate);
        vTaskDelete(nullptr);
        return;
    }

    // The worker can only notify after supervisor_task is installed. Give it
    // a scheduler turn before entering the request pump loop.
    vTaskDelay(1);
    for (std::uint32_t cycle = 0; cycle < kNormalCycles; ++cycle) {
        if (!wait_gate(state->supervisor_control_gate) ||
            !pump_supervisor(*state, bridge, host, scratch)) {
            state->failures.fetch_add(1);
            break;
        }
        signal_gate(state->worker_control_gate);
    }
    if (wait_gate(state->supervisor_control_gate) && pump_supervisor(*state, bridge, host, scratch)) {
        signal_gate(state->worker_control_gate);
    } else {
        state->failures.fetch_add(1);
    }
    if (!wait_gate(state->supervisor_control_gate)) {
        state->failures.fetch_add(1);
    }
    if (xSemaphoreTake(state->worker_exit_ack, kWait) != pdTRUE) {
        state->failures.fetch_add(1);
        ESP_LOGE(kTag, "initial worker exit ack timed out");
    }
    // A self-deleting FreeRTOS task is reclaimed by Idle. Do not create the
    // next generation until the deletion pass has had a scheduling window.
    vTaskDelay(pdMS_TO_TICKS(20));

    // The normal-flow worker owns the first session. Retire it before
    // creating the teardown generations: ScriptTaskSupervisor intentionally
    // permits only one active session, so a new begin() otherwise returns an
    // invalid value and the following worker cannot post or publish.
    const auto normal_begin = protocol.begin_teardown(first_session);
    const auto normal_bridge_begin = bridge.begin_teardown(first_session);
    const auto normal_host_end = host.terminate_current(jellyframe::AppTeardownReason::NormalExit);
    const auto normal_bridge_end = bridge.complete_teardown(first_session);
    const auto normal_end = protocol.complete_teardown(first_session);
    if (normal_begin.session != first_session ||
        normal_bridge_begin.cancelled_pending_host_jobs != 0 ||
        normal_bridge_begin.awaiting_in_flight_host_completions != 0 ||
        normal_host_end.app_instance_id != first.id ||
        normal_bridge_end.retired_records != 0 || normal_end.session != first_session) {
        state->failures.fetch_add(1);
        ESP_LOGE(kTag, "normal session retirement failed");
    }
    state->session = {};

    // Each teardown gets a fresh worker and session. The UI is deliberately
    // gated until teardown has discarded the old sealed frame.
    state->stale_ui_gate = xSemaphoreCreateBinary();
    if (state->stale_ui_gate == nullptr) {
        state->failures.fetch_add(1);
    }
    for (std::uint32_t cycle = 0; cycle < kTeardownCycles; ++cycle) {
        if (state->session.valid()) {
            const jellyframe::ScriptAppSession replacement = state->session;
            const auto replacement_begin = protocol.begin_teardown(replacement);
            const auto replacement_bridge_begin = bridge.begin_teardown(replacement);
            const auto replacement_host_end = host.terminate_current(
                jellyframe::AppTeardownReason::AppSwitch);
            const auto replacement_bridge_end = bridge.complete_teardown(replacement);
            const auto replacement_end = protocol.complete_teardown(replacement);
            if (replacement_begin.session != replacement ||
                replacement_bridge_begin.cancelled_pending_host_jobs != 0 ||
                replacement_bridge_begin.awaiting_in_flight_host_completions != 0 ||
                replacement_host_end.app_instance_id != replacement.app_instance_id ||
                replacement_bridge_end.retired_records != 0 ||
                replacement_end.session != replacement) {
                log_teardown_failure(*state, cycle + 1, "replacement-retirement");
            }
            state->session = {};
        }
        const jellyframe::AppInstance app = host.launch(
            "org.jellyframe.script.teardown", jellyframe::AppRole::App);
        state->session = protocol.begin(app.id);
        state->teardown_cycles.store(cycle + 1);
        state->teardown_worker.store(true);
        state->stale_ui_pending.store(false);
        const BaseType_t created = xTaskCreatePinnedToCore(
            worker_entry, "script_task_reaper", CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE,
            state, 5, &state->worker_task, tskNO_AFFINITY);
        if (created != pdPASS) {
            state->failures.fetch_add(1);
            state->worker_create_failures.fetch_add(1);
            ESP_LOGE(kTag, "teardown worker create failed cycle=%u internal_free=%u largest_internal=%u",
                     static_cast<unsigned>(cycle + 1),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
            break;
        }
        if (!wait_gate(state->supervisor_control_gate)) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-post");
            break;
        }
        const auto requests = bridge.pump_service_requests();
        state->host_rejected.fetch_add(static_cast<std::uint32_t>(requests.host_rejected));
        jellyframe::HostServiceRequest late_request;
        if (!host.pop_worker_request(late_request)) {
            log_teardown_failure(*state, cycle + 1, "supervisor-pop-host-request");
        }
        signal_gate(state->worker_control_gate);
        if (!wait_gate(state->supervisor_control_gate)) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-worker-frame");
            break;
        }
        const jellyframe::ScriptAppSession retired = state->session;
        const auto supervisor_begin = protocol.begin_teardown(retired);
        const auto bridge_begin = bridge.begin_teardown(retired);
        (void)supervisor_begin;
        (void)bridge_begin;
        host.terminate_current(jellyframe::AppTeardownReason::RuntimeError);
        host.push_completion({late_request.job_id,
                              late_request.kind,
                              jellyframe::HostServiceStatus::Completed,
                              late_request.app_instance_id,
                              0,
                              0,
                              4,
                              late_request.client_token});
        bridge.complete_teardown(retired);
        const auto late_pump = host.pump_frame_completions(scratch);
        state->late_completions_stale.fetch_add(static_cast<std::uint32_t>(late_pump.stale));
        const auto supervisor_end = protocol.complete_teardown(retired);
        state->teardown_frame_discards.fetch_add(
            static_cast<std::uint32_t>(supervisor_begin.discarded_frame_packets +
                                       supervisor_end.released_frame_leases));
        signal_gate(state->stale_ui_gate);
        if (!wait_gate(state->supervisor_ui_gate)) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-ui-stale");
        }
        if (xSemaphoreTake(state->worker_exit_ack, kWait) != pdTRUE) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-worker-exit");
            ESP_LOGE(kTag, "teardown worker exit ack timed out cycle=%u",
                     static_cast<unsigned>(cycle + 1));
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        state->teardown_worker.store(false);
        host.launch("org.jellyframe.script.next", jellyframe::AppRole::App);
        const jellyframe::ScriptAppSession next = protocol.begin(host.current_app_instance_id());
        state->session = next;
        if (!next.valid()) {
            log_teardown_failure(*state, cycle + 1, "replacement-begin");
            break;
        }
        state->replacement_worker.store(true);
        const BaseType_t replacement_created = xTaskCreatePinnedToCore(
            worker_entry, "script_task_next", CONFIG_JELLYFRAME_ESP32S3_SCRIPT_TASK_STACK_SIZE,
            state, 5, &state->worker_task, tskNO_AFFINITY);
        if (replacement_created != pdPASS) {
            state->failures.fetch_add(1);
            state->worker_create_failures.fetch_add(1);
            ESP_LOGE(kTag, "replacement worker create failed cycle=%u internal_free=%u largest_internal=%u",
                     static_cast<unsigned>(cycle + 1),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
            break;
        }
        if (!wait_gate(state->supervisor_control_gate)) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-replacement-frame");
            break;
        }
        if (xSemaphoreTake(state->worker_exit_ack, kWait) != pdTRUE) {
            log_teardown_failure(*state, cycle + 1, "supervisor-wait-replacement-exit");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        state->replacement_worker.store(false);
    }

    if (state->replacement_frames.load() != kTeardownCycles ||
        state->ui_frames.load() != state->normal_frames.load() + 1 + kTeardownCycles ||
        state->teardown_frame_discards.load() != kTeardownCycles * 2) {
        state->failures.fetch_add(1);
        ESP_LOGE(kTag, "teardown replacement accounting mismatch replacements=%u ui_frames=%u discarded=%u",
                 static_cast<unsigned>(state->replacement_frames.load()),
                 static_cast<unsigned>(state->ui_frames.load()),
                 static_cast<unsigned>(state->teardown_frame_discards.load()));
    }

    state->stop.store(true);
    signal_gate(state->ui_work_gate);
    (void)wait_gate(state->supervisor_ui_gate);
    update_stack_watermark(state->supervisor_stack_low_water, uxTaskGetStackHighWaterMark(nullptr));
    update_watermarks(*state);
    const auto input_stats = protocol.current();
    ESP_LOGI(kTag,
             "script_task_value_protocol_summary scripting=1 script_task_runtime=1 normal_cycles=%u normal_completions=%u normal_frames=%u host_rejected=%u rejected_completions=%u ui_frames=%u ui_frame_rejects=%u ui_stale_frames=%u channel_isolation_failures=%u teardown_cycles=%u replacement_frames=%u late_completions_stale=%u teardown_frame_discards=%u failures=%u worker_create_failures=%u worker_exit_acks=%u worker_stack_low_water_words=%u ui_stack_low_water_words=%u supervisor_stack_low_water_words=%u internal_free_min=%u psram_free_min=%u final_session_valid=%d status=%s",
             static_cast<unsigned>(state->normal_cycles.load()),
             static_cast<unsigned>(state->normal_completions.load()),
             static_cast<unsigned>(state->normal_frames.load()),
             static_cast<unsigned>(state->host_rejected.load()),
             static_cast<unsigned>(state->rejected_completions.load()),
             static_cast<unsigned>(state->ui_frames.load()),
             static_cast<unsigned>(state->ui_frame_rejects.load()),
             static_cast<unsigned>(state->ui_stale_frames.load()),
             static_cast<unsigned>(state->channel_isolation_failures.load()),
             static_cast<unsigned>(state->teardown_cycles.load()),
             static_cast<unsigned>(state->replacement_frames.load()),
             static_cast<unsigned>(state->late_completions_stale.load()),
             static_cast<unsigned>(state->teardown_frame_discards.load()),
             static_cast<unsigned>(state->failures.load()),
             static_cast<unsigned>(state->worker_create_failures.load()),
             static_cast<unsigned>(state->worker_exit_acks.load()),
             static_cast<unsigned>(state->worker_stack_low_water.load()),
             static_cast<unsigned>(state->ui_stack_low_water.load()),
             static_cast<unsigned>(state->supervisor_stack_low_water.load()),
             static_cast<unsigned>(state->internal_free_min.load()),
             static_cast<unsigned>(state->psram_free_min.load()),
             input_stats.valid() ? 1 : 0,
             state->failures.load() == 0 ? "pass" : "fail");
     if (state->stale_ui_gate != nullptr) {
         vSemaphoreDelete(state->stale_ui_gate);
         state->stale_ui_gate = nullptr;
     }
     if (state->worker_exit_ack != nullptr) {
         vSemaphoreDelete(state->worker_exit_ack);
         state->worker_exit_ack = nullptr;
     }
     if (state->worker_control_gate != nullptr) {
         vSemaphoreDelete(state->worker_control_gate);
         state->worker_control_gate = nullptr;
     }
     if (state->worker_ui_gate != nullptr) {
         vSemaphoreDelete(state->worker_ui_gate);
         state->worker_ui_gate = nullptr;
     }
     if (state->supervisor_control_gate != nullptr) {
         vSemaphoreDelete(state->supervisor_control_gate);
         state->supervisor_control_gate = nullptr;
     }
     if (state->supervisor_ui_gate != nullptr) {
         vSemaphoreDelete(state->supervisor_ui_gate);
         state->supervisor_ui_gate = nullptr;
     }
     if (state->ui_work_gate != nullptr) {
         vSemaphoreDelete(state->ui_work_gate);
         state->ui_work_gate = nullptr;
     }
     delete state;
    vTaskDelete(nullptr);
}

} // namespace

bool start_script_task_value_protocol_acceptance_task() {
    if (!start_band_shell_ui_task()) {
        ESP_LOGE(kTag, "native system shell task did not start");
        return false;
    }
    auto* state = new FixtureState();
    if (state == nullptr) {
        return false;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(
        supervisor_entry, "script_task_supervisor", 12288, state, 5, &state->supervisor_task,
        tskNO_AFFINITY);
    if (created != pdPASS) {
        delete state;
        return false;
    }
    return true;
}

} // namespace jellyframe_esp32s3

#else

namespace jellyframe_esp32s3 {
bool start_script_task_value_protocol_acceptance_task() {
    return false;
}
} // namespace jellyframe_esp32s3

#endif
