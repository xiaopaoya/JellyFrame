#include "app_runtime/app_services.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace jellyframe {
namespace {

AppServiceSubmitResult rejected(AppServiceSubmitStatus status, HostServiceStatus host_status) {
    return AppServiceSubmitResult{status, 0, host_status};
}

AppServiceSubmitResult from_submit(const HostServiceSubmitResult& result) {
    if (!result.accepted) {
        return AppServiceSubmitResult{AppServiceSubmitStatus::QueueFull, 0, result.rejected_status};
    }
    return AppServiceSubmitResult{AppServiceSubmitStatus::Accepted, result.job_id, HostServiceStatus::Completed};
}

template <typename T>
typename std::vector<T>::iterator find_job(std::vector<T>& items, std::uint32_t job_id) {
    return std::find_if(items.begin(), items.end(), [job_id](const T& item) {
        return item.job_id == job_id;
    });
}

} // namespace

const char* app_service_submit_status_name(AppServiceSubmitStatus status) {
    switch (status) {
    case AppServiceSubmitStatus::Accepted:
        return "accepted";
    case AppServiceSubmitStatus::EmptyInstance:
        return "empty-instance";
    case AppServiceSubmitStatus::CapabilityDenied:
        return "capability-denied";
    case AppServiceSubmitStatus::InvalidInput:
        return "invalid-input";
    case AppServiceSubmitStatus::QueueFull:
        return "queue-full";
    case AppServiceSubmitStatus::BudgetExceeded:
        return "budget-exceeded";
    }
    return "unknown";
}

const char* host_service_status_name(HostServiceStatus status) {
    switch (status) {
    case HostServiceStatus::Completed:
        return "completed";
    case HostServiceStatus::Failed:
        return "failed";
    case HostServiceStatus::Cancelled:
        return "cancelled";
    case HostServiceStatus::Unsupported:
        return "unsupported";
    case HostServiceStatus::BudgetExceeded:
        return "budget-exceeded";
    case HostServiceStatus::Timeout:
        return "timeout";
    }
    return "unknown";
}

AppServiceHostProfile app_service_host_profile_from_capabilities(const HostDeviceCapabilities& capabilities,
                                                                 const AppPrivateKvPolicy& storage_policy) {
    AppServiceHostProfile profile;
    profile.allow_network_fetch = capabilities.has_network && capabilities.network.supports_fetch;
    profile.max_network_url_bytes = capabilities.network.max_request_bytes == 0
        ? profile.max_network_url_bytes
        : capabilities.network.max_request_bytes;
    profile.max_network_response_bytes = capabilities.network.max_response_bytes == 0
        ? profile.max_network_response_bytes
        : capabilities.network.max_response_bytes;

    profile.allow_storage_kv = storage_policy.enabled;
    profile.max_storage_key_bytes = storage_policy.max_key_bytes;
    profile.max_storage_value_bytes = storage_policy.max_value_bytes;
    profile.max_storage_items_per_app = storage_policy.max_items_per_app;
    profile.max_storage_bytes_per_app = storage_policy.max_bytes_per_app;

    profile.allow_image_decode = capabilities.media.supports_image_decode;
    profile.max_image_width = capabilities.media.max_image_width;
    profile.max_image_height = capabilities.media.max_image_height;
    profile.max_decoded_image_bytes = capabilities.media.max_decoded_image_bytes;
    profile.max_pending_image_decodes = capabilities.async.max_in_flight_jobs == 0
        ? profile.max_pending_image_decodes
        : capabilities.async.max_in_flight_jobs;

    profile.allow_audio_playback = capabilities.media.supports_audio_playback;
    profile.max_audio_streams = capabilities.media.max_audio_streams == 0
        ? profile.max_audio_streams
        : capabilities.media.max_audio_streams;
    return profile;
}

AppServicePolicies app_service_policies_for_app(const AppServiceManifestCapabilities& manifest,
                                                const AppServiceHostProfile& profile) {
    AppServicePolicies policies;
    policies.network = NetworkFetchPolicy{
        manifest.network_fetch && profile.allow_network_fetch,
        profile.max_network_url_bytes,
        profile.max_network_response_bytes,
    };
    policies.storage = AppPrivateKvPolicy{
        manifest.storage_kv && profile.allow_storage_kv,
        profile.max_storage_key_bytes,
        profile.max_storage_value_bytes,
        profile.max_storage_items_per_app,
        profile.max_storage_bytes_per_app,
    };
    policies.image = ImageDecodePolicy{
        manifest.image_decode && profile.allow_image_decode,
        profile.max_image_url_bytes,
        profile.max_image_width,
        profile.max_image_height,
        profile.max_decoded_image_bytes,
        profile.max_pending_image_decodes,
    };
    policies.audio = AudioPlaybackPolicy{
        manifest.audio_playback && profile.allow_audio_playback,
        profile.max_audio_source_url_bytes,
        profile.max_audio_streams,
    };
    return policies;
}

NetworkFetchMock::NetworkFetchMock(NetworkFetchPolicy policy)
    : policy_(policy) {}

void NetworkFetchMock::set_policy(NetworkFetchPolicy policy) {
    policy_ = policy;
}

bool NetworkFetchMock::add_fixture(NetworkFetchFixture fixture) {
    if (fixture.url.empty() || fixture.url.size() > policy_.max_url_bytes ||
        fixture.body.size() > policy_.max_response_bytes) {
        return false;
    }
    fixtures_.push_back(std::move(fixture));
    return true;
}

AppServiceSubmitResult NetworkFetchMock::submit_fetch(AppRuntimeHost& host,
                                                      const std::string& url,
                                                      std::uint32_t timeout_ms) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (url.empty() || url.size() > policy_.max_url_bytes) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed);
    }

    const HostServiceSubmitResult submitted =
        host.submit_current(HostServiceJobKind::NetworkFetch, 0, 0, timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingFetch pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = host.current_app_instance_id();
    const auto found = std::find_if(fixtures_.begin(), fixtures_.end(), [&url](const NetworkFetchFixture& fixture) {
        return fixture.url == url;
    });
    if (found == fixtures_.end()) {
        pending.status = HostServiceStatus::Failed;
        pending.error_code = 404;
    } else if (found->body.size() > policy_.max_response_bytes) {
        pending.status = HostServiceStatus::BudgetExceeded;
        pending.error_code = 413;
    } else {
        pending.status = HostServiceStatus::Completed;
        pending.fixture = *found;
    }
    pending_.push_back(std::move(pending));
    return result;
}

bool NetworkFetchMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::NetworkFetch, request)) {
        return false;
    }
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return host.push_completion(make_cancelled_completion(request));
    }

    HostServiceCompletion completion{
        request.job_id,
        HostServiceJobKind::NetworkFetch,
        pending->status,
        request.app_instance_id,
        0,
        pending->error_code,
        0,
    };
    if (pending->status == HostServiceStatus::Completed) {
        const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::FetchResponse,
                                                            request.app_instance_id,
                                                            static_cast<std::uint32_t>(pending->fixture.body.size()));
        if (handle == 0) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = 507;
        } else {
            completion.handle = handle;
            completion.byte_count = static_cast<std::uint32_t>(pending->fixture.body.size());
            records_.push_back(NetworkFetchRecord{
                handle,
                request.app_instance_id,
                pending->fixture.status_code,
                pending->fixture.content_type,
                pending->fixture.body,
            });
        }
    }
    pending_.erase(pending);
    return host.push_completion(completion);
}

const NetworkFetchRecord* NetworkFetchMock::response(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const NetworkFetchRecord& record) {
        return record.handle == handle;
    });
    return found == records_.end() ? nullptr : &*found;
}

bool NetworkFetchMock::release_response(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const NetworkFetchRecord& record) {
        return record.handle == handle;
    });
    if (found == records_.end()) {
        const HostHandleInfo* info = host.handles().lookup(handle);
        if (info == nullptr || info->kind != HostServiceHandleKind::FetchResponse) {
            return false;
        }
        return host.handles().release(handle);
    }
    records_.erase(found);
    return host.handles().release(handle);
}

void NetworkFetchMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
}

std::size_t decoded_surface_byte_count(int width,
                                       int height,
                                       int stride_pixels,
                                       HostPixelFormat pixel_format) {
    if (width <= 0 || height <= 0 || stride_pixels < width) {
        return 0;
    }
    const std::size_t stride = static_cast<std::size_t>(stride_pixels);
    const std::size_t rows = static_cast<std::size_t>(height);
    switch (pixel_format) {
    case HostPixelFormat::Rgba8888:
    case HostPixelFormat::Bgra8888:
        return stride * rows * 4;
    case HostPixelFormat::Rgb565:
    case HostPixelFormat::Bgr565:
        return stride * rows * 2;
    case HostPixelFormat::Rgb332:
    case HostPixelFormat::Gray8:
        return stride * rows;
    case HostPixelFormat::Mono1:
        return ((stride + 7) / 8) * rows;
    case HostPixelFormat::Unknown:
        break;
    }
    return 0;
}

const char* app_image_surface_state_name(AppImageSurfaceState state) {
    switch (state) {
    case AppImageSurfaceState::Missing:
        return "missing";
    case AppImageSurfaceState::Pending:
        return "pending";
    case AppImageSurfaceState::Ready:
        return "ready";
    case AppImageSurfaceState::Failed:
        return "failed";
    }
    return "missing";
}

const char* app_image_failure_reason_name(AppImageFailureReason reason) {
    switch (reason) {
    case AppImageFailureReason::None:
        return "none";
    case AppImageFailureReason::EmptyInstance:
        return "empty-instance";
    case AppImageFailureReason::CapabilityDenied:
        return "capability-denied";
    case AppImageFailureReason::InvalidSource:
        return "invalid-source";
    case AppImageFailureReason::QueueFull:
        return "queue-full";
    case AppImageFailureReason::PendingBudget:
        return "pending-budget";
    case AppImageFailureReason::ResourceNotFound:
        return "resource-not-found";
    case AppImageFailureReason::DecodeBudgetExceeded:
        return "decode-budget-exceeded";
    case AppImageFailureReason::SurfaceBudgetExceeded:
        return "surface-budget-exceeded";
    case AppImageFailureReason::DecodeFailed:
        return "decode-failed";
    case AppImageFailureReason::DecodeCancelled:
        return "decode-cancelled";
    case AppImageFailureReason::DecodeTimeout:
        return "decode-timeout";
    case AppImageFailureReason::Unsupported:
        return "unsupported";
    case AppImageFailureReason::Unknown:
        return "unknown";
    }
    return "unknown";
}

AppImageFailureReason classify_app_image_failure(AppServiceSubmitStatus submit_status,
                                                 HostServiceStatus host_status,
                                                 std::uint32_t error_code) {
    if (submit_status != AppServiceSubmitStatus::Accepted) {
        switch (submit_status) {
        case AppServiceSubmitStatus::Accepted:
            break;
        case AppServiceSubmitStatus::EmptyInstance:
            return AppImageFailureReason::EmptyInstance;
        case AppServiceSubmitStatus::CapabilityDenied:
            return AppImageFailureReason::CapabilityDenied;
        case AppServiceSubmitStatus::InvalidInput:
            return AppImageFailureReason::InvalidSource;
        case AppServiceSubmitStatus::QueueFull:
            return AppImageFailureReason::QueueFull;
        case AppServiceSubmitStatus::BudgetExceeded:
            return AppImageFailureReason::PendingBudget;
        }
    }

    switch (host_status) {
    case HostServiceStatus::Completed:
        return AppImageFailureReason::None;
    case HostServiceStatus::Unsupported:
        return AppImageFailureReason::Unsupported;
    case HostServiceStatus::Cancelled:
        return AppImageFailureReason::DecodeCancelled;
    case HostServiceStatus::Timeout:
        return AppImageFailureReason::DecodeTimeout;
    case HostServiceStatus::BudgetExceeded:
        if (error_code == 507) {
            return AppImageFailureReason::SurfaceBudgetExceeded;
        }
        return AppImageFailureReason::DecodeBudgetExceeded;
    case HostServiceStatus::Failed:
        if (error_code == 404) {
            return AppImageFailureReason::ResourceNotFound;
        }
        if (error_code == 413) {
            return AppImageFailureReason::DecodeBudgetExceeded;
        }
        return AppImageFailureReason::DecodeFailed;
    }
    return AppImageFailureReason::Unknown;
}

std::string app_image_failure_detail(const std::string& url,
                                     AppServiceSubmitStatus submit_status,
                                     HostServiceStatus host_status,
                                     std::uint32_t error_code) {
    std::ostringstream stream;
    stream << "src=" << (url.empty() ? "unknown" : url)
           << "; reason="
           << app_image_failure_reason_name(classify_app_image_failure(submit_status, host_status, error_code))
           << "; submit=" << app_service_submit_status_name(submit_status);
    if (host_status != HostServiceStatus::Completed) {
        stream << "; host=" << host_service_status_name(host_status);
    }
    if (error_code != 0) {
        stream << "; error=" << error_code;
    }
    return stream.str();
}

ImageDecodeMock::ImageDecodeMock(ImageDecodePolicy policy)
    : policy_(policy) {}

void ImageDecodeMock::set_policy(ImageDecodePolicy policy) {
    policy_ = policy;
}

bool ImageDecodeMock::valid_fixture(const ImageDecodeFixture& fixture) const {
    if (fixture.url.empty() || fixture.url.size() > policy_.max_url_bytes) {
        return false;
    }
    if (policy_.max_width > 0 && fixture.width > policy_.max_width) {
        return false;
    }
    if (policy_.max_height > 0 && fixture.height > policy_.max_height) {
        return false;
    }
    const std::size_t decoded_bytes =
        decoded_surface_byte_count(fixture.width, fixture.height, fixture.stride_pixels, fixture.pixel_format);
    if (decoded_bytes == 0) {
        return false;
    }
    if (decoded_bytes > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    if (!fixture.pixels.empty() && fixture.pixels.size() != decoded_bytes) {
        return false;
    }
    return policy_.max_decoded_bytes == 0 || decoded_bytes <= policy_.max_decoded_bytes;
}

bool ImageDecodeMock::add_fixture(ImageDecodeFixture fixture) {
    if (!valid_fixture(fixture)) {
        return false;
    }
    fixtures_.push_back(std::move(fixture));
    return true;
}

AppServiceSubmitResult ImageDecodeMock::submit_decode(AppRuntimeHost& host,
                                                      const std::string& url,
                                                      std::uint32_t timeout_ms) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (url.empty() || url.size() > policy_.max_url_bytes) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed);
    }
    if (policy_.max_pending_decodes != 0 && pending_.size() >= policy_.max_pending_decodes) {
        return rejected(AppServiceSubmitStatus::BudgetExceeded, HostServiceStatus::BudgetExceeded);
    }

    const HostServiceSubmitResult submitted =
        host.submit_current(HostServiceJobKind::ImageDecode, 0, 0, timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingDecode pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = host.current_app_instance_id();
    const auto found = std::find_if(fixtures_.begin(), fixtures_.end(), [&url](const ImageDecodeFixture& fixture) {
        return fixture.url == url;
    });
    if (found == fixtures_.end()) {
        pending.status = HostServiceStatus::Failed;
        pending.error_code = 404;
    } else if (!valid_fixture(*found)) {
        pending.status = HostServiceStatus::BudgetExceeded;
        pending.error_code = 413;
    } else {
        pending.status = HostServiceStatus::Completed;
        pending.fixture_index = static_cast<std::size_t>(found - fixtures_.begin());
    }
    pending_.push_back(pending);
    return result;
}

bool ImageDecodeMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::ImageDecode, request)) {
        return false;
    }
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return host.push_completion(make_cancelled_completion(request));
    }

    HostServiceCompletion completion{
        request.job_id,
        HostServiceJobKind::ImageDecode,
        pending->status,
        request.app_instance_id,
        0,
        pending->error_code,
        0,
    };
    if (pending->status == HostServiceStatus::Completed && pending->fixture_index < fixtures_.size()) {
        const ImageDecodeFixture& fixture = fixtures_[pending->fixture_index];
        const std::size_t decoded_bytes =
            decoded_surface_byte_count(fixture.width, fixture.height, fixture.stride_pixels, fixture.pixel_format);
        const std::uint32_t handle = host.handles().allocate(HostServiceHandleKind::Surface,
                                                            request.app_instance_id,
                                                            static_cast<std::uint32_t>(decoded_bytes));
        if (handle == 0) {
            completion.status = HostServiceStatus::BudgetExceeded;
            completion.error_code = 507;
        } else {
            completion.handle = handle;
            completion.byte_count = static_cast<std::uint32_t>(decoded_bytes);
            records_.push_back(AppDecodedSurfaceRecord{
                handle,
                request.app_instance_id,
                fixture.url,
                fixture.width,
                fixture.height,
                fixture.stride_pixels,
                fixture.pixel_format,
                fixture.pixels,
            });
        }
    }
    pending_.erase(pending);
    return host.push_completion(completion);
}

const AppDecodedSurfaceRecord* ImageDecodeMock::surface(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppDecodedSurfaceRecord& record) {
        return record.handle == handle;
    });
    return found == records_.end() ? nullptr : &*found;
}

bool ImageDecodeMock::release_surface(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppDecodedSurfaceRecord& record) {
        return record.handle == handle;
    });
    if (found == records_.end()) {
        const HostHandleInfo* info = host.handles().lookup(handle);
        if (info == nullptr || info->kind != HostServiceHandleKind::Surface) {
            return false;
        }
        return host.handles().release(handle);
    }
    records_.erase(found);
    return host.handles().release(handle);
}

void ImageDecodeMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
}

AudioCommandMock::AudioCommandMock(AudioPlaybackPolicy policy)
    : policy_(policy) {}

void AudioCommandMock::set_policy(AudioPlaybackPolicy policy) {
    policy_ = policy;
}

bool AudioCommandMock::add_source(AudioSourceFixture fixture) {
    if (fixture.url.empty() || fixture.url.size() > policy_.max_source_url_bytes) {
        return false;
    }
    sources_.push_back(std::move(fixture));
    return true;
}

std::size_t AudioCommandMock::active_stream_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(streams_.begin(), streams_.end(), [app_instance_id](const AudioStreamRecord& stream) {
        return app_instance_id == 0 || stream.app_instance_id == app_instance_id;
    }));
}

std::size_t AudioCommandMock::pending_open_count(std::uint32_t app_instance_id) const {
    return static_cast<std::size_t>(std::count_if(pending_.begin(), pending_.end(), [app_instance_id](const PendingCommand& pending) {
        return pending.command == AudioCommandKind::Open &&
               (app_instance_id == 0 || pending.app_instance_id == app_instance_id);
    }));
}

AudioStreamRecord* AudioCommandMock::find_stream(std::uint32_t audio_handle) {
    const auto found = std::find_if(streams_.begin(), streams_.end(), [audio_handle](const AudioStreamRecord& stream) {
        return stream.handle == audio_handle;
    });
    return found == streams_.end() ? nullptr : &*found;
}

const AudioStreamRecord* AudioCommandMock::find_stream(std::uint32_t audio_handle) const {
    const auto found = std::find_if(streams_.begin(), streams_.end(), [audio_handle](const AudioStreamRecord& stream) {
        return stream.handle == audio_handle;
    });
    return found == streams_.end() ? nullptr : &*found;
}

bool AudioCommandMock::valid_handle_for_current_app(const AppRuntimeHost& host, std::uint32_t audio_handle) const {
    const HostHandleInfo* info = host.handles().lookup(audio_handle);
    return info != nullptr && info->kind == HostServiceHandleKind::AudioStream &&
           info->app_instance_id == host.current_app_instance_id() &&
           find_stream(audio_handle) != nullptr;
}

AppServiceSubmitResult AudioCommandMock::submit_command(AppRuntimeHost& host,
                                                        AudioCommandKind command,
                                                        std::uint32_t audio_handle,
                                                        std::string url,
                                                        std::uint8_t volume,
                                                        std::uint32_t timeout_ms) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (command == AudioCommandKind::Open) {
        if (url.empty() || url.size() > policy_.max_source_url_bytes) {
            return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed);
        }
        if (policy_.max_audio_streams != 0 &&
            active_stream_count(host.current_app_instance_id()) + pending_open_count(host.current_app_instance_id()) >=
                policy_.max_audio_streams) {
            return rejected(AppServiceSubmitStatus::BudgetExceeded, HostServiceStatus::BudgetExceeded);
        }
    } else if (!valid_handle_for_current_app(host, audio_handle)) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed);
    }

    const HostServiceSubmitResult submitted =
        host.submit_current(HostServiceJobKind::AudioCommand, audio_handle, 0, timeout_ms);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }

    PendingCommand pending;
    pending.job_id = result.job_id;
    pending.app_instance_id = host.current_app_instance_id();
    pending.command = command;
    pending.audio_handle = audio_handle;
    pending.url = std::move(url);
    pending.volume = volume > 100 ? 100 : volume;
    pending.status = HostServiceStatus::Completed;

    if (command == AudioCommandKind::Open) {
        const auto source = std::find_if(sources_.begin(), sources_.end(), [&pending](const AudioSourceFixture& fixture) {
            return fixture.url == pending.url;
        });
        if (source == sources_.end()) {
            pending.status = HostServiceStatus::Failed;
            pending.error_code = 404;
        } else {
            pending.duration_ms = source->duration_ms;
        }
    }

    pending_.push_back(std::move(pending));
    return result;
}

AppServiceSubmitResult AudioCommandMock::submit_open(AppRuntimeHost& host,
                                                     const std::string& url,
                                                     std::uint8_t volume,
                                                     std::uint32_t timeout_ms) {
    return submit_command(host, AudioCommandKind::Open, 0, url, volume, timeout_ms);
}

AppServiceSubmitResult AudioCommandMock::submit_play(AppRuntimeHost& host, std::uint32_t audio_handle) {
    return submit_command(host, AudioCommandKind::Play, audio_handle);
}

AppServiceSubmitResult AudioCommandMock::submit_pause(AppRuntimeHost& host, std::uint32_t audio_handle) {
    return submit_command(host, AudioCommandKind::Pause, audio_handle);
}

AppServiceSubmitResult AudioCommandMock::submit_stop(AppRuntimeHost& host, std::uint32_t audio_handle) {
    return submit_command(host, AudioCommandKind::Stop, audio_handle);
}

AppServiceSubmitResult AudioCommandMock::submit_close(AppRuntimeHost& host, std::uint32_t audio_handle) {
    return submit_command(host, AudioCommandKind::Close, audio_handle);
}

AppServiceSubmitResult AudioCommandMock::submit_set_volume(AppRuntimeHost& host,
                                                           std::uint32_t audio_handle,
                                                           std::uint8_t volume) {
    return submit_command(host, AudioCommandKind::SetVolume, audio_handle, {}, volume);
}

bool AudioCommandMock::push_state_completion(AppRuntimeHost& host,
                                             std::uint32_t job_id,
                                             std::uint32_t app_instance_id,
                                             HostServiceStatus status,
                                             std::uint32_t audio_handle,
                                             AudioStreamState state,
                                             std::uint32_t error_code) {
    return host.push_completion(HostServiceCompletion{
        job_id,
        HostServiceJobKind::AudioCommand,
        status,
        app_instance_id,
        audio_handle,
        error_code,
        static_cast<std::uint32_t>(state),
    });
}

bool AudioCommandMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::AudioCommand, request)) {
        return false;
    }
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return host.push_completion(make_cancelled_completion(request));
    }

    HostServiceStatus status = pending->status;
    std::uint32_t handle = pending->audio_handle;
    std::uint32_t error_code = pending->error_code;
    AudioStreamState state = AudioStreamState::Error;

    if (status == HostServiceStatus::Completed && pending->command == AudioCommandKind::Open) {
        handle = host.handles().allocate(HostServiceHandleKind::AudioStream, request.app_instance_id, 0);
        if (handle == 0) {
            status = HostServiceStatus::BudgetExceeded;
            error_code = 507;
        } else {
            streams_.push_back(AudioStreamRecord{
                handle,
                request.app_instance_id,
                pending->url,
                AudioStreamState::Open,
                pending->volume,
                pending->duration_ms,
            });
            state = AudioStreamState::Open;
        }
    } else if (status == HostServiceStatus::Completed) {
        AudioStreamRecord* record = find_stream(handle);
        const HostHandleInfo* info = host.handles().lookup(handle);
        if (record == nullptr || info == nullptr || info->kind != HostServiceHandleKind::AudioStream ||
            info->app_instance_id != request.app_instance_id) {
            status = HostServiceStatus::Failed;
            error_code = 410;
            handle = 0;
        } else {
            switch (pending->command) {
            case AudioCommandKind::Play:
                record->state = AudioStreamState::Playing;
                break;
            case AudioCommandKind::Pause:
                record->state = AudioStreamState::Paused;
                break;
            case AudioCommandKind::Stop:
                record->state = AudioStreamState::Stopped;
                break;
            case AudioCommandKind::SetVolume:
                record->volume = pending->volume;
                break;
            case AudioCommandKind::Close:
                state = AudioStreamState::Stopped;
                release_stream(host, handle);
                pending_.erase(pending);
                return push_state_completion(host,
                                             request.job_id,
                                             request.app_instance_id,
                                             HostServiceStatus::Completed,
                                             handle,
                                             state);
            case AudioCommandKind::Open:
                break;
            }
            state = record->state;
        }
    }

    const bool pushed = push_state_completion(host, request.job_id, request.app_instance_id, status, handle, state, error_code);
    pending_.erase(pending);
    return pushed;
}

bool AudioCommandMock::post_ended(AppRuntimeHost& host, std::uint32_t audio_handle) {
    AudioStreamRecord* record = find_stream(audio_handle);
    const HostHandleInfo* info = host.handles().lookup(audio_handle);
    if (record == nullptr || info == nullptr || info->kind != HostServiceHandleKind::AudioStream) {
        return false;
    }
    record->state = AudioStreamState::Ended;
    return push_state_completion(host,
                                 0,
                                 record->app_instance_id,
                                 HostServiceStatus::Completed,
                                 audio_handle,
                                 AudioStreamState::Ended);
}

bool AudioCommandMock::post_error(AppRuntimeHost& host, std::uint32_t audio_handle, std::uint32_t error_code) {
    AudioStreamRecord* record = find_stream(audio_handle);
    const HostHandleInfo* info = host.handles().lookup(audio_handle);
    if (record == nullptr || info == nullptr || info->kind != HostServiceHandleKind::AudioStream) {
        return false;
    }
    record->state = AudioStreamState::Error;
    return push_state_completion(host,
                                 0,
                                 record->app_instance_id,
                                 HostServiceStatus::Failed,
                                 audio_handle,
                                 AudioStreamState::Error,
                                 error_code);
}

const AudioStreamRecord* AudioCommandMock::stream(std::uint32_t audio_handle) const {
    return find_stream(audio_handle);
}

bool AudioCommandMock::release_stream(AppRuntimeHost& host, std::uint32_t audio_handle) {
    const auto found = std::find_if(streams_.begin(), streams_.end(), [audio_handle](const AudioStreamRecord& stream) {
        return stream.handle == audio_handle;
    });
    if (found == streams_.end()) {
        return false;
    }
    streams_.erase(found);
    HostHandleInfo* info = host.handles().lookup(audio_handle);
    if (info == nullptr) {
        return true;
    }
    if (info->kind != HostServiceHandleKind::AudioStream) {
        return false;
    }
    return host.handles().release(audio_handle);
}

std::size_t AudioCommandMock::release_app_streams(AppRuntimeHost& host, std::uint32_t app_instance_id) {
    std::size_t released = 0;
    for (auto it = streams_.begin(); it != streams_.end();) {
        if (it->app_instance_id != app_instance_id) {
            ++it;
            continue;
        }
        const std::uint32_t handle = it->handle;
        it = streams_.erase(it);
        HostHandleInfo* info = host.handles().lookup(handle);
        if (info != nullptr && info->kind == HostServiceHandleKind::AudioStream) {
            host.handles().release(handle);
        }
        ++released;
    }
    return released;
}

std::size_t AudioCommandMock::collect_released_streams(const AppRuntimeHost& host) {
    const auto old_size = streams_.size();
    streams_.erase(std::remove_if(streams_.begin(),
                                  streams_.end(),
                                  [&host](const AudioStreamRecord& stream) {
                                      const HostHandleInfo* info = host.handles().lookup(stream.handle);
                                      return info == nullptr || info->kind != HostServiceHandleKind::AudioStream ||
                                             info->app_instance_id != stream.app_instance_id;
                                  }),
                   streams_.end());
    return old_size - streams_.size();
}

void AudioCommandMock::clear() {
    sources_.clear();
    pending_.clear();
    streams_.clear();
}

AppImageSurfaceCache::AppImageSurfaceCache(AppImageSurfaceCacheOptions options)
    : options_(options) {}

void AppImageSurfaceCache::set_options(AppImageSurfaceCacheOptions options) {
    options_ = options;
}

AppImageSurfaceCache::Entry* AppImageSurfaceCache::find_url(const std::string& url) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&url](const Entry& entry) {
        return entry.url == url;
    });
    return found == entries_.end() ? nullptr : &*found;
}

const AppImageSurfaceCache::Entry* AppImageSurfaceCache::find_url(const std::string& url) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&url](const Entry& entry) {
        return entry.url == url;
    });
    return found == entries_.end() ? nullptr : &*found;
}

AppImageSurfaceCache::Entry* AppImageSurfaceCache::find_job(std::uint32_t job_id) {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [job_id](const Entry& entry) {
        return entry.job_id == job_id && entry.state == AppImageSurfaceState::Pending;
    });
    return found == entries_.end() ? nullptr : &*found;
}

std::size_t AppImageSurfaceCache::ready_surface_count() const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& entry) {
        return entry.state == AppImageSurfaceState::Ready;
    }));
}

std::size_t AppImageSurfaceCache::ready_byte_count() const {
    std::size_t total = 0;
    for (const Entry& entry : entries_) {
        if (entry.state == AppImageSurfaceState::Ready) {
            total += entry.decoded_bytes;
        }
    }
    return total;
}

bool AppImageSurfaceCache::over_budget() const {
    return (options_.max_ready_surfaces != 0 && ready_surface_count() > options_.max_ready_surfaces) ||
           (options_.max_ready_bytes != 0 && ready_byte_count() > options_.max_ready_bytes);
}

AppImageSurfaceCache::Entry* AppImageSurfaceCache::least_recently_used_unprotected(
    const std::uint32_t* protected_handles,
    std::size_t protected_handle_count) {
    Entry* candidate = nullptr;
    for (Entry& entry : entries_) {
        if (entry.state != AppImageSurfaceState::Ready || entry.handle == 0) {
            continue;
        }
        bool protected_handle = false;
        for (std::size_t i = 0; i < protected_handle_count; ++i) {
            if (protected_handles != nullptr && protected_handles[i] == entry.handle) {
                protected_handle = true;
                break;
            }
        }
        if (protected_handle) {
            continue;
        }
        if (candidate == nullptr || entry.last_used_tick < candidate->last_used_tick) {
            candidate = &entry;
        }
    }
    return candidate;
}

bool AppImageSurfaceCache::resolve_or_request(AppRuntimeHost& host,
                                              ImageDecodeMock& decoder,
                                              const std::string& url,
                                              std::uint32_t* handle) {
    if (handle != nullptr) {
        *handle = 0;
    }
    if (url.empty()) {
        return false;
    }

    Entry* entry = find_url(url);
    if (entry != nullptr) {
        if (entry->state == AppImageSurfaceState::Ready) {
            const HostHandleInfo* info = host.handles().lookup(entry->handle);
            if (info != nullptr && info->kind == HostServiceHandleKind::Surface &&
                info->app_instance_id == host.current_app_instance_id()) {
                if (handle != nullptr) {
                    *handle = entry->handle;
                }
                entry->last_used_tick = use_tick_++;
                return true;
            }
            entry->state = AppImageSurfaceState::Missing;
            entry->handle = 0;
            entry->job_id = 0;
            entry->decoded_bytes = 0;
        } else if (entry->state == AppImageSurfaceState::Pending ||
                   entry->state == AppImageSurfaceState::Failed) {
            return false;
        }
    }

    const AppServiceSubmitResult submitted = decoder.submit_decode(host, url);
    if (!submitted.accepted()) {
        if (entry == nullptr) {
            entries_.push_back(Entry{url});
            entry = &entries_.back();
        }
        entry->app_instance_id = host.current_app_instance_id();
        entry->state = AppImageSurfaceState::Failed;
        entry->handle = 0;
        entry->job_id = 0;
        entry->decoded_bytes = 0;
        entry->submit_status = submitted.status;
        entry->status = submitted.rejected_status;
        entry->error_code = 0;
        if (submitted.status == AppServiceSubmitStatus::QueueFull ||
            submitted.status == AppServiceSubmitStatus::BudgetExceeded) {
            entry->state = AppImageSurfaceState::Missing;
            return false;
        }
        return false;
    }

    if (entry == nullptr) {
        entries_.push_back(Entry{url});
        entry = &entries_.back();
    }
    entry->app_instance_id = host.current_app_instance_id();
    entry->job_id = submitted.job_id;
    entry->handle = 0;
    entry->decoded_bytes = 0;
    entry->submit_status = AppServiceSubmitStatus::Accepted;
    entry->status = HostServiceStatus::Failed;
    entry->error_code = 0;
    entry->state = AppImageSurfaceState::Pending;
    return false;
}

bool AppImageSurfaceCache::handle_completion(const HostServiceCompletion& completion) {
    if (completion.kind != HostServiceJobKind::ImageDecode) {
        return false;
    }
    Entry* entry = find_job(completion.job_id);
    if (entry == nullptr) {
        return false;
    }
    entry->status = completion.status;
    entry->error_code = completion.error_code;
    entry->app_instance_id = completion.app_instance_id;
    entry->submit_status = AppServiceSubmitStatus::Accepted;
    if (completion.status == HostServiceStatus::Completed && completion.handle != 0) {
        entry->handle = completion.handle;
        entry->decoded_bytes = completion.byte_count;
        entry->last_used_tick = use_tick_++;
        entry->state = AppImageSurfaceState::Ready;
    } else {
        entry->handle = 0;
        entry->decoded_bytes = 0;
        entry->state = AppImageSurfaceState::Failed;
    }
    return true;
}

std::size_t AppImageSurfaceCache::evict_unreferenced(AppRuntimeHost& host,
                                                     ImageDecodeMock& decoder,
                                                     const std::uint32_t* protected_handles,
                                                     std::size_t protected_handle_count) {
    std::size_t released = 0;
    while (over_budget()) {
        Entry* victim = least_recently_used_unprotected(protected_handles, protected_handle_count);
        if (victim == nullptr) {
            break;
        }
        const std::uint32_t handle = victim->handle;
        if (handle == 0 || !decoder.release_surface(host, handle)) {
            break;
        }
        ++released;
        entries_.erase(std::remove_if(entries_.begin(),
                                      entries_.end(),
                                      [handle](const Entry& entry) {
                                          return entry.state == AppImageSurfaceState::Ready && entry.handle == handle;
                                      }),
                       entries_.end());
    }
    return released;
}

std::size_t AppImageSurfaceCache::release_all(AppRuntimeHost& host, ImageDecodeMock& decoder) {
    std::size_t released = 0;
    for (Entry& entry : entries_) {
        if (entry.state == AppImageSurfaceState::Ready && entry.handle != 0 &&
            decoder.release_surface(host, entry.handle)) {
            ++released;
        }
    }
    clear();
    return released;
}

void AppImageSurfaceCache::clear() {
    entries_.clear();
}

AppImageSurfaceState AppImageSurfaceCache::state_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    return entry == nullptr ? AppImageSurfaceState::Missing : entry->state;
}

std::string AppImageSurfaceCache::url_for_job(std::uint32_t job_id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [job_id](const Entry& entry) {
        return entry.job_id == job_id;
    });
    return found == entries_.end() ? std::string{} : found->url;
}

AppServiceSubmitStatus AppImageSurfaceCache::last_submit_status_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    return entry == nullptr ? AppServiceSubmitStatus::InvalidInput : entry->submit_status;
}

HostServiceStatus AppImageSurfaceCache::last_host_status_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    return entry == nullptr ? HostServiceStatus::Failed : entry->status;
}

std::uint32_t AppImageSurfaceCache::last_error_code_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    return entry == nullptr ? 0 : entry->error_code;
}

AppImageFailureReason AppImageSurfaceCache::last_failure_reason_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    if (entry == nullptr) {
        return AppImageFailureReason::InvalidSource;
    }
    if (entry->state == AppImageSurfaceState::Ready) {
        return AppImageFailureReason::None;
    }
    return classify_app_image_failure(entry->submit_status, entry->status, entry->error_code);
}

std::string AppImageSurfaceCache::diagnostic_detail_for_url(const std::string& url) const {
    const Entry* entry = find_url(url);
    if (entry == nullptr) {
        return app_image_failure_detail(url, AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed, 0) +
            "; state=missing";
    }

    std::ostringstream stream;
    stream << "src=" << (entry->url.empty() ? "unknown" : entry->url)
           << "; state=" << app_image_surface_state_name(entry->state)
           << "; reason="
           << app_image_failure_reason_name(classify_app_image_failure(entry->submit_status,
                                                                       entry->status,
                                                                       entry->error_code))
           << "; submit=" << app_service_submit_status_name(entry->submit_status);
    if (entry->status != HostServiceStatus::Completed) {
        stream << "; host=" << host_service_status_name(entry->status);
    }
    if (entry->error_code != 0) {
        stream << "; error=" << entry->error_code;
    }
    if (entry->job_id != 0) {
        stream << "; job=" << entry->job_id;
    }
    if (entry->handle != 0) {
        stream << "; handle=" << entry->handle;
    }
    if (entry->decoded_bytes != 0) {
        stream << "; bytes=" << entry->decoded_bytes;
    }
    return stream.str();
}

AppPrivateKvStorageMock::AppPrivateKvStorageMock(AppPrivateKvPolicy policy)
    : policy_(policy) {}

void AppPrivateKvStorageMock::set_policy(AppPrivateKvPolicy policy) {
    policy_ = policy;
}

bool AppPrivateKvStorageMock::valid_key(const std::string& key) const {
    return !key.empty() && key.size() <= policy_.max_key_bytes;
}

bool AppPrivateKvStorageMock::can_store(const AppSpace& space,
                                        const std::string& key,
                                        const std::string& value) const {
    if (value.size() > policy_.max_value_bytes) {
        return false;
    }
    const auto existing = space.values.find(key);
    const std::size_t existing_bytes =
        existing == space.values.end() ? 0 : existing->first.size() + existing->second.size();
    if (existing == space.values.end() && space.values.size() >= policy_.max_items_per_app) {
        return false;
    }
    const std::size_t next_bytes = space.bytes - existing_bytes + key.size() + value.size();
    return next_bytes <= policy_.max_bytes_per_app;
}

AppServiceSubmitResult AppPrivateKvStorageMock::submit(AppRuntimeHost& host,
                                                       AppPrivateKvOperation operation,
                                                       std::string key,
                                                       std::string value) {
    if (host.current_app_instance_id() == 0) {
        return rejected(AppServiceSubmitStatus::EmptyInstance, HostServiceStatus::Cancelled);
    }
    if (!policy_.enabled) {
        return rejected(AppServiceSubmitStatus::CapabilityDenied, HostServiceStatus::Unsupported);
    }
    if (operation != AppPrivateKvOperation::Clear && !valid_key(key)) {
        return rejected(AppServiceSubmitStatus::InvalidInput, HostServiceStatus::Failed);
    }
    if (operation == AppPrivateKvOperation::Set && value.size() > policy_.max_value_bytes) {
        return rejected(AppServiceSubmitStatus::BudgetExceeded, HostServiceStatus::BudgetExceeded);
    }

    const HostServiceSubmitResult submitted = host.submit_current(HostServiceJobKind::StorageKv);
    AppServiceSubmitResult result = from_submit(submitted);
    if (!result.accepted()) {
        return result;
    }
    pending_.push_back(PendingOp{
        result.job_id,
        host.current_app_instance_id(),
        host.current().app_id,
        operation,
        std::move(key),
        std::move(value),
    });
    return result;
}

AppServiceSubmitResult AppPrivateKvStorageMock::submit_get(AppRuntimeHost& host, const std::string& key) {
    return submit(host, AppPrivateKvOperation::Get, key);
}

AppServiceSubmitResult AppPrivateKvStorageMock::submit_set(AppRuntimeHost& host,
                                                           const std::string& key,
                                                           const std::string& value) {
    return submit(host, AppPrivateKvOperation::Set, key, value);
}

AppServiceSubmitResult AppPrivateKvStorageMock::submit_remove(AppRuntimeHost& host, const std::string& key) {
    return submit(host, AppPrivateKvOperation::Remove, key);
}

AppServiceSubmitResult AppPrivateKvStorageMock::submit_clear(AppRuntimeHost& host) {
    return submit(host, AppPrivateKvOperation::Clear, {});
}

AppLocalStorageShadow::AppLocalStorageShadow(AppPrivateKvPolicy policy)
    : policy_(policy) {}

void AppLocalStorageShadow::set_policy(AppPrivateKvPolicy policy) {
    policy_ = policy;
    clear();
}

bool AppLocalStorageShadow::valid_key(const std::string& key) const {
    return !key.empty() && key.size() <= policy_.max_key_bytes;
}

bool AppLocalStorageShadow::can_store(std::size_t existing_index,
                                      const std::string& key,
                                      const std::string& value) const {
    if (value.size() > policy_.max_value_bytes) {
        return false;
    }
    const bool exists = existing_index < entries_.size();
    if (!exists && entries_.size() >= policy_.max_items_per_app) {
        return false;
    }
    const std::size_t existing_bytes = exists
        ? entries_[existing_index].key.size() + entries_[existing_index].value.size()
        : 0;
    const std::size_t next_bytes = bytes_ - existing_bytes + key.size() + value.size();
    return next_bytes <= policy_.max_bytes_per_app;
}

AppLocalStorageStatus AppLocalStorageShadow::get_item(const std::string& key, std::string* value) const {
    if (!policy_.enabled) {
        return AppLocalStorageStatus::Disabled;
    }
    if (!valid_key(key)) {
        return AppLocalStorageStatus::InvalidKey;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&key](const Entry& entry) {
        return entry.key == key;
    });
    if (found == entries_.end()) {
        return AppLocalStorageStatus::NotFound;
    }
    if (value != nullptr) {
        *value = found->value;
    }
    return AppLocalStorageStatus::Ok;
}

AppLocalStorageStatus AppLocalStorageShadow::set_item(const std::string& key, const std::string& value) {
    if (!policy_.enabled) {
        return AppLocalStorageStatus::Disabled;
    }
    if (!valid_key(key)) {
        return AppLocalStorageStatus::InvalidKey;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&key](const Entry& entry) {
        return entry.key == key;
    });
    const std::size_t index = found == entries_.end()
        ? entries_.size()
        : static_cast<std::size_t>(found - entries_.begin());
    if (!can_store(index, key, value)) {
        return AppLocalStorageStatus::BudgetExceeded;
    }
    if (found == entries_.end()) {
        entries_.push_back(Entry{key, value});
    } else {
        bytes_ -= found->key.size() + found->value.size();
        found->value = value;
    }
    bytes_ += key.size() + value.size();
    return AppLocalStorageStatus::Ok;
}

AppLocalStorageStatus AppLocalStorageShadow::remove_item(const std::string& key) {
    if (!policy_.enabled) {
        return AppLocalStorageStatus::Disabled;
    }
    if (!valid_key(key)) {
        return AppLocalStorageStatus::InvalidKey;
    }
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&key](const Entry& entry) {
        return entry.key == key;
    });
    if (found == entries_.end()) {
        return AppLocalStorageStatus::NotFound;
    }
    bytes_ -= found->key.size() + found->value.size();
    entries_.erase(found);
    return AppLocalStorageStatus::Ok;
}

void AppLocalStorageShadow::clear() {
    entries_.clear();
    bytes_ = 0;
}

std::size_t AppLocalStorageShadow::length() const {
    return entries_.size();
}

AppLocalStorageStatus AppLocalStorageShadow::key(std::size_t index, std::string* key) const {
    if (!policy_.enabled) {
        return AppLocalStorageStatus::Disabled;
    }
    if (index >= entries_.size()) {
        return AppLocalStorageStatus::NotFound;
    }
    if (key != nullptr) {
        *key = entries_[index].key;
    }
    return AppLocalStorageStatus::Ok;
}

std::size_t AppLocalStorageShadow::used_bytes() const {
    return bytes_;
}

HostServiceStatus AppPrivateKvStorageMock::apply(const PendingOp& op,
                                                 AppRuntimeHost& host,
                                                 std::uint32_t& handle,
                                                 std::uint32_t& byte_count) {
    if (op.operation == AppPrivateKvOperation::Set) {
        AppSpace& space = spaces_[op.app_id];
        if (!can_store(space, op.key, op.value)) {
            return HostServiceStatus::BudgetExceeded;
        }
        const auto existing = space.values.find(op.key);
        if (existing != space.values.end()) {
            space.bytes -= existing->first.size() + existing->second.size();
        }
        space.values[op.key] = op.value;
        space.bytes += op.key.size() + op.value.size();
        byte_count = static_cast<std::uint32_t>(op.value.size());
        return HostServiceStatus::Completed;
    }
    const auto space_found = spaces_.find(op.app_id);
    if (space_found == spaces_.end()) {
        return op.operation == AppPrivateKvOperation::Get ? HostServiceStatus::Failed : HostServiceStatus::Completed;
    }
    AppSpace& space = space_found->second;
    if (op.operation == AppPrivateKvOperation::Get) {
        const auto found = space.values.find(op.key);
        if (found == space.values.end()) {
            return HostServiceStatus::Failed;
        }
        handle = host.handles().allocate(HostServiceHandleKind::StorageValue,
                                         op.app_instance_id,
                                         static_cast<std::uint32_t>(found->second.size()));
        if (handle == 0) {
            return HostServiceStatus::BudgetExceeded;
        }
        byte_count = static_cast<std::uint32_t>(found->second.size());
        records_.push_back(AppPrivateKvRecord{handle, op.app_instance_id, op.app_id, op.key, found->second});
        return HostServiceStatus::Completed;
    }
    if (op.operation == AppPrivateKvOperation::Remove) {
        const auto found = space.values.find(op.key);
        if (found != space.values.end()) {
            space.bytes -= found->first.size() + found->second.size();
            space.values.erase(found);
            if (space.values.empty()) {
                spaces_.erase(space_found);
            }
        }
        return HostServiceStatus::Completed;
    }
    if (op.operation == AppPrivateKvOperation::Clear) {
        spaces_.erase(space_found);
        return HostServiceStatus::Completed;
    }
    return HostServiceStatus::Failed;
}

bool AppPrivateKvStorageMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(HostServiceJobKind::StorageKv, request)) {
        return false;
    }
    const auto pending = find_job(pending_, request.job_id);
    if (pending == pending_.end()) {
        return host.push_completion(make_cancelled_completion(request));
    }
    std::uint32_t handle = 0;
    std::uint32_t byte_count = 0;
    const HostServiceStatus status = apply(*pending, host, handle, byte_count);
    const bool pushed = host.push_completion(HostServiceCompletion{
        request.job_id,
        HostServiceJobKind::StorageKv,
        status,
        request.app_instance_id,
        handle,
        0,
        byte_count,
    });
    pending_.erase(pending);
    return pushed;
}

const AppPrivateKvRecord* AppPrivateKvStorageMock::value(std::uint32_t handle) const {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppPrivateKvRecord& record) {
        return record.handle == handle;
    });
    return found == records_.end() ? nullptr : &*found;
}

bool AppPrivateKvStorageMock::release_value(AppRuntimeHost& host, std::uint32_t handle) {
    const auto found = std::find_if(records_.begin(), records_.end(), [handle](const AppPrivateKvRecord& record) {
        return record.handle == handle;
    });
    if (found == records_.end()) {
        return false;
    }
    records_.erase(found);
    return host.handles().release(handle);
}

std::size_t AppPrivateKvStorageMock::clear_app(const std::string& app_id) {
    const auto found = spaces_.find(app_id);
    if (found == spaces_.end()) {
        return 0;
    }
    const std::size_t count = found->second.values.size();
    spaces_.erase(found);
    return count;
}

} // namespace jellyframe
