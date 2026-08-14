# Runtime-owned active development-line identity.
#
# Render Core owns its package version in render_core_version.cmake. The
# Runtime reads its own VERSION file here so native package loading shares the
# same active-line contract as the packer and registry tools.

file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" _jellyframe_runtime_version_raw)
string(STRIP "${_jellyframe_runtime_version_raw}" _jellyframe_runtime_version_raw)
string(REGEX REPLACE "-.*$" "" JELLYFRAME_RUNTIME_ACTIVE_RELEASE_VERSION
    "${_jellyframe_runtime_version_raw}")
if(NOT JELLYFRAME_RUNTIME_ACTIVE_RELEASE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION does not declare a release version: '${_jellyframe_runtime_version_raw}'")
endif()

unset(_jellyframe_runtime_version_raw)
