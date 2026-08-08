#include "app_runtime/script_task_fatal_codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

ScriptTaskSupervisor make_supervisor() {
    ScriptTaskSupervisorOptions options;
    options.input_mailbox = {2, 32};
    options.worker_mailbox = {2, 32};
    options.service_request_mailbox = {2, 32};
    options.frame_leases = {2, 64, 128};
    options.service_payload_leases = {2, 64, 128};
    options.max_service_tombstones = 2;
    options.max_native_release_intents = 2;
    options.fatal_mailbox = {2, 40};
    return ScriptTaskSupervisor(options);
}

void fatal_records_round_trip_as_bounded_values() {
    const ScriptTaskFatalRecord expected{
        {7, 8, 9}, 3, 11, 12, 13, 0x1020304050607080ULL, 17};
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_fatal(expected, {40}, encoded) == ScriptTaskFatalCodecStatus::Accepted);
    assert(encoded.size() == 40);
    ScriptTaskFatalRecord decoded;
    assert(decode_script_task_fatal(encoded, {40}, decoded) == ScriptTaskFatalCodecStatus::Accepted);
    assert(decoded.session == expected.session);
    assert(decoded.reason == expected.reason);
    assert(decoded.diagnostic_code == expected.diagnostic_code);
    assert(decoded.last_input_sequence == expected.last_input_sequence);
    assert(decoded.last_frame_sequence == expected.last_frame_sequence);
    assert(decoded.internal_bytes == expected.internal_bytes);
    assert(decoded.message_bytes == expected.message_bytes);
}

void fatal_records_reject_invalid_wire_values() {
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_fatal({{1, 1, 1}, 1, 0, 0, 0, 0, 0}, {39}, encoded) ==
           ScriptTaskFatalCodecStatus::PayloadTooLarge);
    assert(encode_script_task_fatal({{1, 1, 1}, 0, 0, 0, 0, 0, 0}, {40}, encoded) ==
           ScriptTaskFatalCodecStatus::InvalidValue);
    assert(encode_script_task_fatal({{1, 1, 1}, 1, 0, 0, 0, 0, 0}, {40}, encoded) ==
           ScriptTaskFatalCodecStatus::Accepted);
    encoded[0] = 0;
    ScriptTaskFatalRecord decoded;
    assert(decode_script_task_fatal(encoded, {40}, decoded) == ScriptTaskFatalCodecStatus::Malformed);
}

void supervisor_keeps_fatal_traffic_separate_and_bounded() {
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(10);
    const ScriptTaskFatalRecord record{{10, 1, 1}, 2, 3, 4, 5, 6, 7};
    assert(post_script_task_fatal(supervisor, record, 1, {40}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    ScriptTaskPacket packet;
    assert(supervisor.take_fatal(packet));
    assert(packet.kind == ScriptTaskPacketKind::FatalRecord);
    assert(packet.session == session);
    assert(!supervisor.take_worker_packet(packet));
    assert(!supervisor.take_input(packet));
}

} // namespace

int script_task_fatal_codec_tests_main() {
    fatal_records_round_trip_as_bounded_values();
    fatal_records_reject_invalid_wire_values();
    supervisor_keeps_fatal_traffic_separate_and_bounded();
    std::cout << "script task fatal codec tests passed\n";
    return 0;
}
