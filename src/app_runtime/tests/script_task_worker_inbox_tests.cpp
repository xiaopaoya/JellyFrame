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
    ScriptTaskSupervisorOptions options;
    options.input_mailbox = {4, 32};
    options.frame_mailbox = {2, 0};
    options.frame_leases = {1, 64, 64};
    options.max_service_tombstones = 2;
    options.service_request_mailbox = {1, 20};
    options.service_payload_leases = {2, 20, 40};
    return ScriptTaskSupervisor(options);
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
    assert(sink.last.payload_lease_id == 6);
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

void worker_copies_and_releases_completion_payload_values() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(33);
    std::uint32_t lease_id = 0;
    assert(supervisor.publish_service_payload(session, {9, 8, 7}, lease_id) ==
           ScriptTaskServicePayloadLeaseStatus::Accepted);
    ScriptTaskServiceCompletion completion{
        HostServiceJobKind::StorageKv, HostServiceStatus::Completed, 1, 2, lease_id, 0, 3};
    std::vector<std::uint8_t> payload;
    assert(take_script_task_service_payload(supervisor, session, completion, payload) ==
           ScriptTaskServicePayloadTakeStatus::Accepted);
    assert(payload == std::vector<std::uint8_t>({9, 8, 7}));
    assert(take_script_task_service_payload(supervisor, session, completion, payload) ==
           ScriptTaskServicePayloadTakeStatus::LeaseRejected);
    completion.payload_lease_id = 0;
    assert(take_script_task_service_payload(supervisor, session, completion, payload) ==
           ScriptTaskServicePayloadTakeStatus::NoPayload);
}

} // namespace

int script_task_worker_inbox_tests_main() {
    worker_inbox_dispatches_only_input_and_completion_values();
    worker_inbox_rejects_malformed_completion_values();
    worker_copies_and_releases_completion_payload_values();
    std::cout << "script task worker inbox tests passed\n";
    return 0;
}
