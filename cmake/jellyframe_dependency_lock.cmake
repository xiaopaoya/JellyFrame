# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.12.0")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "11")
# This is the deterministic source identity exported by the 0.12.0 candidate
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "dfb4723c72679f2a7a7bdfa71360c8055e03ba5d795a9afac73cab3076be3849")
