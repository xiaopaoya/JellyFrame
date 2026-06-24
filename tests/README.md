# Tests

Root-level tests are reserved for future cross-subproject and app-lifecycle
acceptance tests.

Current subproject tests live next to their owners:

- `../src/render_core/tests`: render pipeline, DOM, CSS, layout, layer,
  framebuffer, input and diagnostics tests.
- `../src/app_runtime/tests`: app-runtime host-service queue and handle tests.
- `../src/script/tests`: optional JerryScript bridge tests.
- `fixtures/apps`: intentionally small app packages used by cross-subproject
  acceptance tests. These are not polished user samples; for example,
  `jelly_watchdog_smoke` deliberately runs an endless script so the Win32 shell
  can validate watchdog recovery.
