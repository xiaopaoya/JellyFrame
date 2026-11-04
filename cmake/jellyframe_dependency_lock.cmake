# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.7.0")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "2")
# This is the deterministic source identity exported by the reviewed 0.7.0
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "609927a3cb5307c9c9f33314cc2638b8aae4bec8c71d91208c2f9eb7c521fb55")
