# Public version metadata for the standalone Render Core boundary.
# Keep this file in the Render Core source checkout so a Runtime source override
# carries its own package identity instead of inheriting the Runtime version.

set(JELLYFRAME_RENDER_CORE_PACKAGE_VERSION "0.6.2" CACHE STRING
    "Render Core package version")
set(JELLYFRAME_RENDER_CORE_ENGINE_ABI "1" CACHE STRING
    "Render Core public engine ABI version")
