cmake_minimum_required(VERSION 3.21)
project(jfsg_shaped_pipeline_qualification LANGUAGES C CXX)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
foreach(option JELLYFRAME_BUILD_EXAMPLES JELLYFRAME_BUILD_TESTS JELLYFRAME_BUILD_BENCHMARKS JELLYFRAME_BUILD_RENDER_CORE_TESTS JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS)
    set(${option} OFF CACHE BOOL "Qualification" FORCE)
endforeach()
foreach(option JELLYFRAME_BUILD_APP_RUNTIME JELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS JELLYFRAME_BUILD_SCRIPTING JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME JELLYFRAME_ENABLE_TEXT_SHAPING)
    set(${option} ON CACHE BOOL "Qualification" FORCE)
endforeach()
if(NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_executor_catalog/regular.jfcat1")
    message(FATAL_ERROR "Set JELLYFRAME_RENDER_CORE_SOURCE_DIR to matching shaping Core with real fixtures")
endif()
add_subdirectory("${runtime_root}" runtime)
add_subdirectory(../package_signature signature)
add_library(jfsg_shaped_store OBJECT "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_device_image_store.cpp")
target_compile_definitions(jfsg_shaped_store PRIVATE
    CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=2048
    verify_device_package_signature=verify_host_fixture_signature)
target_include_directories(jfsg_shaped_store PRIVATE ../signed_install/stubs "${runtime_root}/src")
target_link_libraries(jfsg_shaped_store PRIVATE jellyframe_app_runtime jfsg_mbedcrypto)
add_executable(jfsg_shaped_pipeline_tests signed_shaped_pipeline_tests.cpp allocation_fault.cpp $<TARGET_OBJECTS:jfsg_shaped_store>
    "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_package_signature.cpp"
    "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_resources.cpp"
    "${runtime_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_installed_resource_owner.cpp")
target_include_directories(jfsg_shaped_pipeline_tests PRIVATE ../signed_install ../signed_install/stubs
    ../package_signature "${runtime_root}/ports/esp32s3-idf/main")
target_compile_definitions(jfsg_shaped_pipeline_tests PRIVATE
    CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_MAX_PACKAGE_KIB=2048
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM=1
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER=0
    JELLYFRAME_PACKAGE_SIGNATURE_TESTING=1 JFSG_HOST_PARTITION_BYTES=6291456)
target_compile_options(jfsg_shaped_pipeline_tests PRIVATE -UNDEBUG)
target_link_libraries(jfsg_shaped_pipeline_tests PRIVATE jellyframe_script_task_runtime jellyframe_script jfsg_mbedcrypto)
# Changing a declared package input creates a fresh evidence directory. Never silently
# reuse a previous signed package or overwrite its bytes during an incremental build.
set(fixture_inputs
    "${runtime_root}/project_tools/prepare_signed_shaped_fixture.py"
    "${runtime_root}/tools/package_app.py"
    "${runtime_root}/tools/sign_app_package.py"
    "${runtime_root}/tools/app_registry.py"
    "${runtime_root}/tools/jellyframe_versions.py"
    "${runtime_root}/tools/svg_rasterize.py"
    "${runtime_root}/project_tools/render_core_feature_registry.py"
    "${runtime_root}/VERSION"
    "${runtime_root}/cmake/jellyframe_dependency_lock.cmake"
    "${runtime_root}/tools/device_install_package.py"
    "${runtime_root}/samples/apps/packages/jelly_font_policy/jellyframe.app.json"
    "${runtime_root}/ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2"
    "${runtime_root}/ports/esp32s3-idf/font/LICENSE-NOTO-SANS-SC.txt"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_executor_catalog/manifest.json"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_executor_catalog/regular.jfcat1"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_resource/manifest.json"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_resource/regular.jfshr2"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_normalization/unicode14.bin"
    "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures/shaping_normalization/regular.eligibility")
set(fixture_identity "")
foreach(input IN LISTS fixture_inputs)
    file(SHA256 "${input}" input_hash)
    string(APPEND fixture_identity "${input_hash}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${fixture_inputs})
string(SHA256 fixture_identity "${fixture_identity}")
string(SUBSTRING "${fixture_identity}" 0 16 fixture_identity)
set(fixtures "${CMAKE_CURRENT_BINARY_DIR}/fixtures-${fixture_identity}")
add_custom_command(OUTPUT "${fixtures}/package-evidence.json"
    COMMAND "${Python3_EXECUTABLE}" "${runtime_root}/project_tools/prepare_signed_shaped_fixture.py"
        --core-fixtures "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures"
        --font "${runtime_root}/ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2"
        --output "${fixtures}" --max-package-bytes 2093056 --public-test-signature
    DEPENDS ${fixture_inputs})
add_custom_target(jfsg_shaped_fixtures DEPENDS "${fixtures}/package-evidence.json")
add_dependencies(jfsg_shaped_pipeline_tests jfsg_shaped_fixtures)
foreach(case pipeline reboot tamper capacity resource_budget binding_oom worker_init_oom worker_mutation_oom worker_input_oom worker_fatal_oom)
    add_test(NAME jfsg_shaped_${case} COMMAND jfsg_shaped_pipeline_tests ${case} "${fixtures}")
endforeach()
add_test(NAME jfsg_shaped_provider_transport COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/provider_transport_tests.py" "${fixtures}")
