#include "app_runtime/app_video_frames.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace jellyframe {
namespace {

constexpr std::uint32_t kVideoErrorInvalidSource = 1;
constexpr std::uint32_t kVideoErrorUnsupportedCodec = 2;
constexpr std::uint32_t kVideoErrorFrameBudget = 3;
constexpr std::uint32_t kVideoErrorPendingFrame = 4;
constexpr std::uint32_t kVideoErrorSourceNotFound = 5;
constexpr std::uint32_t kVideoErrorHandleBudget = 6;

AppServiceSubmitResult rejected(AppServiceSubmitStatus status,
                                HostServiceStatus host_status,
                                std::uint32_t error_code) {
    return AppServiceSubmitResult{status, 0, host_status, error_code};
}

AppServiceSubmitResult from_submit(const HostServiceSubmitResult& result) {
    return result.accepted
        ? AppServiceSubmitResult{AppServiceSubmitStatus::Accepted, result.job_id, HostServiceStatus::Completed, 0}
        : AppServiceSubmitResult{AppServiceSubmitStatus::QueueFull, 0, result.rejected_status, 0};
}

} // namespace

const char* app_video_frame_codec_name(AppVideoFrameCodec codec) {
    switch (codec) {
    case AppVideoFrameCodec::Mjpeg: return "mjpeg";
    case AppVideoFrameCodec::H264Baseline: return "h264-baseline";
    }
    return "unknown";
}

const char* app_video_frame_failure_reason_name(AppVideoFrameFailureReason reason) {
    switch (reason) {
    case AppVideoFrameFailureReason::None: return "none";
    case AppVideoFrameFailureReason::EmptyInstance: return "empty-instance";
    case AppVideoFrameFailureReason::CapabilityDenied: return "capability-denied";
    case AppVideoFrameFailureReason::UnsupportedCodec: return "unsupported-codec";
    case AppVideoFrameFailureReason::InvalidSource: return "invalid-source";
    case AppVideoFrameFailureReason::FrameBudgetExceeded: return "frame-budget-exceeded";
    case AppVideoFrameFailureReason::QueueFull: return "queue-full";
    case AppVideoFrameFailureReason::PendingFrame: return "pending-frame";
    case AppVideoFrameFailureReason::SourceNotFound: return "source-not-found";
    case AppVideoFrameFailureReason::DecodeFailed: return "decode-failed";
    case AppVideoFrameFailureReason::DecodeCancelled: return "decode-cancelled";
    case AppVideoFrameFailureReason::DecodeTimeout: return "decode-timeout";
    case AppVideoFrameFailureReason::Unsupported: return "unsupported";
    case AppVideoFrameFailureReason::Unknown: return "unknown";
    }
    return "unknown";
}

AppVideoFrameFailureReason classify_app_video_frame_failure(AppServiceSubmitStatus submit_status,
                                                            HostServiceStatus host_status,
                                                            std::uint32_t error_code) {
    if (submit_status != AppServiceSubmitStatus::Accepted) {
        switch (submit_status) {
        case AppServiceSubmitStatus::EmptyInstance: return AppVideoFrameFailureReason::EmptyInstance;
        case AppServiceSubmitStatus::CapabilityDenied: return AppVideoFrameFailureReason::CapabilityDenied;
        case AppServiceSubmitStatus::InvalidInput:
            return error_code == kVideoErrorUnsupportedCodec
                ? AppVideoFrameFailureReason::UnsupportedCodec
                : AppVideoFrameFailureReason::InvalidSource;
        case AppServiceSubmitStatus::QueueFull:
            return error_code == kVideoErrorPendingFrame
                ? AppVideoFrameFailureReason::PendingFrame
                : AppVideoFrameFailureReason::QueueFull;
        case AppServiceSubmitStatus::BudgetExceeded: return AppVideoFrameFailureReason::FrameBudgetExceeded;
        case AppServiceSubmitStatus::Accepted: break;
        }
    }
    switch (host_status) {
    case HostServiceStatus::Completed: return AppVideoFrameFailureReason::None;
    case HostServiceStatus::Cancelled: return AppVideoFrameFailureReason::DecodeCancelled;
    case HostServiceStatus::Timeout: return AppVideoFrameFailureReason::DecodeTimeout;
    case HostServiceStatus::Unsupported: return AppVideoFrameFailureReason::Unsupported;
    case HostServiceStatus::BudgetExceeded: return AppVideoFrameFailureReason::FrameBudgetExceeded;
    case HostServiceStatus::Failed:
        return error_code == kVideoErrorSourceNotFound
            ? AppVideoFrameFailureReason::SourceNotFound
            : AppVideoFrameFailureReason::DecodeFailed;
    }
    return AppVideoFrameFailureReason::Unknown;
}

std::string app_video_frame_failure_detail(const std::string& source,
                                           AppServiceSubmitStatus submit_status,
                                           HostServiceStatus host_status,
                                           std::uint32_t error_code) {
    return "source=" + source + " reason=" +
        app_video_frame_failure_reason_name(classify_app_video_frame_failure(submit_status, host_status, error_code));
}

AppVideoFrameProviderMock::AppVideoFrameProviderMock(AppVideoFramePolicy policy) : policy_(policy) {}

void AppVideoFrameProviderMock::set_policy(AppVideoFramePolicy policy) {
    policy_ = policy;
    clear();
}

bool AppVideoFrameProviderMock::codec_allowed(AppVideoFrameCodec codec) const {
    return codec == AppVideoFrameCodec::Mjpeg ? policy_.allow_mjpeg : policy_.allow_h264;
}

bool AppVideoFrameProviderMock::valid_fixture(const AppVideoFrameFixture& fixture) const {
    return !fixture.source.empty() && codec_allowed(fixture.codec) &&
        fixture.width > 0 && fixture.height > 0 && fixture.stride_pixels >= fixture.width &&
        fixture.width <= policy_.max_width && fixture.height <= policy_.max_height &&
        fixture.pixel_format == policy_.pixel_format &&
        fixture.pixels.size() == decoded_surface_byte_count(fixture.width, fixture.height,
                                                            fixture.stride_pixels, fixture.pixel_format) &&
        fixture.pixels.size() <= policy_.max_frame_bytes;
}

bool AppVideoFrameProviderMock::add_fixture(AppVideoFrameFixture fixture) {
    if (!valid_fixture(fixture)) {
        return false;
    }
    fixtures_.push_back(std::move(fixture));
    return true;
}

bool AppVideoFrameProviderMock::source_pending(std::uint32_t app_instance_id, const std::string& source) const {
    return std::any_of(pending_.begin(), pending_.end(), [app_instance_id, &source, this](const PendingFrame& pending) {
        return pending.app_instance_id == app_instance_id && fixtures_[pending.fixture_index].source == source;
    });
}

AppServiceSubmitResult AppVideoFrameProviderMock::request_next_frame(AppRuntimeHost& host,
                                                                      AppVideoFrameRequest request) {
    const std::uint32_t instance = host.current_app_instance_id();
    if (instance == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled, 0);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported, 0);
    }
    if (request.source.empty()) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed, kVideoErrorInvalidSource);
    }
    if (!codec_allowed(request.codec)) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Unsupported, kVideoErrorUnsupportedCodec);
    }
    if (source_pending(instance, request.source)) {
        return rejected(AppServiceSubmitStatus::QueueFull, HostServiceStatus::BudgetExceeded, kVideoErrorPendingFrame);
    }
    const auto fixture = std::find_if(fixtures_.begin(), fixtures_.end(), [&request](const AppVideoFrameFixture& item) {
        return item.source == request.source && item.codec == request.codec;
    });
    if (fixture == fixtures_.end()) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed, kVideoErrorSourceNotFound);
    }
    const HostServiceSubmitResult submitted = host.submit_current(HostServiceJobKind::VideoFrameDecode,
                                                                  0, 0, request.timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (result.accepted()) {
        pending_.push_back(PendingFrame{submitted.job_id, instance,
                                        static_cast<std::size_t>(fixture - fixtures_.begin())});
    }
    return result;
}

HostServiceCompletion AppVideoFrameProviderMock::complete_request(AppRuntimeHost& host,
                                                                   const HostServiceRequest& request) {
    const auto pending = std::find_if(pending_.begin(), pending_.end(), [&request](const PendingFrame& item) {
        return item.job_id == request.job_id;
    });
    if (pending == pending_.end()) {
        return make_cancelled_completion(request);
    }
    const AppVideoFrameFixture& fixture = fixtures_[pending->fixture_index];
    HostServiceCompletion completion{request.job_id, HostServiceJobKind::VideoFrameDecode,
                                     HostServiceStatus::Completed, request.app_instance_id, 0, 0, 0};
    if (!valid_fixture(fixture) || fixture.pixels.size() > std::numeric_limits<std::uint32_t>::max()) {
        completion.status = HostServiceStatus::BudgetExceeded;
        completion.error_code = kVideoErrorFrameBudget;
    } else {
        const std::uint32_t bytes = static_cast<std::uint32_t>(fixture.pixels.size());
        const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::VideoFrame,
                                                             request.app_instance_id, bytes);
        if (handle == 0) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = kVideoErrorHandleBudget;
        } else {
            bool dropped_previous = false;
            for (auto record = records_.begin(); record != records_.end();) {
                if (record->app_instance_id == request.app_instance_id && record->source == fixture.source) {
                    host.handles().release(record->handle);
                    record = records_.erase(record);
                    dropped_previous = true;
                } else {
                    ++record;
                }
            }
            completion.handle = handle;
            completion.byte_count = bytes;
            records_.push_back(AppVideoFrameRecord{handle, request.app_instance_id, fixture.source, fixture.codec,
                                                    fixture.width, fixture.height, fixture.stride_pixels,
                                                    fixture.pixel_format, fixture.pts_ms, dropped_previous,
                                                    fixture.pixels});
        }
    }
    pending_.erase(pending);
    return completion;
}

bool AppVideoFrameProviderMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::VideoFrameDecode, request)) {
        return false;
    }
    return host.push_completion(complete_request(host, request));
}

std::vector<AppVideoFrameRecord>::iterator AppVideoFrameProviderMock::find_record(std::uint32_t handle) {
    return std::find_if(records_.begin(), records_.end(), [handle](const AppVideoFrameRecord& record) {
        return record.handle == handle;
    });
}

std::vector<AppVideoFrameRecord>::const_iterator AppVideoFrameProviderMock::find_record(std::uint32_t handle) const {
    return std::find_if(records_.begin(), records_.end(), [handle](const AppVideoFrameRecord& record) {
        return record.handle == handle;
    });
}

const AppVideoFrameRecord* AppVideoFrameProviderMock::frame(std::uint32_t handle) const {
    const auto record = find_record(handle);
    return record == records_.end() ? nullptr : &*record;
}

std::uint32_t AppVideoFrameProviderMock::latest_frame_handle(const std::string& source) const {
    const auto record = std::find_if(records_.rbegin(), records_.rend(), [&source](const AppVideoFrameRecord& item) {
        return item.source == source;
    });
    return record == records_.rend() ? 0 : record->handle;
}

bool AppVideoFrameProviderMock::release_frame(AppRuntimeHost& host, std::uint32_t handle) {
    const auto record = find_record(handle);
    if (record == records_.end() || !host.handles().release(handle)) {
        return false;
    }
    records_.erase(record);
    return true;
}

std::size_t AppVideoFrameProviderMock::collect_released_frames(const AppRuntimeHost& host) {
    const std::size_t before = records_.size();
    records_.erase(std::remove_if(records_.begin(), records_.end(), [&host](const AppVideoFrameRecord& record) {
        return host.handles().lookup(record.handle) == nullptr;
    }), records_.end());
    return before - records_.size();
}

std::size_t AppVideoFrameProviderMock::collect_stale_pending_frames(const AppRuntimeHost& host) {
    const std::uint32_t current = host.current_app_instance_id();
    const std::size_t before = pending_.size();
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [current](const PendingFrame& item) {
        return item.app_instance_id != current;
    }), pending_.end());
    return before - pending_.size();
}

void AppVideoFrameProviderMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
}

} // namespace jellyframe
