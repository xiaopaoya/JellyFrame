#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "app_runtime/app_host.h"
#include "app_runtime/app_frame_policy.h"
#include "app_runtime/app_service_worker.h"
#include "app_runtime/app_services.h"
#include "app_runtime/system_events.h"
#include "render_core/animation_invalidation.h"
#include "render_core/animation_timeline.h"
#include "render_core/budget.h"
#include "render_core/css_parser.h"
#include "render_core/display_invalidation.h"
#include "render_core/diagnostics.h"
#include "render_core/dirty_region.h"
#include "render_core/document_script.h"
#include "render_core/document_style.h"
#include "render_core/frame_scratch.h"
#include "render_core/frame_update.h"
#include "render_core/html_parser.h"
#include "render_core/input.h"
#include "render_core/layer_tree.h"
#include "render_core/layout.h"
#include "render_core/render_tree.h"
#include "render_core/software_renderer.h"
#include "render_core/style.h"

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
#include "script/jerryscript_runtime.h"
#endif

#include "app_registry.h"
#include "app_package.h"
#include "example_css_io.h"

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace jellyframe;

namespace {

constexpr std::size_t kMaxInputBytes = 1024 * 1024;
constexpr wchar_t kWindowClassName[] = L"JellyFrameWin32Browser";
constexpr UINT_PTR kScriptTimerId = 1;
constexpr UINT kScriptTimerPeriodMs = 16;
constexpr int kIncrementalDirtyAreaLimitPercent = 70;
constexpr const char* kDefaultLauncherAppPath = "samples/apps/system/sample_launcher";
constexpr const char* kLauncherStatusMarker = "<!-- JELLYFRAME_STATUS -->";
constexpr const char* kLauncherAppListMarker = "<!-- JELLYFRAME_APP_LIST -->";
constexpr int kMaxDebugPackageImageWidth = 256;
constexpr int kMaxDebugPackageImageHeight = 256;
constexpr const wchar_t* kWin32AudioAlias = L"jellyframe_audio_smoke";
constexpr std::size_t kMaxDebugPackageImageDecodedBytes =
    static_cast<std::size_t>(kMaxDebugPackageImageWidth) * kMaxDebugPackageImageHeight * 4U;

InteractionInvalidationOptions input_invalidation_options_from_style(const StyleResolver& resolver) {
    const InteractionInvalidationHints hints = resolver.interaction_invalidation_hints();
    InteractionInvalidationOptions options;
    options.hover_style = hints.hover;
    options.active_style = hints.active;
    options.focus_style = hints.focus;
    return options;
}

std::uint16_t pack_rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void append_rgb565(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value & 0xffU));
    output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

std::vector<std::uint8_t> make_debug_rgb565_surface(int width, int height, Color top, Color bottom) {
    std::vector<std::uint8_t> pixels;
    if (width <= 0 || height <= 0) {
        return pixels;
    }
    pixels.reserve(static_cast<std::size_t>(width * height * 2));
    const int denom = std::max(1, height - 1);
    for (int y = 0; y < height; ++y) {
        const int t = (y * 255) / denom;
        const auto lerp = [&](std::uint8_t a, std::uint8_t b) {
            return static_cast<std::uint8_t>((static_cast<int>(a) * (255 - t) + static_cast<int>(b) * t + 127) / 255);
        };
        const std::uint16_t color = pack_rgb565(lerp(top.r, bottom.r), lerp(top.g, bottom.g), lerp(top.b, bottom.b));
        for (int x = 0; x < width; ++x) {
            append_rgb565(pixels, color);
        }
    }
    return pixels;
}

void add_debug_image_fixtures(ImageDecodeMock& images) {
    images.add_fixture(ImageDecodeFixture{
        "/debug/icon.raw",
        48,
        48,
        48,
        HostPixelFormat::Rgb565,
        make_debug_rgb565_surface(48, 48, Color{37, 99, 235, 255}, Color{14, 165, 233, 255}),
    });
    images.add_fixture(ImageDecodeFixture{
        "/debug/photo.raw",
        120,
        80,
        120,
        HostPixelFormat::Rgb565,
        make_debug_rgb565_surface(120, 80, Color{251, 191, 36, 255}, Color{244, 63, 94, 255}),
    });
}

std::int32_t read_i32_le(const std::uint8_t* data) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8U) |
        (static_cast<std::uint32_t>(data[2]) << 16U) |
        (static_cast<std::uint32_t>(data[3]) << 24U));
}

bool decode_bmp_to_fixture(const std::string& url,
                           const std::string& bytes,
                           ImageDecodeFixture& fixture,
                           DiagnosticSink* diagnostics) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const std::size_t size = bytes.size();
    if (size < 54 || data[0] != 'B' || data[1] != 'M') {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-unsupported",
                          "Package image is not a supported BMP file",
                          url);
        return false;
    }
    const std::uint32_t pixel_offset = jellyframe_example::read_le32(data + 10);
    const std::uint32_t dib_size = jellyframe_example::read_le32(data + 14);
    if (dib_size < 40 || size < 14U + dib_size || pixel_offset >= size) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-invalid",
                          "BMP header is truncated or invalid",
                          url);
        return false;
    }
    const std::int32_t width = read_i32_le(data + 18);
    const std::int32_t signed_height = read_i32_le(data + 22);
    const std::uint16_t planes = jellyframe_example::read_le16(data + 26);
    const std::uint16_t bits_per_pixel = jellyframe_example::read_le16(data + 28);
    const std::uint32_t compression = jellyframe_example::read_le32(data + 30);
    if (width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min() ||
        planes != 1 || compression != 0 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32)) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-unsupported",
                          "Only uncompressed 24-bit and 32-bit BMP package images are supported in the Win32 debug shell",
                          url);
        return false;
    }
    const int height = std::abs(signed_height);
    if (width > kMaxDebugPackageImageWidth || height > kMaxDebugPackageImageHeight) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP exceeds the Win32 debug shell image budget",
                          url);
        return false;
    }
    const std::size_t decoded_bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    if (decoded_bytes > kMaxDebugPackageImageDecodedBytes) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-budget",
                          "Package BMP decoded bytes exceed the Win32 debug shell image budget",
                          url);
        return false;
    }
    const bool top_down = signed_height < 0;
    const std::size_t bytes_per_pixel = bits_per_pixel / 8;
    const std::size_t row_stride = ((static_cast<std::size_t>(width) * bits_per_pixel + 31U) / 32U) * 4U;
    if (!jellyframe_example::byte_range_is_valid(size, pixel_offset, row_stride * static_cast<std::size_t>(height))) {
        report_diagnostic(diagnostics,
                          DiagnosticStage::Package,
                          DiagnosticSeverity::Warning,
                          "image-decode-invalid",
                          "BMP pixel data is truncated",
                          url);
        return false;
    }

    std::vector<std::uint8_t> pixels;
    pixels.resize(decoded_bytes);
    for (int y = 0; y < height; ++y) {
        const int source_y = top_down ? y : height - 1 - y;
        const std::uint8_t* source_row = data + pixel_offset + static_cast<std::size_t>(source_y) * row_stride;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* source = source_row + static_cast<std::size_t>(x) * bytes_per_pixel;
            const std::size_t output_index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                             static_cast<std::size_t>(x)) * 4U;
            pixels[output_index] = source[2];
            pixels[output_index + 1] = source[1];
            pixels[output_index + 2] = source[0];
            pixels[output_index + 3] = bits_per_pixel == 32 ? source[3] : 255;
        }
    }

    fixture = ImageDecodeFixture{
        url,
        width,
        height,
        width,
        HostPixelFormat::Rgba8888,
        std::move(pixels),
    };
    return true;
}

void collect_image_sources(const Node& node, std::vector<std::string>& sources) {
    if (node.type == NodeType::Element && node.tag_name == "img") {
        const std::string& src = node.attribute("src");
        if (!src.empty() && std::find(sources.begin(), sources.end(), src) == sources.end()) {
            sources.push_back(src);
        }
    }
    for (const auto& child : node.children) {
        collect_image_sources(*child, sources);
    }
}

void add_package_image_fixtures(const Node& document,
                                jellyframe_example::PackageResourceContext& package_context,
                                ImageDecodeMock& images,
                                DiagnosticSink* diagnostics) {
    std::vector<std::string> sources;
    collect_image_sources(document, sources);
    for (const std::string& source : sources) {
        std::string bytes;
        if (!jellyframe_example::load_package_resource(source, package_context.base_url, bytes, &package_context)) {
            continue;
        }
        ImageDecodeFixture fixture;
        if (decode_bmp_to_fixture(source, bytes, fixture, diagnostics)) {
            images.add_fixture(std::move(fixture));
        }
    }
}

void collect_image_handles(const LayerNode& layer, std::vector<std::uint32_t>& handles) {
    for (const DisplayCommand& command : layer.display_list) {
        if (command.type != DisplayCommandType::Image || command.image_handle == 0) {
            continue;
        }
        if (std::find(handles.begin(), handles.end(), command.image_handle) == handles.end()) {
            handles.push_back(command.image_handle);
        }
    }
    for (const auto& child : layer.children) {
        collect_image_handles(*child, handles);
    }
}

void report_image_request_failure(DiagnosticSink* diagnostics,
                                  const std::string& src,
                                  AppServiceSubmitStatus submit_status,
                                  HostServiceStatus host_status) {
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Warning,
                      "image-decode-request",
                      "Image decode request was rejected",
                      app_image_failure_detail(src, submit_status, host_status, 0));
}

void report_image_completion_failure(DiagnosticSink* diagnostics,
                                     const HostServiceCompletion& completion,
                                     const std::string& src) {
    if (completion.kind != HostServiceJobKind::ImageDecode ||
        completion.status == HostServiceStatus::Completed) {
        return;
    }
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Warning,
                      "image-decode-completion",
                      "Image decode did not produce a drawable surface",
                      app_image_failure_detail(src,
                                               AppServiceSubmitStatus::Accepted,
                                               completion.status,
                                               completion.error_code));
}

void report_image_cache_state(DiagnosticSink* diagnostics,
                              const AppImageSurfaceCache& cache,
                              const std::string& src) {
    if (src.empty()) {
        return;
    }
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Info,
                      "image-cache-state",
                      "Image cache state after decode",
                      cache.diagnostic_detail_for_url(src));
}

void report_image_eviction_result(DiagnosticSink* diagnostics,
                                  const AppImageSurfaceEvictionResult& result) {
    if (result.dropped_stale_entries == 0) {
        return;
    }
    report_diagnostic(diagnostics,
                      DiagnosticStage::Package,
                      DiagnosticSeverity::Warning,
                      "image-cache-stale-entry",
                      "Image cache dropped stale surface entries during eviction",
                      "released=" + std::to_string(result.released_surfaces) +
                          "; dropped_stale=" + std::to_string(result.dropped_stale_entries));
}

enum class ScriptedFrameEventKind {
    NetworkOnline,
    NetworkOffline,
    ScreenVisible,
    ScreenHidden,
    LowPowerOn,
    LowPowerOff,
    PointerMove,
    PointerDown,
    PointerUp,
    Click,
};

struct ScriptedFrameEvent {
    int frame_index = 0;
    ScriptedFrameEventKind kind = ScriptedFrameEventKind::PointerMove;
    int x = 0;
    int y = 0;
};

struct ParsedFrameEvent {
    bool ok = false;
    ScriptedFrameEvent event;
    std::string error;
};

struct BrowserOptions {
    bool capture = false;
    bool capture_frames = false;
    std::string output_path;
    std::string frame_output_dir;
    std::string html_path = "src/render_core/samples/pages/modern/app_shell.html";
    std::string css_path = "src/render_core/samples/pages/modern/app_shell.css";
    std::string inline_html;
    std::string inline_css;
    std::string script_path;
    std::string app_path;
    std::string registry_store_path;
    std::string launcher_app_path = kDefaultLauncherAppPath;
    std::string install_bundle_path;
    std::string launch_app_id;
    std::string remove_app_id;
    std::string audio_smoke_source;
    int audio_smoke_ms = 1000;
    std::string startup_status;
    int viewport_width = 390;
    int viewport_height = 640;
    bool viewport_width_set = false;
    bool viewport_height_set = false;
    bool use_app_fonts = false;
    int frame_count = 30;
    std::uint32_t frame_step_ms = 33;
    std::uint64_t frame_start_ms = 1000;
    std::string frame_script_path;
    std::string frame_montage_path;
    int frame_montage_columns = 0;
    int frame_montage_gap = 6;
    int animation_frame_rate = -1;
    int animation_callbacks_per_frame = -1;
    std::vector<ScriptedFrameEvent> frame_events;
};

struct HostServiceDebugCounters {
    std::size_t completion_batches = 0;
    std::size_t completions_consumed = 0;
    std::size_t completions_accepted = 0;
    std::size_t completions_stale = 0;
    std::size_t released_stale_handles = 0;
    std::size_t network_completions = 0;
    std::size_t storage_completions = 0;
    std::size_t image_completions = 0;
    std::size_t audio_completions = 0;
    std::size_t other_completions = 0;
    std::size_t image_handled = 0;
    std::size_t script_host_completions_handled = 0;
    std::size_t system_event_batches = 0;
    std::size_t system_events_consumed = 0;
    std::size_t system_events_accepted = 0;
    std::size_t system_events_stale = 0;
    std::size_t script_system_events_handled = 0;
};

struct FramePolicyDebugCounters {
    std::size_t sampled_frames = 0;
    std::size_t accepts_input_frames = 0;
    std::size_t timer_frames = 0;
    std::size_t animation_frames = 0;
    std::size_t present_frames = 0;
    std::size_t network_active_frames = 0;
    std::size_t audio_active_frames = 0;
    std::size_t sensor_active_frames = 0;
    std::size_t pause_audio_frames = 0;
    std::size_t throttle_sensor_frames = 0;
};

class NetworkFetchMockWorker final : public AppHostServiceWorker {
public:
    NetworkFetchMockWorker(AppRuntimeHost& host, NetworkFetchMock& network)
        : host_(host),
          network_(network) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        return network_.complete_request(host_, request);
    }

private:
    AppRuntimeHost& host_;
    NetworkFetchMock& network_;
};

class ImageDecodeMockWorker final : public AppHostServiceWorker {
public:
    ImageDecodeMockWorker(AppRuntimeHost& host, ImageDecodeMock& images)
        : host_(host),
          images_(images) {}

    HostServiceCompletion process(const HostServiceRequest& request) override {
        return images_.complete_request(host_, request);
    }

private:
    AppRuntimeHost& host_;
    ImageDecodeMock& images_;
};

void count_host_completion_kind(HostServiceDebugCounters& counters, HostServiceJobKind kind) {
    switch (kind) {
    case HostServiceJobKind::NetworkFetch:
        ++counters.network_completions;
        break;
    case HostServiceJobKind::StorageKv:
        ++counters.storage_completions;
        break;
    case HostServiceJobKind::ImageDecode:
        ++counters.image_completions;
        break;
    case HostServiceJobKind::AudioCommand:
        ++counters.audio_completions;
        break;
    case HostServiceJobKind::Other:
        ++counters.other_completions;
        break;
    }
}

AppBackgroundServicePolicy background_policy_from_manifest(const jellyframe_example::AppPackageManifest& manifest) {
    AppBackgroundServicePolicy policy;
    policy.network_while_suspended = manifest.background_network_while_suspended;
    policy.network_while_screen_off = manifest.background_network_while_screen_off;
    policy.audio_while_suspended = manifest.background_audio_while_suspended;
    policy.audio_while_screen_off = manifest.background_audio_while_screen_off;
    policy.sensors_while_suspended = manifest.background_sensors_while_suspended;
    policy.sensors_while_screen_off = manifest.background_sensors_while_screen_off;
    policy.sensors_in_low_power = manifest.background_sensors_in_low_power;
    return policy;
}

struct LoadedPage {
    std::unique_ptr<Node> document;
    Stylesheet stylesheet;
    std::filesystem::path script_base_dir;
    jellyframe_example::PackageResourceContext package_context;
    jellyframe_example::PackageResourceStats package_stats;
    jellyframe_example::AppPackageManifest package_manifest;
    bool package_mode = false;

    LoadedPage() = default;
    LoadedPage(const LoadedPage&) = delete;
    LoadedPage& operator=(const LoadedPage&) = delete;

    LoadedPage(LoadedPage&& other) noexcept
        : document(std::move(other.document)),
          stylesheet(std::move(other.stylesheet)),
          script_base_dir(std::move(other.script_base_dir)),
          package_context(std::move(other.package_context)),
          package_stats(other.package_stats),
          package_manifest(std::move(other.package_manifest)),
          package_mode(other.package_mode) {
        if (package_context.stats == &other.package_stats) {
            package_context.stats = &package_stats;
        }
        other.package_context.stats = nullptr;
    }

    LoadedPage& operator=(LoadedPage&& other) noexcept {
        if (this != &other) {
            document = std::move(other.document);
            stylesheet = std::move(other.stylesheet);
            script_base_dir = std::move(other.script_base_dir);
            package_context = std::move(other.package_context);
            package_stats = other.package_stats;
            package_manifest = std::move(other.package_manifest);
            package_mode = other.package_mode;
            if (package_context.stats == &other.package_stats) {
                package_context.stats = &package_stats;
            }
            other.package_context.stats = nullptr;
        }
        return *this;
    }
};

HostBudgets desktop_browser_budgets() {
    HostBudgets budgets;
    budgets.max_resource_bytes = kMaxInputBytes;
    budgets.max_input_events_per_frame = 64;
    budgets.max_timer_callbacks_per_frame = 32;
    budgets.max_framebuffer_pixels = 2400 * 2400;
    return budgets;
}

AppRuntimeHostOptions desktop_app_runtime_options() {
    return AppRuntimeHostOptions{64, 32, 64, 1024 * 1024, 4};
}

std::string read_file_limited(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("failed to open input file: " + path);
    }

    std::ostringstream output;
    char buffer[4096];
    std::size_t total = 0;
    while (file && total < kMaxInputBytes) {
        const std::size_t remaining = kMaxInputBytes - total;
        const std::size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        file.read(buffer, static_cast<std::streamsize>(chunk));
        const std::streamsize read = file.gcount();
        if (read <= 0) {
            break;
        }
        output.write(buffer, read);
        total += static_cast<std::size_t>(read);
    }
    return output.str();
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        wide.data(), required);
    return wide;
}

std::string extension_for_audio_source(const std::string& source) {
    std::string cleaned = source;
    const std::size_t query = cleaned.find_first_of("?#");
    if (query != std::string::npos) {
        cleaned.resize(query);
    }
    const std::filesystem::path path(cleaned);
    std::string extension = path.extension().string();
    if (extension.empty() || extension.size() > 8) {
        extension = ".dat";
    }
    return extension;
}

std::filesystem::path write_temp_audio_resource(const std::string& source,
                                                const std::string& bytes) {
    wchar_t temp_dir[MAX_PATH + 1]{};
    if (GetTempPathW(MAX_PATH, temp_dir) == 0) {
        throw std::runtime_error("failed to locate temp directory for audio smoke");
    }
    wchar_t temp_name[MAX_PATH + 1]{};
    if (GetTempFileNameW(temp_dir, L"jfa", 0, temp_name) == 0) {
        throw std::runtime_error("failed to create temp file for audio smoke");
    }
    std::filesystem::path path(temp_name);
    const std::filesystem::path renamed =
        path.replace_extension(extension_for_audio_source(source));
    std::error_code error;
    std::filesystem::rename(temp_name, renamed, error);
    path = error ? std::filesystem::path(temp_name) : renamed;

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("failed to write temp audio resource");
    }
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("failed while writing temp audio resource");
    }
    return path;
}

std::string mci_error_text(MCIERROR error) {
    wchar_t buffer[256]{};
    if (mciGetErrorStringW(error, buffer, static_cast<UINT>(std::size(buffer)))) {
        const int required = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
        if (required > 1) {
            std::string text(static_cast<std::size_t>(required - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer, -1, text.data(), required, nullptr, nullptr);
            return text;
        }
    }
    return "MCI error " + std::to_string(static_cast<unsigned long>(error));
}

void throw_mci_error(const std::string& operation, MCIERROR error) {
    if (error == 0) {
        return;
    }
    throw std::runtime_error(operation + " failed: " + mci_error_text(error));
}

std::wstring mci_quote_path(const std::filesystem::path& path) {
    std::wstring wide = path.wstring();
    for (wchar_t& ch : wide) {
        if (ch == L'"') {
            ch = L'\'';
        }
    }
    return L"\"" + wide + L"\"";
}

void stop_win32_audio_playback() {
    PlaySoundW(nullptr, nullptr, 0);
    const std::wstring stop = std::wstring(L"stop ") + kWin32AudioAlias;
    mciSendStringW(stop.c_str(), nullptr, 0, nullptr);
    const std::wstring close = std::wstring(L"close ") + kWin32AudioAlias;
    mciSendStringW(close.c_str(), nullptr, 0, nullptr);
}

bool play_win32_audio_file_async(const std::filesystem::path& path, std::string& error) {
    const std::string extension = path.extension().string();
    std::string lower_extension;
    lower_extension.reserve(extension.size());
    for (char ch : extension) {
        lower_extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lower_extension == ".wav") {
        const bool played = PlaySoundW(path.wstring().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        if (!played) {
            error = "PlaySoundW failed for WAV source";
            return false;
        }
        return true;
    }

    try {
        stop_win32_audio_playback();
        const std::wstring open = L"open " + mci_quote_path(path) + L" alias " + kWin32AudioAlias;
        throw_mci_error("audio open", mciSendStringW(open.c_str(), nullptr, 0, nullptr));
        const std::wstring play = std::wstring(L"play ") + kWin32AudioAlias;
        throw_mci_error("audio play", mciSendStringW(play.c_str(), nullptr, 0, nullptr));
        return true;
    } catch (const std::exception& exception) {
        stop_win32_audio_playback();
        error = exception.what();
        return false;
    }
}

std::uint32_t read_le_u32(const std::array<unsigned char, 4>& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint16_t read_le_u16(const std::array<unsigned char, 2>& bytes) {
    return static_cast<std::uint16_t>(bytes[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

bool read_exact(std::ifstream& input, unsigned char* data, std::size_t size) {
    input.read(reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    return input.good();
}

std::uint32_t estimate_wav_duration_ms(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return 0;
    }
    char riff[4] = {};
    char wave[4] = {};
    std::array<unsigned char, 4> u32{};
    if (!read_exact(input, reinterpret_cast<unsigned char*>(riff), 4) ||
        !read_exact(input, u32.data(), u32.size()) ||
        !read_exact(input, reinterpret_cast<unsigned char*>(wave), 4) ||
        std::strncmp(riff, "RIFF", 4) != 0 ||
        std::strncmp(wave, "WAVE", 4) != 0) {
        return 0;
    }

    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::uint32_t data_bytes = 0;
    while (input && (!sample_rate || !data_bytes)) {
        char chunk_id[4] = {};
        if (!read_exact(input, reinterpret_cast<unsigned char*>(chunk_id), 4) ||
            !read_exact(input, u32.data(), u32.size())) {
            break;
        }
        const std::uint32_t chunk_size = read_le_u32(u32);
        const std::streampos payload_begin = input.tellg();
        if (std::strncmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            std::array<unsigned char, 2> u16{};
            if (!read_exact(input, u16.data(), u16.size())) {
                return 0;
            }
            const std::uint16_t audio_format = read_le_u16(u16);
            if (!read_exact(input, u16.data(), u16.size())) {
                return 0;
            }
            channels = read_le_u16(u16);
            if (!read_exact(input, u32.data(), u32.size())) {
                return 0;
            }
            sample_rate = read_le_u32(u32);
            input.seekg(6, std::ios::cur);
            if (!read_exact(input, u16.data(), u16.size())) {
                return 0;
            }
            bits_per_sample = read_le_u16(u16);
            if (audio_format != 1) {
                return 0;
            }
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            data_bytes = chunk_size;
        }
        input.seekg(payload_begin + static_cast<std::streamoff>(chunk_size + (chunk_size & 1U)));
    }
    const std::uint32_t bytes_per_second =
        sample_rate * std::max<std::uint16_t>(1, channels) * std::max<std::uint16_t>(1, bits_per_sample) / 8U;
    if (bytes_per_second == 0 || data_bytes == 0) {
        return 0;
    }
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(data_bytes) * 1000ULL) / bytes_per_second));
}

bool run_win32_audio_smoke_file(const std::filesystem::path& path,
                                int duration_ms,
                                std::string& error) {
    duration_ms = std::max(0, std::min(duration_ms, 10000));
    const std::string extension = path.extension().string();
    std::string lower_extension;
    lower_extension.reserve(extension.size());
    for (char ch : extension) {
        lower_extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (lower_extension == ".wav") {
        if (duration_ms == 0) {
            const bool played = PlaySoundW(path.wstring().c_str(), nullptr, SND_FILENAME | SND_SYNC | SND_NODEFAULT);
            if (!played) {
                error = "PlaySoundW failed for WAV source";
                return false;
            }
            return true;
        }
        const bool played = PlaySoundW(path.wstring().c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        if (!played) {
            error = "PlaySoundW failed for WAV source";
            return false;
        }
        Sleep(static_cast<DWORD>(duration_ms));
        PlaySoundW(nullptr, nullptr, 0);
        return true;
    }

    try {
        stop_win32_audio_playback();
        const std::wstring open = L"open " + mci_quote_path(path) + L" alias " + kWin32AudioAlias;
        throw_mci_error("audio open", mciSendStringW(open.c_str(), nullptr, 0, nullptr));
        const std::wstring play = std::wstring(L"play ") + kWin32AudioAlias;
        throw_mci_error("audio play", mciSendStringW(play.c_str(), nullptr, 0, nullptr));
        if (duration_ms > 0) {
            Sleep(static_cast<DWORD>(duration_ms));
        }
        stop_win32_audio_playback();
        return true;
    } catch (const std::exception& exception) {
        stop_win32_audio_playback();
        error = exception.what();
        return false;
    }
}

bool resolve_audio_smoke_source(const BrowserOptions& options,
                                std::filesystem::path& audio_path,
                                bool& temporary,
                                std::string& error) {
    temporary = false;
    if (options.audio_smoke_source.empty()) {
        error = "missing audio source";
        return false;
    }
    const std::string& source = options.audio_smoke_source;
    if (!options.app_path.empty() && !source.empty() && source.front() == '/') {
        const std::string package_source = source;
        try {
            const auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
            jellyframe_example::PackageResourceStats stats;
            jellyframe_example::PackageResourceContext context;
            context.root = package.root;
            context.base_url = package.manifest.entry;
            context.max_input_bytes = kMaxInputBytes;
            context.stats = &stats;
            context.bundle_bytes = package.bundle_bytes;
            context.bundle_entries = package.bundle_entries;
            context.bundle_payload_offset = package.bundle_payload_offset;
            std::string bytes;
            if (!jellyframe_example::load_package_resource(package_source, package.manifest.entry, bytes, &context)) {
                error = "package audio resource not found: " + package_source;
                return false;
            }
            audio_path = write_temp_audio_resource(package_source, bytes);
            temporary = true;
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
            return false;
        }
    }
    audio_path = std::filesystem::path(source);
    if (!std::filesystem::is_regular_file(audio_path)) {
        error = "audio file does not exist: " + source;
        return false;
    }
    return true;
}

int run_win32_audio_smoke(const BrowserOptions& options) {
    std::filesystem::path audio_path;
    bool temporary = false;
    std::string error;
    if (!resolve_audio_smoke_source(options, audio_path, temporary, error)) {
        std::cerr << "audio smoke failed: " << error << '\n';
        return 1;
    }
    const bool ok = run_win32_audio_smoke_file(audio_path, options.audio_smoke_ms, error);
    if (temporary) {
        std::error_code ignored;
        std::filesystem::remove(audio_path, ignored);
    }
    if (!ok) {
        std::cerr << "audio smoke failed: " << error << '\n';
        return 1;
    }
    std::cout << "audio smoke ok source=" << options.audio_smoke_source
              << " duration_ms=" << options.audio_smoke_ms << '\n';
    return 0;
}

std::string wide_char_to_utf8(wchar_t ch) {
    const int required = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::string describe_node(const Node* node) {
    if (node == nullptr) {
        return "(none)";
    }
    if (node->type == NodeType::Text) {
        return "#text";
    }

    std::string label = node->tag_name.empty() ? "(element)" : node->tag_name;
    const std::string& id = node->attribute("id");
    if (!id.empty()) {
        label += "#" + id;
    }
    const std::string& classes = node->attribute("class");
    if (!classes.empty()) {
        label += "." + classes;
    }
    return label;
}

std::uint8_t clamp_u8(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

void blend_pixel(FrameBuffer& target, int x, int y, Color source) {
    if (!target.contains(x, y) || source.a == 0) {
        return;
    }
    Color& destination = target.pixel(x, y);
    if (source.a == 255) {
        destination = source;
        return;
    }

    const int src_a = source.a;
    const int dst_a = destination.a;
    const int inv_src_a = 255 - src_a;
    const int out_a = src_a + ((dst_a * inv_src_a + 127) / 255);
    if (out_a == 0) {
        destination = Color{0, 0, 0, 0};
        return;
    }

    const auto blend_channel = [&](std::uint8_t src, std::uint8_t dst) {
        const int premul = src * src_a + ((dst * dst_a * inv_src_a + 127) / 255);
        return clamp_u8((premul + out_a / 2) / out_a);
    };

    destination = Color{
        blend_channel(source.r, destination.r),
        blend_channel(source.g, destination.g),
        blend_channel(source.b, destination.b),
        clamp_u8(out_a),
    };
}

bool draw_text_with_gdi(FrameBuffer& target,
                        Rect rect,
                        Color color,
                        const std::string& text,
                        int font_size,
                        int font_weight,
                        TextCommandAlign align,
                        bool single_line,
                        void*) {
    const std::wstring wide = utf8_to_wide(text);
    if (wide.empty() || rect.width <= 0 || rect.height <= 0 || color.a == 0) {
        return false;
    }

    const int bitmap_width = rect.width + std::max(4, rect.height / 8);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bitmap_width;
    info.bmiHeader.biHeight = -rect.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen_dc);
    if (memory_dc == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        return false;
    }

    HGDIOBJ old_bitmap = SelectObject(memory_dc, bitmap);
    RECT bounds{0, 0, rect.width, rect.height};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory_dc, &bounds, black);
    DeleteObject(black);

    const int font_height = -std::max(8, font_size);
    const int gdi_weight = font_weight >= 600 ? FW_BOLD : FW_NORMAL;
    HFONT font = CreateFontW(font_height, 0, 0, 0, gdi_weight, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");
    HGDIOBJ old_font = font != nullptr ? SelectObject(memory_dc, font) : nullptr;
    SetBkMode(memory_dc, TRANSPARENT);
    SetTextColor(memory_dc, RGB(255, 255, 255));
    UINT flags = DT_NOPREFIX;
    if (align == TextCommandAlign::Center) {
        flags |= DT_CENTER;
    } else if (align == TextCommandAlign::End) {
        flags |= DT_RIGHT;
    } else {
        flags |= DT_LEFT;
    }
    flags |= single_line ? (DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS) : DT_WORDBREAK;
    DrawTextW(memory_dc, wide.c_str(), static_cast<int>(wide.size()), &bounds, flags);

    const auto* pixels = static_cast<const std::uint32_t*>(bits);
    for (int y = 0; y < rect.height; ++y) {
        for (int x = 0; x < rect.width; ++x) {
            const std::uint32_t pixel = pixels[static_cast<std::size_t>(y * bitmap_width + x)];
            const int blue = static_cast<int>(pixel & 0xffU);
            const int green = static_cast<int>((pixel >> 8U) & 0xffU);
            const int red = static_cast<int>((pixel >> 16U) & 0xffU);
            const int coverage = std::max(red, std::max(green, blue));
            if (coverage == 0) {
                continue;
            }
            Color source = color;
            source.a = clamp_u8((static_cast<int>(source.a) * coverage + 127) / 255);
            blend_pixel(target, rect.x + x, rect.y + y, source);
        }
    }

    if (old_font != nullptr) {
        SelectObject(memory_dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    SelectObject(memory_dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    return true;
}

struct BrowserImageContext {
    ImageDecodeMock* images = nullptr;
};

struct BrowserImageResolveContext {
    AppRuntimeHost* runtime = nullptr;
    ImageDecodeMock* images = nullptr;
    AppImageSurfaceCache* cache = nullptr;
    DiagnosticSink* diagnostics = nullptr;
};

bool resolve_browser_image_handle(const Node& node, std::uint32_t& handle, void* raw_context) {
    auto* context = static_cast<BrowserImageResolveContext*>(raw_context);
    if (context == nullptr || context->runtime == nullptr || context->images == nullptr ||
        context->cache == nullptr || node.type != NodeType::Element || node.tag_name != "img") {
        return false;
    }
    const std::string src = node.attribute("src");
    const AppImageSurfaceState previous_state = context->cache->state_for_url(src);
    const bool resolved = context->cache->resolve_or_request(*context->runtime, *context->images, src, &handle);
    const AppServiceSubmitStatus submit_status = context->cache->last_submit_status_for_url(src);
    const AppImageSurfaceState current_state = context->cache->state_for_url(src);
    if (!resolved && submit_status != AppServiceSubmitStatus::Accepted &&
        previous_state != AppImageSurfaceState::Failed &&
        current_state != AppImageSurfaceState::Pending) {
        report_image_request_failure(context->diagnostics,
                                     src,
                                     submit_status,
                                     context->cache->last_host_status_for_url(src));
        if (context->diagnostics != nullptr) {
            report_diagnostic(context->diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Info,
                              "image-cache-state",
                              "Image cache state after request failure",
                              context->cache->diagnostic_detail_for_url(src));
        }
    }
    return resolved;
}

Color read_surface_pixel(const AppDecodedSurfaceRecord& surface, int x, int y) {
    if (x < 0 || y < 0 || x >= surface.width || y >= surface.height ||
        surface.stride_pixels < surface.width) {
        return Color{0, 0, 0, 0};
    }
    const std::size_t index = static_cast<std::size_t>(y * surface.stride_pixels + x);
    if (surface.pixel_format == HostPixelFormat::Rgb565) {
        const std::size_t byte_index = index * 2;
        if (byte_index + 1 >= surface.pixels.size()) {
            return Color{0, 0, 0, 0};
        }
        const std::uint16_t packed =
            static_cast<std::uint16_t>(surface.pixels[byte_index] |
                                      (static_cast<std::uint16_t>(surface.pixels[byte_index + 1]) << 8U));
        const std::uint8_t r5 = static_cast<std::uint8_t>((packed >> 11U) & 0x1fU);
        const std::uint8_t g6 = static_cast<std::uint8_t>((packed >> 5U) & 0x3fU);
        const std::uint8_t b5 = static_cast<std::uint8_t>(packed & 0x1fU);
        return Color{
            static_cast<std::uint8_t>((r5 << 3U) | (r5 >> 2U)),
            static_cast<std::uint8_t>((g6 << 2U) | (g6 >> 4U)),
            static_cast<std::uint8_t>((b5 << 3U) | (b5 >> 2U)),
            255,
        };
    }
    if (surface.pixel_format == HostPixelFormat::Rgba8888) {
        const std::size_t byte_index = index * 4;
        if (byte_index + 3 >= surface.pixels.size()) {
            return Color{0, 0, 0, 0};
        }
        return Color{
            surface.pixels[byte_index],
            surface.pixels[byte_index + 1],
            surface.pixels[byte_index + 2],
            surface.pixels[byte_index + 3],
        };
    }
    return Color{0, 0, 0, 0};
}

Color lerp_color_fixed(Color a, Color b, int t256) {
    return Color{
        static_cast<std::uint8_t>((static_cast<int>(a.r) * (256 - t256) + static_cast<int>(b.r) * t256 + 128) >> 8),
        static_cast<std::uint8_t>((static_cast<int>(a.g) * (256 - t256) + static_cast<int>(b.g) * t256 + 128) >> 8),
        static_cast<std::uint8_t>((static_cast<int>(a.b) * (256 - t256) + static_cast<int>(b.b) * t256 + 128) >> 8),
        static_cast<std::uint8_t>((static_cast<int>(a.a) * (256 - t256) + static_cast<int>(b.a) * t256 + 128) >> 8),
    };
}

Color sample_surface_bilinear(const AppDecodedSurfaceRecord& surface,
                              Rect source_rect,
                              int local_x,
                              int local_y,
                              int dst_width,
                              int dst_height) {
    if (source_rect.width <= 1 || source_rect.height <= 1 || dst_width <= 1 || dst_height <= 1) {
        const int src_x = std::min(surface.width - 1,
                                   source_rect.x + (local_x * source_rect.width) / std::max(1, dst_width));
        const int src_y = std::min(surface.height - 1,
                                   source_rect.y + (local_y * source_rect.height) / std::max(1, dst_height));
        return read_surface_pixel(surface, src_x, src_y);
    }

    const int fx = (local_x * (source_rect.width - 1) * 256) / std::max(1, dst_width - 1);
    const int fy = (local_y * (source_rect.height - 1) * 256) / std::max(1, dst_height - 1);
    const int base_x = std::min(source_rect.width - 1, fx >> 8);
    const int base_y = std::min(source_rect.height - 1, fy >> 8);
    const int next_x = std::min(source_rect.width - 1, base_x + 1);
    const int next_y = std::min(source_rect.height - 1, base_y + 1);
    const int tx = fx & 0xff;
    const int ty = fy & 0xff;

    const Color top = lerp_color_fixed(read_surface_pixel(surface, source_rect.x + base_x, source_rect.y + base_y),
                                       read_surface_pixel(surface, source_rect.x + next_x, source_rect.y + base_y),
                                       tx);
    const Color bottom = lerp_color_fixed(read_surface_pixel(surface, source_rect.x + base_x, source_rect.y + next_y),
                                          read_surface_pixel(surface, source_rect.x + next_x, source_rect.y + next_y),
                                          tx);
    return lerp_color_fixed(top, bottom, ty);
}

struct ImageDrawMapping {
    Rect dst;
    Rect src;
};

int object_position_offset(int outer_size, int inner_size, int percent) {
    if (outer_size == inner_size) {
        return 0;
    }
    const int delta = outer_size - inner_size;
    return (delta * std::max(0, std::min(100, percent)) + (delta >= 0 ? 50 : -50)) / 100;
}

Rect positioned_rect(Rect outer, int width, int height, ObjectPosition position) {
    return Rect{
        outer.x + object_position_offset(outer.width, width, position.x_percent),
        outer.y + object_position_offset(outer.height, height, position.y_percent),
        width,
        height,
    };
}

ImageDrawMapping map_image_draw_rect(Rect rect,
                                     int source_width,
                                     int source_height,
                                     ObjectFit fit,
                                     ObjectPosition position) {
    if (rect.width <= 0 || rect.height <= 0 || source_width <= 0 || source_height <= 0) {
        return ImageDrawMapping{};
    }
    if (fit == ObjectFit::Fill) {
        return ImageDrawMapping{rect, Rect{0, 0, source_width, source_height}};
    }

    if (fit == ObjectFit::None ||
        (fit == ObjectFit::ScaleDown && source_width <= rect.width && source_height <= rect.height)) {
        const int dst_width = std::min(source_width, rect.width);
        const int dst_height = std::min(source_height, rect.height);
        const int src_x = std::max(0, -object_position_offset(rect.width, source_width, position.x_percent));
        const int src_y = std::max(0, -object_position_offset(rect.height, source_height, position.y_percent));
        return ImageDrawMapping{
            positioned_rect(rect, dst_width, dst_height, position),
            Rect{src_x, src_y, dst_width, dst_height},
        };
    }

    const bool contain = fit == ObjectFit::Contain || fit == ObjectFit::ScaleDown;
    const long long lhs = static_cast<long long>(rect.width) * source_height;
    const long long rhs = static_cast<long long>(rect.height) * source_width;
    const bool limit_by_width = contain ? lhs <= rhs : lhs >= rhs;
    if (limit_by_width) {
        const int dst_width = rect.width;
        const int dst_height = std::max(1, static_cast<int>(
            (static_cast<long long>(source_height) * rect.width + source_width / 2) / source_width));
        return ImageDrawMapping{
            positioned_rect(rect, dst_width, dst_height, position),
            Rect{0, 0, source_width, source_height},
        };
    }
    const int dst_height = rect.height;
    const int dst_width = std::max(1, static_cast<int>(
        (static_cast<long long>(source_width) * rect.height + source_height / 2) / source_height));
    return ImageDrawMapping{
        positioned_rect(rect, dst_width, dst_height, position),
        Rect{0, 0, source_width, source_height},
    };
}

bool paint_image_surface(FrameBuffer& target,
                         Rect rect,
                         std::uint32_t image_handle,
                         ObjectFit object_fit,
                         ObjectPosition object_position,
                         ImageRendering image_rendering,
                         void* raw_context) {
    auto* context = static_cast<BrowserImageContext*>(raw_context);
    if (context == nullptr || context->images == nullptr || rect.width <= 0 || rect.height <= 0) {
        return false;
    }
    const AppDecodedSurfaceRecord* surface = context->images->surface(image_handle);
    if (surface == nullptr || surface->width <= 0 || surface->height <= 0 || surface->pixels.empty()) {
        return false;
    }
    const ImageDrawMapping mapping =
        map_image_draw_rect(rect, surface->width, surface->height, object_fit, object_position);
    if (mapping.dst.width <= 0 || mapping.dst.height <= 0 || mapping.src.width <= 0 || mapping.src.height <= 0) {
        return false;
    }
    const Rect clip{
        std::max(rect.x, mapping.dst.x),
        std::max(rect.y, mapping.dst.y),
        std::min(rect.x + rect.width, mapping.dst.x + mapping.dst.width) - std::max(rect.x, mapping.dst.x),
        std::min(rect.y + rect.height, mapping.dst.y + mapping.dst.height) - std::max(rect.y, mapping.dst.y),
    };
    if (clip.width <= 0 || clip.height <= 0) {
        return true;
    }
    const bool smooth = image_rendering == ImageRendering::Auto;
    for (int y = 0; y < clip.height; ++y) {
        const int dst_y = clip.y + y;
        const int local_y = dst_y - mapping.dst.y;
        for (int x = 0; x < clip.width; ++x) {
            const int dst_x = clip.x + x;
            if (!target.contains(dst_x, dst_y)) {
                continue;
            }
            const int local_x = dst_x - mapping.dst.x;
            if (smooth) {
                blend_pixel(target,
                            dst_x,
                            dst_y,
                            sample_surface_bilinear(*surface,
                                                    mapping.src,
                                                    local_x,
                                                    local_y,
                                                    mapping.dst.width,
                                                    mapping.dst.height));
            } else {
                const int src_y = std::min(surface->height - 1,
                                           mapping.src.y + (local_y * mapping.src.height) /
                                               std::max(1, mapping.dst.height));
                const int src_x = std::min(surface->width - 1,
                                           mapping.src.x + (local_x * mapping.src.width) /
                                               std::max(1, mapping.dst.width));
                blend_pixel(target, dst_x, dst_y, read_surface_pixel(*surface, src_x, src_y));
            }
        }
    }
    return true;
}

HFONT create_gdi_text_font(int font_size, int font_weight) {
    const int font_height = -std::max(8, font_size);
    const int gdi_weight = font_weight >= 600 ? FW_BOLD : FW_NORMAL;
    return CreateFontW(font_height, 0, 0, 0, gdi_weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                       L"Microsoft YaHei UI");
}

bool measure_text_with_gdi(const std::string& text,
                           int font_size,
                           int font_weight,
                           TextMetrics* metrics,
                           void*) {
    if (metrics == nullptr) {
        return false;
    }
    const std::wstring wide = utf8_to_wide(text);
    if (!text.empty() && wide.empty()) {
        return false;
    }

    HDC dc = GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }
    HFONT font = create_gdi_text_font(font_size, font_weight);
    HGDIOBJ old_font = font != nullptr ? SelectObject(dc, font) : nullptr;

    SIZE size{0, 0};
    bool ok = true;
    if (!wide.empty() &&
        GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &size) == 0) {
        ok = false;
    }
    TEXTMETRICW text_metric{};
    if (GetTextMetricsW(dc, &text_metric) == 0) {
        ok = false;
    }

    if (ok) {
        metrics->width = std::max(0L, size.cx) + (wide.empty() ? 0 : std::max(2, font_size / 4));
        metrics->line_height = std::max(1L, text_metric.tmHeight + text_metric.tmExternalLeading) +
            std::max(2, font_size / 6);
    }

    if (old_font != nullptr) {
        SelectObject(dc, old_font);
    }
    if (font != nullptr) {
        DeleteObject(font);
    }
    ReleaseDC(nullptr, dc);
    return ok;
}

struct BrowserTextBackend {
    TextMeasureProvider measure;
    TextPainter painter;
};

BrowserTextBackend make_browser_text_backend(const BrowserOptions& options, AppRuntimeHost* runtime) {
    if (options.use_app_fonts && runtime != nullptr && runtime->fonts().primary_font() != nullptr) {
        return BrowserTextBackend{runtime->fonts().measure_provider(), runtime->fonts().painter()};
    }
    return BrowserTextBackend{
        TextMeasureProvider{measure_text_with_gdi, nullptr},
        TextPainter{draw_text_with_gdi, nullptr},
    };
}

InputModifiers modifiers_from_keys(WPARAM wparam) {
    InputModifiers modifiers;
    modifiers.shift = (wparam & MK_SHIFT) != 0;
    modifiers.ctrl = (wparam & MK_CONTROL) != 0;
    modifiers.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    return modifiers;
}

int buttons_from_keys(WPARAM wparam) {
    int buttons = 0;
    if ((wparam & MK_LBUTTON) != 0) {
        buttons |= 1;
    }
    if ((wparam & MK_RBUTTON) != 0) {
        buttons |= 2;
    }
    if ((wparam & MK_MBUTTON) != 0) {
        buttons |= 4;
    }
    return buttons;
}

std::uint32_t color_to_bgrx(Color pixel) {
    if (pixel.a != 255) {
        pixel = Color{
            clamp_u8((pixel.r * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.g * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            clamp_u8((pixel.b * pixel.a + 255 * (255 - pixel.a) + 127) / 255),
            255,
        };
    }
    return (static_cast<std::uint32_t>(pixel.r) << 16U) |
           (static_cast<std::uint32_t>(pixel.g) << 8U) |
           static_cast<std::uint32_t>(pixel.b);
}

void append_to_montage(FrameBuffer& montage,
                       const FrameBuffer& frame,
                       int frame_index,
                       int columns,
                       int cell_width,
                       int cell_height,
                       int gap) {
    if (columns <= 0 || cell_width <= 0 || cell_height <= 0 ||
        frame.width <= 0 || frame.height <= 0) {
        return;
    }
    const int column = frame_index % columns;
    const int row = frame_index / columns;
    const int dst_x0 = column * (cell_width + gap);
    const int dst_y0 = row * (cell_height + gap);
    const int copy_width = std::min(cell_width, frame.width);
    const int copy_height = std::min(cell_height, frame.height);
    for (int y = 0; y < copy_height; ++y) {
        const int dst_y = dst_y0 + y;
        if (dst_y < 0 || dst_y >= montage.height) {
            continue;
        }
        for (int x = 0; x < copy_width; ++x) {
            const int dst_x = dst_x0 + x;
            if (dst_x < 0 || dst_x >= montage.width) {
                continue;
            }
            montage.pixel(dst_x, dst_y) = frame.pixel(x, y);
        }
    }
}

int parse_int_arg(const char* value, int fallback) {
    try {
        return std::max(1, std::stoi(value));
    } catch (...) {
        return fallback;
    }
}

int parse_int_unclamped(const char* value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

std::uint64_t parse_u64_arg(const char* value, std::uint64_t fallback) {
    try {
        return std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::stoull(value)));
    } catch (...) {
        return fallback;
    }
}

std::vector<std::string> split_colon_fields(const std::string& text) {
    std::vector<std::string> fields;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t end = text.find(':', begin);
        fields.push_back(text.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return fields;
}

std::string ascii_lowercase_local(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim_ascii(std::string value) {
    const auto not_space = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };
    const auto begin = std::find_if(value.begin(), value.end(), not_space);
    const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::vector<std::string> split_whitespace(const std::string& text) {
    std::vector<std::string> fields;
    std::istringstream input(text);
    std::string field;
    while (input >> field) {
        fields.push_back(field);
    }
    return fields;
}

ParsedFrameEvent parse_frame_event(const std::string& spec) {
    ParsedFrameEvent parsed;
    const std::vector<std::string> fields = split_colon_fields(spec);
    if (fields.size() < 2) {
        parsed.error = "expected FRAME:kind[:x:y]";
        return parsed;
    }
    parsed.event.frame_index = parse_int_unclamped(fields[0].c_str(), -1);
    if (fields[0].empty() || parsed.event.frame_index < 0) {
        parsed.error = "frame index must be non-negative";
        return parsed;
    }
    const std::string kind = ascii_lowercase_local(fields[1]);
    if (kind == "network-online") {
        parsed.event.kind = ScriptedFrameEventKind::NetworkOnline;
    } else if (kind == "network-offline") {
        parsed.event.kind = ScriptedFrameEventKind::NetworkOffline;
    } else if (kind == "screen-visible") {
        parsed.event.kind = ScriptedFrameEventKind::ScreenVisible;
    } else if (kind == "screen-hidden") {
        parsed.event.kind = ScriptedFrameEventKind::ScreenHidden;
    } else if (kind == "low-power-on") {
        parsed.event.kind = ScriptedFrameEventKind::LowPowerOn;
    } else if (kind == "low-power-off") {
        parsed.event.kind = ScriptedFrameEventKind::LowPowerOff;
    } else if (kind == "pointer-move" || kind == "pointer-down" ||
               kind == "pointer-up" || kind == "click") {
        if (fields.size() != 4) {
            parsed.error = "pointer/click events require FRAME:kind:x:y";
            return parsed;
        }
        parsed.event.x = parse_int_unclamped(fields[2].c_str(), 0);
        parsed.event.y = parse_int_unclamped(fields[3].c_str(), 0);
        if (kind == "pointer-move") {
            parsed.event.kind = ScriptedFrameEventKind::PointerMove;
        } else if (kind == "pointer-down") {
            parsed.event.kind = ScriptedFrameEventKind::PointerDown;
        } else if (kind == "pointer-up") {
            parsed.event.kind = ScriptedFrameEventKind::PointerUp;
        } else {
            parsed.event.kind = ScriptedFrameEventKind::Click;
        }
    } else {
        parsed.error = "unknown frame event kind: " + fields[1];
        return parsed;
    }
    parsed.ok = true;
    return parsed;
}

std::string event_spec_from_fields(const std::vector<std::string>& fields) {
    if (fields.size() < 3) {
        return {};
    }
    std::ostringstream spec;
    spec << fields[1] << ':' << fields[2];
    if (fields.size() >= 5) {
        spec << ':' << fields[3] << ':' << fields[4];
    }
    return spec.str();
}

bool apply_frame_script(BrowserOptions& options, const std::string& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "failed to open frame script: " + path;
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = trim_ascii(line);
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> fields = split_whitespace(line);
        if (fields.empty()) {
            continue;
        }
        const std::string command = ascii_lowercase_local(fields[0]);
        const auto require_count = [&](std::size_t count) -> bool {
            if (fields.size() == count) {
                return true;
            }
            std::ostringstream message;
            message << path << ':' << line_number << ": " << fields[0]
                    << " expects " << (count - 1) << " argument(s)";
            error = message.str();
            return false;
        };

        if (command == "capture-frames" || command == "output-dir") {
            if (!require_count(2)) {
                return false;
            }
            options.capture_frames = true;
            options.frame_output_dir = fields[1];
        } else if (command == "montage" || command == "contact-sheet") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_montage_path = fields[1];
        } else if (command == "montage-columns" || command == "columns") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_montage_columns = std::min(30, parse_int_arg(fields[1].c_str(), 0));
        } else if (command == "montage-gap" || command == "gap") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_montage_gap = std::min(64, parse_int_arg(fields[1].c_str(), options.frame_montage_gap));
        } else if (command == "frame-count" || command == "frames") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_count = std::min(600, parse_int_arg(fields[1].c_str(), options.frame_count));
        } else if (command == "frame-step-ms" || command == "step-ms") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_step_ms = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(1000, parse_u64_arg(fields[1].c_str(), options.frame_step_ms)));
        } else if (command == "frame-start-ms" || command == "start-ms") {
            if (!require_count(2)) {
                return false;
            }
            options.frame_start_ms = parse_u64_arg(fields[1].c_str(), options.frame_start_ms);
        } else if (command == "animation-fps" || command == "animation-frame-rate") {
            if (!require_count(2)) {
                return false;
            }
            options.animation_frame_rate =
                std::min(240, std::max(0, parse_int_unclamped(fields[1].c_str(), options.animation_frame_rate)));
        } else if (command == "animation-callbacks" || command == "animation-callbacks-per-frame") {
            if (!require_count(2)) {
                return false;
            }
            options.animation_callbacks_per_frame =
                std::min(1024,
                         std::max(0, parse_int_unclamped(fields[1].c_str(), options.animation_callbacks_per_frame)));
        } else if (command == "viewport") {
            if (!require_count(3)) {
                return false;
            }
            options.viewport_width = parse_int_arg(fields[1].c_str(), options.viewport_width);
            options.viewport_height = parse_int_arg(fields[2].c_str(), options.viewport_height);
            options.viewport_width_set = true;
            options.viewport_height_set = true;
        } else if (command == "viewport-width") {
            if (!require_count(2)) {
                return false;
            }
            options.viewport_width = parse_int_arg(fields[1].c_str(), options.viewport_width);
            options.viewport_width_set = true;
        } else if (command == "viewport-height") {
            if (!require_count(2)) {
                return false;
            }
            options.viewport_height = parse_int_arg(fields[1].c_str(), options.viewport_height);
            options.viewport_height_set = true;
        } else if (command == "event") {
            std::string event_spec;
            if (fields.size() == 2) {
                event_spec = fields[1];
            } else if (fields.size() == 3 || fields.size() == 5) {
                event_spec = event_spec_from_fields(fields);
            } else {
                std::ostringstream message;
                message << path << ':' << line_number
                        << ": event expects FRAME:kind[:x:y] or FRAME kind [x y]";
                error = message.str();
                return false;
            }
            ParsedFrameEvent parsed = parse_frame_event(event_spec);
            if (!parsed.ok) {
                std::ostringstream message;
                message << path << ':' << line_number << ": invalid event: " << parsed.error;
                error = message.str();
                return false;
            }
            options.frame_events.push_back(parsed.event);
        } else {
            std::ostringstream message;
            message << path << ':' << line_number << ": unknown frame script command: " << fields[0];
            error = message.str();
            return false;
        }
    }

    if (!options.frame_montage_path.empty()) {
        options.capture_frames = true;
    }
    return true;
}

void print_win32_browser_usage(std::ostream& output) {
    output
        << "usage: jellyframe_win32_browser [options] [page.html style.css [width height]]\n"
        << "\n"
        << "Options:\n"
        << "  --help                         Show this help and exit.\n"
        << "  --app PATH                     Load a JellyFrame app package directory or .jfapp.\n"
        << "  --script PATH                  Load an extra classic script file.\n"
        << "  --capture PATH                 Render one frame to BMP/PPM by extension.\n"
        << "  --capture-frames DIR           Hidden deterministic frame capture directory.\n"
        << "  --frame-script PATH            Apply a deterministic frame script file.\n"
        << "  --capture-montage PATH         Write a BMP/PPM contact sheet for captured frames.\n"
        << "  --montage-columns N            Contact sheet columns, default auto.\n"
        << "  --frame-count N                Frames for --capture-frames, default 30.\n"
        << "  --frame-step-ms N              Fixed frame step, default 33 ms.\n"
        << "  --frame-start-ms N             First scripted timestamp, default 1000 ms.\n"
        << "  --animation-fps N              Override host animation frame rate budget.\n"
        << "  --animation-callbacks N        Override animation callbacks pumped per frame.\n"
        << "  --frame-event SPEC             Inject event: FRAME:kind[:x:y].\n"
        << "                                 Kinds: click, pointer-move, pointer-down,\n"
        << "                                 pointer-up, network-online/offline,\n"
        << "                                 screen-visible/hidden, low-power-on/off.\n"
        << "  --viewport-width N             Override viewport width.\n"
        << "  --viewport-height N            Override viewport height.\n"
        << "  --use-app-fonts                Use package .jffont resources when available.\n"
        << "  --audio-smoke SOURCE           Win32-only host audio smoke test. SOURCE can\n"
        << "                                 be a local file, or /path in --app package.\n"
        << "  --audio-smoke-ms N             Playback duration for --audio-smoke, default 1000.\n"
        << "  --registry-store DIR           Run system-shell/app-manager mode.\n"
        << "  --launcher-app PATH            Launcher app used with --registry-store.\n"
        << "  --install-bundle PATH          Install .jfapp into registry store.\n"
        << "  --launch-app ID                Launch installed app id.\n"
        << "  --remove-app ID                Remove installed app id.\n";
}

const Node* find_first_element(const Node& node, const char* tag_name) {
    if (node.type == NodeType::Element && node.tag_name == tag_name) {
        return &node;
    }
    for (const auto& child : node.children) {
        const Node* found = find_first_element(*child, tag_name);
        if (found != nullptr) {
            return found;
        }
    }
    return nullptr;
}

Color page_background_color(const Node& document, const StyleResolver& resolver) {
    const Node* body = find_first_element(document, "body");
    if (body != nullptr) {
        const Style style = resolver.resolve(*body);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    const Node* html = find_first_element(document, "html");
    if (html != nullptr) {
        const Style style = resolver.resolve(*html);
        if (style.background_color.a != 0) {
            return style.background_color;
        }
    }
    return Color{255, 255, 255, 255};
}

void print_diagnostics(const VectorDiagnosticSink& diagnostics) {
    static bool printed_empty_diagnostics = false;
    if (diagnostics.empty()) {
        if (printed_empty_diagnostics) {
            return;
        }
        printed_empty_diagnostics = true;
        std::cout << "diagnostics: 0\n";
        return;
    }
    std::cout << "diagnostics: " << diagnostics.size() << '\n';
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        std::cout << "  [" << diagnostic_severity_name(diagnostic.severity) << "] "
                  << diagnostic_stage_name(diagnostic.stage) << "::" << diagnostic.code
                  << " - " << diagnostic.message;
        if (!diagnostic.detail.empty()) {
            std::cout << " (" << diagnostic.detail << ')';
        }
        std::cout << '\n';
    }
}

std::string html_escape_text(const std::string& value) {
    std::string output;
    output.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

bool replace_once(std::string& text, std::string_view marker, std::string_view replacement) {
    const std::size_t pos = text.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    text.replace(pos, marker.size(), replacement);
    return true;
}

std::string build_launcher_app_list_html(const std::filesystem::path& registry_store) {
    const jellyframe_example::InstalledAppRegistry registry =
        jellyframe_example::load_installed_app_registry(registry_store);
    std::ostringstream html;
    if (registry.apps.empty()) {
        html << "<section class='empty'><p>No installed apps.</p>"
             << "<p class='hint'>Use --install-bundle app.jfapp with --registry-store.</p></section>";
    }
    for (const jellyframe_example::InstalledAppEntry& app : registry.apps) {
        html << "<article class='app'>"
             << "<h2 class='name'>" << html_escape_text(app.name) << "</h2>"
             << "<p class='meta'>" << html_escape_text(app.id) << " - v"
             << html_escape_text(app.version_name) << " - "
             << app.bundle_size << " bytes</p>"
             << "<div class='actions'>"
             << "<button class='primary' data-action='launch' data-app-id='" << html_escape_text(app.id) << "'>Launch</button>"
             << "<button class='danger' data-action='delete' data-app-id='" << html_escape_text(app.id) << "'>Delete</button>"
             << "</div></article>";
    }
    return html.str();
}

std::string build_launcher_status_html(const std::string& status) {
    if (status.empty()) {
        return {};
    }
    return "<p class='status'>" + html_escape_text(status) + "</p>";
}

std::string load_launcher_resource(const jellyframe_example::AppPackage& package,
                                   const std::string& resource_path,
                                   jellyframe::DiagnosticSink* diagnostics = nullptr) {
    jellyframe_example::PackageResourceStats stats;
    jellyframe_example::PackageResourceContext context;
    context.root = package.root;
    context.base_url = package.manifest.entry;
    context.max_input_bytes = kMaxInputBytes;
    context.stats = &stats;
    context.diagnostics = diagnostics;
    context.bundle_bytes = package.bundle_bytes;
    context.bundle_entries = package.bundle_entries;
    context.bundle_payload_offset = package.bundle_payload_offset;
    std::string text;
    if (!jellyframe_example::load_package_resource(resource_path, package.manifest.entry, text, &context)) {
        return {};
    }
    return text;
}

std::string load_launcher_entry_html(const jellyframe_example::AppPackage& package) {
    std::string html = load_launcher_resource(package, package.manifest.entry);
    if (html.empty()) {
        throw std::runtime_error("failed to load launcher entry: " + package.manifest.entry);
    }
    return html;
}

void inject_launcher_markup(std::string& html, const std::string& app_list_html, const std::string& status_html) {
    if (!replace_once(html, kLauncherStatusMarker, status_html) && !status_html.empty()) {
        const std::size_t main_end = html.find("</main>");
        html.insert(main_end == std::string::npos ? html.size() : main_end, status_html);
    }
    if (!replace_once(html, kLauncherAppListMarker, app_list_html)) {
        const std::size_t main_end = html.find("</main>");
        html.insert(main_end == std::string::npos ? html.size() : main_end, app_list_html);
    }
}

std::string build_system_shell_html(const std::filesystem::path& launcher_app_path,
                                    const std::filesystem::path& registry_store,
                                    const std::string& status) {
    const auto package = jellyframe_example::load_app_package(launcher_app_path, kMaxInputBytes);
    std::string html = load_launcher_entry_html(package);
    inject_launcher_markup(html, build_launcher_app_list_html(registry_store), build_launcher_status_html(status));
    return html;
}

std::string load_system_shell_css(const std::filesystem::path& launcher_app_path) {
    const auto package = jellyframe_example::load_app_package(launcher_app_path, kMaxInputBytes);
    return load_launcher_resource(package, "/styles/app.css");
}

std::string emergency_launcher_error_html(const std::string& message) {
    return "<body><main class='launcher'><section class='empty'><h1>Launcher unavailable</h1><p>" +
        html_escape_text(message) + "</p></section></main></body>";
}

std::string emergency_launcher_error_css() {
    return "body{margin:0;padding:18px;background:#101418;color:#f8fafc;font-size:14px}"
           ".empty{padding:12px;background:#171d24;border:1px solid #ef4444;border-radius:8px}"
           "h1{margin:0 0 8px 0;font-size:20px;color:#ffffff}p{margin:0;color:#fecaca}";
}

const Node* find_shell_action_node(const Node* node) {
    for (const Node* current = node; current != nullptr; current = current->parent) {
        if (current->type == NodeType::Element && !current->attribute("data-action").empty() &&
            !current->attribute("data-app-id").empty()) {
            return current;
        }
    }
    return nullptr;
}

void load_package_fonts_into_runtime(const jellyframe_example::AppPackageManifest& manifest,
                                     jellyframe_example::PackageResourceContext& package_context,
                                     AppRuntimeHost* app_runtime,
                                     DiagnosticSink* diagnostics) {
    if (app_runtime == nullptr || manifest.font_sources.empty()) {
        return;
    }
    app_runtime->clear_current_fonts();
    for (const std::string& source : manifest.font_sources) {
        jellyframe_example::PackageResourceView view;
        if (jellyframe_example::load_package_resource_view(source, manifest.entry, view, &package_context) &&
            view.stable) {
            const AppFontLoadResult result = app_runtime->attach_current_jffont_view(view.data, view.size);
            if (result.loaded()) {
                continue;
            }
            report_diagnostic(diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Warning,
                              "app-font-resource-invalid",
                              "Manifest font resource is not a supported .jffont supplement",
                              source);
            continue;
        }

        std::string bytes;
        if (!jellyframe_example::load_package_resource(source, manifest.entry, bytes, &package_context)) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Warning,
                              "app-font-resource-missing",
                              "Manifest font resource could not be loaded",
                              source);
            continue;
        }
        const AppFontLoadResult result = app_runtime->load_current_jffont(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        if (!result.loaded()) {
            report_diagnostic(diagnostics,
                              DiagnosticStage::Package,
                              DiagnosticSeverity::Warning,
                              "app-font-resource-invalid",
                              "Manifest font resource is not a supported .jffont supplement",
                              source);
        }
    }
}

LoadedPage load_page(const BrowserOptions& options,
                     const HostBudgets& budgets,
                     DiagnosticSink* diagnostics,
                     AppRuntimeHost* app_runtime = nullptr) {
    HtmlParser html_parser;
    CssParser css_parser;
    HtmlParserOptions html_options = html_parser_options_from_budgets(budgets);
    html_options.diagnostics = diagnostics;
    CssParserOptions css_options = css_parser_options_from_budgets(budgets);
    css_options.diagnostics = diagnostics;
    LoadedPage page;
    if (!options.inline_html.empty()) {
        page.document = html_parser.parse(options.inline_html, html_options);
        page.stylesheet = css_parser.parse(options.inline_css, css_options);
        page.script_base_dir = std::filesystem::current_path();
        return page;
    }
    if (!options.app_path.empty()) {
        auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
        page.package_mode = true;
        page.package_manifest = package.manifest;
        page.package_stats = {};
        page.package_context.root = package.root;
        page.package_context.base_url = package.manifest.entry;
        page.package_context.max_input_bytes = kMaxInputBytes;
        page.package_context.stats = &page.package_stats;
        page.package_context.diagnostics = diagnostics;
        page.package_context.bundle_bytes = std::move(package.bundle_bytes);
        page.package_context.bundle_entries = std::move(package.bundle_entries);
        page.package_context.bundle_payload_offset = package.bundle_payload_offset;
        load_package_fonts_into_runtime(package.manifest, page.package_context, app_runtime, diagnostics);
        std::string html;
        if (!jellyframe_example::load_package_resource(package.manifest.entry,
                                                       package.manifest.entry,
                                                       html,
                                                       &page.package_context)) {
            throw std::runtime_error("failed to load package entry: " + package.manifest.entry);
        }
        page.document = html_parser.parse(html, html_options);
        const std::string css = combine_author_css(
            {}, *page.document, jellyframe_example::load_package_stylesheet, &page.package_context);
        page.stylesheet = css_parser.parse(css, css_options);
        page.script_base_dir = package.root / std::filesystem::path(package.manifest.entry).parent_path().relative_path();
        return page;
    }

    page.document = html_parser.parse(read_file_limited(options.html_path), html_options);
    page.stylesheet = css_parser.parse(
        [&]() {
            jellyframe_example::StylesheetLoadContext stylesheet_context;
            const std::filesystem::path css_path(options.css_path);
            stylesheet_context.base_dir =
                css_path.has_parent_path() ? css_path.parent_path() : std::filesystem::current_path();
            stylesheet_context.max_input_bytes = kMaxInputBytes;
            stylesheet_context.diagnostics = diagnostics;
            return combine_author_css(jellyframe_example::read_file_limited(options.css_path, kMaxInputBytes),
                                      *page.document,
                                      jellyframe_example::load_linked_stylesheet,
                                      &stylesheet_context);
        }(),
        css_options);
    const std::filesystem::path html_path(options.html_path);
    page.script_base_dir = html_path.has_parent_path() ? html_path.parent_path() : std::filesystem::current_path();
    return page;
}

FrameBuffer render_page_with_browser_text(const BrowserOptions& options) {
    const HostBudgets budgets = desktop_browser_budgets();
    VectorDiagnosticSink diagnostics;
    const AppRuntimeHostOptions app_runtime_options = desktop_app_runtime_options();
    AppRuntimeHost app_runtime{app_runtime_options};
    AppFrameScratch app_frame_scratch;
    app_frame_scratch.reserve_from_options(app_runtime_options);
    AppRuntimeHost* runtime = nullptr;
    if (!options.app_path.empty()) {
        app_runtime.launch(options.app_path, AppRole::App);
        runtime = &app_runtime;
    } else {
        app_runtime.launch("org.jellyframe.debug.capture", AppRole::App);
        runtime = &app_runtime;
    }
    LoadedPage page = load_page(options, budgets, &diagnostics, runtime);
    BrowserTextBackend text_backend = make_browser_text_backend(options, runtime);
    ImageDecodeMock debug_images(ImageDecodePolicy{true, 1024, 256, 256, 256 * 256 * 4, 4});
    add_debug_image_fixtures(debug_images);
    if (page.package_mode) {
        add_package_image_fixtures(*page.document, page.package_context, debug_images, &diagnostics);
    }
    AppImageSurfaceCache image_cache(AppImageSurfaceCacheOptions{8, 512 * 1024});
    BrowserImageResolveContext image_resolve_context{&app_runtime, &debug_images, &image_cache, &diagnostics};
    BrowserImageContext image_context{&debug_images};
    StyleResolverOptions style_options;
    style_options.diagnostics = &diagnostics;
    StyleResolver resolver(std::move(page.stylesheet), style_options);

    RenderTreeOptions render_options = render_tree_options_from_budgets(budgets);
    render_options.diagnostics = &diagnostics;
    RenderTreeBuilder render_builder(resolver, render_options);
    auto render_tree = render_builder.build(*page.document);
    LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets);
    layout_options.diagnostics = &diagnostics;
    LayoutEngine layout_engine(resolver, text_backend.measure, layout_options);
    auto layout_tree = layout_engine.layout(*render_tree, options.viewport_width);
    LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets);
    layer_options.diagnostics = &diagnostics;
    layer_options.image_resolver = ImageHandleResolver{resolve_browser_image_handle, &image_resolve_context};
    LayerTreeBuilder layer_builder(layer_options);
    auto layer_tree = layer_builder.build(*layout_tree);
    for (int pass = 0; pass < 4 && debug_images.complete_next(app_runtime); ++pass) {
        app_runtime.pump_frame_completions(app_frame_scratch);
        bool image_ready = false;
        for (const HostServiceCompletion& completion : app_frame_scratch.accepted_completions) {
            const std::string image_src = image_cache.url_for_job(completion.job_id);
            report_image_completion_failure(&diagnostics, completion, image_src);
            const bool handled_image = image_cache.handle_completion(completion);
            if (handled_image && completion.kind == HostServiceJobKind::ImageDecode &&
                completion.status != HostServiceStatus::Completed) {
                report_image_cache_state(&diagnostics, image_cache, image_src);
            }
            image_ready = handled_image || image_ready;
        }
        if (image_ready) {
            layer_tree = layer_builder.build(*layout_tree);
            std::vector<std::uint32_t> protected_handles;
            collect_image_handles(*layer_tree, protected_handles);
            const AppImageSurfaceEvictionResult evicted =
                image_cache.evict_unreferenced_with_result(app_runtime,
                                                           debug_images,
                                                           protected_handles.data(),
                                                           protected_handles.size());
            report_image_eviction_result(&diagnostics, evicted);
        }
    }

    SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets);
    compositor_options.diagnostics = &diagnostics;
    SoftwareCompositor compositor(text_backend.painter, ImagePainter{paint_image_surface, &image_context}, compositor_options);
    FrameBuffer frame_buffer = compositor.render(*layer_tree,
                                                 options.viewport_width,
                                                 options.viewport_height,
                                                 page_background_color(*page.document, resolver));
    if (frame_buffer.width <= 0 || frame_buffer.height <= 0) {
        throw std::runtime_error("framebuffer budget exceeded");
    }
    print_diagnostics(diagnostics);
    return frame_buffer;
}

FrameBuffer render_page_with_browser_text(const std::string& html_path,
                                          const std::string& css_path,
                                          int viewport_width,
                                          int min_viewport_height) {
    BrowserOptions options;
    options.html_path = html_path;
    options.css_path = css_path;
    options.viewport_width = viewport_width;
    options.viewport_height = min_viewport_height;
    return render_page_with_browser_text(options);
}

class BrowserApp {
public:
    explicit BrowserApp(BrowserOptions options)
        : options_(std::move(options)),
          active_app_id_(options_.launch_app_id) {
        if (options_.animation_frame_rate >= 0) {
            budgets_.animation_frame_rate = static_cast<std::size_t>(options_.animation_frame_rate);
        }
        if (options_.animation_callbacks_per_frame >= 0) {
            budgets_.max_animation_callbacks_per_frame =
                static_cast<std::size_t>(options_.animation_callbacks_per_frame);
        }
        frame_scratch_.reserve_from_budgets(budgets_);
        app_frame_scratch_.reserve_from_options(desktop_app_runtime_options());
        if (!options_.launch_app_id.empty()) {
            app_runtime_.launch(options_.launch_app_id, AppRole::App);
        } else if (!options_.app_path.empty()) {
            app_runtime_.launch(options_.app_path, AppRole::App);
        } else if (!options_.registry_store_path.empty()) {
            app_runtime_.launch("org.jellyframe.system.launcher", AppRole::Launcher);
        } else {
            app_runtime_.launch("org.jellyframe.debug.page", AppRole::App);
        }
        const char* weather_json =
            "{\"modes\":{"
            "\"hourly\":{\"temp\":\"27\",\"condition\":\"Rain soon\",\"summary\":\"Next hour 35%\",\"wind\":\"9\",\"rain\":\"35\",\"updated\":\"Live\",\"icon\":\"rain\"},"
            "\"daily\":{\"temp\":\"24\",\"condition\":\"Cloudy\",\"summary\":\"AQI 42 Good\",\"wind\":\"8\",\"rain\":\"20\",\"updated\":\"Live\",\"icon\":\"cloudy\"},"
            "\"air\":{\"temp\":\"42\",\"condition\":\"Air\",\"summary\":\"AQI good\",\"wind\":\"5\",\"rain\":\"10\",\"updated\":\"Live\",\"icon\":\"haze\"}"
            "}}";
        const char* service_status_json = "{\"data\":\"Live\",\"audio\":\"Host owned\",\"sensors\":\"FG only\"}";
        debug_network_.add_fixture(NetworkFetchFixture{"/debug/ping.txt", 200, "text/plain", "pong"});
        debug_network_.add_fixture(NetworkFetchFixture{
            "/data/weather.json",
            200,
            "application/json",
            weather_json,
        });
        debug_network_.add_fixture(NetworkFetchFixture{
            "/data/service-status.json",
            200,
            "application/json",
            service_status_json,
        });
        add_debug_image_fixtures(debug_images_);
    }

    ~BrowserApp() {
        stop_win32_audio_playback();
        cleanup_temp_audio_files();
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        pending_script_audio_events_.clear();
        script_runtime_.reset();
        script_runtime_instance_id_ = 0;
#endif
        if (hwnd_ != nullptr) {
            HWND hwnd = hwnd_;
            hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            DestroyWindow(hwnd);
        }
    }

    bool initialize(HINSTANCE instance, int show_command) {
        if (options_.capture_frames) {
            scripted_time_enabled_ = true;
            scripted_now_ms_ = options_.frame_start_ms;
        }

        WNDCLASSW window_class{};
        window_class.lpfnWndProc = BrowserApp::window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClassName;
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassW(&window_class);

        RECT window_rect{0, 0, options_.viewport_width, options_.viewport_height};
        AdjustWindowRectEx(&window_rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        hwnd_ = CreateWindowExW(0,
                                kWindowClassName,
                                L"JellyFrame Win32 Browser",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                std::max<LONG>(1, window_rect.right - window_rect.left),
                                std::max<LONG>(1, window_rect.bottom - window_rect.top),
                                nullptr,
                                nullptr,
                                instance,
                                this);
        if (hwnd_ == nullptr) {
            return false;
        }

        ShowWindow(hwnd_, show_command);
        UpdateWindow(hwnd_);
        return true;
    }

    int run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    int capture_frames() {
        if (hwnd_ == nullptr ||
            (options_.frame_output_dir.empty() && options_.frame_montage_path.empty())) {
            return 1;
        }
        if (!options_.frame_output_dir.empty()) {
            std::filesystem::create_directories(options_.frame_output_dir);
        }

        FrameBuffer montage;
        bool montage_enabled = false;
        int montage_columns = 0;
        int montage_cell_width = 0;
        int montage_cell_height = 0;
        int montage_rows = 0;
        scripted_time_enabled_ = true;
        for (int frame = 0; frame < options_.frame_count; ++frame) {
            scripted_now_ms_ = options_.frame_start_ms +
                static_cast<std::uint64_t>(frame) * options_.frame_step_ms;
            dispatch_scripted_frame_events(frame);
            handle_timer(kScriptTimerId);
            record_frame_policy_sample();
            if (frame_buffer_.width <= 0 || frame_buffer_.height <= 0) {
                render_current(input_ ? input_->hovered_node() : nullptr,
                               nullptr,
                               input_ ? input_->focused_node() : nullptr);
            }
            if (!options_.frame_montage_path.empty() && frame == 0) {
                montage_columns = options_.frame_montage_columns > 0
                    ? options_.frame_montage_columns
                    : std::max(1, static_cast<int>(std::ceil(std::sqrt(
                          static_cast<double>(options_.frame_count) *
                          static_cast<double>(std::max(1, frame_buffer_.width)) /
                          static_cast<double>(std::max(1, frame_buffer_.height))))));
                montage_columns = std::max(1, std::min(options_.frame_count, montage_columns));
                montage_rows = (options_.frame_count + montage_columns - 1) / montage_columns;
                montage_cell_width = std::max(1, frame_buffer_.width);
                montage_cell_height = std::max(1, frame_buffer_.height);
                const int gap = std::max(0, options_.frame_montage_gap);
                const std::uint64_t sheet_width =
                    static_cast<std::uint64_t>(montage_columns) * static_cast<std::uint64_t>(montage_cell_width) +
                    static_cast<std::uint64_t>(std::max(0, montage_columns - 1)) * static_cast<std::uint64_t>(gap);
                const std::uint64_t sheet_height =
                    static_cast<std::uint64_t>(montage_rows) * static_cast<std::uint64_t>(montage_cell_height) +
                    static_cast<std::uint64_t>(std::max(0, montage_rows - 1)) * static_cast<std::uint64_t>(gap);
                constexpr std::uint64_t kMaxMontagePixels = 16ULL * 1024ULL * 1024ULL;
                if (sheet_width > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                    sheet_height > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                    sheet_width * sheet_height > kMaxMontagePixels) {
                    std::cerr << "capture montage skipped: contact sheet would exceed "
                              << kMaxMontagePixels << " pixels\n";
                } else {
                    montage.resize(static_cast<int>(sheet_width),
                                   static_cast<int>(sheet_height),
                                   Color{18, 20, 24, 255});
                    montage_enabled = true;
                }
            }
            if (montage_enabled) {
                append_to_montage(montage,
                                  frame_buffer_,
                                  frame,
                                  montage_columns,
                                  montage_cell_width,
                                  montage_cell_height,
                                  std::max(0, options_.frame_montage_gap));
            }
            if (!options_.frame_output_dir.empty()) {
                std::ostringstream name;
                name << "frame_" << std::setw(3) << std::setfill('0') << frame << ".bmp";
                const std::filesystem::path output =
                    std::filesystem::path(options_.frame_output_dir) / name.str();
                write_image(frame_buffer_, output.string());
            }
        }
        if (montage_enabled) {
            std::filesystem::path montage_path(options_.frame_montage_path);
            if (montage_path.has_parent_path()) {
                std::filesystem::create_directories(montage_path.parent_path());
            }
            write_image(montage, options_.frame_montage_path);
        }
        std::cout << "JellyFrame Win32 browser frame capture\n"
                  << "  output_dir=" << options_.frame_output_dir << '\n'
                  << "  montage=" << options_.frame_montage_path << '\n'
                  << "  frames=" << options_.frame_count << '\n'
                  << "  frame_step_ms=" << options_.frame_step_ms << '\n'
                  << "  viewport=" << viewport_width_ << "x" << viewport_height_ << '\n'
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
                  << "  scripting=on\n"
#else
                  << "  scripting=off\n"
#endif
                  << "  dirty_local=" << dirty_region_statistics_.dirty_rect_frames
                  << " full=" << dirty_region_statistics_.full_frame_frames
                  << " clean=" << dirty_region_statistics_.clean_frames << '\n'
                  << "  host_completion_batches=" << host_service_counters_.completion_batches
                  << " consumed=" << host_service_counters_.completions_consumed
                  << " accepted=" << host_service_counters_.completions_accepted
                  << " stale=" << host_service_counters_.completions_stale
                  << " released_stale_handles=" << host_service_counters_.released_stale_handles << '\n'
                  << "  host_completion_kinds network=" << host_service_counters_.network_completions
                  << " storage=" << host_service_counters_.storage_completions
                  << " image=" << host_service_counters_.image_completions
                  << " audio=" << host_service_counters_.audio_completions
                  << " other=" << host_service_counters_.other_completions << '\n'
                  << "  host_completion_handlers image=" << host_service_counters_.image_handled
                  << " script=" << host_service_counters_.script_host_completions_handled << '\n'
                  << "  system_event_batches=" << host_service_counters_.system_event_batches
                  << " consumed=" << host_service_counters_.system_events_consumed
                  << " accepted=" << host_service_counters_.system_events_accepted
                  << " stale=" << host_service_counters_.system_events_stale
                  << " script=" << host_service_counters_.script_system_events_handled << '\n'
                  << "  frame_policy_samples=" << frame_policy_counters_.sampled_frames
                  << " input=" << frame_policy_counters_.accepts_input_frames
                  << " timers=" << frame_policy_counters_.timer_frames
                  << " animation=" << frame_policy_counters_.animation_frames
                  << " present=" << frame_policy_counters_.present_frames << '\n'
                  << "  service_activity network=" << frame_policy_counters_.network_active_frames
                  << " audio=" << frame_policy_counters_.audio_active_frames
                  << " sensors=" << frame_policy_counters_.sensor_active_frames
                  << " pause_audio=" << frame_policy_counters_.pause_audio_frames
                  << " throttle_sensors=" << frame_policy_counters_.throttle_sensor_frames << '\n';
        return 0;
    }

private:
    HWND hwnd_ = nullptr;
    BrowserOptions options_;
    int viewport_width_ = 1;
    int viewport_height_ = 1;
    int scroll_y_ = 0;
    HostBudgets budgets_ = desktop_browser_budgets();

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
    std::unique_ptr<JerryScriptRuntime> script_runtime_;
    std::uint32_t script_runtime_instance_id_ = 0;
    struct PendingScriptAudioEvent {
        std::uint32_t audio_id = 0;
        std::uint64_t due_ms = 0;
        ScriptAudioEventKind kind = ScriptAudioEventKind::Ended;
    };
#endif
    std::unique_ptr<Node> document_;
    std::unique_ptr<StyleResolver> style_resolver_;
    RenderObjectPtr render_tree_;
    LayoutBoxPtr layout_tree_;
    LayerNodePtr layer_tree_;
    std::unique_ptr<InputController> input_;
    AnimationTimeline animation_timeline_;
    std::vector<StyleOverride> style_overrides_;
    std::vector<StyleOverride> previous_style_overrides_;
    bool clear_animation_overrides_after_render_ = false;
    bool scripted_time_enabled_ = false;
    std::uint64_t scripted_now_ms_ = 0;
    FrameScratch frame_scratch_;
    FrameBuffer frame_buffer_;
    Color page_background_{255, 255, 255, 255};
    std::vector<std::uint32_t> blit_pixels_;
    DirtyRegionMode last_dirty_region_mode_ = DirtyRegionMode::Clean;
    DirtyRegionFallbackReason last_dirty_region_reason_ = DirtyRegionFallbackReason::None;
    std::size_t last_dirty_rect_count_ = 0;
    int last_dirty_area_percent_ = 0;
    DisplayInvalidationResult last_display_invalidation_;
    DirtyRegionStatistics dirty_region_statistics_;
    VectorDiagnosticSink diagnostics_;
    bool system_shell_mode_ = false;
    std::string active_app_id_;
    AppRuntimeHost app_runtime_{desktop_app_runtime_options()};
    AppFrameScratch app_frame_scratch_;
    AppSystemEventQueue system_events_{32, 8};
    AppSystemStateSnapshot debug_system_state_;
    NetworkFetchMock debug_network_{NetworkFetchPolicy{true, 1024, 64 * 1024}};
    ImageDecodeMock debug_images_{ImageDecodePolicy{true, 1024, 256, 256, 256 * 256 * 4, 4}};
    AppImageSurfaceCache image_cache_{AppImageSurfaceCacheOptions{8, 512 * 1024}};
    BrowserImageContext image_context_{&debug_images_};
    AppLocalStorageShadow debug_local_storage_{AppPrivateKvPolicy{true, 64, 2048, 64, 32 * 1024}};
    std::uint32_t debug_local_storage_instance_id_ = 0;
    std::vector<std::filesystem::path> temp_audio_files_;
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
    std::vector<PendingScriptAudioEvent> pending_script_audio_events_;
#endif
    HostServiceDebugCounters host_service_counters_;
    AppBackgroundServicePolicy background_service_policy_;
    FramePolicyDebugCounters frame_policy_counters_;
    std::string pending_shell_action_;
    std::string pending_shell_app_id_;

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        BrowserApp* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            app = static_cast<BrowserApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        } else {
            app = reinterpret_cast<BrowserApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (app == nullptr) {
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        return app->handle_message(message, wparam, lparam);
    }

    std::uint64_t current_time_ms() const {
        return scripted_time_enabled_ ? scripted_now_ms_ : GetTickCount64();
    }

    LPARAM pointer_lparam(int x, int y) const {
        return MAKELPARAM(static_cast<SHORT>(x), static_cast<SHORT>(y));
    }

    void dispatch_scripted_frame_events(int frame_index) {
        for (const ScriptedFrameEvent& event : options_.frame_events) {
            if (event.frame_index != frame_index) {
                continue;
            }
            switch (event.kind) {
            case ScriptedFrameEventKind::NetworkOnline:
                debug_system_state_.network_online = true;
                queue_system_event(AppSystemEventKind::NetworkStatusChanged, "network online");
                break;
            case ScriptedFrameEventKind::NetworkOffline:
                debug_system_state_.network_online = false;
                queue_system_event(AppSystemEventKind::NetworkStatusChanged, "network offline");
                break;
            case ScriptedFrameEventKind::ScreenVisible:
                debug_system_state_.screen_on = true;
                queue_system_event(AppSystemEventKind::ScreenStateChanged, "screen visible");
                break;
            case ScriptedFrameEventKind::ScreenHidden:
                debug_system_state_.screen_on = false;
                queue_system_event(AppSystemEventKind::ScreenStateChanged, "screen hidden");
                break;
            case ScriptedFrameEventKind::LowPowerOn:
                debug_system_state_.low_power_mode = true;
                queue_system_event(AppSystemEventKind::LowPowerModeChanged, "low power on");
                break;
            case ScriptedFrameEventKind::LowPowerOff:
                debug_system_state_.low_power_mode = false;
                queue_system_event(AppSystemEventKind::LowPowerModeChanged, "low power off");
                break;
            case ScriptedFrameEventKind::PointerMove:
                handle_pointer_move(0, pointer_lparam(event.x, event.y));
                break;
            case ScriptedFrameEventKind::PointerDown:
                handle_pointer_down(0, pointer_lparam(event.x, event.y));
                break;
            case ScriptedFrameEventKind::PointerUp:
                handle_pointer_up(MK_LBUTTON, pointer_lparam(event.x, event.y));
                break;
            case ScriptedFrameEventKind::Click:
                handle_pointer_move(0, pointer_lparam(event.x, event.y));
                handle_pointer_down(0, pointer_lparam(event.x, event.y));
                handle_pointer_up(MK_LBUTTON, pointer_lparam(event.x, event.y));
                break;
            }
        }
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            resize_to_client();
            if (!options_.registry_store_path.empty() && options_.app_path.empty() && options_.inline_html.empty()) {
                configure_system_shell(options_.startup_status);
            }
            rebuild();
            return 0;
        case WM_SIZE:
            resize_to_client();
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_MOUSEMOVE:
            handle_pointer_move(wparam, lparam);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            handle_pointer_down(wparam, lparam);
            return 0;
        case WM_LBUTTONUP:
            handle_pointer_up(wparam, lparam);
            ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            handle_wheel(wparam, lparam);
            return 0;
        case WM_CHAR:
            handle_char(wparam);
            return 0;
        case WM_KEYDOWN:
            handle_key_down(wparam);
            return 0;
        case WM_TIMER:
            handle_timer(wparam);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kScriptTimerId);
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            {
                HWND destroyed = hwnd_;
                SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
                hwnd_ = nullptr;
                return DefWindowProcW(destroyed, message, wparam, lparam);
            }
        default:
            return DefWindowProcW(hwnd_, message, wparam, lparam);
        }
    }

    void resize_to_client() {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        viewport_width_ = std::max(1L, rect.right - rect.left);
        viewport_height_ = std::max(1L, rect.bottom - rect.top);
    }

    bool runtime_controls_page() const {
        return !options_.app_path.empty() || !options_.registry_store_path.empty();
    }

    bool accepts_ui_events() const {
        return !runtime_controls_page() ||
            app_frame_policy_for(app_runtime_.current(), debug_system_state_).accepts_input;
    }

    AppFramePolicy current_frame_policy() const {
        if (!runtime_controls_page()) {
            return AppFramePolicy{true, true, true, true, false};
        }
        return app_frame_policy_for(app_runtime_.current(), debug_system_state_);
    }

    AppServiceActivityPolicy current_service_activity_policy() const {
        if (!runtime_controls_page()) {
            return AppServiceActivityPolicy{true, true, true, false, false};
        }
        return app_service_activity_policy_for(app_runtime_.current(),
                                               debug_system_state_,
                                               background_service_policy_);
    }

    void record_frame_policy_sample() {
        const AppFramePolicy frame_policy = current_frame_policy();
        const AppServiceActivityPolicy service_policy = current_service_activity_policy();
        ++frame_policy_counters_.sampled_frames;
        frame_policy_counters_.accepts_input_frames += frame_policy.accepts_input ? 1 : 0;
        frame_policy_counters_.timer_frames += frame_policy.pumps_timers ? 1 : 0;
        frame_policy_counters_.animation_frames += frame_policy.pumps_animation ? 1 : 0;
        frame_policy_counters_.present_frames += frame_policy.presents_frames ? 1 : 0;
        frame_policy_counters_.network_active_frames += service_policy.network_fetch ? 1 : 0;
        frame_policy_counters_.audio_active_frames += service_policy.audio_playback ? 1 : 0;
        frame_policy_counters_.sensor_active_frames += service_policy.sensor_sampling ? 1 : 0;
        frame_policy_counters_.pause_audio_frames += service_policy.should_pause_audio ? 1 : 0;
        frame_policy_counters_.throttle_sensor_frames += service_policy.should_throttle_sensors ? 1 : 0;
    }

    FrameLoopOptions current_frame_loop_options() const {
        return apply_app_frame_policy(frame_loop_options_from_budgets(budgets_),
                                      current_frame_policy());
    }

    void reset_image_services() {
        image_cache_.release_all(app_runtime_, debug_images_);
        debug_images_.clear();
        add_debug_image_fixtures(debug_images_);
    }

    void cleanup_temp_audio_files() {
        for (const std::filesystem::path& path : temp_audio_files_) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        temp_audio_files_.clear();
    }

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
    bool pump_script_audio_events(std::uint64_t now_ms) {
        if (script_runtime_ == nullptr || pending_script_audio_events_.empty() ||
            script_runtime_instance_id_ != app_runtime_.current_app_instance_id()) {
            return false;
        }
        bool handled = false;
        for (PendingScriptAudioEvent& event : pending_script_audio_events_) {
            if (event.audio_id == 0 || event.due_ms > now_ms) {
                continue;
            }
            handled = script_runtime_->dispatch_audio_event(event.audio_id, event.kind) || handled;
            event.audio_id = 0;
        }
        pending_script_audio_events_.erase(
            std::remove_if(pending_script_audio_events_.begin(),
                           pending_script_audio_events_.end(),
                           [](const PendingScriptAudioEvent& event) {
                               return event.audio_id == 0;
                           }),
            pending_script_audio_events_.end());
        return handled;
    }

    static bool play_script_audio_callback(void* user,
                                           std::uint32_t audio_id,
                                           std::string_view src,
                                           double volume,
                                           std::string* error) {
        auto* app = static_cast<BrowserApp*>(user);
        return app != nullptr && app->play_script_audio(audio_id, src, volume, error);
    }

    bool play_script_audio(std::uint32_t audio_id, std::string_view src, double volume, std::string* error) {
        (void) volume;
        const std::string source(src);
        std::filesystem::path audio_path;
        bool temporary = false;
        std::string local_error;
        if (!resolve_script_audio_source(source, audio_path, temporary, local_error)) {
            if (error != nullptr) {
                *error = local_error;
            }
            return false;
        }
        std::uint32_t duration_ms = 0;
        const std::string extension = audio_path.extension().string();
        std::string lower_extension;
        lower_extension.reserve(extension.size());
        for (char ch : extension) {
            lower_extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        if (lower_extension == ".wav") {
            duration_ms = estimate_wav_duration_ms(audio_path);
        }
        if (temporary) {
            temp_audio_files_.push_back(audio_path);
        }
        if (!play_win32_audio_file_async(audio_path, local_error)) {
            if (error != nullptr) {
                *error = local_error;
            }
            return false;
        }
        pending_script_audio_events_.push_back(PendingScriptAudioEvent{
            audio_id,
            current_time_ms() + std::max<std::uint32_t>(duration_ms, 1000U),
            ScriptAudioEventKind::Ended,
        });
        return true;
    }

    bool resolve_script_audio_source(const std::string& source,
                                     std::filesystem::path& audio_path,
                                     bool& temporary,
                                     std::string& error) {
        temporary = false;
        if (source.empty()) {
            error = "empty audio source";
            return false;
        }
        if (!options_.app_path.empty()) {
            try {
                const auto package = jellyframe_example::load_app_package(options_.app_path, kMaxInputBytes);
                jellyframe_example::PackageResourceStats stats;
                jellyframe_example::PackageResourceContext context;
                context.root = package.root;
                context.base_url = package.manifest.entry;
                context.max_input_bytes = kMaxInputBytes;
                context.stats = &stats;
                context.bundle_bytes = package.bundle_bytes;
                context.bundle_entries = package.bundle_entries;
                context.bundle_payload_offset = package.bundle_payload_offset;
                std::string bytes;
                if (jellyframe_example::load_package_resource(source, package.manifest.entry, bytes, &context)) {
                    audio_path = write_temp_audio_resource(source, bytes);
                    temporary = true;
                    return true;
                }
            } catch (const std::exception& exception) {
                error = exception.what();
                return false;
            }
        }
        audio_path = std::filesystem::path(source);
        if (!std::filesystem::is_regular_file(audio_path)) {
            error = "audio source not found: " + source;
            return false;
        }
        return true;
    }
#endif

    bool drain_host_completions() {
        const AppCompletionPumpResult result = app_runtime_.pump_frame_completions(app_frame_scratch_);
        if (result.consumed == 0) {
            return false;
        }
        ++host_service_counters_.completion_batches;
        host_service_counters_.completions_consumed += result.consumed;
        host_service_counters_.completions_accepted += result.accepted;
        host_service_counters_.completions_stale += result.stale;
        host_service_counters_.released_stale_handles += result.released_stale_handles;
        std::size_t image_handled = 0;
        for (const HostServiceCompletion& completion : app_frame_scratch_.accepted_completions) {
            count_host_completion_kind(host_service_counters_, completion.kind);
            const std::string image_src = image_cache_.url_for_job(completion.job_id);
            report_image_completion_failure(&diagnostics_, completion, image_src);
            if (image_cache_.handle_completion(completion)) {
                ++image_handled;
                if (completion.kind == HostServiceJobKind::ImageDecode &&
                    completion.status != HostServiceStatus::Completed) {
                    report_image_cache_state(&diagnostics_, image_cache_, image_src);
                }
            } else if (completion.kind == HostServiceJobKind::ImageDecode && completion.handle != 0) {
                debug_images_.release_surface(app_runtime_, completion.handle);
            }
        }
        if (image_handled != 0 && document_ != nullptr) {
            mark_dirty(*document_, DomDirtyPaint);
        }
        host_service_counters_.image_handled += image_handled;
        std::size_t script_handled = 0;
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ != nullptr &&
            script_runtime_instance_id_ == app_runtime_.current_app_instance_id()) {
            for (const HostServiceCompletion& completion : app_frame_scratch_.accepted_completions) {
                if (script_runtime_->handle_host_completion(completion)) {
                    ++script_handled;
                }
            }
        }
#endif
        host_service_counters_.script_host_completions_handled += script_handled;
        std::cout << "host completions consumed=" << result.consumed
                  << " accepted=" << result.accepted
                  << " stale=" << result.stale
                  << " released_stale_handles=" << result.released_stale_handles
                  << " image_handled=" << image_handled
                  << " script_handled=" << script_handled << '\n';
        return script_handled != 0 || image_handled != 0;
    }

    bool drain_system_events() {
        std::vector<AppSystemEvent> accepted;
        const AppSystemEventPumpResult result = system_events_.pump_current(app_runtime_, accepted);
        if (result.consumed == 0) {
            return false;
        }
        ++host_service_counters_.system_event_batches;
        host_service_counters_.system_events_consumed += result.consumed;
        host_service_counters_.system_events_accepted += result.accepted;
        host_service_counters_.system_events_stale += result.stale;
        std::size_t script_handled = 0;
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ != nullptr &&
            script_runtime_instance_id_ == app_runtime_.current_app_instance_id()) {
            for (const AppSystemEvent& event : accepted) {
                if (script_runtime_->handle_system_event(event)) {
                    ++script_handled;
                }
            }
        }
#endif
        host_service_counters_.script_system_events_handled += script_handled;
        std::cout << "system events consumed=" << result.consumed
                  << " accepted=" << result.accepted
                  << " stale=" << result.stale
                  << " script_handled=" << script_handled << '\n';
        return script_handled != 0;
    }

    void queue_system_event(AppSystemEventKind kind, const char* status) {
        const AppSystemEventPushStatus pushed =
            system_events_.try_push_current(app_runtime_, kind, debug_system_state_);
        if (pushed != AppSystemEventPushStatus::Accepted) {
            report_diagnostic(&diagnostics_,
                              DiagnosticStage::Script,
                              DiagnosticSeverity::Warning,
                              "system-event-rejected",
                              "Host system event was rejected",
                              std::string("event=") + status +
                                  "; reason=" + app_system_event_push_status_name(pushed));
            set_title(std::string("system event rejected: ") + status);
            return;
        }
        if (drain_system_events() && current_frame_policy().presents_frames) {
            rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
        }
        set_title(std::string("system ") + status);
    }

    void evict_unused_image_surfaces() {
        AppImageSurfaceEvictionResult evicted;
        if (layer_tree_ == nullptr) {
            evicted = image_cache_.evict_unreferenced_with_result(app_runtime_, debug_images_);
        } else {
            std::vector<std::uint32_t> protected_handles;
            collect_image_handles(*layer_tree_, protected_handles);
            evicted = image_cache_.evict_unreferenced_with_result(app_runtime_,
                                                                  debug_images_,
                                                                  protected_handles.data(),
                                                                  protected_handles.size());
        }
        report_image_eviction_result(&diagnostics_, evicted);
    }

    void configure_system_shell(std::string status) {
        if (options_.registry_store_path.empty()) {
            return;
        }
        system_shell_mode_ = true;
        active_app_id_.clear();
        background_service_policy_ = AppBackgroundServicePolicy{};
        reset_image_services();
        app_runtime_.launch("org.jellyframe.system.launcher", AppRole::Launcher);
        options_.app_path.clear();
        options_.script_path.clear();
        try {
            options_.inline_html =
                build_system_shell_html(options_.launcher_app_path, options_.registry_store_path, status);
            options_.inline_css = load_system_shell_css(options_.launcher_app_path);
        } catch (const std::exception& error) {
            options_.inline_html = emergency_launcher_error_html(error.what());
            options_.inline_css = emergency_launcher_error_css();
        }
        scroll_y_ = 0;
    }

    void launch_installed_app(const std::string& app_id) {
        try {
            const std::filesystem::path bundle_path =
                jellyframe_example::find_installed_app_bundle_path(options_.registry_store_path, app_id);
            auto package = jellyframe_example::load_app_package(bundle_path, kMaxInputBytes);
            options_.app_path = bundle_path.string();
            options_.inline_html.clear();
            options_.inline_css.clear();
            active_app_id_ = app_id;
            background_service_policy_ = background_policy_from_manifest(package.manifest);
            reset_image_services();
            app_runtime_.launch(app_id, AppRole::App);
            system_shell_mode_ = false;
            scroll_y_ = 0;
            if (!options_.viewport_width_set && package.manifest.viewport_width > 0) {
                options_.viewport_width = package.manifest.viewport_width;
            }
            if (!options_.viewport_height_set && package.manifest.viewport_height > 0) {
                options_.viewport_height = package.manifest.viewport_height;
            }
            if (!rebuild()) {
                return;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            set_title("launched " + app_id);
        } catch (const std::exception& error) {
            configure_system_shell(std::string("Launch failed: ") + error.what());
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void delete_installed_app(const std::string& app_id) {
        try {
            if (!active_app_id_.empty() && active_app_id_ == app_id) {
                reset_image_services();
                app_runtime_.terminate_current(AppTeardownReason::UserKill);
                configure_system_shell("Cannot delete the active app; returned to shell first.");
            }
            const auto removed = jellyframe_example::remove_bundle_from_registry(options_.registry_store_path, app_id);
            configure_system_shell("Deleted " + removed.name + ".");
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        } catch (const std::exception& error) {
            configure_system_shell(std::string("Delete failed: ") + error.what());
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    bool process_shell_action_if_needed() {
        if (pending_shell_action_.empty() || pending_shell_app_id_.empty()) {
            return false;
        }
        const std::string action = std::move(pending_shell_action_);
        const std::string app_id = std::move(pending_shell_app_id_);
        pending_shell_action_.clear();
        pending_shell_app_id_.clear();
        if (action == "launch") {
            launch_installed_app(app_id);
        } else if (action == "delete") {
            delete_installed_app(app_id);
        }
        return true;
    }

    void recover_active_app_after_failure(const std::exception& error) {
        std::cerr << "rebuild failed: " << error.what() << '\n';
        if (!options_.registry_store_path.empty() && !system_shell_mode_) {
            reset_image_services();
            const AppTeardownResult teardown = app_runtime_.terminate_current(AppTeardownReason::LoadFailure);
            const std::string crashed_app = active_app_id_.empty() ? "app" : active_app_id_;
            configure_system_shell(
                "Recovered from " + crashed_app + " after " +
                app_teardown_reason_name(teardown.reason) + "; released instance " +
                std::to_string(teardown.app_instance_id) + ".");
            try {
                rebuild();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } catch (const std::exception& shell_error) {
                std::cerr << "system shell recovery failed: " << shell_error.what() << '\n';
                set_title(std::string("error: ") + shell_error.what());
            }
            return;
        }
        set_title(std::string("error: ") + error.what());
    }

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
    void recover_active_app_after_script_watchdog(const std::string& detail) {
        std::cerr << "script watchdog interrupted app: " << detail << '\n';
        if (!options_.registry_store_path.empty() && !system_shell_mode_) {
            reset_image_services();
            pending_script_audio_events_.clear();
            script_runtime_.reset();
            script_runtime_instance_id_ = 0;
            KillTimer(hwnd_, kScriptTimerId);
            const AppTeardownResult teardown = app_runtime_.terminate_current(AppTeardownReason::ScriptWatchdog);
            const std::string crashed_app = active_app_id_.empty() ? "app" : active_app_id_;
            configure_system_shell(
                "Recovered from " + crashed_app + " after " +
                app_teardown_reason_name(teardown.reason) + "; released instance " +
                std::to_string(teardown.app_instance_id) + ".");
            try {
                rebuild();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } catch (const std::exception& shell_error) {
                std::cerr << "system shell recovery failed: " << shell_error.what() << '\n';
                set_title(std::string("error: ") + shell_error.what());
            }
            return;
        }
        set_title("script watchdog: " + detail);
    }

    bool handle_script_evaluation_failure(const ScriptEvaluationResult& result,
                                          const char* title,
                                          const std::string& detail) {
        report_diagnostic(&diagnostics_,
                          DiagnosticStage::Script,
                          DiagnosticSeverity::Error,
                          result.status == ScriptEvaluationStatus::ExecutionBudgetExceeded
                              ? "script-execution-budget-exceeded"
                              : "script-evaluation-failed",
                          title,
                          detail + ": " + result.error);
        if (result.status == ScriptEvaluationStatus::ExecutionBudgetExceeded) {
            recover_active_app_after_script_watchdog(detail + ": " + result.error);
            return true;
        }
        std::cerr << "script failed: " << result.error << '\n';
        set_title("script error: " + result.error);
        return false;
    }
#endif

    bool rebuild() {
        try {
            diagnostics_.clear();
            reset_image_services();
            stop_win32_audio_playback();
            cleanup_temp_audio_files();
            KillTimer(hwnd_, kScriptTimerId);
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            pending_script_audio_events_.clear();
            script_runtime_.reset();
            script_runtime_instance_id_ = 0;
#endif
            LoadedPage page = load_page(options_, budgets_, &diagnostics_, &app_runtime_);
            background_service_policy_ = page.package_mode
                ? background_policy_from_manifest(page.package_manifest)
                : AppBackgroundServicePolicy{};
            if (page.package_mode) {
                add_package_image_fixtures(*page.document, page.package_context, debug_images_, &diagnostics_);
            }
            document_ = std::move(page.document);
            StyleResolverOptions style_options;
            style_options.diagnostics = &diagnostics_;
            style_resolver_ = std::make_unique<StyleResolver>(std::move(page.stylesheet), style_options);
            page_background_ = page_background_color(*document_, *style_resolver_);
            render_tree_.reset();
            layout_tree_.reset();
            layer_tree_.reset();
            input_.reset();
            animation_timeline_ = AnimationTimeline(AnimationTimelineOptions{budgets_.max_active_animations, &diagnostics_});
            style_overrides_.clear();
            previous_style_overrides_.clear();
            clear_animation_overrides_after_render_ = false;
            pending_shell_action_.clear();
            pending_shell_app_id_.clear();
            dirty_region_statistics_ = DirtyRegionStatistics{};

            if (system_shell_mode_) {
                document_->add_event_listener("click", [this](Event& event) {
                    const Node* action_node = find_shell_action_node(event.target());
                    if (action_node == nullptr) {
                        return;
                    }
                    pending_shell_action_ = action_node->attribute("data-action");
                    pending_shell_app_id_ = action_node->attribute("data-app-id");
                    event.prevent_default();
                });
            } else {
                document_->add_event_listener("click", [this](Event& event) {
                    std::cout << "click target=" << describe_node(event.target()) << '\n';
                    set_title("clicked " + describe_node(event.target()));
                });
            }

#if defined(JELLYFRAME_ENABLE_SCRIPTING)
            jellyframe_example::ScriptLoadContext script_context;
            script_context.base_dir = page.script_base_dir.empty() ? std::filesystem::current_path() : page.script_base_dir;
            script_context.max_input_bytes = kMaxInputBytes;
            script_context.diagnostics = &diagnostics_;
            std::vector<DocumentScript> document_scripts =
                page.package_mode
                    ? collect_classic_scripts(
                          *document_, jellyframe_example::load_package_script, &page.package_context, &diagnostics_)
                    : collect_classic_scripts(
                          *document_, jellyframe_example::load_linked_script, &script_context, &diagnostics_);
            if (!document_scripts.empty() || !options_.script_path.empty()) {
                script_runtime_ = std::make_unique<JerryScriptRuntime>(budgets_);
                script_runtime_instance_id_ = app_runtime_.current_app_instance_id();
                if (debug_local_storage_instance_id_ != script_runtime_instance_id_) {
                    debug_local_storage_.clear();
                    debug_local_storage_instance_id_ = script_runtime_instance_id_;
                }
                script_runtime_->bind_app_services(app_runtime_, debug_network_);
                script_runtime_->bind_local_storage(debug_local_storage_);
                script_runtime_->bind_audio_host(ScriptAudioHost{play_script_audio_callback, this});
                script_runtime_->set_system_state(ScriptSystemState{
                    !debug_system_state_.screen_on || debug_system_state_.low_power_mode,
                    debug_system_state_.network_online,
                });
                script_runtime_->set_host_time_ms(current_time_ms());
                script_runtime_->bind_document(*document_);
                for (const DocumentScript& script : document_scripts) {
                    const ScriptEvaluationResult result = script_runtime_->eval(script.source, script.name);
                    if (!result.ok) {
                        if (handle_script_evaluation_failure(result,
                                                             "Document script evaluation failed",
                                                             script.name)) {
                            return false;
                        }
                        break;
                    }
                }
                if (!options_.script_path.empty()) {
                    const ScriptEvaluationResult result =
                        script_runtime_->eval(read_file_limited(options_.script_path), options_.script_path);
                    if (!result.ok) {
                        if (handle_script_evaluation_failure(result,
                                                             "Standalone script evaluation failed",
                                                             options_.script_path)) {
                            return false;
                        }
                    }
                }
            }
#endif
            render_current(nullptr, nullptr, nullptr);
            SetTimer(hwnd_, kScriptTimerId, kScriptTimerPeriodMs, nullptr);
            print_diagnostics(diagnostics_);
            return true;
        } catch (const std::exception& error) {
            recover_active_app_after_failure(error);
            return false;
        }
    }

    void render_current(const Node* hovered_node, const Node* active_node, const Node* focused_node) {
        if (document_ == nullptr || style_resolver_ == nullptr) {
            return;
        }
        drain_host_completions();
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ != nullptr && script_runtime_->take_execution_watchdog_interrupt()) {
            recover_active_app_after_script_watchdog("script host-completion execution budget exceeded");
            return;
        }
#endif
        drain_system_events();
        std::vector<std::pair<const Node*, Style>> previous_styles;
        if ((document_->dirty_flags & DomDirtyStyle) != 0U && render_tree_ != nullptr) {
            collect_transition_candidate_styles(*render_tree_, previous_styles);
        }
        style_resolver_->set_interaction_state(hovered_node, active_node, focused_node);
        const DomDirtyFlags dirty_flags = document_->dirty_flags;
        const int current_content_height = layout_tree_ != nullptr
            ? std::max(viewport_height_, layout_tree_->rect.height)
            : frame_buffer_.height;
        FramePipelineCacheState cache_state;
        cache_state.has_render_tree = render_tree_ != nullptr;
        cache_state.has_layout_tree = layout_tree_ != nullptr;
        cache_state.has_layer_tree = layer_tree_ != nullptr;
        cache_state.has_framebuffer = frame_buffer_.width > 0 && frame_buffer_.height > 0;
        cache_state.framebuffer_width = frame_buffer_.width;
        cache_state.framebuffer_height = frame_buffer_.height;
        cache_state.viewport = Rect{0, 0, viewport_width_, viewport_height_};
        cache_state.content_height = current_content_height;
        const FrameUpdateState update_state = make_frame_update_state(dirty_flags, cache_state);
        const FrameUpdatePlan update_plan = plan_frame_update(update_state);
        if (update_plan.action == FrameUpdateAction::None) {
            record_dirty_region(DirtyRegionResult{});
            return;
        }
        frame_scratch_.begin_frame();

        LayerTreeBuilderOptions layer_options = layer_tree_options_from_budgets(budgets_);
        layer_options.diagnostics = &diagnostics_;
        BrowserImageResolveContext image_resolve_context{&app_runtime_, &debug_images_, &image_cache_, &diagnostics_};
        layer_options.image_resolver = ImageHandleResolver{resolve_browser_image_handle, &image_resolve_context};
        LayerTreeBuilder layer_builder(layer_options);
        SoftwareCompositor::Options compositor_options = software_compositor_options_from_budgets(budgets_);
        compositor_options.diagnostics = &diagnostics_;
        BrowserTextBackend text_backend = make_browser_text_backend(options_, &app_runtime_);
        SoftwareCompositor compositor(text_backend.painter,
                                      ImagePainter{paint_image_surface, &image_context_},
                                      compositor_options);
        if (update_plan.action == FrameUpdateAction::RepaintExisting &&
            update_plan.dirty_rect_mode == FrameDirtyRectMode::CurrentLayout &&
            layout_tree_ != nullptr) {
            const int content_height = std::max(viewport_height_, layout_tree_->rect.height);
            const bool animation_only_dirty =
                !style_overrides_.empty() && dirty_flags == DomDirtyPaint;
            if (animation_only_dirty) {
                compute_animation_dirty_region_into(
                    *layout_tree_,
                    previous_style_overrides_,
                    style_overrides_,
                    AnimationInvalidationOptions{Rect{0, 0, viewport_width_, content_height}, budgets_.max_dirty_rects, 3},
                    frame_scratch_.dirty_region);
            }
            apply_animation_overrides_to_cached_trees();
            auto next_layer_tree = layer_builder.build(*layout_tree_);
            if (!animation_only_dirty) {
                compute_dirty_region_into(
                    *document_,
                    layout_tree_.get(),
                    layout_tree_.get(),
                    dirty_region_options_from_budgets(budgets_, Rect{0, 0, viewport_width_, content_height}, 3),
                    frame_scratch_.dirty_region,
                    &frame_scratch_.dirty_region_scratch);
            }
            const DirtyRegionResult& dirty_region = frame_scratch_.dirty_region;
            const std::vector<Rect>& dirty_rects = dirty_region.rects;
            layer_tree_ = std::move(next_layer_tree);
            evict_unused_image_surfaces();
            if (!dirty_rects.empty() &&
                dirty_region_should_repaint_incrementally(dirty_region,
                                                          Rect{0, 0, viewport_width_, content_height},
                                                          kIncrementalDirtyAreaLimitPercent)) {
                compositor.render_into(*layer_tree_,
                                       frame_buffer_,
                                       page_background_,
                                       dirty_rects.data(),
                                       dirty_rects.size());
                last_display_invalidation_ =
                    analyze_display_invalidation(*layer_tree_, dirty_rects.data(), dirty_rects.size());
                record_dirty_region(dirty_region);
            } else {
                render_full_frame(compositor, dirty_region, dirty_rects.empty(), content_height);
            }
            input_ = std::make_unique<InputController>(
                *layer_tree_,
                input_invalidation_options_from_style(*style_resolver_));
            input_->set_interaction_state(hovered_node, active_node, focused_node);
            update_blit_pixels();
            clear_dirty_flags(*document_);
            clear_finished_animation_overrides();
            return;
        }

        const DomDirtyFlags rebuild_dirty_flags = document_->dirty_flags;
        LayoutBoxPtr previous_layout = update_plan.needs_previous_layout ? std::move(layout_tree_) : LayoutBoxPtr{};
        RenderTreeOptions render_options = render_tree_options_from_budgets(budgets_);
        render_options.diagnostics = &diagnostics_;
        RenderTreeBuilder render_builder(*style_resolver_, render_options);
        auto target_render_tree = render_builder.build(*document_);
        const std::uint64_t now_ms = current_time_ms();
        bool animation_started = false;
        if (!previous_styles.empty()) {
            animation_started =
                start_transitions_from_previous_styles(previous_styles, *target_render_tree, now_ms);
        }
        std::vector<KeyframeAnimationKey> live_keyframe_animations;
        animation_started =
            ensure_keyframe_animations_for_render_tree(*target_render_tree, now_ms, live_keyframe_animations) ||
            animation_started;
        animation_timeline_.retain_keyframe_animations(live_keyframe_animations);
        if (animation_started || !animation_timeline_.empty()) {
            animation_timeline_.sample(now_ms, style_overrides_);
            render_options.style_overrides = style_overrides_.empty() ? nullptr : &style_overrides_;
        }
        RenderTreeBuilder sampled_render_builder(*style_resolver_, render_options);
        auto next_render_tree = sampled_render_builder.build(*document_);
        LayoutEngineOptions layout_options = layout_engine_options_from_budgets(budgets_);
        layout_options.diagnostics = &diagnostics_;
        LayoutEngine layout_engine(*style_resolver_, text_backend.measure, layout_options);
        auto next_layout_tree = layout_engine.layout(*next_render_tree, viewport_width_);
        auto next_layer_tree = layer_builder.build(*next_layout_tree);

        const int content_height = std::max(viewport_height_, next_layout_tree->rect.height);
        const FrameRepaintPlan repaint_plan = plan_frame_repaint(update_state, update_plan, content_height);
        scroll_y_ = std::max(0, std::min(scroll_y_, std::max(0, content_height - viewport_height_)));
        DirtyRegionResult& dirty_region = frame_scratch_.dirty_region;
        const bool can_repaint_incrementally = repaint_plan.can_repaint_dirty_rects &&
            repaint_plan.dirty_rect_mode == FrameDirtyRectMode::PreviousAndCurrentLayout &&
            previous_layout != nullptr;
        if (can_repaint_incrementally && rebuild_dirty_flags != DomDirtyNone) {
            compute_dirty_region_into(*document_,
                                      previous_layout.get(),
                                      next_layout_tree.get(),
                                      dirty_region_options_from_budgets(
                                          budgets_, Rect{0, 0, viewport_width_, content_height}, 3),
                                      dirty_region,
                                      &frame_scratch_.dirty_region_scratch);
        }
        const std::vector<Rect>& dirty_rects = dirty_region.rects;

        render_tree_ = std::move(next_render_tree);
        layout_tree_ = std::move(next_layout_tree);
        layer_tree_ = std::move(next_layer_tree);
        evict_unused_image_surfaces();

        if (can_repaint_incrementally && !dirty_rects.empty() &&
            dirty_region_should_repaint_incrementally(dirty_region,
                                                      Rect{0, 0, viewport_width_, content_height},
                                                      kIncrementalDirtyAreaLimitPercent)) {
            compositor.render_into(*layer_tree_,
                                   frame_buffer_,
                                   page_background_,
                                   dirty_rects.data(),
                                   dirty_rects.size());
            last_display_invalidation_ =
                analyze_display_invalidation(*layer_tree_, dirty_rects.data(), dirty_rects.size());
            record_dirty_region(dirty_region);
        } else {
            render_full_frame(compositor, dirty_region, dirty_rects.empty(), content_height);
        }
        input_ = std::make_unique<InputController>(
            *layer_tree_,
            input_invalidation_options_from_style(*style_resolver_));
        input_->set_interaction_state(hovered_node, active_node, focused_node);
        update_blit_pixels();
        clear_dirty_flags(*document_);
        clear_finished_animation_overrides();
    }

    void collect_transition_candidate_styles(const RenderObject& object,
                                             std::vector<std::pair<const Node*, Style>>& output) const {
        if (object.node != nullptr && object.style.transition_count != 0) {
            output.push_back({object.node, object.style});
        }
        for (const auto& child : object.children) {
            collect_transition_candidate_styles(*child, output);
        }
    }

    bool start_transitions_from_previous_styles(const std::vector<std::pair<const Node*, Style>>& previous_styles,
                                                const RenderObject& object,
                                                std::uint64_t now_ms) {
        bool started = false;
        if (object.node != nullptr && object.style.transition_count != 0) {
            for (const auto& entry : previous_styles) {
                if (entry.first == object.node) {
                    started = animation_timeline_.start_transitions(*object.node, entry.second, object.style, now_ms) ||
                        started;
                    break;
                }
            }
        }
        for (const auto& child : object.children) {
            started = start_transitions_from_previous_styles(previous_styles, *child, now_ms) || started;
        }
        return started;
    }

    bool ensure_keyframe_animations_for_render_tree(const RenderObject& object,
                                                    std::uint64_t now_ms,
                                                    std::vector<KeyframeAnimationKey>& live_keys) {
        bool started = false;
        if (object.node != nullptr && object.style.animation_count != 0 && style_resolver_ != nullptr) {
            for (std::size_t index = 0; index < object.style.animation_count; ++index) {
                const StyleAnimation& animation = object.style.animations[index];
                if (animation.name.empty() || animation.duration_ms == 0) {
                    continue;
                }
                const CssKeyframesRule* keyframes = style_resolver_->keyframes(animation.name);
                if (keyframes == nullptr) {
                    report_diagnostic(&diagnostics_,
                                      DiagnosticStage::Style,
                                      DiagnosticSeverity::Warning,
                                      "animation-keyframes-missing",
                                      "CSS animation referenced a missing @keyframes rule",
                                      animation.name);
                    continue;
                }
                live_keys.push_back(KeyframeAnimationKey{object.node, animation.name});
                started = animation_timeline_.ensure_keyframe_animation(
                    *object.node, object.style, animation, *keyframes, now_ms) || started;
            }
        }
        for (const auto& child : object.children) {
            started = ensure_keyframe_animations_for_render_tree(*child, now_ms, live_keys) || started;
        }
        return started;
    }

    void apply_override_to_style(Style& style, const StyleOverride& override) const {
        if (override.has_opacity) {
            style.opacity = override.opacity;
        }
        if (override.has_color) {
            style.color = override.color;
            style.color_specified = true;
        }
        if (override.has_background_color) {
            style.background_color = override.background_color;
        }
        if (override.has_transform) {
            style.transform = override.transform;
        }
    }

    void apply_animation_overrides(RenderObject& object) const {
        if (object.node != nullptr) {
            for (const StyleOverride& override : style_overrides_) {
                if (override.node == object.node) {
                    apply_override_to_style(object.style, override);
                    break;
                }
            }
        }
        for (const auto& child : object.children) {
            apply_animation_overrides(*child);
        }
    }

    void apply_animation_overrides(LayoutBox& box) const {
        if (box.node != nullptr) {
            for (const StyleOverride& override : style_overrides_) {
                if (override.node == box.node) {
                    apply_override_to_style(box.style, override);
                    break;
                }
            }
        }
        for (const auto& child : box.children) {
            apply_animation_overrides(*child);
        }
    }

    void apply_animation_overrides_to_cached_trees() {
        if (style_overrides_.empty()) {
            return;
        }
        previous_style_overrides_ = style_overrides_;
        if (render_tree_ != nullptr) {
            apply_animation_overrides(*render_tree_);
        }
        if (layout_tree_ != nullptr) {
            apply_animation_overrides(*layout_tree_);
        }
    }

    int max_scroll_y() const {
        return std::max(0, frame_buffer_.height - viewport_height_);
    }

    void render_full_frame(SoftwareCompositor& compositor,
                           const DirtyRegionResult& attempted_region,
                           bool had_no_dirty_rects,
                           int content_height) {
        frame_buffer_ = compositor.render(*layer_tree_, viewport_width_, content_height, page_background_);
        DirtyRegionResult full_region;
        full_region.mode = DirtyRegionMode::FullFrame;
        full_region.fallback_reason = had_no_dirty_rects
            ? attempted_region.fallback_reason
            : DirtyRegionFallbackReason::DirtyAreaTooLarge;
        full_region.rects.push_back(Rect{0, 0, viewport_width_, content_height});
        last_display_invalidation_ =
            analyze_display_invalidation(*layer_tree_, full_region.rects.data(), full_region.rects.size());
        record_dirty_region(full_region);
    }

    int clamp_scroll_y(int value) const {
        const int content_height = layout_tree_ != nullptr
            ? std::max(viewport_height_, layout_tree_->rect.height)
            : frame_buffer_.height;
        return std::max(0, std::min(value, std::max(0, content_height - viewport_height_)));
    }

    bool scroll_by(int delta_y) {
        const int previous = scroll_y_;
        scroll_y_ = std::max(0, std::min(scroll_y_ + delta_y, max_scroll_y()));
        if (scroll_y_ == previous) {
            return false;
        }
        update_blit_pixels();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    const LayoutBox* find_layout_by_id(const LayoutBox& box, const std::string& id) const {
        if (box.node != nullptr && box.node->attribute("id") == id) {
            return &box;
        }
        for (const auto& child : box.children) {
            const LayoutBox* found = find_layout_by_id(*child, id);
            if (found != nullptr) {
                return found;
            }
        }
        return nullptr;
    }

    bool scroll_to_y(int y) {
        const int previous = scroll_y_;
        scroll_y_ = clamp_scroll_y(y);
        if (scroll_y_ == previous) {
            return false;
        }
        update_blit_pixels();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return true;
    }

    bool follow_hash_anchor(const Node* node) {
        if (node == nullptr || layout_tree_ == nullptr || node->type != NodeType::Element ||
            node->tag_name != "a") {
            return false;
        }
        const std::string& href = node->attribute("href");
        if (href.size() <= 1 || href.front() != '#') {
            return false;
        }
        const LayoutBox* target = find_layout_by_id(*layout_tree_, href.substr(1));
        return target != nullptr && scroll_to_y(target->rect.y);
    }

    void update_blit_pixels() {
        blit_pixels_.assign(static_cast<std::size_t>(viewport_width_) * static_cast<std::size_t>(viewport_height_),
                            color_to_bgrx(page_background_));
        if (frame_buffer_.width <= 0 || frame_buffer_.height <= 0) {
            return;
        }
        scroll_y_ = std::max(0, std::min(scroll_y_, max_scroll_y()));
        const int copy_width = std::min(viewport_width_, frame_buffer_.width);
        const int copy_height = std::min(viewport_height_, frame_buffer_.height - scroll_y_);
        for (int y = 0; y < copy_height; ++y) {
            for (int x = 0; x < copy_width; ++x) {
                blit_pixels_[static_cast<std::size_t>(y * viewport_width_ + x)] =
                    color_to_bgrx(frame_buffer_.pixel(x, scroll_y_ + y));
            }
        }
    }

    void paint() {
        PAINTSTRUCT paint_struct{};
        HDC dc = BeginPaint(hwnd_, &paint_struct);
        if (!blit_pixels_.empty() && viewport_width_ > 0 && viewport_height_ > 0) {
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = viewport_width_;
            info.bmiHeader.biHeight = -viewport_height_;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetDIBitsToDevice(dc,
                              0,
                              0,
                              static_cast<DWORD>(viewport_width_),
                              static_cast<DWORD>(viewport_height_),
                              0,
                              0,
                              0,
                              static_cast<UINT>(viewport_height_),
                              blit_pixels_.data(),
                              &info,
                              DIB_RGB_COLORS);
        }
        EndPaint(hwnd_, &paint_struct);
    }

    void handle_pointer_move(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.buttons = buttons_from_keys(wparam);
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_move(input);
        rerender_if_dirty(input_->focused_node());
        set_title("hover " + describe_node(target));
    }

    void handle_pointer_down(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.button = PointerButton::Primary;
        input.buttons = buttons_from_keys(wparam) | 1;
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_down(input);
        rerender_if_dirty(input_->focused_node());
        set_title("active " + describe_node(target));
    }

    void handle_pointer_up(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        PointerInput input;
        input.x = GET_X_LPARAM(lparam);
        input.y = GET_Y_LPARAM(lparam) + scroll_y_;
        input.button = PointerButton::Primary;
        input.buttons = buttons_from_keys(wparam) & ~1;
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->pointer_up(input);
        rerender_if_dirty(input_->focused_node());
        follow_hash_anchor(target);
        if (process_shell_action_if_needed()) {
            return;
        }
        set_title("up " + describe_node(target));
    }

    void handle_wheel(WPARAM wparam, LPARAM lparam) {
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);

        WheelInput input;
        input.x = point.x;
        input.y = point.y + scroll_y_;
        input.delta_y = GET_WHEEL_DELTA_WPARAM(wparam);
        input.modifiers = modifiers_from_keys(wparam);
        const Node* target = input_->wheel(input);
        rerender_if_dirty(input_->focused_node());
        const int scroll_delta = -input.delta_y;
        scroll_by(scroll_delta);
        set_title("wheel " + describe_node(target) + " scrollY=" + std::to_string(scroll_y_));
    }

    void handle_char(WPARAM wparam) {
        if (!input_ || !accepts_ui_events() || wparam < 0x20 || wparam == 0x7f) {
            return;
        }
        const Node* focus = input_->focused_node();
        if (input_->text_input(wide_char_to_utf8(static_cast<wchar_t>(wparam)))) {
            rerender_if_dirty(focus);
        }
    }

    void handle_key_down(WPARAM wparam) {
        if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            if (wparam == VK_F6) {
                debug_system_state_.network_online = !debug_system_state_.network_online;
                queue_system_event(AppSystemEventKind::NetworkStatusChanged,
                                   debug_system_state_.network_online ? "network online" : "network offline");
                return;
            }
            if (wparam == VK_F7) {
                debug_system_state_.screen_on = !debug_system_state_.screen_on;
                queue_system_event(AppSystemEventKind::ScreenStateChanged,
                                   debug_system_state_.screen_on ? "screen visible" : "screen hidden");
                return;
            }
            if (wparam == VK_F8) {
                debug_system_state_.low_power_mode = !debug_system_state_.low_power_mode;
                queue_system_event(AppSystemEventKind::LowPowerModeChanged,
                                   debug_system_state_.low_power_mode ? "low power on" : "low power off");
                return;
            }
        }
        if (!input_ || !accepts_ui_events()) {
            return;
        }
        KeyInput key;
        if (wparam == VK_BACK) {
            key.code = KeyCode::Backspace;
        } else if (wparam == VK_ESCAPE && !options_.registry_store_path.empty() && !system_shell_mode_) {
            configure_system_shell("Returned from " + active_app_id_ + ".");
            rebuild();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return;
        } else if (wparam == VK_RETURN) {
            key.code = KeyCode::Enter;
        } else if (wparam == VK_SPACE) {
            key.code = KeyCode::Space;
        } else if (wparam == VK_TAB) {
            key.code = KeyCode::Tab;
        } else if (wparam == VK_UP) {
            key.code = KeyCode::ArrowUp;
        } else if (wparam == VK_DOWN) {
            key.code = KeyCode::ArrowDown;
        } else {
            return;
        }
        const Node* focus = input_->focused_node();
        if (input_->key_down(key)) {
            rerender_if_dirty(focus);
        }
    }

    void handle_timer(WPARAM timer_id) {
        if (timer_id != kScriptTimerId) {
            return;
        }
        const AppFramePolicy frame_policy = current_frame_policy();
        const FrameLoopOptions frame_options = current_frame_loop_options();
        ImageDecodeMockWorker image_worker(app_runtime_, debug_images_);
        AppHostServiceWorkerSlot image_slot[] = {
            AppHostServiceWorkerSlot{HostServiceJobKind::ImageDecode, 1, &image_worker},
        };
        const AppHostServiceWorkerGroupPumpResult image_pump =
            pump_app_host_service_workers(app_runtime_, image_slot, 1);
        const bool completed_image = image_pump.completions_posted != 0;
        bool handled_completion = drain_host_completions();
        const bool handled_system_event = drain_system_events();
        const bool animation_budget_enabled = frame_options.animation_frame_rate > 0 &&
            frame_options.max_animation_callbacks_per_frame > 0;
        const bool advanced_animation = frame_policy.presents_frames &&
            animation_budget_enabled && advance_animation_timeline(current_time_ms());
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ == nullptr || !frame_policy.pumps_timers ||
            script_runtime_instance_id_ != app_runtime_.current_app_instance_id()) {
            if (frame_policy.presents_frames &&
                (completed_image || handled_completion || handled_system_event || advanced_animation)) {
                rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
            }
            return;
        }
        NetworkFetchMockWorker network_worker(app_runtime_, debug_network_);
        AppHostServiceWorkerSlot network_slot[] = {
            AppHostServiceWorkerSlot{HostServiceJobKind::NetworkFetch, 1, &network_worker},
        };
        const AppHostServiceWorkerGroupPumpResult network_pump =
            pump_app_host_service_workers(app_runtime_, network_slot, 1);
        const bool completed_network = network_pump.completions_posted != 0;
        handled_completion = drain_host_completions() || handled_completion;
        if (script_runtime_ != nullptr && script_runtime_->take_execution_watchdog_interrupt()) {
            recover_active_app_after_script_watchdog("script host-completion execution budget exceeded");
            return;
        }
        const std::uint64_t now_ms = current_time_ms();
        const std::size_t callbacks =
            script_runtime_->pump_timers(now_ms, frame_options.max_timer_callbacks_per_frame);
        const bool audio_event_handled = pump_script_audio_events(now_ms);
        const std::size_t animation_callbacks = animation_budget_enabled
            ? script_runtime_->pump_animation_frame(now_ms, frame_options.max_animation_callbacks_per_frame)
            : 0;
        if (script_runtime_ != nullptr && script_runtime_->take_execution_watchdog_interrupt()) {
            recover_active_app_after_script_watchdog("script callback execution budget exceeded");
            return;
        }
        if (frame_policy.presents_frames &&
            (callbacks != 0 || animation_callbacks != 0 || audio_event_handled || completed_network || advanced_animation ||
             completed_image || handled_completion || handled_system_event)) {
            rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
        }
#else
        if (frame_policy.presents_frames &&
            (completed_image || handled_completion || handled_system_event || advanced_animation)) {
            rerender_if_dirty(input_ ? input_->focused_node() : nullptr);
        }
#endif
    }

    bool advance_animation_timeline(std::uint64_t now_ms) {
        if (document_ == nullptr || animation_timeline_.empty()) {
            return false;
        }
        previous_style_overrides_ = style_overrides_;
        if (!animation_timeline_.sample(now_ms, style_overrides_)) {
            return false;
        }
        clear_animation_overrides_after_render_ = animation_timeline_.empty();
        mark_dirty(*document_, DomDirtyPaint);
        return true;
    }

    void clear_finished_animation_overrides() {
        if (!clear_animation_overrides_after_render_) {
            return;
        }
        style_overrides_.clear();
        previous_style_overrides_.clear();
        clear_animation_overrides_after_render_ = false;
    }

    void rerender_if_dirty(const Node* focused_node) {
#if defined(JELLYFRAME_ENABLE_SCRIPTING)
        if (script_runtime_ != nullptr && script_runtime_->take_execution_watchdog_interrupt()) {
            recover_active_app_after_script_watchdog("script event execution budget exceeded");
            return;
        }
#endif
        if (document_ == nullptr || subtree_dirty_flags(*document_) == DomDirtyNone) {
            return;
        }
        const Node* hovered_node = input_ != nullptr ? input_->hovered_node() : nullptr;
        const Node* active_node = input_ != nullptr ? input_->active_node() : nullptr;
        render_current(hovered_node, active_node, focused_node);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void record_dirty_region(const DirtyRegionResult& region) {
        last_dirty_region_mode_ = region.mode;
        last_dirty_region_reason_ = region.fallback_reason;
        last_dirty_rect_count_ = region.rects.size();
        last_dirty_area_percent_ =
            dirty_region_area_percent(region, Rect{0, 0, frame_buffer_.width, frame_buffer_.height});
        if (region.rects.empty()) {
            last_display_invalidation_ = DisplayInvalidationResult{};
        }
        record_dirty_region_result(dirty_region_statistics_, region);
        if (hwnd_ != nullptr) {
            std::ostringstream status;
            status << "dirty=" << dirty_region_mode_name(last_dirty_region_mode_)
                   << " rects=" << last_dirty_rect_count_
                   << " area=" << last_dirty_area_percent_ << "%"
                   << " cmds=" << last_display_invalidation_.commands_intersecting
                   << "/" << last_display_invalidation_.commands_visited
                   << " local=" << dirty_region_statistics_.dirty_rect_frames
                   << " full=" << dirty_region_statistics_.full_frame_frames
                   << " clean=" << dirty_region_statistics_.clean_frames;
            if (last_dirty_region_reason_ != DirtyRegionFallbackReason::None) {
                status << " reason=" << dirty_region_fallback_reason_name(last_dirty_region_reason_);
            }
            set_title(status.str());
        }
    }

    void set_title(const std::string& status) {
        SetWindowTextW(hwnd_, utf8_to_wide("JellyFrame Win32 Browser - " + status).c_str());
    }
};

} // namespace

int main(int argc, char** argv) {
    BrowserOptions options;
    options.html_path = "src/render_core/samples/pages/modern/app_shell.html";
    options.css_path = "src/render_core/samples/pages/modern/app_shell.css";
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_win32_browser_usage(std::cout);
            return 0;
        }
        if (arg == "--capture") {
            options.capture = true;
            if (i + 1 >= argc) {
                std::cerr << "--capture requires an output file path\n";
                return 1;
            }
            options.output_path = argv[++i];
            continue;
        }
        if (arg == "--capture-frames") {
            options.capture_frames = true;
            if (i + 1 >= argc) {
                std::cerr << "--capture-frames requires an output directory\n";
                return 1;
            }
            options.frame_output_dir = argv[++i];
            continue;
        }
        if (arg == "--frame-script") {
            if (i + 1 >= argc) {
                std::cerr << "--frame-script requires a script file path\n";
                return 1;
            }
            options.frame_script_path = argv[++i];
            std::string error;
            if (!apply_frame_script(options, options.frame_script_path, error)) {
                std::cerr << error << '\n';
                return 1;
            }
            continue;
        }
        if (arg == "--capture-montage") {
            options.capture_frames = true;
            if (i + 1 >= argc) {
                std::cerr << "--capture-montage requires an output image path\n";
                return 1;
            }
            options.frame_montage_path = argv[++i];
            continue;
        }
        if (arg == "--montage-columns") {
            if (i + 1 >= argc) {
                std::cerr << "--montage-columns requires a number\n";
                return 1;
            }
            options.frame_montage_columns = std::min(30, parse_int_arg(argv[++i], options.frame_montage_columns));
            continue;
        }
        if (arg == "--frame-count") {
            if (i + 1 >= argc) {
                std::cerr << "--frame-count requires a number\n";
                return 1;
            }
            options.frame_count = std::min(600, parse_int_arg(argv[++i], options.frame_count));
            continue;
        }
        if (arg == "--frame-step-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--frame-step-ms requires a number\n";
                return 1;
            }
            options.frame_step_ms =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(1000, parse_u64_arg(argv[++i], options.frame_step_ms)));
            continue;
        }
        if (arg == "--frame-start-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--frame-start-ms requires a number\n";
                return 1;
            }
            options.frame_start_ms = parse_u64_arg(argv[++i], options.frame_start_ms);
            continue;
        }
        if (arg == "--animation-fps") {
            if (i + 1 >= argc) {
                std::cerr << "--animation-fps requires a number\n";
                return 1;
            }
            options.animation_frame_rate =
                std::min(240, std::max(0, parse_int_unclamped(argv[++i], options.animation_frame_rate)));
            continue;
        }
        if (arg == "--animation-callbacks") {
            if (i + 1 >= argc) {
                std::cerr << "--animation-callbacks requires a number\n";
                return 1;
            }
            options.animation_callbacks_per_frame =
                std::min(1024, std::max(0, parse_int_unclamped(argv[++i], options.animation_callbacks_per_frame)));
            continue;
        }
        if (arg == "--frame-event") {
            if (i + 1 >= argc) {
                std::cerr << "--frame-event requires FRAME:kind[:x:y]\n";
                return 1;
            }
            ParsedFrameEvent parsed = parse_frame_event(argv[++i]);
            if (!parsed.ok) {
                std::cerr << "invalid --frame-event: " << parsed.error << '\n';
                return 1;
            }
            options.frame_events.push_back(parsed.event);
            continue;
        }
        if (arg == "--app") {
            if (i + 1 >= argc) {
                std::cerr << "--app requires a package directory\n";
                return 1;
            }
            options.app_path = argv[++i];
            continue;
        }
        if (arg == "--registry-store") {
            if (i + 1 >= argc) {
                std::cerr << "--registry-store requires a directory\n";
                return 1;
            }
            options.registry_store_path = argv[++i];
            continue;
        }
        if (arg == "--launcher-app") {
            if (i + 1 >= argc) {
                std::cerr << "--launcher-app requires a JellyFrame app package directory or .jfapp file\n";
                return 1;
            }
            options.launcher_app_path = argv[++i];
            continue;
        }
        if (arg == "--install-bundle") {
            if (i + 1 >= argc) {
                std::cerr << "--install-bundle requires a .jfapp file\n";
                return 1;
            }
            options.install_bundle_path = argv[++i];
            continue;
        }
        if (arg == "--launch-app") {
            if (i + 1 >= argc) {
                std::cerr << "--launch-app requires an installed app id\n";
                return 1;
            }
            options.launch_app_id = argv[++i];
            continue;
        }
        if (arg == "--remove-app") {
            if (i + 1 >= argc) {
                std::cerr << "--remove-app requires an installed app id\n";
                return 1;
            }
            options.remove_app_id = argv[++i];
            continue;
        }
        if (arg == "--script" || arg == "-s") {
            if (i + 1 >= argc) {
                std::cerr << "--script requires a script file path\n";
                return 1;
            }
            options.script_path = argv[++i];
            continue;
        }
        if (arg == "--viewport-width") {
            if (i + 1 >= argc) {
                std::cerr << "--viewport-width requires a number\n";
                return 1;
            }
            options.viewport_width = parse_int_arg(argv[++i], options.viewport_width);
            options.viewport_width_set = true;
            continue;
        }
        if (arg == "--viewport-height") {
            if (i + 1 >= argc) {
                std::cerr << "--viewport-height requires a number\n";
                return 1;
            }
            options.viewport_height = parse_int_arg(argv[++i], options.viewport_height);
            options.viewport_height_set = true;
            continue;
        }
        if (arg == "--use-app-fonts") {
            options.use_app_fonts = true;
            continue;
        }
        if (arg == "--audio-smoke") {
            if (i + 1 >= argc) {
                std::cerr << "--audio-smoke requires a local file path or package /path resource\n";
                return 1;
            }
            options.audio_smoke_source = argv[++i];
            continue;
        }
        if (arg == "--audio-smoke-ms") {
            if (i + 1 >= argc) {
                std::cerr << "--audio-smoke-ms requires a number\n";
                return 1;
            }
            options.audio_smoke_ms = std::min(10000, std::max(0, parse_int_unclamped(argv[++i], options.audio_smoke_ms)));
            continue;
        }
        positional.push_back(arg);
    }

    if (options.registry_store_path.empty() && options.app_path.empty() && positional.empty() &&
        options.audio_smoke_source.empty()) {
        options.app_path = "samples/apps/packages/watch_weather";
    }

    if (options.capture && options.capture_frames) {
        std::cerr << "--capture and --capture-frames are mutually exclusive\n";
        return 1;
    }
    if (options.capture_frames && options.frame_output_dir.empty() && options.frame_montage_path.empty()) {
        std::cerr << "--capture-frames/--frame-script requires an output directory or --capture-montage\n";
        return 1;
    }

    if (!options.install_bundle_path.empty() || !options.remove_app_id.empty() || !options.launch_app_id.empty()) {
        if (options.registry_store_path.empty()) {
            std::cerr << "--registry-store is required for install/remove/launch app manager commands\n";
            return 1;
        }
    }

    try {
        if (!options.install_bundle_path.empty()) {
            const auto installed = jellyframe_example::install_bundle_into_registry(
                options.registry_store_path, options.install_bundle_path, kMaxInputBytes);
            options.startup_status = "Installed " + installed.name + ".";
            std::cout << "installed " << installed.id << " " << installed.version_name << '\n';
        }
        if (!options.remove_app_id.empty()) {
            const auto removed =
                jellyframe_example::remove_bundle_from_registry(options.registry_store_path, options.remove_app_id);
            options.startup_status = "Removed " + removed.name + ".";
            std::cout << "removed " << removed.id << '\n';
        }
        if (!options.launch_app_id.empty()) {
            options.app_path =
                jellyframe_example::find_installed_app_bundle_path(options.registry_store_path, options.launch_app_id).string();
        }
    } catch (const std::exception& error) {
        std::cerr << "app manager command failed: " << error.what() << '\n';
            return 1;
    }

    if (!options.audio_smoke_source.empty()) {
        const int audio_result = run_win32_audio_smoke(options);
        if (audio_result != 0) {
            return audio_result;
        }
        if (!options.capture && !options.capture_frames && options.registry_store_path.empty() && positional.empty()) {
            return 0;
        }
    }

    if (!options.registry_store_path.empty() && options.app_path.empty() && positional.empty()) {
        try {
            options.inline_html =
                build_system_shell_html(options.launcher_app_path, options.registry_store_path, options.startup_status);
            options.inline_css = load_system_shell_css(options.launcher_app_path);
        } catch (const std::exception& error) {
            std::cerr << "launcher app load failed: " << error.what() << '\n';
            return 1;
        }
    }

    if (!options.app_path.empty()) {
        try {
            const auto package = jellyframe_example::load_app_package(options.app_path, kMaxInputBytes);
            options.viewport_width = options.viewport_width_set ? options.viewport_width : package.manifest.viewport_width;
            options.viewport_height = options.viewport_height_set ? options.viewport_height : package.manifest.viewport_height;
            if (options.viewport_width <= 0) {
                options.viewport_width = 390;
            }
            if (options.viewport_height <= 0) {
                options.viewport_height = 640;
            }
            if (options.capture) {
                options.html_path.clear();
                options.css_path.clear();
                FrameBuffer frame_buffer = render_page_with_browser_text(options);
                write_image(frame_buffer, options.output_path);
                std::cout << "JellyFrame Win32 browser capture\n"
                          << "  output=" << options.output_path << '\n'
                          << "  viewport_width=" << options.viewport_width << '\n'
                          << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                          << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                          << "  non_background_pixels="
                          << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
                return 0;
            }
        } catch (const std::exception& error) {
            std::cerr << "app load failed: " << error.what() << '\n';
            return 1;
        }
    } else if (options.capture) {
        if (!options.inline_html.empty()) {
            try {
                FrameBuffer frame_buffer = render_page_with_browser_text(options);
                write_image(frame_buffer, options.output_path);
                std::cout << "JellyFrame Win32 browser capture\n"
                          << "  output=" << options.output_path << '\n'
                          << "  viewport_width=" << options.viewport_width << '\n'
                          << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                          << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                          << "  non_background_pixels="
                          << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
                return 0;
            } catch (const std::exception& error) {
                std::cerr << "capture failed: " << error.what() << '\n';
                return 1;
            }
        }
        if (positional.size() >= 1) {
            options.html_path = positional[0];
        }
        if (positional.size() >= 2) {
            options.css_path = positional[1];
        }
        if (positional.size() >= 3) {
            options.viewport_width = parse_int_arg(positional[2].c_str(), options.viewport_width);
        }
        if (positional.size() >= 4) {
            options.viewport_height = parse_int_arg(positional[3].c_str(), options.viewport_height);
        }
        try {
            FrameBuffer frame_buffer = render_page_with_browser_text(options.html_path,
                                                                      options.css_path,
                                                                      options.viewport_width,
                                                                      options.viewport_height);
            write_image(frame_buffer, options.output_path);
            std::cout << "JellyFrame Win32 browser capture\n"
                      << "  output=" << options.output_path << '\n'
                      << "  viewport_width=" << options.viewport_width << '\n'
                      << "  app_fonts=" << (options.use_app_fonts ? "on" : "off") << '\n'
                      << "  image=" << frame_buffer.width << "x" << frame_buffer.height << '\n'
                      << "  non_background_pixels="
                      << count_non_background_pixels(frame_buffer, Color{255, 255, 255, 255}) << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "capture failed: " << error.what() << '\n';
            return 1;
        }
    }

#if !defined(JELLYFRAME_ENABLE_SCRIPTING)
    if (!options.script_path.empty()) {
        std::cerr << "this build was compiled without JELLYFRAME_BUILD_SCRIPTING=ON\n";
        return 1;
    }
#endif

    if (options.app_path.empty()) {
        if (!positional.empty()) {
            options.html_path = positional[0];
        }
        if (positional.size() >= 2) {
            options.css_path = positional[1];
        }
        if (positional.size() >= 3) {
            options.viewport_width = parse_int_arg(positional[2].c_str(), options.viewport_width);
        }
        if (positional.size() >= 4) {
            options.viewport_height = parse_int_arg(positional[3].c_str(), options.viewport_height);
        }
    }

    const bool capture_frames_mode = options.capture_frames;
    BrowserApp app(std::move(options));
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const int show_command = capture_frames_mode ? SW_HIDE : SW_SHOWNORMAL;
    if (!app.initialize(instance, show_command)) {
        std::cerr << "failed to create Win32 browser window\n";
        return 1;
    }
    if (capture_frames_mode) {
        return app.capture_frames();
    }
    return app.run();
}
