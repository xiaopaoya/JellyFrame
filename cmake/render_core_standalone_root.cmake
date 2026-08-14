# This file is copied to the root of a JellyFrame Render Core source archive as
# CMakeLists.txt. Keep it small so standalone and in-repository builds share
# their target, package and test definitions.
cmake_minimum_required(VERSION 3.16)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/render_core_version.cmake")
project(JellyFrameRenderCore VERSION "${JELLYFRAME_RENDER_CORE_PACKAGE_VERSION}" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/render_core_standalone.cmake")
