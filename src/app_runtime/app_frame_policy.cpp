#include "app_runtime/app_frame_policy.h"

namespace jellyframe {

AppFramePolicy app_frame_policy_for(const AppInstance& instance,
                                    const AppSystemStateSnapshot& system_state) {
    AppFramePolicy policy;
    if (instance.state != AppLifecycleState::Foreground) {
        policy.resume_should_repaint = instance.state == AppLifecycleState::Suspended;
        return policy;
    }

    policy.presents_frames = system_state.screen_on;
    policy.accepts_input = system_state.screen_on;
    policy.pumps_timers = system_state.screen_on;
    policy.pumps_animation = system_state.screen_on && !system_state.low_power_mode;
    return policy;
}

FrameLoopOptions apply_app_frame_policy(FrameLoopOptions options,
                                        const AppFramePolicy& policy) {
    if (!policy.accepts_input) {
        options.max_input_events_per_frame = 0;
    }
    if (!policy.pumps_timers) {
        options.max_timer_callbacks_per_frame = 0;
    }
    if (!policy.pumps_animation) {
        options.max_animation_callbacks_per_frame = 0;
        options.animation_frame_rate = 0;
    }
    return options;
}

bool app_resume_needs_repaint(const AppFramePolicy& previous_policy,
                              const AppFramePolicy& next_policy) {
    return !previous_policy.presents_frames && next_policy.presents_frames;
}

AppServiceActivityPolicy app_service_activity_policy_for(
    const AppInstance& instance,
    const AppSystemStateSnapshot& system_state,
    const AppBackgroundServicePolicy& background_policy) {
    AppServiceActivityPolicy policy;
    if (!instance.active()) {
        policy.should_pause_audio = true;
        policy.should_throttle_sensors = true;
        return policy;
    }

    const bool foreground = instance.state == AppLifecycleState::Foreground;
    policy.network_fetch = foreground ||
        (background_policy.network_while_suspended && instance.state == AppLifecycleState::Suspended);
    policy.audio_playback = foreground ||
        (background_policy.audio_while_suspended && instance.state == AppLifecycleState::Suspended);
    policy.sensor_sampling = foreground ||
        (background_policy.sensors_while_suspended && instance.state == AppLifecycleState::Suspended);

    if (!system_state.screen_on) {
        policy.network_fetch = policy.network_fetch && background_policy.network_while_screen_off;
        policy.audio_playback = policy.audio_playback && background_policy.audio_while_screen_off;
        policy.sensor_sampling = policy.sensor_sampling && background_policy.sensors_while_screen_off;
    }
    if (system_state.low_power_mode && !background_policy.sensors_in_low_power) {
        policy.sensor_sampling = false;
    }

    policy.should_pause_audio = !policy.audio_playback;
    policy.should_throttle_sensors = !policy.sensor_sampling || system_state.low_power_mode;
    return policy;
}

} // namespace jellyframe
