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
  include("${runtime_repo_root}/cmake/jellyframe_script_event_config.cmake")
  jellyframe_configure_script_events(runtime_v10_script PUBLIC JellyFrame::jellyframe_render_core)
  target_link_libraries(runtime_v10_script PUBLIC runtime_v10_support
    PRIVATE ${JELLYFRAME_SCRIPT_BACKEND_LIBRARIES})
  target_include_directories(runtime_v10_script PRIVATE ${JELLYFRAME_SCRIPT_BACKEND_INCLUDE_DIRS})
  target_compile_definitions(runtime_v10_script PUBLIC JELLYFRAME_ENABLE_SCRIPTING=1
    PRIVATE JELLYFRAME_SCRIPT_ENGINE_JERRYSCRIPT=1)
  set_source_files_properties("${script_cpp}/tests/script_runtime_tests.cpp"
    PROPERTIES COMPILE_DEFINITIONS "main=script_runtime_tests_main")
  v10_test(installed_runtime_canonical_v10_worker
    "${runtime_repo_root}/src/script/tests/script_tests.cpp"
    "${script_cpp}/tests/script_runtime_tests.cpp"
    "${script_cpp}/tests/script_task_worker_runtime_tests.cpp")
  target_compile_definitions(installed_runtime_canonical_v10_worker
    PRIVATE JELLYFRAME_SCRIPT_TASK_RUNTIME_ENABLED=1)
  target_link_libraries(installed_runtime_canonical_v10_worker PRIVATE runtime_v10_script)
endif()

message(STATUS "Core package=${JellyFrameRenderCore_PACKAGE_VERSION} ABI=${JellyFrameRenderCore_ENGINE_ABI} profile=${JellyFrameRenderCore_PROFILE_ID}")
message(STATUS "Core source=${JellyFrameRenderCore_SOURCE_HASH}/${JellyFrameRenderCore_SOURCE_FILE_COUNT}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qualified_core_identity.txt"
  "package=${JellyFrameRenderCore_PACKAGE_VERSION}\nABI=${JellyFrameRenderCore_ENGINE_ABI}\nprofile=${JellyFrameRenderCore_PROFILE_ID}\nsource=${JellyFrameRenderCore_SOURCE_HASH}\nfiles=${JellyFrameRenderCore_SOURCE_FILE_COUNT}\nconfig=${JellyFrameRenderCore_DIR}\n")
