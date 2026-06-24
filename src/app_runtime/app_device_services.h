#pragma once

#include "app_runtime/app_host.h"
#include "app_runtime/app_services.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

enum class AppSensorKind {
    Accelerometer,
    Gyroscope,
    HeartRate,
    AmbientLight,
};

enum class AppDeviceFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidRequest,
    QueueFull,
    SampleUnavailable,
    RecordBudgetExceeded,
    HandleBudgetExceeded,
    RequestFailed,
    RequestCancelled,
    RequestTimeout,
    Unsupported,
    Unknown,
};

struct AppSensorSamplePolicy {
    bool accelerometer = false;
    bool gyroscope = false;
    bool heart_rate = false;
    bool ambient_light = false;
    std::size_t max_records = 4;
};

struct AppLocationSnapshotPolicy {
    bool position = false;
    std::size_t max_records = 2;
};

struct AppSensorSampleFixture {
    AppSensorKind kind = AppSensorKind::Accelerometer;
    std::uint64_t timestamp_ms = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    float accuracy = 0.0f;
};

struct AppSensorSampleRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    AppSensorKind kind = AppSensorKind::Accelerometer;
    std::uint64_t timestamp_ms = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float value = 0.0f;
    float accuracy = 0.0f;
};

struct AppLocationSnapshotFixture {
    std::uint64_t timestamp_ms = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude_m = 0.0f;
    float accuracy_m = 0.0f;
    float speed_mps = 0.0f;
};

struct AppLocationSnapshotRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::uint64_t timestamp_ms = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    float altitude_m = 0.0f;
    float accuracy_m = 0.0f;
    float speed_mps = 0.0f;
};

const char* app_sensor_kind_name(AppSensorKind kind);
const char* app_device_failure_reason_name(AppDeviceFailureReason reason);

AppSensorSamplePolicy app_sensor_sample_policy_from_service_policies(const AppServicePolicies& policies);
AppLocationSnapshotPolicy app_location_snapshot_policy_from_service_policies(const AppServicePolicies& policies);

AppDeviceFailureReason classify_app_device_failure(AppServiceSubmitStatus submit_status,
                                                   HostServiceStatus host_status,
                                                   std::uint32_t error_code);

class AppSensorSampleMock {
public:
    explicit AppSensorSampleMock(AppSensorSamplePolicy policy = {});

    void set_policy(AppSensorSamplePolicy policy);
    bool add_fixture(AppSensorSampleFixture fixture);
    AppServiceSubmitResult submit_sample(AppRuntimeHost& host,
                                         AppSensorKind kind,
                                         std::uint32_t timeout_ms = 0);
    HostServiceCompletion complete_request(AppRuntimeHost& host, const HostServiceRequest& request);
    bool complete_next(AppRuntimeHost& host);
    const AppSensorSampleRecord* sample(std::uint32_t handle) const;
    bool release_sample(AppRuntimeHost& host, std::uint32_t handle);
    std::size_t release_app_samples(AppRuntimeHost& host, std::uint32_t app_instance_id);
    std::size_t collect_released_samples(const AppRuntimeHost& host);
    void clear();

private:
    struct PendingSample {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        AppSensorSampleFixture fixture;
    };

    bool enabled(AppSensorKind kind) const;
    std::size_t active_record_count(std::uint32_t app_instance_id = 0) const;
    std::size_t pending_record_count(std::uint32_t app_instance_id = 0) const;

    AppSensorSamplePolicy policy_;
    std::vector<AppSensorSampleFixture> fixtures_;
    std::vector<PendingSample> pending_;
    std::vector<AppSensorSampleRecord> records_;
};

class AppLocationSnapshotMock {
public:
    explicit AppLocationSnapshotMock(AppLocationSnapshotPolicy policy = {});

    void set_policy(AppLocationSnapshotPolicy policy);
    bool set_fixture(AppLocationSnapshotFixture fixture);
    AppServiceSubmitResult submit_position(AppRuntimeHost& host, std::uint32_t timeout_ms = 0);
    HostServiceCompletion complete_request(AppRuntimeHost& host, const HostServiceRequest& request);
    bool complete_next(AppRuntimeHost& host);
    const AppLocationSnapshotRecord* snapshot(std::uint32_t handle) const;
    bool release_snapshot(AppRuntimeHost& host, std::uint32_t handle);
    std::size_t release_app_snapshots(AppRuntimeHost& host, std::uint32_t app_instance_id);
    std::size_t collect_released_snapshots(const AppRuntimeHost& host);
    void clear();

private:
    struct PendingSnapshot {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        AppLocationSnapshotFixture fixture;
    };

    std::size_t active_record_count(std::uint32_t app_instance_id = 0) const;
    std::size_t pending_record_count(std::uint32_t app_instance_id = 0) const;

    AppLocationSnapshotPolicy policy_;
    bool has_fixture_ = false;
    AppLocationSnapshotFixture fixture_;
    std::vector<PendingSnapshot> pending_;
    std::vector<AppLocationSnapshotRecord> records_;
};

} // namespace jellyframe
