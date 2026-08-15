cmake_minimum_required(VERSION 3.16)

project(JellyFrameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JELLYFRAME_BUILD_EXAMPLES "Build JellyFrame native validation and dump tools" ON)
option(JELLYFRAME_BUILD_TESTS "Build JellyFrame regression tests" ON)
option(JELLYFRAME_BUILD_BENCHMARKS "Build JellyFrame microbenchmarks" ON)
option(JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS "Register sample and external-trial CTest coverage" ON)
option(JELLYFRAME_BUILD_APP_RUNTIME "Build the JellyFrame App Runtime layer" ON)
option(JELLYFRAME_INSTALL_RENDER_CORE "Install the standalone Render Core CMake package" OFF)
set(JELLYFRAME_RENDER_CORE_PROVIDER "in-tree" CACHE STRING
    "Render Core provider: in-tree or package")
set_property(CACHE JELLYFRAME_RENDER_CORE_PROVIDER PROPERTY STRINGS in-tree package)
set(JELLYFRAME_RENDER_CORE_PACKAGE_DIR "" CACHE PATH
    "Prefix containing the locked JellyFrameRenderCore CMake package")
set(JELLYFRAME_RENDER_CORE_SOURCE_DIR "" CACHE PATH
    "Optional Render Core source checkout used by the in-tree provider")
if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package")
    set(_JELLYFRAME_DEFAULT_BUILD_RENDER_CORE_TESTS OFF)
else()
    set(_JELLYFRAME_DEFAULT_BUILD_RENDER_CORE_TESTS ON)
endif()
option(JELLYFRAME_BUILD_RENDER_CORE_TESTS
       "Build Render Core source regression tests from this checkout"
       ${_JELLYFRAME_DEFAULT_BUILD_RENDER_CORE_TESTS})
unset(_JELLYFRAME_DEFAULT_BUILD_RENDER_CORE_TESTS)
option(JELLYFRAME_BUILD_SCRIPTING "Build optional JerryScript scripting bridge" OFF)
option(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME
       "Compile optional cross-task script App runtime contracts" OFF)
if(DEFINED JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME)
    message(FATAL_ERROR
        "JELLYFRAME_ENABLE_SCRIPT_TASK_RUNTIME was replaced by "
        "JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME; remove the stale CMake cache entry and reconfigure.")
endif()
option(JELLYFRAME_ENABLE_IMAGE_FILE_IO "Expose desktop image file writers from render_core" ON)
option(JELLYFRAME_ENABLE_CANVAS2D "Compile the graphics.canvas2d Render Core family" ON)
option(JELLYFRAME_ENABLE_MODERN_PAINT "Compile the css.modern-paint Render Core family" ON)
option(JELLYFRAME_ENABLE_FLEX_GRID "Compile the css.flex-grid Render Core family" ON)
option(JELLYFRAME_ENABLE_ADVANCED_FORMS "Compile the forms.advanced Render Core family" ON)
option(JELLYFRAME_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer on supported toolchains" OFF)

include(GNUInstallDirs)

if(JELLYFRAME_BUILD_SCRIPTING AND NOT JELLYFRAME_BUILD_APP_RUNTIME)
    message(FATAL_ERROR
        "JELLYFRAME_BUILD_SCRIPTING requires JELLYFRAME_BUILD_APP_RUNTIME=ON")
endif()

if(NOT JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "in-tree" AND
   NOT JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package")
    message(FATAL_ERROR
        "JELLYFRAME_RENDER_CORE_PROVIDER must be in-tree or package")
endif()
if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package" AND JELLYFRAME_INSTALL_RENDER_CORE)
    message(FATAL_ERROR
        "JELLYFRAME_INSTALL_RENDER_CORE is only valid with the in-tree Render Core provider")
endif()
if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "package" AND JELLYFRAME_RENDER_CORE_SOURCE_DIR)
    message(FATAL_ERROR
        "JELLYFRAME_RENDER_CORE_SOURCE_DIR is only valid with the in-tree Render Core provider")
endif()

if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME AND NOT JELLYFRAME_BUILD_SCRIPTING)
    message(FATAL_ERROR
        "JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME requires JELLYFRAME_BUILD_SCRIPTING=ON")
endif()

if(JELLYFRAME_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR
            "JELLYFRAME_ENABLE_SANITIZERS requires a Clang/GCC-style toolchain; "
            "use the dedicated MSVC AddressSanitizer build or configure with clang-cl.")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)

    if(WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        execute_process(
            COMMAND ${CMAKE_CXX_COMPILER} --print-file-name=clang_rt.asan_dynamic-x86_64.dll
            OUTPUT_VARIABLE JELLYFRAME_CLANG_ASAN_RUNTIME
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT EXISTS "${JELLYFRAME_CLANG_ASAN_RUNTIME}")
            message(FATAL_ERROR
                "Clang sanitizer runtime was not found: ${JELLYFRAME_CLANG_ASAN_RUNTIME}")
        endif()
    endif()
endif()

function(jellyframe_stage_sanitizer_runtime target)
    if(JELLYFRAME_ENABLE_SANITIZERS AND WIN32 AND JELLYFRAME_CLANG_ASAN_RUNTIME)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${JELLYFRAME_CLANG_ASAN_RUNTIME}"
                    "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

set(JERRYSCRIPT_ROOT "" CACHE PATH "JerryScript source or install root")
set(JERRYSCRIPT_INCLUDE_DIR "" CACHE PATH "JerryScript include directory")
set(JERRYSCRIPT_LIBRARIES "" CACHE STRING "JerryScript libraries")

if(JERRYSCRIPT_ROOT)
    set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "")
    if(EXISTS "${JERRYSCRIPT_ROOT}/jerry-core/include/jerryscript.h")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/jerry-core/include")
    elseif(EXISTS "${JERRYSCRIPT_ROOT}/include/jerryscript.h")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/include")
    endif()

    if(_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR AND
            (NOT JERRYSCRIPT_INCLUDE_DIR OR
             NOT EXISTS "${JERRYSCRIPT_INCLUDE_DIR}/jerryscript.h"))
        set(JERRYSCRIPT_INCLUDE_DIR "${_JELLYFRAME_DETECTED_JERRYSCRIPT_INCLUDE_DIR}" CACHE PATH
            "JerryScript include directory" FORCE)
    endif()

    if(NOT JERRYSCRIPT_LIBRARIES)
        set(_JELLYFRAME_JERRYSCRIPT_LIBRARY_DIRS
            "${JERRYSCRIPT_ROOT}/build/lib/Release"
            "${JERRYSCRIPT_ROOT}/build/lib/MinSizeRel"
            "${JERRYSCRIPT_ROOT}/build/lib/RelWithDebInfo"
            "${JERRYSCRIPT_ROOT}/build/lib/Debug")
        set(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES "")
        foreach(_JELLYFRAME_JERRYSCRIPT_LIBRARY_NAME IN ITEMS jerry-core jerry-ext jerry-port)
            unset(_JELLYFRAME_JERRYSCRIPT_LIBRARY CACHE)
            unset(_JELLYFRAME_JERRYSCRIPT_LIBRARY)
            find_library(_JELLYFRAME_JERRYSCRIPT_LIBRARY
                NAMES ${_JELLYFRAME_JERRYSCRIPT_LIBRARY_NAME}
                PATHS ${_JELLYFRAME_JERRYSCRIPT_LIBRARY_DIRS}
                NO_DEFAULT_PATH)
            if(NOT _JELLYFRAME_JERRYSCRIPT_LIBRARY)
                set(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES "")
                break()
            endif()
            list(APPEND _JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES
                 "${_JELLYFRAME_JERRYSCRIPT_LIBRARY}")
        endforeach()
        if(_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES)
            set(JERRYSCRIPT_LIBRARIES "${_JELLYFRAME_DETECTED_JERRYSCRIPT_LIBRARIES}" CACHE STRING
                "JerryScript libraries" FORCE)
        endif()
    endif()
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jellyframe_runtime_version.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jellyframe_render_core_provider.cmake")

add_library(jellyframe_device_runtime_contracts
    src/device_runtime_contracts/device_install_transaction.cpp
    src/device_runtime_contracts/device_runtime_protocol.cpp
)
add_library(JellyFrame::device_runtime_contracts ALIAS jellyframe_device_runtime_contracts)
target_include_directories(jellyframe_device_runtime_contracts
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(jellyframe_device_runtime_contracts PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(jellyframe_device_runtime_contracts PRIVATE /W4)
else()
    target_compile_options(jellyframe_device_runtime_contracts PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(JELLYFRAME_BUILD_APP_RUNTIME)
add_library(jellyframe_app_runtime
    src/app_runtime/app_budget.cpp
    src/app_runtime/app_capability_broker.cpp
    src/app_runtime/app_compute_jobs.cpp
    src/app_runtime/app_device_services.cpp
    src/app_runtime/app_font_set.cpp
    src/app_runtime/app_frame_policy.cpp
    src/app_runtime/app_host.cpp
    src/app_runtime/app_host_data.cpp
    src/app_runtime/app_lifecycle.cpp
    src/app_runtime/app_load_telemetry.cpp
    src/app_runtime/app_service_worker.cpp
    src/app_runtime/app_video_frames.cpp
    src/app_runtime/app_services.cpp
    src/app_runtime/app_storage_lifecycle_policy.cpp
    src/app_runtime/authorized_file_broker.cpp
    src/app_runtime/host_services.cpp
    src/app_runtime/system_events.cpp
    src/app_runtime/xml_http_request.cpp
)

target_link_libraries(jellyframe_app_runtime
    PUBLIC jellyframe_render_core
    PRIVATE jellyframe_device_runtime_contracts)
target_include_directories(jellyframe_app_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(jellyframe_app_runtime
    PUBLIC
        JELLYFRAME_RUNTIME_ACTIVE_RELEASE_VERSION="${JELLYFRAME_RUNTIME_ACTIVE_RELEASE_VERSION}"
        JELLYFRAME_RENDER_CORE_ACTIVE_PACKAGE_VERSION="${JELLYFRAME_RENDER_CORE_PACKAGE_VERSION}"
)
target_compile_features(jellyframe_app_runtime PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(jellyframe_app_runtime PRIVATE /W4)
else()
    target_compile_options(jellyframe_app_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()
endif()

if(JELLYFRAME_BUILD_SCRIPTING)
    if(NOT JERRYSCRIPT_INCLUDE_DIR OR NOT JERRYSCRIPT_LIBRARIES)
        message(FATAL_ERROR
            "JELLYFRAME_BUILD_SCRIPTING requires JERRYSCRIPT_INCLUDE_DIR and JERRYSCRIPT_LIBRARIES")
    endif()
    if(NOT EXISTS "${JERRYSCRIPT_INCLUDE_DIR}/jerryscript.h")
        message(FATAL_ERROR
            "JERRYSCRIPT_INCLUDE_DIR does not contain jerryscript.h: ${JERRYSCRIPT_INCLUDE_DIR}\n"
            "If configuring from PowerShell, use $PWD instead of cmd.exe-only %CD%.")
    endif()
    foreach(_JELLYFRAME_JERRYSCRIPT_LIBRARY IN LISTS JERRYSCRIPT_LIBRARIES)
        if(NOT EXISTS "${_JELLYFRAME_JERRYSCRIPT_LIBRARY}")
            message(FATAL_ERROR
                "JerryScript library does not exist: ${_JELLYFRAME_JERRYSCRIPT_LIBRARY}\n"
                "If configuring from PowerShell, use $PWD instead of cmd.exe-only %CD%.")
        endif()
    endforeach()

    if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
        add_library(jellyframe_script_task_runtime
            src/app_runtime/script_task_contract.cpp
            src/app_runtime/script_task_frame_codec.cpp
            src/app_runtime/script_task_frame_renderer.cpp
            src/app_runtime/script_task_input_codec.cpp
            src/app_runtime/script_task_input_dispatch.cpp
            src/app_runtime/script_task_service_request_codec.cpp
            src/app_runtime/script_task_service_bridge.cpp
            src/app_runtime/script_task_worker_inbox.cpp
            src/app_runtime/script_task_fatal_codec.cpp
        )
        target_link_libraries(jellyframe_script_task_runtime PUBLIC jellyframe_app_runtime)
        target_include_directories(jellyframe_script_task_runtime
            PUBLIC
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_compile_features(jellyframe_script_task_runtime PUBLIC cxx_std_17)
        if(MSVC)
            target_compile_options(jellyframe_script_task_runtime PRIVATE /W4)
        else()
            target_compile_options(jellyframe_script_task_runtime PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()

    set(JELLYFRAME_SCRIPT_SOURCES
        src/script/jerryscript_runtime.cpp
    )
    if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
        list(APPEND JELLYFRAME_SCRIPT_SOURCES
            src/script/script_task_worker_runtime.cpp
        )
    endif()
    add_library(jellyframe_script ${JELLYFRAME_SCRIPT_SOURCES})
    target_link_libraries(jellyframe_script PUBLIC jellyframe_app_runtime ${JERRYSCRIPT_LIBRARIES})
    if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
        target_link_libraries(jellyframe_script PUBLIC jellyframe_script_task_runtime)
    endif()
    target_include_directories(jellyframe_script
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${JERRYSCRIPT_INCLUDE_DIR}
    )
    target_compile_features(jellyframe_script PUBLIC cxx_std_17)
    target_compile_definitions(jellyframe_script PUBLIC JELLYFRAME_ENABLE_SCRIPTING=1)

    if(MSVC)
        target_compile_options(jellyframe_script PRIVATE /W4)
    else()
        target_compile_options(jellyframe_script PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

function(add_jellyframe_tool target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE jellyframe_render_core)
endfunction()

function(jellyframe_enable_test_asserts target)
    target_compile_definitions(${target} PRIVATE JELLYFRAME_TEST_ASSERTS_ENABLED=1)
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:JELLYFRAME_TEST_CONFIG_DEBUG=1>)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endfunction()

if(JELLYFRAME_BUILD_EXAMPLES)
    add_jellyframe_tool(jellyframe_demo tools/native/basic_demo.cpp)
    add_jellyframe_tool(jellyframe_dom_dump tools/native/dom_dump.cpp)
    add_jellyframe_tool(jellyframe_cssom_dump tools/native/cssom_dump.cpp)
    add_jellyframe_tool(jellyframe_style_dump tools/native/style_dump.cpp)
    add_jellyframe_tool(jellyframe_render_tree_dump tools/native/render_tree_dump.cpp)
    add_jellyframe_tool(jellyframe_layer_tree_dump tools/native/layer_tree_dump.cpp)
    add_jellyframe_tool(jellyframe_pipeline_dump tools/native/pipeline_dump.cpp)
    add_jellyframe_tool(jellyframe_pseudo_browser tools/native/pseudo_browser.cpp)
    add_jellyframe_tool(jellyframe_font_resource_check tools/native/font_resource_check.cpp)
    add_jellyframe_tool(jellyframe_font_pack_gen tools/native/font_pack_gen.cpp)
    add_jellyframe_tool(jellyframe_embedded_host_demo ports/embedded_host_demo/embedded_host_demo.cpp)
    if(JELLYFRAME_BUILD_SCRIPTING)
        target_link_libraries(jellyframe_pseudo_browser PRIVATE jellyframe_script)
    endif()
    if(WIN32)
        if(JELLYFRAME_BUILD_APP_RUNTIME)
            add_jellyframe_tool(jellyframe_desktop_shell tools/native/win32_browser.cpp)
        endif()
        if(JELLYFRAME_BUILD_APP_RUNTIME)
        target_link_libraries(jellyframe_desktop_shell PRIVATE jellyframe_app_runtime gdi32 user32 winmm)
        if(JELLYFRAME_BUILD_SCRIPTING)
            target_link_libraries(jellyframe_desktop_shell PRIVATE jellyframe_script)
        endif()
        endif()
    endif()
endif()

if(JELLYFRAME_BUILD_BENCHMARKS)
    add_jellyframe_tool(jellyframe_render_core_microbench src/render_core/benchmarks/microbench.cpp)
    jellyframe_enable_render_core_link_map(jellyframe_render_core_microbench)
    if(JELLYFRAME_BUILD_APP_RUNTIME)
        add_executable(jellyframe_app_runtime_microbench src/app_runtime/benchmarks/app_runtime_bench.cpp)
        target_link_libraries(jellyframe_app_runtime_microbench PRIVATE jellyframe_app_runtime)
    endif()
    add_jellyframe_tool(jellyframe_virtual_bench ports/virtual_board/virtual_bench.cpp)
    if(JELLYFRAME_BUILD_SCRIPTING)
        add_executable(jellyframe_script_microbench src/script/benchmarks/script_microbench.cpp)
        target_link_libraries(jellyframe_script_microbench PRIVATE jellyframe_script)
    endif()
endif()

if(JELLYFRAME_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(JELLYFRAME_BUILD_RENDER_CORE_TESTS)
        include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_tests.cmake")
    endif()

    if(JELLYFRAME_BUILD_APP_RUNTIME)
    set(JELLYFRAME_APP_RUNTIME_TEST_SOURCES
        src/app_runtime/tests/app_capability_broker_tests.cpp
        src/app_runtime/tests/app_compute_jobs_tests.cpp
        src/app_runtime/tests/app_device_services_tests.cpp
        src/app_runtime/tests/app_frame_policy_tests.cpp
        src/app_runtime/tests/app_host_data_tests.cpp
        src/app_runtime/tests/app_host_tests.cpp
        src/app_runtime/tests/app_lifecycle_tests.cpp
        src/app_runtime/tests/app_load_telemetry_tests.cpp
        src/app_runtime/tests/app_service_worker_tests.cpp
        src/app_runtime/tests/app_video_frames_tests.cpp
        src/app_runtime/tests/app_services_tests.cpp
        src/app_runtime/tests/app_storage_lifecycle_policy_tests.cpp
        src/app_runtime/tests/authorized_file_broker_tests.cpp
        src/app_runtime/tests/host_services_tests.cpp
        src/app_runtime/tests/system_events_tests.cpp
        src/app_runtime/tests/xml_http_request_tests.cpp
    )
    foreach(test_source IN LISTS JELLYFRAME_APP_RUNTIME_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        set_source_files_properties(${test_source}
            PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    endforeach()

    add_executable(jellyframe_app_runtime_tests
        src/app_runtime/tests/app_runtime_tests.cpp
        ${JELLYFRAME_APP_RUNTIME_TEST_SOURCES}
    )
    target_link_libraries(jellyframe_app_runtime_tests PRIVATE jellyframe_app_runtime)
    jellyframe_enable_test_asserts(jellyframe_app_runtime_tests)
    jellyframe_stage_sanitizer_runtime(jellyframe_app_runtime_tests)
    add_test(NAME jellyframe_app_runtime_tests COMMAND jellyframe_app_runtime_tests)

    endif()

    set(JELLYFRAME_DEVICE_RUNTIME_CONTRACT_TEST_SOURCES
        src/device_runtime_contracts/tests/device_install_transaction_tests.cpp
        src/device_runtime_contracts/tests/device_runtime_protocol_tests.cpp
    )
    foreach(test_source IN LISTS JELLYFRAME_DEVICE_RUNTIME_CONTRACT_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        set_source_files_properties(${test_source}
            PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    endforeach()
    add_executable(jellyframe_device_runtime_contracts_tests
        src/device_runtime_contracts/tests/device_runtime_contracts_tests.cpp
        ${JELLYFRAME_DEVICE_RUNTIME_CONTRACT_TEST_SOURCES}
    )
    target_link_libraries(jellyframe_device_runtime_contracts_tests
        PRIVATE jellyframe_device_runtime_contracts)
    jellyframe_enable_test_asserts(jellyframe_device_runtime_contracts_tests)
    jellyframe_stage_sanitizer_runtime(jellyframe_device_runtime_contracts_tests)
    add_test(NAME jellyframe_device_runtime_contracts_tests
        COMMAND jellyframe_device_runtime_contracts_tests)

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME jellyframe_device_reference_cli_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/device_reference_cli_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(
            NAME jellyframe_tool_package_preflight_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/package_preflight_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        if(JELLYFRAME_BUILD_RENDER_CORE_TESTS)
            add_test(
                NAME jellyframe_render_core_link_map_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/render_core_link_map_tests.py
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        get_property(_jellyframe_render_core_tests_map
            TARGET jellyframe_render_core_tests
            PROPERTY JELLYFRAME_RENDER_CORE_LINK_MAP)
        add_test(
            NAME jellyframe_render_core_generated_link_map
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/project_tools/check_render_core_link_map.py
                    --profile ${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_profile.json
                    --map ${_jellyframe_render_core_tests_map})
        set_tests_properties(jellyframe_render_core_generated_link_map PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        unset(_jellyframe_render_core_tests_map)

        add_test(
            NAME jellyframe_render_core_feature_profile_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/render_core_feature_profile_tests.py
                    --cmake ${CMAKE_COMMAND}
                    --source-root ${CMAKE_CURRENT_SOURCE_DIR}
                    --generator ${CMAKE_GENERATOR}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_render_core_feature_registry_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/render_core_feature_registry_tests.py
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        endif()

        add_test(
            NAME jellyframe_html_support_table_consistency
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/html_support_table_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_css_support_table_consistency
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/css_support_table_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_documentation_freshness
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/documentation_freshness_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_tool_app_registry_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/app_registry_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        if(JELLYFRAME_BUILD_EXAMPLES AND JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS)
            add_test(
                NAME jellyframe_pipeline_visual_diagnostics_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/pipeline_visual_diagnostics_tests.py
                        $<TARGET_FILE:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_template_trial_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/template_trial_tests.py
                        $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            if(WIN32)
                add_test(
                    NAME jellyframe_cli_external_trial_evidence
                    COMMAND ${Python3_EXECUTABLE}
                            ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                            trial
                            --build-dir $<TARGET_FILE_DIR:jellyframe_desktop_shell>
                            --output-dir ${CMAKE_BINARY_DIR}/test_outputs/external_trial
                            --clean)
                set_tests_properties(jellyframe_cli_external_trial_evidence PROPERTIES
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            endif()

            add_test(
                NAME jellyframe_cli_check_audio_smoke_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_audio_smoke
                        --target esp32s3-round-300
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_audio_smoke.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_service_status_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status
                        --target esp32s3-round-300
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_service_status.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_font_policy_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_font_policy
                        --target round-300
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_font_policy.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_wearable_launcher_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_wearable_launcher
                        --target round-300
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_wearable_launcher.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_band_system_shell
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/system/band_system_shell
                        --target rect-172x320
                        --report ${CMAKE_BINARY_DIR}/test_outputs/band_system_shell.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_canvas_smoke_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_smoke
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_canvas_smoke.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_canvas_gauges_package
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_gauges
                        --targets round-300,rect-320x240,rect-172x320
                        --report ${CMAKE_BINARY_DIR}/test_outputs/jelly_canvas_gauges.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                        --strict
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_check_watch_weather_responsive
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        check
                        --root ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/watch_weather
                        --target round-300
                        --targets round-300,rect-320x240
                        --report ${CMAKE_BINARY_DIR}/test_outputs/watch_weather.responsive.report.json
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_cli_doctor_sample_packages
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        doctor
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                        --report-dir ${CMAKE_BINARY_DIR}/test_outputs/doctor
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_cli_doctor_sample_packages PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "doctor summary: samples=[0-9]+ checked=[0-9]+ failed=0 diagnostics=[0-9]+/[0-9]+/[0-9]+")

            add_test(
                NAME jellyframe_cli_doctor_external_trial
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tools/jellyframe_cli.py
                        doctor
                        --trial
                        --build-dir $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
                        --report-dir ${CMAKE_BINARY_DIR}/test_outputs/external_trial
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_cli_doctor_external_trial PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "JellyFrame doctor: mode=external-trial samples=4")

        if(TARGET jellyframe_virtual_bench)
            add_test(
                NAME jellyframe_virtual_board_bench_smoke
                    COMMAND $<TARGET_FILE:jellyframe_virtual_bench>
                            300 300 8 3 40 0.85 40
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
                set_tests_properties(jellyframe_virtual_board_bench_smoke PROPERTIES
                    PASS_REGULAR_EXPRESSION
                        "scroll_strip_frame_estimate_fps")
            endif()
        endif()
    endif()

    if(WIN32 AND JELLYFRAME_BUILD_APP_RUNTIME AND JELLYFRAME_BUILD_EXAMPLES AND JELLYFRAME_BUILD_SAMPLE_REGRESSION_TESTS)
        set(JELLYFRAME_WIN32_BROWSER_CLI_TEST_ARGS
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/win32_browser_cli_tests.py
            $<TARGET_FILE:jellyframe_desktop_shell>)
        if(JELLYFRAME_BUILD_SCRIPTING)
            list(APPEND JELLYFRAME_WIN32_BROWSER_CLI_TEST_ARGS --scripting)
        endif()
        add_test(
            NAME jellyframe_desktop_shell_cli_contract
            COMMAND ${JELLYFRAME_WIN32_BROWSER_CLI_TEST_ARGS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_desktop_app_shell_workflow
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/desktop_app_shell_workflow_tests.py
                    $<TARGET_FILE:jellyframe_desktop_shell>
                    --output-dir ${CMAKE_BINARY_DIR}/test_outputs/desktop_app_shell_workflow
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_win32_flex_grid_capture
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/flex_grid_capture_tests.py
                    $<TARGET_FILE:jellyframe_desktop_shell>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_win32_service_status_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status/capture_system_events.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_service_status_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "service_activity network=18 audio=36 sensors=16 location=16 pause_audio=0 throttle_sensors=20 throttle_location=20")

        add_test(
            NAME jellyframe_win32_service_status_host_data_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status/capture_system_events.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_service_status_host_data_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "host_data battery=yes percent=88 charging=1 weather=rain temp_x10=213 activity=yes steps=6400 minutes=32 location=yes lat=31\\.2304 lon=121\\.474 accuracy_m=8 accelerometer=yes accel_x=0\\.1 gyroscope=no gyro_x=0 heart_rate=yes bpm=72 ambient_light=no lux=0")

        add_test(
            NAME jellyframe_win32_scroll_probe_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_probe/capture_wheel_scroll.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_probe_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_blits full=1 fast=4")

        add_test(
            NAME jellyframe_win32_scroll_container_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe/capture_scroll_container.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=3 strip_blits=0 full_repaints=0 dirty_repaints=3")

        add_test(
            NAME jellyframe_win32_scroll_container_touch_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe/capture_touch_scroll_container.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_touch_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=9 strip_blits=0 full_repaints=0 dirty_repaints=9 inertia=6")

        add_test(
            NAME jellyframe_win32_scroll_container_drag_no_inertia_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe/capture_touch_drag_no_inertia.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_drag_no_inertia_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=2 strip_blits=0 full_repaints=0 dirty_repaints=2 inertia=0")

        add_test(
            NAME jellyframe_win32_scroll_container_drag_inertia_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe/capture_touch_drag_inertia.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_drag_inertia_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=11 strip_blits=0 full_repaints=0 dirty_repaints=11 inertia=8")

        add_test(
            NAME jellyframe_win32_scroll_container_drag_edge_stop_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_container_probe/capture_touch_drag_edge_stop.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_drag_edge_stop_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=6 strip_blits=0 full_repaints=0 dirty_repaints=6 inertia=4")

        add_test(
            NAME jellyframe_win32_scroll_container_strip_blit_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_blit_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_blit_probe/capture_scroll_blit.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_container_strip_blit_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=3 strip_blits=3 full_repaints=0 dirty_repaints=3")

        add_test(
            NAME jellyframe_win32_component_recipes_scroll_frame_script
            COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_component_recipes
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_component_recipes/capture_scroll_recipes.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_component_recipes_scroll_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_containers scrolls=4 strip_blits=0 full_repaints=0 dirty_repaints=4")

        add_test(
            NAME jellyframe_win32_package_image_fixture
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/package_image_fixture_tests.py
                    $<TARGET_FILE:jellyframe_desktop_shell>
                    $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_win32_font_policy_report
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/font_policy_report_tests.py
                    $<TARGET_FILE:jellyframe_desktop_shell>
                    $<TARGET_FILE_DIR:jellyframe_pseudo_browser>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_win32_incremental_repaint_equivalence
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/incremental_repaint_equivalence_tests.py
                    $<TARGET_FILE:jellyframe_desktop_shell>
                    ${CMAKE_CURRENT_SOURCE_DIR}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        if(JELLYFRAME_BUILD_SCRIPTING)
            add_test(
                NAME jellyframe_win32_canvas_smoke_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_smoke
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_smoke/capture_canvas_smoke.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_canvas_smoke_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "layer_tree layers=2 display_commands=15")

            add_test(
                NAME jellyframe_win32_canvas_gauges_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_gauges
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_canvas_gauges/capture_canvas_gauges.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_win32_route_tabs_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_route_tabs
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_route_tabs/capture_route_tabs.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_route_tabs_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "dirty_local=3 full=1 clean=0")

            add_test(
                NAME jellyframe_win32_motion_lab_low_power_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_low_power_static.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_low_power_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_policy_samples=30 input=30 timers=30 animation=14 present=30")

            add_test(
                NAME jellyframe_win32_opacity_layer_reuse_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_opacity_layer_reuse
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_opacity_layer_reuse/capture_opacity_layer_reuse.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_opacity_layer_reuse_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION "animation_layer_reuse opacity=[1-9]")

            add_test(
                NAME jellyframe_win32_motion_lab_soak_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_soak_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_policy_samples=90 input=90 timers=90 animation=90 present=90")

            add_test(
                NAME jellyframe_win32_watch_face_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_watch_face
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_watch_face/capture_watch_face_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_win32_wearable_launcher_capture
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_wearable_launcher
                        --capture ${CMAKE_BINARY_DIR}/test_outputs/wearable_launcher.bmp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_wearable_launcher_capture PROPERTIES
                PASS_REGULAR_EXPRESSION "JellyFrame Desktop Shell capture"
                FAIL_REGULAR_EXPRESSION "\\[warning\\]|\\[error\\]")

            add_test(
                NAME jellyframe_win32_band_system_shell_capture
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/system/band_system_shell
                        --capture ${CMAKE_BINARY_DIR}/test_outputs/band_system_shell.bmp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_band_system_shell_capture PROPERTIES
                PASS_REGULAR_EXPRESSION "JellyFrame Desktop Shell capture"
                FAIL_REGULAR_EXPRESSION "\\[warning\\]|\\[error\\]")

            add_test(
                NAME jellyframe_win32_weather_interaction_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/watch_weather
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/watch_weather/capture_weather_interaction.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_weather_interaction_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "host_completion_kinds network=1 storage=0 image=3 audio=0 location=0 other=0"
                FAIL_REGULAR_EXPRESSION
                    "dirty_area_too_large=[1-9]")

            add_test(
                NAME jellyframe_win32_motion_lab_dirty_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_dirty_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "dirty_local=[1-9][0-9]* full=[1-9][0-9]* clean=0")

            add_test(
                NAME jellyframe_win32_motion_lab_dirty_flags_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_dirty_flags_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_dirty_flags none=1 tree=0 attributes=9 text=90 style=9 layout=90 paint=90 overlay=0 render_or_layout=90")

            add_test(
                NAME jellyframe_win32_motion_lab_text_reuse_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_text_reuse_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_update idle=0 repaint=90 rebuild=1 first_paint=1 paint_dirty=0 text_stable=81 style_stable=9 layout_previous=0")

            add_test(
                NAME jellyframe_win32_script_watchdog_recovery
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --registry-store ${CMAKE_BINARY_DIR}/test_outputs/watchdog_registry
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_watchdog_smoke
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_watchdog_smoke/capture_watchdog.jfcapture
                        --capture-montage ${CMAKE_BINARY_DIR}/test_outputs/watchdog_recovery.bmp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_script_watchdog_recovery PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "script_watchdog_recovery reason=script-watchdog")

            add_test(
                NAME jellyframe_win32_budget_recovery
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --registry-store ${CMAKE_BINARY_DIR}/test_outputs/budget_registry
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_budget_spam
                        --app-runtime-jobs 4
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_budget_spam/capture_budget_recovery.jfcapture
                        --capture-montage ${CMAKE_BINARY_DIR}/test_outputs/budget_recovery.bmp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_budget_recovery PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "budget_recovery_teardown reason=budget-exceeded")

            add_test(
                NAME jellyframe_win32_service_spam_frame_script
                COMMAND $<TARGET_FILE:jellyframe_desktop_shell>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_service_spam
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_service_spam/capture_service_spam.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_service_spam_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "host_completion_kinds network=16 storage=0 image=0 audio=0 location=2 other=0"
                FAIL_REGULAR_EXPRESSION
                    "dirty_area_too_large=[1-9]|overloaded=[1-9]|app_budget instance=1 role=app state=foreground requests=[1-9]|app_budget instance=1 role=app state=foreground requests=0/64 completions=[1-9]|app_budget instance=1 role=app state=foreground requests=0/64 completions=0/64 handles=[1-9]")
        endif()
    endif()

    if(JELLYFRAME_BUILD_SCRIPTING)
        set(JELLYFRAME_SCRIPT_TEST_SOURCES
            src/script/tests/script_runtime_tests.cpp
        )
        if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
            list(APPEND JELLYFRAME_SCRIPT_TEST_SOURCES
                src/script/tests/script_task_worker_runtime_tests.cpp
            )
        endif()
        foreach(test_source IN LISTS JELLYFRAME_SCRIPT_TEST_SOURCES)
            get_filename_component(test_name ${test_source} NAME_WE)
            set_source_files_properties(${test_source}
                PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
        endforeach()

        add_executable(jellyframe_script_tests
            src/script/tests/script_tests.cpp
            ${JELLYFRAME_SCRIPT_TEST_SOURCES}
        )
        target_link_libraries(jellyframe_script_tests PRIVATE jellyframe_script)
        if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
            target_compile_definitions(jellyframe_script_tests
                PRIVATE JELLYFRAME_SCRIPT_TASK_RUNTIME_ENABLED=1)
        endif()
        jellyframe_enable_test_asserts(jellyframe_script_tests)
        jellyframe_stage_sanitizer_runtime(jellyframe_script_tests)
        add_test(NAME jellyframe_script_tests COMMAND jellyframe_script_tests)

        if(JELLYFRAME_BUILD_SCRIPT_TASK_RUNTIME)
            set(JELLYFRAME_SCRIPT_TASK_RUNTIME_TEST_SOURCES
                src/app_runtime/tests/script_task_contract_tests.cpp
                src/app_runtime/tests/script_task_frame_codec_tests.cpp
                src/app_runtime/tests/script_task_frame_renderer_tests.cpp
                src/app_runtime/tests/script_task_input_codec_tests.cpp
                src/app_runtime/tests/script_task_input_dispatch_tests.cpp
                src/app_runtime/tests/script_task_service_request_codec_tests.cpp
                src/app_runtime/tests/script_task_service_bridge_tests.cpp
                src/app_runtime/tests/script_task_worker_inbox_tests.cpp
                src/app_runtime/tests/script_task_value_flow_tests.cpp
                src/app_runtime/tests/script_task_fatal_codec_tests.cpp
            )
            foreach(test_source IN LISTS JELLYFRAME_SCRIPT_TASK_RUNTIME_TEST_SOURCES)
                get_filename_component(test_name ${test_source} NAME_WE)
                set_source_files_properties(${test_source}
                    PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
            endforeach()
            add_executable(jellyframe_script_task_runtime_tests
                src/app_runtime/tests/script_task_runtime_tests.cpp
                ${JELLYFRAME_SCRIPT_TASK_RUNTIME_TEST_SOURCES}
            )
            target_link_libraries(jellyframe_script_task_runtime_tests PRIVATE jellyframe_script_task_runtime)
            jellyframe_enable_test_asserts(jellyframe_script_task_runtime_tests)
            jellyframe_stage_sanitizer_runtime(jellyframe_script_task_runtime_tests)
            add_test(NAME jellyframe_script_task_runtime_tests COMMAND jellyframe_script_task_runtime_tests)
        endif()
    endif()
endif()
