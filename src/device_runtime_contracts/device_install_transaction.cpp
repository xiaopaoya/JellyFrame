#include "device_runtime_contracts/device_install_transaction.h"

#include <cstring>

namespace jellyframe {

DeviceInstallTransaction::DeviceInstallTransaction(DeviceInstallLimits limits) : limits_(limits) {}

DeviceInstallResult DeviceInstallTransaction::result(DeviceInstallStatus status) const {
    return DeviceInstallResult{status, phase_, request_.transaction_id, received_bytes_, request_.bundle_bytes};
}

void DeviceInstallTransaction::reset() {
    phase_ = DeviceInstallPhase::Idle;
    request_ = {};
    received_bytes_ = 0;
}

DeviceInstallResult DeviceInstallTransaction::begin(std::uint32_t transaction_id,
                                                     std::string_view app_id,
                                                     std::uint32_t bundle_bytes,
                                                     std::uint32_t bundle_crc32,
                                                     bool allow_downgrade,
                                                     DeviceInstallStore& store) {
    if (phase_ != DeviceInstallPhase::Idle) {
        return result(DeviceInstallStatus::Busy);
    }
    if (transaction_id == 0 || app_id.empty() || app_id.size() > kDeviceInstallMaxAppIdBytes ||
        app_id.find('\0') != std::string_view::npos ||
        bundle_bytes == 0 || limits_.max_bundle_bytes == 0 || bundle_bytes > limits_.max_bundle_bytes) {
        return result(DeviceInstallStatus::InvalidRequest);
    }

    DeviceInstallRequest next;
    next.transaction_id = transaction_id;
    std::memcpy(next.app_id.data(), app_id.data(), app_id.size());
    next.app_id[app_id.size()] = '\0';
    next.bundle_bytes = bundle_bytes;
    next.bundle_crc32 = bundle_crc32;
    next.allow_downgrade = allow_downgrade;
    if (!store.begin_staging(next)) {
        return result(DeviceInstallStatus::StoreRejected);
    }

    request_ = next;
    phase_ = DeviceInstallPhase::Receiving;
    return result(DeviceInstallStatus::Accepted);
}

DeviceInstallResult DeviceInstallTransaction::append(std::uint32_t transaction_id,
                                                      std::uint32_t offset,
                                                      const std::uint8_t* bytes,
                                                      std::size_t size,
                                                      DeviceInstallStore& store) {
    if (phase_ == DeviceInstallPhase::Idle) {
        return result(DeviceInstallStatus::NoActiveTransaction);
    }
    if (transaction_id != request_.transaction_id || offset != received_bytes_) {
        return result(DeviceInstallStatus::OffsetMismatch);
    }
    if (bytes == nullptr || size == 0 || size > limits_.max_chunk_bytes || size > request_.bundle_bytes - received_bytes_) {
        return result(DeviceInstallStatus::PayloadTooLarge);
    }
    if (!store.write_staging(offset, bytes, size)) {
        store.abort_staging(request_.transaction_id);
        const DeviceInstallResult rejected = result(DeviceInstallStatus::StoreRejected);
        reset();
        return rejected;
    }

    received_bytes_ += static_cast<std::uint32_t>(size);
    return result(DeviceInstallStatus::Accepted);
}

DeviceInstallResult DeviceInstallTransaction::commit(std::uint32_t transaction_id, DeviceInstallStore& store) {
    if (phase_ == DeviceInstallPhase::Idle) {
        return result(DeviceInstallStatus::NoActiveTransaction);
    }
    if (transaction_id != request_.transaction_id) {
        return result(DeviceInstallStatus::OffsetMismatch);
    }
    if (received_bytes_ != request_.bundle_bytes) {
        return result(DeviceInstallStatus::Incomplete);
    }
    if (!store.verify_staging(request_)) {
        store.abort_staging(request_.transaction_id);
        const DeviceInstallResult rejected = result(DeviceInstallStatus::IntegrityRejected);
        reset();
        return rejected;
    }
    if (!store.commit_staging(request_)) {
        store.abort_staging(request_.transaction_id);
        const DeviceInstallResult failed = result(DeviceInstallStatus::CommitFailed);
        reset();
        return failed;
    }

    DeviceInstallResult committed = result(DeviceInstallStatus::Accepted);
    committed.phase = DeviceInstallPhase::Idle;
    reset();
    return committed;
}

DeviceInstallResult DeviceInstallTransaction::abort(std::uint32_t transaction_id, DeviceInstallStore& store) {
    if (phase_ == DeviceInstallPhase::Idle) {
        return result(DeviceInstallStatus::NoActiveTransaction);
    }
    if (transaction_id != request_.transaction_id) {
        return result(DeviceInstallStatus::OffsetMismatch);
    }
    const DeviceInstallResult aborted = result(DeviceInstallStatus::Aborted);
    store.abort_staging(request_.transaction_id);
    reset();
    return aborted;
}

const char* device_install_phase_name(DeviceInstallPhase phase) {
    switch (phase) {
    case DeviceInstallPhase::Idle:
        return "idle";
    case DeviceInstallPhase::Receiving:
        return "receiving";
    }
    return "idle";
}

const char* device_install_status_name(DeviceInstallStatus status) {
    switch (status) {
    case DeviceInstallStatus::Accepted:
        return "accepted";
    case DeviceInstallStatus::Busy:
        return "busy";
    case DeviceInstallStatus::InvalidRequest:
        return "invalid-request";
    case DeviceInstallStatus::OffsetMismatch:
        return "offset-mismatch";
    case DeviceInstallStatus::PayloadTooLarge:
        return "payload-too-large";
    case DeviceInstallStatus::Incomplete:
        return "incomplete";
    case DeviceInstallStatus::StoreRejected:
        return "store-rejected";
    case DeviceInstallStatus::IntegrityRejected:
        return "integrity-rejected";
    case DeviceInstallStatus::CommitFailed:
        return "commit-failed";
    case DeviceInstallStatus::Aborted:
        return "aborted";
    case DeviceInstallStatus::NoActiveTransaction:
        return "no-active-transaction";
    }
    return "invalid-request";
}

} // namespace jellyframe
