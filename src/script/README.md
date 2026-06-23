# Script

Optional JerryScript integration.

This layer binds the documented JellyFrame DOM/event/timer subset to
JerryScript. It is disabled unless `JELLYFRAME_BUILD_SCRIPTING=ON` is set.
Product builds should use a JerryScript library built with `JERRY_VM_HALT=ON`
when script execution budgets are required.

Keep this directory separate from `src/render_core` and `src/app_runtime` so
embedded builds can ship without JavaScript support.
