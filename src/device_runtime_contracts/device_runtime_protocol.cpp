#include "device_runtime_contracts/device_runtime_protocol.h"

#include <cstring>

namespace jellyframe {
namespace {

constexpr std::uint8_t kMagic[] = {'J', 'F', 'D', 'P'};
constexpr std::size_t kCapabilityFixedBytes = 20;

std::uint16_t read_u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t read_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_u16(std::uint8_t* p, std::uint16_t value) {
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::uint8_t* p, std::uint32_t value) {
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
    p[2] = static_cast<std::uint8_t>(value >> 16);
    p[3] = static_cast<std::uint8_t>(value >> 24);
}

std::size_t bounded_c_string_length(const char* value, std::size_t capacity) {
    std::size_t length = 0;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return length;
}

std::uint32_t crc32(const std::uint8_t* bytes, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

} // namespace

bool is_device_message_type(std::uint8_t value) {
    return value >= static_cast<std::uint8_t>(DeviceMessageType::Discovery) &&
           value <= static_cast<std::uint8_t>(DeviceMessageType::Recovery);
}

DeviceProtocolStatus encode_device_frame(const DeviceFrameHeader& header,
                                         const std::uint8_t* payload,
                                         std::size_t payload_size,
                                         std::uint8_t* output,
                                         std::size_t output_capacity,
                                         std::size_t& output_size) {
    output_size = 0;
    if (payload_size > kDeviceProtocolMaxPayloadBytes ||
        (payload_size != 0 && payload == nullptr) || output == nullptr) {
        return payload_size > kDeviceProtocolMaxPayloadBytes
                   ? DeviceProtocolStatus::PayloadTooLarge
                   : DeviceProtocolStatus::InvalidArgument;
    }
    if (!is_device_message_type(static_cast<std::uint8_t>(header.type))) {
        return DeviceProtocolStatus::UnknownMessageType;
    }
    if (header.payload_length != 0 && header.payload_length != payload_size) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t frame_size = kDeviceProtocolHeaderBytes + payload_size;
    if (output_capacity < frame_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }

    std::memcpy(output, kMagic, sizeof(kMagic));
    output[4] = kDeviceProtocolVersion;
    output[5] = static_cast<std::uint8_t>(header.type);
    write_u16(output + 6, header.flags);
    write_u32(output + 8, header.session_id);
    write_u32(output + 12, header.request_id);
    write_u32(output + 16, static_cast<std::uint32_t>(payload_size));
    write_u32(output + 20, crc32(payload, payload_size));
    if (payload_size != 0) {
        std::memcpy(output + kDeviceProtocolHeaderBytes, payload, payload_size);
    }
    output_size = frame_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_frame(const std::uint8_t* input,
                                         std::size_t input_size,
                                         DeviceFrameHeader& header,
                                         const std::uint8_t*& payload) {
    payload = nullptr;
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kDeviceProtocolHeaderBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (std::memcmp(input, kMagic, sizeof(kMagic)) != 0) {
        return DeviceProtocolStatus::InvalidMagic;
    }
    if (input[4] != kDeviceProtocolVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    if (!is_device_message_type(input[5])) {
        return DeviceProtocolStatus::UnknownMessageType;
    }
    const std::uint32_t payload_length = read_u32(input + 16);
    if (payload_length > kDeviceProtocolMaxPayloadBytes) {
        return DeviceProtocolStatus::PayloadTooLarge;
    }
    if (input_size != kDeviceProtocolHeaderBytes + payload_length) {
        return DeviceProtocolStatus::Truncated;
    }

    header.type = static_cast<DeviceMessageType>(input[5]);
    header.flags = read_u16(input + 6);
    header.session_id = read_u32(input + 8);
    header.request_id = read_u32(input + 12);
    header.payload_length = payload_length;
    header.payload_crc32 = read_u32(input + 20);
    payload = input + kDeviceProtocolHeaderBytes;
    if (crc32(payload, payload_length) != header.payload_crc32) {
        payload = nullptr;
        return DeviceProtocolStatus::BadPayloadCrc;
    }
    return DeviceProtocolStatus::Ok;
}

const char* device_message_type_name(DeviceMessageType type) {
    switch (type) {
    case DeviceMessageType::Discovery: return "discovery";
    case DeviceMessageType::AppList: return "app-list";
    case DeviceMessageType::InstallBegin: return "install-begin";
    case DeviceMessageType::InstallChunk: return "install-chunk";
    case DeviceMessageType::InstallCommit: return "install-commit";
    case DeviceMessageType::InstallAbort: return "install-abort";
    case DeviceMessageType::Launch: return "launch";
    case DeviceMessageType::Stop: return "stop";
    case DeviceMessageType::Logs: return "logs";
    case DeviceMessageType::Recovery: return "recovery";
    }
    return "unknown";
}

const char* device_protocol_status_name(DeviceProtocolStatus status) {
    switch (status) {
    case DeviceProtocolStatus::Ok: return "ok";
    case DeviceProtocolStatus::InvalidArgument: return "invalid-argument";
    case DeviceProtocolStatus::BufferTooSmall: return "buffer-too-small";
    case DeviceProtocolStatus::Truncated: return "truncated";
    case DeviceProtocolStatus::InvalidMagic: return "invalid-magic";
    case DeviceProtocolStatus::UnsupportedVersion: return "unsupported-version";
    case DeviceProtocolStatus::UnknownMessageType: return "unknown-message-type";
    case DeviceProtocolStatus::PayloadTooLarge: return "payload-too-large";
    case DeviceProtocolStatus::BadPayloadCrc: return "bad-payload-crc";
    }
    return "invalid-argument";
}

const char* device_request_result_code_name(DeviceRequestResultCode code) {
    switch (code) {
    case DeviceRequestResultCode::Ok: return "ok";
    case DeviceRequestResultCode::Accepted: return "accepted";
    case DeviceRequestResultCode::Queued: return "queued";
    case DeviceRequestResultCode::InvalidRequest: return "invalid-request";
    case DeviceRequestResultCode::Busy: return "busy";
    case DeviceRequestResultCode::Unsupported: return "unsupported";
    case DeviceRequestResultCode::Denied: return "denied";
    case DeviceRequestResultCode::NotFound: return "not-found";
    case DeviceRequestResultCode::StaleSession: return "stale-session";
    case DeviceRequestResultCode::StaleRequest: return "stale-request";
    case DeviceRequestResultCode::PayloadTooLarge: return "payload-too-large";
    case DeviceRequestResultCode::IntegrityFailed: return "integrity-failed";
    case DeviceRequestResultCode::StorageFull: return "storage-full";
    case DeviceRequestResultCode::Cancelled: return "cancelled";
    case DeviceRequestResultCode::Failed: return "failed";
    }
    return "failed";
}

DeviceProtocolStatus encode_device_capabilities(const DeviceCapabilitySnapshot& capabilities,
                                                std::uint8_t* output,
                                                std::size_t output_capacity,
                                                std::size_t& output_size) {
    output_size = 0;
    const std::size_t board_length = bounded_c_string_length(
        capabilities.board_id, kDeviceCapabilityMaxBoardIdBytes + 1);
    const std::size_t runtime_length = bounded_c_string_length(
        capabilities.runtime_version, kDeviceCapabilityMaxRuntimeVersionBytes + 1);
    if (output == nullptr || board_length > kDeviceCapabilityMaxBoardIdBytes ||
        runtime_length > kDeviceCapabilityMaxRuntimeVersionBytes) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kCapabilityFixedBytes + board_length + runtime_length;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = 1;
    output[1] = static_cast<std::uint8_t>(board_length);
    output[2] = static_cast<std::uint8_t>(runtime_length);
    output[3] = 0;
    write_u16(output + 4, capabilities.display_width);
    write_u16(output + 6, capabilities.display_height);
    write_u32(output + 8, capabilities.capability_bits);
    write_u32(output + 12, capabilities.max_bundle_bytes);
    write_u32(output + 16, capabilities.available_storage_bytes);
    std::memcpy(output + kCapabilityFixedBytes, capabilities.board_id, board_length);
    std::memcpy(output + kCapabilityFixedBytes + board_length, capabilities.runtime_version, runtime_length);
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_capabilities(const std::uint8_t* input,
                                                std::size_t input_size,
                                                DeviceCapabilitySnapshot& capabilities) {
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kCapabilityFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != 1 || input[3] != 0) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t board_length = input[1];
    const std::size_t runtime_length = input[2];
    if (board_length > kDeviceCapabilityMaxBoardIdBytes ||
        runtime_length > kDeviceCapabilityMaxRuntimeVersionBytes ||
        input_size != kCapabilityFixedBytes + board_length + runtime_length) {
        return input_size < kCapabilityFixedBytes + board_length + runtime_length
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    capabilities = {};
    capabilities.protocol_version = kDeviceProtocolVersion;
    capabilities.display_width = read_u16(input + 4);
    capabilities.display_height = read_u16(input + 6);
    capabilities.capability_bits = read_u32(input + 8);
    capabilities.max_bundle_bytes = read_u32(input + 12);
    capabilities.available_storage_bytes = read_u32(input + 16);
    std::memcpy(capabilities.board_id, input + kCapabilityFixedBytes, board_length);
    std::memcpy(capabilities.runtime_version,
                input + kCapabilityFixedBytes + board_length,
                runtime_length);
    capabilities.board_id[board_length] = '\0';
    capabilities.runtime_version[runtime_length] = '\0';
    return DeviceProtocolStatus::Ok;
}

} // namespace jellyframe
