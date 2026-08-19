# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.6.1")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "1")
# This is the deterministic source identity exported by the reviewed 0.6.1
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "105d016677a689a4cc352884464aa552aa023f72c20c8b25eed12bc9b797c52b")
