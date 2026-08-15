#include "app_runtime/script_task_service_request_codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

void service_requests_round_trip_as_fixed_values() {
    const ScriptTaskServiceRequest expected{
        HostServiceJobKind::NetworkFetch, 7, 11, 13, 2, 500};
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_service_request(expected, {20}, encoded) ==
           ScriptTaskServiceRequestCodecStatus::Accepted);
    assert(encoded.size() == 20);
    ScriptTaskServiceRequest decoded;
    assert(decode_script_task_service_request(encoded, {20}, decoded) ==
           ScriptTaskServiceRequestCodecStatus::Accepted);
    assert(decoded.kind == expected.kind);
    assert(decoded.request_id == expected.request_id);
    assert(decoded.client_token == expected.client_token);
    assert(decoded.request_handle == expected.request_handle);
    assert(decoded.priority == expected.priority);
    assert(decoded.timeout_ms == expected.timeout_ms);
}

void service_requests_reject_bad_values_and_wire_data() {
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_service_request({HostServiceJobKind::Other, 0, 1, 0, 0, 0}, {20}, encoded) ==
           ScriptTaskServiceRequestCodecStatus::InvalidValue);
    assert(encode_script_task_service_request({HostServiceJobKind::Other, 1, 1, 0, 0, 0}, {19}, encoded) ==
           ScriptTaskServiceRequestCodecStatus::PayloadTooLarge);
    assert(encode_script_task_service_request({HostServiceJobKind::Other, 1, 1, 0, 0, 0}, {20}, encoded) ==
           ScriptTaskServiceRequestCodecStatus::Accepted);
    encoded[3] = 1;
    ScriptTaskServiceRequest decoded;
    assert(decode_script_task_service_request(encoded, {20}, decoded) ==
           ScriptTaskServiceRequestCodecStatus::Malformed);
}

void service_request_post_uses_the_dedicated_supervisor_mailbox() {
    ScriptTaskSupervisor supervisor({{2, 32}, {2, 0}, {2, 64, 128}, 2, 2, {2, 20}});
    const ScriptAppSession active = supervisor.begin(52);
    const ScriptTaskServiceRequest request{HostServiceJobKind::SensorSample, 8, 9, 0, 1, 100};
    assert(post_script_task_service_request(supervisor, active, 3, request, {20}).accepted());

    ScriptTaskPacket output;
    assert(supervisor.take_service_request(output));
    assert(output.kind == ScriptTaskPacketKind::ServiceRequest);
    assert(output.session == active);
    assert(output.sequence == 3);
    assert(!supervisor.take_frame_packet(output));
}

void service_cancels_round_trip_as_value_only_packet() {
    ScriptTaskSupervisor supervisor({{2, 32}, {2, 0}, {2, 64, 128}, 2, 2, {2, 20}});
    const ScriptAppSession active = supervisor.begin(53);
    const ScriptTaskServiceCancel expected{17, 19};
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_service_cancel(expected, {12}, encoded) ==
           ScriptTaskServiceRequestCodecStatus::Accepted);
    ScriptTaskServiceCancel decoded;
    assert(decode_script_task_service_cancel(encoded, {12}, decoded) ==
           ScriptTaskServiceRequestCodecStatus::Accepted);
    assert(decoded.request_id == expected.request_id);
    assert(decoded.client_token == expected.client_token);
    assert(post_script_task_service_cancel(supervisor, active, 4, expected, {12}).accepted());

    ScriptTaskPacket output;
    assert(supervisor.take_service_request(output));
    assert(output.kind == ScriptTaskPacketKind::ServiceCancel);
    assert(output.sequence == 4);
    assert(output.payload.size() == 12);
    encoded[1] = 0;
    assert(decode_script_task_service_cancel(encoded, {12}, decoded) ==
           ScriptTaskServiceRequestCodecStatus::Malformed);
}

} // namespace

int script_task_service_request_codec_tests_main() {
    service_requests_round_trip_as_fixed_values();
    service_requests_reject_bad_values_and_wire_data();
    service_request_post_uses_the_dedicated_supervisor_mailbox();
    service_cancels_round_trip_as_value_only_packet();
    std::cout << "script task service request codec tests passed\n";
    return 0;
}
