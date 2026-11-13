cmake_minimum_required(VERSION 3.16)
project(RuntimeCanonicalV10InstalledConsumer LANGUAGES CXX)

find_package(JellyFrameRenderCore CONFIG REQUIRED)
if(NOT DEFINED JellyFrameRenderCore_PACKAGE_VERSION OR
   JellyFrameRenderCore_PACKAGE_VERSION VERSION_LESS "0.9")
  message(FATAL_ERROR "Canonical v10 qualification requires Core package 0.9 or newer")
endif()
if(NOT JellyFrameRenderCore_ENGINE_ABI STREQUAL "4")
  message(FATAL_ERROR "Canonical v10 qualification requires an installed ABI4 Core")
endif()
foreach(identity IN ITEMS SOURCE_HASH PROFILE_ID)
  if(NOT DEFINED EXPECTED_CORE_${identity} OR "${EXPECTED_CORE_${identity}}" STREQUAL "")
    message(FATAL_ERROR "Set EXPECTED_CORE_${identity} to the qualified package identity")
  endif()
  if(NOT "${JellyFrameRenderCore_${identity}}" STREQUAL "${EXPECTED_CORE_${identity}}")
    message(FATAL_ERROR "Canonical v10 consumer refuses Core ${identity} mismatch")
  endif()
endforeach()

option(BUILD_V10_WORKER "Qualify the actual JerryScript worker and script API" OFF)
option(REQUIRE_NATURAL_IMAGE_API "Require the Core natural-image layout API in this qualification" OFF)
if(REQUIRE_NATURAL_IMAGE_API)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES JellyFrame::jellyframe_render_core)
  unset(QUALIFIED_CORE_HAS_NATURAL_IMAGE_API CACHE)
  check_cxx_source_compiles("
    #include <render_core/layout.h>
    #if !defined(JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION) || JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION < 1
    #error Natural image layout API is required
    #endif
    int main() { return 0; }
  " QUALIFIED_CORE_HAS_NATURAL_IMAGE_API)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(NOT QUALIFIED_CORE_HAS_NATURAL_IMAGE_API)
    message(FATAL_ERROR "This qualification requires the Core natural-image layout API")
  endif()
endif()
option(REQUIRE_SHARED_INLINE_API "Require canonical prepared-text v2 in this qualification" OFF)
if(REQUIRE_SHARED_INLINE_API)
  include(CheckCXXSourceCompiles)
  set(CMAKE_REQUIRED_LIBRARIES JellyFrame::jellyframe_render_core)
  unset(QUALIFIED_CORE_HAS_SHARED_INLINE_API CACHE)
  check_cxx_source_compiles("
    #include <render_core/prepared_text.h>
    #if !defined(JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION) || JELLYFRAME_RENDER_CORE_PREPARED_TEXT_PACKET_VERSION != 2
    #error Canonical prepared-text v2 is required
    #endif
    int main() { return 0; }
  " QUALIFIED_CORE_HAS_SHARED_INLINE_API)
  unset(CMAKE_REQUIRED_LIBRARIES)
  if(NOT QUALIFIED_CORE_HAS_SHARED_INLINE_API)
    message(FATAL_ERROR "This qualification requires canonical prepared-text v2")
  endif()
endif()
option(QUALIFY_NATURAL_INLINE_IMAGES "Run Core-mainline inline natural-image worker qualification" OFF)
if(QUALIFY_NATURAL_INLINE_IMAGES AND
   (NOT BUILD_V10_WORKER OR NOT REQUIRE_NATURAL_IMAGE_API OR NOT REQUIRE_SHARED_INLINE_API))
  message(FATAL_ERROR "Natural inline qualification requires the worker, natural-image API and shared-inline API checks")
endif()
get_filename_component(runtime_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(runtime_source "${CMAKE_CURRENT_BINARY_DIR}/runtime-source")
# No bundled render_core headers are copied into this consumer tree. Register
# each staged input so an incremental build cannot reuse an older source copy.
function(stage_runtime_directory name)
  set(origin "${runtime_repo_root}/src/${name}")
  file(GLOB_RECURSE staged_inputs CONFIGURE_DEPENDS RELATIVE "${origin}" "${origin}/*")
  foreach(input IN LISTS staged_inputs)
    if(NOT IS_DIRECTORY "${origin}/${input}")
      configure_file("${origin}/${input}" "${runtime_source}/src/${name}/${input}" COPYONLY)
    endif()
  endforeach()
endfunction()
stage_runtime_directory(app_runtime)
set(runtime_cpp "${runtime_source}/src/app_runtime")
set(support_sources
  "${runtime_cpp}/app_font_set.cpp"
  "${runtime_cpp}/script_task_contract.cpp"
  "${runtime_cpp}/script_task_frame_codec.cpp"
  "${runtime_cpp}/script_task_frame_renderer.cpp"
  "${runtime_cpp}/runtime_font_catalog.cpp"
  "${runtime_cpp}/runtime_image_provider.cpp")
if(BUILD_V10_WORKER)
  stage_runtime_directory(script)
  stage_runtime_directory(device_runtime_contracts)
  file(GLOB support_sources CONFIGURE_DEPENDS "${runtime_cpp}/*.cpp"
    "${runtime_source}/src/device_runtime_contracts/*.cpp")
endif()
add_library(runtime_v10_support STATIC ${support_sources})
target_include_directories(runtime_v10_support PUBLIC "${runtime_source}/src")
target_link_libraries(runtime_v10_support PUBLIC JellyFrame::jellyframe_render_core)
target_compile_features(runtime_v10_support PUBLIC cxx_std_17)

function(v10_test target entry)
  add_executable(${target} "${entry}" ${ARGN})
  target_link_libraries(${target} PRIVATE runtime_v10_support)
  target_compile_definitions(${target} PRIVATE JELLYFRAME_TEST_ASSERTS_ENABLED=1)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

enable_testing()
v10_test(installed_runtime_canonical_v10 "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
  "${runtime_cpp}/tests/script_task_frame_codec_tests.cpp"
  "${runtime_cpp}/tests/script_task_frame_renderer_tests.cpp")

if(BUILD_V10_WORKER)
  include("${runtime_repo_root}/cmake/jellyframe_script_backend_jerryscript.cmake")
  set(script_cpp "${runtime_source}/src/script")
  add_library(runtime_v10_script STATIC
    "${script_cpp}/jerryscript_runtime.cpp" "${script_cpp}/script_task_worker_runtime.cpp")
  include("${runtime_repo_root}/cmake/jellyframe_script_config.cmake")
  jellyframe_configure_script(runtime_v10_script PUBLIC JellyFrame::jellyframe_render_core)
  target_link_libraries(runtime_v10_script PUBLIC runtime_v10_support
    PRIVATE ${JELLYFRAME_SCRIPT_BACKEND_LIBRARIES})
  target_include_directories(runtime_v10_script PRIVATE ${JELLYFRAME_SCRIPT_BACKEND_INCLUDE_DIRS})
  target_compile_definitions(runtime_v10_script PUBLIC JELLYFRAME_ENABLE_SCRIPTING=1
    PRIVATE JELLYFRAME_SCRIPT_ENGINE_JERRYSCRIPT=1 JELLYFRAME_SCRIPT_EVENT_TESTING=1
      JELLYFRAME_SCRIPT_IDENTITY_TESTING=1)
  set_source_files_properties("${script_cpp}/tests/script_runtime_tests.cpp"
    PROPERTIES COMPILE_DEFINITIONS "main=script_runtime_tests_main")
  v10_test(installed_runtime_canonical_v10_worker
    "${runtime_repo_root}/src/script/tests/script_tests.cpp"
    "${script_cpp}/tests/script_runtime_tests.cpp"
    "${script_cpp}/tests/script_task_worker_runtime_tests.cpp")
  target_compile_definitions(installed_runtime_canonical_v10_worker
    PRIVATE JELLYFRAME_SCRIPT_TASK_RUNTIME_ENABLED=1 JELLYFRAME_SCRIPT_EVENT_TESTING=1
      JELLYFRAME_SCRIPT_IDENTITY_TESTING=1)
  if(QUALIFY_NATURAL_INLINE_IMAGES)
    target_compile_definitions(installed_runtime_canonical_v10_worker
      PRIVATE JELLYFRAME_TEST_NATURAL_INLINE_IMAGES=1)
  endif()
  target_link_libraries(installed_runtime_canonical_v10_worker PRIVATE runtime_v10_script)
  v10_test(installed_stylesheet_resources
    "${runtime_repo_root}/ports/esp32s3-idf/tests/installed_resource_owner_tests.cpp"
    "${runtime_repo_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_resources.cpp"
    "${runtime_repo_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_installed_resource_owner.cpp"
    "${runtime_repo_root}/ports/esp32s3-idf/main/jellyframe_esp32s3_image.cpp")
  target_link_libraries(installed_stylesheet_resources PRIVATE runtime_v10_script)
  target_include_directories(installed_stylesheet_resources PRIVATE "${runtime_repo_root}/ports/esp32s3-idf/main")
  target_compile_definitions(installed_stylesheet_resources PRIVATE
    JELLYFRAME_SCRIPT_EVENT_TESTING=1 JELLYFRAME_SCRIPT_IDENTITY_TESTING=1
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_CORE_TEXT_SYSTEM=1
    CONFIG_JELLYFRAME_ESP32S3_ENABLE_BMP_IMAGE_ADAPTER=1
    "JELLYFRAME_CORE_SYSTEM_FONT_FIXTURE=\"${runtime_repo_root}/ports/esp32s3-idf/font/core-system-noto-regular-16.jffont2\"")

endif()

message(STATUS "Core package=${JellyFrameRenderCore_PACKAGE_VERSION} ABI=${JellyFrameRenderCore_ENGINE_ABI} profile=${JellyFrameRenderCore_PROFILE_ID}")
message(STATUS "Core source=${JellyFrameRenderCore_SOURCE_HASH}/${JellyFrameRenderCore_SOURCE_FILE_COUNT}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qualified_core_identity.txt"
  "package=${JellyFrameRenderCore_PACKAGE_VERSION}\nABI=${JellyFrameRenderCore_ENGINE_ABI}\nprofile=${JellyFrameRenderCore_PROFILE_ID}\nsource=${JellyFrameRenderCore_SOURCE_HASH}\nfiles=${JellyFrameRenderCore_SOURCE_FILE_COUNT}\nconfig=${JellyFrameRenderCore_DIR}\n")
