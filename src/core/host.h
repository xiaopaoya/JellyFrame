#pragma once

#include "core/geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wearweb {

enum class HostResourceKind {
    Stylesheet,
    ClassicScript,
    Image,
    Font,
    Other,
};

struct HostResourceRequest {
    HostResourceKind kind = HostResourceKind::Other;
    std::string_view url;
    std::string_view base_url;
};

using HostResourceLoadCallback = bool (*)(const HostResourceRequest& request,
                                          std::string& output,
                                          void* context);

struct HostClock {
    std::uint64_t (*now_ms)(void* context) = nullptr;
    void* context = nullptr;
};

struct HostFrameBufferView {
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    const Color* pixels = nullptr;
};

struct HostFrameSink {
    bool (*present)(const HostFrameBufferView& frame,
                    const Rect* dirty_rects,
                    std::size_t dirty_rect_count,
                    void* context) = nullptr;
    void* context = nullptr;
};

struct HostBudgets {
    std::size_t max_dom_nodes = 4096;
    std::size_t max_css_rules = 4096;
    std::size_t max_display_commands = 8192;
    std::size_t max_timers = 64;
    std::size_t max_event_listeners = 512;
    std::size_t max_resource_bytes = 512 * 1024;
};

enum class HostPixelFormat {
    Unknown,
    Rgba8888,
    Bgra8888,
    Rgb565,
    Bgr565,
    Rgb332,
    Gray8,
    Mono1,
};

struct HostDisplayCapabilities {
    int width = 0;
    int height = 0;
    int dpi = 0;
    HostPixelFormat preferred_pixel_format = HostPixelFormat::Unknown;
    bool supports_partial_present = true;
    bool has_full_framebuffer = true;
    bool has_touch_overlay = false;
};

struct HostInputCapabilities {
    bool pointer = false;
    bool touch = false;
    bool wheel = false;
    bool crown = false;
    bool focus_buttons = false;
    bool keyboard = false;
    bool text_input = false;
};

struct HostMemoryCapabilities {
    std::size_t total_heap_bytes = 0;
    std::size_t max_single_allocation_bytes = 0;
    std::size_t preferred_framebuffer_bytes = 0;
};

struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
    HostBudgets budgets;
    bool has_monotonic_clock = true;
    bool has_filesystem = false;
    bool has_network = false;
};

} // namespace wearweb
