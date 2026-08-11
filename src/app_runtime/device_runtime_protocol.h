#pragma once

#include <cstddef>
#include <cstdint>

namespace jellyframe {

constexpr std::uint8_t kDeviceProtocolVersion = 1;
constexpr std::size_t kDeviceProtocolHeaderBytes = 24;
constexpr std::size_t kDeviceProtocolMaxPayloadBytes = 4096;
constexpr std::size_t kDeviceCapabilityMaxBoardIdBytes = 63;
constexpr std::size_t kDeviceCapabilityMaxRuntimeVersionBytes = 31;
constexpr std::uint16_t kDeviceFrameFlagResponse = 1u << 0;

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

enum class DeviceRequestResultCode : std::uint8_t {
    Ok = 0,
    Accepted = 1,
    Queued = 2,
    InvalidRequest = 3,
    Busy = 4,
    Unsupported = 5,
    Denied = 6,
    NotFound = 7,
    StaleSession = 8,
    StaleRequest = 9,
    PayloadTooLarge = 10,
    IntegrityFailed = 11,
    StorageFull = 12,
    Cancelled = 13,
    Failed = 14,
};

enum DeviceCapability : std::uint32_t {
    DeviceCapabilityScripting = 1u << 0,
    DeviceCapabilityCanvas2d = 1u << 1,
    DeviceCapabilityMediaFrame = 1u << 2,
    DeviceCapabilityTouch = 1u << 3,
    DeviceCapabilityDeviceLogs = 1u << 4,
    DeviceCapabilityFrameCapture = 1u << 5,
    DeviceCapabilityStorageKv = 1u << 6,
};

struct DeviceCapabilitySnapshot {
    std::uint8_t protocol_version = kDeviceProtocolVersion;
    std::uint16_t display_width = 0;
    std::uint16_t display_height = 0;
    std::uint32_t capability_bits = 0;
    std::uint32_t max_bundle_bytes = 0;
    std::uint32_t available_storage_bytes = 0;
    char board_id[kDeviceCapabilityMaxBoardIdBytes + 1]{};
    char runtime_version[kDeviceCapabilityMaxRuntimeVersionBytes + 1]{};
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
const char* device_request_result_code_name(DeviceRequestResultCode code);

DeviceProtocolStatus encode_device_capabilities(const DeviceCapabilitySnapshot& capabilities,
                                                std::uint8_t* output,
                                                std::size_t output_capacity,
                                                std::size_t& output_size);
DeviceProtocolStatus decode_device_capabilities(const std::uint8_t* input,
                                                std::size_t input_size,
                                                DeviceCapabilitySnapshot& capabilities);

} // namespace jellyframe
