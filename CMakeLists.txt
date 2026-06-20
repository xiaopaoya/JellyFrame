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
    src/render_core/text_adapter.cpp
    src/render_core/text_backend.cpp
    src/render_core/text_normalization.cpp
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
    src/app_runtime/app_font_set.cpp
    src/app_runtime/app_host.cpp
    src/app_runtime/app_lifecycle.cpp
    src/app_runtime/app_services.cpp
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
        target_link_libraries(jellyframe_win32_browser PRIVATE jellyframe_app_runtime gdi32 user32)
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
        src/render_core/tests/text_adapter_tests.cpp
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
        src/app_runtime/tests/app_host_tests.cpp
        src/app_runtime/tests/app_lifecycle_tests.cpp
        src/app_runtime/tests/app_services_tests.cpp
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
