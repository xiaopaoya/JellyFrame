set(JELLYFRAME_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
if(JELLYFRAME_RENDER_CORE_SOURCE_DIR)
    get_filename_component(JELLYFRAME_RENDER_CORE_SOURCE_ROOT
        "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}" ABSOLUTE)
else()
    set(JELLYFRAME_RENDER_CORE_SOURCE_ROOT "${JELLYFRAME_ROOT}")
endif()
set(JELLYFRAME_SOURCE_ROOT "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
unset(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION CACHE)
unset(JELLYFRAME_RENDER_CORE_ENGINE_ABI CACHE)
unset(JELLYFRAME_RENDER_CORE_CMAKE_VERSION CACHE)
include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_version.cmake")
include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_sources.cmake")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_feature_profile.cmake")
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_source_hash.cmake")
    jellyframe_compute_render_core_source_hash("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
    set(JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/jellyframe_render_core_source_manifest.json")
    configure_file(
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_source_manifest.json.in"
        "${JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_FILE}"
        @ONLY)
    set(JELLYFRAME_RENDER_CORE_PORT_PROVENANCE_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/generated/jellyframe_render_core_port_provenance.txt")
    file(WRITE "${JELLYFRAME_RENDER_CORE_PORT_PROVENANCE_FILE}"
        "package=${JELLYFRAME_RENDER_CORE_PACKAGE_VERSION}\n"
        "abi=${JELLYFRAME_RENDER_CORE_ENGINE_ABI}\n"
        "profile=${JELLYFRAME_RENDER_CORE_PROFILE_ID}\n"
        "source=${JELLYFRAME_RENDER_CORE_SOURCE_HASH}\n"
        "files=${JELLYFRAME_RENDER_CORE_SOURCE_FILE_COUNT}\n")
endif()
set(JELLYFRAME_RENDER_CORE_INCLUDE_DIRS
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src")
if(EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/include")
    list(PREPEND JELLYFRAME_RENDER_CORE_INCLUDE_DIRS
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/include")
endif()

idf_component_register(
    SRCS
        ${JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE}
    INCLUDE_DIRS
        ${JELLYFRAME_RENDER_CORE_INCLUDE_DIRS}
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

target_compile_definitions(${COMPONENT_LIB} PUBLIC
    JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED=${JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED}
    JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED=${JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED}
    JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED=${JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED}
    JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED=${JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED}
)

# Match the source-selected optional API in every dependent translation unit.
# Older bundled Core trees do not expose this feature variable.
# Public headers and sources must see the same selected feature values. Some
# headers provide legacy defaults, so omitting a definition can change a macro
# halfway through a translation unit (for example LENGTH_MATH).
foreach(_core_feature TEXT_SHAPING CONTEXTUAL_TEXT CONTEXTUAL_LAYOUT SHAPED_PACKET_V2
        LENGTH_MATH CSS_COUNTERS CSS_BORDERS TABLES POSITIONED_SIZING
        POSITIONED_TRANSACTIONS AFFINE2D COMPUTED_FONT_PRECISION SHARED_INLINE PRESERVED_TEXT)
    if(DEFINED JELLYFRAME_RENDER_CORE_${_core_feature}_ENABLED)
        target_compile_definitions(${COMPONENT_LIB} PUBLIC
            JELLYFRAME_RENDER_CORE_${_core_feature}_ENABLED=${JELLYFRAME_RENDER_CORE_${_core_feature}_ENABLED})
    endif()
endforeach()

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Wno-missing-field-initializers
    # Core's compact statements remain warnings under GCC, as in the standalone
    # consumer build; retain diagnostics without IDF's blanket -Werror=all.
    -Wno-error=misleading-indentation
)
