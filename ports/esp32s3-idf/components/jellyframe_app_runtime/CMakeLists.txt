set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

idf_component_register(
    SRCS
        "${JELLYFRAME_ROOT}/src/app_runtime/app_budget.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_capability_broker.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_compute_jobs.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_device_services.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_font_set.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_frame_policy.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_host.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_host_data.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_lifecycle.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_load_telemetry.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_service_worker.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_video_frames.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_services.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_storage_lifecycle_policy.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/authorized_file_broker.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/host_services.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/system_events.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/xml_http_request.cpp"
    INCLUDE_DIRS
        "${JELLYFRAME_ROOT}/src"
    REQUIRES
        jellyframe_render_core
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)
