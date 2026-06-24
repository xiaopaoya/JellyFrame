#pragma once

#include "render_core/geometry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace jellyframe {

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
    // Presentation is a frame-lifetime boundary. When this callback returns
    // true, the caller may render into or convert from the same framebuffer
    // again. Hosts using asynchronous DMA must either block/wait here, copy
    // into a DMA-owned buffer, or keep the UI frame loop from starting the next
    // render until the panel driver reports completion.
    bool (*present)(const HostFrameBufferView& frame,
                    const Rect* dirty_rects,
                    std::size_t dirty_rect_count,
                    void* context) = nullptr;
    void* context = nullptr;
};

struct HostBudgets {
    std::size_t max_dom_nodes = 4096;
    std::size_t max_dom_depth = 64;
    std::size_t max_attributes_per_element = 64;
    std::size_t max_css_rules = 4096;
    std::size_t max_css_declarations_per_rule = 256;
    std::size_t max_render_objects = 4096;
    std::size_t max_layout_boxes = 4096;
    std::size_t max_layers = 1024;
    std::size_t max_display_commands = 8192;
    std::size_t max_dirty_rects = 8;
    std::size_t max_timers = 64;
    std::size_t max_detached_dom_nodes = 256;
    std::size_t max_input_events_per_frame = 8;
    std::size_t max_timer_callbacks_per_frame = 8;
    std::size_t max_animation_callbacks_per_frame = 4;
    std::size_t max_active_animations = 16;
    std::size_t animation_frame_rate = 30;
    std::size_t max_event_listeners = 512;
    std::size_t max_script_execution_checks = 2048;
    std::size_t script_execution_check_interval = 16;
    std::size_t max_resource_bytes = 512 * 1024;
    std::size_t max_framebuffer_pixels = 480 * 480;
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

struct HostAsyncCapabilities {
    bool runs_jobs_off_ui_thread = false;
    bool supports_cancel = false;
    std::size_t max_in_flight_jobs = 0;
    std::size_t max_completion_events_per_frame = 0;
};

struct HostMediaCapabilities {
    bool supports_image_decode = false;
    bool supports_audio_playback = false;
    bool supports_video_decode = false;
    bool supports_mjpeg = false;
    bool supports_h264 = false;
    bool supports_mp3 = false;
    HostPixelFormat preferred_decoded_image_format = HostPixelFormat::Unknown;
    HostPixelFormat preferred_video_frame_format = HostPixelFormat::Unknown;
    int max_image_width = 0;
    int max_image_height = 0;
    int max_video_width = 0;
    int max_video_height = 0;
    int max_video_fps = 0;
    std::size_t max_decoded_image_bytes = 0;
    std::size_t max_video_frame_bytes = 0;
    std::size_t max_audio_streams = 0;
};

struct HostNetworkCapabilities {
    bool supports_fetch = false;
    bool allows_remote_page_resources = false;
    std::size_t max_in_flight_requests = 0;
    std::size_t max_request_bytes = 0;
    std::size_t max_response_bytes = 0;
};

struct HostSensorCapabilities {
    bool supports_accelerometer = false;
    bool supports_gyroscope = false;
    bool supports_heart_rate = false;
    bool supports_ambient_light = false;
    std::size_t max_sensor_sample_records = 0;
};

struct HostLocationCapabilities {
    bool supports_position = false;
    std::size_t max_location_snapshot_records = 0;
};

struct HostAppBundleCapabilities {
    bool supports_installable_bundles = false;
    bool supports_atomic_install = false;
    bool supports_integrity_check = false;
    std::size_t max_bundle_bytes = 0;
    std::size_t max_installed_apps = 0;
};

struct HostDeviceCapabilities {
    HostDisplayCapabilities display;
    HostInputCapabilities input;
    HostMemoryCapabilities memory;
    HostAsyncCapabilities async;
    HostMediaCapabilities media;
    HostNetworkCapabilities network;
    HostSensorCapabilities sensors;
    HostLocationCapabilities location;
    HostAppBundleCapabilities app_bundles;
    HostBudgets budgets;
    bool has_monotonic_clock = true;
    bool has_filesystem = false;
    bool has_network = false;
};

} // namespace jellyframe
