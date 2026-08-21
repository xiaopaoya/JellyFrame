#include "jellyframe_esp32s3_device_image_store.h"

#include "esp_rom_crc.h"
#include "esp_system.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr std::uint32_t kRegistryMagic = 0x4a465247u; // JFRG
constexpr std::uint16_t kRegistryVersion = 1;
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
    PartitionReader(const DeviceImageStore& store, std::uint8_t slot) : store_(store), slot_(slot) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        if (output == nullptr || size == 0) {
            return false;
        }
        // Bundle validation reads the header, summary and resource CRCs in
        // small windows. Keep one flash-sector cache in this port reader so
        // those bounded logical reads do not become hundreds of raw-partition
        // operations during an install commit.
        const std::uint32_t sector_offset = offset & ~(kFlashSectorBytes - 1u);
        const std::size_t in_sector = offset - sector_offset;
        if (size <= cache_.size() - in_sector) {
            if (cache_offset_ != sector_offset) {
                if (!store_.read_slot(slot_, sector_offset, cache_.data(), cache_.size())) {
                    return false;
                }
                cache_offset_ = sector_offset;
            }
            std::memcpy(output, cache_.data() + in_sector, size);
            return true;
        }
        return store_.read_slot(slot_, offset, output, size);
    }

private:
    const DeviceImageStore& store_;
    std::uint8_t slot_ = kNoSlot;
    mutable std::array<std::uint8_t, kFlashSectorBytes> cache_{};
    mutable std::uint32_t cache_offset_ = 0xffffffffu;
};

class DeviceImageStore::Lease final : public AppInstalledBundleLease {
public:
    Lease(const DeviceImageStore& store, std::uint8_t slot, const DeviceBundleDescriptor& descriptor)
        : reader_(store, slot), descriptor_(descriptor) {}

    bool read_at(std::uint32_t offset, std::uint8_t* output, std::size_t size) const override {
        return !released_ && reader_.read_at(offset, output, size);
    }

    const DeviceBundleDescriptor& descriptor() const override { return descriptor_; }
    void release() override { released_ = true; delete this; }

private:
    PartitionReader reader_;
    DeviceBundleDescriptor descriptor_{};
    bool released_ = false;
};

DeviceImageStore::DeviceImageStore(const esp_partition_t* partition) : partition_(partition) {
    registry_ = new (std::nothrow) RegistryRecord();
}

bool DeviceImageStore::initialize() {
    if (partition_ == nullptr || registry_ == nullptr || partition_->size <= kStorageHeaderBytes) {
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
    return initialized_;
}

bool DeviceImageStore::begin_staging(const DeviceInstallRequest& request) {
    if (!initialized_ || staging_active_ || request.bundle_bytes == 0 || request.bundle_bytes > kMaxBundleBytes) {
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
    staging_descriptor_ = {};
    restart_for_acceptance_fault(AcceptanceFaultPoint::AfterBegin);
    return true;
}

bool DeviceImageStore::write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) {
    if (!staging_active_ || bytes == nullptr || size == 0 || offset > kMaxBundleBytes ||
        size > kMaxBundleBytes - offset) {
        return false;
    }
    if (!write_slot(staging_slot_, offset, bytes, size)) {
        return false;
    }
    if (offset == 0) {
        restart_for_acceptance_fault(AcceptanceFaultPoint::AfterFirstChunk);
    }
    if (offset + size == staging_bundle_bytes_) {
        restart_for_acceptance_fault(AcceptanceFaultPoint::AfterLastChunk);
    }
    return true;
}

bool DeviceImageStore::verify_staging(const DeviceInstallRequest& request) {
    if (!staging_active_ || request.transaction_id != staging_transaction_id_ || request.bundle_bytes > kMaxBundleBytes) {
        return false;
    }
    PartitionReader reader(*this, staging_slot_);
    DeviceBundleValidationPolicy policy;
    policy.max_bundle_bytes = kMaxBundleBytes;
    policy.max_resource_entries = 128;
    policy.max_summary_bytes = kDeviceBundleMaxSummaryBytes;
    DeviceBundleDescriptor descriptor;
    std::array<std::uint8_t, 1024> scratch{};
    std::uint32_t transport_crc = 0;
    for (std::uint32_t offset = 0; offset < request.bundle_bytes;) {
        const std::size_t bytes = std::min<std::size_t>(scratch.size(), request.bundle_bytes - offset);
        if (!reader.read_at(offset, scratch.data(), bytes)) {
            return false;
        }
        transport_crc = esp_rom_crc32_le(transport_crc, scratch.data(), bytes);
        offset += static_cast<std::uint32_t>(bytes);
    }
    if (transport_crc != request.bundle_crc32 ||
        inspect_device_bundle(reader, request.bundle_bytes, policy, descriptor) != DeviceBundleStatus::Ok ||
        descriptor.summary.app_id_view() != request.app_id_view()) {
        return false;
    }
    if (!request.allow_downgrade && registry_->active.slot != kNoSlot &&
        descriptor.summary.app_id_view() == string_view(registry_->active.app_id) &&
        descriptor.summary.version_code < registry_->active.version_code) {
        return false;
    }
    restart_for_acceptance_fault(AcceptanceFaultPoint::DuringVerify);
    staging_descriptor_ = descriptor;
    staging_verified_ = true;
    return true;
}

bool DeviceImageStore::commit_staging(const DeviceInstallRequest& request) {
    if (!staging_active_ || !staging_verified_ || request.transaction_id != staging_transaction_id_) {
        return false;
    }
    BundleRecord next;
    next.slot = staging_slot_;
    next.bundle_bytes = request.bundle_bytes;
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
    if (!publish_registry()) {
        *registry_ = previous;
        return false;
    }
    staging_active_ = false;
    staging_verified_ = false;
    staging_transaction_id_ = 0;
    staging_bundle_bytes_ = 0;
    staging_descriptor_ = {};
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
    auto* next = new (std::nothrow) Lease(*this, registry_->active.slot, descriptor);
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

bool DeviceImageStore::rollback(std::string_view app_id) {
    if (!initialized_ || registry_->active.slot == kNoSlot || registry_->rollback.slot == kNoSlot ||
        app_id != string_view(registry_->active.app_id)) {
        return false;
    }
    const RegistryRecord previous = *registry_;
    std::swap(registry_->active, registry_->rollback);
    ++registry_->generation;
    if (publish_registry()) {
        return true;
    }
    *registry_ = previous;
    return false;
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
std::uint32_t DeviceImageStore::registry_generation() const { return registry_ == nullptr ? 0 : registry_->generation; }

bool DeviceImageStore::inject_registry_corruption_for_test() {
    if (partition_ == nullptr || !initialized_) {
        return false;
    }
    const std::uint32_t invalid = 0;
    return esp_partition_write(partition_, 0, &invalid, sizeof(invalid)) == ESP_OK &&
           esp_partition_write(partition_, kRegistryBytes, &invalid, sizeof(invalid)) == ESP_OK;
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
                   candidates[i].bytes == sizeof(RegistryRecord) && stored == crc32(&candidates[i], sizeof(RegistryRecord));
        candidates[i].crc32 = stored;
    }
    if (!valid[0] && !valid[1]) {
        return false;
    }
    const RegistryRecord& selected = !valid[1] || (valid[0] && candidates[0].generation >= candidates[1].generation)
        ? candidates[0] : candidates[1];
    *registry_ = selected;
    if (registry_->active.slot != kNoSlot && !validate_record(registry_->active, nullptr)) {
        return false;
    }
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

bool DeviceImageStore::write_slot(std::uint8_t slot, std::uint32_t offset, const void* bytes, std::size_t size) {
    const std::uint32_t slot_bytes = bundle_slot_bytes(partition_);
    return partition_ != nullptr && bytes != nullptr && slot < kBundleSlots && offset <= slot_bytes && size <= slot_bytes - offset &&
           esp_partition_write(partition_, kStorageHeaderBytes + slot * slot_bytes + offset, bytes, size) == ESP_OK;
}

bool DeviceImageStore::validate_record(const BundleRecord& record, DeviceBundleDescriptor* descriptor) const {
    if (record.slot == kNoSlot || record.bundle_bytes == 0 || record.bundle_bytes > kMaxBundleBytes) {
        return false;
    }
    PartitionReader reader(*this, record.slot);
    DeviceBundleValidationPolicy policy;
    policy.max_bundle_bytes = kMaxBundleBytes;
    policy.max_resource_entries = 128;
    policy.max_summary_bytes = kDeviceBundleMaxSummaryBytes;
    DeviceBundleDescriptor inspected;
    if (inspect_device_bundle(reader, record.bundle_bytes, policy, inspected) != DeviceBundleStatus::Ok ||
        inspected.bundle_crc32 != record.bundle_crc32 || inspected.summary.app_id_view() != string_view(record.app_id)) {
        return false;
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
