#include "app_runtime/script_task_worker_inbox.h"

namespace jellyframe {

ScriptTaskWorkerInboxDispatchResult take_and_dispatch_script_task_worker_packet(
    ScriptTaskSupervisor& supervisor,
    InputController& controller,
    ScriptTaskServiceCompletionSink& completion_sink,
    const ScriptTaskInputCodecOptions& input_options) {
    ScriptTaskPacket packet;
    if (!supervisor.take_input(packet)) {
        return {};
    }

    if (packet.kind == ScriptTaskPacketKind::Input) {
        const ScriptTaskInputDispatchResult dispatched =
            dispatch_script_task_input_packet(controller, packet, input_options);
        return {dispatched.status == ScriptTaskInputDispatchStatus::Accepted
                    ? ScriptTaskWorkerInboxDispatchStatus::InputAccepted
                    : ScriptTaskWorkerInboxDispatchStatus::InputRejected,
                dispatched.handled};
    }
    if (packet.kind != ScriptTaskPacketKind::ServiceCompletion) {
        return {ScriptTaskWorkerInboxDispatchStatus::PacketRejected, false};
    }

    ScriptTaskServiceCompletion completion;
    if (!decode_script_task_service_completion(packet.payload, completion)) {
        return {ScriptTaskWorkerInboxDispatchStatus::PacketRejected, false};
    }
    const bool handled = completion_sink.handle_script_task_service_completion(completion);
    return {handled ? ScriptTaskWorkerInboxDispatchStatus::CompletionAccepted
                    : ScriptTaskWorkerInboxDispatchStatus::CompletionRejected,
            handled};
}

} // namespace jellyframe
