#pragma once

#include "app_runtime/app_services.h"

#include <cstdint>

namespace jellyframe {

enum class AppWeatherCondition : std::uint8_t {
    Unknown,
    Clear,
    Cloudy,
    Rain,
    Snow,
    Storm,
    Fog,
};

struct AppBatterySnapshot {
    std::uint64_t timestamp_ms = 0;
    std::uint8_t percent = 0;
    bool charging = false;
};

struct AppWeatherSnapshot {
    std::uint64_t timestamp_ms = 0;
    AppWeatherCondition condition = AppWeatherCondition::Unknown;
    std::int16_t temperature_c_x10 = 0;
    std::int16_t feels_like_c_x10 = 0;
    std::uint8_t humidity_percent = 0;
    std::uint16_t wind_speed_mps_x10 = 0;
    std::uint16_t precipitation_mm_x10 = 0;
    std::uint16_t air_quality_index = 0;
};

struct AppActivitySnapshot {
    std::uint64_t timestamp_ms = 0;
    std::uint32_t steps = 0;
    std::uint32_t active_minutes = 0;
    std::uint32_t calories_kcal = 0;
    std::uint32_t distance_m = 0;
};

struct AppLocationSummarySnapshot {
    std::uint64_t timestamp_ms = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    float accuracy_m = 0.0f;
};

struct AppSensorSummarySnapshot {
    std::uint64_t timestamp_ms = 0;
    float accelerometer_x = 0.0f;
    float accelerometer_y = 0.0f;
    float accelerometer_z = 0.0f;
    float gyroscope_x = 0.0f;
    float gyroscope_y = 0.0f;
    float gyroscope_z = 0.0f;
    float heart_rate_bpm = 0.0f;
    float ambient_light_lux = 0.0f;
};

struct AppHostDataPresence {
    bool battery = false;
    bool weather = false;
    bool activity = false;
    bool location = false;
    bool accelerometer = false;
    bool gyroscope = false;
    bool heart_rate = false;
    bool ambient_light = false;
};

struct AppHostDataSnapshot {
    AppHostDataPresence has;
    AppBatterySnapshot battery;
    AppWeatherSnapshot weather;
    AppActivitySnapshot activity;
    AppLocationSummarySnapshot location;
    AppSensorSummarySnapshot sensors;
};

struct AppHostDataAccessPolicy {
    bool battery = false;
    bool weather = false;
    bool activity = false;
    AppServicePolicies services;
};

const char* app_weather_condition_name(AppWeatherCondition condition);

AppHostDataSnapshot app_host_data_filter_for_app(const AppHostDataSnapshot& source,
                                                 const AppHostDataAccessPolicy& policy);

std::uint8_t app_host_data_clamp_percent(std::uint8_t percent);
bool app_host_data_valid_location(const AppLocationSummarySnapshot& location);

} // namespace jellyframe
