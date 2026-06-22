#include "app_runtime/app_services.h"
#include "app_runtime/app_storage_lifecycle_policy.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace jellyframe;

namespace {

AppRuntimeHost make_host() {
    return AppRuntimeHost(AppRuntimeHostOptions{
        4,
        2,
        8,
        4096,
        1,
    });
}

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "check failed: " << message << '\n';
        std::exit(1);
    }
}

void default_policy_flushes_normal_exit_and_drops_crash_work() {
    AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Exit);
    assert(decision.flush_pending_writes);
    assert(!decision.drop_pending_writes);
    assert(!decision.delete_persistent_data);
    assert(decision.keep_persistent_data);

    decision = app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Crash);
    assert(!decision.flush_pending_writes);
    assert(decision.drop_pending_writes);
    assert(!decision.delete_persistent_data);
    assert(decision.keep_persistent_data);
}

void uninstall_and_update_policy_are_explicit() {
    AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Uninstall);
    assert(!decision.flush_pending_writes);
    assert(decision.drop_pending_writes);
    assert(decision.delete_persistent_data);
    assert(!decision.keep_persistent_data);

    AppStorageLifecyclePolicy policy;
    policy.delete_data_on_uninstall = false;
    policy.keep_data_on_update = false;
    decision = app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Uninstall, policy);
    assert(!decision.delete_persistent_data);
    assert(decision.keep_persistent_data);

    decision = app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::UpdateReplace, policy);
    assert(decision.flush_pending_writes);
    assert(decision.delete_persistent_data);
    assert(!decision.keep_persistent_data);
}

void suspend_and_memory_pressure_follow_budget_policy() {
    AppStorageLifecyclePolicy policy;
    AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Suspend, policy);
    assert(!decision.flush_pending_writes);
    assert(!decision.drop_pending_writes);

    policy.flush_pending_on_suspend = true;
    decision = app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Suspend, policy);
    assert(decision.flush_pending_writes);
    assert(!decision.drop_pending_writes);

    decision = app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::MemoryPressure, policy);
    assert(!decision.flush_pending_writes);
    assert(decision.drop_pending_writes);
}

void storage_mock_can_drop_pending_by_instance_or_app() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 8, 256});

    const AppInstance first = host.launch("org.example.settings", AppRole::App);
    assert(storage.submit_set(host, "theme", "dark").accepted());
    assert(storage.submit_set(host, "mode", "quiet").accepted());
    assert(storage.drop_pending_app_instance(first.id) == 2);
    assert(!storage.complete_next(host));

    host.launch("org.example.settings", AppRole::App);
    assert(storage.submit_set(host, "theme", "light").accepted());
    assert(storage.drop_pending_app("org.example.settings") == 1);
    assert(!storage.complete_next(host));
}

void storage_mock_can_apply_delete_policy_to_persistent_data() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 8, 256});
    host.launch("org.example.settings", AppRole::App);

    assert(storage.submit_set(host, "theme", "dark").accepted());
    assert(storage.complete_next(host));
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    assert(accepted.size() == 1);
    assert(accepted.front().status == HostServiceStatus::Completed);

    const AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Uninstall);
    assert(decision.delete_persistent_data);
    assert(storage.clear_app("org.example.settings") == 1);
}

void apply_lifecycle_flushes_pending_writes_on_exit() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 8, 256});
    const AppInstance app = host.launch("org.example.settings", AppRole::App);

    check(storage.submit_set(host, "theme", "dark").accepted(), "first storage set accepted");
    check(storage.submit_set(host, "mode", "quiet").accepted(), "second storage set accepted");
    check(storage.pending_count_app_instance(app.id) == 2, "pending storage writes counted");

    const AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Exit);
    const AppStorageLifecycleApplyResult applied =
        apply_app_storage_lifecycle_decision(host, storage, app.app_id, app.id, decision);
    check(applied.flushed_pending_writes == 2, "exit flushes pending writes");
    check(applied.remaining_pending_writes == 0, "exit flush leaves no pending writes");
    check(!applied.flush_stopped_before_empty, "exit flush is complete");

    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    check(accepted.size() == 2, "flushed writes post completions");
    host.exit_current();

    host.launch("org.example.settings", AppRole::App);
    check(storage.submit_get(host, "theme").accepted(), "stored value get accepted");
    check(storage.complete_next(host), "stored value get completed");
    accepted.clear();
    host.pump_frame_completions(accepted);
    check(accepted.size() == 1, "stored value completion accepted");
    const AppPrivateKvRecord* record = storage.value(accepted.front().handle);
    check(record != nullptr && record->value == "dark", "flushed value is readable after relaunch");
    storage.release_value(host, accepted.front().handle);
}

void apply_lifecycle_respects_flush_budget_and_reports_remaining_work() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 8, 256});
    const AppInstance app = host.launch("org.example.budget", AppRole::App);

    check(storage.submit_set(host, "a", "1").accepted(), "budget set a accepted");
    check(storage.submit_set(host, "b", "2").accepted(), "budget set b accepted");

    const AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Exit);
    AppStorageLifecycleApplyResult applied =
        apply_app_storage_lifecycle_decision(host, storage, app.app_id, app.id, decision, 1);
    check(applied.flushed_pending_writes == 1, "flush budget limits work");
    check(applied.remaining_pending_writes == 1, "flush budget reports remaining pending write");
    check(applied.flush_stopped_before_empty, "flush budget reports incomplete flush");

    AppStorageLifecycleDecision crash_decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Crash);
    applied = apply_app_storage_lifecycle_decision(host, storage, app.app_id, app.id, crash_decision);
    check(applied.dropped_pending_writes == 1, "crash policy drops remaining pending write");
    check(storage.pending_count() == 0, "crash drop leaves no pending writes");
}

void apply_lifecycle_deletes_persistent_data_on_uninstall() {
    AppRuntimeHost host = make_host();
    AppPrivateKvStorageMock storage(AppPrivateKvPolicy{true, 16, 32, 8, 256});
    const AppInstance app = host.launch("org.example.delete", AppRole::App);

    check(storage.submit_set(host, "theme", "dark").accepted(), "delete setup set accepted");
    check(storage.complete_next(host), "delete setup set completed");
    std::vector<HostServiceCompletion> accepted;
    host.pump_frame_completions(accepted);
    check(accepted.size() == 1, "delete setup completion accepted");

    const AppStorageLifecycleDecision decision =
        app_storage_lifecycle_decision_for(AppStorageLifecycleTrigger::Uninstall);
    const AppStorageLifecycleApplyResult applied =
        apply_app_storage_lifecycle_decision(host, storage, app.app_id, app.id, decision);
    check(applied.deleted_persistent_items == 1, "uninstall deletes persistent data");
    check(storage.clear_app(app.app_id) == 0, "uninstall data is already gone");
}

} // namespace

int main() {
    default_policy_flushes_normal_exit_and_drops_crash_work();
    uninstall_and_update_policy_are_explicit();
    suspend_and_memory_pressure_follow_budget_policy();
    storage_mock_can_drop_pending_by_instance_or_app();
    storage_mock_can_apply_delete_policy_to_persistent_data();
    apply_lifecycle_flushes_pending_writes_on_exit();
    apply_lifecycle_respects_flush_budget_and_reports_remaining_work();
    apply_lifecycle_deletes_persistent_data_on_uninstall();
    return 0;
}
