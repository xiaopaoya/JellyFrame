# Shared Render Core target and package-export implementation.
#
# Both the JellyFrame Runtime checkout and a standalone Render Core source
# archive include this file. Keep the boundary free of App Runtime, scripting,
# desktop-host and port assumptions.

if(NOT DEFINED JELLYFRAME_RENDER_CORE_SOURCE_ROOT)
    message(FATAL_ERROR
        "render_core_build.cmake requires JELLYFRAME_RENDER_CORE_SOURCE_ROOT")
endif()

include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_sources.cmake")
include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_feature_profile.cmake")

add_library(jellyframe_render_core
    ${JELLYFRAME_RENDER_CORE_SOURCES_ABSOLUTE}
)
add_library(JellyFrame::jellyframe_render_core ALIAS jellyframe_render_core)

target_include_directories(jellyframe_render_core
    PUBLIC
        $<BUILD_INTERFACE:${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(jellyframe_render_core PUBLIC cxx_std_17)
target_compile_definitions(jellyframe_render_core PUBLIC
    JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED=${JELLYFRAME_RENDER_CORE_CANVAS2D_ENABLED}
    JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED=${JELLYFRAME_RENDER_CORE_MODERN_PAINT_ENABLED}
    JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED=${JELLYFRAME_RENDER_CORE_FLEX_GRID_ENABLED}
    JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED=${JELLYFRAME_RENDER_CORE_ADVANCED_FORMS_ENABLED}
)
if(JELLYFRAME_ENABLE_IMAGE_FILE_IO)
    target_compile_definitions(jellyframe_render_core PUBLIC JELLYFRAME_ENABLE_IMAGE_FILE_IO=1)
endif()

if(MSVC)
    target_compile_options(jellyframe_render_core PRIVATE /W4)
else()
    target_compile_options(jellyframe_render_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

set(JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_FILE
    "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_source_manifest.json")
configure_file(
    "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_source_manifest.json.in"
    "${JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_FILE}"
    @ONLY)

if(JELLYFRAME_INSTALL_RENDER_CORE)
    include(CMakePackageConfigHelpers)
    set(JELLYFRAME_RENDER_CORE_INSTALL_CMAKE_DIR
        "${CMAKE_INSTALL_LIBDIR}/cmake/JellyFrameRenderCore")
    configure_package_config_file(
        "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/JellyFrameRenderCoreConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/JellyFrameRenderCoreConfig.cmake"
        INSTALL_DESTINATION "${JELLYFRAME_RENDER_CORE_INSTALL_CMAKE_DIR}")
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/JellyFrameRenderCoreConfigVersion.cmake"
        VERSION "${JELLYFRAME_RENDER_CORE_PACKAGE_VERSION}"
        COMPATIBILITY SameMajorVersion)
    install(TARGETS jellyframe_render_core
        EXPORT JellyFrameRenderCoreTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/src/render_core/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/render_core"
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
        PATTERN "benchmarks" EXCLUDE
        PATTERN "docs" EXCLUDE
        PATTERN "samples" EXCLUDE
        PATTERN "tests" EXCLUDE)
    install(EXPORT JellyFrameRenderCoreTargets
        FILE JellyFrameRenderCoreTargets.cmake
        NAMESPACE JellyFrame::
        DESTINATION "${JELLYFRAME_RENDER_CORE_INSTALL_CMAKE_DIR}")
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/JellyFrameRenderCoreConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/JellyFrameRenderCoreConfigVersion.cmake"
        DESTINATION "${JELLYFRAME_RENDER_CORE_INSTALL_CMAKE_DIR}")
    install(FILES
        "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_profile.json"
        "${JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_FILE}"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/jellyframe-render-core")
endif()
