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

} // namespace wearweb
