#include "device_runtime_contracts/device_runtime_protocol.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace jellyframe;

namespace {

template <std::size_t DestinationSize, std::size_t SourceSize>
void copy_string_literal(char (&destination)[DestinationSize], const char (&source)[SourceSize]) {
    static_assert(SourceSize <= DestinationSize, "string literal must fit its destination");
    std::copy_n(source, SourceSize, destination);
}

template <std::size_t DestinationSize, std::size_t SourceSize>
void copy_string_literal(std::array<char, DestinationSize>& destination,
                         const char (&source)[SourceSize]) {
    static_assert(SourceSize <= DestinationSize, "string literal must fit its destination");
    std::copy_n(source, SourceSize, destination.begin());
}

void round_trip_preserves_value_header_and_payload() {
    const std::array<std::uint8_t, 5> payload{{1, 2, 3, 4, 5}};
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + payload.size()> encoded{};
    DeviceFrameHeader input;
    input.type = DeviceMessageType::InstallChunk;
    input.flags = 0x55aa;
    input.session_id = 17;
    input.request_id = 99;
    std::size_t encoded_size = 0;
    assert(encode_device_frame(input, payload.data(), payload.size(), encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::Ok);
    assert(encoded_size == encoded.size());

    DeviceFrameHeader output;
    const std::uint8_t* decoded_payload = nullptr;
    assert(decode_device_frame(encoded.data(), encoded_size, output, decoded_payload) == DeviceProtocolStatus::Ok);
    assert(output.type == input.type);
    assert(output.flags == input.flags);
    assert(output.session_id == input.session_id);
    assert(output.request_id == input.request_id);
    assert(output.payload_length == payload.size());
    assert(decoded_payload != nullptr);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        assert(decoded_payload[i] == payload[i]);
    }
}

void rejects_malformed_frames_without_exposing_payload() {
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + 1> encoded{};
    const std::uint8_t byte = 7;
    DeviceFrameHeader input;
    input.type = DeviceMessageType::Discovery;
    std::size_t encoded_size = 0;
    assert(encode_device_frame(input, &byte, 1, encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::Ok);

    DeviceFrameHeader output;
    const std::uint8_t* payload = nullptr;
    encoded[0] = 'X';
    assert(decode_device_frame(encoded.data(), encoded_size, output, payload) == DeviceProtocolStatus::InvalidMagic);
    assert(payload == nullptr);
    encoded[0] = 'J';
    encoded[4] = 2;
    assert(decode_device_frame(encoded.data(), encoded_size, output, payload) == DeviceProtocolStatus::UnsupportedVersion);
    encoded[4] = kDeviceProtocolVersion;
    encoded[5] = 0xff;
    assert(decode_device_frame(encoded.data(), encoded_size, output, payload) == DeviceProtocolStatus::UnknownMessageType);
    encoded[5] = static_cast<std::uint8_t>(DeviceMessageType::Discovery);
    encoded[kDeviceProtocolHeaderBytes] ^= 1;
    assert(decode_device_frame(encoded.data(), encoded_size, output, payload) == DeviceProtocolStatus::BadPayloadCrc);
    assert(payload == nullptr);
}

void enforces_bounded_payload_and_exact_frame_size() {
    std::array<std::uint8_t, kDeviceProtocolMaxPayloadBytes + 1> too_large{};
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + kDeviceProtocolMaxPayloadBytes> output{};
    DeviceFrameHeader header;
    std::size_t output_size = 123;
    assert(encode_device_frame(header, too_large.data(), too_large.size(), output.data(), output.size(), output_size) == DeviceProtocolStatus::PayloadTooLarge);
    assert(output_size == 0);

    const std::uint8_t byte = 1;
    assert(encode_device_frame(header, &byte, 1, output.data(), kDeviceProtocolHeaderBytes, output_size) == DeviceProtocolStatus::BufferTooSmall);
    assert(output_size == 0);

    const std::uint8_t* payload = nullptr;
    assert(decode_device_frame(output.data(), kDeviceProtocolHeaderBytes - 1, header, payload) == DeviceProtocolStatus::Truncated);
    assert(payload == nullptr);

    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + 1> exact_frame{};
    std::size_t exact_frame_size = 0;
    assert(encode_device_frame(header,
                               &byte,
                               1,
                               exact_frame.data(),
                               exact_frame.size(),
                               exact_frame_size) == DeviceProtocolStatus::Ok);
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + 2> trailing_frame{};
    std::copy(exact_frame.begin(), exact_frame.end(), trailing_frame.begin());
    assert(decode_device_frame(trailing_frame.data(), trailing_frame.size(), header, payload) ==
           DeviceProtocolStatus::InvalidArgument);

}

void capabilities_round_trip_without_dynamic_storage() {
    DeviceCapabilitySnapshot input;
    input.display_width = 172;
    input.display_height = 320;
    input.capability_bits = DeviceCapabilityScripting | DeviceCapabilityTouch | DeviceCapabilityDeviceLogs;
    input.max_bundle_bytes = 256 * 1024;
    input.available_storage_bytes = 1024 * 1024;
    copy_string_literal(input.board_id, "ws147");
    copy_string_literal(input.runtime_version, "0.6.0-dev");

    std::array<std::uint8_t, 128> encoded{};
    std::size_t encoded_size = 0;
    assert(encode_device_capabilities(input, encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::Ok);
    DeviceCapabilitySnapshot output;
    assert(decode_device_capabilities(encoded.data(), encoded_size, output) == DeviceProtocolStatus::Ok);
    assert(output.display_width == 172);
    assert(output.display_height == 320);
    assert(output.capability_bits == input.capability_bits);
    assert(output.max_bundle_bytes == input.max_bundle_bytes);
    assert(output.available_storage_bytes == input.available_storage_bytes);
    assert(std::strcmp(output.board_id, input.board_id) == 0);
    assert(std::strcmp(output.runtime_version, input.runtime_version) == 0);
}

void capabilities_reject_unterminated_or_truncated_values() {
    DeviceCapabilitySnapshot input;
    std::fill(std::begin(input.board_id), std::end(input.board_id), 'x');
    std::array<std::uint8_t, 128> encoded{};
    std::size_t encoded_size = 0;
    assert(encode_device_capabilities(input, encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::InvalidArgument);

    input.board_id[0] = 'w';
    input.board_id[1] = '\0';
    assert(encode_device_capabilities(input, encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::Ok);
    DeviceCapabilitySnapshot output;
    assert(decode_device_capabilities(encoded.data(), encoded_size - 1, output) == DeviceProtocolStatus::Truncated);

    copy_string_literal(input.board_id, "ws147");
    assert(encode_device_capabilities(input, encoded.data(), encoded.size(), encoded_size) == DeviceProtocolStatus::Ok);
    constexpr std::size_t kCapabilityFixedPayloadBytes = 20;
    encoded[kCapabilityFixedPayloadBytes + 1] = 0;
    assert(decode_device_capabilities(encoded.data(), encoded_size, output) ==
           DeviceProtocolStatus::InvalidArgument);
}

void discovery_request_response_loopback_preserves_session_and_capabilities() {
    std::array<std::uint8_t, kDeviceProtocolHeaderBytes> request_bytes{};
    DeviceFrameHeader request;
    request.type = DeviceMessageType::Discovery;
    request.session_id = 0x1001u;
    request.request_id = 0x2002u;
    std::size_t request_size = 0;
    assert(encode_device_frame(request, nullptr, 0, request_bytes.data(), request_bytes.size(), request_size) ==
           DeviceProtocolStatus::Ok);

    DeviceFrameHeader endpoint_request;
    const std::uint8_t* request_payload = nullptr;
    assert(decode_device_frame(request_bytes.data(), request_size, endpoint_request, request_payload) ==
           DeviceProtocolStatus::Ok);
    assert(endpoint_request.type == DeviceMessageType::Discovery);
    assert((endpoint_request.flags & kDeviceFrameFlagResponse) == 0);
    assert(endpoint_request.payload_length == 0);

    DeviceCapabilitySnapshot advertised;
    advertised.display_width = 172;
    advertised.display_height = 320;
    advertised.capability_bits = DeviceCapabilityTouch | DeviceCapabilityDeviceLogs;
    advertised.max_bundle_bytes = 192 * 1024;
    advertised.available_storage_bytes = 384 * 1024;
    copy_string_literal(advertised.board_id, "loopback-172x320");
    copy_string_literal(advertised.runtime_version, "0.6.0-dev");

    std::array<std::uint8_t, 128> capability_bytes{};
    std::size_t capability_size = 0;
    assert(encode_device_capabilities(advertised,
                                      capability_bytes.data(),
                                      capability_bytes.size(),
                                      capability_size) == DeviceProtocolStatus::Ok);

    std::array<std::uint8_t, kDeviceProtocolHeaderBytes + 128> response_bytes{};
    DeviceFrameHeader response;
    response.type = endpoint_request.type;
    response.flags = kDeviceFrameFlagResponse;
    response.session_id = endpoint_request.session_id;
    response.request_id = endpoint_request.request_id;
    std::size_t response_size = 0;
    assert(encode_device_frame(response,
                               capability_bytes.data(),
                               capability_size,
                               response_bytes.data(),
                               response_bytes.size(),
                               response_size) == DeviceProtocolStatus::Ok);

    DeviceFrameHeader client_response;
    const std::uint8_t* response_payload = nullptr;
    assert(decode_device_frame(response_bytes.data(), response_size, client_response, response_payload) ==
           DeviceProtocolStatus::Ok);
    assert(client_response.type == DeviceMessageType::Discovery);
    assert((client_response.flags & kDeviceFrameFlagResponse) != 0);
    assert(client_response.session_id == request.session_id);
    assert(client_response.request_id == request.request_id);

    DeviceCapabilitySnapshot observed;
    assert(decode_device_capabilities(response_payload, client_response.payload_length, observed) ==
           DeviceProtocolStatus::Ok);
    assert(observed.display_width == advertised.display_width);
    assert(observed.display_height == advertised.display_height);
    assert(observed.capability_bits == advertised.capability_bits);
    assert(observed.max_bundle_bytes == advertised.max_bundle_bytes);
    assert(observed.available_storage_bytes == advertised.available_storage_bytes);
    assert(std::strcmp(observed.board_id, advertised.board_id) == 0);
    assert(std::strcmp(observed.runtime_version, advertised.runtime_version) == 0);
}

void typed_install_payloads_preserve_bounded_values() {
    DeviceInstallBeginPayload begin;
    begin.transaction_id = 71;
    begin.bundle_bytes = 1234;
    begin.bundle_crc32 = 0xaabbccdd;
    begin.allow_downgrade = true;
    copy_string_literal(begin.app_id, "org.example.payload");

    std::array<std::uint8_t, kDeviceProtocolMaxPayloadBytes> encoded{};
    std::size_t encoded_size = 0;
    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    assert(encoded[0] == 1);
    assert(encoded[1] == 1);
    assert(encoded[2] == begin.app_id_view().size());

    DeviceInstallBeginPayload decoded_begin;
    assert(decode_device_install_begin_payload(encoded.data(), encoded_size, decoded_begin) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_begin.transaction_id == begin.transaction_id);
    assert(decoded_begin.bundle_bytes == begin.bundle_bytes);
    assert(decoded_begin.bundle_crc32 == begin.bundle_crc32);
    assert(decoded_begin.allow_downgrade);
    assert(decoded_begin.app_id_view() == begin.app_id_view());

    const std::array<std::uint8_t, 3> chunk_bytes{{7, 8, 9}};
    assert(encode_device_install_chunk_payload(begin.transaction_id,
                                               17,
                                               chunk_bytes.data(),
                                               chunk_bytes.size(),
                                               encoded.data(),
                                               encoded.size(),
                                               encoded_size) == DeviceProtocolStatus::Ok);
    DeviceInstallChunkView decoded_chunk;
    assert(decode_device_install_chunk_payload(encoded.data(), encoded_size, decoded_chunk) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_chunk.transaction_id == begin.transaction_id);
    assert(decoded_chunk.offset == 17);
    assert(decoded_chunk.byte_count == chunk_bytes.size());
    assert(std::equal(chunk_bytes.begin(), chunk_bytes.end(), decoded_chunk.bytes));

    DeviceTransactionPayload transaction{begin.transaction_id};
    assert(encode_device_transaction_payload(transaction, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    DeviceTransactionPayload decoded_transaction;
    assert(decode_device_transaction_payload(encoded.data(), encoded_size, decoded_transaction) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_transaction.transaction_id == transaction.transaction_id);
}

void typed_lifecycle_and_result_payloads_preserve_values() {
    std::array<std::uint8_t, kDeviceProtocolMaxPayloadBytes> encoded{};
    std::size_t encoded_size = 0;

    DeviceAppIdPayload app;
    copy_string_literal(app.app_id, "org.example.lifecycle");
    assert(encode_device_app_id_payload(app, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    DeviceAppIdPayload decoded_app;
    assert(decode_device_app_id_payload(encoded.data(), encoded_size, decoded_app) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_app.app_id_view() == app.app_id_view());

    DeviceLogsRequestPayload logs;
    logs.limit = 64;
    assert(encode_device_logs_request_payload(logs, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    DeviceLogsRequestPayload decoded_logs;
    assert(decode_device_logs_request_payload(encoded.data(), encoded_size, decoded_logs) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_logs.app_id_view().empty());
    assert(decoded_logs.limit == logs.limit);

    DeviceOperationResultPayload result;
    result.result_code = DeviceRequestResultCode::Accepted;
    result.flags = DeviceOperationResultComplete | DeviceOperationResultActive;
    result.transaction_id = 91;
    result.received_bytes = 1024;
    result.expected_bytes = 1024;
    assert(encode_device_operation_result_payload(result, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    DeviceOperationResultPayload decoded_result;
    assert(decode_device_operation_result_payload(encoded.data(), encoded_size, decoded_result) ==
           DeviceProtocolStatus::Ok);
    assert(decoded_result.result_code == result.result_code);
    assert(decoded_result.flags == result.flags);
    assert(decoded_result.transaction_id == result.transaction_id);
    assert(decoded_result.received_bytes == result.received_bytes);
    assert(decoded_result.expected_bytes == result.expected_bytes);

    assert(is_device_message_type(static_cast<std::uint8_t>(DeviceMessageType::Remove)));
    assert(is_device_message_type(static_cast<std::uint8_t>(DeviceMessageType::Rollback)));
    assert(std::strcmp(device_message_type_name(DeviceMessageType::Remove), "remove") == 0);
    assert(std::strcmp(device_message_type_name(DeviceMessageType::Rollback), "rollback") == 0);
}

void typed_payloads_reject_malformed_or_ambiguous_input() {
    DeviceInstallBeginPayload begin;
    begin.transaction_id = 1;
    begin.bundle_bytes = 4;
    copy_string_literal(begin.app_id, "org.example.reject");
    std::array<std::uint8_t, 128> encoded{};
    std::size_t encoded_size = 0;
    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    encoded[1] = 0x80;
    DeviceInstallBeginPayload decoded_begin;
    assert(decode_device_install_begin_payload(encoded.data(), encoded_size, decoded_begin) ==
           DeviceProtocolStatus::InvalidArgument);

    begin.bundle_bytes = 0;
    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::InvalidArgument);
    begin.bundle_bytes = 4;
    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    std::fill(encoded.begin() + 8, encoded.begin() + 12, 0);
    assert(decode_device_install_begin_payload(encoded.data(), encoded_size, decoded_begin) ==
           DeviceProtocolStatus::InvalidArgument);

    assert(encode_device_install_begin_payload(begin, encoded.data(), encoded.size(), encoded_size) ==
           DeviceProtocolStatus::Ok);
    encoded[16 + 3] = '\0';
    assert(decode_device_install_begin_payload(encoded.data(), encoded_size, decoded_begin) ==
           DeviceProtocolStatus::InvalidArgument);

    const std::uint8_t byte = 1;
    assert(encode_device_install_chunk_payload(1,
                                               0,
                                               &byte,
                                               1,
                                               encoded.data(),
                                               encoded.size(),
                                               encoded_size) == DeviceProtocolStatus::Ok);
    encoded[2] = 2;
    DeviceInstallChunkView decoded_chunk;
    assert(decode_device_install_chunk_payload(encoded.data(), encoded_size, decoded_chunk) ==
           DeviceProtocolStatus::Truncated);

    encoded.fill(0);
    encoded[0] = 1;
    encoded[1] = 0xff;
    DeviceOperationResultPayload decoded_result;
    assert(decode_device_operation_result_payload(encoded.data(), 16, decoded_result) ==
           DeviceProtocolStatus::InvalidArgument);
}

} // namespace

int main() {
    round_trip_preserves_value_header_and_payload();
    rejects_malformed_frames_without_exposing_payload();
    enforces_bounded_payload_and_exact_frame_size();
    capabilities_round_trip_without_dynamic_storage();
    capabilities_reject_unterminated_or_truncated_values();
    discovery_request_response_loopback_preserves_session_and_capabilities();
    typed_install_payloads_preserve_bounded_values();
    typed_lifecycle_and_result_payloads_preserve_values();
    typed_payloads_reject_malformed_or_ambiguous_input();
    return 0;
}
