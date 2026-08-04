#include "app_runtime/script_task_worker_inbox.h"

#include "render_core/layer_tree.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

class CompletionSink final : public ScriptTaskServiceCompletionSink {
public:
    bool handle_script_task_service_completion(const ScriptTaskServiceCompletion& completion) override {
        last = completion;
        ++calls;
        return accept;
    }

    ScriptTaskServiceCompletion last;
    std::size_t calls = 0;
    bool accept = true;
};

ScriptTaskSupervisor make_supervisor() {
    return ScriptTaskSupervisor({{4, 32}, {2, 0}, {1, 64, 64}, 2, 0, {1, 20}});
}

void worker_inbox_dispatches_only_input_and_completion_values() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(31);
    LayerNode root;
    root.type = LayerType::Root;
    InputController controller(root);
    CompletionSink sink;
    ScriptTaskServiceCompletion completion{
        HostServiceJobKind::StorageKv, HostServiceStatus::Completed, 4, 5, 6, 7, 8};
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::ServiceCompletion;
    packet.session = session;
    packet.sequence = 1;
    assert(encode_script_task_service_completion(completion, packet.payload));
    assert(supervisor.post_service_completion(packet) == ScriptTaskMailboxPostStatus::Accepted);

    const ScriptTaskWorkerInboxDispatchResult result = take_and_dispatch_script_task_worker_packet(
        supervisor, controller, sink, {0, 32});
    assert(result.status == ScriptTaskWorkerInboxDispatchStatus::CompletionAccepted);
    assert(result.handled);
    assert(sink.calls == 1);
    assert(sink.last.request_id == 4);
    assert(sink.last.handle == 6);
}

void worker_inbox_rejects_malformed_completion_values() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(32);
    LayerNode root;
    root.type = LayerType::Root;
    InputController controller(root);
    CompletionSink sink;
    assert(supervisor.post_service_completion({ScriptTaskPacketKind::ServiceCompletion, session, 1, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    const ScriptTaskWorkerInboxDispatchResult result = take_and_dispatch_script_task_worker_packet(
        supervisor, controller, sink, {0, 32});
    assert(result.status == ScriptTaskWorkerInboxDispatchStatus::PacketRejected);
    assert(!result.handled);
    assert(sink.calls == 0);
}

} // namespace

int script_task_worker_inbox_tests_main() {
    worker_inbox_dispatches_only_input_and_completion_values();
    worker_inbox_rejects_malformed_completion_values();
    std::cout << "script task worker inbox tests passed\n";
    return 0;
}
