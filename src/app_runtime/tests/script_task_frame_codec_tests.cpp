#include "app_runtime/script_task_frame_codec.h"
#include "app_runtime/script_task_contract.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {

ScriptTaskAppFrameCodecOptions limits() { return {4, 32, 3, 512}; }

ScriptTaskAppFrame fixture() {
    ScriptTaskAppFrame frame;
    frame.viewport = {0, 0, 172, 320};
    DisplayCommand fill;
    fill.type = DisplayCommandType::LinearGradient;
    fill.rect = {1, 2, 30, 40};
    fill.color = {1, 2, 3, 4};
    fill.color2 = {5, 6, 7, 8};
    fill.gradient_axis = GradientAxis::DiagonalDownRight;
    fill.gradient_stop_percent = 57;
    fill.border_radius = 9;
    frame.display_list.push_back(fill);
    DisplayCommand text;
    text.type = DisplayCommandType::Text;
    text.rect = {4, 5, 80, 16};
    text.text = "Hi \xE4\xB8\x96\xE7\x95\x8C";
    text.font_size = 13;
    text.font_weight = 600;
    text.font_family_hash = 44;
    text.text_align = TextCommandAlign::Center;
    text.text_single_line = false;
    frame.display_list.push_back(text);
    frame.input_targets = {{1, {2, 2, 30, 30}, true}, {2, {10, 10, 30, 30}, true}, {3, {60, 10, 20, 20}, false}};
    return frame;
}

void frame_round_trip_preserves_render_values_and_target_order() {
    const ScriptTaskAppFrame expected = fixture();
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_app_frame(expected, limits(), bytes) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(bytes, limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.viewport.width == 172);
    assert(decoded.display_list.size() == 2);
    assert(decoded.display_list[0].type == DisplayCommandType::LinearGradient);
    assert(decoded.display_list[0].gradient_axis == GradientAxis::DiagonalDownRight);
    assert(decoded.display_list[0].color2.a == 8);
    assert(decoded.display_list[1].text == expected.display_list[1].text);
    assert(decoded.display_list[1].font_family_hash == 44);
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

void sealed_lease_carries_only_serialized_frame_bytes() {
    const ScriptTaskAppFrame frame = fixture();
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_app_frame(frame, limits(), encoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    ScriptTaskSupervisor supervisor({{2, 24}, {1, 0}, {1, 512, 512}, 0, 0});
    const ScriptAppSession session = supervisor.begin(71);
    const ScriptTaskFramePublishResult published = supervisor.publish_frame(session, encoded);
    assert(published.accepted());
    ScriptTaskPacket packet;
    assert(supervisor.take_worker_packet(packet));
    assert(packet.kind == ScriptTaskPacketKind::FrameReady);
    assert(packet.payload.empty());
    std::vector<std::uint8_t> copied;
    assert(supervisor.copy_frame(session, packet.lease_id, copied) == ScriptTaskFrameLeaseStatus::Accepted);
    ScriptTaskAppFrame decoded;
    assert(decode_script_task_app_frame(copied, limits(), decoded) == ScriptTaskAppFrameCodecStatus::Accepted);
    assert(decoded.display_list[1].text == frame.display_list[1].text);
    assert(supervisor.release_frame(session, packet.lease_id) == ScriptTaskFrameLeaseStatus::Accepted);
}

} // namespace

int script_task_frame_codec_tests_main() {
    frame_round_trip_preserves_render_values_and_target_order();
    frame_codec_rejects_budget_and_wire_integrity_failures();
    sealed_lease_carries_only_serialized_frame_bytes();
    std::cout << "script task frame codec tests passed\n";
    return 0;
}
