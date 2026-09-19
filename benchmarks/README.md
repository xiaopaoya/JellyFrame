# Benchmarks

> Last updated: 2026-09-19; Applies to: 0.6.0-dev

Root-level benchmarks are reserved for future cross-subproject and app-lifecycle
benchmarks.

Current microbenchmarks live next to their owning subprojects:

- `../src/render_core/benchmarks`: parser/style/render/layout/layer pipeline
  microbenchmarks.
- `../src/app_runtime/benchmarks`: request/completion queue and host-handle
  microbenchmarks.

Build them with:

```powershell
cmake -S . -B build/desktop-release -DJELLYFRAME_BUILD_BENCHMARKS=ON
cmake --build build/desktop-release --config Release
```

The generated executables are `jellyframe_render_core_microbench` and
`jellyframe_app_runtime_microbench`.

Windows builds also produce `jellyframe_cpu2d_compare`. Its `opaque-fill`
workload compares the same opaque full-frame RGB fill against a memory-DIB GDI
operation and requires exact normalized RGB output. Its `horizontal-gradient`
and `vertical-gradient` workloads compare opaque gradients against the
corresponding GDI `GradientFill` mode and require normalized RGB RMSE <= 1.0.
These are 172x320 primitive probes and do
not claim whole-library, complete UI, device, rounded-gradient, text, or GPU
performance.

The same executable can optionally load an SDL2 DLL at runtime and compare the
`opaque-fill`, `opaque-dirty-fill` and `alpha-grid` workloads with
`SDL_CreateSoftwareRenderer`. The dirty workload writes a fixed 96x48 rectangle
on an initialized 172x320 black surface, reports 4,608 pixels per operation,
and batches 64 operations inside each timing sample. This does not add an SDL
dependency to JellyFrame builds or packages. The adapter deliberately rejects
gradient workloads because SDL2's software renderer has no equivalent gradient
primitive.

`alpha-grid` draws 64 non-overlapping 16x12 source-over tiles on black. Every
sample resets the surface outside the timed interval, then reports per-tile time
and 192 pixels. JellyFrame, GDI `AlphaBlend` and SDL2 software blending must
produce exact normalized RGB output.

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-fill 100 opaque-fill
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-gradient 100 horizontal-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-vertical-gradient 100 vertical-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-sdl2 100 opaque-fill sdl2 C:\path\to\SDL2.dll
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-dirty-sdl2 100 opaque-dirty-fill sdl2 C:\path\to\SDL2.dll
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-alpha-sdl2 100 alpha-grid sdl2 C:\path\to\SDL2.dll
```

The SDL2 run writes `jellyframe.json` and `sdl2.json`. Compare them with
`../tools/benchmark_compare.py`; both sides must still pass exact normalized RGB
validation before timing or throughput is comparable. The result describes only
the declared full or local opaque fill, not layout, dirty-region planning,
window present, complete UI performance or GPU rendering.

Cross-library comparisons use the fixed-condition manifest consumed by
`../tools/benchmark_compare.py`. Each adapter must provide the same workload,
viewport, pixel format, antialiasing and repaint mode before p50/p95 or MPix/s
results are considered comparable.

```json
{
  "format": "jellyframe.benchmark.run.v0",
  "library": "jellyframe",
  "version": "0.6.0-dev",
  "workload": "opaque-fill-rgb-v1",
  "viewport": {"width": 172, "height": 320},
  "pixelFormat": "rgb888",
  "antialiasing": false,
  "mode": "full",
  "environment": {"os": "windows", "architecture": "x64", "cpu": "model", "buildType": "release"},
  "warmupIterations": 30,
  "operationsPerSample": 1,
  "workloadParameters": {"surfaceInitialRgb": "000000", "rect": {"x": 0, "y": 0, "width": 172, "height": 320}, "operation": "fill-rect", "sourceRgb": "164757", "blend": "opaque-replace"},
  "outputValidation": {"status": "pass", "method": "normalized-rgb-exact", "reference": "opaque-fill-rgb-v1"},
  "measurements": {"paint_us": [4.0, 3.9, 4.1], "pixels": [55040, 55040, 55040]}
}
```

Run the comparison with:

```powershell
python tools\benchmark_compare.py --baseline build\jellyframe.json --candidate build\cairo.json --output build\comparison.json --html-output build\comparison.html
```

Only standardized metrics are accepted: `frame_us`, `cpu_us`, `paint_us`,
`present_us`, `dma_wait_us`, `pixels`, `dirty_pixels`, and `peak_bytes`.
Throughput is derived only when paired pixel and positive-duration samples have
the same cardinality; same-name metrics with different sample counts are
individually marked `not-comparable`. Both sides must pass the same
output-validation method, reference and tolerance; otherwise every metric is
marked `not-comparable`. `operationsPerSample` defaults to 1 for old manifests,
must be a positive integer, and must match between both sides. New adapters
should also emit the bounded `workloadParameters` object. Geometry, colors,
blend, gradient axis and endpoint convention must match exactly; a legacy
manifest without parameters is comparable only with another parameter-less
manifest.
