#include "app_runtime/script_task_command_access.h"
#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_contract.h"
#include "app_runtime/script_task_sampling.h"
#include "app_runtime/script_task_frame_groups.h"
#include "app_runtime/script_task_prepared_text.h"
#include "script_task_canonical_codec_tests.h"

#include <cassert>
#include <iostream>
#include <limits>

#if __has_include("render_core/fractional_sampling.h")
#include "render_core/fractional_sampling.h"
#define JELLYFRAME_TEST_HAS_FRACTIONAL_SAMPLING 1
#endif

using namespace jellyframe;

namespace {

ScriptTaskAppFrameCodecOptions limits() { return {4, 32, 3, 512}; }

ScriptTaskAppFrameCodecOptions v2_limits() {
    ScriptTaskAppFrameCodecOptions result = limits();
    result.version = 2;
    result.max_clips = 4;
    result.max_clip_depth = 4;
    return result;
}

ScriptTaskAppFrameCodecOptions v3_limits() {
    ScriptTaskAppFrameCodecOptions result = v2_limits();
    result.version = 3;
    return result;
}

ScriptTaskAppFrameCodecOptions v4_limits() {
    ScriptTaskAppFrameCodecOptions result = v3_limits();
    result.version = 4;
    return result;
}

ScriptTaskAppFrameCodecOptions v5_limits() {
    ScriptTaskAppFrameCodecOptions result = v4_limits();
    result.version = 5;
    return result;
}

ScriptTaskAppFrame fixture() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 172, 320};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::LinearGradient);
    fill.rect = {1, 2, 30, 40};
    fill.color = {1, 2, 3, 4};
    fill.color2 = {5, 6, 7, 8};
    jellyframe::detail::command_gradient_axis(fill) = GradientAxis::DiagonalDownRight;
    jellyframe::detail::command_gradient_stop_percent(fill) = 57;
    jellyframe::detail::command_border_radius(fill) = 9;
    frame.display_list.push_back(fill);
    DisplayCommand text;
    jellyframe::detail::set_command_type(text, DisplayCommandType::Text);
    text.rect = {4, 5, 80, 16};
    text.text = "Hi \xE4\xB8\x96\xE7\x95\x8C";
    jellyframe::detail::command_font_size(text) = 13;
    jellyframe::detail::command_font_weight(text) = 600;
    jellyframe::detail::command_font_family_hash(text) = 44;
    jellyframe::detail::command_text_align(text) = TextCommandAlign::Center;
    jellyframe::detail::command_text_single_line(text) = false;
    frame.display_list.push_back(text);
    frame.input_targets = {{1, {2, 2, 30, 30}, true}, {2, {10, 10, 30, 30}, true}, {3, {60, 10, 20, 20}, false}};
    return frame;
}

void destroy_deep_layer_chain(LayerNode& root) {
    if (root.children.empty()) {
        return;
    }
    LayerNodePtr current = std::move(root.children.front());
    root.children.clear();
    while (current != nullptr) {
        LayerNode& node = *current;
        LayerNodePtr next;
        if (!node.children.empty()) {
            next = std::move(node.children.front());
            node.children.clear();
        }
        current.reset();
        current = std::move(next);
    }
}

void frame_round_trip_preserves_render_values_and_target_order() {
    const ScriptTaskAppFrame expected = fixture();
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(expected, limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.viewport.width == 172);
    assert(decoded.display_list.size() == 2);
    assert(jellyframe::detail::command_type(decoded.display_list[0]) == DisplayCommandType::LinearGradient);
    assert(jellyframe::detail::command_gradient_axis(decoded.display_list[0]) == GradientAxis::DiagonalDownRight);
    assert(decoded.display_list[0].color2.a == 8);
    assert(decoded.display_list[1].text == expected.display_list[1].text);
    assert(jellyframe::detail::command_font_family_hash(decoded.display_list[1]) == 44);
    assert(resolve_script_task_input_target(decoded, 15, 15) == 2);
    assert(resolve_script_task_input_target(decoded, 3, 3) == 1);
    assert(resolve_script_task_input_target(decoded, 65, 15) == 0);
    assert(resolve_script_task_input_target(decoded, 150, 150) == 0);
}

void frame_codec_rejects_budget_and_wire_integrity_failures() {
    ScriptTaskAppFrame frame = fixture();
    std::vector<std::uint8_t> bytes;
    ScriptTaskAppFrameCodecOptions too_small = limits();
    too_small.max_text_bytes = 1;
    assert(encode_script_task_app_frame(frame, too_small, bytes) == ScriptTaskAppFrameCodecStatus::TooManyTextBytes);
    frame.input_targets.push_back({2, {0, 0, 1, 1}, true});
    assert(encode_script_task_app_frame(frame, limits(), bytes) == ScriptTaskAppFrameCodecStatus::TooManyInputTargets);
    frame.input_targets.pop_back();
    assert(encode_script_task_app_frame(frame, limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    bytes[0] = 0;
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, limits(), decoded) == ScriptTaskAppFrameCodecStatus::Malformed);
}

void v2_frame_round_trip_preserves_nested_clips_and_target_references() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 64};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {0, 0, 64, 64};
    fill.color = {20, 120, 240, 255};
    frame.display_list.push_back(fill);
    frame.clips = {
        {{4, 4, 56, 56}, 12, kScriptTaskNoParentClip},
        {{12, 12, 40, 40}, 8, 0},
    };
    frame.display_clip_indices = {1};
    frame.input_targets = {{7, {0, 0, 64, 64}, true, 1}};

    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v2_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, v2_limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.clips.size() == 2);
    assert(decoded.clips[0].border_radius == 12);
    assert(decoded.clips[1].parent_clip == 0);
    assert(decoded.display_clip_indices.size() == 1 && decoded.display_clip_indices[0] == 1);
    assert(decoded.input_targets[0].clip_index == 1);
    assert(resolve_script_task_input_target(decoded, 32, 32) == 7);
    assert(resolve_script_task_input_target(decoded, 4, 4) == 0);
}

void v2_frame_rejects_invalid_clip_chain_and_v1_clip_downgrade() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 32, 32};
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {0, 0, 32, 32};
    fill.color = {1, 2, 3, 255};
    frame.display_list.push_back(fill);
    frame.clips = {{{0, 0, 32, 32}, 8, kScriptTaskNoParentClip}};
    frame.display_clip_indices = {0};

    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, limits(), bytes) == ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature);
    assert(encode_script_task_app_frame(frame, v2_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    bytes[36 + 20] = 0;
    bytes[36 + 21] = 0;
    bytes[36 + 22] = 0;
    bytes[36 + 23] = 0;
    assert(decode_script_task_app_frame(bytes, v2_limits(), frame) == ScriptTaskAppFrameCodecStatus::Malformed);
}

void v3_frame_round_trip_preserves_fixed_point_command_transform() {
    ScriptTaskAppFrame frame = fixture();
    DisplayCommandTransform& transform = frame.display_list.front().transform;
    transform.enabled = true;
    transform.xx_1024 = 0;
    transform.xy_1024 = -1024;
    transform.yx_1024 = 1024;
    transform.yy_1024 = 0;
    transform.tx_1024 = 32 * 1024;
    transform.ty_1024 = 0;

    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v2_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedTransformFeature);
    assert(encode_script_task_app_frame(frame, v3_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, v3_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    const DisplayCommandTransform& copied = decoded.display_list.front().transform;
    assert(copied.enabled && copied.xy_1024 == -1024 && copied.yx_1024 == 1024 &&
           copied.tx_1024 == 32 * 1024);

    bytes[36 + 72] = 2;
    assert(decode_script_task_app_frame(bytes, v3_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::Malformed);

    frame.display_list.front().transform = {};
    frame.display_list.front().transform.tx_1024 = 1;
    assert(encode_script_task_app_frame(frame, v3_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::InvalidValue);

    frame.display_list.front().transform = {};
    assert(encode_script_task_app_frame(frame, v3_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    bytes[36 + 92] = 1;
    assert(decode_script_task_app_frame(bytes, v3_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::Malformed);
}

void v4_frame_round_trip_preserves_transform_source_clip() {
    ScriptTaskAppFrame frame = fixture();
    frame.clips = {{{2, 2, 24, 24}, 6, kScriptTaskNoParentClip}};
    frame.display_clip_indices.assign(frame.display_list.size(), 0);
    DisplayCommandTransform& transform = frame.display_list.front().transform;
    transform.enabled = true;
    transform.xx_1024 = 0;
    transform.xy_1024 = -1024;
    transform.yx_1024 = 1024;
    transform.yy_1024 = 0;
    transform.tx_1024 = 32 * 1024;
    transform.source_clip_index = 0;

    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v3_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedTransformFeature);
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, v4_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.display_list.front().transform.source_clip_index == 0);

    bytes[36 + 102] = 1;
    assert(decode_script_task_app_frame(bytes, v4_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::Malformed);
}

void v5_frame_round_trip_preserves_sampling_geometry_and_opaque_packet() {
    if (!detail::kScriptTaskSamplingTransportAvailable) {
        std::vector<std::uint8_t> unchanged{17, 23};
        assert(encode_script_task_app_frame(fixture(), v5_limits(), unchanged) ==
               ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);
        assert((unchanged == std::vector<std::uint8_t>{17, 23}));
        return;
    }
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 32};
    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {4, 2, 20, 16};
    detail::set_command_sampling_rect(image, {3, 2, 40, 16});
    jellyframe::detail::command_image_handle(image) = 9;
    image.text.assign("JFFS\0fractional", 15);
    frame.display_list.push_back(image);

    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);
    image.text.clear();
    frame.display_list.front() = image;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);
    image.text.assign("JFFS\0fractional", 15);
    detail::set_command_sampling_rect(image, {});
    frame.display_list.front() = image;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);
    detail::set_command_sampling_rect(image, {3, 2, 40, 16});
    frame.display_list.front() = image;
    assert(encode_script_task_app_frame(frame, v5_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, v5_limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.display_list.size() == 1);
    assert(detail::equal_sampling_rects(detail::command_sampling_rect(decoded.display_list.front()),
                                      {3, 2, 40, 16}));
    assert(decoded.display_list.front().text == image.text);

    detail::set_command_sampling_rect(image, {});
    image.text.clear();
    frame.display_list.front() = image;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    bytes[4] = 5;
    ScriptTaskAppFrame unchanged = fixture();
    assert(decode_script_task_app_frame(bytes, v5_limits(), unchanged) == ScriptTaskAppFrameCodecStatus::Malformed);
    assert(unchanged.display_list.size() == 2);
}

void legacy_frame_versions_reject_fractional_packets() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 16, 16};
    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {0, 0, 8, 8};
    image.text.assign("JFFS\0fractional", 15);
    frame.display_list.push_back(image);

    std::vector<std::uint8_t> bytes;
    for (const std::uint8_t version : {1, 2, 3, 4}) {
        ScriptTaskAppFrameCodecOptions options = v5_limits();
        options.version = version;
        assert(encode_script_task_app_frame(frame, options, bytes) ==
               ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);
    }

    image.text.clear();
    frame.display_list.front() = image;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    constexpr std::size_t kV4TextOffset = 36 + 104;
    bytes[36 + 68] = 15;
    bytes.resize(kV4TextOffset + 15);
    const char packet[] = "JFFS\0fractional";
    for (std::size_t index = 0; index < 15; ++index) {
        bytes[kV4TextOffset + index] = static_cast<std::uint8_t>(packet[index]);
    }
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, v4_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedSamplingFeature);

    // A text node containing the packet prefix is ordinary text, not metadata.
    jellyframe::detail::set_command_type(frame.display_list.front(), DisplayCommandType::Text);
    frame.display_list.front().text.assign(packet, 15);
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decode_script_task_app_frame(bytes, v4_limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.display_list.front().text == frame.display_list.front().text);
}

void codec_capability_tracks_the_selected_core_provider() {
    assert(!script_task_app_frame_codec_supports_version(0));
    for (const std::uint8_t version : {1, 2, 3, 4}) {
        assert(script_task_app_frame_codec_supports_version(version));
    }
    assert(script_task_app_frame_codec_supports_version(5) == detail::kScriptTaskSamplingTransportAvailable);
    assert(script_task_app_frame_codec_supports_version(6) ==
        (detail::kScriptTaskSamplingTransportAvailable && detail::kScriptTaskGroupTransportAvailable));
    const bool canonical_groups = detail::kScriptTaskSamplingTransportAvailable &&
        detail::kScriptTaskGroupTransportAvailable && JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API;
    assert(script_task_app_frame_codec_supports_version(7) == canonical_groups);
    assert(script_task_app_frame_codec_supports_version(8) ==
        (canonical_groups && detail::has_background_clip_transport<DisplayCommand>()));
    std::cout << "codec capabilities: sampling=" << detail::kScriptTaskSamplingTransportAvailable
              << " groups=" << detail::kScriptTaskGroupTransportAvailable
              << " prepared-v3=" << JELLYFRAME_SCRIPT_TASK_PREPARED_TEXT_API
              << " typed-clips=" << detail::has_background_clip_transport<DisplayCommand>() << '\n';
    assert(!script_task_app_frame_codec_supports_version(255));
}

void typed_clip_adapters_preserve_each_paint_alternative() {
    if constexpr (!detail::has_background_clip_transport<DisplayCommand>()) return;
    for (const auto type : {DisplayCommandType::Image, DisplayCommandType::LinearGradient,
                            DisplayCommandType::ConicGradient, DisplayCommandType::RadialGradient}) {
        DisplayCommand command;
        detail::set_command_type(command, type);
        command.rect = {10, 20, 100, 200};
        detail::set_command_sampling_rect(command, {-10, -20, 300, 400});
        detail::set_command_background_paint_clip(command, {-32768, 32767, 65535, 1});
        const auto clip = detail::command_background_paint_clip(command);
        assert(clip.dx == -32768 && clip.dy == 32767 && clip.width == 65535 && clip.height == 1);
        assert(command.rect.x == 10 && command.rect.width == 100);
        const auto sampling = detail::command_sampling_rect(command);
        if (detail::kScriptTaskSamplingTransportAvailable)
            assert(sampling.x == -10 && sampling.width == 300);
        for (std::uint8_t version = 1; version <= 7; ++version) {
            if (!script_task_app_frame_codec_supports_version(version)) continue;
            auto options = limits(); options.version = version;
            ScriptTaskAppFrame frame; frame.viewport = {0, 0, 32, 32};
            frame.display_list.push_back(command);
            detail::set_command_sampling_rect(frame.display_list.back(), {});
            std::vector<std::uint8_t> unchanged{7, 8, 9};
            assert(encode_script_task_app_frame(frame, options, unchanged) ==
                   ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature);
            assert((unchanged == std::vector<std::uint8_t>{7, 8, 9}));
        }
    }
    std::cout << "typed clips: image and all three gradient adapters; supported legacy refusal passed\n";
}

void supported_legacy_command_records_keep_frozen_layouts() {
    ScriptTaskAppFrame frame; frame.viewport = {0, 0, 32, 32};
    DisplayCommand command;
    detail::set_command_type(command, DisplayCommandType::FillRect);
    command.rect = {1, 2, 3, 4};
    command.color = {5, 6, 7, 8}; command.color2 = {9, 10, 11, 12};
    frame.display_list.push_back(command);
    // Frozen little-endian command words, independent of the active Core's
    // typed union storage. Only v1's reserved clip bytes differ from v2+.
    const std::uint32_t words[] = {
        0x00010000, 0, 1, 2, 3, 4, 0x08070605, 0x0c0b0a09,
        0, 1, 14, 400, 0, 100, 0, 50, 50, 0,
        0, 1024, 0, 0, 1024, 0, 0, 65535, 0, 0, 0, 0};
    std::vector<std::uint8_t> golden;
    for (auto word : words)
        for (unsigned shift = 0; shift < 32; shift += 8)
            golden.push_back(static_cast<std::uint8_t>(word >> shift));
    for (std::uint8_t version = 1; version <= 7; ++version) {
        if (!script_task_app_frame_codec_supports_version(version)) {
            std::cout << "legacy v" << unsigned(version) << " byte qualification unavailable on selected Core\n";
            continue;
        }
        auto options = limits(); options.version = version; options.max_groups = 1;
        auto version_frame = frame;
        if (version >= 6) {
            ScriptTaskFrameGroup group; group.bounds = frame.viewport; group.command_count = 1;
            version_frame.groups.push_back(group);
        }
        const std::size_t header = version == 1 ? 32 : version < 6 ? 36 : 44;
        const std::size_t command_at = header + (version >= 6 ? 152 : 0);
        const std::size_t record = version <= 2 ? 72 : version == 3 ? 100 : version == 4 ? 104 : 120;
        std::vector<std::uint8_t> bytes;
        const auto encoded = encode_script_task_app_frame(version_frame, options, bytes);
        if (encoded != ScriptTaskAppFrameCodecStatus::Accepted)
            std::cerr << "legacy golden v" << unsigned(version) << " refused status=" << int(encoded) << '\n';
        assert(encoded == ScriptTaskAppFrameCodecStatus::Accepted);
        assert(bytes.size() == command_at + record);
        auto expected = golden;
        if (version >= 2) expected[6] = expected[7] = 255;
        assert(std::equal(expected.begin(), expected.begin() + record, bytes.begin() + command_at));
        ScriptTaskAppFrame decoded;
        assert(decode_script_task_app_frame(bytes, options, decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
        std::vector<std::uint8_t> reencoded;
        assert(encode_script_task_app_frame(decoded, options, reencoded) == ScriptTaskAppFrameCodecStatus::Accepted);
        assert(reencoded == bytes);
    }
}

void v5_sampling_geometry_validation_is_atomic() {
    if (!detail::kScriptTaskSamplingTransportAvailable) return;
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 16, 16};
    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {0, 0, 8, 8};
    detail::set_command_sampling_rect(image, {-7, 9, -1, 8});
    frame.display_list.push_back(image);
    std::vector<std::uint8_t> bytes{17, 23};
    assert(encode_script_task_app_frame(frame, v5_limits(), bytes) == ScriptTaskAppFrameCodecStatus::InvalidValue);
    assert((bytes == std::vector<std::uint8_t>{17, 23}));
    detail::set_command_sampling_rect(frame.display_list.front(), {-7, 9, 32, 12});
    assert(encode_script_task_app_frame(frame, v5_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(bytes.size() == 36 + 120);
    assert(bytes[36 + 104] == 0xf9 && bytes[36 + 107] == 0xff);
    assert(bytes[36 + 108] == 9 && bytes[36 + 112] == 32 && bytes[36 + 116] == 12);

    const auto valid = bytes;
    for (const std::size_t dimension : {112U, 116U}) {
        bytes = valid;
        for (std::size_t index = 0; index < 4; ++index) bytes[36 + dimension + index] = 0xff;
        ScriptTaskAppFrame decoded = fixture();
        assert(decode_script_task_app_frame(bytes, v5_limits(), decoded) == ScriptTaskAppFrameCodecStatus::Malformed);
        assert(decoded.display_list.size() == 2 && decoded.viewport.width == 172);
    }
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(valid, v4_limits(), decoded) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedVersion);
    auto exact_budget = v5_limits();
    exact_budget.max_payload_bytes = valid.size();
    assert(encode_script_task_app_frame(frame, exact_budget, bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    --exact_budget.max_payload_bytes;
    assert(encode_script_task_app_frame(frame, exact_budget, bytes) == ScriptTaskAppFrameCodecStatus::PayloadTooLarge);
    assert(bytes == valid);
}

void v5_transports_the_current_core_packet() {
#ifdef JELLYFRAME_TEST_HAS_FRACTIONAL_SAMPLING
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 64, 32};
    frame.clips = {{{0, 0, 32, 16}, 3, kScriptTaskNoParentClip}};
    frame.display_clip_indices = {0};
    DisplayCommand image;
    jellyframe::detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {2, 3, 12, 8};
    image.sampling_rect = {-2, 3, 16, 8};
    image.transform.enabled = true;
    image.transform.tx_1024 = 7 * 1024;
    image.transform.source_clip_index = 0;
    const FractionalSampling sampling{{1, 3}, {-1, 7}, {47, 3}, {57, 7}};
    assert(encode_fractional_sampling(sampling, image.text));
    frame.display_list.push_back(image);
    auto options = v5_limits();
    options.max_text_bytes = kFractionalSamplingPacketBytes;
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, options, bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, options, decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    const auto& actual = decoded.display_list.front();
    assert(actual.text == image.text && actual.transform.source_clip_index == 0 &&
           actual.transform.tx_1024 == image.transform.tx_1024 && decoded.display_clip_indices.front() == 0);
    assert(detail::equal_sampling_rects(actual.sampling_rect, image.sampling_rect));
    FractionalSampling unpacked;
    assert(decode_fractional_sampling(actual.text, unpacked));
    assert(unpacked.x.numerator == 1 && unpacked.x.denominator == 3 &&
           unpacked.y.numerator == -1 && unpacked.y.denominator == 7 &&
           unpacked.width.numerator == 47 && unpacked.width.denominator == 3 &&
           unpacked.height.numerator == 57 && unpacked.height.denominator == 7);
#endif
}

void frame_codec_rejects_unrepresentable_clip_metadata() {
    ScriptTaskAppFrame frame = fixture();
    frame.clip_metadata_overflow = true;
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::InvalidClip);
}

void worker_frame_producer_rejects_oversized_generated_clip_table() {
    LayerNode root;
    root.type = LayerType::Root;
    LayerNode* parent = &root;
    for (std::size_t index = 0; index <= std::numeric_limits<std::uint16_t>::max(); ++index) {
        LayerNodePtr clipped(new LayerNode(), LayerNodeDeleter{});
        clipped->type = LayerType::Clip;
        clipped->has_clip = true;
        clipped->clip_rect = {-1000000, -1000000, 2000000, 2000000};
        clipped->transform.translate_x = 1.0F;
        clipped->has_transform = true;
        LayerNode* next = clipped.get();
        parent->children.push_back(std::move(clipped));
        parent = next;
    }
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {0, 0, 1, 1};
    fill.color = {1, 2, 3, 255};
    parent->display_list.push_back(fill);

    const ScriptTaskAppFrame frame = make_script_task_app_frame(root, {0, 0, 16, 16}, {}, true);
    assert(frame.clip_metadata_overflow);
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v4_limits(), bytes) ==
           ScriptTaskAppFrameCodecStatus::InvalidClip);
    // The deliberately deep fixture must be dismantled iteratively; default
    // unique_ptr destruction would recurse once per clip node on small stacks.
    destroy_deep_layer_chain(root);
}

void sealed_lease_carries_only_serialized_frame_bytes() {
    const ScriptTaskAppFrame frame = fixture();
    ScriptTaskSupervisor supervisor({{2, 24}, {1, 0}, {1, 512, 512}, 0, 0});
    const ScriptAppSession session = supervisor.begin(71);
    ScriptTaskAppFramePublisher publisher(limits());
    assert(publisher.publish(supervisor, session, frame).accepted());
    ScriptTaskAppFrame decoded;
    assert(take_script_task_app_frame(supervisor, session, limits(), decoded) == ScriptTaskAppFrameTakeStatus::Accepted);
    assert(decoded.display_list[1].text == frame.display_list[1].text);
    assert(take_script_task_app_frame(supervisor, session, limits(), decoded) == ScriptTaskAppFrameTakeStatus::NoFrame);
}

void malformed_frame_releases_its_lease_before_reporting_decode_failure() {
    ScriptTaskSupervisor supervisor({{2, 24}, {1, 0}, {1, 512, 512}, 0, 0});
    const ScriptAppSession session = supervisor.begin(72);
    std::vector<std::uint8_t> malformed;
    assert(encode_script_task_app_frame(fixture(), limits(), malformed) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    malformed[0] = 0;
    assert(supervisor.publish_frame(session, malformed).accepted());

    ScriptTaskAppFrame output;
    output.viewport = {1, 2, 3, 4};
    std::uint32_t accepted_sequence = 99;
    assert(take_script_task_app_frame(supervisor, session, limits(), output, &accepted_sequence) ==
           ScriptTaskAppFrameTakeStatus::DecodeRejected);
    assert(output.viewport.x == 1 && output.viewport.y == 2 && output.viewport.width == 3 &&
           output.viewport.height == 4);
    assert(accepted_sequence == 99);

    // A capacity-one registry can accept another frame only when the malformed
    // lease was released before the decoder reported its failure.
    assert(supervisor.publish_frame(session, malformed).accepted());
}

void stale_frame_consumer_cannot_take_new_session_frame() {
    ScriptTaskSupervisor supervisor({{2, 24}, {1, 0}, {1, 512, 512}, 0, 0});
    const ScriptAppSession retired = supervisor.begin(73);
    assert(supervisor.begin_teardown(retired).session == retired);
    assert(supervisor.complete_teardown(retired).session == retired);

    const ScriptAppSession active = supervisor.begin(74);
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_app_frame(fixture(), limits(), encoded) ==
           ScriptTaskAppFrameCodecStatus::Accepted);
    assert(supervisor.publish_frame(active, encoded).accepted());

    ScriptTaskAppFrame output;
    assert(take_script_task_app_frame(supervisor, retired, limits(), output) ==
           ScriptTaskAppFrameTakeStatus::NoFrame);
    assert(take_script_task_app_frame(supervisor, active, limits(), output) ==
           ScriptTaskAppFrameTakeStatus::Accepted);
}

void worker_frame_producer_flattens_private_layer_values_before_publish() {
    LayerNode root;
    root.type = LayerType::Root;
    DisplayCommand background;
    jellyframe::detail::set_command_type(background, DisplayCommandType::FillRect);
    background.rect = {0, 0, 20, 20};
    background.color = {1, 2, 3, 255};
    root.display_list.push_back(background);
    LayerNodePtr child(new LayerNode(), LayerNodeDeleter{});
    child->type = LayerType::Composited;
    child->transform.translate_x = 7.0F;
    child->transform.translate_y = 3.0F;
    child->opacity = 0.5F;
    DisplayCommand foreground;
    jellyframe::detail::set_command_type(foreground, DisplayCommandType::FillRect);
    foreground.rect = {1, 2, 4, 5};
    foreground.color = {20, 30, 40, 200};
    child->display_list.push_back(foreground);
    root.children.push_back(std::move(child));

    const ScriptTaskAppFrame frame = make_script_task_app_frame(root, {0, 0, 172, 320}, {{7, {1, 1, 2, 2}, true}});
    assert(frame.display_list.size() == 2);
    assert(frame.display_list[1].rect.x == 8);
    assert(frame.display_list[1].rect.y == 5);
    assert(frame.display_list[1].color.a == 100);
    assert(frame.input_targets[0].target_key == 7);
}

void worker_frame_producer_exports_clips_only_for_v2() {
    LayerNode root;
    LayerNodePtr clipped(new LayerNode(), LayerNodeDeleter{});
    clipped->type = LayerType::Clip;
    clipped->has_clip = true;
    clipped->clip_rect = {4, 6, 40, 24};
    clipped->clip_border_radius = 8;
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = {4, 6, 12, 12};
    fill.color = {10, 20, 30, 255};
    clipped->display_list.push_back(fill);
    root.children.push_back(std::move(clipped));

    const ScriptTaskAppFrame v1 = make_script_task_app_frame(root, {0, 0, 64, 64});
    assert(v1.clips.empty() && v1.display_clip_indices.empty());

    const ScriptTaskAppFrame v2 = make_script_task_app_frame(root, {0, 0, 64, 64}, {}, true);
    assert(v2.clips.size() == 1);
    assert(v2.clips[0].rect.x == 4 && v2.clips[0].rect.y == 6);
    assert(v2.clips[0].border_radius == 8);
    assert(v2.display_clip_indices.size() == v2.display_list.size());
    assert(v2.display_clip_indices.size() == 1 && v2.display_clip_indices[0] == 0);
}

void worker_frame_producer_preserves_layer_rotation_for_v3_consumers() {
    LayerNode root;
    root.type = LayerType::Root;
    root.bounds = {0, 0, 32, 32};
    LayerNodePtr hand(new LayerNode(), LayerNodeDeleter{});
    hand->type = LayerType::Composited;
    hand->bounds = {15, 5, 2, 12};
    hand->transform.rotate_degrees = 90.0F;
    hand->transform_origin_x_percent = 50;
    hand->transform_origin_y_percent = 100;
    DisplayCommand fill;
    jellyframe::detail::set_command_type(fill, DisplayCommandType::FillRect);
    fill.rect = hand->bounds;
    fill.color = {240, 80, 90, 255};
    hand->display_list.push_back(fill);
    root.children.push_back(std::move(hand));

    const ScriptTaskAppFrame frame = make_script_task_app_frame(root, {0, 0, 32, 32}, {}, true);
    assert(frame.display_list.size() == 1 && frame.display_list.front().transform.enabled);
    assert(frame.display_list.front().transform.xy_1024 != 0 || frame.display_list.front().transform.yx_1024 != 0);
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, v3_limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
}

void v6_group_codec_preserves_values_and_rejects_malformed_topology() {
    using Status = ScriptTaskAppFrameCodecStatus;
    ScriptTaskAppFrameCodecOptions options;
    options.version = 6; options.max_commands = 8; options.max_groups = 8;
    options.max_text_bytes = 128; options.max_payload_bytes = 8192;
    LayerNode root;
    root.bounds = {0, 0, 32, 32};
    ScriptTaskAppFrame frame;
    if (!script_task_app_frame_codec_supports_version(6)) {
        assert(make_script_task_group_frame(root, root.bounds, options, frame) == Status::UnsupportedGroupFeature);
        return;
    }
    root.opacity = 0.375F;
    root.transform.rotate_degrees = 23.125F;
    DisplayCommand command;
    command.rect = {2, 3, 17, 19}; command.color = {10, 20, 30, 123};
    command.text.assign("opaque\0bytes", 12);
    root.display_list.push_back(command);
    assert(make_script_task_group_frame(root, root.bounds, options, frame) == Status::Accepted);
    assert(frame.groups.size() == 1 && frame.display_list[0].color.a == 123);
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(frame, options, bytes) == Status::Accepted);
    assert(bytes.size() == 44 + 152 + 120 + command.text.size());
    assert(bytes[4] == 6 && bytes[36] == 1 && bytes[40] == 0);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, options, decoded) == Status::Accepted);
    assert(decoded.groups[0].opacity == root.opacity);
    assert(decoded.groups[0].transform.rotate_degrees == root.transform.rotate_degrees);
    assert(decoded.display_list[0].text == command.text);
    assert(decoded.display_list[0].color.a == 123);
#if JELLYFRAME_SCRIPT_TASK_GROUP_API
    for (const auto type : {DisplayCommandType::BaselineText, DisplayCommandType::OriginText,
                           DisplayCommandType::BorderRing, DisplayCommandType::GlyphRun}) {
        auto extended = frame;
        jellyframe::detail::set_command_type(extended.display_list[0], type);
        jellyframe::detail::command_stroke_width(extended.display_list[0]) = -5;
        jellyframe::detail::command_object_position(extended.display_list[0]) = {-123, 456};
        std::vector<std::uint8_t> extended_bytes;
        assert(encode_script_task_app_frame(extended, options, extended_bytes) == Status::Accepted);
        ScriptTaskAppFrame extended_decoded;
        assert(decode_script_task_app_frame(extended_bytes, options, extended_decoded) == Status::Accepted);
        assert(jellyframe::detail::command_type(extended_decoded.display_list[0]) == type);
        assert(jellyframe::detail::command_stroke_width(extended_decoded.display_list[0]) == -5);
        assert(jellyframe::detail::command_object_position(extended_decoded.display_list[0]).x_percent == -123);
        assert(extended_decoded.display_list[0].text == command.text);
    }
#endif
    auto downgraded = options; downgraded.version = 5;
    std::vector<std::uint8_t> sentinel{9, 8, 7};
    assert(encode_script_task_app_frame(frame, downgraded, sentinel) == Status::UnsupportedGroupFeature);
    assert((sentinel == std::vector<std::uint8_t>{9, 8, 7}));
    auto limited = options; limited.max_payload_bytes = bytes.size();
    assert(make_script_task_group_frame(root, root.bounds, limited, decoded) == Status::Accepted);
    --limited.max_payload_bytes;
    decoded.viewport = {1, 2, 3, 4};
    assert(make_script_task_group_frame(root, root.bounds, limited, decoded) == Status::PayloadTooLarge);
    assert(decoded.viewport.x == 1 && decoded.viewport.width == 3);
    for (std::size_t length = 0; length < bytes.size(); ++length) {
        auto truncated = bytes; truncated.resize(length);
        assert(decode_script_task_app_frame(truncated, options, decoded) != Status::Accepted);
        assert(decoded.viewport.x == 1 && decoded.viewport.width == 3);
    }
    // Parent, command coverage, enum, axes, nonfinite opacity and reserved word.
    for (const std::size_t offset : {48U, 52U, 92U, 96U, 100U, 104U, 192U}) {
        auto corrupt = bytes;
        for (std::size_t i = 0; i < 4; ++i) corrupt[offset + i] = 0xff;
        assert(decode_script_task_app_frame(corrupt, options, decoded) != Status::Accepted);
        assert(decoded.viewport.x == 1);
    }
    auto root_cycle = bytes; root_cycle[44] = 0;
    assert(decode_script_task_app_frame(root_cycle, options, decoded) == Status::InvalidGroup);
    // A closed subtree cannot be reopened even though every parent precedes its child.
    auto malformed = frame;
    malformed.groups[0].command_count = 0;
    malformed.groups.push_back({}); malformed.groups.back().parent = 0;
    malformed.groups.push_back({}); malformed.groups.back().parent = 0;
    malformed.groups.push_back({}); malformed.groups.back().parent = 1;
    malformed.groups.back().command_count = 1;
    assert(encode_script_task_app_frame(malformed, options, sentinel) == Status::InvalidGroup);
    limited = options; limited.max_groups = 0;
    assert(encode_script_task_app_frame(frame, limited, sentinel) == Status::TooManyGroups);
    assert(decode_script_task_app_frame(bytes, limited, decoded) == Status::TooManyGroups);
    limited = options; limited.max_group_depth = 0;
    assert(encode_script_task_app_frame(frame, limited, sentinel) == Status::InvalidGroup);
    std::cout << "v6 group codec: all " << bytes.size() << " truncations rejected atomically\n";
}

template<class Command> void typed_payloads_require_negotiated_transport() {
    if constexpr (detail::has_typed_command<Command>::value) {
        using Type = decltype(detail::command_type(std::declval<const Command&>()));
        auto frame = fixture();
        Command css;
        detail::set_command_type(css, Type::CssBoxShadow);
        css.rect = {0, 0, 12, 12};
        frame.display_list.push_back(css);
        std::vector<std::uint8_t> sentinel{1, 2, 3};
        assert(encode_script_task_app_frame(frame, limits(), sentinel) == ScriptTaskAppFrameCodecStatus::InvalidValue);
        assert((sentinel == std::vector<std::uint8_t>{1, 2, 3}));
        frame.display_list.pop_back();
        Command image;
        detail::set_command_type(image, Type::Image);
        image.image_paint().background_paint_clip = {1, 2, 3, 4};
        frame.display_list.push_back(image);
        assert(encode_script_task_app_frame(frame, limits(), sentinel) == ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature);
        assert((sentinel == std::vector<std::uint8_t>{1, 2, 3}));
        frame.display_list.pop_back();
        std::vector<std::uint8_t> bytes;
        assert(encode_script_task_app_frame(frame, limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
        bytes[32] = static_cast<std::uint8_t>(Type::CssBoxShadow);
        ScriptTaskAppFrame decoded;
        decoded.viewport = {9, 8, 7, 6};
        assert(decode_script_task_app_frame(bytes, limits(), decoded) == ScriptTaskAppFrameCodecStatus::Malformed);
        assert(decoded.viewport.x == 9 && decoded.viewport.width == 7);
    }
}

void v8_background_paint_clip_transport_is_opt_in_and_atomic() {
    if constexpr (!detail::has_background_clip_transport<DisplayCommand>()) return;
    if (!script_task_app_frame_codec_supports_version(8)) {
        std::cout << "v8 positive transport NOT EXERCISED: canonical group/prepared-text prerequisites unavailable\n";
        return;
    }

    LayerNode root;
    root.bounds = {0, 0, 32, 32};
    DisplayCommand image;
    detail::set_command_type(image, DisplayCommandType::Image);
    image.rect = {1, 2, 12, 10};
    detail::set_command_background_paint_clip(image, {-123, 456, 300, 200});
    root.display_list.push_back(image);
    DisplayCommand gradient;
    detail::set_command_type(gradient, DisplayCommandType::LinearGradient);
    gradient.rect = {0, 0, 32, 32};
    detail::set_command_background_paint_clip(gradient, {-2, 7, 9, 11});
    root.display_list.push_back(gradient);

    ScriptTaskAppFrameCodecOptions options;
    options.version = 8; options.max_commands = 4; options.max_groups = 4;
    options.max_text_bytes = 64; options.max_payload_bytes = 4096;
    ScriptTaskAppFrame frame;
    assert(make_script_task_group_frame(root, root.bounds, options, frame) == ScriptTaskAppFrameCodecStatus::Accepted);
    std::vector<std::uint8_t> bytes{91, 92};
    assert(encode_script_task_app_frame(frame, options, bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    const std::size_t command_at = 44 + 152;
    assert(bytes.size() == 44 + 152 + 2 * 128);
    assert(bytes[4] == 8 && bytes[command_at + 120] == 0x85 && bytes[command_at + 121] == 0xff);
    assert(bytes[command_at + 122] == 0xc8 && bytes[command_at + 123] == 0x01);
    assert(bytes[command_at + 124] == 0x2c && bytes[command_at + 125] == 0x01);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, options, decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(detail::command_background_paint_clip(decoded.display_list[0]).dx == -123);
    assert(detail::command_background_paint_clip(decoded.display_list[0]).height == 200);
    assert(detail::command_background_paint_clip(decoded.display_list[1]).dx == -2);
    assert(detail::command_background_paint_clip(decoded.display_list[1]).width == 9);

    auto old_options = options;
    old_options.version = 7;
    std::vector<std::uint8_t> unchanged{17, 18, 19};
    assert(encode_script_task_app_frame(frame, old_options, unchanged) ==
           ScriptTaskAppFrameCodecStatus::UnsupportedClipFeature);
    assert((unchanged == std::vector<std::uint8_t>{17, 18, 19}));

    LayerNode unclipped_root;
    unclipped_root.bounds = root.bounds;
    unclipped_root.display_list = root.display_list;
    detail::set_command_background_paint_clip(unclipped_root.display_list[0], {});
    detail::set_command_background_paint_clip(unclipped_root.display_list[1], {});
    ScriptTaskAppFrameCodecOptions v7_options = options;
    v7_options.version = 7;
    if (script_task_app_frame_codec_supports_version(7)) {
        ScriptTaskAppFrame v7_frame;
        assert(make_script_task_group_frame(unclipped_root, unclipped_root.bounds, v7_options, v7_frame) ==
               ScriptTaskAppFrameCodecStatus::Accepted);
        std::vector<std::uint8_t> v7_bytes;
        assert(encode_script_task_app_frame(v7_frame, v7_options, v7_bytes) ==
               ScriptTaskAppFrameCodecStatus::Accepted);
        assert(v7_bytes.size() == 44 + 152 + 2 * 120);
    }

    const auto valid = bytes;
    auto malformed = valid;
    malformed[command_at + 124] = malformed[command_at + 125] = 0;
    decoded.viewport = {9, 8, 7, 6};
    assert(decode_script_task_app_frame(malformed, options, decoded) == ScriptTaskAppFrameCodecStatus::Malformed);
    assert(decoded.viewport.x == 9 && decoded.viewport.width == 7);
    for (std::size_t length = 0; length < valid.size(); ++length) {
        auto truncated = valid;
        truncated.resize(length);
        assert(decode_script_task_app_frame(truncated, options, decoded) != ScriptTaskAppFrameCodecStatus::Accepted);
        assert(decoded.viewport.x == 9 && decoded.viewport.width == 7);
    }
}

} // namespace

int script_task_frame_codec_tests_main() {
    canonical_codec_tests::run();
    typed_clip_adapters_preserve_each_paint_alternative();
    supported_legacy_command_records_keep_frozen_layouts();
    typed_payloads_require_negotiated_transport<DisplayCommand>();
    v8_background_paint_clip_transport_is_opt_in_and_atomic();
    v6_group_codec_preserves_values_and_rejects_malformed_topology();
    frame_round_trip_preserves_render_values_and_target_order();
    frame_codec_rejects_budget_and_wire_integrity_failures();
    v2_frame_round_trip_preserves_nested_clips_and_target_references();
    v2_frame_rejects_invalid_clip_chain_and_v1_clip_downgrade();
    v3_frame_round_trip_preserves_fixed_point_command_transform();
    v4_frame_round_trip_preserves_transform_source_clip();
    v5_frame_round_trip_preserves_sampling_geometry_and_opaque_packet();
    legacy_frame_versions_reject_fractional_packets();
    codec_capability_tracks_the_selected_core_provider();
    v5_sampling_geometry_validation_is_atomic();
    v5_transports_the_current_core_packet();
    frame_codec_rejects_unrepresentable_clip_metadata();
    worker_frame_producer_rejects_oversized_generated_clip_table();
    sealed_lease_carries_only_serialized_frame_bytes();
    malformed_frame_releases_its_lease_before_reporting_decode_failure();
    stale_frame_consumer_cannot_take_new_session_frame();
    worker_frame_producer_flattens_private_layer_values_before_publish();
    worker_frame_producer_exports_clips_only_for_v2();
    worker_frame_producer_preserves_layer_rotation_for_v3_consumers();
    std::cout << "script task frame codec tests passed\n";
    return 0;
}
