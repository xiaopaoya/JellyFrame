#include "script/script_task_worker_runtime.h"

#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_fatal_codec.h"
#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/tests/runtime_font_test_fixture.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

using namespace jellyframe;

namespace worker_test_access {
template<class T, class = void> struct WorkerHasTypedCommand : std::false_type {};
template<class T> struct WorkerHasTypedCommand<T,
    std::void_t<decltype(std::declval<const T&>().type())>> : std::true_type {};
template<class T> auto worker_command_type(const T& command) {
    if constexpr (WorkerHasTypedCommand<T>::value) return command.type();
    else return command.type;
}
} // namespace worker_test_access

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ScriptTaskSupervisor make_supervisor() {
    ScriptTaskSupervisorOptions options;
    options.worker_inbox = {16, 4096};
    options.frame_mailbox = {16, 4096};
    options.service_request_mailbox = {16, 1024};
    options.frame_leases = {4, 32 * 1024, 64 * 1024};
    options.service_payload_leases = {4, 4096, 8192};
    options.max_service_tombstones = 16;
    options.max_native_release_intents = 16;
    options.fatal_mailbox = {4, 40};
    return ScriptTaskSupervisor(options);
}

ScriptTaskSupervisor make_service_stress_supervisor() {
    ScriptTaskSupervisorOptions options;
    options.worker_inbox = {128, 4096};
    options.frame_mailbox = {4, 32 * 1024};
    options.service_request_mailbox = {128, 1024};
    options.frame_leases = {4, 32 * 1024, 64 * 1024};
    options.service_payload_leases = {128, 4096, 64 * 1024};
    options.max_service_tombstones = 128;
    options.max_native_release_intents = 16;
    return ScriptTaskSupervisor(options);
}

ScriptTaskSupervisor make_frame_backpressure_supervisor() {
    ScriptTaskSupervisorOptions options;
    options.worker_inbox = {4, 4096};
    options.frame_mailbox = {1, 4096};
    options.service_request_mailbox = {4, 1024};
    options.frame_leases = {4, 32 * 1024, 64 * 1024};
    options.service_payload_leases = {4, 4096, 8192};
    options.max_service_tombstones = 4;
    options.max_native_release_intents = 4;
    options.fatal_mailbox = {2, 40};
    return ScriptTaskSupervisor(options);
}

ScriptTaskWorkerRuntimeOptions runtime_options() {
    ScriptTaskWorkerRuntimeOptions options;
    options.viewport = {0, 0, 160, 100};
    options.budgets.max_framebuffer_pixels = 160 * 100;
    options.budgets.max_resource_bytes = 64 * 1024;
    options.frame_codec = {128, 16 * 1024, 16, 32 * 1024};
    options.input_codec = {1024, 4096};
    options.service_request_codec = {64};
    options.script.max_execution_check_count = 4096;
    return options;
}

ScriptTaskWorkerRuntimeOptions runtime_options_v2() {
    ScriptTaskWorkerRuntimeOptions options = runtime_options();
    options.frame_codec.version = 2;
    options.frame_codec.max_clips = 16;
    options.frame_codec.max_clip_depth = 4;
    return options;
}

ScriptTaskWorkerRuntimeOptions runtime_options_v3() {
    ScriptTaskWorkerRuntimeOptions options = runtime_options_v2();
    options.frame_codec.version = 3;
    return options;
}

ScriptTaskWorkerRuntimeOptions runtime_options_with_service_callback_limit(std::size_t limit) {
    ScriptTaskWorkerRuntimeOptions options = runtime_options();
    options.script.max_service_callbacks = limit;
    return options;
}

void worker_input_publishes_value_frame() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(7);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><button id='button'>idle</button></body>",
              "button { display: block; width: 120px; height: 40px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "worker runtime initializes private document");
    check(runtime.eval(
              "var button = document.getElementById('button');"
              "button.addEventListener('click', function () { button.textContent = 'clicked'; });"
              "'ready';").ok,
          "worker runtime evaluates listener in private realm");

    const ScriptTaskAppFramePublishResult initial = runtime.publish_frame(supervisor);
    check(initial.accepted(), "initial worker frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI accepts copied initial frame");

    const ScriptTaskInputCodecOptions input_options = runtime_options().input_codec;
    check(post_script_task_input(supervisor, session, 1,
                                 {ScriptTaskInputKind::PointerDown, 1, 1, 0, 0, 0, 1, 0, 0, {}},
                                 input_options).accepted(),
          "pointer down enters worker mailbox");
    const ScriptTaskWorkerRuntimeStepResult down = runtime.process_one(supervisor);
    check(down.input_accepted && !down.dom_mutated && !down.frame_published,
          "pointer down without interaction CSS or DOM mutation needs no frame");
    check(post_script_task_input(supervisor, session, 2,
                                 {ScriptTaskInputKind::PointerUp, 1, 1, 0, 0, 0, 0, 0, 0, {}},
                                 input_options).accepted(),
          "pointer up enters worker mailbox");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.process_one(supervisor);
    check(step.packet_consumed && step.input_accepted && step.frame_published,
          "click publishes a replacement frame");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI accepts the replacement frame by value");
    bool saw_clicked = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "clicked") saw_clicked = true;
    }
    check(saw_clicked, "replacement frame contains JS DOM mutation");
    check(runtime.telemetry().input_packet_seq == 2 && runtime.telemetry().js_mutation_seq > 0,
          "worker exposes monotonic value sequence telemetry");
}

void worker_interaction_hints_preserve_css_and_dom_events() {
    struct Case { const char* css; const char* script; ScriptTaskInputKind input; bool changes; };
    const Case cases[] = {
        {"button:hover { background-color: #ff0000; }", "", ScriptTaskInputKind::PointerMove, true},
        {"button:is(:active) { background-color: #ff0000; }", "", ScriptTaskInputKind::PointerDown, true},
        {"body:focus-within button { background-color: #ff0000; }", "", ScriptTaskInputKind::PointerDown, true},
        {"", "button.addEventListener('focus', function () { button.textContent = 'focused'; });",
         ScriptTaskInputKind::PointerDown, true},
        {"", "button.addEventListener('mousedown', function () { button.textContent = 'pressed'; });",
         ScriptTaskInputKind::PointerDown, true},
        {"", "", ScriptTaskInputKind::PointerMove, false},
    };
    for (const auto& fixture : cases) {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const ScriptAppSession session = supervisor.begin(75);
        const auto options = runtime_options();
        ScriptTaskWorkerRuntime runtime(session, options);
        check(runtime.initialize("<body><button id='button'>idle</button></body>",
                  std::string("body { margin: 0; } button { display: block; width: 120px; height: 40px; "
                              "margin: 0; background-color: #0000ff; }") + fixture.css) ==
                  ScriptTaskWorkerRuntimeInitStatus::Accepted, "interaction hint fixture initializes");
        check(runtime.eval(std::string("var button = document.getElementById('button');") + fixture.script).ok,
              "interaction listeners evaluate");
        check(runtime.publish_frame(supervisor).accepted(), "interaction initial frame publishes");
        ScriptTaskAppFrame frame;
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted, "interaction initial frame consumed");
        check(post_script_task_input(supervisor, session, 1,
                  {fixture.input, 1, 1, 0, 0, 0, 1, 0, 0, {}}, options.input_codec).accepted(),
              "interaction input posts");
        const auto step = runtime.process_one(supervisor);
        check(step.input_accepted && !step.fatal && step.frame_published == fixture.changes,
              "only effective interaction styles or event mutations publish frames");
        if (!fixture.changes) continue;
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted, "interaction replacement frame consumed");
        bool saw_change = false;
        for (const auto& command : frame.display_list) {
            saw_change = saw_change || command.text == "focused" || command.text == "pressed" ||
                (worker_test_access::worker_command_type(command) == DisplayCommandType::FillRect &&
                 command.color.r == 255 && command.color.b == 0);
        }
        check(saw_change, "replacement carries actual pseudo CSS or event output");
    }
}

void worker_retries_dirty_frame_after_ui_mailbox_backpressure() {
    ScriptTaskSupervisor supervisor = make_frame_backpressure_supervisor();
    const ScriptAppSession session = supervisor.begin(71);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><button id='button'>idle</button></body>",
              "button { display: block; width: 120px; height: 40px; margin: 0; } "
              "button:active { background-color: #ff0000; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "backpressure worker fixture initializes");
    check(runtime.publish_frame(supervisor).accepted(), "initial frame fills the one-slot UI mailbox");

    const ScriptTaskInputCodecOptions input_options = runtime_options().input_codec;
    check(post_script_task_input(supervisor, session, 1,
                                 {ScriptTaskInputKind::PointerDown, 1, 1, 0, 0, 0, 1, 0, 0, {}},
                                 input_options).accepted(),
          "input posts while UI mailbox is full");
    const ScriptTaskWorkerRuntimeStepResult blocked = runtime.process_one(supervisor);
    check(blocked.packet_consumed && blocked.dom_mutated && !blocked.frame_published,
          "dirty interaction frame remains pending after mailbox backpressure");

    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI consumes the older frame and releases mailbox capacity");
    const ScriptTaskWorkerRuntimeStepResult retry = runtime.process_one(supervisor);
    check(!retry.packet_consumed && retry.frame_published,
          "idle worker retry publishes the retained dirty frame after capacity returns");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI receives the retried frame by value");
}

void worker_v2_publishes_clip_metadata_without_cross_task_objects() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(70);
    const ScriptTaskWorkerRuntimeOptions options = runtime_options_v2();
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(
              "<body><section id='clip'><p>Visible</p></section></body>",
              "body { margin: 0; } #clip { overflow: hidden; width: 80px; height: 40px; "
              "border-radius: 10px; background: #ffffff; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "v2 worker fixture initializes");
    check(runtime.publish_frame(supervisor).accepted(), "v2 worker frame publishes");

    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "v2 UI accepts the copied frame");
    check(!frame.clips.empty(), "v2 worker frame contains clip metadata");
    check(frame.display_clip_indices.size() == frame.display_list.size(),
          "v2 clip references remain parallel to display commands");
}

void worker_resolves_media_queries_against_its_runtime_viewport() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(72);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><div id='panel'></div></body>",
              "body { margin: 0; } #panel { display: block; width: 160px; height: 100px; background: #ff0000; } "
              "@media (max-width: 200px) { #panel { background: #00ff00; } }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "media viewport fixture initializes");
    check(runtime.publish_frame(supervisor).accepted(), "media viewport frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "media viewport frame is received");
    bool saw_media_color = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (worker_test_access::worker_command_type(command) == DisplayCommandType::FillRect &&
            command.color.r == 0 && command.color.g == 255 &&
            command.color.b == 0) {
            saw_media_color = true;
        }
    }
    check(saw_media_color, "worker CSS media queries use its configured viewport rather than parser defaults");
}

void worker_rebuild_does_not_refire_autofocus() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(73);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><input id='field' autofocus><p id='status'>idle</p></body>",
              "body { margin: 0; } input, p { display: block; width: 120px; height: 20px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "autofocus fixture initializes");
    check(runtime.eval(
              "var focusCount = 0;"
              "var field = document.getElementById('field');"
              "field.addEventListener('focus', function () { focusCount++; });"
              "document.getElementById('status').textContent = 'one';").ok,
          "first unrelated mutation evaluates");
    check(runtime.eval(
              "document.getElementById('status').textContent = 'two';").ok,
          "second unrelated mutation evaluates");
    check(runtime.eval("focusCount;").value == "0",
          "layout rebuild preserves focus without dispatching a new focus event");
}

void worker_applies_host_layout_box_budget() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(74);
    ScriptTaskWorkerRuntimeOptions options = runtime_options();
    options.budgets.max_layout_boxes = 1;
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(
              "<body><button>must-not-reach-layout</button></body>",
              "button { display: block; width: 120px; height: 40px; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "layout budget fixture initializes");
    check(runtime.publish_frame(supervisor).accepted(), "layout budget frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "layout budget frame is received");
    for (const DisplayCommand& command : frame.display_list) {
        check(command.text != "must-not-reach-layout",
              "worker layout honors the host box budget before display-list generation");
    }
}

void worker_eval_failure_becomes_value_fatal() {
    const ScriptAppSession session{8, 1, 1};
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "fatal fixture initializes");
    const ScriptEvaluationResult result = runtime.eval("var = ;", "fault.js");
    check(!result.ok && runtime.fatal(), "script exception stops worker runtime");
    check(runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptException,
          "script exception maps to a value fatal reason");
    check(runtime.initialize("<body><p>replacement</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Fatal,
          "fatal worker cannot reinitialize a new private realm");
}

void worker_timer_publishes_value_frame() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(10);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><p id='status'>idle</p></body>",
              "p { display: block; width: 120px; height: 24px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "timer fixture initializes");
    check(runtime.eval(
              "var status = document.getElementById('status');"
              "setTimeout(function () { status.textContent = 'timer'; }, 5);"
              "'ready';").ok,
          "timer callback evaluates");
    check(runtime.publish_frame(supervisor).accepted(), "timer initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "timer initial frame is consumed");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.pump_callbacks(5, supervisor);
    check(step.dom_mutated && step.frame_published, "timer mutation publishes a replacement frame");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "timer replacement frame is consumed");
    bool saw_timer = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "timer") saw_timer = true;
    }
    check(saw_timer, "timer replacement frame contains the worker DOM mutation");
    check(runtime.telemetry().timer_callbacks == 1, "timer callback telemetry increments");
}

void worker_interval_rotation_publishes_transformed_value_frame() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(16);
    const ScriptTaskWorkerRuntimeOptions options = runtime_options_v3();
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(
              "<body><div id='hand'></div></body>",
              "body { position: relative; width: 160px; height: 100px; }"
              "#hand { position: absolute; left: 79px; top: 10px; width: 2px; height: 60px; "
              "background: #ff5060; transform-origin: 50% 100%; transform: rotate(0deg); }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "interval rotation fixture initializes");
    check(runtime.eval(
              "var hand = document.getElementById('hand'); var turn = 0;"
              "setInterval(function () { turn += 90; hand.style.transform = 'rotate(' + turn + 'deg)'; }, 5);").ok,
          "interval rotation callback evaluates");
    check(runtime.publish_frame(supervisor).accepted(), "interval rotation initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "interval rotation initial frame is consumed");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.pump_callbacks(5, supervisor);
    check(step.dom_mutated && step.frame_published, "interval rotation publishes a replacement frame");
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "interval rotation replacement frame is consumed");
    bool saw_rotation = false;
    for (const DisplayCommand& command : frame.display_list) {
        saw_rotation = saw_rotation || command.transform.enabled;
    }
    check(saw_rotation, "interval rotation survives worker value-frame flattening");
    check(runtime.telemetry().timer_callbacks == 1, "interval rotation timer telemetry increments");
}

void worker_host_time_is_initialized_before_author_script_runs() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(21);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>clock</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "clock fixture initializes");
    runtime.set_host_time_ms(123456);
    const ScriptEvaluationResult result = runtime.eval("String(Date.now())");
    check(result.ok && result.value == "123456", "worker exposes the established host time");
}

void worker_service_completion_reaches_js_as_copied_value() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(11);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><p id='status'>idle</p></body>",
              "p { display: block; width: 120px; height: 24px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "service fixture initializes");
    check(runtime.eval_with_supervisor(
              supervisor,
              "var status = document.getElementById('status');"
              "services.request(3, function (completion) {"
              "  status.textContent = completion.ok && completion.payloadBytes[0] === 101 ? 'done' : 'bad';"
              "});"
              "'ready';").ok,
          "service request evaluates inside the worker realm");

    ScriptTaskPacket request_packet;
    check(supervisor.take_service_request(request_packet), "service request reaches the dedicated mailbox");
    ScriptTaskServiceRequest request;
    check(decode_script_task_service_request(request_packet.payload, runtime_options().service_request_codec, request) ==
              ScriptTaskServiceRequestCodecStatus::Accepted,
          "service request is a typed value packet");
    check(request.kind == HostServiceJobKind::NetworkFetch && request.request_id != 0 && request.client_token != 0,
          "service request contains only bounded scalar identity");

    ScriptTaskLeaseId payload_lease_id = 0;
    check(supervisor.publish_service_payload(session, {101, 99}, payload_lease_id) ==
              ScriptTaskServicePayloadLeaseStatus::Accepted,
          "service completion payload is sealed before delivery");
    ScriptTaskServiceCompletion completion;
    completion.kind = request.kind;
    completion.status = HostServiceStatus::Completed;
    completion.request_id = request.request_id;
    completion.client_token = request.client_token;
    completion.payload_lease_id = payload_lease_id;
    completion.byte_count = 2;
    std::vector<std::uint8_t> completion_bytes;
    check(encode_script_task_service_completion(completion, completion_bytes),
          "service completion encodes as a value packet");
    check(supervisor.post_service_completion(
              {ScriptTaskPacketKind::ServiceCompletion, session, 1, 0, completion_bytes}) ==
              ScriptTaskMailboxPostStatus::Accepted,
          "service completion enters the worker inbox");

    check(runtime.publish_frame(supervisor).accepted(), "service initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "service initial frame is consumed");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.process_one(supervisor);
    check(step.packet_consumed && step.dom_mutated && step.frame_published,
          "copied service completion mutates DOM and publishes a frame");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "service replacement frame is consumed");
    bool saw_done = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "done") saw_done = true;
    }
    check(saw_done, "service callback sees copied payload bytes");
}

void worker_service_payload_lease_failure_completes_callback_as_error() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(21);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><p id='status'>idle</p></body>",
              "p { display: block; width: 120px; height: 24px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "lease failure fixture initializes");
    check(runtime.eval_with_supervisor(
              supervisor,
              "services.request(3, function (completion) {"
              "  document.getElementById('status').textContent = "
              "    !completion.ok && completion.errorCode === 3 && completion.payloadLength === 0 "
              "      ? 'lease-failed' : 'unexpected';"
              "});"
              "'ready';").ok,
          "lease failure service request evaluates in the worker realm");

    ScriptTaskPacket request_packet;
    check(supervisor.take_service_request(request_packet), "lease failure request reaches the service mailbox");
    ScriptTaskServiceRequest request;
    check(decode_script_task_service_request(request_packet.payload,
                                              runtime_options().service_request_codec,
                                              request) == ScriptTaskServiceRequestCodecStatus::Accepted,
          "lease failure request remains decodable");
    ScriptTaskServiceCompletion completion;
    completion.kind = request.kind;
    completion.status = HostServiceStatus::Completed;
    completion.request_id = request.request_id;
    completion.client_token = request.client_token;
    completion.payload_lease_id = 1;
    completion.byte_count = 4;
    std::vector<std::uint8_t> completion_bytes;
    check(encode_script_task_service_completion(completion, completion_bytes),
          "lease failure completion encodes");
    check(supervisor.post_service_completion(
              {ScriptTaskPacketKind::ServiceCompletion, session, 1, 0, completion_bytes}) ==
              ScriptTaskMailboxPostStatus::Accepted,
          "lease failure completion reaches the worker inbox");

    check(runtime.publish_frame(supervisor).accepted(), "lease failure initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "lease failure initial frame is consumed");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.process_one(supervisor);
    check(step.packet_consumed && step.service_completion_consumed && step.dom_mutated &&
              step.service_status == HostServiceStatus::Failed && !step.fatal,
          "lease failure reaches the callback as a terminal failed completion");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "lease failure callback publishes a replacement frame");
    bool saw_failure = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "lease-failed") saw_failure = true;
    }
    check(saw_failure, "lease failure callback observes the normalized error value");
}

void worker_service_callback_budget_rejects_before_queueing() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(22);
    ScriptTaskWorkerRuntime runtime(session, runtime_options_with_service_callback_limit(1));
    check(runtime.initialize("<body><p>ready</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "service callback budget fixture initializes");
    const ScriptEvaluationResult evaluated = runtime.eval_with_supervisor(
        supervisor,
        "var first = services.request(3, function () {});"
        "var rejected = false;"
        "try { services.request(3, function () {}); } catch (error) { rejected = true; }"
        "rejected ? 'ok' : 'bad';");
    check(evaluated.ok && evaluated.value == "ok", "service callback budget rejects the second request");

    ScriptTaskPacket packet;
    check(supervisor.take_service_request(packet), "first callback budget request reaches the service mailbox");
    check(!supervisor.take_service_request(packet), "rejected callback budget request is never queued");
}

void worker_service_cancel_posts_only_value_identity() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(12);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><p id='status'>idle</p></body>",
              "p { display: block; width: 120px; height: 24px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "service cancellation fixture initializes");
    check(runtime.eval_with_supervisor(
              supervisor,
              "var request = services.request(3, function () {"
              "  document.getElementById('status').textContent = 'unexpected';"
              "});"
              "var cancelled = services.cancel(request);"
              "'ready';").ok,
          "service cancellation evaluates in the worker realm");

    ScriptTaskPacket request_packet;
    check(supervisor.take_service_request(request_packet), "service request is posted before cancel");
    ScriptTaskServiceRequest request;
    check(decode_script_task_service_request(request_packet.payload,
                                              runtime_options().service_request_codec,
                                              request) == ScriptTaskServiceRequestCodecStatus::Accepted,
          "service request remains a typed value packet");
    ScriptTaskPacket cancel_packet;
    check(supervisor.take_service_request(cancel_packet), "service cancel is posted after request");
    check(cancel_packet.kind == ScriptTaskPacketKind::ServiceCancel,
          "worker cancellation uses a dedicated packet kind");
    ScriptTaskServiceCancel cancel;
    check(decode_script_task_service_cancel(cancel_packet.payload,
                                             runtime_options().service_request_codec,
                                             cancel) == ScriptTaskServiceRequestCodecStatus::Accepted,
          "service cancellation is a typed value packet");
    check(cancel.request_id == request.request_id && cancel.client_token == request.client_token,
          "service cancellation carries only request identity");
}

void worker_service_completion_replay_handles_one_hundred_callbacks() {
    ScriptTaskSupervisor supervisor = make_service_stress_supervisor();
    const ScriptAppSession session = supervisor.begin(13);
    ScriptTaskWorkerRuntime runtime(session, runtime_options_with_service_callback_limit(128));
    check(runtime.initialize(
              "<body><p id='status'>idle</p></body>",
              "p { display: block; width: 120px; height: 24px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "completion replay fixture initializes");
    check(runtime.eval_with_supervisor(
              supervisor,
              "var completionCount = 0;"
              "for (var i = 0; i < 100; ++i) {"
              "  services.request(3, function () { ++completionCount; });"
              "}"
              "'ready';").ok,
          "one hundred service requests evaluate in the worker realm");
    check(runtime.publish_frame(supervisor).accepted(), "completion replay initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "completion replay initial frame is consumed");

    std::vector<ScriptTaskServiceRequest> requests;
    requests.reserve(100);
    ScriptTaskPacket request_packet;
    while (supervisor.take_service_request(request_packet)) {
        ScriptTaskServiceRequest request;
        check(decode_script_task_service_request(request_packet.payload,
                                                  runtime_options().service_request_codec,
                                                  request) == ScriptTaskServiceRequestCodecStatus::Accepted,
              "replayed request remains decodable");
        requests.push_back(request);
    }
    check(requests.size() == 100, "one hundred requests reach the service mailbox");

    std::uint32_t completion_sequence = 1;
    for (const ScriptTaskServiceRequest& request : requests) {
        ScriptTaskServiceCompletion completion;
        completion.kind = request.kind;
        completion.status = HostServiceStatus::Completed;
        completion.request_id = request.request_id;
        completion.client_token = request.client_token;
        std::vector<std::uint8_t> payload;
        check(encode_script_task_service_completion(completion, payload),
              "replayed completion encodes as a value packet");
        check(supervisor.post_service_completion(
                  {ScriptTaskPacketKind::ServiceCompletion, session, completion_sequence++, 0, payload}) ==
                  ScriptTaskMailboxPostStatus::Accepted,
              "replayed completion enters the worker inbox");
    }

    for (std::size_t index = 0; index < requests.size(); ++index) {
        const ScriptTaskWorkerRuntimeStepResult step = runtime.process_one(supervisor);
        check(step.packet_consumed && !step.fatal, "replayed completion is consumed without worker fatal");
        if (step.frame_published) {
            check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
                      ScriptTaskAppFrameTakeStatus::Accepted,
                  "replayed callback frame is consumed");
        }
    }
    const ScriptEvaluationResult count = runtime.eval("completionCount === 100 ? 'ok' : 'bad'");
    check(count.ok && count.value == "ok",
          "all one hundred callbacks remain executable in the private realm");
}

void worker_timer_exception_becomes_fatal_value() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(14);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "timer fatal fixture initializes");
    check(runtime.eval("setTimeout(function () { throw new Error('timer-fault'); }, 5);").ok,
          "timer fatal callback is scheduled");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.pump_callbacks(5, supervisor);
    check(step.fatal && runtime.fatal(), "timer exception stops the worker");
    check(runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptException &&
              runtime.fatal_record().message_bytes != 0,
          "timer exception is exposed as a bounded pure-value fatal record");
}

void worker_animation_watchdog_becomes_fatal_value() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(15);
    ScriptTaskWorkerRuntimeOptions options = runtime_options();
    options.script.max_execution_check_count = 32;
    options.script.execution_check_interval = 1;
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "animation watchdog fixture initializes");
    check(runtime.eval("requestAnimationFrame(function () { while (true) {} });").ok,
          "animation watchdog callback is scheduled");
    const ScriptTaskWorkerRuntimeStepResult step = runtime.pump_callbacks(5, supervisor);
    check(step.fatal && runtime.fatal(), "animation watchdog stops the worker");
    check(runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptWatchdog,
          "animation watchdog is exposed as a pure-value fatal reason");
}

void worker_input_and_service_callback_exceptions_stop_the_worker() {
    {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const ScriptAppSession session = supervisor.begin(16);
        ScriptTaskWorkerRuntime runtime(session, runtime_options());
        check(runtime.initialize(
                  "<body><button id='button'>fault</button></body>",
                  "button { display: block; width: 120px; height: 40px; margin: 0; }") ==
                  ScriptTaskWorkerRuntimeInitStatus::Accepted,
              "input fatal fixture initializes");
        check(runtime.eval(
                  "var button = document.getElementById('button');"
                  "button.addEventListener('click', function () { throw new Error('input-fault'); });").ok,
              "input fatal listener evaluates");
        check(runtime.publish_frame(supervisor).accepted(), "input fatal initial frame publishes");
        ScriptTaskAppFrame frame;
        check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "input fatal initial frame is consumed");
        const ScriptTaskInputCodecOptions input_options = runtime_options().input_codec;
        check(post_script_task_input(supervisor, session, 1,
                                     {ScriptTaskInputKind::PointerDown, 1, 1, 0, 0, 0, 1, 0, 0, {}},
                                     input_options).accepted(),
              "input fatal pointer down posts");
        const ScriptTaskWorkerRuntimeStepResult down = runtime.process_one(supervisor);
        if (down.frame_published) {
            check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
                      ScriptTaskAppFrameTakeStatus::Accepted,
                  "input fatal pointer-down frame is consumed");
        }
        check(post_script_task_input(supervisor, session, 2,
                                     {ScriptTaskInputKind::PointerUp, 1, 1, 0, 0, 0, 0, 0, 0, {}},
                                     input_options).accepted(),
              "input fatal pointer up posts");
        const ScriptTaskWorkerRuntimeStepResult up = runtime.process_one(supervisor);
        check(up.fatal && runtime.fatal() &&
                  runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptException,
              "input listener exception stops the worker");
    }

    {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const ScriptAppSession session = supervisor.begin(17);
        ScriptTaskWorkerRuntime runtime(session, runtime_options());
        check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
                  ScriptTaskWorkerRuntimeInitStatus::Accepted,
              "service fatal fixture initializes");
        check(runtime.eval_with_supervisor(
                  supervisor,
                  "services.request(3, function () { throw new Error('service-fault'); });").ok,
              "service fatal callback evaluates");
        ScriptTaskPacket request_packet;
        check(supervisor.take_service_request(request_packet), "service fatal request posts");
        ScriptTaskServiceRequest request;
        check(decode_script_task_service_request(request_packet.payload,
                                                  runtime_options().service_request_codec,
                                                  request) == ScriptTaskServiceRequestCodecStatus::Accepted,
              "service fatal request decodes");
        check(runtime.publish_frame(supervisor).accepted(), "service fatal initial frame publishes");
        ScriptTaskAppFrame frame;
        check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "service fatal initial frame is consumed");
        ScriptTaskServiceCompletion completion;
        completion.kind = request.kind;
        completion.status = HostServiceStatus::Completed;
        completion.request_id = request.request_id;
        completion.client_token = request.client_token;
        std::vector<std::uint8_t> payload;
        check(encode_script_task_service_completion(completion, payload),
              "service fatal completion encodes");
        check(supervisor.post_service_completion(
                  {ScriptTaskPacketKind::ServiceCompletion, session, 1, 0, payload}) ==
                  ScriptTaskMailboxPostStatus::Accepted,
              "service fatal completion posts");
        const ScriptTaskWorkerRuntimeStepResult step = runtime.process_one(supervisor);
        check(step.fatal && runtime.fatal() &&
                  runtime.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::ScriptException,
              "service callback exception stops the worker");
    }
}

void worker_stop_is_explicit_and_idempotent() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(18);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "explicit stop fixture initializes");
    check(runtime.eval_with_supervisor(
              supervisor,
              "services.request(3, function () {});").ok,
          "explicit stop fixture creates worker-local service callback");
    runtime.stop();
    runtime.stop();
    check(runtime.stopped() && !runtime.initialized() && !runtime.service_gateway_available(),
          "worker stop releases local state and is idempotent");
    check(!runtime.eval("1 + 1").ok, "stopped worker rejects further script evaluation");
    check(!runtime.publish_frame(supervisor).accepted(),
          "stopped worker cannot publish a frame");
    check(runtime.initialize("<body></body>", "") == ScriptTaskWorkerRuntimeInitStatus::Stopped,
          "stopped worker cannot be rebound to another realm");
}

void worker_stop_preserves_supervisor_owned_frame_lease() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(21);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>retained</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted, "queued lease fixture initializes");
    check(runtime.publish_frame(supervisor).accepted(), "queued frame lease publishes");
    runtime.stop();
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "worker stop leaves sealed supervisor frame available for UI release");
    check(frame.display_list.size() == 1 && frame.display_list[0].text == "retained",
          "queued frame retains copied display values after worker teardown");
}

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
void worker_canonical_rejects_invalid_config_and_catalog_mismatch_before_vm(std::uint8_t version) {
    {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const runtime_test_fonts::Fonts fonts;
        const ScriptAppSession session = supervisor.begin(22, fonts.catalog);
        ScriptTaskWorkerRuntimeOptions options = runtime_options_v3();
        options.frame_codec.version = version;
        ScriptTaskWorkerRuntime missing(session, options);
        check(missing.initialize(supervisor, "<body></body>", "") ==
                  ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration,
              "v9 rejects missing configured catalog before VM creation");
        check(!missing.initialized() && !missing.eval("1 + 1").ok,
              "invalid v9 configuration performs no script work");
    }
    {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const runtime_test_fonts::Fonts expected;
        const runtime_test_fonts::Fonts other;
        const ScriptAppSession session = supervisor.begin(23, expected.catalog);
        ScriptTaskWorkerRuntimeOptions options = runtime_options_v3();
        options.frame_codec.version = version;
        options.text_catalog = other.catalog;
        ScriptTaskWorkerRuntime mismatch(session, options);
        check(mismatch.initialize(supervisor, "<body></body>", "") ==
                  ScriptTaskWorkerRuntimeInitStatus::ResourceMismatch,
              "v9 refuses a different session catalog before VM creation");
        check(!mismatch.initialized(), "catalog mismatch does not initialize worker realm");
    }
}

void worker_canonical_uses_catalog_for_layout_and_publishes_group_frame(std::uint8_t version) {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const runtime_test_fonts::Fonts fonts;
    const ScriptAppSession session = supervisor.begin(24, fonts.catalog);
    ScriptTaskWorkerRuntimeOptions options = runtime_options_v3();
    options.frame_codec.version = version;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(supervisor, "<body><div>AB</div></body>",
              "body { margin: 0; } div { display: block; font-size: 16px; "
              "font-family: sans-serif; white-space: nowrap; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "v9 worker initializes with its session catalog");
    check(runtime.publish_frame(supervisor).accepted(), "v9 worker publishes canonical frame");
    ScriptTaskAcceptedAppFrame accepted;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI accepts canonical worker frame with its session catalog");
    const auto has_prepared_text = [](const ScriptTaskAppFrame& frame) {
        return std::any_of(
            frame.display_list.begin(), frame.display_list.end(),
            [](const DisplayCommand& command) {
                return worker_test_access::worker_command_type(command) == DisplayCommandType::PreparedText;
            });
    };
    check(has_prepared_text(accepted.frame()), "canonical frame contains prepared text output");
    check(runtime.eval_with_supervisor(supervisor, "document.body.setAttribute('data-ready', 'yes');").ok,
          "v9 worker executes DOM mutation after resource identity validation");
    check(runtime.publish_frame(supervisor).accepted(), "v9 worker republishes its current canonical frame");
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI accepts the current canonical worker frame");
    check(!accepted.frame().groups.empty() && accepted.text_catalog() == fonts.catalog,
          "canonical group frame retains the exact catalog used for layout");
    check(has_prepared_text(accepted.frame()), "updated canonical frame retains prepared text output");
}

template<class Command> void worker_v10_mutation_publishes_changed_packed_axes() {
    if constexpr (detail::canonical::has_radius_y_fields<Command>::value) {
        ScriptTaskSupervisor supervisor = make_supervisor();
        const runtime_test_fonts::Fonts fonts;
        const ScriptAppSession session = supervisor.begin(25, fonts.catalog);
        auto options = runtime_options_v3(); options.frame_codec.version = 10;
        options.frame_codec.max_groups = 32; options.text_catalog = fonts.catalog;
        ScriptTaskWorkerRuntime runtime(session, options);
        check(runtime.initialize(supervisor, "<body><div id='ellipse'>AB</div></body>",
                  "body { margin: 0; } #ellipse { display: block; width: 96px; height: 64px; "
                  "background-color: #1122ee; overflow: hidden; font-size: 16px; "
                  "font-family: sans-serif; border-radius: 30px 20px 10px 24px / 6px 12px 4px 8px; }") ==
                  ScriptTaskWorkerRuntimeInitStatus::Accepted,
              "v10 worker initializes packed independent radius axes");
        check(!runtime.eval("1 + 1").ok && !runtime.fatal(),
              "v10 evaluation requires supervisor validation without corrupting the worker");
        check(runtime.publish_frame(supervisor).accepted(), "v10 worker publishes its initial packed-axis frame");
        ScriptTaskAcceptedAppFrame accepted;
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "consumer accepts the actual worker-produced v10 wire frame");
        const auto before = accepted.frame();
        const int radius_x = encode_corner_radii({30, 20, 10, 24});
        const int initial_y = encode_corner_radii({6, 12, 4, 8});
        const int changed_y = encode_corner_radii({18, 8, 16, 4});
        const auto has_axes = [&](const ScriptTaskAppFrame& frame, int expected_y) {
            bool paint = false, clip = false;
            for (const auto& command : frame.display_list)
                if (worker_test_access::worker_command_type(command) == DisplayCommandType::FillRect &&
                    command.color.r == 17 && command.color.g == 34 && command.color.b == 238)
                    paint = detail::legacy_command_fields(command).border_radius == radius_x &&
                        detail::canonical::radius_y(command) == expected_y;
            for (const auto& group : frame.groups)
                clip = clip || (group.has_clip && group.clip_border_radius == radius_x &&
                                group.clip_border_radius_y == expected_y);
            return paint && clip;
        };
        check(has_axes(before, initial_y), "published v10 paints and clip groups preserve signed per-corner Y");
        check(runtime.eval_with_supervisor(supervisor,
                  "document.getElementById('ellipse').setAttribute('style', "
                  "'border-radius: 30px 20px 10px 24px / 18px 8px 16px 4px;');").ok,
              "v10 worker rebuilds geometry after a Y-only DOM style mutation");
        check(runtime.publish_frame(supervisor).accepted(), "v10 worker republishes mutated packed axes");
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "consumer accepts the mutated v10 frame with the same catalog");
        check(has_axes(accepted.frame(), changed_y) && accepted.text_catalog() == fonts.catalog,
              "mutated v10 frame carries the new Y axes and exact layout catalog");
        check(runtime.telemetry().published_frame_seq == 2, "worker published both versions of the frame");
        ScriptTaskFrameRendererOptions render_options; render_options.text_catalog = accepted.text_catalog();
        ScriptTaskFrameRenderer renderer({}, {}, render_options);
        check(plan_script_task_group_damage(renderer, before, accepted.frame()).kind == ScriptTaskFrameDamageKind::Full,
              "worker-produced Y-only changes invalidate retained pixels");
        ScriptTaskFrameRenderStatus status;
        const auto old_pixels = renderer.render(before, {255, 255, 255, 255}, &status);
        check(status == ScriptTaskFrameRenderStatus::Accepted, "initial worker frame replays with its catalog");
        runtime.stop();
        const auto new_pixels = renderer.render(accepted.frame(), {255, 255, 255, 255}, &status);
        check(status == ScriptTaskFrameRenderStatus::Accepted, "accepted mutated frame survives worker teardown");
        bool changed = false;
        for (std::size_t i = 0; i < old_pixels.pixels.size(); ++i) {
            const auto a = old_pixels.pixels[i], b = new_pixels.pixels[i];
            changed = changed || a.r != b.r || a.g != b.g || a.b != b.b || a.a != b.a;
        }
        check(changed, "the worker Y-only mutation changes rendered pixels");
        std::cout << "worker v10: signed axes, supervisor-gated mutation, publish/take, damage and retained replay passed\n";
    }
}
#endif

void worker_fatal_publishes_once_as_a_supervisor_value() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(19);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body></body>", "") == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "fatal publication fixture initializes");
    check(!runtime.eval_with_supervisor(supervisor, "throw new Error('fatal-publish');").ok,
          "fatal publication fixture faults");
    check(runtime.publish_fatal(supervisor), "worker fatal publication is idempotent");
    check(runtime.publish_fatal(supervisor), "repeating fatal publication is idempotent");
    ScriptTaskPacket packet;
    check(supervisor.take_fatal(packet), "supervisor receives fatal value packet");
    ScriptTaskFatalRecord record;
    check(decode_script_task_fatal(packet.payload, {40}, record) == ScriptTaskFatalCodecStatus::Accepted,
          "supervisor fatal packet decodes");
    check(record.session == session && record.reason ==
              static_cast<std::uint8_t>(ScriptTaskWorkerRuntimeFatalReason::ScriptException),
          "supervisor fatal packet preserves worker value identity");
    check(!supervisor.take_fatal(packet), "repeated publish does not duplicate fatal packet");
    check(runtime.telemetry().fatal_publish_attempts == 1 &&
              runtime.telemetry().fatal_publish_rejections == 0,
          "worker records automatic fatal publication telemetry");
}

void worker_c_safe_fatal_boundary_publishes_value_record() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(20);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize("<body><p>safe</p></body>", "p { display: block; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "C-safe fatal fixture initializes");
    check(runtime.inject_c_safe_fatal_for_test(supervisor, 0x43534654),
          "C-safe fatal hook publishes without a VM abort");
    check(runtime.fatal() && runtime.fatal_record().reason ==
              ScriptTaskWorkerRuntimeFatalReason::ScriptFatal &&
              runtime.fatal_record().diagnostic_code == 0x43534654,
          "C-safe fatal hook preserves the explicit pure-value reason");
    check(!runtime.inject_c_safe_fatal_for_test(supervisor, 1),
          "C-safe fatal hook cannot duplicate an already terminal runtime");
    ScriptTaskPacket packet;
    check(supervisor.take_fatal(packet), "C-safe fatal record reaches supervisor mailbox");
    ScriptTaskFatalRecord record;
    check(decode_script_task_fatal(packet.payload, {40}, record) == ScriptTaskFatalCodecStatus::Accepted,
          "C-safe fatal record remains within the fixed value codec");
    check(record.session == session && record.reason ==
              static_cast<std::uint8_t>(ScriptTaskWorkerRuntimeFatalReason::ScriptFatal) &&
              record.diagnostic_code == 0x43534654,
          "C-safe fatal record carries no VM or native identity");
    check(!supervisor.take_fatal(packet), "C-safe fatal hook publishes exactly one record");
}

void worker_input_node_destruction_clears_interaction_state() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(9);
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
    check(runtime.initialize(
              "<body><button id='button'>destroy</button></body>",
              "button { display: block; width: 120px; height: 40px; margin: 0; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "node destruction fixture initializes");
    check(runtime.eval(
              "var button = document.getElementById('button');"
              "button.addEventListener('click', function () { document.body.textContent = 'gone'; });"
              "'ready';").ok,
          "node destruction listener evaluates");
    check(runtime.publish_frame(supervisor).accepted(), "node destruction initial frame publishes");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "node destruction initial frame is consumed");
    const ScriptTaskInputCodecOptions input_options = runtime_options().input_codec;
    check(post_script_task_input(supervisor, session, 1,
                                 {ScriptTaskInputKind::PointerDown, 1, 1, 0, 0, 0, 1, 0, 0, {}},
                                 input_options).accepted(),
          "node destruction pointer down posts");
    const ScriptTaskWorkerRuntimeStepResult down = runtime.process_one(supervisor);
    if (down.frame_published) {
        check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "node destruction pointer-down frame is consumed");
    }
    check(post_script_task_input(supervisor, session, 2,
                                 {ScriptTaskInputKind::PointerUp, 1, 1, 0, 0, 0, 0, 0, 0, {}},
                                 input_options).accepted(),
          "node destruction pointer up posts");
    const ScriptTaskWorkerRuntimeStepResult up = runtime.process_one(supervisor);
    check(!up.fatal && !runtime.fatal(), "destroyed input target does not leave a dangling worker pointer");
}

void worker_event_identity_refusal_becomes_budget_fatal_without_publishing_a_frame() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(91);
    auto options = runtime_options();
    options.script.max_active_event_dispatches = 0;
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(
              "<body><button id='button'>unchanged</button></body>",
              "button { display:block; width:120px; height:40px; }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "event refusal worker initializes normally");
    check(runtime.eval(
              "var b=document.getElementById('button');"
              "b.onclick=function(e){b.textContent='must not run';}; 'ready'").ok,
          "event refusal worker registers a normal handler");
    const auto result = runtime.eval_with_supervisor(supervisor, "b.click(); 'returned'");
    check(result.ok && runtime.fatal() && runtime.fatal_record().reason ==
              ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded,
          "Event construction refusal becomes worker budget fatal even if surrounding eval returns normally");
    check(!runtime.publish_frame(supervisor).accepted(), "failed Event dispatch cannot publish a later worker frame");
    ScriptTaskPacket packet;
    check(supervisor.take_fatal(packet), "Event refusal publishes a bounded fatal value record");
    ScriptTaskFatalRecord record;
    check(decode_script_task_fatal(packet.payload, {40}, record) == ScriptTaskFatalCodecStatus::Accepted &&
              record.reason == static_cast<std::uint8_t>(ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded),
          "Event refusal uses the existing budget-fatal value contract");
}

struct WorkerOwnerMutationContext {
    std::unique_ptr<ScriptTaskWorkerRuntime>* owner = nullptr;
    ScriptTaskSupervisor* supervisor = nullptr;
    bool reset = false;
    bool nested = false;
    std::size_t calls = 0;
    bool rejected = false;
};

void worker_owner_mutation(void* user, const Node&, DomDirtyFlags, std::uint64_t) {
    auto& context = *static_cast<WorkerOwnerMutationContext*>(user);
    ++context.calls;
    auto* worker = context.owner->get();
    if (context.nested) {
        context.nested = false;
        check(worker->eval_with_supervisor(*context.supervisor,
            "document.getElementById('button').textContent='nested';").ok,
            "nested supervisor scope survives owner reset");
        return;
    }
    if (context.reset) {
        context.owner->reset();
    } else {
        worker->stop();
        worker->stop();
        context.rejected = !worker->eval("'refused'").ok &&
            !worker->publish_frame(*context.supervisor).accepted() &&
            !worker->service_gateway_available() && worker->stopped();
    }
}

void worker_owner_teardown_during_callbacks() {
    for (bool reset : {false, true}) {
        for (bool nested : {false, true}) {
            for (bool timer : {false, true}) {
                auto supervisor = make_supervisor();
                const auto session = supervisor.begin(94);
                WorkerOwnerMutationContext context;
                auto options = runtime_options();
                options.mutation_observer = worker_owner_mutation;
                options.mutation_observer_user = &context;
                auto owner = std::make_unique<ScriptTaskWorkerRuntime>(session, options);
                context.owner = &owner;
                context.supervisor = &supervisor;
                context.reset = reset;
                context.nested = nested;
                check(owner->initialize("<body><button id='button'>ready</button></body>",
                    "button { display:block; width:100px; height:40px; }") ==
                    ScriptTaskWorkerRuntimeInitStatus::Accepted, "owner teardown initialize");
                check(owner->eval(timer ?
                    "setTimeout(function(){document.getElementById('button').textContent='timer';},1);" :
                    "document.getElementById('button').onclick=function(){this.textContent='event';};").ok,
                    "owner teardown registers callback");
                if (timer) {
                    auto result = owner->pump_callbacks(2, supervisor);
                    check(!result.frame_published, "stopped timer cannot publish old frame");
                } else {
                    (void)owner->eval_with_supervisor(supervisor,
                        "document.getElementById('button').click();");
                }
                check(context.calls == (nested ? 2u : 1u), "stopped observer admits no later callback");
                check(reset ? !owner : context.rejected, "real reset or immediate stop rejection observed");
                ScriptTaskAppFrame frame;
                check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) !=
                    ScriptTaskAppFrameTakeStatus::Accepted, "retired callback publishes no old frame");
            }
        }
    }
}

void worker_owner_reset_during_service_completion() {
    auto supervisor = make_supervisor();
    auto session = supervisor.begin(95);
    WorkerOwnerMutationContext context;
    auto options = runtime_options();
    options.mutation_observer = worker_owner_mutation;
    options.mutation_observer_user = &context;
    auto owner = std::make_unique<ScriptTaskWorkerRuntime>(session, options);
    context.owner = &owner;
    context.supervisor = &supervisor;
    context.reset = true;
    check(owner->initialize("<body><button id='button'>ready</button></body>", "") ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted, "service owner initialize");
    check(owner->eval_with_supervisor(supervisor,
        "services.request(3,function(){document.getElementById('button').textContent='done';});").ok,
        "service owner request");
    ScriptTaskPacket request_packet;
    check(supervisor.take_service_request(request_packet), "service owner receives request");
    ScriptTaskServiceRequest request;
    check(decode_script_task_service_request(request_packet.payload, options.service_request_codec, request) ==
        ScriptTaskServiceRequestCodecStatus::Accepted, "service owner decodes request");
    ScriptTaskServiceCompletion completion;
    completion.kind = request.kind;
    completion.status = HostServiceStatus::Completed;
    completion.request_id = request.request_id;
    completion.client_token = request.client_token;
    std::vector<std::uint8_t> bytes;
    check(encode_script_task_service_completion(completion, bytes), "service owner completion encodes");
    check(supervisor.post_service_completion(
        {ScriptTaskPacketKind::ServiceCompletion, session, 1, 0, bytes}) ==
        ScriptTaskMailboxPostStatus::Accepted, "service owner completion queues");
    const auto result = owner->process_one(supervisor);
    check(!owner && context.calls == 1 && result.service_completion_consumed && !result.frame_published,
        "real service callback owner reset drains safely without frame publication");
}

void worker_owner_admission_refusal() {
    auto supervisor = make_supervisor();
    auto options = runtime_options();
    options.max_active_operations = 0;
    ScriptTaskWorkerRuntime worker(supervisor.begin(94), options);
    check(worker.initialize("<body></body>", "") != ScriptTaskWorkerRuntimeInitStatus::Accepted,
        "zero operation capacity refuses session initialization");
    check(!worker.eval("'no admission'").ok, "zero operation capacity refuses eval");
    worker.stop();
    check(worker.stopped() && !worker.initialized(), "refused worker stops idempotently");
    options.max_active_operations = 1;
    ScriptTaskWorkerRuntime capped(supervisor.begin(96), options);
    check(capped.initialize("<body></body>", "") != ScriptTaskWorkerRuntimeInitStatus::Accepted,
        "backend owner pin cannot exceed worker admission cap");
    capped.stop();
}

} // namespace

int script_task_worker_runtime_tests_main() {
    try {
        worker_owner_teardown_during_callbacks();
        worker_owner_admission_refusal();
        worker_owner_reset_during_service_completion();
        worker_input_publishes_value_frame();
        worker_interaction_hints_preserve_css_and_dom_events();
        worker_retries_dirty_frame_after_ui_mailbox_backpressure();
        worker_v2_publishes_clip_metadata_without_cross_task_objects();
        worker_resolves_media_queries_against_its_runtime_viewport();
        worker_rebuild_does_not_refire_autofocus();
        worker_applies_host_layout_box_budget();
        worker_eval_failure_becomes_value_fatal();
        worker_timer_publishes_value_frame();
        worker_interval_rotation_publishes_transformed_value_frame();
        worker_host_time_is_initialized_before_author_script_runs();
        worker_service_completion_reaches_js_as_copied_value();
        worker_service_payload_lease_failure_completes_callback_as_error();
        worker_service_callback_budget_rejects_before_queueing();
        worker_service_cancel_posts_only_value_identity();
        worker_service_completion_replay_handles_one_hundred_callbacks();
        worker_timer_exception_becomes_fatal_value();
        worker_animation_watchdog_becomes_fatal_value();
        worker_input_and_service_callback_exceptions_stop_the_worker();
        worker_stop_is_explicit_and_idempotent();
        worker_stop_preserves_supervisor_owned_frame_lease();
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        for (auto version : {std::uint8_t{9}, std::uint8_t{10}}) {
            if (!script_task_app_frame_codec_supports_version(version)) continue;
            worker_canonical_rejects_invalid_config_and_catalog_mismatch_before_vm(version);
            worker_canonical_uses_catalog_for_layout_and_publishes_group_frame(version);
        }
        if (script_task_app_frame_codec_supports_version(10))
            worker_v10_mutation_publishes_changed_packed_axes<DisplayCommand>();
#endif
        worker_fatal_publishes_once_as_a_supervisor_value();
        worker_c_safe_fatal_boundary_publishes_value_record();
        worker_input_node_destruction_clears_interaction_state();
        worker_event_identity_refusal_becomes_budget_fatal_without_publishing_a_frame();
    } catch (const std::exception& error) {
        std::cerr << "script task worker runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
