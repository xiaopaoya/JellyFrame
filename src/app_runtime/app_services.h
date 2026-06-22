#pragma once

#include "app_runtime/app_host.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace jellyframe {

enum class AppServiceSubmitStatus {
    Accepted,
    EmptyInstance,
    CapabilityDenied,
    InvalidInput,
    QueueFull,
    BudgetExceeded,
};

struct AppServiceSubmitResult {
    AppServiceSubmitStatus status = AppServiceSubmitStatus::InvalidInput;
    std::uint32_t job_id = 0;
    HostServiceStatus rejected_status = HostServiceStatus::Failed;
    std::uint32_t error_code = 0;

    bool accepted() const {
        return status == AppServiceSubmitStatus::Accepted;
    }
};

const char* app_service_submit_status_name(AppServiceSubmitStatus status);
const char* host_service_status_name(HostServiceStatus status);

struct NetworkFetchPolicy {
    bool enabled = false;
    std::size_t max_url_bytes = 256;
    std::size_t max_response_bytes = 4096;
};

struct NetworkFetchFixture {
    std::string url;
    std::uint16_t status_code = 200;
    std::string content_type = "application/json";
    std::string body;
};

struct NetworkFetchRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::uint16_t status_code = 0;
    std::string content_type;
    std::string body;
};

enum class AppNetworkFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidUrl,
    QueueFull,
    ResourceNotFound,
    Offline,
    ResponseBudgetExceeded,
    ResponseHandleBudgetExceeded,
    RequestFailed,
    RequestCancelled,
    RequestTimeout,
    Unsupported,
    Unknown,
};

const char* app_network_failure_reason_name(AppNetworkFailureReason reason);
AppNetworkFailureReason classify_app_network_failure(AppServiceSubmitStatus submit_status,
                                                     HostServiceStatus host_status,
                                                     std::uint32_t error_code);
std::string app_network_failure_detail(const std::string& url,
                                       AppServiceSubmitStatus submit_status,
                                       HostServiceStatus host_status,
                                       std::uint32_t error_code);

class NetworkFetchMock {
public:
    explicit NetworkFetchMock(NetworkFetchPolicy policy = {});

    void set_policy(NetworkFetchPolicy policy);
    bool add_fixture(NetworkFetchFixture fixture);
    AppServiceSubmitResult submit_fetch(AppRuntimeHost& host,
                                        const std::string& url,
                                        std::uint32_t timeout_ms = 0);
    bool complete_next(AppRuntimeHost& host);
    const NetworkFetchRecord* response(std::uint32_t handle) const;
    bool release_response(AppRuntimeHost& host, std::uint32_t handle);
    void clear();

private:
    struct PendingFetch {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        NetworkFetchFixture fixture;
    };

    NetworkFetchPolicy policy_;
    std::vector<NetworkFetchFixture> fixtures_;
    std::vector<PendingFetch> pending_;
    std::vector<NetworkFetchRecord> records_;
};

struct ImageDecodePolicy {
    bool enabled = false;
    std::size_t max_url_bytes = 256;
    int max_width = 0;
    int max_height = 0;
    std::size_t max_decoded_bytes = 0;
    std::size_t max_pending_decodes = 1;
};

struct ImageDecodeFixture {
    std::string url;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    HostPixelFormat pixel_format = HostPixelFormat::Unknown;
    std::vector<std::uint8_t> pixels;
};

struct AppDecodedSurfaceRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::string url;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
    HostPixelFormat pixel_format = HostPixelFormat::Unknown;
    std::vector<std::uint8_t> pixels;
};

enum class AppImageSurfaceState {
    Missing,
    Pending,
    Ready,
    Failed,
};

enum class AppImageFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidSource,
    QueueFull,
    PendingBudget,
    ResourceNotFound,
    DecodeBudgetExceeded,
    SurfaceBudgetExceeded,
    DecodeFailed,
    DecodeCancelled,
    DecodeTimeout,
    Unsupported,
    Unknown,
};

struct AppImageSurfaceCacheOptions {
    std::size_t max_ready_surfaces = 0;
    std::size_t max_ready_bytes = 0;
};

struct AppImageSurfaceEvictionResult {
    std::size_t released_surfaces = 0;
    std::size_t dropped_stale_entries = 0;
};

std::size_t decoded_surface_byte_count(int width,
                                       int height,
                                       int stride_pixels,
                                       HostPixelFormat pixel_format);
const char* app_image_surface_state_name(AppImageSurfaceState state);
const char* app_image_failure_reason_name(AppImageFailureReason reason);
AppImageFailureReason classify_app_image_failure(AppServiceSubmitStatus submit_status,
                                                 HostServiceStatus host_status,
                                                 std::uint32_t error_code);
std::string app_image_failure_detail(const std::string& url,
                                     AppServiceSubmitStatus submit_status,
                                     HostServiceStatus host_status,
                                     std::uint32_t error_code);

class ImageDecodeMock {
public:
    explicit ImageDecodeMock(ImageDecodePolicy policy = {});

    void set_policy(ImageDecodePolicy policy);
    bool add_fixture(ImageDecodeFixture fixture);
    AppServiceSubmitResult submit_decode(AppRuntimeHost& host,
                                         const std::string& url,
                                         std::uint32_t timeout_ms = 0);
    bool complete_next(AppRuntimeHost& host);
    const AppDecodedSurfaceRecord* surface(std::uint32_t handle) const;
    bool release_surface(AppRuntimeHost& host, std::uint32_t handle);
    void clear();

private:
    struct PendingDecode {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        std::size_t fixture_index = 0;
    };

    bool valid_fixture(const ImageDecodeFixture& fixture) const;

    ImageDecodePolicy policy_;
    std::vector<ImageDecodeFixture> fixtures_;
    std::vector<PendingDecode> pending_;
    std::vector<AppDecodedSurfaceRecord> records_;
};

class AppImageSurfaceCache {
public:
    explicit AppImageSurfaceCache(AppImageSurfaceCacheOptions options = {});

    void set_options(AppImageSurfaceCacheOptions options);
    bool resolve_or_request(AppRuntimeHost& host,
                            ImageDecodeMock& decoder,
                            const std::string& url,
                            std::uint32_t* handle);
    bool handle_completion(const HostServiceCompletion& completion);
    AppImageSurfaceEvictionResult evict_unreferenced_with_result(AppRuntimeHost& host,
                                                                 ImageDecodeMock& decoder,
                                                                 const std::uint32_t* protected_handles = nullptr,
                                                                 std::size_t protected_handle_count = 0);
    std::size_t evict_unreferenced(AppRuntimeHost& host,
                                   ImageDecodeMock& decoder,
                                   const std::uint32_t* protected_handles = nullptr,
                                   std::size_t protected_handle_count = 0);
    std::size_t release_all(AppRuntimeHost& host, ImageDecodeMock& decoder);
    void clear();

    AppImageSurfaceState state_for_url(const std::string& url) const;
    std::string url_for_job(std::uint32_t job_id) const;
    AppServiceSubmitStatus last_submit_status_for_url(const std::string& url) const;
    HostServiceStatus last_host_status_for_url(const std::string& url) const;
    std::uint32_t last_error_code_for_url(const std::string& url) const;
    AppImageFailureReason last_failure_reason_for_url(const std::string& url) const;
    std::string diagnostic_detail_for_url(const std::string& url) const;
    std::size_t size() const {
        return entries_.size();
    }
    std::size_t ready_surface_count() const;
    std::size_t ready_byte_count() const;

private:
    struct Entry {
        std::string url;
        std::uint32_t app_instance_id = 0;
        std::uint32_t job_id = 0;
        std::uint32_t handle = 0;
        std::size_t decoded_bytes = 0;
        AppServiceSubmitStatus submit_status = AppServiceSubmitStatus::InvalidInput;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        std::uint32_t last_used_tick = 0;
        AppImageSurfaceState state = AppImageSurfaceState::Missing;
    };

    Entry* find_url(const std::string& url);
    const Entry* find_url(const std::string& url) const;
    Entry* find_job(std::uint32_t job_id);
    bool over_budget() const;
    Entry* least_recently_used_unprotected(const std::uint32_t* protected_handles,
                                           std::size_t protected_handle_count);

    AppImageSurfaceCacheOptions options_;
    std::vector<Entry> entries_;
    std::uint32_t use_tick_ = 1;
};

enum class AudioCommandKind {
    Open,
    Play,
    Pause,
    Stop,
    Close,
    SetVolume,
};

enum class AudioStreamState {
    Open,
    Playing,
    Paused,
    Stopped,
    Ended,
    Error,
};

enum class AppAudioFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidSource,
    InvalidHandle,
    QueueFull,
    StreamBudget,
    SourceNotFound,
    StreamBudgetExceeded,
    CommandFailed,
    CommandCancelled,
    CommandTimeout,
    Unsupported,
    Unknown,
};

struct AudioPlaybackPolicy {
    bool enabled = false;
    std::size_t max_source_url_bytes = 256;
    std::size_t max_audio_streams = 1;
};

struct AudioSourceFixture {
    std::string url;
    std::uint32_t duration_ms = 0;
};

struct AudioStreamRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::string url;
    AudioStreamState state = AudioStreamState::Open;
    std::uint8_t volume = 100;
    std::uint32_t duration_ms = 0;
};

const char* audio_command_kind_name(AudioCommandKind kind);
const char* audio_stream_state_name(AudioStreamState state);
const char* app_audio_failure_reason_name(AppAudioFailureReason reason);
AppAudioFailureReason classify_app_audio_failure(AppServiceSubmitStatus submit_status,
                                                 HostServiceStatus host_status,
                                                 std::uint32_t error_code);
std::string app_audio_failure_detail(const std::string& source,
                                     AppServiceSubmitStatus submit_status,
                                     HostServiceStatus host_status,
                                     std::uint32_t error_code);

class AudioCommandMock {
public:
    explicit AudioCommandMock(AudioPlaybackPolicy policy = {});

    void set_policy(AudioPlaybackPolicy policy);
    bool add_source(AudioSourceFixture fixture);
    AppServiceSubmitResult submit_open(AppRuntimeHost& host,
                                       const std::string& url,
                                       std::uint8_t volume = 100,
                                       std::uint32_t timeout_ms = 0);
    AppServiceSubmitResult submit_play(AppRuntimeHost& host, std::uint32_t audio_handle);
    AppServiceSubmitResult submit_pause(AppRuntimeHost& host, std::uint32_t audio_handle);
    AppServiceSubmitResult submit_stop(AppRuntimeHost& host, std::uint32_t audio_handle);
    AppServiceSubmitResult submit_close(AppRuntimeHost& host, std::uint32_t audio_handle);
    AppServiceSubmitResult submit_set_volume(AppRuntimeHost& host,
                                             std::uint32_t audio_handle,
                                             std::uint8_t volume);
    bool complete_next(AppRuntimeHost& host);
    bool post_ended(AppRuntimeHost& host, std::uint32_t audio_handle);
    bool post_error(AppRuntimeHost& host, std::uint32_t audio_handle, std::uint32_t error_code);
    const AudioStreamRecord* stream(std::uint32_t audio_handle) const;
    bool release_stream(AppRuntimeHost& host, std::uint32_t audio_handle);
    std::size_t release_app_streams(AppRuntimeHost& host, std::uint32_t app_instance_id);
    std::size_t collect_released_streams(const AppRuntimeHost& host);
    void clear();

private:
    struct PendingCommand {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        AudioCommandKind command = AudioCommandKind::Open;
        std::uint32_t audio_handle = 0;
        std::string url;
        std::uint8_t volume = 100;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        std::uint32_t duration_ms = 0;
    };

    AppServiceSubmitResult submit_command(AppRuntimeHost& host,
                                          AudioCommandKind command,
                                          std::uint32_t audio_handle = 0,
                                          std::string url = {},
                                          std::uint8_t volume = 100,
                                          std::uint32_t timeout_ms = 0);
    AudioStreamRecord* find_stream(std::uint32_t audio_handle);
    const AudioStreamRecord* find_stream(std::uint32_t audio_handle) const;
    std::size_t active_stream_count(std::uint32_t app_instance_id = 0) const;
    std::size_t pending_open_count(std::uint32_t app_instance_id = 0) const;
    bool valid_handle_for_current_app(const AppRuntimeHost& host, std::uint32_t audio_handle) const;
    bool push_state_completion(AppRuntimeHost& host,
                               std::uint32_t job_id,
                               std::uint32_t app_instance_id,
                               HostServiceStatus status,
                               std::uint32_t audio_handle,
                               AudioStreamState state,
                               std::uint32_t error_code = 0);

    AudioPlaybackPolicy policy_;
    std::vector<AudioSourceFixture> sources_;
    std::vector<PendingCommand> pending_;
    std::vector<AudioStreamRecord> streams_;
};

struct AppPrivateKvPolicy {
    bool enabled = false;
    std::size_t max_key_bytes = 64;
    std::size_t max_value_bytes = 1024;
    std::size_t max_items_per_app = 32;
    std::size_t max_bytes_per_app = 4096;
};

struct AppServiceManifestCapabilities {
    bool network_fetch = false;
    bool storage_kv = false;
    bool image_decode = false;
    bool audio_playback = false;
};

struct AppServiceHostProfile {
    bool allow_network_fetch = false;
    std::size_t max_network_url_bytes = 256;
    std::size_t max_network_response_bytes = 4096;

    bool allow_storage_kv = false;
    std::size_t max_storage_key_bytes = 64;
    std::size_t max_storage_value_bytes = 1024;
    std::size_t max_storage_items_per_app = 32;
    std::size_t max_storage_bytes_per_app = 4096;

    bool allow_image_decode = false;
    std::size_t max_image_url_bytes = 256;
    int max_image_width = 0;
    int max_image_height = 0;
    std::size_t max_decoded_image_bytes = 0;
    std::size_t max_pending_image_decodes = 1;

    bool allow_audio_playback = false;
    std::size_t max_audio_source_url_bytes = 256;
    std::size_t max_audio_streams = 1;
};

struct AppServicePolicies {
    NetworkFetchPolicy network;
    AppPrivateKvPolicy storage;
    ImageDecodePolicy image;
    AudioPlaybackPolicy audio;
};

AppServiceHostProfile app_service_host_profile_from_capabilities(
    const HostDeviceCapabilities& capabilities,
    const AppPrivateKvPolicy& storage_policy = {});

AppServicePolicies app_service_policies_for_app(const AppServiceManifestCapabilities& manifest,
                                                const AppServiceHostProfile& profile);

enum class AppPrivateKvOperation {
    Get,
    Set,
    Remove,
    Clear,
};

enum class AppStorageFailureReason {
    None,
    EmptyInstance,
    CapabilityDenied,
    InvalidKey,
    QueueFull,
    ValueBudget,
    QuotaExceeded,
    NotFound,
    HandleBudgetExceeded,
    OperationFailed,
    OperationCancelled,
    OperationTimeout,
    Unsupported,
    Unknown,
};

enum class AppLocalStorageStatus {
    Ok,
    Disabled,
    InvalidKey,
    BudgetExceeded,
    NotFound,
};

const char* app_private_kv_operation_name(AppPrivateKvOperation operation);
const char* app_local_storage_status_name(AppLocalStorageStatus status);
const char* app_storage_failure_reason_name(AppStorageFailureReason reason);
AppStorageFailureReason classify_app_storage_failure(AppServiceSubmitStatus submit_status,
                                                     HostServiceStatus host_status,
                                                     std::uint32_t error_code);
AppStorageFailureReason classify_app_local_storage_failure(AppLocalStorageStatus status);
std::string app_storage_failure_detail(AppPrivateKvOperation operation,
                                       const std::string& key,
                                       AppServiceSubmitStatus submit_status,
                                       HostServiceStatus host_status,
                                       std::uint32_t error_code);

class AppLocalStorageShadow {
public:
    explicit AppLocalStorageShadow(AppPrivateKvPolicy policy = {});

    void set_policy(AppPrivateKvPolicy policy);
    AppLocalStorageStatus get_item(const std::string& key, std::string* value) const;
    AppLocalStorageStatus set_item(const std::string& key, const std::string& value);
    AppLocalStorageStatus remove_item(const std::string& key);
    void clear();
    std::size_t length() const;
    AppLocalStorageStatus key(std::size_t index, std::string* key) const;
    std::size_t used_bytes() const;

private:
    struct Entry {
        std::string key;
        std::string value;
    };

    bool valid_key(const std::string& key) const;
    bool can_store(std::size_t existing_index, const std::string& key, const std::string& value) const;

    AppPrivateKvPolicy policy_;
    std::vector<Entry> entries_;
    std::size_t bytes_ = 0;
};

struct AppPrivateKvRecord {
    std::uint32_t handle = 0;
    std::uint32_t app_instance_id = 0;
    std::string app_id;
    std::string key;
    std::string value;
};

struct AppPrivateKvFlushResult {
    std::size_t flushed = 0;
    std::size_t remaining_pending = 0;
    bool stopped_before_empty = false;
};

class AppPrivateKvStorageMock {
public:
    explicit AppPrivateKvStorageMock(AppPrivateKvPolicy policy = {});

    void set_policy(AppPrivateKvPolicy policy);
    AppServiceSubmitResult submit_get(AppRuntimeHost& host, const std::string& key);
    AppServiceSubmitResult submit_set(AppRuntimeHost& host,
                                      const std::string& key,
                                      const std::string& value);
    AppServiceSubmitResult submit_remove(AppRuntimeHost& host, const std::string& key);
    AppServiceSubmitResult submit_clear(AppRuntimeHost& host);
    bool complete_next(AppRuntimeHost& host);
    const AppPrivateKvRecord* value(std::uint32_t handle) const;
    bool release_value(AppRuntimeHost& host, std::uint32_t handle);
    AppPrivateKvFlushResult flush_pending(AppRuntimeHost& host, std::size_t max_ops = 0);
    std::size_t pending_count() const;
    std::size_t pending_count_app_instance(std::uint32_t app_instance_id) const;
    std::size_t pending_count_app(const std::string& app_id) const;
    std::size_t drop_pending_app_instance(std::uint32_t app_instance_id);
    std::size_t drop_pending_app(const std::string& app_id);
    std::size_t clear_app(const std::string& app_id);

private:
    struct PendingOp {
        std::uint32_t job_id = 0;
        std::uint32_t app_instance_id = 0;
        std::string app_id;
        AppPrivateKvOperation operation = AppPrivateKvOperation::Get;
        std::string key;
        std::string value;
    };

    struct AppSpace {
        std::unordered_map<std::string, std::string> values;
        std::size_t bytes = 0;
    };

    AppServiceSubmitResult submit(AppRuntimeHost& host,
                                  AppPrivateKvOperation operation,
                                  std::string key,
                                  std::string value = {});
    HostServiceStatus apply(const PendingOp& op,
                            AppRuntimeHost& host,
                            std::uint32_t& handle,
                            std::uint32_t& byte_count,
                            std::uint32_t& error_code);
    bool valid_key(const std::string& key) const;
    bool can_store(const AppSpace& space, const std::string& key, const std::string& value) const;

    AppPrivateKvPolicy policy_;
    std::vector<PendingOp> pending_;
    std::unordered_map<std::string, AppSpace> spaces_;
    std::vector<AppPrivateKvRecord> records_;
};

} // namespace jellyframe
