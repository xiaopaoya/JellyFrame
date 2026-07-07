#include "app_runtime/host_services.h"

#include <algorithm>
#include <limits>

namespace jellyframe {

namespace {

constexpr std::uint32_t kSlotMask = 0x0000ffffu;
constexpr std::uint32_t kGenerationShift = 16u;

std::uint16_t next_generation(std::uint16_t generation) {
    ++generation;
    return generation == 0 ? 1 : generation;
}

} // namespace

HostServiceRequestQueue::HostServiceRequestQueue(std::size_t capacity)
    : capacity_(capacity) {
    requests_.reserve(capacity_);
}

HostServiceSubmitResult HostServiceRequestQueue::submit(HostServiceJobKind kind,
                                                        std::uint32_t app_instance_id,
                                                        std::uint32_t request_handle,
                                                        std::uint8_t priority,
                                                        std::uint32_t timeout_ms) {
    if (capacity_ == 0 || full()) {
        return HostServiceSubmitResult{false, 0, HostServiceStatus::BudgetExceeded};
    }
    const std::uint32_t job_id = next_job_id_++;
    if (next_job_id_ == 0) {
        next_job_id_ = 1;
    }
    requests_.push_back(HostServiceRequest{job_id, kind, app_instance_id, request_handle, timeout_ms, priority});
    return HostServiceSubmitResult{true, job_id, HostServiceStatus::Completed};
}

bool HostServiceRequestQueue::pop_next(HostServiceRequest& request) {
    if (requests_.empty()) {
        return false;
    }
    auto best = requests_.begin();
    for (auto it = requests_.begin() + 1; it != requests_.end(); ++it) {
        if (it->priority > best->priority) {
            best = it;
        }
    }
    request = *best;
    requests_.erase(best);
    return true;
}

bool HostServiceRequestQueue::pop_next(HostServiceJobKind kind, HostServiceRequest& request) {
    auto best = requests_.end();
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
        if (it->kind != kind) {
            continue;
        }
        if (best == requests_.end() || it->priority > best->priority) {
            best = it;
        }
    }
    if (best == requests_.end()) {
        return false;
    }
    request = *best;
    requests_.erase(best);
    return true;
}

bool HostServiceRequestQueue::cancel_pending(std::uint32_t job_id) {
    const auto it = std::find_if(requests_.begin(), requests_.end(), [job_id](const HostServiceRequest& request) {
        return request.job_id == job_id;
    });
    if (it == requests_.end()) {
        return false;
    }
    requests_.erase(it);
    return true;
}

std::size_t HostServiceRequestQueue::cancel_app_instance(std::uint32_t app_instance_id) {
    const auto old_size = requests_.size();
    requests_.erase(std::remove_if(requests_.begin(),
                                   requests_.end(),
                                   [app_instance_id](const HostServiceRequest& request) {
                                       return request.app_instance_id == app_instance_id;
                                   }),
                    requests_.end());
    return old_size - requests_.size();
}

void HostServiceRequestQueue::clear() {
    requests_.clear();
}

HostServiceCompletionQueue::HostServiceCompletionQueue(std::size_t capacity)
    : capacity_(capacity) {
    completions_.resize(capacity_);
}

bool HostServiceCompletionQueue::push(const HostServiceCompletion& completion) {
    if (capacity_ == 0 || full()) {
        return false;
    }
    completions_[(head_ + size_) % capacity_] = completion;
    ++size_;
    return true;
}

std::size_t HostServiceCompletionQueue::pop(std::size_t max_count, std::vector<HostServiceCompletion>& output) {
    const std::size_t count = std::min(max_count, size_);
    output.reserve(output.size() + count);
    for (std::size_t i = 0; i < count; ++i) {
        output.push_back(completions_[(head_ + i) % capacity_]);
    }
    if (count > 0) {
        head_ = (head_ + count) % capacity_;
        size_ -= count;
        if (size_ == 0) {
            head_ = 0;
        }
    }
    return count;
}

std::size_t HostServiceCompletionQueue::discard_app_instance(std::uint32_t app_instance_id) {
    const std::size_t old_size = size_;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < old_size; ++i) {
        const HostServiceCompletion completion = completions_[(head_ + i) % capacity_];
        if (completion.app_instance_id == app_instance_id) {
            continue;
        }
        completions_[(head_ + kept) % capacity_] = completion;
        ++kept;
    }
    size_ = kept;
    if (size_ == 0) {
        head_ = 0;
    }
    return old_size - kept;
}

void HostServiceCompletionQueue::clear() {
    head_ = 0;
    size_ = 0;
}

HostHandleTable::HostHandleTable(std::size_t capacity, std::size_t byte_budget)
    : capacity_(std::min<std::size_t>(capacity, kSlotMask)),
      byte_budget_(byte_budget) {
    slots_.resize(capacity_);
}

std::uint32_t HostHandleTable::allocate(HostServiceHandleKind kind,
                                        std::uint32_t app_instance_id,
                                        std::uint32_t bytes,
                                        void* payload) {
    if (kind == HostServiceHandleKind::None || capacity_ == 0 || active_count_ >= capacity_) {
        return 0;
    }
    if (byte_budget_ > 0 && static_cast<std::size_t>(bytes) > byte_budget_ - used_bytes_) {
        return 0;
    }
    std::size_t i = next_free_hint_;
    for (std::size_t offset = 0; offset < slots_.size(); ++offset) {
        Slot& slot = slots_[i];
        if (slot.active) {
            ++i;
            if (i == slots_.size()) {
                i = 0;
            }
            continue;
        }
        slot.active = true;
        slot.info = HostHandleInfo{kind, app_instance_id, bytes, payload};
        used_bytes_ += bytes;
        ++active_count_;
        next_free_hint_ = i + 1;
        if (next_free_hint_ == slots_.size()) {
            next_free_hint_ = 0;
        }
        return make_handle(i, slot.generation);
    }
    return 0;
}

bool HostHandleTable::release(std::uint32_t handle) {
    Slot* slot = slot_for_handle(handle);
    if (slot == nullptr) {
        return false;
    }
    slot->active = false;
    used_bytes_ -= slot->info.bytes;
    slot->info = {};
    slot->generation = next_generation(slot->generation);
    --active_count_;
    next_free_hint_ = slot_index_from_handle(handle);
    return true;
}

const HostHandleInfo* HostHandleTable::lookup(std::uint32_t handle) const {
    const Slot* slot = slot_for_handle(handle);
    return slot == nullptr ? nullptr : &slot->info;
}

HostHandleInfo* HostHandleTable::lookup(std::uint32_t handle) {
    Slot* slot = slot_for_handle(handle);
    return slot == nullptr ? nullptr : &slot->info;
}

std::size_t HostHandleTable::release_app_instance(std::uint32_t app_instance_id) {
    std::size_t released = 0;
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        Slot& slot = slots_[i];
        if (!slot.active || slot.info.app_instance_id != app_instance_id) {
            continue;
        }
        used_bytes_ -= slot.info.bytes;
        slot.active = false;
        slot.info = {};
        slot.generation = next_generation(slot.generation);
        --active_count_;
        if (released == 0) {
            next_free_hint_ = i;
        }
        ++released;
    }
    return released;
}

void HostHandleTable::clear() {
    for (Slot& slot : slots_) {
        if (slot.active) {
            slot.generation = next_generation(slot.generation);
        }
        slot.active = false;
        slot.info = {};
    }
    used_bytes_ = 0;
    active_count_ = 0;
    next_free_hint_ = 0;
}

std::uint32_t HostHandleTable::make_handle(std::size_t slot_index, std::uint16_t generation) {
    if (slot_index >= kSlotMask) {
        return 0;
    }
    return (static_cast<std::uint32_t>(generation) << kGenerationShift) |
           static_cast<std::uint32_t>(slot_index + 1);
}

std::size_t HostHandleTable::slot_index_from_handle(std::uint32_t handle) {
    const std::uint32_t raw = handle & kSlotMask;
    return raw == 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(raw - 1);
}

std::uint16_t HostHandleTable::generation_from_handle(std::uint32_t handle) {
    return static_cast<std::uint16_t>(handle >> kGenerationShift);
}

HostHandleTable::Slot* HostHandleTable::slot_for_handle(std::uint32_t handle) {
    const std::size_t index = slot_index_from_handle(handle);
    if (index >= slots_.size()) {
        return nullptr;
    }
    Slot& slot = slots_[index];
    if (!slot.active || slot.generation != generation_from_handle(handle)) {
        return nullptr;
    }
    return &slot;
}

const HostHandleTable::Slot* HostHandleTable::slot_for_handle(std::uint32_t handle) const {
    const std::size_t index = slot_index_from_handle(handle);
    if (index >= slots_.size()) {
        return nullptr;
    }
    const Slot& slot = slots_[index];
    if (!slot.active || slot.generation != generation_from_handle(handle)) {
        return nullptr;
    }
    return &slot;
}

HostServiceRequestQueue host_service_request_queue_from_capabilities(const HostAsyncCapabilities& capabilities) {
    return HostServiceRequestQueue(capabilities.max_in_flight_jobs);
}

HostServiceCompletionQueue host_service_completion_queue_from_capabilities(const HostAsyncCapabilities& capabilities) {
    return HostServiceCompletionQueue(std::max(capabilities.max_in_flight_jobs,
                                               capabilities.max_completion_events_per_frame));
}

} // namespace jellyframe
