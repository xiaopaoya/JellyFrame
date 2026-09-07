#pragma once

#include <cstddef>
#include <cstdint>

namespace jellyframe_esp32p4 {

// All PPA buffers must be DMA-capable and cache-line aligned. Prefer
// heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA).
struct AccelerationStats {
    bool ppa_ready = false;
    bool jpeg_ready = false;
    std::uint32_t ppa_fill_calls = 0;
    std::uint32_t jpeg_decode_calls = 0;
    std::uint64_t ppa_fill_us = 0;
    std::uint64_t jpeg_decode_us = 0;
};

// Uses ESP32-P4's Pixel Processing Accelerator. The whole RGB565 surface is
// filled with an opaque RGB value. It is synchronous so callers may present
// the buffer as soon as this returns true.
bool ppa_fill_rgb565(std::uint16_t* pixels,
                     int width,
                     int height,
                     std::uint8_t red,
                     std::uint8_t green,
                     std::uint8_t blue,
                     AccelerationStats* stats = nullptr);

struct JpegRgb565Surface {
    std::uint8_t* bytes = nullptr;
    std::size_t byte_count = 0;
    int width = 0;
    int height = 0;
    int stride_pixels = 0; // padded to the JPEG engine's 16-pixel MCU width
};

// Decodes a baseline JPEG through the P4 JPEG hardware into DMA-capable
// RGB565 memory. Release the result with release_jpeg_rgb565_surface().
bool decode_jpeg_rgb565(const std::uint8_t* jpeg,
                         std::size_t jpeg_size,
                         JpegRgb565Surface& output,
                         AccelerationStats* stats = nullptr);
void release_jpeg_rgb565_surface(JpegRgb565Surface& surface);

} // namespace jellyframe_esp32p4
