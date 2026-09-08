#pragma once

#include "jellyframe_esp32s3_resources.h"

#include <cstdint>
#include <string>

namespace jellyframe {
class AppRuntimeHost;
}

namespace jellyframe_esp32s3 {

bool start_timer_ui_task();
bool start_band_shell_ui_task();
bool start_flex_grid_acceptance_task();
bool start_forms_advanced_acceptance_task();
bool start_gradient_fastpath_ui_task();
bool start_scroll_benchmark_task();
bool start_power_acceptance_task();
bool start_soc_power_acceptance_task();
bool start_resource_failure_task();
bool start_image_acceptance_task();
bool start_jfdp_transport_acceptance_task();
bool start_device_image_lifecycle_task();
struct InstalledBundleUiSession;
struct InstalledBundleScriptSession;
// Value-only diagnostics copied from the installed UI task. Device lifecycle
// logs consume this snapshot without retaining renderer or input objects.
struct InstalledBundleUiTaskTelemetry {
    std::uint32_t frames = 0;
    std::uint32_t input_events = 0;
    std::uint32_t queue_left = 0;
    std::uint32_t queue_depth_max = 0;
    std::uint32_t moves_coalesced = 0;
    std::uint32_t input_dropped = 0;
    std::uint32_t presents = 0;
    std::uint32_t present_failures = 0;
    std::uint32_t present_us_last = 0;
    std::uint32_t present_us_p50 = 0;
    std::uint32_t present_us_p95 = 0;
    std::uint32_t present_us_max = 0;
    bool present_ok_last = true;
    std::uint32_t stack_free = 0;
    std::uint32_t internal_free_min = 0;
    std::uint32_t psram_free_min = 0;
};
struct InstalledBundleScriptTaskTelemetry {
    bool initialized = false;
    bool fatal = false;
    bool worker_started = false;
    bool ui_started = false;
    // These are numeric mirrors of the Render Core enums. They keep the
    // lifecycle/log boundary value-only while still identifying startup
    // failures when the USB console is deliberately disabled.
    std::uint8_t init_status = 0xff;
    std::uint8_t fatal_reason = 0;
    std::uint32_t scripts = 0;
    std::uint32_t input_posted = 0;
    std::uint32_t input_rejected = 0;
    std::uint32_t input_unsupported = 0;
    std::uint32_t input_queue_dropped = 0;
    std::uint32_t input_seq = 0;
    std::uint32_t mutation_seq = 0;
    std::uint32_t published_seq = 0;
    std::uint32_t accepted_seq = 0;
    std::uint32_t presents_failed = 0;
    bool malformed_v4_probe_published = false;
    bool malformed_v4_probe_rejected = false;
    std::uint32_t accepted_after_malformed_v4_probe = 0;
};

// The Device Runtime passes a copied entry document to this task. No registry,
// bundle lease, transport buffer, or renderer object crosses the task boundary.
bool start_installed_bundle_ui_task(std::string app_id,
                                    std::uint32_t generation,
                                    std::string entry_path,
                                    std::string entry_document,
                                    InstalledResourceSnapshot resources,
                                    InstalledBundleUiSession*& session);
bool stop_installed_bundle_ui_task(InstalledBundleUiSession*& session,
                                   std::uint32_t timeout_ms = 3000);
InstalledBundleUiTaskTelemetry installed_bundle_ui_task_telemetry(const InstalledBundleUiSession* session);

// Script-mode installed Apps use a separate supervisor/worker/UI task group.
// The worker owns the DOM and VM; the UI consumes sealed value-only frames.
bool start_installed_bundle_script_task(std::string app_id,
                                        std::uint32_t generation,
                                        std::uint32_t app_instance_id,
                                        std::string entry_path,
                                        std::string entry_document,
                                        InstalledResourceSnapshot resources,
                                        jellyframe::AppRuntimeHost& host,
                                        InstalledBundleScriptSession*& session);
bool stop_installed_bundle_script_task(InstalledBundleScriptSession*& session,
                                       std::uint32_t timeout_ms = 3000,
                                       InstalledBundleScriptTaskTelemetry* telemetry = nullptr);
bool installed_bundle_script_task_has_fatal(const InstalledBundleScriptSession* session);
InstalledBundleScriptTaskTelemetry installed_bundle_script_task_telemetry(const InstalledBundleScriptSession* session);
bool start_app_runtime_recovery_acceptance_task();
bool start_script_task_value_protocol_acceptance_task();
bool start_script_app_acceptance_task();
bool start_script_task_value_frame_v2_acceptance_task();
bool start_script_task_value_frame_v4_acceptance_task();
bool start_script_service_echo_acceptance_task();
bool start_script_fault_recovery_acceptance_task();

} // namespace jellyframe_esp32s3
