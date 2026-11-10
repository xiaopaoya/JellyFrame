cmake_minimum_required(VERSION 3.16)
project(RuntimeCanonicalV9InstalledConsumer LANGUAGES CXX)
find_package(JellyFrameRenderCore CONFIG REQUIRED)
get_filename_component(_jellyframe_core_prefix "${JellyFrameRenderCore_DIR}/../../.." ABSOLUTE)
include_directories(BEFORE "${_jellyframe_core_prefix}/include")
if(NOT JellyFrameRenderCore_ENGINE_ABI STREQUAL "4")
  message(FATAL_ERROR "Canonical v9 installed qualification requires actual ABI4")
endif()
foreach(identity IN ITEMS SOURCE_HASH PROFILE_ID)
  if(NOT DEFINED EXPECTED_CORE_${identity} OR "${EXPECTED_CORE_${identity}}" STREQUAL "")
    message(FATAL_ERROR "Set EXPECTED_CORE_${identity} to the qualified package identity")
  endif()
  if(NOT "${JellyFrameRenderCore_${identity}}" STREQUAL "${EXPECTED_CORE_${identity}}")
    message(FATAL_ERROR "Canonical v9 consumer refuses Core ${identity} mismatch")
  endif()
endforeach()
get_filename_component(runtime_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(runtime_source "${CMAKE_CURRENT_BINARY_DIR}/runtime-source")
file(COPY "${runtime_repo_root}/src/app_runtime" DESTINATION "${runtime_source}/src")
add_executable(runtime_canonical_v9_consumer
  main.cpp
  "${runtime_source}/src/app_runtime/app_font_set.cpp"
  "${runtime_source}/src/app_runtime/script_task_contract.cpp"
  "${runtime_source}/src/app_runtime/script_task_frame_codec.cpp"
  "${runtime_source}/src/app_runtime/script_task_frame_renderer.cpp"
  "${runtime_source}/src/app_runtime/runtime_font_catalog.cpp"
  "${runtime_source}/src/app_runtime/runtime_image_provider.cpp"
  "${runtime_source}/src/app_runtime/tests/script_task_frame_codec_tests.cpp"
  "${runtime_source}/src/app_runtime/tests/script_task_frame_renderer_tests.cpp")
target_include_directories(runtime_canonical_v9_consumer PRIVATE "${runtime_source}/src")
target_include_directories(runtime_canonical_v9_consumer BEFORE PRIVATE
  "${_jellyframe_core_prefix}/include")
target_link_libraries(runtime_canonical_v9_consumer PRIVATE JellyFrame::jellyframe_render_core)
target_compile_features(runtime_canonical_v9_consumer PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(runtime_canonical_v9_consumer PRIVATE /UNDEBUG)
else()
  target_compile_options(runtime_canonical_v9_consumer PRIVATE -UNDEBUG)
endif()
message(STATUS "Core package=${JellyFrameRenderCore_PACKAGE_VERSION} ABI=${JellyFrameRenderCore_ENGINE_ABI} profile=${JellyFrameRenderCore_PROFILE_ID}")
message(STATUS "Core source=${JellyFrameRenderCore_SOURCE_HASH}/${JellyFrameRenderCore_SOURCE_FILE_COUNT} features=${JellyFrameRenderCore_FEATURES}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qualified_core_identity.txt"
  "package=${JellyFrameRenderCore_PACKAGE_VERSION}\nABI=${JellyFrameRenderCore_ENGINE_ABI}\nprofile=${JellyFrameRenderCore_PROFILE_ID}\nsource=${JellyFrameRenderCore_SOURCE_HASH}\nfiles=${JellyFrameRenderCore_SOURCE_FILE_COUNT}\nfeatures=${JellyFrameRenderCore_FEATURES}\nconfig=${JellyFrameRenderCore_DIR}\n")
enable_testing()
add_test(NAME installed_runtime_canonical_v9 COMMAND runtime_canonical_v9_consumer)

add_executable(runtime_image_owner_allocation_consumer image_owner_allocation.cpp
  "${runtime_source}/src/app_runtime/runtime_image_provider.cpp"
  "${runtime_source}/src/app_runtime/runtime_font_catalog.cpp"
  "${runtime_source}/src/app_runtime/script_task_contract.cpp"
  "${runtime_source}/src/app_runtime/script_task_frame_codec.cpp"
  "${runtime_source}/src/app_runtime/script_task_frame_renderer.cpp")
target_include_directories(runtime_image_owner_allocation_consumer PRIVATE "${runtime_source}/src")
target_include_directories(runtime_image_owner_allocation_consumer BEFORE PRIVATE
  "${_jellyframe_core_prefix}/include")
target_link_libraries(runtime_image_owner_allocation_consumer PRIVATE JellyFrame::jellyframe_render_core)
target_compile_features(runtime_image_owner_allocation_consumer PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(runtime_image_owner_allocation_consumer PRIVATE /UNDEBUG)
else()
  target_compile_options(runtime_image_owner_allocation_consumer PRIVATE -UNDEBUG)
endif()
add_test(NAME installed_runtime_image_owner_allocation COMMAND runtime_image_owner_allocation_consumer)

include(worker.cmake)
