#include "app_runtime/script_task_frame_codec.h"

#include "render_core/raster_primitives.h"

#include <algorithm>
#include <limits>

namespace jellyframe {
namespace {

constexpr std::uint32_t kMagic = 0x4653464aU; // JFSF in little-endian.
constexpr std::uint8_t kVersionV1 = 1;
constexpr std::uint8_t kVersionV2 = 2;
constexpr std::uint8_t kVersionV3 = 3;
constexpr std::size_t kHeaderBytesV1 = 32;
constexpr std::size_t kHeaderBytesV2 = 36;
constexpr std::size_t kCommandBytesV1V2 = 72;
constexpr std::size_t kCommandBytesV3 = 100;
constexpr std::size_t kTargetBytes = 24;
constexpr std::size_t kClipBytes = 28;

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
std::size_t command_bytes_for(std::uint8_t version) {
    return version == kVersionV3 ? kCommandBytesV3 : kCommandBytesV1V2;
}
bool has_transform(const DisplayCommand& command) {
    return command.transform.enabled;
}
bool valid_transform(const DisplayCommandTransform& transform) {
    const std::int32_t values[] = {
        transform.xx_1024, transform.xy_1024, transform.yx_1024,
        transform.yy_1024, transform.tx_1024, transform.ty_1024,
    };
    return std::all_of(std::begin(values), std::end(values), [](std::int32_t value) {
        constexpr std::int64_t kMaximumFixedValue = 64LL * 1024LL * 1024LL;
        return static_cast<std::int64_t>(value) >= -kMaximumFixedValue &&
            static_cast<std::int64_t>(value) <= kMaximumFixedValue;
    });
}
bool valid_command(const DisplayCommand& c) {
    return static_cast<std::uint8_t>(c.type) <= static_cast<std::uint8_t>(DisplayCommandType::Image) &&
           static_cast<std::uint8_t>(c.text_align) <= static_cast<std::uint8_t>(TextCommandAlign::End) &&
           static_cast<std::uint8_t>(c.gradient_axis) <= static_cast<std::uint8_t>(GradientAxis::RadialPosition) &&
           static_cast<std::uint8_t>(c.object_fit) <= static_cast<std::uint8_t>(ObjectFit::ScaleDown) &&
           static_cast<std::uint8_t>(c.image_rendering) <= static_cast<std::uint8_t>(ImageRendering::CrispEdges) &&
           c.rect.width >= 0 && c.rect.height >= 0 && valid_transform(c.transform) &&
           c.text.size() <= std::numeric_limits<std::uint32_t>::max();
}
bool valid_target(const ScriptTaskInputTarget& target) {
    return target.target_key != 0 && target.rect.width >= 0 && target.rect.height >= 0;
}
bool valid_clip(const ScriptTaskFrameClip& clip) {
    return clip.rect.width >= 0 && clip.rect.height >= 0 && clip.border_radius >= 0;
}
bool point_in_rect(Rect rect, int x, int y) {
    return x >= rect.x && y >= rect.y &&
        static_cast<std::int64_t>(x) < static_cast<std::int64_t>(rect.x) + rect.width &&
        static_cast<std::int64_t>(y) < static_cast<std::int64_t>(rect.y) + rect.height;
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

bool valid_clip_index(std::uint16_t index, std::size_t clip_count) {
    return index == kScriptTaskNoClip || static_cast<std::size_t>(index) < clip_count;
}

bool validate_clip_chain(const std::vector<ScriptTaskFrameClip>& clips,
                         std::uint32_t index,
                         std::size_t max_depth) {
    std::size_t depth = 0;
    while (index != kScriptTaskNoParentClip) {
        if (index >= clips.size() || ++depth > max_depth) {
            return false;
        }
        const std::uint32_t parent = clips[index].parent_clip;
        if (parent != kScriptTaskNoParentClip && parent >= index) {
            return false;
        }
        index = parent;
    }
    return true;
}

bool point_in_clip_chain(const ScriptTaskAppFrame& frame, std::uint16_t clip_index, int x, int y) {
    if (clip_index == kScriptTaskNoClip) {
        return true;
    }
    std::size_t depth = 0;
    std::uint32_t index = clip_index;
    while (index != kScriptTaskNoParentClip) {
        if (index >= frame.clips.size() || ++depth > frame.clips.size()) {
            return false;
        }
        const ScriptTaskFrameClip& clip = frame.clips[index];
        if (!point_in_rect(clip.rect, x, y) ||
            rounded_rect_coverage(clip.rect, clip.border_radius, x, y) == 0) {
            return false;
        }
        index = clip.parent_clip;
    }
    return true;
}
}

ScriptTaskAppFrameCodecStatus encode_script_task_app_frame(const ScriptTaskAppFrame& frame,
                                                            const ScriptTaskAppFrameCodecOptions& options,
                                                            std::vector<std::uint8_t>& output) {
    if (options.version != kVersionV1 && options.version != kVersionV2 && options.version != kVersionV3) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    }
    const bool has_clip_values = !frame.clips.empty() || !frame.display_clip_indices.empty();
    const bool has_target_clips = std::any_of(frame.input_targets.begin(), frame.input_targets.end(),
                                              [](const ScriptTaskInputTarget& target) {
                                                  return target.clip_index != kScriptTaskNoClip;
                                              });
    if (options.version == kVersionV1 && (has_clip_values || has_target_clips)) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature;
    }
    if (options.version != kVersionV3 && std::any_of(frame.display_list.begin(), frame.display_list.end(), has_transform)) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedTransformFeature;
    }
    if (frame.viewport.width < 0 || frame.viewport.height < 0 || frame.display_list.size() > options.max_commands) {
        return ScriptTaskAppFrameCodecStatus::TooManyCommands;
    }
    if (frame.input_targets.size() > options.max_input_targets) return ScriptTaskAppFrameCodecStatus::TooManyInputTargets;
    if (frame.clips.size() > options.max_clips || frame.clips.size() > std::numeric_limits<std::uint16_t>::max()) {
        return ScriptTaskAppFrameCodecStatus::TooManyClips;
    }
    if (!frame.display_clip_indices.empty() && frame.display_clip_indices.size() != frame.display_list.size()) {
        return ScriptTaskAppFrameCodecStatus::InvalidClip;
    }
    if (has_duplicate_target_key(frame.input_targets)) return ScriptTaskAppFrameCodecStatus::InvalidValue;
    for (std::size_t index = 0; index < frame.clips.size(); ++index) {
        const ScriptTaskFrameClip& clip = frame.clips[index];
        if (!valid_clip(clip)) return ScriptTaskAppFrameCodecStatus::InvalidClip;
        if (clip.parent_clip != kScriptTaskNoParentClip && clip.parent_clip >= index) {
            return ScriptTaskAppFrameCodecStatus::InvalidClip;
        }
        if (!validate_clip_chain(frame.clips, static_cast<std::uint32_t>(index), options.max_clip_depth)) {
            return ScriptTaskAppFrameCodecStatus::TooDeepClipChain;
        }
    }
    const auto clip_for_command = [&](std::size_t index) {
        return frame.display_clip_indices.empty() ? kScriptTaskNoClip : frame.display_clip_indices[index];
    };
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        if (!valid_clip_index(clip_for_command(index), frame.clips.size())) {
            return ScriptTaskAppFrameCodecStatus::InvalidClip;
        }
    }
    std::size_t text_bytes = 0;
    const std::size_t header_bytes = options.version == kVersionV1 ? kHeaderBytesV1 : kHeaderBytesV2;
    std::size_t total = header_bytes;
    if (options.version >= kVersionV2) {
        std::size_t clip_bytes = 0;
        if (!checked_multiply(frame.clips.size(), kClipBytes, clip_bytes) ||
            !checked_total(total, clip_bytes, total)) {
            return ScriptTaskAppFrameCodecStatus::PayloadTooLarge;
        }
    }
    for (const DisplayCommand& command : frame.display_list) {
        if (!valid_command(command) || !checked_total(text_bytes, command.text.size(), text_bytes) ||
            !checked_total(total, command_bytes_for(options.version), total) ||
            !checked_total(total, command.text.size(), total)) {
            return ScriptTaskAppFrameCodecStatus::InvalidValue;
        }
    }
    if (text_bytes > options.max_text_bytes) return ScriptTaskAppFrameCodecStatus::TooManyTextBytes;
    std::size_t targets_bytes = 0;
    if (!checked_multiply(frame.input_targets.size(), kTargetBytes, targets_bytes) ||
        !checked_total(total, targets_bytes, total) || total > options.max_payload_bytes) {
        return ScriptTaskAppFrameCodecStatus::PayloadTooLarge;
    }
    for (const ScriptTaskInputTarget& target : frame.input_targets) {
        if (!valid_target(target)) return ScriptTaskAppFrameCodecStatus::InvalidValue;
        if (!valid_clip_index(target.clip_index, frame.clips.size())) return ScriptTaskAppFrameCodecStatus::InvalidClip;
    }

    output.assign(total, 0);
    put_u32(output, 0, kMagic); output[4] = options.version;
    put_int(output, 8, frame.viewport.x); put_int(output, 12, frame.viewport.y);
    put_int(output, 16, frame.viewport.width); put_int(output, 20, frame.viewport.height);
    put_u32(output, 24, static_cast<std::uint32_t>(frame.display_list.size()));
    put_u32(output, 28, static_cast<std::uint32_t>(frame.input_targets.size()));
    std::size_t at = header_bytes;
    if (options.version >= kVersionV2) {
        put_u32(output, 32, static_cast<std::uint32_t>(frame.clips.size()));
        for (const ScriptTaskFrameClip& clip : frame.clips) {
            put_int(output, at, clip.rect.x); put_int(output, at + 4, clip.rect.y);
            put_int(output, at + 8, clip.rect.width); put_int(output, at + 12, clip.rect.height);
            put_int(output, at + 16, clip.border_radius); put_u32(output, at + 20, clip.parent_clip);
            at += kClipBytes;
        }
    }
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        const DisplayCommand& c = frame.display_list[index];
        output[at] = static_cast<std::uint8_t>(c.type); output[at + 1] = static_cast<std::uint8_t>(c.text_align);
        output[at + 2] = c.text_single_line ? 1 : 0; output[at + 3] = static_cast<std::uint8_t>(c.gradient_axis);
        output[at + 4] = static_cast<std::uint8_t>(c.object_fit); output[at + 5] = static_cast<std::uint8_t>(c.image_rendering);
        if (options.version >= kVersionV2) {
            const std::uint16_t clip_index = clip_for_command(index);
            output[at + 6] = static_cast<std::uint8_t>(clip_index & 0xffU);
            output[at + 7] = static_cast<std::uint8_t>(clip_index >> 8U);
        }
        put_int(output, at + 8, c.rect.x); put_int(output, at + 12, c.rect.y);
        put_int(output, at + 16, c.rect.width); put_int(output, at + 20, c.rect.height);
        output[at + 24] = c.color.r; output[at + 25] = c.color.g; output[at + 26] = c.color.b; output[at + 27] = c.color.a;
        output[at + 28] = c.color2.r; output[at + 29] = c.color2.g; output[at + 30] = c.color2.b; output[at + 31] = c.color2.a;
        put_int(output, at + 32, c.border_radius); put_int(output, at + 36, c.stroke_width);
        put_int(output, at + 40, c.font_size); put_int(output, at + 44, c.font_weight);
        put_u32(output, at + 48, c.font_family_hash); put_int(output, at + 52, c.gradient_stop_percent);
        put_u32(output, at + 56, c.image_handle); put_int(output, at + 60, c.object_position.x_percent);
        put_int(output, at + 64, c.object_position.y_percent); put_u32(output, at + 68, static_cast<std::uint32_t>(c.text.size()));
        if (options.version == kVersionV3) {
            output[at + 72] = c.transform.enabled ? 1 : 0;
            put_int(output, at + 76, c.transform.xx_1024);
            put_int(output, at + 80, c.transform.xy_1024);
            put_int(output, at + 84, c.transform.yx_1024);
            put_int(output, at + 88, c.transform.yy_1024);
            put_int(output, at + 92, c.transform.tx_1024);
            put_int(output, at + 96, c.transform.ty_1024);
        }
        at += command_bytes_for(options.version);
        std::copy(c.text.begin(), c.text.end(), output.begin() + static_cast<std::ptrdiff_t>(at)); at += c.text.size();
    }
    for (const ScriptTaskInputTarget& target : frame.input_targets) {
        put_u32(output, at, target.target_key); put_int(output, at + 4, target.rect.x); put_int(output, at + 8, target.rect.y);
        put_int(output, at + 12, target.rect.width); put_int(output, at + 16, target.rect.height); output[at + 20] = target.enabled ? 1 : 0;
        if (options.version >= kVersionV2) {
            output[at + 21] = static_cast<std::uint8_t>(target.clip_index & 0xffU);
            output[at + 22] = static_cast<std::uint8_t>(target.clip_index >> 8U);
        }
        at += kTargetBytes;
    }
    return ScriptTaskAppFrameCodecStatus::Accepted;
}

ScriptTaskAppFrameCodecStatus decode_script_task_app_frame(const std::vector<std::uint8_t>& input,
                                                            const ScriptTaskAppFrameCodecOptions& options,
                                                            ScriptTaskAppFrame& output) {
    if (input.size() < kHeaderBytesV1 || get_u32(input, 0) != kMagic ||
        input[5] != 0 || input[6] != 0 || input[7] != 0 || input.size() > options.max_payload_bytes) {
        return ScriptTaskAppFrameCodecStatus::Malformed;
    }
    const std::uint8_t version = input[4];
    if (version != kVersionV1 && version != kVersionV2 && version != kVersionV3) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    }
    if (options.version != version) return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    const std::size_t header_bytes = version == kVersionV1 ? kHeaderBytesV1 : kHeaderBytesV2;
    if (input.size() < header_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
    const std::size_t command_count = get_u32(input, 24), target_count = get_u32(input, 28);
    if (command_count > options.max_commands) return ScriptTaskAppFrameCodecStatus::TooManyCommands;
    if (target_count > options.max_input_targets) return ScriptTaskAppFrameCodecStatus::TooManyInputTargets;
    ScriptTaskAppFrame decoded;
    decoded.viewport = {get_int(input, 8), get_int(input, 12), get_int(input, 16), get_int(input, 20)};
    if (decoded.viewport.width < 0 || decoded.viewport.height < 0) return ScriptTaskAppFrameCodecStatus::Malformed;
    std::size_t at = header_bytes, text_bytes = 0;
    if (version >= kVersionV2) {
        const std::size_t clip_count = get_u32(input, 32);
        if (clip_count > options.max_clips || clip_count > std::numeric_limits<std::uint16_t>::max()) {
            return ScriptTaskAppFrameCodecStatus::TooManyClips;
        }
        std::size_t clip_bytes = 0;
        if (!checked_multiply(clip_count, kClipBytes, clip_bytes) ||
            clip_bytes > input.size() - at) {
            return ScriptTaskAppFrameCodecStatus::Malformed;
        }
        decoded.clips.reserve(clip_count);
        for (std::size_t index = 0; index < clip_count; ++index) {
            ScriptTaskFrameClip clip;
            clip.rect = {get_int(input, at), get_int(input, at + 4),
                         get_int(input, at + 8), get_int(input, at + 12)};
            clip.border_radius = get_int(input, at + 16);
            clip.parent_clip = get_u32(input, at + 20);
            if (get_u32(input, at + 24) != 0 || !valid_clip(clip) ||
                (clip.parent_clip != kScriptTaskNoParentClip && clip.parent_clip >= index)) {
                return ScriptTaskAppFrameCodecStatus::Malformed;
            }
            decoded.clips.push_back(clip);
            at += kClipBytes;
        }
        for (std::size_t index = 0; index < decoded.clips.size(); ++index) {
            if (!validate_clip_chain(decoded.clips, static_cast<std::uint32_t>(index), options.max_clip_depth)) {
                return ScriptTaskAppFrameCodecStatus::TooDeepClipChain;
            }
        }
    }
    decoded.display_list.reserve(command_count);
    if (version >= kVersionV2) decoded.display_clip_indices.reserve(command_count);
    decoded.input_targets.reserve(target_count);
    for (std::size_t index = 0; index < command_count; ++index) {
        const std::size_t command_bytes = command_bytes_for(version);
        if (at > input.size() || input.size() - at < command_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        DisplayCommand c;
        c.type = static_cast<DisplayCommandType>(input[at]); c.text_align = static_cast<TextCommandAlign>(input[at + 1]);
        if (input[at + 2] > 1) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.text_single_line = input[at + 2] != 0; c.gradient_axis = static_cast<GradientAxis>(input[at + 3]);
        c.object_fit = static_cast<ObjectFit>(input[at + 4]); c.image_rendering = static_cast<ImageRendering>(input[at + 5]);
        const std::uint16_t clip_index = version >= kVersionV2
            ? static_cast<std::uint16_t>(input[at + 6] | (static_cast<std::uint16_t>(input[at + 7]) << 8U))
            : kScriptTaskNoClip;
        if (version == kVersionV1 && (input[at + 6] != 0 || input[at + 7] != 0)) return ScriptTaskAppFrameCodecStatus::Malformed;
        if (!valid_clip_index(clip_index, decoded.clips.size())) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.rect = {get_int(input, at + 8), get_int(input, at + 12), get_int(input, at + 16), get_int(input, at + 20)};
        c.color = {input[at + 24], input[at + 25], input[at + 26], input[at + 27]}; c.color2 = {input[at + 28], input[at + 29], input[at + 30], input[at + 31]};
        c.border_radius = get_int(input, at + 32); c.stroke_width = get_int(input, at + 36); c.font_size = get_int(input, at + 40); c.font_weight = get_int(input, at + 44);
        c.font_family_hash = get_u32(input, at + 48); c.gradient_stop_percent = get_int(input, at + 52); c.image_handle = get_u32(input, at + 56);
        c.object_position = {get_int(input, at + 60), get_int(input, at + 64)}; const std::size_t text_length = get_u32(input, at + 68);
        if (version == kVersionV3) {
            if (input[at + 72] > 1 || input[at + 73] != 0 || input[at + 74] != 0 || input[at + 75] != 0) {
                return ScriptTaskAppFrameCodecStatus::Malformed;
            }
            c.transform.enabled = input[at + 72] != 0;
            c.transform.xx_1024 = get_int(input, at + 76);
            c.transform.xy_1024 = get_int(input, at + 80);
            c.transform.yx_1024 = get_int(input, at + 84);
            c.transform.yy_1024 = get_int(input, at + 88);
            c.transform.tx_1024 = get_int(input, at + 92);
            c.transform.ty_1024 = get_int(input, at + 96);
        }
        at += command_bytes;
        if (!valid_command(c) || text_length > input.size() - at || !checked_total(text_bytes, text_length, text_bytes) || text_bytes > options.max_text_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.text.assign(reinterpret_cast<const char*>(input.data() + at), text_length); at += text_length;
        decoded.display_list.push_back(std::move(c));
        if (version >= kVersionV2) decoded.display_clip_indices.push_back(clip_index);
    }
    for (std::size_t index = 0; index < target_count; ++index) {
        if (at > input.size() || input.size() - at < kTargetBytes || input[at + 20] > 1 ||
            (version == kVersionV1 && (input[at + 21] != 0 || input[at + 22] != 0)) || input[at + 23] != 0) {
            return ScriptTaskAppFrameCodecStatus::Malformed;
        }
        const std::uint16_t clip_index = version >= kVersionV2
            ? static_cast<std::uint16_t>(input[at + 21] | (static_cast<std::uint16_t>(input[at + 22]) << 8U))
            : kScriptTaskNoClip;
        ScriptTaskInputTarget target{get_u32(input, at), {get_int(input, at + 4), get_int(input, at + 8), get_int(input, at + 12), get_int(input, at + 16)}, input[at + 20] != 0, clip_index};
        if (!valid_target(target)) return ScriptTaskAppFrameCodecStatus::Malformed;
        if (!valid_clip_index(target.clip_index, decoded.clips.size())) return ScriptTaskAppFrameCodecStatus::Malformed;
        decoded.input_targets.push_back(target); at += kTargetBytes;
    }
    if (at != input.size() || has_duplicate_target_key(decoded.input_targets)) return ScriptTaskAppFrameCodecStatus::Malformed;
    output = std::move(decoded);
    return ScriptTaskAppFrameCodecStatus::Accepted;
}

std::uint32_t resolve_script_task_input_target(const ScriptTaskAppFrame& frame, int x, int y) {
    for (auto it = frame.input_targets.rbegin(); it != frame.input_targets.rend(); ++it) {
        if (it->enabled && x >= it->rect.x && y >= it->rect.y &&
            x < safe_edge(it->rect.x, it->rect.width) &&
            y < safe_edge(it->rect.y, it->rect.height) &&
            point_in_clip_chain(frame, it->clip_index, x, y)) {
            return it->target_key;
        }
    }
    return 0;
}

ScriptTaskAppFrame make_script_task_app_frame(const LayerNode& layer_tree,
                                              Rect viewport,
                                              std::vector<ScriptTaskInputTarget> input_targets,
                                              bool include_clip_metadata) {
    LayerTreeBuilder flattener;
    ScriptTaskAppFrame frame;
    frame.viewport = viewport;
    if (include_clip_metadata) {
        FlattenedLayerTree flattened = flattener.flatten_with_clip_metadata(layer_tree);
        frame.display_list = std::move(flattened.display_list);
        frame.display_clip_indices.reserve(flattened.display_clip_indices.size());
        for (const std::uint32_t clip_index : flattened.display_clip_indices) {
            frame.display_clip_indices.push_back(clip_index > std::numeric_limits<std::uint16_t>::max()
                                                     ? kScriptTaskNoClip
                                                     : static_cast<std::uint16_t>(clip_index));
        }
        frame.clips.reserve(flattened.clips.size());
        for (const FlattenedClip& clip : flattened.clips) {
            frame.clips.push_back({clip.rect, clip.border_radius, clip.parent_clip});
        }
    } else {
        frame.display_list = flattener.flatten(layer_tree);
    }
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
                                                         ScriptTaskAppFrame& output,
                                                         std::uint32_t* accepted_packet_sequence) {
    ScriptTaskPacket packet;
    if (!supervisor.take_frame_packet(packet)) {
        return ScriptTaskAppFrameTakeStatus::NoFrame;
    }
    if (packet.kind != ScriptTaskPacketKind::FrameReady || packet.session != session) {
        return ScriptTaskAppFrameTakeStatus::UnexpectedPacket;
    }
    std::vector<std::uint8_t> copied;
    if (supervisor.copy_frame(session, packet.frame_lease_id, copied) != ScriptTaskFrameLeaseStatus::Accepted) {
        return ScriptTaskAppFrameTakeStatus::LeaseRejected;
    }
    const ScriptTaskFrameLeaseStatus released = supervisor.release_frame(session, packet.frame_lease_id);
    if (released != ScriptTaskFrameLeaseStatus::Accepted) {
        return ScriptTaskAppFrameTakeStatus::LeaseRejected;
    }
    if (decode_script_task_app_frame(copied, options, output) != ScriptTaskAppFrameCodecStatus::Accepted) {
        return ScriptTaskAppFrameTakeStatus::DecodeRejected;
    }
    if (accepted_packet_sequence != nullptr) {
        *accepted_packet_sequence = packet.sequence;
    }
    return ScriptTaskAppFrameTakeStatus::Accepted;
}
} // namespace jellyframe
