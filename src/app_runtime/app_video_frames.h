#pragma once

#include "app_runtime/app_host.h"
#include "app_runtime/app_services.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace jellyframe {

enum class AppVideoFrameCodec {
    Mjpeg,
    H264Baseline,
};

enum class AppVideoFrameFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    UnsupportedCodec,
    InvalidSource,
    FrameBudgetExceeded,
    QueueFull,
    PendingFrame,
    SourceNotFound,
    DecodeFailed,
    DecodeCancelled,
    DecodeTimeout,
    Unsupported,
    Unknown,
};

struct AppVideoFrameRequest {
    std::string source;
    AppVideoFrameCodec codec = AppVideoFrameCodec::Mjpeg;
    std::uint32_t timeout_ms = 0;
};

struct AppVideoFrameFixture {
    std::string source;
    AppVideoFrameCodec codec = AppVideoFrameCodec::Mjpeg;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    HostPixelFormat pixel_format = HostPixelFormat::Unknown;
    std::uint32_t pts_ms = 0;
    std::vector<std::uint8_t> pixels;
};

struct AppVideoFrameRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::string source;
    AppVideoFrameCodec codec = AppVideoFrameCodec::Mjpeg;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    HostPixelFormat pixel_format = HostPixelFormat::Unknown;
    std::uint32_t pts_ms = 0;
    bool dropped_previous = false;
    std::vector<std::uint8_t> pixels;
};

const char* app_video_frame_codec_name(AppVideoFrameCodec codec);
const char* app_video_frame_failure_reason_name(AppVideoFrameFailureReason reason);
AppVideoFrameFailureReason classify_app_video_frame_failure(AppServiceSubmitStatus submit_status,
                                                            HostServiceStatus host_status,
                                                            std::uint32_t error_code);
std::string app_video_frame_failure_detail(const std::string& source,
                                           AppServiceSubmitStatus submit_status,
                                           HostServiceStatus host_status,
                                           std::uint32_t error_code);

class AppVideoFrameProviderMock {
public:
    explicit AppVideoFrameProviderMock(AppVideoFramePolicy policy = {});

    void set_policy(AppVideoFramePolicy policy);
    bool add_fixture(AppVideoFrameFixture fixture);
    AppServiceSubmitResult request_next_frame(AppRuntimeHost& host, AppVideoFrameRequest request);
    HostServiceCompletion complete_request(AppRuntimeHost& host, const HostServiceRequest& request);
    bool complete_next(AppRuntimeHost& host);
    const AppVideoFrameRecord* frame(std::uint32_t handle) const;
    std::uint32_t latest_frame_handle(const std::string& source) const;
    bool release_frame(AppRuntimeHost& host, std::uint32_t handle);
    std::size_t collect_released_frames(const AppRuntimeHost& host);
    std::size_t collect_stale_pending_frames(const AppRuntimeHost& host);
    void clear();

private:
    struct PendingFrame {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        std::size_t fixture_index = 0;
    };

    bool codec_allowed(AppVideoFrameCodec codec) const;
    bool valid_fixture(const AppVideoFrameFixture& fixture) const;
    bool source_pending(std::uint32_t app_instance_id, const std::string& source) const;
    std::vector<AppVideoFrameRecord>::iterator find_record(std::uint32_t handle);
    std::vector<AppVideoFrameRecord>::const_iterator find_record(std::uint32_t handle) const;

    AppVideoFramePolicy policy_;
    std::vector<AppVideoFrameFixture> fixtures_;
    std::vector<PendingFrame> pending_;
    std::vector<AppVideoFrameRecord> records_;
};

} // namespace jellyframe
