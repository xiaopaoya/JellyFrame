#include "app_runtime/app_device_services.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "app_device_services check failed: " << message << '\n';
    std::abort();
}

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{
        4,
        2,
        8,
        4096,
        1,
    });
}

std::vector<HostServiceCompletion> pump(AppRuntimeHost& host) {
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    return accepted;
}

void sensor_policy_is_derived_from_manifest_and_host() {
    HostDeviceCapabilities capabilities;
    capabilities.sensors.supports_accelerometer = true;
    capabilities.sensors.supports_gyroscope = true;
    capabilities.sensors.max_sensor_sample_records = 3;
    capabilities.location.supports_position = true;
    capabilities.location.max_location_snapshot_records = 1;

    const AppServiceHostProfile profile = app_service_host_profile_from_capabilities(capabilities);
    check(profile.allow_sensor_accelerometer, "profile accelerometer allowed");
    check(profile.allow_sensor_gyroscope, "profile gyroscope allowed");
    check(!profile.allow_sensor_heart_rate, "profile heart rate denied");
    check(profile.allow_location_position, "profile location allowed");
    check(profile.max_sensor_sample_records == 3, "profile sensor budget");
    check(profile.max_location_snapshot_records == 1, "profile location budget");

    AppServicePolicies policies = app_service_policies_for_app(AppServiceManifestCapabilities{}, profile);
    check(!policies.sensor_accelerometer, "accelerometer requires manifest capability");
    check(!policies.sensor_gyroscope, "gyroscope requires manifest capability");
    check(!policies.location_position, "location requires manifest capability");

    AppServiceManifestCapabilities manifest;
    manifest.sensor_accelerometer = true;
    manifest.sensor_gyroscope = true;
    manifest.sensor_heart_rate = true;
    manifest.location_position = true;
    policies = app_service_policies_for_app(manifest, profile);
    check(policies.sensor_accelerometer, "accelerometer allowed by manifest and host");
    check(policies.sensor_gyroscope, "gyroscope allowed by manifest and host");
    check(!policies.sensor_heart_rate, "heart rate denied without host");
    check(policies.location_position, "location allowed by manifest and host");

    const AppSensorSamplePolicy sensor_policy = app_sensor_sample_policy_from_service_policies(policies);
    check(sensor_policy.accelerometer, "sensor policy accelerometer");
    check(sensor_policy.gyroscope, "sensor policy gyroscope");
    check(!sensor_policy.heart_rate, "sensor policy heart rate denied");
    check(sensor_policy.max_records == 3, "sensor policy record budget");

    const AppLocationSnapshotPolicy location_policy = app_location_snapshot_policy_from_service_policies(policies);
    check(location_policy.position, "location policy position");
    check(location_policy.max_records == 1, "location policy record budget");
}

void sensor_sample_requires_capability_and_returns_handle() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.fitness", AppRole::App);

    AppSensorSampleMock sensors;
    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).status ==
              AppServiceSubmitStatus::CapabilityDenied,
          "sensor capability gate");

    sensors.set_policy(AppSensorSamplePolicy{true, false, false, false, 2});
    check(sensors.add_fixture(AppSensorSampleFixture{
              AppSensorKind::Accelerometer,
              1000,
              0.1f,
              0.2f,
              0.3f,
              0.0f,
              0.9f,
          }),
          "sensor fixture accepted");

    const AppServiceSubmitResult submitted = sensors.submit_sample(host, AppSensorKind::Accelerometer, 100);
    check(submitted.accepted(), "sensor sample submitted");
    check(sensors.complete_next(host), "sensor sample completed");

    const std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "sensor completion accepted");
    check(accepted.front().kind == HostServiceJobKind::SensorSample, "sensor completion kind");
    check(accepted.front().status == HostServiceStatus::Completed, "sensor completion status");
    check(accepted.front().handle != 0, "sensor completion handle");

    const AppSensorSampleRecord* sample = sensors.sample(accepted.front().handle);
    check(sample != nullptr, "sensor sample lookup");
    check(sample->app_instance_id == host.current_app_instance_id(), "sensor sample instance");
    check(sample->kind == AppSensorKind::Accelerometer, "sensor sample kind");
    check(sample->timestamp_ms == 1000, "sensor timestamp");
    check(sample->x == 0.1f, "sensor x value");
    check(sample->accuracy == 0.9f, "sensor accuracy value");
    check(sensors.release_sample(host, accepted.front().handle), "sensor sample release");
}

void sensor_sample_reports_missing_data_and_record_budget() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.sensors", AppRole::App);
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, 1});

    const AppServiceSubmitResult missing = sensors.submit_sample(host, AppSensorKind::Accelerometer);
    check(missing.accepted(), "sensor missing sample submitted");
    check(sensors.complete_next(host), "sensor missing sample completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "sensor missing completion accepted");
    check(accepted.front().status == HostServiceStatus::Failed, "sensor missing status");
    check(classify_app_device_failure(AppServiceSubmitStatus::Accepted,
                                      accepted.front().status,
                                      accepted.front().error_code) ==
              AppDeviceFailureReason::SampleUnavailable,
          "sensor missing classification");

    check(sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 2000, 1.0f, 2.0f, 3.0f}),
          "sensor budget fixture accepted");
    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).accepted(), "sensor pending budget first");
    AppServiceSubmitResult over_budget = sensors.submit_sample(host, AppSensorKind::Accelerometer);
    check(over_budget.status == AppServiceSubmitStatus::BudgetExceeded, "sensor pending counts against budget");
    check(sensors.complete_next(host), "sensor pending budget complete");
    accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "sensor pending budget handle");
    check(sensors.release_sample(host, accepted.front().handle), "sensor pending budget release");

    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).accepted(), "sensor budget submit");
    check(sensors.complete_next(host), "sensor budget complete");
    accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "sensor budget handle");
    over_budget = sensors.submit_sample(host, AppSensorKind::Accelerometer);
    check(over_budget.status == AppServiceSubmitStatus::BudgetExceeded, "sensor record budget");
    check(classify_app_device_failure(over_budget.status,
                                      over_budget.rejected_status,
                                      over_budget.error_code) ==
              AppDeviceFailureReason::RecordBudgetExceeded,
          "sensor record budget classification");
    check(sensors.release_sample(host, accepted.front().handle), "sensor budget release");
}

void sensor_samples_follow_app_instance_lifetime() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.first", AppRole::App);
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, 2});
    check(sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 100}),
          "sensor lifetime fixture accepted");

    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).accepted(), "sensor lifetime submit old");
    host.launch("org.example.second", AppRole::App);
    check(host.requests().empty(), "sensor old request cancelled on app switch");
    check(!sensors.complete_next(host), "sensor cancelled request not completed");

    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).accepted(), "sensor lifetime submit current");
    check(sensors.complete_next(host), "sensor lifetime complete current");
    const std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "sensor lifetime handle");
    const std::uint32_t handle = accepted.front().handle;
    host.launch("org.example.third", AppRole::App);
    check(host.handles().lookup(handle) == nullptr, "sensor lifetime host handle released");
    check(sensors.collect_released_samples(host) == 1, "sensor stale records collected");
    check(sensors.sample(handle) == nullptr, "sensor stale sample record removed");
}

void location_snapshot_requires_capability_and_returns_handle() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.map", AppRole::App);

    AppLocationSnapshotMock location;
    check(location.submit_position(host).status == AppServiceSubmitStatus::CapabilityDenied,
          "location capability gate");
    location.set_policy(AppLocationSnapshotPolicy{true, 1});
    check(!location.set_fixture(AppLocationSnapshotFixture{100, 91.0, 0.0}),
          "location rejects invalid latitude");
    check(location.set_fixture(AppLocationSnapshotFixture{100, 31.2304, 121.4737, 4.0f, 8.0f, 0.2f}),
          "location fixture accepted");

    check(location.submit_position(host, 1000).accepted(), "location position submitted");
    check(location.complete_next(host), "location position completed");
    const std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "location completion accepted");
    check(accepted.front().kind == HostServiceJobKind::LocationSnapshot, "location completion kind");
    check(accepted.front().status == HostServiceStatus::Completed, "location completion status");
    check(accepted.front().handle != 0, "location completion handle");

    const AppLocationSnapshotRecord* snapshot = location.snapshot(accepted.front().handle);
    check(snapshot != nullptr, "location snapshot lookup");
    check(snapshot->app_instance_id == host.current_app_instance_id(), "location snapshot instance");
    check(snapshot->latitude == 31.2304, "location latitude");
    check(snapshot->longitude == 121.4737, "location longitude");
    check(snapshot->accuracy_m == 8.0f, "location accuracy");
    check(location.release_snapshot(host, accepted.front().handle), "location snapshot release");
}

void location_snapshot_reports_missing_data_and_budget() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.location", AppRole::App);
    AppLocationSnapshotMock location(AppLocationSnapshotPolicy{true, 1});

    check(location.submit_position(host).accepted(), "location missing submitted");
    check(location.complete_next(host), "location missing completed");
    std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 1, "location missing completion accepted");
    check(accepted.front().status == HostServiceStatus::Failed, "location missing status");
    check(classify_app_device_failure(AppServiceSubmitStatus::Accepted,
                                      accepted.front().status,
                                      accepted.front().error_code) ==
              AppDeviceFailureReason::SampleUnavailable,
          "location missing classification");

    check(location.set_fixture(AppLocationSnapshotFixture{100, 30.0, 120.0}),
          "location budget fixture accepted");
    check(location.submit_position(host).accepted(), "location pending budget submit");
    AppServiceSubmitResult over_budget = location.submit_position(host);
    check(over_budget.status == AppServiceSubmitStatus::BudgetExceeded, "location pending counts against budget");
    check(location.complete_next(host), "location pending budget complete");
    accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "location pending budget handle");
    check(location.release_snapshot(host, accepted.front().handle), "location pending budget release");

    check(location.submit_position(host).accepted(), "location budget submit");
    check(location.complete_next(host), "location budget complete");
    accepted = pump(host);
    check(accepted.size() == 1 && accepted.front().handle != 0, "location budget handle");
    over_budget = location.submit_position(host);
    check(over_budget.status == AppServiceSubmitStatus::BudgetExceeded, "location record budget");
    check(location.release_snapshot(host, accepted.front().handle), "location budget release");
}

void device_workers_do_not_consume_other_service_requests() {
    AppRuntimeHost host = make_host();
    host.launch("org.example.mixed-device", AppRole::App);
    NetworkFetchMock network(NetworkFetchPolicy{true, 64, 128});
    check(network.add_fixture(NetworkFetchFixture{"/data", 200, "application/json", "{}"}),
          "mixed network fixture accepted");
    AppSensorSampleMock sensors(AppSensorSamplePolicy{true, false, false, false, 2});
    check(sensors.add_fixture(AppSensorSampleFixture{AppSensorKind::Accelerometer, 100}),
          "mixed sensor fixture accepted");

    check(network.submit_fetch(host, "/data").accepted(), "mixed network submitted");
    check(sensors.submit_sample(host, AppSensorKind::Accelerometer).accepted(), "mixed sensor submitted");
    check(sensors.complete_next(host), "mixed sensor completed first");
    check(network.complete_next(host), "mixed network completed second");

    const std::vector<HostServiceCompletion> accepted = pump(host);
    check(accepted.size() == 2, "mixed completions accepted");
    check(accepted[0].kind == HostServiceJobKind::SensorSample, "mixed first sensor");
    check(accepted[1].kind == HostServiceJobKind::NetworkFetch, "mixed second network");
    if (accepted[0].handle != 0) {
        check(sensors.release_sample(host, accepted[0].handle), "mixed sensor release");
    }
    if (accepted[1].handle != 0) {
        check(network.release_response(host, accepted[1].handle), "mixed network release");
    }
}

void device_failure_names_are_stable() {
    check(std::string(app_sensor_kind_name(AppSensorKind::HeartRate)) == "heart-rate",
          "sensor kind name");
    check(std::string(app_device_failure_reason_name(AppDeviceFailureReason::HandleBudgetExceeded)) ==
              "handle-budget-exceeded",
          "device failure name");
    check(classify_app_device_failure(AppServiceSubmitStatus::CapabilityDenied,
                                      HostServiceStatus::Unsupported,
                                      0) ==
              AppDeviceFailureReason::CapabilityDenied,
          "device capability classification");
    check(classify_app_device_failure(AppServiceSubmitStatus::QueueFull,
                                      HostServiceStatus::BudgetExceeded,
                                      0) ==
              AppDeviceFailureReason::QueueFull,
          "device queue classification");
    check(classify_app_device_failure(AppServiceSubmitStatus::Accepted,
                                      HostServiceStatus::Timeout,
                                      0) ==
              AppDeviceFailureReason::RequestTimeout,
          "device timeout classification");
}

} // namespace

int main() {
    sensor_policy_is_derived_from_manifest_and_host();
    sensor_sample_requires_capability_and_returns_handle();
    sensor_sample_reports_missing_data_and_record_budget();
    sensor_samples_follow_app_instance_lifetime();
    location_snapshot_requires_capability_and_returns_handle();
    location_snapshot_reports_missing_data_and_budget();
    device_workers_do_not_consume_other_service_requests();
    device_failure_names_are_stable();
    return 0;
}
