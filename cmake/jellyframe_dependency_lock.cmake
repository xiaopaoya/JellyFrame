# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.6.0")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "1")
# This is the deterministic source identity exported by the reviewed 0.6.0
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "c0740548398f1678e2fb73d5272992627d8ce279b60f5256f23d53f5209af413")
