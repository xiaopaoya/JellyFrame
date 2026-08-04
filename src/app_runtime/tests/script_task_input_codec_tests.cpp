#include "app_runtime/script_task_input_codec.h"

#include <cassert>
#include <iostream>

using namespace jellyframe;

namespace {
ScriptTaskInputCodecOptions limits() { return {16, 48}; }

void input_value_round_trips_and_posts_to_worker_inbox() {
    ScriptTaskInputEvent expected;
    expected.kind = ScriptTaskInputKind::PointerDown;
    expected.x = 31; expected.y = 47; expected.button = 0; expected.buttons = 1; expected.modifiers = 0x05;
    expected.key_code = 9;
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_input(expected, limits(), bytes) == ScriptTaskInputCodecStatus::Accepted);
    ScriptTaskInputEvent decoded;
    assert(decode_script_task_input(bytes, limits(), decoded) == ScriptTaskInputCodecStatus::Accepted);
    assert(decoded.kind == ScriptTaskInputKind::PointerDown && decoded.x == 31 && decoded.y == 47);
    assert(decoded.button == 0 && decoded.buttons == 1 && decoded.modifiers == 0x05 && decoded.key_code == 9);

    ScriptTaskSupervisor supervisor({{2, 48}, {1, 0}, {1, 64, 64}, 0, 0});
    const ScriptAppSession session = supervisor.begin(90);
    assert(post_script_task_input(supervisor, session, 4, expected, limits()).accepted());
    ScriptTaskPacket packet;
    assert(supervisor.take_input(packet));
    assert(packet.kind == ScriptTaskPacketKind::Input && packet.sequence == 4);
    assert(decode_script_task_input(packet.payload, limits(), decoded) == ScriptTaskInputCodecStatus::Accepted);
    assert(decoded.x == expected.x);
}

void input_codec_rejects_malformed_or_over_budget_values() {
    ScriptTaskInputEvent input;
    input.kind = ScriptTaskInputKind::TextInput;
    input.text = "12345678901234567";
    std::vector<std::uint8_t> bytes;
    assert(encode_script_task_input(input, limits(), bytes) == ScriptTaskInputCodecStatus::TextTooLarge);
    input.text = "ok";
    input.button = 4;
    assert(encode_script_task_input(input, limits(), bytes) == ScriptTaskInputCodecStatus::InvalidValue);
    input.button = -1;
    assert(encode_script_task_input(input, limits(), bytes) == ScriptTaskInputCodecStatus::Accepted);
    bytes[0] = 0;
    ScriptTaskInputEvent decoded;
    assert(decode_script_task_input(bytes, limits(), decoded) == ScriptTaskInputCodecStatus::Malformed);
}
}

int script_task_input_codec_tests_main() {
    input_value_round_trips_and_posts_to_worker_inbox();
    input_codec_rejects_malformed_or_over_budget_values();
    std::cout << "script task input codec tests passed\n";
    return 0;
}
