cmake_minimum_required(VERSION 3.16)

project(WearWebEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEARWEB_BUILD_EXAMPLES "Build WearWeb example and dump tools" ON)
option(WEARWEB_BUILD_TESTS "Build WearWeb regression tests" ON)
option(WEARWEB_BUILD_BENCHMARKS "Build WearWeb microbenchmarks" ON)
option(WEARWEB_BUILD_SCRIPTING "Build optional JerryScript scripting bridge" OFF)

set(JERRYSCRIPT_ROOT "" CACHE PATH "JerryScript source or install root")
set(JERRYSCRIPT_INCLUDE_DIR "" CACHE PATH "JerryScript include directory")
set(JERRYSCRIPT_LIBRARIES "" CACHE STRING "JerryScript libraries")

if(JERRYSCRIPT_ROOT AND NOT JERRYSCRIPT_INCLUDE_DIR)
    if(EXISTS "${JERRYSCRIPT_ROOT}/jerry-core/include/jerryscript.h")
        set(JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/jerry-core/include" CACHE PATH
            "JerryScript include directory" FORCE)
    elseif(EXISTS "${JERRYSCRIPT_ROOT}/include/jerryscript.h")
        set(JERRYSCRIPT_INCLUDE_DIR "${JERRYSCRIPT_ROOT}/include" CACHE PATH
            "JerryScript include directory" FORCE)
    endif()
endif()

add_library(wearweb_core
    src/core/bitmap_font.cpp
    src/core/css_parser.cpp
    src/core/dirty_region.cpp
    src/core/document_script.cpp
    src/core/document_style.cpp
    src/core/dom.cpp
    src/core/embedded_framebuffer.cpp
    src/core/event.cpp
    src/core/form_control.cpp
    src/core/hit_test.cpp
    src/core/html_parser.cpp
    src/core/html_tokenizer.cpp
    src/core/html_tree_builder.cpp
    src/core/input.cpp
    src/core/layer_tree.cpp
    src/core/layout.cpp
    src/core/render_tree.cpp
    src/core/software_renderer.cpp
    src/core/style.cpp
    src/core/text_backend.cpp
)

target_include_directories(wearweb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(wearweb_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(wearweb_core PRIVATE /W4)
else()
    target_compile_options(wearweb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WEARWEB_BUILD_SCRIPTING)
    if(NOT JERRYSCRIPT_INCLUDE_DIR OR NOT JERRYSCRIPT_LIBRARIES)
        message(FATAL_ERROR
            "WEARWEB_BUILD_SCRIPTING requires JERRYSCRIPT_INCLUDE_DIR and JERRYSCRIPT_LIBRARIES")
    endif()

    add_library(wearweb_script
        src/script/jerryscript_runtime.cpp
    )
    target_link_libraries(wearweb_script PUBLIC wearweb_core ${JERRYSCRIPT_LIBRARIES})
    target_include_directories(wearweb_script
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${JERRYSCRIPT_INCLUDE_DIR}
    )
    target_compile_features(wearweb_script PUBLIC cxx_std_17)
    target_compile_definitions(wearweb_script PUBLIC WEARWEB_ENABLE_SCRIPTING=1)

    if(MSVC)
        target_compile_options(wearweb_script PRIVATE /W4)
    else()
        target_compile_options(wearweb_script PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

function(add_wearweb_tool target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE wearweb_core)
endfunction()

if(WEARWEB_BUILD_EXAMPLES)
    add_wearweb_tool(wearweb_demo examples/basic_demo.cpp)
    add_wearweb_tool(wearweb_dom_dump examples/dom_dump.cpp)
    add_wearweb_tool(wearweb_cssom_dump examples/cssom_dump.cpp)
    add_wearweb_tool(wearweb_style_dump examples/style_dump.cpp)
    add_wearweb_tool(wearweb_render_tree_dump examples/render_tree_dump.cpp)
    add_wearweb_tool(wearweb_layer_tree_dump examples/layer_tree_dump.cpp)
    add_wearweb_tool(wearweb_pipeline_dump examples/pipeline_dump.cpp)
    add_wearweb_tool(wearweb_pseudo_browser examples/pseudo_browser.cpp)
    add_wearweb_tool(wearweb_capability_check examples/capability_check.cpp)
    add_wearweb_tool(wearweb_font_pack_gen examples/font_pack_gen.cpp)
    add_wearweb_tool(wearweb_embedded_host_demo examples/embedded_host_demo.cpp)
    if(WEARWEB_BUILD_SCRIPTING)
        target_link_libraries(wearweb_pseudo_browser PRIVATE wearweb_script)
    endif()
    if(WIN32)
        add_wearweb_tool(wearweb_win32_browser examples/win32_browser.cpp)
        target_link_libraries(wearweb_win32_browser PRIVATE gdi32 user32)
        if(WEARWEB_BUILD_SCRIPTING)
            target_link_libraries(wearweb_win32_browser PRIVATE wearweb_script)
        endif()
    endif()
endif()

if(WEARWEB_BUILD_BENCHMARKS)
    add_wearweb_tool(wearweb_microbench benchmarks/microbench.cpp)
endif()

if(WEARWEB_BUILD_TESTS)
    enable_testing()
    set(WEARWEB_CORE_TEST_SOURCES
        tests/tokenizer_tests.cpp
        tests/dom_mutation_tests.cpp
        tests/document_script_tests.cpp
        tests/dirty_region_tests.cpp
        tests/embedded_framebuffer_tests.cpp
        tests/css_parser_tests.cpp
        tests/event_tests.cpp
        tests/hit_test_tests.cpp
        tests/host_tests.cpp
        tests/input_tests.cpp
        tests/render_tree_tests.cpp
        tests/layer_tree_tests.cpp
        tests/software_renderer_tests.cpp
    )
    if(WEARWEB_BUILD_SCRIPTING)
        list(APPEND WEARWEB_CORE_TEST_SOURCES tests/script_runtime_tests.cpp)
    endif()

    foreach(test_source IN LISTS WEARWEB_CORE_TEST_SOURCES)
        get_filename_component(test_name ${test_source} NAME_WE)
        set_source_files_properties(${test_source}
            PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    endforeach()

    add_wearweb_tool(wearweb_core_tests
        tests/core_tests.cpp
        ${WEARWEB_CORE_TEST_SOURCES}
    )
    if(WEARWEB_BUILD_SCRIPTING)
        target_link_libraries(wearweb_core_tests PRIVATE wearweb_script)
    endif()

    add_test(NAME wearweb_core_tests COMMAND wearweb_core_tests)
endif()
