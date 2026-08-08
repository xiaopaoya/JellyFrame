#include "script/script_task_worker_runtime.h"

#include "app_runtime/script_task_input_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_fatal_codec.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

ScriptTaskSupervisor make_supervisor() {
    ScriptTaskSupervisorOptions options;
    options.input_mailbox = {16, 4096};
    options.worker_mailbox = {16, 4096};
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
    options.input_mailbox = {128, 4096};
    options.worker_mailbox = {4, 32 * 1024};
    options.service_request_mailbox = {128, 1024};
    options.frame_leases = {4, 32 * 1024, 64 * 1024};
    options.service_payload_leases = {128, 4096, 64 * 1024};
    options.max_service_tombstones = 128;
    options.max_native_release_intents = 16;
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
    check(down.frame_published, "pointer down publishes interaction frame");
    check(take_script_task_app_frame(supervisor, session, runtime_options().frame_codec, frame) ==
              ScriptTaskAppFrameTakeStatus::Accepted,
          "UI consumes pointer-down frame before pointer-up");
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

    std::uint32_t payload_lease_id = 0;
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
    ScriptTaskWorkerRuntime runtime(session, runtime_options());
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

} // namespace

int script_task_worker_runtime_tests_main() {
    try {
        worker_input_publishes_value_frame();
        worker_eval_failure_becomes_value_fatal();
        worker_timer_publishes_value_frame();
        worker_service_completion_reaches_js_as_copied_value();
        worker_service_cancel_posts_only_value_identity();
        worker_service_completion_replay_handles_one_hundred_callbacks();
        worker_timer_exception_becomes_fatal_value();
        worker_animation_watchdog_becomes_fatal_value();
        worker_input_and_service_callback_exceptions_stop_the_worker();
        worker_stop_is_explicit_and_idempotent();
        worker_fatal_publishes_once_as_a_supervisor_value();
        worker_c_safe_fatal_boundary_publishes_value_record();
        worker_input_node_destruction_clears_interaction_state();
    } catch (const std::exception& error) {
        std::cerr << "script task worker runtime test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
