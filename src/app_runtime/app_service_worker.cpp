#include "app_runtime/app_service_worker.h"

namespace jellyframe {
namespace {

HostServiceCompletion normalize_completion(const HostServiceRequest& request,
                                           HostServiceCompletion completion) {
    completion.job_id = request.job_id;
    completion.kind = request.kind;
    completion.app_instance_id = request.app_instance_id;
    return completion;
}

} // namespace

AppHostServiceWorkerPumpResult pump_app_host_service_worker(
    AppRuntimeHost& host,
    const AppHostServiceWorkerPumpOptions& options,
    AppHostServiceWorker& worker) {
    AppHostServiceWorkerPumpResult result;
    if (options.max_requests == 0) {
        return result;
    }

    for (std::size_t index = 0; index < options.max_requests; ++index) {
        if (host.completions().full()) {
            result.completion_queue_full = true;
            break;
        }

        HostServiceRequest request;
        if (!host.pop_worker_request(options.kind, request)) {
            result.request_queue_empty = true;
            break;
        }

        ++result.requests_processed;
        const HostServiceCompletion completion = normalize_completion(request, worker.process(request));
        if (!host.push_completion(completion)) {
            result.completion_queue_full = true;
            break;
        }
        ++result.completions_posted;
    }
    return result;
}

} // namespace jellyframe
