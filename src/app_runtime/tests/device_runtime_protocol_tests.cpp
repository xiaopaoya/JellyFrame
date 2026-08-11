#include "app_runtime/device_runtime_protocol.h"

#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace jellyframe;

namespace {

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
}

void capabilities_round_trip_without_dynamic_storage() {
    DeviceCapabilitySnapshot input;
    input.display_width = 172;
    input.display_height = 320;
    input.capability_bits = DeviceCapabilityScripting | DeviceCapabilityTouch | DeviceCapabilityDeviceLogs;
    input.max_bundle_bytes = 256 * 1024;
    input.available_storage_bytes = 1024 * 1024;
    std::strcpy(input.board_id, "ws147");
    std::strcpy(input.runtime_version, "0.6.0-dev");

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
    std::strcpy(advertised.board_id, "loopback-172x320");
    std::strcpy(advertised.runtime_version, "0.6.0-dev");

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

} // namespace

int main() {
    round_trip_preserves_value_header_and_payload();
    rejects_malformed_frames_without_exposing_payload();
    enforces_bounded_payload_and_exact_frame_size();
    capabilities_round_trip_without_dynamic_storage();
    capabilities_reject_unterminated_or_truncated_values();
    discovery_request_response_loopback_preserves_session_and_capabilities();
    return 0;
}
