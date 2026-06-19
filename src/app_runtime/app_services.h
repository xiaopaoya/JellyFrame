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
};

struct AppServicePolicies {
    NetworkFetchPolicy network;
    AppPrivateKvPolicy storage;
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
