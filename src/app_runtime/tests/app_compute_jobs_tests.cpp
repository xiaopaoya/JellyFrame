#include "app_runtime/app_compute_jobs.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "app_compute_jobs check failed: " << message << '\n';
        std::abort();
    }
}

AppRuntimeHost make_host(std::size_t handle_bytes = 256) {
    return AppRuntimeHost(AppRuntimeHostOptions{4, 2, 4, handle_bytes, 1});
}

std::vector<HostServiceCompletion> pump(AppRuntimeHost& host) {
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    return accepted;
}

void compute_job_requires_capability_and_returns_bounded_result() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.compute", AppRole::App);
    AppComputeJobMock jobs;
    check(jobs.submit(host, AppComputeJobRequest{"summarize", {1, 2}}).status ==
              AppServiceSubmitStatus::CapabilityDenied,
          "compute capability gate");

    jobs.set_policy(AppComputeJobPolicy{true, 8, 12, 2});
    check(jobs.add_fixture("summarize", AppComputeJobResult{HostServiceStatus::Completed, 0, {9, 8, 7}}),
          "compute fixture accepted");
    const AppServiceSubmitResult submitted = jobs.submit(host, AppComputeJobRequest{"summarize", {1, 2}, 200});
    check(submitted.accepted(), "compute submit accepted");
    check(jobs.complete_next(host), "compute completes worker request");

    const auto completions = pump(host);
    check(completions.size() == 1, "compute completion accepted");
    check(completions.front().kind == HostServiceJobKind::ComputeJob, "compute completion kind");
    check(completions.front().status == HostServiceStatus::Completed, "compute completion status");
    check(completions.front().handle != 0 && completions.front().byte_count == 3, "compute result handle bounded");
    const AppComputeResultRecord* record = jobs.result(completions.front().handle);
    check(record != nullptr && record->operation == "summarize" && record->output == std::vector<std::uint8_t>({9, 8, 7}),
          "compute result is isolated behind host handle");
    check(jobs.release_result(host, completions.front().handle), "compute result release");
}

void compute_job_enforces_input_job_and_result_budgets() {
    AppRuntimeHost host = make_host(8);
    host.launch("org.example.compute", AppRole::App);
    AppComputeJobMock jobs(AppComputeJobPolicy{true, 2, 3, 1});
    check(jobs.submit(host, AppComputeJobRequest{"", {}}).status == AppServiceSubmitStatus::InvalidInput,
          "empty compute operation rejected");
    check(jobs.submit(host, AppComputeJobRequest{"x", {1, 2, 3}}).status == AppServiceSubmitStatus::InvalidInput,
          "oversized compute input rejected");
    check(jobs.add_fixture("x", AppComputeJobResult{HostServiceStatus::Completed, 0, {1, 2, 3, 4}}) == false,
          "oversized fixture rejected early");
    check(jobs.add_fixture("x", AppComputeJobResult{HostServiceStatus::Completed, 0, {1, 2, 3}}),
          "bounded fixture accepted");
    check(jobs.submit(host, AppComputeJobRequest{"x", {1}}).accepted(), "first compute job accepted");
    check(jobs.submit(host, AppComputeJobRequest{"x", {1}}).status == AppServiceSubmitStatus::BudgetExceeded,
          "per-app compute job budget enforced");
    check(jobs.complete_next(host), "bounded result completion posted");
    const auto completions = pump(host);
    check(completions.size() == 1 && completions.front().status == HostServiceStatus::Completed,
          "bounded result survives completion");
}

void compute_job_cleans_stale_results_and_worker_jobs() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.compute.one", AppRole::App);
    AppComputeJobMock jobs(AppComputeJobPolicy{true, 8, 8, 2});
    check(jobs.add_fixture("x", AppComputeJobResult{HostServiceStatus::Completed, 0, {1}}), "fixture accepted");
    check(jobs.submit(host, AppComputeJobRequest{"x", {}}).accepted(), "first job submitted");
    check(jobs.complete_next(host), "first job completed");
    const auto first = pump(host);
    check(first.size() == 1 && first.front().handle != 0, "first result delivered");
    host.launch("org.example.compute.two", AppRole::App);
    check(jobs.collect_released_results(host) == 1, "old app compute handle collected");
    check(jobs.submit(host, AppComputeJobRequest{"x", {}}).accepted(), "second job submitted");
    HostServiceRequest request;
    check(host.pop_worker_request(HostServiceJobKind::ComputeJob, request), "worker owns second job");
    host.launch("org.example.compute.three", AppRole::App);
    check(jobs.collect_stale_pending_jobs(host) == 1, "worker-popped stale compute job collected");
}

void compute_policy_requires_manifest_and_host_approval() {
    HostDeviceCapabilities capabilities;
    capabilities.compute.supports_compute_jobs = true;
    capabilities.compute.max_compute_input_bytes = 32;
    capabilities.compute.max_compute_result_bytes = 64;
    capabilities.compute.max_compute_jobs_per_app = 2;
    const AppServiceHostProfile profile = app_service_host_profile_from_capabilities(capabilities);
    check(profile.allow_compute_jobs && profile.max_compute_input_bytes == 32, "host compute profile maps capabilities");
    AppServiceManifestCapabilities manifest;
    AppServicePolicies policies = app_service_policies_for_app(manifest, profile);
    check(!policies.compute.enabled, "compute policy denied without manifest request");
    manifest.compute_jobs = true;
    policies = app_service_policies_for_app(manifest, profile);
    check(policies.compute.enabled && policies.compute.max_result_bytes == 64 && policies.compute.max_jobs_per_app == 2,
          "compute policy requires both manifest and host approval");
}

} // namespace

int main() {
    compute_job_requires_capability_and_returns_bounded_result();
    compute_job_enforces_input_job_and_result_budgets();
    compute_job_cleans_stale_results_and_worker_jobs();
    compute_policy_requires_manifest_and_host_approval();
    return 0;
}
