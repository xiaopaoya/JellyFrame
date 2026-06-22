#pragma once

#include "app_runtime/app_host.h"

#include <cstddef>

namespace jellyframe {

class AppHostServiceWorker {
public:
    virtual ~AppHostServiceWorker() = default;
    virtual HostServiceCompletion process(const HostServiceRequest& request) = 0;
};

struct AppHostServiceWorkerPumpOptions {
    HostServiceJobKind kind = HostServiceJobKind::Other;
    std::size_t max_requests = 1;
};

struct AppHostServiceWorkerPumpResult {
    std::size_t requests_processed = 0;
    std::size_t completions_posted = 0;
    bool request_queue_empty = false;
    bool completion_queue_full = false;
};

AppHostServiceWorkerPumpResult pump_app_host_service_worker(
    AppRuntimeHost& host,
    const AppHostServiceWorkerPumpOptions& options,
    AppHostServiceWorker& worker);

struct AppHostServiceWorkerSlot {
    HostServiceJobKind kind = HostServiceJobKind::Other;
    std::size_t max_requests = 1;
    AppHostServiceWorker* worker = nullptr;
};

struct AppHostServiceWorkerGroupPumpResult {
    std::size_t workers_considered = 0;
    std::size_t workers_pumped = 0;
    std::size_t empty_workers = 0;
    std::size_t requests_processed = 0;
    std::size_t completions_posted = 0;
    bool completion_queue_full = false;
};

AppHostServiceWorkerGroupPumpResult pump_app_host_service_workers(
    AppRuntimeHost& host,
    const AppHostServiceWorkerSlot* slots,
    std::size_t slot_count);

} // namespace jellyframe
