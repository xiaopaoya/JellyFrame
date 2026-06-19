# Benchmarks

Root-level benchmarks are reserved for future cross-subproject and app-lifecycle
benchmarks.

Current microbenchmarks live next to their owning subprojects:

- `../src/render_core/benchmarks`: parser/style/render/layout/layer pipeline
  microbenchmarks.
- `../src/app_runtime/benchmarks`: request/completion queue and host-handle
  microbenchmarks.

Build them with:

```powershell
cmake -S . -B build -DJELLYFRAME_BUILD_BENCHMARKS=ON
cmake --build build --config Release
```

The generated executables are `jellyframe_render_core_microbench` and
`jellyframe_app_runtime_microbench`.
