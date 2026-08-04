#pragma once

#include "app_runtime/app_host.h"
#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_service_request_codec.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace jellyframe {

// Stable, fixed-width completion payload sent through ScriptTaskMailbox. It
// intentionally contains no native address or host-table payload pointer.
struct ScriptTaskServiceCompletion {
    HostServiceJobKind kind = HostServiceJobKind::Other;
    HostServiceStatus status = HostServiceStatus::Failed;
    std::uint32_t request_id = 0;
    std::uint32_t client_token = 0;
    std::uint32_t handle = 0;
    std::uint32_t error_code = 0;
    std::uint32_t byte_count = 0;
};

bool encode_script_task_service_completion(const ScriptTaskServiceCompletion& completion,
                                           std::vector<std::uint8_t>& output);
bool decode_script_task_service_completion(const std::vector<std::uint8_t>& input,
                                           ScriptTaskServiceCompletion& output);

struct ScriptTaskServiceBridgeOptions {
    // Includes requests waiting for a host completion and completions waiting
    // for worker-inbox capacity. Must not exceed supervisor tombstone budget.
    std::size_t max_requests = 0;
};

enum class ScriptTaskServiceSubmitStatus {
    Accepted,
    InvalidPacket,
    InvalidSession,
    InvalidToken,
    Duplicate,
    CapacityExceeded,
    PacketBudgetExceeded,
    HostRejected,
};

struct ScriptTaskServiceSubmitResult {
    ScriptTaskServiceSubmitStatus status = ScriptTaskServiceSubmitStatus::InvalidSession;
    ScriptTaskServiceToken token;
    std::uint32_t host_job_id = 0;
    HostServiceStatus host_status = HostServiceStatus::Cancelled;

    bool accepted() const { return status == ScriptTaskServiceSubmitStatus::Accepted; }
};

struct ScriptTaskServiceBridgePumpResult {
    AppCompletionPumpResult host;
    std::size_t queued_for_delivery = 0;
    std::size_t delivered = 0;
    std::size_t cancelled = 0;
    std::size_t stale = 0;
    std::size_t discarded_unmapped = 0;
    std::size_t released_handles = 0;
    bool worker_mailbox_full = false;
};

struct ScriptTaskServiceBridgeTeardownResult {
    std::size_t cancelled_pending_host_jobs = 0;
    std::size_t retained_in_flight_host_jobs = 0;
    std::size_t released_ready_handles = 0;
    std::size_t retired_records = 0;
};

// Supervisor-task-only adapter between AppRuntimeHost and ScriptTaskSupervisor.
// It must be the exclusive consumer of AppRuntimeHost completion events while a
// script app is active. Worker code sees only value packets and opaque handle
// IDs; this class never exposes AppRuntimeHost storage to a worker task.
class ScriptTaskServiceBridge {
public:
    ScriptTaskServiceBridge(AppRuntimeHost& host,
                            ScriptTaskSupervisor& supervisor,
                            ScriptTaskServiceBridgeOptions options = {});

    ScriptTaskServiceSubmitResult submit(const ScriptAppSession& session,
                                         std::uint32_t request_id,
                                         HostServiceJobKind kind,
                                         std::uint32_t request_handle = 0,
                                         std::uint8_t priority = 0,
                                         std::uint32_t timeout_ms = 0,
                                         std::uint32_t client_token = 0);
    // Supervisor-only entry point for a packet taken from the dedicated
    // worker-to-supervisor service mailbox. It never accepts frame or input
    // traffic, and does not expose AppRuntimeHost to the worker.
    ScriptTaskServiceSubmitResult submit_packet(const ScriptTaskPacket& packet);
    bool cancel(const ScriptTaskServiceToken& token);

    // Pumps AppRuntimeHost completions into the worker inbox. Scratch must
    // be reserved by the caller for the host capability budget and is reused.
    ScriptTaskServiceBridgePumpResult pump(AppFrameScratch& scratch);

    // Marks the session cancelled and removes requests the host still has in
    // its pending queue. In-flight jobs remain tracked until host teardown or
    // their late completion so their handles cannot leak.
    ScriptTaskServiceBridgeTeardownResult begin_teardown(const ScriptAppSession& session);

    // Requires the host no longer to expose this app instance as current.
    // AppRuntimeHost teardown then owns all late-completion handle release.
    ScriptTaskServiceBridgeTeardownResult complete_teardown(const ScriptAppSession& retired_session);

    std::size_t active_request_count() const;

private:
    struct Record {
        ScriptTaskServiceToken token;
        std::uint32_t host_job_id = 0;
        bool completion_ready = false;
        HostServiceCompletion completion;
    };

    static bool same_token(const ScriptTaskServiceToken& left, const ScriptTaskServiceToken& right);
    static bool completion_matches(const Record& record, const HostServiceCompletion& completion);
    bool release_completion_handle(const HostServiceCompletion& completion);
    void erase_record(std::size_t index);
    bool deliver_ready_record(std::size_t index, ScriptTaskServiceBridgePumpResult& result);

    AppRuntimeHost& host_;
    ScriptTaskSupervisor& supervisor_;
    std::size_t capacity_ = 0;
    mutable std::mutex mutex_;
    std::vector<Record> records_;
};

} // namespace jellyframe
