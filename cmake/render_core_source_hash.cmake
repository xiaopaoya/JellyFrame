# Compute a deterministic identity for a Render Core source checkout.
#
# This is deliberately a content hash, not a Git revision: source archives and
# local cross-repository overrides must remain identifiable without a VCS
# checkout. It covers the public Render Core implementation, headers and the
# CMake files that select feature-family sources and package metadata.

function(jellyframe_compute_render_core_source_hash source_root)
    get_filename_component(_jellyframe_render_core_hash_root "${source_root}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${_jellyframe_render_core_hash_root}/src/render_core" OR
       NOT IS_DIRECTORY "${_jellyframe_render_core_hash_root}/cmake")
        message(FATAL_ERROR
            "Render Core source hash requires src/render_core and cmake under: ${_jellyframe_render_core_hash_root}")
    endif()

    file(GLOB_RECURSE _jellyframe_render_core_hash_sources CONFIGURE_DEPENDS
        RELATIVE "${_jellyframe_render_core_hash_root}"
        "${_jellyframe_render_core_hash_root}/src/render_core/*.cpp"
        "${_jellyframe_render_core_hash_root}/src/render_core/*.h"
        "${_jellyframe_render_core_hash_root}/src/render_core/*.hpp")
    file(GLOB _jellyframe_render_core_hash_cmake CONFIGURE_DEPENDS
        RELATIVE "${_jellyframe_render_core_hash_root}"
        "${_jellyframe_render_core_hash_root}/cmake/render_core_*.cmake"
        "${_jellyframe_render_core_hash_root}/cmake/render_core_*.json.in"
        "${_jellyframe_render_core_hash_root}/cmake/render_core_*.csv"
        "${_jellyframe_render_core_hash_root}/cmake/JellyFrameRenderCoreConfig.cmake.in")
    list(APPEND _jellyframe_render_core_hash_sources ${_jellyframe_render_core_hash_cmake})
    list(REMOVE_DUPLICATES _jellyframe_render_core_hash_sources)
    list(SORT _jellyframe_render_core_hash_sources)
    if(NOT _jellyframe_render_core_hash_sources)
        message(FATAL_ERROR "Render Core source hash input set is empty")
    endif()

    set(_jellyframe_render_core_hash_manifest "")
    set(_jellyframe_render_core_hash_manifest_entries)
    foreach(_jellyframe_render_core_hash_relative IN LISTS _jellyframe_render_core_hash_sources)
        # Source packages and Git checkouts may use different text line
        # endings on Windows and Unix. Core identity describes source content,
        # not a platform checkout representation, so normalize before hashing.
        file(READ "${_jellyframe_render_core_hash_root}/${_jellyframe_render_core_hash_relative}"
            _jellyframe_render_core_hash_content)
        string(REPLACE "\r\n" "\n" _jellyframe_render_core_hash_content
            "${_jellyframe_render_core_hash_content}")
        string(REPLACE "\r" "\n" _jellyframe_render_core_hash_content
            "${_jellyframe_render_core_hash_content}")
        string(SHA256 _jellyframe_render_core_hash_file
            "${_jellyframe_render_core_hash_content}")
        string(APPEND _jellyframe_render_core_hash_manifest
            "${_jellyframe_render_core_hash_relative}\t${_jellyframe_render_core_hash_file}\n")
        string(REPLACE "\\" "\\\\" _jellyframe_render_core_hash_json_path
            "${_jellyframe_render_core_hash_relative}")
        string(REPLACE "\"" "\\\"" _jellyframe_render_core_hash_json_path
            "${_jellyframe_render_core_hash_json_path}")
        list(APPEND _jellyframe_render_core_hash_manifest_entries
            "    {\"path\": \"${_jellyframe_render_core_hash_json_path}\", \"sha256\": \"${_jellyframe_render_core_hash_file}\"}")
    endforeach()
    string(SHA256 _jellyframe_render_core_hash_value "${_jellyframe_render_core_hash_manifest}")
    list(LENGTH _jellyframe_render_core_hash_sources _jellyframe_render_core_hash_count)

    set(JELLYFRAME_RENDER_CORE_SOURCE_HASH "${_jellyframe_render_core_hash_value}" PARENT_SCOPE)
    set(JELLYFRAME_RENDER_CORE_SOURCE_FILE_COUNT "${_jellyframe_render_core_hash_count}" PARENT_SCOPE)
    string(JOIN ",\n" _jellyframe_render_core_hash_manifest_entries_json
        ${_jellyframe_render_core_hash_manifest_entries})
    set(JELLYFRAME_RENDER_CORE_SOURCE_MANIFEST_ENTRIES_JSON
        "${_jellyframe_render_core_hash_manifest_entries_json}" PARENT_SCOPE)
endfunction()
