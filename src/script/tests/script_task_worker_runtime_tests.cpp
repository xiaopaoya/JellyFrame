#include "script/script_task_worker_runtime.h"
#include "script/script_capabilities.h"

#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_fatal_codec.h"
#include "app_runtime/script_task_canonical_codec.h"
#include "app_runtime/script_task_frame_renderer.h"
#include "app_runtime/tests/runtime_font_test_fixture.h"
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#include "runtime_shaped_text_test_fixture.h"
#include "render_core/html_parser.h"
#include "render_core/css_parser.h"
#endif

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
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    bool saw_clicked = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "clicked") saw_clicked = true;
    }
    check(saw_clicked, "replacement frame contains JS DOM mutation");
#endif
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
            saw_change = saw_change ||
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
                command.text == "focused" || command.text == "pressed" ||
#endif
                (worker_test_access::worker_command_type(command) == DisplayCommandType::FillRect &&
                 command.color.r == 255 && command.color.b == 0);
        }
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        check(saw_change, "replacement carries actual pseudo CSS or event output");
#else
        check(step.frame_published, "Core text path publishes the interaction frame");
#endif
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
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    bool saw_timer = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "timer") saw_timer = true;
    }
    check(saw_timer, "timer replacement frame contains the worker DOM mutation");
#endif
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
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    bool saw_done = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "done") saw_done = true;
    }
    check(saw_done, "service callback sees copied payload bytes");
#endif
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
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    bool saw_failure = false;
    for (const DisplayCommand& command : frame.display_list) {
        if (command.text == "lease-failed") saw_failure = true;
    }
    check(saw_failure, "lease failure callback observes the normalized error value");
#endif
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
#if !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
    check(frame.display_list.size() == 1 && frame.display_list[0].text == "retained",
          "queued frame retains copied display values after worker teardown");
#endif
}

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
void worker_core_text_select_popup_prepares_and_selects_visible_options(std::uint8_t version) {
#if !JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED
    (void)version;
    return;
#else
    auto supervisor = make_supervisor();
    runtime_test_fonts::Fonts fonts;
    auto options = runtime_options_v3();
    options.frame_codec.version = version;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    const auto session = supervisor.begin(296, fonts.catalog);
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(supervisor,
        "<body><select id='choice'><option value='a'>A</option><option value='b'>B</option></select></body>",
        "*{margin:0;padding:0;border:0} body{height:100px} select{display:block;width:120px;height:24px;"
        "font-family:sans-serif;font-size:16px;line-height:20px;color:#000;background:#fff}") ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted,
        "Core text popup worker initializes");
    const auto observer = runtime.eval_with_supervisor(supervisor,
        "var changes=0; var choice=document.getElementById('choice');"
        "choice.addEventListener('change', function(){changes++}); 'ready'");
    check(observer.ok,
          "Core text popup change observer installs");

    ScriptTaskAcceptedAppFrame frame;
    const auto take_frame = [&] {
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
                  ScriptTaskAppFrameTakeStatus::Accepted,
              "Core text popup worker frame is accepted");
    };
    check(runtime.publish_frame(supervisor).accepted(), "Core text popup initial frame publishes");
    take_frame();

    std::uint32_t input_sequence = 1;
    const auto send_input = [&](ScriptTaskInputKind kind, int x, int y) {
        check(post_script_task_input(supervisor, session, input_sequence++,
                  {kind, x, y, 0, 0, kind == ScriptTaskInputKind::PointerMove ? -1 : 0,
                   kind == ScriptTaskInputKind::PointerUp ? 0 : 1, 0, 0, {}},
                  options.input_codec).accepted(),
              "Core text popup pointer input posts");
        const auto step = runtime.process_one(supervisor);
        check(step.input_accepted && !step.fatal, "Core text popup pointer input is accepted");
        if (step.frame_published) take_frame();
    };
    send_input(ScriptTaskInputKind::PointerDown, 4, 12);
    send_input(ScriptTaskInputKind::PointerUp, 4, 12);

    Rect option_b_rect{};
    bool found_option_b = false;
    for (const auto& command : frame.frame().display_list) {
        if (worker_test_access::worker_command_type(command) == DisplayCommandType::Text &&
            command.text == "B") {
            check(false, "Core text popup does not emit legacy option Text");
        }
        if (worker_test_access::worker_command_type(command) != DisplayCommandType::PreparedText) continue;
        PreparedTextView view;
        check(decode_prepared_text(command.text, view), "popup prepared text packet decodes");
        for (std::size_t i = 0; i < view.size(); ++i) {
            PreparedTextGlyph glyph;
            check(view.read(i, glyph), "popup prepared glyph reads");
            if (glyph.source_codepoint == 'B') {
                option_b_rect = command.rect;
                found_option_b = true;
            }
        }
    }
    check(found_option_b && option_b_rect.width > 0 && option_b_rect.height > 0,
          "every visible Core text popup option is prepared");

    ScriptTaskFrameRenderer renderer;
    FrameBuffer pixels(options.viewport.width, options.viewport.height, {255, 255, 255, 255});
    check(renderer.render_into(frame, pixels, {255, 255, 255, 255}),
          "Core text popup prepared frame renders");
    bool option_b_has_ink = false;
    for (int y = std::max(0, option_b_rect.y);
         y < std::min(pixels.height, option_b_rect.y + option_b_rect.height); ++y) {
        for (int x = std::max(0, option_b_rect.x);
             x < std::min(pixels.width, option_b_rect.x + option_b_rect.width); ++x) {
            const Color pixel = pixels.pixel(x, y);
            option_b_has_ink = option_b_has_ink || pixel.r < 240 || pixel.g < 240 || pixel.b < 240;
        }
    }
    check(option_b_has_ink, "Core text popup option B paints visible pixels in its row");

    const int option_b_x = option_b_rect.x + option_b_rect.width / 2;
    const int option_b_y = option_b_rect.y + option_b_rect.height / 2;
    send_input(ScriptTaskInputKind::PointerDown, option_b_x, option_b_y);
    send_input(ScriptTaskInputKind::PointerUp, option_b_x, option_b_y);
    const auto change_count = runtime.eval_with_supervisor(supervisor, "changes");
    check(change_count.ok && change_count.value == "1",
          "clicking prepared popup option B dispatches one change event");

    std::size_t selected_b_glyphs = 0;
    bool popup_still_has_option_a = false;
    for (const auto& command : frame.frame().display_list) {
        if (worker_test_access::worker_command_type(command) != DisplayCommandType::PreparedText) continue;
        PreparedTextView view;
        check(decode_prepared_text(command.text, view), "selected popup label packet decodes");
        for (std::size_t i = 0; i < view.size(); ++i) {
            PreparedTextGlyph glyph;
            check(view.read(i, glyph), "selected popup label glyph reads");
            selected_b_glyphs += glyph.source_codepoint == 'B' ? 1U : 0U;
            popup_still_has_option_a = popup_still_has_option_a || glyph.source_codepoint == 'A';
        }
    }
    check(selected_b_glyphs == 1 && !popup_still_has_option_a,
          "selecting option B closes the popup and emits only its selected Core text label");
#endif
}

#if JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION >= 2
void worker_shared_inline_paragraph(std::uint8_t version) {
    FrameBuffer reference;
    for (const bool nested : {false, true}) {
        auto supervisor = make_supervisor();
        runtime_test_fonts::Fonts fonts;
        const auto session = supervisor.begin(297, fonts.catalog);
        auto options = runtime_options_v3();
        options.frame_codec.version = version; options.frame_codec.max_groups = 32;
        options.text_catalog = fonts.catalog;
        auto runtime = std::make_unique<ScriptTaskWorkerRuntime>(session, options);
        const auto html = nested
            ? "<body><div><span>A<span>B A</span>B </span><span>AB</span></div></body>"
            : "<body><div><span>AB AB AB</span></div></body>";
        check(runtime->initialize(supervisor, html,
            "* { margin: 0; padding: 0; border: 0; } div { width: 25px; font-size: 16px; "
            "line-height: 20px; text-align: center; }") == ScriptTaskWorkerRuntimeInitStatus::Accepted,
            "shared paragraph worker initializes");
        check(runtime->publish_frame(supervisor).accepted(), "shared paragraph publishes");
        ScriptTaskAcceptedAppFrame accepted;
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
            ScriptTaskAppFrameTakeStatus::Accepted, "shared paragraph frame accepted");
        bool shifted = false;
        for (const auto& command : accepted.frame().display_list) {
            if (worker_test_access::worker_command_type(command) != DisplayCommandType::PreparedText) continue;
            PreparedTextView view; PreparedTextGlyph first;
            check(decode_prepared_text(command.text, view), "shared paragraph packet decodes");
            shifted = shifted || (command.text[4] == 2 && view.read(0, first) && first.x != 0);
        }
        check(shifted, "shared paragraph emits genuine nonzero-origin v2 packet");
        runtime->stop(); runtime.reset(); supervisor.begin(298, {}); fonts.catalog.reset(); options.text_catalog.reset();
        ScriptTaskFrameRenderer renderer;
        FrameBuffer pixels(options.viewport.width, options.viewport.height, {});
        check(renderer.render_into(accepted, pixels, {255, 255, 255, 255}),
            "shared paragraph retains catalog after worker stop and owner replacement");
        check(std::any_of(pixels.pixels.begin(), pixels.pixels.end(),
            [](Color color) { return color.r != 255 || color.g != 255 || color.b != 255; }),
            "shared paragraph paints visible pixels");
        if (!nested) reference = std::move(pixels);
        else {
            check(reference.pixels.size() == pixels.pixels.size(), "shared paragraph pixel extents match");
            for (std::size_t i = 0; i < pixels.pixels.size(); ++i) {
                const auto a = reference.pixels[i], b = pixels.pixels[i];
                check(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a,
                    "nested shared paragraph preserves continuous text pixels");
            }
        }
    }
    std::cout << "worker v" << int(version) << ": shared inline v2 paragraphs preserve partition pixels and retained catalog\n";
}
#endif

#if defined(JELLYFRAME_TEST_GENERATED_INLINE_TEXT) && JELLYFRAME_TEST_GENERATED_INLINE_TEXT
void worker_generated_inline_text(std::uint8_t version) {
    auto supervisor = make_supervisor();
    runtime_test_fonts::Fonts fonts;
    const auto session = supervisor.begin(301, fonts.catalog);
    auto options = runtime_options_v3();
    options.frame_codec.version = version;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    auto runtime = std::make_unique<ScriptTaskWorkerRuntime>(session, options);
    const auto html = "<body><div id='host'><span id='member'>A</span></div></body>";
    const auto css =
        "* { margin: 0; padding: 0; border: 0; } "
        "#host { display: block; width: 120px; font-size: 16px; line-height: 20px; color: black; } "
        "#host::before { content: 'A'; color: red; } "
        "#member { display: inline; } "
        "#member::before { content: 'B'; color: green; } "
        "#member::after { content: '?'; color: blue; } "
        "#host::after { content: 'B'; color: red; }";
    check(runtime->initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "generated inline text worker initializes");
    check(runtime->publish_frame(supervisor).accepted(), "generated inline text frame publishes");
    ScriptTaskAcceptedAppFrame accepted;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "generated inline text frame is accepted and decoded");

    struct ObservedGlyph { std::uint32_t codepoint; Color color; };
    std::vector<ObservedGlyph> observed;
    for (const auto& command : accepted.frame().display_list) {
        if (worker_test_access::worker_command_type(command) != DisplayCommandType::PreparedText) continue;
        PreparedTextView view;
        check(decode_prepared_text(command.text, view), "generated text packet decodes");
        for (std::size_t i = 0; i < view.size(); ++i) {
            PreparedTextGlyph glyph;
            check(view.read(i, glyph), "generated text glyph reads");
            observed.push_back({glyph.source_codepoint, command.color});
        }
    }
    const std::vector<std::uint32_t> expected_order{'A', 'B', 'A', '?', 'B'};
    check(observed.size() == expected_order.size(), "worker frame carries all generated and DOM text");
    for (std::size_t i = 0; i < expected_order.size(); ++i)
        check(observed[i].codepoint == expected_order[i],
              "worker frame preserves host/member before-text-after source order");
    const auto same_color = [&](std::size_t a_index, std::size_t b_index) {
        const auto a = observed[a_index].color, b = observed[b_index].color;
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    check(same_color(0, 4), "host before and after text retain one host paint style");
    check(!same_color(0, 2) && !same_color(1, 2) && !same_color(3, 2),
          "generated source paints remain distinct from DOM text paint");
    check(!same_color(0, 1) && !same_color(0, 3) && !same_color(1, 3),
          "host and inline-member generated sources retain their individual paint styles");

    runtime->stop();
    runtime.reset();
    supervisor.begin(302, {});
    fonts.catalog.reset();
    options.text_catalog.reset();
    ScriptTaskFrameRenderer renderer;
    FrameBuffer first(options.viewport.width, options.viewport.height, {255, 255, 255, 255});
    FrameBuffer second(options.viewport.width, options.viewport.height, {255, 255, 255, 255});
    ScriptTaskFrameRenderStatus status;
    check(renderer.render_into(accepted, first, {255, 255, 255, 255}, nullptr, 0, nullptr, &status) &&
          status == ScriptTaskFrameRenderStatus::Accepted,
          "generated text retained frame paints after worker and catalog teardown");
    check(renderer.render_into(accepted, second, {255, 255, 255, 255}, nullptr, 0, nullptr, &status) &&
          status == ScriptTaskFrameRenderStatus::Accepted,
          "generated text retained frame replays without resolving live DOM state");
    bool visible = false;
    for (std::size_t i = 0; i < first.pixels.size(); ++i) {
        const auto a = first.pixels[i], b = second.pixels[i];
        check(a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a,
              "generated text retained replay is pixel-identical");
        visible = visible || a.r != 255 || a.g != 255 || a.b != 255;
    }
    check(visible, "generated text retained replay paints visible pixels");
    std::cout << "worker v" << int(version)
              << ": generated before/text/after order, colors and retained replay passed\n";
}
#endif

class WorkerImages final : public RuntimeImageProviderSource {
public:
    explicit WorkerImages(ImageIntrinsicSize size = {ImageIntrinsicSizeState::Known, 20, 20}) : size(size) {}
    ImageIntrinsicSize size;
    mutable std::size_t resolves = 0;
    mutable std::uint32_t metadata_handle = 0;
    mutable std::uint32_t painted_handle = 0;
    std::uint64_t generation() const noexcept override { return 1; }
    bool contains(std::uint32_t handle) const noexcept override {
        return handle == 73 || handle == 74;
    }
    bool resolve(std::string_view url, ResolvedImageResource& output) const override {
        ++resolves;
        if (url != "/assets/content.bmp" && url != "/assets/background.bmp") return false;
        output.image_handle = url == "/assets/content.bmp" ? 73 : 74;
        output.intrinsic_size = url == "/assets/content.bmp"
            ? size : ImageIntrinsicSize{ImageIntrinsicSizeState::Known, 7, 13};
        if (resolves == 1) metadata_handle = output.image_handle;
        return true;
    }
    bool paint(FrameBuffer& target, const ImagePaintRequest& request) const override {
        if (!contains(request.image_handle)) return false;
        painted_handle = request.image_handle;
        const Color color = request.image_handle == 73 ? Color{231, 17, 53, 255} : Color{19, 173, 67, 255};
        const auto rect = request.destination;
        for (int y = std::max(0, rect.y); y < std::min(target.height, rect.y + rect.height); ++y) {
            for (int x = std::max(0, rect.x); x < std::min(target.width, rect.x + rect.width); ++x) {
                target.pixel(x, y) = color;
            }
        }
        return true;
    }
};

#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) && JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1
void worker_natural_image_snapshot(std::string_view sizing, ImageIntrinsicSize size,
                                   std::size_t query_limit, int width, int height,
                                   std::size_t accepted_metadata, std::size_t expected_queries = 1) {
    auto supervisor = make_supervisor();
    const runtime_test_fonts::Fonts fonts;
    auto source = std::make_shared<const WorkerImages>(size);
    RuntimeImageProvider::Handle images;
    check(RuntimeImageProvider::create(source, images) == RuntimeImageProviderStatus::Ready,
          "natural image snapshot created");
    const auto session = supervisor.begin(197, fonts.catalog, images);
    auto options = runtime_options_v3();
    options.frame_codec.version = 10;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.image_provider = images;
    NaturalImageSizingStatistics statistics;
    options.natural_image_sizing.statistics = &statistics;
    options.natural_image_sizing.max_queries = query_limit;
    ScriptTaskWorkerRuntime runtime(session, options);
    const std::string css = "body { margin: 0; } img { display: block; " + std::string(sizing) + " }";
    check(runtime.initialize(supervisor, "<body><img src='/assets/content.bmp'></body>", css) ==
          ScriptTaskWorkerRuntimeInitStatus::Accepted, "natural worker initializes through supervisor");
    check(statistics.queries == (query_limit ? expected_queries : 0u) && statistics.accepted == accepted_metadata,
          "natural metadata has one pass-local query or bounded refusal");
    check(runtime.publish_frame(supervisor).accepted(), "natural frame publishes");
    ScriptTaskAcceptedAppFrame accepted;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
          ScriptTaskAppFrameTakeStatus::Accepted, "natural frame accepted");
    bool found = false;
    for (const auto& command : accepted.frame().display_list) {
        if (worker_test_access::worker_command_type(command) != DisplayCommandType::Image) continue;
        found = true;
        check(command.rect.width == width && command.rect.height == height,
              "natural image command preserves computed dimensions");
    }
    check(found == (width > 0 && height > 0), "natural fallback image command admission");
    check(source->resolves == statistics.queries + 1u,
          "source calls distinguish layout metadata from paint handle resolution");
    const auto queries_before_replay = source->resolves;
    runtime.stop();
    supervisor.begin(198, fonts.catalog); // Release supervisor's old resource owner.
    options.image_provider.reset();
    images.reset();
    ScriptTaskFrameRendererOptions render_options;
    render_options.text_catalog = fonts.catalog;
    ScriptTaskFrameRenderer renderer(render_options);
    ScriptTaskFrameRenderStatus status;
    FrameBuffer pixels(accepted.frame().viewport.width, accepted.frame().viewport.height, {});
    renderer.render_into(accepted, pixels, {255, 255, 255, 255}, nullptr, 0, nullptr, &status);
    check(status == ScriptTaskFrameRenderStatus::Accepted, "accepted snapshot replays after owner replacement");
    if (found) {
        const auto pixel = pixels.pixels[(height / 2) * pixels.width + width / 2];
        check(pixel.r == 231 && pixel.g == 17 && pixel.b == 53, "retained natural snapshot paints pixels");
        check(source->metadata_handle == 73 && source->painted_handle == source->metadata_handle,
              "natural metadata and paint retain the selected content image handle");
    }
    check(source->resolves == queries_before_replay, "replay performs no metadata or URL resolution");
}

void worker_natural_image_cases() {
    const ImageIntrinsicSize known{ImageIntrinsicSizeState::Known, 40, 20};
    worker_natural_image_snapshot("", known, 128, 40, 20, 1);
    worker_natural_image_snapshot("width: 20px;", known, 128, 20, 10, 1);
    worker_natural_image_snapshot("height: 10px;", known, 128, 20, 10, 1);
    worker_natural_image_snapshot("width: 20px; aspect-ratio: 1 / 1;", known, 128, 20, 20, 1);
    worker_natural_image_snapshot("max-width: 20px;", known, 128, 20, 10, 1);
    worker_natural_image_snapshot("min-height: 30px;", known, 128, 60, 30, 1);
    worker_natural_image_snapshot("", {ImageIntrinsicSizeState::Known, 0, 20}, 128, 0, 0, 0);
    worker_natural_image_snapshot("", known, 0, 0, 0, 0);
    worker_natural_image_snapshot("", {}, 128, 0, 0, 0);
    worker_natural_image_snapshot("width: 20px; height: 10px;", known, 128, 20, 10, 0, 0);
    std::cout << "worker natural image: 10 metadata, dimensions, fallback and retained-pixel cases passed\n";
}

#if defined(JELLYFRAME_TEST_NATURAL_INLINE_IMAGES) && JELLYFRAME_TEST_NATURAL_INLINE_IMAGES
// Behavioral qualification is explicitly enabled for the pinned post-PR35 Core.
// The earlier natural-image API also exists on block-only Core revisions.
void worker_natural_inline_image_snapshot(ImageIntrinsicSize size, bool expect_image) {
    auto supervisor = make_supervisor();
    const runtime_test_fonts::Fonts fonts;
    auto source = std::make_shared<const WorkerImages>(size);
    RuntimeImageProvider::Handle images;
    check(RuntimeImageProvider::create(source, images) == RuntimeImageProviderStatus::Ready,
          "natural inline image snapshot created");
    const auto session = supervisor.begin(199, fonts.catalog, images);
    auto options = runtime_options_v3();
    options.frame_codec.version = 10;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.image_provider = images;
    NaturalImageSizingStatistics statistics;
    options.natural_image_sizing.statistics = &statistics;
    ScriptTaskWorkerRuntime runtime(session, options);
    const std::string html =
        "<body><div id='line'><span>A<img src='/assets/content.bmp'>B</span></div></body>";
    const std::string css =
        "body { margin: 0; } #line { display: block; width: 100px; font-size: 16px; line-height: 24px; } "
        "span { display: inline; } img { display: inline; }";
    check(runtime.initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "natural inline worker initializes through supervisor");
    check(statistics.queries == 1 && statistics.accepted == (expect_image ? 1u : 0u),
          "inline metadata is queried once and accepted only when valid");
    check(runtime.publish_frame(supervisor).accepted(), "natural inline frame publishes");
    ScriptTaskAcceptedAppFrame accepted;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
          ScriptTaskAppFrameTakeStatus::Accepted, "natural inline frame accepted");
    std::size_t image_commands = 0;
    std::size_t text_commands = 0;
    Rect image_rect{};
    for (const auto& command : accepted.frame().display_list) {
        const auto type = worker_test_access::worker_command_type(command);
        if (type == DisplayCommandType::Image) {
            ++image_commands;
            image_rect = command.rect;
            check(text_commands == 1, "natural image stays between its two text owners");
            check(command.rect.width >= 40 && command.rect.width <= 41 && command.rect.height == 20,
                  "inline natural image command covers its Q1000 width and integral height");
        } else if (type == DisplayCommandType::PreparedText) {
            ++text_commands;
        }
    }
    check(image_commands == (expect_image ? 1u : 0u),
          "natural inline image is emitted once or omitted for unknown metadata");
    check(text_commands == 2,
          "shared inline text on both sides of the image survives acceptance or fallback");
    check(source->resolves == statistics.queries + 1u,
          "inline metadata lookup and paint handle resolution are distinct provider calls");
    const auto resolves_before_replay = source->resolves;
    runtime.stop();
    supervisor.begin(200, fonts.catalog);
    options.image_provider.reset();
    images.reset();
    ScriptTaskFrameRendererOptions render_options;
    render_options.text_catalog = fonts.catalog;
    ScriptTaskFrameRenderer renderer(render_options);
    FrameBuffer pixels(accepted.frame().viewport.width, accepted.frame().viewport.height, {});
    ScriptTaskFrameRenderStatus status;
    check(renderer.render_into(accepted, pixels, {255, 255, 255, 255}, nullptr, 0, nullptr, &status) &&
          status == ScriptTaskFrameRenderStatus::Accepted,
          "natural inline accepted frame replays after worker and provider release");
    check(source->resolves == resolves_before_replay,
          "natural inline replay does not resolve metadata or image URLs again");
    if (expect_image) {
        const auto pixel = pixels.pixel(image_rect.x + image_rect.width / 2,
                                        image_rect.y + image_rect.height / 2);
        check(pixel.r == 231 && pixel.g == 17 && pixel.b == 53,
              "retained natural inline image paints provider pixels");
    }
}

void worker_natural_inline_image_cases() {
    worker_natural_inline_image_snapshot({ImageIntrinsicSizeState::Known, 40, 20}, true);
    worker_natural_inline_image_snapshot({}, false);
    std::cout << "worker natural inline image: accepted sizing, fallback text and retained replay passed\n";
}
#endif
#endif

void worker_canonical_resolves_content_and_background_images(std::uint8_t version) {
    auto supervisor = make_supervisor();
    const runtime_test_fonts::Fonts fonts;
    RuntimeImageProvider::Handle images;
    check(RuntimeImageProvider::create(std::make_shared<const WorkerImages>(), images) ==
              RuntimeImageProviderStatus::Ready, "immutable worker image provider created");
    const auto session = supervisor.begin(97, fonts.catalog, images);
    auto options = runtime_options_v3();
    options.frame_codec.version = version;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.image_provider = images;
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(supervisor,
              "<body><img src='/assets/content.bmp'><div></div></body>",
              "body { margin: 0; } img { display: block; width: 20px; height: 20px; border-radius: 12px; } "
              "div { display: block; width: 20px; height: 20px; "
              "background-image: url('/assets/background.bmp'); }") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted, "worker image document initializes");
    check(runtime.publish_frame(supervisor).accepted(), "worker image frame publishes");
    ScriptTaskAcceptedAppFrame accepted;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
              ScriptTaskAppFrameTakeStatus::Accepted, "consumer accepts worker image frame");
    bool content = false, background = false;
    for (const auto& command : accepted.frame().display_list) {
        if (worker_test_access::worker_command_type(command) != DisplayCommandType::Image) continue;
        content = content || command.image_paint().image_handle == 73;
        background = background || command.image_paint().image_handle == 74;
    }
    check(background, "worker preserves resolved CSS background image commands");
    check(content, "worker resolves HTML img src into a content image command");
    ScriptTaskFrameRendererOptions render_options;
    render_options.text_catalog = fonts.catalog;
    render_options.image_provider = images;
    ScriptTaskFrameRenderer renderer(render_options);
    runtime.stop();
    ScriptTaskFrameRenderStatus status;
    const auto pixels = renderer.render(accepted.frame(), {255, 255, 255, 255}, &status);
    check(status == ScriptTaskFrameRenderStatus::Accepted, "worker image frame replays after worker stop");
    const auto content_pixel = pixels.pixels[10 * pixels.width + 10];
    const auto background_pixel = pixels.pixels[30 * pixels.width + 10];
    check(content_pixel.r == 231 && content_pixel.g == 17 && content_pixel.b == 53,
          "accepted HTML image paints provider pixels");
    check(background_pixel.r == 19 && background_pixel.g == 173 && background_pixel.b == 67,
          "accepted CSS background paints provider pixels");
    std::cout << "worker v" << int(version) << ": content/background images publish, resolve and paint passed\n";
}
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

#endif // scalar canonical worker cases

#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)

#if JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
void worker_shaped_dom_layout_and_input() {
    runtime_test_shaped_text::Assets assets("regular", 7401);
    auto binding = runtime_test_shaped_text::create_binding(assets);
    runtime_test_fonts::Fonts fonts;
    auto supervisor = make_supervisor();
    const auto session = supervisor.begin(740, fonts.catalog, {}, binding);
    auto options = runtime_options_v3();
    options.frame_codec.version = kRuntimeShapedFrameVersion;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.shaped_text_binding = binding;
    const std::string html = "<body><div id='first'>A A A</div><section><div id='second'>A A</div></section></body>";
    const std::string css = "*{margin:0;padding:0;font-weight:400}body{background:#ffffff}"
        "div{display:block;width:18px;font-size:16px;line-height:18px}"
        "#first{background:#ffeeaa;color:#224488;text-align:center}"
        "#second{background:#aaffee;text-align:end}";
    ScriptTaskAcceptedAppFrame initial, changed, clicked;
    ScriptTaskFrameRendererOptions render_options;
    render_options.text_catalog = fonts.catalog;
    render_options.shaped_text_binding = binding;
    ScriptTaskFrameRenderer renderer(render_options);
    auto render_checked = [&](const ScriptTaskAcceptedAppFrame& accepted) {
        ScriptTaskFrameRenderStatus status;
        auto image = renderer.render(accepted.frame(), {255,255,255,255}, &status);
        check(status == ScriptTaskFrameRenderStatus::Accepted, "shaped worker frame renders with exact owner");
        return image;
    };
    auto take = [&](ScriptTaskAcceptedAppFrame& accepted) {
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
              ScriptTaskAppFrameTakeStatus::Accepted, "UI accepts real shaped DOM frame");
        check(accepted.shaped_text_binding() == binding && accepted.frame().shaped_text.size() > 0,
              "shaped frame retains exact session binding and final-line records");
        for (const auto& command : accepted.frame().display_list)
            check(!detail::is_prepared_command_type(worker_test_access::worker_command_type(command)) &&
                  !detail::has_prepared_text_packet(command),
                  "no scalar JFTP text is mixed into shaped layout");
    };
    ScriptTaskWorkerRuntime worker(session, options);
    const auto init_result = worker.initialize(supervisor, html, css);
    if (init_result != ScriptTaskWorkerRuntimeInitStatus::Accepted)
        std::cerr << "shaped worker init=" << int(init_result) << " fatal=" << int(worker.fatal_record().reason) << '\n';
    check(init_result == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "ordinary multi-paragraph DOM initializes selected shaped worker");
    check(worker.publish_frame(supervisor).accepted(), "real v11 worker publishes shaped DOM");
    take(initial);
    check(initial.frame().shaped_text.size() == 5, "multiple paragraphs produce five final lines");
    const auto initial_pixels = render_checked(initial);
    const auto first_packet = initial.frame().shaped_text[0].packet;
    // Independently feed the same DOM/CSS to Core, then compare final packets,
    // alignment and insertion points against the worker's transported records.
    auto dom = HtmlParser{}.parse(html);
    StyleResolver styles(CssParser{}.parse(css));
    auto render = RenderTreeBuilder(styles).build(*dom);
    LayoutEngine engine(styles);
    runtime_shaped::LayoutResult layout;
    std::uint64_t work = binding->work_limit();
    check(binding->layout_document(engine,*render,160,100,options.shaped_layout,work,layout) ==
              runtime_shaped::Status::ok, "independent Core document producer accepts same source");
    runtime_shaped::LayerCapture capture;
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    check(LayerTreeBuilder{}.build_contextual(layout,capture), "independent Core layer mapping");
    const auto* capture_layer = capture.layer_tree();
    const auto& capture_positions = capture.after_command_counts();
#else
    check(LayerTreeBuilder{}.build_shaped(layout,capture), "independent Core layer mapping");
    const auto* capture_layer = capture.layer_tree.get();
    const auto& capture_positions = capture.after_command_counts;
#endif
    std::size_t record = 0;
    for (std::size_t p = 0; p < layout.paragraphs().size(); ++p) {
        const auto& paragraph = layout.paragraphs()[p];
        for (const auto& line : paragraph.paragraph.lines()) {
            const auto& value = initial.frame().shaped_text[record++];
            const auto spare = std::int64_t(paragraph.content_width_q1000) - line.metrics.advance_x;
            const auto shift = paragraph.align == TextAlign::Center ?
                #if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
                spare / 2 : spare;
#else
                (spare >= 0 ? spare/2 : -((-spare+1)/2)) : spare;
#endif
            check(value.packet == line.packet && value.after_command_count == capture_positions[p] &&
                  value.baseline_y_q1000 == paragraph.origin_y_q1000+line.baseline_y_q1000 &&
                  value.baseline_x_q1000 == paragraph.origin_x_q1000+shift,
                  "worker preserves exact Core final packets, per-line alignment and paint order");
        }
    }
    // Reference pixels come directly from Core's captured backgrounds and
    // final-line painter, independently of the Runtime codec and renderer.
    FrameBuffer reference(160,100,{255,255,255,255});
    SoftwareRasterizer rasterizer;
    record = 0;
    work = binding->work_limit();
    for (std::size_t command = 0; command <= capture_layer->display_list.size(); ++command) {
        while (record < initial.frame().shaped_text.size() &&
               initial.frame().shaped_text[record].after_command_count == command) {
            const auto& value = initial.frame().shaped_text[record++];
            check(runtime_shaped::paint(binding->core_binding(),value.packet,reference,
                value.baseline_x_q1000,value.baseline_y_q1000,value.clip,value.color,{},work) ==
                runtime_shaped::Status::ok, "independent Core final-line paint");
        }
        if (command < capture_layer->display_list.size())
            rasterizer.rasterize(capture_layer->display_list[command],reference,{0,0,160,100});
    }
    bool saw_text_ink = false;
    for (std::size_t i=0;i<reference.pixels.size();++i) {
        const auto a=reference.pixels[i], b=initial_pixels.pixels[i];
        check(a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a,
              "worker transport pixels equal direct Core paint in insertion order");
        saw_text_ink = saw_text_ink || (b.r < 100 && b.g < 150 && b.b < 200);
    }
    check(saw_text_ink, "real worker frame paints visible glyph ink");
    check(!worker.eval("1+1").ok && !worker.fatal(), "v11 JS requires validated supervisor");
    check(worker.eval_with_supervisor(supervisor,
        "var first=document.getElementById('first');first.textContent='A A';"
        "first.style.width='40px';first.addEventListener('click',function(){first.textContent='A';});").ok &&
          !worker.fatal(), "JS text and width mutation rebuild shaped geometry");
    check(worker.publish_frame(supervisor).accepted(), "mutated shaped frame publishes");
    take(changed);
    check(changed.frame().shaped_text.size() == 3 && initial.frame().shaped_text.size() == 5 &&
          initial.frame().shaped_text[0].packet == first_packet,
          "new wrapping updates all block heights without mutating old accepted frame");
    render_checked(changed);
    for (auto kind : {ScriptTaskInputKind::PointerDown, ScriptTaskInputKind::PointerUp}) {
        const auto seq = kind == ScriptTaskInputKind::PointerDown ? 1 : 2;
        check(post_script_task_input(supervisor,session,seq,
            {kind,2,2,0,0,0,kind == ScriptTaskInputKind::PointerDown ? 1 : 0,0,0,{}},
            options.input_codec).accepted(), "shaped DOM input posts");
        const auto step = worker.process_one(supervisor);
        check(step.input_accepted && !step.fatal, "shaped DOM uses ordinary hit testing and input dispatch");
        if (kind == ScriptTaskInputKind::PointerUp)
            check(step.frame_published, "generic block click publishes shaped mutation");
    }
    take(clicked);
    check(clicked.frame().shaped_text[0].packet != changed.frame().shaped_text[0].packet,
          "click changes authoritative shaped packet");
    render_checked(clicked);
#if JELLYFRAME_RUNTIME_CONTEXTUAL_TEXT_API
    check(worker.eval_with_supervisor(supervisor,
        "first.style.width='140px';first.textContent='office AV \\u4e2d e\\u0301';").ok && !worker.fatal(),
        "contextual mixed Latin/CJK and combining-source mutation");
    check(worker.publish_frame(supervisor).accepted(), "mixed contextual source publishes");
    ScriptTaskAcceptedAppFrame contextual;
    take(contextual);
    work = binding->work_limit();
    std::string expected_packet;
    runtime_shaped::Metrics mixed_metrics;
    check(binding->prepare(16000, u8"office AV \u4e2d e\u0301", work, expected_packet, mixed_metrics) ==
              runtime_shaped::Status::ok && contextual.frame().shaped_text.front().packet == expected_packet,
          "DOM transport preserves complete contextual shaping output for mixed source");
    check(mixed_metrics.glyph_count < 15 && mixed_metrics.has_ink,
          "ligature and normalization output remains authoritative");
    render_checked(contextual);
#endif
    worker.stop();
    check(supervisor.begin_teardown(session).session == session &&
          supervisor.complete_teardown(session).session == session, "shaped session tears down");
    binding.reset(); assets.clear_source_buffers();
    const auto retained = render_checked(initial);
    check(retained.pixels.size() == initial_pixels.pixels.size(), "retained framebuffer shape");
    for (std::size_t i=0;i<retained.pixels.size();++i) {
        const auto a=retained.pixels[i], b=initial_pixels.pixels[i];
        check(a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a,
              "accepted shaped pixels survive mutation, worker/session and package buffer release");
    }
}

#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
void worker_contextual_inline_input_uses_presented_frame() {
    runtime_test_shaped_text::Assets assets("regular", 7410);
    const auto binding = runtime_test_shaped_text::create_binding(assets);
    runtime_test_fonts::Fonts fonts;
    auto supervisor = make_frame_backpressure_supervisor();
    const auto session = supervisor.begin(741, fonts.catalog, {}, binding);
    auto options = runtime_options_v3();
    options.frame_codec.version = kRuntimeShapedFrameVersion;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.shaped_text_binding = binding;
    options.contextual_inline_input = true;
    const std::string html = "<body><div id='run'>of<span id='ink'>fice</span></div></body>";
    const std::string css = "*{margin:0;padding:0;font-weight:400}div{width:140px;font-size:16px}"
        "#run:active{background:#ffeeaa}";
    ScriptTaskWorkerRuntime worker(session, options);
    check(worker.initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "shared inline initializes with explicit input opt-in");
    check(worker.eval_with_supervisor(supervisor,
        "var clicks=0;var ink=document.getElementById('ink');"
        "ink.addEventListener('click',function(){clicks++;});").ok, "register span callback");
    check(worker.publish_frame(supervisor).accepted(), "publish shared inline frame");
    ScriptTaskAcceptedAppFrame initial, pressed, released, replacement;
    const auto take = [&](ScriptTaskAcceptedAppFrame& frame) {
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
            ScriptTaskAppFrameTakeStatus::Accepted, "take shared inline frame");
    };
    take(initial);

    auto dom = HtmlParser{}.parse(html);
    StyleResolver styles(CssParser{}.parse(css));
    auto render = RenderTreeBuilder(styles).build(*dom);
    LayoutEngine engine(styles);
    auto work = binding->work_limit();
    runtime_shaped::LayoutResult layout;
    runtime_shaped::LayerCapture capture;
    check(binding->layout_inline_document(engine, *render, 160, 100, options.shaped_layout, work, layout) ==
          runtime_shaped::Status::ok &&
          LayerTreeBuilder{}.build_contextual(layout, runtime_shaped::TextHitCaptureOptions{}, capture),
          "independent Core capture locates the shared glyph");
    check(initial.frame().shaped_text.size() == 1 &&
          initial.frame().shaped_text.front().packet == layout.paragraphs().front().paragraph.lines().front().packet,
          "worker transports one jointly shaped line");
    const auto& hits = capture.text_hit_fragments();
    const auto shared = std::adjacent_find(hits.begin(), hits.end(), [](const auto& left, const auto& right) {
        return left.owner_index == 0 && right.owner_index == 1 &&
               left.cluster_index == right.cluster_index && left.glyph_index == right.glyph_index;
    });
    check(shared != hits.end(), "fixture includes a ligature spanning both owners");
    const Rect ink = shared->ink;
    std::uint32_t input_sequence = 0;
    const auto send = [&](ScriptTaskInputKind kind, std::uint32_t frame) {
        check(post_script_task_input(supervisor, session, ++input_sequence,
              {kind, ink.x, ink.y, 0, 0, 0, kind == ScriptTaskInputKind::PointerDown ? 1 : 0, 0, 0, {}},
              options.input_codec, frame).accepted(), "post tagged contextual input");
        return worker.process_one(supervisor);
    };
    check(!send(ScriptTaskInputKind::PointerDown, initial.sequence()).input_accepted,
          "publication alone grants no input authority");
    worker.set_ui_accepted_frame_sequence(initial.sequence());
    check(!send(ScriptTaskInputKind::PointerDown, 0).input_accepted, "untagged input is refused");
    check(worker.publish_frame(supervisor).accepted(), "fill the frame mailbox");
    const auto rejected_publish = send(ScriptTaskInputKind::PointerDown, initial.sequence());
    check(rejected_publish.input_accepted && !rejected_publish.frame_published && !rejected_publish.fatal,
          "press handling survives a full frame mailbox");
    check(send(ScriptTaskInputKind::PointerUp, initial.sequence()).input_accepted,
          "failed publication leaves the displayed geometry usable for release");
    take(pressed);
    check(worker.process_one(supervisor).frame_published, "worker retries the current frame after backpressure");
    take(released);
    check(worker.eval_with_supervisor(supervisor, "clicks").value == "1",
          "one click arrives despite publication failure");
    worker.set_ui_accepted_frame_sequence(released.sequence());
    const auto displayed_after_retry = released.sequence();
    const auto down = send(ScriptTaskInputKind::PointerDown, displayed_after_retry);
    check(down.input_accepted && down.frame_published && !down.dom_mutated,
          "active styling publishes a frame while preserving the source DOM version");
    take(pressed);
    check(post_script_task_input(supervisor, session, input_sequence,
          {ScriptTaskInputKind::PointerDown, ink.x, ink.y, 0, 0, 0, 1, 0, 0, {}},
          options.input_codec, displayed_after_retry).accepted() &&
          !worker.process_one(supervisor).input_accepted,
          "replayed pointer packet does not restart the active gesture");
    check(send(ScriptTaskInputKind::PointerUp, displayed_after_retry).input_accepted,
          "release can still use the displayed frame while the press frame is pending");
    take(released);
    check(worker.eval_with_supervisor(supervisor, "clicks").value == "2",
          "release on the ligature dispatched one click to the span");
    worker.set_ui_accepted_frame_sequence(released.sequence());
    check(send(ScriptTaskInputKind::PointerDown, released.sequence()).frame_published,
          "a second press publishes active styling");
    take(pressed);
    worker.set_ui_accepted_frame_sequence(pressed.sequence());
    check(send(ScriptTaskInputKind::PointerUp, pressed.sequence()).frame_published,
          "release also dispatches after the press frame is presented");
    take(released);
    check(worker.eval_with_supervisor(supervisor, "clicks").value == "3",
          "the active span survives the acknowledged frame switch");
    worker.set_ui_accepted_frame_sequence(released.sequence());
    const auto acquired_release = released.sequence();
    check(send(ScriptTaskInputKind::PointerDown, acquired_release).frame_published,
          "press queues its next frame");
    take(pressed);
    worker.set_ui_accepted_frame_sequence(pressed.sequence());
    check(send(ScriptTaskInputKind::PointerUp, acquired_release).frame_published,
          "a queued release retains the already admitted press capture");
    take(released);
    check(worker.eval_with_supervisor(supervisor, "clicks").value == "4",
          "a presentation racing with the queued release does not lose the click");
    worker.set_ui_accepted_frame_sequence(released.sequence());
    check(worker.eval_with_supervisor(supervisor, "ink.textContent='A';").ok,
          "source mutation builds a replacement");
    check(!send(ScriptTaskInputKind::PointerDown, released.sequence()).input_accepted,
          "a DOM edit invalidates input against displayed old source geometry");
    take(replacement);
    worker.set_ui_accepted_frame_sequence(replacement.sequence());
    check(!send(ScriptTaskInputKind::PointerDown, released.sequence()).input_accepted,
          "an earlier acknowledged frame no longer admits input");
    worker.stop();
    check(initial.shaped_text_binding() == binding && initial.frame().shaped_text.size() == 1,
          "accepted frame retains its authenticated shaped data after worker stop");
}
#endif

void worker_shaped_dom_refuses_incomplete_paths() {
    runtime_test_shaped_text::Assets assets("regular", 7402);
    const auto binding = runtime_test_shaped_text::create_binding(assets);
    runtime_test_shaped_text::Assets other_assets("medium", 7402);
    const auto other = runtime_test_shaped_text::create_binding(other_assets);
    runtime_test_fonts::Fonts fonts;
    auto make_options = [&] {
        auto options = runtime_options_v3(); options.frame_codec.version=kRuntimeShapedFrameVersion;
        options.frame_codec.max_groups=32; options.text_catalog=fonts.catalog;
        options.shaped_text_binding=binding; return options;
    };
    for (int mode=0;mode<7;++mode) {
        auto supervisor=make_supervisor();
        const auto session=supervisor.begin(750+mode,fonts.catalog,{},binding);
        auto options=make_options();
        std::string html="<body><div>A A</div></body>";
        std::string css="*{margin:0;padding:0;font-weight:400}div{display:block;font-size:16px}";
        if(mode==0) options.shaped_text_binding.reset();
        if(mode==1) options.shaped_text_binding=other;
        if(mode==2) css+="div{letter-spacing:1px}";
        if(mode==3) html="<body><div>A<span>A</span>A</div></body>";
        if(mode==4) options.shaped_layout.max_source_bytes=1;
        if(mode==5) options.shaped_layout.paragraph.shaping.work=1;
        ScriptTaskWorkerRuntime worker(session,options);
        const auto result=mode==6 ? worker.initialize(html,css) : worker.initialize(supervisor,html,css);
        check(result != ScriptTaskWorkerRuntimeInitStatus::Accepted && !worker.initialized(),
              "v11 refuses missing/wrong owner, unsupported style/topology, budget and unvalidated init");
        check(!worker.publish_frame(supervisor).accepted(), "refused shaping never falls back to scalar frame");
    }
    auto supervisor=make_supervisor();
    const auto session=supervisor.begin(759,fonts.catalog,{},binding);
    auto options=make_options();
    ScriptTaskWorkerRuntime worker(session,options);
    check(worker.initialize(supervisor,"<body><div id='x'>A</div></body>",
        "*{margin:0;padding:0;font-weight:400}div{display:block;font-size:16px}") ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted, "supported shaped document before mutation");
    worker.eval_with_supervisor(supervisor,"document.getElementById('x').setAttribute('style','letter-spacing:1px');");
    check(worker.fatal() && !worker.publish_frame(supervisor).accepted(),
          "unsupported mutation fails closed instead of publishing old/scalar geometry");
}
#endif

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
void shaped_binding_owner_survives_worker_and_session_teardown() {
    using runtime_test_shaped_text::Assets;
    Assets regular("regular", 7301);
    Assets medium("medium", 7301);
    auto regular_binding = runtime_test_shaped_text::create_binding(regular);
    auto medium_binding = runtime_test_shaped_text::create_binding(medium);
    check(regular_binding->core_binding().resource_id() == medium_binding->core_binding().resource_id() &&
          regular_binding->core_binding().content_identity() != medium_binding->core_binding().content_identity(),
          "same resource ID with different verified content creates distinct Core bindings");

    auto mismatched_manifest = regular.manifest;
    mismatched_manifest.catalog_identity[0] ^= 0x80;
    RuntimeShapedTextBinding::Handle preserved = medium_binding;
    check(RuntimeShapedTextBinding::create(mismatched_manifest, preserved) ==
              RuntimeShapedTextStatus::CoreRejected && preserved == medium_binding,
          "digest mismatch refuses transactionally without replacing an existing binding");

    regular.clear_source_buffers();
    medium.clear_source_buffers();
    std::uint64_t work = 1024U * 1024U;
    std::string packet;
    runtime_shaped::Metrics metrics;
    check(regular_binding->prepare(16000, "A", work, packet, metrics) == runtime_shaped::Status::ok &&
          metrics.glyph_count == 1 && regular_binding->validate_packet(packet, work) == runtime_shaped::Status::ok,
          "Core binding retains validated asset bytes after package buffers are released");

    auto supervisor = make_supervisor();
    runtime_test_fonts::Fonts fonts;
    const auto session = supervisor.begin(739, fonts.catalog, {}, regular_binding);
    check(session.valid() && supervisor.shaped_text_binding(session) == regular_binding,
          "supervisor publishes exact immutable shaped binding");

    auto mismatch_options = runtime_options_v3();
    mismatch_options.frame_codec.version = 9;
    mismatch_options.text_catalog = fonts.catalog;
    mismatch_options.shaped_text_binding = medium_binding;
    ScriptTaskWorkerRuntime mismatch(session, mismatch_options);
    check(mismatch.initialize(supervisor, "<body></body>", "") ==
              ScriptTaskWorkerRuntimeInitStatus::ResourceMismatch && !mismatch.initialized(),
          "worker rejects a different shaped binding before initialization");

    auto options = runtime_options_v3();
    options.frame_codec.version = 9;
    options.frame_codec.max_groups = 32;
    options.text_catalog = fonts.catalog;
    options.shaped_text_binding = regular_binding;
    ScriptTaskAcceptedAppFrame accepted;
    {
        ScriptTaskWorkerRuntime worker(session, options);
        check(worker.initialize(supervisor, "<body><div>A</div></body>",
              "body{margin:0} div{display:block;font-family:sans-serif;font-size:16px}") ==
              ScriptTaskWorkerRuntimeInitStatus::Accepted,
              "worker accepts the exact session shaped binding");
        check(worker.publish_frame(supervisor).accepted(), "bound worker publishes a frame");
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
                  ScriptTaskAppFrameTakeStatus::Accepted &&
              accepted.shaped_text_binding() == regular_binding,
              "accepted frame captures the session shaped binding");
        worker.stop();
    }

    check(supervisor.begin_teardown(session).session == session,
          "session begins teardown after worker stops");
    check(!supervisor.shaped_text_binding(session),
          "supervisor releases its shaped binding during teardown");
    check(supervisor.complete_teardown(session).session == session,
          "session completes teardown while UI frame retains its binding");
    regular_binding.reset();
    medium_binding.reset();
    check(accepted.valid() && accepted.shaped_text_binding() &&
          accepted.shaped_text_binding()->core_binding().resource_id() == 7301,
          "accepted frame pins the binding after worker and session teardown");
    work = 1024U * 1024U;
    packet.clear();
    check(accepted.shaped_text_binding()->prepare(16000, "A", work, packet, metrics) ==
              runtime_shaped::Status::ok,
          "retained accepted-frame binding remains usable after teardown");
}
#endif
#endif

#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
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

#if defined(JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION) && JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION >= 1
void worker_v10_recovers_after_layer_budget_refusal() {
    ScriptTaskSupervisor supervisor = make_frame_backpressure_supervisor();
    const runtime_test_fonts::Fonts fonts;
    const ScriptAppSession session = supervisor.begin(26, fonts.catalog);
    auto options = runtime_options_v3();
    options.frame_codec.version = 10;
    options.frame_codec.max_groups = 2;
    options.frame_codec.max_group_depth = 4;
    options.text_catalog = fonts.catalog;
    options.budgets.max_layers = 2;
    constexpr auto html = "<body><div id='target' class='panel'></div></body>";
    constexpr auto css = "body { margin: 0; } .panel { display: block; width: 100px; height: 30px; "
                         "opacity: 0.9; background-color: #0000ff; }";
    constexpr auto storm = "<body><div class='panel'></div><div class='panel'></div></body>";
    {
        ScriptTaskWorkerRuntime first_frame(session, options);
        check(first_frame.initialize(supervisor, storm, css) ==
                  ScriptTaskWorkerRuntimeInitStatus::BudgetRejected &&
              first_frame.fatal_record().reason == ScriptTaskWorkerRuntimeFatalReason::BudgetExceeded,
              "v10 first layout over the layer limit is terminal");
        check(!first_frame.publish_frame(supervisor).accepted(), "over-budget first layout publishes no frame");
        ScriptTaskPacket fatal;
        check(supervisor.take_fatal(fatal), "first-frame refusal publishes a fatal record");
    }
    ScriptTaskWorkerRuntime runtime(session, options);
    check(runtime.initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "v10 layer limit fixture initializes");
    const auto initial = runtime.publish_frame(supervisor);
    check(initial.accepted(), "v10 publishes the frame on the exact layer boundary");
    ScriptTaskAcceptedAppFrame original;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, original) ==
              ScriptTaskAppFrameTakeStatus::Accepted && original.sequence() == initial.lease.frame_sequence,
          "the UI owns the original frame");
    runtime.set_ui_accepted_frame_sequence(original.sequence());
    auto post = [&](std::uint32_t sequence, ScriptTaskInputKind kind, std::uint32_t frame) {
        const int buttons = kind == ScriptTaskInputKind::PointerDown ? 1 : 0;
        check(post_script_task_input(supervisor, session, sequence,
                  {kind, 1, 1, 0, 0, 0, buttons, 0, 0, {}}, options.input_codec, frame).accepted(),
              "test gesture reaches the Worker mailbox");
    };
    check(runtime.eval_with_supervisor(supervisor,
              "var releases = 0; var repaired = 0; "
              "document.getElementById('target').addEventListener('pointerdown', function () {"
              "document.body.textContent = '';"
              "for (var i = 0; i < 2; i++) {"
              "  var panel = document.createElement('div');"
              "  panel.setAttribute('class', 'panel');"
              "  document.body.appendChild(panel);"
              "}"
              "setTimeout(function () { repaired++; }, 5);"
              "setTimeout(function () {"
              "document.body.textContent = '';"
              "var panel = document.createElement('div');"
              "panel.setAttribute('class', 'panel');"
              "panel.addEventListener('pointerup', function () { releases++; });"
              "document.body.appendChild(panel);"
              "}, 10); });").ok, "recovery fixture listeners install");
    post(1, ScriptTaskInputKind::PointerDown, original.sequence());
    const auto refused = runtime.process_one(supervisor);
    check(refused.input_accepted && refused.dom_mutated && !refused.fatal && !refused.frame_published,
          "an input callback may destroy its target and then exceed the budget");
    ScriptTaskPacket packet;
    check(!supervisor.take_fatal(packet) && !supervisor.take_frame_packet(session, packet),
          "recovery emits neither a partial frame nor a terminal record");
    const auto attempts = runtime.telemetry().frame_publish_attempts;
    post(2, ScriptTaskInputKind::PointerUp, original.sequence());
    check(!runtime.process_one(supervisor).input_accepted, "suspended Worker drops input");
    check(!runtime.process_one(supervisor).frame_published &&
              runtime.telemetry().frame_publish_attempts == attempts,
          "idle Worker does not repeat a refused build");
    check(!runtime.pump_callbacks(5, supervisor).frame_published &&
              runtime.eval_with_supervisor(supervisor, "repaired").value == "1",
          "the script realm and timer processing remain live");
    // Consume a supervisor sequence and occupy the only publication slot.
    // Recovery must retry and wait for the accepted sequence of its own lease.
    const auto filler = supervisor.publish_frame(session, {0});
    check(filler.accepted(), "a concurrent diagnostic frame fills the mailbox");
    const auto repair = runtime.pump_callbacks(10, supervisor);
    check(repair.dom_mutated && !repair.frame_published && !repair.fatal,
          "a repaired document survives publication backpressure");
    check(supervisor.take_frame_packet(session, packet) &&
              supervisor.release_frame(session, packet.frame_lease_id) == ScriptTaskFrameLeaseStatus::Accepted,
          "the blocked mailbox slot can be retired");
    const auto retry = runtime.process_one(supervisor);
    check(retry.frame_published && retry.frame.lease.frame_sequence != original.sequence() + 1,
          "the first recovery publication uses its actual supervisor sequence");
    post(3, ScriptTaskInputKind::PointerDown, retry.frame.lease.frame_sequence);
    check(!runtime.process_one(supervisor).input_accepted,
          "publication alone cannot resume input before the UI accepts the frame");
    ScriptTaskAcceptedAppFrame replacement;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, replacement) ==
              ScriptTaskAppFrameTakeStatus::Accepted, "UI takes the complete recovery frame");
    runtime.set_ui_accepted_frame_sequence(replacement.sequence());
    post(4, ScriptTaskInputKind::PointerDown, original.sequence());
    check(!runtime.process_one(supervisor).input_accepted, "queued input on the old frame is discarded after recovery");
    post(5, ScriptTaskInputKind::PointerUp, replacement.sequence());
    check(!runtime.process_one(supervisor).input_accepted, "release on the restored frame needs a fresh press");
    post(6, ScriptTaskInputKind::PointerDown, replacement.sequence());
    check(runtime.process_one(supervisor).input_accepted, "input resumes on the accepted recovery frame");
    post(7, ScriptTaskInputKind::PointerUp, replacement.sequence());
    check(runtime.process_one(supervisor).input_accepted &&
              runtime.eval_with_supervisor(supervisor, "releases").value == "1",
          "a fresh gesture targets the rebuilt tree");
    ScriptTaskFrameRendererOptions render_options;
    render_options.text_catalog = original.text_catalog();
    ScriptTaskFrameRenderer renderer(render_options);
    ScriptTaskFrameRenderStatus status;
    renderer.render(original.frame(), {255, 255, 255, 255}, &status);
    check(status == ScriptTaskFrameRenderStatus::Accepted,
          "an old UI-held frame remains replayable after Worker tree destruction");
}
#endif

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
        ScriptTaskFrameRenderer renderer(render_options);
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

void worker_callback_admission_refusal_becomes_budget_fatal_without_publishing_a_frame() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(91);
    auto options = runtime_options();
#if JELLYFRAME_SCRIPT_R3_EVENTS
    options.script.max_active_event_dispatches = 0;
#else
    // The surrounding eval is allowed; its nested native callback exceeds the
    // shared operation allowance. Legacy has no shared Event-record capacity.
    options.script.max_active_operations = 1;
#endif
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
          "callback admission refusal becomes worker budget fatal even if surrounding eval returns normally");
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

void worker_initialization_admission_preserves_session() {
    auto supervisor = make_supervisor();
    const auto session = supervisor.begin(97);
    struct Context {
        ScriptTaskWorkerRuntime* worker = nullptr;
        ScriptTaskSupervisor* supervisor = nullptr;
        std::size_t calls = 0;
    } context;
    auto options = runtime_options();
    options.mutation_observer_user = &context;
    options.mutation_observer = [](void* user, const Node&, DomDirtyFlags, std::uint64_t) {
        auto& context = *static_cast<Context*>(user);
        ++context.calls;
        check(context.worker->initialize("<body>replacement</body>", "") ==
            ScriptTaskWorkerRuntimeInitStatus::Busy, "active initialize refuses before replacing DOM");
        check(context.worker->initialize(*context.supervisor, "<body>replacement</body>", "") ==
            ScriptTaskWorkerRuntimeInitStatus::Busy, "active supervisor initialize refuses before replacing DOM");
    };
    ScriptTaskWorkerRuntime worker(session, options);
    context.worker = &worker;
    context.supervisor = &supervisor;
    check(worker.initialize(supervisor, "<body><button id='button'>original</button></body>", "") ==
        ScriptTaskWorkerRuntimeInitStatus::Accepted, "initial supervisor continuation remains admitted");
    check(worker.initialize("<body>replacement</body>", "") ==
        ScriptTaskWorkerRuntimeInitStatus::AlreadyInitialized, "repeated initialize refuses deterministically");
    check(worker.initialize(supervisor, "<body>replacement</body>", "") ==
        ScriptTaskWorkerRuntimeInitStatus::AlreadyInitialized, "repeated supervisor initialize refuses");
    auto result = worker.eval_with_supervisor(supervisor,
        "var preserved=41; document.getElementById('button').textContent='preserved'; preserved+1;");
    check(result.ok && result.value == "42" && context.calls == 1 && !worker.fatal(),
        "active initialization refusal preserves original VM and DOM without fatal");
    check(worker.eval("document.getElementById('button').textContent").value == "preserved",
        "refused initialization preserves the original DOM text mutation");
    check(worker.eval("preserved+1").value == "42" && worker.publish_frame(supervisor).accepted(),
        "refused initialization preserves original realm and frame publication");
    ScriptTaskAppFrame frame;
    check(take_script_task_app_frame(supervisor, session, options.frame_codec, frame) ==
        ScriptTaskAppFrameTakeStatus::Accepted, "preserved session frame reaches UI");
    worker.stop();
    check(worker.initialize("<body></body>", "") == ScriptTaskWorkerRuntimeInitStatus::Stopped,
        "stop takes precedence over initialized refusal");
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

#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
class ScalarWorkerImages final : public RuntimeImageProviderSource {
public:
    std::uint64_t generation() const noexcept override { return 1; }
    bool contains(std::uint32_t handle) const noexcept override { return handle == 73; }
    bool resolve(std::string_view url, RuntimeResolvedImage& output) const override {
        if (url != "/assets/content.bmp") return false;
        output = {73, 20, 12};
        return true;
    }
    bool intrinsic_size(std::uint32_t handle, int& width, int& height) const override {
        if (!contains(handle)) return false;
        width = 20;
        height = 12;
        return true;
    }
    bool paint(FrameBuffer& target, const RuntimeImagePaintRequest& request) const override {
        if (!contains(request.image_handle)) return false;
        const auto& clip = request.clip;
        for (int y = std::max(0, clip.y); y < std::min(target.height, clip.y + clip.height); ++y)
            for (int x = std::max(0, clip.x); x < std::min(target.width, clip.x + clip.width); ++x)
                target.pixel(x, y) = {227, 13, 49, 255};
        return true;
    }
};

void worker_scalar_v13_owns_nbsp_and_natural_images() {
    auto supervisor = make_supervisor();
    runtime_test_fonts::Fonts fonts;
    RuntimeImageProvider::Handle images;
    check(RuntimeImageProvider::create(std::make_shared<ScalarWorkerImages>(), images) == RuntimeImageProviderStatus::Ready,
          "scalar worker image owner is ready");
    const auto session = supervisor.begin(313, fonts.catalog, images);
    auto options = runtime_options_v3();
    options.frame_codec.version = kRuntimeScalarFrameVersion;
    options.frame_codec.max_groups = 24;
    options.text_catalog = fonts.catalog;
    options.image_provider = images;
    const auto html = "<body><div id='card'><p id='label'>A&nbsp;B</p><img src='/assets/content.bmp'></div></body>";
    const auto css = "body,p{margin:0}#card{width:64px;opacity:.75;border-radius:4px;overflow:hidden}"
                     "p{font-size:16px;line-height:20px;word-spacing:3px}img{display:block}";
    {
        RuntimeImageProvider::Handle foreign;
        check(RuntimeImageProvider::create(std::make_shared<ScalarWorkerImages>(), foreign) == RuntimeImageProviderStatus::Ready,
              "foreign image namespace is ready");
        auto mismatched = options;
        mismatched.image_provider = std::move(foreign);
        ScriptTaskWorkerRuntime rejected(session, mismatched);
        check(rejected.initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::ResourceMismatch,
              "scalar worker rejects foreign image owner before VM initialization");
    }
    auto worker = std::make_unique<ScriptTaskWorkerRuntime>(session, options);
    check(worker->initialize(supervisor, html, css) == ScriptTaskWorkerRuntimeInitStatus::Accepted,
          "ordinary installed scalar DOM initializes");
    const auto take = [&](ScriptTaskAcceptedAppFrame& accepted) {
        check(worker->publish_frame(supervisor).accepted(), "ordinary installed scalar frame publishes");
        check(take_script_task_app_frame(supervisor, session, options.frame_codec, accepted) ==
                  ScriptTaskAppFrameTakeStatus::Accepted, "ordinary scalar frame is accepted with resources");
    };
    ScriptTaskAcceptedAppFrame original;
    take(original);
    check(original.frame().groups.size() > 1 && original.frame().image_provider_id == images->id(),
          "scalar frame retains non-root group and image namespace");
    bool natural_image = false, spaced_nbsp = false;
    for (const auto& command : original.frame().display_list) {
        if (command.type == DisplayCommandType::Image)
            natural_image |= command.image_handle == 73 && command.rect.width == 20 && command.rect.height == 12;
        if (command.type != DisplayCommandType::GlyphRun) continue;
        PreparedTextView packet;
        check(decode_prepared_text(command.text, packet), "scalar worker JFTP decodes");
        spaced_nbsp |= packet.packet_version() == 4 && packet.word_spacing_for(0xa0) == 3000;
    }
    check(natural_image && spaced_nbsp, "ordinary scalar frame transports natural image and JFTP v4 NBSP");

    check(worker->eval_with_supervisor(supervisor, "document.getElementById('label').textContent = 'B A';").ok,
          "scalar worker rebuilds after document mutation");
    ScriptTaskAcceptedAppFrame changed;
    take(changed);
    check(std::none_of(changed.frame().display_list.begin(), changed.frame().display_list.end(), [](const auto& command) {
        return command.type == DisplayCommandType::GlyphRun && command.text.size() > 4 && command.text[4] == 4;
    }), "scalar mutation returns compatible JFTP v3 packets");

    worker->stop(); worker.reset();
    supervisor.begin_teardown(session);
    supervisor.complete_teardown(session);
    images.reset(); options.image_provider.reset();
    fonts.catalog.reset(); options.text_catalog.reset();
    ScriptTaskFrameRenderer renderer;
    FrameBuffer pixels(options.viewport.width, options.viewport.height, {});
    check(renderer.render_into(original, pixels, {255, 255, 255, 255}, nullptr, 0, nullptr),
          "accepted scalar frame replays after worker and supervisor release");
    check(std::any_of(pixels.pixels.begin(), pixels.pixels.end(), [](Color c) {
        return c.r > c.g + 60 && c.r > c.b + 60;
    }), "retained ordinary image paints visible pixels");
}
#endif

} // namespace

int script_task_worker_runtime_tests_main() {
    try {
#if !JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
        {
            auto supervisor = make_supervisor();
            auto options = runtime_options();
            options.contextual_inline_input = true;
            ScriptTaskWorkerRuntime worker(supervisor.begin(742), options);
            check(worker.initialize(supervisor, "<body></body>", "") ==
                  ScriptTaskWorkerRuntimeInitStatus::InvalidConfiguration,
                  "explicit contextual inline input fails closed on older or disabled Core builds");
        }
#endif
        worker_owner_teardown_during_callbacks();
        worker_owner_admission_refusal();
        worker_initialization_admission_preserves_session();
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
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API && !JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        if (script_task_app_frame_codec_supports_version(kRuntimeScalarFrameVersion))
            worker_scalar_v13_owns_nbsp_and_natural_images();
#endif
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        for (auto version : {std::uint8_t{9}, std::uint8_t{10}}) {
            if (!script_task_app_frame_codec_supports_version(version)) continue;
            worker_canonical_rejects_invalid_config_and_catalog_mismatch_before_vm(version);
            worker_canonical_uses_catalog_for_layout_and_publishes_group_frame(version);
            worker_canonical_resolves_content_and_background_images(version);
#if defined(JELLYFRAME_TEST_CORE_SELECT_POPUP_TEXT) && JELLYFRAME_TEST_CORE_SELECT_POPUP_TEXT
            worker_core_text_select_popup_prepares_and_selects_visible_options(version);
#endif
#if JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION >= 2
            worker_shared_inline_paragraph(version);
#endif
#if defined(JELLYFRAME_TEST_GENERATED_INLINE_TEXT) && JELLYFRAME_TEST_GENERATED_INLINE_TEXT
            worker_generated_inline_text(version);
#endif
        }
#endif // scalar canonical worker cases
#if JELLYFRAME_RUNTIME_SHAPED_TEXT_API && defined(JELLYFRAME_SHAPED_TEST_FIXTURES_ROOT)
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
        if (script_task_app_frame_codec_supports_version(9))
            shaped_binding_owner_survives_worker_and_session_teardown();
#endif
#if JELLYFRAME_RUNTIME_SHAPED_LAYOUT_API
        if (script_task_app_frame_codec_supports_version(kRuntimeShapedFrameVersion)) {
            worker_shaped_dom_layout_and_input();
#if JELLYFRAME_RUNTIME_CONTEXTUAL_INLINE_INPUT_API
            worker_contextual_inline_input_uses_presented_frame();
#endif
            worker_shaped_dom_refuses_incomplete_paths();
            std::cout << "shaped DOM worker v" << unsigned(kRuntimeShapedFrameVersion)
                      << ": Core packet/pixel oracle, JS mutation, pointer input and retained lifetime passed\n";
        }
#endif
#endif
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_TEXT_API
#if defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) && JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1
        worker_natural_image_cases();
#if defined(JELLYFRAME_TEST_NATURAL_INLINE_IMAGES) && JELLYFRAME_TEST_NATURAL_INLINE_IMAGES
        worker_natural_inline_image_cases();
#endif
#endif
        if (script_task_app_frame_codec_supports_version(10))
            worker_v10_mutation_publishes_changed_packed_axes<DisplayCommand>();
#if defined(JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION) && JELLYFRAME_RENDER_CORE_STRICT_LAYER_BUILD_API_VERSION >= 1
        if (script_task_app_frame_codec_supports_version(10))
            worker_v10_recovers_after_layer_budget_refusal();
#endif
#endif
        worker_fatal_publishes_once_as_a_supervisor_value();
        worker_c_safe_fatal_boundary_publishes_value_record();
        worker_input_node_destruction_clears_interaction_state();
        worker_callback_admission_refusal_becomes_budget_fatal_without_publishing_a_frame();
    } catch (const std::exception& error) {
        std::cerr << "script task worker runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
