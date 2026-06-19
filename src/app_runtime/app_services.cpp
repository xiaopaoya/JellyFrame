#include "app_runtime/app_services.h"

#include <algorithm>

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
    if (!host.pop_worker_request(request) || request.kind != HostServiceJobKind::NetworkFetch) {
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
        return false;
    }
    records_.erase(found);
    return host.handles().release(handle);
}

void NetworkFetchMock::clear() {
    fixtures_.clear();
    pending_.clear();
    records_.clear();
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

HostServiceStatus AppPrivateKvStorageMock::apply(const PendingOp& op,
                                                 AppRuntimeHost& host,
                                                 std::uint32_t& handle,
                                                 std::uint32_t& byte_count) {
    AppSpace& space = spaces_[op.app_id];
    if (op.operation == AppPrivateKvOperation::Set) {
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
        }
        return HostServiceStatus::Completed;
    }
    if (op.operation == AppPrivateKvOperation::Clear) {
        space.values.clear();
        space.bytes = 0;
        return HostServiceStatus::Completed;
    }
    return HostServiceStatus::Failed;
}

bool AppPrivateKvStorageMock::complete_next(AppRuntimeHost& host) {
    HostServiceRequest request;
    if (!host.pop_worker_request(request) || request.kind != HostServiceJobKind::StorageKv) {
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
