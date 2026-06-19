# Embedded Framebuffer Backend


`src/render_core/embedded_framebuffer.h` provides the first deployable presentation
adapter for embedded hosts. It is still platform-neutral: it does not open a
device, allocate display memory, start DMA, talk to LVGL or call an RTOS API.
It converts JellyFrame's internal RGBA `FrameBuffer` view into a caller-owned
display buffer and reports the same dirty rectangles back to the host.

## Supported Formats

| Format | Storage | Intended Use |
| --- | --- | --- |
| `Rgba8888` | 4 bytes per pixel, RGBA order | Validation or 32-bit display buffers. |
| `Bgra8888` | 4 bytes per pixel, BGRA order | Hosts with BGRA scanout. |
| `Rgb565` | 16-bit little-endian RGB 5:6:5 | Common MCU/TFT panel format. |
| `Bgr565` | 16-bit little-endian BGR 5:6:5 | Panels wired with swapped red/blue fields. |
| `Rgb332` | 8-bit RGB 3:3:2 | Very small color buffers. |
| `Gray8` | 8-bit luminance | Grayscale panels or debug capture. |
| `Mono1Msb` | 1 bit per pixel, leftmost pixel in bit 7 | Common monochrome controller packing. |
| `Mono1Lsb` | 1 bit per pixel, leftmost pixel in bit 0 | Alternate monochrome packing. |

Alpha is flattened before conversion. In normal rendering the source
framebuffer is already composited over an opaque background, so this is mostly
a safety path.

## Host Contract

The host owns:

- the target byte buffer;
- the display controller or graphics library;
- any double buffering or DMA lifetime rules;
- the optional flush callback that submits each dirty rectangle.

The core owns only conversion:

```cpp
std::vector<std::uint8_t> display(
    embedded_framebuffer_min_size(width, height, EmbeddedPixelFormat::Rgb565));

EmbeddedFrameBufferSink embedded{
    EmbeddedFrameBufferTarget{width,
                              height,
                              EmbeddedPixelFormat::Rgb565,
                              display.data(),
                              display.size(),
                              0},
    flush_rect_to_panel,
    panel_context};

HostFrameSink sink = embedded_frame_sink(embedded);
present_frame(frame_buffer, sink, dirty_rects, dirty_rect_count);
```

If `dirty_rects` is empty, the full frame is converted. Otherwise only clipped
dirty rectangles are converted and flushed. Pixels outside those rectangles are
left untouched.

## Limits

- Source and target dimensions must match exactly.
- The adapter does not allocate or retain memory.
- The adapter does not coalesce dirty rectangles; upstream `dirty_region`
  chooses the rectangle list.
- The adapter does not scroll hardware layers or perform partial framebuffer
  address-window setup; the host does that in `flush`.
- Text quality still depends on the renderer's `TextPainter`, not this adapter.

## Why This Shape

Small devices differ wildly: some expose a linear framebuffer, some require
SPI address-window writes, some use LVGL draw buffers, some need RGB565, and
some are monochrome. A plain byte-buffer converter plus a rectangle callback
keeps the core reusable while still making the important embedded rule explicit:
do not repaint or flush more pixels than the dirty region requires.
