if(CMAKE_BUILD_EARLY_EXPANSION)
    # ESP-IDF scans requirements before sdkconfig and project cache options are
    # available. Declare the dependency superset here and select sources below.
    idf_component_register(REQUIRES jellyframe_render_core jellyframe_jerryscript)
    return()
endif()

set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
if(NOT JELLYFRAME_JERRYSCRIPT_ROOT)
    set(JELLYFRAME_JERRYSCRIPT_ROOT "${JELLYFRAME_ROOT}/third_party/jerryscript")
endif()
get_filename_component(JELLYFRAME_JERRYSCRIPT_INCLUDE_DIR
    "${JELLYFRAME_JERRYSCRIPT_ROOT}/jerry-core/include" ABSOLUTE)
if(JELLYFRAME_RENDER_CORE_SOURCE_DIR)
    get_filename_component(JELLYFRAME_RENDER_CORE_INCLUDE_ROOT
        "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/include" ABSOLUTE)
else()
    set(JELLYFRAME_RENDER_CORE_INCLUDE_ROOT "${JELLYFRAME_ROOT}/src")
endif()
set(JELLYFRAME_COMPONENT_REQUIRES jellyframe_render_core)
set(JELLYFRAME_SCRIPT_INCLUDE_DIRS)
set(JELLYFRAME_SCRIPT_BACKEND_SOURCES)

if(NOT DEFINED JELLYFRAME_SCRIPT_ENGINE)
    set(JELLYFRAME_SCRIPT_ENGINE "jerryscript")
endif()

if(DEFINED JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME)
    message(FATAL_ERROR
        "JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME was replaced by "
        "JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME; remove the stale CMake cache entry and reconfigure.")
endif()

set(JELLYFRAME_SCRIPT_TASK_SOURCES)
if(CONFIG_JELLYFRAME_ESP32S3_ENABLE_SCRIPT_TASK_RUNTIME)
    if(NOT JELLYFRAME_BUILD_SCRIPTING)
        message(FATAL_ERROR
            "script-task port preflight requires JELLYFRAME_BUILD_SCRIPTING=ON")
    endif()
    if(NOT JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
        message(FATAL_ERROR
            "script-task port preflight requires JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME=ON")
    endif()
    list(APPEND JELLYFRAME_SCRIPT_TASK_SOURCES
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_contract.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_frame_codec.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_frame_renderer.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_input_codec.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_input_dispatch.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_service_request_codec.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_service_bridge.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_fatal_codec.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/script_task_worker_inbox.cpp"
    )
    # Canonical frame rendering references these resource adapters even when
    # the installed app selects the resource-free frame path.
    list(APPEND JELLYFRAME_SCRIPT_TASK_SOURCES
        "${JELLYFRAME_ROOT}/src/app_runtime/runtime_font_catalog.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/runtime_image_provider.cpp"
    )
    if(CONFIG_JELLYFRAME_ESP32S3_RUN_DEVICE_IMAGE_LIFECYCLE_ACCEPTANCE OR
       CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_APP_ACCEPTANCE OR
       CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V2_ACCEPTANCE OR
       CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_TASK_VALUE_FRAME_V4_ACCEPTANCE OR
       CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_SERVICE_ECHO_ACCEPTANCE OR
       CONFIG_JELLYFRAME_ESP32S3_RUN_SCRIPT_FAULT_RECOVERY_ACCEPTANCE)
        if(NOT JELLYFRAME_BUILD_SCRIPTING)
            message(FATAL_ERROR
                "real script-app acceptance requires JELLYFRAME_BUILD_SCRIPTING=ON")
        endif()
        if(NOT JELLYFRAME_SCRIPT_ENGINE STREQUAL "jerryscript")
            message(FATAL_ERROR
                "ESP32-S3 currently provides only the jerryscript script backend; "
                "got JELLYFRAME_SCRIPT_ENGINE='${JELLYFRAME_SCRIPT_ENGINE}'")
        endif()
        # The engine-specific source and IDF component are selected once at
        # configure time. Worker and frame protocols remain engine-neutral.
        list(APPEND JELLYFRAME_SCRIPT_BACKEND_SOURCES
            "${JELLYFRAME_ROOT}/src/script/jerryscript_runtime.cpp"
        )
        list(APPEND JELLYFRAME_SCRIPT_TASK_SOURCES
            "${JELLYFRAME_ROOT}/src/script/script_task_worker_runtime.cpp"
        )
        list(APPEND JELLYFRAME_COMPONENT_REQUIRES jellyframe_jerryscript)
        list(APPEND JELLYFRAME_SCRIPT_INCLUDE_DIRS
            "${JELLYFRAME_JERRYSCRIPT_INCLUDE_DIR}"
        )
    endif()
endif()

idf_component_register(
    SRCS
        "${JELLYFRAME_ROOT}/src/app_runtime/app_budget.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_capability_broker.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_compute_jobs.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_device_services.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_font_set.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/core_system_font_catalog.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_frame_policy.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_host.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_host_data.cpp"
        "${JELLYFRAME_ROOT}/src/app_runtime/app_installed_bundle.cpp"
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
        ${JELLYFRAME_SCRIPT_TASK_SOURCES}
        ${JELLYFRAME_SCRIPT_BACKEND_SOURCES}
    INCLUDE_DIRS
        "${JELLYFRAME_RENDER_CORE_INCLUDE_ROOT}"
        "${JELLYFRAME_ROOT}/src"
        ${JELLYFRAME_SCRIPT_INCLUDE_DIRS}
    REQUIRES
        ${JELLYFRAME_COMPONENT_REQUIRES}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(JELLYFRAME_SCRIPT_BACKEND_SOURCES)
    include("${JELLYFRAME_ROOT}/cmake/jellyframe_script_config.cmake")
    jellyframe_configure_script(${COMPONENT_LIB} PUBLIC idf::jellyframe_render_core)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
)
