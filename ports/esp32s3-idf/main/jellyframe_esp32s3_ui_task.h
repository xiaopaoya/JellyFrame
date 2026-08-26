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
    std::uint32_t input_seq = 0;
    std::uint32_t mutation_seq = 0;
    std::uint32_t published_seq = 0;
    std::uint32_t accepted_seq = 0;
    std::uint32_t presents_failed = 0;
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
bool start_script_service_echo_acceptance_task();
bool start_script_fault_recovery_acceptance_task();

} // namespace jellyframe_esp32s3
