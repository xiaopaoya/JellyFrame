#pragma once

#include "app_runtime/app_host.h"
#include "app_runtime/app_services.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

enum class AppComputeFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidOperation,
    InputBudgetExceeded,
    QueueFull,
    JobBudgetExceeded,
    ResultBudgetExceeded,
    ResultHandleBudgetExceeded,
    JobFailed,
    JobCancelled,
    JobTimeout,
    Unsupported,
    Unknown,
};

struct AppComputeJobRequest {
    std::string operation;
    std::vector<std::uint8_t> input;
    std::uint32_t timeout_ms = 0;
};

struct AppComputeJobResult {
    HostServiceStatus status = HostServiceStatus::Failed;
    std::uint32_t error_code = 0;
    std::vector<std::uint8_t> output;
};

struct AppComputeResultRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::string operation;
    std::vector<std::uint8_t> output;
};

const char* app_compute_failure_reason_name(AppComputeFailureReason reason);
AppComputeFailureReason classify_app_compute_failure(AppServiceSubmitStatus submit_status,
                                                     HostServiceStatus host_status,
                                                     std::uint32_t error_code);
std::string app_compute_failure_detail(const std::string& operation,
                                       AppServiceSubmitStatus submit_status,
                                       HostServiceStatus host_status,
                                       std::uint32_t error_code);

class AppComputeJobMock {
public:
    explicit AppComputeJobMock(AppComputeJobPolicy policy = {});

    void set_policy(AppComputeJobPolicy policy);
    bool add_fixture(std::string operation, AppComputeJobResult result);
    AppServiceSubmitResult submit(AppRuntimeHost& host, AppComputeJobRequest request);
    HostServiceCompletion complete_request(AppRuntimeHost& host, const HostServiceRequest& request);
    bool complete_next(AppRuntimeHost& host);
    const AppComputeResultRecord* result(std::uint32_t handle) const;
    bool release_result(AppRuntimeHost& host, std::uint32_t handle);
    std::size_t collect_released_results(const AppRuntimeHost& host);
    std::size_t collect_stale_pending_jobs(const AppRuntimeHost& host);
    void clear();

private:
    struct PendingJob {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        std::string operation;
        AppComputeJobResult result;
    };

    std::size_t active_result_count(std::uint32_t app_instance_id = 0) const;
    std::size_t pending_job_count(std::uint32_t app_instance_id = 0) const;

    AppComputeJobPolicy policy_;
    std::vector<std::pair<std::string, AppComputeJobResult>> fixtures_;
    std::vector<PendingJob> pending_;
    std::vector<AppComputeResultRecord> records_;
};

} // namespace jellyframe
