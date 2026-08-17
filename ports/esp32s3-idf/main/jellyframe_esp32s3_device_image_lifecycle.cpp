#include "jellyframe_esp32s3_device_image_store.h"
#include "jellyframe_esp32s3_jfdp_transport.h"

#include "app_runtime/app_installed_bundle.h"
#include "device_runtime_contracts/device_install_transaction.h"

#include "driver/usb_serial_jtag.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <array>
#include <cstring>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr std::size_t kUsbReadBytes = 256;
constexpr std::uint32_t kUsbBufferBytes = 1024;
constexpr TickType_t kUsbIoTimeout = pdMS_TO_TICKS(50);
constexpr std::int64_t kPartialFrameTimeoutUs = 500000;

DeviceRequestResultCode result_code(DeviceInstallStatus status) {
    switch (status) {
    case DeviceInstallStatus::Accepted: return DeviceRequestResultCode::Accepted;
    case DeviceInstallStatus::Busy: return DeviceRequestResultCode::Busy;
    case DeviceInstallStatus::InvalidRequest:
    case DeviceInstallStatus::OffsetMismatch: return DeviceRequestResultCode::InvalidRequest;
    case DeviceInstallStatus::PayloadTooLarge: return DeviceRequestResultCode::PayloadTooLarge;
    case DeviceInstallStatus::Incomplete: return DeviceRequestResultCode::Accepted;
    case DeviceInstallStatus::StoreRejected: return DeviceRequestResultCode::StorageFull;
    case DeviceInstallStatus::IntegrityRejected: return DeviceRequestResultCode::IntegrityFailed;
    case DeviceInstallStatus::CommitFailed: return DeviceRequestResultCode::Failed;
    case DeviceInstallStatus::Aborted: return DeviceRequestResultCode::Cancelled;
    case DeviceInstallStatus::NoActiveTransaction: return DeviceRequestResultCode::NotFound;
    }
    return DeviceRequestResultCode::Failed;
}

class ProtectedLauncher final : public AppProtectedLauncher {
public:
    bool launch_protected_launcher(AppRuntimeHost& host, AppTeardownReason) override {
        return host.launch("protected-launcher", AppRole::Launcher).active();
    }
};

class DeviceImageEndpoint final : public JfdpStreamSink {
public:
    explicit DeviceImageEndpoint(DeviceImageStore& store)
        : store_(store), transaction_(DeviceInstallLimits{DeviceImageStore::kMaxBundleBytes, 4096}),
          host_(AppRuntimeHostOptions{2, 2, 4, 4096, 1}), binding_(store_, &launcher_) {}

    void on_jfdp_frame(const std::uint8_t* frame, std::size_t frame_size) override {
        DeviceFrameHeader request;
        const std::uint8_t* payload = nullptr;
        if (decode_device_frame(frame, frame_size, request, payload) != DeviceProtocolStatus::Ok || request.flags != 0) {
            ++counters_.rejected_frames;
            abort_active_transaction();
            return;
        }
        ++counters_.dispatched_frames;
        switch (request.type) {
        case DeviceMessageType::Discovery: send_capabilities(request); return;
        case DeviceMessageType::AppList: send_app_list(request); return;
        case DeviceMessageType::Recovery: send_recovery(request); return;
        default: break;
        }

        DeviceOperationResultPayload result{};
        result.result_code = DeviceRequestResultCode::Unsupported;
        switch (request.type) {
        case DeviceMessageType::InstallBegin: handle_begin(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallChunk: handle_chunk(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallCommit: handle_commit(payload, request.payload_length, result); break;
        case DeviceMessageType::InstallAbort: handle_abort(payload, request.payload_length, result); break;
        case DeviceMessageType::Launch: handle_launch(payload, request.payload_length, result); break;
        case DeviceMessageType::Stop: handle_stop(payload, request.payload_length, result); break;
        case DeviceMessageType::Remove: handle_remove(payload, request.payload_length, result); break;
        case DeviceMessageType::Rollback: handle_rollback(payload, request.payload_length, result); break;
        default: break;
        }
        send_result(request, result);
    }

    void on_jfdp_transport_reset() override { abort_active_transaction(); }
    JfdpTransportCounters& counters() { return counters_; }

private:
    void abort_active_transaction() {
        if (transaction_.phase() != DeviceInstallPhase::Idle) {
            (void)transaction_.abort(transaction_.request().transaction_id, store_);
        }
    }

    void fill_install_result(const DeviceInstallResult& install,
                             DeviceOperationResultPayload& result,
                             bool complete) {
        result.result_code = result_code(install.status);
        result.transaction_id = install.transaction_id;
        result.received_bytes = install.received_bytes;
        result.expected_bytes = install.expected_bytes;
        if (complete) {
            result.flags |= DeviceOperationResultComplete;
        }
        if (install.accepted() && complete) {
            ++counters_.registry_publications;
        }
    }

    void handle_begin(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceInstallBeginPayload begin;
        if (decode_device_install_begin_payload(bytes, size, begin) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        ++counters_.staging_begins;
        fill_install_result(transaction_.begin(begin.transaction_id, begin.app_id_view(), begin.bundle_bytes,
                                               begin.bundle_crc32, begin.allow_downgrade, store_), result, false);
    }

    void handle_chunk(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceInstallChunkView chunk;
        if (decode_device_install_chunk_payload(bytes, size, chunk) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install = transaction_.append(chunk.transaction_id, chunk.offset, chunk.bytes,
                                                                  chunk.byte_count, store_);
        if (install.accepted()) {
            ++counters_.staging_writes;
        }
        fill_install_result(install, result, install.accepted() && install.received_bytes == install.expected_bytes);
    }

    void handle_commit(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceTransactionPayload commit;
        if (decode_device_transaction_payload(bytes, size, commit) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install = transaction_.commit(commit.transaction_id, store_);
        fill_install_result(install, result, install.accepted());
    }

    void handle_abort(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceTransactionPayload abort;
        if (decode_device_transaction_payload(bytes, size, abort) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        ++counters_.staging_aborts;
        fill_install_result(transaction_.abort(abort.transaction_id, store_), result, true);
    }

    bool decode_app_id(const std::uint8_t* bytes, std::size_t size, DeviceAppIdPayload& app_id,
                       DeviceOperationResultPayload& result) {
        if (decode_device_app_id_payload(bytes, size, app_id) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return false;
        }
        return true;
    }

    void handle_launch(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        const AppInstalledBundleLaunchResult launch = binding_.launch(host_, app_id.app_id_view());
        if (!launch.launched()) {
            store_.record_recovery(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view(),
                                   DeviceRecoveryLauncherActive | DeviceRecoveryAppDisabled);
            (void)binding_.recover_to_protected_launcher(host_, AppTeardownReason::LoadFailure);
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        std::array<std::uint8_t, 512> entry_bytes{};
        std::size_t read_bytes = 0;
        DeviceBundleDescriptor descriptor;
        const DeviceBundleStatus entry_status = binding_.copy_active_descriptor(descriptor)
            ? binding_.read_active_resource(descriptor.summary.entry_path_view(), entry_bytes.data(),
                                            entry_bytes.size(), read_bytes)
            : DeviceBundleStatus::ResourceNotFound;
        if (entry_status != DeviceBundleStatus::Ok || read_bytes == 0) {
            store_.record_recovery(DeviceRecoveryReason::AppLoadFailure, app_id.app_id_view(),
                                   DeviceRecoveryLauncherActive | DeviceRecoveryAppDisabled);
            (void)binding_.recover_to_protected_launcher(host_, AppTeardownReason::LoadFailure);
            result.result_code = DeviceRequestResultCode::Failed;
            result.flags = DeviceOperationResultLauncherActive;
            return;
        }
        result.result_code = DeviceRequestResultCode::Ok;
        result.flags = DeviceOperationResultComplete | DeviceOperationResultActive;
    }

    void handle_stop(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (!binding_.has_active_bundle() || host_.current().app_id != app_id.app_id_view()) {
            result.result_code = DeviceRequestResultCode::NotFound;
            return;
        }
        (void)binding_.terminate_current(host_, AppTeardownReason::NormalExit);
        result.result_code = DeviceRequestResultCode::Ok;
        result.flags = DeviceOperationResultComplete;
    }

    void handle_remove(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (binding_.has_active_bundle()) {
            (void)binding_.terminate_current(host_, AppTeardownReason::SystemPolicy);
        }
        result.result_code = store_.remove(app_id.app_id_view()) ? DeviceRequestResultCode::Ok : DeviceRequestResultCode::NotFound;
        result.flags = result.result_code == DeviceRequestResultCode::Ok ? DeviceOperationResultComplete : 0;
    }

    void handle_rollback(const std::uint8_t* bytes, std::size_t size, DeviceOperationResultPayload& result) {
        DeviceAppIdPayload app_id;
        if (!decode_app_id(bytes, size, app_id, result)) {
            return;
        }
        if (binding_.has_active_bundle()) {
            (void)binding_.terminate_current(host_, AppTeardownReason::AppSwitch);
        }
        result.result_code = store_.rollback(app_id.app_id_view()) ? DeviceRequestResultCode::Ok : DeviceRequestResultCode::NotFound;
        result.flags = result.result_code == DeviceRequestResultCode::Ok ? DeviceOperationResultComplete : 0;
    }

    void send_capabilities(const DeviceFrameHeader& request) {
        DeviceCapabilitySnapshot capabilities{};
        capabilities.display_width = 172;
        capabilities.display_height = 320;
        capabilities.capability_bits = DeviceCapabilityTouch;
        capabilities.max_bundle_bytes = DeviceImageStore::kMaxBundleBytes;
        capabilities.available_storage_bytes = store_.available_storage_bytes();
        std::memcpy(capabilities.board_id, "esp32s3-ws147", 14);
        std::memcpy(capabilities.runtime_version, "0.6.0-dev", 10);
        std::array<std::uint8_t, 128> payload{};
        std::size_t size = 0;
        if (encode_device_capabilities(capabilities, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_app_list(const DeviceFrameHeader& request) {
        DeviceAppListPayload list{};
        if (!store_.list(list)) {
            return;
        }
        // The WS147 raw store exposes one active app record in this first
        // image. Keep list encoding bounded to that typed record instead of
        // paying a second full JFDP frame-sized stack scratch.
        std::array<std::uint8_t, 256> payload{};
        std::size_t size = 0;
        if (encode_device_app_list_payload(list, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_recovery(const DeviceFrameHeader& request) {
        DeviceRecoveryDetailPayload recovery{};
        store_.copy_recovery(recovery);
        std::array<std::uint8_t, 128> payload{};
        std::size_t size = 0;
        if (encode_device_recovery_detail_payload(recovery, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_result(const DeviceFrameHeader& request, const DeviceOperationResultPayload& result) {
        std::array<std::uint8_t, 32> payload{};
        std::size_t size = 0;
        if (encode_device_operation_result_payload(result, payload.data(), payload.size(), size) == DeviceProtocolStatus::Ok) {
            send_payload(request, payload.data(), size);
        }
    }

    void send_payload(const DeviceFrameHeader& request, const std::uint8_t* payload, std::size_t payload_size) {
        std::array<std::uint8_t, kDeviceProtocolHeaderBytes + kDeviceProtocolMaxPayloadBytes> frame{};
        DeviceFrameHeader response{};
        response.type = request.type;
        response.flags = kDeviceFrameFlagResponse;
        response.session_id = request.session_id;
        response.request_id = request.request_id;
        std::size_t frame_size = 0;
        if (encode_device_frame(response, payload, payload_size, frame.data(), frame.size(), frame_size) != DeviceProtocolStatus::Ok ||
            usb_serial_jtag_write_bytes(frame.data(), frame_size, kUsbIoTimeout) != static_cast<int>(frame_size)) {
            ++counters_.response_write_failures;
            return;
        }
        ++counters_.responses;
    }

    JfdpTransportCounters counters_{};
    DeviceImageStore& store_;
    DeviceInstallTransaction transaction_;
    AppRuntimeHost host_;
    ProtectedLauncher launcher_;
    AppInstalledBundleBinding binding_;
};

void device_image_lifecycle_task(void*) {
    usb_serial_jtag_driver_config_t config{};
    config.rx_buffer_size = kUsbBufferBytes;
    config.tx_buffer_size = kUsbBufferBytes;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
        vTaskDelete(nullptr);
        return;
    }
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                                 ESP_PARTITION_SUBTYPE_ANY, "storage");
    DeviceImageStore store(partition);
    if (!store.initialize()) {
        vTaskDelete(nullptr);
        return;
    }
    DeviceImageEndpoint endpoint(store);
    JfdpStreamAdapter adapter(endpoint);
    std::array<std::uint8_t, kUsbReadBytes> received{};
    std::int64_t last_byte_us = 0;
    bool was_connected = usb_serial_jtag_is_connected();
    for (;;) {
        const bool connected = usb_serial_jtag_is_connected();
        if (was_connected && !connected) {
            adapter.reset(endpoint.counters(), true);
            last_byte_us = 0;
        }
        was_connected = connected;
        const int count = usb_serial_jtag_read_bytes(received.data(), received.size(), kUsbIoTimeout);
        const std::int64_t now = esp_timer_get_time();
        if (count > 0) {
            last_byte_us = now;
            adapter.feed(received.data(), static_cast<std::size_t>(count), endpoint.counters());
        } else if (adapter.has_partial_frame() && last_byte_us != 0 && now - last_byte_us >= kPartialFrameTimeoutUs) {
            adapter.reset(endpoint.counters(), true);
            last_byte_us = 0;
        }
    }
}

} // namespace

bool start_device_image_lifecycle_task() {
    // Reassembly plus JFAPPV0 inspection owns bounded 4 KiB buffers while a
    // response frame is encoded. Keep this acceptance endpoint separate from
    // UI/script stacks and leave measured headroom for nested bundle checks.
    return xTaskCreate(device_image_lifecycle_task, "device_image", 24576, nullptr, 5, nullptr) == pdPASS;
}

} // namespace jellyframe_esp32s3
