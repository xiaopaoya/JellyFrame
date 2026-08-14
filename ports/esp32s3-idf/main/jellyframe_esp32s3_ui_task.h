#pragma once

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
bool start_app_runtime_recovery_acceptance_task();
bool start_script_task_value_protocol_acceptance_task();
bool start_script_app_acceptance_task();
bool start_script_task_value_frame_v2_acceptance_task();
bool start_script_service_echo_acceptance_task();
bool start_script_fault_recovery_acceptance_task();

} // namespace jellyframe_esp32s3
