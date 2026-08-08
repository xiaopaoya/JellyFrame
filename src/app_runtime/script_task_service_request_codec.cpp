#include "app_runtime/script_task_service_request_codec.h"

namespace jellyframe {
namespace {

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kPacketBytes = 20;
constexpr std::size_t kCancelPacketBytes = 12;

void put_u32(std::vector<std::uint8_t>& output, std::size_t offset, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint32_t get_u32(const std::vector<std::uint8_t>& input, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

bool known_kind(std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(HostServiceJobKind::Other);
}

bool valid(const ScriptTaskServiceRequest& request) {
    return known_kind(static_cast<std::uint8_t>(request.kind)) && request.request_id != 0 &&
           request.client_token != 0;
}

} // namespace

ScriptTaskServiceRequestCodecStatus encode_script_task_service_request(
    const ScriptTaskServiceRequest& request,
    const ScriptTaskServiceRequestCodecOptions& options,
    std::vector<std::uint8_t>& output) {
    if (!valid(request)) {
        return ScriptTaskServiceRequestCodecStatus::InvalidValue;
    }
    if (kPacketBytes > options.max_payload_bytes) {
        return ScriptTaskServiceRequestCodecStatus::PayloadTooLarge;
    }

    output.assign(kPacketBytes, 0);
    output[0] = kVersion;
    output[1] = static_cast<std::uint8_t>(request.kind);
    output[2] = request.priority;
    put_u32(output, 4, request.request_id);
    put_u32(output, 8, request.client_token);
    put_u32(output, 12, request.request_handle);
    put_u32(output, 16, request.timeout_ms);
    return ScriptTaskServiceRequestCodecStatus::Accepted;
}

ScriptTaskServiceRequestCodecStatus decode_script_task_service_request(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskServiceRequestCodecOptions& options,
    ScriptTaskServiceRequest& output) {
    if (input.size() > options.max_payload_bytes) {
        return ScriptTaskServiceRequestCodecStatus::PayloadTooLarge;
    }
    if (input.size() != kPacketBytes || input[0] != kVersion || input[3] != 0 || !known_kind(input[1])) {
        return ScriptTaskServiceRequestCodecStatus::Malformed;
    }

    const ScriptTaskServiceRequest decoded{
        static_cast<HostServiceJobKind>(input[1]),
        get_u32(input, 4),
        get_u32(input, 8),
        get_u32(input, 12),
        input[2],
        get_u32(input, 16),
    };
    if (!valid(decoded)) {
        return ScriptTaskServiceRequestCodecStatus::Malformed;
    }
    output = decoded;
    return ScriptTaskServiceRequestCodecStatus::Accepted;
}

ScriptTaskServiceRequestPostResult post_script_task_service_request(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    std::uint32_t sequence,
    const ScriptTaskServiceRequest& request,
    const ScriptTaskServiceRequestCodecOptions& options) {
    ScriptTaskServiceRequestPostResult result;
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::ServiceRequest;
    packet.session = session;
    packet.sequence = sequence;
    result.codec_status = encode_script_task_service_request(request, options, packet.payload);
    if (result.codec_status == ScriptTaskServiceRequestCodecStatus::Accepted) {
        result.mailbox_status = supervisor.post_service_request(packet);
    }
    return result;
}

ScriptTaskServiceRequestCodecStatus encode_script_task_service_cancel(
    const ScriptTaskServiceCancel& cancel,
    const ScriptTaskServiceRequestCodecOptions& options,
    std::vector<std::uint8_t>& output) {
    if (!cancel.valid()) {
        return ScriptTaskServiceRequestCodecStatus::InvalidValue;
    }
    if (kCancelPacketBytes > options.max_payload_bytes) {
        return ScriptTaskServiceRequestCodecStatus::PayloadTooLarge;
    }
    output.assign(kCancelPacketBytes, 0);
    output[0] = kVersion;
    output[1] = 1;
    put_u32(output, 4, cancel.request_id);
    put_u32(output, 8, cancel.client_token);
    return ScriptTaskServiceRequestCodecStatus::Accepted;
}

ScriptTaskServiceRequestCodecStatus decode_script_task_service_cancel(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskServiceRequestCodecOptions& options,
    ScriptTaskServiceCancel& output) {
    if (input.size() > options.max_payload_bytes) {
        return ScriptTaskServiceRequestCodecStatus::PayloadTooLarge;
    }
    if (input.size() != kCancelPacketBytes || input[0] != kVersion || input[1] != 1 ||
        input[2] != 0 || input[3] != 0) {
        return ScriptTaskServiceRequestCodecStatus::Malformed;
    }
    const ScriptTaskServiceCancel decoded{get_u32(input, 4), get_u32(input, 8)};
    if (!decoded.valid()) {
        return ScriptTaskServiceRequestCodecStatus::Malformed;
    }
    output = decoded;
    return ScriptTaskServiceRequestCodecStatus::Accepted;
}

ScriptTaskServiceRequestPostResult post_script_task_service_cancel(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    std::uint32_t sequence,
    const ScriptTaskServiceCancel& cancel,
    const ScriptTaskServiceRequestCodecOptions& options) {
    ScriptTaskServiceRequestPostResult result;
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::ServiceCancel;
    packet.session = session;
    packet.sequence = sequence;
    result.codec_status = encode_script_task_service_cancel(cancel, options, packet.payload);
    if (result.codec_status == ScriptTaskServiceRequestCodecStatus::Accepted) {
        result.mailbox_status = supervisor.post_service_request(packet);
    }
    return result;
}

} // namespace jellyframe
