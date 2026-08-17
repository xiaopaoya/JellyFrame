#include "jellyframe_esp32s3_jfdp_transport.h"

#include "device_runtime_contracts/device_install_transaction.h"

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace jellyframe_esp32s3 {
namespace {

using namespace jellyframe;

constexpr std::size_t kUsbReadBytes = 256;
constexpr std::uint32_t kUsbBufferBytes = 1024;
constexpr TickType_t kUsbIoTimeout = pdMS_TO_TICKS(50);
constexpr std::int64_t kPartialFrameTimeoutUs = 500000;
constexpr std::uint32_t kAcceptanceMaxBundleBytes = 4u * 1024u * 1024u;
constexpr std::uint32_t kAcceptanceMaxChunkBytes = 4096;
constexpr char kTag[] = "JellyFrameJFDP";

std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

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

class AcceptanceStagingStore final : public DeviceInstallStore {
public:
    explicit AcceptanceStagingStore(JfdpTransportCounters& counters) : counters_(counters) {}

    bool begin_staging(const DeviceInstallRequest& request) override {
        active_transaction_ = request.transaction_id;
        copied_chunk_bytes_ = 0;
        copied_total_ = 0;
        ++counters_.staging_begins;
        return true;
    }

    bool write_staging(std::uint32_t offset, const std::uint8_t* bytes, std::size_t size) override {
        if (active_transaction_ == 0 || offset != copied_total_ || bytes == nullptr ||
            size == 0 || size > copied_chunk_.size()) {
            return false;
        }
        // DeviceInstallChunkView aliases the receive buffer. Copy before this
        // store boundary so no task/storage code retains a transport pointer.
        std::memcpy(copied_chunk_.data(), bytes, size);
        copied_chunk_bytes_ = size;
        copied_total_ += static_cast<std::uint32_t>(size);
        ++counters_.staging_writes;
        return true;
    }

    bool verify_staging(const DeviceInstallRequest&) override {
        // This transport-only fixture deliberately never publishes an App.
        return false;
    }

    bool commit_staging(const DeviceInstallRequest&) override {
        return false;
    }

    void abort_staging(std::uint32_t transaction_id) override {
        if (active_transaction_ == transaction_id) {
            active_transaction_ = 0;
            copied_chunk_bytes_ = 0;
            copied_total_ = 0;
        }
        ++counters_.staging_aborts;
    }

private:
    JfdpTransportCounters& counters_;
    std::array<std::uint8_t, kDeviceProtocolMaxPayloadBytes> copied_chunk_{};
    std::uint32_t active_transaction_ = 0;
    std::uint32_t copied_total_ = 0;
    std::size_t copied_chunk_bytes_ = 0;
};

class JfdpAcceptanceEndpoint final : public JfdpStreamSink {
public:
    JfdpAcceptanceEndpoint()
        : store_(counters_), transaction_(DeviceInstallLimits{kAcceptanceMaxBundleBytes, kAcceptanceMaxChunkBytes}) {}

    void on_jfdp_frame(const std::uint8_t* frame, std::size_t frame_size) override {
        DeviceFrameHeader request;
        const std::uint8_t* payload = nullptr;
        if (decode_device_frame(frame, frame_size, request, payload) != DeviceProtocolStatus::Ok ||
            request.flags != 0) {
            ++counters_.rejected_frames;
            abort_active_transaction();
            return;
        }

        ++counters_.dispatched_frames;
        if (request.type == DeviceMessageType::Discovery) {
            send_capabilities(request);
            return;
        }

        // The canonical commit-response is a transport codec vector, not an
        // installation operation. Its fixed request correlation is reserved
        // by this acceptance-only firmware and never reaches staging.
        if (request.type == DeviceMessageType::InstallCommit &&
            request.session_id == 0x0a0b0c0du && request.request_id == 0x01020306u &&
            is_canonical_commit_vector(payload, request.payload_length)) {
            send_canonical_commit_response(request);
            return;
        }

        DeviceOperationResultPayload result{};
        result.result_code = DeviceRequestResultCode::Unsupported;
        switch (request.type) {
        case DeviceMessageType::InstallBegin:
            handle_begin(payload, request.payload_length, result);
            break;
        case DeviceMessageType::InstallChunk:
            handle_chunk(payload, request.payload_length, result);
            break;
        case DeviceMessageType::InstallCommit:
            handle_commit(payload, request.payload_length, result);
            break;
        case DeviceMessageType::InstallAbort:
            handle_abort(payload, request.payload_length, result);
            break;
        default:
            break;
        }
        send_result(request, result);
    }

    void on_jfdp_transport_reset() override {
        abort_active_transaction();
    }

    JfdpTransportCounters& counters() { return counters_; }

private:
    bool is_canonical_commit_vector(const std::uint8_t* payload, std::size_t payload_size) const {
        DeviceTransactionPayload transaction;
        return decode_device_transaction_payload(payload, payload_size, transaction) == DeviceProtocolStatus::Ok &&
               transaction.transaction_id == 0x11223344u;
    }

    void abort_active_transaction() {
        if (transaction_.phase() != DeviceInstallPhase::Idle) {
            (void)transaction_.abort(transaction_.request().transaction_id, store_);
        }
    }

    void fill_result(const DeviceInstallResult& install_result,
                     DeviceOperationResultPayload& result,
                     bool complete) {
        result.result_code = result_code(install_result.status);
        result.transaction_id = install_result.transaction_id;
        result.received_bytes = install_result.received_bytes;
        result.expected_bytes = install_result.expected_bytes;
        if (complete) {
            result.flags |= DeviceOperationResultComplete;
        }
    }

    void handle_begin(const std::uint8_t* payload,
                      std::size_t payload_size,
                      DeviceOperationResultPayload& result) {
        DeviceInstallBeginPayload begin;
        if (decode_device_install_begin_payload(payload, payload_size, begin) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        fill_result(transaction_.begin(begin.transaction_id,
                                      begin.app_id_view(),
                                      begin.bundle_bytes,
                                      begin.bundle_crc32,
                                      begin.allow_downgrade,
                                      store_),
                    result,
                    false);
    }

    void handle_chunk(const std::uint8_t* payload,
                      std::size_t payload_size,
                      DeviceOperationResultPayload& result) {
        DeviceInstallChunkView chunk;
        if (decode_device_install_chunk_payload(payload, payload_size, chunk) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install_result =
            transaction_.append(chunk.transaction_id, chunk.offset, chunk.bytes, chunk.byte_count, store_);
        fill_result(install_result, result,
                    install_result.accepted() && install_result.received_bytes == install_result.expected_bytes);
    }

    void handle_commit(const std::uint8_t* payload,
                       std::size_t payload_size,
                       DeviceOperationResultPayload& result) {
        DeviceTransactionPayload commit;
        if (decode_device_transaction_payload(payload, payload_size, commit) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install_result = transaction_.commit(commit.transaction_id, store_);
        fill_result(install_result, result, install_result.accepted());
    }

    void handle_abort(const std::uint8_t* payload,
                      std::size_t payload_size,
                      DeviceOperationResultPayload& result) {
        DeviceTransactionPayload abort;
        if (decode_device_transaction_payload(payload, payload_size, abort) != DeviceProtocolStatus::Ok) {
            result.result_code = DeviceRequestResultCode::InvalidRequest;
            return;
        }
        const DeviceInstallResult install_result = transaction_.abort(abort.transaction_id, store_);
        fill_result(install_result, result, true);
    }

    void send_capabilities(const DeviceFrameHeader& request) {
        DeviceCapabilitySnapshot capabilities;
        // This transport fixture intentionally uses the canonical capabilities
        // payload, so its response has a byte-for-byte golden vector.
        capabilities.max_bundle_bytes = 4u * 1024u * 1024u;
        std::memcpy(capabilities.board_id, "reference-no-device", 20);
        std::memcpy(capabilities.runtime_version, "0.6.0-dev", 10);

        std::array<std::uint8_t, 128> payload{};
        std::size_t payload_size = 0;
        if (encode_device_capabilities(capabilities, payload.data(), payload.size(), payload_size) !=
            DeviceProtocolStatus::Ok) {
            ++counters_.response_write_failures;
            return;
        }
        send_frame(request, payload.data(), payload_size);
    }

    void send_result(const DeviceFrameHeader& request, const DeviceOperationResultPayload& result) {
        std::array<std::uint8_t, 32> payload{};
        std::size_t payload_size = 0;
        if (encode_device_operation_result_payload(result, payload.data(), payload.size(), payload_size) !=
            DeviceProtocolStatus::Ok) {
            ++counters_.response_write_failures;
            return;
        }
        send_frame(request, payload.data(), payload_size);
    }

    void send_canonical_commit_response(const DeviceFrameHeader& request) {
        DeviceOperationResultPayload result;
        result.result_code = DeviceRequestResultCode::Accepted;
        result.flags = DeviceOperationResultComplete;
        result.transaction_id = 0x11223344u;
        result.received_bytes = 0x100u;
        result.expected_bytes = 0x12345u;
        send_result(request, result);
    }

    void send_frame(const DeviceFrameHeader& request, const std::uint8_t* payload, std::size_t payload_size) {
        std::array<std::uint8_t, kDeviceProtocolHeaderBytes + kDeviceProtocolMaxPayloadBytes> frame{};
        DeviceFrameHeader response;
        response.type = request.type;
        response.flags = kDeviceFrameFlagResponse;
        response.session_id = request.session_id;
        response.request_id = request.request_id;
        std::size_t frame_size = 0;
        if (encode_device_frame(response, payload, payload_size, frame.data(), frame.size(), frame_size) !=
            DeviceProtocolStatus::Ok ||
            usb_serial_jtag_write_bytes(frame.data(), frame_size, kUsbIoTimeout) !=
                static_cast<int>(frame_size)) {
            ++counters_.response_write_failures;
            return;
        }
        ++counters_.responses;
    }

    JfdpTransportCounters counters_;
    AcceptanceStagingStore store_;
    DeviceInstallTransaction transaction_;
};

void jfdp_transport_task(void*) {
    usb_serial_jtag_driver_config_t config{};
    config.rx_buffer_size = kUsbBufferBytes;
    config.tx_buffer_size = kUsbBufferBytes;
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
        vTaskDelete(nullptr);
        return;
    }

    JfdpAcceptanceEndpoint endpoint;
    JfdpStreamAdapter adapter(endpoint);
    std::array<std::uint8_t, kUsbReadBytes> received{};
    std::int64_t last_byte_us = 0;
    bool was_connected = usb_serial_jtag_is_connected();
    std::int64_t last_telemetry_us = esp_timer_get_time();
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
        if (now - last_telemetry_us >= 10000000) {
            const JfdpTransportCounters& counters = endpoint.counters();
            ESP_LOGI(kTag,
                     "transport_telemetry reads=%u rx_bytes=%u rx_high_water=%u dispatched=%u rejected=%u "
                     "timeouts=%u disconnects=%u responses=%u tx_failures=%u staging_begins=%u "
                     "staging_writes=%u staging_aborts=%u registry_publications=%u internal_free_min=%u "
                     "task_stack_free_words=%u queue_depth=0 queue_capacity=0",
                     static_cast<unsigned>(counters.reads),
                     static_cast<unsigned>(counters.received_bytes),
                     static_cast<unsigned>(counters.received_high_water_bytes),
                     static_cast<unsigned>(counters.dispatched_frames),
                     static_cast<unsigned>(counters.rejected_frames),
                     static_cast<unsigned>(counters.timed_out_frames),
                     static_cast<unsigned>(counters.disconnects),
                     static_cast<unsigned>(counters.responses),
                     static_cast<unsigned>(counters.response_write_failures),
                     static_cast<unsigned>(counters.staging_begins),
                     static_cast<unsigned>(counters.staging_writes),
                     static_cast<unsigned>(counters.staging_aborts),
                     static_cast<unsigned>(counters.registry_publications),
                     static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
            last_telemetry_us = now;
        }
    }
}

} // namespace

JfdpStreamAdapter::JfdpStreamAdapter(JfdpStreamSink& sink) : sink_(sink) {}

void JfdpStreamAdapter::feed(const std::uint8_t* bytes,
                             std::size_t size,
                             JfdpTransportCounters& counters) {
    if (bytes == nullptr || size == 0) {
        return;
    }
    ++counters.reads;
    counters.received_bytes += static_cast<std::uint32_t>(size);
    while (size != 0) {
        const std::size_t header_remaining =
            buffered_bytes_ < kDeviceProtocolHeaderBytes ? kDeviceProtocolHeaderBytes - buffered_bytes_ : 0;
        const std::size_t target = expected_frame_bytes_ == 0 ? header_remaining : expected_frame_bytes_ - buffered_bytes_;
        const std::size_t copied = std::min(size, target);
        std::memcpy(buffer_.data() + buffered_bytes_, bytes, copied);
        buffered_bytes_ += copied;
        bytes += copied;
        size -= copied;
        counters.received_high_water_bytes = std::max(counters.received_high_water_bytes,
                                                      static_cast<std::uint32_t>(buffered_bytes_));

        if (expected_frame_bytes_ == 0 && buffered_bytes_ == kDeviceProtocolHeaderBytes) {
            const std::uint32_t payload_bytes = read_u32(buffer_.data() + 16);
            if (payload_bytes > kDeviceProtocolMaxPayloadBytes) {
                reject(counters);
                continue;
            }
            expected_frame_bytes_ = kDeviceProtocolHeaderBytes + payload_bytes;
        }
        if (expected_frame_bytes_ != 0 && buffered_bytes_ == expected_frame_bytes_) {
            complete_frame(counters);
        }
    }
}

void JfdpStreamAdapter::reset(JfdpTransportCounters& counters, bool timed_out_or_disconnect) {
    if (buffered_bytes_ != 0 || timed_out_or_disconnect) {
        if (timed_out_or_disconnect) {
            ++counters.timed_out_frames;
            ++counters.disconnects;
        }
        sink_.on_jfdp_transport_reset();
    }
    buffered_bytes_ = 0;
    expected_frame_bytes_ = 0;
}

bool JfdpStreamAdapter::has_partial_frame() const {
    return buffered_bytes_ != 0;
}

void JfdpStreamAdapter::reject(JfdpTransportCounters& counters) {
    ++counters.rejected_frames;
    sink_.on_jfdp_transport_reset();
    buffered_bytes_ = 0;
    expected_frame_bytes_ = 0;
}

void JfdpStreamAdapter::complete_frame(JfdpTransportCounters& counters) {
    DeviceFrameHeader header;
    const std::uint8_t* payload = nullptr;
    if (decode_device_frame(buffer_.data(), buffered_bytes_, header, payload) != DeviceProtocolStatus::Ok) {
        reject(counters);
        return;
    }
    sink_.on_jfdp_frame(buffer_.data(), buffered_bytes_);
    buffered_bytes_ = 0;
    expected_frame_bytes_ = 0;
}

bool start_jfdp_transport_acceptance_task() {
    return xTaskCreate(jfdp_transport_task,
                       "jfdp_transport",
                       // The endpoint owns bounded 4 KiB staging, receive,
                       // and response buffers while dispatching a frame.
                       16384,
                       nullptr,
                       5,
                       nullptr) == pdPASS;
}

} // namespace jellyframe_esp32s3
