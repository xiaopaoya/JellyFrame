#include "app_runtime/device_runtime_protocol.h"

#include <array>
#include <cassert>
#include <cstdint>

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

} // namespace

int main() {
    round_trip_preserves_value_header_and_payload();
    rejects_malformed_frames_without_exposing_payload();
    enforces_bounded_payload_and_exact_frame_size();
    return 0;
}
