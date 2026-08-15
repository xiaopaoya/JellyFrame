#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace jellyframe {

// Identifies one isolated script-worker lifetime. These are values only: they
// intentionally cannot encode DOM, VM or renderer ownership.
struct ScriptAppSession {
    std::uint32_t app_instance_id = 0;
    std::uint32_t generation = 0;
    std::uint32_t worker_epoch = 0;

    bool valid() const {
        return app_instance_id != 0 && generation != 0 && worker_epoch != 0;
    }
};

bool operator==(const ScriptAppSession& left, const ScriptAppSession& right);
bool operator!=(const ScriptAppSession& left, const ScriptAppSession& right);

class ScriptAppSessionController {
public:
    ScriptAppSession begin(std::uint32_t app_instance_id);
    bool accepts(const ScriptAppSession& session) const;
    bool invalidate(const ScriptAppSession& session);
    ScriptAppSession current_snapshot() const { return current_; }

private:
    static std::uint32_t next_nonzero(std::uint32_t value);

    ScriptAppSession current_;
    std::uint32_t next_generation_ = 1;
    std::uint32_t next_worker_epoch_ = 1;
};

enum class ScriptTaskPacketKind : std::uint8_t {
    None,
    Input,
    FrameReady,
    ServiceRequest,
    ServiceCancel,
    ServiceCompletion,
    NativeLeaseRelease,
    FatalRecord,
};

struct ScriptTaskPacket {
    ScriptTaskPacketKind kind = ScriptTaskPacketKind::None;
    ScriptAppSession session;
    std::uint32_t sequence = 0;
    // FrameReady carries a sealed UI-frame lease ID. Other packets must leave
    // this at zero; service payload leases are encoded in their own packet.
    std::uint32_t frame_lease_id = 0;
    std::vector<std::uint8_t> payload;
};

struct ScriptTaskMailboxOptions {
    std::size_t max_packets = 0;
    std::size_t max_payload_bytes = 0;
};

enum class ScriptTaskMailboxPostStatus {
    Accepted,
    InvalidPacket,
    PayloadTooLarge,
    Full,
};

struct ScriptTaskMailboxStatistics {
    std::size_t posted = 0;
    std::size_t popped = 0;
    std::size_t discarded_stale = 0;
    std::size_t discarded_on_teardown = 0;
    std::size_t rejected_invalid = 0;
    std::size_t rejected_payload = 0;
    std::size_t rejected_full = 0;
};

// Fixed-slot, value-copy mailbox. Payload vector capacity is reserved at
// construction so normal post/pop activity does not allocate.
class ScriptTaskMailbox {
public:
    explicit ScriptTaskMailbox(ScriptTaskMailboxOptions options = {});

    ScriptTaskMailboxPostStatus post(const ScriptTaskPacket& packet);
    bool pop_for(const ScriptAppSession& session, ScriptTaskPacket& output);
    std::size_t discard_all();

    std::size_t size() const;
    std::size_t capacity() const { return slots_.size(); }
    std::size_t max_payload_bytes() const { return max_payload_bytes_; }
    ScriptTaskMailboxStatistics statistics() const;

private:
    struct Slot {
        ScriptTaskPacketKind kind = ScriptTaskPacketKind::None;
        ScriptAppSession session;
        std::uint32_t sequence = 0;
        std::uint32_t frame_lease_id = 0;
        std::vector<std::uint8_t> payload;
    };

    bool packet_is_valid(const ScriptTaskPacket& packet) const;
    void clear_slot(Slot& slot);

    std::size_t max_payload_bytes_ = 0;
    mutable std::mutex mutex_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::vector<Slot> slots_;
    ScriptTaskMailboxStatistics statistics_;
};

struct ScriptTaskFrameLeaseOptions {
    std::size_t max_leases = 0;
    std::size_t max_bytes_per_lease = 0;
    std::size_t max_total_bytes = 0;
};

enum class ScriptTaskFrameLeaseStatus {
    Accepted,
    InvalidSession,
    PayloadTooLarge,
    ByteBudgetExceeded,
    CapacityExceeded,
    StaleLease,
    SessionMismatch,
};

struct ScriptTaskFrameLeaseStatistics {
    std::size_t published = 0;
    std::size_t copied = 0;
    std::size_t released = 0;
    std::size_t released_on_teardown = 0;
    std::size_t rejected_invalid = 0;
    std::size_t rejected_payload = 0;
    std::size_t rejected_budget = 0;
    std::size_t rejected_capacity = 0;
    std::size_t rejected_stale = 0;
    std::size_t rejected_session = 0;
};

// Service payloads use the same sealed-byte invariants as AppFrames, but a
// separate registry keeps UI-frame pressure from consuming a service result's
// byte budget. The aliases make that shared implementation explicit without
// exposing it as a UI frame in the public service contract.
using ScriptTaskServicePayloadLeaseOptions = ScriptTaskFrameLeaseOptions;
using ScriptTaskServicePayloadLeaseStatus = ScriptTaskFrameLeaseStatus;
using ScriptTaskServicePayloadLeaseStatistics = ScriptTaskFrameLeaseStatistics;

// Supervisor-owned sealed frame storage. Worker and UI tasks exchange only the
// returned token; payload bytes are copied in/out and never expose a slot pointer.
class ScriptTaskFrameLeaseRegistry {
public:
    explicit ScriptTaskFrameLeaseRegistry(ScriptTaskFrameLeaseOptions options = {});

    ScriptTaskFrameLeaseStatus publish(const ScriptAppSession& session,
                                       const std::vector<std::uint8_t>& payload,
                                       std::uint32_t& lease_id);
    ScriptTaskFrameLeaseStatus copy_sealed(const ScriptAppSession& session,
                                           std::uint32_t lease_id,
                                           std::vector<std::uint8_t>& output) const;
    ScriptTaskFrameLeaseStatus release(const ScriptAppSession& session, std::uint32_t lease_id);
    std::size_t release_session(const ScriptAppSession& session);

    std::size_t active_count() const;
    std::size_t used_bytes() const;
    ScriptTaskFrameLeaseStatistics statistics() const;

private:
    struct Slot {
        ScriptAppSession session;
        std::uint16_t generation = 1;
        bool active = false;
        std::vector<std::uint8_t> payload;
    };

    static std::uint32_t next_generation(std::uint16_t generation);
    static std::uint32_t make_lease_id(std::size_t index, std::uint16_t generation);
    Slot* slot_for_lease_id(std::uint32_t lease_id);
    const Slot* slot_for_lease_id(std::uint32_t lease_id) const;
    void release_slot(Slot& slot);

    std::size_t max_bytes_per_lease_ = 0;
    std::size_t max_total_bytes_ = 0;
    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::size_t used_bytes_ = 0;
    std::size_t active_count_ = 0;
    mutable ScriptTaskFrameLeaseStatistics statistics_;
};

struct ScriptTaskServiceToken {
    ScriptAppSession session;
    std::uint32_t request_id = 0;
    std::uint32_t client_token = 0;

    bool valid() const {
        return session.valid() && request_id != 0 && client_token != 0;
    }
};

enum class ScriptTaskServiceTrackStatus {
    Accepted,
    InvalidToken,
    Duplicate,
    Full,
};

enum class ScriptTaskServiceCompletionDisposition {
    Accepted,
    Cancelled,
    Stale,
};

struct ScriptTaskServiceStatistics {
    std::size_t tracked = 0;
    std::size_t cancelled = 0;
    std::size_t accepted_completions = 0;
    std::size_t cancelled_completions = 0;
    std::size_t stale_completions = 0;
    std::size_t rejected_invalid = 0;
    std::size_t rejected_duplicate = 0;
    std::size_t rejected_full = 0;
};

// Bounded cancellation tombstones. The supervisor uses this before accepting a
// service completion for a script worker; it does not own host handles itself.
class ScriptTaskServiceLedger {
public:
    explicit ScriptTaskServiceLedger(std::size_t capacity = 0);

    ScriptTaskServiceTrackStatus track(const ScriptTaskServiceToken& token);
    bool cancel(const ScriptTaskServiceToken& token);
    bool retire(const ScriptTaskServiceToken& token);
    std::size_t cancel_session(const ScriptAppSession& session);
    ScriptTaskServiceCompletionDisposition consume_completion(const ScriptAppSession& active_session,
                                                               const ScriptTaskServiceToken& token);
    std::size_t clear_session(const ScriptAppSession& session);
    ScriptTaskServiceStatistics statistics() const;

private:
    struct Record {
        ScriptTaskServiceToken token;
        bool cancelled = false;
    };

    mutable std::mutex mutex_;
    std::vector<Record> records_;
    std::size_t capacity_ = 0;
    ScriptTaskServiceStatistics statistics_;
};

struct ScriptTaskNativeLeaseReleaseIntent {
    ScriptAppSession session;
    std::uint32_t native_lease_id = 0;

    bool valid() const {
        return session.valid() && native_lease_id != 0;
    }
};

enum class ScriptTaskReleaseIntentStatus {
    Accepted,
    Duplicate,
    Invalid,
    Full,
};

// A finalizer can post this value-only intent. The supervisor later performs
// the native release; duplicate intents are harmless and not queued twice.
class ScriptTaskReleaseIntentMailbox {
public:
    explicit ScriptTaskReleaseIntentMailbox(std::size_t capacity = 0);

    ScriptTaskReleaseIntentStatus post(const ScriptTaskNativeLeaseReleaseIntent& intent);
    bool pop(ScriptTaskNativeLeaseReleaseIntent& output);
    std::size_t discard_session(const ScriptAppSession& session);

private:
    mutable std::mutex mutex_;
    std::vector<ScriptTaskNativeLeaseReleaseIntent> intents_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

struct ScriptTaskSupervisorOptions {
    // Shared worker-facing ingress for raw input and service completions.
    ScriptTaskMailboxOptions worker_inbox;
    ScriptTaskMailboxOptions frame_mailbox;
    ScriptTaskFrameLeaseOptions frame_leases;
    std::size_t max_service_tombstones = 0;
    std::size_t max_native_release_intents = 0;
    ScriptTaskMailboxOptions service_request_mailbox;
    ScriptTaskServicePayloadLeaseOptions service_payload_leases;
    // Dedicated worker-to-supervisor fatal status mailbox. It is separate
    // from frame, input and service traffic so recovery cannot be blocked by
    // a full display queue.
    ScriptTaskMailboxOptions fatal_mailbox;
};

struct ScriptTaskFramePublishResult {
    ScriptTaskFrameLeaseStatus lease_status = ScriptTaskFrameLeaseStatus::InvalidSession;
    ScriptTaskMailboxPostStatus mailbox_status = ScriptTaskMailboxPostStatus::InvalidPacket;
    std::uint32_t frame_lease_id = 0;

    bool accepted() const {
        return lease_status == ScriptTaskFrameLeaseStatus::Accepted &&
            mailbox_status == ScriptTaskMailboxPostStatus::Accepted;
    }
};

struct ScriptTaskTeardownResult {
    ScriptAppSession session;
    std::size_t discarded_worker_inbox_packets = 0;
    std::size_t discarded_frame_packets = 0;
    std::size_t discarded_service_request_packets = 0;
    std::size_t released_service_payload_leases = 0;
    std::size_t cancelled_service_requests = 0;
    std::size_t released_frame_leases = 0;
    std::size_t discarded_release_intents = 0;
    std::size_t discarded_fatal_packets = 0;
    std::size_t retired_service_tombstones = 0;
};

// Coordinates the value-only facilities without creating an RTOS task or a VM.
// Teardown is deliberately two stage: the port calls begin_teardown before it
// stops the worker, drains release intents after worker exit, then calls
// complete_teardown only after the UI task has dropped its accepted frame.
class ScriptTaskSupervisor {
public:
    explicit ScriptTaskSupervisor(ScriptTaskSupervisorOptions options = {});

    ScriptAppSession begin(std::uint32_t app_instance_id);
    bool accepts(const ScriptAppSession& session) const;
    ScriptAppSession current() const;

    ScriptTaskMailboxPostStatus post_input(const ScriptTaskPacket& packet);
    // Worker-facing receive path for input and service-completion values. A
    // worker must name its own session so it cannot consume a newer lifetime's
    // packet.
    bool take_worker_packet(const ScriptAppSession& session, ScriptTaskPacket& output);
    ScriptTaskFramePublishResult publish_frame(const ScriptAppSession& session,
                                               const std::vector<std::uint8_t>& payload);
    // Supervisor-only worker-inbox path. Service bridges may post a bounded
    // value packet alongside input, but cannot inject arbitrary packet kinds.
    ScriptTaskMailboxPostStatus post_service_completion(const ScriptTaskPacket& packet);
    bool take_frame_packet(ScriptTaskPacket& output);
    ScriptTaskMailboxPostStatus post_fatal(const ScriptTaskPacket& packet);
    bool take_fatal(ScriptTaskPacket& output);
    std::size_t worker_inbox_max_payload_bytes() const {
        return worker_inbox_.max_payload_bytes();
    }
    ScriptTaskFrameLeaseStatus copy_frame(const ScriptAppSession& session,
                                          std::uint32_t lease_id,
                                          std::vector<std::uint8_t>& output) const;
    ScriptTaskFrameLeaseStatus release_frame(const ScriptAppSession& session, std::uint32_t lease_id);

    // A distinct sealed registry for service-result bytes. Worker code may
    // copy a matching value and release it, but never maps host storage.
    ScriptTaskServicePayloadLeaseStatus publish_service_payload(const ScriptAppSession& session,
                                                                 const std::vector<std::uint8_t>& payload,
                                                                 std::uint32_t& lease_id);
    ScriptTaskServicePayloadLeaseStatus copy_service_payload(const ScriptAppSession& session,
                                                              std::uint32_t lease_id,
                                                              std::vector<std::uint8_t>& output) const;
    ScriptTaskServicePayloadLeaseStatus release_service_payload(const ScriptAppSession& session,
                                                                 std::uint32_t lease_id);

    ScriptTaskServiceTrackStatus track_service(const ScriptTaskServiceToken& token);
    ScriptTaskMailboxPostStatus post_service_request(const ScriptTaskPacket& packet);
    bool take_service_request(ScriptTaskPacket& output);
    bool cancel_service(const ScriptTaskServiceToken& token);
    // Used only when the host atomically cancels a still-pending request, so
    // no late completion can require its cancellation tombstone.
    bool retire_service(const ScriptTaskServiceToken& token);
    ScriptTaskServiceCompletionDisposition consume_service_completion(const ScriptTaskServiceToken& token);

    ScriptTaskReleaseIntentStatus post_native_release_intent(const ScriptTaskNativeLeaseReleaseIntent& intent);
    bool take_native_release_intent(ScriptTaskNativeLeaseReleaseIntent& output);

    ScriptTaskTeardownResult begin_teardown(const ScriptAppSession& session);
    ScriptTaskTeardownResult complete_teardown(const ScriptAppSession& retired_session);

private:
    static std::uint32_t next_nonzero(std::uint32_t value);

    mutable std::mutex state_mutex_;
    ScriptAppSessionController sessions_;
    ScriptTaskMailbox worker_inbox_;
    ScriptTaskMailbox frame_mailbox_;
    ScriptTaskMailbox service_request_mailbox_;
    ScriptTaskFrameLeaseRegistry frame_leases_;
    ScriptTaskFrameLeaseRegistry service_payload_leases_;
    ScriptTaskServiceLedger services_;
    ScriptTaskReleaseIntentMailbox release_intents_;
    ScriptTaskMailbox fatal_mailbox_;
    // Keeps the retiring identity alive until every supervisor-owned resource
    // has been released in complete_teardown().
    ScriptAppSession retiring_session_;
    std::uint32_t next_frame_sequence_ = 1;
};

} // namespace jellyframe
