#pragma once

#include "render_core/host.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

enum class HostServiceJobKind {
    ImageDecode,
    AudioCommand,
    VideoFrameDecode,
    NetworkFetch,
    StorageKv,
    BundleInstall,
    BundleRemove,
    Other,
};

enum class HostServiceStatus {
    Completed,
    Failed,
    Cancelled,
    Unsupported,
    BudgetExceeded,
    Timeout,
};

enum class HostServiceHandleKind {
    None,
    Surface,
    AudioStream,
    FetchResponse,
    StorageValue,
    BundleRecord,
    Other,
};

struct HostServiceRequest {
    std::uint32_t job_id = 0;
    HostServiceJobKind kind = HostServiceJobKind::Other;
    std::uint32_t app_instance_id = 0;
    std::uint32_t request_handle = 0;
    std::uint32_t timeout_ms = 0;
    std::uint8_t priority = 0;
};

struct HostServiceCompletion {
    std::uint32_t job_id = 0;
    HostServiceJobKind kind = HostServiceJobKind::Other;
    HostServiceStatus status = HostServiceStatus::Failed;
    std::uint32_t app_instance_id = 0;
    std::uint32_t handle = 0;
    std::uint32_t error_code = 0;
    std::uint32_t byte_count = 0;
};

struct HostServiceSubmitResult {
    bool accepted = false;
    std::uint32_t job_id = 0;
    HostServiceStatus rejected_status = HostServiceStatus::BudgetExceeded;
};

class HostServiceRequestQueue {
public:
    explicit HostServiceRequestQueue(std::size_t capacity = 0);

    HostServiceSubmitResult submit(HostServiceJobKind kind,
                                   std::uint32_t app_instance_id,
                                   std::uint32_t request_handle = 0,
                                   std::uint8_t priority = 0,
                                   std::uint32_t timeout_ms = 0);

    bool pop_next(HostServiceRequest& request);
    bool pop_next(HostServiceJobKind kind, HostServiceRequest& request);
    bool cancel_pending(std::uint32_t job_id);
    std::size_t cancel_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t size() const { return requests_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return requests_.empty(); }
    bool full() const { return requests_.size() >= capacity_; }

private:
    std::size_t capacity_ = 0;
    std::uint32_t next_job_id_ = 1;
    std::vector<HostServiceRequest> requests_;
};

class HostServiceCompletionQueue {
public:
    explicit HostServiceCompletionQueue(std::size_t capacity = 0);

    bool push(const HostServiceCompletion& completion);
    std::size_t pop(std::size_t max_count, std::vector<HostServiceCompletion>& output);
    std::size_t discard_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t size() const { return completions_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool empty() const { return completions_.empty(); }
    bool full() const { return completions_.size() >= capacity_; }

private:
    std::size_t capacity_ = 0;
    std::vector<HostServiceCompletion> completions_;
};

struct HostHandleInfo {
    HostServiceHandleKind kind = HostServiceHandleKind::None;
    std::uint32_t app_instance_id = 0;
    std::uint32_t bytes = 0;
    void* payload = nullptr;
};

class HostHandleTable {
public:
    explicit HostHandleTable(std::size_t capacity = 0, std::size_t byte_budget = 0);

    std::uint32_t allocate(HostServiceHandleKind kind,
                           std::uint32_t app_instance_id,
                           std::uint32_t bytes = 0,
                           void* payload = nullptr);
    bool release(std::uint32_t handle);
    const HostHandleInfo* lookup(std::uint32_t handle) const;
    HostHandleInfo* lookup(std::uint32_t handle);
    std::size_t release_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t active_count() const { return active_count_; }
    std::size_t capacity() const { return capacity_; }
    std::size_t used_bytes() const { return used_bytes_; }
    std::size_t byte_budget() const { return byte_budget_; }

private:
    struct Slot {
        HostHandleInfo info;
        std::uint16_t generation = 1;
        bool active = false;
    };

    static std::uint32_t make_handle(std::size_t slot_index, std::uint16_t generation);
    static std::size_t slot_index_from_handle(std::uint32_t handle);
    static std::uint16_t generation_from_handle(std::uint32_t handle);
    Slot* slot_for_handle(std::uint32_t handle);
    const Slot* slot_for_handle(std::uint32_t handle) const;

    std::size_t capacity_ = 0;
    std::size_t byte_budget_ = 0;
    std::size_t used_bytes_ = 0;
    std::size_t active_count_ = 0;
    std::vector<Slot> slots_;
};

inline HostServiceCompletion make_cancelled_completion(const HostServiceRequest& request) {
    return HostServiceCompletion{
        request.job_id,
        request.kind,
        HostServiceStatus::Cancelled,
        request.app_instance_id,
        0,
        0,
        0,
    };
}

HostServiceRequestQueue host_service_request_queue_from_capabilities(const HostAsyncCapabilities& capabilities);
HostServiceCompletionQueue host_service_completion_queue_from_capabilities(const HostAsyncCapabilities& capabilities);

} // namespace jellyframe
