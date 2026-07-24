#include "app_runtime/app_compute_jobs.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace jellyframe {
namespace {

constexpr std::uint32_t kComputeErrorInvalidOperation = 1;
constexpr std::uint32_t kComputeErrorInputBudget = 2;
constexpr std::uint32_t kComputeErrorJobBudget = 3;
constexpr std::uint32_t kComputeErrorResultBudget = 4;
constexpr std::uint32_t kComputeErrorResultHandleBudget = 5;
constexpr std::uint32_t kComputeErrorOperationNotFound = 6;

AppServiceSubmitResult rejected(AppServiceSubmitStatus status,
                                HostServiceStatus host_status,
                                std::uint32_t error_code) {
    return AppServiceSubmitResult{status, 0, host_status, error_code};
}

AppServiceSubmitResult from_submit(const HostServiceSubmitResult& result) {
    if (!result.accepted) {
        return AppServiceSubmitResult{AppServiceSubmitStatus::QueueFull, 0, result.rejected_status, 0};
    }
    return AppServiceSubmitResult{AppServiceSubmitStatus::Accepted, result.job_id, HostServiceStatus::Completed, 0};
}

} // namespace

const char* app_compute_failure_reason_name(AppComputeFailureReason reason) {
    switch (reason) {
    case AppComputeFailureReason::None: return "none";
    case AppComputeFailureReason::EmptyInstance: return "empty-instance";
    case AppComputeFailureReason::CapabilityDenied: return "capability-denied";
    case AppComputeFailureReason::InvalidOperation: return "invalid-operation";
    case AppComputeFailureReason::InputBudgetExceeded: return "input-budget-exceeded";
    case AppComputeFailureReason::QueueFull: return "queue-full";
    case AppComputeFailureReason::JobBudgetExceeded: return "job-budget-exceeded";
    case AppComputeFailureReason::ResultBudgetExceeded: return "result-budget-exceeded";
    case AppComputeFailureReason::ResultHandleBudgetExceeded: return "result-handle-budget-exceeded";
    case AppComputeFailureReason::JobFailed: return "job-failed";
    case AppComputeFailureReason::JobCancelled: return "job-cancelled";
    case AppComputeFailureReason::JobTimeout: return "job-timeout";
    case AppComputeFailureReason::Unsupported: return "unsupported";
    case AppComputeFailureReason::Unknown: return "unknown";
    }
    return "unknown";
}

AppComputeFailureReason classify_app_compute_failure(AppServiceSubmitStatus submit_status,
                                                     HostServiceStatus host_status,
                                                     std::uint32_t error_code) {
    if (submit_status != AppServiceSubmitStatus::Accepted) {
        switch (submit_status) {
        case AppServiceSubmitStatus::EmptyInstance: return AppComputeFailureReason::EmptyInstance;
        case AppServiceSubmitStatus::CapabilityDenied: return AppComputeFailureReason::CapabilityDenied;
        case AppServiceSubmitStatus::InvalidInput:
            return error_code == kComputeErrorInputBudget
                ? AppComputeFailureReason::InputBudgetExceeded
                : AppComputeFailureReason::InvalidOperation;
        case AppServiceSubmitStatus::QueueFull: return AppComputeFailureReason::QueueFull;
        case AppServiceSubmitStatus::BudgetExceeded: return AppComputeFailureReason::JobBudgetExceeded;
        case AppServiceSubmitStatus::Accepted: break;
        }
    }
    switch (host_status) {
    case HostServiceStatus::Completed: return AppComputeFailureReason::None;
    case HostServiceStatus::Cancelled: return AppComputeFailureReason::JobCancelled;
    case HostServiceStatus::Timeout: return AppComputeFailureReason::JobTimeout;
    case HostServiceStatus::Unsupported: return AppComputeFailureReason::Unsupported;
    case HostServiceStatus::BudgetExceeded:
        return error_code == kComputeErrorResultBudget
            ? AppComputeFailureReason::ResultBudgetExceeded
            : AppComputeFailureReason::ResultHandleBudgetExceeded;
    case HostServiceStatus::Failed: return AppComputeFailureReason::JobFailed;
    }
    return AppComputeFailureReason::Unknown;
}

std::string app_compute_failure_detail(const std::string& operation,
                                       AppServiceSubmitStatus submit_status,
                                       HostServiceStatus host_status,
                                       std::uint32_t error_code) {
    return "operation=" + operation + " reason=" +
        app_compute_failure_reason_name(classify_app_compute_failure(submit_status, host_status, error_code));
}

AppComputeJobMock::AppComputeJobMock(AppComputeJobPolicy policy) : policy_(policy) {}

void AppComputeJobMock::set_policy(AppComputeJobPolicy policy) {
    policy_ = policy;
    clear();
}

bool AppComputeJobMock::add_fixture(std::string operation, AppComputeJobResult result) {
    if (operation.empty() || result.output.size() > policy_.max_result_bytes) {
        return false;
    }
    fixtures_.push_back(std::make_pair(std::move(operation), std::move(result)));
    return true;
}

AppServiceSubmitResult AppComputeJobMock::submit(AppRuntimeHost& host, AppComputeJobRequest request) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled, 0);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported, 0);
    }
    if (request.operation.empty()) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed, kComputeErrorInvalidOperation);
    }
    if (request.input.size() > policy_.max_input_bytes) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::BudgetExceeded, kComputeErrorInputBudget);
    }
    const std::uint32_t instance = host.current_app_instance_id();
    if (policy_.max_jobs_per_app != 0 &&
        pending_job_count(instance) + active_result_count(instance) >= policy_.max_jobs_per_app) {
        return rejected(AppServiceSubmitStatus::BudgetExceeded, HostServiceStatus::BudgetExceeded, kComputeErrorJobBudget);
    }

    const HostServiceSubmitResult submitted = host.submit_current(HostServiceJobKind::ComputeJob,
                                                                  0,
                                                                  0,
                                                                  request.timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingJob pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = instance;
    pending.operation = std::move(request.operation);
    const auto fixture = std::find_if(fixtures_.begin(), fixtures_.end(), [&pending](const auto& item) {
        return item.first == pending.operation;
    });
    if (fixture == fixtures_.end()) {
        pending.result.status = HostServiceStatus::Failed;
        pending.result.error_code = kComputeErrorOperationNotFound;
    } else {
        pending.result = fixture->second;
    }
    pending_.push_back(std::move(pending));
    return result;
}

bool AppComputeJobMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::ComputeJob, request)) {
        return false;
    }
    return host.push_completion(complete_request(host, request));
}

HostServiceCompletion AppComputeJobMock::complete_request(AppRuntimeHost& host,
                                                           const HostServiceRequest& request) {
    const auto pending = std::find_if(pending_.begin(), pending_.end(), [&request](const PendingJob& item) {
        return item.job_id == request.job_id;
    });
    if (pending == pending_.end()) {
        return make_cancelled_completion(request);
    }
    HostServiceCompletion completion{request.job_id, HostServiceJobKind::ComputeJob, pending->result.status,
                                     request.app_instance_id, 0, pending->result.error_code, 0};
    if (completion.status == HostServiceStatus::Completed) {
        if (pending->result.output.size() > policy_.max_result_bytes) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = kComputeErrorResultBudget;
        } else if (pending->result.output.size() > std::numeric_limits<std::uint32_t>::max()) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = kComputeErrorResultBudget;
        } else {
            const std::uint32_t bytes = static_cast<std::uint32_t>(pending->result.output.size());
            const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::ComputeResult,
                                                                 request.app_instance_id, bytes);
            if (handle == 0) {
                completion.status = HostServiceStatus::BudgetExceeded;
                completion.error_code = kComputeErrorResultHandleBudget;
            } else {
                completion.handle = handle;
                completion.byte_count = bytes;
                records_.push_back(AppComputeResultRecord{handle, request.app_instance_id,
                                                           pending->operation, pending->result.output});
            }
        }
    }
    pending_.erase(pending);
    return completion;
}

const AppComputeResultRecord* AppComputeJobMock::result(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppComputeResultRecord& record) {
        return record.handle == handle;
    });
    return found == records_.end() ? nullptr : &*found;
}

bool AppComputeJobMock::release_result(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppComputeResultRecord& record) {
        return record.handle == handle;
    });
    if (found == records_.end() || !host.handles().release(handle)) {
        return false;
    }
    records_.erase(found);
    return true;
}

std::size_t AppComputeJobMock::collect_released_results(const AppRuntimeHost& host) {
    const std::size_t before = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(), [&host](const AppComputeResultRecord& record) {
        return !host.handles().contains(record.handle);
    }), records_.end());
    return before - records_.size();
}

std::size_t AppComputeJobMock::collect_stale_pending_jobs(const AppRuntimeHost& host) {
    const std::uint32_t current = host.current_app_instance_id();
    const std::size_t before = pending_.size();
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [current](const PendingJob& item) {
        return item.app_instance_id != current;
    }), pending_.end());
    return before - pending_.size();
}

std::size_t AppComputeJobMock::active_result_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(records_.begin(), records_.end(), [app_instance_id](const AppComputeResultRecord& record) {
        return app_instance_id == 0 || record.app_instance_id == app_instance_id;
    }));
}

std::size_t AppComputeJobMock::pending_job_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(pending_.begin(), pending_.end(), [app_instance_id](const PendingJob& job) {
        return app_instance_id == 0 || job.app_instance_id == app_instance_id;
    }));
}

void AppComputeJobMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
}

} // namespace jellyframe
