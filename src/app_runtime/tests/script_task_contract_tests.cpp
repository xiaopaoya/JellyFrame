#include "app_runtime/script_task_contract.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

ScriptAppSession session(std::uint32_t instance, std::uint32_t generation, std::uint32_t epoch) {
    return ScriptAppSession{instance, generation, epoch};
}

void session_generation_rejects_stale_packets() {
    ScriptAppSessionController controller;
    const ScriptAppSession first = controller.begin(41);
    assert(first.valid());
    assert(controller.accepts(first));
    assert(controller.invalidate(first));
    const ScriptAppSession second = controller.begin(41);
    assert(second.valid());
    assert(second.app_instance_id == first.app_instance_id);
    assert(second.generation != first.generation);
    assert(second.worker_epoch != first.worker_epoch);
    assert(!controller.accepts(first));
    assert(controller.accepts(second));
}

void mailbox_copies_values_and_discards_stale_packets() {
    ScriptTaskMailbox mailbox({2, 8});
    const ScriptAppSession active = session(4, 3, 2);
    const ScriptAppSession stale = session(4, 2, 1);
    assert(mailbox.post({ScriptTaskPacketKind::Input, stale, 1, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    assert(mailbox.post({ScriptTaskPacketKind::Input, active, 2, 0, {2, 3}}) ==
           ScriptTaskMailboxPostStatus::Accepted);

    ScriptTaskPacket output;
    output.payload.reserve(8);
    assert(mailbox.pop_for(active, output));
    assert(output.session == active);
    assert(output.sequence == 2);
    assert(output.payload == std::vector<std::uint8_t>({2, 3}));
    assert(!mailbox.pop_for(active, output));
    const ScriptTaskMailboxStatistics stats = mailbox.statistics();
    assert(stats.discarded_stale == 1);
    assert(stats.popped == 1);
    assert(mailbox.post({ScriptTaskPacketKind::Input, active, 3, 0, std::vector<std::uint8_t>(9, 0)}) ==
           ScriptTaskMailboxPostStatus::PayloadTooLarge);
    assert(mailbox.post({ScriptTaskPacketKind::FrameReady, active, 4, 0, {}}) ==
           ScriptTaskMailboxPostStatus::InvalidPacket);
}

void sealed_frame_leases_are_session_scoped_and_reusable() {
    ScriptTaskFrameLeaseRegistry leases({2, 8, 12});
    const ScriptAppSession active = session(8, 1, 1);
    const ScriptAppSession other = session(8, 2, 2);
    std::uint32_t lease_id = 0;
    assert(leases.publish(active, {7, 8, 9}, lease_id) == ScriptTaskFrameLeaseStatus::Accepted);
    assert(lease_id != 0);
    std::vector<std::uint8_t> copied;
    copied.reserve(8);
    assert(leases.copy_sealed(active, lease_id, copied) == ScriptTaskFrameLeaseStatus::Accepted);
    assert(copied == std::vector<std::uint8_t>({7, 8, 9}));
    assert(leases.copy_sealed(other, lease_id, copied) == ScriptTaskFrameLeaseStatus::SessionMismatch);
    assert(leases.release(other, lease_id) == ScriptTaskFrameLeaseStatus::SessionMismatch);
    assert(leases.release(active, lease_id) == ScriptTaskFrameLeaseStatus::Accepted);
    assert(leases.release(active, lease_id) == ScriptTaskFrameLeaseStatus::StaleLease);
    assert(leases.active_count() == 0);
    assert(leases.used_bytes() == 0);

    std::uint32_t first = 0;
    std::uint32_t second = 0;
    assert(leases.publish(active, {1, 2, 3, 4, 5, 6}, first) == ScriptTaskFrameLeaseStatus::Accepted);
    assert(leases.publish(active, {1, 2, 3, 4, 5, 6, 7}, second) ==
           ScriptTaskFrameLeaseStatus::ByteBudgetExceeded);
    assert(leases.release_session(active) == 1);
    assert(leases.statistics().released_on_teardown == 1);
}

void service_tombstones_reject_cancelled_and_stale_completions() {
    ScriptTaskServiceLedger ledger(2);
    const ScriptAppSession active = session(6, 4, 5);
    const ScriptAppSession next = session(6, 5, 6);
    const ScriptTaskServiceToken token{active, 11, 17};
    assert(ledger.track(token) == ScriptTaskServiceTrackStatus::Accepted);
    assert(ledger.cancel(token));
    assert(ledger.consume_completion(active, token) == ScriptTaskServiceCompletionDisposition::Cancelled);
    assert(ledger.consume_completion(active, token) == ScriptTaskServiceCompletionDisposition::Stale);
    assert(ledger.track(token) == ScriptTaskServiceTrackStatus::Accepted);
    assert(ledger.consume_completion(next, token) == ScriptTaskServiceCompletionDisposition::Stale);
    assert(ledger.track(token) == ScriptTaskServiceTrackStatus::Accepted);
    assert(ledger.cancel_session(active) == 1);
    assert(ledger.consume_completion(active, token) == ScriptTaskServiceCompletionDisposition::Cancelled);
}

void native_release_intents_are_value_only_and_deduplicated() {
    ScriptTaskReleaseIntentMailbox intents(2);
    const ScriptTaskNativeLeaseReleaseIntent release{session(10, 2, 3), 99};
    const ScriptTaskNativeLeaseReleaseIntent stale{session(10, 1, 2), 100};
    const ScriptTaskNativeLeaseReleaseIntent later{session(10, 2, 3), 101};
    assert(intents.post(release) == ScriptTaskReleaseIntentStatus::Accepted);
    assert(intents.post(release) == ScriptTaskReleaseIntentStatus::Duplicate);
    assert(intents.post(stale) == ScriptTaskReleaseIntentStatus::Accepted);
    assert(intents.post(later) == ScriptTaskReleaseIntentStatus::Full);
    ScriptTaskNativeLeaseReleaseIntent output;
    assert(intents.pop(output));
    assert(output.session == release.session);
    assert(output.native_lease_id == release.native_lease_id);
    assert(intents.post(later) == ScriptTaskReleaseIntentStatus::Accepted);
    assert(intents.discard_session(stale.session) == 1);
    assert(intents.pop(output));
    assert(output.session == later.session);
    assert(output.native_lease_id == later.native_lease_id);
    assert(!intents.pop(output));
}

void supervisor_requires_ordered_value_only_teardown() {
    ScriptTaskSupervisor supervisor({{2, 8}, {1, 0}, {2, 8, 16}, 2, 2, {2, 8}});
    const ScriptAppSession active = supervisor.begin(30);
    assert(active.valid());
    assert(supervisor.post_input({ScriptTaskPacketKind::Input, active, 1, 0, {9}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    const ScriptTaskFramePublishResult frame = supervisor.publish_frame(active, {1, 2, 3});
    assert(frame.accepted());
    const ScriptTaskFramePublishResult rejected_frame = supervisor.publish_frame(active, {4, 5, 6});
    assert(rejected_frame.lease_status == ScriptTaskFrameLeaseStatus::Accepted);
    assert(rejected_frame.mailbox_status == ScriptTaskMailboxPostStatus::Full);
    assert(rejected_frame.lease_id == 0);
    assert(supervisor.track_service({active, 5, 6}) == ScriptTaskServiceTrackStatus::Accepted);
    assert(supervisor.post_service_request({ScriptTaskPacketKind::ServiceRequest, active, 5, 0, {1}}) ==
           ScriptTaskMailboxPostStatus::Accepted);
    assert(supervisor.post_native_release_intent({active, 44}) == ScriptTaskReleaseIntentStatus::Accepted);

    const ScriptTaskTeardownResult first = supervisor.begin_teardown(active);
    assert(first.session == active);
    assert(first.discarded_input_packets == 1);
    assert(first.discarded_worker_packets == 1);
    assert(first.discarded_service_request_packets == 1);
    assert(first.cancelled_service_requests == 1);
    assert(!supervisor.accepts(active));
    assert(supervisor.post_input({ScriptTaskPacketKind::Input, active, 2, 0, {7}}) ==
           ScriptTaskMailboxPostStatus::InvalidPacket);
    assert(supervisor.consume_service_completion({active, 5, 6}) ==
           ScriptTaskServiceCompletionDisposition::Cancelled);

    ScriptTaskNativeLeaseReleaseIntent intent;
    assert(supervisor.take_native_release_intent(intent));
    assert(intent.native_lease_id == 44);
    const ScriptTaskTeardownResult second = supervisor.complete_teardown(active);
    assert(second.session == active);
    assert(second.released_frame_leases == 1);
    assert(second.retired_service_tombstones == 0);
    assert(supervisor.begin(31).valid());
}

} // namespace

int script_task_contract_tests_main() {
    session_generation_rejects_stale_packets();
    mailbox_copies_values_and_discards_stale_packets();
    sealed_frame_leases_are_session_scoped_and_reusable();
    service_tombstones_reject_cancelled_and_stale_completions();
    native_release_intents_are_value_only_and_deduplicated();
    supervisor_requires_ordered_value_only_teardown();
    std::cout << "script task contract tests passed\n";
    return 0;
}
