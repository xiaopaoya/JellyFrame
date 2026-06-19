#pragma once

#include "app_runtime/app_host.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jellyframe {

enum class AppSystemEventKind {
    TimeChanged,
    TimezoneChanged,
    NetworkStatusChanged,
    BatteryChanged,
    ScreenStateChanged,
    LowPowerModeChanged,
};

struct AppSystemStateSnapshot {
    std::uint64_t unix_time_ms = 0;
    std::int16_t timezone_offset_minutes = 0;
    std::uint8_t battery_percent = 0;
    bool charging = false;
    bool network_online = false;
    bool screen_on = true;
    bool low_power_mode = false;
};

struct AppSystemEvent {
    std::uint32_t app_instance_id = 0;
    AppSystemEventKind kind = AppSystemEventKind::TimeChanged;
    AppSystemStateSnapshot snapshot;
};

struct AppSystemEventPumpResult {
    std::size_t consumed = 0;
    std::size_t accepted = 0;
    std::size_t stale = 0;
};

class AppSystemEventQueue {
public:
    AppSystemEventQueue(std::size_t capacity = 0, std::size_t max_events_per_frame = 0);

    bool push_current(const AppRuntimeHost& host,
                      AppSystemEventKind kind,
                      const AppSystemStateSnapshot& snapshot);
    AppSystemEventPumpResult pump_current(const AppRuntimeHost& host,
                                          std::vector<AppSystemEvent>& output);
    std::size_t discard_app_instance(std::uint32_t app_instance_id);
    void clear();

    std::size_t size() const {
        return events_.size();
    }

    std::size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        return events_.empty();
    }

    bool full() const {
        return events_.size() >= capacity_;
    }

private:
    std::size_t capacity_ = 0;
    std::size_t max_events_per_frame_ = 0;
    std::vector<AppSystemEvent> events_;
};

} // namespace jellyframe
