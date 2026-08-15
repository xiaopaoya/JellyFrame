#include "app_runtime/script_task_service_bridge.h"

#include <algorithm>
#include <array>

namespace jellyframe {

namespace {

constexpr std::uint8_t kCompletionPacketVersion = 2;
constexpr std::size_t kCompletionPacketSize = 24;

void write_u32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    output[offset] = static_cast<std::uint8_t>(value & 0xffU);
    output[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    output[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    output[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& input, std::size_t offset) {
    return static_cast<std::uint32_t>(input[offset]) |
           (static_cast<std::uint32_t>(input[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(input[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(input[offset + 3]) << 24U);
}

bool known_kind(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(HostServiceJobKind::Other);
}

bool known_status(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(HostServiceStatus::Timeout);
}

} // namespace

ScriptTaskServicePayloadWriter::ScriptTaskServicePayloadWriter(std::vector<std::uint8_t>& storage,
                                                               std::size_t capacity)
    : storage_(storage), capacity_(capacity) {
    storage_.clear();
}

bool ScriptTaskServicePayloadWriter::append(const std::uint8_t* bytes, std::size_t size) {
    if (size == 0) {
        return true;
    }
    if (bytes == nullptr || size > capacity_ - storage_.size()) {
        return false;
    }
    storage_.insert(storage_.end(), bytes, bytes + size);
    return true;
}

bool ScriptTaskServicePayloadWriter::append(const std::vector<std::uint8_t>& bytes) {
    return append(bytes.data(), bytes.size());
}

std::size_t ScriptTaskServicePayloadWriter::size() const {
    return storage_.size();
}

std::size_t ScriptTaskServicePayloadWriter::capacity() const {
    return capacity_;
}

bool encode_script_task_service_completion(const ScriptTaskServiceCompletion& completion,
                                           std::vector<std::uint8_t>& output) {
    const std::uint8_t kind = static_cast<std::uint8_t>(completion.kind);
    const std::uint8_t status = static_cast<std::uint8_t>(completion.status);
    if (!known_kind(kind) || !known_status(status) || completion.request_id == 0 || completion.client_token == 0) {
        return false;
    }
    output.assign(kCompletionPacketSize, 0);
    output[0] = kCompletionPacketVersion;
    output[1] = kind;
    output[2] = status;
    write_u32(output, 4, completion.request_id);
    write_u32(output, 8, completion.client_token);
    write_u32(output, 12, completion.payload_lease_id);
    write_u32(output, 16, completion.error_code);
    write_u32(output, 20, completion.byte_count);
    return true;
}

bool decode_script_task_service_completion(const std::vector<std::uint8_t>& input,
                                           ScriptTaskServiceCompletion& output) {
    if (input.size() != kCompletionPacketSize || input[0] != kCompletionPacketVersion ||
        input[3] != 0 || !known_kind(input[1]) || !known_status(input[2])) {
        return false;
    }
    const std::uint32_t request_id = read_u32(input, 4);
    const std::uint32_t client_token = read_u32(input, 8);
    if (request_id == 0 || client_token == 0) {
        return false;
    }
    output.kind = static_cast<HostServiceJobKind>(input[1]);
    output.status = static_cast<HostServiceStatus>(input[2]);
    output.request_id = request_id;
    output.client_token = client_token;
    output.payload_lease_id = read_u32(input, 12);
    output.error_code = read_u32(input, 16);
    output.byte_count = read_u32(input, 20);
    return true;
}

ScriptTaskServiceBridge::ScriptTaskServiceBridge(AppRuntimeHost& host,
                                                 ScriptTaskSupervisor& supervisor,
                                                 ScriptTaskServiceBridgeOptions options)
    : host_(host),
      supervisor_(supervisor),
      capacity_(options.max_requests),
      max_service_payload_bytes_(options.max_service_payload_bytes),
      payload_copy_(options.payload_copy),
      payload_copy_user_(options.payload_copy_user),
      payload_release_(options.payload_release),
      payload_release_user_(options.payload_release_user) {
    records_.reserve(capacity_);
    payload_scratch_.reserve(max_service_payload_bytes_);
}

bool ScriptTaskServiceBridge::same_token(const ScriptTaskServiceToken& left,
                                         const ScriptTaskServiceToken& right) {
    return left.session == right.session && left.request_id == right.request_id &&
           left.client_token == right.client_token;
}

bool ScriptTaskServiceBridge::completion_matches(const Record& record,
                                                 const HostServiceCompletion& completion) {
    return record.host_job_id == completion.job_id &&
           record.kind == completion.kind &&
           record.token.session.app_instance_id == completion.app_instance_id &&
           record.token.client_token == completion.client_token;
}

ScriptTaskServiceSubmitResult ScriptTaskServiceBridge::submit(const ScriptAppSession& session,
                                                               std::uint32_t request_id,
                                                               HostServiceJobKind kind,
                                                               std::uint32_t input_handle,
                                                               std::uint8_t priority,
                                                               std::uint32_t timeout_ms,
                                                               std::uint32_t client_token) {
    ScriptTaskServiceSubmitResult result;
    result.token = {session, request_id, client_token};
    if (!supervisor_.accepts(session) || host_.current_app_instance_id() != session.app_instance_id) {
        result.status = ScriptTaskServiceSubmitStatus::InvalidSession;
        return result;
    }
    if (!result.token.valid()) {
        result.status = ScriptTaskServiceSubmitStatus::InvalidToken;
        return result;
    }
    if (supervisor_.worker_inbox_max_payload_bytes() < kCompletionPacketSize) {
        result.status = ScriptTaskServiceSubmitStatus::PacketBudgetExceeded;
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto duplicate = std::find_if(records_.begin(), records_.end(), [&result](const Record& record) {
        return same_token(record.token, result.token);
    });
    if (duplicate != records_.end()) {
        result.status = ScriptTaskServiceSubmitStatus::Duplicate;
        return result;
    }
    if (records_.size() >= capacity_) {
        result.status = ScriptTaskServiceSubmitStatus::CapacityExceeded;
        return result;
    }
    const ScriptTaskServiceTrackStatus tracked = supervisor_.track_service(result.token);
    if (tracked == ScriptTaskServiceTrackStatus::Duplicate) {
        result.status = ScriptTaskServiceSubmitStatus::Duplicate;
        return result;
    }
    if (tracked == ScriptTaskServiceTrackStatus::Full) {
        result.status = ScriptTaskServiceSubmitStatus::CapacityExceeded;
        return result;
    }
    if (tracked != ScriptTaskServiceTrackStatus::Accepted) {
        result.status = ScriptTaskServiceSubmitStatus::InvalidToken;
        return result;
    }

    const HostServiceSubmitResult submitted = host_.submit_current(kind, input_handle, priority, timeout_ms, client_token);
    result.host_status = submitted.rejected_status;
    if (!submitted.accepted) {
        // A rejected submit is still a terminal value that the worker must
        // observe. Keep its ledger record until the normal completion path
        // delivers it, including when the worker inbox is briefly full.
        records_.push_back({result.token,
                            0,
                            kind,
                            true,
                            false,
                            {0,
                             kind,
                             submitted.rejected_status,
                             session.app_instance_id,
                             0,
                             static_cast<std::uint32_t>(ScriptTaskServiceSubmitStatus::HostRejected),
                             0,
                             client_token},
                            0});
        result.status = ScriptTaskServiceSubmitStatus::HostRejected;
        return result;
    }
    records_.push_back({result.token, submitted.job_id, kind, false, false, {}, 0});
    result.host_job_id = submitted.job_id;
    result.host_status = HostServiceStatus::Completed;
    result.status = ScriptTaskServiceSubmitStatus::Accepted;
    return result;
}

ScriptTaskServiceSubmitResult ScriptTaskServiceBridge::submit_packet(const ScriptTaskPacket& packet) {
    if (packet.kind != ScriptTaskPacketKind::ServiceRequest || !supervisor_.accepts(packet.session)) {
        ScriptTaskServiceSubmitResult result;
        result.status = ScriptTaskServiceSubmitStatus::InvalidPacket;
        return result;
    }

    ScriptTaskServiceRequest request;
    const ScriptTaskServiceRequestCodecStatus decoded = decode_script_task_service_request(
        packet.payload, {packet.payload.size()}, request);
    if (decoded != ScriptTaskServiceRequestCodecStatus::Accepted) {
        ScriptTaskServiceSubmitResult result;
        result.status = ScriptTaskServiceSubmitStatus::InvalidPacket;
        return result;
    }
    return submit(packet.session,
                  request.request_id,
                  request.kind,
                  request.input_handle,
                  request.priority,
                  request.timeout_ms,
                  request.client_token);
}

bool ScriptTaskServiceBridge::cancel_packet(const ScriptTaskPacket& packet) {
    if (packet.kind != ScriptTaskPacketKind::ServiceCancel ||
        !supervisor_.accepts(packet.session)) {
        return false;
    }
    ScriptTaskServiceCancel decoded;
    if (decode_script_task_service_cancel(packet.payload, {packet.payload.size()}, decoded) !=
        ScriptTaskServiceRequestCodecStatus::Accepted) {
        return false;
    }
    return cancel({packet.session, decoded.request_id, decoded.client_token});
}

ScriptTaskServiceRequestPumpResult ScriptTaskServiceBridge::pump_service_requests() {
    ScriptTaskServiceRequestPumpResult result;
    ScriptTaskPacket packet;
    while (supervisor_.take_service_request(packet)) {
        ++result.received;
        if (packet.kind == ScriptTaskPacketKind::ServiceCancel) {
            if (cancel_packet(packet)) {
                ++result.cancelled;
            } else {
                ++result.invalid_cancels;
            }
            continue;
        }
        switch (submit_packet(packet).status) {
        case ScriptTaskServiceSubmitStatus::Accepted:
            ++result.accepted;
            break;
        case ScriptTaskServiceSubmitStatus::InvalidPacket:
            ++result.invalid_packets;
            break;
        case ScriptTaskServiceSubmitStatus::InvalidSession:
            ++result.invalid_sessions;
            break;
        case ScriptTaskServiceSubmitStatus::InvalidToken:
            ++result.invalid_tokens;
            break;
        case ScriptTaskServiceSubmitStatus::Duplicate:
            ++result.duplicates;
            break;
        case ScriptTaskServiceSubmitStatus::CapacityExceeded:
            ++result.capacity_exceeded;
            break;
        case ScriptTaskServiceSubmitStatus::PacketBudgetExceeded:
            ++result.packet_budget_exceeded;
            break;
        case ScriptTaskServiceSubmitStatus::HostRejected:
            ++result.host_rejected;
            break;
        }
    }
    return result;
}

void ScriptTaskServiceBridge::erase_record(std::size_t index) {
    records_[index] = records_.back();
    records_.pop_back();
}

bool ScriptTaskServiceBridge::completion_result_handle_is_owned(
    const HostServiceCompletion& completion) const {
    if (completion.result_handle == 0) {
        return true;
    }
    HostHandleInfo info;
    return host_.handles().lookup_copy(completion.result_handle, info) &&
           info.app_instance_id == completion.app_instance_id &&
           (info.client_token == 0 || info.client_token == completion.client_token);
}

bool ScriptTaskServiceBridge::release_completion_payload(const HostServiceCompletion& completion) {
    if (!completion_result_handle_is_owned(completion) || completion.result_handle == 0) {
        return false;
    }
    if (payload_release_ != nullptr) {
        return payload_release_(payload_release_user_, completion);
    }
    return host_.handles().release(completion.result_handle);
}

bool ScriptTaskServiceBridge::release_record_completion_payload(Record& record) {
    if (record.completion.result_handle == 0) {
        return false;
    }
    const HostServiceCompletion completion = record.completion;
    record.completion.result_handle = 0;
    return release_completion_payload(completion);
}

bool ScriptTaskServiceBridge::release_record_payload(Record& record) {
    if (record.payload_lease_id == 0) {
        return false;
    }
    const std::uint32_t payload_lease_id = record.payload_lease_id;
    record.payload_lease_id = 0;
    return supervisor_.release_service_payload(record.token.session, payload_lease_id) ==
           ScriptTaskServicePayloadLeaseStatus::Accepted;
}

void ScriptTaskServiceBridge::prepare_completion_payload(Record& record,
                                                         ScriptTaskServiceBridgePumpResult& result) {
    if (record.completion.result_handle == 0) {
        return;
    }
    if (!completion_result_handle_is_owned(record.completion)) {
        record.completion.result_handle = 0;
        record.completion.status = HostServiceStatus::Failed;
        record.completion.error_code = static_cast<std::uint32_t>(ScriptTaskServicePayloadErrorCode::HandleRejected);
        record.completion.byte_count = 0;
        ++result.payload_handle_rejections;
        return;
    }

    const HostServiceCompletion host_completion = record.completion;
    ScriptTaskServicePayloadWriter writer(payload_scratch_, max_service_payload_bytes_);
    bool copied = false;
    if (payload_copy_ != nullptr && max_service_payload_bytes_ != 0) {
        copied = payload_copy_(payload_copy_user_, host_completion, writer);
    }
    if (!copied) {
        if (release_record_completion_payload(record)) {
            ++result.released_completion_sources;
        }
        record.completion.status = HostServiceStatus::Failed;
        record.completion.error_code = static_cast<std::uint32_t>(
            payload_copy_ == nullptr ? ScriptTaskServicePayloadErrorCode::CopyUnavailable
                                     : ScriptTaskServicePayloadErrorCode::CopyFailed);
        record.completion.byte_count = 0;
        ++result.payload_copy_failures;
        return;
    }

    std::uint32_t payload_lease_id = 0;
    const ScriptTaskServicePayloadLeaseStatus lease_status = supervisor_.publish_service_payload(
        record.token.session, payload_scratch_, payload_lease_id);
    if (release_record_completion_payload(record)) {
        ++result.released_completion_sources;
    }
    if (lease_status != ScriptTaskServicePayloadLeaseStatus::Accepted) {
        record.completion.status = lease_status == ScriptTaskServicePayloadLeaseStatus::PayloadTooLarge ||
                lease_status == ScriptTaskServicePayloadLeaseStatus::ByteBudgetExceeded ||
                lease_status == ScriptTaskServicePayloadLeaseStatus::CapacityExceeded
            ? HostServiceStatus::BudgetExceeded
            : HostServiceStatus::Failed;
        record.completion.error_code = static_cast<std::uint32_t>(ScriptTaskServicePayloadErrorCode::LeaseRejected);
        record.completion.byte_count = 0;
        ++result.payload_lease_rejections;
        return;
    }
    record.payload_lease_id = payload_lease_id;
    record.completion.byte_count = static_cast<std::uint32_t>(payload_scratch_.size());
    ++result.published_payload_leases;
}

bool ScriptTaskServiceBridge::cancel(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(records_.begin(), records_.end(), [&token](const Record& record) {
        return same_token(record.token, token);
    });
    if (found == records_.end()) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(found - records_.begin());
    if (records_[index].completion_ready) {
        supervisor_.cancel_service(token);
        const bool released_payload = release_record_payload(records_[index]);
        (void)released_payload;
        const bool released_handle = release_record_completion_payload(records_[index]);
        (void)released_handle;
        supervisor_.consume_service_completion(token);
        erase_record(index);
        return true;
    }
    if (!supervisor_.cancel_service(token)) {
        return false;
    }
    if (host_.requests().cancel_pending(records_[index].host_job_id)) {
        supervisor_.retire_service(token);
        erase_record(index);
    } else {
        records_[index].cancelled = true;
    }
    return true;
}

bool ScriptTaskServiceBridge::deliver_ready_record(std::size_t index,
                                                   ScriptTaskServiceBridgePumpResult& result) {
    Record& record = records_[index];
    ScriptTaskServiceCompletion packet_completion{
        record.completion.kind,
        record.completion.status,
        record.token.request_id,
        record.token.client_token,
        record.payload_lease_id,
        record.completion.error_code,
        record.completion.byte_count,
    };
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::ServiceCompletion;
    packet.session = record.token.session;
    packet.sequence = record.token.request_id;
    if (!encode_script_task_service_completion(packet_completion, packet.payload)) {
        supervisor_.cancel_service(record.token);
        supervisor_.consume_service_completion(record.token);
        if (release_record_payload(record)) {
            ++result.released_payload_leases;
        }
        ++result.cancelled;
        erase_record(index);
        return true;
    }
    const ScriptTaskServiceCompletionDisposition disposition =
        supervisor_.consume_service_completion(record.token);
    if (disposition == ScriptTaskServiceCompletionDisposition::Cancelled) {
        if (release_record_payload(record)) {
            ++result.released_payload_leases;
        }
        ++result.cancelled;
        erase_record(index);
        return true;
    }
    if (disposition == ScriptTaskServiceCompletionDisposition::Stale) {
        if (release_record_payload(record)) {
            ++result.released_payload_leases;
        }
        ++result.stale;
        erase_record(index);
        return true;
    }
    const ScriptTaskMailboxPostStatus posted = supervisor_.post_service_completion(packet);
    if (posted == ScriptTaskMailboxPostStatus::Accepted) {
        ++result.delivered;
        erase_record(index);
        return true;
    }
    // Keep the record and restore its ledger entry, preserving cancellation
    // semantics until worker-inbox capacity becomes available.
    supervisor_.track_service(record.token);
    if (posted == ScriptTaskMailboxPostStatus::Full) {
        result.worker_inbox_full = true;
    }
    return false;
}

ScriptTaskServiceBridgePumpResult ScriptTaskServiceBridge::pump(AppFrameScratch& scratch) {
    ScriptTaskServiceBridgePumpResult result;
    result.host = host_.pump_frame_completions(scratch);
    std::lock_guard<std::mutex> lock(mutex_);
    for (const HostServiceCompletion& completion : scratch.accepted_completions) {
        const auto found = std::find_if(records_.begin(), records_.end(), [&completion](const Record& record) {
            return record.host_job_id == completion.job_id;
        });
        if (found == records_.end() || found->completion_ready) {
            if (release_completion_payload(completion)) {
                ++result.released_completion_sources;
            }
            ++result.discarded_unmatched_completions;
            continue;
        }
        if (!completion_matches(*found, completion)) {
            if (release_completion_payload(completion)) {
                ++result.released_completion_sources;
            }
            ++result.discarded_unmatched_completions;
            continue;
        }
        found->completion = completion;
        found->completion_ready = true;
        if (found->cancelled) {
            if (release_record_completion_payload(*found)) {
                ++result.released_completion_sources;
            }
        } else {
            prepare_completion_payload(*found, result);
        }
        ++result.queued_for_delivery;
    }
    for (std::size_t index = 0; index < records_.size();) {
        if (!records_[index].completion_ready || !deliver_ready_record(index, result)) {
            ++index;
        }
    }
    return result;
}

ScriptTaskServiceBridgeTeardownResult ScriptTaskServiceBridge::begin_teardown(const ScriptAppSession& session) {
    ScriptTaskServiceBridgeTeardownResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t index = 0; index < records_.size();) {
        Record& record = records_[index];
        if (record.token.session != session) {
            ++index;
            continue;
        }
        if (record.completion_ready) {
            supervisor_.cancel_service(record.token);
            supervisor_.consume_service_completion(record.token);
            if (release_record_payload(record)) {
                ++result.released_ready_payload_leases;
            }
            erase_record(index);
            ++result.retired_records;
            continue;
        }
        supervisor_.cancel_service(record.token);
        if (host_.requests().cancel_pending(record.host_job_id)) {
            supervisor_.retire_service(record.token);
            erase_record(index);
            ++result.cancelled_pending_host_jobs;
            ++result.retired_records;
            continue;
        }
        record.cancelled = true;
        ++result.awaiting_in_flight_host_completions;
        ++index;
    }
    return result;
}

ScriptTaskServiceBridgeTeardownResult ScriptTaskServiceBridge::complete_teardown(
    const ScriptAppSession& retired_session) {
    ScriptTaskServiceBridgeTeardownResult result;
    if (!retired_session.valid() || host_.current_app_instance_id() == retired_session.app_instance_id) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t index = 0; index < records_.size();) {
        if (records_[index].token.session != retired_session) {
            ++index;
            continue;
        }
        if (records_[index].completion_ready && release_record_completion_payload(records_[index])) {
            ++result.released_ready_completion_sources;
        }
        if (records_[index].completion_ready && release_record_payload(records_[index])) {
            ++result.released_ready_payload_leases;
        }
        supervisor_.retire_service(records_[index].token);
        erase_record(index);
        ++result.retired_records;
    }
    return result;
}

std::size_t ScriptTaskServiceBridge::active_request_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

} // namespace jellyframe
