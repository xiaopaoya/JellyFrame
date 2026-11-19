# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.11.0")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "10")
# This is the deterministic source identity exported by the 0.11.0 release
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "f71e637c92c3d5e031fe11f49700aa80cf05bdd77700629f9c40fcc93b4c7cb4")
