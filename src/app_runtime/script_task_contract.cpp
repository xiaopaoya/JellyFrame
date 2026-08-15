#include "app_runtime/script_task_contract.h"

#include <algorithm>

namespace jellyframe {
namespace {

constexpr std::uint32_t kLeaseIndexMask = 0xFFFFU;
constexpr std::size_t kMaxLeaseSlots = static_cast<std::size_t>(kLeaseIndexMask);

bool same_service_token(const ScriptTaskServiceToken& left, const ScriptTaskServiceToken& right) {
    return left.session == right.session && left.request_id == right.request_id &&
        left.client_token == right.client_token;
}

} // namespace

bool operator==(const ScriptAppSession& left, const ScriptAppSession& right) {
    return left.app_instance_id == right.app_instance_id && left.generation == right.generation &&
        left.worker_epoch == right.worker_epoch;
}

bool operator!=(const ScriptAppSession& left, const ScriptAppSession& right) {
    return !(left == right);
}

std::uint32_t ScriptAppSessionController::next_nonzero(std::uint32_t value) {
    ++value;
    return value == 0 ? 1 : value;
}

ScriptAppSession ScriptAppSessionController::begin(std::uint32_t app_instance_id) {
    if (app_instance_id == 0) {
        return {};
    }
    current_ = {app_instance_id, next_generation_, next_worker_epoch_};
    next_generation_ = next_nonzero(next_generation_);
    next_worker_epoch_ = next_nonzero(next_worker_epoch_);
    return current_;
}

bool ScriptAppSessionController::accepts(const ScriptAppSession& session) const {
    return current_.valid() && current_ == session;
}

bool ScriptAppSessionController::invalidate(const ScriptAppSession& session) {
    if (!accepts(session)) {
        return false;
    }
    current_ = {};
    return true;
}

ScriptTaskMailbox::ScriptTaskMailbox(ScriptTaskMailboxOptions options)
    : max_payload_bytes_(options.max_payload_bytes), slots_(options.max_packets) {
    for (Slot& slot : slots_) {
        slot.payload.reserve(max_payload_bytes_);
    }
}

bool ScriptTaskMailbox::packet_is_valid(const ScriptTaskPacket& packet) const {
    if (packet.kind == ScriptTaskPacketKind::None || !packet.session.valid() || packet.sequence == 0) {
        return false;
    }
    if (packet.kind == ScriptTaskPacketKind::FrameReady) {
        return packet.frame_lease_id != 0 && packet.payload.empty();
    }
    return packet.frame_lease_id == 0;
}

void ScriptTaskMailbox::clear_slot(Slot& slot) {
    slot.kind = ScriptTaskPacketKind::None;
    slot.session = {};
    slot.sequence = 0;
    slot.frame_lease_id = 0;
    slot.payload.clear();
}

ScriptTaskMailboxPostStatus ScriptTaskMailbox::post(const ScriptTaskPacket& packet) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!packet_is_valid(packet)) {
        ++statistics_.rejected_invalid;
        return ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    if (packet.payload.size() > max_payload_bytes_) {
        ++statistics_.rejected_payload;
        return ScriptTaskMailboxPostStatus::PayloadTooLarge;
    }
    if (size_ == slots_.size()) {
        ++statistics_.rejected_full;
        return ScriptTaskMailboxPostStatus::Full;
    }
    if (slots_.empty()) {
        ++statistics_.rejected_full;
        return ScriptTaskMailboxPostStatus::Full;
    }

    Slot& slot = slots_[(head_ + size_) % slots_.size()];
    slot.kind = packet.kind;
    slot.session = packet.session;
    slot.sequence = packet.sequence;
    slot.frame_lease_id = packet.frame_lease_id;
    slot.payload.assign(packet.payload.begin(), packet.payload.end());
    ++size_;
    ++statistics_.posted;
    return ScriptTaskMailboxPostStatus::Accepted;
}

bool ScriptTaskMailbox::pop_for(const ScriptAppSession& session, ScriptTaskPacket& output) {
    if (!session.valid()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    while (size_ != 0) {
        Slot& slot = slots_[head_];
        if (slot.session != session) {
            clear_slot(slot);
            head_ = (head_ + 1) % slots_.size();
            --size_;
            ++statistics_.discarded_stale;
            continue;
        }
        output.kind = slot.kind;
        output.session = slot.session;
        output.sequence = slot.sequence;
        output.frame_lease_id = slot.frame_lease_id;
        output.payload.assign(slot.payload.begin(), slot.payload.end());
        clear_slot(slot);
        head_ = (head_ + 1) % slots_.size();
        --size_;
        ++statistics_.popped;
        return true;
    }
    return false;
}

std::size_t ScriptTaskMailbox::discard_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t discarded = size_;
    while (size_ != 0) {
        clear_slot(slots_[head_]);
        head_ = (head_ + 1) % slots_.size();
        --size_;
    }
    statistics_.discarded_on_teardown += discarded;
    return discarded;
}

std::size_t ScriptTaskMailbox::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

ScriptTaskMailboxStatistics ScriptTaskMailbox::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

ScriptTaskFrameLeaseRegistry::ScriptTaskFrameLeaseRegistry(ScriptTaskFrameLeaseOptions options)
    : max_bytes_per_lease_(options.max_bytes_per_lease),
      max_total_bytes_(options.max_total_bytes),
      slots_(std::min(options.max_leases, kMaxLeaseSlots)) {
    for (Slot& slot : slots_) {
        slot.payload.reserve(max_bytes_per_lease_);
    }
}

std::uint32_t ScriptTaskFrameLeaseRegistry::next_generation(std::uint16_t generation) {
    ++generation;
    return generation == 0 ? 1 : generation;
}

std::uint32_t ScriptTaskFrameLeaseRegistry::make_lease_id(std::size_t index, std::uint16_t generation) {
    return (static_cast<std::uint32_t>(generation) << 16U) |
        static_cast<std::uint32_t>(index + 1U);
}

ScriptTaskFrameLeaseRegistry::Slot* ScriptTaskFrameLeaseRegistry::slot_for_lease_id(std::uint32_t lease_id) {
    const std::uint32_t index_value = lease_id & kLeaseIndexMask;
    if (lease_id == 0 || index_value == 0) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(index_value - 1U);
    if (index >= slots_.size()) {
        return nullptr;
    }
    Slot& slot = slots_[index];
    const std::uint16_t generation = static_cast<std::uint16_t>(lease_id >> 16U);
    return slot.active && slot.generation == generation ? &slot : nullptr;
}

const ScriptTaskFrameLeaseRegistry::Slot* ScriptTaskFrameLeaseRegistry::slot_for_lease_id(std::uint32_t lease_id) const {
    return const_cast<ScriptTaskFrameLeaseRegistry*>(this)->slot_for_lease_id(lease_id);
}

void ScriptTaskFrameLeaseRegistry::release_slot(Slot& slot) {
    used_bytes_ -= std::min(used_bytes_, slot.payload.size());
    slot.payload.clear();
    slot.session = {};
    slot.active = false;
    slot.generation = static_cast<std::uint16_t>(next_generation(slot.generation));
    --active_count_;
}

ScriptTaskFrameLeaseStatus ScriptTaskFrameLeaseRegistry::publish(const ScriptAppSession& session,
                                                                  const std::vector<std::uint8_t>& payload,
                                                                  std::uint32_t& lease_id) {
    lease_id = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!session.valid()) {
        ++statistics_.rejected_invalid;
        return ScriptTaskFrameLeaseStatus::InvalidSession;
    }
    if (payload.size() > max_bytes_per_lease_) {
        ++statistics_.rejected_payload;
        return ScriptTaskFrameLeaseStatus::PayloadTooLarge;
    }
    if (payload.size() > max_total_bytes_ - std::min(max_total_bytes_, used_bytes_)) {
        ++statistics_.rejected_budget;
        return ScriptTaskFrameLeaseStatus::ByteBudgetExceeded;
    }
    const auto free_slot = std::find_if(slots_.begin(), slots_.end(), [](const Slot& slot) {
        return !slot.active;
    });
    if (free_slot == slots_.end()) {
        ++statistics_.rejected_capacity;
        return ScriptTaskFrameLeaseStatus::CapacityExceeded;
    }
    free_slot->session = session;
    free_slot->payload.assign(payload.begin(), payload.end());
    free_slot->active = true;
    ++active_count_;
    used_bytes_ += free_slot->payload.size();
    const std::size_t index = static_cast<std::size_t>(free_slot - slots_.begin());
    lease_id = make_lease_id(index, free_slot->generation);
    ++statistics_.published;
    return ScriptTaskFrameLeaseStatus::Accepted;
}

ScriptTaskFrameLeaseStatus ScriptTaskFrameLeaseRegistry::copy_sealed(const ScriptAppSession& session,
                                                                      std::uint32_t lease_id,
                                                                      std::vector<std::uint8_t>& output) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = slot_for_lease_id(lease_id);
    if (slot == nullptr) {
        ++statistics_.rejected_stale;
        return ScriptTaskFrameLeaseStatus::StaleLease;
    }
    if (slot->session != session) {
        ++statistics_.rejected_session;
        return ScriptTaskFrameLeaseStatus::SessionMismatch;
    }
    output.assign(slot->payload.begin(), slot->payload.end());
    ++statistics_.copied;
    return ScriptTaskFrameLeaseStatus::Accepted;
}

ScriptTaskFrameLeaseStatus ScriptTaskFrameLeaseRegistry::release(const ScriptAppSession& session,
                                                                  std::uint32_t lease_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = slot_for_lease_id(lease_id);
    if (slot == nullptr) {
        ++statistics_.rejected_stale;
        return ScriptTaskFrameLeaseStatus::StaleLease;
    }
    if (slot->session != session) {
        ++statistics_.rejected_session;
        return ScriptTaskFrameLeaseStatus::SessionMismatch;
    }
    release_slot(*slot);
    ++statistics_.released;
    return ScriptTaskFrameLeaseStatus::Accepted;
}

std::size_t ScriptTaskFrameLeaseRegistry::release_session(const ScriptAppSession& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t released = 0;
    for (Slot& slot : slots_) {
        if (slot.active && slot.session == session) {
            release_slot(slot);
            ++released;
        }
    }
    statistics_.released_on_teardown += released;
    return released;
}

std::size_t ScriptTaskFrameLeaseRegistry::active_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_count_;
}

std::size_t ScriptTaskFrameLeaseRegistry::used_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return used_bytes_;
}

ScriptTaskFrameLeaseStatistics ScriptTaskFrameLeaseRegistry::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

ScriptTaskServiceLedger::ScriptTaskServiceLedger(std::size_t capacity)
    : capacity_(capacity) {
    records_.reserve(capacity_);
}

ScriptTaskServiceTrackStatus ScriptTaskServiceLedger::track(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!token.valid()) {
        ++statistics_.rejected_invalid;
        return ScriptTaskServiceTrackStatus::InvalidToken;
    }
    const auto existing = std::find_if(records_.begin(), records_.end(), [&token](const Record& record) {
        return same_service_token(record.token, token);
    });
    if (existing != records_.end()) {
        ++statistics_.rejected_duplicate;
        return ScriptTaskServiceTrackStatus::Duplicate;
    }
    if (records_.size() == capacity_) {
        ++statistics_.rejected_full;
        return ScriptTaskServiceTrackStatus::Full;
    }
    records_.push_back({token, false});
    ++statistics_.tracked;
    return ScriptTaskServiceTrackStatus::Accepted;
}

bool ScriptTaskServiceLedger::cancel(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(records_.begin(), records_.end(), [&token](const Record& record) {
        return same_service_token(record.token, token);
    });
    if (found == records_.end() || found->cancelled) {
        return false;
    }
    found->cancelled = true;
    ++statistics_.cancelled;
    return true;
}

bool ScriptTaskServiceLedger::retire(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = std::find_if(records_.begin(), records_.end(), [&token](const Record& record) {
        return same_service_token(record.token, token);
    });
    if (found == records_.end()) {
        return false;
    }
    records_.erase(found);
    return true;
}

std::size_t ScriptTaskServiceLedger::cancel_session(const ScriptAppSession& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t cancelled = 0;
    for (Record& record : records_) {
        if (record.token.session == session && !record.cancelled) {
            record.cancelled = true;
            ++cancelled;
        }
    }
    statistics_.cancelled += cancelled;
    return cancelled;
}

ScriptTaskServiceCompletionDisposition ScriptTaskServiceLedger::consume_completion(
    const ScriptAppSession& active_session,
    const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!token.valid()) {
        ++statistics_.stale_completions;
        return ScriptTaskServiceCompletionDisposition::Stale;
    }
    const auto found = std::find_if(records_.begin(), records_.end(), [&token](const Record& record) {
        return same_service_token(record.token, token);
    });
    if (found == records_.end()) {
        ++statistics_.stale_completions;
        return ScriptTaskServiceCompletionDisposition::Stale;
    }
    const bool cancelled = found->cancelled;
    records_.erase(found);
    if (cancelled) {
        ++statistics_.cancelled_completions;
        return ScriptTaskServiceCompletionDisposition::Cancelled;
    }
    if (!active_session.valid() || token.session != active_session) {
        ++statistics_.stale_completions;
        return ScriptTaskServiceCompletionDisposition::Stale;
    }
    ++statistics_.accepted_completions;
    return ScriptTaskServiceCompletionDisposition::Accepted;
}

std::size_t ScriptTaskServiceLedger::clear_session(const ScriptAppSession& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(), [&session](const Record& record) {
        return record.token.session == session;
    }), records_.end());
    return before - records_.size();
}

ScriptTaskServiceStatistics ScriptTaskServiceLedger::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

ScriptTaskReleaseIntentMailbox::ScriptTaskReleaseIntentMailbox(std::size_t capacity)
    : intents_(capacity), capacity_(capacity) {}

ScriptTaskReleaseIntentStatus ScriptTaskReleaseIntentMailbox::post(
    const ScriptTaskNativeLeaseReleaseIntent& intent) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!intent.valid()) {
        return ScriptTaskReleaseIntentStatus::Invalid;
    }
    for (std::size_t offset = 0; offset < size_; ++offset) {
        const ScriptTaskNativeLeaseReleaseIntent& queued = intents_[(head_ + offset) % capacity_];
        if (queued.session == intent.session && queued.native_lease_id == intent.native_lease_id) {
            return ScriptTaskReleaseIntentStatus::Duplicate;
        }
    }
    if (size_ == capacity_) {
        return ScriptTaskReleaseIntentStatus::Full;
    }
    intents_[(head_ + size_) % capacity_] = intent;
    ++size_;
    return ScriptTaskReleaseIntentStatus::Accepted;
}

bool ScriptTaskReleaseIntentMailbox::pop(ScriptTaskNativeLeaseReleaseIntent& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (size_ == 0) {
        return false;
    }
    output = intents_[head_];
    intents_[head_] = {};
    head_ = (head_ + 1) % capacity_;
    --size_;
    return true;
}

std::size_t ScriptTaskReleaseIntentMailbox::discard_session(const ScriptAppSession& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t before = size_;
    std::size_t kept = 0;
    for (std::size_t offset = 0; offset < before; ++offset) {
        const std::size_t read_index = (head_ + offset) % capacity_;
        const ScriptTaskNativeLeaseReleaseIntent intent = intents_[read_index];
        if (intent.session == session) {
            continue;
        }
        intents_[(head_ + kept) % capacity_] = intent;
        ++kept;
    }
    for (std::size_t offset = kept; offset < before; ++offset) {
        intents_[(head_ + offset) % capacity_] = {};
    }
    size_ = kept;
    return before - kept;
}

ScriptTaskSupervisor::ScriptTaskSupervisor(ScriptTaskSupervisorOptions options)
    : input_mailbox_(options.input_mailbox),
      frame_mailbox_(options.frame_mailbox),
      service_request_mailbox_(options.service_request_mailbox),
      frame_leases_(options.frame_leases),
      service_payload_leases_(options.service_payload_leases),
      services_(options.max_service_tombstones),
      release_intents_(options.max_native_release_intents),
      fatal_mailbox_(options.fatal_mailbox) {}

std::uint32_t ScriptTaskSupervisor::next_nonzero(std::uint32_t value) {
    ++value;
    return value == 0 ? 1 : value;
}

ScriptAppSession ScriptTaskSupervisor::begin(std::uint32_t app_instance_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (sessions_.current_snapshot().valid() || retiring_session_.valid()) {
        return {};
    }
    next_frame_sequence_ = 1;
    return sessions_.begin(app_instance_id);
}

bool ScriptTaskSupervisor::accepts(const ScriptAppSession& session) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return sessions_.accepts(session);
}

ScriptAppSession ScriptTaskSupervisor::current() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return sessions_.current_snapshot();
}

ScriptTaskMailboxPostStatus ScriptTaskSupervisor::post_input(const ScriptTaskPacket& packet) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (packet.kind != ScriptTaskPacketKind::Input || !sessions_.accepts(packet.session)) {
        return ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    return input_mailbox_.post(packet);
}

bool ScriptTaskSupervisor::take_input(ScriptTaskPacket& output) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return input_mailbox_.pop_for(sessions_.current_snapshot(), output);
}

ScriptTaskFramePublishResult ScriptTaskSupervisor::publish_frame(const ScriptAppSession& session,
    const std::vector<std::uint8_t>& payload) {
    ScriptTaskFramePublishResult result;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.accepts(session)) {
        return result;
    }
    result.lease_status = frame_leases_.publish(session, payload, result.frame_lease_id);
    if (result.lease_status != ScriptTaskFrameLeaseStatus::Accepted) {
        return result;
    }
    const ScriptTaskPacket frame{ScriptTaskPacketKind::FrameReady,
                                 session,
                                 next_frame_sequence_,
                                 result.frame_lease_id,
                                 {}};
    result.mailbox_status = frame_mailbox_.post(frame);
    if (result.mailbox_status != ScriptTaskMailboxPostStatus::Accepted) {
        frame_leases_.release(session, result.frame_lease_id);
        result.frame_lease_id = 0;
        return result;
    }
    next_frame_sequence_ = next_nonzero(next_frame_sequence_);
    return result;
}

ScriptTaskMailboxPostStatus ScriptTaskSupervisor::post_service_completion(const ScriptTaskPacket& packet) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (packet.kind != ScriptTaskPacketKind::ServiceCompletion || !sessions_.accepts(packet.session)) {
        return ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    return input_mailbox_.post(packet);
}

bool ScriptTaskSupervisor::take_frame_packet(ScriptTaskPacket& output) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return frame_mailbox_.pop_for(sessions_.current_snapshot(), output);
}

ScriptTaskMailboxPostStatus ScriptTaskSupervisor::post_fatal(const ScriptTaskPacket& packet) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (packet.kind != ScriptTaskPacketKind::FatalRecord || !sessions_.accepts(packet.session)) {
        return ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    return fatal_mailbox_.post(packet);
}

bool ScriptTaskSupervisor::take_fatal(ScriptTaskPacket& output) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return fatal_mailbox_.pop_for(sessions_.current_snapshot(), output);
}

ScriptTaskFrameLeaseStatus ScriptTaskSupervisor::copy_frame(const ScriptAppSession& session,
                                                             std::uint32_t lease_id,
                                                             std::vector<std::uint8_t>& output) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.accepts(session)) {
        return ScriptTaskFrameLeaseStatus::InvalidSession;
    }
    return frame_leases_.copy_sealed(session, lease_id, output);
}

ScriptTaskFrameLeaseStatus ScriptTaskSupervisor::release_frame(const ScriptAppSession& session,
                                                                std::uint32_t lease_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return frame_leases_.release(session, lease_id);
}

ScriptTaskServicePayloadLeaseStatus ScriptTaskSupervisor::publish_service_payload(
    const ScriptAppSession& session,
    const std::vector<std::uint8_t>& payload,
    std::uint32_t& lease_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.accepts(session)) {
        return ScriptTaskServicePayloadLeaseStatus::InvalidSession;
    }
    return service_payload_leases_.publish(session, payload, lease_id);
}

ScriptTaskServicePayloadLeaseStatus ScriptTaskSupervisor::copy_service_payload(
    const ScriptAppSession& session,
    std::uint32_t lease_id,
    std::vector<std::uint8_t>& output) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.accepts(session)) {
        return ScriptTaskServicePayloadLeaseStatus::InvalidSession;
    }
    return service_payload_leases_.copy_sealed(session, lease_id, output);
}

ScriptTaskServicePayloadLeaseStatus ScriptTaskSupervisor::release_service_payload(
    const ScriptAppSession& session,
    std::uint32_t lease_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return service_payload_leases_.release(session, lease_id);
}

ScriptTaskServiceTrackStatus ScriptTaskSupervisor::track_service(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.accepts(token.session)) {
        return ScriptTaskServiceTrackStatus::InvalidToken;
    }
    return services_.track(token);
}

ScriptTaskMailboxPostStatus ScriptTaskSupervisor::post_service_request(const ScriptTaskPacket& packet) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if ((packet.kind != ScriptTaskPacketKind::ServiceRequest &&
         packet.kind != ScriptTaskPacketKind::ServiceCancel) ||
        !sessions_.accepts(packet.session)) {
        return ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    return service_request_mailbox_.post(packet);
}

bool ScriptTaskSupervisor::take_service_request(ScriptTaskPacket& output) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return service_request_mailbox_.pop_for(sessions_.current_snapshot(), output);
}

bool ScriptTaskSupervisor::cancel_service(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return sessions_.accepts(token.session) && services_.cancel(token);
}

bool ScriptTaskSupervisor::retire_service(const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return (sessions_.accepts(token.session) || token.session == retiring_session_) &&
           services_.retire(token);
}

ScriptTaskServiceCompletionDisposition ScriptTaskSupervisor::consume_service_completion(
    const ScriptTaskServiceToken& token) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return services_.consume_completion(sessions_.current_snapshot(), token);
}

ScriptTaskReleaseIntentStatus ScriptTaskSupervisor::post_native_release_intent(
    const ScriptTaskNativeLeaseReleaseIntent& intent) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (intent.session != retiring_session_ && !sessions_.accepts(intent.session)) {
        return ScriptTaskReleaseIntentStatus::Invalid;
    }
    return release_intents_.post(intent);
}

bool ScriptTaskSupervisor::take_native_release_intent(ScriptTaskNativeLeaseReleaseIntent& output) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return release_intents_.pop(output);
}

ScriptTaskTeardownResult ScriptTaskSupervisor::begin_teardown(const ScriptAppSession& session) {
    ScriptTaskTeardownResult result;
    result.session = session;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!sessions_.invalidate(session)) {
        result.session = {};
        return result;
    }
    retiring_session_ = session;
    result.discarded_input_packets = input_mailbox_.discard_all();
    result.discarded_frame_packets = frame_mailbox_.discard_all();
    result.discarded_service_request_packets = service_request_mailbox_.discard_all();
    result.discarded_fatal_packets = fatal_mailbox_.discard_all();
    result.cancelled_service_requests = services_.cancel_session(session);
    return result;
}

ScriptTaskTeardownResult ScriptTaskSupervisor::complete_teardown(const ScriptAppSession& retired_session) {
    ScriptTaskTeardownResult result;
    result.session = retired_session;
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!retired_session.valid() || retired_session != retiring_session_ ||
        sessions_.current_snapshot().valid()) {
        result.session = {};
        return result;
    }
    result.released_frame_leases = frame_leases_.release_session(retired_session);
    result.released_service_payload_leases = service_payload_leases_.release_session(retired_session);
    result.discarded_release_intents = release_intents_.discard_session(retired_session);
    result.retired_service_tombstones = services_.clear_session(retired_session);
    retiring_session_ = {};
    return result;
}

} // namespace jellyframe
