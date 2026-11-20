# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.12.1")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "11")
# This is the deterministic source identity exported by the 0.12.1 candidate
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "d74b28ca238774b6f3b400a5d264b376a8c9dc02ef1e2f65c33c75305f4c254e")
