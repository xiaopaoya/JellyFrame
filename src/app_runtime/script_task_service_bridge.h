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
    // A session-scoped sealed service-payload lease. This is never a host
    // handle and must be copied then released by the worker when nonzero.
    std::uint32_t payload_lease_id = 0;
    std::uint32_t error_code = 0;
    std::uint32_t byte_count = 0;
};

bool encode_script_task_service_completion(const ScriptTaskServiceCompletion& completion,
                                           std::vector<std::uint8_t>& output);
bool decode_script_task_service_completion(const std::vector<std::uint8_t>& input,
                                           ScriptTaskServiceCompletion& output);

enum class ScriptTaskServicePayloadErrorCode : std::uint32_t {
    None = 0,
    CopyUnavailable = 1,
    CopyFailed = 2,
    LeaseRejected = 3,
};

// Supervisor-only bounded writer for a host completion representation. The
// callback cannot grow the scratch storage past its declared capacity.
class ScriptTaskServicePayloadWriter {
public:
    bool append(const std::uint8_t* bytes, std::size_t size);
    bool append(const std::vector<std::uint8_t>& bytes);
    std::size_t size() const;
    std::size_t capacity() const;

private:
    friend class ScriptTaskServiceBridge;

    ScriptTaskServicePayloadWriter(std::vector<std::uint8_t>& storage, std::size_t capacity);
    std::vector<std::uint8_t>& storage_;
    std::size_t capacity_ = 0;
};

// Runs only in the supervisor task. It receives completion values and writes
// a bounded representation; it must not retain the writer or re-enter the
// bridge. No worker or JS object is supplied to this callback.
using ScriptTaskServicePayloadCopyCallback = bool (*)(void* user,
                                                      const HostServiceCompletion& completion,
                                                      ScriptTaskServicePayloadWriter& output);

// Releases the provider record and its host-table entry exactly once after a
// completion has been copied or discarded. When absent, the bridge releases
// only the generic host-table entry.
using ScriptTaskServicePayloadReleaseCallback = bool (*)(void* user,
                                                         const HostServiceCompletion& completion);

struct ScriptTaskServiceBridgeOptions {
    // Includes requests waiting for a host completion and completions waiting
    // for worker-inbox capacity. Must not exceed supervisor tombstone budget.
    std::size_t max_requests = 0;
    // The supervisor scratch reservation used by payload_copy. A nonzero host
    // handle requires a callback and bounded copy before worker delivery.
    std::size_t max_service_payload_bytes = 0;
    ScriptTaskServicePayloadCopyCallback payload_copy = nullptr;
    void* payload_copy_user = nullptr;
    ScriptTaskServicePayloadReleaseCallback payload_release = nullptr;
    void* payload_release_user = nullptr;
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
    // Includes unknown, duplicate and identity-mismatched host completions.
    std::size_t discarded_unmatched_completions = 0;
    std::size_t released_completion_sources = 0;
    std::size_t released_payload_leases = 0;
    std::size_t published_payload_leases = 0;
    std::size_t payload_copy_failures = 0;
    std::size_t payload_lease_rejections = 0;
    bool worker_inbox_full = false;
};

// Results of draining only the worker-to-supervisor request mailbox. Keeping
// this separate from completion pumping lets a supervisor expose useful
// diagnostics without consuming UI-frame traffic.
struct ScriptTaskServiceRequestPumpResult {
    std::size_t received = 0;
    std::size_t accepted = 0;
    std::size_t invalid_packets = 0;
    std::size_t invalid_sessions = 0;
    std::size_t invalid_tokens = 0;
    std::size_t duplicates = 0;
    std::size_t capacity_exceeded = 0;
    std::size_t packet_budget_exceeded = 0;
    std::size_t host_rejected = 0;
    std::size_t cancelled = 0;
    std::size_t invalid_cancels = 0;
};

struct ScriptTaskServiceBridgeTeardownResult {
    std::size_t cancelled_pending_host_jobs = 0;
    // Jobs already accepted by the provider and awaiting a late completion.
    std::size_t awaiting_in_flight_host_completions = 0;
    std::size_t released_ready_completion_sources = 0;
    std::size_t released_ready_payload_leases = 0;
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
                                         std::uint32_t input_handle = 0,
                                         std::uint8_t priority = 0,
                                         std::uint32_t timeout_ms = 0,
                                         std::uint32_t client_token = 0);
    // Supervisor-only entry point for a packet taken from the dedicated
    // worker-to-supervisor service mailbox. It never accepts frame or input
    // traffic, and does not expose AppRuntimeHost to the worker.
    ScriptTaskServiceSubmitResult submit_packet(const ScriptTaskPacket& packet);
    bool cancel_packet(const ScriptTaskPacket& packet);
    // Supervisor-only bounded drain of the dedicated service-request mailbox.
    // It never consumes an AppFrame, raw input or completion packet.
    ScriptTaskServiceRequestPumpResult pump_service_requests();
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
        bool cancelled = false;
        HostServiceCompletion completion;
        std::uint32_t payload_lease_id = 0;
    };

    static bool same_token(const ScriptTaskServiceToken& left, const ScriptTaskServiceToken& right);
    static bool completion_matches(const Record& record, const HostServiceCompletion& completion);
    bool release_completion_payload(const HostServiceCompletion& completion);
    bool release_record_completion_payload(Record& record);
    bool release_record_payload(Record& record);
    void prepare_completion_payload(Record& record, ScriptTaskServiceBridgePumpResult& result);
    void erase_record(std::size_t index);
    bool deliver_ready_record(std::size_t index, ScriptTaskServiceBridgePumpResult& result);

    AppRuntimeHost& host_;
    ScriptTaskSupervisor& supervisor_;
    std::size_t capacity_ = 0;
    std::size_t max_service_payload_bytes_ = 0;
    ScriptTaskServicePayloadCopyCallback payload_copy_ = nullptr;
    void* payload_copy_user_ = nullptr;
    ScriptTaskServicePayloadReleaseCallback payload_release_ = nullptr;
    void* payload_release_user_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<Record> records_;
    std::vector<std::uint8_t> payload_scratch_;
};

} // namespace jellyframe
