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

    bool accepted() const {
        return status == AppServiceSubmitStatus::Accepted;
    }
};

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

std::size_t decoded_surface_byte_count(int width,
                                       int height,
                                       int stride_pixels,
                                       HostPixelFormat pixel_format);

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
    bool resolve_or_request(AppRuntimeHost& host,
                            ImageDecodeMock& decoder,
                            const std::string& url,
                            std::uint32_t* handle);
    bool handle_completion(const HostServiceCompletion& completion);
    std::size_t release_all(AppRuntimeHost& host, ImageDecodeMock& decoder);
    void clear();

    AppImageSurfaceState state_for_url(const std::string& url) const;
    std::size_t size() const {
        return entries_.size();
    }

private:
    struct Entry {
        std::string url;
        std::uint32_t app_instance_id = 0;
        std::uint32_t job_id = 0;
        std::uint32_t handle = 0;
        HostServiceStatus status = HostServiceStatus::Failed;
        std::uint32_t error_code = 0;
        AppImageSurfaceState state = AppImageSurfaceState::Missing;
    };

    Entry* find_url(const std::string& url);
    const Entry* find_url(const std::string& url) const;
    Entry* find_job(std::uint32_t job_id);

    std::vector<Entry> entries_;
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
};

struct AppServicePolicies {
    NetworkFetchPolicy network;
    AppPrivateKvPolicy storage;
    ImageDecodePolicy image;
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

enum class AppLocalStorageStatus {
    Ok,
    Disabled,
    InvalidKey,
    BudgetExceeded,
    NotFound,
};

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
    HostServiceStatus apply(const PendingOp& op, AppRuntimeHost& host, std::uint32_t& handle, std::uint32_t& byte_count);
    bool valid_key(const std::string& key) const;
    bool can_store(const AppSpace& space, const std::string& key, const std::string& value) const;

    AppPrivateKvPolicy policy_;
    std::vector<PendingOp> pending_;
    std::unordered_map<std::string, AppSpace> spaces_;
    std::vector<AppPrivateKvRecord> records_;
};

} // namespace jellyframe
