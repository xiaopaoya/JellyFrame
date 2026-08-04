#include "app_runtime/script_task_input_codec.h"

#include "render_core/geometry.h"

#include <limits>

namespace jellyframe {
namespace {
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 32;
constexpr std::uint8_t kAllowedModifiers = 0x0f;
void put_u32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}
std::uint32_t get_u32(const std::vector<std::uint8_t>& in, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[at + i]) << (i * 8U);
    return value;
}
void put_int(std::vector<std::uint8_t>& out, std::size_t at, int value) { put_u32(out, at, static_cast<std::uint32_t>(value)); }
int get_int(const std::vector<std::uint8_t>& in, std::size_t at) { return static_cast<int>(static_cast<std::int32_t>(get_u32(in, at))); }
bool known_kind(std::uint8_t kind) { return kind >= static_cast<std::uint8_t>(ScriptTaskInputKind::PointerMove) && kind <= static_cast<std::uint8_t>(ScriptTaskInputKind::TextInput); }
bool valid(const ScriptTaskInputEvent& input) {
    const std::uint8_t kind = static_cast<std::uint8_t>(input.kind);
    return known_kind(kind) && input.button >= -1 && input.button <= 2 && (input.modifiers & ~kAllowedModifiers) == 0 &&
           input.buttons >= 0 && input.text.size() <= std::numeric_limits<std::uint32_t>::max();
}
}

ScriptTaskInputCodecStatus encode_script_task_input(const ScriptTaskInputEvent& input,
                                                     const ScriptTaskInputCodecOptions& options,
                                                     std::vector<std::uint8_t>& output) {
    if (!valid(input)) return ScriptTaskInputCodecStatus::InvalidValue;
    if (input.text.size() > options.max_text_bytes) return ScriptTaskInputCodecStatus::TextTooLarge;
    std::size_t size = 0;
    if (!checked_add(kHeaderBytes, input.text.size(), size) || size > options.max_payload_bytes) return ScriptTaskInputCodecStatus::PayloadTooLarge;
    output.assign(size, 0);
    output[0] = kVersion; output[1] = static_cast<std::uint8_t>(input.kind); output[2] = input.modifiers;
    output[3] = static_cast<std::uint8_t>(static_cast<std::int8_t>(input.button));
    put_int(output, 4, input.x); put_int(output, 8, input.y); put_int(output, 12, input.delta_x); put_int(output, 16, input.delta_y);
    put_int(output, 20, input.buttons); put_u32(output, 24, input.key_code); put_u32(output, 28, static_cast<std::uint32_t>(input.text.size()));
    for (std::size_t i = 0; i < input.text.size(); ++i) output[kHeaderBytes + i] = static_cast<std::uint8_t>(input.text[i]);
    return ScriptTaskInputCodecStatus::Accepted;
}

ScriptTaskInputCodecStatus decode_script_task_input(const std::vector<std::uint8_t>& input,
                                                     const ScriptTaskInputCodecOptions& options,
                                                     ScriptTaskInputEvent& output) {
    if (input.size() < kHeaderBytes || input.size() > options.max_payload_bytes || input[0] != kVersion || !known_kind(input[1]) ||
        (input[2] & ~kAllowedModifiers) != 0) return ScriptTaskInputCodecStatus::Malformed;
    const int button = static_cast<int>(static_cast<std::int8_t>(input[3]));
    const std::size_t text_size = get_u32(input, 28);
    if (button < -1 || button > 2 || text_size > options.max_text_bytes || text_size != input.size() - kHeaderBytes) return ScriptTaskInputCodecStatus::Malformed;
    ScriptTaskInputEvent decoded;
    decoded.kind = static_cast<ScriptTaskInputKind>(input[1]); decoded.modifiers = input[2]; decoded.button = button;
    decoded.x = get_int(input, 4); decoded.y = get_int(input, 8); decoded.delta_x = get_int(input, 12); decoded.delta_y = get_int(input, 16);
    decoded.buttons = get_int(input, 20); decoded.key_code = get_u32(input, 24);
    if (decoded.buttons < 0) return ScriptTaskInputCodecStatus::Malformed;
    decoded.text.assign(reinterpret_cast<const char*>(input.data() + kHeaderBytes), text_size);
    output = std::move(decoded);
    return ScriptTaskInputCodecStatus::Accepted;
}

ScriptTaskInputPostResult post_script_task_input(ScriptTaskSupervisor& supervisor,
                                                  const ScriptAppSession& session,
                                                  std::uint32_t sequence,
                                                  const ScriptTaskInputEvent& input,
                                                  const ScriptTaskInputCodecOptions& options) {
    ScriptTaskInputPostResult result;
    ScriptTaskPacket packet;
    packet.kind = ScriptTaskPacketKind::Input; packet.session = session; packet.sequence = sequence;
    result.codec_status = encode_script_task_input(input, options, packet.payload);
    if (result.codec_status == ScriptTaskInputCodecStatus::Accepted) result.mailbox_status = supervisor.post_input(packet);
    return result;
}
} // namespace jellyframe
