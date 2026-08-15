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
    "cc78dc3f1bac8aa03d3c928ccf5292407586fe70d08d6ecec1907d59b41609e1")
