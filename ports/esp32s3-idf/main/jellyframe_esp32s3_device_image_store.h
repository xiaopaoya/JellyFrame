#pragma once

#include "app_runtime/app_installed_bundle.h"
#include "device_runtime_contracts/device_install_transaction.h"
#include "device_runtime_contracts/device_runtime_protocol.h"

#include "esp_partition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe_esp32s3 {

// Raw `storage` partition adapter for the Developer Image. It owns staging,
// registry publication and immutable committed-bundle leases; neither the
// JFDP receive buffer nor a mutable registry record escapes this boundary.
class DeviceImageStore final : public jellyframe::DeviceInstallStore,
                               public jellyframe::AppInstalledBundleProvider {
public:
    static constexpr std::uint32_t kMaxBundleBytes = 320u * 1024u;

    explicit DeviceImageStore(const esp_partition_t* partition);

    bool initialize();

    bool begin_staging(const jellyframe::DeviceInstallRequest& request) override;
    bool write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) override;
    bool verify_staging(const jellyframe::DeviceInstallRequest& request) override;
    bool commit_staging(const jellyframe::DeviceInstallRequest& request) override;
    void abort_staging(std::uint32_t transaction_id) override;

    jellyframe::DeviceBundleStatus acquire_installed_bundle(std::string_view app_id,
                                                            jellyframe::AppInstalledBundleLease*& lease) override;

    bool list(jellyframe::DeviceAppListPayload& list) const;
    bool rollback(std::string_view app_id);
    bool remove(std::string_view app_id);
    void copy_recovery(jellyframe::DeviceRecoveryDetailPayload& recovery) const;
    void record_recovery(jellyframe::DeviceRecoveryReason reason,
                         std::string_view app_id,
                         std::uint16_t flags);
    std::uint32_t available_storage_bytes() const;
    std::uint32_t registry_generation() const;

    struct VerifyTelemetry {
        std::uint32_t transport_crc_us = 0;
        std::uint32_t inspect_bundle_us = 0;
        std::uint32_t registry_publish_us = 0;
        std::uint32_t reader_calls = 0;
        std::uint32_t reader_bytes = 0;
    };

    VerifyTelemetry copy_verify_telemetry() const;

    // Acceptance-only fault injection. It uses the same registry reader that
    // production boot uses; no host-side registry mutation is involved.
    bool inject_registry_corruption_for_test();

private:
    struct RegistryRecord;
    struct BundleRecord;
    class PartitionReader;
    class Lease;

    bool load_registry();
    bool publish_registry();
    bool erase_slot(std::uint8_t slot);
    bool erase_slot_range(std::uint8_t slot, std::uint32_t bytes);
    bool slot_range_is_erased(std::uint8_t slot, std::uint32_t bytes) const;
    bool read_slot(std::uint8_t slot, std::uint32_t offset, void* output, std::size_t size) const;
    bool read_slot_cached(std::uint8_t slot, std::uint32_t offset, void* output, std::size_t size) const;
    bool write_slot(std::uint8_t slot, std::uint32_t offset, const void* bytes, std::size_t size);
    bool validate_record(const BundleRecord& record, jellyframe::DeviceBundleDescriptor* descriptor) const;
    void set_recovery(jellyframe::DeviceRecoveryReason reason, std::string_view app_id, std::uint16_t flags);

    const esp_partition_t* partition_ = nullptr;
    RegistryRecord* registry_ = nullptr;
    bool initialized_ = false;
    bool staging_active_ = false;
    bool staging_verified_ = false;
    std::uint8_t staging_slot_ = 0;
    std::uint32_t staging_transaction_id_ = 0;
    std::uint32_t staging_bundle_bytes_ = 0;
    jellyframe::DeviceBundleDescriptor staging_descriptor_{};
    jellyframe::DeviceRecoveryDetailPayload recovery_{};
    // The endpoint serializes store access. Keep validation workspace and the
    // sector cache here rather than transiently stacking them in verify/read.
    mutable jellyframe::DeviceBundleInspectionWorkspace inspection_workspace_{};
    std::array<std::uint8_t, 1024> verify_scratch_{};
    mutable std::array<std::uint8_t, 4096> reader_cache_{};
    mutable std::uint32_t reader_cache_slot_ = 0xffu;
    mutable std::uint32_t reader_cache_offset_ = 0xffffffffu;
    mutable VerifyTelemetry verify_telemetry_{};
};

} // namespace jellyframe_esp32s3
