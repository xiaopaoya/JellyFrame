#include "app_runtime/app_device_services.h"

#include <algorithm>

namespace jellyframe {
namespace {

constexpr std::uint32_t kDeviceErrorUnavailable = 404;
constexpr std::uint32_t kDeviceErrorGone = 410;
constexpr std::uint32_t kDeviceErrorBudgetExceeded = 507;

AppServiceSubmitResult rejected(AppServiceSubmitStatus status,
                                HostServiceStatus host_status,
                                std::uint32_t error_code = 0) {
    return AppServiceSubmitResult{status, 0, host_status, error_code};
}

AppServiceSubmitResult from_submit(const HostServiceSubmitResult& result) {
    if (!result.accepted) {
        return AppServiceSubmitResult{AppServiceSubmitStatus::QueueFull, 0, result.rejected_status, 0};
    }
    return AppServiceSubmitResult{AppServiceSubmitStatus::Accepted, result.job_id, HostServiceStatus::Completed, 0};
}

template <typename T>
typename std::vector<T>::iterator find_job(std::vector<T>& items, std::uint32_t job_id) {
    return std::find_if(items.begin(), items.end(), [job_id](const T& item) {
        return item.job_id == job_id;
    });
}

bool valid_location(const AppLocationSnapshotFixture& fixture) {
    return fixture.latitude >= -90.0 && fixture.latitude <= 90.0 &&
        fixture.longitude >= -180.0 && fixture.longitude <= 180.0;
}

} // namespace

const char* app_sensor_kind_name(AppSensorKind kind) {
    switch (kind) {
    case AppSensorKind::Accelerometer:
        return "accelerometer";
    case AppSensorKind::Gyroscope:
        return "gyroscope";
    case AppSensorKind::HeartRate:
        return "heart-rate";
    case AppSensorKind::AmbientLight:
        return "ambient-light";
    }
    return "unknown";
}

const char* app_device_failure_reason_name(AppDeviceFailureReason reason) {
    switch (reason) {
    case AppDeviceFailureReason::None:
        return "none";
    case AppDeviceFailureReason::EmptyInstance:
        return "empty-instance";
    case AppDeviceFailureReason::CapabilityDenied:
        return "capability-denied";
    case AppDeviceFailureReason::InvalidRequest:
        return "invalid-request";
    case AppDeviceFailureReason::QueueFull:
        return "queue-full";
    case AppDeviceFailureReason::SampleUnavailable:
        return "sample-unavailable";
    case AppDeviceFailureReason::RecordBudgetExceeded:
        return "record-budget-exceeded";
    case AppDeviceFailureReason::HandleBudgetExceeded:
        return "handle-budget-exceeded";
    case AppDeviceFailureReason::RequestFailed:
        return "request-failed";
    case AppDeviceFailureReason::RequestCancelled:
        return "request-cancelled";
    case AppDeviceFailureReason::RequestTimeout:
        return "request-timeout";
    case AppDeviceFailureReason::Unsupported:
        return "unsupported";
    case AppDeviceFailureReason::Unknown:
        return "unknown";
    }
    return "unknown";
}

AppSensorSamplePolicy app_sensor_sample_policy_from_service_policies(const AppServicePolicies& policies) {
    return AppSensorSamplePolicy{
        policies.sensor_accelerometer,
        policies.sensor_gyroscope,
        policies.sensor_heart_rate,
        policies.sensor_ambient_light,
        policies.max_sensor_sample_records,
    };
}

AppLocationSnapshotPolicy app_location_snapshot_policy_from_service_policies(const AppServicePolicies& policies) {
    return AppLocationSnapshotPolicy{
        policies.location_position,
        policies.max_location_snapshot_records,
    };
}

AppDeviceFailureReason classify_app_device_failure(AppServiceSubmitStatus submit_status,
                                                   HostServiceStatus host_status,
                                                   std::uint32_t error_code) {
    if (submit_status != AppServiceSubmitStatus::Accepted) {
        switch (submit_status) {
        case AppServiceSubmitStatus::Accepted:
            break;
        case AppServiceSubmitStatus::EmptyInstance:
            return AppDeviceFailureReason::EmptyInstance;
        case AppServiceSubmitStatus::CapabilityDenied:
            return AppDeviceFailureReason::CapabilityDenied;
        case AppServiceSubmitStatus::InvalidInput:
            return AppDeviceFailureReason::InvalidRequest;
        case AppServiceSubmitStatus::QueueFull:
            return AppDeviceFailureReason::QueueFull;
        case AppServiceSubmitStatus::BudgetExceeded:
            return AppDeviceFailureReason::RecordBudgetExceeded;
        }
    }

    switch (host_status) {
    case HostServiceStatus::Completed:
        return AppDeviceFailureReason::None;
    case HostServiceStatus::Unsupported:
        return AppDeviceFailureReason::Unsupported;
    case HostServiceStatus::Cancelled:
        return AppDeviceFailureReason::RequestCancelled;
    case HostServiceStatus::Timeout:
        return AppDeviceFailureReason::RequestTimeout;
    case HostServiceStatus::BudgetExceeded:
        return error_code == kDeviceErrorBudgetExceeded
            ? AppDeviceFailureReason::HandleBudgetExceeded
            : AppDeviceFailureReason::RecordBudgetExceeded;
    case HostServiceStatus::Failed:
        if (error_code == kDeviceErrorUnavailable) {
            return AppDeviceFailureReason::SampleUnavailable;
        }
        if (error_code == kDeviceErrorGone) {
            return AppDeviceFailureReason::InvalidRequest;
        }
        return AppDeviceFailureReason::RequestFailed;
    }
    return AppDeviceFailureReason::Unknown;
}

AppSensorSampleMock::AppSensorSampleMock(AppSensorSamplePolicy policy)
    : policy_(policy) {}

void AppSensorSampleMock::set_policy(AppSensorSamplePolicy policy) {
    policy_ = policy;
}

bool AppSensorSampleMock::enabled(AppSensorKind kind) const {
    switch (kind) {
    case AppSensorKind::Accelerometer:
        return policy_.accelerometer;
    case AppSensorKind::Gyroscope:
        return policy_.gyroscope;
    case AppSensorKind::HeartRate:
        return policy_.heart_rate;
    case AppSensorKind::AmbientLight:
        return policy_.ambient_light;
    }
    return false;
}

bool AppSensorSampleMock::add_fixture(AppSensorSampleFixture fixture) {
    fixtures_.push_back(fixture);
    return true;
}

std::size_t AppSensorSampleMock::active_record_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(records_.begin(),
                                                  records_.end(),
                                                  [app_instance_id](const AppSensorSampleRecord& record) {
                                                      return app_instance_id == 0 ||
                                                          record.app_instance_id == app_instance_id;
                                                  }));
}

std::size_t AppSensorSampleMock::pending_record_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(pending_.begin(),
                                                  pending_.end(),
                                                  [app_instance_id](const PendingSample& pending) {
                                                      return app_instance_id == 0 ||
                                                          pending.app_instance_id == app_instance_id;
                                                  }));
}

AppServiceSubmitResult AppSensorSampleMock::submit_sample(AppRuntimeHost& host,
                                                          AppSensorKind kind,
                                                          std::uint32_t timeout_ms) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!enabled(kind)) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (policy_.max_records != 0 &&
        active_record_count(host.current_app_instance_id()) + pending_record_count(host.current_app_instance_id()) >=
            policy_.max_records) {
        return rejected(AppServiceSubmitStatus::BudgetExceeded,
                        HostServiceStatus::BudgetExceeded,
                        kDeviceErrorBudgetExceeded);
    }

    const HostServiceSubmitResult submitted =
        host.submit_current(HostServiceJobKind::SensorSample, 0, 0, timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingSample pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = host.current_app_instance_id();
    const auto found = std::find_if(fixtures_.begin(), fixtures_.end(), [kind](const AppSensorSampleFixture& item) {
        return item.kind == kind;
    });
    if (found == fixtures_.end()) {
        pending.status = HostServiceStatus::Failed;
        pending.error_code = kDeviceErrorUnavailable;
        pending.fixture.kind = kind;
    } else {
        pending.status = HostServiceStatus::Completed;
        pending.fixture = *found;
    }
    pending_.push_back(pending);
    return result;
}

bool AppSensorSampleMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::SensorSample, request)) {
        return false;
    }
    return host.push_completion(complete_request(host, request));
}

HostServiceCompletion AppSensorSampleMock::complete_request(AppRuntimeHost& host,
                                                            const HostServiceRequest& request) {
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return make_cancelled_completion(request);
    }

    HostServiceCompletion completion{
        request.job_id,
        HostServiceJobKind::SensorSample,
        pending->status,
        request.app_instance_id,
        0,
        pending->error_code,
        0,
    };
    if (pending->status == HostServiceStatus::Completed) {
        const std::uint32_t bytes = static_cast<std::uint32_t>(sizeof(AppSensorSampleRecord));
        const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::SensorSample,
                                                            request.app_instance_id,
                                                            bytes);
        if (handle == 0) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = kDeviceErrorBudgetExceeded;
        } else {
            completion.handle = handle;
            completion.byte_count = bytes;
            records_.push_back(AppSensorSampleRecord{
                handle,
                request.app_instance_id,
                pending->fixture.kind,
                pending->fixture.timestamp_ms,
                pending->fixture.x,
                pending->fixture.y,
                pending->fixture.z,
                pending->fixture.value,
                pending->fixture.accuracy,
            });
        }
    }
    pending_.erase(pending);
    return completion;
}

const AppSensorSampleRecord* AppSensorSampleMock::sample(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppSensorSampleRecord& record) {
        return record.handle == handle;
    });
    return found == records_.end() ? nullptr : &*found;
}

bool AppSensorSampleMock::release_sample(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppSensorSampleRecord& record) {
        return record.handle == handle;
    });
    if (found == records_.end()) {
        return false;
    }
    records_.erase(found);
    return host.handles().release(handle);
}

std::size_t AppSensorSampleMock::release_app_samples(AppRuntimeHost& host, std::uint32_t app_instance_id) {
    std::vector<std::uint32_t> handles;
    handles.reserve(records_.size());
    for (const AppSensorSampleRecord& record : records_) {
        if (record.app_instance_id == app_instance_id) {
            handles.push_back(record.handle);
        }
    }
    for (std::uint32_t handle : handles) {
        release_sample(host, handle);
    }
    return handles.size();
}

std::size_t AppSensorSampleMock::collect_released_samples(const AppRuntimeHost& host) {
    const auto old_size = records_.size();
    records_.erase(std::remove_if(records_.begin(),
                                  records_.end(),
                                  [&host](const AppSensorSampleRecord& record) {
                                      return host.handles().lookup(record.handle) == nullptr;
                                  }),
                   records_.end());
    return old_size - records_.size();
}

void AppSensorSampleMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
}

AppLocationSnapshotMock::AppLocationSnapshotMock(AppLocationSnapshotPolicy policy)
    : policy_(policy) {}

void AppLocationSnapshotMock::set_policy(AppLocationSnapshotPolicy policy) {
    policy_ = policy;
}

bool AppLocationSnapshotMock::set_fixture(AppLocationSnapshotFixture fixture) {
    if (!valid_location(fixture)) {
        return false;
    }
    fixture_ = fixture;
    has_fixture_ = true;
    return true;
}

std::size_t AppLocationSnapshotMock::active_record_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(records_.begin(),
                                                  records_.end(),
                                                  [app_instance_id](const AppLocationSnapshotRecord& record) {
                                                      return app_instance_id == 0 ||
                                                          record.app_instance_id == app_instance_id;
                                                  }));
}

std::size_t AppLocationSnapshotMock::pending_record_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(pending_.begin(),
                                                  pending_.end(),
                                                  [app_instance_id](const PendingSnapshot& pending) {
                                                      return app_instance_id == 0 ||
                                                          pending.app_instance_id == app_instance_id;
                                                  }));
}

AppServiceSubmitResult AppLocationSnapshotMock::submit_position(AppRuntimeHost& host, std::uint32_t timeout_ms) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!policy_.position) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (policy_.max_records != 0 &&
        active_record_count(host.current_app_instance_id()) + pending_record_count(host.current_app_instance_id()) >=
            policy_.max_records) {
        return rejected(AppServiceSubmitStatus::BudgetExceeded,
                        HostServiceStatus::BudgetExceeded,
                        kDeviceErrorBudgetExceeded);
    }

    const HostServiceSubmitResult submitted =
        host.submit_current(HostServiceJobKind::LocationSnapshot, 0, 0, timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingSnapshot pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = host.current_app_instance_id();
    if (!has_fixture_) {
        pending.status = HostServiceStatus::Failed;
        pending.error_code = kDeviceErrorUnavailable;
    } else {
        pending.status = HostServiceStatus::Completed;
        pending.fixture = fixture_;
    }
    pending_.push_back(pending);
    return result;
}

bool AppLocationSnapshotMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::LocationSnapshot, request)) {
        return false;
    }
    return host.push_completion(complete_request(host, request));
}

HostServiceCompletion AppLocationSnapshotMock::complete_request(AppRuntimeHost& host,
                                                                const HostServiceRequest& request) {
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return make_cancelled_completion(request);
    }

    HostServiceCompletion completion{
        request.job_id,
        HostServiceJobKind::LocationSnapshot,
        pending->status,
        request.app_instance_id,
        0,
        pending->error_code,
        0,
    };
    if (pending->status == HostServiceStatus::Completed) {
        const std::uint32_t bytes = static_cast<std::uint32_t>(sizeof(AppLocationSnapshotRecord));
        const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::LocationSnapshot,
                                                            request.app_instance_id,
                                                            bytes);
        if (handle == 0) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = kDeviceErrorBudgetExceeded;
        } else {
            completion.handle = handle;
            completion.byte_count = bytes;
            records_.push_back(AppLocationSnapshotRecord{
                handle,
                request.app_instance_id,
                pending->fixture.timestamp_ms,
                pending->fixture.latitude,
                pending->fixture.longitude,
                pending->fixture.altitude_m,
                pending->fixture.accuracy_m,
                pending->fixture.speed_mps,
            });
        }
    }
    pending_.erase(pending);
    return completion;
}

const AppLocationSnapshotRecord* AppLocationSnapshotMock::snapshot(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(),
                                    records_.end(),
                                    [handle](const AppLocationSnapshotRecord& record) {
                                        return record.handle == handle;
                                    });
    return found == records_.end() ? nullptr : &*found;
}

bool AppLocationSnapshotMock::release_snapshot(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(),
                                    records_.end(),
                                    [handle](const AppLocationSnapshotRecord& record) {
                                        return record.handle == handle;
                                    });
    if (found == records_.end()) {
        return false;
    }
    records_.erase(found);
    return host.handles().release(handle);
}

std::size_t AppLocationSnapshotMock::release_app_snapshots(AppRuntimeHost& host, std::uint32_t app_instance_id) {
    std::vector<std::uint32_t> handles;
    handles.reserve(records_.size());
    for (const AppLocationSnapshotRecord& record : records_) {
        if (record.app_instance_id == app_instance_id) {
            handles.push_back(record.handle);
        }
    }
    for (std::uint32_t handle : handles) {
        release_snapshot(host, handle);
    }
    return handles.size();
}

std::size_t AppLocationSnapshotMock::collect_released_snapshots(const AppRuntimeHost& host) {
    const auto old_size = records_.size();
    records_.erase(std::remove_if(records_.begin(),
                                  records_.end(),
                                  [&host](const AppLocationSnapshotRecord& record) {
                                      return host.handles().lookup(record.handle) == nullptr;
                                  }),
                   records_.end());
    return old_size - records_.size();
}

void AppLocationSnapshotMock::clear() {
    has_fixture_ = false;
    fixture_ = {};
    pending_.clear();
    records_.clear();
}

} // namespace jellyframe
