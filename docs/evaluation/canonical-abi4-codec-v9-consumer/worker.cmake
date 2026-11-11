get_filename_component(_worker_repo "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT JERRYSCRIPT_ROOT)
  set(JERRYSCRIPT_ROOT "${_worker_repo}/third_party/jerryscript" CACHE PATH
    "JerryScript build used by the Runtime worker consumer")
endif()
if(NOT JERRYSCRIPT_INCLUDE_DIR)
  set(JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/jerry-core/include")
endif()
if(NOT JERRYSCRIPT_LIBRARIES)
  set(JERRYSCRIPT_LIBRARIES
    "${JERRYSCRIPT_ROOT}/build/lib/MinSizeRel/jerry-core.lib;${JERRYSCRIPT_ROOT}/build/lib/MinSizeRel/jerry-ext.lib;${JERRYSCRIPT_ROOT}/build/lib/MinSizeRel/jerry-port.lib")
endif()
include("${_worker_repo}/cmake/jellyframe_script_backend_jerryscript.cmake")
file(COPY "${_worker_repo}/src/script/script_task_worker_runtime.cpp"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/worker/src/script")
file(COPY "${_worker_repo}/src/script/tests/script_task_worker_runtime_tests.cpp"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/worker/src/script/tests")
# Stage private script headers from one inventory. configure_file also tracks
# edits to existing headers, while CONFIGURE_DEPENDS detects new headers.
file(GLOB _worker_script_headers CONFIGURE_DEPENDS "${_worker_repo}/src/script/*.h")
foreach(_worker_header IN LISTS _worker_script_headers)
  get_filename_component(_worker_header_name "${_worker_header}" NAME)
  configure_file("${_worker_header}"
    "${CMAKE_CURRENT_BINARY_DIR}/worker/src/script/${_worker_header_name}" COPYONLY)
endforeach()
file(COPY "${_worker_repo}/src/app_runtime"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/worker/src")
file(COPY "${_worker_repo}/src/device_runtime_contracts"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/worker/src")
set(_worker_source "${CMAKE_CURRENT_BINARY_DIR}/worker/src")
add_executable(runtime_canonical_v9_worker_consumer
  worker_main.cpp
  "${_worker_source}/script/script_task_worker_runtime.cpp"
  "${_worker_source}/script/tests/script_task_worker_runtime_tests.cpp"
  "${_worker_repo}/src/script/jerryscript_runtime.cpp"
  "${_worker_repo}/src/app_runtime/script_task_contract.cpp"
  "${_worker_repo}/src/app_runtime/script_task_frame_codec.cpp"
  "${_worker_repo}/src/app_runtime/script_task_frame_renderer.cpp"
  "${_worker_repo}/src/app_runtime/runtime_font_catalog.cpp"
  "${_worker_repo}/src/app_runtime/runtime_image_provider.cpp"
  "${_worker_repo}/src/app_runtime/script_task_input_codec.cpp"
  "${_worker_repo}/src/app_runtime/script_task_input_dispatch.cpp"
  "${_worker_repo}/src/app_runtime/script_task_service_request_codec.cpp"
  "${_worker_repo}/src/app_runtime/script_task_service_bridge.cpp"
  "${_worker_repo}/src/app_runtime/script_task_worker_inbox.cpp"
  "${_worker_repo}/src/app_runtime/script_task_fatal_codec.cpp"
  "${_worker_repo}/src/app_runtime/app_budget.cpp"
  "${_worker_repo}/src/app_runtime/app_capability_broker.cpp"
  "${_worker_repo}/src/app_runtime/app_compute_jobs.cpp"
  "${_worker_repo}/src/app_runtime/app_device_services.cpp"
  "${_worker_repo}/src/app_runtime/app_font_set.cpp"
  "${_worker_repo}/src/app_runtime/app_frame_policy.cpp"
  "${_worker_repo}/src/app_runtime/app_host.cpp"
  "${_worker_repo}/src/app_runtime/app_host_data.cpp"
  "${_worker_repo}/src/app_runtime/app_installed_bundle.cpp"
  "${_worker_repo}/src/app_runtime/app_lifecycle.cpp"
  "${_worker_repo}/src/app_runtime/app_load_telemetry.cpp"
  "${_worker_repo}/src/app_runtime/app_service_worker.cpp"
  "${_worker_repo}/src/app_runtime/app_video_frames.cpp"
  "${_worker_repo}/src/app_runtime/app_services.cpp"
  "${_worker_repo}/src/app_runtime/app_storage_lifecycle_policy.cpp"
  "${_worker_repo}/src/app_runtime/authorized_file_broker.cpp"
  "${_worker_repo}/src/app_runtime/host_services.cpp"
  "${_worker_repo}/src/app_runtime/system_events.cpp"
  "${_worker_repo}/src/app_runtime/xml_http_request.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_bundle.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_install_transaction.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_runtime_protocol.cpp")
target_include_directories(runtime_canonical_v9_worker_consumer BEFORE PRIVATE
  "${_jellyframe_core_prefix}/include")
target_include_directories(runtime_canonical_v9_worker_consumer PRIVATE
  "${_worker_source}" "${JERRYSCRIPT_INCLUDE_DIR}")
target_link_libraries(runtime_canonical_v9_worker_consumer PRIVATE
  JellyFrame::jellyframe_render_core ${JERRYSCRIPT_LIBRARIES})
target_compile_features(runtime_canonical_v9_worker_consumer PRIVATE cxx_std_17)
target_compile_definitions(runtime_canonical_v9_worker_consumer PRIVATE
  JELLYFRAME_ENABLE_SCRIPTING=1 JELLYFRAME_SCRIPT_TASK_RUNTIME_ENABLED=1
  JELLYFRAME_SCRIPT_ENGINE_JERRYSCRIPT=1 JELLYFRAME_TEST_ASSERTS_ENABLED=1)
target_include_directories(runtime_canonical_v9_worker_consumer PRIVATE "${JERRYSCRIPT_INCLUDE_DIR}")
if(MSVC)
  target_compile_options(runtime_canonical_v9_worker_consumer PRIVATE /UNDEBUG)
else()
  target_compile_options(runtime_canonical_v9_worker_consumer PRIVATE -UNDEBUG)
endif()
add_test(NAME installed_runtime_canonical_v9_worker COMMAND runtime_canonical_v9_worker_consumer)

add_executable(runtime_canonical_v9_script_api_consumer
  script_api_main.cpp
  "${_worker_repo}/src/script/tests/script_runtime_tests.cpp"
  "${_worker_repo}/src/script/jerryscript_runtime.cpp"
  "${_worker_repo}/src/app_runtime/runtime_font_catalog.cpp"
  "${_worker_repo}/src/app_runtime/runtime_image_provider.cpp"
  "${_worker_repo}/src/app_runtime/app_host.cpp"
  "${_worker_repo}/src/app_runtime/app_host_data.cpp"
  "${_worker_repo}/src/app_runtime/app_lifecycle.cpp"
  "${_worker_repo}/src/app_runtime/app_budget.cpp"
  "${_worker_repo}/src/app_runtime/app_capability_broker.cpp"
  "${_worker_repo}/src/app_runtime/app_compute_jobs.cpp"
  "${_worker_repo}/src/app_runtime/app_device_services.cpp"
  "${_worker_repo}/src/app_runtime/app_font_set.cpp"
  "${_worker_repo}/src/app_runtime/app_frame_policy.cpp"
  "${_worker_repo}/src/app_runtime/app_installed_bundle.cpp"
  "${_worker_repo}/src/app_runtime/app_load_telemetry.cpp"
  "${_worker_repo}/src/app_runtime/app_service_worker.cpp"
  "${_worker_repo}/src/app_runtime/app_video_frames.cpp"
  "${_worker_repo}/src/app_runtime/app_services.cpp"
  "${_worker_repo}/src/app_runtime/app_storage_lifecycle_policy.cpp"
  "${_worker_repo}/src/app_runtime/authorized_file_broker.cpp"
  "${_worker_repo}/src/app_runtime/host_services.cpp"
  "${_worker_repo}/src/app_runtime/system_events.cpp"
  "${_worker_repo}/src/app_runtime/xml_http_request.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_bundle.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_install_transaction.cpp"
  "${_worker_repo}/src/device_runtime_contracts/device_runtime_protocol.cpp")
set_source_files_properties("${_worker_repo}/src/script/tests/script_runtime_tests.cpp"
  PROPERTIES COMPILE_DEFINITIONS "main=script_runtime_tests_main")
target_include_directories(runtime_canonical_v9_script_api_consumer BEFORE PRIVATE
  "${_jellyframe_core_prefix}/include")
target_include_directories(runtime_canonical_v9_script_api_consumer PRIVATE
  "${_worker_source}" "${JERRYSCRIPT_INCLUDE_DIR}")
target_link_libraries(runtime_canonical_v9_script_api_consumer PRIVATE
  JellyFrame::jellyframe_render_core ${JERRYSCRIPT_LIBRARIES})
target_compile_features(runtime_canonical_v9_script_api_consumer PRIVATE cxx_std_17)
target_compile_definitions(runtime_canonical_v9_script_api_consumer PRIVATE
  JELLYFRAME_ENABLE_SCRIPTING=1 JELLYFRAME_SCRIPT_ENGINE_JERRYSCRIPT=1
  JELLYFRAME_TEST_ASSERTS_ENABLED=1)
if(MSVC)
  target_compile_options(runtime_canonical_v9_script_api_consumer PRIVATE /UNDEBUG)
else()
  target_compile_options(runtime_canonical_v9_script_api_consumer PRIVATE -UNDEBUG)
endif()
add_test(NAME installed_runtime_canonical_v9_script_api COMMAND runtime_canonical_v9_script_api_consumer)
