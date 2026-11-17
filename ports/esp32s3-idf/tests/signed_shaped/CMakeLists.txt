cmake_minimum_required(VERSION 3.21)
project(jfsg_shaped_pipeline_qualification LANGUAGES C CXX)
# Keep the selected package headers ahead of Runtime's bundled legacy Core.
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
foreach(option JELLYFRAME_BUILD_EXAMPLES JELLYFRAME_BUILD_TESTS JELLYFRAME_BUILD_BENCHMARKS JELLYFRAME_BUILD_RENDER_CORE_TESTS JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS)
    set(${option} OFF CACHE BOOL "Qualification" FORCE)
endforeach()
foreach(option JELLYFRAME_BUILD_APP_RUNTIME JELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS JELLYFRAME_BUILD_SCRIPTING JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME JELLYFRAME_ENABLE_TEXT_SHAPING)
    set(${option} ON CACHE BOOL "Qualification" FORCE)
endforeach()
set(JFSG_CORE_FIXTURES_DIR "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}/tests/fixtures" CACHE PATH
    "Offline exact Core fixture assets; not a private include or source fallback")
if(NOT EXISTS "${JFSG_CORE_FIXTURES_DIR}/shaping_executor_catalog/regular.jfcat1")
    message(FATAL_ERROR "Set JFSG_CORE_FIXTURES_DIR to matching complete Core fixtures")
endif()
add_subdirectory("${runtime_root}" runtime)
file(READ "${CMAKE_CURRENT_BINARY_DIR}/runtime/generated/jellyframe_render_core_provenance.json" core_provenance)
string(JSON qualification_core_abi GET "${core_provenance}" engineAbi)
string(JSON qualification_core_version GET "${core_provenance}" packageVersion)
if(JELLYFRAME_ENABLE_CONTEXTUAL_TEXT OR JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT)
    if(NOT JELLYFRAME_ENABLE_CONTEXTUAL_TEXT OR NOT JELLYFRAME_ENABLE_CONTEXTUAL_LAYOUT OR
       NOT JELLYFRAME_ENABLE_SHAPED_PACKET_V2 OR NOT qualification_core_abi STREQUAL "9")
        message(FATAL_ERROR "Contextual signed qualification requires matching ABI9 text/layout/JFSP2")
    endif()
    set(fixture_pipeline jfsp2-block-v12)
    set(qualification_contextual 1)
    set(fixture_font "${JFSG_CORE_FIXTURES_DIR}/contextual_font/regular.jffont2")
    set(fixture_candidate_args --candidate-core-version "${qualification_core_version}")
else()
    set(fixture_pipeline jfsp1-block-v11)
    set(qualification_contextual 0)
    set(fixture_font "${runtime_root}/ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2")
    set(fixture_candidate_args "")
endif()
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
    JFSG_EXPECT_CONTEXTUAL=${qualification_contextual}
    JELLYFRAME_PACKAGE_SIGNATURE_TESTING=1 JFSG_HOST_PARTITION_BYTES=6291456)
target_compile_options(jfsg_shaped_pipeline_tests PRIVATE -UNDEBUG)
target_link_libraries(jfsg_shaped_pipeline_tests PRIVATE jellyframe_script_task_runtime jellyframe_script jfsg_mbedcrypto)
if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package")
    # Imported Core target is scoped to the Runtime subdirectory; its public
    # consumer exports the selected package-first include order to this parent.
    get_target_property(qualification_core_includes jellyframe_app_runtime INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(jfsg_shaped_pipeline_tests BEFORE PRIVATE ${qualification_core_includes})
    target_include_directories(jfsg_shaped_store BEFORE PRIVATE ${qualification_core_includes})
endif()
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
    "${runtime_root}/tools/device_render_core_capabilities.py"
    "${runtime_root}/samples/apps/packages/jelly_font_policy/jellyframe.app.json"
    "${fixture_font}"
    "${runtime_root}/ports/esp32s3-idf/font/LICENSE-NOTO-SANS-SC.txt"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_executor_catalog/manifest.json"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_executor_catalog/regular.jfcat1"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_resource/manifest.json"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_resource/regular.jfshr2"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_normalization/unicode14.bin"
    "${JFSG_CORE_FIXTURES_DIR}/shaping_normalization/regular.eligibility")
if(fixture_pipeline STREQUAL "jfsp2-block-v12")
    list(APPEND fixture_inputs "${JFSG_CORE_FIXTURES_DIR}/contextual_font/manifest.json")
endif()
set(fixture_identity "${fixture_pipeline};${fixture_candidate_args}")
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
        --core-fixtures "${JFSG_CORE_FIXTURES_DIR}"
        --font "${fixture_font}" --text-pipeline "${fixture_pipeline}" ${fixture_candidate_args}
        --output "${fixtures}" --max-package-bytes 2093056 --public-test-signature
    DEPENDS ${fixture_inputs})
add_custom_target(jfsg_shaped_fixtures DEPENDS "${fixtures}/package-evidence.json")
add_dependencies(jfsg_shaped_pipeline_tests jfsg_shaped_fixtures)
foreach(case pipeline reboot tamper capacity resource_budget binding_oom worker_init_oom worker_mutation_oom worker_input_oom worker_fatal_oom)
    add_test(NAME jfsg_shaped_${case} COMMAND jfsg_shaped_pipeline_tests ${case} "${fixtures}")
endforeach()
add_test(NAME jfsg_shaped_provider_transport COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/provider_transport_tests.py" "${fixtures}")
