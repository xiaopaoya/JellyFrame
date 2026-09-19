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
`opaque-fill` workload with `SDL_CreateSoftwareRenderer`. This does not add an
SDL dependency to JellyFrame builds or packages. The adapter deliberately
rejects gradient workloads because SDL2's software renderer has no equivalent
gradient primitive.

```powershell
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-fill 100 opaque-fill
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-gradient 100 horizontal-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-vertical-gradient 100 vertical-gradient
build\desktop-release\Release\jellyframe_cpu2d_compare.exe build\cpu2d-sdl2 100 opaque-fill sdl2 C:\path\to\SDL2.dll
```

The SDL2 run writes `jellyframe.json` and `sdl2.json`. Compare them with
`../tools/benchmark_compare.py`; both sides must still pass exact normalized RGB
validation before timing or throughput is comparable. The result describes only
this 172x320 full-surface fill, not complete UI performance or GPU rendering.

Cross-library comparisons use the fixed-condition manifest consumed by
`../tools/benchmark_compare.py`. Each adapter must provide the same workload,
viewport, pixel format, antialiasing and repaint mode before p50/p95 or MPix/s
results are considered comparable.

```json
{
  "format": "jellyframe.benchmark.run.v0",
  "library": "jellyframe",
  "version": "0.6.0-dev",
  "workload": "opaque-fill-v1",
  "viewport": {"width": 172, "height": 320},
  "pixelFormat": "rgba8888",
  "antialiasing": false,
  "mode": "full",
  "environment": {"os": "windows", "architecture": "x64", "cpu": "model", "buildType": "release"},
  "warmupIterations": 30,
  "outputValidation": {"status": "pass", "method": "rgba-sha256", "reference": "opaque-fill-v1"},
  "measurements": {"frame_us": [120, 118, 121], "pixels": [55040, 55040, 55040]}
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
marked `not-comparable`.
