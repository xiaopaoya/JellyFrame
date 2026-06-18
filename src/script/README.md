# Script

Optional JerryScript integration.

This layer binds the documented JellyFrame DOM/event/timer subset to
JerryScript. It is disabled unless `JELLYFRAME_BUILD_SCRIPTING=ON` is set.

Keep this directory separate from `src/core` so embedded builds can ship without
JavaScript support.
