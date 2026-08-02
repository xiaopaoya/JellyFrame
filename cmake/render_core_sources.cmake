# Shared Render Core source manifest.
#
# Consumers set JELLYFRAME_SOURCE_ROOT to the repository root before including
# this file. Keep feature-family boundaries here; do not duplicate this list in
# a port or tool-specific build script.

if(NOT DEFINED JELLYFRAME_SOURCE_ROOT)
    get_filename_component(JELLYFRAME_SOURCE_ROOT
        "${CMAKE_CURRENT_LIST_DIR}/.."
        ABSOLUTE)
endif()

set(JELLYFRAME_RENDER_CORE_BASE_SOURCES
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
    src/render_core/form_submission.cpp
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
    src/render_core/scroll_blit.cpp
    src/render_core/software_renderer.cpp
    src/render_core/style.cpp
    src/render_core/style_repaint.cpp
    src/render_core/text_adapter.cpp
    src/render_core/text_backend.cpp
    src/render_core/text_layout_reuse.cpp
    src/render_core/text_normalization.cpp
    src/render_core/text_repaint.cpp
    src/render_core/text_scan.cpp
)

set(JELLYFRAME_RENDER_CORE_GRAPHICS_CANVAS2D_SOURCES
    src/render_core/canvas2d.cpp
)

set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES
    src/render_core/modern_paint.cpp
)

if(NOT DEFINED JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_ENABLE_CANVAS2D ON)
endif()
if(NOT DEFINED JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_ENABLE_MODERN_PAINT ON)
endif()
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED 1)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED 0)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION)
endif()

# Keep feature-family source selection explicit. A family must have a safe
# compile-time fallback before it is removed from a profile.
if(JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED 1)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION
        ${JELLYFRAME_RENDER_CORE_GRAPHICS_CANVAS2D_SOURCES})
else()
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED 0)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION
        src/render_core/canvas2d_disabled.cpp)
endif()

set(JELLYFRAME_RENDER_CORE_SOURCES
    ${JELLYFRAME_RENDER_CORE_BASE_SOURCES}
    ${JELLYFRAME_RENDER_CORE_MODERN_PAINT_IMPLEMENTATION}
    ${JELLYFRAME_RENDER_CORE_CANVAS2D_IMPLEMENTATION}
)

set(JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE)
set(_jellyframe_seen_render_core_sources)
foreach(_jellyframe_source IN LISTS JELLYFRAME_RENDER_CORE_SOURCES)
    list(FIND _jellyframe_seen_render_core_sources
        "${_jellyframe_source}" _jellyframe_source_index)
    if(NOT _jellyframe_source_index EQUAL -1)
        message(FATAL_ERROR
            "Render Core source is listed more than once: ${_jellyframe_source}")
    endif()
    if(NOT EXISTS "${JELLYFRAME_SOURCE_ROOT}/${_jellyframe_source}")
        message(FATAL_ERROR
            "Render Core source does not exist: ${JELLYFRAME_SOURCE_ROOT}/${_jellyframe_source}")
    endif()
    list(APPEND JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE
        "${JELLYFRAME_SOURCE_ROOT}/${_jellyframe_source}")
    list(APPEND _jellyframe_seen_render_core_sources "${_jellyframe_source}")
endforeach()

unset(_jellyframe_source)
unset(_jellyframe_source_index)
unset(_jellyframe_seen_render_core_sources)
