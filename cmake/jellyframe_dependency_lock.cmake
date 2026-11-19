# Pinned public dependencies for the JellyFrame Runtime repository boundary.
#
# Keep these values independent from the top-level JellyFrame development
# version. An extracted Render Core may iterate more frequently while the
# Runtime only accepts an explicitly reviewed package/ABI pair.

set(JELLYFRAME_RENDER_CORE_LOCKED_VERSION "0.10.1")
set(JELLYFRAME_RENDER_CORE_LOCKED_ENGINE_ABI "9")
# This is the deterministic source identity exported by the 0.10.1 release
# package. Package consumers must match it exactly; archive checksums remain
# release-artifact metadata because they are not available from an installed
# CMake package.
set(JELLYFRAME_RENDER_CORE_LOCKED_SOURCE_HASH
    "04700108f8ace9f2056dcc6d4a7cbfa07f58e0298e8140b31609a3dd1e26d424")
