#include "jellyframe_esp32s3_device_image_store.h"
#include "jellyframe_esp32s3_package_signature.h"

#include "esp_rom_crc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "mbedtls/sha256.h"

#include <algorithm>
#include <cstring>
#include <new>
#include <memory>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr std::uint32_t kRegistryMagic = 0x4a465247u; // JFRG
constexpr std::uint16_t kRegistryVersion = 3;
constexpr std::uint32_t kRegistryBytes = 4096;
constexpr std::uint32_t kRegistrySlots = 2;
constexpr std::uint8_t kBundleSlots = 3;
constexpr std::uint8_t kNoSlot = 0xff;
constexpr std::uint32_t kStorageHeaderBytes = kRegistryBytes * kRegistrySlots;
constexpr std::uint32_t kFlashSectorBytes = 4096;

enum class AcceptanceFaultPoint : int {
    None = 0,
    AfterBegin = 1,
    AfterFirstChunk = 2,
    AfterLastChunk = 3,
    DuringVerify = 4,
    BeforeRegistryPublish = 5,
    AfterRegistryPublish = 6,
    CorruptRegistryAtBoot = 7,
    RejectRegistryPublish = 8,
    RejectStagingAllocation = 9,
    CorruptPublishedEnvelope = 10,
    DowngradeRegistryVersion = 11,
};

bool has_acceptance_fault(AcceptanceFaultPoint point) {
#if CONFIG_JELLYFRAME_ESP32S3_RUN_DEVICE_IMAGE_LIFECYCLE_ACCEPTANCE && \
    defined(CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_TEST_FAULT_POINT)
    return CONFIG_JELLYFRAME_ESP32S3_DEVICE_IMAGE_TEST_FAULT_POINT == static_cast<int>(point);
#else
    (void)point;
    return false;
#endif
}

void restart_for_acceptance_fault(AcceptanceFaultPoint point) {
    if (has_acceptance_fault(point)) {
        esp_restart();
    }
}

std::uint32_t bundle_slot_bytes(const esp_partition_t* partition) {
    if (partition == nullptr || partition->size <= kStorageHeaderBytes) {
        return 0;
    }
    return ((partition->size - kStorageHeaderBytes) / kBundleSlots) & ~(kFlashSectorBytes - 1u);
}

std::uint32_t crc32(const void* bytes, std::size_t size) {
    return esp_rom_crc32_le(0, static_cast<const std::uint8_t*>(bytes), size);
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8u) |
           (static_cast<std::uint32_t>(p[2]) << 16u) | (static_cast<std::uint32_t>(p[3]) << 24u);
}

bool sha256(const std::uint8_t* bytes, std::size_t size, std::array<std::uint8_t, 32>& digest) {
    return bytes != nullptr && mbedtls_sha256(bytes, size, digest.data(), 0) == 0;
}

template <std::size_t N>
bool copy_string(std::array<char, N>& output, std::string_view input) {
    if (input.empty() || input.size() >= N || input.find('\0') != std::string_view::npos) {
        return false;
    }
    std::memset(output.data(), 0, output.size());
    std::memcpy(output.data(), input.data(), input.size());
    return true;
}

template <std::size_t N>
std::string_view string_view(const std::array<char, N>& value) {
    std::size_t length = 0;
    while (length < value.size() && value[length] != '\0') {
        ++length;
    }
    return std::string_view(value.data(), length);
}

} // namespace

struct DeviceImageStore::BundleRecord {
    std::uint8_t slot = kNoSlot;
    std::uint8_t reserved[3]{};
    std::uint32_t bundle_bytes = 0;
    std::uint32_t envelope_payload_offset = 0;
    std::uint32_t envelope_bytes = 0;
    std::uint32_t envelope_crc32 = 0;
    std::uint32_t bundle_crc32 = 0;
    std::uint32_t version_code = 0;
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
    std::array<char, kDeviceMaxVersionNameBytes + 1> version_name{};
};

struct DeviceImageStore::RegistryRecord {
    std::uint32_t magic = kRegistryMagic;
    std::uint16_t version = kRegistryVersion;
    std::uint16_t bytes = sizeof(RegistryRecord);
    std::uint32_t generation = 0;
    BundleRecord active{};
    BundleRecord rollback{};
    std::uint32_t crc32 = 0;
};

class DeviceImageStore::PartitionReader final : public DeviceBundleReader {
public:
    PartitionReader(const DeviceImageStore& store, std::uint8_t slot, std::uint32_t base_offset = 0)
        : store_(store), slot_(slot), base_offset_(base_offset) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        return offset <= UINT32_MAX - base_offset_ &&
               store_.read_slot_cached(slot_, base_offset_ + offset, output, size);
    }

private:
    const DeviceImageStore& store_;
    std::uint8_t slot_ = kNoSlot;
    std::uint32_t base_offset_ = 0;
};

class DeviceImageStore::Lease final : public AppInstalledBundleLease {
public:
    Lease(const DeviceImageStore& store, std::uint8_t slot, std::uint32_t payload_offset,
          const DeviceBundleDescriptor& descriptor,
          const std::array<std::array<char, kDeviceSignedPackageMaxPathBytes + 1>, 128>& paths,
          const std::array<std::array<std::uint8_t, 32>, 128>& hashes,
          std::uint32_t resource_count,
          const std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes>& key_id)
        : reader_(store, slot, payload_offset), descriptor_(descriptor), paths_(paths), hashes_(hashes),
          resource_count_(resource_count), key_id_(key_id) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        return !released_ && reader_.read_at(offset, output, size);
    }

    const DeviceBundleDescriptor& descriptor() const override { return descriptor_; }
    bool copy_authenticated_resource_sha256(std::string_view path,
                                            std::array<std::uint8_t, 32>& sha256) const override {
        sha256.fill(0);
        if (released_ || path.empty()) return false;
        for (std::uint32_t i = 0; i < resource_count_; ++i) {
            const std::string_view owned_path(paths_[i].data());
            if (owned_path == path) {
                sha256 = hashes_[i];
                return true;
            }
        }
        return false;
    }
    bool copy_authenticated_signer_key_id(
        std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes>& key_id) const override {
        key_id.fill(0);
        if (released_) return false;
        key_id = key_id_;
        return true;
    }
    void release() override { released_ = true; delete this; }

private:
    PartitionReader reader_;
    DeviceBundleDescriptor descriptor_{};
    std::array<std::array<char, kDeviceSignedPackageMaxPathBytes + 1>, 128> paths_{};
    std::array<std::array<std::uint8_t, 32>, 128> hashes_{};
    std::uint32_t resource_count_ = 0;
    std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes> key_id_{};
    bool released_ = false;
};

DeviceImageStore::DeviceImageStore(const esp_partition_t* partition) : partition_(partition) {
    registry_ = new (std::nothrow) RegistryRecord();
}

bool DeviceImageStore::initialize() {
    if (partition_ == nullptr || registry_ == nullptr || max_bundle_bytes() == 0) {
        return false;
    }
    initialized_ = load_registry();
    if (!initialized_) {
        *registry_ = {};
        set_recovery(DeviceRecoveryReason::RegistryInvalid, {}, DeviceRecoveryLauncherActive);
        // Erased or damaged metadata is a recoverable empty library. Publish
        // an empty generation so the next boot has a bounded trusted record.
        initialized_ = publish_registry();
    }
    if (initialized_ && registry_->active.slot != kNoSlot &&
        has_acceptance_fault(AcceptanceFaultPoint::CorruptRegistryAtBoot)) {
        // This test-only path deliberately damages both durable copies through
        // the same raw-partition owner that boot recovery uses, then restarts.
        if (inject_registry_corruption_for_test()) {
            esp_restart();
        }
        return false;
    }
    if (initialized_ && registry_->active.slot != kNoSlot &&
        has_acceptance_fault(AcceptanceFaultPoint::DowngradeRegistryVersion)) {
        if (inject_legacy_registry_for_test()) esp_restart();
        return false;
    }
    return initialized_;
}

bool DeviceImageStore::begin_staging(const DeviceInstallRequest& request) {
    if (!initialized_ || staging_active_ || request.bundle_bytes == 0 || request.bundle_bytes > max_bundle_bytes()) {
        return false;
    }
    // Test-only port adapter refusal. This reaches the real transaction/store
    // boundary used when flash capacity or a staging allocation is unavailable;
    // the shared transaction controller must return StorageFull and leave the
    // current durable registry untouched.
    if (has_acceptance_fault(AcceptanceFaultPoint::RejectStagingAllocation)) {
        return false;
    }
    // A staged replacement must never overwrite the rollback generation.
    // With three physical slots there is always a third, currently unreferenced
    // slot while an active and a rollback record coexist.
    staging_slot_ = kNoSlot;
    for (std::uint8_t candidate = 0; candidate < kBundleSlots; ++candidate) {
        if (candidate != registry_->active.slot && candidate != registry_->rollback.slot) {
            staging_slot_ = candidate;
            break;
        }
    }
    if (staging_slot_ == kNoSlot) {
        return false;
    }
    // The registry records the exact trusted bundle length, so bytes beyond
    // this package are never readable by a committed lease.  Erasing only
    // the incoming package's sector range keeps InstallBegin bounded on the
    // USB endpoint without weakening staging isolation.
    if (!slot_range_is_erased(staging_slot_, request.bundle_bytes) &&
        !erase_slot_range(staging_slot_, request.bundle_bytes)) {
        return false;
    }
    staging_active_ = true;
    staging_verified_ = false;
    staging_transaction_id_ = request.transaction_id;
    staging_bundle_bytes_ = request.bundle_bytes;
    staging_payload_offset_ = 0;
    staging_payload_bytes_ = 0;
    staging_descriptor_ = {};
    restart_for_acceptance_fault(AcceptanceFaultPoint::AfterBegin);
    return true;
}

bool DeviceImageStore::write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) {
    if (!staging_active_ || bytes == nullptr || size == 0 || offset > staging_bundle_bytes_ ||
        size > staging_bundle_bytes_ - offset) {
        return false;
    }
    if (!write_slot(staging_slot_, offset, bytes, size)) {
        return false;
    }
    staging_verified_ = false;
    staging_descriptor_ = {};
    if (offset == 0) {
        restart_for_acceptance_fault(AcceptanceFaultPoint::AfterFirstChunk);
    }
    if (offset + size == staging_bundle_bytes_) {
        restart_for_acceptance_fault(AcceptanceFaultPoint::AfterLastChunk);
    }
    return true;
}

bool DeviceImageStore::verify_staging(const DeviceInstallRequest& request) {
    if (!staging_active_ || request.transaction_id != staging_transaction_id_ ||
        request.bundle_bytes != staging_bundle_bytes_ || request.bundle_bytes > max_bundle_bytes()) {
        return false;
    }
    staging_verified_ = false;
    staging_descriptor_ = {};
    // JFSG verification needs one immutable envelope snapshot: the parser
    // checks exact package bytes while all descriptor/hash reads use its JFAPP
    // payload. The bounded snapshot is placed in PSRAM, keeping internal heap
    // available for the verifier and normal device tasks.
    std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)> package(
        static_cast<std::uint8_t*>(heap_caps_malloc(request.bundle_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        &heap_caps_free);
    if (!package || !read_slot(staging_slot_, 0, package.get(), request.bundle_bytes)) return false;
    verify_telemetry_ = {};
    const std::int64_t crc_started_us = esp_timer_get_time();
    const std::uint32_t package_crc = crc32(package.get(), request.bundle_bytes);
    verify_telemetry_.transport_crc_us = static_cast<std::uint32_t>(esp_timer_get_time() - crc_started_us);
    if (package_crc != request.bundle_crc32) return false;

    constexpr std::size_t kHeaderBytes = kDeviceSignedPackageHeaderBytes;
    if (request.bundle_bytes < kHeaderBytes + 2 * kDeviceSignedPackageSignatureBytes) return false;
    const std::uint32_t manifest_bytes = read_le32(package.get() + 16);
    if (manifest_bytes > request.bundle_bytes - kHeaderBytes - 2 * kDeviceSignedPackageSignatureBytes) return false;
    const std::uint32_t bundle_offset = static_cast<std::uint32_t>(kHeaderBytes) + manifest_bytes +
        static_cast<std::uint32_t>(kDeviceSignedPackageSignatureBytes);
    if (bundle_offset > request.bundle_bytes) return false;
    const std::uint32_t inner_bundle_bytes = request.bundle_bytes - bundle_offset;
    DeviceBundleMemoryReader inner_reader(package.get() + bundle_offset, inner_bundle_bytes);
    DeviceBundleValidationPolicy policy;
    policy.max_bundle_bytes = kMaxBundleBytes;
    policy.max_resource_entries = 128;
    policy.max_summary_bytes = kDeviceBundleMaxSummaryBytes;
    DeviceBundleDescriptor descriptor;
    const std::int64_t inspect_started_us = esp_timer_get_time();
    const DeviceBundleStatus inspect = inspect_device_bundle(inner_reader, inner_bundle_bytes, policy,
                                                              inspection_workspace_, descriptor);
    verify_telemetry_.inspect_bundle_us = static_cast<std::uint32_t>(esp_timer_get_time() - inspect_started_us);
    if (inspect != DeviceBundleStatus::Ok ||
        descriptor.summary.app_id_view() != request.app_id_view()) {
        return false;
    }
    if (registry_->active.slot != kNoSlot &&
        descriptor.summary.app_id_view() == string_view(registry_->active.app_id) &&
        descriptor.summary.version_code < registry_->active.version_code) {
        return false;
    }

    std::array<std::uint8_t, 32> bundle_hash{};
    if (!sha256(package.get() + bundle_offset, inner_bundle_bytes, bundle_hash)) return false;
    auto* resource_metadata = new (std::nothrow) DeviceSignedPackageResourceMetadata[descriptor.resource_count]{};
    auto* output_resources = new (std::nothrow) DeviceSignedPackageResourceOutput[descriptor.resource_count]{};
    if (resource_metadata == nullptr || output_resources == nullptr) {
        delete[] resource_metadata;
        delete[] output_resources;
        return false;
    }
    bool hashes_valid = true;
    for (std::uint32_t i = 0; i < descriptor.resource_count && hashes_valid; ++i) {
        const std::uint8_t* entry = package.get() + bundle_offset + descriptor.index_offset +
            i * kDeviceBundleResourceEntryBytes;
        const std::uint32_t path_offset = read_le32(entry + 4);
        const std::uint16_t path_bytes = static_cast<std::uint16_t>(entry[8]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(entry[9]) << 8u);
        const std::uint32_t payload_offset = read_le32(entry + 12);
        const std::uint32_t payload_bytes = read_le32(entry + 16);
        if (path_offset > descriptor.string_table_bytes || path_bytes > descriptor.string_table_bytes - path_offset ||
            payload_offset > descriptor.payload_bytes || payload_bytes > descriptor.payload_bytes - payload_offset) {
            hashes_valid = false;
            break;
        }
        const char* path = reinterpret_cast<const char*>(package.get() + bundle_offset +
            descriptor.string_table_offset + path_offset);
        resource_metadata[i].path = std::string_view(path, path_bytes);
        hashes_valid = sha256(package.get() + bundle_offset + descriptor.payload_offset + payload_offset,
                              payload_bytes, resource_metadata[i].sha256);
    }
    DeviceSignedPackageBundleMetadata metadata;
    metadata.app_id = descriptor.summary.app_id_view();
    metadata.version = descriptor.summary.version_name_view();
    metadata.version_code = descriptor.summary.version_code;
    metadata.bundle_sha256 = bundle_hash;
    metadata.resources = resource_metadata;
    metadata.resource_count = descriptor.resource_count;
    DeviceSignedPackageView package_view;
    const DeviceSignedPackageStatus signed_status = hashes_valid
        ? inspect_device_signed_package(package.get(), request.bundle_bytes, metadata,
              verify_device_package_signature, nullptr, output_resources, descriptor.resource_count, {}, package_view)
        : DeviceSignedPackageStatus::InvalidBundle;
    delete[] resource_metadata;
    delete[] output_resources;
    if (signed_status != DeviceSignedPackageStatus::Ok ||
        package_view.bundle_size != inner_bundle_bytes ||
        package_view.app_id != descriptor.summary.app_id_view() ||
        package_view.version != descriptor.summary.version_name_view() ||
        package_view.version_code != descriptor.summary.version_code) return false;

    restart_for_acceptance_fault(AcceptanceFaultPoint::DuringVerify);
    staging_descriptor_ = descriptor;
    staging_package_crc32_ = package_crc;
    staging_payload_offset_ = bundle_offset;
    staging_payload_bytes_ = inner_bundle_bytes;
    staging_verified_ = true;
    return true;
}

bool DeviceImageStore::commit_staging(const DeviceInstallRequest& request) {
    if (!staging_active_ || !staging_verified_ || request.transaction_id != staging_transaction_id_ ||
        request.bundle_bytes != staging_bundle_bytes_ || request.bundle_crc32 != staging_package_crc32_ ||
        request.app_id_view() != staging_descriptor_.summary.app_id_view()) {
        return false;
    }
    BundleRecord next;
    next.slot = staging_slot_;
    next.bundle_bytes = staging_payload_bytes_;
    next.envelope_payload_offset = staging_payload_offset_;
    next.envelope_bytes = staging_bundle_bytes_;
    next.envelope_crc32 = staging_package_crc32_;
    next.bundle_crc32 = staging_descriptor_.bundle_crc32;
    next.version_code = staging_descriptor_.summary.version_code;
    if (!copy_string(next.app_id, staging_descriptor_.summary.app_id_view()) ||
        !copy_string(next.version_name, staging_descriptor_.summary.version_name_view())) {
        return false;
    }
    RegistryRecord next_registry = *registry_;
    next_registry.generation = registry_->generation + 1;
    next_registry.rollback = registry_->active;
    next_registry.active = next;
    const RegistryRecord previous = *registry_;
    restart_for_acceptance_fault(AcceptanceFaultPoint::BeforeRegistryPublish);
    if (has_acceptance_fault(AcceptanceFaultPoint::RejectRegistryPublish)) {
        return false;
    }
    *registry_ = next_registry;
    const std::int64_t publish_started_us = esp_timer_get_time();
    if (!publish_registry()) {
        verify_telemetry_.registry_publish_us = static_cast<std::uint32_t>(esp_timer_get_time() - publish_started_us);
        *registry_ = previous;
        return false;
    }
    verify_telemetry_.registry_publish_us = static_cast<std::uint32_t>(esp_timer_get_time() - publish_started_us);
    staging_active_ = false;
    staging_verified_ = false;
    staging_transaction_id_ = 0;
    staging_bundle_bytes_ = 0;
    staging_payload_offset_ = 0;
    staging_payload_bytes_ = 0;
    staging_package_crc32_ = 0;
    staging_descriptor_ = {};
    if (has_acceptance_fault(AcceptanceFaultPoint::CorruptPublishedEnvelope)) {
        const std::uint8_t invalid = 0;
        if (!write_slot(next.slot, next.envelope_payload_offset, &invalid, sizeof(invalid))) return false;
    }
    restart_for_acceptance_fault(AcceptanceFaultPoint::CorruptPublishedEnvelope);
    restart_for_acceptance_fault(AcceptanceFaultPoint::AfterRegistryPublish);
    return true;
}

void DeviceImageStore::abort_staging(std::uint32_t transaction_id) {
    if (!staging_active_ || transaction_id != staging_transaction_id_) {
        return;
    }
    (void)erase_slot_range(staging_slot_, staging_bundle_bytes_);
    staging_active_ = false;
    staging_verified_ = false;
    staging_transaction_id_ = 0;
    staging_bundle_bytes_ = 0;
    staging_payload_offset_ = 0;
    staging_payload_bytes_ = 0;
    staging_package_crc32_ = 0;
    staging_descriptor_ = {};
    set_recovery(DeviceRecoveryReason::StagingDiscarded, {}, DeviceRecoveryLauncherActive);
}

DeviceBundleStatus DeviceImageStore::acquire_installed_bundle(std::string_view app_id,
                                                                AppInstalledBundleLease*& lease) {
    lease = nullptr;
    if (!initialized_ || registry_->active.slot == kNoSlot || app_id != string_view(registry_->active.app_id)) {
        return DeviceBundleStatus::ResourceNotFound;
    }
    DeviceBundleDescriptor descriptor;
    if (!validate_record(registry_->active, &descriptor)) {
        set_recovery(DeviceRecoveryReason::RegistryInvalid, app_id, DeviceRecoveryLauncherActive);
        return DeviceBundleStatus::BadChecksum;
    }
    using AuthenticatedPaths = std::array<std::array<char, kDeviceSignedPackageMaxPathBytes + 1>, 128>;
    using AuthenticatedHashes = std::array<std::array<std::uint8_t, 32>, 128>;
    std::unique_ptr<AuthenticatedPaths> paths(new (std::nothrow) AuthenticatedPaths{});
    std::unique_ptr<AuthenticatedHashes> hashes(new (std::nothrow) AuthenticatedHashes{});
    if (!paths || !hashes) return DeviceBundleStatus::ReadFailed;
    std::uint32_t resource_count = 0;
    std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes> key_id{};
    if (!verify_record_envelope(registry_->active, &descriptor, paths.get(), hashes.get(), &resource_count, &key_id)) {
        set_recovery(DeviceRecoveryReason::RegistryInvalid, app_id, DeviceRecoveryLauncherActive);
        return DeviceBundleStatus::BadChecksum;
    }
    auto* next = new (std::nothrow) Lease(*this, registry_->active.slot,
                                          registry_->active.envelope_payload_offset, descriptor,
                                          *paths, *hashes, resource_count, key_id);
    if (next == nullptr) {
        return DeviceBundleStatus::ReadFailed;
    }
    lease = next;
    return DeviceBundleStatus::Ok;
}

bool DeviceImageStore::list(DeviceAppListPayload& list) const {
    list = {};
    if (!initialized_) {
        return initialized_;
    }
    list.registry_generation = registry_->generation;
    if (registry_->active.slot == kNoSlot) {
        return true;
    }
    DeviceAppLibraryEntry& entry = list.entries[0];
    std::memcpy(entry.app_id.data(), registry_->active.app_id.data(), entry.app_id.size());
    std::memcpy(entry.version_name.data(), registry_->active.version_name.data(), entry.version_name.size());
    entry.version_code = registry_->active.version_code;
    entry.bundle_bytes = registry_->active.bundle_bytes;
    entry.state = DeviceAppLibraryState::Installed;
    entry.flags = registry_->rollback.slot == kNoSlot ? 0 : DeviceAppLibraryEntryRollbackAvailable;
    list.entry_count = 1;
    return true;
}

DeviceRequestResultCode DeviceImageStore::rollback(std::string_view app_id) {
    if (!initialized_ || registry_->active.slot == kNoSlot || registry_->rollback.slot == kNoSlot ||
        app_id != string_view(registry_->active.app_id)) {
        return DeviceRequestResultCode::NotFound;
    }
    // Validate the candidate before changing the durable active pointer. A
    // damaged rollback slot must leave the currently running generation
    // intact and be observable as an integrity failure to the provider.
    if (!validate_record(registry_->rollback, nullptr)) {
        set_recovery(DeviceRecoveryReason::RegistryInvalid, app_id, DeviceRecoveryLauncherActive);
        return DeviceRequestResultCode::IntegrityFailed;
    }
    DeviceBundleDescriptor rollback_descriptor;
    if (!validate_record(registry_->rollback, &rollback_descriptor) ||
        rollback_descriptor.summary.app_id_view() != string_view(registry_->rollback.app_id)) {
        set_recovery(DeviceRecoveryReason::RegistryInvalid, app_id, DeviceRecoveryLauncherActive);
        return DeviceRequestResultCode::IntegrityFailed;
    }
    const RegistryRecord previous = *registry_;
    std::swap(registry_->active, registry_->rollback);
    ++registry_->generation;
    if (publish_registry()) {
        return DeviceRequestResultCode::Ok;
    }
    *registry_ = previous;
    return DeviceRequestResultCode::Failed;
}

bool DeviceImageStore::remove(std::string_view app_id) {
    if (!initialized_ || registry_->active.slot == kNoSlot || app_id != string_view(registry_->active.app_id)) {
        return false;
    }
    const std::uint8_t active_slot = registry_->active.slot;
    const std::uint8_t rollback_slot = registry_->rollback.slot;
    const std::uint32_t next_generation = registry_->generation + 1;
    const RegistryRecord previous = *registry_;
    *registry_ = {};
    registry_->generation = next_generation;
    if (!publish_registry()) {
        *registry_ = previous;
        return false;
    }
    (void)erase_slot(active_slot);
    if (rollback_slot != kNoSlot) {
        (void)erase_slot(rollback_slot);
    }
    return true;
}

void DeviceImageStore::copy_recovery(DeviceRecoveryDetailPayload& recovery) const { recovery = recovery_; }
void DeviceImageStore::record_recovery(DeviceRecoveryReason reason, std::string_view app_id, std::uint16_t flags) {
    set_recovery(reason, app_id, flags);
}
std::uint32_t DeviceImageStore::available_storage_bytes() const {
    return partition_ == nullptr || partition_->size <= kStorageHeaderBytes ? 0 : partition_->size - kStorageHeaderBytes;
}
std::uint32_t DeviceImageStore::max_bundle_bytes() const {
    return std::min(kMaxBundleBytes, bundle_slot_bytes(partition_));
}
std::uint32_t DeviceImageStore::registry_generation() const { return registry_ == nullptr ? 0 : registry_->generation; }

bool DeviceImageStore::inject_registry_corruption_for_test() {
    if (partition_ == nullptr || !initialized_) {
        return false;
    }
    const std::uint32_t invalid = 0;
    return esp_partition_write(partition_, 0, &invalid, sizeof(invalid)) == ESP_OK &&
           esp_partition_write(partition_, kRegistryBytes, &invalid, sizeof(invalid)) == ESP_OK;
}

bool DeviceImageStore::inject_legacy_registry_for_test() {
    if (partition_ == nullptr || !initialized_) return false;
    const std::uint16_t legacy_version = 1;
    return esp_partition_write(partition_, 4, &legacy_version, sizeof(legacy_version)) == ESP_OK &&
           esp_partition_write(partition_, kRegistryBytes + 4, &legacy_version, sizeof(legacy_version)) == ESP_OK;
}

bool DeviceImageStore::load_registry() {
    RegistryRecord candidates[kRegistrySlots]{};
    bool valid[kRegistrySlots]{};
    for (std::uint32_t i = 0; i < kRegistrySlots; ++i) {
        if (esp_partition_read(partition_, i * kRegistryBytes, &candidates[i], sizeof(RegistryRecord)) != ESP_OK) {
            continue;
        }
        const std::uint32_t stored = candidates[i].crc32;
        candidates[i].crc32 = 0;
        valid[i] = candidates[i].magic == kRegistryMagic && candidates[i].version == kRegistryVersion &&
                   candidates[i].bytes == sizeof(RegistryRecord) &&
                   stored == crc32(&candidates[i], sizeof(RegistryRecord));
        candidates[i].crc32 = stored;
        if (valid[i] && candidates[i].active.slot != kNoSlot) {
            valid[i] = validate_record(candidates[i].active, nullptr);
        }
    }
    if (!valid[0] && !valid[1]) {
        return false;
    }
    const RegistryRecord& selected = !valid[1] || (valid[0] && candidates[0].generation >= candidates[1].generation)
        ? candidates[0] : candidates[1];
    *registry_ = selected;
    return true;
}

bool DeviceImageStore::publish_registry() {
    if (partition_ == nullptr || registry_ == nullptr) {
        return false;
    }
    RegistryRecord durable = *registry_;
    durable.magic = kRegistryMagic;
    durable.version = kRegistryVersion;
    durable.bytes = sizeof(RegistryRecord);
    durable.crc32 = 0;
    durable.crc32 = crc32(&durable, sizeof(durable));
    const std::uint32_t target = (durable.generation & 1u) * kRegistryBytes;
    if (esp_partition_erase_range(partition_, target, kRegistryBytes) != ESP_OK ||
        esp_partition_write(partition_, target, &durable, sizeof(durable)) != ESP_OK) {
        return false;
    }
    *registry_ = durable;
    return true;
}

bool DeviceImageStore::erase_slot(std::uint8_t slot) {
    if (partition_ == nullptr || slot >= kBundleSlots) {
        return false;
    }
    return erase_slot_range(slot, bundle_slot_bytes(partition_));
}

bool DeviceImageStore::erase_slot_range(std::uint8_t slot, std::uint32_t bytes) {
    if (partition_ == nullptr || slot >= kBundleSlots) {
        return false;
    }
    const std::uint32_t slot_bytes = bundle_slot_bytes(partition_);
    if (bytes == 0 || bytes > slot_bytes || bytes > UINT32_MAX - (kFlashSectorBytes - 1u)) {
        return false;
    }
    const std::uint32_t erased_bytes = (bytes + kFlashSectorBytes - 1u) & ~(kFlashSectorBytes - 1u);
    return esp_partition_erase_range(partition_, kStorageHeaderBytes + slot * slot_bytes, erased_bytes) == ESP_OK;
}

bool DeviceImageStore::slot_range_is_erased(std::uint8_t slot, std::uint32_t bytes) const {
    const std::uint32_t slot_bytes = bundle_slot_bytes(partition_);
    if (partition_ == nullptr || slot >= kBundleSlots || bytes == 0 || bytes > slot_bytes) {
        return false;
    }
    std::array<std::uint8_t, 256> probe{};
    for (std::uint32_t offset = 0; offset < bytes;) {
        const std::size_t read_bytes = std::min<std::size_t>(probe.size(), bytes - offset);
        if (!read_slot(slot, offset, probe.data(), read_bytes) ||
            std::any_of(probe.begin(), probe.begin() + read_bytes,
                        [](std::uint8_t value) { return value != 0xffu; })) {
            return false;
        }
        offset += static_cast<std::uint32_t>(read_bytes);
    }
    return true;
}

bool DeviceImageStore::read_slot(std::uint8_t slot, std::uint32_t offset, void* output, std::size_t size) const {
    const std::uint32_t bytes = bundle_slot_bytes(partition_);
    return partition_ != nullptr && output != nullptr && slot < kBundleSlots && offset <= bytes && size <= bytes - offset &&
           esp_partition_read(partition_, kStorageHeaderBytes + slot * bytes + offset, output, size) == ESP_OK;
}

bool DeviceImageStore::read_slot_cached(std::uint8_t slot, std::uint32_t offset, void* output, std::size_t size) const {
    ++verify_telemetry_.reader_calls;
    verify_telemetry_.reader_bytes += static_cast<std::uint32_t>(size);
    if (output == nullptr || size == 0) return false;
    const std::uint32_t sector_offset = offset & ~(kFlashSectorBytes - 1u);
    const std::size_t in_sector = offset - sector_offset;
    if (size > reader_cache_.size() - in_sector) return read_slot(slot, offset, output, size);
    if (reader_cache_slot_ != slot || reader_cache_offset_ != sector_offset) {
        if (!read_slot(slot, sector_offset, reader_cache_.data(), reader_cache_.size())) return false;
        reader_cache_slot_ = slot;
        reader_cache_offset_ = sector_offset;
    }
    std::memcpy(output, reader_cache_.data() + in_sector, size);
    return true;
}

DeviceImageStore::VerifyTelemetry DeviceImageStore::copy_verify_telemetry() const {
    return verify_telemetry_;
}

bool DeviceImageStore::write_slot(std::uint8_t slot, std::uint32_t offset, const void* bytes, std::size_t size) {
    const std::uint32_t slot_bytes = bundle_slot_bytes(partition_);
    return partition_ != nullptr && bytes != nullptr && slot < kBundleSlots && offset <= slot_bytes && size <= slot_bytes - offset &&
           esp_partition_write(partition_, kStorageHeaderBytes + slot * slot_bytes + offset, bytes, size) == ESP_OK;
}

bool DeviceImageStore::validate_record(const BundleRecord& record, DeviceBundleDescriptor* descriptor) const {
    return verify_record_envelope(record, descriptor, nullptr, nullptr, nullptr, nullptr);
}

bool DeviceImageStore::verify_record_envelope(
    const BundleRecord& record,
    DeviceBundleDescriptor* descriptor,
    std::array<std::array<char, kDeviceSignedPackageMaxPathBytes + 1>, 128>* paths,
    std::array<std::array<std::uint8_t, 32>, 128>* hashes,
    std::uint32_t* resource_count,
    std::array<std::uint8_t, kDeviceSignedPackageKeyIdBytes>* key_id) const {
    if (record.slot == kNoSlot || record.bundle_bytes == 0 || record.bundle_bytes > max_bundle_bytes() ||
        record.envelope_bytes < kDeviceSignedPackageHeaderBytes + kDeviceSignedPackageSignatureBytes ||
        record.envelope_bytes > max_bundle_bytes() || record.envelope_payload_offset > record.envelope_bytes ||
        record.bundle_bytes != record.envelope_bytes - record.envelope_payload_offset ||
        record.envelope_payload_offset > UINT32_MAX - kDeviceSignedPackageSignatureBytes) return false;

    std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)> package(
        static_cast<std::uint8_t*>(heap_caps_malloc(record.envelope_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
        &heap_caps_free);
    if (!package || !read_slot(record.slot, 0, package.get(), record.envelope_bytes) ||
        crc32(package.get(), record.envelope_bytes) != record.envelope_crc32) return false;

    const std::uint32_t manifest_bytes = read_le32(package.get() + 16);
    if (manifest_bytes > UINT32_MAX - kDeviceSignedPackageHeaderBytes - kDeviceSignedPackageSignatureBytes ||
        kDeviceSignedPackageHeaderBytes + manifest_bytes + kDeviceSignedPackageSignatureBytes !=
            record.envelope_payload_offset) return false;
    DeviceBundleMemoryReader bundle_reader(package.get() + record.envelope_payload_offset, record.bundle_bytes);
    DeviceBundleValidationPolicy policy;
    policy.max_bundle_bytes = kMaxBundleBytes;
    policy.max_resource_entries = 128;
    policy.max_summary_bytes = kDeviceBundleMaxSummaryBytes;
    DeviceBundleDescriptor inspected;
    if (inspect_device_bundle(bundle_reader, record.bundle_bytes, policy, inspection_workspace_, inspected) !=
            DeviceBundleStatus::Ok || inspected.bundle_crc32 != record.bundle_crc32 ||
        inspected.summary.app_id_view() != string_view(record.app_id) ||
        inspected.summary.version_code != record.version_code ||
        inspected.summary.version_name_view() != string_view(record.version_name) || inspected.resource_count > 128) {
        return false;
    }

    std::unique_ptr<DeviceSignedPackageResourceMetadata[]> expected(
        new (std::nothrow) DeviceSignedPackageResourceMetadata[128]{});
    std::unique_ptr<DeviceSignedPackageResourceOutput[]> parsed(
        new (std::nothrow) DeviceSignedPackageResourceOutput[128]{});
    if (!expected || !parsed) return false;
    for (std::uint32_t i = 0; i < inspected.resource_count; ++i) {
        const std::uint8_t* entry = package.get() + record.envelope_payload_offset + inspected.index_offset +
            i * kDeviceBundleResourceEntryBytes;
        const std::uint32_t path_offset = read_le32(entry + 4);
        const std::uint16_t path_bytes = static_cast<std::uint16_t>(entry[8]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(entry[9]) << 8u);
        const std::uint32_t payload_offset = read_le32(entry + 12);
        const std::uint32_t payload_bytes = read_le32(entry + 16);
        if (path_offset > inspected.string_table_bytes || path_bytes > inspected.string_table_bytes - path_offset ||
            payload_offset > inspected.payload_bytes || payload_bytes > inspected.payload_bytes - payload_offset) return false;
        const char* path = reinterpret_cast<const char*>(package.get() + record.envelope_payload_offset +
            inspected.string_table_offset + path_offset);
        expected[i].path = std::string_view(path, path_bytes);
        if (!sha256(package.get() + record.envelope_payload_offset + inspected.payload_offset + payload_offset,
                    payload_bytes, expected[i].sha256)) return false;
    }
    std::array<std::uint8_t, 32> bundle_sha{};
    if (!sha256(package.get() + record.envelope_payload_offset, record.bundle_bytes, bundle_sha)) return false;
    DeviceSignedPackageBundleMetadata metadata;
    metadata.app_id = inspected.summary.app_id_view();
    metadata.version = inspected.summary.version_name_view();
    metadata.version_code = inspected.summary.version_code;
    metadata.bundle_sha256 = bundle_sha;
    metadata.resources = expected.get();
    metadata.resource_count = inspected.resource_count;
    DeviceSignedPackageView signed_view;
    const auto signed_status = inspect_device_signed_package(
        package.get(), record.envelope_bytes, metadata, verify_device_package_signature, nullptr,
        parsed.get(), 128, {}, signed_view);
    if (signed_status != DeviceSignedPackageStatus::Ok ||
        signed_view.bundle_bytes != package.get() + record.envelope_payload_offset ||
        signed_view.bundle_size != record.bundle_bytes) return false;

    if (paths != nullptr && hashes != nullptr && resource_count != nullptr && key_id != nullptr) {
        for (std::uint32_t i = 0; i < inspected.resource_count; ++i) {
            if (parsed[i].path.empty() || parsed[i].path.size() >= (*paths)[i].size()) return false;
            std::memcpy((*paths)[i].data(), parsed[i].path.data(), parsed[i].path.size());
            (*paths)[i][parsed[i].path.size()] = '\0';
            (*hashes)[i] = parsed[i].sha256;
        }
        *resource_count = static_cast<std::uint32_t>(inspected.resource_count);
        *key_id = signed_view.key_id;
    }
    if (descriptor != nullptr) {
        *descriptor = inspected;
    }
    return true;
}

void DeviceImageStore::set_recovery(DeviceRecoveryReason reason, std::string_view app_id, std::uint16_t flags) {
    ++recovery_.recovery_sequence;
    recovery_.registry_generation = registry_ == nullptr ? 0 : registry_->generation;
    recovery_.reason = reason;
    recovery_.flags = flags;
    std::memset(recovery_.app_id.data(), 0, recovery_.app_id.size());
    if (!app_id.empty() && app_id.size() < recovery_.app_id.size()) {
        std::memcpy(recovery_.app_id.data(), app_id.data(), app_id.size());
    }
}

} // namespace jellyframe_esp32s3
