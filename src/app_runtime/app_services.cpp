#include "app_runtime/app_services.h"

#include <algorithm>
#include <limits>

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

    profile.allow_image_decode = capabilities.media.supports_image_decode;
    profile.max_image_width = capabilities.media.max_image_width;
    profile.max_image_height = capabilities.media.max_image_height;
    profile.max_decoded_image_bytes = capabilities.media.max_decoded_image_bytes;
    profile.max_pending_image_decodes = capabilities.async.max_in_flight_jobs == 0
        ? profile.max_pending_image_decodes
        : capabilities.async.max_in_flight_jobs;
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
