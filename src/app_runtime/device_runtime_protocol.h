#pragma once

#include <cstddef>
#include <cstdint>

namespace jellyframe {

constexpr std::uint8_t kDeviceProtocolVersion = 1;
constexpr std::size_t kDeviceProtocolHeaderBytes = 24;
constexpr std::size_t kDeviceProtocolMaxPayloadBytes = 4096;

enum class DeviceMessageType : std::uint8_t {
    Discovery = 1,
    AppList = 2,
    InstallBegin = 3,
    InstallChunk = 4,
    InstallCommit = 5,
    InstallAbort = 6,
    Launch = 7,
    Stop = 8,
    Logs = 9,
    Recovery = 10,
};

enum class DeviceProtocolStatus : std::uint8_t {
    Ok,
    InvalidArgument,
    BufferTooSmall,
    Truncated,
    InvalidMagic,
    UnsupportedVersion,
    UnknownMessageType,
    PayloadTooLarge,
    BadPayloadCrc,
};

struct DeviceFrameHeader {
    DeviceMessageType type = DeviceMessageType::Discovery;
    std::uint16_t flags = 0;
    std::uint32_t session_id = 0;
    std::uint32_t request_id = 0;
    std::uint32_t payload_length = 0;
    std::uint32_t payload_crc32 = 0;
};

DeviceProtocolStatus encode_device_frame(const DeviceFrameHeader& header,
                                         const std::uint8_t* payload,
                                         std::size_t payload_size,
                                         std::uint8_t* output,
                                         std::size_t output_capacity,
                                         std::size_t& output_size);

// The returned payload aliases input and must be copied before crossing a task boundary.
DeviceProtocolStatus decode_device_frame(const std::uint8_t* input,
                                         std::size_t input_size,
                                         DeviceFrameHeader& header,
                                         const std::uint8_t*& payload);

bool is_device_message_type(std::uint8_t value);
const char* device_message_type_name(DeviceMessageType type);
const char* device_protocol_status_name(DeviceProtocolStatus status);

} // namespace jellyframe
