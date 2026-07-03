#include "app_runtime/app_host_data.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace jellyframe;

namespace {

void check(bool condition, const char* message) {
    if (condition) {
        return;
    }
    std::cerr << "app_host_data check failed: " << message << '\n';
    std::abort();
}

AppHostDataSnapshot make_snapshot() {
    AppHostDataSnapshot snapshot;
    snapshot.has.battery = true;
    snapshot.battery = AppBatterySnapshot{1000, 120, true};
    snapshot.has.weather = true;
    snapshot.weather = AppWeatherSnapshot{1000, AppWeatherCondition::Rain, 213, 201, 74, 42, 13, 38};
    snapshot.has.activity = true;
    snapshot.activity = AppActivitySnapshot{1000, 6400, 32, 230, 4100};
    snapshot.has.location = true;
    snapshot.location = AppLocationSummarySnapshot{1000, 31.2304, 121.4737, 8.0f};
    snapshot.has.accelerometer = true;
    snapshot.has.gyroscope = true;
    snapshot.has.heart_rate = true;
    snapshot.has.ambient_light = true;
    snapshot.sensors = AppSensorSummarySnapshot{1000, 0.1f, 0.2f, 0.3f, 1.0f, 2.0f, 3.0f, 72.0f, 250.0f};
    return snapshot;
}

void host_data_names_and_sanity_helpers_are_stable() {
    check(std::string(app_weather_condition_name(AppWeatherCondition::Storm)) == "storm",
          "weather condition name");
    check(app_host_data_clamp_percent(250) == 100, "percent clamp");
    check(app_host_data_valid_location(AppLocationSummarySnapshot{0, -90.0, 180.0, 0.0f}),
          "location boundary valid");
    check(!app_host_data_valid_location(AppLocationSummarySnapshot{0, 91.0, 0.0, 0.0f}),
          "invalid latitude rejected");
}

void host_data_filter_is_private_by_default() {
    AppHostDataSnapshot snapshot = make_snapshot();
    AppHostDataAccessPolicy policy;

    const AppHostDataSnapshot filtered = app_host_data_filter_for_app(snapshot, policy);
    check(!filtered.has.battery, "battery requires explicit access");
    check(!filtered.has.weather, "weather requires explicit access");
    check(!filtered.has.activity, "activity requires explicit access");
    check(!filtered.has.location, "location requires capability");
    check(!filtered.has.accelerometer, "accelerometer requires capability");
    check(!filtered.has.gyroscope, "gyroscope requires capability");
    check(!filtered.has.heart_rate, "heart rate requires capability");
    check(!filtered.has.ambient_light, "ambient light requires capability");
    check(filtered.sensors.timestamp_ms == 0, "sensor timestamp cleared when no sensors remain");
}

void host_data_filter_applies_access_and_existing_device_policies() {
    AppHostDataSnapshot snapshot = make_snapshot();
    AppHostDataAccessPolicy policy;
    policy.battery = true;
    policy.weather = true;
    policy.services.location_position = true;
    policy.services.sensor_accelerometer = true;
    policy.services.sensor_heart_rate = true;

    const AppHostDataSnapshot filtered = app_host_data_filter_for_app(snapshot, policy);
    check(filtered.has.battery, "battery kept with explicit access");
    check(filtered.battery.percent == 100, "battery percent clamped with access");
    check(filtered.has.weather, "weather kept with explicit access");
    check(!filtered.has.activity, "activity stripped without explicit access");
    check(filtered.has.location, "location kept with capability");
    check(filtered.has.accelerometer, "accelerometer kept with capability");
    check(!filtered.has.gyroscope, "gyroscope stripped without capability");
    check(filtered.has.heart_rate, "heart rate kept with capability");
    check(!filtered.has.ambient_light, "ambient light stripped without capability");
    check(filtered.sensors.gyroscope_x == 0.0f && filtered.sensors.ambient_light_lux == 0.0f,
          "stripped sensor values zeroed");
}

void host_data_filter_rejects_invalid_location_even_with_capability() {
    AppHostDataSnapshot snapshot = make_snapshot();
    snapshot.location.latitude = 100.0;
    AppHostDataAccessPolicy policy;
    policy.services.location_position = true;

    const AppHostDataSnapshot filtered = app_host_data_filter_for_app(snapshot, policy);
    check(!filtered.has.location, "invalid location stripped");
    check(filtered.location.timestamp_ms == 0, "invalid location payload cleared");
}

} // namespace

int main() {
    host_data_names_and_sanity_helpers_are_stable();
    host_data_filter_is_private_by_default();
    host_data_filter_applies_access_and_existing_device_policies();
    host_data_filter_rejects_invalid_location_even_with_capability();
    return 0;
}
