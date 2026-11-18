cmake_minimum_required(VERSION 3.21)
project(jfsg_scalar_pipeline_qualification LANGUAGES C CXX)
set(CMAKE_NO_SYSTEM_FROM_IMPORTED ON)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(runtime_root "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
foreach(option JELLYFRAME_BUILD_EXAMPLES JELLYFRAME_BUILD_TESTS JELLYFRAME_BUILD_BENCHMARKS
        JELLYFRAME_BUILD_RENDER_CORE_TESTS JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS)
    set(${option} OFF CACHE BOOL "Qualification" FORCE)
endforeach()
foreach(option JELLYFRAME_BUILD_APP_RUNTIME JELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS
        JELLYFRAME_BUILD_SCRIPTING JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
    set(${option} ON CACHE BOOL "Qualification" FORCE)
endforeach()
add_subdirectory("${runtime_root}" runtime)
add_subdirectory(../package_signature signature)
set(port "${runtime_root}/ports/esp32s3-idf")
add_library(jfsg_scalar_store OBJECT "${port}/main/jellyframe_esp32s3_device_image_store.cpp")
target_compile_definitions(jfsg_scalar_store PRIVATE verify_device_package_signature=verify_host_fixture_signature)
target_include_directories(jfsg_scalar_store PRIVATE ../signed_install/stubs "${runtime_root}/src")
target_link_libraries(jfsg_scalar_store PRIVATE jellyframe_app_runtime jfsg_mbedcrypto)
add_executable(jfsg_scalar_pipeline_tests signed_scalar_pipeline_tests.cpp $<TARGET_OBJECTS:jfsg_scalar_store>
    "${port}/main/jellyframe_esp32s3_package_signature.cpp"
    "${port}/main/jellyframe_esp32s3_resources.cpp"
    "${port}/main/jellyframe_esp32s3_installed_resource_owner.cpp"
    "${port}/main/jellyframe_esp32s3_image.cpp")
target_include_directories(jfsg_scalar_pipeline_tests PRIVATE ../signed_install ../signed_install/stubs
    ../package_signature "${port}/main")
target_compile_definitions(jfsg_scalar_pipeline_tests PRIVATE
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM=1
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER=1
    JELLYFRAME_PACKAGE_SIGNATURE_TESTING=1)
target_link_libraries(jfsg_scalar_pipeline_tests PRIVATE jellyframe_script_task_runtime jellyframe_script jfsg_mbedcrypto)
if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package")
    get_target_property(core_includes jellyframe_app_runtime INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(jfsg_scalar_pipeline_tests BEFORE PRIVATE ${core_includes})
    target_include_directories(jfsg_scalar_store BEFORE PRIVATE ${core_includes})
endif()
set(fixture "${CMAKE_CURRENT_BINARY_DIR}/scalar.jfsg")
add_custom_command(OUTPUT "${fixture}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_fixture.py" "${fixture}"
    DEPENDS generate_fixture.py "${runtime_root}/tools/package_app.py" "${runtime_root}/tools/sign_app_package.py"
        "${runtime_root}/tools/app_registry.py" "${runtime_root}/tools/jellyframe_versions.py"
        "${runtime_root}/VERSION" "${runtime_root}/cmake/jellyframe_dependency_lock.cmake"
        "${runtime_root}/samples/apps/packages/jelly_font_policy/jellyframe.app.json"
    VERBATIM)
add_custom_target(jfsg_scalar_fixture DEPENDS "${fixture}")
add_dependencies(jfsg_scalar_pipeline_tests jfsg_scalar_fixture)
add_test(NAME jfsg_scalar_installed_frame13 COMMAND jfsg_scalar_pipeline_tests "${fixture}"
    "${port}/font/core-system-noto-regular-16.jffont2")
