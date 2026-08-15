#include "app_runtime/script_task_service_bridge.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host(std::size_t capacity = 4, std::size_t completions = 4) {
    return AppRuntimeHost({capacity, completions, 8, 4096, 1});
}

ScriptTaskSupervisor make_supervisor(std::size_t worker_slots = 4, std::size_t payload_bytes = 24) {
    ScriptTaskSupervisorOptions options;
    options.input_mailbox = {worker_slots, payload_bytes};
    options.frame_mailbox = {2, 0};
    options.frame_leases = {2, 64, 128};
    options.max_service_tombstones = 4;
    options.max_native_release_intents = 2;
    options.service_request_mailbox = {4, 20};
    options.service_payload_leases = {4, 20, 80};
    return ScriptTaskSupervisor(options);
}

AppFrameScratch make_scratch() {
    AppFrameScratch scratch;
    scratch.reserve_from_options({4, 4, 8, 4096, 1});
    return scratch;
}

HostServiceCompletion complete(const HostServiceRequest& request, std::uint32_t handle = 0) {
    return {request.job_id,
            request.kind,
            HostServiceStatus::Completed,
            request.app_instance_id,
            handle,
            19,
            23,
            request.client_token};
}

struct PayloadAdapter {
    AppRuntimeHost* host = nullptr;
    std::uint32_t expected_handle = 0;
    std::vector<std::uint8_t> payload;
    bool allow_copy = true;
    std::size_t copy_calls = 0;
    std::size_t release_calls = 0;
};

bool copy_payload(void* user,
                  const HostServiceCompletion& completion,
                  ScriptTaskServicePayloadWriter& output) {
    auto& adapter = *static_cast<PayloadAdapter*>(user);
    ++adapter.copy_calls;
    return adapter.allow_copy && completion.result_handle == adapter.expected_handle && output.append(adapter.payload);
}

bool release_payload(void* user, const HostServiceCompletion& completion) {
    auto& adapter = *static_cast<PayloadAdapter*>(user);
    ++adapter.release_calls;
    return completion.result_handle == adapter.expected_handle && adapter.host != nullptr &&
           adapter.host->handles().release(completion.result_handle);
}

void completion_payload_round_trips_without_native_data() {
    const ScriptTaskServiceCompletion expected{
        HostServiceJobKind::NetworkFetch, HostServiceStatus::Timeout, 6, 7, 8, 9, 10};
    std::vector<std::uint8_t> encoded;
    assert(encode_script_task_service_completion(expected, encoded));
    assert(encoded.size() == 24);
    ScriptTaskServiceCompletion decoded;
    assert(decode_script_task_service_completion(encoded, decoded));
    assert(decoded.kind == expected.kind);
    assert(decoded.status == expected.status);
    assert(decoded.request_id == expected.request_id);
    assert(decoded.client_token == expected.client_token);
    assert(decoded.payload_lease_id == expected.payload_lease_id);
    assert(decoded.error_code == expected.error_code);
    assert(decoded.byte_count == expected.byte_count);
    encoded[0] = 0;
    assert(!decode_script_task_service_completion(encoded, decoded));
    encoded[0] = 1;
    assert(!decode_script_task_service_completion(encoded, decoded));
}

void bridge_delivers_completion_as_bounded_worker_value_packet() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.bridge", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    PayloadAdapter adapter{&host, 0, {1, 2, 3}};
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 20, copy_payload, &adapter, release_payload, &adapter});

    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 31, HostServiceJobKind::NetworkFetch, 0, 2, 200, 47);
    assert(submitted.accepted());
    assert(submitted.token.request_id == 31);
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(request.job_id == submitted.host_job_id);
    assert(request.client_token == 47);
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, app.id, 20, nullptr, request.client_token);
    assert(handle != 0);
    adapter.expected_handle = handle;
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.host.accepted == 1);
    assert(pumped.queued_for_delivery == 1);
    assert(pumped.delivered == 1);
    assert(bridge.active_request_count() == 0);

    ScriptTaskPacket packet;
    assert(supervisor.take_worker_packet(session, packet));
    assert(packet.kind == ScriptTaskPacketKind::ServiceCompletion);
    assert(packet.session == session);
    ScriptTaskServiceCompletion decoded;
    assert(decode_script_task_service_completion(packet.payload, decoded));
    assert(decoded.request_id == 31);
    assert(decoded.client_token == 47);
    assert(decoded.payload_lease_id != 0);
    assert(decoded.error_code == 19);
    assert(decoded.byte_count == adapter.payload.size());
    assert(adapter.copy_calls == 1);
    assert(adapter.release_calls == 1);
    assert(!host.handles().contains(handle));
    std::vector<std::uint8_t> copied;
    assert(supervisor.copy_service_payload(session, decoded.payload_lease_id, copied) ==
           ScriptTaskServicePayloadLeaseStatus::Accepted);
    assert(copied == adapter.payload);
    assert(supervisor.release_service_payload(session, decoded.payload_lease_id) ==
           ScriptTaskServicePayloadLeaseStatus::Accepted);
}

void bridge_submits_dedicated_worker_service_packets() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.worker-request", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceRequest request{HostServiceJobKind::NetworkFetch, 72, 73, 0, 2, 400};
    assert(post_script_task_service_request(supervisor, session, 9, request, {20}).accepted());

    const ScriptTaskServiceRequestPumpResult pumped = bridge.pump_service_requests();
    assert(pumped.received == 1);
    assert(pumped.accepted == 1);
    assert(pumped.invalid_packets == 0);
    HostServiceRequest host_request;
    assert(host.pop_worker_request(host_request));
    assert(host_request.kind == request.kind);
    assert(host_request.client_token == request.client_token);
    assert(host_request.timeout_ms == request.timeout_ms);
    assert(host_request.priority == request.priority);
    assert(bridge.active_request_count() == 1);
}

void bridge_cancels_a_queued_worker_request() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.worker-cancel", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceRequest request{HostServiceJobKind::NetworkFetch, 72, 73, 0, 2, 400};
    assert(post_script_task_service_request(supervisor, session, 9, request, {20}).accepted());
    assert(post_script_task_service_cancel(supervisor, session, 10, {72, 73}, {12}).accepted());

    const ScriptTaskServiceRequestPumpResult pumped = bridge.pump_service_requests();
    assert(pumped.received == 2);
    assert(pumped.accepted == 1);
    assert(pumped.cancelled == 1);
    assert(bridge.active_request_count() == 0);
    assert(host.requests().empty());
}

void bridge_rejects_non_service_or_malformed_packets_without_host_access() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.bad-worker-request", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    assert(bridge.submit_packet({ScriptTaskPacketKind::Input, session, 1, 0, {}}).status ==
           ScriptTaskServiceSubmitStatus::InvalidPacket);
    assert(bridge.submit_packet({ScriptTaskPacketKind::ServiceRequest, session, 1, 0, {1}}).status ==
           ScriptTaskServiceSubmitStatus::InvalidPacket);
    assert(host.requests().empty());
}

void bridge_request_pump_reports_host_and_wire_rejections() {
    AppRuntimeHost host = make_host(0);
    const AppInstance app = host.launch("org.example.script.request-rejections", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    assert(post_script_task_service_request(
               supervisor,
               session,
               1,
               {HostServiceJobKind::ComputeJob, 19, 20, 0, 0, 0},
               {20})
               .accepted());
    assert(supervisor.post_service_request({ScriptTaskPacketKind::ServiceRequest, session, 2, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);

    const ScriptTaskServiceRequestPumpResult pumped = bridge.pump_service_requests();
    assert(pumped.received == 2);
    assert(pumped.host_rejected == 1);
    assert(pumped.invalid_packets == 1);
    assert(pumped.accepted == 0);
    assert(host.requests().empty());
    assert(bridge.active_request_count() == 1);

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult completions = bridge.pump(scratch);
    assert(completions.delivered == 1);
    assert(bridge.active_request_count() == 0);
    ScriptTaskPacket completion_packet;
    assert(supervisor.take_worker_packet(session, completion_packet));
    ScriptTaskServiceCompletion completion;
    assert(decode_script_task_service_completion(completion_packet.payload, completion));
    assert(completion.status == HostServiceStatus::BudgetExceeded);
    assert(completion.payload_lease_id == 0);
    assert(completion.error_code == static_cast<std::uint32_t>(ScriptTaskServiceSubmitStatus::HostRejected));
}

void bridge_cancels_pending_jobs_without_leaving_tombstones() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.cancel-pending", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 4, HostServiceJobKind::StorageKv, 0, 0, 0, 9);
    assert(submitted.accepted());
    assert(bridge.cancel(submitted.token));
    assert(host.requests().empty());
    assert(bridge.active_request_count() == 0);
    assert(bridge.submit(session, 4, HostServiceJobKind::StorageKv, 0, 0, 0, 9).accepted());
}

void bridge_releases_cancelled_late_completion_handles() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.cancel-late", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 15, HostServiceJobKind::ImageDecode, 0, 0, 0, 5);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(bridge.cancel(submitted.token));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::Surface, app.id, 24, nullptr, request.client_token);
    assert(handle != 0);
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.cancelled == 1);
    assert(pumped.released_completion_sources == 1);
    assert(!host.handles().contains(handle));
    ScriptTaskPacket ignored;
    assert(!supervisor.take_frame_packet(ignored));
}

void bridge_does_not_copy_payload_for_cancelled_inflight_completion() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.cancelled-payload", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    PayloadAdapter adapter{&host, 0, {4, 5, 6}};
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 20, copy_payload, &adapter, release_payload, &adapter});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 24, HostServiceJobKind::NetworkFetch, 0, 0, 0, 25);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(bridge.cancel(submitted.token));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, app.id, 8, nullptr, request.client_token);
    assert(handle != 0);
    adapter.expected_handle = handle;
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.cancelled == 1);
    assert(pumped.published_payload_leases == 0);
    assert(adapter.copy_calls == 0);
    assert(adapter.release_calls == 1);
    assert(!host.handles().contains(handle));
}

void bridge_reports_payload_copy_and_lease_failures_as_terminal_values() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.payload-failures", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 2});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 91, HostServiceJobKind::NetworkFetch, 0, 0, 0, 92);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, app.id, 8, nullptr, request.client_token);
    assert(handle != 0);
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult copy_unavailable = bridge.pump(scratch);
    assert(copy_unavailable.payload_copy_failures == 1);
    assert(copy_unavailable.released_completion_sources == 1);
    ScriptTaskPacket packet;
    assert(supervisor.take_worker_packet(session, packet));
    ScriptTaskServiceCompletion completion;
    assert(decode_script_task_service_completion(packet.payload, completion));
    assert(completion.status == HostServiceStatus::Failed);
    assert(completion.payload_lease_id == 0);
    assert(completion.error_code == static_cast<std::uint32_t>(ScriptTaskServicePayloadErrorCode::CopyUnavailable));
    assert(!host.handles().contains(handle));
}

void bridge_rejects_oversized_copied_payload_without_leaking_source_handle() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.payload-budget", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    PayloadAdapter adapter{&host, 0, {1, 2, 3}};
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 2, copy_payload, &adapter, release_payload, &adapter});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 93, HostServiceJobKind::NetworkFetch, 0, 0, 0, 94);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, app.id, 8, nullptr, request.client_token);
    assert(handle != 0);
    adapter.expected_handle = handle;
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.payload_copy_failures == 1);
    assert(pumped.released_completion_sources == 1);
    assert(adapter.release_calls == 1);
    assert(!host.handles().contains(handle));
}

void bridge_releases_published_payload_when_teardown_precedes_delivery() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.payload-teardown", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor(1);
    const ScriptAppSession session = supervisor.begin(app.id);
    PayloadAdapter adapter{&host, 0, {7, 6}};
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 20, copy_payload, &adapter, release_payload, &adapter});
    assert(supervisor.post_input({ScriptTaskPacketKind::Input, session, 1, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 95, HostServiceJobKind::StorageKv, 0, 0, 0, 96);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::StorageValue, app.id, 8, nullptr, request.client_token);
    assert(handle != 0);
    adapter.expected_handle = handle;
    assert(host.push_completion(complete(request, handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.worker_inbox_full);
    assert(pumped.published_payload_leases == 1);
    assert(!host.handles().contains(handle));
    assert(supervisor.begin_teardown(session).cancelled_service_requests == 1);
    const ScriptTaskServiceBridgeTeardownResult teardown = bridge.begin_teardown(session);
    assert(teardown.released_ready_payload_leases == 1);
    assert(bridge.active_request_count() == 0);
    assert(supervisor.complete_teardown(session).released_service_payload_leases == 0);
}

void bridge_retries_after_worker_inbox_backpressure() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.backpressure", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor(1);
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    assert(supervisor.post_input({ScriptTaskPacketKind::Input, session, 1, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 17, HostServiceJobKind::SensorSample, 0, 0, 0, 3);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    assert(host.push_completion(complete(request)));

    AppFrameScratch scratch = make_scratch();
    ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.worker_inbox_full);
    assert(pumped.delivered == 0);
    assert(bridge.active_request_count() == 1);
    ScriptTaskPacket input;
    assert(supervisor.take_worker_packet(session, input));
    assert(input.kind == ScriptTaskPacketKind::Input);

    pumped = bridge.pump(scratch);
    assert(pumped.delivered == 1);
    assert(bridge.active_request_count() == 0);
}

void bridge_discards_malformed_completion_without_retiring_inflight_request() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.bad-completion", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 18, HostServiceJobKind::NetworkFetch, 0, 0, 0, 19);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));

    const std::uint32_t malformed_handle = host.handles().allocate(
        HostServiceHandleKind::StorageValue, app.id, 8, nullptr, request.client_token);
    assert(malformed_handle != 0);
    HostServiceCompletion malformed = complete(request, malformed_handle);
    malformed.kind = HostServiceJobKind::StorageKv;
    // Bypass the host's worker-facing validator to verify the bridge remains
    // safe if a corrupt or incorrectly routed event reaches its queue.
    assert(host.completions().push(malformed));

    AppFrameScratch scratch = make_scratch();
    ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.discarded_unmatched_completions == 1);
    assert(pumped.released_completion_sources == 1);
    assert(pumped.delivered == 0);
    assert(bridge.active_request_count() == 1);
    assert(!host.handles().contains(malformed_handle));

    assert(host.push_completion(complete(request)));
    pumped = bridge.pump(scratch);
    assert(pumped.delivered == 1);
    assert(bridge.active_request_count() == 0);
    ScriptTaskPacket packet;
    assert(supervisor.take_worker_packet(session, packet));
    ScriptTaskServiceCompletion completion;
    assert(decode_script_task_service_completion(packet.payload, completion));
    assert(completion.kind == HostServiceJobKind::NetworkFetch);
}

void bridge_rejects_unowned_completion_handle_before_provider_callbacks() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.unowned-handle", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    PayloadAdapter adapter{&host, 0, {1, 2, 3}};
    ScriptTaskServiceBridge bridge(host, supervisor, {4, 20, copy_payload, &adapter, release_payload, &adapter});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 41, HostServiceJobKind::NetworkFetch, 0, 0, 0, 42);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));

    const std::uint32_t other_consumer_handle = host.handles().allocate(
        HostServiceHandleKind::FetchResponse, app.id, 8, nullptr, request.client_token + 1);
    assert(other_consumer_handle != 0);
    assert(host.completions().push(complete(request, other_consumer_handle)));

    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult rejected = bridge.pump(scratch);
    assert(rejected.payload_handle_rejections == 1);
    assert(rejected.payload_copy_failures == 0);
    assert(rejected.released_completion_sources == 0);
    assert(adapter.copy_calls == 0);
    assert(adapter.release_calls == 0);
    assert(host.handles().contains(other_consumer_handle));
    assert(bridge.active_request_count() == 0);

    ScriptTaskPacket packet;
    assert(supervisor.take_worker_packet(session, packet));
    ScriptTaskServiceCompletion completion;
    assert(decode_script_task_service_completion(packet.payload, completion));
    assert(completion.status == HostServiceStatus::Failed);
    assert(completion.error_code == static_cast<std::uint32_t>(ScriptTaskServicePayloadErrorCode::HandleRejected));
    assert(completion.payload_lease_id == 0);
    assert(host.handles().release(other_consumer_handle));
}

void bridge_rejects_mailboxes_that_cannot_hold_completion_payloads() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.packet-budget", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor(2, 23);
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {2});
    const ScriptTaskServiceSubmitResult result = bridge.submit(
        session, 1, HostServiceJobKind::ComputeJob, 0, 0, 0, 1);
    assert(result.status == ScriptTaskServiceSubmitStatus::PacketBudgetExceeded);
    assert(host.requests().empty());
}

void bridge_teardown_leaves_late_inflight_work_to_host_stale_cleanup() {
    AppRuntimeHost host = make_host();
    const AppInstance app = host.launch("org.example.script.teardown", AppRole::App);
    ScriptTaskSupervisor supervisor = make_supervisor();
    const ScriptAppSession session = supervisor.begin(app.id);
    ScriptTaskServiceBridge bridge(host, supervisor, {4});
    const ScriptTaskServiceSubmitResult submitted = bridge.submit(
        session, 8, HostServiceJobKind::LocationSnapshot, 0, 0, 0, 27);
    assert(submitted.accepted());
    HostServiceRequest request;
    assert(host.pop_worker_request(request));
    const std::uint32_t handle = host.handles().allocate(
        HostServiceHandleKind::LocationSnapshot, app.id, 12, nullptr, request.client_token);
    assert(handle != 0);

    const ScriptTaskTeardownResult supervisor_begin = supervisor.begin_teardown(session);
    assert(supervisor_begin.cancelled_service_requests == 1);
    const ScriptTaskServiceBridgeTeardownResult bridge_begin = bridge.begin_teardown(session);
    assert(bridge_begin.awaiting_in_flight_host_completions == 1);
    assert(bridge.active_request_count() == 1);
    assert(host.terminate_current(AppTeardownReason::RuntimeError).released_handles == 1);
    assert(!host.handles().contains(handle));
    assert(bridge.complete_teardown(session).retired_records == 1);
    assert(supervisor.complete_teardown(session).session == session);

    assert(host.push_completion(complete(request, handle)));
    AppFrameScratch scratch = make_scratch();
    const ScriptTaskServiceBridgePumpResult pumped = bridge.pump(scratch);
    assert(pumped.host.stale == 1);
    assert(pumped.delivered == 0);
}

} // namespace

int script_task_service_bridge_tests_main() {
    completion_payload_round_trips_without_native_data();
    bridge_delivers_completion_as_bounded_worker_value_packet();
    bridge_submits_dedicated_worker_service_packets();
    bridge_cancels_a_queued_worker_request();
    bridge_rejects_non_service_or_malformed_packets_without_host_access();
    bridge_request_pump_reports_host_and_wire_rejections();
    bridge_cancels_pending_jobs_without_leaving_tombstones();
    bridge_releases_cancelled_late_completion_handles();
    bridge_does_not_copy_payload_for_cancelled_inflight_completion();
    bridge_retries_after_worker_inbox_backpressure();
    bridge_discards_malformed_completion_without_retiring_inflight_request();
    bridge_rejects_unowned_completion_handle_before_provider_callbacks();
    bridge_rejects_mailboxes_that_cannot_hold_completion_payloads();
    bridge_teardown_leaves_late_inflight_work_to_host_stale_cleanup();
    bridge_reports_payload_copy_and_lease_failures_as_terminal_values();
    bridge_rejects_oversized_copied_payload_without_leaking_source_handle();
    bridge_releases_published_payload_when_teardown_precedes_delivery();
    std::cout << "script task service bridge tests passed\n";
    return 0;
}
