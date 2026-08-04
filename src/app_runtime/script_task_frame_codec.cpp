#include "app_runtime/script_task_frame_codec.h"

#include <algorithm>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::uint32_t kMagic = 0x4653464aU; // JFSF in little-endian.
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 32;
constexpr std::size_t kCommandBytes = 72;
constexpr std::size_t kTargetBytes = 24;

void put_u32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(value >> (i * 8U));
}
std::uint32_t get_u32(const std::vector<std::uint8_t>& in, std::size_t at) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(in[at + i]) << (i * 8U);
    return value;
}
void put_int(std::vector<std::uint8_t>& out, std::size_t at, int value) {
    put_u32(out, at, static_cast<std::uint32_t>(value));
}
int get_int(const std::vector<std::uint8_t>& in, std::size_t at) {
    return static_cast<int>(static_cast<std::int32_t>(get_u32(in, at)));
}
bool valid_command(const DisplayCommand& c) {
    return static_cast<std::uint8_t>(c.type) <= static_cast<std::uint8_t>(DisplayCommandType::Image) &&
           static_cast<std::uint8_t>(c.text_align) <= static_cast<std::uint8_t>(TextCommandAlign::End) &&
           static_cast<std::uint8_t>(c.gradient_axis) <= static_cast<std::uint8_t>(GradientAxis::RadialPosition) &&
           static_cast<std::uint8_t>(c.object_fit) <= static_cast<std::uint8_t>(ObjectFit::ScaleDown) &&
           static_cast<std::uint8_t>(c.image_rendering) <= static_cast<std::uint8_t>(ImageRendering::CrispEdges) &&
           c.rect.width >= 0 && c.rect.height >= 0 && c.text.size() <= std::numeric_limits<std::uint32_t>::max();
}
bool valid_target(const ScriptTaskInputTarget& target) {
    return target.target_key != 0 && target.rect.width >= 0 && target.rect.height >= 0;
}
bool checked_total(std::size_t left, std::size_t right, std::size_t& result) {
    return checked_add(left, right, result);
}
bool has_duplicate_target_key(const std::vector<ScriptTaskInputTarget>& targets) {
    for (std::size_t i = 0; i < targets.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) if (targets[i].target_key == targets[j].target_key) return true;
    }
    return false;
}
}

ScriptTaskAppFrameCodecStatus encode_script_task_app_frame(const ScriptTaskAppFrame& frame,
                                                            const ScriptTaskAppFrameCodecOptions& options,
                                                            std::vector<std::uint8_t>& output) {
    if (frame.viewport.width < 0 || frame.viewport.height < 0 || frame.display_list.size() > options.max_commands) {
        return ScriptTaskAppFrameCodecStatus::TooManyCommands;
    }
    if (frame.input_targets.size() > options.max_input_targets) return ScriptTaskAppFrameCodecStatus::TooManyInputTargets;
    if (has_duplicate_target_key(frame.input_targets)) return ScriptTaskAppFrameCodecStatus::InvalidValue;
    std::size_t text_bytes = 0;
    std::size_t total = kHeaderBytes;
    for (const DisplayCommand& command : frame.display_list) {
        if (!valid_command(command) || !checked_total(text_bytes, command.text.size(), text_bytes) ||
            !checked_total(total, kCommandBytes, total) || !checked_total(total, command.text.size(), total)) {
            return ScriptTaskAppFrameCodecStatus::InvalidValue;
        }
    }
    if (text_bytes > options.max_text_bytes) return ScriptTaskAppFrameCodecStatus::TooManyTextBytes;
    std::size_t targets_bytes = 0;
    if (!checked_multiply(frame.input_targets.size(), kTargetBytes, targets_bytes) ||
        !checked_total(total, targets_bytes, total) || total > options.max_payload_bytes) {
        return ScriptTaskAppFrameCodecStatus::PayloadTooLarge;
    }
    for (const ScriptTaskInputTarget& target : frame.input_targets) if (!valid_target(target)) return ScriptTaskAppFrameCodecStatus::InvalidValue;

    output.assign(total, 0);
    put_u32(output, 0, kMagic); output[4] = kVersion;
    put_int(output, 8, frame.viewport.x); put_int(output, 12, frame.viewport.y);
    put_int(output, 16, frame.viewport.width); put_int(output, 20, frame.viewport.height);
    put_u32(output, 24, static_cast<std::uint32_t>(frame.display_list.size()));
    put_u32(output, 28, static_cast<std::uint32_t>(frame.input_targets.size()));
    std::size_t at = kHeaderBytes;
    for (const DisplayCommand& c : frame.display_list) {
        output[at] = static_cast<std::uint8_t>(c.type); output[at + 1] = static_cast<std::uint8_t>(c.text_align);
        output[at + 2] = c.text_single_line ? 1 : 0; output[at + 3] = static_cast<std::uint8_t>(c.gradient_axis);
        output[at + 4] = static_cast<std::uint8_t>(c.object_fit); output[at + 5] = static_cast<std::uint8_t>(c.image_rendering);
        put_int(output, at + 8, c.rect.x); put_int(output, at + 12, c.rect.y);
        put_int(output, at + 16, c.rect.width); put_int(output, at + 20, c.rect.height);
        output[at + 24] = c.color.r; output[at + 25] = c.color.g; output[at + 26] = c.color.b; output[at + 27] = c.color.a;
        output[at + 28] = c.color2.r; output[at + 29] = c.color2.g; output[at + 30] = c.color2.b; output[at + 31] = c.color2.a;
        put_int(output, at + 32, c.border_radius); put_int(output, at + 36, c.stroke_width);
        put_int(output, at + 40, c.font_size); put_int(output, at + 44, c.font_weight);
        put_u32(output, at + 48, c.font_family_hash); put_int(output, at + 52, c.gradient_stop_percent);
        put_u32(output, at + 56, c.image_handle); put_int(output, at + 60, c.object_position.x_percent);
        put_int(output, at + 64, c.object_position.y_percent); put_u32(output, at + 68, static_cast<std::uint32_t>(c.text.size()));
        at += kCommandBytes;
        std::copy(c.text.begin(), c.text.end(), output.begin() + static_cast<std::ptrdiff_t>(at)); at += c.text.size();
    }
    for (const ScriptTaskInputTarget& target : frame.input_targets) {
        put_u32(output, at, target.target_key); put_int(output, at + 4, target.rect.x); put_int(output, at + 8, target.rect.y);
        put_int(output, at + 12, target.rect.width); put_int(output, at + 16, target.rect.height); output[at + 20] = target.enabled ? 1 : 0;
        at += kTargetBytes;
    }
    return ScriptTaskAppFrameCodecStatus::Accepted;
}

ScriptTaskAppFrameCodecStatus decode_script_task_app_frame(const std::vector<std::uint8_t>& input,
                                                            const ScriptTaskAppFrameCodecOptions& options,
                                                            ScriptTaskAppFrame& output) {
    if (input.size() < kHeaderBytes || get_u32(input, 0) != kMagic || input[4] != kVersion ||
        input[5] != 0 || input[6] != 0 || input[7] != 0 || input.size() > options.max_payload_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
    const std::size_t command_count = get_u32(input, 24), target_count = get_u32(input, 28);
    if (command_count > options.max_commands) return ScriptTaskAppFrameCodecStatus::TooManyCommands;
    if (target_count > options.max_input_targets) return ScriptTaskAppFrameCodecStatus::TooManyInputTargets;
    ScriptTaskAppFrame decoded;
    decoded.viewport = {get_int(input, 8), get_int(input, 12), get_int(input, 16), get_int(input, 20)};
    if (decoded.viewport.width < 0 || decoded.viewport.height < 0) return ScriptTaskAppFrameCodecStatus::Malformed;
    decoded.display_list.reserve(command_count); decoded.input_targets.reserve(target_count);
    std::size_t at = kHeaderBytes, text_bytes = 0;
    for (std::size_t index = 0; index < command_count; ++index) {
        if (at > input.size() || input.size() - at < kCommandBytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        DisplayCommand c;
        c.type = static_cast<DisplayCommandType>(input[at]); c.text_align = static_cast<TextCommandAlign>(input[at + 1]);
        if (input[at + 2] > 1) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.text_single_line = input[at + 2] != 0; c.gradient_axis = static_cast<GradientAxis>(input[at + 3]);
        c.object_fit = static_cast<ObjectFit>(input[at + 4]); c.image_rendering = static_cast<ImageRendering>(input[at + 5]);
        if (input[at + 6] != 0 || input[at + 7] != 0) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.rect = {get_int(input, at + 8), get_int(input, at + 12), get_int(input, at + 16), get_int(input, at + 20)};
        c.color = {input[at + 24], input[at + 25], input[at + 26], input[at + 27]}; c.color2 = {input[at + 28], input[at + 29], input[at + 30], input[at + 31]};
        c.border_radius = get_int(input, at + 32); c.stroke_width = get_int(input, at + 36); c.font_size = get_int(input, at + 40); c.font_weight = get_int(input, at + 44);
        c.font_family_hash = get_u32(input, at + 48); c.gradient_stop_percent = get_int(input, at + 52); c.image_handle = get_u32(input, at + 56);
        c.object_position = {get_int(input, at + 60), get_int(input, at + 64)}; const std::size_t text_length = get_u32(input, at + 68);
        at += kCommandBytes;
        if (!valid_command(c) || text_length > input.size() - at || !checked_total(text_bytes, text_length, text_bytes) || text_bytes > options.max_text_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.text.assign(reinterpret_cast<const char*>(input.data() + at), text_length); at += text_length; decoded.display_list.push_back(std::move(c));
    }
    for (std::size_t index = 0; index < target_count; ++index) {
        if (at > input.size() || input.size() - at < kTargetBytes || input[at + 20] > 1 || input[at + 21] != 0 || input[at + 22] != 0 || input[at + 23] != 0) return ScriptTaskAppFrameCodecStatus::Malformed;
        ScriptTaskInputTarget target{get_u32(input, at), {get_int(input, at + 4), get_int(input, at + 8), get_int(input, at + 12), get_int(input, at + 16)}, input[at + 20] != 0};
        if (!valid_target(target)) return ScriptTaskAppFrameCodecStatus::Malformed;
        decoded.input_targets.push_back(target); at += kTargetBytes;
    }
    if (at != input.size() || has_duplicate_target_key(decoded.input_targets)) return ScriptTaskAppFrameCodecStatus::Malformed;
    output = std::move(decoded);
    return ScriptTaskAppFrameCodecStatus::Accepted;
}

std::uint32_t resolve_script_task_input_target(const ScriptTaskAppFrame& frame, int x, int y) {
    for (auto it = frame.input_targets.rbegin(); it != frame.input_targets.rend(); ++it) {
        if (it->enabled && x >= it->rect.x && y >= it->rect.y && x < safe_edge(it->rect.x, it->rect.width) && y < safe_edge(it->rect.y, it->rect.height)) return it->target_key;
    }
    return 0;
}

ScriptTaskAppFrame make_script_task_app_frame(const LayerNode& layer_tree,
                                              Rect viewport,
                                              std::vector<ScriptTaskInputTarget> input_targets) {
    LayerTreeBuilder flattener;
    ScriptTaskAppFrame frame;
    frame.viewport = viewport;
    frame.display_list = flattener.flatten(layer_tree);
    frame.input_targets = std::move(input_targets);
    return frame;
}

ScriptTaskAppFramePublisher::ScriptTaskAppFramePublisher(ScriptTaskAppFrameCodecOptions options)
    : options_(options) {
    encoded_.reserve(options_.max_payload_bytes);
}

ScriptTaskAppFramePublishResult ScriptTaskAppFramePublisher::publish(
    ScriptTaskSupervisor& supervisor,
    const ScriptAppSession& session,
    const ScriptTaskAppFrame& frame) {
    ScriptTaskAppFramePublishResult result;
    result.codec_status = encode_script_task_app_frame(frame, options_, encoded_);
    if (result.codec_status != ScriptTaskAppFrameCodecStatus::Accepted) {
        return result;
    }
    result.lease = supervisor.publish_frame(session, encoded_);
    return result;
}

ScriptTaskAppFrameTakeStatus take_script_task_app_frame(ScriptTaskSupervisor& supervisor,
                                                         const ScriptAppSession& session,
                                                         const ScriptTaskAppFrameCodecOptions& options,
                                                         ScriptTaskAppFrame& output) {
    ScriptTaskPacket packet;
    if (!supervisor.take_worker_packet(packet)) {
        return ScriptTaskAppFrameTakeStatus::NoFrame;
    }
    if (packet.kind != ScriptTaskPacketKind::FrameReady || packet.session != session) {
        return ScriptTaskAppFrameTakeStatus::UnexpectedPacket;
    }
    std::vector<std::uint8_t> copied;
    if (supervisor.copy_frame(session, packet.lease_id, copied) != ScriptTaskFrameLeaseStatus::Accepted) {
        return ScriptTaskAppFrameTakeStatus::LeaseRejected;
    }
    const ScriptTaskFrameLeaseStatus released = supervisor.release_frame(session, packet.lease_id);
    if (released != ScriptTaskFrameLeaseStatus::Accepted) {
        return ScriptTaskAppFrameTakeStatus::LeaseRejected;
    }
    return decode_script_task_app_frame(copied, options, output) == ScriptTaskAppFrameCodecStatus::Accepted
        ? ScriptTaskAppFrameTakeStatus::Accepted
        : ScriptTaskAppFrameTakeStatus::DecodeRejected;
}
} // namespace jellyframe
