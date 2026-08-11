# Generate the build-time Render Core capability profile.
# This is metadata for package/check and host integration; it is not an App
# resource and is not loaded by the renderer at runtime.

set(JELLYFRAME_RENDER_CORE_ENGINE_ABI 1 CACHE STRING
    "Render Core public engine ABI version")

include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_feature_registry.cmake")
jellyframe_render_core_enabled_features(_jellyframe_render_core_features)

set(_jellyframe_render_core_disabled_profile_entries)
foreach(_jellyframe_render_core_feature IN LISTS JELLYFRAME_RENDER_CORE_FEATURE_IDS)
    jellyframe_render_core_feature_metadata("${_jellyframe_render_core_feature}" OPTION _jellyframe_render_core_option)
    jellyframe_render_core_feature_metadata("${_jellyframe_render_core_feature}" SUFFIX _jellyframe_render_core_suffix)
    jellyframe_render_core_feature_metadata("${_jellyframe_render_core_feature}" ORDER _jellyframe_render_core_order)
    if(NOT _jellyframe_render_core_option STREQUAL "" AND NOT ${_jellyframe_render_core_option})
        list(APPEND _jellyframe_render_core_disabled_profile_entries
            "${_jellyframe_render_core_order}|${_jellyframe_render_core_suffix}")
    endif()
endforeach()
list(SORT _jellyframe_render_core_disabled_profile_entries)
set(_jellyframe_render_core_disabled_profile_families)
foreach(_jellyframe_render_core_disabled_profile_entry IN LISTS _jellyframe_render_core_disabled_profile_entries)
    string(REGEX REPLACE "^[0-9]+\\|" "" _jellyframe_render_core_disabled_profile_family
        "${_jellyframe_render_core_disabled_profile_entry}")
    list(APPEND _jellyframe_render_core_disabled_profile_families
        "${_jellyframe_render_core_disabled_profile_family}")
endforeach()
if(NOT _jellyframe_render_core_disabled_profile_families)
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-default")
elseif(NOT JELLYFRAME_ENABLE_FLEX_GRID AND NOT JELLYFRAME_ENABLE_MODERN_PAINT AND
       NOT JELLYFRAME_ENABLE_CANVAS2D)
    if(JELLYFRAME_ENABLE_ADVANCED_FORMS)
        set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-minimal")
    else()
        set(JELLYFRAME_RENDER_CORE_PROFILE_ID "render-core-minimal-no-forms-advanced")
    endif()
else()
    string(JOIN "-" _jellyframe_render_core_profile_suffix
        ${_jellyframe_render_core_disabled_profile_families})
    set(JELLYFRAME_RENDER_CORE_PROFILE_ID
        "render-core-${_jellyframe_render_core_profile_suffix}")
endif()

string(REPLACE ";" "\",\n    \"" _jellyframe_render_core_features_json
    "${_jellyframe_render_core_features}")
set(JELLYFRAME_RENDER_CORE_FEATURES_JSON
    "\"${_jellyframe_render_core_features_json}\"")

string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_document_sources_json
    "${JELLYFRAME_RENDER_CORE_CORE_DOCUMENT_SOURCES}")
set(JELLYFRAME_RENDER_CORE_DOCUMENT_SOURCES_JSON
    "\"${_jellyframe_render_core_document_sources_json}\"")
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_paint_sources_json
    "${JELLYFRAME_RENDER_CORE_CORE_PAINT_SOURCES}")
set(JELLYFRAME_RENDER_CORE_PAINT_SOURCES_JSON
    "\"${_jellyframe_render_core_paint_sources_json}\"")
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_modern_paint_sources_json
    "${JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES}")
if(JELLYFRAME_ENABLE_MODERN_PAINT)
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES_JSON
        "\"${_jellyframe_render_core_modern_paint_sources_json}\"")
else()
    set(JELLYFRAME_RENDER_CORE_MODERN_PAINT_SOURCES_JSON)
endif()
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_flex_grid_sources_json
    "${JELLYFRAME_RENDER_CORE_FLEX_GRID_IMPLEMENTATION}")
if(JELLYFRAME_ENABLE_FLEX_GRID)
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_SOURCES_JSON
        "\"${_jellyframe_render_core_flex_grid_sources_json}\"")
else()
    set(JELLYFRAME_RENDER_CORE_FLEX_GRID_SOURCES_JSON)
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
string(REPLACE ";" "\",\n      \"" _jellyframe_render_core_advanced_forms_sources_json
    "${JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_SOURCES}")
if(JELLYFRAME_ENABLE_ADVANCED_FORMS)
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_SOURCES_JSON
        "\"${_jellyframe_render_core_advanced_forms_sources_json}\"")
else()
    set(JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_SOURCES_JSON
        "\"src/render_core/form_submission_disabled.cpp\"")
endif()

set(JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}")
configure_file(
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_feature_profile.json.in"
    "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_profile.json"
    @ONLY)

unset(_jellyframe_render_core_features)
unset(_jellyframe_render_core_features_json)
unset(_jellyframe_render_core_disabled_profile_families)
unset(_jellyframe_render_core_profile_suffix)
unset(_jellyframe_render_core_document_sources_json)
unset(_jellyframe_render_core_paint_sources_json)
unset(_jellyframe_render_core_modern_paint_sources_json)
unset(_jellyframe_render_core_flex_grid_sources_json)
unset(_jellyframe_render_core_canvas2d_sources_json)
unset(_jellyframe_render_core_advanced_forms_sources_json)
