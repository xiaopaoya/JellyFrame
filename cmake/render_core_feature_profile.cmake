# Generate the build-time Render Core capability profile.
# This is metadata for package/check and host integration; it is not an App
# resource and is not loaded by the renderer at runtime.

set(JELLYFRAME_RENDER_CORE_ENGINE_ABI 1 CACHE STRING
    "Render Core public engine ABI version")

set(_jellyframe_render_core_features
    "core.document"
    "core.paint"
    "css.flex-grid"
    "forms.advanced"
)
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    list(APPEND _jellyframe_render_core_features "css.modern-paint")
endif()
if(JELLYFRAME_ENABLE_CANVAS2D)
    list(APPEND _jellyframe_render_core_features "graphics.canvas2d")
endif()

if(JELLYFRAME_ENABLE_CANVAS2D AND JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-default")
elseif(JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-no-modern-paint")
elseif(JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-no-canvas")
else()
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-minimal")
endif()

string(REPLACE ";" "\",\n    \"" _jellyframe_render_core_features_json
    "${_jellyframe_render_core_features}")
set(JELLYFRAME_RENDER_CORE_FEATURES_JSON
    "\"${_jellyframe_render_core_features_json}\"")

string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_base_sources_json
    "${JELLYFRAME_RENDER_CORE_BASE_SOURCES}")
set(JELLYFRAME_RENDER_CORE_BASE_SOURCES_JSON
    "\"${_jellyframe_render_core_base_sources_json}\"")
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_modern_paint_sources_json
    "${JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES}")
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES_JSON
        "\"${_jellyframe_render_core_modern_paint_sources_json}\"")
else()
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES_JSON)
endif()
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_canvas2d_sources_json
    "${JELLYFRAME_RENDER_CORE_GRAPHICS_CANVAS2D_SOURCES}")
if(JELLYFRAME_ENABLE_CANVAS2D)
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_SOURCES_JSON
        "\"${_jellyframe_render_core_canvas2d_sources_json}\"")
else()
    set(JELLYFRAME_RENDER_CORE_CANVAS2D_SOURCES_JSON
        "\"src/render_core/canvas2d_disabled.cpp\"")
endif()

set(JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}")
configure_file(
    "${JELLYFRAME_SOURCE_ROOT}/cmake/render_core_feature_profile.json.in"
    "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_profile.json"
    @ONLY)

unset(_jellyframe_render_core_features)
unset(_jellyframe_render_core_features_json)
unset(_jellyframe_render_core_base_sources_json)
unset(_jellyframe_render_core_modern_paint_sources_json)
unset(_jellyframe_render_core_canvas2d_sources_json)
