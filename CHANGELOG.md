# Changelog

All notable changes to JellyFrame Engine are tracked here.

The project uses lightweight semantic versioning. See `docs/versioning.md`.

## Unreleased

### Added

- Started the `0.5.0-dev` device-usability line with a fixed-size storage
  lifecycle report for system shells. Hosts can now apply exit, crash, uninstall,
  update and memory-pressure storage policy through one helper and receive stable
  diagnostics such as `storage-flush-ok`, `storage-flush-failed`,
  `storage-drop-pending`, `storage-delete-data` and `storage-retain-data`.
- `AppBudgetSnapshot` can now carry lightweight localStorage-shadow item/byte
  counters when the host supplies them, and the Win32 shell prints those counters
  in scripted capture summaries.
- The desktop installed-app registry mock now models app-private data under
  `data/<sanitized-app-id>`. Removing an app deletes that data by default,
  `--keep-data` retains it, and `delete-data` / Win32 `--delete-app-data` can
  clear data without removing the installed bundle.
- Added fixed-size budget recovery classification. `AppBudgetRecoveryReport`
  maps exhausted runtime counters to `warn` or `terminate-app`; Win32
  system-shell validation now recovers request-queue exhaustion as
  `budget-exceeded` and returns to the launcher.
- Documented the authorized file-access boundary: ordinary apps still have no
  general filesystem access, while future file managers or system components
  should use a host-owned, user-authorized file broker with async budgets,
  rollback/fallback and no raw filesystem handles.
- Win32 frame-script summaries now include `layer_tree layers=N
  display_commands=N`, making retained-rendering and full-frame fallback
  sampling easier to compare across watch samples.
- `embedded_framebuffer` now offers optional `EmbeddedFrameBufferPresentStats`
  for board bring-up, reporting converted pixels, packed bytes, clipped/empty
  dirty rects and flush count. The virtual board benchmark prints these fields
  so port-side panel bytes and DMA wait time can be compared with core output.
- Added the first production image codec adapter shape. `AppImageCodecAdapter`
  and `AppImageCodecRequest`/`AppImageCodecResult` let product hosts connect
  PNG, JPEG, WebP or vendor decoders behind the existing image
  request/completion/handle path, while `app_image_codec_result_within_policy`
  validates decoded surfaces against app image budgets.
- Package reports now include `imageDiagnostics`, a packaging-time image codec
  and target-profile summary. The tool classifies package-local BMP, PNG, JPEG,
  WebP, GIF and unknown image resources, reads lightweight BMP/PNG metadata,
  reports the selected target's `hostServices.imageDecode` / `imageCodecs`
  support and warns when a package uses an unsupported or unvalidated codec.
- Added a package-image acceptance regression that checks `watch_weather`
  `imageDiagnostics`, captures the sample through the Win32 shell and inspects
  the output BMP pixels to make sure package-local BMP icons are actually
  painted.

### Changed

- Low-depth embedded framebuffer conversion is now dispatched per rectangle and
  pixel format, reducing per-pixel branch and stride recomputation overhead on
  RGB565/BGR565-style targets.

## 0.4.0-dev - 2026-06-28

### Added

- Added an optional JerryScript execution watchdog. Runtime options and
  `HostBudgets` can now set a finite script execution-check budget; when linked
  against a JerryScript library built with `JERRY_VM_HALT=ON`, runaway evals,
  timers, rAF and event callbacks are interrupted with a stable
  `script execution budget exceeded` exception.
- Added stable app teardown reasons and `AppRuntimeHost::terminate_current(...)`
  so hosts can distinguish normal exit, app switch, user kill, runtime error,
  script watchdog, budget exceeded, load failure and system policy recovery
  while reusing the same bounded request/completion/handle/font cleanup path.
- `ScriptEvaluationResult` now carries a stable status, and the scripting
  runtime exposes a sticky watchdog-interrupt flag for callback paths. The Win32
  shell uses this to recover package apps after script watchdog interrupts.
- Added a low-cost CSS `background: linear-gradient(...)` paint subset. Two
  color vertical gradients now flow from style resolution into the layer display
  list and software rasterizer, while unsupported angles/stops keep earlier
  solid fallbacks alive.
- Extended the opt-in visual CSS subset with horizontal `linear-gradient(...)`,
  first-shadow `text-shadow` painting and non-layout `outline` strokes, and
  documented the standard-subset/opt-in-cost policy for future visual features.
- Added the first visual-quality/antialiasing path: rounded
  fill/stroke/linear-gradient commands use local coverage AA, composited layer
  scale defaults to bilinear sampling, `image-rendering: auto | pixelated |
  crisp-edges` is passed to image painters, and RGB565/BGR565 embedded
  framebuffer targets can enable 4x4 ordered dithering. Ordinary opaque square
  rectangles keep the fast fill path.
- Added opt-in bitmap-font antialiasing through `.jffont` V1 coverage glyphs.
  `jellyframe_font_pack_gen --coverage-bits 2|4` can emit 2bpp/4bpp glyph
  coverage into C++ `BitmapFont` headers or `.jffont` resources, while V0/1bpp
  fonts keep the compact no-extra-cost path. Package diagnostics now report the
  parsed font coverage depth for manifest `.jffont` resources.
- Added a cheap `text-decoration` / `text-decoration-line` subset for
  `underline`, `line-through` and `none`.
- Added the first bounded CSS `@keyframes` / `animation-*` subset. The parser
  stores `from`/`to` or `0%`/`100%` keyframes, style resolution keeps up to four
  animation entries per style, and `AnimationTimeline` samples `opacity`,
  `transform: translate()/scale()`, `background-color` and `color` under the
  shared active-animation budget.
- Added diagnostics and tests for unsupported keyframe properties, missing
  keyframe names and bounded keyframe sampling, plus a render-core
  `keyframe_animation_sample` microbenchmark.
- Updated `watch_weather` with a small standard CSS keyframe pulse so packaged
  app samples demonstrate supported animation without custom APIs.
- Added Jelly UI visual-system samples: the installable `jelly_controls`
  package, a focused `jelly_motion` fixture and a `jelly_launcher_mock` fixture.
  The sample launcher now follows the same gel panel/button style without
  relying on unsupported pseudo-element drawing.
- Added the installable `jelly_motion_lab` sample for LVGL/watch-style motion:
  icon-to-window expansion, bottom-sheet rise and jelly button feedback. It also
  updates a frame counter through `requestAnimationFrame` to validate JS/rAF.
- Added scripting-build CTest coverage for `jelly_motion_lab` deterministic
  captures: a 90-frame 30fps soak and a low-power budget run that verifies
  animation work stops while presentation remains safe.
- The Win32 browser shell now supports hidden deterministic frame capture with
  `--capture-frames DIR --frame-count 30 --frame-step-ms 33`, plus
  `--frame-event FRAME:kind[:x:y]` for click, pointer and system-state injection.
  The same path can now be driven by `--frame-script PATH`, including frame
  count, viewport, events, per-frame output and contact-sheet montage output.
- Main native command-line tools now accept `--help` / `-h`.
- Added the R1 responsive profile report: `jellyframe_cli.py check`/`preview`/
  `package`/`install` can explicitly pass `--targets` or `--all-targets`, run
  the render-core pseudo browser across multiple target presets and write
  `responsiveProfiles[]` into JSON reports, including viewport, content height,
  layout bounds, horizontal/vertical overflow, pipeline counts and diagnostic
  summaries. Single-target commands keep the older report shape.
- Added the generic `rect-172x320` target preset for narrow portrait wearable
  panels such as Waveshare ESP32-S3-Touch-LCD-1.47 class boards. The weather,
  controls, motion-lab and service-status sample packages now declare and pass
  `round-300`, `rect-320x240` and `rect-172x320` responsive profiles.
- Added font-family policy diagnostics for packages. `fontDiagnostics` now
  includes `fontFamilyUsage`, matching explicit CSS `font-family` declarations
  against manifest `.jffont` family metadata, generic fallback names and
  unmatched primary custom families. Added the `jelly_font_policy` sample
  package and an app-runtime font fallback microbenchmark. Runtime app-font
  selection now uses normalized `font-family` preferences so matching manifest
  `.jffont` resources are measured and painted consistently when enabled.
- Added manifest/profile policy merging for optional data services:
  `AppServiceManifestCapabilities`, `AppServiceHostProfile` and
  `app_service_policies_for_app(...)` now gate `network.fetch` and `storage.kv`
  before runtime mocks or JS bindings can submit work.
- Added `AppSystemEventQueue`, a bounded app-instance-scoped queue for
  host-injected time, timezone, network, battery, screen and low-power snapshots.
- `AppSystemEventQueue` now exposes `try_push_current(...)` and stable push
  status names so hosts/the Win32 shell can report system-event injection
  failures such as `empty-instance` and `queue-full` through diagnostics.
- The JerryScript bridge now maps accepted network status changes to the
  standard `window` `online`/`offline` event subset, with bounded function
  listeners, `removeEventListener` and `once` support.
- Added app-runtime microbench coverage for optional network fetch, KV storage,
  image-decode mock and system-event pumping.
- Added a static-table app host service worker group pump so cooperative MCU
  loops can pump network, storage and audio workers with per-service budgets
  without dynamic allocation or cross-service request consumption.
- `AppPrivateKvStorageMock` now exposes `complete_request(...)`, matching the
  network/image mock worker boundary so generic host workers can pop a
  `StorageKv` request, produce a normalized completion and leave queue posting
  to the shared pump. Regression coverage now soaks mixed network and storage
  mock workers across many ticks and verifies handle release.
- Package `serviceIntent` reports now include `targetSupport` for optional
  host services when a target preset declares `hostServices`; absent profile
  data remains `unknown`.
- Built-in target presets now declare conservative `hostServices` support, and
  packaging warns with `service-target-unsupported` when a requested service is
  explicitly unavailable on the selected target.
- Manifest font `sizes` and `weights` metadata now produce stable diagnostics:
  missing arrays report `font-axis-metadata-missing`, invalid arrays report
  `font-axis-metadata-invalid`, and normalized values appear in
  `fontDiagnostics.manifestFonts[]`.
- The Win32 debug shell now drives mock image decode and debug network fetch
  through the same host worker pump boundary used by ports, while keeping real
  resources and callbacks on the UI completion path.
- Added the B1 image-decode V0 helper: `ImageDecodePolicy`, `ImageDecodeMock`
  and `AppDecodedSurfaceRecord` define platform-neutral raw-surface fixtures,
  `Surface` handle lifetime, width/height/decoded-byte/pending budgets and
  release rules.
- Added a lightweight render-core image display command, `ImageHandleResolver`
  and `ImagePainter`; `<img src>` can now enter the display list when the host
  supplies a surface-handle resolver, and the host painter owns actual drawing.
  Real resource loading and production codecs remain host/runtime integration
  work.
- Added `AppImageSurfaceCache` to map `<img src>`/icon URLs to bounded async
  image-decode requests, completions, ready surface handles and release. The
  Win32 browser debug shell now wires `/debug/icon.raw` and `/debug/photo.raw`
  raw RGB565 fixtures, automatically submits mock decodes and schedules
  paint-dirty rerender after completion.
- The Win32 browser debug shell can load uncompressed 24/32-bit BMP resources
  from `.jfapp`/source packages as the in-bundle image V0 path, reusing the image
  surface cache and repaint flow.
- `AppImageSurfaceCache` now supports general ready-surface eviction by surface
  count and decoded-byte budgets while protecting surfaces referenced by the
  current display list. Render core now carries an `object-fit` subset
  (`fill`, `contain`, `cover`, `none`, `scale-down`) and a keyword/percentage
  one/two-value `object-position` subset through image display commands; the
  Win32 painter draws images using that position.
  The Win32 debug shell reports image-decode request rejections and completion
  failures through diagnostics, preserving the original `src`, stable failure
  reason and status for missing-resource, budget-rejection and decode-failure
  debugging. app-runtime now exposes `classify_app_image_failure(...)` /
  `app_image_failure_detail(...)` plus
  `AppImageSurfaceCache::diagnostic_detail_for_url(...)` so desktop tools and
  future embedded diagnostic ports can share the same image failure categories
  and stable cache-state fields after request or completion failures.
  PNG/JPEG/WebP, complex four-value/length-offset `object-position` and
  production MCU codecs remain future work.
- Hardened image cache lifecycle behavior: direct `AppImageSurfaceCache`
  completion handling now rejects stale app-instance completions, eviction can
  drop/report stale ready entries instead of stalling behind invalid handles,
  and the Win32 debug shell reports stale image-cache drops through diagnostics.
- Added `runtime_data_api.md` / `runtime_data_api_zh.md`, documenting the
  standard-subset runtime data API direction: asynchronous `XMLHttpRequest`
  first, `fetch()` only after bounded Promise/microtask support, tiny
  `localStorage` only when backed by a non-blocking app-private shadow, and
  system state mapped to web-adjacent events where possible.
- Added `AppLocalStorageShadow`, a compact in-memory helper for the standard
  `localStorage` V0 subset that enforces the app-private KV policy without
  performing host I/O on the UI task.
- Added `AppXmlHttpRequest`, a platform-neutral async XHR V0 state machine over
  `NetworkFetchMock`/host completions, covering GET, abort, readyState/status,
  response text and standard event sequencing used by the JS binding.
- Exposed an async `XMLHttpRequest` GET V0 subset through the JerryScript bridge
  and wired Win32 browser scripting builds to a debug network mock so host
  completions dispatch JavaScript callbacks on the main task.
- Exposed a tiny `localStorage` V0 subset through the JerryScript bridge when a
  host binds a non-blocking `AppLocalStorageShadow`; Win32 browser scripting
  builds provide a debug shadow cleared on active app instance changes.
- Extended the host-optional JerryScript `Audio()` V0 subset with
  `onended`/`onerror`, `addEventListener`/`removeEventListener` for `ended` and
  `error`, a public runtime dispatch hook for host status events and Win32
  debug-shell `ended` dispatch after local/package playback.
- Added `.jffont` V0 binary font supplement output to
  `jellyframe_font_pack_gen`, reusing the existing `BitmapFont` glyph data model
  without C++ pointers or compile-time symbols as groundwork for future dynamic
  in-bundle `.jfapp` fonts.
- Added `BitmapFontResource`, which parses `.jffont` bytes into a read-only
  `BitmapFont` view that reuses the existing bitmap font measurement and
  painting backend.
- Added `AppFontSet` and wired it into `AppRuntimeHost` so `.jffont` resources
  load, clear and switch with `app_instance_id`; the Win32 package loader reads
  manifest `fonts` declarations and attaches `.jffont` resources to the current
  runtime state.
- Added `--use-app-fonts` to the Win32 browser shell so in-bundle `.jffont`
  resources can explicitly participate in layout and paint for dynamic font
  supplement validation; the default path still uses GDI text.
- Completed the first A4 app lifecycle contract pass: `AppRuntimeHost::crash_current()`
  reuses the unified teardown rule, and the Win32 shell now binds package
  loading, scripting, timers, input and completion pumping to the active
  `app_instance_id`; app load failures release the instance and return to the
  system shell.
- Added B3/B4 platform-neutral mocks: `NetworkFetchMock` provides a
  fixture/handle/completion contract for future manifest/profile-gated runtime
  network data, and `AppPrivateKvStorageMock` provides app-id-isolated async KV
  storage semantics with budget checks.
- Added `storage.kv` capability summaries to package reports and the C++
  package manifest reader for later runtime storage gates.
- `jellyframe_cli.py package/check/preview/install` now runs font resource
  preflight by default; `--no-font-check` skips it explicitly.
- Added shared `PipelineStatistics` accounting for DOM, render, layout, layer,
  display-list, framebuffer, resource and arena usage.
- Added arena capacity and waste accounting so embedded benchmarks can separate
  live object bytes from block-allocation slack.
- Added low-cost `StyleResolver` candidate-cache statistics for entries,
  cached rule references, hits, misses and budget clears.
- Added a lightweight pipeline diagnostics sink. The HTML parser, CSS parser,
  style resolver, render tree, layout and layer tree now report budget caps,
  skipped input, ignored declarations and degraded pipeline events to desktop
  tools.
- Expanded diagnostics fallback coverage. The HTML tokenizer/tree builder now
  reports unusual tags, attributes, character references, unclosed raw text and
  unmatched end tags; script collection reports module/unknown script skips and
  external load failures; package/resource loading, inline style parsing and
  software renderer/paint fallbacks also report the triggering field.
- Added `jellyframe_pseudo_browser --diagnostics-json`, a structured desktop
  report for pipeline statistics, script status, package resource loads and
  component diagnostics.
- Added a GitHub Actions CI workflow that builds the Windows validation target,
  runs core tests, checks Python/VS Code tooling and exercises package pipeline
  diagnostics.
- Added README app-gallery screenshots rendered by the JellyFrame pseudo
  browser from the starter app templates.
- Added Host/HAL capability-profile fields for async, media, network and app
  bundles, preparing optional image/audio/lightweight-video services, runtime
  network data requests and installable bundles.
- Added an optional host-services contract that defines the V0 shape for generic
  jobs/completions, image surfaces, audio handles, lightweight video, fetch
  responses and installable-bundle registries.
- Added a `host_services` core helper module with bounded request/completion
  queues and a generation-checked host handle table for future installable apps,
  images, audio and network services.
- Added `AppLifecycleController` for active `app_instance_id` assignment,
  foreground/suspended state and request/completion/host-handle cleanup during
  app switch or exit.
- Added `AppRuntimeHost`, a bounded state container that combines app lifecycle,
  request/completion queues and the host handle table for desktop shells and MCU
  hosts wiring optional services.
- Added `.jfapp` V0 installable-bundle output. `tools/package_app.py` and
  `jellyframe_cli.py package` can now emit little-endian, uncompressed,
  fixed-index binary resource bundles and report bundle CRC/SHA-256 plus section
  sizes.
- The pseudo browser and Win32 browser shell can now load installable bundles
  directly with `--app path.jfapp`, making it possible to compare bundle output
  against source-package directories.
- Added a desktop installed-app registry mock. `jellyframe_cli.py registry
  install/list/path/remove` validates `.jfapp` bundles, installs through staging,
  atomically commits the registry and prepares the system-shell app-manager path.
- Added `samples/apps/system/sample_launcher` and modeled the Win32 App Manager
  launcher as a privileged JellyFrame App role; `--launcher-app` can point to a
  different trusted launcher app.
- Added ESP32-S3 N16R8 benchmark defaults and a 16 MB partition table, and
  recorded the 2026-06-19 real-chip baseline: 16 MB flash, 8 MB octal PSRAM and
  a passing 300x300 / 40 cards / 20 iterations full pipeline.
- Added an optional ESP32-S3 Waveshare 1.47-inch bring-up profile for
  `ESP32-S3-Touch-LCD-1.47`: Kconfig-selectable JD9853 SPI LCD initialization,
  AXS5106L touch probing, packed RGB565 dirty-rectangle flush and a 172x320
  timer resource fixture. The profile is disabled by default and remains a
  validation adapter, not a product backend.

### Changed

- CSS media query evaluation now receives the actual pseudo-browser/Win32
  viewport at page load. Recognized but non-matching `@media` blocks are
  reported as informational branch selection instead of warning-level skipped
  CSS, and responsive reports now include `paintBounds` so clipped layout boxes
  do not look like visible horizontal overflow.
- Public samples, templates and script/runtime tests now use package-local
  standard paths such as `/data/weather.json` and `/audio/tone.wav`; Win32-only
  debug fixtures stay shell internals. The old debug-only `app://...` fixture
  scheme was removed; before 1.0, JellyFrame intentionally avoids compatibility
  shims for private syntax that was never part of the documented Web subset.
- Package reports now include a stable `serviceIntent` summary for requested
  network, storage, audio and background-service manifest intent without
  implying host authorization.
- Host service workers can now pop requests by `HostServiceJobKind`, preventing
  network, storage, image and media workers from accidentally consuming each
  other's queued jobs.
- Updated HAL, host abstraction, run-loop, app-packaging and roadmap docs after
  reviewing ESP32-S3 decode experiments: MP3 and small MJPEG/image decode may
  be optional host services, H.264 is not in the default ESP32-S3 profile, and
  networking remains runtime data fetch only, not remote page-resource loading.
- App manifests/schemas now include a `role` field and reserved system
  capabilities. Declaring launcher/watchface/settings roles does not grant
  privileges; host/profile policy still owns authorization.
- Pseudo browser, pipeline dump, embedded host demo and virtual-board benchmark
  now report memory/budget-oriented pipeline statistics through the same helper.
- Render, layout and layer tree counting now use explicit work stacks instead
  of recursive helper walks.
- Software compositor users can now cap offscreen compositing pixels from
  `HostBudgets`; oversized opacity/composited layers degrade to direct
  per-command opacity instead of allocating a large temporary framebuffer.
- `SoftwareCompositor::render()` now rejects oversized primary framebuffers
  before allocation when a framebuffer pixel budget is configured.
- Microbench and virtual-board benchmark now print style candidate-cache
  statistics so computed-style sharing decisions can be based on app data.
- Retired the old text-search compatibility scanner. `jellyframe_font_resource_check`
  is now retained only for deterministic font resource work such as used-character
  collection, bitmap font budget estimates and font coverage checks.
- Renamed the former `jellyframe_capability_check` binary to
  `jellyframe_font_resource_check`; the old name remains only as historical
  wording.
- `jellyframe_cli.py package` and `check` now run pseudo-browser pipeline
  diagnostics by default. `preview` is itself a full pipeline run. Font resource
  checks still run only when font options are requested.
- CLI `check`, `preview` and `package` now merge pseudo-browser diagnostics into
  the JSON report under `pipelineDiagnostics`. Errors fail by default; warnings
  remain advisory unless `--strict` is passed.
- The VS Code helper now consumes `pipelineDiagnostics` in its report panel and
  inline diagnostics, makes `preview` write a report, and adds a command to open
  the selected package in the Win32 browser shell.
- Removed the loose `watch_calculator` fixture to avoid shipping an app that was
  intentionally close to a proprietary watch calculator design.
- ESP32-S3 resource bundle generation now runs as a build dependency instead of
  configure-time process execution, so edited HTML/CSS/JS/manifest resources
  can regenerate without forcing a full CMake reconfigure.
- Split `SoftwareCompositor` constructors to keep ESP-IDF C++ toolchains away
  from ambiguous/default aggregate option parameters while preserving the image
  painter path.
- JerryScript `XMLHttpRequest` and `Audio` constructors are now exposed only
  when the host has bound the matching network or audio adapter, so apps can
  use standard `typeof` capability checks and unsupported targets do not expose
  unusable APIs.
- Script event objects now use a lightweight event-kind tag instead of RTTI for
  mouse/wheel field projection. Base `Event("click")` remains a generic event
  and no longer risks being treated as a fake mouse event on embedded builds.
- Added stable `FrameUpdateReason`/`FrameUpdateStatistics` diagnostics and
  surfaced them in Win32 frame capture output, so full-frame fallback work can
  be attributed before adding retained rendering structures.
- `element.textContent` now updates an existing sole text child in place instead
  of replacing the child node, avoiding unnecessary `DomDirtyTree` full-frame
  planning for common timer/counter/rAF labels.
- Added the standard-shaped `element.className` reflection to the JerryScript
  DOM subset, backed by the existing `class` attribute and style/layout dirty
  path.
- Win32 animation pumping now sets only the root aggregate paint-dirty bit for
  timeline sampling instead of marking the document as a local dirty node. The
  `jelly_motion_lab` frame-capture probe now stays on dirty-rect repaint after
  the first frame instead of falling back to full-frame repaint.
- Win32 frame capture now reports attempted dirty fallback details, including
  attempted rect count, max attempted dirty area and the largest dirty node
  observed before a full-frame fallback.
- Added a Win32 motion-lab dirty-frame CTest regression that requires the
  90-frame animation soak to stay at one full frame followed by dirty-rect
  frames.

## 0.3.0-dev - 2026-06-18

### Added

- Refreshed the starter app templates and `samples/apps/packages/watch_weather` into
  modern watch-style UI fixtures, then verified their 300x300 output through
  the pseudo browser.
- Added Win32 `--app` package preview/capture parity with the pseudo browser,
  including manifest viewport loading, package-local CSS/script resources and
  fixed-viewport capture output.
- Moved the platform-neutral embedded host bring-up demo source under
  `ports/embedded_host_demo` while keeping the executable name unchanged.
- Consolidated sample resources under `samples/` and moved native C++ validation
  tools under `tools/native`, removing the mixed-purpose top-level `examples`
  directory.
- The render tree now skips pure formatting whitespace text nodes outside
  preserving contexts, which keeps indentation from polluting block/grid/flex
  layout and reduces wasted render/layout objects.
- Added support for `repeat(N, minmax(0, 1fr))` as a simplified fixed grid
  column template for common keypad/card UIs.
- Added PolyForm Noncommercial 1.0.0 licensing, a commercial-license contact
  note and README wording that clearly describes JellyFrame as noncommercial
  source-available software.
- Added README files to the public source, example, test, tool, preset, schema,
  template and port directories so cloned repositories are easier to review.
- Added the first M12 `DomOwner` prototype plus JerryScript detached-node
  statistics and budget enforcement for script-created or removed DOM nodes.
- Added platform-neutral budget stress tests and pseudo-browser script runtime
  diagnostics for timers, listeners and detached DOM node counts.
- Completed the current M10 text/font workflow scope with capability-check
  font profile recommendations for tiny, symbol-only, Chinese app subset,
  Chinese standard and global product font packs.
- Documented the ESP32-S3 audit conclusion that LVGL/vendor SDKs should
  remain optional thin panel/input/text hooks, not the primary JellyFrame
  renderer backend.
- Added the first M7.6 HTML parser compatibility batch: parser budget
  diagnostics for node/depth/attribute limits, a compact common named-entity
  table and Windows-1252 legacy numeric-reference remaps.
- Added shared render-time text normalization so DOM text keeps author
  whitespace while layout/layer output still collapses ordinary display text.
- Added first-time developer onboarding docs (`HOW_TO_START.md` /
  `HOW_TO_START_zh.md`) and a bilingual `docs/README` index that separates
  technical contracts from active project/process documents.
- Renamed the project to `JellyFrame`; `WearWeb` is now documented only as the
  early codename.
- Added platform-neutral `TextMeasureProvider` so layout can use host text
  metrics while keeping font APIs outside `jellyframe_render_core`.
- Added minimal text paint semantics to display commands: horizontal alignment
  and single-line versus wrapped text.
- Added Win32/GDI text measurement injection alongside the existing GDI text
  painter for more faithful UTF-8/Chinese desktop validation.
- Added bilingual text backend documentation covering measurement/painting
  contracts and fallback limits.
- Added font coverage support to `jellyframe_font_resource_check`: it can emit
  non-ASCII used characters and verify them against a UTF-8 font coverage file.
- Added button/crown-friendly focus navigation on `InputController`:
  `focus_next()`, `focus_previous()` and `activate_focused()`.
- Added bilingual embedded HAL API documentation for board ports, including
  ESP32-S3 mapping notes.
- Added bilingual porting work guides for ESP32-S3/RTOS/LVGL ports, covering
  phased tasks, implementation requirements, acceptance checks and boundaries
  that require core work first.
- Added a `ports/virtual_board` desktop virtual-board benchmark and normalized
  the ESP32-S3/QEMU experiment into a `ports/esp32s3-idf` bring-up project.
- Added a bounded ESP32-S3 static resource bundle hook for local HTML/CSS and
  classic-script assets, including a generated C++ table and P2 smoke resources.
- Added bilingual ESP32-S3 QEMU PSRAM gradient benchmark documentation with
  4M/8M/16M/32M timing data and memory-capacity recommendations.
- Added a platform-neutral static bitmap font backend with measurement and
  painting callbacks for generated embedded font packs.
- Added `jellyframe_font_pack_gen`, a desktop BDF subset generator that emits C++
  `BitmapFont` headers for embedded builds.
- Added `jellyframe_embedded_host_demo`, a platform-neutral static-resource demo
  that wires HTML/CSS parsing, bitmap text, focus activation and RGB565
  framebuffer presentation without Win32, files or hardware I/O.
- Added first host device capability structs for board ports, covering display,
  input, memory, budgets and optional host services.
- Added `render_core/budget.h` helpers that map `HostBudgets` into HTML/CSS parser,
  render/layout/layer/display-list, dirty-rectangle and JerryScript
  timer/listener limits.
- Replaced per-node DOM attribute `std::unordered_map` storage with a compact
  sequential `AttributeList`, reducing heap overhead for small embedded UI nodes
  while keeping the existing map-like call shape.
- Added a core `MonotonicArena` memory utility with block based linear
  allocation, reverse-order destruction and full arena reset as the base for
  future DOM/render/layout/layer lifetime allocation.
- Added an arena-backed render tree build path and exercised it from microbench,
  virtual board and ESP32-S3 benchmarks to validate document-lifetime
  allocation.
- Added an arena-backed layout tree build path, switched embedded-oriented
  benchmarks to it and covered it with core regression tests.
- Added an arena-backed layer tree build path, switched embedded-oriented
  benchmarks to it and covered it with layer-tree regression tests.
- Added a bounded `StyleResolver` candidate-rule cache for repeated id/class/tag
  patterns while preserving per-node selector matching and cascade semantics.
- Added iterative DOM subtree teardown and whole-subtree `textContent`
  replacement to reduce stack pressure on deeply nested generated documents.
- Added bilingual DOM arena feasibility notes documenting why direct DOM arena
  allocation is deferred for mutable/scripted documents.
- Added iterative `compute_dom_statistics()` instrumentation and surfaced DOM
  depth/attribute counts from pipeline diagnostics.
- Added bilingual project status and milestone documents that define the
  hardware-neutral mainline scope, completed capabilities, merged port-support
  code and the next core milestones.
- Added paint-only DOM dirty state for form-control value/checked/selection
  changes, enabling the Win32 shell to reuse render/layout and repaint bounded
  dirty rectangles for common control interaction.
- Added platform-neutral linked stylesheet collection through a callback-based
  `document_style` API. Core code still performs no file or network I/O; example
  tools and the Win32 shell provide local-file loading for validation.
- Added usable default styling for common HTML5 semantic/content elements:
  `a`, `mark`, `blockquote`, `summary`, `details`, `address`, `hgroup`,
  `progress` and `meter`.
- Added simple software painting for `progress` and `meter` value bars.
- Added `jellyframe_win32_browser --capture` to render a page through the Win32/GDI
  text path and write a BMP/PPM image for visual inspection.
- Added a lightweight platform-neutral form-control state layer for common
  embedded-app controls: text inputs, textareas, checkboxes, radios, ranges and
  selects.
- Added core input APIs for UTF-8 text input, simple key handling and stateful
  control activation.
- Added DOM mutation primitives for the JerryScript bridge: child insertion and
  removal, attribute changes, `textContent` updates and dirty flags for
  tree/attribute/text/style/layout invalidation.
- Added bilingual JerryScript integration planning documents covering runtime
  lifecycle, binding ownership, milestones, risks and the first interactive demo
  target.
- Added optional `jellyframe_script` JerryScript runtime shell, gated behind
  `JELLYFRAME_BUILD_SCRIPTING=OFF` by default so `jellyframe_render_core` remains independent
  from JerryScript headers and libraries.
- Added initial `jellyframe_pseudo_browser --script` support for scripting builds:
  it evaluates one external JavaScript file and reports the result or exception.
- Added `src/script/samples/classic/runtime_probe.*` as the first scripting
  acceptance page.
- Added M3 minimal DOM bindings for JerryScript: `window`, `document`,
  `getElementById`, `createElement`, `createTextNode`, `appendChild`,
  `removeChild`, `setAttribute`, `getAttribute` and `textContent`.
- Added `src/script/samples/classic/dom_mutation_probe.*` to validate script-driven
  DOM mutation through the pseudo browser.
- Added M4 JavaScript event bindings for `addEventListener`,
  `removeEventListener`, event objects, default prevention and propagation
  control.
- Added scripting support to the Win32 browser shell so desktop native input can
  dispatch into JavaScript listeners and rerender dirty DOM mutations.
- Added `src/script/samples/classic/event_probe.*` for interactive event bridge
  validation.
- Added M5 JavaScript form-control properties for app UI: `value`, `checked`,
  `selectedIndex` and `select.value`.
- Added app-style acceptance examples for weather, clock, timer and calculator
  under `samples/apps/loose`.
- Added bilingual embedded app subset documentation describing what can be
  built after M6 and which browser assumptions are intentionally absent.
- Added M6 host-pumped timers: `setTimeout`, `clearTimeout`, `setInterval` and
  `clearInterval`.
- Added `jellyframe_pseudo_browser --pump-timers ms` for timer-driven script smoke
  tests without an interactive window.
- Added bilingual memory management review documents covering current ownership,
  embedded risks and allocator/container priorities.
- Added a single aggregate `jellyframe_render_core_tests` executable for platform-neutral
  regression coverage, replacing the many standalone test executables in normal
  builds.
- Added `JERRYSCRIPT_ROOT` CMake support for local official JerryScript source
  trees such as `third_party/jerryscript`.
- Added a responsive grid-card layout subset for embedded apps:
  `display:grid`, `repeat(auto-fit, minmax(<length>, 1fr))`, `gap`,
  `grid-auto-rows: minmax(<length>, auto)` and `grid-column`/`grid-row:
  span N`.
- Added `aspect-ratio` sizing for visual/media boxes.
- Added cheap approximate `box-shadow` painting as rounded translucent fills.
- Added developer-facing capability matrix documentation covering supported,
  degraded, lazy and deferred HTML/CSS/DOM/script/rendering features.
- Added physical edge CSS longhands for `margin-*`, `padding-*` and
  `border-*-width`.
- Added M7 classic document script loading: scripting builds collect and execute
  inline `<script>` and local external `<script src>` through shell callbacks.
- Added `document_script` helpers for platform-neutral script collection.
- Added a first host abstraction draft and `src/render_core/host.h` with resource,
  clock, frame sink and budget structs.
- Added `src/script/samples/classic/inline_loading_probe.*` for automatic document
  script loading validation.
- Added `font-weight` parsing/inheritance and display-list propagation, with
  approximate bold rendering in the core fallback and native weight selection
  in the Win32/GDI text path.
- Added lightweight list marker support: `list-style`/`list-style-type`,
  native-lite `ul`/`ol` markers and a tiny `::before content: counter(...)`
  path for common custom ordered lists.
- Added simple fixed grid column templates such as
  `grid-template-columns: 120px 1fr` for definition lists and settings-style
  structured data.
- Added dirty-rectangle framebuffer repaint through
  `SoftwareCompositor::render_into` and `HostFrameSink` presentation helpers.
- Added `dirty_region`, the first automatic dirty-rectangle source for direct
  text, attribute and form-control mutations. Tree mutations remain
  conservatively full-viewport.
- Added the first M8 frame-update planner and bilingual run-loop contract
  documents for host input, timer, dirty-update, repaint and present ordering.
- Added `FrameLoopOptions` / `FrameLoopPendingWork` planning helpers so hosts
  can cap input-event dispatch and script-timer pumping per frame without
  handing queue ownership to the core.
- Added `FramePipelineCacheState` / `make_frame_update_state` so hosts can build
  frame-update plans from a shared cache snapshot shape without transferring
  render/layout/layer ownership to the core.
- Added second-stage frame repaint planning so hosts recheck framebuffer reuse
  after layout resolves the new content height.
- Added long-running dirty-update smoke coverage to ensure repeated paint-only
  control changes keep bounded dirty rectangles and clear dirty flags.
- Added long-running frame-loop smoke coverage for bounded input/timer backlog
  draining and clean cached idle recovery.
- Added `compute_dirty_region(...)` diagnostics with clean, dirty-rect and
  full-frame modes plus explicit fallback reasons for M9 invalidation audits.
- Added stable dirty-region mode/reason names and surfaced the latest Win32
  validation-shell dirty repaint mode in the window title.
- Added `DirtyRegionStatistics` so tests and validation shells can accumulate
  dirty-rect/full-frame counts, dirty area and fallback reason distribution.
- Added dirty-region repaint-cost helpers so hosts can compare estimated dirty
  area against the viewport and choose full-frame repaint when partial flushes
  would no longer be worthwhile.
- Added `display_invalidation` diagnostics to count how dirty rectangles cover
  layers and display commands, and surfaced command coverage in the Win32
  validation shell title.
- Added `HostTextAdapter`, a platform-neutral bridge for LVGL/vendor text
  measurement and painting callbacks.
- Added font budget summaries to `jellyframe_font_resource_check` and font-pack
  size estimates to `jellyframe_font_pack_gen`.
- Added `embedded_framebuffer`, a platform-neutral `HostFrameSink` adapter that
  converts dirty rectangles into caller-owned RGBA8888/BGRA8888, RGB565/BGR565,
  RGB332, Gray8 or 1-bit monochrome display buffers.
- Added ESP32-S3 P3 display bring-up support: an 8 MB flash partition layout,
  RGB565 packed dirty-rectangle flush callbacks, scratch-buffer row packing and
  a QEMU display smoke path that exercises full-frame and partial dirty
  presentation.
- Added ESP32-S3 P4/P5/P6 bring-up smoke support: a tiny bitmap font, a bounded
  board input queue, focus/text/control validation and dirty-rectangle RGB565
  presentation checks.
- Added embedded-app JavaScript helpers: `children`, `parentElement`,
  simple-selector `matches`/`closest`, existing-attribute `dataset` snapshots,
  a small writable `element.style` object and boolean `hidden`/`disabled`
  reflection.
- Added mouse-like `pointerdown`/`pointerup` and `touchstart`/`touchend` event
  dispatch for wearable press feedback.
- Added the early `jellyframe_capability_check` desktop HTML/CSS/JS scanner for
  supported subsets, degraded features and unsupported browser APIs. This tool
  was later retired and replaced by pipeline diagnostics; its remaining font
  work was renamed to `jellyframe_font_resource_check`.
- Added conservative modern length function support for `min()`, `max()`,
  `clamp()` and simple `calc(A +/- B)` when arguments resolve to supported
  lengths.
- Added simplified `flex-wrap` row wrapping for common card/box layouts.
- Added simplified flex row sizing for common app layouts using `flex`,
  `flex-grow`, `flex-shrink` and `flex-basis`.
- Added bounded positioned layout for common `relative`, `absolute` and `fixed`
  app overlays using simple `top`/`right`/`bottom`/`left` offsets.
- Added a bounded conditional `@media` subset for `screen`/`all` queries using
  `min-width`, `max-width`, `min-height` and `max-height` against the parser
  viewport.
- Added a small CSS custom property resolution subset for direct
  `var(--token)` and `var(--token, fallback)` values inherited along the DOM
  path.
- Added adjacent and general sibling selector matching for `+` and `~`.
- Added dynamic pseudo-class style matching for `:hover`, `:active`, `:focus`,
  `:focus-within`, `:checked` and `:disabled`, with input-state dirty
  invalidation.
- Added `:is()` and `:where()` selector-list matching with max-argument and
  zero specificity respectively.
- Added a conservative `@supports` subset for declaration feature queries,
  including `not`, homogeneous `and`/`or` chains and safe block flattening.
- Added regression coverage for linked stylesheet merging, semantic fallback
  styles, inline highlight painting, DOM mutation invalidation and form-control
  fallback behavior. Scripting builds also add JerryScript runtime lifecycle and
  exception-path coverage.

### Improved

- Expanded bitmap font regression coverage for scaling, wide punctuation,
  bold approximation and missing high-codepoint fallback glyphs.
- Changed bitmap font glyph lookup from linear scan to binary search;
  generated glyph tables must stay sorted by Unicode codepoint.
- `textarea` and `title` now use a bounded RCDATA-like tokenizer path with
  character-reference decoding; `script` and `style` remain simplified raw text.
- Non-void HTML elements with a self-closing slash now follow HTML semantics and
  stay open; true void elements still remain leaf nodes.
- Folded the HTML Living Standard degradation audit into the roadmap as an
  HTML parser/DOM compatibility short track, prioritizing low-cost app-author
  surprises while keeping quirks mode and heavy historical compatibility out of
  scope.
- Improved inline layout so runs of text, links, highlights and inline controls
  flow horizontally and wrap by available width instead of stacking every inline
  node vertically.
- Preserved parent `text-align` for inline text runs in the simplified layout
  engine.
- Shrunk inline background/border painting to child text bounds so `mark` and
  similar inline elements do not fill an entire line.
- Treated common replaced controls/media nodes as leaf render objects so
  `select` options and unsupported media fallback text do not spill into the
  page layout.
- Improved default form-control sizing and support for `border: none`, keeping
  buttons shrink-wrapped while making unstylized inputs/selects more usable.
- Painted native-lite control affordances in the display list, including range
  tracks/thumbs, checked checkbox/radio marks, select arrows and text
  value/placeholder content.
- Forwarded Win32 character and Backspace input into the core control model and
  rerendered the existing DOM so desktop validation reflects live control
  changes.
- Replaced hash-table event listener storage with compact per-type listener
  groups, reducing ordinary embedded-page listener overhead while preserving the
  public event API.
- Gave form controls an intrinsic content line height in layout so selects and
  empty inputs remain readable without author-specified heights.
- Limited script form accessors to actual form controls, reducing wrapper
  property setup on ordinary DOM nodes.
- Updated the clock and timer app cases to use M6 `setInterval` instead of
  manual-only refresh.
- Improved simplified flex row layout to honor `column-gap`.
- Improved dirty rerender paths: root dirty checks are O(1), dirty clearing
  skips clean branches, unchanged `textContent` avoids invalidation and the
  Win32 shell no longer rebuilds the pipeline after clean input callbacks.
- Improved dirty-flag and dirty-region traversal to use explicit work stacks
  and aggregate-dirty pruning, reducing stack pressure on deeply nested
  embedded documents.
- Improved dirty-region layout matching to scan previous/current layout trees
  once and aggregate dirty-node bounds, avoiding repeated full-tree searches for
  pages with multiple dirty nodes.
- Improved frame-update planning for structural DOM changes so `DomDirtyTree`
  no longer retains a previous layout tree that would only lead to a
  conservative full-frame repaint.
- Improved the Win32 browser dirty repaint path to use the shared second-stage
  repaint planner instead of duplicating layout/framebuffer size checks.
- Improved the Win32 browser dirty repaint path to fall back to full-frame
  repaint when estimated dirty rectangles exceed 70% of the framebuffer area.
- Tightened dirty-region repaint-cost helpers so a 100% threshold still rejects
  conservative estimates that exceed the viewport area.
- Avoided paint dirty invalidation for no-op form activations such as clicking
  an already checked radio button or cycling a single-option select.
- Improved core text fallback to measure and paint UTF-8 by codepoint instead
  of treating every non-ASCII byte as a separate glyph.
- Improved the bitmap font backend so missing glyphs draw a visible stable-width
  fallback box instead of silently reserving empty space.
- Improved text wrapping heuristics so single unbreakable symbols are not
  treated as multi-line text when their measured width slightly exceeds a small
  control.
- Improved grid layout so auto-width grid items are laid out against their
  assigned track width, preserving centered button text after stretch.
- Preserved explicit grid item heights and margins during grid placement.
- Updated pseudo/Win32 browser shells to use body/html background as the canvas
  clear color instead of always clearing to white.
- Updated the calculator example to use the supported grid/gap subset
  instead of relying on inline-block whitespace.
- Updated scripting and roadmap documentation to treat M7 script loading as
  available and shift the next major work toward host presentation and dirty
  rectangles.
- Updated architecture, host abstraction and compatibility planning documents
  to align next-step guidance with the hardware-neutral mainline scope.
- Fixed child-combinator selector parsing with whitespace around `>`, so rules
  such as `.list > li` no longer accidentally match deeper descendants.
- Fixed form-control state changes so text input, select, range and other
  control interactions mark DOM dirty and trigger Win32 shell rerendering.
- Improved keyboard behavior for interactive controls: datalist-backed text
  inputs can accept the first matching candidate with Tab/Enter, and selects
  can move through options across `optgroup` boundaries with Up/Down.
- Added Win32-shell hash anchor scrolling for `<a href="#id">` validation
  pages.
- Updated `jellyframe_pseudo_browser` to present through `HostFrameSink` while
  preserving BMP/PPM validation output.
- Updated the Win32 browser shell to reuse its framebuffer and repaint only
  computed dirty rectangles after non-structural DOM changes.
- Added embedded framebuffer backend documentation and updated host/roadmap
  docs to make platform text and wearable navigation the next priorities.
- Implemented `hidden` rendering semantics and disabled form-control behavior
  for pointer/text/control activation paths.

### Notes

- Superseded development reports/plans were archived outside the workspace:
  old modern/full-pipeline compatibility analyses, the completed JerryScript
  integration plan and the old embedded-app subset status note.
- `jellyframe_pseudo_browser` still uses the tiny built-in bitmap font when no
  platform `TextPainter` is injected, so non-ASCII text appears as fallback
  glyphs in BMP smoke-test output. The Win32 browser shell uses GDI text
  measurement and painting for readable UTF-8/Chinese validation.
- Local linked stylesheets are resolved by the example/Win32 helper relative to
  the CSS path passed on the command line. Missing linked files are ignored
  conservatively, matching the engine's graceful-degradation policy.
- `@container` and `object-fit` remain deferred. Container queries need bounded
  style/layout feedback handling; object-fit should wait for real image decode.

## 0.2.0-dev - 2026-06-15

### Added

- Added CPU framebuffer rendering with `FrameBuffer`, `SoftwareRasterizer` and
  `SoftwareCompositor`.
- Added source-over alpha compositing, opacity-layer offscreen compositing and
  BMP/PPM image output helpers.
- Added `jellyframe_pseudo_browser` for full-pipeline framebuffer validation.
- Added core `Event`, `MouseEvent`, `WheelEvent` and `EventTarget` support.
- Added DOM-style capture, target and bubble event dispatch with
  `preventDefault`, propagation stopping and one-shot listeners.
- Added layer-aware hit testing over layout/layer geometry, including z-index
  ordering, overflow clipping and text-node normalization to element targets.
- Added platform-neutral `InputController` for pointer move/down/up, click
  synthesis, wheel dispatch and hover/active/focus tracking.
- Added Windows-only `jellyframe_win32_browser`, an interactive validation shell
  that renders through the core pipeline, blits the framebuffer with GDI,
  injects native text painting and forwards mouse/wheel input into
  `InputController`.
- Added viewport scrolling to the Win32 browser shell. Wheel events are still
  dispatched through the core input controller before the shell performs the
  desktop default scroll action.
- Added `document_style` helpers that collect embedded `<style>` element text
  and merge it into author CSS for end-to-end tools and the Win32 shell.
- Added lightweight support for common static-page CSS: fractional lengths,
  `rem`/`em`, `max-width`, horizontal `margin: auto`, `line-height` and
  `text-indent`.
- Added regression tests for events, hit testing, input synthesis, embedded
  styles and wrapped-text layout.
- Added bilingual events/hit-testing scope documents and updated architecture,
  optimization and README documentation.

### Optimized

- Changed `EventTarget` listener storage to allocate lazily so ordinary DOM
  nodes do not carry an empty listener table.
- Split native text painting out of the core software renderer through an
  optional `TextPainter` callback. The core keeps a pure C++ bitmap fallback and
  no longer links Win32/GDI.
- Optimized opaque rectangle fill with direct row fills.
- Clipped offscreen compositing before iterating pixels.
- Hardened framebuffer resize pixel-count calculation against integer
  multiplication overflow.
- Increased wrapped-text line-height padding to avoid clipping with native
  desktop text metrics.

### Notes

- `jellyframe_render_core` remains platform-neutral. Windows libraries are linked only by
  Windows-specific example tools.
- The core text fallback is intentionally tiny and portable; the Win32 browser
  uses native GDI text for readable UTF-8/Chinese validation.

## 0.1.0-dev - 2026-06-13

### Added

- Created the initial C++17 CMake project and `jellyframe_render_core` library.
- Added tolerant HTML tokenizer/parser support with start/end tags, attributes,
  doctype, comments, text, raw-text and character references.
- Added resilient DOM construction with synthesized `html/body`, common implied
  end tags, void elements, unmatched-end-tag tolerance and parser resource
  limits.
- Added `jellyframe_dom_dump` for tokenizer output and ASCII DOM visualization.
- Added a tolerant CSS parser with comments, balanced block recovery, ordered
  declarations, selector-list splitting, `@layer` flattening and conservative
  recovery for unsupported enhancement blocks.
- Added lightweight CSSOM rule metadata, specificity, source order and cascade
  ordering.
- Added selector matching for tag, class, id, descendant, child, simple
  attribute selectors and `:root`.
- Added default style boxes for common controls and UI nodes such as forms,
  inputs, buttons, dialogs and media nodes.
- Added render tree, box-model layout, sparse layer tree and display-list
  generation.
- Added pipeline inspection tools: `jellyframe_style_dump`,
  `jellyframe_render_tree_dump`, `jellyframe_layer_tree_dump` and
  `jellyframe_pipeline_dump`.
- Added modern HTML/CSS compatibility samples and bilingual analysis documents.
- Added microbenchmarks, CTest registration and CMake options for examples,
  tests and benchmarks.
- Added bilingual documentation policy, roadmap, versioning, architecture and
  feature-scope documents.

### Optimized

- Streamed tokenizer output into DOM construction without storing a full token
  stream.
- Avoided tokenizer input copies when CR normalization is unnecessary.
- Indexed CSS rules by id/class/tag/universal buckets and precomputed selector
  parts during parsing.
- Used fixed cascade slots instead of per-node cascade hash maps.
- Kept layer creation sparse: ordinary boxes paint into their parent layer until
  clipping, stacking or compositing boundaries require a layer.
