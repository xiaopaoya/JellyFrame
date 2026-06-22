#pragma once

#include "app_runtime/app_lifecycle.h"
#include "app_runtime/system_events.h"
#include "render_core/frame_loop.h"

namespace jellyframe {

struct AppFramePolicy {
    bool accepts_input = false;
    bool pumps_timers = false;
    bool pumps_animation = false;
    bool presents_frames = false;
    bool resume_should_repaint = false;
};

struct AppBackgroundServicePolicy {
    bool network_while_suspended = false;
    bool network_while_screen_off = false;
    bool audio_while_suspended = false;
    bool audio_while_screen_off = false;
    bool sensors_while_suspended = false;
    bool sensors_while_screen_off = false;
    bool sensors_in_low_power = false;
};

struct AppServiceActivityPolicy {
    bool network_fetch = false;
    bool audio_playback = false;
    bool sensor_sampling = false;
    bool should_pause_audio = false;
    bool should_throttle_sensors = false;
};

AppFramePolicy app_frame_policy_for(const AppInstance& instance,
                                    const AppSystemStateSnapshot& system_state);

FrameLoopOptions apply_app_frame_policy(FrameLoopOptions options,
                                        const AppFramePolicy& policy);

bool app_resume_needs_repaint(const AppFramePolicy& previous_policy,
                              const AppFramePolicy& next_policy);

AppServiceActivityPolicy app_service_activity_policy_for(
    const AppInstance& instance,
    const AppSystemStateSnapshot& system_state,
    const AppBackgroundServicePolicy& background_policy = {});

} // namespace jellyframe
