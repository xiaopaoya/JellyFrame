# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.9.0")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "4")
# This is the deterministic source identity exported by the 0.9.0 candidate
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "d7873ce8c0b25e1012fb151811c9d502533bad00c5c439f82f1ad6628303d0d4")
