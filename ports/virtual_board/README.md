# JellyFrame Virtual Board Benchmark

This host-side benchmark is a fast stand-in for early ESP32-S3 bring-up when a
QEMU or physical board is not available. It runs the real JellyFrame core pipeline,
renders a framebuffer, converts it through the embedded RGB565 adapter, and then
adds a configurable display bus cost model.
It also applies the same embedded-oriented `HostBudgets` profile used by the
ESP32-S3 benchmark app.

Default profile:

- viewport: 300x300
- panel: RGB565 SPI TFT style flush
- bus: 40 MHz, 85% effective payload efficiency
- per-flush overhead: 40 us
- benchmark content: synthetic card UI

Build from the repository root:

```powershell
cmake -S . -B build-virtual -DJELLYFRAME_BUILD_SCRIPTING=OFF
cmake --build build-virtual --config Release --target jellyframe_virtual_bench
```

Run:

```powershell
.\build-virtual\Release\jellyframe_virtual_bench.exe
```

Arguments:

```text
jellyframe_virtual_bench [width height cards iterations bus_mhz efficiency overhead_us]
```

Example:

```powershell
.\build-virtual\Release\jellyframe_virtual_bench.exe 300 300 60 200 80 0.85 40
```

Interpretation:

- `*_cpu_avg_us` values are measured on the host CPU.
- `virtual_flush_avg_us` estimates panel transfer time for the dirty rectangles.
- `steady_frame_estimate_us` is `render_frame + present_rgb565 + virtual_flush`.
- `cold_pipeline_frame_estimate_us` additionally includes full parse/style/layout
  pipeline work.
- `present_pixels`, `present_packed_bytes`, `present_clipped_rects` and
  `present_flushes` come from `EmbeddedFrameBufferPresentStats`; compare them
  with board-side panel bytes, DMA wait time and flush-done counts.

Use the estimates for trend comparison and display bandwidth planning, not as a
cycle-accurate ESP32-S3 replacement.
