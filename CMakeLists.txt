cmake_minimum_required(VERSION 3.16)

project(JellyFrameEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JELLYFRAME_BUILD_EXAMPLES "Build JellyFrame native validation and dump tools" ON)
option(JELLYFRAME_BUILD_TESTS "Build JellyFrame regression tests" ON)
option(JELLYFRAME_BUILD_BENCHMARKS "Build JellyFrame microbenchmarks" ON)
option(JELLYFRAME_BUILD_SCRIPTING "Build optional JerryScript scripting bridge" OFF)

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
endif()

add_library(jellyframe_render_core
    src/render_core/animation_invalidation.cpp
    src/render_core/animation_timeline.cpp
    src/render_core/arena.cpp
    src/render_core/bitmap_font.cpp
    src/render_core/bitmap_font_resource.cpp
    src/render_core/css_parser.cpp
    src/render_core/display_invalidation.cpp
    src/render_core/dirty_region.cpp
    src/render_core/document_script.cpp
    src/render_core/document_style.cpp
    src/render_core/dom.cpp
    src/render_core/dom_owner.cpp
    src/render_core/embedded_framebuffer.cpp
    src/render_core/event.cpp
    src/render_core/form_control.cpp
    src/render_core/frame_loop.cpp
    src/render_core/frame_update.cpp
    src/render_core/hit_test.cpp
    src/render_core/html_parser.cpp
    src/render_core/html_tokenizer.cpp
    src/render_core/html_tree_builder.cpp
    src/render_core/input.cpp
    src/render_core/layer_tree.cpp
    src/render_core/layout.cpp
    src/render_core/pipeline_statistics.cpp
    src/render_core/render_tree.cpp
    src/render_core/software_renderer.cpp
    src/render_core/style.cpp
    src/render_core/style_repaint.cpp
    src/render_core/text_adapter.cpp
    src/render_core/text_backend.cpp
    src/render_core/text_layout_reuse.cpp
    src/render_core/text_normalization.cpp
    src/render_core/text_repaint.cpp
)

target_include_directories(jellyframe_render_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(jellyframe_render_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(jellyframe_render_core PRIVATE /W4)
else()
    target_compile_options(jellyframe_render_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(jellyframe_app_runtime
    src/app_runtime/app_budget.cpp
    src/app_runtime/app_capability_broker.cpp
    src/app_runtime/app_device_services.cpp
    src/app_runtime/app_font_set.cpp
    src/app_runtime/app_frame_policy.cpp
    src/app_runtime/app_host.cpp
    src/app_runtime/app_lifecycle.cpp
    src/app_runtime/app_load_telemetry.cpp
    src/app_runtime/app_service_worker.cpp
    src/app_runtime/app_services.cpp
    src/app_runtime/app_storage_lifecycle_policy.cpp
    src/app_runtime/host_services.cpp
    src/app_runtime/system_events.cpp
    src/app_runtime/xml_http_request.cpp
)

target_link_libraries(jellyframe_app_runtime PUBLIC jellyframe_render_core)
target_include_directories(jellyframe_app_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(jellyframe_app_runtime PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(jellyframe_app_runtime PRIVATE /W4)
else()
    target_compile_options(jellyframe_app_runtime PRIVATE -Wall -Wextra -Wpedantic)
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

    add_library(jellyframe_script
        src/script/jerryscript_runtime.cpp
    )
    target_link_libraries(jellyframe_script PUBLIC jellyframe_app_runtime ${JERRYSCRIPT_LIBRARIES})
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
        add_jellyframe_tool(jellyframe_win32_browser tools/native/win32_browser.cpp)
        target_link_libraries(jellyframe_win32_browser PRIVATE jellyframe_app_runtime gdi32 user32 winmm)
        if(JELLYFRAME_BUILD_SCRIPTING)
            target_link_libraries(jellyframe_win32_browser PRIVATE jellyframe_script)
        endif()
    endif()
endif()

if(JELLYFRAME_BUILD_BENCHMARKS)
    add_jellyframe_tool(jellyframe_render_core_microbench src/render_core/benchmarks/microbench.cpp)
    add_executable(jellyframe_app_runtime_microbench src/app_runtime/benchmarks/app_runtime_bench.cpp)
    target_link_libraries(jellyframe_app_runtime_microbench PRIVATE jellyframe_app_runtime)
    add_jellyframe_tool(jellyframe_virtual_bench ports/virtual_board/virtual_bench.cpp)
endif()

if(JELLYFRAME_BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    set(JELLYFRAME_RENDER_CORE_TEST_SOURCES
        src/render_core/tests/tokenizer_tests.cpp
        src/render_core/tests/arena_tests.cpp
        src/render_core/tests/animation_timeline_tests.cpp
        src/render_core/tests/budget_stress_tests.cpp
        src/render_core/tests/dom_mutation_tests.cpp
        src/render_core/tests/dom_owner_tests.cpp
        src/render_core/tests/document_script_tests.cpp
        src/render_core/tests/display_invalidation_tests.cpp
        src/render_core/tests/dirty_region_tests.cpp
        src/render_core/tests/embedded_framebuffer_tests.cpp
        src/render_core/tests/css_parser_tests.cpp
        src/render_core/tests/event_tests.cpp
        src/render_core/tests/frame_loop_tests.cpp
        src/render_core/tests/frame_update_tests.cpp
        src/render_core/tests/hit_test_tests.cpp
        src/render_core/tests/host_tests.cpp
        src/render_core/tests/input_tests.cpp
        src/render_core/tests/layout_tests.cpp
        src/render_core/tests/pipeline_statistics_tests.cpp
        src/render_core/tests/render_tree_tests.cpp
        src/render_core/tests/layer_tree_tests.cpp
        src/render_core/tests/software_renderer_tests.cpp
        src/render_core/tests/style_repaint_tests.cpp
        src/render_core/tests/text_adapter_tests.cpp
        src/render_core/tests/text_repaint_tests.cpp
    )

    foreach(test_source IN LISTS JELLYFRAME_RENDER_CORE_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        set_source_files_properties(${test_source}
            PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    endforeach()

    add_jellyframe_tool(jellyframe_render_core_tests
        src/render_core/tests/render_core_tests.cpp
        ${JELLYFRAME_RENDER_CORE_TEST_SOURCES}
    )
    add_test(NAME jellyframe_render_core_tests COMMAND jellyframe_render_core_tests)

    set(JELLYFRAME_APP_RUNTIME_TEST_SOURCES
        src/app_runtime/tests/app_capability_broker_tests.cpp
        src/app_runtime/tests/app_device_services_tests.cpp
        src/app_runtime/tests/app_frame_policy_tests.cpp
        src/app_runtime/tests/app_host_tests.cpp
        src/app_runtime/tests/app_lifecycle_tests.cpp
        src/app_runtime/tests/app_load_telemetry_tests.cpp
        src/app_runtime/tests/app_service_worker_tests.cpp
        src/app_runtime/tests/app_services_tests.cpp
        src/app_runtime/tests/app_storage_lifecycle_policy_tests.cpp
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
    add_test(NAME jellyframe_app_runtime_tests COMMAND jellyframe_app_runtime_tests)

    if(Python3_Interpreter_FOUND)
        add_test(
            NAME jellyframe_tool_package_preflight_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/package_preflight_tests.py
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        if(JELLYFRAME_BUILD_EXAMPLES)
            add_test(
                NAME jellyframe_pipeline_visual_diagnostics_tests
                COMMAND ${Python3_EXECUTABLE}
                        ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/pipeline_visual_diagnostics_tests.py
                        $<TARGET_FILE:jellyframe_pseudo_browser>
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

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
        endif()
    endif()

    if(WIN32 AND JELLYFRAME_BUILD_EXAMPLES)
        add_test(
            NAME jellyframe_win32_browser_cli_contract
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/tool_regression/win32_browser_cli_tests.py
                    $<TARGET_FILE:jellyframe_win32_browser>
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

        add_test(
            NAME jellyframe_win32_service_status_frame_script
            COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_service_status/capture_system_events.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_service_status_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "service_activity network=18 audio=36 sensors=16 location=16 pause_audio=0 throttle_sensors=20 throttle_location=20")

        add_test(
            NAME jellyframe_win32_scroll_probe_frame_script
            COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                    --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_probe
                    --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_scroll_probe/capture_wheel_scroll.jfcapture
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(jellyframe_win32_scroll_probe_frame_script PROPERTIES
            PASS_REGULAR_EXPRESSION
                "scroll_blits full=1 fast=4")

        if(JELLYFRAME_BUILD_SCRIPTING)
            add_test(
                NAME jellyframe_win32_motion_lab_low_power_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_low_power_static.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_low_power_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_policy_samples=30 input=30 timers=30 animation=14 present=30")

            add_test(
                NAME jellyframe_win32_motion_lab_soak_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_soak_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_policy_samples=90 input=90 timers=90 animation=90 present=90")

            add_test(
                NAME jellyframe_win32_watch_face_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_watch_face
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_watch_face/capture_watch_face_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

            add_test(
                NAME jellyframe_win32_motion_lab_dirty_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_dirty_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "dirty_local=90 full=1 clean=0")

            add_test(
                NAME jellyframe_win32_motion_lab_dirty_flags_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_dirty_flags_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_dirty_flags none=1 tree=0 attributes=9 text=90 style=9 layout=90 paint=90 render_or_layout=90")

            add_test(
                NAME jellyframe_win32_motion_lab_text_reuse_frame_script
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/samples/apps/packages/jelly_motion_lab/capture_soak_30fps.jfcapture
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_motion_lab_text_reuse_frame_script PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "frame_update idle=0 repaint=90 rebuild=1 first_paint=1 paint_dirty=0 text_stable=81 style_stable=9 layout_previous=0")

            add_test(
                NAME jellyframe_win32_script_watchdog_recovery
                COMMAND $<TARGET_FILE:jellyframe_win32_browser>
                        --registry-store ${CMAKE_BINARY_DIR}/test_outputs/watchdog_registry
                        --app ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_watchdog_smoke
                        --frame-script ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apps/jelly_watchdog_smoke/capture_watchdog.jfcapture
                        --capture-montage ${CMAKE_BINARY_DIR}/test_outputs/watchdog_recovery.bmp
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
            set_tests_properties(jellyframe_win32_script_watchdog_recovery PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "script_watchdog_recovery reason=script-watchdog")
        endif()
    endif()

    if(JELLYFRAME_BUILD_SCRIPTING)
        set(JELLYFRAME_SCRIPT_TEST_SOURCES
            src/script/tests/script_runtime_tests.cpp
        )
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
        add_test(NAME jellyframe_script_tests COMMAND jellyframe_script_tests)
    endif()
endif()
