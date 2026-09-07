# JellyFrame ESP32-P4 Port (ESP-IDF 5.5.5)

This is an independent ESP-IDF bring-up application for ESP32-P4.  It builds
the existing Render Core without changing its source, then validates the P4
Pixel Processing Accelerator (PPA) and JPEG decoder before a display adapter
is attached.  It is designed for the WT9932P4-TINY on `COM22`.

> **IDF version gate:** the connected WT9932P4-TINY reports ESP32-P4 revision
> `v1.3`. IDF 5.3.1 can compile this project but rejects the bootloader during
> flash. The validated environment is ESP-IDF `v5.5.5` with the v1.x target
> selection below; it builds and flashes the board without `esptool --force`.

## Build and board smoke test

The validated build uses the IDF 5.5.5 tree (the `idf.py` command is not
globally added to the current PowerShell session):

```powershell
Set-Location C:\Users\Administrator\Documents\New project\ports\esp32p4-idf
. C:\esp-idf-v5.5.5\export.ps1
$env:IDF_SKIP_CHECK_SUBMODULES='1'
idf.py -B build-wt9932p4-v555-clean -D "SDKCONFIG=build-wt9932p4-v555-clean/sdkconfig" -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults" build
```

After upgrading IDF, flash with:

```powershell
idf.py -B build-wt9932p4-v555-clean -p COM22 flash monitor
```

The initial serial acceptance line is `render_core=ok`, followed by a
successful `PPA RGB565 fill=ok` and `JPEG decoder=ready`. Neither check
requires panel GPIO or a panel power sequence, so it is safe to run before the
WT9932P4-TINY schematic/BSP is integrated.

## P4 hardware allocation policy

`main/jellyframe_esp32p4_accel.*` is the only P4-specific accelerator layer:

| Hardware | Current port use | Buffer/ownership rule |
| --- | --- | --- |
| PPA / 2D-DMA | Hardware RGB565 clear; reusable wrapper for scaling, rotation and blend work | DMA-capable, 64-byte aligned buffers; submit from the UI/compositor owner only. |
| JPEG decoder | RGB565 decoder wrapper, ready for image resource loading | Compressed input must stay unchanged during decode. Output is hardware-allocated, MCU-padded and released with `release_jpeg_rgb565_surface`. |
| MIPI-DSI / LCD | Deferred until the WT9932P4-TINY panel timing and pin mapping are supplied | Display flush must keep a frame or DMA strip alive until the panel transfer completion callback. |
| H.264/video | Not enabled | ESP-IDF 5.5.5 exposes JPEG/PPA drivers, but this port still has no board-approved public P4 H.264/video decoder component. Do not claim H.264 capability in `HostMediaCapabilities` on this SDK. |

PPA should be used for large opaque fills, RGB565/RGB888 scale-rotate-mirror
operations and ARGB/RGB blend operations.  Do not send individual glyphs,
tiny rounded clips, or CPU-owned `FrameBuffer::pixel()` writes to PPA: the DMA
setup and cache synchronization cost dominates those operations.  A good
threshold is a dirty rectangle of at least 64 x 64 pixels, measured again on
the actual panel configuration.

## Interfaces still required from the project

The core intentionally exposes only `ImageHandleResolver` and `ImagePainter`.
That is sufficient for the current synchronous BMP adapter, but does not
provide an image-surface lifetime or async completion path.  To wire JPEG
hardware into ordinary `<img>` and CSS background resources without changing
the renderer's subject matter, the project needs a port-facing image contract
with all of these fields:

```cpp
struct DecodedImageSurface {
    uint32_t handle;
    const void* pixels;          // immutable while frame is presented
    int width, height, stride_pixels;
    HostPixelFormat format;      // RGB565 for P4 JPEG output
    void (*release)(uint32_t handle, void* context);
    void* context;
};

// Request is queued off the UI task. Completion marks only affected image
// owners dirty; it must not rebuild the full document.
bool request_image_decode(HostResourceRequest, uint32_t request_id, void* context);
bool take_image_decode_completion(uint32_t request_id, DecodedImageSurface* out, void* context);
```

The painter then needs either a read-only RGB565 sampling path or an optional
blit callback that receives source/destination rectangles and the object-fit
mapping.  It must retain the `DecodedImageSurface` until the frame sink reports
that its final DMA transfer is complete.  This lets a port choose CPU sampling
for small images and PPA SRM/blend for large ones without a Render Core change.

For video, add a separate `VideoFrameSurface` lease contract (format,
dimensions, stride(s), PTS, release callback), a bounded decode request queue,
and a completion-to-dirty-owner mapping.  Only after a board-approved decoder
component is present should the P4 port set `supports_video_decode`,
`supports_mjpeg` or `supports_h264`; with the supplied IDF 5.5.5 all must stay
false.  MJPEG can still be implemented port-side by issuing bounded JPEG decode
requests through the image-surface contract. With the supplied IDF 5.5.5,
`supports_video_decode`, `supports_mjpeg` and `supports_h264` remain false.

## WT9932P4-TINY board data needed for display integration

Please provide the vendor schematic/BSP or confirm these details before panel
code is added:

1. LCD interface type (MIPI-DSI/RGB/SPI), native resolution, color order and
   refresh target.
2. DSI lane count/bit rate or RGB data/control pin assignment and pixel clock;
   panel reset, enable and backlight GPIO polarity.
3. Touch-controller bus, address, interrupt/reset GPIOs and orientation.
4. PSRAM size/mode and whether the display controller/DMA can read it directly.
5. Whether a panel framebuffer must be RGB565, RGB888 or ARGB8888, plus any
   cache-alignment restriction imposed by its driver.

Those values belong in a new `boards/wt9932p4_tiny.*` adapter under this port,
not in `src/`.
