#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

constexpr std::size_t kDeviceInstallMaxAppIdBytes = 95;

enum class DeviceInstallPhase : std::uint8_t {
    Idle,
    Receiving,
};

enum class DeviceInstallStatus : std::uint8_t {
    Accepted,
    Busy,
    InvalidRequest,
    OffsetMismatch,
    PayloadTooLarge,
    Incomplete,
    StoreRejected,
    IntegrityRejected,
    CommitFailed,
    Aborted,
    NoActiveTransaction,
};

struct DeviceInstallLimits {
    std::uint32_t max_bundle_bytes = 0;
    std::uint32_t max_chunk_bytes = 0;
};

struct DeviceInstallRequest {
    std::uint32_t transaction_id = 0;
    std::array<char, kDeviceInstallMaxAppIdBytes + 1> app_id{};
    std::uint32_t bundle_bytes = 0;
    std::uint32_t bundle_crc32 = 0;
    bool allow_downgrade = false;

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }
};

struct DeviceInstallResult {
    DeviceInstallStatus status = DeviceInstallStatus::InvalidRequest;
    DeviceInstallPhase phase = DeviceInstallPhase::Idle;
    std::uint32_t transaction_id = 0;
    std::uint32_t received_bytes = 0;
    std::uint32_t expected_bytes = 0;

    bool accepted() const {
        return status == DeviceInstallStatus::Accepted;
    }
};

// Implemented by a port or desktop reference host. The adapter owns all I/O,
// bundle parsing, integrity checks and atomic registry publication.
class DeviceInstallStore {
public:
    virtual ~DeviceInstallStore() = default;

    virtual bool begin_staging(const DeviceInstallRequest& request) = 0;
    virtual bool write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) = 0;
    virtual bool verify_staging(const DeviceInstallRequest& request) = 0;
    virtual bool commit_staging(const DeviceInstallRequest& request) = 0;
    virtual void abort_staging(std::uint32_t transaction_id) = 0;
};

class DeviceInstallTransaction {
public:
    explicit DeviceInstallTransaction(DeviceInstallLimits limits);

    DeviceInstallResult begin(std::uint32_t transaction_id,
                              std::string_view app_id,
                              std::uint32_t bundle_bytes,
                              std::uint32_t bundle_crc32,
                              bool allow_downgrade,
                              DeviceInstallStore& store);
    DeviceInstallResult append(std::uint32_t transaction_id,
                               std::uint32_t offset,
                               const std::uint8_t* bytes,
                               std::size_t size,
                               DeviceInstallStore& store);
    DeviceInstallResult commit(std::uint32_t transaction_id, DeviceInstallStore& store);
    DeviceInstallResult abort(std::uint32_t transaction_id, DeviceInstallStore& store);

    DeviceInstallPhase phase() const {
        return phase_;
    }

    const DeviceInstallRequest& request() const {
        return request_;
    }

    std::uint32_t received_bytes() const {
        return received_bytes_;
    }

private:
    DeviceInstallResult result(DeviceInstallStatus status) const;
    void reset();

    DeviceInstallLimits limits_;
    DeviceInstallPhase phase_ = DeviceInstallPhase::Idle;
    DeviceInstallRequest request_;
    std::uint32_t received_bytes_ = 0;
};

const char* device_install_phase_name(DeviceInstallPhase phase);
const char* device_install_status_name(DeviceInstallStatus status);

} // namespace jellyframe
