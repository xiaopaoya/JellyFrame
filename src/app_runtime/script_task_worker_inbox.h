#pragma once

#include "app_runtime/script_task_input_dispatch.h"
#include "app_runtime/script_task_service_bridge.h"

namespace jellyframe {

// Implemented inside the script worker. The sink receives only decoded
// service values and service-payload lease IDs; it must not retain supervisor or host
// pointers in a JS wrapper.
class ScriptTaskServiceCompletionSink {
public:
    virtual ~ScriptTaskServiceCompletionSink() = default;
    virtual bool handle_script_task_service_completion(const ScriptTaskServiceCompletion& completion) = 0;
};

enum class ScriptTaskWorkerInboxDispatchStatus {
    NoPacket,
    InputAccepted,
    InputRejected,
    CompletionAccepted,
    CompletionRejected,
    PacketRejected,
};

struct ScriptTaskWorkerInboxDispatchResult {
    ScriptTaskWorkerInboxDispatchStatus status = ScriptTaskWorkerInboxDispatchStatus::NoPacket;
    bool handled = false;
};

// Worker-task-only dispatch loop primitive. The supervisor validates session
// generation before exposing the packet. This function never receives a
// worker-to-supervisor service request or a UI frame packet.
ScriptTaskWorkerInboxDispatchResult take_and_dispatch_script_task_worker_packet(
    ScriptTaskSupervisor& supervisor,
    InputController& controller,
    ScriptTaskServiceCompletionSink& completion_sink,
    const ScriptTaskInputCodecOptions& input_options);

enum class ScriptTaskServicePayloadTakeStatus {
    NoPayload,
    Accepted,
    LeaseRejected,
};

// Worker-task-only helper for a completion payload. It always releases a
// copied lease, including if the caller passes a stale session, and leaves no
// host handle or shared buffer reachable from the worker.
ScriptTaskServicePayloadTakeStatus take_script_task_service_payload(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    const ScriptTaskServiceCompletion& completion,
    std::vector<std::uint8_t>& output);

} // namespace jellyframe
