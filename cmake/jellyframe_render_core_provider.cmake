# Runtime-owned Render Core provider selection and provenance.
#
# Keep this boundary separate from render_core_*.cmake: those files are part of
# the independently distributable Render Core source package, while this module
# belongs to the JellyFrame Runtime consumer and its version/ABI policy.

set(JELLYFRAME_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(_jellyframe_runtime_cmake_dir "${CMAKE_CURRENT_LIST_DIR}")

if(JELLYFRAME_RENDER_CORE_SOURCE_DIR)
    get_filename_component(JELLYFRAME_RENDER_CORE_SOURCE_ROOT
        "${JELLYFRAME_RENDER_CORE_SOURCE_DIR}" ABSOLUTE)
else()
    set(JELLYFRAME_RENDER_CORE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "in-tree")
    if(NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_version.cmake")
        message(FATAL_ERROR
            "Render Core source checkout is missing cmake/render_core_version.cmake: ${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
    endif()
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_version.cmake")
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_source_hash.cmake")
    jellyframe_compute_render_core_source_hash("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
    if(JELLYFRAME_RENDER_CORE_SOURCE_DIR)
        set(JELLYFRAME_RENDER_CORE_PROVENANCE_PROVIDER "source-override")
    else()
        set(JELLYFRAME_RENDER_CORE_PROVENANCE_PROVIDER "in-tree")
    endif()
    set(JELLYFRAME_RENDER_CORE_PROVENANCE_LOCK_ENFORCED false)
endif()

include("${_jellyframe_runtime_cmake_dir}/jellyframe_dependency_lock.cmake")
include("${_jellyframe_runtime_cmake_dir}/render_core_link_map.cmake")

if(JELLYFRAME_RENDER_CORE_PROVIDER STREQUAL "in-tree")
    if(NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_sources.cmake" OR
       NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_feature_profile.cmake" OR
       NOT EXISTS "${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_build.cmake")
        message(FATAL_ERROR
            "Render Core source checkout is missing its CMake boundary: ${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}")
    endif()
    include("${JELLYFRAME_RENDER_CORE_SOURCE_ROOT}/cmake/render_core_build.cmake")
else()
    if(JELLYFRAME_RENDER_CORE_PACKAGE_DIR)
        find_package(JellyFrameRenderCore ${JELLYFRAME_RENDER_CORE_LOCKED_VERSION} EXACT CONFIG REQUIRED
            PATHS "${JELLYFRAME_RENDER_CORE_PACKAGE_DIR}" NO_DEFAULT_PATH)
    else()
        find_package(JellyFrameRenderCore ${JELLYFRAME_RENDER_CORE_LOCKED_VERSION} EXACT CONFIG REQUIRED)
    endif()
    if(NOT TARGET JellyFrame::jellyframe_render_core)
        message(FATAL_ERROR "JellyFrameRenderCore package did not export JellyFrame::jellyframe_render_core")
    endif()
    if(NOT DEFINED JellyFrameRenderCore_PACKAGE_VERSION OR
       NOT JellyFrameRenderCore_PACKAGE_VERSION STREQUAL JELLYFRAME_RENDER_CORE_LOCKED_VERSION)
        message(FATAL_ERROR
            "JellyFrameRenderCore package declares version '${JellyFrameRenderCore_PACKAGE_VERSION}', expected locked version "
            "${JELLYFRAME_RENDER_CORE_LOCKED_VERSION}")
    endif()
    if(NOT JellyFrameRenderCore_ENGINE_ABI STREQUAL JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI)
        message(FATAL_ERROR
            "JellyFrameRenderCore ABI ${JellyFrameRenderCore_ENGINE_ABI} does not match locked ABI "
            "${JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI}")
    endif()
    set(_jellyframe_render_core_package_source_hash "${JellyFrameRenderCore_SOURCE_HASH}")
    string(LENGTH "${_jellyframe_render_core_package_source_hash}"
        _jellyframe_render_core_package_source_hash_length)
    if(NOT DEFINED JellyFrameRenderCore_SOURCE_HASH OR
       NOT _jellyframe_render_core_package_source_hash_length EQUAL 64 OR
       NOT JellyFrameRenderCore_SOURCE_HASH MATCHES "^[0-9a-fA-F]+$")
        message(FATAL_ERROR "JellyFrameRenderCore package has no valid source hash")
    endif()
    string(TOLOWER "${JellyFrameRenderCore_SOURCE_HASH}"
        _jellyframe_render_core_package_source_hash_normalized)
    if(NOT _jellyframe_render_core_package_source_hash_normalized STREQUAL
       JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH)
        message(FATAL_ERROR
            "JellyFrameRenderCore source hash ${JellyFrameRenderCore_SOURCE_HASH} does not match locked source hash "
            "${JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH}")
    endif()
    if(NOT DEFINED JellyFrameRenderCore_SOURCE_FILE_COUNT OR
       NOT JellyFrameRenderCore_SOURCE_FILE_COUNT MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "JellyFrameRenderCore package has no valid source file count")
    endif()
    if(NOT EXISTS "${JellyFrameRenderCore_PROFILE_FILE}")
        message(FATAL_ERROR "JellyFrameRenderCore profile is missing: ${JellyFrameRenderCore_PROFILE_FILE}")
    endif()
    if(NOT EXISTS "${JellyFrameRenderCore_SOURCE_MANIFEST_FILE}")
        message(FATAL_ERROR
            "JellyFrameRenderCore source manifest is missing: ${JellyFrameRenderCore_SOURCE_MANIFEST_FILE}")
    endif()
    file(READ "${JellyFrameRenderCore_SOURCE_MANIFEST_FILE}"
        _jellyframe_render_core_package_source_manifest)
    string(FIND "${_jellyframe_render_core_package_source_manifest}"
        "\"sourceHash\": \"${JellyFrameRenderCore_SOURCE_HASH}\""
        _jellyframe_render_core_package_source_hash_position)
    string(FIND "${_jellyframe_render_core_package_source_manifest}"
        "\"sourceFileCount\": ${JellyFrameRenderCore_SOURCE_FILE_COUNT}"
        _jellyframe_render_core_package_source_count_position)
    if(_jellyframe_render_core_package_source_hash_position EQUAL -1 OR
       _jellyframe_render_core_package_source_count_position EQUAL -1)
        message(FATAL_ERROR
            "JellyFrameRenderCore source manifest does not match its exported source identity")
    endif()
    add_library(jellyframe_render_core ALIAS JellyFrame::jellyframe_render_core)
    set(JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}")
    configure_file("${JellyFrameRenderCore_PROFILE_FILE}"
        "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_profile.json"
        COPYONLY)
    configure_file("${JellyFrameRenderCore_SOURCE_MANIFEST_FILE}"
        "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_source_manifest.json"
        COPYONLY)
    set(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION "${JellyFrameRenderCore_PACKAGE_VERSION}")
    set(JELLYFRAME_RENDER_CORE_ENGINE_ABI "${JellyFrameRenderCore_ENGINE_ABI}")
    set(JELLYFRAME_RENDER_CORE_SOURCE_HASH "${JellyFrameRenderCore_SOURCE_HASH}")
    set(JELLYFRAME_RENDER_CORE_SOURCE_FILE_COUNT "${JellyFrameRenderCore_SOURCE_FILE_COUNT}")
    set(JELLYFRAME_RENDER_CORE_PROVENANCE_PROVIDER "package")
    set(JELLYFRAME_RENDER_CORE_PROVENANCE_LOCK_ENFORCED true)
    unset(_jellyframe_render_core_package_source_hash)
    unset(_jellyframe_render_core_package_source_hash_length)
    unset(_jellyframe_render_core_package_source_hash_normalized)
    unset(_jellyframe_render_core_package_source_manifest)
    unset(_jellyframe_render_core_package_source_hash_position)
    unset(_jellyframe_render_core_package_source_count_position)
endif()

set(JELLYFRAME_RENDER_CORE_PROVENANCE_FILE
    "${JELLYFRAME_RENDER_CORE_PROFILE_OUTPUT_DIR}/jellyframe_render_core_provenance.json")
configure_file(
    "${_jellyframe_runtime_cmake_dir}/render_core_provenance.json.in"
    "${JELLYFRAME_RENDER_CORE_PROVENANCE_FILE}"
    @ONLY)

unset(_jellyframe_runtime_cmake_dir)
