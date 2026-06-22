#include "app_runtime/app_storage_lifecycle_policy.h"

#include "app_runtime/app_services.h"

namespace jellyframe {

AppStorageLifecycleDecision app_storage_lifecycle_decision_for(
    AppStorageLifecycleTrigger trigger,
    const AppStorageLifecyclePolicy& policy) {
    AppStorageLifecycleDecision decision;
    switch (trigger) {
    case AppStorageLifecycleTrigger::Suspend:
        decision.flush_pending_writes = policy.flush_pending_on_suspend;
        break;
    case AppStorageLifecycleTrigger::Resume:
        break;
    case AppStorageLifecycleTrigger::Exit:
        decision.flush_pending_writes = policy.flush_pending_on_exit;
        break;
    case AppStorageLifecycleTrigger::Crash:
        decision.drop_pending_writes = policy.drop_pending_on_crash;
        break;
    case AppStorageLifecycleTrigger::Uninstall:
        decision.drop_pending_writes = true;
        decision.delete_persistent_data = policy.delete_data_on_uninstall;
        decision.keep_persistent_data = !decision.delete_persistent_data;
        break;
    case AppStorageLifecycleTrigger::UpdateReplace:
        decision.flush_pending_writes = policy.flush_pending_on_update;
        decision.keep_persistent_data = policy.keep_data_on_update;
        decision.delete_persistent_data = !policy.keep_data_on_update;
        break;
    case AppStorageLifecycleTrigger::MemoryPressure:
        decision.drop_pending_writes = policy.drop_pending_on_memory_pressure;
        break;
    }
    if (decision.drop_pending_writes) {
        decision.flush_pending_writes = false;
    }
    return decision;
}

AppStorageLifecycleApplyResult apply_app_storage_lifecycle_decision(
    AppRuntimeHost& host,
    AppPrivateKvStorageMock& storage,
    const std::string& app_id,
    std::uint32_t app_instance_id,
    const AppStorageLifecycleDecision& decision,
    std::size_t max_flush_ops) {
    AppStorageLifecycleApplyResult result;
    if (decision.flush_pending_writes) {
        const AppPrivateKvFlushResult flushed = storage.flush_pending(host, max_flush_ops);
        result.flushed_pending_writes = flushed.flushed;
        result.remaining_pending_writes = flushed.remaining_pending;
        result.flush_stopped_before_empty = flushed.stopped_before_empty;
    }
    if (decision.drop_pending_writes && app_instance_id != 0) {
        result.dropped_pending_writes += storage.drop_pending_app_instance(app_instance_id);
    }
    if (decision.drop_pending_writes && !app_id.empty()) {
        result.dropped_pending_writes += storage.drop_pending_app(app_id);
    }
    if (decision.delete_persistent_data && !app_id.empty()) {
        result.deleted_persistent_items = storage.clear_app(app_id);
    }
    if (!decision.flush_pending_writes) {
        result.remaining_pending_writes = storage.pending_count();
    }
    return result;
}

const char* app_storage_lifecycle_trigger_name(AppStorageLifecycleTrigger trigger) {
    switch (trigger) {
    case AppStorageLifecycleTrigger::Suspend:
        return "suspend";
    case AppStorageLifecycleTrigger::Resume:
        return "resume";
    case AppStorageLifecycleTrigger::Exit:
        return "exit";
    case AppStorageLifecycleTrigger::Crash:
        return "crash";
    case AppStorageLifecycleTrigger::Uninstall:
        return "uninstall";
    case AppStorageLifecycleTrigger::UpdateReplace:
        return "update-replace";
    case AppStorageLifecycleTrigger::MemoryPressure:
        return "memory-pressure";
    }
    return "unknown";
}

} // namespace jellyframe
