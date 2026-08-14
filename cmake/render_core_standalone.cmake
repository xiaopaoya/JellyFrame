# Standalone Render Core source-distribution entry point.
# This file is installed into a source archive as cmake support, while the
# archive root CMakeLists.txt remains deliberately small.

option(JELLYFRAME_BUILD_TESTS "Build Render Core regression tests" ON)
option(JELLYFRAME_INSTALL_RENDER_CORE "Install the Render Core CMake package" ON)
option(JELLYFRAME_ENABLE_IMAGE_FILE_IO "Expose desktop image file writers from Render Core" ON)
option(JELLYFRAME_ENABLE_CANVAS2D "Compile the graphics.canvas2d Render Core family" ON)
option(JELLYFRAME_ENABLE_MODERN_PAINT "Compile the css.modern-paint Render Core family" ON)
option(JELLYFRAME_ENABLE_FLEX_GRID "Compile the css.flex-grid Render Core family" ON)
option(JELLYFRAME_ENABLE_ADVANCED_FORMS "Compile the forms.advanced Render Core family" ON)

include(GNUInstallDirs)

set(JELLYFRAME_RENDER_CORE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_source_hash.cmake")
jellyframe_compute_render_core_source_hash("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_build.cmake")

if(JELLYFRAME_BUILD_TESTS)
    enable_testing()
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_tests.cmake")
endif()
