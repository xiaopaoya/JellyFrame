#include "app_runtime/app_host_data.h"

namespace jellyframe {

const char* app_weather_condition_name(AppWeatherCondition condition) {
    switch (condition) {
    case AppWeatherCondition::Unknown:
        return "unknown";
    case AppWeatherCondition::Clear:
        return "clear";
    case AppWeatherCondition::Cloudy:
        return "cloudy";
    case AppWeatherCondition::Rain:
        return "rain";
    case AppWeatherCondition::Snow:
        return "snow";
    case AppWeatherCondition::Storm:
        return "storm";
    case AppWeatherCondition::Fog:
        return "fog";
    }
    return "unknown";
}

std::uint8_t app_host_data_clamp_percent(std::uint8_t percent) {
    return percent > 100 ? 100 : percent;
}

bool app_host_data_valid_location(const AppLocationSummarySnapshot& location) {
    return location.latitude >= -90.0 && location.latitude <= 90.0 &&
        location.longitude >= -180.0 && location.longitude <= 180.0;
}

AppHostDataSnapshot app_host_data_filter_for_app(const AppHostDataSnapshot& source,
                                                 const AppHostDataAccessPolicy& policy) {
    AppHostDataSnapshot output = source;
    output.battery.percent = app_host_data_clamp_percent(output.battery.percent);

    if (!policy.battery) {
        output.has.battery = false;
        output.battery = {};
    }
    if (!policy.weather) {
        output.has.weather = false;
        output.weather = {};
    }
    if (!policy.activity) {
        output.has.activity = false;
        output.activity = {};
    }
    if (!policy.services.location_position || !app_host_data_valid_location(output.location)) {
        output.has.location = false;
        output.location = {};
    }
    if (!policy.services.sensor_accelerometer) {
        output.has.accelerometer = false;
        output.sensors.accelerometer_x = 0.0f;
        output.sensors.accelerometer_y = 0.0f;
        output.sensors.accelerometer_z = 0.0f;
    }
    if (!policy.services.sensor_gyroscope) {
        output.has.gyroscope = false;
        output.sensors.gyroscope_x = 0.0f;
        output.sensors.gyroscope_y = 0.0f;
        output.sensors.gyroscope_z = 0.0f;
    }
    if (!policy.services.sensor_heart_rate) {
        output.has.heart_rate = false;
        output.sensors.heart_rate_bpm = 0.0f;
    }
    if (!policy.services.sensor_ambient_light) {
        output.has.ambient_light = false;
        output.sensors.ambient_light_lux = 0.0f;
    }
    if (!output.has.accelerometer && !output.has.gyroscope &&
        !output.has.heart_rate && !output.has.ambient_light) {
        output.sensors.timestamp_ms = 0;
    }
    return output;
}

} // namespace jellyframe
