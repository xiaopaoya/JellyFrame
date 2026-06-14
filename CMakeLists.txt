cmake_minimum_required(VERSION 3.16)

project(WearWebEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WEARWEB_BUILD_EXAMPLES "Build WearWeb example and dump tools" ON)
option(WEARWEB_BUILD_TESTS "Build WearWeb regression tests" ON)
option(WEARWEB_BUILD_BENCHMARKS "Build WearWeb microbenchmarks" ON)

add_library(wearweb_core
    src/core/css_parser.cpp
    src/core/dom.cpp
    src/core/html_parser.cpp
    src/core/html_tokenizer.cpp
    src/core/html_tree_builder.cpp
    src/core/layer_tree.cpp
    src/core/layout.cpp
    src/core/render_tree.cpp
    src/core/software_renderer.cpp
    src/core/style.cpp
)

target_include_directories(wearweb_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(wearweb_core PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(wearweb_core PUBLIC gdi32 user32)
endif()

if(MSVC)
    target_compile_options(wearweb_core PRIVATE /W4)
else()
    target_compile_options(wearweb_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(add_wearweb_tool target source)
    add_executable(${target} ${source})
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
endif()

if(WEARWEB_BUILD_BENCHMARKS)
    add_wearweb_tool(wearweb_microbench benchmarks/microbench.cpp)
endif()

if(WEARWEB_BUILD_TESTS)
    enable_testing()
    add_wearweb_tool(wearweb_tokenizer_tests tests/tokenizer_tests.cpp)
    add_wearweb_tool(wearweb_css_parser_tests tests/css_parser_tests.cpp)
    add_wearweb_tool(wearweb_render_tree_tests tests/render_tree_tests.cpp)
    add_wearweb_tool(wearweb_layer_tree_tests tests/layer_tree_tests.cpp)
    add_wearweb_tool(wearweb_software_renderer_tests tests/software_renderer_tests.cpp)

    add_test(NAME wearweb_tokenizer_tests COMMAND wearweb_tokenizer_tests)
    add_test(NAME wearweb_css_parser_tests COMMAND wearweb_css_parser_tests)
    add_test(NAME wearweb_render_tree_tests COMMAND wearweb_render_tree_tests)
    add_test(NAME wearweb_layer_tree_tests COMMAND wearweb_layer_tree_tests)
    add_test(NAME wearweb_software_renderer_tests COMMAND wearweb_software_renderer_tests)
endif()
