# Benchmarks

This directory contains desktop microbenchmarks for the platform-neutral engine
core. They are not product code and are not required by embedded ports.

Build them with:

```powershell
cmake -S . -B build -DJELLYFRAME_BUILD_BENCHMARKS=ON
cmake --build build --config Release
```

Current benchmarks focus on parser, style, render/layout/layer and framebuffer
paths that are sensitive on small devices.
