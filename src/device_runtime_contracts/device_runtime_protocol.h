#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace jellyframe {

constexpr std::uint8_t kDeviceProtocolVersion = 1;
constexpr std::size_t kDeviceProtocolHeaderBytes = 24;
constexpr std::size_t kDeviceProtocolMaxPayloadBytes = 4096;
constexpr std::size_t kDeviceCapabilityMaxBoardIdBytes = 63;
constexpr std::size_t kDeviceCapabilityMaxRuntimeVersionBytes = 31;
constexpr std::size_t kDeviceMaxAppIdBytes = 95;
constexpr std::size_t kDeviceMaxVersionNameBytes = 63;
constexpr std::size_t kDeviceAppListMaxEntries = 24;
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
    Remove = 11,
    Rollback = 12,
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

// All request and result payloads below start with payload version 1. Decoded
// fixed fields are copied values. DeviceInstallChunkView aliases its input and
// must be copied before it crosses a task or asynchronous queue boundary.
struct DeviceInstallBeginPayload {
    std::uint32_t transaction_id = 0;
    std::uint32_t bundle_bytes = 0;
    std::uint32_t bundle_crc32 = 0;
    bool allow_downgrade = false;
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }
};

struct DeviceInstallChunkView {
    std::uint32_t transaction_id = 0;
    std::uint32_t offset = 0;
    const std::uint8_t* bytes = nullptr;
    std::size_t byte_count = 0;
};

struct DeviceTransactionPayload {
    std::uint32_t transaction_id = 0;
};

struct DeviceAppIdPayload {
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }
};

struct DeviceLogsRequestPayload {
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
    std::uint16_t limit = 0;

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }
};

enum class DeviceAppLibraryState : std::uint8_t {
    Installed = 0,
    Disabled = 1,
    Failed = 2,
};

enum DeviceAppLibraryEntryFlags : std::uint8_t {
    DeviceAppLibraryEntryRollbackAvailable = 1u << 0,
};

struct DeviceAppLibraryEntry {
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
    std::array<char, kDeviceMaxVersionNameBytes + 1> version_name{};
    std::uint32_t version_code = 0;
    std::uint32_t bundle_bytes = 0;
    DeviceAppLibraryState state = DeviceAppLibraryState::Installed;
    std::uint8_t flags = 0;

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }

    std::string_view version_name_view() const {
        return std::string_view(version_name.data());
    }
};

struct DeviceAppListPayload {
    std::uint32_t registry_generation = 0;
    std::array<DeviceAppLibraryEntry, kDeviceAppListMaxEntries> entries{};
    std::size_t entry_count = 0;
};

enum class DeviceRecoveryReason : std::uint8_t {
    None = 0,
    RegistryInvalid = 1,
    StagingDiscarded = 2,
    AppLoadFailure = 3,
    AppRuntimeFailure = 4,
    AppBudgetExceeded = 5,
    LauncherFallback = 6,
};

enum DeviceRecoveryFlags : std::uint16_t {
    DeviceRecoveryLauncherActive = 1u << 0,
    DeviceRecoveryAppDisabled = 1u << 1,
    DeviceRecoveryRollbackAvailable = 1u << 2,
};

struct DeviceRecoveryDetailPayload {
    std::array<char, kDeviceMaxAppIdBytes + 1> app_id{};
    std::uint32_t registry_generation = 0;
    std::uint32_t recovery_sequence = 0;
    DeviceRecoveryReason reason = DeviceRecoveryReason::None;
    std::uint16_t flags = 0;

    std::string_view app_id_view() const {
        return std::string_view(app_id.data());
    }
};

enum DeviceOperationResultFlags : std::uint16_t {
    DeviceOperationResultComplete = 1u << 0,
    DeviceOperationResultActive = 1u << 1,
    DeviceOperationResultLauncherActive = 1u << 2,
};

struct DeviceOperationResultPayload {
    DeviceRequestResultCode result_code = DeviceRequestResultCode::Failed;
    std::uint16_t flags = 0;
    std::uint32_t transaction_id = 0;
    std::uint32_t received_bytes = 0;
    std::uint32_t expected_bytes = 0;
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
bool is_device_request_result_code(std::uint8_t value);
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

DeviceProtocolStatus encode_device_install_begin_payload(const DeviceInstallBeginPayload& payload,
                                                         std::uint8_t* output,
                                                         std::size_t output_capacity,
                                                         std::size_t& output_size);
DeviceProtocolStatus decode_device_install_begin_payload(const std::uint8_t* input,
                                                         std::size_t input_size,
                                                         DeviceInstallBeginPayload& payload);

DeviceProtocolStatus encode_device_install_chunk_payload(std::uint32_t transaction_id,
                                                         std::uint32_t offset,
                                                         const std::uint8_t* bytes,
                                                         std::size_t byte_count,
                                                         std::uint8_t* output,
                                                         std::size_t output_capacity,
                                                         std::size_t& output_size);
DeviceProtocolStatus decode_device_install_chunk_payload(const std::uint8_t* input,
                                                         std::size_t input_size,
                                                         DeviceInstallChunkView& payload);

DeviceProtocolStatus encode_device_transaction_payload(const DeviceTransactionPayload& payload,
                                                       std::uint8_t* output,
                                                       std::size_t output_capacity,
                                                       std::size_t& output_size);
DeviceProtocolStatus decode_device_transaction_payload(const std::uint8_t* input,
                                                       std::size_t input_size,
                                                       DeviceTransactionPayload& payload);

DeviceProtocolStatus encode_device_app_id_payload(const DeviceAppIdPayload& payload,
                                                  std::uint8_t* output,
                                                  std::size_t output_capacity,
                                                  std::size_t& output_size);
DeviceProtocolStatus decode_device_app_id_payload(const std::uint8_t* input,
                                                  std::size_t input_size,
                                                  DeviceAppIdPayload& payload);

DeviceProtocolStatus encode_device_logs_request_payload(const DeviceLogsRequestPayload& payload,
                                                        std::uint8_t* output,
                                                        std::size_t output_capacity,
                                                        std::size_t& output_size);
DeviceProtocolStatus decode_device_logs_request_payload(const std::uint8_t* input,
                                                        std::size_t input_size,
                                                        DeviceLogsRequestPayload& payload);

DeviceProtocolStatus encode_device_app_list_payload(const DeviceAppListPayload& payload,
                                                    std::uint8_t* output,
                                                    std::size_t output_capacity,
                                                    std::size_t& output_size);
DeviceProtocolStatus decode_device_app_list_payload(const std::uint8_t* input,
                                                    std::size_t input_size,
                                                    DeviceAppListPayload& payload);

DeviceProtocolStatus encode_device_recovery_detail_payload(const DeviceRecoveryDetailPayload& payload,
                                                           std::uint8_t* output,
                                                           std::size_t output_capacity,
                                                           std::size_t& output_size);
DeviceProtocolStatus decode_device_recovery_detail_payload(const std::uint8_t* input,
                                                           std::size_t input_size,
                                                           DeviceRecoveryDetailPayload& payload);

DeviceProtocolStatus encode_device_operation_result_payload(const DeviceOperationResultPayload& payload,
                                                            std::uint8_t* output,
                                                            std::size_t output_capacity,
                                                            std::size_t& output_size);
DeviceProtocolStatus decode_device_operation_result_payload(const std::uint8_t* input,
                                                            std::size_t input_size,
                                                            DeviceOperationResultPayload& payload);

const char* device_app_library_state_name(DeviceAppLibraryState state);
const char* device_recovery_reason_name(DeviceRecoveryReason reason);

} // namespace jellyframe
