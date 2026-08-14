# Shared Render Core regression-test registration.

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
    src/render_core/tests/form_submission_tests.cpp
    src/render_core/tests/frame_loop_tests.cpp
    src/render_core/tests/frame_update_tests.cpp
    src/render_core/tests/hit_test_tests.cpp
    src/render_core/tests/host_tests.cpp
    src/render_core/tests/input_tests.cpp
    src/render_core/tests/layout_tests.cpp
    src/render_core/tests/pipeline_statistics_tests.cpp
    src/render_core/tests/render_tree_tests.cpp
    src/render_core/tests/layer_tree_tests.cpp
    src/render_core/tests/scroll_blit_tests.cpp
    src/render_core/tests/scroll_gesture_tests.cpp
    src/render_core/tests/software_renderer_tests.cpp
    src/render_core/tests/style_repaint_tests.cpp
    src/render_core/tests/text_adapter_tests.cpp
    src/render_core/tests/text_repaint_tests.cpp
    src/render_core/tests/text_scan_tests.cpp
)
if(JELLYFRAME_ENABLE_CANVAS2D)
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        src/render_core/tests/canvas2d_tests.cpp)
else()
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        src/render_core/tests/canvas2d_disabled_tests.cpp)
endif()
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES
        src/render_core/tests/modern_paint_tests.cpp)
endif()

foreach(test_source IN LISTS JELLYFRAME_RENDER_CORE_TEST_SOURCES)
    set(test_source_absolute "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/${test_source}")
    get_filename_component(test_name "${test_source_absolute}" NAME_WE)
    set_source_files_properties("${test_source_absolute}"
        PROPERTIES COMPILE_DEFINITIONS "main=${test_name}_main")
    list(APPEND JELLYFRAME_RENDER_CORE_TEST_SOURCES_ABSOLUTE
        "${test_source_absolute}")
endforeach()

add_executable(jellyframe_render_core_tests
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src/render_core/tests/render_core_tests.cpp"
    ${JELLYFRAME_RENDER_CORE_TEST_SOURCES_ABSOLUTE})
target_link_libraries(jellyframe_render_core_tests PRIVATE jellyframe_render_core)
target_compile_definitions(jellyframe_render_core_tests PRIVATE JELLYFRAME_TEST_ASSERTS_ENABLED=1)
target_compile_definitions(jellyframe_render_core_tests
    PRIVATE $<$<CONFIG:Debug>:JELLYFRAME_TEST_CONFIG_DEBUG=1>)
if(MSVC)
    target_compile_options(jellyframe_render_core_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(jellyframe_render_core_tests PRIVATE -UNDEBUG)
endif()
if(COMMAND jellyframe_enable_render_core_link_map)
    jellyframe_enable_render_core_link_map(jellyframe_render_core_tests)
endif()
if(COMMAND jellyframe_stage_sanitizer_runtime)
    jellyframe_stage_sanitizer_runtime(jellyframe_render_core_tests)
endif()
add_test(
    NAME jellyframe_render_core_tests
    COMMAND jellyframe_render_core_tests
    WORKING_DIRECTORY "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
