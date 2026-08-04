#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_service_bridge.h"
#include "app_runtime/script_task_worker_inbox.h"

#include "render_core/layer_tree.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

class CompletionSink final : public ScriptTaskServiceCompletionSink {
public:
    bool handle_script_task_service_completion(const ScriptTaskServiceCompletion& completion) override {
        received = completion;
        ++calls;
        return true;
    }

    ScriptTaskServiceCompletion received;
    std::size_t calls = 0;
};

ScriptTaskSupervisor make_supervisor() {
    return ScriptTaskSupervisor({{4, 32}, {2, 0}, {2, 512, 1024}, 4, 0, {2, 20}});
}

ScriptTaskAppFrameCodecOptions frame_limits() {
    return {2, 16, 0, 512};
}

ScriptTaskAppFrame completed_frame() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 172, 320};
    DisplayCommand command;
    command.type = DisplayCommandType::FillRect;
    command.rect = {0, 0, 172, 320};
    command.color = {12, 34, 56, 255};
    frame.display_list.push_back(command);
    return frame;
}

void service_completion_to_worker_frame_to_ui_stays_value_only() {
    AppRuntimeHost host({2, 2, 4, 1024, 0});
    const AppInstance app = host.launch("org.example.script.value-flow", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {2});

    const ScriptTaskServiceRequest request{HostServiceJobKind::SensorSample, 11, 12, 0, 1, 250};
    assert(post_script_task_service_request(supervisor, session, 1, request, {20}).accepted());
    assert(bridge.pump_service_requests().accepted == 1);
    HostServiceRequest host_request;
    assert(host.pop_worker_request(host_request));
    assert(host_request.app_instance_id == app.id);
    assert(host.push_completion({host_request.job_id,
                                 host_request.kind,
                                 HostServiceStatus::Completed,
                                 app.id,
                                 0,
                                 0,
                                 4,
                                 host_request.client_token}));
    AppFrameScratch scratch;
    scratch.reserve_from_options({2, 2, 4, 1024, 0});
    assert(bridge.pump(scratch).delivered == 1);

    LayerNode private_root;
    private_root.type = LayerType::Root;
    InputController private_input(private_root);
    CompletionSink completion_sink;
    assert(take_and_dispatch_script_task_worker_packet(supervisor, private_input, completion_sink, {0, 32}).handled);
    assert(completion_sink.calls == 1);
    assert(completion_sink.received.request_id == request.request_id);

    ScriptTaskAppFramePublisher publisher(frame_limits());
    assert(publisher.publish(supervisor, session, completed_frame()).accepted());
    ScriptTaskAppFrame ui_frame;
    assert(take_script_task_app_frame(supervisor, session, frame_limits(), ui_frame) ==
           ScriptTaskAppFrameTakeStatus::Accepted);
    assert(ui_frame.viewport.width == 172);
    assert(ui_frame.display_list.size() == 1);
    assert(ui_frame.display_list.front().color.g == 34);
}

} // namespace

int script_task_value_flow_tests_main() {
    service_completion_to_worker_frame_to_ui_stays_value_only();
    std::cout << "script task value flow tests passed\n";
    return 0;
}
