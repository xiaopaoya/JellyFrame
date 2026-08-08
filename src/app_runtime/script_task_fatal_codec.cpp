#include "app_runtime/script_task_fatal_codec.h"

namespace jellyframe {
namespace {

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kPacketBytes = 40;

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

void put_u64(std::vector<std::uint8_t>& output, std::size_t offset, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

std::uint64_t get_u64(const std::vector<std::uint8_t>& input, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8U);
    }
    return value;
}

} // namespace

ScriptTaskFatalCodecStatus encode_script_task_fatal(
    const ScriptTaskFatalRecord& record,
    const ScriptTaskFatalCodecOptions& options,
    std::vector<std::uint8_t>& output) {
    if (!record.valid()) {
        return ScriptTaskFatalCodecStatus::InvalidValue;
    }
    if (kPacketBytes > options.max_payload_bytes) {
        return ScriptTaskFatalCodecStatus::PayloadTooLarge;
    }
    output.assign(kPacketBytes, 0);
    output[0] = kVersion;
    output[1] = record.reason;
    put_u32(output, 4, record.session.app_instance_id);
    put_u32(output, 8, record.session.generation);
    put_u32(output, 12, record.session.worker_epoch);
    put_u32(output, 16, record.diagnostic_code);
    put_u32(output, 20, record.last_input_sequence);
    put_u32(output, 24, record.last_frame_sequence);
    put_u64(output, 28, record.internal_bytes);
    put_u32(output, 36, record.message_bytes);
    return ScriptTaskFatalCodecStatus::Accepted;
}

ScriptTaskFatalCodecStatus decode_script_task_fatal(
    const std::vector<std::uint8_t>& input,
    const ScriptTaskFatalCodecOptions& options,
    ScriptTaskFatalRecord& output) {
    if (input.size() > options.max_payload_bytes) {
        return ScriptTaskFatalCodecStatus::PayloadTooLarge;
    }
    if (input.size() != kPacketBytes || input[0] != kVersion || input[1] == 0 ||
        input[2] != 0 || input[3] != 0) {
        return ScriptTaskFatalCodecStatus::Malformed;
    }
    const ScriptTaskFatalRecord decoded{
        {get_u32(input, 4), get_u32(input, 8), get_u32(input, 12)},
        input[1],
        get_u32(input, 16),
        get_u32(input, 20),
        get_u32(input, 24),
        get_u64(input, 28),
        get_u32(input, 36),
    };
    if (!decoded.valid()) {
        return ScriptTaskFatalCodecStatus::Malformed;
    }
    output = decoded;
    return ScriptTaskFatalCodecStatus::Accepted;
}

ScriptTaskMailboxPostStatus post_script_task_fatal(
    ScriptTaskSupervisor& supervisor,
    const ScriptTaskFatalRecord& record,
    std::uint32_t sequence,
    const ScriptTaskFatalCodecOptions& options) {
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::FatalRecord;
    packet.session = record.session;
    packet.sequence = sequence;
    const ScriptTaskFatalCodecStatus encoded = encode_script_task_fatal(record, options, packet.payload);
    if (encoded != ScriptTaskFatalCodecStatus::Accepted) {
        return encoded == ScriptTaskFatalCodecStatus::PayloadTooLarge
            ? ScriptTaskMailboxPostStatus::PayloadTooLarge
            : ScriptTaskMailboxPostStatus::InvalidPacket;
    }
    return supervisor.post_fatal(packet);
}

} // namespace jellyframe
