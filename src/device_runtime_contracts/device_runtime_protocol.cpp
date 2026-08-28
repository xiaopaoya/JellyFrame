#include "device_runtime_contracts/device_runtime_protocol.h"

#include <cstring>
#include <utility>

namespace jellyframe {
namespace {

constexpr std::uint8_t kMagic[] = {'J', 'F', 'D', 'P'};
constexpr std::size_t kCapabilityFixedBytes = 20;
constexpr std::size_t kImageIdentityFixedBytes = 16;
constexpr std::uint8_t kDevicePayloadVersion = 1;
constexpr std::size_t kInstallBeginFixedBytes = 16;
constexpr std::size_t kInstallChunkFixedBytes = 12;
constexpr std::size_t kTransactionFixedBytes = 8;
constexpr std::size_t kAppIdFixedBytes = 4;
constexpr std::size_t kLogsRequestFixedBytes = 4;
constexpr std::size_t kAppLogsFixedBytes = 8;
constexpr std::size_t kAppLogEntryFixedBytes = 16;
constexpr std::size_t kAppListFixedBytes = 8;
constexpr std::size_t kAppListEntryFixedBytes = 12;
constexpr std::size_t kRecoveryDetailFixedBytes = 16;
constexpr std::size_t kOperationResultFixedBytes = 16;
constexpr std::uint8_t kInstallBeginFlagAllowDowngrade = 1u << 0;

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

std::uint64_t read_u64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(read_u32(p)) |
           (static_cast<std::uint64_t>(read_u32(p + 4)) << 32);
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

void write_u64(std::uint8_t* p, std::uint64_t value) {
    write_u32(p, static_cast<std::uint32_t>(value));
    write_u32(p + 4, static_cast<std::uint32_t>(value >> 32));
}

std::size_t bounded_c_string_length(const char* value, std::size_t capacity) {
    std::size_t length = 0;
    while (length < capacity && value[length] != '\0') {
        ++length;
    }
    return length;
}

bool has_valid_app_id(const char* value, std::size_t& length) {
    length = bounded_c_string_length(value, kDeviceMaxAppIdBytes + 1);
    return length != 0 && length <= kDeviceMaxAppIdBytes;
}

bool has_valid_bounded_string(const char* value, std::size_t capacity, std::size_t& length) {
    length = bounded_c_string_length(value, capacity);
    return length != 0 && length < capacity;
}

bool is_device_app_library_state(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(DeviceAppLibraryState::Failed);
}

bool is_device_recovery_reason(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(DeviceRecoveryReason::LauncherFallback);
}

bool is_device_app_log_level(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(DeviceAppLogLevel::Error);
}

bool has_valid_lower_hex_revision(const char* value, std::size_t& length) {
    length = bounded_c_string_length(value, kDeviceIdentitySourceRevisionBytes + 1);
    if (length != kDeviceIdentitySourceRevisionBytes) {
        return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
        const char character = value[index];
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

bool has_valid_render_core_feature_families(std::uint32_t bits) {
    return bits != 0 && (bits & ~kDeviceRenderCoreFeatureFamilyKnownMask) == 0 &&
           (bits & (DeviceRenderCoreFeatureDocument | DeviceRenderCoreFeaturePaint)) ==
               (DeviceRenderCoreFeatureDocument | DeviceRenderCoreFeaturePaint);
}

DeviceProtocolStatus decode_app_id(const std::uint8_t* input,
                                   std::size_t input_size,
                                   std::size_t fixed_bytes,
                                   std::size_t app_id_length_offset,
                                   std::array<char, kDeviceMaxAppIdBytes + 1>& app_id,
                                   bool allow_empty) {
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < fixed_bytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion || input[3] != 0) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t app_id_length = input[app_id_length_offset];
    if (app_id_length > kDeviceMaxAppIdBytes || (!allow_empty && app_id_length == 0)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size != fixed_bytes + app_id_length) {
        return input_size < fixed_bytes + app_id_length
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    if (std::memchr(input + fixed_bytes, '\0', app_id_length) != nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    app_id = {};
    if (app_id_length != 0) {
        std::memcpy(app_id.data(), input + fixed_bytes, app_id_length);
    }
    return DeviceProtocolStatus::Ok;
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
           value <= static_cast<std::uint8_t>(DeviceMessageType::Identity);
}

bool is_device_request_result_code(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(DeviceRequestResultCode::Failed);
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
    const std::size_t expected_frame_size = kDeviceProtocolHeaderBytes + payload_length;
    if (input_size != expected_frame_size) {
        return input_size < expected_frame_size
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
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
    case DeviceMessageType::Remove: return "remove";
    case DeviceMessageType::Rollback: return "rollback";
    case DeviceMessageType::Identity: return "identity";
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
    if (std::memchr(input + kCapabilityFixedBytes, '\0', board_length) != nullptr ||
        std::memchr(input + kCapabilityFixedBytes + board_length, '\0', runtime_length) != nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
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

DeviceProtocolStatus encode_device_image_identity_payload(const DeviceImageIdentityPayload& payload,
                                                          std::uint8_t* output,
                                                          std::size_t output_capacity,
                                                          std::size_t& output_size) {
    output_size = 0;
    std::size_t image_id_length = 0;
    std::size_t profile_id_length = 0;
    std::size_t image_version_length = 0;
    std::size_t render_core_version_length = 0;
    std::size_t source_revision_length = 0;
    if (output == nullptr ||
        !has_valid_bounded_string(payload.image_id.data(), payload.image_id.size(), image_id_length) ||
        !has_valid_bounded_string(payload.profile_id.data(), payload.profile_id.size(), profile_id_length) ||
        !has_valid_bounded_string(payload.image_version.data(), payload.image_version.size(), image_version_length) ||
        !has_valid_bounded_string(payload.render_core_version.data(), payload.render_core_version.size(), render_core_version_length) ||
        !has_valid_lower_hex_revision(payload.source_revision.data(), source_revision_length) ||
        payload.render_core_abi == 0 || !has_valid_render_core_feature_families(payload.feature_family_bits)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kImageIdentityFixedBytes + image_id_length + profile_id_length +
                                     image_version_length + render_core_version_length + source_revision_length;
    if (payload_size > kDeviceProtocolMaxPayloadBytes) {
        return DeviceProtocolStatus::PayloadTooLarge;
    }
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(image_id_length);
    output[2] = static_cast<std::uint8_t>(profile_id_length);
    output[3] = static_cast<std::uint8_t>(image_version_length);
    output[4] = static_cast<std::uint8_t>(render_core_version_length);
    output[5] = static_cast<std::uint8_t>(source_revision_length);
    output[6] = 0;
    output[7] = 0;
    write_u32(output + 8, payload.render_core_abi);
    write_u32(output + 12, payload.feature_family_bits);
    std::size_t cursor = kImageIdentityFixedBytes;
    for (const auto& value : {std::pair<const char*, std::size_t>{payload.image_id.data(), image_id_length},
                              {payload.profile_id.data(), profile_id_length},
                              {payload.image_version.data(), image_version_length},
                              {payload.render_core_version.data(), render_core_version_length},
                              {payload.source_revision.data(), source_revision_length}}) {
        std::memcpy(output + cursor, value.first, value.second);
        cursor += value.second;
    }
    output_size = cursor;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_image_identity_payload(const std::uint8_t* input,
                                                          std::size_t input_size,
                                                          DeviceImageIdentityPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kImageIdentityFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion || input[6] != 0 || input[7] != 0) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t image_id_length = input[1];
    const std::size_t profile_id_length = input[2];
    const std::size_t image_version_length = input[3];
    const std::size_t render_core_version_length = input[4];
    const std::size_t source_revision_length = input[5];
    const std::size_t string_bytes = image_id_length + profile_id_length + image_version_length +
                                     render_core_version_length + source_revision_length;
    if (image_id_length == 0 || image_id_length > kDeviceIdentityMaxImageIdBytes ||
        profile_id_length == 0 || profile_id_length > kDeviceIdentityMaxProfileIdBytes ||
        image_version_length == 0 || image_version_length > kDeviceIdentityMaxImageVersionBytes ||
        render_core_version_length == 0 || render_core_version_length > kDeviceIdentityMaxRenderCoreVersionBytes ||
        source_revision_length != kDeviceIdentitySourceRevisionBytes ||
        input_size != kImageIdentityFixedBytes + string_bytes) {
        return input_size < kImageIdentityFixedBytes + string_bytes
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    const std::uint32_t render_core_abi = read_u32(input + 8);
    const std::uint32_t feature_family_bits = read_u32(input + 12);
    if (render_core_abi == 0 || !has_valid_render_core_feature_families(feature_family_bits)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    std::size_t cursor = kImageIdentityFixedBytes;
    const auto copy_string = [&](auto& destination, std::size_t length) {
        if (std::memchr(input + cursor, '\0', length) != nullptr) {
            return false;
        }
        std::memcpy(destination.data(), input + cursor, length);
        cursor += length;
        return true;
    };
    if (!copy_string(payload.image_id, image_id_length) ||
        !copy_string(payload.profile_id, profile_id_length) ||
        !copy_string(payload.image_version, image_version_length) ||
        !copy_string(payload.render_core_version, render_core_version_length) ||
        !copy_string(payload.source_revision, source_revision_length)) {
        payload = {};
        return DeviceProtocolStatus::InvalidArgument;
    }
    std::size_t checked_revision_length = 0;
    if (!has_valid_lower_hex_revision(payload.source_revision.data(), checked_revision_length)) {
        payload = {};
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.render_core_abi = render_core_abi;
    payload.feature_family_bits = feature_family_bits;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_install_begin_payload(const DeviceInstallBeginPayload& payload,
                                                         std::uint8_t* output,
                                                         std::size_t output_capacity,
                                                         std::size_t& output_size) {
    output_size = 0;
    std::size_t app_id_length = 0;
    if (output == nullptr || payload.transaction_id == 0 || payload.bundle_bytes == 0 ||
        !has_valid_app_id(payload.app_id.data(), app_id_length)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kInstallBeginFixedBytes + app_id_length;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = payload.allow_downgrade ? kInstallBeginFlagAllowDowngrade : 0;
    output[2] = static_cast<std::uint8_t>(app_id_length);
    output[3] = 0;
    write_u32(output + 4, payload.transaction_id);
    write_u32(output + 8, payload.bundle_bytes);
    write_u32(output + 12, payload.bundle_crc32);
    std::memcpy(output + kInstallBeginFixedBytes, payload.app_id.data(), app_id_length);
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_install_begin_payload(const std::uint8_t* input,
                                                         std::size_t input_size,
                                                         DeviceInstallBeginPayload& payload) {
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kInstallBeginFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion || input[3] != 0) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    if ((input[1] & ~kInstallBeginFlagAllowDowngrade) != 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t app_id_length = input[2];
    if (app_id_length == 0 || app_id_length > kDeviceMaxAppIdBytes ||
        input_size != kInstallBeginFixedBytes + app_id_length) {
        return input_size < kInstallBeginFixedBytes + app_id_length
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    if (std::memchr(input + kInstallBeginFixedBytes, '\0', app_id_length) != nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::uint32_t transaction_id = read_u32(input + 4);
    if (transaction_id == 0 || read_u32(input + 8) == 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload = {};
    payload.transaction_id = transaction_id;
    payload.bundle_bytes = read_u32(input + 8);
    payload.bundle_crc32 = read_u32(input + 12);
    payload.allow_downgrade = (input[1] & kInstallBeginFlagAllowDowngrade) != 0;
    std::memcpy(payload.app_id.data(), input + kInstallBeginFixedBytes, app_id_length);
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_install_chunk_payload(std::uint32_t transaction_id,
                                                         std::uint32_t offset,
                                                         const std::uint8_t* bytes,
                                                         std::size_t byte_count,
                                                         std::uint8_t* output,
                                                         std::size_t output_capacity,
                                                         std::size_t& output_size) {
    output_size = 0;
    if (output == nullptr || transaction_id == 0 || byte_count == 0 || bytes == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (byte_count > kDeviceProtocolMaxPayloadBytes - kInstallChunkFixedBytes || byte_count > 0xffffu) {
        return DeviceProtocolStatus::PayloadTooLarge;
    }
    const std::size_t payload_size = kInstallChunkFixedBytes + byte_count;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = 0;
    write_u16(output + 2, static_cast<std::uint16_t>(byte_count));
    write_u32(output + 4, transaction_id);
    write_u32(output + 8, offset);
    std::memcpy(output + kInstallChunkFixedBytes, bytes, byte_count);
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_install_chunk_payload(const std::uint8_t* input,
                                                         std::size_t input_size,
                                                         DeviceInstallChunkView& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kInstallChunkFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion || input[1] != 0) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t byte_count = read_u16(input + 2);
    if (byte_count == 0 || input_size != kInstallChunkFixedBytes + byte_count) {
        return input_size < kInstallChunkFixedBytes + byte_count
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    const std::uint32_t transaction_id = read_u32(input + 4);
    if (transaction_id == 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.transaction_id = transaction_id;
    payload.offset = read_u32(input + 8);
    payload.bytes = input + kInstallChunkFixedBytes;
    payload.byte_count = byte_count;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_transaction_payload(const DeviceTransactionPayload& payload,
                                                       std::uint8_t* output,
                                                       std::size_t output_capacity,
                                                       std::size_t& output_size) {
    output_size = 0;
    if (output == nullptr || payload.transaction_id == 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (output_capacity < kTransactionFixedBytes) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = 0;
    output[2] = 0;
    output[3] = 0;
    write_u32(output + 4, payload.transaction_id);
    output_size = kTransactionFixedBytes;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_transaction_payload(const std::uint8_t* input,
                                                       std::size_t input_size,
                                                       DeviceTransactionPayload& payload) {
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kTransactionFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input_size != kTransactionFixedBytes || input[0] != kDevicePayloadVersion ||
        input[1] != 0 || input[2] != 0 || input[3] != 0) {
        return input_size < kTransactionFixedBytes
                   ? DeviceProtocolStatus::Truncated
                   : (input[0] != kDevicePayloadVersion ? DeviceProtocolStatus::UnsupportedVersion
                                                         : DeviceProtocolStatus::InvalidArgument);
    }
    const std::uint32_t transaction_id = read_u32(input + 4);
    if (transaction_id == 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.transaction_id = transaction_id;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_app_id_payload(const DeviceAppIdPayload& payload,
                                                  std::uint8_t* output,
                                                  std::size_t output_capacity,
                                                  std::size_t& output_size) {
    output_size = 0;
    std::size_t app_id_length = 0;
    if (output == nullptr || !has_valid_app_id(payload.app_id.data(), app_id_length)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kAppIdFixedBytes + app_id_length;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(app_id_length);
    output[2] = 0;
    output[3] = 0;
    std::memcpy(output + kAppIdFixedBytes, payload.app_id.data(), app_id_length);
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_app_id_payload(const std::uint8_t* input,
                                                  std::size_t input_size,
                                                  DeviceAppIdPayload& payload) {
    payload = {};
    return decode_app_id(input, input_size, kAppIdFixedBytes, 1, payload.app_id, false);
}

DeviceProtocolStatus encode_device_logs_request_payload(const DeviceLogsRequestPayload& payload,
                                                        std::uint8_t* output,
                                                        std::size_t output_capacity,
                                                        std::size_t& output_size) {
    output_size = 0;
    const std::size_t app_id_length = bounded_c_string_length(
        payload.app_id.data(), kDeviceMaxAppIdBytes + 1);
    if (output == nullptr || app_id_length > kDeviceMaxAppIdBytes || payload.limit == 0 ||
        payload.limit > kDeviceAppLogMaxEntries) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kLogsRequestFixedBytes + app_id_length;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(app_id_length);
    write_u16(output + 2, payload.limit);
    if (app_id_length != 0) {
        std::memcpy(output + kLogsRequestFixedBytes, payload.app_id.data(), app_id_length);
    }
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_logs_request_payload(const std::uint8_t* input,
                                                        std::size_t input_size,
                                                        DeviceLogsRequestPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kLogsRequestFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t app_id_length = input[1];
    if (app_id_length > kDeviceMaxAppIdBytes || input_size != kLogsRequestFixedBytes + app_id_length) {
        return input_size < kLogsRequestFixedBytes + app_id_length
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    if (std::memchr(input + kLogsRequestFixedBytes, '\0', app_id_length) != nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::uint16_t limit = read_u16(input + 2);
    if (limit == 0 || limit > kDeviceAppLogMaxEntries) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.limit = limit;
    if (app_id_length != 0) {
        std::memcpy(payload.app_id.data(), input + kLogsRequestFixedBytes, app_id_length);
    }
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_app_logs_payload(const DeviceAppLogsPayload& payload,
                                                    std::uint8_t* output,
                                                    std::size_t output_capacity,
                                                    std::size_t& output_size) {
    output_size = 0;
    if (output == nullptr || payload.entry_count > kDeviceAppLogMaxEntries || payload.entry_count > 0xffu) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    std::array<std::size_t, kDeviceAppLogMaxEntries> app_id_lengths{};
    std::array<std::size_t, kDeviceAppLogMaxEntries> message_lengths{};
    std::size_t payload_size = kAppLogsFixedBytes;
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        const DeviceAppLogEntry& entry = payload.entries[index];
        if (!has_valid_app_id(entry.app_id.data(), app_id_lengths[index]) ||
            !has_valid_bounded_string(entry.message.data(), entry.message.size(), message_lengths[index]) ||
            !is_device_app_log_level(static_cast<std::uint8_t>(entry.level))) {
            return DeviceProtocolStatus::InvalidArgument;
        }
        payload_size += kAppLogEntryFixedBytes + app_id_lengths[index] + message_lengths[index];
        if (payload_size > kDeviceProtocolMaxPayloadBytes) {
            return DeviceProtocolStatus::PayloadTooLarge;
        }
    }
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(payload.entry_count);
    output[2] = 0;
    output[3] = 0;
    write_u32(output + 4, payload.dropped_records);
    std::size_t cursor = kAppLogsFixedBytes;
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        const DeviceAppLogEntry& entry = payload.entries[index];
        output[cursor] = static_cast<std::uint8_t>(app_id_lengths[index]);
        output[cursor + 1] = static_cast<std::uint8_t>(message_lengths[index]);
        output[cursor + 2] = static_cast<std::uint8_t>(entry.level);
        output[cursor + 3] = 0;
        write_u32(output + cursor + 4, entry.generation);
        write_u64(output + cursor + 8, entry.timestamp_ms);
        cursor += kAppLogEntryFixedBytes;
        std::memcpy(output + cursor, entry.app_id.data(), app_id_lengths[index]);
        cursor += app_id_lengths[index];
        std::memcpy(output + cursor, entry.message.data(), message_lengths[index]);
        cursor += message_lengths[index];
    }
    output_size = cursor;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_app_logs_payload(const std::uint8_t* input,
                                                    std::size_t input_size,
                                                    DeviceAppLogsPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kAppLogsFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    if (input[2] != 0 || input[3] != 0 || input[1] > kDeviceAppLogMaxEntries) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.dropped_records = read_u32(input + 4);
    payload.entry_count = input[1];
    std::size_t cursor = kAppLogsFixedBytes;
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        if (input_size - cursor < kAppLogEntryFixedBytes) {
            payload = {};
            return DeviceProtocolStatus::Truncated;
        }
        const std::size_t app_id_length = input[cursor];
        const std::size_t message_length = input[cursor + 1];
        const std::uint8_t level = input[cursor + 2];
        if (app_id_length == 0 || app_id_length > kDeviceMaxAppIdBytes ||
            message_length == 0 || message_length > kDeviceAppLogMaxMessageBytes ||
            !is_device_app_log_level(level) || input[cursor + 3] != 0) {
            payload = {};
            return DeviceProtocolStatus::InvalidArgument;
        }
        const std::uint32_t generation = read_u32(input + cursor + 4);
        const std::uint64_t timestamp_ms = read_u64(input + cursor + 8);
        cursor += kAppLogEntryFixedBytes;
        const std::size_t string_bytes = app_id_length + message_length;
        if (input_size - cursor < string_bytes) {
            payload = {};
            return DeviceProtocolStatus::Truncated;
        }
        if (std::memchr(input + cursor, '\0', app_id_length) != nullptr ||
            std::memchr(input + cursor + app_id_length, '\0', message_length) != nullptr) {
            payload = {};
            return DeviceProtocolStatus::InvalidArgument;
        }
        DeviceAppLogEntry& entry = payload.entries[index];
        std::memcpy(entry.app_id.data(), input + cursor, app_id_length);
        cursor += app_id_length;
        std::memcpy(entry.message.data(), input + cursor, message_length);
        cursor += message_length;
        entry.generation = generation;
        entry.timestamp_ms = timestamp_ms;
        entry.level = static_cast<DeviceAppLogLevel>(level);
    }
    if (cursor != input_size) {
        payload = {};
        return DeviceProtocolStatus::InvalidArgument;
    }
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_app_list_payload(const DeviceAppListPayload& payload,
                                                    std::uint8_t* output,
                                                    std::size_t output_capacity,
                                                    std::size_t& output_size) {
    output_size = 0;
    if (output == nullptr || payload.entry_count > kDeviceAppListMaxEntries || payload.entry_count > 0xffu) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    std::size_t payload_size = kAppListFixedBytes;
    std::array<std::size_t, kDeviceAppListMaxEntries> app_id_lengths{};
    std::array<std::size_t, kDeviceAppListMaxEntries> version_lengths{};
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        const DeviceAppLibraryEntry& entry = payload.entries[index];
        if (!has_valid_app_id(entry.app_id.data(), app_id_lengths[index]) ||
            !has_valid_bounded_string(entry.version_name.data(), entry.version_name.size(), version_lengths[index]) ||
            entry.bundle_bytes == 0 ||
            !is_device_app_library_state(static_cast<std::uint8_t>(entry.state)) ||
            (entry.flags & ~DeviceAppLibraryEntryRollbackAvailable) != 0) {
            return DeviceProtocolStatus::InvalidArgument;
        }
        payload_size += kAppListEntryFixedBytes + app_id_lengths[index] + version_lengths[index];
        if (payload_size > kDeviceProtocolMaxPayloadBytes) {
            return DeviceProtocolStatus::PayloadTooLarge;
        }
    }
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(payload.entry_count);
    output[2] = 0;
    output[3] = 0;
    write_u32(output + 4, payload.registry_generation);
    std::size_t cursor = kAppListFixedBytes;
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        const DeviceAppLibraryEntry& entry = payload.entries[index];
        output[cursor] = static_cast<std::uint8_t>(app_id_lengths[index]);
        output[cursor + 1] = static_cast<std::uint8_t>(version_lengths[index]);
        output[cursor + 2] = static_cast<std::uint8_t>(entry.state);
        output[cursor + 3] = entry.flags;
        write_u32(output + cursor + 4, entry.version_code);
        write_u32(output + cursor + 8, entry.bundle_bytes);
        cursor += kAppListEntryFixedBytes;
        std::memcpy(output + cursor, entry.app_id.data(), app_id_lengths[index]);
        cursor += app_id_lengths[index];
        std::memcpy(output + cursor, entry.version_name.data(), version_lengths[index]);
        cursor += version_lengths[index];
    }
    output_size = cursor;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_app_list_payload(const std::uint8_t* input,
                                                    std::size_t input_size,
                                                    DeviceAppListPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kAppListFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    if (input[2] != 0 || input[3] != 0 || input[1] > kDeviceAppListMaxEntries) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.registry_generation = read_u32(input + 4);
    payload.entry_count = input[1];
    std::size_t cursor = kAppListFixedBytes;
    for (std::size_t index = 0; index < payload.entry_count; ++index) {
        if (input_size - cursor < kAppListEntryFixedBytes) {
            return DeviceProtocolStatus::Truncated;
        }
        const std::size_t app_id_length = input[cursor];
        const std::size_t version_length = input[cursor + 1];
        const std::uint8_t state = input[cursor + 2];
        const std::uint8_t flags = input[cursor + 3];
        const std::uint32_t version_code = read_u32(input + cursor + 4);
        const std::uint32_t bundle_bytes = read_u32(input + cursor + 8);
        if (app_id_length == 0 || app_id_length > kDeviceMaxAppIdBytes || version_length == 0 ||
            version_length > kDeviceMaxVersionNameBytes || bundle_bytes == 0 || !is_device_app_library_state(state) ||
            (flags & ~DeviceAppLibraryEntryRollbackAvailable) != 0) {
            return DeviceProtocolStatus::InvalidArgument;
        }
        cursor += kAppListEntryFixedBytes;
        const std::size_t string_bytes = app_id_length + version_length;
        if (input_size - cursor < string_bytes) {
            return DeviceProtocolStatus::Truncated;
        }
        if (std::memchr(input + cursor, '\0', app_id_length) != nullptr ||
            std::memchr(input + cursor + app_id_length, '\0', version_length) != nullptr) {
            return DeviceProtocolStatus::InvalidArgument;
        }
        DeviceAppLibraryEntry& entry = payload.entries[index];
        std::memcpy(entry.app_id.data(), input + cursor, app_id_length);
        cursor += app_id_length;
        std::memcpy(entry.version_name.data(), input + cursor, version_length);
        cursor += version_length;
        entry.version_code = version_code;
        entry.bundle_bytes = bundle_bytes;
        entry.state = static_cast<DeviceAppLibraryState>(state);
        entry.flags = flags;
    }
    return cursor == input_size ? DeviceProtocolStatus::Ok : DeviceProtocolStatus::InvalidArgument;
}

DeviceProtocolStatus encode_device_recovery_detail_payload(const DeviceRecoveryDetailPayload& payload,
                                                           std::uint8_t* output,
                                                           std::size_t output_capacity,
                                                           std::size_t& output_size) {
    output_size = 0;
    std::size_t app_id_length = 0;
    const bool empty_recovery = payload.reason == DeviceRecoveryReason::None;
    app_id_length = bounded_c_string_length(payload.app_id.data(), payload.app_id.size());
    if (output == nullptr ||
        (empty_recovery
             ? (bounded_c_string_length(payload.app_id.data(), payload.app_id.size()) != 0 || payload.recovery_sequence != 0 ||
                payload.flags != 0)
             // A damaged registry or a discarded staging transaction has no
             // trustworthy app identity to report. Such global recovery
             // events are still meaningful typed diagnostics.
             : (app_id_length > kDeviceMaxAppIdBytes ||
                (app_id_length != 0 && !has_valid_app_id(payload.app_id.data(), app_id_length)))) ||
        !is_device_recovery_reason(static_cast<std::uint8_t>(payload.reason)) ||
        (payload.flags & ~(DeviceRecoveryLauncherActive | DeviceRecoveryAppDisabled | DeviceRecoveryRollbackAvailable)) != 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    const std::size_t payload_size = kRecoveryDetailFixedBytes + app_id_length;
    if (output_capacity < payload_size) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(payload.reason);
    write_u16(output + 2, payload.flags);
    write_u32(output + 4, payload.registry_generation);
    write_u32(output + 8, payload.recovery_sequence);
    output[12] = static_cast<std::uint8_t>(app_id_length);
    output[13] = 0;
    output[14] = 0;
    output[15] = 0;
    std::memcpy(output + kRecoveryDetailFixedBytes, payload.app_id.data(), app_id_length);
    output_size = payload_size;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_recovery_detail_payload(const std::uint8_t* input,
                                                           std::size_t input_size,
                                                           DeviceRecoveryDetailPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kRecoveryDetailFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input[0] != kDevicePayloadVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::size_t app_id_length = input[12];
    const std::uint16_t flags = read_u16(input + 2);
    const bool empty_recovery = input[1] == static_cast<std::uint8_t>(DeviceRecoveryReason::None);
    if (!is_device_recovery_reason(input[1]) ||
        (empty_recovery ? (app_id_length != 0 || read_u32(input + 8) != 0 || flags != 0)
                        : app_id_length > kDeviceMaxAppIdBytes) ||
        input[13] != 0 || input[14] != 0 || input[15] != 0 ||
        (flags & ~(DeviceRecoveryLauncherActive | DeviceRecoveryAppDisabled | DeviceRecoveryRollbackAvailable)) != 0) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size != kRecoveryDetailFixedBytes + app_id_length) {
        return input_size < kRecoveryDetailFixedBytes + app_id_length
                   ? DeviceProtocolStatus::Truncated
                   : DeviceProtocolStatus::InvalidArgument;
    }
    if (std::memchr(input + kRecoveryDetailFixedBytes, '\0', app_id_length) != nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    std::memcpy(payload.app_id.data(), input + kRecoveryDetailFixedBytes, app_id_length);
    payload.reason = static_cast<DeviceRecoveryReason>(input[1]);
    payload.flags = flags;
    payload.registry_generation = read_u32(input + 4);
    payload.recovery_sequence = read_u32(input + 8);
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus encode_device_operation_result_payload(const DeviceOperationResultPayload& payload,
                                                            std::uint8_t* output,
                                                            std::size_t output_capacity,
                                                            std::size_t& output_size) {
    output_size = 0;
    if (output == nullptr || !is_device_request_result_code(static_cast<std::uint8_t>(payload.result_code)) ||
        (payload.flags & ~(DeviceOperationResultComplete | DeviceOperationResultActive |
                           DeviceOperationResultLauncherActive)) != 0 ||
        payload.received_bytes > payload.expected_bytes) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (output_capacity < kOperationResultFixedBytes) {
        return DeviceProtocolStatus::BufferTooSmall;
    }
    output[0] = kDevicePayloadVersion;
    output[1] = static_cast<std::uint8_t>(payload.result_code);
    write_u16(output + 2, payload.flags);
    write_u32(output + 4, payload.transaction_id);
    write_u32(output + 8, payload.received_bytes);
    write_u32(output + 12, payload.expected_bytes);
    output_size = kOperationResultFixedBytes;
    return DeviceProtocolStatus::Ok;
}

DeviceProtocolStatus decode_device_operation_result_payload(const std::uint8_t* input,
                                                            std::size_t input_size,
                                                            DeviceOperationResultPayload& payload) {
    payload = {};
    if (input == nullptr) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input_size < kOperationResultFixedBytes) {
        return DeviceProtocolStatus::Truncated;
    }
    if (input_size != kOperationResultFixedBytes) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    if (input[0] != kDevicePayloadVersion) {
        return DeviceProtocolStatus::UnsupportedVersion;
    }
    const std::uint16_t flags = read_u16(input + 2);
    if (!is_device_request_result_code(input[1]) ||
        (flags & ~(DeviceOperationResultComplete | DeviceOperationResultActive |
                   DeviceOperationResultLauncherActive)) != 0 ||
        read_u32(input + 8) > read_u32(input + 12)) {
        return DeviceProtocolStatus::InvalidArgument;
    }
    payload.result_code = static_cast<DeviceRequestResultCode>(input[1]);
    payload.flags = flags;
    payload.transaction_id = read_u32(input + 4);
    payload.received_bytes = read_u32(input + 8);
    payload.expected_bytes = read_u32(input + 12);
    return DeviceProtocolStatus::Ok;
}

const char* device_app_library_state_name(DeviceAppLibraryState state) {
    switch (state) {
    case DeviceAppLibraryState::Installed: return "installed";
    case DeviceAppLibraryState::Disabled: return "disabled";
    case DeviceAppLibraryState::Failed: return "failed";
    }
    return "installed";
}

const char* device_app_log_level_name(DeviceAppLogLevel level) {
    switch (level) {
    case DeviceAppLogLevel::Debug: return "debug";
    case DeviceAppLogLevel::Info: return "info";
    case DeviceAppLogLevel::Warning: return "warn";
    case DeviceAppLogLevel::Error: return "error";
    }
    return "info";
}

const char* device_recovery_reason_name(DeviceRecoveryReason reason) {
    switch (reason) {
    case DeviceRecoveryReason::None: return "none";
    case DeviceRecoveryReason::RegistryInvalid: return "registry-invalid";
    case DeviceRecoveryReason::StagingDiscarded: return "staging-discarded";
    case DeviceRecoveryReason::AppLoadFailure: return "app-load-failure";
    case DeviceRecoveryReason::AppRuntimeFailure: return "app-runtime-failure";
    case DeviceRecoveryReason::AppBudgetExceeded: return "app-budget-exceeded";
    case DeviceRecoveryReason::LauncherFallback: return "launcher-fallback";
    }
    return "none";
}

} // namespace jellyframe
