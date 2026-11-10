#include "app_runtime/script_task_command_access.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_sampling.h"
#include "app_runtime/script_task_frame_groups.h"
#include "app_runtime/script_task_prepared_text.h"
#include "app_runtime/script_task_canonical_codec.h"
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
#include "app_runtime/app_host.h"
#endif

#include "render_core/raster_primitives.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <cstring>
#include <new>

namespace jellyframe {
namespace {

constexpr std::uint32_t kMagic = 0x4653464aU; // JFSF in little-endian.
constexpr std::uint8_t kVersionV1 = 1;
constexpr std::uint8_t kVersionV2 = 2;
constexpr std::uint8_t kVersionV3 = 3;
constexpr std::uint8_t kVersionV4 = 4;
constexpr std::uint8_t kVersionV5 = 5;
constexpr std::uint8_t kVersionV6 = 6;
constexpr std::uint8_t kVersionV7 = 7;
constexpr std::uint8_t kVersionV8 = 8;
constexpr std::size_t kHeaderBytesV1 = 32;
constexpr std::size_t kHeaderBytesV2 = 36;
constexpr std::size_t kCommandBytesV1V2 = 72;
constexpr std::size_t kCommandBytesV3 = 100;
constexpr std::size_t kCommandBytesV4 = 104;
constexpr std::size_t kCommandBytesV5 = 120;
constexpr std::size_t kCommandBytesV8 = 128;
constexpr std::size_t kTargetBytes = 24;
constexpr std::size_t kClipBytes = 28;
constexpr std::size_t kHeaderBytesV6 = 44;
constexpr std::size_t kGroupBytes = 152;
constexpr std::size_t kEllipseBytes = 36;

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
void put_float(std::vector<std::uint8_t>& out, std::size_t at, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t) && std::numeric_limits<float>::is_iec559);
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    put_u32(out, at, bits);
}
float get_float(const std::vector<std::uint8_t>& in, std::size_t at) {
    const auto bits = get_u32(in, at);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
void put_rect(std::vector<std::uint8_t>& out, std::size_t at, Rect value) {
    put_int(out, at, value.x); put_int(out, at + 4, value.y);
    put_int(out, at + 8, value.width); put_int(out, at + 12, value.height);
}
Rect get_rect(const std::vector<std::uint8_t>& in, std::size_t at) {
    return {get_int(in, at), get_int(in, at + 4), get_int(in, at + 8), get_int(in, at + 12)};
}
void put_group(std::vector<std::uint8_t>& out, std::size_t at, const ScriptTaskFrameGroup& group) {
    put_u32(out, at, group.parent);
    put_u32(out, at + 4, group.command_begin); put_u32(out, at + 8, group.command_count);
    put_rect(out, at + 12, group.bounds); put_rect(out, at + 28, group.clip_rect);
    put_int(out, at + 44, group.clip_border_radius);
    put_u32(out, at + 48, (group.has_clip ? 1U : 0U) | (group.has_affine ? 2U : 0U) |
        (group.has_clip_ellipse ? 4U : 0U));
    put_u32(out, at + 52, static_cast<std::uint32_t>(group.type));
    put_u32(out, at + 56, group.clip_axes); put_float(out, at + 60, group.opacity);
    put_float(out, at + 64, group.transform.translate_x); put_float(out, at + 68, group.transform.translate_y);
    put_float(out, at + 72, group.transform.scale_x); put_float(out, at + 76, group.transform.scale_y);
    put_float(out, at + 80, group.transform.rotate_degrees);
    put_int(out, at + 84, group.origin_x_percent); put_int(out, at + 88, group.origin_y_percent);
    for (std::size_t i = 0; i < 6; ++i) put_float(out, at + 92 + i * 4, group.affine[i]);
    for (std::size_t i = 0; i < 8; ++i) put_int(out, at + 116 + i * 4, group.clip_ellipse[i]);
}
bool get_group(const std::vector<std::uint8_t>& in, std::size_t at, ScriptTaskFrameGroup& group) {
    const auto flags = get_u32(in, at + 48);
    const auto type = get_u32(in, at + 52), axes = get_u32(in, at + 56);
    if (flags > 7 || axes > 3 || type > static_cast<unsigned>(LayerType::Composited) ||
        get_u32(in, at + 148) != 0) return false;
    group.parent = get_u32(in, at);
    group.command_begin = get_u32(in, at + 4); group.command_count = get_u32(in, at + 8);
    group.bounds = get_rect(in, at + 12); group.clip_rect = get_rect(in, at + 28);
    group.clip_border_radius = get_int(in, at + 44);
    group.has_clip = (flags & 1) != 0; group.has_affine = (flags & 2) != 0;
    group.has_clip_ellipse = (flags & 4) != 0;
    group.type = static_cast<LayerType>(type); group.clip_axes = static_cast<std::uint8_t>(axes);
    group.opacity = get_float(in, at + 60);
    group.transform = {get_float(in, at + 64), get_float(in, at + 68), get_float(in, at + 72),
        get_float(in, at + 76), get_float(in, at + 80)};
    group.origin_x_percent = get_int(in, at + 84); group.origin_y_percent = get_int(in, at + 88);
    for (std::size_t i = 0; i < 6; ++i) group.affine[i] = get_float(in, at + 92 + i * 4);
    for (std::size_t i = 0; i < 8; ++i) group.clip_ellipse[i] = get_int(in, at + 116 + i * 4);
    return true;
}
std::size_t command_bytes_for(std::uint8_t version) {
    if (version >= kVersionV8) return kCommandBytesV8;
    return version >= kVersionV5 ? kCommandBytesV5 :
        (version == kVersionV4 ? kCommandBytesV4 :
         (version == kVersionV3 ? kCommandBytesV3 : kCommandBytesV1V2));
}
bool has_transform(const DisplayCommand& command) {
    return command.transform.enabled;
}

bool is_identity_transform(const DisplayCommandTransform& transform) {
    return transform.xx_1024 == 1024 && transform.xy_1024 == 0 &&
        transform.yx_1024 == 0 && transform.yy_1024 == 1024 &&
        transform.tx_1024 == 0 && transform.ty_1024 == 0 && transform.source_clip_index == kScriptTaskNoClip;
}

bool valid_transform(const DisplayCommandTransform& transform) {
    const std::int32_t values[] = {
        transform.xx_1024, transform.xy_1024, transform.yx_1024,
        transform.yy_1024, transform.tx_1024, transform.ty_1024,
    };
    const bool values_in_range = std::all_of(std::begin(values), std::end(values), [](std::int32_t value) {
        constexpr std::int64_t kMaximumFixedValue = 64LL * 1024LL * 1024LL;
        return static_cast<std::int64_t>(value) >= -kMaximumFixedValue &&
            static_cast<std::int64_t>(value) <= kMaximumFixedValue;
    });
    if (!values_in_range) {
        return false;
    }
    // A disabled transform has no visual effect. Canonicalize that wire state
    // by requiring identity coefficients, so equal frames cannot differ only
    // in ignored bytes. Enabled transforms must be invertible enough for the
    // consumer's inverse affine sampling path. A singular enabled matrix is
    // still a valid CSS transform (for example scale(0)); the renderer treats
    // it as a command with no covered destination pixels.
    if (!transform.enabled) {
        return is_identity_transform(transform);
    }
    return true;
}
bool valid_wire_fields(const detail::LegacyCommandPaintFields& p) {
    return static_cast<std::uint8_t>(p.text_align) <= static_cast<std::uint8_t>(TextCommandAlign::End) &&
        static_cast<std::uint8_t>(p.gradient_axis) <= static_cast<std::uint8_t>(GradientAxis::RadialPosition) &&
        static_cast<std::uint8_t>(p.object_fit) <= static_cast<std::uint8_t>(ObjectFit::ScaleDown) &&
        static_cast<std::uint8_t>(p.image_rendering) <= static_cast<std::uint8_t>(ImageRendering::CrispEdges);
}
bool transport_command_type(DisplayCommandType type, std::uint8_t version) {
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    if (version >= kVersionV6 && (type == DisplayCommandType::GlyphRun ||
        type == DisplayCommandType::BaselineText || type == DisplayCommandType::OriginText ||
        type == DisplayCommandType::BorderRing)) return true;
#else
    (void)version;
#endif
    return detail::legacy_command_type(type);
}
bool valid_command(const DisplayCommand& c, std::uint8_t version = kVersionV5) {
    return transport_command_type(detail::command_type(c), version) &&
        (!detail::command_has_untransported_payload(c) ||
         (version >= kVersionV7 && detail::is_prepared_command_type(detail::command_type(c)))) &&
        valid_wire_fields(detail::legacy_command_fields(c)) &&
        (version >= kVersionV8 || detail::command_background_paint_clip_empty(detail::command_background_paint_clip(c))) &&
        c.rect.width >= 0 && c.rect.height >= 0 && valid_transform(c.transform) &&
        c.text.size() <= std::numeric_limits<std::uint32_t>::max();
}
bool valid_background_paint_clip(detail::CommandBackgroundPaintClip clip) {
    if (detail::command_background_paint_clip_empty(clip)) return true;
    return clip.dx >= std::numeric_limits<std::int16_t>::min() && clip.dx <= std::numeric_limits<std::int16_t>::max() &&
        clip.dy >= std::numeric_limits<std::int16_t>::min() && clip.dy <= std::numeric_limits<std::int16_t>::max() &&
        clip.width > 0 && clip.width <= std::numeric_limits<std::uint16_t>::max() &&
        clip.height > 0 && clip.height <= std::numeric_limits<std::uint16_t>::max();
}
bool valid_sampling_rect(Rect rect) {
    return rect.width >= 0 && rect.height >= 0;
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
    std::unordered_set<std::uint32_t> keys;
    keys.reserve(targets.size());
    for (const ScriptTaskInputTarget& target : targets) {
        if (!keys.insert(target.target_key).second) {
            return true;
        }
    }
    return false;
}

bool valid_clip_index(std::uint16_t index, std::size_t clip_count) {
    return index == kScriptTaskNoClip || static_cast<std::size_t>(index) < clip_count;
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

bool script_task_app_frame_codec_supports_version(std::uint8_t version) {
    return (version == 9 && detail::canonical::available) ||
        (version >= kVersionV1 && version <= kVersionV4) ||
        (version == kVersionV5 && detail::kScriptTaskSamplingTransportAvailable) ||
        (version == kVersionV6 && detail::kScriptTaskGroupTransportAvailable && detail::kScriptTaskSamplingTransportAvailable) ||
        (version == kVersionV7 && JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API &&
         detail::kScriptTaskGroupTransportAvailable && detail::kScriptTaskSamplingTransportAvailable) ||
        (version == kVersionV8 && JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API &&
         detail::kScriptTaskGroupTransportAvailable && detail::kScriptTaskSamplingTransportAvailable &&
         detail::has_background_clip_transport<DisplayCommand>());
}

ScriptTaskAppFrameCodecStatus encode_script_task_app_frame(const ScriptTaskAppFrame& frame,
                                                            const ScriptTaskAppFrameCodecOptions& options,
                                                            std::vector<std::uint8_t>& output) {
    if (options.version == 9) return detail::canonical::encode(frame, options, output);
    if (frame.image_provider_id) return ScriptTaskAppFrameCodecStatus::InvalidValue;
    if (options.version != kVersionV1 && options.version != kVersionV2 && options.version != kVersionV3 &&
        options.version != kVersionV4 && options.version != kVersionV5 && options.version != kVersionV6 && options.version != kVersionV7 &&
        options.version != kVersionV8) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    }
    if (options.version >= kVersionV7 && !JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API)
        return ScriptTaskAppFrameCodecStatus::UnsupportedTextFeature;
    if (options.version >= kVersionV7 && frame.groups.empty()) return ScriptTaskAppFrameCodecStatus::InvalidGroup;
    if (options.version == kVersionV8 && !script_task_app_frame_codec_supports_version(kVersionV8))
        return ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature;
    if (options.version >= kVersionV5 && !detail::kScriptTaskSamplingTransportAvailable) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature;
    }
    if ((!frame.groups.empty() || !frame.command_ellipses.empty()) && options.version < kVersionV6)
        return ScriptTaskAppFrameCodecStatus::UnsupportedGroupFeature;
    if (options.version >= kVersionV6) {
        if (!detail::kScriptTaskGroupTransportAvailable) return ScriptTaskAppFrameCodecStatus::UnsupportedGroupFeature;
        const auto group_status = detail::validate_frame_groups(frame, options.max_groups, options.max_group_depth);
        if (group_status != ScriptTaskAppFrameCodecStatus::Accepted) return group_status;
    }
    const bool has_clip_values = !frame.clips.empty() || !frame.display_clip_indices.empty();
    const bool has_target_clips = std::any_of(frame.input_targets.begin(), frame.input_targets.end(),
                                              [](const ScriptTaskInputTarget& target) {
                                                  return target.clip_index != kScriptTaskNoClip;
                                              });
    if (options.version == kVersionV1 && (has_clip_values || has_target_clips)) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature;
    }
    if (options.version < kVersionV3 && std::any_of(frame.display_list.begin(), frame.display_list.end(), has_transform)) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedTransformFeature;
    }
    if (options.version < kVersionV4 && std::any_of(frame.display_list.begin(), frame.display_list.end(),
                                                     [](const DisplayCommand& command) {
                                                         return command.transform.source_clip_index != kScriptTaskNoClip;
                                                     })) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedTransformFeature;
    }
    if (options.version < kVersionV5 && std::any_of(frame.display_list.begin(), frame.display_list.end(),
                                                     detail::has_legacy_incompatible_sampling)) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature;
    }
    if (options.version < kVersionV8 && std::any_of(frame.display_list.begin(), frame.display_list.end(),
        [](const DisplayCommand& command) {
            return !detail::command_background_paint_clip_empty(detail::command_background_paint_clip(command));
        })) return ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature;
    if (frame.viewport.width < 0 || frame.viewport.height < 0 || frame.display_list.size() > options.max_commands) {
        return ScriptTaskAppFrameCodecStatus::TooManyCommands;
    }
    if (frame.clip_metadata_overflow) return ScriptTaskAppFrameCodecStatus::InvalidClip;
    if (frame.input_targets.size() > options.max_input_targets) return ScriptTaskAppFrameCodecStatus::TooManyInputTargets;
    if (frame.clips.size() > options.max_clips || frame.clips.size() > std::numeric_limits<std::uint16_t>::max()) {
        return ScriptTaskAppFrameCodecStatus::TooManyClips;
    }
    if (!frame.display_clip_indices.empty() && frame.display_clip_indices.size() != frame.display_list.size()) {
        return ScriptTaskAppFrameCodecStatus::InvalidClip;
    }
    if (has_duplicate_target_key(frame.input_targets)) return ScriptTaskAppFrameCodecStatus::InvalidValue;
    std::vector<std::size_t> clip_depths(frame.clips.size(), 0);
    for (std::size_t index = 0; index < frame.clips.size(); ++index) {
        const ScriptTaskFrameClip& clip = frame.clips[index];
        if (!valid_clip(clip)) return ScriptTaskAppFrameCodecStatus::InvalidClip;
        if (clip.parent_clip != kScriptTaskNoParentClip && clip.parent_clip >= index) {
            return ScriptTaskAppFrameCodecStatus::InvalidClip;
        }
        clip_depths[index] = clip.parent_clip == kScriptTaskNoParentClip
            ? 1
            : clip_depths[clip.parent_clip] + 1;
        if (clip_depths[index] > options.max_clip_depth) {
            return ScriptTaskAppFrameCodecStatus::TooDeepClipChain;
        }
    }
    const auto clip_for_command = [&](std::size_t index) {
        return frame.display_clip_indices.empty() ? kScriptTaskNoClip : frame.display_clip_indices[index];
    };
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        if (!valid_clip_index(clip_for_command(index), frame.clips.size()) ||
            !valid_clip_index(frame.display_list[index].transform.source_clip_index, frame.clips.size())) {
            return ScriptTaskAppFrameCodecStatus::InvalidClip;
        }
    }
    std::size_t text_bytes = 0;
    const std::size_t header_bytes = options.version >= kVersionV6 ? kHeaderBytesV6 :
        (options.version == kVersionV1 ? kHeaderBytesV1 : kHeaderBytesV2);
    std::size_t total = header_bytes;
    if (options.version >= kVersionV6) {
        std::size_t group_bytes = 0, ellipse_bytes = 0;
        if (!checked_multiply(frame.groups.size(), kGroupBytes, group_bytes) ||
            !checked_multiply(frame.command_ellipses.size(), kEllipseBytes, ellipse_bytes) ||
            !checked_total(total, group_bytes, total) || !checked_total(total, ellipse_bytes, total))
            return ScriptTaskAppFrameCodecStatus::PayloadTooLarge;
    }
    if (options.version >= kVersionV2) {
        std::size_t clip_bytes = 0;
        if (!checked_multiply(frame.clips.size(), kClipBytes, clip_bytes) ||
            !checked_total(total, clip_bytes, total)) {
            return ScriptTaskAppFrameCodecStatus::PayloadTooLarge;
        }
    }
    for (const DisplayCommand& command : frame.display_list) {
        if (!valid_command(command, options.version) || !detail::valid_transport_text_command(command, options.version) ||
            (options.version >= kVersionV8 && !valid_background_paint_clip(detail::command_background_paint_clip(command))) ||
            (options.version >= kVersionV5 && !valid_sampling_rect(detail::command_sampling_rect(command))) ||
            !checked_total(text_bytes, command.text.size(), text_bytes) ||
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
    if (options.version >= kVersionV6) {
        put_u32(output, 36, static_cast<std::uint32_t>(frame.groups.size()));
        put_u32(output, 40, static_cast<std::uint32_t>(frame.command_ellipses.size()));
        for (const auto& group : frame.groups) { put_group(output, at, group); at += kGroupBytes; }
        for (const auto& ellipse : frame.command_ellipses) {
            put_u32(output, at, ellipse.command_index);
            for (std::size_t i = 0; i < 8; ++i) put_int(output, at + 4 + i * 4, ellipse.radii[i]);
            at += kEllipseBytes;
        }
    }
    for (std::size_t index = 0; index < frame.display_list.size(); ++index) {
        const DisplayCommand& c = frame.display_list[index];
        const auto p = detail::legacy_command_fields(c);
        output[at] = static_cast<std::uint8_t>(detail::command_type(c)); output[at + 1] = static_cast<std::uint8_t>(p.text_align);
        output[at + 2] = p.text_single_line ? 1 : 0; output[at + 3] = static_cast<std::uint8_t>(p.gradient_axis);
        output[at + 4] = static_cast<std::uint8_t>(p.object_fit); output[at + 5] = static_cast<std::uint8_t>(p.image_rendering);
        if (options.version >= kVersionV2) {
            const std::uint16_t clip_index = clip_for_command(index);
            output[at + 6] = static_cast<std::uint8_t>(clip_index & 0xffU);
            output[at + 7] = static_cast<std::uint8_t>(clip_index >> 8U);
        }
        put_int(output, at + 8, c.rect.x); put_int(output, at + 12, c.rect.y);
        put_int(output, at + 16, c.rect.width); put_int(output, at + 20, c.rect.height);
        output[at + 24] = c.color.r; output[at + 25] = c.color.g; output[at + 26] = c.color.b; output[at + 27] = c.color.a;
        output[at + 28] = c.color2.r; output[at + 29] = c.color2.g; output[at + 30] = c.color2.b; output[at + 31] = c.color2.a;
        put_int(output, at + 32, p.border_radius); put_int(output, at + 36, p.stroke_width);
        put_int(output, at + 40, p.font_size); put_int(output, at + 44, p.font_weight);
        put_u32(output, at + 48, p.font_family_hash); put_int(output, at + 52, p.gradient_stop_percent);
        put_u32(output, at + 56, p.image_handle); put_int(output, at + 60, p.object_position.x_percent);
        put_int(output, at + 64, p.object_position.y_percent); put_u32(output, at + 68, static_cast<std::uint32_t>(c.text.size()));
        if (options.version >= kVersionV3) {
            output[at + 72] = c.transform.enabled ? 1 : 0;
            put_int(output, at + 76, c.transform.xx_1024);
            put_int(output, at + 80, c.transform.xy_1024);
            put_int(output, at + 84, c.transform.yx_1024);
            put_int(output, at + 88, c.transform.yy_1024);
            put_int(output, at + 92, c.transform.tx_1024);
            put_int(output, at + 96, c.transform.ty_1024);
            if (options.version >= kVersionV4) {
                output[at + 100] = static_cast<std::uint8_t>(c.transform.source_clip_index & 0xffU);
                output[at + 101] = static_cast<std::uint8_t>(c.transform.source_clip_index >> 8U);
            }
            if (options.version >= kVersionV5) {
                const Rect sampling = detail::command_sampling_rect(c);
                put_int(output, at + 104, sampling.x);
                put_int(output, at + 108, sampling.y);
                put_int(output, at + 112, sampling.width);
                put_int(output, at + 116, sampling.height);
            }
            if (options.version >= kVersionV8) {
                const auto clip = detail::command_background_paint_clip(c);
                const auto dx = static_cast<std::uint16_t>(static_cast<std::int16_t>(clip.dx));
                const auto dy = static_cast<std::uint16_t>(static_cast<std::int16_t>(clip.dy));
                const auto width = static_cast<std::uint16_t>(clip.width);
                const auto height = static_cast<std::uint16_t>(clip.height);
                output[at + 120] = static_cast<std::uint8_t>(dx);
                output[at + 121] = static_cast<std::uint8_t>(dx >> 8U);
                output[at + 122] = static_cast<std::uint8_t>(dy);
                output[at + 123] = static_cast<std::uint8_t>(dy >> 8U);
                output[at + 124] = static_cast<std::uint8_t>(width);
                output[at + 125] = static_cast<std::uint8_t>(width >> 8U);
                output[at + 126] = static_cast<std::uint8_t>(height);
                output[at + 127] = static_cast<std::uint8_t>(height >> 8U);
            }
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
    if (options.version == 9 || (input.size() > 4 && input[4] == 9)) {
        if (input.size() > 4 && options.version != input[4]) return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
        return detail::canonical::decode(input, options, output);
    }
    if (input.size() < kHeaderBytesV1 || get_u32(input, 0) != kMagic ||
        input[5] != 0 || input[6] != 0 || input[7] != 0 || input.size() > options.max_payload_bytes) {
        return ScriptTaskAppFrameCodecStatus::Malformed;
    }
    const std::uint8_t version = input[4];
    if (version != kVersionV1 && version != kVersionV2 && version != kVersionV3 && version != kVersionV4 &&
        version != kVersionV5 && version != kVersionV6 && version != kVersionV7 && version != kVersionV8) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    }
    if (options.version != version) return ScriptTaskAppFrameCodecStatus::UnsupportedVersion;
    if (version >= kVersionV7 && !JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API)
        return ScriptTaskAppFrameCodecStatus::UnsupportedTextFeature;
    if (version == kVersionV8 && !script_task_app_frame_codec_supports_version(kVersionV8))
        return ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature;
    if (version >= kVersionV5 && !detail::kScriptTaskSamplingTransportAvailable) {
        return ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature;
    }
    if (version >= kVersionV6 && !detail::kScriptTaskGroupTransportAvailable)
        return ScriptTaskAppFrameCodecStatus::UnsupportedGroupFeature;
    const std::size_t header_bytes = version >= kVersionV6 ? kHeaderBytesV6 :
        (version == kVersionV1 ? kHeaderBytesV1 : kHeaderBytesV2);
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
        std::vector<std::size_t> clip_depths(decoded.clips.size(), 0);
        for (std::size_t index = 0; index < decoded.clips.size(); ++index) {
            const std::uint32_t parent = decoded.clips[index].parent_clip;
            clip_depths[index] = parent == kScriptTaskNoParentClip
                ? 1
                : clip_depths[parent] + 1;
            if (clip_depths[index] > options.max_clip_depth) {
                return ScriptTaskAppFrameCodecStatus::TooDeepClipChain;
            }
        }
    }
    if (version >= kVersionV6) {
        const std::size_t group_count = get_u32(input, 36), ellipse_count = get_u32(input, 40);
        if (group_count > options.max_groups) return ScriptTaskAppFrameCodecStatus::TooManyGroups;
        if (ellipse_count > command_count) return ScriptTaskAppFrameCodecStatus::Malformed;
        std::size_t group_bytes = 0, ellipse_bytes = 0;
        if (!checked_multiply(group_count, kGroupBytes, group_bytes) ||
            !checked_multiply(ellipse_count, kEllipseBytes, ellipse_bytes) ||
            group_bytes > input.size() - at || ellipse_bytes > input.size() - at - group_bytes)
            return ScriptTaskAppFrameCodecStatus::Malformed;
        decoded.groups.reserve(group_count);
        for (std::size_t i = 0; i < group_count; ++i) {
            ScriptTaskFrameGroup group;
            if (!get_group(input, at, group)) return ScriptTaskAppFrameCodecStatus::Malformed;
            decoded.groups.push_back(group); at += kGroupBytes;
        }
        decoded.command_ellipses.reserve(ellipse_count);
        for (std::size_t i = 0; i < ellipse_count; ++i) {
            ScriptTaskFrameEllipse ellipse;
            ellipse.command_index = get_u32(input, at);
            for (std::size_t j = 0; j < 8; ++j) ellipse.radii[j] = get_int(input, at + 4 + j * 4);
            decoded.command_ellipses.push_back(ellipse); at += kEllipseBytes;
        }
    }
    // Validate the minimum fixed record footprint before count-based reserves.
    std::size_t minimum_commands = 0, minimum_targets = 0, minimum_records = 0;
    if (!checked_multiply(command_count, command_bytes_for(version), minimum_commands) ||
        !checked_multiply(target_count, kTargetBytes, minimum_targets) ||
        !checked_add(minimum_commands, minimum_targets, minimum_records) ||
        minimum_records > input.size() - at) return ScriptTaskAppFrameCodecStatus::Malformed;
    decoded.display_list.reserve(command_count);
    if (version >= kVersionV2 && decoded.groups.empty()) decoded.display_clip_indices.reserve(command_count);
    decoded.input_targets.reserve(target_count);
    for (std::size_t index = 0; index < command_count; ++index) {
        const std::size_t command_bytes = command_bytes_for(version);
        if (at > input.size() || input.size() - at < command_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        DisplayCommand c;
        const auto type = static_cast<DisplayCommandType>(input[at]);
        if (!transport_command_type(type, version)) return ScriptTaskAppFrameCodecStatus::Malformed;
        detail::set_command_type(c, type);
        detail::LegacyCommandPaintFields p; p.text_align = static_cast<TextCommandAlign>(input[at + 1]);
        if (input[at + 2] > 1) return ScriptTaskAppFrameCodecStatus::Malformed;
        p.text_single_line = input[at + 2] != 0; p.gradient_axis = static_cast<GradientAxis>(input[at + 3]);
        p.object_fit = static_cast<ObjectFit>(input[at + 4]); p.image_rendering = static_cast<ImageRendering>(input[at + 5]);
        const std::uint16_t clip_index = version >= kVersionV2
            ? static_cast<std::uint16_t>(input[at + 6] | (static_cast<std::uint16_t>(input[at + 7]) << 8U))
            : kScriptTaskNoClip;
        if (version == kVersionV1 && (input[at + 6] != 0 || input[at + 7] != 0)) return ScriptTaskAppFrameCodecStatus::Malformed;
        if (!valid_clip_index(clip_index, decoded.clips.size())) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.rect = {get_int(input, at + 8), get_int(input, at + 12), get_int(input, at + 16), get_int(input, at + 20)};
        c.color = {input[at + 24], input[at + 25], input[at + 26], input[at + 27]}; c.color2 = {input[at + 28], input[at + 29], input[at + 30], input[at + 31]};
        p.border_radius = get_int(input, at + 32); p.stroke_width = get_int(input, at + 36); p.font_size = get_int(input, at + 40); p.font_weight = get_int(input, at + 44);
        p.font_family_hash = get_u32(input, at + 48); p.gradient_stop_percent = get_int(input, at + 52); p.image_handle = get_u32(input, at + 56);
        p.object_position = {get_int(input, at + 60), get_int(input, at + 64)}; const std::size_t text_length = get_u32(input, at + 68);
        if (version >= kVersionV3) {
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
            if (version >= kVersionV4) {
                if (input[at + 102] != 0 || input[at + 103] != 0) return ScriptTaskAppFrameCodecStatus::Malformed;
                c.transform.source_clip_index = static_cast<std::uint16_t>(
                    input[at + 100] | (static_cast<std::uint16_t>(input[at + 101]) << 8U));
            }
            if (version >= kVersionV5) {
                const Rect sampling = {get_int(input, at + 104), get_int(input, at + 108),
                    get_int(input, at + 112), get_int(input, at + 116)};
                if (!valid_sampling_rect(sampling)) return ScriptTaskAppFrameCodecStatus::Malformed;
                detail::set_command_sampling_rect(c, sampling);
            }
            if (version >= kVersionV8) {
                const auto get_i16 = [&](std::size_t offset) {
                    const std::uint16_t value = static_cast<std::uint16_t>(input[at + offset] |
                        (static_cast<std::uint16_t>(input[at + offset + 1]) << 8U));
                    return value < 0x8000U ? static_cast<int>(value) : static_cast<int>(value) - 0x10000;
                };
                const auto get_u16 = [&](std::size_t offset) {
                    return static_cast<int>(input[at + offset] |
                        (static_cast<std::uint16_t>(input[at + offset + 1]) << 8U));
                };
                const detail::CommandBackgroundPaintClip clip{
                    get_i16(120), get_i16(122), get_u16(124), get_u16(126)};
                if (!valid_background_paint_clip(clip)) return ScriptTaskAppFrameCodecStatus::Malformed;
                detail::set_command_background_paint_clip(c, clip);
                const auto applied = detail::command_background_paint_clip(c);
                if (applied.dx != clip.dx || applied.dy != clip.dy ||
                    applied.width != clip.width || applied.height != clip.height)
                    return ScriptTaskAppFrameCodecStatus::Malformed;
            }
        }
        if (!valid_wire_fields(p)) return ScriptTaskAppFrameCodecStatus::Malformed;
        detail::apply_legacy_command_fields(c, p);
        at += command_bytes;
        if (!valid_command(c, version) || !valid_clip_index(c.transform.source_clip_index, decoded.clips.size()) ||
            text_length > input.size() - at || !checked_total(text_bytes, text_length, text_bytes) ||
            text_bytes > options.max_text_bytes) return ScriptTaskAppFrameCodecStatus::Malformed;
        c.text.assign(reinterpret_cast<const char*>(input.data() + at), text_length); at += text_length;
        if (!detail::valid_transport_text_command(c, version)) return ScriptTaskAppFrameCodecStatus::Malformed;
        if (version < kVersionV5 && detail::has_legacy_incompatible_sampling(c)) {
            return ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature;
        }
        decoded.display_list.push_back(std::move(c));
        if (version >= kVersionV2 && decoded.groups.empty()) decoded.display_clip_indices.push_back(clip_index);
        if (!decoded.groups.empty() && clip_index != kScriptTaskNoClip) return ScriptTaskAppFrameCodecStatus::Malformed;
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
    if (version >= kVersionV7 && decoded.groups.empty()) return ScriptTaskAppFrameCodecStatus::InvalidGroup;
    const auto group_status = detail::validate_frame_groups(decoded, options.max_groups, options.max_group_depth);
    if (group_status != ScriptTaskAppFrameCodecStatus::Accepted) return group_status;
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
        // A transformed layer can reference its source clip even when the
        // destination display-clip index is kNoFlattenedClip. Preserve an
        // oversized clip table as an explicit rejection instead of allowing
        // the 16-bit sentinel to drop that clip.
        frame.clip_metadata_overflow =
            flattened.clips.size() > std::numeric_limits<std::uint16_t>::max();
        frame.display_clip_indices.reserve(flattened.display_clip_indices.size());
        for (const std::uint32_t clip_index : flattened.display_clip_indices) {
            if (clip_index != kNoFlattenedClip &&
                clip_index > std::numeric_limits<std::uint16_t>::max()) {
                frame.clip_metadata_overflow = true;
            }
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

ScriptTaskAppFrameCodecStatus make_script_task_group_frame(
    const LayerNode& root, Rect viewport, const ScriptTaskAppFrameCodecOptions& limits,
    ScriptTaskAppFrame& output) {
    using Status = ScriptTaskAppFrameCodecStatus;
    if (limits.version == 9) return detail::canonical::capture(root, viewport, limits, output);
    if ((limits.version != kVersionV6 && limits.version != kVersionV7 && limits.version != kVersionV8) ||
        !script_task_app_frame_codec_supports_version(limits.version))
        return Status::UnsupportedGroupFeature;
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    if (viewport.width <= 0 || viewport.height <= 0) return Status::InvalidValue;
    ScriptTaskAppFrame captured;
    captured.viewport = viewport;
    struct Cursor { const LayerNode* node; std::uint32_t index; std::size_t next_child; };
    std::vector<Cursor> pending;
    std::size_t text_bytes = 0;
    const auto append = [&](const LayerNode& node, std::uint32_t parent) -> Status {
        if (captured.groups.size() >= limits.max_groups || captured.groups.size() >= UINT32_MAX)
            return Status::TooManyGroups;
        if (captured.display_list.size() > limits.max_commands ||
            node.display_list.size() > limits.max_commands - captured.display_list.size() ||
            captured.display_list.size() + node.display_list.size() > UINT32_MAX)
            return Status::TooManyCommands;
        ScriptTaskFrameGroup group;
        group.parent = parent;
        group.command_begin = static_cast<std::uint32_t>(captured.display_list.size());
        group.command_count = static_cast<std::uint32_t>(node.display_list.size());
        group.bounds = node.bounds; group.clip_rect = node.clip_rect;
        group.clip_border_radius = node.clip_border_radius;
        group.type = node.type; group.has_clip = node.has_clip; group.clip_axes = node.clip_axes;
        group.opacity = node.opacity; group.transform = node.transform;
        group.origin_x_percent = node.transform_origin_x_percent;
        group.origin_y_percent = node.transform_origin_y_percent;
#if JELLYFRAME_RENDER_CORE_AFFINE2D_ENABLED
        if (node.affine_transform) {
            group.has_affine = true;
            const auto& matrix = *node.affine_transform;
            group.affine = {{matrix.xx, matrix.xy, matrix.yx, matrix.yy, matrix.tx, matrix.ty}};
        }
#endif
        if (node.clip_ellipse_geometry) {
            group.has_clip_ellipse = true;
            group.clip_ellipse = detail::group_radii(*node.clip_ellipse_geometry);
        }
        if (node.ellipse_geometry) {
            if (node.ellipse_geometry->entries.size() > node.display_list.size()) return Status::InvalidGroup;
            for (const auto& ellipse : node.ellipse_geometry->entries) {
                if (ellipse.command_index >= node.display_list.size()) return Status::InvalidGroup;
                captured.command_ellipses.push_back({group.command_begin + ellipse.command_index,
                    detail::group_radii(ellipse.radii)});
            }
        }
        for (const auto& command : node.display_list) {
            if (!valid_command(command, limits.version) || !detail::valid_transport_text_command(command, limits.version) ||
                !valid_sampling_rect(detail::command_sampling_rect(command)))
                return Status::InvalidValue;
            if (!checked_add(text_bytes, command.text.size(), text_bytes) || text_bytes > limits.max_text_bytes)
                return Status::TooManyTextBytes;
            captured.display_list.push_back(command);
        }
        const auto index = static_cast<std::uint32_t>(captured.groups.size());
        captured.groups.push_back(group);
        pending.push_back({&node, index, 0});
        return Status::Accepted;
    };
    auto status = append(root, kScriptTaskNoParentClip);
    if (status != Status::Accepted) return status;
    while (!pending.empty()) {
        auto& cursor = pending.back();
        if (cursor.next_child == cursor.node->children.size()) { pending.pop_back(); continue; }
        if (pending.size() >= limits.max_group_depth) return Status::InvalidGroup;
        const auto parent = cursor.index;
        const auto* child = cursor.node->children[cursor.next_child++].get();
        if (!child) return Status::InvalidGroup;
        status = append(*child, parent);
        if (status != Status::Accepted) return status;
    }
    status = detail::validate_frame_groups(captured, limits.max_groups, limits.max_group_depth);
    if (status != Status::Accepted) return status;
    // Exact wire-size admission without allocating an extra serialized copy.
    std::size_t payload = kHeaderBytesV6, bytes = 0;
    for (const auto& count_and_size : {std::pair<std::size_t, std::size_t>{captured.groups.size(), kGroupBytes},
            {captured.command_ellipses.size(), kEllipseBytes}, {captured.display_list.size(), kCommandBytesV5}}) {
        if (!checked_multiply(count_and_size.first, count_and_size.second, bytes) ||
            !checked_add(payload, bytes, payload)) return Status::PayloadTooLarge;
    }
    if (!checked_add(payload, text_bytes, payload) || payload > limits.max_payload_bytes)
        return Status::PayloadTooLarge;
    output = std::move(captured);
    return Status::Accepted;
#else
    (void)root; (void)viewport; (void)output;
    return Status::UnsupportedGroupFeature;
#endif
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
    if (!supervisor.take_frame_packet(session, packet)) {
        return ScriptTaskAppFrameTakeStatus::NoFrame;
    }
    if (packet.kind != ScriptTaskPacketKind::FrameReady || packet.session != session) {
        return ScriptTaskAppFrameTakeStatus::UnexpectedPacket;
    }
    struct LeaseRelease {
        ScriptTaskSupervisor& supervisor;
        ScriptAppSession session;
        ScriptTaskLeaseId lease;
        ~LeaseRelease() { if (lease) (void)supervisor.release_frame(session, lease); }
    } release{supervisor, session, packet.frame_lease_id};
    try {
        std::vector<std::uint8_t> copied;
        if (supervisor.copy_frame(session, packet.frame_lease_id, copied) != ScriptTaskFrameLeaseStatus::Accepted)
            return ScriptTaskAppFrameTakeStatus::LeaseRejected;
        const auto released = supervisor.release_frame(session, packet.frame_lease_id);
        release.lease = 0;
        if (released != ScriptTaskFrameLeaseStatus::Accepted) return ScriptTaskAppFrameTakeStatus::LeaseRejected;
        if (decode_script_task_app_frame(copied, options, output) != ScriptTaskAppFrameCodecStatus::Accepted)
            return ScriptTaskAppFrameTakeStatus::DecodeRejected;
    } catch (const std::bad_alloc&) {
        return ScriptTaskAppFrameTakeStatus::ResourceRejected;
    }
    if (accepted_packet_sequence != nullptr) {
        *accepted_packet_sequence = packet.sequence;
    }
    return ScriptTaskAppFrameTakeStatus::Accepted;
}
#if JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
ScriptAppSession begin_script_task_app_session(const AppRuntimeHost& host, ScriptTaskSupervisor& supervisor) {
    const auto instance = host.current_app_instance_id();
    const auto font_instance = host.fonts().app_instance_id();
    if (!instance || (font_instance && font_instance != instance)) return {};
    return supervisor.begin(instance, host.fonts().catalog());
}
#endif

#if JELLYFRAME_SCRIPT_TASK_TEXT_RESOURCE_API
bool script_task_frame_uses_image_provider(const ScriptTaskAppFrame& frame, const RuntimeImageProvider::Handle& images) {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
    bool found = false;
    for (const auto& c : frame.display_list) {
        if (c.type() != DisplayCommandType::Image) continue;
        found = true;
        if (!images || images->id() != frame.image_provider_id || !images->contains(c.image_paint().image_handle)) return false;
    }
    return found || frame.image_provider_id == 0;
#else
    (void)images; return frame.image_provider_id == 0;
#endif
}

bool script_task_frame_uses_catalog(const ScriptTaskAppFrame& frame, const RuntimeFontCatalog& catalog) {
    if (!catalog.text_system().ready()) return false;
    std::uint64_t last_known_resource = 0;
    for (const auto& command : frame.display_list) {
#if JELLYFRAME_SCRIPT_TASK_CANONICAL_ABI4_API
        if (!detail::canonical::valid_command(command)) return false;
        if (command.type() != DisplayCommandType::PreparedText) continue;
#else
        if (!detail::valid_prepared_text_command(command)) return false;
        if (!detail::has_prepared_text_packet(command)) continue;
#endif
        if (frame.groups.empty()) return false;
        PreparedTextView view;
        if (!decode_prepared_text(command.text, view)) return false;
        for (std::size_t i = 0; i < view.size(); ++i) {
            PreparedTextGlyph glyph;
            if (!view.read(i, glyph)) return false;
            if (glyph.resource_id == last_known_resource) continue;
            bool found = false;
            for (std::size_t face = 0; face < catalog.size(); ++face)
                found = found || catalog.resource_id(face) == glyph.resource_id;
            if (!found) return false;
            last_known_resource = glyph.resource_id;
        }
    }
    return true;
}

ScriptTaskAppFrameTakeStatus take_script_task_app_frame(
    ScriptTaskSupervisor& supervisor, const ScriptAppSession& session,
    const ScriptTaskAppFrameCodecOptions& options, ScriptTaskAcceptedAppFrame& output) {
    if (!((options.version == 7 && JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API) ||
          (options.version == 9 && detail::canonical::available)))
        return ScriptTaskAppFrameTakeStatus::TextCatalogRejected;
    auto catalog = supervisor.text_catalog(session);
    if (!catalog) return ScriptTaskAppFrameTakeStatus::TextCatalogRejected;
    ScriptTaskAcceptedAppFrame accepted;
    const auto status = take_script_task_app_frame(supervisor, session, options, accepted.frame_, &accepted.sequence_);
    if (status != ScriptTaskAppFrameTakeStatus::Accepted) return status;
    if (!script_task_frame_uses_catalog(accepted.frame_, *catalog))
        return ScriptTaskAppFrameTakeStatus::TextCatalogRejected;
    if (options.version == 9) {
        accepted.images_ = supervisor.image_provider(session);
        if (!script_task_frame_uses_image_provider(accepted.frame_, accepted.images_))
            return ScriptTaskAppFrameTakeStatus::ImageProviderRejected;
    }
    accepted.session_ = session;
    accepted.catalog_ = std::move(catalog);
    output = std::move(accepted);
    return ScriptTaskAppFrameTakeStatus::Accepted;
}
#endif

} // namespace jellyframe
